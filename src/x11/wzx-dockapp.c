/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Dockapp wmake-zones: ladrilho de 64x64 no Dock/Clip do Window Maker,
 * dentro do próprio daemon (um processo só, sem serviço duplicado).
 *
 *   clique esquerdo   painel de layouts (miniaturas)
 *   clique direito    menu no estilo do Window Maker
 *   roda              layout anterior/seguinte
 *
 * O ladrilho mostra a miniatura do layout ativo numa "tela" afundada e,
 * embaixo, um LED com o modo: apagado, ZONAS (verde) ou MOSAICO (âmbar).
 * Só redesenha quando algo muda; parado, não acorda.
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

enum {
    MI_ZONES, MI_AUTOTILE, MI_LAYOUT_INFO, MI_NEXT, MI_CHOOSE, MI_ARRANGE, MI_RESTORE, MI_UNDO,
    MI_EDITOR, MI_PREFS, MI_ABOUT, MI_COUNT
};

static Pixmap face;
static GC dgc;

/* ---- fonte de pixels 3x5 (LED) ------------------------------------------ */

static const struct {
    char c;
    unsigned char rows[5];
} glyphs[] = {
    { 'A', { 2, 5, 7, 5, 5 } }, { 'C', { 3, 4, 4, 4, 3 } }, { 'D', { 6, 5, 5, 5, 6 } },
    { 'E', { 7, 4, 6, 4, 7 } }, { 'F', { 7, 4, 6, 4, 4 } }, { 'I', { 7, 2, 2, 2, 7 } },
    { 'L', { 4, 4, 4, 4, 7 } }, { 'M', { 5, 7, 7, 5, 5 } }, { 'N', { 6, 5, 5, 5, 5 } },
    { 'O', { 2, 5, 5, 5, 2 } }, { 'S', { 3, 4, 2, 1, 6 } }, { 'T', { 7, 2, 2, 2, 2 } },
    { 'Z', { 7, 1, 2, 4, 7 } }, { '+', { 0, 2, 7, 2, 0 } }, { ' ', { 0, 0, 0, 0, 0 } },
};

static void
pixtext(wzx *z, Drawable d, const char *s, int x, int y, unsigned rgb)
{
    XSetForeground(z->dpy, dgc, wzx_pixel(z, rgb));
    for (; *s; s++, x += 4)
        for (size_t g = 0; g < sizeof glyphs / sizeof *glyphs; g++) {
            if (glyphs[g].c != *s)
                continue;
            for (int r = 0; r < 5; r++)
                for (int c = 0; c < 3; c++)
                    if (glyphs[g].rows[r] & (4 >> c))
                        XDrawPoint(z->dpy, d, dgc, x + c, y + r);
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

static const wz_layout_def *
current_layout(wzx *z)
{
    Window r, c;
    int x = 0, y = 0, wx, wy;
    unsigned m;
    XQueryPointer(z->dpy, z->root, &r, &c, &x, &y, &wx, &wy, &m);
    int mon = wzx_monitor_at(z, x, y);
    return wz_config_layout_for(&z->cfg, z->mon[mon].name, (int)z->current_desktop + 1);
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
    wz_rect scr = { 5, 5, 54, 40 };
    XSetForeground(z->dpy, dgc, wzx_pixel(z, 0x1c2024));
    XFillRectangle(z->dpy, face, dgc, scr.x, scr.y, (unsigned)scr.w, (unsigned)scr.h);
    wzx_draw_bevel(z, face, dgc, scr.x - 1, scr.y - 1, scr.w + 2, scr.h + 2, 0);
    const wz_layout_def *l = current_layout(z);
    int on = z->cfg.zones || z->cfg.autotile;
    unsigned tile = on ? 0xc2c0c5 : 0x6a6a70;
    if (l)
        draw_mini(z, face, dgc, l->tree, (wz_rect){ scr.x + 3, scr.y + 3, scr.w - 6, scr.h - 6 }, tile,
                  on ? 0 : -1, z->cfg.autotile ? 0xe0a030 : 0x7fb0e0);
    /* LED e rótulo do modo. */
    unsigned led = z->cfg.autotile ? 0xf0b030 : z->cfg.zones ? 0x40d040 : 0x505050;
    const char *label = z->cfg.autotile && z->cfg.zones ? "Z+TILE" : z->cfg.autotile ? "TILE" : z->cfg.zones ? "ZONES" : "OFF";
    XSetForeground(z->dpy, dgc, wzx_pixel(z, 0x1c2024));
    XFillRectangle(z->dpy, face, dgc, 5, 49, 54, 10);
    wzx_draw_bevel(z, face, dgc, 4, 48, 56, 12, 0);
    XSetForeground(z->dpy, dgc, wzx_pixel(z, led));
    XFillRectangle(z->dpy, face, dgc, 8, 51, 5, 5);
    pixtext(z, face, label, 17, 51, on ? 0xe8e8e8 : 0x8a8a8a);
    XCopyArea(z->dpy, face, z->dock_icon, dgc, 0, 0, TILE, TILE, 0, 0);
    XFlush(z->dpy);
}

void
wzx_dockapp_create(wzx *z)
{
    XSetWindowAttributes a = { 0 };
    a.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | StructureNotifyMask;
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

/* ---- popups ------------------------------------------------------------- */

typedef struct {
    int x, y, w, h;
    int title_h, item_h, cols, cell_w, cell_h;
} popup_geom;

static popup_geom pg;

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

static const char *
menu_label(wzx *z, int i, char *buf, size_t len)
{
    switch (i) {
    case MI_ZONES: return _("Zones");
    case MI_AUTOTILE: return _("Auto Tiling");
    case MI_LAYOUT_INFO:
        snprintf(buf, len, _("Layout: %s"), layout_label(current_layout(z)));
        return buf;
    case MI_NEXT: return _("Next layout");
    case MI_CHOOSE: return _("Choose layout...");
    case MI_ARRANGE: return _("Arrange windows now");
    case MI_RESTORE: return _("Restore windows");
    case MI_UNDO: return _("Undo last arrangement");
    case MI_EDITOR: return _("Edit zones...");
    case MI_PREFS: return _("Preferences...");
    case MI_ABOUT: return _("About wmake-zones");
    }
    return "";
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
popup_redraw(wzx *z)
{
    if (!z->popup)
        return;
    Pixmap buf = XCreatePixmap(z->dpy, z->popup, (unsigned)pg.w, (unsigned)pg.h, (unsigned)DefaultDepth(z->dpy, z->screen));
    XftDraw *xd = XftDrawCreate(z->dpy, buf, DefaultVisual(z->dpy, z->screen), DefaultColormap(z->dpy, z->screen));
    char tmp[256];
    if (z->popup_kind == POP_MENU) {
        title_bar(z, buf, xd, pg.w, pg.title_h, "wmake-zones");
        for (int i = 0; i < MI_COUNT; i++) {
            int y = pg.title_h + i * pg.item_h;
            int hl = i == z->popup_sel && i != MI_LAYOUT_INFO;
            item_back(z, buf, 0, y, pg.w, pg.item_h, hl);
            unsigned fg = i == MI_LAYOUT_INFO ? z->theme.disabled_fg : hl ? z->theme.hl_fg : z->theme.menu_fg;
            int ty = y + (pg.item_h + z->font->ascent - z->font->descent) / 2;
            xft_text(z, xd, z->font, fg, 8, ty, menu_label(z, i, tmp, sizeof tmp));
            if (i == MI_ZONES || i == MI_AUTOTILE) {
                /* indicador de estado à direita, como nos menus do wmaker */
                int on = i == MI_ZONES ? z->cfg.zones : z->cfg.autotile;
                int bx = pg.w - 18, by = y + (pg.item_h - 10) / 2;
                XSetForeground(z->dpy, dgc, wzx_pixel(z, on ? 0x40c040 : 0x808080));
                XFillRectangle(z->dpy, buf, dgc, bx, by, 10, 10);
                wzx_draw_bevel(z, buf, dgc, bx - 1, by - 1, 12, 12, 0);
            }
        }
    } else {
        title_bar(z, buf, xd, pg.w, pg.title_h, _("Layouts"));
        const wz_layout_def *cur = current_layout(z);
        for (int i = 0; i < z->cfg.nlayouts; i++) {
            int cx = (i % pg.cols) * pg.cell_w, cy = pg.title_h + (i / pg.cols) * pg.cell_h;
            int hl = i == z->popup_sel;
            item_back(z, buf, cx, cy, pg.cell_w, pg.cell_h, hl);
            int is_cur = &z->cfg.layouts[i] == cur;
            wz_rect box = { cx + (pg.cell_w - 64) / 2, cy + 6, 64, 40 };
            XSetForeground(z->dpy, dgc, wzx_pixel(z, 0x1c2024));
            XFillRectangle(z->dpy, buf, dgc, box.x, box.y, (unsigned)box.w, (unsigned)box.h);
            wzx_draw_bevel(z, buf, dgc, box.x - 1, box.y - 1, box.w + 2, box.h + 2, 0);
            draw_mini(z, buf, dgc, z->cfg.layouts[i].tree, (wz_rect){ box.x + 2, box.y + 2, box.w - 4, box.h - 4 },
                      is_cur ? 0x7fb0e0 : 0xc2c0c5, -1, 0);
            const char *lab = layout_label(&z->cfg.layouts[i]);
            char cut[64];
            snprintf(cut, sizeof cut, "%s", lab);
            while (strlen(cut) > 3 && text_w(z, z->font, cut) > pg.cell_w - 6) {
                size_t l = strlen(cut);
                /* corta por caractere UTF-8 e põe reticências */
                l--;
                while (l > 0 && (cut[l] & 0xc0) == 0x80)
                    l--;
                cut[l] = 0;
                if (l > 3)
                    snprintf(cut + l - 1, sizeof cut - l + 1, "…");
            }
            int tw = text_w(z, z->font, cut);
            xft_text(z, xd, is_cur ? z->font_bold : z->font, hl ? z->theme.hl_fg : z->theme.menu_fg,
                     cx + (pg.cell_w - tw) / 2, cy + pg.cell_h - 7, cut);
        }
    }
    XCopyArea(z->dpy, buf, z->popup, dgc, 0, 0, (unsigned)pg.w, (unsigned)pg.h, 0, 0);
    XftDrawDestroy(xd);
    XFreePixmap(z->dpy, buf);
    XFlush(z->dpy);
}

static void
popup_close(wzx *z)
{
    if (!z->popup)
        return;
    XUngrabPointer(z->dpy, CurrentTime);
    XUngrabKeyboard(z->dpy, CurrentTime);
    XDestroyWindow(z->dpy, z->popup);
    z->popup = None;
    z->popup_kind = POP_NONE;
    XFlush(z->dpy);
}

static void
popup_open(wzx *z, int kind)
{
    popup_close(z);
    wzx_fonts(z);
    if (!dgc)
        dgc = XCreateGC(z->dpy, z->root, 0, NULL);
    z->popup_kind = kind;
    z->popup_sel = -1;
    pg.title_h = z->font_bold->height + 8;
    if (kind == POP_MENU) {
        char tmp[256];
        int w = text_w(z, z->font_bold, "wmake-zones") + 24;
        for (int i = 0; i < MI_COUNT; i++) {
            int tw = text_w(z, z->font, menu_label(z, i, tmp, sizeof tmp)) + 40;
            if (tw > w)
                w = tw;
        }
        pg.item_h = z->font->height + 8;
        pg.w = w;
        pg.h = pg.title_h + MI_COUNT * pg.item_h;
        z->popup_nitems = MI_COUNT;
    } else {
        pg.cols = z->cfg.nlayouts > 8 ? 4 : 3;
        pg.cell_w = 104;
        pg.cell_h = 52 + z->font->height + 6;
        int rows = (z->cfg.nlayouts + pg.cols - 1) / pg.cols;
        pg.w = pg.cols * pg.cell_w;
        pg.h = pg.title_h + rows * pg.cell_h;
        z->popup_nitems = z->cfg.nlayouts;
    }
    /* Ao lado do ladrilho, para dentro da tela. */
    int ix = 0, iy = 0;
    Window child;
    XTranslateCoordinates(z->dpy, z->dock_icon, z->root, 0, 0, &ix, &iy, &child);
    int mon = wzx_monitor_at(z, ix + TILE / 2, iy + TILE / 2);
    wz_rect g = z->mon[mon].geom;
    pg.x = ix + TILE + pg.w <= g.x + g.w ? ix + TILE : ix - pg.w;
    pg.y = iy;
    if (pg.y + pg.h > g.y + g.h)
        pg.y = g.y + g.h - pg.h;
    if (pg.y < g.y)
        pg.y = g.y;
    if (pg.x < g.x)
        pg.x = g.x;
    XSetWindowAttributes a = { 0 };
    a.override_redirect = True;
    a.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | KeyPressMask;
    a.save_under = True;
    z->popup = XCreateWindow(z->dpy, z->root, pg.x, pg.y, (unsigned)pg.w, (unsigned)pg.h, 0, CopyFromParent,
                             InputOutput, CopyFromParent, CWOverrideRedirect | CWEventMask | CWSaveUnder, &a);
    XClassHint ch = { "wmake-zones-menu", "WmakeZones" };
    XSetClassHint(z->dpy, z->popup, &ch);
    XMapRaised(z->dpy, z->popup);
    XGrabPointer(z->dpy, z->popup, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync,
                 GrabModeAsync, None, None, CurrentTime);
    XGrabKeyboard(z->dpy, z->popup, True, GrabModeAsync, GrabModeAsync, CurrentTime);
    popup_redraw(z);
}

static int
popup_hit(wzx *z, int x, int y)
{
    if (x < 0 || y < pg.title_h || x >= pg.w || y >= pg.h)
        return -1;
    if (z->popup_kind == POP_MENU) {
        int i = (y - pg.title_h) / pg.item_h;
        return i >= 0 && i < MI_COUNT ? i : -1;
    }
    int i = ((y - pg.title_h) / pg.cell_h) * pg.cols + x / pg.cell_w;
    return i >= 0 && i < z->cfg.nlayouts ? i : -1;
}

static void
menu_activate(wzx *z, int i)
{
    popup_close(z);
    switch (i) {
    case MI_ZONES: wzx_command(z, "zones toggle", NULL, 0); break;
    case MI_AUTOTILE: wzx_command(z, "autotile toggle", NULL, 0); break;
    case MI_NEXT: wzx_command(z, "layout next", NULL, 0); break;
    case MI_CHOOSE: popup_open(z, POP_LAYOUTS); return;
    case MI_ARRANGE: wzx_command(z, "arrange", NULL, 0); break;
    case MI_RESTORE: wzx_command(z, "restore-all", NULL, 0); break;
    case MI_UNDO: wzx_command(z, "undo", NULL, 0); break;
    case MI_EDITOR: wzx_command(z, "editor", NULL, 0); break;
    case MI_PREFS: wzx_command(z, "prefs", NULL, 0); break;
    case MI_ABOUT: {
        pid_t p = fork();
        if (p == 0) {
            if (fork() != 0)
                _exit(0);
            execlp("wmake-zones-prefs", "wmake-zones-prefs", "--about", (char *)NULL);
            _exit(127);
        }
        if (p > 0)
            waitpid(p, NULL, 0);
        break;
    }
    default: break;
    }
    wzx_dockapp_redraw(z);
}

static void
popup_activate(wzx *z, int i)
{
    if (i < 0)
        return;
    if (z->popup_kind == POP_MENU) {
        if (i != MI_LAYOUT_INFO)
            menu_activate(z, i);
        return;
    }
    char cmd[300];
    snprintf(cmd, sizeof cmd, "layout %s", z->cfg.layouts[i].id);
    popup_close(z);
    wzx_command(z, cmd, NULL, 0);
    wzx_dockapp_redraw(z);
}

int
wzx_dockapp_event(wzx *z, XEvent *e)
{
    Window w = e->xany.window;
    if (z->popup && w == z->popup) {
        switch (e->type) {
        case Expose:
            if (e->xexpose.count == 0)
                popup_redraw(z);
            break;
        case MotionNotify: {
            int i = popup_hit(z, e->xmotion.x, e->xmotion.y);
            if (i != z->popup_sel) {
                z->popup_sel = i;
                popup_redraw(z);
            }
            break;
        }
        case ButtonPress:
            if (popup_hit(z, e->xbutton.x, e->xbutton.y) < 0 &&
                (e->xbutton.x < 0 || e->xbutton.y < 0 || e->xbutton.x >= pg.w || e->xbutton.y >= pg.h))
                popup_close(z);
            break;
        case ButtonRelease:
            if (e->xbutton.button <= 3)
                popup_activate(z, popup_hit(z, e->xbutton.x, e->xbutton.y));
            break;
        case KeyPress: {
            KeySym ks = XLookupKeysym(&e->xkey, 0);
            int step = z->popup_kind == POP_LAYOUTS ? pg.cols : 1;
            if (ks == XK_Escape)
                popup_close(z);
            else if (ks == XK_Return || ks == XK_KP_Enter || ks == XK_space)
                popup_activate(z, z->popup_sel);
            else if (ks == XK_Down || ks == XK_Up || ks == XK_Left || ks == XK_Right) {
                int d = ks == XK_Down ? step : ks == XK_Up ? -step : ks == XK_Right ? 1 : -1;
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
    case ButtonPress:
        if (e->xbutton.button == 1)
            popup_open(z, POP_LAYOUTS);
        else if (e->xbutton.button == 3)
            popup_open(z, POP_MENU);
        else if (e->xbutton.button == 4)
            wzx_command(z, "layout prev", NULL, 0);
        else if (e->xbutton.button == 5)
            wzx_command(z, "layout next", NULL, 0);
        wzx_dockapp_redraw(z);
        break;
    case DestroyNotify:
        if (w == z->dock_win) {
            /* Tirado do Dock e fechado: o daemon continua, sem ladrilho. */
            z->dockapp = 0;
            z->dock_win = z->dock_icon = None;
        }
        break;
    }
    return 1;
}
