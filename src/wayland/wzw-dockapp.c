/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * wmake-zones-wl: o ladrilho do wmake-zones no Clip/Dock do wlmaker, como
 * cliente Wayland do protocolo de ícones do wlmaker (sem XWayland).
 *
 *   clique esquerdo / roda   layout seguinte / anterior
 *   clique do meio           liga ou desliga as zonas
 *   clique direito           menu do wlmaker (ações do Dockapps.plist)
 *
 * O desenho é o mesmo do dockapp do X11. O estado vem do arquivo de
 * configuração, vigiado por inotify (o wlmaker grava nele a cada troca):
 * parado, o processo dorme em poll() sem timeout.
 */
#define _GNU_SOURCE
#include "wz-config.h"
#include "wz-ipc.h"
#include "wz-layout.h"
#include "wz-theme.h"

#include <cairo.h>
#include <errno.h>
#include <fcntl.h>
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
    wz_config cfg;
    char *cfg_path;
    wz_theme theme;
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
    { 'E', { 7, 4, 6, 4, 7 } }, { 'F', { 7, 4, 6, 4, 4 } }, { 'I', { 7, 2, 2, 2, 7 } },
    { 'L', { 4, 4, 4, 4, 7 } }, { 'N', { 6, 5, 5, 5, 5 } }, { 'O', { 2, 5, 5, 5, 2 } },
    { 'S', { 3, 4, 2, 1, 6 } }, { 'T', { 7, 2, 2, 2, 2 } }, { 'Z', { 7, 1, 2, 4, 7 } },
    { '+', { 0, 2, 7, 2, 0 } },
};

static void
pixtext(cairo_t *cr, const char *s, double x, double y, unsigned c)
{
    rgb(cr, c);
    for (; *s; s++, x += 4)
        for (size_t g = 0; g < sizeof glyphs / sizeof *glyphs; g++)
            if (glyphs[g].c == *s)
                for (int r = 0; r < 5; r++)
                    for (int k = 0; k < 3; k++)
                        if (glyphs[g].rows[r] & (4 >> k))
                            cairo_rectangle(cr, x + k, y + r, 1, 1);
    cairo_fill(cr);
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
    cairo_rectangle(cr, 5, 5, 54, 40);
    cairo_fill(cr);
    bevel(cr, 4, 4, 56, 42, 0);
    int on = a->cfg.zones || a->cfg.autotile;
    const wz_layout_def *l = wz_config_layout(&a->cfg, a->cfg.layout);
    if (l) {
        wz_node *t = wz_parse(l->tree, NULL);
        wz_rect zr[64];
        int n = t ? wz_compute(t, (wz_rect){ 8, 8, 48, 34 }, 2, 0, zr, 64) : 0;
        wz_free(t);
        for (int i = 0; i < n && i < 64; i++) {
            rgb(cr, i == 0 && on ? (a->cfg.autotile ? 0xe0a030 : 0x7fb0e0) : on ? 0xc2c0c5 : 0x6a6a70);
            cairo_rectangle(cr, zr[i].x, zr[i].y, zr[i].w, zr[i].h);
            cairo_fill(cr);
            if (zr[i].w >= 4 && zr[i].h >= 4)
                bevel(cr, zr[i].x, zr[i].y, zr[i].w, zr[i].h, 1);
        }
    }
    rgb(cr, 0x1c2024);
    cairo_rectangle(cr, 5, 49, 54, 10);
    cairo_fill(cr);
    bevel(cr, 4, 48, 56, 12, 0);
    rgb(cr, a->cfg.autotile ? 0xf0b030 : a->cfg.zones ? 0x40d040 : 0x505050);
    cairo_rectangle(cr, 8, 51, 5, 5);
    cairo_fill(cr);
    const char *label = a->cfg.autotile && a->cfg.zones ? "Z+TILE" : a->cfg.autotile ? "TILE" : a->cfg.zones ? "ZONES" : "OFF";
    pixtext(cr, label, 17, 51, on ? 0xe8e8e8 : 0x8a8a8a);
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

static void
command(app *a, const char *cmd)
{
    char out[512];
    if (wz_ipc_request_session(cmd, out, sizeof out) != 0)
        fprintf(stderr, "wmake-zones-wl: o wlmaker não respondeu (zonas desligadas na compilação?)\n");
    reload(a);
    redraw(a);
}

/* ---- protocolo de ícones ---------------------------------------------- */

static void
icon_configure(void *data, struct zwlmaker_toplevel_icon_v1 *icon, int32_t w, int32_t h, uint32_t serial)
{
    app *a = data;
    a->width = w > 0 ? w : 64;
    a->height = h > 0 ? h : 64;
    zwlmaker_toplevel_icon_v1_ack_configure(icon, serial);
    a->configured = 1;
    redraw(a);
}

static const struct zwlmaker_toplevel_icon_v1_listener icon_listener = { icon_configure };

/* ---- ponteiro ----------------------------------------------------------- */

static void
p_enter(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *s, wl_fixed_t x, wl_fixed_t y)
{
    app *a = data;
    a->pointer_inside = s == a->surface;
}

static void
p_leave(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *s)
{
    ((app *)data)->pointer_inside = 0;
}

static void
p_motion(void *data, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
}

static void
p_button(void *data, struct wl_pointer *p, uint32_t serial, uint32_t t, uint32_t button, uint32_t state)
{
    app *a = data;
    if (!a->pointer_inside || state != WL_POINTER_BUTTON_STATE_RELEASED)
        return;
    if (button == BTN_LEFT)
        command(a, "layout next");
    else if (button == BTN_MIDDLE)
        command(a, "zones toggle");
}

static void
p_axis(void *data, struct wl_pointer *p, uint32_t t, uint32_t axis, wl_fixed_t v)
{
    app *a = data;
    if (a->pointer_inside && axis == WL_POINTER_AXIS_VERTICAL_SCROLL && v != 0)
        command(a, v > 0 ? "layout next" : "layout prev");
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
    app a = { 0 };
    a.cfg_path = wz_config_path();
    wz_config_defaults(&a.cfg);
    reload(&a);
    a.display = wl_display_connect(NULL);
    if (!a.display) {
        fprintf(stderr, "wmake-zones-wl: sem compositor Wayland\n");
        return 1;
    }
    struct wl_registry *reg = wl_display_get_registry(a.display);
    wl_registry_add_listener(reg, &reg_listener, &a);
    wl_display_roundtrip(a.display);
    if (!a.compositor || !a.shm || !a.icon_manager) {
        fprintf(stderr, "wmake-zones-wl: o compositor não oferece o protocolo de ícones do wlmaker\n");
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
