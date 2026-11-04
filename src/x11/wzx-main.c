/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * wmake-zones: daemon X11 e laço de eventos.
 *
 * Em repouso o processo dorme em poll() sem timeout: só acorda com eventos
 * do servidor X (cliques, janelas que aparecem ou somem, troca de área de
 * trabalho) ou com um comando no socket. Timers existem só durante um
 * arrasto (atraso do overlay), numa prévia e para agrupar o mosaico.
 */
#define _GNU_SOURCE
#include "wzx.h"
#include "wz-i18n.h"
#include "wz-ipc.h"

#include <X11/Xatom.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/shape.h>
#include <errno.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

long
wzx_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

void
wzx_log(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    fprintf(stderr, "wmake-zones: %s\n", buf);
    wz_log_line(buf);
}

void
wzx_save_cfg(wzx *z, const char *section, const char *key, const char *value)
{
    if (wz_ini_set(z->cfg_path, section, key, value) != 0)
        wzx_log("não consegui gravar %s/%s em %s", section, key, z->cfg_path);
}

static int
x_error(Display *d, XErrorEvent *e)
{
    /* Janelas somem a qualquer momento (BadWindow numa corrida com o
     * destroy); isso não pode derrubar o daemon. */
    char msg[128];
    XGetErrorText(d, e->error_code, msg, sizeof msg);
    if (e->error_code != BadWindow && e->error_code != BadDrawable && e->error_code != BadMatch)
        wz_log_line(msg);
    return 0;
}

static int
x_io_error(Display *d)
{
    (void)d;
    /* Servidor X fechou (fim da sessão): sai limpo. */
    _exit(0);
}

/* Lê de novo a configuração e ajusta o que depende dela. */
void
wzx_apply_config(wzx *z)
{
    wz_config c;
    wz_config_defaults(&c);
    wz_config_load(&c, z->cfg_path);
    int had_tile = z->cfg.autotile;
    wz_config_free(&z->cfg);
    z->cfg = c;
    wzx_wm_snapping(z, z->cfg.zones);
    wzx_xi_select(z);
    wzx_keys_grab(z);
    z->work_dirty = 1;
    int any_tile = z->cfg.autotile;
    for (int i = 0; i < z->nmon && !any_tile; i++)
        any_tile = wzx_tile_active_for(z, i);
    if (had_tile && !any_tile)
        wzx_tile_disable_all(z);
    wzx_tile_mark(z);
    if (z->ov.shown && z->ov.mode != 3)
        wzx_overlay_hide(z);
    wzx_dockapp_redraw(z);
    if (!z->cfg.zones && !any_tile && !z->dockapp && !z->cfg.keep_running) {
        wzx_log("Zones e Auto Tiling desligados: o serviço sai");
        z->quit = 1;
    }
}

static void
handle_property(wzx *z, XPropertyEvent *e)
{
    if (e->window == z->root) {
        if (e->atom == z->atom[A_NET_CLIENT_LIST]) {
            wzx_sync_clients(z);
        } else if (e->atom == z->atom[A_NET_CURRENT_DESKTOP]) {
            long old = z->current_desktop;
            Atom rt;
            int fmt;
            unsigned long n, after;
            unsigned char *d = NULL;
            if (XGetWindowProperty(z->dpy, z->root, e->atom, 0, 1, False, AnyPropertyType, &rt, &fmt, &n, &after, &d) ==
                    Success && d && n == 1)
                z->current_desktop = *(long *)d;
            if (d)
                XFree(d);
            if (old != z->current_desktop) {
                wzx_tile_mark(z);
                wzx_dockapp_redraw(z);
            }
        } else if (e->atom == z->atom[A_NET_ACTIVE_WINDOW]) {
            Atom rt;
            int fmt;
            unsigned long n, after;
            unsigned char *d = NULL;
            if (XGetWindowProperty(z->dpy, z->root, e->atom, 0, 1, False, AnyPropertyType, &rt, &fmt, &n, &after, &d) ==
                    Success && d && n == 1)
                z->active = (Window)*(long *)d;
            if (d)
                XFree(d);
        }
        return;
    }
    wzx_win *w = wzx_find(z, e->window);
    if (!w)
        return;
    if (e->atom == z->atom[A_NET_WM_STRUT] || e->atom == z->atom[A_NET_WM_STRUT_PARTIAL]) {
        z->work_dirty = 1;
        wzx_tile_mark(z);
    } else if (e->atom == z->atom[A_NET_WM_STATE] || e->atom == z->atom[A_NET_WM_DESKTOP] ||
               e->atom == XA_WM_NORMAL_HINTS || e->atom == z->atom[A_NET_FRAME_EXTENTS] ||
               e->atom == z->atom[A_NET_WM_WINDOW_TYPE]) {
        unsigned old_state = w->state;
        long old_desk = w->desktop;
        wzx_refresh_win(z, w);
        if (old_state != w->state || old_desk != w->desktop || e->atom == XA_WM_NORMAL_HINTS)
            wzx_tile_mark(z);
    }
}

static void
handle_event(wzx *z, XEvent *e)
{
    if (e->type == GenericEvent && e->xcookie.extension == z->xi_opcode) {
        if (XGetEventData(z->dpy, &e->xcookie)) {
            wzx_drag_raw(z, &e->xcookie);
            XFreeEventData(z->dpy, &e->xcookie);
        }
        return;
    }
    if (z->has_randr && e->type == z->randr_event + RRScreenChangeNotify) {
        XRRUpdateConfiguration(e);
        wzx_overlay_hide(z);
        wzx_update_monitors(z);
        /* Monitores mudaram: os conjuntos do mosaico se refazem. */
        for (int i = 0; i < z->nwin; i++)
            z->win[i]->tile_mon = -1, z->win[i]->expect_valid = 0;
        for (int i = 0; i < 64; i++)
            if (z->tset[i].used)
                wz_bsp_clear(&z->tset[i].bsp);
        wzx_tile_mark(z);
        return;
    }
    if (wzx_dockapp_event(z, e))
        return;
    switch (e->type) {
    case ConfigureNotify:
        if (getenv("WZ_DEBUG"))
            wzx_log("CN ev=0x%lx win=0x%lx %d,%d or=%d", e->xconfigure.event, e->xconfigure.window, e->xconfigure.x,
                    e->xconfigure.y, e->xconfigure.override_redirect);
        if (e->xconfigure.event == z->root) {
            XConfigureEvent *c = &e->xconfigure;
            /* Os quadros do Window Maker também são override-redirect. */
            if (c->window == z->drag_frame)
                wzx_drag_configure(z, c);
            else if (c->override_redirect && !wzx_find_frame(z, c->window))
                wzx_or_event(z, c->window, c->x, c->y, c->width + 2 * c->border_width,
                             c->height + 2 * c->border_width, 1, 0);
        }
        break;
    case MapNotify:
        if (e->xmap.event == z->root && e->xmap.override_redirect && !wzx_find_frame(z, e->xmap.window)) {
            XWindowAttributes wa;
            if (XGetWindowAttributes(z->dpy, e->xmap.window, &wa))
                wzx_or_event(z, e->xmap.window, wa.x, wa.y, wa.width + 2 * wa.border_width,
                             wa.height + 2 * wa.border_width, 1, 0);
        } else {
            wzx_win *w = wzx_find(z, e->xmap.window);
            if (w && !w->mapped) {
                w->mapped = 1;
                wzx_refresh_win(z, w);
                wzx_tile_mark(z);
            }
        }
        break;
    case UnmapNotify:
        if (e->xunmap.event == z->root) {
            wzx_or_event(z, e->xunmap.window, 0, 0, 0, 0, 0, 0);
        } else {
            wzx_win *w = wzx_find(z, e->xunmap.window);
            if (w && w->mapped) {
                w->mapped = 0;
                wzx_tile_mark(z);
            }
        }
        break;
    case DestroyNotify:
        if (e->xdestroywindow.event == z->root)
            wzx_or_event(z, e->xdestroywindow.window, 0, 0, 0, 0, 0, 1);
        break;
    case PropertyNotify:
        handle_property(z, &e->xproperty);
        break;
    case KeyPress:
        if (!wzx_picker_key(z, &e->xkey))
            wzx_keys_event(z, &e->xkey);
        break;
    case ButtonPress:
        wzx_picker_button(z, &e->xbutton);
        break;
    case Expose:
        if (e->xexpose.window == z->ov.win && e->xexpose.count == 0)
            wzx_overlay_expose(z);
        break;
    default:
        break;
    }
}

static void
usage(void)
{
    puts("Uso: wmake-zones [--dockapp | --session | --reset | COMANDO [ARG]]\n"
         "\n"
         "  (sem argumentos)  inicia o serviço (X11)\n"
         "  --dockapp         inicia com o ladrilho no Dock/Clip do Window Maker\n"
         "  --session         usado no login: só fica se Zones ou Auto Tiling\n"
         "                    estiverem ligados\n"
         "  --reset           desliga tudo e devolve o WindowSnapping do wmaker\n"
         "\n"
         "Comandos (para o serviço em execução):\n"
         "  status | zones on|off|toggle | autotile on|off|toggle\n"
         "  layout ID|next|prev | layouts | algorithm NOME | picker | arrange\n"
         "  snap N | move|swap|focus left|right|up|down | restore | restore-all\n"
         "  float | promote | grow | shrink | undo | preview ÁRVORE | editor\n"
         "  prefs | reload | log | quit");
}

/* Sobe o serviço em segundo plano (para comandos com o daemon parado). */
static int
start_daemon(char **argv)
{
    (void)argv;
    pid_t p = fork();
    if (p < 0)
        return -1;
    if (p == 0) {
        setsid();
        if (fork() != 0)
            _exit(0);
        execlp("wmake-zones", "wmake-zones", (char *)NULL);
        _exit(127);
    }
    waitpid(p, NULL, 0);
    char *path = wz_ipc_path("x11");
    char out[64];
    for (int i = 0; i < 40; i++) {
        if (wz_ipc_request(path, "ping", out, sizeof out) == 0) {
            free(path);
            return 0;
        }
        struct timespec ts = { 0, 50 * 1000000L };
        nanosleep(&ts, NULL);
    }
    free(path);
    return -1;
}

/* Cliente: manda o comando ao serviço da sessão. */
static int
client(int argc, char **argv)
{
    char cmd[WZ_IPC_MAX] = "";
    for (int i = 1; i < argc; i++) {
        if (i > 1)
            strncat(cmd, " ", sizeof cmd - strlen(cmd) - 1);
        strncat(cmd, argv[i], sizeof cmd - strlen(cmd) - 1);
    }
    char *path = wz_ipc_session_path();
    static char out[65536];
    if (wz_ipc_request(path, cmd, out, sizeof out) != 0) {
        /* Ligar algo com o serviço parado: grava e sobe o serviço (X11). */
        int is_on = argc >= 3 && (!strcmp(argv[1], "zones") || !strcmp(argv[1], "autotile")) &&
                    (!strcmp(argv[2], "on") || !strcmp(argv[2], "toggle"));
        int is_layout = argc >= 3 && !strcmp(argv[1], "layout") && strcmp(argv[2], "next") && strcmp(argv[2], "prev");
        char *cfgp = wz_config_path();
        if (is_layout) {
            wz_ini_set(cfgp, "General", "Layout", argv[2]);
            free(cfgp);
            free(path);
            return 0;
        }
        if (is_on && getenv("DISPLAY") && (!getenv("XDG_SESSION_TYPE") || strcmp(getenv("XDG_SESSION_TYPE"), "wayland"))) {
            wz_ini_set(cfgp, "General", !strcmp(argv[1], "zones") ? "Zones" : "Autotile", "true");
            free(cfgp);
            free(path);
            if (start_daemon(argv) == 0) {
                puts(!strcmp(argv[1], "zones") ? "zones=on" : "autotile=on");
                return 0;
            }
            fprintf(stderr, "wmake-zones: não consegui iniciar o serviço\n");
            return 1;
        }
        free(cfgp);
        if (!strcmp(argv[1], "status")) {
            puts("running=no");
            free(path);
            return 0;
        }
        fprintf(stderr, "wmake-zones: o serviço não está em execução (%s)\n", path ? path : "sem display");
        free(path);
        return 1;
    }
    free(path);
    fputs(out, stdout);
    return strncmp(out, "error=", 6) == 0 ? 1 : 0;
}

static int
reset(void)
{
    char *cfgp = wz_config_path();
    char out[256];
    char *path = wz_ipc_session_path();
    wz_ipc_request(path, "zones off", out, sizeof out);
    wz_ipc_request(path, "autotile off", out, sizeof out);
    free(path);
    wz_ini_set(cfgp, "General", "Zones", "false");
    wz_ini_set(cfgp, "General", "Autotile", "false");
    if (wzx_snapping_marked(cfgp)) {
        if (system("wdwrite WindowMaker WindowSnapping YES >/dev/null 2>&1") == 0)
            wz_ini_set(cfgp, "State", "RestoreWindowSnapping", NULL);
    }
    free(cfgp);
    puts("wmake-zones: Zones e Auto Tiling desligados; o Window Maker voltou ao normal.");
    return 0;
}

int
main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
#ifdef WZ_ENABLE_NLS
    bindtextdomain("wmake-zones", WZ_LOCALEDIR);
    textdomain("wmake-zones");
#endif
    int dockapp = 0, session = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dockapp") || !strcmp(argv[i], "-d"))
            dockapp = 1;
        else if (!strcmp(argv[i], "--session"))
            session = 1;
        else if (!strcmp(argv[i], "--reset"))
            return reset();
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage();
            return 0;
        } else if (!strcmp(argv[i], "--version")) {
            puts("wmake-zones " WZ_VERSION);
            return 0;
        } else if (argv[i][0] != '-')
            return client(argc - i + 1, argv + i - 1);
    }

    const char *st = getenv("XDG_SESSION_TYPE");
    if (st && !strcmp(st, "wayland") && getenv("WAYLAND_DISPLAY")) {
#ifdef WZ_HAVE_WAYLAND_DOCKAPP
        if (dockapp)
            execlp("wmake-zones-wl", "wmake-zones-wl", (char *)NULL);
#endif
        if (session)
            return 0; /* no Wayland quem faz o trabalho é o wlmaker */
        fprintf(stderr, "wmake-zones: na sessão Wayland as zonas são do wlmaker; use os comandos.\n");
        return 1;
    }

    wzx *z = calloc(1, sizeof *z);
    z->listen_fd = -1;
    z->cfg_path = wz_config_path();
    wz_config_defaults(&z->cfg);
    wz_config_load(&z->cfg, z->cfg_path);
    if (session && !z->cfg.zones && !z->cfg.autotile && !z->cfg.keep_running) {
        /* Desligado: nenhum processo na sessão. Restaura o WindowSnapping se
         * uma sessão anterior caiu com as zonas ligadas. */
        if (wzx_snapping_marked(z->cfg_path) && system("wdwrite WindowMaker WindowSnapping YES >/dev/null 2>&1") == 0)
            wz_ini_set(z->cfg_path, "State", "RestoreWindowSnapping", NULL);
        return 0;
    }

    z->dpy = XOpenDisplay(NULL);
    if (!z->dpy) {
        fprintf(stderr, "wmake-zones: sem servidor X (DISPLAY)\n");
        return 1;
    }
    XSetErrorHandler(x_error);
    XSetIOErrorHandler(x_io_error);
    z->screen = DefaultScreen(z->dpy);
    z->root = RootWindow(z->dpy, z->screen);
    z->fd = ConnectionNumber(z->dpy);

    /* Uma instância por display. "--dockapp" assume o lugar de uma instância
     * sem ladrilho (o Dock precisa ser dono da janela). */
    int lr = wzx_ipc_listen(z);
    if (lr == -1) {
        fprintf(stderr, "wmake-zones: não consegui criar o socket de controle em $XDG_RUNTIME_DIR/wmake-zones\n");
        return 1;
    }
    if (lr != 0) {
        char *path = wz_ipc_path("x11");
        char out[64];
        if (dockapp && wz_ipc_request(path, "quit", out, sizeof out) == 0) {
            for (int i = 0; i < 40 && wzx_ipc_listen(z) != 0; i++) {
                struct timespec ts = { 0, 50 * 1000000L };
                nanosleep(&ts, NULL);
            }
        }
        free(path);
        if (z->listen_fd < 0) {
            fprintf(stderr, "wmake-zones: já está em execução neste display\n");
            return 0;
        }
    }

    int ev, err, maj = 2, min = 1;
    z->has_xi = XQueryExtension(z->dpy, "XInputExtension", &z->xi_opcode, &ev, &err) &&
                XIQueryVersion(z->dpy, &maj, &min) == Success && (maj > 2 || (maj == 2 && min >= 1));
    if (!z->has_xi)
        wzx_log("XInput 2.1 ausente: o arrasto não será detectado (só atalhos e comandos)");
    z->has_randr = XRRQueryExtension(z->dpy, &z->randr_event, &err);
    z->has_shape = XShapeQueryExtension(z->dpy, &ev, &err);

    wzx_wm_init(z);
    wzx_update_monitors(z);
    wzx_drag_init(z);
    XSelectInput(z->dpy, z->root, SubstructureNotifyMask | PropertyChangeMask);
    if (z->has_randr)
        XRRSelectInput(z->dpy, z->root, RRScreenChangeNotifyMask);
    wzx_sync_clients(z);
    if (dockapp)
        wzx_dockapp_create(z);
    wzx_apply_config(z);
    z->quit = 0; /* apply_config pode pedir saída; no início vale ficar */
    if (!z->cfg.zones && !z->cfg.autotile && !z->dockapp && !z->cfg.keep_running && session)
        z->quit = 1;
    wzx_log("iniciado (%d monitor(es), XInput2 %s, compositor %s)", z->nmon, z->has_xi ? "sim" : "não",
            wzx_compositing(z) ? "sim" : "não");

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGHUP);
    sigprocmask(SIG_BLOCK, &mask, NULL);
    int sfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);

    while (!z->quit) {
        while (XPending(z->dpy)) {
            XEvent e;
            XNextEvent(z->dpy, &e);
            handle_event(z, &e);
        }
        wzx_drag_flush(z);
        long now = wzx_now_ms();
        if (z->tile_dirty && now >= z->tile_at)
            wzx_tile_run(z);
        if (z->ov_hide_at && now >= z->ov_hide_at && z->ov.mode == 3)
            wzx_overlay_hide(z);
        XFlush(z->dpy);
        if (XPending(z->dpy))
            continue;

        long due = -1;
        if (z->drag == DRAG_MOVING && z->drag_dirty && z->drag_show_at > now)
            due = z->drag_show_at;
        if (z->tile_dirty && (due < 0 || z->tile_at < due))
            due = z->tile_at;
        if (z->ov_hide_at && (due < 0 || z->ov_hide_at < due))
            due = z->ov_hide_at;
        int timeout = due < 0 ? -1 : (int)(due > now ? due - now : 0);

        struct pollfd p[3] = { { z->fd, POLLIN, 0 }, { z->listen_fd, POLLIN, 0 }, { sfd, POLLIN, 0 } };
        int r = poll(p, 3, timeout);
        if (r < 0 && errno != EINTR)
            break;
        if (p[1].revents & POLLIN)
            wzx_ipc_accept(z);
        if (p[2].revents & POLLIN) {
            struct signalfd_siginfo si;
            if (read(sfd, &si, sizeof si) == sizeof si) {
                if (si.ssi_signo == SIGHUP)
                    wzx_apply_config(z);
                else
                    z->quit = 1;
            }
        }
        if (r == 0)
            wzx_drag_timer(z);
    }

    /* Saída limpa: nada de overlay preso, grabs soltos, wmaker como antes. */
    wzx_picker_close(z);
    wzx_overlay_hide(z);
    wzx_keys_ungrab(z);
    if (!z->cfg.zones)
        wzx_wm_snapping(z, 0);
    wzx_ipc_close(z);
    XCloseDisplay(z->dpy);
    wzx_log("encerrado");
    return 0;
}
