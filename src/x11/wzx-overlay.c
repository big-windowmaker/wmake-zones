/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Overlay das zonas e seletor por teclado.
 *
 * Uma janela override-redirect do tamanho do monitor, criada ao mostrar e
 * destruída ao esconder (nada fica alocado no servidor em repouso). A forma
 * de entrada é vazia: o ponteiro atravessa o overlay, que nunca rouba
 * cliques nem foco. Com compositor (picom) a janela é ARGB e as zonas são
 * painéis translúcidos; sem compositor, a forma da janela recorta só as
 * bordas, os números e uma trama 50% na zona escolhida (o "translúcido"
 * clássico do X).
 *
 * Visual: ladrilhos do OpenStep. Zona = painel com chanfro (luz em cima e à
 * esquerda, sombra embaixo e à direita) na cor do menu do tema; número num
 * ladrilho como o de um ícone do Dock; a zona escolhida ganha a cor da barra
 * de título da janela em foco.
 */
#define _GNU_SOURCE
#include "wz-alloc.h"
#include "wzx.h"
#include "wz-i18n.h"

#include <X11/Xatom.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/shape.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ---- tema e fontes ------------------------------------------------------ */

static time_t theme_mtime;

void
wzx_theme_reload(wzx *z)
{
    char path[1024];
    const char *root = getenv("GNUSTEP_USER_ROOT"), *home = getenv("HOME");
    if (root && *root)
        snprintf(path, sizeof path, "%s/Defaults/WindowMaker", root);
    else
        snprintf(path, sizeof path, "%s/GNUstep/Defaults/WindowMaker", home ? home : "");
    struct stat st;
    time_t mt = stat(path, &st) == 0 ? st.st_mtime : 0;
    if (z->theme_loaded && mt == theme_mtime)
        return;
    theme_mtime = mt;
    wz_theme_load(&z->theme, path);
    z->theme_loaded = 1;
    if (z->font) {
        XftFontClose(z->dpy, z->font);
        XftFontClose(z->dpy, z->font_bold);
        XftFontClose(z->dpy, z->font_big);
        z->font = z->font_bold = z->font_big = NULL;
    }
}

static void
family_of(const char *pattern, char *out, size_t len)
{
    const char *colon = strchr(pattern, ':');
    size_t l = colon ? (size_t)(colon - pattern) : strlen(pattern);
    if (l >= len)
        l = len - 1;
    memcpy(out, pattern, l);
    out[l] = 0;
    if (!*out)
        snprintf(out, len, "sans");
}

void
wzx_fonts(wzx *z)
{
    wzx_theme_reload(z);
    if (z->font)
        return;
    char fam[128], pat[256];
    family_of(z->theme.menu_font, fam, sizeof fam);
    z->font = XftFontOpenName(z->dpy, z->screen, z->theme.menu_font);
    z->font_bold = XftFontOpenName(z->dpy, z->screen, z->theme.title_font);
    snprintf(pat, sizeof pat, "%s:bold:pixelsize=26", fam);
    z->font_big = XftFontOpenName(z->dpy, z->screen, pat);
    if (!z->font)
        z->font = XftFontOpenName(z->dpy, z->screen, "sans:pixelsize=12");
    if (!z->font_bold)
        z->font_bold = XftFontOpenName(z->dpy, z->screen, "sans:bold:pixelsize=12");
    if (!z->font_big)
        z->font_big = XftFontOpenName(z->dpy, z->screen, "sans:bold:pixelsize=26");
}

unsigned long
wzx_pixel(wzx *z, unsigned rgb)
{
    Visual *v = DefaultVisual(z->dpy, z->screen);
    if (v->class != TrueColor && v->class != DirectColor) {
        XColor c = { .red = (unsigned short)((rgb >> 16 & 0xff) * 257),
                     .green = (unsigned short)((rgb >> 8 & 0xff) * 257),
                     .blue = (unsigned short)((rgb & 0xff) * 257) };
        XAllocColor(z->dpy, DefaultColormap(z->dpy, z->screen), &c);
        return c.pixel;
    }
    unsigned long p = 0;
    unsigned long masks[3] = { v->red_mask, v->green_mask, v->blue_mask };
    unsigned comp[3] = { rgb >> 16 & 0xff, rgb >> 8 & 0xff, rgb & 0xff };
    for (int i = 0; i < 3; i++) {
        unsigned long m = masks[i];
        int shift = 0, bits = 0;
        while (m && !(m & 1)) {
            m >>= 1;
            shift++;
        }
        while (m & 1) {
            m >>= 1;
            bits++;
        }
        unsigned long c = bits >= 8 ? (unsigned long)comp[i] << (bits - 8) : comp[i] >> (8 - bits);
        p |= c << shift;
    }
    return p;
}

void
wzx_draw_bevel(wzx *z, Drawable d, GC gc, int x, int y, int w, int h, int raised)
{
    unsigned light = 0xffffff, dark = 0x555555, black = 0x000000;
    XSetForeground(z->dpy, gc, wzx_pixel(z, raised ? light : black));
    XDrawLine(z->dpy, d, gc, x, y, x + w - 1, y);
    XDrawLine(z->dpy, d, gc, x, y, x, y + h - 1);
    XSetForeground(z->dpy, gc, wzx_pixel(z, raised ? black : light));
    XDrawLine(z->dpy, d, gc, x, y + h - 1, x + w - 1, y + h - 1);
    XDrawLine(z->dpy, d, gc, x + w - 1, y, x + w - 1, y + h - 1);
    XSetForeground(z->dpy, gc, wzx_pixel(z, dark));
    if (raised) {
        XDrawLine(z->dpy, d, gc, x + 1, y + h - 2, x + w - 2, y + h - 2);
        XDrawLine(z->dpy, d, gc, x + w - 2, y + 1, x + w - 2, y + h - 2);
    } else {
        XDrawLine(z->dpy, d, gc, x + 1, y + 1, x + w - 2, y + 1);
        XDrawLine(z->dpy, d, gc, x + 1, y + 1, x + 1, y + h - 2);
    }
}

/* ---- desenho ------------------------------------------------------------ */

static XRenderColor
rc(unsigned rgb, int alpha255)
{
    /* Cores pré-multiplicadas, como o XRender espera. */
    unsigned a = (unsigned)alpha255 * 257;
    XRenderColor c;
    c.red = (unsigned short)((rgb >> 16 & 0xff) * 257 * a / 65535);
    c.green = (unsigned short)((rgb >> 8 & 0xff) * 257 * a / 65535);
    c.blue = (unsigned short)((rgb & 0xff) * 257 * a / 65535);
    c.alpha = (unsigned short)a;
    return c;
}

static void
fill(wzx *z, int op, wz_rect r, unsigned rgb, int alpha)
{
    XRenderColor c = rc(rgb, alpha);
    XRenderFillRectangle(z->dpy, op, z->ov.pict, &c, r.x, r.y, (unsigned)(r.w > 0 ? r.w : 0),
                         (unsigned)(r.h > 0 ? r.h : 0));
}

/* Coordenadas locais (o overlay começa na origem do monitor). */
static wz_rect
local(wzx *z, wz_rect r)
{
    wz_rect g = z->mon[z->ov.mon].geom;
    return (wz_rect){ r.x - g.x, r.y - g.y, r.w, r.h };
}

static void
text_center(wzx *z, XftFont *f, unsigned rgb, const char *s, int cx, int cy, int shadow)
{
    if (!f || !z->ov.xft || !s || !*s)
        return;
    XGlyphInfo gi;
    XftTextExtentsUtf8(z->dpy, f, (const FcChar8 *)s, (int)strlen(s), &gi);
    int x = cx - gi.xOff / 2;
    int y = cy + (f->ascent - f->descent) / 2;
    XftColor col;
    XRenderColor xr;
    if (shadow) {
        xr = rc(0x000000, 200);
        XftColorAllocValue(z->dpy, z->ov.visual, z->ov.cmap, &xr, &col);
        XftDrawStringUtf8(z->ov.xft, &col, f, x + 1, y + 1, (const FcChar8 *)s, (int)strlen(s));
        XftColorFree(z->dpy, z->ov.visual, z->ov.cmap, &col);
    }
    xr = rc(rgb, 255);
    XftColorAllocValue(z->dpy, z->ov.visual, z->ov.cmap, &xr, &col);
    XftDrawStringUtf8(z->ov.xft, &col, f, x, y, (const FcChar8 *)s, (int)strlen(s));
    XftColorFree(z->dpy, z->ov.visual, z->ov.cmap, &col);
}

/* Cor do destaque: a escolhida nas preferências ou a da barra de título. */
static unsigned
hl1(wzx *z)
{
    return z->cfg.highlight != WZ_COLOR_AUTO ? z->cfg.highlight : z->theme.ftitle1;
}

static unsigned
hl2(wzx *z)
{
    return z->cfg.highlight != WZ_COLOR_AUTO ? wz_rgb_mix(z->cfg.highlight, 0, 90) : z->theme.ftitle2;
}

static int
badge_size(wz_rect r)
{
    int m = r.w < r.h ? r.w : r.h;
    int s = m / 4;
    return s < 28 ? 28 : s > 64 ? 64 : s;
}

/* Painel chanfrado em ARGB. */
static void
panel_argb(wzx *z, wz_rect r, unsigned base, int alpha, int raised, int thick)
{
    fill(z, PictOpSrc, r, base, alpha);
    unsigned lt = raised ? 0xffffff : 0x000000, dk = raised ? 0x000000 : 0xffffff;
    for (int t = 0; t < thick; t++) {
        int a1 = t == 0 ? 230 : 150, a2 = t == 0 ? 220 : 140;
        fill(z, PictOpOver, (wz_rect){ r.x + t, r.y + t, r.w - 2 * t, 1 }, lt, a1);
        fill(z, PictOpOver, (wz_rect){ r.x + t, r.y + t, 1, r.h - 2 * t }, lt, a1);
        fill(z, PictOpOver, (wz_rect){ r.x + t, r.y + r.h - 1 - t, r.w - 2 * t, 1 }, dk, a2);
        fill(z, PictOpOver, (wz_rect){ r.x + r.w - 1 - t, r.y + t, 1, r.h - 2 * t }, dk, a2);
    }
}

static void
badge_argb(wzx *z, wz_rect zr, int idx, const char *name, int hl)
{
    int s = badge_size(zr);
    int cx = zr.x + zr.w / 2, cy = zr.y + zr.h / 2;
    wz_rect b = { cx - s / 2, cy - s / 2, s, s };
    /* Ladrilho de ícone: degradê diagonal do IconBack, aproximado em faixas. */
    unsigned c1 = hl ? hl1(z) : z->theme.icon1, c2 = hl ? hl2(z) : z->theme.icon2;
    for (int i = 0; i < s; i += 2)
        fill(z, PictOpSrc, (wz_rect){ b.x, b.y + i, s, i + 2 <= s ? 2 : 1 }, wz_rgb_mix(c1, c2, i * 255 / s), 245);
    fill(z, PictOpOver, (wz_rect){ b.x, b.y, s, 1 }, 0xffffff, 230);
    fill(z, PictOpOver, (wz_rect){ b.x, b.y, 1, s }, 0xffffff, 230);
    fill(z, PictOpOver, (wz_rect){ b.x, b.y + s - 1, s, 1 }, 0x000000, 230);
    fill(z, PictOpOver, (wz_rect){ b.x + s - 1, b.y, 1, s }, 0x000000, 230);
    if (z->cfg.show_numbers || z->ov.mode == 2) {
        char num[16];
        snprintf(num, sizeof num, "%d", idx + 1);
        text_center(z, z->font_big, 0xffffff, num, cx, cy, 1);
    }
    if (name && *name && zr.h > s + 40)
        text_center(z, z->font_bold, hl ? z->theme.ftitle_fg : z->theme.menu_fg, _(name), cx, b.y + s + 14, 0);
}

static void
draw_zone_argb(wzx *z, int i, int hl)
{
    wz_rect r = local(z, z->ov.zones[i]);
    int op = z->cfg.opacity;
    if (hl)
        panel_argb(z, r, hl1(z), 90 + op * 13 / 10 > 235 ? 235 : 90 + op * 13 / 10, 1, 3);
    else
        panel_argb(z, r, z->theme.menu_bg1, 30 + op, 1, 2);
    badge_argb(z, r, i, z->ov.names[i], hl);
}

static void
draw_hint_argb(wzx *z)
{
    if (z->ov.mode != 2 || !z->font)
        return;
    const char *msg = _("1–9 or arrows to choose · Enter snaps · Tab changes the layout · Esc cancels");
    XGlyphInfo gi;
    XftTextExtentsUtf8(z->dpy, z->font, (const FcChar8 *)msg, (int)strlen(msg), &gi);
    wz_rect g = z->mon[z->ov.mon].geom;
    int w = gi.xOff + 24, h = z->font->height + 10;
    wz_rect bar = { (g.w - w) / 2, g.h - h - 48, w, h };
    /* Barra de título de menu do Window Maker. */
    for (int i = 0; i < w; i += 2)
        fill(z, PictOpSrc, (wz_rect){ bar.x + i, bar.y, 2, h }, wz_rgb_mix(z->theme.title_bg1, z->theme.title_bg2, i * 255 / w), 250);
    fill(z, PictOpOver, (wz_rect){ bar.x, bar.y, w, 1 }, 0xffffff, 200);
    fill(z, PictOpOver, (wz_rect){ bar.x, bar.y + h - 1, w, 1 }, 0x000000, 220);
    text_center(z, z->font, z->theme.title_fg, msg, bar.x + w / 2, bar.y + h / 2, 0);
}

/* ---- painel de layouts (arrasto) ---------------------------------------- */

/* Pintura comum aos dois modos: XRender com compositor, GC sem ele. */
static void
pfill(wzx *z, GC gc, wz_rect r, unsigned rgb, int alpha)
{
    if (r.w <= 0 || r.h <= 0)
        return;
    wz_rect l = local(z, r);
    if (z->ov.argb) {
        fill(z, alpha >= 255 ? PictOpSrc : PictOpOver, l, rgb, alpha);
        return;
    }
    XSetForeground(z->dpy, gc, wzx_pixel(z, rgb));
    XFillRectangle(z->dpy, z->ov.win, gc, l.x, l.y, (unsigned)l.w, (unsigned)l.h);
}

static void
pbevel(wzx *z, GC gc, wz_rect r, int raised)
{
    unsigned lt = raised ? 0xffffff : 0x000000, dk = raised ? 0x000000 : 0xffffff;
    pfill(z, gc, (wz_rect){ r.x, r.y, r.w, 1 }, lt, 255);
    pfill(z, gc, (wz_rect){ r.x, r.y, 1, r.h }, lt, 255);
    pfill(z, gc, (wz_rect){ r.x, r.y + r.h - 1, r.w, 1 }, dk, 255);
    pfill(z, gc, (wz_rect){ r.x + r.w - 1, r.y, 1, r.h }, dk, 255);
    /* Segunda linha escura, como os botões do WPrefs. */
    pfill(z, gc, raised ? (wz_rect){ r.x + 1, r.y + r.h - 2, r.w - 2, 1 } : (wz_rect){ r.x + 1, r.y + 1, r.w - 2, 1 },
          0x555555, 255);
    pfill(z, gc, raised ? (wz_rect){ r.x + r.w - 2, r.y + 1, 1, r.h - 2 } : (wz_rect){ r.x + 1, r.y + 1, 1, r.h - 2 },
          0x555555, 255);
}

/* Degradê horizontal em faixas de 2 pixels. */
static void
pgrad(wzx *z, GC gc, wz_rect r, unsigned c1, unsigned c2)
{
    for (int i = 0; i < r.w; i += 2)
        pfill(z, gc, (wz_rect){ r.x + i, r.y, i + 2 <= r.w ? 2 : 1, r.h }, wz_rgb_mix(c1, c2, i * 255 / (r.w > 1 ? r.w : 1)),
              255);
}

static void
text_at(wzx *z, XftFont *f, unsigned rgb, const char *s, int x, int y)
{
    if (!f || !z->ov.xft || !s || !*s)
        return;
    wz_rect l = local(z, (wz_rect){ x, y, 0, 0 });
    XftColor col;
    XRenderColor xr = rc(rgb, 255);
    XftColorAllocValue(z->dpy, z->ov.visual, z->ov.cmap, &xr, &col);
    XftDrawStringUtf8(z->ov.xft, &col, f, l.x, l.y, (const FcChar8 *)s, (int)strlen(s));
    XftColorFree(z->dpy, z->ov.visual, z->ov.cmap, &col);
}

static int
text_width(wzx *z, XftFont *f, const char *s)
{
    XGlyphInfo gi;
    XftTextExtentsUtf8(z->dpy, f, (const FcChar8 *)s, (int)strlen(s), &gi);
    return gi.xOff;
}

/* Corta o texto (por caractere UTF-8) até caber, com reticências. */
static void
fit_text(wzx *z, XftFont *f, char *buf, size_t len, const char *src, int maxw)
{
    snprintf(buf, len, "%s", src);
    while (strlen(buf) > 3 && text_width(z, f, buf) > maxw) {
        size_t l = strlen(buf) - 1;
        while (l > 0 && (buf[l] & 0xc0) == 0x80)
            l--;
        buf[l] = 0;
        if (l > 3) {
            size_t k = l - 1;
            while (k > 0 && (buf[k] & 0xc0) == 0x80)
                k--;
            snprintf(buf + k, len - k, "…");
        }
    }
}

/* Zonas da miniatura i, em coordenadas da raiz. */
static int
strip_minis(wzx *z, int i, wz_rect *out, int max)
{
    wz_node *t = wz_parse(z->ov.strip_tree[i], NULL);
    if (!t)
        return 0;
    wz_rect b = z->ov.strip_thumb[i];
    int n = wz_compute(t, (wz_rect){ b.x + 3, b.y + 3, b.w - 6, b.h - 6 }, 2, 0, out, max);
    wz_free(t);
    return n > max ? max : n;
}

void
wzx_strip_clear(wzx *z)
{
    for (int i = 0; i < z->ov.strip_n; i++) {
        free(z->ov.strip_id[i]);
        free(z->ov.strip_tree[i]);
        free(z->ov.strip_label[i]);
        z->ov.strip_id[i] = z->ov.strip_tree[i] = z->ov.strip_label[i] = NULL;
    }
    z->ov.strip_n = 0;
    z->ov.strip = 0;
    z->ov.strip_hover = z->ov.strip_mini = z->ov.strip_sel = -1;
}

void
wzx_strip_build(wzx *z, int mon, int bottom)
{
    wzx_strip_clear(z);
    wzx_fonts(z);
    if (!z->font || !z->font_bold || mon < 0 || mon >= z->nmon)
        return;
    wzx_update_workarea(z);
    wz_rect work = z->mon[mon].work, geom = z->mon[mon].geom;
    /* Escala pela fonte do tema: acompanha o Xft.dpi (HiDPI). */
    double sc = z->font->height / 15.0;
    sc = sc < 1 ? 1 : sc > 3 ? 3 : sc;
    int cell_w = (int)(100 * sc), thumb_w = (int)(78 * sc);
    int thumb_h = geom.w > 0 ? thumb_w * geom.h / geom.w : thumb_w * 9 / 16;
    if (thumb_h < (int)(30 * sc))
        thumb_h = (int)(30 * sc);
    if (thumb_h > (int)(66 * sc))
        thumb_h = (int)(66 * sc);
    int label_h = z->font->height + 4, pad = (int)(6 * sc);
    int cell_h = pad + thumb_h + 4 + label_h + pad / 2;
    int title_h = z->font_bold->height + 8;
    const wz_layout_def *ls[WZX_STRIP_MAX];
    int n = wz_config_drag_layouts(&z->cfg, ls, WZX_STRIP_MAX);
    int fit = (work.w - 32 - 2 * pad) / cell_w;
    if (n > fit)
        n = fit;
    if (n <= 0)
        return;
    int w = n * cell_w + 2 * pad, h = title_h + cell_h + 2 * pad;
    int x = work.x + (work.w - w) / 2;
    int y = bottom ? work.y + work.h - h - (int)(16 * sc) : work.y + (int)(16 * sc);
    z->ov.strip_box = (wz_rect){ x, y, w, h };
    z->ov.strip_title_h = title_h;
    for (int i = 0; i < n; i++) {
        z->ov.strip_id[i] = wz_xstrdup(ls[i]->id);
        z->ov.strip_tree[i] = wz_xstrdup(ls[i]->tree);
        const char *lab = ls[i]->label ? ls[i]->label : wz_builtin_label(ls[i]->id);
        z->ov.strip_label[i] = wz_xstrdup(lab ? (ls[i]->label ? lab : _(lab)) : ls[i]->id);
        wz_rect c = { x + pad + i * cell_w, y + title_h + pad, cell_w, cell_h };
        z->ov.strip_cell[i] = c;
        z->ov.strip_thumb[i] = (wz_rect){ c.x + (cell_w - thumb_w) / 2, c.y + pad, thumb_w, thumb_h };
    }
    z->ov.strip_n = n;
    z->ov.strip = 1;
}

int
wzx_strip_hit(wzx *z, int px, int py, int *mini)
{
    if (mini)
        *mini = -1;
    if (!z->ov.strip || !wz_rect_contains(z->ov.strip_box, px, py))
        return -1;
    for (int i = 0; i < z->ov.strip_n; i++) {
        if (!wz_rect_contains(z->ov.strip_cell[i], px, py))
            continue;
        if (mini && wz_rect_contains(z->ov.strip_thumb[i], px, py)) {
            wz_rect m[64];
            int k = strip_minis(z, i, m, 64);
            for (int j = 0; j < k; j++)
                if (wz_rect_contains(m[j], px, py))
                    *mini = j;
        }
        return i;
    }
    return -2;
}

static void
draw_strip(wzx *z, GC gc)
{
    if (!z->ov.strip || z->ov.strip_n <= 0 || !z->font || !z->font_bold)
        return;
    wz_rect b = z->ov.strip_box;
    int th = z->ov.strip_title_h;
    /* Barra de título do Window Maker e corpo de menu, com chanfro. */
    pgrad(z, gc, (wz_rect){ b.x, b.y, b.w, th }, z->theme.title_bg1, z->theme.title_bg2);
    pbevel(z, gc, (wz_rect){ b.x, b.y, b.w, th }, 1);
    char buf[160];
    fit_text(z, z->font_bold, buf, sizeof buf, _("Window layout: choose a layout, then a zone"), b.w - 16);
    text_at(z, z->font_bold, z->theme.title_fg, buf, b.x + 8, b.y + (th + z->font_bold->ascent - z->font_bold->descent) / 2);
    wz_rect body = { b.x, b.y + th, b.w, b.h - th };
    pgrad(z, gc, body, z->theme.menu_bg1, z->theme.menu_bg2);
    pbevel(z, gc, body, 1);
    for (int i = 0; i < z->ov.strip_n; i++) {
        wz_rect c = z->ov.strip_cell[i];
        int sel = i == z->ov.strip_sel, hov = i == z->ov.strip_hover;
        wz_rect btn = { c.x + 2, c.y + 2, c.w - 4, c.h - 4 };
        if (sel) {
            /* Botão afundado: o layout escolhido. */
            pfill(z, gc, btn, wz_rgb_mix(z->theme.menu_bg1, 0x000000, 40), 255);
            pbevel(z, gc, btn, 0);
        } else if (hov) {
            pfill(z, gc, btn, z->theme.hl_bg, 255);
            pbevel(z, gc, btn, 1);
        }
        wz_rect t = z->ov.strip_thumb[i];
        pfill(z, gc, t, 0x1c2024, 255);
        pbevel(z, gc, (wz_rect){ t.x - 1, t.y - 1, t.w + 2, t.h + 2 }, 0);
        wz_rect m[64];
        int k = strip_minis(z, i, m, 64);
        for (int j = 0; j < k; j++) {
            int on = (hov || sel) && j == z->ov.strip_mini && hov;
            unsigned col = on ? hl1(z) : sel ? wz_rgb_mix(hl1(z), 0xffffff, 120) : 0xc2c0c5;
            pfill(z, gc, m[j], col, 255);
            if (m[j].w >= 4 && m[j].h >= 4) {
                pfill(z, gc, (wz_rect){ m[j].x, m[j].y, m[j].w, 1 }, 0xffffff, 255);
                pfill(z, gc, (wz_rect){ m[j].x, m[j].y, 1, m[j].h }, 0xffffff, 255);
                pfill(z, gc, (wz_rect){ m[j].x, m[j].y + m[j].h - 1, m[j].w, 1 }, 0x303030, 255);
                pfill(z, gc, (wz_rect){ m[j].x + m[j].w - 1, m[j].y, 1, m[j].h }, 0x303030, 255);
            }
        }
        fit_text(z, sel ? z->font_bold : z->font, buf, sizeof buf, z->ov.strip_label[i], c.w - 8);
        XftFont *f = sel ? z->font_bold : z->font;
        int tw = text_width(z, f, buf);
        text_at(z, f, hov && !sel ? z->theme.hl_fg : z->theme.menu_fg, buf, c.x + (c.w - tw) / 2,
                t.y + t.h + 4 + f->ascent);
    }
}

void
wzx_strip_set(wzx *z, int hover, int mini, int sel)
{
    if (hover == z->ov.strip_hover && mini == z->ov.strip_mini && sel == z->ov.strip_sel)
        return;
    z->ov.strip_hover = hover;
    z->ov.strip_mini = mini;
    z->ov.strip_sel = sel;
    wzx_overlay_redraw(z);
}

static void
redraw_argb(wzx *z)
{
    wz_rect g = z->mon[z->ov.mon].geom;
    fill(z, PictOpSrc, (wz_rect){ 0, 0, g.w, g.h }, 0, 0);
    for (int i = 0; i < z->ov.nzones; i++)
        draw_zone_argb(z, i, 0);
    if (z->ov.has_target) {
        wz_rect t = local(z, z->ov.target);
        if (z->ov.hl_b >= 0)
            panel_argb(z, t, hl1(z), 120 + z->cfg.opacity, 1, 3);
        else
            draw_zone_argb(z, z->ov.hl_a, 1);
        if (z->ov.hl_b >= 0) {
            badge_argb(z, local(z, z->ov.zones[z->ov.hl_a]), z->ov.hl_a, z->ov.names[z->ov.hl_a], 1);
            badge_argb(z, local(z, z->ov.zones[z->ov.hl_b]), z->ov.hl_b, z->ov.names[z->ov.hl_b], 1);
        }
    }
    draw_hint_argb(z);
    draw_strip(z, None);
}

/* Sem compositor: janela comum recortada pela forma. A forma só muda quando
 * o destaque muda (mudar a forma gera Expose; refazê-la no Expose seria um
 * laço). No Expose só se pinta. */
static void
redraw_plain(wzx *z, int reshape)
{
    wz_rect g = z->mon[z->ov.mon].geom;
    /* Sem forma nova, a máscara vai para um pixmap de 1x1 descartável. */
    Pixmap mask = XCreatePixmap(z->dpy, z->ov.win, reshape ? (unsigned)g.w : 1, reshape ? (unsigned)g.h : 1, 1);
    GC mgc = XCreateGC(z->dpy, mask, 0, NULL);
    XSetForeground(z->dpy, mgc, 0);
    XFillRectangle(z->dpy, mask, mgc, 0, 0, (unsigned)g.w, (unsigned)g.h);
    XSetForeground(z->dpy, mgc, 1);
    GC gc = XCreateGC(z->dpy, z->ov.win, 0, NULL);
    for (int i = 0; i < z->ov.nzones; i++) {
        wz_rect r = local(z, z->ov.zones[i]);
        int hl = z->ov.has_target && (i == z->ov.hl_a || i == z->ov.hl_b);
        int th = hl ? 5 : 3;
        XSetForeground(z->dpy, gc, wzx_pixel(z, hl ? hl1(z) : z->theme.menu_bg1));
        XFillRectangle(z->dpy, z->ov.win, gc, r.x, r.y, (unsigned)r.w, (unsigned)r.h);
        for (int t = 0; t < th; t++)
            XDrawRectangle(z->dpy, mask, mgc, r.x + t, r.y + t, (unsigned)(r.w - 1 - 2 * t), (unsigned)(r.h - 1 - 2 * t));
        wzx_draw_bevel(z, z->ov.win, gc, r.x, r.y, r.w, r.h, 1);
        int s = badge_size(r);
        int bx = r.x + r.w / 2 - s / 2, by = r.y + r.h / 2 - s / 2;
        XSetForeground(z->dpy, gc, wzx_pixel(z, hl ? hl2(z) : z->theme.icon2));
        XFillRectangle(z->dpy, z->ov.win, gc, bx, by, (unsigned)s, (unsigned)s);
        wzx_draw_bevel(z, z->ov.win, gc, bx, by, s, s, 1);
        XFillRectangle(z->dpy, mask, mgc, bx, by, (unsigned)s, (unsigned)s);
        if (z->cfg.show_numbers || z->ov.mode == 2) {
            char num[16];
            snprintf(num, sizeof num, "%d", i + 1);
            text_center(z, z->font_big, 0xffffff, num, r.x + r.w / 2, r.y + r.h / 2, 1);
        }
    }
    if (z->ov.has_target) {
        /* Área final em listras horizontais (uma linha a cada três): poucas
         * caixas na forma. Uma trama de pontos viraria centenas de milhares
         * de retângulos de 1 pixel e travaria o servidor. */
        wz_rect t = local(z, z->ov.target);
        for (int y = t.y + 6; y < t.y + t.h - 6; y += 3)
            XFillRectangle(z->dpy, mask, mgc, t.x, y, (unsigned)t.w, 1);
        for (int k = 0; k < 6; k++)
            XDrawRectangle(z->dpy, mask, mgc, t.x + k, t.y + k, (unsigned)(t.w - 1 - 2 * k), (unsigned)(t.h - 1 - 2 * k));
    }
    if (z->ov.strip) {
        wz_rect sb = local(z, z->ov.strip_box);
        XFillRectangle(z->dpy, mask, mgc, sb.x, sb.y, (unsigned)sb.w, (unsigned)sb.h);
        draw_strip(z, gc);
    }
    if (reshape)
        XShapeCombineMask(z->dpy, z->ov.win, ShapeBounding, 0, 0, mask, ShapeSet);
    XFreeGC(z->dpy, gc);
    XFreeGC(z->dpy, mgc);
    XFreePixmap(z->dpy, mask);
}

static void
redraw_full(wzx *z, int reshape)
{
    if (!z->ov.shown)
        return;
    if (z->ov.argb)
        redraw_argb(z);
    else
        redraw_plain(z, reshape);
    XFlush(z->dpy);
}

static void
redraw(wzx *z)
{
    redraw_full(z, 1);
}

void
wzx_overlay_redraw(wzx *z)
{
    redraw_full(z, 1);
}

void
wzx_overlay_expose(wzx *z)
{
    redraw_full(z, 0);
}

void
wzx_overlay_set_zones(wzx *z, const wz_rect *zones, char **names, int n)
{
    for (int i = 0; i < z->ov.nzones; i++) {
        free(z->ov.names[i]);
        z->ov.names[i] = NULL;
    }
    if (n > WZX_MAX_ZONES)
        n = WZX_MAX_ZONES;
    z->ov.nzones = n;
    for (int i = 0; i < n; i++) {
        z->ov.zones[i] = zones[i];
        z->ov.names[i] = names && names[i] ? wz_xstrdup(names[i]) : NULL;
    }
    z->ov.hl_a = z->ov.hl_b = -1;
    z->ov.has_target = 0;
    if (z->ov.shown)
        redraw(z);
}

void
wzx_overlay_show(wzx *z, int mon, int mode)
{
    if (z->ov.shown && z->ov.mon == mon) {
        z->ov.mode = mode;
        redraw(z);
        return;
    }
    if (z->ov.shown)
        wzx_overlay_hide(z);
    wzx_fonts(z);
    z->ov.mon = mon;
    z->ov.mode = mode;
    wz_rect g = z->mon[mon].geom;
    XVisualInfo vi;
    XSetWindowAttributes a = { 0 };
    unsigned long mask = CWOverrideRedirect | CWBorderPixel | CWBackPixel | CWColormap | CWEventMask;
    a.override_redirect = True;
    a.event_mask = ExposureMask;
    z->ov.argb = wzx_compositing(z) && XMatchVisualInfo(z->dpy, z->screen, 32, TrueColor, &vi);
    if (z->ov.argb) {
        z->ov.visual = vi.visual;
        z->ov.depth = 32;
        z->ov.cmap = XCreateColormap(z->dpy, z->root, vi.visual, AllocNone);
        a.colormap = z->ov.cmap;
        a.background_pixel = 0;
        a.border_pixel = 0;
    } else {
        z->ov.visual = DefaultVisual(z->dpy, z->screen);
        z->ov.depth = DefaultDepth(z->dpy, z->screen);
        z->ov.cmap = DefaultColormap(z->dpy, z->screen);
        a.colormap = z->ov.cmap;
        a.background_pixel = wzx_pixel(z, z->theme.menu_bg1);
        a.border_pixel = 0;
    }
    z->ov.win = XCreateWindow(z->dpy, z->root, g.x, g.y, (unsigned)g.w, (unsigned)g.h, 0, z->ov.depth, InputOutput,
                              z->ov.visual, mask, &a);
    /* Nome e classe para o picom e para diagnóstico; sem sombra nem fade. */
    XClassHint ch = { "wmake-zones-overlay", "WmakeZones" };
    XSetClassHint(z->dpy, z->ov.win, &ch);
    XStoreName(z->dpy, z->ov.win, "wmake-zones");
    if (z->has_shape) {
        XShapeCombineRectangles(z->dpy, z->ov.win, ShapeInput, 0, 0, NULL, 0, ShapeSet, Unsorted);
        if (!z->ov.argb) {
            /* Começa sem nada visível; o redraw monta a forma. */
            XShapeCombineRectangles(z->dpy, z->ov.win, ShapeBounding, 0, 0, NULL, 0, ShapeSet, Unsorted);
        }
    }
    if (z->ov.argb) {
        XRenderPictFormat *fmt = XRenderFindVisualFormat(z->dpy, z->ov.visual);
        z->ov.pict = XRenderCreatePicture(z->dpy, z->ov.win, fmt, 0, NULL);
    }
    z->ov.xft = XftDrawCreate(z->dpy, z->ov.win, z->ov.visual, z->ov.cmap);
    XMapRaised(z->dpy, z->ov.win);
    z->ov.shown = 1;
    redraw(z);
}

void
wzx_overlay_highlight(wzx *z, int a, int b, const wz_rect *target)
{
    z->ov.hl_a = a;
    z->ov.hl_b = b;
    z->ov.has_target = a >= 0;
    if (target)
        z->ov.target = *target;
    redraw(z);
}

void
wzx_overlay_hide(wzx *z)
{
    if (!z->ov.shown)
        return;
    if (z->ov.xft)
        XftDrawDestroy(z->ov.xft);
    if (z->ov.pict)
        XRenderFreePicture(z->dpy, z->ov.pict);
    XDestroyWindow(z->dpy, z->ov.win);
    if (z->ov.argb)
        XFreeColormap(z->dpy, z->ov.cmap);
    z->ov.xft = NULL;
    z->ov.pict = None;
    z->ov.win = None;
    z->ov.shown = 0;
    z->ov.has_target = 0;
    z->ov.hl_a = z->ov.hl_b = -1;
    z->ov_hide_at = 0;
    wzx_strip_clear(z);
    XFlush(z->dpy);
}

/* ---- seletor por teclado (Super+Z) --------------------------------------- */

static wzx_win *
picker_win(wzx *z)
{
    return wzx_find(z, z->picker_target);
}

static void
picker_load(wzx *z)
{
    wzx_win *w = picker_win(z);
    if (!w)
        return;
    int mon = wzx_monitor_of(z, w->outer);
    wz_rect zones[WZX_MAX_ZONES];
    char *names[WZX_MAX_ZONES] = { 0 };
    int n = wzx_zones_for(z, mon, zones, names, WZX_MAX_ZONES);
    if (n <= 0) {
        wzx_picker_close(z);
        return;
    }
    /* Outro monitor ou outro modo (uma prévia do editor aberta): o overlay
     * do seletor é sempre um novo, com a barra de dicas e sem prazo. */
    if (z->ov.shown && (z->ov.mon != mon || z->ov.mode != 2))
        wzx_overlay_hide(z);
    wzx_overlay_set_zones(z, zones, names, n);
    for (int i = 0; i < n; i++)
        free(names[i]);
    if (!z->ov.shown)
        wzx_overlay_show(z, mon, 2);
    long cur = wzx_get_zone_prop(z, w);
    z->picker_sel = cur >= 0 && cur < n ? (int)cur
                                        : wz_zone_at(zones, n, w->outer.x + w->outer.w / 2, w->outer.y + w->outer.h / 2);
    if (z->picker_sel < 0 || z->picker_sel >= n)
        z->picker_sel = 0;
    wzx_overlay_highlight(z, z->picker_sel, -1, &z->ov.zones[z->picker_sel]);
}

void
wzx_picker_open(wzx *z)
{
    if (z->picker || z->drag != DRAG_IDLE)
        return;
    wzx_win *w = wzx_find(z, z->active);
    if (!w || !wzx_snappable(z, w)) {
        wzx_log("seletor: nenhuma janela ativa que possa ser encaixada");
        return;
    }
    if (XGrabKeyboard(z->dpy, z->root, False, GrabModeAsync, GrabModeAsync, CurrentTime) != GrabSuccess) {
        wzx_log("seletor: teclado ocupado por outro programa");
        return;
    }
    /* O ponteiro também, para clicar numa zona; sem ele, só teclado. */
    XGrabPointer(z->dpy, z->root, False, ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    z->picker = 1;
    z->picker_target = w->client;
    wzx_refresh_geom(z, w);
    picker_load(z);
}

void
wzx_picker_close(wzx *z)
{
    if (!z->picker)
        return;
    z->picker = 0;
    XUngrabKeyboard(z->dpy, CurrentTime);
    XUngrabPointer(z->dpy, CurrentTime);
    wzx_overlay_hide(z);
}

static void
picker_apply(wzx *z, int idx)
{
    wzx_win *w = picker_win(z);
    if (w && idx >= 0 && idx < z->ov.nzones) {
        wz_rect pre;
        if (!wzx_get_restore(z, w, &pre))
            wzx_set_restore(z, w, w->outer);
        wzx_snap(z, w, z->ov.zones[idx], idx, 1);
    }
    wzx_picker_close(z);
}

int
wzx_picker_key(wzx *z, XKeyEvent *e)
{
    if (!z->picker)
        return 0;
    KeySym ks = XLookupKeysym(e, 0);
    if (ks >= XK_1 && ks <= XK_9) {
        int i = (int)(ks - XK_1);
        if (i < z->ov.nzones)
            picker_apply(z, i);
        return 1;
    }
    if (ks >= XK_KP_1 && ks <= XK_KP_9) {
        int i = (int)(ks - XK_KP_1);
        if (i < z->ov.nzones)
            picker_apply(z, i);
        return 1;
    }
    int dir = -1;
    switch (ks) {
    case XK_Escape:
        wzx_picker_close(z);
        return 1;
    case XK_Return:
    case XK_KP_Enter:
    case XK_space:
        picker_apply(z, z->picker_sel);
        return 1;
    case XK_Tab:
    case XK_ISO_Left_Tab:
        /* O layout que muda é o do monitor mostrado, não o do ponteiro. */
        if (picker_win(z))
            wzx_cycle_layout(z, (e->state & ShiftMask) || ks == XK_ISO_Left_Tab ? -1 : 1,
                             wzx_monitor_of(z, picker_win(z)->outer));
        picker_load(z);
        return 1;
    case XK_Left: case XK_h: dir = WZ_LEFT; break;
    case XK_Right: case XK_l: dir = WZ_RIGHT; break;
    case XK_Up: case XK_k: dir = WZ_UP; break;
    case XK_Down: case XK_j: dir = WZ_DOWN; break;
    default:
        return 1;
    }
    int n = wz_neighbor(z->ov.zones, z->ov.nzones, z->picker_sel, dir);
    if (n >= 0) {
        z->picker_sel = n;
        wzx_overlay_highlight(z, n, -1, &z->ov.zones[n]);
    }
    return 1;
}

int
wzx_picker_button(wzx *z, XButtonEvent *e)
{
    if (!z->picker)
        return 0;
    for (int i = 0; i < z->ov.nzones; i++)
        if (wz_rect_contains(z->ov.zones[i], e->x_root, e->y_root)) {
            picker_apply(z, i);
            return 1;
        }
    wzx_picker_close(z);
    return 1;
}
