/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Dockapp wmake-zones: ladrilho de 64x64 no Dock/Clip do Window Maker,
 * dentro do próprio daemon (um processo só, sem serviço duplicado).
 *
 *   clique esquerdo   Zonas <-> Mosaico          ([Dockapp] LeftClick)
 *   clique do meio    desliga / religa o último   ([Dockapp] MiddleClick)
 *   clique direito    menu no estilo do Window Maker, com submenus
 *   roda              layout anterior/seguinte    ([Dockapp] Wheel)
 *
 * O ladrilho mostra a miniatura do layout ativo numa "tela" afundada e,
 * embaixo, duas teclas com LED: Z (zonas, verde) e M (mosaico, âmbar);
 * nenhuma acesa = desligado. Cada troca mostra um balão discreto com o modo
 * e o layout, que some sozinho (a dica ao parar o ponteiro é o balão do
 * próprio Window Maker, com o nome do dockapp). Só redesenha quando algo muda; parado, não acorda.
 */
#define _GNU_SOURCE
#include "wzx.h"
#include "wz-i18n.h"

#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define TILE 64

enum { POP_NONE = 0, POP_LAYOUTS, POP_MENU };

static Pixmap face;
static GC dgc;

/* ---- fonte de pixels 3x5 (LED) ------------------------------------------ */

static const struct {
    char c;
    unsigned char rows[5];
} glyphs[] = {
    { 'M', { 5, 7, 7, 5, 5 } }, { 'Z', { 7, 1, 2, 4, 7 } },
};

static void
pixtext(wzx *z, Drawable d, const char *s, int x, int y, unsigned rgb, int scale)
{
    XSetForeground(z->dpy, dgc, wzx_pixel(z, rgb));
    for (; *s; s++, x += 4 * scale)
        for (size_t g = 0; g < sizeof glyphs / sizeof *glyphs; g++) {
            if (glyphs[g].c != *s)
                continue;
            for (int r = 0; r < 5; r++)
                for (int c = 0; c < 3; c++)
                    if (glyphs[g].rows[r] & (4 >> c))
                        XFillRectangle(z->dpy, d, dgc, x + c * scale, y + r * scale, (unsigned)scale, (unsigned)scale);
        }
}

/* ---- miniatura de um layout --------------------------------------------- */

static void
draw_mini(wzx *z, Drawable d, GC gc, const char *tree, wz_rect box, unsigned tile_rgb, int hl_index, unsigned hl_rgb)
{
    wz_node *t = wz_parse(tree, NULL);
    if (!t)
        return;
    wz_rect zr[64];
    int n = wz_compute(t, box, 2, 0, zr, 64);
    wz_free(t);
    for (int i = 0; i < n && i < 64; i++) {
        XSetForeground(z->dpy, gc, wzx_pixel(z, i == hl_index ? hl_rgb : tile_rgb));
        XFillRectangle(z->dpy, d, gc, zr[i].x, zr[i].y, (unsigned)zr[i].w, (unsigned)zr[i].h);
        if (zr[i].w >= 4 && zr[i].h >= 4) {
            /* chanfro de um pixel: o ladrilho do OpenStep em miniatura */
            XSetForeground(z->dpy, gc, wzx_pixel(z, 0xffffff));
            XDrawLine(z->dpy, d, gc, zr[i].x, zr[i].y, zr[i].x + zr[i].w - 1, zr[i].y);
            XDrawLine(z->dpy, d, gc, zr[i].x, zr[i].y, zr[i].x, zr[i].y + zr[i].h - 1);
            XSetForeground(z->dpy, gc, wzx_pixel(z, 0x303030));
            XDrawLine(z->dpy, d, gc, zr[i].x, zr[i].y + zr[i].h - 1, zr[i].x + zr[i].w - 1, zr[i].y + zr[i].h - 1);
            XDrawLine(z->dpy, d, gc, zr[i].x + zr[i].w - 1, zr[i].y, zr[i].x + zr[i].w - 1, zr[i].y + zr[i].h - 1);
        }
    }
}

static int
pointer_mon(wzx *z)
{
    Window r, c;
    int x = 0, y = 0, wx, wy;
    unsigned m;
    XQueryPointer(z->dpy, z->root, &r, &c, &x, &y, &wx, &wy, &m);
    return wzx_monitor_at(z, x, y);
}

static const wz_layout_def *
current_layout(wzx *z)
{
    return wzx_layout_of(z, pointer_mon(z));
}

static const char *
layout_label(const wz_layout_def *l)
{
    if (!l)
        return "?";
    if (l->label)
        return l->label;
    const char *b = wz_builtin_label(l->id);
    return b ? _(b) : l->id;
}

static const char *
mode_label(wz_mode m)
{
    return m == WZ_MODE_TILING ? _("Tiling") : m == WZ_MODE_ZONES ? _("Zones") : _("Off");
}

static const char *const algo_ids[] = { "master", "columns", "rows", "grid", "bsp", "monocle", "spiral", "dwindle" };
#define NALGO (int)(sizeof algo_ids / sizeof *algo_ids)

static const char *
algo_label(const char *id)
{
    static const struct {
        const char *id, *label;
    } t[] = {
        { "master", N_("Main + stack") }, { "columns", N_("Columns") },  { "rows", N_("Rows") },
        { "grid", N_("Grid") },           { "bsp", N_("BSP (binary tree)") }, { "monocle", N_("Monocle") },
        { "spiral", N_("Spiral") },       { "dwindle", N_("Dwindle") },
    };
    for (size_t i = 0; i < sizeof t / sizeof *t; i++)
        if (!strcmp(t[i].id, id))
            return _(t[i].label);
    return id;
}

/* Tecla com LED: afundada e escura apagada, acesa com a letra escura. */
static void
led_key(wzx *z, int x, int y, const char *letter, int lit, unsigned color)
{
    const int w = 25, h = 14;
    XSetForeground(z->dpy, dgc, wzx_pixel(z, lit ? color : 0x2a2e33));
    XFillRectangle(z->dpy, face, dgc, x + 1, y + 1, w - 2, h - 2);
    wzx_draw_bevel(z, face, dgc, x, y, w, h, 0);
    if (lit) {
        /* brilho no alto do LED */
        XSetForeground(z->dpy, dgc, wzx_pixel(z, wz_rgb_mix(color, 0xffffff, 110)));
        XDrawLine(z->dpy, face, dgc, x + 2, y + 2, x + w - 3, y + 2);
    }
    pixtext(z, face, letter, x + (w - 6) / 2, y + 2, lit ? 0x101010 : 0x7a7e84, 2);
}

void
wzx_dockapp_redraw(wzx *z)
{
    if (!z->dockapp || !z->dock_icon)
        return;
    wzx_theme_reload(z);
    if (!face)
        face = XCreatePixmap(z->dpy, z->dock_icon, TILE, TILE, (unsigned)DefaultDepth(z->dpy, z->screen));
    if (!dgc)
        dgc = XCreateGC(z->dpy, face, 0, NULL);
    /* Fundo: ladrilho de ícone (degradê diagonal do IconBack). */
    for (int i = 0; i < 2 * TILE; i++) {
        XSetForeground(z->dpy, dgc, wzx_pixel(z, wz_rgb_mix(z->theme.icon1, z->theme.icon2, i * 255 / (2 * TILE))));
        XDrawLine(z->dpy, face, dgc, i, 0, i - TILE, TILE);
        XDrawLine(z->dpy, face, dgc, i, 0, 0, i);
    }
    wzx_draw_bevel(z, face, dgc, 0, 0, TILE, TILE, 1);
    /* Tela afundada com a miniatura do layout. */
    wz_rect scr = { 5, 5, 54, 36 };
    XSetForeground(z->dpy, dgc, wzx_pixel(z, 0x1c2024));
    XFillRectangle(z->dpy, face, dgc, scr.x, scr.y, (unsigned)scr.w, (unsigned)scr.h);
    wzx_draw_bevel(z, face, dgc, scr.x - 1, scr.y - 1, scr.w + 2, scr.h + 2, 0);
    wz_mode m = wz_config_mode(&z->cfg);
    const wz_layout_def *l = current_layout(z);
    unsigned tile = m != WZ_MODE_OFF ? 0xc2c0c5 : 0x5e6066;
    if (l)
        draw_mini(z, face, dgc, l->tree, (wz_rect){ scr.x + 3, scr.y + 3, scr.w - 6, scr.h - 6 }, tile,
                  m != WZ_MODE_OFF ? 0 : -1, m == WZ_MODE_TILING ? 0xe0a030 : 0x7fb0e0);
    /* Teclas Z e M: a do modo ativo acesa. */
    led_key(z, 5, 45, "Z", m == WZ_MODE_ZONES, 0x58d858);
    led_key(z, 34, 45, "M", m == WZ_MODE_TILING, 0xf0b030);
    XCopyArea(z->dpy, face, z->dock_icon, dgc, 0, 0, TILE, TILE, 0, 0);
    XFlush(z->dpy);
}

void
wzx_dockapp_create(wzx *z)
{
    XSetWindowAttributes a = { 0 };
    a.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | StructureNotifyMask | EnterWindowMask |
                   LeaveWindowMask;
    a.background_pixmap = ParentRelative;
    z->dock_win = XCreateWindow(z->dpy, z->root, 0, 0, TILE, TILE, 0, CopyFromParent, InputOutput, CopyFromParent,
                                CWEventMask, &a);
    z->dock_icon = XCreateWindow(z->dpy, z->root, 0, 0, TILE, TILE, 0, CopyFromParent, InputOutput, CopyFromParent,
                                 CWEventMask | CWBackPixmap, &a);
    XClassHint ch = { "wmake-zones", "DockApp" };
    XSetClassHint(z->dpy, z->dock_win, &ch);
    XStoreName(z->dpy, z->dock_win, "wmake-zones");
    XWMHints *h = XAllocWMHints();
    h->flags = StateHint | IconWindowHint | WindowGroupHint | IconPositionHint;
    h->initial_state = WithdrawnState;
    h->icon_window = z->dock_icon;
    h->window_group = z->dock_win;
    h->icon_x = h->icon_y = 0;
    XSetWMHints(z->dpy, z->dock_win, h);
    XFree(h);
    char *argv[] = { "wmake-zones", "--dockapp" };
    XSetCommand(z->dpy, z->dock_win, argv, 2);
    XMapWindow(z->dpy, z->dock_win);
    z->dockapp = 1;
}

/* ---- texto -------------------------------------------------------------- */

static void
xft_text(wzx *z, XftDraw *xd, XftFont *f, unsigned rgb, int x, int y, const char *s)
{
    if (!f || !s)
        return;
    XRenderColor rc = { (unsigned short)((rgb >> 16 & 0xff) * 257), (unsigned short)((rgb >> 8 & 0xff) * 257),
                        (unsigned short)((rgb & 0xff) * 257), 0xffff };
    XftColor col;
    Visual *v = DefaultVisual(z->dpy, z->screen);
    Colormap cm = DefaultColormap(z->dpy, z->screen);
    XftColorAllocValue(z->dpy, v, cm, &rc, &col);
    XftDrawStringUtf8(xd, &col, f, x, y, (const FcChar8 *)s, (int)strlen(s));
    XftColorFree(z->dpy, v, cm, &col);
}

static int
text_w(wzx *z, XftFont *f, const char *s)
{
    XGlyphInfo gi;
    XftTextExtentsUtf8(z->dpy, f, (const FcChar8 *)s, (int)strlen(s), &gi);
    return gi.xOff;
}

/* Posição na raiz do ladrilho e o monitor onde ele está. */
static int
tile_origin(wzx *z, int *ix, int *iy)
{
    Window child;
    *ix = *iy = 0;
    XTranslateCoordinates(z->dpy, z->dock_icon, z->root, 0, 0, ix, iy, &child);
    return wzx_monitor_at(z, *ix + TILE / 2, *iy + TILE / 2);
}

/* Ao lado do ladrilho (lado com espaço), dentro do monitor. */
static void
place_beside(wzx *z, int w, int h, int *x, int *y)
{
    int ix, iy;
    int mon = tile_origin(z, &ix, &iy);
    wz_rect g = z->mon[mon].geom;
    *x = ix + TILE + w <= g.x + g.w ? ix + TILE : ix - w;
    *y = iy;
    if (*y + h > g.y + g.h)
        *y = g.y + g.h - h;
    if (*y < g.y)
        *y = g.y;
    if (*x < g.x)
        *x = g.x;
}

/* ---- balão -------------------------------------------------------------- */

static Window balloon;
static long balloon_hide_at, tip_at;

static void
balloon_hide(void)
{
    balloon_hide_at = 0;
}

static void
balloon_close(wzx *z)
{
    if (balloon) {
        XDestroyWindow(z->dpy, balloon);
        balloon = None;
        XFlush(z->dpy);
    }
    balloon_hide();
}

/* Balão amarelo-claro dos dicas do WPrefs, com duas linhas: o modo e o
 * layout. ms > 0: some sozinho. */
static void
balloon_show(wzx *z, long ms)
{
    if (!z->dock_icon || z->popup)
        return;
    wzx_fonts(z);
    if (!z->font || !z->font_bold)
        return;
    balloon_close(z);
    char l1[128], l2[200];
    snprintf(l1, sizeof l1, "wmake-zones: %s", mode_label(wz_config_mode(&z->cfg)));
    snprintf(l2, sizeof l2, _("Layout: %s"), layout_label(current_layout(z)));
    int w1 = text_w(z, z->font_bold, l1), w2 = text_w(z, z->font, l2);
    int w = (w1 > w2 ? w1 : w2) + 16, h = z->font_bold->height + z->font->height + 12;
    int x, y;
    place_beside(z, w, h, &x, &y);
    y += 6;
    XSetWindowAttributes a = { 0 };
    a.override_redirect = True;
    a.save_under = True;
    a.event_mask = ExposureMask;
    balloon = XCreateWindow(z->dpy, z->root, x, y, (unsigned)w, (unsigned)h, 0, CopyFromParent, InputOutput,
                            CopyFromParent, CWOverrideRedirect | CWSaveUnder | CWEventMask, &a);
    XClassHint ch = { "wmake-zones-balloon", "WmakeZones" };
    XSetClassHint(z->dpy, balloon, &ch);
    XMapRaised(z->dpy, balloon);
    Pixmap buf = XCreatePixmap(z->dpy, balloon, (unsigned)w, (unsigned)h, (unsigned)DefaultDepth(z->dpy, z->screen));
    XftDraw *xd = XftDrawCreate(z->dpy, buf, DefaultVisual(z->dpy, z->screen), DefaultColormap(z->dpy, z->screen));
    XSetForeground(z->dpy, dgc, wzx_pixel(z, 0xfcf6d0));
    XFillRectangle(z->dpy, buf, dgc, 0, 0, (unsigned)w, (unsigned)h);
    XSetForeground(z->dpy, dgc, wzx_pixel(z, 0x000000));
    XDrawRectangle(z->dpy, buf, dgc, 0, 0, (unsigned)w - 1, (unsigned)h - 1);
    xft_text(z, xd, z->font_bold, 0x000000, 8, 5 + z->font_bold->ascent, l1);
    xft_text(z, xd, z->font, 0x202020, 8, 7 + z->font_bold->height + z->font->ascent, l2);
    XSetWindowBackgroundPixmap(z->dpy, balloon, buf);
    XClearWindow(z->dpy, balloon);
    XftDrawDestroy(xd);
    XFreePixmap(z->dpy, buf);
    XFlush(z->dpy);
    balloon_hide_at = ms > 0 ? wzx_now_ms() + ms : 0;
}

/* ---- menus -------------------------------------------------------------- */

/* Menu no estilo do Window Maker: barra de título, itens com chanfro,
 * submenus com a seta à direita e o indicador de rádio dos itens de
 * estado. Dois níveis: o menu e um submenu aberto por vez. */
enum { K_ACT = 0, K_SUB, K_RADIO };
enum { M_TOP = 0, M_MODE, M_LAYOUT, M_ALGO, M_SETTINGS, M_HELP };
enum {
    A_NONE = 0, A_UNDO, A_REDO, A_RESTORE, A_RESTORE_ALL, A_ARRANGE, A_FLOAT, A_EDITOR, A_PREFS, A_KEYS, A_RULES,
    A_MONITORS, A_HELP_KEYS, A_ABOUT, A_MODE_ZONES, A_MODE_TILING, A_MODE_OFF, A_EMERGENCY,
    A_LAYOUT0 = 100, A_ALGO0 = 1000,
};

typedef struct {
    int kind, id, on;
    char label[160];
} mitem;

#define MAX_ITEMS 300

typedef struct {
    Window win;
    int menu, n, sel;
    int x, y, w, h, title_h, item_h;
    mitem items[MAX_ITEMS];
} mpop;

static mpop pops[2];
static int npops;
/* Grade de layouts (clique "layouts"). */
static struct {
    int cols, cell_w, cell_h, title_h, w, h;
} grid;

static void
add(mpop *p, int kind, int id, int on, const char *label)
{
    if (p->n >= MAX_ITEMS)
        return;
    mitem *it = &p->items[p->n++];
    it->kind = kind;
    it->id = id;
    it->on = on;
    snprintf(it->label, sizeof it->label, "%s", label);
}

static const char *
menu_title(int menu)
{
    switch (menu) {
    case M_MODE: return _("Mode");
    case M_LAYOUT: return _("Layout");
    case M_ALGO: return _("Tiling algorithm");
    case M_SETTINGS: return _("Settings");
    case M_HELP: return _("Help");
    default: return "wmake-zones";
    }
}

static void
fill_menu(wzx *z, mpop *p, int menu)
{
    p->n = 0;
    p->menu = menu;
    wz_mode m = wz_config_mode(&z->cfg);
    switch (menu) {
    case M_TOP:
        add(p, K_SUB, M_MODE, 0, _("Mode"));
        add(p, K_SUB, M_LAYOUT, 0, _("Layout"));
        add(p, K_SUB, M_ALGO, 0, _("Tiling algorithm"));
        add(p, K_ACT, A_UNDO, 0, _("Undo"));
        add(p, K_ACT, A_REDO, 0, _("Redo"));
        add(p, K_ACT, A_RESTORE, 0, _("Restore window"));
        add(p, K_ACT, A_RESTORE_ALL, 0, _("Restore all arranged windows"));
        add(p, K_ACT, A_ARRANGE, 0, _("Arrange the desktop"));
        add(p, K_ACT, A_FLOAT, 0, _("Active window: floating/tiled"));
        add(p, K_SUB, M_SETTINGS, 0, _("Settings"));
        add(p, K_SUB, M_HELP, 0, _("Help"));
        break;
    case M_MODE:
        add(p, K_RADIO, A_MODE_ZONES, m == WZ_MODE_ZONES, _("Zones"));
        add(p, K_RADIO, A_MODE_TILING, m == WZ_MODE_TILING, _("Tiling"));
        add(p, K_RADIO, A_MODE_OFF, m == WZ_MODE_OFF, _("Off"));
        add(p, K_ACT, A_EMERGENCY, 0, _("Float everything now"));
        break;
    case M_LAYOUT: {
        const wz_layout_def *cur = current_layout(z);
        for (int i = 0; i < z->cfg.nlayouts && i < 200; i++)
            add(p, K_RADIO, A_LAYOUT0 + i, &z->cfg.layouts[i] == cur, layout_label(&z->cfg.layouts[i]));
        add(p, K_ACT, A_EDITOR, 0, _("Edit zones..."));
        break;
    }
    case M_ALGO: {
        const char *cur = wz_algo_name(wz_config_algo_for(&z->cfg, z->mon[pointer_mon(z)].name,
                                                          (int)z->current_desktop + 1));
        for (int i = 0; i < NALGO; i++)
            add(p, K_RADIO, A_ALGO0 + i, !strcmp(cur, algo_ids[i]), algo_label(algo_ids[i]));
        break;
    }
    case M_SETTINGS:
        add(p, K_ACT, A_EDITOR, 0, _("Edit zones..."));
        add(p, K_ACT, A_PREFS, 0, _("Preferences..."));
        add(p, K_ACT, A_KEYS, 0, _("Shortcuts..."));
        add(p, K_ACT, A_RULES, 0, _("Application rules..."));
        add(p, K_ACT, A_MONITORS, 0, _("Monitors..."));
        break;
    case M_HELP:
        add(p, K_ACT, A_HELP_KEYS, 0, _("Available shortcuts..."));
        add(p, K_ACT, A_ABOUT, 0, _("About wmake-zones"));
        break;
    }
}

static void
title_bar(wzx *z, Drawable d, XftDraw *xd, int w, int h, const char *title)
{
    for (int i = 0; i < w; i++) {
        XSetForeground(z->dpy, dgc, wzx_pixel(z, wz_rgb_mix(z->theme.title_bg1, z->theme.title_bg2, i * 255 / (w > 1 ? w : 1))));
        XDrawLine(z->dpy, d, dgc, i, 0, i, h - 1);
    }
    wzx_draw_bevel(z, d, dgc, 0, 0, w, h, 1);
    xft_text(z, xd, z->font_bold, z->theme.title_fg, 6, (h + z->font_bold->ascent - z->font_bold->descent) / 2, title);
}

static void
item_back(wzx *z, Drawable d, int x, int y, int w, int h, int hl)
{
    if (hl) {
        XSetForeground(z->dpy, dgc, wzx_pixel(z, z->theme.hl_bg));
        XFillRectangle(z->dpy, d, dgc, x, y, (unsigned)w, (unsigned)h);
    } else {
        for (int i = 0; i < w; i++) {
            XSetForeground(z->dpy, dgc, wzx_pixel(z, wz_rgb_mix(z->theme.menu_bg1, z->theme.menu_bg2, i * 255 / (w > 1 ? w : 1))));
            XDrawLine(z->dpy, d, dgc, x + i, y, x + i, y + h - 1);
        }
    }
    wzx_draw_bevel(z, d, dgc, x, y, w, h, 1);
}

static void
draw_pop(wzx *z, mpop *p)
{
    if (!p->win)
        return;
    Pixmap buf = XCreatePixmap(z->dpy, p->win, (unsigned)p->w, (unsigned)p->h, (unsigned)DefaultDepth(z->dpy, z->screen));
    XftDraw *xd = XftDrawCreate(z->dpy, buf, DefaultVisual(z->dpy, z->screen), DefaultColormap(z->dpy, z->screen));
    title_bar(z, buf, xd, p->w, p->title_h, menu_title(p->menu));
    for (int i = 0; i < p->n; i++) {
        mitem *it = &p->items[i];
        int y = p->title_h + i * p->item_h;
        int hl = i == p->sel;
        item_back(z, buf, 0, y, p->w, p->item_h, hl);
        unsigned fg = hl ? z->theme.hl_fg : z->theme.menu_fg;
        int ty = y + (p->item_h + z->font->ascent - z->font->descent) / 2;
        xft_text(z, xd, z->font, fg, 8, ty, it->label);
        int cy = y + p->item_h / 2;
        if (it->kind == K_SUB) {
            /* Seta de submenu, como nos menus do wmaker. */
            XSetForeground(z->dpy, dgc, wzx_pixel(z, fg));
            for (int k = 0; k < 5; k++)
                XDrawLine(z->dpy, buf, dgc, p->w - 14 + k, cy - 4 + k, p->w - 14 + k, cy + 4 - k);
        } else if (it->kind == K_RADIO) {
            /* Indicador de estado à direita: afundado, aceso se ativo. */
            int bx = p->w - 18, by = cy - 5;
            XSetForeground(z->dpy, dgc, wzx_pixel(z, it->on ? 0x40c040 : 0x808080));
            XFillRectangle(z->dpy, buf, dgc, bx, by, 10, 10);
            wzx_draw_bevel(z, buf, dgc, bx - 1, by - 1, 12, 12, 0);
        }
    }
    XCopyArea(z->dpy, buf, p->win, dgc, 0, 0, (unsigned)p->w, (unsigned)p->h, 0, 0);
    XftDrawDestroy(xd);
    XFreePixmap(z->dpy, buf);
}

static Window
make_popup_window(wzx *z, int x, int y, int w, int h)
{
    XSetWindowAttributes a = { 0 };
    a.override_redirect = True;
    a.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | KeyPressMask;
    a.save_under = True;
    Window win = XCreateWindow(z->dpy, z->root, x, y, (unsigned)w, (unsigned)h, 0, CopyFromParent, InputOutput,
                               CopyFromParent, CWOverrideRedirect | CWEventMask | CWSaveUnder, &a);
    XClassHint ch = { "wmake-zones-menu", "WmakeZones" };
    XSetClassHint(z->dpy, win, &ch);
    XMapRaised(z->dpy, win);
    return win;
}

static void
measure(wzx *z, mpop *p)
{
    p->title_h = z->font_bold->height + 8;
    p->item_h = z->font->height + 8;
    int w = text_w(z, z->font_bold, menu_title(p->menu)) + 24;
    for (int i = 0; i < p->n; i++) {
        int tw = text_w(z, z->font, p->items[i].label) + 44;
        if (tw > w)
            w = tw;
    }
    p->w = w;
    p->h = p->title_h + p->n * p->item_h;
}

static void
sub_close(wzx *z)
{
    if (npops > 1 && pops[1].win) {
        XDestroyWindow(z->dpy, pops[1].win);
        pops[1].win = None;
    }
    if (npops > 1)
        npops = 1;
}

static void
popup_close(wzx *z)
{
    if (!z->popup)
        return;
    sub_close(z);
    XUngrabPointer(z->dpy, CurrentTime);
    XUngrabKeyboard(z->dpy, CurrentTime);
    XDestroyWindow(z->dpy, z->popup);
    z->popup = None;
    z->popup_kind = POP_NONE;
    pops[0].win = None;
    npops = 0;
    XFlush(z->dpy);
}

/* Submenu do item i do menu de cima, à direita dele (ou à esquerda). */
static void
sub_open(wzx *z, int i)
{
    mpop *top = &pops[0];
    if (npops > 1 && pops[1].menu == top->items[i].id)
        return;
    sub_close(z);
    mpop *p = &pops[1];
    memset(p, 0, sizeof *p);
    fill_menu(z, p, top->items[i].id);
    measure(z, p);
    p->sel = -1;
    int mon = wzx_monitor_at(z, top->x + top->w / 2, top->y);
    wz_rect g = z->mon[mon].geom;
    p->x = top->x + top->w + p->w <= g.x + g.w ? top->x + top->w - 1 : top->x - p->w + 1;
    p->y = top->y + top->title_h + i * top->item_h - p->title_h;
    if (p->y + p->h > g.y + g.h)
        p->y = g.y + g.h - p->h;
    if (p->y < g.y)
        p->y = g.y;
    p->win = make_popup_window(z, p->x, p->y, p->w, p->h);
    npops = 2;
    draw_pop(z, p);
}

static void
grid_draw(wzx *z)
{
    Pixmap buf = XCreatePixmap(z->dpy, z->popup, (unsigned)grid.w, (unsigned)grid.h,
                               (unsigned)DefaultDepth(z->dpy, z->screen));
    XftDraw *xd = XftDrawCreate(z->dpy, buf, DefaultVisual(z->dpy, z->screen), DefaultColormap(z->dpy, z->screen));
    title_bar(z, buf, xd, grid.w, grid.title_h, _("Layouts"));
    /* Fundo do menu também nas células vazias da última linha. */
    item_back(z, buf, 0, grid.title_h, grid.w, grid.h - grid.title_h, 0);
    const wz_layout_def *cur = current_layout(z);
    for (int i = 0; i < z->cfg.nlayouts; i++) {
        int cx = (i % grid.cols) * grid.cell_w, cy = grid.title_h + (i / grid.cols) * grid.cell_h;
        int hl = i == z->popup_sel;
        item_back(z, buf, cx, cy, grid.cell_w, grid.cell_h, hl);
        int is_cur = &z->cfg.layouts[i] == cur;
        wz_rect box = { cx + (grid.cell_w - 64) / 2, cy + 6, 64, 40 };
        XSetForeground(z->dpy, dgc, wzx_pixel(z, 0x1c2024));
        XFillRectangle(z->dpy, buf, dgc, box.x, box.y, (unsigned)box.w, (unsigned)box.h);
        wzx_draw_bevel(z, buf, dgc, box.x - 1, box.y - 1, box.w + 2, box.h + 2, 0);
        draw_mini(z, buf, dgc, z->cfg.layouts[i].tree, (wz_rect){ box.x + 2, box.y + 2, box.w - 4, box.h - 4 },
                  is_cur ? 0x7fb0e0 : 0xc2c0c5, -1, 0);
        char cut[64];
        snprintf(cut, sizeof cut, "%s", layout_label(&z->cfg.layouts[i]));
        while (strlen(cut) > 3 && text_w(z, z->font, cut) > grid.cell_w - 6) {
            size_t l = strlen(cut) - 1;
            while (l > 0 && (cut[l] & 0xc0) == 0x80)
                l--;
            cut[l] = 0;
            if (l > 3) {
                size_t k = l - 1;
                while (k > 0 && (cut[k] & 0xc0) == 0x80)
                    k--;
                snprintf(cut + k, sizeof cut - k, "…");
            }
        }
        int tw = text_w(z, z->font, cut);
        xft_text(z, xd, is_cur ? z->font_bold : z->font, hl ? z->theme.hl_fg : z->theme.menu_fg,
                 cx + (grid.cell_w - tw) / 2, cy + grid.cell_h - 7, cut);
    }
    XCopyArea(z->dpy, buf, z->popup, dgc, 0, 0, (unsigned)grid.w, (unsigned)grid.h, 0, 0);
    XftDrawDestroy(xd);
    XFreePixmap(z->dpy, buf);
}

static void
popup_redraw(wzx *z)
{
    if (!z->popup)
        return;
    /* O tema pode ter mudado (fontes fechadas e reabertas) com o menu aberto. */
    wzx_fonts(z);
    if (!z->font || !z->font_bold)
        return;
    if (z->popup_kind == POP_MENU) {
        for (int i = 0; i < npops; i++)
            draw_pop(z, &pops[i]);
    } else {
        grid_draw(z);
    }
    XFlush(z->dpy);
}

static void
popup_open(wzx *z, int kind)
{
    popup_close(z);
    balloon_close(z);
    tip_at = 0;
    wzx_fonts(z);
    if (!z->font || !z->font_bold)
        return; /* nenhuma fonte abriu: sem menu, sem travar */
    if (!dgc)
        dgc = XCreateGC(z->dpy, z->root, 0, NULL);
    z->popup_kind = kind;
    z->popup_sel = -1;
    int w, h, x, y;
    if (kind == POP_MENU) {
        mpop *p = &pops[0];
        memset(p, 0, sizeof *p);
        fill_menu(z, p, M_TOP);
        measure(z, p);
        p->sel = -1;
        w = p->w;
        h = p->h;
        z->popup_nitems = p->n;
    } else {
        grid.title_h = z->font_bold->height + 8;
        grid.cols = z->cfg.nlayouts > 8 ? 4 : 3;
        grid.cell_w = 104;
        grid.cell_h = 52 + z->font->height + 6;
        int rows = (z->cfg.nlayouts + grid.cols - 1) / grid.cols;
        grid.w = w = grid.cols * grid.cell_w;
        grid.h = h = grid.title_h + rows * grid.cell_h;
        z->popup_nitems = z->cfg.nlayouts;
    }
    place_beside(z, w, h, &x, &y);
    z->popup = make_popup_window(z, x, y, w, h);
    if (kind == POP_MENU) {
        pops[0].win = z->popup;
        pops[0].x = x;
        pops[0].y = y;
        npops = 1;
    }
    XGrabPointer(z->dpy, z->popup, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync,
                 GrabModeAsync, None, None, CurrentTime);
    XGrabKeyboard(z->dpy, z->popup, True, GrabModeAsync, GrabModeAsync, CurrentTime);
    popup_redraw(z);
}

/* Item sob o ponto (coordenadas da raiz): nível em *lvl. */
static int
menu_hit(int rx, int ry, int *lvl)
{
    for (int l = npops - 1; l >= 0; l--) {
        mpop *p = &pops[l];
        if (!p->win || rx < p->x || rx >= p->x + p->w || ry < p->y || ry >= p->y + p->h)
            continue;
        *lvl = l;
        int i = (ry - p->y - p->title_h) / p->item_h;
        return ry - p->y >= p->title_h && i >= 0 && i < p->n ? i : -1;
    }
    *lvl = -1;
    return -1;
}

static int
grid_hit(wzx *z, int x, int y)
{
    if (x < 0 || y < grid.title_h || x >= grid.w || y >= grid.h)
        return -1;
    int i = ((y - grid.title_h) / grid.cell_h) * grid.cols + x / grid.cell_w;
    return i >= 0 && i < z->cfg.nlayouts ? i : -1;
}

static void
run_detached(const char *prog, const char *arg)
{
    pid_t p = fork();
    if (p == 0) {
        if (fork() != 0)
            _exit(0);
        setsid();
        wzx_child_reset();
        execlp(prog, prog, arg, (char *)NULL);
        _exit(127);
    }
    if (p > 0)
        waitpid(p, NULL, 0);
}

static void
do_action(wzx *z, int id)
{
    char cmd[300];
    switch (id) {
    case A_UNDO: wzx_command(z, "undo", NULL, 0); break;
    case A_REDO: wzx_command(z, "redo", NULL, 0); break;
    case A_RESTORE: wzx_command(z, "restore", NULL, 0); break;
    case A_RESTORE_ALL: wzx_command(z, "restore-all", NULL, 0); break;
    case A_ARRANGE: wzx_command(z, "arrange", NULL, 0); break;
    case A_FLOAT: wzx_command(z, "float", NULL, 0); break;
    case A_EDITOR: wzx_command(z, "editor", NULL, 0); break;
    case A_PREFS: run_detached("wmake-zones-prefs", "--general"); break;
    case A_KEYS:
    case A_HELP_KEYS: run_detached("wmake-zones-prefs", "--keys"); break;
    case A_RULES: run_detached("wmake-zones-prefs", "--rules"); break;
    case A_MONITORS: run_detached("wmake-zones-prefs", "--monitors"); break;
    case A_ABOUT: run_detached("wmake-zones-prefs", "--about"); break;
    case A_MODE_ZONES: wzx_command(z, "mode zones", NULL, 0); break;
    case A_MODE_TILING: wzx_command(z, "mode tiling", NULL, 0); break;
    case A_MODE_OFF: wzx_command(z, "mode off", NULL, 0); break;
    case A_EMERGENCY:
        /* Volta ao Window Maker flutuante agora: desliga e devolve tudo. */
        wzx_command(z, "mode off", NULL, 0);
        wzx_command(z, "restore-all", NULL, 0);
        break;
    default:
        if (id >= A_ALGO0 && id < A_ALGO0 + NALGO) {
            snprintf(cmd, sizeof cmd, "algorithm %s", algo_ids[id - A_ALGO0]);
            wzx_command(z, cmd, NULL, 0);
        } else if (id >= A_LAYOUT0 && id < A_LAYOUT0 + z->cfg.nlayouts) {
            snprintf(cmd, sizeof cmd, "layout %s", z->cfg.layouts[id - A_LAYOUT0].id);
            wzx_command(z, cmd, NULL, 0);
        }
        break;
    }
}

static void
menu_activate(wzx *z, int lvl, int i)
{
    if (lvl < 0 || i < 0 || i >= pops[lvl].n)
        return;
    mitem it = pops[lvl].items[i];
    if (it.kind == K_SUB) {
        sub_open(z, i);
        return;
    }
    popup_close(z);
    do_action(z, it.id);
    wzx_dockapp_redraw(z);
    if (it.id >= A_MODE_ZONES || it.id == A_UNDO || it.id == A_REDO)
        balloon_show(z, 1500);
}

static void
menu_key(wzx *z, KeySym ks)
{
    int lvl = npops - 1;
    mpop *p = &pops[lvl];
    if (ks == XK_Escape) {
        if (lvl > 0)
            sub_close(z);
        else
            popup_close(z);
        return;
    }
    if (ks == XK_Left && lvl > 0) {
        sub_close(z);
        return;
    }
    if (ks == XK_Return || ks == XK_KP_Enter || ks == XK_space || ks == XK_Right) {
        if (p->sel >= 0 && (ks != XK_Right || p->items[p->sel].kind == K_SUB)) {
            menu_activate(z, lvl, p->sel);
            if (npops == 2 && lvl == 0)
                pops[1].sel = 0;
            popup_redraw(z);
        }
        return;
    }
    if (ks == XK_Down || ks == XK_Up) {
        int s = p->sel < 0 ? 0 : p->sel + (ks == XK_Down ? 1 : -1);
        if (s >= 0 && s < p->n)
            p->sel = s;
        popup_redraw(z);
    }
}

static void
grid_activate(wzx *z, int i)
{
    if (i < 0)
        return;
    char cmd[300];
    snprintf(cmd, sizeof cmd, "layout %s", z->cfg.layouts[i].id);
    popup_close(z);
    wzx_command(z, cmd, NULL, 0);
    wzx_dockapp_redraw(z);
    balloon_show(z, 1500);
}

/* ---- cliques no ladrilho ------------------------------------------------ */

static void
click(wzx *z, wz_click action, int step)
{
    switch (action) {
    case WZ_CLICK_MODE: wzx_command(z, "mode toggle", NULL, 0); break;
    case WZ_CLICK_ENABLE: wzx_command(z, "mode enable", NULL, 0); break;
    case WZ_CLICK_LAYOUTS: popup_open(z, POP_LAYOUTS); return;
    case WZ_CLICK_MENU: popup_open(z, POP_MENU); return;
    case WZ_CLICK_ZONES: wzx_command(z, "zones toggle", NULL, 0); break;
    case WZ_CLICK_AUTOTILE: wzx_command(z, "autotile toggle", NULL, 0); break;
    case WZ_CLICK_CYCLE: wzx_command(z, step < 0 ? "layout prev" : "layout next", NULL, 0); break;
    default: return;
    }
    wzx_dockapp_redraw(z);
    balloon_show(z, 1500);
}

int
wzx_dockapp_event(wzx *z, XEvent *e)
{
    Window w = e->xany.window;
    if (balloon && w == balloon)
        return 1;
    int on_menu = z->popup && z->popup_kind == POP_MENU && (w == pops[0].win || (npops > 1 && w == pops[1].win));
    if (on_menu) {
        switch (e->type) {
        case Expose:
            if (e->xexpose.count == 0)
                popup_redraw(z);
            break;
        case MotionNotify: {
            int lvl;
            int i = menu_hit(e->xmotion.x_root, e->xmotion.y_root, &lvl);
            if (lvl < 0)
                break;
            if (i != pops[lvl].sel) {
                pops[lvl].sel = i;
                if (lvl == 0 && i >= 0 && pops[0].items[i].kind == K_SUB)
                    sub_open(z, i);
                else if (lvl == 0 && i >= 0)
                    sub_close(z);
                popup_redraw(z);
            }
            break;
        }
        case ButtonPress: {
            int lvl;
            menu_hit(e->xbutton.x_root, e->xbutton.y_root, &lvl);
            if (lvl < 0)
                popup_close(z);
            break;
        }
        case ButtonRelease:
            if (e->xbutton.button <= 3) {
                int lvl;
                int i = menu_hit(e->xbutton.x_root, e->xbutton.y_root, &lvl);
                menu_activate(z, lvl, i);
            }
            break;
        case KeyPress:
            menu_key(z, XLookupKeysym(&e->xkey, 0));
            break;
        }
        return 1;
    }
    if (z->popup && w == z->popup) {
        switch (e->type) {
        case Expose:
            if (e->xexpose.count == 0)
                popup_redraw(z);
            break;
        case MotionNotify: {
            int i = grid_hit(z, e->xmotion.x, e->xmotion.y);
            if (i != z->popup_sel) {
                z->popup_sel = i;
                popup_redraw(z);
            }
            break;
        }
        case ButtonPress:
            if (e->xbutton.x < 0 || e->xbutton.y < 0 || e->xbutton.x >= grid.w || e->xbutton.y >= grid.h)
                popup_close(z);
            break;
        case ButtonRelease:
            if (e->xbutton.button <= 3)
                grid_activate(z, grid_hit(z, e->xbutton.x, e->xbutton.y));
            break;
        case KeyPress: {
            KeySym ks = XLookupKeysym(&e->xkey, 0);
            if (ks == XK_Escape)
                popup_close(z);
            else if (ks == XK_Return || ks == XK_KP_Enter || ks == XK_space)
                grid_activate(z, z->popup_sel);
            else if (ks == XK_Down || ks == XK_Up || ks == XK_Left || ks == XK_Right) {
                int d = ks == XK_Down ? grid.cols : ks == XK_Up ? -grid.cols : ks == XK_Right ? 1 : -1;
                int s = z->popup_sel < 0 ? 0 : z->popup_sel + d;
                if (s >= 0 && s < z->popup_nitems)
                    z->popup_sel = s;
                popup_redraw(z);
            }
            break;
        }
        }
        return 1;
    }
    if (w != z->dock_icon && w != z->dock_win)
        return 0;
    switch (e->type) {
    case Expose:
        if (e->xexpose.count == 0)
            wzx_dockapp_redraw(z);
        break;
    case LeaveNotify:
        tip_at = 0;
        if (balloon && !balloon_hide_at)
            balloon_close(z);
        break;
    case ButtonPress:
        tip_at = 0;
        switch (e->xbutton.button) {
        case 1: click(z, z->cfg.click[0], 0); break;
        case 2: click(z, z->cfg.click[1], 0); break;
        case 3: click(z, z->cfg.click[2], 0); break;
        case 4: click(z, z->cfg.click[3], -1); break;
        case 5: click(z, z->cfg.click[3], 1); break;
        }
        break;
    case DestroyNotify:
        if (w == z->dock_win) {
            /* Tirado do Dock e fechado: o daemon continua, sem ladrilho. */
            z->dockapp = 0;
            z->dock_win = z->dock_icon = None;
            balloon_close(z);
        }
        break;
    }
    return 1;
}

long
wzx_dockapp_due(wzx *z)
{
    (void)z;
    long due = -1;
    if (balloon_hide_at)
        due = balloon_hide_at;
    if (tip_at && (due < 0 || tip_at < due))
        due = tip_at;
    return due;
}

void
wzx_dockapp_popup_close(wzx *z)
{
    popup_close(z);
}

void
wzx_dockapp_timer(wzx *z, long now)
{
    if (balloon_hide_at && now >= balloon_hide_at)
        balloon_close(z);
    if (tip_at && now >= tip_at) {
        tip_at = 0;
        balloon_show(z, 0); /* dica: some ao sair do ladrilho */
    }
}
