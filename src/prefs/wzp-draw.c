/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Desenho no feitio do OpenStep (cairo): chanfros de um pixel, ladrilhos em
 * degradê, miniaturas de layouts e os ícones das seções, desenhados em
 * vetor para qualquer escala (sem depender do tema de ícones).
 */
#include "wzp.h"

#include <math.h>

void
wzp_rgb(cairo_t *cr, unsigned rgb, double a)
{
    cairo_set_source_rgba(cr, (rgb >> 16 & 0xff) / 255.0, (rgb >> 8 & 0xff) / 255.0, (rgb & 0xff) / 255.0, a);
}

static void
line(cairo_t *cr, double x1, double y1, double x2, double y2)
{
    cairo_move_to(cr, x1, y1);
    cairo_line_to(cr, x2, y2);
    cairo_stroke(cr);
}

void
wzp_bevel(cairo_t *cr, double x, double y, double w, double h, int raised)
{
    cairo_save(cr);
    cairo_set_line_width(cr, 1);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    x = floor(x) + 0.5;
    y = floor(y) + 0.5;
    w = floor(w) - 1;
    h = floor(h) - 1;
    wzp_rgb(cr, raised ? 0xffffff : 0x000000, 1);
    line(cr, x, y, x + w, y);
    line(cr, x, y, x, y + h);
    wzp_rgb(cr, raised ? 0x000000 : 0xffffff, 1);
    line(cr, x, y + h, x + w, y + h);
    line(cr, x + w, y, x + w, y + h);
    wzp_rgb(cr, 0x555555, 1);
    if (raised) {
        line(cr, x + 1, y + h - 1, x + w - 1, y + h - 1);
        line(cr, x + w - 1, y + 1, x + w - 1, y + h - 1);
    } else {
        line(cr, x + 1, y + 1, x + w - 1, y + 1);
        line(cr, x + 1, y + 1, x + 1, y + h - 1);
    }
    cairo_restore(cr);
}

void
wzp_tile(cairo_t *cr, double x, double y, double w, double h, unsigned c1, unsigned c2)
{
    cairo_pattern_t *p = cairo_pattern_create_linear(x, y, x + w, y + h);
    cairo_pattern_add_color_stop_rgb(p, 0, (c1 >> 16 & 0xff) / 255.0, (c1 >> 8 & 0xff) / 255.0, (c1 & 0xff) / 255.0);
    cairo_pattern_add_color_stop_rgb(p, 1, (c2 >> 16 & 0xff) / 255.0, (c2 >> 8 & 0xff) / 255.0, (c2 & 0xff) / 255.0);
    cairo_rectangle(cr, x, y, w, h);
    cairo_set_source(cr, p);
    cairo_fill(cr);
    cairo_pattern_destroy(p);
    wzp_bevel(cr, x, y, w, h, 1);
}

void
wzp_draw_layout(cairo_t *cr, const char *tree, double x, double y, double w, double h, int sel, unsigned hl,
                int numbers)
{
    wz_node *t = wz_parse(tree, NULL);
    if (!t)
        return;
    wz_rect zr[128];
    int gap = w > 200 ? 4 : 2;
    int n = wz_compute(t, (wz_rect){ (int)x, (int)y, (int)w, (int)h }, gap, 0, zr, 128);
    for (int i = 0; i < n && i < 128; i++) {
        unsigned c = i == sel ? hl : 0xc2c0c5;
        wzp_rgb(cr, c, 1);
        cairo_rectangle(cr, zr[i].x, zr[i].y, zr[i].w, zr[i].h);
        cairo_fill(cr);
        if (zr[i].w >= 4 && zr[i].h >= 4)
            wzp_bevel(cr, zr[i].x, zr[i].y, zr[i].w, zr[i].h, 1);
        if (numbers && zr[i].w > 30 && zr[i].h > 30) {
            double s = fmin(fmin(zr[i].w, zr[i].h) / 3.0, 48);
            double bx = zr[i].x + zr[i].w / 2.0 - s / 2, by = zr[i].y + zr[i].h / 2.0 - s / 2;
            wzp_tile(cr, floor(bx), floor(by), floor(s), floor(s), i == sel ? 0x505a5e : 0xa6a6b6,
                     i == sel ? 0x202a2e : 0x515561);
            char num[16];
            g_snprintf(num, sizeof num, "%d", i + 1);
            cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, s * 0.55);
            cairo_text_extents_t te;
            cairo_text_extents(cr, num, &te);
            double tx = bx + s / 2 - te.width / 2 - te.x_bearing, ty = by + s / 2 - te.height / 2 - te.y_bearing;
            wzp_rgb(cr, 0x000000, 0.8);
            cairo_move_to(cr, tx + 1, ty + 1);
            cairo_show_text(cr, num);
            wzp_rgb(cr, 0xffffff, 1);
            cairo_move_to(cr, tx, ty);
            cairo_show_text(cr, num);
            wz_node *leaf = wz_leaf_nth(t, i);
            if (leaf && leaf->name && zr[i].h > s + 40) {
                cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
                cairo_set_font_size(cr, 12);
                const char *nm = _(leaf->name);
                cairo_text_extents(cr, nm, &te);
                wzp_rgb(cr, 0x000000, 1);
                cairo_move_to(cr, zr[i].x + zr[i].w / 2.0 - te.width / 2 - te.x_bearing, by + s + 18);
                cairo_show_text(cr, nm);
            }
        }
    }
    wz_free(t);
}

/* Monitor de mesa: moldura cinza chanfrada, tela escura, pé. */
static void
monitor(cairo_t *cr, double x, double y, double w, double h)
{
    wzp_rgb(cr, 0xb8b6bb, 1);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
    wzp_bevel(cr, x, y, w, h, 1);
    wzp_rgb(cr, 0x1c2024, 1);
    cairo_rectangle(cr, x + 3, y + 3, w - 6, h - 7);
    cairo_fill(cr);
    wzp_bevel(cr, x + 2, y + 2, w - 4, h - 5, 0);
    wzp_rgb(cr, 0x8a888d, 1);
    cairo_rectangle(cr, x + w / 2 - w / 8, y + h, w / 4, 3);
    cairo_fill(cr);
    cairo_rectangle(cr, x + w / 2 - w / 4, y + h + 3, w / 2, 2);
    cairo_fill(cr);
}

void
wzp_draw_icon(cairo_t *cr, int kind, double size)
{
    double s = size / 48.0;
    cairo_save(cr);
    cairo_scale(cr, s, s);
    switch (kind) {
    case 0: /* Geral: monitor com interruptor */
        monitor(cr, 4, 6, 40, 30);
        wzp_rgb(cr, 0x40c040, 1);
        cairo_rectangle(cr, 12, 15, 24, 12);
        cairo_fill(cr);
        wzp_rgb(cr, 0xd0d0d0, 1);
        cairo_rectangle(cr, 25, 16, 10, 10);
        cairo_fill(cr);
        wzp_bevel(cr, 25, 16, 10, 10, 1);
        break;
    case 1: /* Zonas: quadrantes, um destacado */
        monitor(cr, 4, 6, 40, 30);
        wzp_draw_layout(cr, "V(H(1,1),H(1,1))", 8, 10, 32, 21, 1, 0x7fb0e0, 0);
        break;
    case 2: /* Mosaico: principal + pilha */
        monitor(cr, 4, 6, 40, 30);
        wzp_draw_layout(cr, "H(3,2:V(1,1,1))", 8, 10, 32, 21, 0, 0xe0a030, 0);
        break;
    case 3: /* Monitores: dois */
        monitor(cr, 2, 10, 26, 20);
        monitor(cr, 22, 6, 24, 30);
        wzp_draw_layout(cr, "H(1,1)", 6, 14, 18, 13, -1, 0, 0);
        wzp_draw_layout(cr, "V(1,1)", 26, 10, 16, 23, -1, 0, 0);
        break;
    case 4: { /* Aparência: paleta */
        wzp_rgb(cr, 0xc2c0c5, 1);
        cairo_arc(cr, 24, 24, 18, 0, 2 * M_PI);
        cairo_fill_preserve(cr);
        wzp_rgb(cr, 0x000000, 1);
        cairo_set_line_width(cr, 1.2);
        cairo_stroke(cr);
        unsigned cols[] = { 0x505a5e, 0x7fb0e0, 0xe0a030, 0x40c040, 0xffffff };
        for (int i = 0; i < 5; i++) {
            double a = -2.2 + i * 0.85;
            wzp_rgb(cr, cols[i], 1);
            cairo_arc(cr, 24 + cos(a) * 11, 24 + sin(a) * 11, 4, 0, 2 * M_PI);
            cairo_fill_preserve(cr);
            wzp_rgb(cr, 0x000000, 1);
            cairo_set_line_width(cr, 0.8);
            cairo_stroke(cr);
        }
        break;
    }
    case 5: /* Atalhos: tecla "Z" */
        wzp_tile(cr, 8, 10, 32, 28, 0xe8e6ea, 0x9a989d);
        wzp_rgb(cr, 0x000000, 1);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 18);
        cairo_move_to(cr, 17, 31);
        cairo_show_text(cr, "Z");
        break;
    case 6: /* Regras: folha com linhas e uma janela */
        wzp_rgb(cr, 0xf4f2f6, 1);
        cairo_rectangle(cr, 10, 4, 28, 38);
        cairo_fill(cr);
        wzp_bevel(cr, 10, 4, 28, 38, 1);
        wzp_rgb(cr, 0x555555, 1);
        cairo_set_line_width(cr, 1);
        for (int i = 0; i < 4; i++)
            line(cr, 14, 12 + i * 7, 34, 12 + i * 7);
        wzp_tile(cr, 22, 26, 20, 16, 0x505a5e, 0x202a2e);
        break;
    case 7: { /* Avançado: engrenagem */
        wzp_rgb(cr, 0x8a888d, 1);
        for (int i = 0; i < 8; i++) {
            cairo_save(cr);
            cairo_translate(cr, 24, 24);
            cairo_rotate(cr, i * M_PI / 4);
            cairo_rectangle(cr, -3, -19, 6, 8);
            cairo_fill(cr);
            cairo_restore(cr);
        }
        cairo_arc(cr, 24, 24, 13, 0, 2 * M_PI);
        cairo_fill_preserve(cr);
        wzp_rgb(cr, 0x000000, 1);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
        wzp_rgb(cr, 0xd8d6db, 1);
        cairo_arc(cr, 24, 24, 5, 0, 2 * M_PI);
        cairo_fill(cr);
        break;
    }
    default: /* Logo: ladrilho do Dock com as zonas */
        wzp_tile(cr, 0, 0, 48, 48, 0xa6a6b6, 0x515561);
        wzp_rgb(cr, 0x1c2024, 1);
        cairo_rectangle(cr, 5, 5, 38, 30);
        cairo_fill(cr);
        wzp_bevel(cr, 4, 4, 40, 32, 0);
        wzp_draw_layout(cr, "H(3:V(3,1),2)", 7, 7, 34, 26, 0, 0x7fb0e0, 0);
        wzp_rgb(cr, 0x1c2024, 1);
        cairo_rectangle(cr, 5, 38, 38, 6);
        cairo_fill(cr);
        wzp_rgb(cr, 0x40d040, 1);
        cairo_rectangle(cr, 8, 39, 4, 4);
        cairo_fill(cr);
        break;
    }
    cairo_restore(cr);
}

static gboolean
icon_draw(GtkWidget *w, cairo_t *cr, gpointer data)
{
    int size = gtk_widget_get_allocated_width(w);
    wzp_draw_icon(cr, GPOINTER_TO_INT(data), size);
    return TRUE;
}

GtkWidget *
wzp_icon_widget(int kind, int size)
{
    GtkWidget *d = gtk_drawing_area_new();
    gtk_widget_set_size_request(d, size, size);
    g_signal_connect(d, "draw", G_CALLBACK(icon_draw), GINT_TO_POINTER(kind));
    return d;
}
