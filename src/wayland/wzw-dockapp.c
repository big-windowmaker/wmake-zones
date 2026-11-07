/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * wmake-zones-wl: o ladrilho do wmake-zones no Clip/Dock do wlmaker, como
 * cliente Wayland do protocolo de ícones do wlmaker (sem XWayland).
 *
 *   clique esquerdo   Zonas <-> Mosaico           ([Dockapp] LeftClick)
 *   clique do meio    desliga / religa o último   ([Dockapp] MiddleClick)
 *   roda              layout anterior / seguinte, com uma prévia breve das
 *                     zonas no monitor                ([Dockapp] Wheel)
 *   clique direito    menu do wlmaker (ações do Dockapps.plist)
 *
 * O desenho é o mesmo do dockapp do X11: a miniatura do layout do monitor
 * e as teclas Z (zonas) e M (mosaico) com LED. O estado vem do arquivo de
 * configuração, vigiado por inotify (o wlmaker grava nele a cada troca), e
 * o layout em efeito, do status do wlmaker (o automático depende do
 * monitor): parado, o processo dorme em poll() sem timeout.
 */
#define _GNU_SOURCE
#include "wz-config.h"
#include "wz-i18n.h"
#include "wz-ipc.h"
#include "wz-layout.h"
#include "wz-theme.h"
#include "wz-tile.h"

#include <cairo.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wayland-client.h>

#include "wlmaker-icon-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

typedef struct {
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct zwlmaker_icon_manager_v1 *icon_manager;
    struct wl_surface *surface;
    struct zwlmaker_toplevel_icon_v1 *icon;
    int width, height;
    int configured;
    int pointer_inside;
    double px, py;      /* ponteiro no ladrilho, em coordenadas de 64x64 */
    wz_config cfg;
    char *cfg_path;
    wz_theme theme;
    char layout[128];   /* layout em efeito no primeiro monitor (status) */
    int quit;
} app;

static void
rgb(cairo_t *cr, unsigned c)
{
    cairo_set_source_rgb(cr, (c >> 16 & 0xff) / 255.0, (c >> 8 & 0xff) / 255.0, (c & 0xff) / 255.0);
}

static void
bevel(cairo_t *cr, double x, double y, double w, double h, int raised)
{
    rgb(cr, raised ? 0xffffff : 0x000000);
    cairo_rectangle(cr, x, y, w, 1);
    cairo_rectangle(cr, x, y, 1, h);
    cairo_fill(cr);
    rgb(cr, raised ? 0x000000 : 0xffffff);
    cairo_rectangle(cr, x, y + h - 1, w, 1);
    cairo_rectangle(cr, x + w - 1, y, 1, h);
    cairo_fill(cr);
}

/* Fonte de pixels 3x5 do LED, a mesma do dockapp do X11. */
static const struct {
    char c;
    unsigned char rows[5];
} glyphs[] = {
    { 'M', { 5, 7, 7, 5, 5 } }, { 'Z', { 7, 1, 2, 4, 7 } },
};

static void
pixtext(cairo_t *cr, const char *s, double x, double y, unsigned c, double sc)
{
    rgb(cr, c);
    for (; *s; s++, x += 4 * sc)
        for (size_t g = 0; g < sizeof glyphs / sizeof *glyphs; g++)
            if (glyphs[g].c == *s)
                for (int r = 0; r < 5; r++)
                    for (int k = 0; k < 3; k++)
                        if (glyphs[g].rows[r] & (4 >> k))
                            cairo_rectangle(cr, x + k * sc, y + r * sc, sc, sc);
    cairo_fill(cr);
}

/* Fileira de baixo, como no dockapp do X11: seta, Z, M, seta. */
#define ROW_Y 45
#define ROW_H 14
enum { HIT_NONE, HIT_PREV, HIT_Z, HIT_M, HIT_NEXT };
static const struct {
    int x, w;
} row_btn[] = { [HIT_PREV] = { 4, 9 }, [HIT_Z] = { 14, 17 }, [HIT_M] = { 33, 17 }, [HIT_NEXT] = { 51, 9 } };

static int
row_hit(double x, double y)
{
    if (y < ROW_Y || y >= ROW_Y + ROW_H)
        return HIT_NONE;
    for (int i = HIT_PREV; i <= HIT_NEXT; i++)
        if (x >= row_btn[i].x && x < row_btn[i].x + row_btn[i].w)
            return i;
    return HIT_NONE;
}

static void
arrow_key(cairo_t *cr, int which, int enabled)
{
    double x = row_btn[which].x, w = row_btn[which].w, y = ROW_Y, h = ROW_H;
    rgb(cr, 0xb4b6bb);
    cairo_rectangle(cr, x + 1, y + 1, w - 2, h - 2);
    cairo_fill(cr);
    bevel(cr, x, y, w, h, 1);
    rgb(cr, enabled ? 0x101010 : 0x8a8c90);
    double cx = x + w / 2, cy = y + h / 2, d = which == HIT_PREV ? -1 : 1;
    cairo_move_to(cr, cx + 2 * d, cy);
    cairo_line_to(cr, cx - 2 * d, cy - 4);
    cairo_line_to(cr, cx - 2 * d, cy + 4);
    cairo_close_path(cr);
    cairo_fill(cr);
}

/* Mosaico: quatro janelas com barra de título na disposição do algoritmo,
 * calculada numa área 16:9 (o núcleo não divide abaixo de 64 px) e reduzida
 * ao ladrilho; a principal com a barra âmbar, a cor do LED M. */
static void
draw_windows(app *a, cairo_t *cr, wz_rect box)
{
    wz_tile_params p = a->cfg.tile;
    if (p.algo == WZ_TILE_BSP)
        p.algo = WZ_TILE_DWINDLE; /* a árvore é do compositor; ela começa assim */
    wz_rect area = { 0, 0, 1600, 900 };
    p.gap_in = 2 * area.w / box.w;
    p.gap_out = 0;
    p.smart_gaps = 0;
    wz_rect r[8];
    int n = wz_tile(&p, area, 4, r, 8);
    for (int i = n - 1; i >= 0; i--) {
        double x = box.x + (double)r[i].x * box.w / area.w, y = box.y + (double)r[i].y * box.h / area.h;
        double w = (double)r[i].w * box.w / area.w, h = (double)r[i].h * box.h / area.h;
        rgb(cr, 0xc2c0c5);
        cairo_rectangle(cr, x, y, w, h);
        cairo_fill(cr);
        rgb(cr, i == 0 ? 0xe0a030 : 0x30363c);
        cairo_rectangle(cr, x, y, w, 3);
        cairo_fill(cr);
        rgb(cr, 0x303030);
        cairo_set_line_width(cr, 1);
        cairo_rectangle(cr, x + 0.5, y + 0.5, w - 1, h - 1);
        cairo_stroke(cr);
    }
}

/* Tecla com LED: afundada e escura apagada, acesa com a letra escura. */
static void
led_key(cairo_t *cr, double x, double y, const char *letter, int lit, unsigned color)
{
    const double w = 17, h = 14;
    rgb(cr, lit ? color : 0x2a2e33);
    cairo_rectangle(cr, x + 1, y + 1, w - 2, h - 2);
    cairo_fill(cr);
    bevel(cr, x, y, w, h, 0);
    if (lit) {
        rgb(cr, wz_rgb_mix(color, 0xffffff, 110));
        cairo_rectangle(cr, x + 2, y + 2, w - 4, 1);
        cairo_fill(cr);
    }
    pixtext(cr, letter, x + (w - 6) / 2 + 1, y + 2, lit ? 0x101010 : 0x7a7e84, 2);
}

static void
draw_face(app *a, cairo_t *cr, int W, int H)
{
    double s = W / 64.0;
    cairo_save(cr);
    cairo_scale(cr, s, (double)H / 64.0);
    cairo_pattern_t *p = cairo_pattern_create_linear(0, 0, 64, 64);
    unsigned c1 = a->theme.icon1, c2 = a->theme.icon2;
    cairo_pattern_add_color_stop_rgb(p, 0, (c1 >> 16 & 0xff) / 255.0, (c1 >> 8 & 0xff) / 255.0, (c1 & 0xff) / 255.0);
    cairo_pattern_add_color_stop_rgb(p, 1, (c2 >> 16 & 0xff) / 255.0, (c2 >> 8 & 0xff) / 255.0, (c2 & 0xff) / 255.0);
    cairo_rectangle(cr, 0, 0, 64, 64);
    cairo_set_source(cr, p);
    cairo_fill(cr);
    cairo_pattern_destroy(p);
    bevel(cr, 0, 0, 64, 64, 1);

    rgb(cr, 0x1c2024);
    cairo_rectangle(cr, 5, 5, 54, 36);
    cairo_fill(cr);
    bevel(cr, 4, 4, 56, 38, 0);
    /* Zonas: as regiões do layout; Mosaico: as janelas organizadas pelo
     * algoritmo; desligado: a pausa. */
    wz_mode m = wz_config_mode(&a->cfg);
    const wz_layout_def *l = wz_config_layout(&a->cfg, a->layout[0] ? a->layout : a->cfg.layout);
    if (m == WZ_MODE_ZONES && l) {
        wz_node *t = wz_parse(l->tree, NULL);
        wz_rect zr[64];
        int n = t ? wz_compute(t, (wz_rect){ 8, 8, 48, 30 }, 2, 0, zr, 64) : 0;
        wz_free(t);
        for (int i = 0; i < n && i < 64; i++) {
            rgb(cr, i == 0 ? 0x7fb0e0 : 0xc2c0c5);
            cairo_rectangle(cr, zr[i].x, zr[i].y, zr[i].w, zr[i].h);
            cairo_fill(cr);
            if (zr[i].w >= 4 && zr[i].h >= 4)
                bevel(cr, zr[i].x, zr[i].y, zr[i].w, zr[i].h, 1);
        }
    } else if (m == WZ_MODE_TILING) {
        draw_windows(a, cr, (wz_rect){ 8, 8, 48, 30 });
    } else {
        rgb(cr, 0x5e6066);
        cairo_rectangle(cr, 26, 16, 4, 14);
        cairo_rectangle(cr, 34, 16, 4, 14);
        cairo_fill(cr);
    }
    arrow_key(cr, HIT_PREV, m != WZ_MODE_OFF);
    led_key(cr, row_btn[HIT_Z].x, ROW_Y, "Z", m == WZ_MODE_ZONES, 0x58d858);
    led_key(cr, row_btn[HIT_M].x, ROW_Y, "M", m == WZ_MODE_TILING, 0xf0b030);
    arrow_key(cr, HIT_NEXT, m != WZ_MODE_OFF);
    cairo_restore(cr);
}

static void
buffer_release(void *data, struct wl_buffer *b)
{
    (void)data;
    wl_buffer_destroy(b);
}

static const struct wl_buffer_listener buffer_listener = { buffer_release };

static void
redraw(app *a)
{
    if (!a->configured)
        return;
    int W = a->width > 0 ? a->width : 64, H = a->height > 0 ? a->height : 64;
    int stride = W * 4, size = stride * H;
    int fd = memfd_create("wmake-zones-wl", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, size) != 0) {
        if (fd >= 0)
            close(fd);
        return;
    }
    void *data = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return;
    }
    cairo_surface_t *cs = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32, W, H, stride);
    cairo_t *cr = cairo_create(cs);
    draw_face(a, cr, W, H);
    cairo_destroy(cr);
    cairo_surface_destroy(cs);
    munmap(data, (size_t)size);
    struct wl_shm_pool *pool = wl_shm_create_pool(a->shm, fd, size);
    struct wl_buffer *b = wl_shm_pool_create_buffer(pool, 0, W, H, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    wl_buffer_add_listener(b, &buffer_listener, NULL);
    wl_surface_attach(a->surface, b, 0, 0);
    wl_surface_damage_buffer(a->surface, 0, 0, W, H);
    wl_surface_commit(a->surface);
}

static void
reload(app *a)
{
    wz_config_free(&a->cfg);
    wz_config_defaults(&a->cfg);
    wz_config_load(&a->cfg, a->cfg_path);
    wz_theme_load(&a->theme, NULL);
}

/* Layout em efeito no primeiro monitor, pelo status do wlmaker (sem
 * resposta, o geral do arquivo). */
static void
query_layout(app *a)
{
    char out[4096];
    a->layout[0] = 0;
    if (wz_ipc_request_session("status", out, sizeof out) != 0)
        return;
    const char *p = strstr(out, " layout=");
    if (!p)
        return;
    p += 8;
    size_t n = strcspn(p, " \n");
    if (n < sizeof a->layout) {
        memcpy(a->layout, p, n);
        a->layout[n] = 0;
    }
}

static void
command(app *a, const char *cmd)
{
    char out[512];
    if (wz_ipc_request_session(cmd, out, sizeof out) != 0)
        fprintf(stderr, "wmake-zones-wl: %s\n", _("wlmaker did not answer (zones disabled at build time?)"));
    reload(a);
    query_layout(a);
    redraw(a);
}

/* Troca de layout pela roda: uma prévia breve das zonas mostra a escolha
 * (o ladrilho é pequeno demais para o nome). */
static void
cycle(app *a, int step)
{
    if (wz_config_mode(&a->cfg) == WZ_MODE_TILING) {
        /* No Mosaico, a roda e as setas trocam o algoritmo. */
        command(a, step < 0 ? "algorithm prev" : "algorithm next");
        return;
    }
    command(a, step < 0 ? "layout prev" : "layout next");
    const wz_layout_def *l = wz_config_layout(&a->cfg, a->layout[0] ? a->layout : a->cfg.layout);
    if (l) {
        char cmd[WZ_IPC_MAX];
        char out[64];
        snprintf(cmd, sizeof cmd, "preview %s", l->tree);
        wz_ipc_request_session(cmd, out, sizeof out);
    }
}

static void
click(app *a, wz_click action, int step)
{
    switch (action) {
    case WZ_CLICK_MODE: command(a, "mode toggle"); break;
    case WZ_CLICK_ENABLE: command(a, "mode enable"); break;
    case WZ_CLICK_ZONES: command(a, "zones toggle"); break;
    case WZ_CLICK_AUTOTILE: command(a, "autotile toggle"); break;
    case WZ_CLICK_CYCLE: cycle(a, step); break;
    case WZ_CLICK_LAYOUTS: command(a, "picker"); break;
    default: break; /* o menu é do wlmaker (clique direito) */
    }
}

/* ---- protocolo de ícones ---------------------------------------------- */

static void
icon_configure(void *data, struct zwlmaker_toplevel_icon_v1 *icon, int32_t w, int32_t h, uint32_t serial)
{
    app *a = data;
    /* O ladrilho inteiro, com o relevo próprio, como o dockapp do X11 (a
     * janela de ícone de 64x64): o wlmaker sugere aos dockapps nativos o
     * ladrilho menos a borda de 3 px de cada lado (58 no de 64). Nunca
     * menos de 64: um wlmaker sem essa regra sugere o ContentSize (48). */
    a->width = w + 6 > 64 ? w + 6 : 64;
    a->height = h + 6 > 64 ? h + 6 : 64;
    zwlmaker_toplevel_icon_v1_ack_configure(icon, serial);
    a->configured = 1;
    redraw(a);
}

static const struct zwlmaker_toplevel_icon_v1_listener icon_listener = { icon_configure };

/* ---- ponteiro ----------------------------------------------------------- */

static void
pointer_at(app *a, wl_fixed_t x, wl_fixed_t y)
{
    int W = a->width > 0 ? a->width : 64, H = a->height > 0 ? a->height : 64;
    a->px = wl_fixed_to_double(x) * 64.0 / W;
    a->py = wl_fixed_to_double(y) * 64.0 / H;
}

static void
p_enter(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *s, wl_fixed_t x, wl_fixed_t y)
{
    app *a = data;
    a->pointer_inside = s == a->surface;
    pointer_at(a, x, y);
}

static void
p_leave(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *s)
{
    ((app *)data)->pointer_inside = 0;
}

static void
p_motion(void *data, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
    pointer_at(data, x, y);
}

/* Teclas da fileira de baixo: setas trocam o layout (Zonas) ou o algoritmo
 * (Mosaico); Z e M escolhem o modo, e a acesa desliga. */
static int
row_click(app *a)
{
    int hit = row_hit(a->px, a->py);
    wz_mode m = wz_config_mode(&a->cfg);
    if (hit == HIT_PREV || hit == HIT_NEXT) {
        if (m != WZ_MODE_OFF)
            cycle(a, hit == HIT_PREV ? -1 : 1);
    } else if (hit == HIT_Z) {
        command(a, m == WZ_MODE_ZONES ? "mode off" : "mode zones");
    } else if (hit == HIT_M) {
        command(a, m == WZ_MODE_TILING ? "mode off" : "mode tiling");
    }
    return hit != HIT_NONE;
}

static void
p_button(void *data, struct wl_pointer *p, uint32_t serial, uint32_t t, uint32_t button, uint32_t state)
{
    app *a = data;
    if (!a->pointer_inside || state != WL_POINTER_BUTTON_STATE_RELEASED)
        return;
    if (button == BTN_LEFT && row_click(a))
        return;
    if (button == BTN_LEFT)
        click(a, a->cfg.click[0], 1);
    else if (button == BTN_MIDDLE)
        click(a, a->cfg.click[1], 1);
}

static void
p_axis(void *data, struct wl_pointer *p, uint32_t t, uint32_t axis, wl_fixed_t v)
{
    app *a = data;
    if (a->pointer_inside && axis == WL_POINTER_AXIS_VERTICAL_SCROLL && v != 0)
        click(a, a->cfg.click[3], v > 0 ? 1 : -1);
}

static void p_frame(void *d, struct wl_pointer *p) {}
static void p_axis_source(void *d, struct wl_pointer *p, uint32_t s) {}
static void p_axis_stop(void *d, struct wl_pointer *p, uint32_t t, uint32_t a) {}
static void p_axis_discrete(void *d, struct wl_pointer *p, uint32_t a, int32_t v) {}

static const struct wl_pointer_listener pointer_listener = {
    p_enter, p_leave, p_motion, p_button, p_axis, p_frame, p_axis_source, p_axis_stop, p_axis_discrete,
};

static void
seat_caps(void *data, struct wl_seat *seat, uint32_t caps)
{
    app *a = data;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !a->pointer) {
        a->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(a->pointer, &pointer_listener, a);
    } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && a->pointer) {
        wl_pointer_destroy(a->pointer);
        a->pointer = NULL;
    }
}

static void seat_name(void *d, struct wl_seat *s, const char *n) {}

static const struct wl_seat_listener seat_listener = { seat_caps, seat_name };

static void
reg_global(void *data, struct wl_registry *r, uint32_t name, const char *iface, uint32_t version)
{
    app *a = data;
    if (!strcmp(iface, wl_compositor_interface.name))
        a->compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
    else if (!strcmp(iface, wl_shm_interface.name))
        a->shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, wl_seat_interface.name) && !a->seat) {
        a->seat = wl_registry_bind(r, name, &wl_seat_interface, version < 5 ? version : 5);
        wl_seat_add_listener(a->seat, &seat_listener, a);
    } else if (!strcmp(iface, zwlmaker_icon_manager_v1_interface.name))
        a->icon_manager = wl_registry_bind(r, name, &zwlmaker_icon_manager_v1_interface, 1);
}

static void reg_remove(void *d, struct wl_registry *r, uint32_t n) {}

static const struct wl_registry_listener reg_listener = { reg_global, reg_remove };

/* Vigia o diretório de um arquivo (renomeações atômicas também contam). */
static void
watch_dir_of(int ifd, const char *path)
{
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (!slash)
        return;
    *slash = 0;
    mkdir(dir, 0700);
    inotify_add_watch(ifd, dir, IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
}

int
main(void)
{
    setlocale(LC_ALL, "");
#ifdef WZ_ENABLE_NLS
    bindtextdomain("wmake-zones", WZ_LOCALEDIR);
    textdomain("wmake-zones");
#endif
    app a = { 0 };
    a.cfg_path = wz_config_path();
    wz_config_defaults(&a.cfg);
    reload(&a);
    query_layout(&a);
    a.display = wl_display_connect(NULL);
    if (!a.display) {
        fprintf(stderr, "wmake-zones-wl: %s\n", _("no Wayland compositor"));
        return 1;
    }
    struct wl_registry *reg = wl_display_get_registry(a.display);
    wl_registry_add_listener(reg, &reg_listener, &a);
    wl_display_roundtrip(a.display);
    if (!a.compositor || !a.shm || !a.icon_manager) {
        fprintf(stderr, "wmake-zones-wl: %s\n", _("the compositor does not offer the wlmaker icon protocol"));
        return 1;
    }
    a.surface = wl_compositor_create_surface(a.compositor);
    a.icon = zwlmaker_icon_manager_v1_get_toplevel_icon(a.icon_manager, NULL, a.surface);
    zwlmaker_toplevel_icon_v1_add_listener(a.icon, &icon_listener, &a);
    wl_surface_commit(a.surface);
    wl_display_roundtrip(a.display);

    int ifd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    watch_dir_of(ifd, a.cfg_path);
    char tpath[1024];
    const char *home = getenv("HOME");
    snprintf(tpath, sizeof tpath, "%s/GNUstep/Defaults/WindowMaker", home ? home : "");
    watch_dir_of(ifd, tpath);

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, NULL);
    int sfd = signalfd(-1, &mask, SFD_CLOEXEC);

    while (!a.quit) {
        while (wl_display_prepare_read(a.display) != 0)
            wl_display_dispatch_pending(a.display);
        wl_display_flush(a.display);
        struct pollfd p[3] = { { wl_display_get_fd(a.display), POLLIN, 0 }, { ifd, POLLIN, 0 }, { sfd, POLLIN, 0 } };
        if (poll(p, 3, -1) < 0 && errno != EINTR) {
            wl_display_cancel_read(a.display);
            break;
        }
        if (p[0].revents & POLLIN) {
            if (wl_display_read_events(a.display) < 0)
                break;
        } else {
            wl_display_cancel_read(a.display);
        }
        if (p[0].revents & (POLLERR | POLLHUP))
            break;
        wl_display_dispatch_pending(a.display);
        if (p[1].revents & POLLIN) {
            char buf[4096];
            while (read(ifd, buf, sizeof buf) > 0)
                ;
            reload(&a);
            query_layout(&a);
            redraw(&a);
        }
        if (p[2].revents & POLLIN)
            a.quit = 1;
    }
    if (a.icon)
        zwlmaker_toplevel_icon_v1_destroy(a.icon);
    if (a.surface)
        wl_surface_destroy(a.surface);
    wl_display_disconnect(a.display);
    wz_config_free(&a.cfg);
    free(a.cfg_path);
    return 0;
}
