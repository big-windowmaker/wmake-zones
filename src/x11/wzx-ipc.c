/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Comandos (IPC, atalhos, dockapp) e o socket de controle.
 *
 * Socket em $XDG_RUNTIME_DIR/wmake-zones/x11-<display>.sock, diretório
 * 0700; cada conexão confere o uid do outro lado (SO_PEERCRED). Protocolo:
 * uma linha de comando, uma resposta terminada por linha vazia. Só comandos
 * desta tabela; nada do cliente vira execução de programa.
 */
#define _GNU_SOURCE
#include "wzx.h"
#include "wz-ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static void
outf(char *out, size_t len, const char *fmt, ...)
{
    if (!out || !len)
        return;
    size_t used = strlen(out);
    if (used >= len - 1)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(out + used, len - used, fmt, ap);
    va_end(ap);
}

/* Executa um programa fixo, sem shell, desligado do daemon. */
static void
spawn(const char *prog, const char *arg)
{
    pid_t p = fork();
    if (p < 0)
        return;
    if (p == 0) {
        if (fork() != 0)
            _exit(0);
        setsid();
        int fd = open("/dev/null", O_RDWR);
        if (fd >= 0) {
            dup2(fd, 0);
            if (fd > 2)
                close(fd);
        }
        execlp(prog, prog, arg, (char *)NULL);
        _exit(127);
    }
    waitpid(p, NULL, 0);
}

static wzx_win *
active_win(wzx *z)
{
    wzx_win *w = wzx_find(z, z->active);
    if (w)
        wzx_refresh_geom(z, w);
    return w;
}

static int
parse_dir(const char *s)
{
    if (!s)
        return -1;
    if (!strcmp(s, "left"))
        return WZ_LEFT;
    if (!strcmp(s, "right"))
        return WZ_RIGHT;
    if (!strcmp(s, "up"))
        return WZ_UP;
    if (!strcmp(s, "down"))
        return WZ_DOWN;
    return -1;
}

static int
onoff(const char *arg, int cur)
{
    if (!arg || !strcmp(arg, "toggle"))
        return !cur;
    if (!strcmp(arg, "on") || !strcmp(arg, "true") || !strcmp(arg, "1"))
        return 1;
    if (!strcmp(arg, "off") || !strcmp(arg, "false") || !strcmp(arg, "0"))
        return 0;
    return cur;
}

/* Monitor do ponteiro (para escolher layout por monitor). */
static int
pointer_monitor(wzx *z)
{
    Window r, c;
    int x = 0, y = 0, wx, wy;
    unsigned m;
    XQueryPointer(z->dpy, z->root, &r, &c, &x, &y, &wx, &wy, &m);
    return wzx_monitor_at(z, x, y);
}

static void
set_layout(wzx *z, const char *id)
{
    if (!wz_config_layout(&z->cfg, id))
        return;
    /* Monitor com layout próprio: troca o dele; senão, o geral. */
    int mon = pointer_monitor(z);
    for (int i = 0; i < z->cfg.nscopes; i++) {
        wz_scope *s = &z->cfg.scopes[i];
        if (s->monitor && !strcmp(s->monitor, z->mon[mon].name) && s->workspace == 0 && s->layout) {
            free(s->layout);
            s->layout = strdup(id);
            char sec[96];
            snprintf(sec, sizeof sec, "Monitor %s", s->monitor);
            wzx_save_cfg(z, sec, "Layout", id);
            wzx_dockapp_redraw(z);
            return;
        }
    }
    free(z->cfg.layout);
    z->cfg.layout = strdup(id);
    wzx_save_cfg(z, "General", "Layout", id);
    wzx_dockapp_redraw(z);
}

static void
cycle_layout(wzx *z, int step)
{
    int mon = pointer_monitor(z);
    const wz_layout_def *cur = wz_config_layout_for(&z->cfg, z->mon[mon].name, (int)z->current_desktop + 1);
    int idx = 0;
    for (int i = 0; i < z->cfg.nlayouts; i++)
        if (&z->cfg.layouts[i] == cur)
            idx = i;
    idx = (idx + step + z->cfg.nlayouts) % z->cfg.nlayouts;
    set_layout(z, z->cfg.layouts[idx].id);
}

/* Encaixa a janela na zona n do monitor dela. */
static void
snap_to(wzx *z, wzx_win *w, int mon, int n)
{
    wz_rect zones[WZX_MAX_ZONES];
    int k = wzx_zones_for(z, mon, zones, NULL, WZX_MAX_ZONES);
    if (n < 0 || n >= k)
        return;
    wz_rect pre;
    if (!wzx_get_restore(z, w, &pre))
        wzx_set_restore(z, w, w->outer);
    wzx_snap(z, w, zones[n], n, 1);
}

static void
move_dir(wzx *z, wzx_win *w, int dir)
{
    if (!w || !wzx_snappable(z, w))
        return;
    if (w->tiled) {
        wzx_tile_swap_dir(z, w, dir);
        return;
    }
    int mon = wzx_monitor_of(z, w->outer);
    wz_rect zones[WZX_MAX_ZONES];
    int n = wzx_zones_for(z, mon, zones, NULL, WZX_MAX_ZONES);
    if (n <= 0)
        return;
    long cur = wzx_get_zone_prop(z, w);
    int from = cur >= 0 && cur < n && wz_rect_overlap_area(zones[cur], w->outer) > 0 ? (int)cur : -1;
    if (from < 0) {
        /* Ainda solta: a zona onde está o centro dela. */
        snap_to(z, w, mon, wz_zone_at(zones, n, w->outer.x + w->outer.w / 2, w->outer.y + w->outer.h / 2));
        return;
    }
    int nb = wz_neighbor(zones, n, from, dir);
    if (nb >= 0) {
        snap_to(z, w, mon, nb);
        return;
    }
    /* Sem vizinha: atravessa para o monitor ao lado, na zona mais perto. */
    wz_rect f = zones[from];
    int best = -1;
    long bd = 0;
    for (int m = 0; m < z->nmon; m++) {
        if (m == mon)
            continue;
        wz_rect g = z->mon[m].geom, cg = z->mon[mon].geom;
        int ok = (dir == WZ_LEFT && g.x + g.w <= cg.x) || (dir == WZ_RIGHT && g.x >= cg.x + cg.w) ||
                 (dir == WZ_UP && g.y + g.h <= cg.y) || (dir == WZ_DOWN && g.y >= cg.y + cg.h);
        if (!ok)
            continue;
        long d = labs((long)(g.x + g.w / 2) - (cg.x + cg.w / 2)) + labs((long)(g.y + g.h / 2) - (cg.y + cg.h / 2));
        if (best < 0 || d < bd) {
            best = m;
            bd = d;
        }
    }
    if (best < 0)
        return;
    wz_rect oz[WZX_MAX_ZONES];
    int on = wzx_zones_for(z, best, oz, NULL, WZX_MAX_ZONES);
    if (on <= 0)
        return;
    /* Entra pela borda oposta: a zona mais próxima do ponto de saída. */
    wz_rect g = z->mon[best].geom;
    int px = dir == WZ_LEFT ? g.x + g.w - 1 : dir == WZ_RIGHT ? g.x : f.x + f.w / 2 - z->mon[mon].geom.x + g.x;
    int py = dir == WZ_UP ? g.y + g.h - 1 : dir == WZ_DOWN ? g.y : f.y + f.h / 2 - z->mon[mon].geom.y + g.y;
    snap_to(z, w, best, wz_zone_at(oz, on, px, py));
}

static void
focus_dir(wzx *z, wzx_win *w, int dir)
{
    if (!w)
        return;
    wz_rect r[WZX_MAX_WIN];
    wzx_win *ws[WZX_MAX_WIN];
    int n = 0, from = -1;
    for (int i = 0; i < z->nwin && n < WZX_MAX_WIN; i++) {
        wzx_win *c = z->win[i];
        if (!c->mapped || c->special || (c->state & (WS_HIDDEN | WS_SHADED)) || c->desktop != z->current_desktop)
            continue;
        if (c == w)
            from = n;
        ws[n] = c;
        r[n++] = c->outer;
    }
    int nb = from >= 0 ? wz_neighbor(r, n, from, dir) : -1;
    if (nb >= 0)
        wzx_activate(z, ws[nb]);
}

/* Organiza agora: cada zona recebe a janela que mais a cobre. */
static void
arrange(wzx *z)
{
    for (int mon = 0; mon < z->nmon; mon++) {
        wz_rect zones[WZX_MAX_ZONES];
        int n = wzx_zones_for(z, mon, zones, NULL, WZX_MAX_ZONES);
        wzx_win *cand[WZX_MAX_WIN];
        int nc = 0;
        for (int i = 0; i < z->nwin; i++) {
            wzx_win *w = z->win[i];
            if (w->mapped && !w->tiled && wzx_snappable(z, w) && !w->transient && w->desktop == z->current_desktop &&
                wzx_monitor_of(z, w->outer) == mon)
                cand[nc++] = w;
        }
        wzx_history_push(z, cand, nc);
        for (int k = 0; k < n && nc > 0; k++) {
            int best = 0;
            long ba = -1;
            for (int i = 0; i < nc; i++) {
                long a = wz_rect_overlap_area(cand[i]->outer, zones[k]);
                if (a > ba) {
                    ba = a;
                    best = i;
                }
            }
            wzx_win *w = cand[best];
            wz_rect pre;
            if (!wzx_get_restore(z, w, &pre))
                wzx_set_restore(z, w, w->outer);
            wzx_snap(z, w, zones[k], k, 0);
            cand[best] = cand[--nc];
        }
    }
}

static void
restore_win(wzx *z, wzx_win *w)
{
    if (!w)
        return;
    if (w->tiled) {
        wzx_tile_toggle_float(z, w);
        return;
    }
    wz_rect r;
    if (wzx_get_restore(z, w, &r)) {
        wzx_history_push(z, &w, 1);
        wzx_move_outer(z, w, r);
        wzx_clear_restore(z, w);
    }
}

static void
preview(wzx *z, const char *tree)
{
    if (z->drag != DRAG_IDLE || z->picker)
        return;
    wz_node *t = wz_parse(tree, NULL);
    if (!t)
        return;
    int mon = pointer_monitor(z);
    wzx_update_workarea(z);
    wz_rect zones[WZX_MAX_ZONES];
    int n = wz_compute(t, z->mon[mon].work, z->cfg.gap_in, z->cfg.gap_out, zones, WZX_MAX_ZONES);
    if (getenv("WZ_DEBUG"))
        wzx_log("prévia '%s': %d zonas", tree, n);
    char *names[WZX_MAX_ZONES] = { 0 };
    for (int i = 0; i < n && i < WZX_MAX_ZONES; i++) {
        wz_node *l = wz_leaf_nth(t, i);
        names[i] = l && l->name ? strdup(l->name) : NULL;
    }
    wz_free(t);
    wzx_overlay_hide(z);
    wzx_overlay_set_zones(z, zones, names, n > WZX_MAX_ZONES ? WZX_MAX_ZONES : n);
    for (int i = 0; i < n && i < WZX_MAX_ZONES; i++)
        free(names[i]);
    wzx_overlay_show(z, mon, 3);
    z->ov_hide_at = wzx_now_ms() + 1800;
}

static void
status(wzx *z, char *out, size_t len)
{
    outf(out, len, "backend=x11\nversion=%s\n", WZ_VERSION);
    outf(out, len, "zones=%s\nautotile=%s\n", z->cfg.zones ? "on" : "off", z->cfg.autotile ? "on" : "off");
    outf(out, len, "layout=%s\nalgorithm=%s\n", z->cfg.layout, wz_algo_name(z->cfg.tile.algo));
    outf(out, len, "desktop=%ld\ncompositor=%s\n", z->current_desktop + 1, wzx_compositing(z) ? "yes" : "no");
    outf(out, len, "xinput2=%s\nrandr=%s\nshape=%s\n", z->has_xi ? "yes" : "no", z->has_randr ? "yes" : "no",
         z->has_shape ? "yes" : "no");
    wzx_update_workarea(z);
    for (int i = 0; i < z->nmon; i++) {
        const wz_layout_def *l = wz_config_layout_for(&z->cfg, z->mon[i].name, (int)z->current_desktop + 1);
        outf(out, len, "monitor=%s %dx%d+%d+%d work=%dx%d+%d+%d layout=%s autotile=%s\n", z->mon[i].name,
             z->mon[i].geom.w, z->mon[i].geom.h, z->mon[i].geom.x, z->mon[i].geom.y, z->mon[i].work.w,
             z->mon[i].work.h, z->mon[i].work.x, z->mon[i].work.y, l ? l->id : "?",
             wzx_tile_active_for(z, i) ? "on" : "off");
    }
    int tiled = 0, snapped = 0;
    for (int i = 0; i < z->nwin; i++) {
        tiled += z->win[i]->tiled;
        snapped += !z->win[i]->tiled && wzx_get_zone_prop(z, z->win[i]) >= 0;
    }
    outf(out, len, "windows=%d tiled=%d snapped=%d\n", z->nwin, tiled, snapped);
    for (int i = 0; i < WZ_KEY_N; i++)
        if (z->keys[i].conflict)
            outf(out, len, "conflict=%s %s\n", wz_key_name(i), z->cfg.keys[i]);
}

void
wzx_command(wzx *z, const char *line, char *out, size_t outlen)
{
    char buf[WZ_IPC_MAX];
    snprintf(buf, sizeof buf, "%s", line);
    char *nl = strpbrk(buf, "\r\n");
    if (nl)
        *nl = 0;
    char *arg = strchr(buf, ' ');
    if (arg)
        *arg++ = 0;
    const char *cmd = buf;
    if (out && outlen)
        out[0] = 0;

    if (!strcmp(cmd, "status")) {
        status(z, out, outlen);
    } else if (!strcmp(cmd, "zones")) {
        int v = onoff(arg, z->cfg.zones);
        z->cfg.zones = v;
        wzx_save_cfg(z, "General", "Zones", v ? "true" : "false");
        wzx_apply_config(z);
        outf(out, outlen, "zones=%s\n", v ? "on" : "off");
    } else if (!strcmp(cmd, "autotile")) {
        int v = onoff(arg, z->cfg.autotile);
        if (!v)
            wzx_history_push(z, z->win, z->nwin < 64 ? z->nwin : 64);
        z->cfg.autotile = v;
        wzx_save_cfg(z, "General", "Autotile", v ? "true" : "false");
        wzx_apply_config(z);
        outf(out, outlen, "autotile=%s\n", v ? "on" : "off");
    } else if (!strcmp(cmd, "algorithm") && arg && wz_algo_from_name(arg) >= 0) {
        z->cfg.tile.algo = wz_algo_from_name(arg);
        wzx_save_cfg(z, "Tiling", "Algorithm", arg);
        wzx_tile_mark(z);
    } else if (!strcmp(cmd, "layout")) {
        if (arg && !strcmp(arg, "next"))
            cycle_layout(z, 1);
        else if (arg && !strcmp(arg, "prev"))
            cycle_layout(z, -1);
        else if (arg && wz_config_layout(&z->cfg, arg))
            set_layout(z, arg);
        else
            outf(out, outlen, "error=layout desconhecido\n");
        if (z->ov.shown && z->ov.mode == 0)
            wzx_overlay_hide(z);
    } else if (!strcmp(cmd, "layouts")) {
        for (int i = 0; i < z->cfg.nlayouts; i++)
            outf(out, outlen, "%s\t%s\t%s\n", z->cfg.layouts[i].id,
                 z->cfg.layouts[i].label ? z->cfg.layouts[i].label : wz_builtin_label(z->cfg.layouts[i].id),
                 z->cfg.layouts[i].tree);
    } else if (!strcmp(cmd, "picker")) {
        wzx_picker_open(z);
    } else if (!strcmp(cmd, "editor")) {
        spawn("wmake-zones-prefs", "--editor");
    } else if (!strcmp(cmd, "prefs")) {
        spawn("wmake-zones-prefs", "--general");
    } else if (!strcmp(cmd, "arrange")) {
        arrange(z);
    } else if (!strcmp(cmd, "restore")) {
        restore_win(z, active_win(z));
    } else if (!strcmp(cmd, "restore-all")) {
        wzx_history_push(z, z->win, z->nwin < 64 ? z->nwin : 64);
        for (int i = 0; i < z->nwin; i++)
            if (!z->win[i]->tiled)
                restore_win(z, z->win[i]);
    } else if (!strcmp(cmd, "float")) {
        wzx_tile_toggle_float(z, active_win(z));
    } else if (!strcmp(cmd, "promote")) {
        wzx_tile_promote(z, active_win(z));
    } else if (!strcmp(cmd, "grow") || !strcmp(cmd, "shrink")) {
        wzx_tile_ratio(z, active_win(z), !strcmp(cmd, "grow") ? 0.05 : -0.05);
    } else if (!strcmp(cmd, "move") && parse_dir(arg) >= 0) {
        move_dir(z, active_win(z), parse_dir(arg));
    } else if (!strcmp(cmd, "swap") && parse_dir(arg) >= 0) {
        wzx_win *w = active_win(z);
        if (w && w->tiled)
            wzx_tile_swap_dir(z, w, parse_dir(arg));
        else
            move_dir(z, w, parse_dir(arg));
    } else if (!strcmp(cmd, "focus") && parse_dir(arg) >= 0) {
        focus_dir(z, active_win(z), parse_dir(arg));
    } else if (!strcmp(cmd, "snap") && arg) {
        wzx_win *w = active_win(z);
        if (w && wzx_snappable(z, w))
            snap_to(z, w, wzx_monitor_of(z, w->outer), atoi(arg) - 1);
    } else if (!strcmp(cmd, "undo")) {
        wzx_history_undo(z);
    } else if (!strcmp(cmd, "preview") && arg) {
        preview(z, arg);
    } else if (!strcmp(cmd, "reload")) {
        wzx_apply_config(z);
    } else if (!strcmp(cmd, "log")) {
        outf(out, outlen, "%s", wz_log_text());
    } else if (!strcmp(cmd, "quit")) {
        z->quit = 1;
    } else if (!strcmp(cmd, "ping")) {
        outf(out, outlen, "pong\n");
    } else {
        outf(out, outlen, "error=comando desconhecido: %s\n", cmd);
    }
    XFlush(z->dpy);
}

/* ---- socket ------------------------------------------------------------- */

int
wzx_ipc_listen(wzx *z)
{
    char *path = wz_ipc_path("x11");
    if (!path)
        return -1;
    int fd = wz_ipc_server(path);
    if (fd < 0) {
        free(path);
        return fd; /* -2: outra instância responde */
    }
    z->sock_path = path;
    z->listen_fd = fd;
    return 0;
}

void
wzx_ipc_accept(wzx *z)
{
    int c = accept4(z->listen_fd, NULL, NULL, SOCK_CLOEXEC);
    if (c < 0)
        return;
    if (!wz_ipc_peer_ok(c)) {
        close(c);
        return;
    }
    /* Pedido curto; o cliente manda tudo de uma vez. */
    struct timeval tv = { 0, 300000 };
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    char line[WZ_IPC_MAX];
    ssize_t n = recv(c, line, sizeof line - 1, 0);
    if (n > 0) {
        line[n] = 0;
        static char out[16384];
        wzx_command(z, line, out, sizeof out);
        size_t l = strlen(out);
        if (l == 0 || out[l - 1] != '\n')
            strncat(out, "ok\n", sizeof out - l - 1);
        strncat(out, "\n", sizeof out - strlen(out) - 1);
        (void)!send(c, out, strlen(out), MSG_NOSIGNAL);
    }
    close(c);
}

void
wzx_ipc_close(wzx *z)
{
    if (z->listen_fd >= 0)
        close(z->listen_fd);
    if (z->sock_path) {
        unlink(z->sock_path);
        free(z->sock_path);
    }
    z->listen_fd = -1;
    z->sock_path = NULL;
}
