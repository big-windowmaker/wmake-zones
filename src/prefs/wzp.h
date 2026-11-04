/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * wmake-zones-prefs: preferências e editor de zonas (GTK 3), no feitio do
 * WPrefs. Carregado só quando o usuário abre; o serviço não depende dele.
 * Lê e grava o mesmo arquivo do serviço pelo núcleo (wz_ini_set preserva
 * comentários e chaves que não conhece) e pede "reload" pelo socket.
 */
#ifndef WZP_H
#define WZP_H

#include "wz-config.h"
#include "wz-layout.h"
#include "wz-theme.h"
#include "wz-i18n.h"

#include <gtk/gtk.h>

typedef struct {
    char *path;
    wz_config cfg;
    wz_theme theme;
    GtkWidget *window;
    GtkWidget *stack;
    GtkWidget *status;
} wzp_app;

extern wzp_app wzp;

/* wzp-draw.c: desenho OpenStep em cairo */
void wzp_rgb(cairo_t *cr, unsigned rgb, double a);
void wzp_bevel(cairo_t *cr, double x, double y, double w, double h, int raised);
void wzp_tile(cairo_t *cr, double x, double y, double w, double h, unsigned c1, unsigned c2);
void wzp_draw_layout(cairo_t *cr, const char *tree, double x, double y, double w, double h, int sel,
                     unsigned hl, int numbers);
void wzp_draw_icon(cairo_t *cr, int kind, double size);
GtkWidget *wzp_icon_widget(int kind, int size);

/* wzp-main.c */
void wzp_set(const char *section, const char *key, const char *value);
void wzp_set_int(const char *section, const char *key, int v);
void wzp_set_bool(const char *section, const char *key, int v);
void wzp_reload_daemon(void);
void wzp_reload_config(void);
const char *wzp_layout_label(const wz_layout_def *l);
int wzp_request(const char *cmd, char *out, size_t len);

/* wzp-editor.c */
GtkWidget *wzp_editor_window(GtkWindow *parent, const char *layout_id);

#endif
