/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * wmake-zones, backend X11 (Window Maker). Um processo pequeno, orientado a
 * eventos: em repouso não acorda (sem polling de ponteiro, sem timers).
 */
#ifndef WZX_H
#define WZX_H

#include "wz-config.h"
#include "wz-layout.h"
#include "wz-tile.h"
#include "wz-theme.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <stdint.h>

#define WZX_MAX_MON 16
#define WZX_MAX_ZONES 128
#define WZX_MAX_WIN 512
#define WZX_MAX_OR 256
#define WZX_HISTORY 12

enum {
    A_NET_SUPPORTED, A_NET_CLIENT_LIST, A_NET_ACTIVE_WINDOW, A_NET_CURRENT_DESKTOP,
    A_NET_WM_DESKTOP, A_NET_WM_STATE, A_NET_WM_STATE_FULLSCREEN, A_NET_WM_STATE_MAXIMIZED_VERT,
    A_NET_WM_STATE_MAXIMIZED_HORZ, A_NET_WM_STATE_HIDDEN, A_NET_WM_STATE_SHADED,
    A_NET_WM_STATE_STICKY, A_NET_WM_STATE_SKIP_TASKBAR, A_NET_WM_STATE_MODAL, A_NET_WM_STATE_ABOVE,
    A_NET_WM_WINDOW_TYPE, A_NET_WM_WINDOW_TYPE_NORMAL, A_NET_WM_WINDOW_TYPE_DIALOG,
    A_NET_WM_WINDOW_TYPE_DOCK, A_NET_WM_WINDOW_TYPE_DESKTOP, A_NET_WM_WINDOW_TYPE_UTILITY,
    A_NET_WM_WINDOW_TYPE_SPLASH, A_NET_WM_WINDOW_TYPE_TOOLBAR, A_NET_WM_WINDOW_TYPE_MENU,
    A_NET_WM_WINDOW_TYPE_NOTIFICATION, A_NET_FRAME_EXTENTS, A_NET_WM_STRUT, A_NET_WM_STRUT_PARTIAL,
    A_NET_WM_NAME, A_NET_WM_PID, A_NET_WM_CM, A_UTF8_STRING, A_WM_STATE, A_WM_DELETE_WINDOW,
    A_WM_PROTOCOLS, A_WZ_RESTORE, A_WZ_ZONE, A_WZ_STATE,
    A_COUNT
};

typedef struct {
    char name[64];
    wz_rect geom;     /* monitor inteiro */
    wz_rect work;     /* área útil (struts, Dock) */
    int primary;
} wzx_monitor;

/* Janela de topo override-redirect (ladrilhos do Dock/Clip, menus). */
typedef struct {
    Window w;
    wz_rect r;
    int mapped;
} wzx_or;

enum {
    WS_FULLSCREEN = 1 << 0, WS_MAXV = 1 << 1, WS_MAXH = 1 << 2, WS_HIDDEN = 1 << 3,
    WS_SHADED = 1 << 4, WS_STICKY = 1 << 5, WS_SKIP_TASKBAR = 1 << 6, WS_MODAL = 1 << 7,
    WS_ABOVE = 1 << 8,
};

typedef struct {
    Window client, frame;
    char *wm_class, *instance;
    int special;          /* diálogo, menu, splash, dock... */
    int transient;
    int fixed;            /* min == max */
    wz_limits hints;      /* do cliente (sem decoração) */
    int gravity;
    int ext[4];           /* left, right, top, bottom */
    long desktop;         /* -1: todas */
    unsigned state;
    wz_rect outer;        /* quadro, com bordas */
    int mapped;
    /* mosaico */
    int user_float;       /* o usuário tirou do mosaico */
    int rule_float;       /* regra ou restrição */
    int tiled;
    int tile_mon;
    long order;           /* posição no mosaico (menor = principal) */
    wz_rect expect;       /* geometria pedida por nós */
    int expect_valid;
    int seen;             /* marca para diff da lista */
    int rule_done;
} wzx_win;

typedef enum { DRAG_IDLE = 0, DRAG_ARMED, DRAG_MOVING, DRAG_RESIZING } wzx_drag_state;

typedef struct wzx wzx;

typedef struct {
    Window win;
    int mon;
    int shown;
    int argb;
    Picture pict;
    Visual *visual;
    Colormap cmap;
    int depth;
    XftDraw *xft;
    wz_rect zones[WZX_MAX_ZONES];
    char *names[WZX_MAX_ZONES];
    int nzones;
    int hl_a, hl_b;       /* zonas destacadas (-1) */
    wz_rect target;
    int has_target;
    int mode;             /* 0 zonas, 1 mosaico, 2 seletor, 3 prévia */
} wzx_overlay;

struct wzx {
    Display *dpy;
    int screen;
    Window root;
    Atom atom[A_COUNT];
    int xi_opcode;
    int has_xi, has_randr, has_shape, randr_event;
    int fd;
    wz_config cfg;
    char *cfg_path;
    wz_theme theme;
    int theme_loaded;

    wzx_monitor mon[WZX_MAX_MON];
    int nmon;
    int work_dirty;

    wzx_or ors[WZX_MAX_OR];
    int nor;

    wzx_win *win[WZX_MAX_WIN];
    int nwin;
    long order_seq;
    long current_desktop;
    Window active;

    /* arrasto */
    wzx_drag_state drag;
    Window drag_frame;
    wzx_win *drag_w;
    int press_x, press_y;
    wz_rect drag_start;
    int drag_cancel;
    int drag_mod;          /* modificador segurado */
    long drag_show_at;     /* ms monotônico para mostrar o overlay */
    int drag_dirty;
    int super_down, alt_down, ctrl_down, shift_down;
    int raw_keys_selected;

    wzx_overlay ov;
    long ov_hide_at;       /* prévia: esconder em */

    /* seletor por teclado */
    int picker;
    Window picker_target;
    int picker_sel;

    /* mosaico: um conjunto por monitor e área de trabalho */
    int tile_dirty;
    long tile_at;
    struct wzx_tset {
        int used;
        char mon[64];
        long desk;
        wz_bsp bsp;
        double ratio;
    } tset[64];

    /* histórico (desfazer) */
    struct {
        int n;
        Window w[64];
        wz_rect r[64];
    } hist[WZX_HISTORY];
    int nhist;

    /* atalhos */
    struct {
        unsigned mods;
        KeyCode code;
        int grabbed;
        int conflict;
    } keys[WZ_KEY_N];

    /* dockapp */
    int dockapp;
    Window dock_win, dock_icon;
    Window popup;
    int popup_kind;        /* 1 layouts, 2 menu */
    int popup_sel;
    int popup_nitems;
    XftFont *font, *font_bold, *font_big;

    /* IPC */
    int listen_fd;
    char *sock_path;
    int quit;
    int wm_snapping_saved; /* WindowSnapping desligado por nós */
};

/* wzx-main.c */
long wzx_now_ms(void);
long wzx_now_us(void);
void wzx_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void wzx_save_cfg(wzx *z, const char *section, const char *key, const char *value);
void wzx_apply_config(wzx *z);

/* wzx-wm.c */
void wzx_wm_init(wzx *z);
void wzx_update_monitors(wzx *z);
void wzx_update_workarea(wzx *z);
void wzx_sync_clients(wzx *z);
wzx_win *wzx_find(wzx *z, Window w);
wzx_win *wzx_find_frame(wzx *z, Window frame);
void wzx_refresh_win(wzx *z, wzx_win *w);
void wzx_refresh_geom(wzx *z, wzx_win *w);
int wzx_monitor_at(wzx *z, int x, int y);
int wzx_monitor_of(wzx *z, wz_rect r);
void wzx_move_outer(wzx *z, wzx_win *w, wz_rect r);
void wzx_set_restore(wzx *z, wzx_win *w, wz_rect r);
int wzx_get_restore(wzx *z, wzx_win *w, wz_rect *r);
void wzx_clear_restore(wzx *z, wzx_win *w);
void wzx_set_zone_prop(wzx *z, wzx_win *w, long zone);
long wzx_get_zone_prop(wzx *z, wzx_win *w);
int wzx_tileable(wzx *z, wzx_win *w);
int wzx_snappable(wzx *z, wzx_win *w);
void wzx_activate(wzx *z, wzx_win *w);
const char *wzx_title(wzx *z, wzx_win *w, char *buf, size_t len);
int wzx_compositing(wzx *z);
void wzx_or_event(wzx *z, Window w, int x, int y, int wd, int ht, int mapped, int destroyed);
void wzx_wm_snapping(wzx *z, int disable);
int wzx_snapping_marked(const char *cfg_path);
int wzx_zones_for(wzx *z, int mon, wz_rect *out, char **names, int max);
long wzx_desktop_of(wzx *z, wzx_win *w);

/* wzx-drag.c */
void wzx_drag_init(wzx *z);
void wzx_xi_select(wzx *z);
void wzx_drag_raw(wzx *z, XGenericEventCookie *c);
void wzx_drag_configure(wzx *z, XConfigureEvent *e);
void wzx_drag_flush(wzx *z);
void wzx_drag_timer(wzx *z);
void wzx_snap(wzx *z, wzx_win *w, wz_rect target, long zone, int record);
void wzx_history_push(wzx *z, wzx_win **ws, int n);
void wzx_history_undo(wzx *z);

/* wzx-overlay.c */
void wzx_overlay_show(wzx *z, int mon, int mode);
void wzx_overlay_set_zones(wzx *z, const wz_rect *zones, char **names, int n);
void wzx_overlay_highlight(wzx *z, int a, int b, const wz_rect *target);
void wzx_overlay_hide(wzx *z);
void wzx_overlay_expose(wzx *z);
void wzx_fonts(wzx *z);
void wzx_theme_reload(wzx *z);
unsigned long wzx_pixel(wzx *z, unsigned rgb);
void wzx_draw_bevel(wzx *z, Drawable d, GC gc, int x, int y, int w, int h, int raised);

/* wzx-picker (wzx-overlay.c) */
void wzx_picker_open(wzx *z);
int wzx_picker_key(wzx *z, XKeyEvent *e);
int wzx_picker_button(wzx *z, XButtonEvent *e);
void wzx_picker_close(wzx *z);

/* wzx-tile.c */
void wzx_tile_mark(wzx *z);
void wzx_tile_run(wzx *z);
void wzx_tile_disable_all(wzx *z);
int wzx_tile_active_for(wzx *z, int mon);
int wzx_tile_cells(wzx *z, int mon, wz_rect *out, wzx_win **ws, int max);
void wzx_tile_drop(wzx *z, wzx_win *w, int px, int py);
void wzx_tile_user_resize(wzx *z, wzx_win *w);
void wzx_tile_swap_dir(wzx *z, wzx_win *w, int dir);
void wzx_tile_promote(wzx *z, wzx_win *w);
void wzx_tile_ratio(wzx *z, wzx_win *w, double delta);
void wzx_tile_toggle_float(wzx *z, wzx_win *w);
void wzx_tile_forget(wzx *z, wzx_win *w);

/* wzx-keys.c */
void wzx_keys_grab(wzx *z);
void wzx_keys_ungrab(wzx *z);
int wzx_keys_event(wzx *z, XKeyEvent *e);
int wzx_parse_key(Display *dpy, const char *spec, unsigned *mods, KeyCode *code);

/* wzx-ipc.c */
int wzx_ipc_listen(wzx *z);
void wzx_ipc_accept(wzx *z);
void wzx_ipc_close(wzx *z);
/* Executa um comando (IPC, atalho, dockapp). out pode ser NULL. */
void wzx_command(wzx *z, const char *line, char *out, size_t outlen);

/* wzx-dockapp.c */
void wzx_dockapp_create(wzx *z);
void wzx_dockapp_redraw(wzx *z);
int wzx_dockapp_event(wzx *z, XEvent *e);

#endif
