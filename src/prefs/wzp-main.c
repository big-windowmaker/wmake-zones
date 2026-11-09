/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Preferências do wmake-zones, no feitio do WPrefs: uma fileira de ícones
 * das seções em cima, o painel da seção no meio, Salvar/Fechar embaixo.
 * A seção Geral basta para o uso comum (ligar as zonas e escolher o layout);
 * o resto fica nas outras seções.
 *
 *   wmake-zones-prefs [--general|--zones|--tiling|--monitors|--appearance|
 *                      --keys|--rules|--advanced|--editor|--about]
 */
#define _GNU_SOURCE
#include "wzp.h"
#include "wz-ipc.h"

#include <glib/gstdio.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>

wzp_app wzp;

static void
free_closure(gpointer data, GClosure *c)
{
    g_free(data);
}

/* ---- configuração -------------------------------------------------------- */

void
wzp_set(const char *section, const char *key, const char *value)
{
    if (wz_ini_set(wzp.path, section, key, value) != 0 && wzp.status)
        gtk_label_set_text(GTK_LABEL(wzp.status), _("Could not write the configuration file."));
}

void
wzp_set_int(const char *section, const char *key, int v)
{
    char b[32];
    g_snprintf(b, sizeof b, "%d", v);
    wzp_set(section, key, b);
}

void
wzp_set_bool(const char *section, const char *key, int v)
{
    wzp_set(section, key, v ? "true" : "false");
}

static void
set_double(const char *section, const char *key, double v)
{
    char b[32];
    wz_fmt_double(b, sizeof b, v);
    wzp_set(section, key, b);
}

void
wzp_reload_config(void)
{
    wz_config_free(&wzp.cfg);
    wz_config_defaults(&wzp.cfg);
    wz_config_load(&wzp.cfg, wzp.path);
}

int
wzp_request(const char *cmd, char *out, size_t len)
{
    return wz_ipc_request_session(cmd, out, len);
}

void
wzp_reload_daemon(void)
{
    char out[64];
    wzp_request("reload", out, sizeof out);
}

const char *
wzp_layout_label(const wz_layout_def *l)
{
    if (!l)
        return "?";
    if (l->label)
        return l->label;
    const char *b = wz_builtin_label(l->id);
    return b ? _(b) : l->id;
}

/* Ligar/desligar passa pelo comando: sobe o serviço se preciso (X11) ou
 * fala com o wlmaker (Wayland). */
static void
run_cmd(const char *a, const char *b)
{
    char *argv[] = { "wmake-zones", (char *)a, (char *)b, NULL };
    g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                 NULL, NULL, NULL, NULL, NULL, NULL);
}

/* ---- widgets auxiliares --------------------------------------------------- */

static GtkWidget *
frame(const char *title, GtkWidget *child)
{
    GtkWidget *f = gtk_frame_new(title);
    gtk_container_set_border_width(GTK_CONTAINER(child), 8);
    gtk_container_add(GTK_CONTAINER(f), child);
    return f;
}

static GtkWidget *
grid(void)
{
    GtkWidget *g = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(g), 6);
    gtk_grid_set_column_spacing(GTK_GRID(g), 10);
    return g;
}

static GtkWidget *
label(const char *t)
{
    GtkWidget *l = gtk_label_new(t);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    return l;
}

typedef struct {
    const char *section, *key;
} keyref;

static keyref *
kref(const char *s, const char *k)
{
    keyref *r = g_new(keyref, 1);
    r->section = s;
    r->key = k;
    return r;
}

static void
on_check(GtkToggleButton *b, keyref *r)
{
    wzp_set_bool(r->section, r->key, gtk_toggle_button_get_active(b));
    wzp_reload_daemon();
}

static GtkWidget *
check(const char *text, const char *section, const char *key, int value, const char *tip)
{
    GtkWidget *c = gtk_check_button_new_with_mnemonic(text);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(c), value);
    if (tip)
        gtk_widget_set_tooltip_text(c, tip);
    g_signal_connect_data(c, "toggled", G_CALLBACK(on_check), kref(section, key), free_closure, 0);
    return c;
}

static void
on_spin(GtkSpinButton *b, keyref *r)
{
    wzp_set_int(r->section, r->key, gtk_spin_button_get_value_as_int(b));
    wzp_reload_daemon();
}

static GtkWidget *
spin(const char *section, const char *key, int value, int lo, int hi)
{
    GtkWidget *s = gtk_spin_button_new_with_range(lo, hi, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(s), value);
    g_signal_connect_data(s, "value-changed", G_CALLBACK(on_spin), kref(section, key), free_closure, 0);
    return s;
}

static void
on_combo_id(GtkComboBox *c, keyref *r)
{
    const char *id = gtk_combo_box_get_active_id(c);
    if (id) {
        wzp_set(r->section, r->key, *id ? id : NULL);
        wzp_reload_daemon();
    }
}

/* Combo de pares id/rótulo terminados por NULL. */
static GtkWidget *
combo(const char *section, const char *key, const char *active, const char *const *pairs)
{
    GtkWidget *c = gtk_combo_box_text_new();
    for (int i = 0; pairs[i]; i += 2)
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(c), pairs[i], _(pairs[i + 1]));
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(c), active);
    if (gtk_combo_box_get_active(GTK_COMBO_BOX(c)) < 0)
        gtk_combo_box_set_active(GTK_COMBO_BOX(c), 0);
    g_signal_connect_data(c, "changed", G_CALLBACK(on_combo_id), kref(section, key), free_closure, 0);
    return c;
}

static GtkWidget *
layout_combo(const char *section, const char *key, const char *active, int with_inherit)
{
    GtkWidget *c = gtk_combo_box_text_new();
    if (with_inherit)
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(c), "", _("(same as the general layout)"));
    for (int i = 0; i < wzp.cfg.nlayouts; i++)
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(c), wzp.cfg.layouts[i].id, wzp_layout_label(&wzp.cfg.layouts[i]));
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(c), active ? active : "");
    if (gtk_combo_box_get_active(GTK_COMBO_BOX(c)) < 0)
        gtk_combo_box_set_active(GTK_COMBO_BOX(c), 0);
    g_signal_connect_data(c, "changed", G_CALLBACK(on_combo_id), kref(section, key), free_closure, 0);
    return c;
}

/* ---- Geral ---------------------------------------------------------------- */

static GtkWidget *preview_area;

static gboolean
draw_preview(GtkWidget *w, cairo_t *cr, gpointer data)
{
    int W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w);
    wzp_rgb(cr, 0x1c2024, 1);
    cairo_rectangle(cr, 0, 0, W, H);
    cairo_fill(cr);
    wzp_bevel(cr, 0, 0, W, H, 0);
    const wz_layout_def *l = wz_config_layout(&wzp.cfg, wzp.cfg.layout);
    if (l)
        wzp_draw_layout(cr, l->tree, 4, 4, W - 8, H - 8, -1, 0, 1);
    return TRUE;
}

static void
on_zones(GtkToggleButton *b, gpointer d)
{
    run_cmd("zones", gtk_toggle_button_get_active(b) ? "on" : "off");
    wzp_reload_config();
}

static void
on_autotile(GtkToggleButton *b, gpointer d)
{
    run_cmd("autotile", gtk_toggle_button_get_active(b) ? "on" : "off");
    wzp_reload_config();
}

static void
on_general_layout(GtkComboBox *c, gpointer d)
{
    const char *id = gtk_combo_box_get_active_id(c);
    if (!id)
        return;
    if (!*id) {
        /* Automático: sem a chave, o layout vem da proporção do monitor. */
        wzp_set("General", "Layout", NULL);
        wzp_reload_daemon();
    } else {
        run_cmd("layout", id);
        wzp_set("General", "Layout", id);
    }
    wzp_reload_config();
    gtk_widget_queue_draw(preview_area);
}

static GtkWidget *
editor_for_current(void)
{
    return wzp_editor_window(GTK_WINDOW(wzp.window), wzp.cfg.layout);
}

static void
on_edit(GtkButton *b, gpointer d)
{
    editor_for_current();
}

static const char *const focus_edges[] = { "cross", N_("Go on to the next monitor"), "stop",
                                           N_("Stay on the same monitor"), "wrap",
                                           N_("Wrap around on the same monitor"), NULL };
static const char *const clicks[] = { "mode", N_("Switch between Zones and Tiling"), "enable",
                                      N_("Turn off / back on"), "layouts", N_("Show the layouts"),
                                      "zones", N_("Zones on/off"), "autotile", N_("Auto Tiling on/off"),
                                      "none", N_("Nothing"), NULL };
static const char *const wheels[] = { "layout", N_("Previous / next layout"), "none", N_("Nothing"), NULL };

static const char *
click_id(wz_click c)
{
    switch (c) {
    case WZ_CLICK_MODE: return "mode";
    case WZ_CLICK_ENABLE: return "enable";
    case WZ_CLICK_LAYOUTS: return "layouts";
    case WZ_CLICK_ZONES: return "zones";
    case WZ_CLICK_AUTOTILE: return "autotile";
    case WZ_CLICK_CYCLE: return "layout";
    case WZ_CLICK_MENU: return "menu";
    default: return "none";
    }
}

static GtkWidget *
page_general(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *g = grid();
    GtkWidget *z = gtk_check_button_new_with_mnemonic(_("Snap windows to _zones while dragging them"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(z), wzp.cfg.zones);
    g_signal_connect(z, "toggled", G_CALLBACK(on_zones), NULL);
    gtk_grid_attach(GTK_GRID(g), z, 0, 0, 2, 1);
    GtkWidget *t = gtk_check_button_new_with_mnemonic(_("_Auto Tiling: arrange windows automatically"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(t), wzp.cfg.autotile);
    g_signal_connect(t, "toggled", G_CALLBACK(on_autotile), NULL);
    gtk_grid_attach(GTK_GRID(g), t, 0, 1, 2, 1);
    gtk_grid_attach(GTK_GRID(g), label(_("Layout:")), 0, 2, 1, 1);
    GtkWidget *lc = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(lc), "", _("Automatic (by the monitor's shape)"));
    for (int i = 0; i < wzp.cfg.nlayouts; i++)
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(lc), wzp.cfg.layouts[i].id, wzp_layout_label(&wzp.cfg.layouts[i]));
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(lc), wzp.cfg.layout_set ? wzp.cfg.layout : "");
    g_signal_connect(lc, "changed", G_CALLBACK(on_general_layout), NULL);
    gtk_widget_set_hexpand(lc, TRUE);
    gtk_grid_attach(GTK_GRID(g), lc, 1, 2, 1, 1);
    GtkWidget *ed = gtk_button_new_with_mnemonic(_("_Edit zones..."));
    g_signal_connect(ed, "clicked", G_CALLBACK(on_edit), NULL);
    gtk_grid_attach(GTK_GRID(g), ed, 2, 2, 1, 1);
    preview_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(preview_area, 240, 135);
    g_signal_connect(preview_area, "draw", G_CALLBACK(draw_preview), NULL);
    gtk_grid_attach(GTK_GRID(g), preview_area, 0, 3, 3, 1);
    gtk_box_pack_start(GTK_BOX(box), frame(_("Window organization"), g), FALSE, FALSE, 0);

    GtkWidget *g2 = grid();
    gtk_grid_attach(GTK_GRID(g2),
                    check(_("Restore the original size when a snapped window is dragged out of the zones"), "General",
                          "RestoreSizeOnUnsnap", wzp.cfg.restore_size_on_unsnap, NULL),
                    0, 0, 2, 1);
    gtk_grid_attach(GTK_GRID(g2),
                    check(_("Keep the service running for the keyboard shortcuts even with everything off"), "General",
                          "KeepRunning", wzp.cfg.keep_running,
                          _("Uses about 3 MiB of memory and no processor time while idle")),
                    0, 1, 2, 1);
    gtk_grid_attach(GTK_GRID(g2), label(_("Undo steps kept:")), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(g2), spin("General", "HistoryDepth", wzp.cfg.history_depth, 1, 100), 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(g2), label(_("Focusing the window next to it, at the screen edge:")), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(g2),
                    combo("General", "FocusEdge",
                          wzp.cfg.focus_edge == WZ_FOCUS_STOP   ? "stop"
                          : wzp.cfg.focus_edge == WZ_FOCUS_WRAP ? "wrap"
                                                                : "cross",
                          focus_edges),
                    1, 3, 1, 1);
    gtk_box_pack_start(GTK_BOX(box), frame(_("Behaviour"), g2), FALSE, FALSE, 0);

    /* Ladrilho do Dock: o que cada botão faz. */
    GtkWidget *g3 = grid();
    gtk_grid_attach(GTK_GRID(g3), label(_("Left click:")), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g3), combo("Dockapp", "LeftClick", click_id(wzp.cfg.click[0]), clicks), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g3), label(_("Middle click:")), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(g3), combo("Dockapp", "MiddleClick", click_id(wzp.cfg.click[1]), clicks), 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(g3), label(_("Wheel:")), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(g3), combo("Dockapp", "Wheel", click_id(wzp.cfg.click[3]), wheels), 1, 2, 1, 1);
    gtk_box_pack_start(GTK_BOX(box), frame(_("Dock tile"), g3), FALSE, FALSE, 0);
    GtkWidget *note = label(_("Zones are on from the start: windows float as in the traditional Window Maker and "
                              "move only when you drop them on a zone. Auto Tiling arranges them by itself; it stays "
                              "off until you turn it on. Left click on the wmake-zones tile in the Dock switches "
                              "between the two; the middle click turns everything off and back on."));
    gtk_label_set_line_wrap(GTK_LABEL(note), TRUE);
    gtk_box_pack_start(GTK_BOX(box), note, FALSE, FALSE, 0);
    return box;
}

/* ---- Zonas ---------------------------------------------------------------- */

static const char *const activations[] = { "drag", N_("When a window starts being dragged"), "modifier",
                                           N_("Only while the modifier key is held"), "edge",
                                           N_("When the pointer reaches a screen edge"), NULL };
static const char *const modifiers[] = { "super", N_("Super (Windows key)"), "alt", N_("Alt"), "ctrl", N_("Ctrl"),
                                         "shift", N_("Shift"), NULL };

static const char *const drag_flows[] = { "layouts", N_("First the layouts, then the zones"), "zones",
                                          N_("The zones of the current layout"), "layouts-modifier",
                                          N_("The layouts only while the modifier key is held"), "layouts-edge",
                                          N_("The layouts at the top edge of the screen"), NULL };

static const char *
activation_id(void)
{
    return wzp.cfg.activation == WZ_ACTIVATE_MODIFIER ? "modifier" : wzp.cfg.activation == WZ_ACTIVATE_EDGE ? "edge" : "drag";
}

static const char *
modifier_id(void)
{
    const char *m[] = { "super", "alt", "ctrl", "shift" };
    return m[wzp.cfg.modifier];
}

static GtkWidget *
page_zones(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *g = grid();
    gtk_grid_attach(GTK_GRID(g), label(_("Show the zones:")), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g), combo("General", "Activation", activation_id(), activations), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g), label(_("Modifier key:")), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(g), combo("General", "Modifier", modifier_id(), modifiers), 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(g), label(_("Delay before showing (ms):")), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(g), spin("General", "ShowDelay", wzp.cfg.show_delay_ms, 0, 2000), 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(g), label(_("Minimum drag distance (px):")), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(g), spin("General", "MinDrag", wzp.cfg.min_drag_px, 0, 400), 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(g), label(_("Edge sensitivity (px):")), 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(g), spin("General", "Edge", wzp.cfg.edge_px, 1, 400), 1, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(g), label(_("Join two zones near their border (px):")), 0, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(g), spin("General", "Span", wzp.cfg.span_px, 0, 200), 1, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(g), label(_("While dragging, show:")), 0, 6, 1, 1);
    gtk_grid_attach(GTK_GRID(g),
                    combo("General", "DragFlow",
                          wzp.cfg.drag_flow == WZ_FLOW_ZONES              ? "zones"
                          : wzp.cfg.drag_flow == WZ_FLOW_LAYOUTS_MODIFIER ? "layouts-modifier"
                          : wzp.cfg.drag_flow == WZ_FLOW_LAYOUTS_EDGE     ? "layouts-edge"
                                                                          : "layouts",
                          drag_flows),
                    1, 6, 1, 1);
    gtk_grid_attach(GTK_GRID(g), label(_("Rest on a layout to see its zones (ms):")), 0, 7, 1, 1);
    gtk_grid_attach(GTK_GRID(g), spin("General", "LayoutDwell", wzp.cfg.dwell_ms, 0, 3000), 1, 7, 1, 1);
    gtk_box_pack_start(GTK_BOX(box), frame(_("Dragging"), g), FALSE, FALSE, 0);

    GtkWidget *g2 = grid();
    gtk_grid_attach(GTK_GRID(g2), label(_("Space between zones (px):")), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g2), spin("General", "Gap", wzp.cfg.gap_in, 0, 64), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g2), label(_("Margin at the screen edges (px):")), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(g2), spin("General", "OuterGap", wzp.cfg.gap_out, 0, 64), 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(g2),
                    check(_("Keep windows off the Dock and the Clip"), "General", "AvoidDock", wzp.cfg.avoid_dock, NULL),
                    0, 2, 2, 1);
    gtk_grid_attach(GTK_GRID(g2), check(_("Show zone numbers"), "General", "ShowNumbers", wzp.cfg.show_numbers, NULL),
                    0, 3, 2, 1);
    GtkWidget *ed = gtk_button_new_with_mnemonic(_("_Edit zones..."));
    g_signal_connect(ed, "clicked", G_CALLBACK(on_edit), NULL);
    gtk_grid_attach(GTK_GRID(g2), ed, 0, 4, 1, 1);
    gtk_box_pack_start(GTK_BOX(box), frame(_("Zones"), g2), FALSE, FALSE, 0);
    return box;
}

/* ---- Mosaico -------------------------------------------------------------- */

static const char *const algos[] = { "master", N_("Main + stack"), "grid", N_("Grid"), "columns", N_("Columns"),
                                     "rows", N_("Rows"), "monocle", N_("Monocle (one at a time)"), "bsp",
                                     N_("BSP (binary splits)"), "dwindle", N_("Dwindle"), "spiral", N_("Spiral"), NULL };
static const char *const positions[] = { "left", N_("Left"), "right", N_("Right"), "top", N_("Top"), "bottom",
                                         N_("Bottom"), NULL };

static void
on_ratio(GtkRange *r, gpointer d)
{
    set_double("Tiling", "MasterRatio", gtk_range_get_value(r) / 100.0);
    wzp_reload_daemon();
}

static GtkWidget *
page_tiling(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *g = grid();
    gtk_grid_attach(GTK_GRID(g), label(_("Algorithm:")), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g), combo("Tiling", "Algorithm", wz_algo_name(wzp.cfg.tile.algo), algos), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g), label(_("Main area position:")), 0, 1, 1, 1);
    const char *pos[] = { "left", "right", "top", "bottom" };
    gtk_grid_attach(GTK_GRID(g), combo("Tiling", "MasterPosition", pos[wzp.cfg.tile.master_pos], positions), 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(g), label(_("Main area size (%):")), 0, 2, 1, 1);
    GtkWidget *sc = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 10, 90, 1);
    gtk_range_set_value(GTK_RANGE(sc), wzp.cfg.tile.master_ratio * 100);
    gtk_widget_set_hexpand(sc, TRUE);
    g_signal_connect(sc, "value-changed", G_CALLBACK(on_ratio), NULL);
    gtk_grid_attach(GTK_GRID(g), sc, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(g), label(_("Windows in the main area:")), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(g), spin("Tiling", "Masters", wzp.cfg.tile.nmaster, 1, 8), 1, 3, 1, 1);
    gtk_box_pack_start(GTK_BOX(box), frame(_("Layout"), g), FALSE, FALSE, 0);
    GtkWidget *g2 = grid();
    gtk_grid_attach(GTK_GRID(g2), label(_("Space between windows (px):")), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g2), spin("Tiling", "Gap", wzp.cfg.tile.gap_in, 0, 64), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g2), label(_("Margin at the screen edges (px):")), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(g2), spin("Tiling", "OuterGap", wzp.cfg.tile.gap_out, 0, 64), 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(g2),
                    check(_("No spacing when a single window is tiled"), "Tiling", "SmartGaps", wzp.cfg.tile.smart_gaps, NULL),
                    0, 2, 2, 1);
    gtk_box_pack_start(GTK_BOX(box), frame(_("Spacing"), g2), FALSE, FALSE, 0);
    GtkWidget *n = label(_("Dialogs, menus, fixed-size and full-screen windows are never tiled. A window whose minimum "
                           "size does not fit its cell floats. Drag a tiled window onto another to swap them; "
                           "Super+Ctrl+F floats or tiles the active window."));
    gtk_label_set_line_wrap(GTK_LABEL(n), TRUE);
    gtk_box_pack_start(GTK_BOX(box), n, FALSE, FALSE, 0);
    return box;
}

/* ---- Monitores ------------------------------------------------------------ */

typedef struct {
    GtkWidget *mon, *ws, *layout, *tile, *algo, *body;
} mon_page;

static mon_page mp;

static const char *const tri[] = { "", N_("(same as general)"), "true", N_("On"), "false", N_("Off"), NULL };

static void mon_rebuild(void);

static void
on_mon_changed(GtkWidget *w, gpointer d)
{
    mon_rebuild();
}

static char *scope_section;

static void
mon_rebuild(void)
{
    const char *mon = gtk_combo_box_get_active_id(GTK_COMBO_BOX(mp.mon));
    int ws = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(mp.ws));
    g_free(scope_section);
    if (mon && *mon)
        scope_section = ws > 0 ? g_strdup_printf("Monitor %s Workspace %d", mon, ws) : g_strdup_printf("Monitor %s", mon);
    else
        scope_section = ws > 0 ? g_strdup_printf("Workspace %d", ws) : g_strdup("General");
    const wz_scope *s = NULL;
    for (int i = 0; i < wzp.cfg.nscopes; i++) {
        const wz_scope *c = &wzp.cfg.scopes[i];
        if (c->workspace == ws && ((!c->monitor && !(mon && *mon)) || (c->monitor && mon && !strcmp(c->monitor, mon))))
            s = c;
    }
    if (mp.body)
        gtk_widget_destroy(mp.body);
    GtkWidget *g = grid();
    gtk_grid_attach(GTK_GRID(g), label(_("Zone layout:")), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g), layout_combo(scope_section, "Layout", s ? s->layout : NULL, 1), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g), label(_("Auto Tiling:")), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(g), combo(scope_section, "Autotile", !s || s->autotile < 0 ? "" : s->autotile ? "true" : "false", tri),
                    1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(g), label(_("Tiling algorithm:")), 0, 2, 1, 1);
    static const char *algos_inherit[2 + 2 * WZ_TILE_N + 1];
    algos_inherit[0] = "";
    algos_inherit[1] = N_("(same as general)");
    for (int i = 0; algos[i]; i++)
        algos_inherit[2 + i] = algos[i];
    gtk_grid_attach(GTK_GRID(g), combo(scope_section, "Algorithm", s && s->algo >= 0 ? wz_algo_name(s->algo) : "", algos_inherit),
                    1, 2, 1, 1);
    mp.body = g;
    gtk_box_pack_start(GTK_BOX(gtk_widget_get_parent(mp.mon) ? gtk_widget_get_parent(gtk_widget_get_parent(mp.mon)) : NULL),
                       g, FALSE, FALSE, 0);
    gtk_widget_show_all(g);
}

static GtkWidget *
page_monitors(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *g = grid();
    gtk_grid_attach(GTK_GRID(g), label(_("Monitor:")), 0, 0, 1, 1);
    mp.mon = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mp.mon), "", _("All monitors"));
    /* Nomes como o serviço os vê (XRandR / wlroots). */
    char out[8192];
    if (wzp_request("status", out, sizeof out) == 0) {
        char *save = NULL;
        for (char *l = strtok_r(out, "\n", &save); l; l = strtok_r(NULL, "\n", &save))
            if (!strncmp(l, "monitor=", 8)) {
                char name[64];
                int w = 0, h = 0;
                if (sscanf(l + 8, "%63s %dx%d", name, &w, &h) >= 1) {
                    char *lab = g_strdup_printf("%s (%d×%d)", name, w, h);
                    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mp.mon), name, lab);
                    g_free(lab);
                }
            }
    }
    for (int i = 0; i < wzp.cfg.nscopes; i++)
        if (wzp.cfg.scopes[i].monitor) {
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(mp.mon), wzp.cfg.scopes[i].monitor);
            if (gtk_combo_box_get_active(GTK_COMBO_BOX(mp.mon)) < 0)
                gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mp.mon), wzp.cfg.scopes[i].monitor,
                                          wzp.cfg.scopes[i].monitor);
        }
    gtk_combo_box_set_active(GTK_COMBO_BOX(mp.mon), 0);
    gtk_widget_set_hexpand(mp.mon, TRUE);
    gtk_grid_attach(GTK_GRID(g), mp.mon, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g), label(_("Workspace (0 = all):")), 0, 1, 1, 1);
    mp.ws = gtk_spin_button_new_with_range(0, 32, 1);
    gtk_grid_attach(GTK_GRID(g), mp.ws, 1, 1, 1, 1);
    GtkWidget *inner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_pack_start(GTK_BOX(inner), g, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), frame(_("Settings for a monitor or workspace"), inner), FALSE, FALSE, 0);
    g_signal_connect(mp.mon, "changed", G_CALLBACK(on_mon_changed), NULL);
    g_signal_connect(mp.ws, "value-changed", G_CALLBACK(on_mon_changed), NULL);
    mp.body = NULL;
    mon_rebuild();
    GtkWidget *n = label(_("A monitor or workspace without its own settings follows the general ones. When a monitor is "
                           "unplugged, its windows are arranged again on the remaining monitors."));
    gtk_label_set_line_wrap(GTK_LABEL(n), TRUE);
    gtk_box_pack_start(GTK_BOX(box), n, FALSE, FALSE, 0);
    return box;
}

/* ---- Aparência ------------------------------------------------------------ */

static void
on_color(GtkColorButton *b, gpointer d)
{
    GdkRGBA c;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(b), &c);
    char hex[16];
    g_snprintf(hex, sizeof hex, "#%02x%02x%02x", (int)(c.red * 255), (int)(c.green * 255), (int)(c.blue * 255));
    wzp_set("Appearance", "Highlight", hex);
    wzp_reload_daemon();
}

static void
on_color_auto(GtkButton *b, gpointer d)
{
    wzp_set("Appearance", "Highlight", "auto");
    wzp_reload_daemon();
}

static GtkWidget *
page_appearance(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *g = grid();
    gtk_grid_attach(GTK_GRID(g), label(_("Highlight colour:")), 0, 0, 1, 1);
    unsigned hc = wzp.cfg.highlight != WZ_COLOR_AUTO ? wzp.cfg.highlight : wzp.theme.ftitle1;
    GdkRGBA rgba = { (hc >> 16 & 0xff) / 255.0, (hc >> 8 & 0xff) / 255.0, (hc & 0xff) / 255.0, 1 };
    GtkWidget *cb = gtk_color_button_new_with_rgba(&rgba);
    g_signal_connect(cb, "color-set", G_CALLBACK(on_color), NULL);
    gtk_grid_attach(GTK_GRID(g), cb, 1, 0, 1, 1);
    GtkWidget *au = gtk_button_new_with_mnemonic(_("Use the _theme colour"));
    g_signal_connect(au, "clicked", G_CALLBACK(on_color_auto), NULL);
    gtk_grid_attach(GTK_GRID(g), au, 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g), label(_("Zone opacity (%):")), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(g), spin("Appearance", "Opacity", wzp.cfg.opacity, 0, 100), 1, 1, 1, 1);
    gtk_box_pack_start(GTK_BOX(box), frame(_("Zone overlay"), g), FALSE, FALSE, 0);
    GtkWidget *n = label(_("The overlay, the Dock tile and the menus follow the active Window Maker theme "
                           "(big-wm-theme light or dark). Translucency needs a compositor (picom); without one the "
                           "zones are drawn as outlines with a striped target."));
    gtk_label_set_line_wrap(GTK_LABEL(n), TRUE);
    gtk_box_pack_start(GTK_BOX(box), n, FALSE, FALSE, 0);
    return box;
}

/* ---- Atalhos -------------------------------------------------------------- */

static const char *const key_labels[WZ_KEY_N] = {
    N_("Choose a zone with the keyboard"), N_("Turn Zones on or off"), N_("Turn Auto Tiling on or off"),
    N_("Open the zone editor"), N_("Next layout"), N_("Move window to the zone on the left"),
    N_("Move window to the zone on the right"), N_("Move window to the zone above"),
    N_("Move window to the zone below"), N_("Focus the window on the left"), N_("Focus the window on the right"),
    N_("Focus the window above"), N_("Focus the window below"), N_("Swap with the window on the left"),
    N_("Swap with the window on the right"), N_("Swap with the window above"), N_("Swap with the window below"),
    N_("Restore the previous size and position"), N_("Float or tile the window"), N_("Make it the main window"),
    N_("Grow the main area"), N_("Shrink the main area"), N_("Undo the last arrangement"),
};

static GtkListStore *keys_store;

static char *
accel_to_spec(guint key, GdkModifierType mods)
{
    GString *s = g_string_new(NULL);
    if (mods & GDK_SUPER_MASK || mods & GDK_MOD4_MASK)
        g_string_append(s, "Super+");
    if (mods & GDK_CONTROL_MASK)
        g_string_append(s, "Ctrl+");
    if (mods & GDK_MOD1_MASK)
        g_string_append(s, "Alt+");
    if (mods & GDK_SHIFT_MASK)
        g_string_append(s, "Shift+");
    g_string_append(s, gdk_keyval_name(gdk_keyval_to_lower(key)));
    return g_string_free(s, FALSE);
}

static void
on_accel(GtkCellRendererAccel *r, gchar *path, guint key, GdkModifierType mods, guint hw, gpointer d)
{
    GtkTreeIter it;
    if (!gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(keys_store), &it, path))
        return;
    int idx;
    gtk_tree_model_get(GTK_TREE_MODEL(keys_store), &it, 2, &idx, -1);
    char *spec = accel_to_spec(key, mods);
    gtk_list_store_set(keys_store, &it, 1, spec, -1);
    wzp_set("Keys", wz_key_name(idx), spec);
    g_free(spec);
    wzp_reload_daemon();
}

static void
on_accel_clear(GtkCellRendererAccel *r, gchar *path, gpointer d)
{
    GtkTreeIter it;
    if (!gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(keys_store), &it, path))
        return;
    int idx;
    gtk_tree_model_get(GTK_TREE_MODEL(keys_store), &it, 2, &idx, -1);
    gtk_list_store_set(keys_store, &it, 1, "", -1);
    wzp_set("Keys", wz_key_name(idx), "");
    wzp_reload_daemon();
}

static void
keys_fill(void)
{
    gtk_list_store_clear(keys_store);
    char out[8192] = "";
    wzp_request("status", out, sizeof out);
    for (int i = 0; i < WZ_KEY_N; i++) {
        char pat[96];
        g_snprintf(pat, sizeof pat, "conflict=%s ", wz_key_name(i));
        GtkTreeIter it;
        gtk_list_store_append(keys_store, &it);
        gtk_list_store_set(keys_store, &it, 0, _(key_labels[i]), 1, wzp.cfg.keys[i], 2, i, 3,
                           strstr(out, pat) ? _("in use by another program") : "", -1);
    }
}

static void
on_keys_defaults(GtkButton *b, gpointer d)
{
    for (int i = 0; i < WZ_KEY_N; i++)
        wzp_set("Keys", wz_key_name(i), NULL);
    wzp_reload_config();
    wzp_reload_daemon();
    keys_fill();
}

static GtkWidget *
page_keys(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    keys_store = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING);
    GtkWidget *tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(keys_store));
    gtk_tree_view_append_column(GTK_TREE_VIEW(tv), gtk_tree_view_column_new_with_attributes(
                                                       _("Action"), gtk_cell_renderer_text_new(), "text", 0, NULL));
    GtkCellRenderer *ra = gtk_cell_renderer_accel_new();
    g_object_set(ra, "editable", TRUE, "accel-mode", GTK_CELL_RENDERER_ACCEL_MODE_OTHER, NULL);
    g_signal_connect(ra, "accel-edited", G_CALLBACK(on_accel), NULL);
    g_signal_connect(ra, "accel-cleared", G_CALLBACK(on_accel_clear), NULL);
    GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(_("Shortcut"), gtk_cell_renderer_text_new(), "text", 1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tv), c);
    GtkTreeViewColumn *ce = gtk_tree_view_column_new_with_attributes(_("Change"), ra, NULL);
    gtk_tree_view_column_set_cell_data_func(ce, ra, NULL, NULL, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tv), ce);
    GtkCellRenderer *rw = gtk_cell_renderer_text_new();
    g_object_set(rw, "foreground", "#a02020", NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tv), gtk_tree_view_column_new_with_attributes(_("Conflict"), rw, "text", 3, NULL));
    keys_fill();
    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(sw, -1, 240);
    gtk_container_add(GTK_CONTAINER(sw), tv);
    GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_pack_start(GTK_BOX(v), sw, TRUE, TRUE, 0);
    GtkWidget *hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *n = label(_("Click \"Change\" and press the new combination; Backspace clears it."));
    gtk_box_pack_start(GTK_BOX(hb), n, TRUE, TRUE, 0);
    GtkWidget *def = gtk_button_new_with_mnemonic(_("Restore _defaults"));
    g_signal_connect(def, "clicked", G_CALLBACK(on_keys_defaults), NULL);
    gtk_box_pack_start(GTK_BOX(hb), def, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(v), hb, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), frame(_("Keyboard shortcuts"), v), TRUE, TRUE, 0);
    return box;
}

/* ---- Regras --------------------------------------------------------------- */

static GtkListStore *rules_store;
static GtkWidget *rules_view;
static const char *const rule_actions[] = { "float", N_("Always floating"), "tile", N_("Tile"), "zone",
                                            N_("Put in zone"), "ignore", N_("Ignore"), NULL };

static void
rules_fill(void)
{
    gtk_list_store_clear(rules_store);
    for (int i = 0; i < wzp.cfg.nrules; i++) {
        const wz_rule *r = &wzp.cfg.rules[i];
        if (!r->wm_class && !r->instance && !r->title)
            continue;
        const char *act = rule_actions[2 * (r->action == WZ_RULE_FLOAT ? 0 : r->action == WZ_RULE_TILE ? 1 : r->action == WZ_RULE_ZONE ? 2 : 3)];
        GtkTreeIter it;
        gtk_list_store_append(rules_store, &it);
        gtk_list_store_set(rules_store, &it, 0, r->id, 1, r->wm_class ? r->wm_class : "", 2, r->title ? r->title : "",
                           3, act, 4, r->zone, -1);
    }
}

static void
rule_edited(GtkCellRendererText *cr, gchar *path, gchar *text, gpointer col)
{
    GtkTreeIter it;
    if (!gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(rules_store), &it, path))
        return;
    char *id;
    gtk_tree_model_get(GTK_TREE_MODEL(rules_store), &it, 0, &id, -1);
    char sec[160];
    g_snprintf(sec, sizeof sec, "Rule %s", id);
    int c = GPOINTER_TO_INT(col);
    if (c == 1) {
        wzp_set(sec, "Class", *text ? text : NULL);
        gtk_list_store_set(rules_store, &it, 1, text, -1);
    } else if (c == 2) {
        wzp_set(sec, "Title", *text ? text : NULL);
        gtk_list_store_set(rules_store, &it, 2, text, -1);
    } else if (c == 3) {
        for (int i = 0; rule_actions[i]; i += 2)
            if (!strcmp(text, _(rule_actions[i + 1])) || !strcmp(text, rule_actions[i])) {
                wzp_set(sec, "Action", rule_actions[i]);
                gtk_list_store_set(rules_store, &it, 3, rule_actions[i], -1);
            }
    } else if (c == 4) {
        int z = atoi(text);
        wzp_set_int(sec, "Zone", z);
        gtk_list_store_set(rules_store, &it, 4, z, -1);
    }
    g_free(id);
    wzp_reload_config();
    wzp_reload_daemon();
}

static void
add_rule(const char *cls)
{
    /* Primeiro número livre como id da regra. */
    char *id = NULL;
    for (int i = 1;; i++) {
        g_free(id);
        id = g_strdup_printf("%d", i);
        int used = 0;
        for (int k = 0; k < wzp.cfg.nrules; k++)
            used |= !strcmp(wzp.cfg.rules[k].id, id);
        if (!used)
            break;
    }
    char sec[160];
    g_snprintf(sec, sizeof sec, "Rule %s", id);
    wzp_set(sec, "Class", cls && *cls ? cls : "Application");
    wzp_set(sec, "Action", "float");
    g_free(id);
    wzp_reload_config();
    rules_fill();
    wzp_reload_daemon();
}

static void
on_rule_add(GtkButton *b, gpointer d)
{
    add_rule(NULL);
}

static void
on_rule_from_window(GtkMenuItem *mi, gpointer cls)
{
    add_rule(cls);
}

static void
on_rule_pick(GtkButton *b, gpointer d)
{
    char out[16384];
    if (wzp_request("windows", out, sizeof out) != 0) {
        gtk_label_set_text(GTK_LABEL(wzp.status), _("The service is not running; type the class by hand."));
        return;
    }
    GtkWidget *menu = gtk_menu_new();
    char *save = NULL;
    for (char *l = strtok_r(out, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        char **f = g_strsplit(l, "\t", 3);
        if (f[0] && *f[0] && strcmp(f[0], "ok")) {
            char *lab = g_strdup_printf("%s — %s", f[0], f[2] ? f[2] : "");
            GtkWidget *it = gtk_menu_item_new_with_label(lab);
            g_signal_connect_data(it, "activate", G_CALLBACK(on_rule_from_window), g_strdup(f[0]),
                                  free_closure, 0);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), it);
            g_free(lab);
        }
        g_strfreev(f);
    }
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(b), GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
}

static void
on_rule_del(GtkButton *b, gpointer d)
{
    GtkTreeIter it;
    GtkTreeSelection *s = gtk_tree_view_get_selection(GTK_TREE_VIEW(rules_view));
    if (!gtk_tree_selection_get_selected(s, NULL, &it))
        return;
    char *id;
    gtk_tree_model_get(GTK_TREE_MODEL(rules_store), &it, 0, &id, -1);
    char sec[160];
    g_snprintf(sec, sizeof sec, "Rule %s", id);
    const char *keys[] = { "Class", "Instance", "Title", "Action", "Zone", "Layout", "Monitor", "Workspace" };
    for (size_t i = 0; i < G_N_ELEMENTS(keys); i++)
        wzp_set(sec, keys[i], NULL);
    g_free(id);
    wzp_reload_config();
    rules_fill();
    wzp_reload_daemon();
}

static GtkWidget *
editable_col(const char *title, int col)
{
    GtkCellRenderer *r = gtk_cell_renderer_text_new();
    g_object_set(r, "editable", TRUE, NULL);
    g_signal_connect(r, "edited", G_CALLBACK(rule_edited), GINT_TO_POINTER(col));
    gtk_tree_view_append_column(GTK_TREE_VIEW(rules_view),
                                gtk_tree_view_column_new_with_attributes(title, r, "text", col, NULL));
    return NULL;
}

/* ---- Compatibilidade de aplicativos -------------------------------------- */

/* As janelas abertas e por que cada uma está ou não no mosaico ("windows
 * -d" do serviço): mínimo maior que a célula (GTK4/libadwaita), diálogo,
 * tamanho fixo, regra... Uma regra "float" sai daqui com um clique. */
static GtkListStore *compat_store;
static GtkWidget *compat_view;

/* O motivo do "windows -d" é texto do protocolo, o mesmo no serviço do X11
 * e no wlmaker (os testes dos dois procuram por ele): fica como está lá e
 * só a coluna das preferências o mostra no idioma do usuário. Um motivo
 * desconhecido (serviço mais novo) aparece como veio. */
static char *
compat_reason(const char *why)
{
    static const struct {
        const char *proto, *label;
    } fixed[] = {
        { "mosaico", N_("Tiled") },
        { "regra: ignorar", N_("Rule: ignore") },
        { "tipo especial (diálogo, utilitário, menu)", N_("Special type (dialog, utility, menu)") },
        { "diálogo", N_("Dialog") },
        { "tamanho fixo", N_("Fixed size") },
        { "tela cheia", N_("Full screen") },
        { "minimizada ou enrolada", N_("Minimized or shaded") },
        { "minimizada", N_("Minimized") },
        { "janela filha (transient)", N_("Child window (transient)") },
        { "modal", N_("Modal") },
        { "em todas as áreas, sempre acima ou fora da barra de tarefas",
          N_("On all workspaces, always on top or not in the taskbar") },
        { "flutuante por escolha do usuário", N_("Floating by choice of the user") },
        { "regra: flutuante", N_("Rule: floating") },
        { "outra área de trabalho", N_("On another workspace") },
        { "mosaico desligado neste monitor", N_("Tiling is off on this monitor") },
        { "mosaico desligado ou aguardando", N_("Tiling is off, or waiting") },
        { "aguardando", N_("Waiting") },
    };
    int a, b, c, e, n = 0;
    if (sscanf(why, "não cabe: mínimo %dx%d, célula %dx%d%n", &a, &b, &c, &e, &n) == 4 && !why[n])
        /* TRANSLATORS: the window's minimum size and the size of its cell, in pixels. */
        return g_strdup_printf(_("Does not fit: minimum %d×%d, cell %d×%d"), a, b, c, e);
    if (sscanf(why, "tamanho fixo (%dx%d)%n", &a, &b, &n) == 2 && !why[n])
        return g_strdup_printf(_("Fixed size (%d×%d)"), a, b);
    /* O wlmaker acrescenta " (mínimo LxA)" quando a janela pede um mínimo. */
    char *base = g_strdup(why);
    char *suffix = NULL;
    char *p = strstr(base, " (mínimo ");
    if (p && sscanf(p, " (mínimo %dx%d)%n", &a, &b, &n) == 2 && !p[n]) {
        *p = '\0';
        /* TRANSLATORS: appended to a reason; the window's minimum size in pixels. */
        suffix = g_strdup_printf(_(" (minimum %d×%d)"), a, b);
    }
    const char *label = NULL;
    for (size_t i = 0; i < G_N_ELEMENTS(fixed); i++)
        if (!strcmp(base, fixed[i].proto))
            label = _(fixed[i].label);
    char *r = label ? g_strconcat(label, suffix ? suffix : "", NULL) : g_strdup(why);
    g_free(base);
    g_free(suffix);
    return r;
}

static void
compat_fill(GtkButton *b, gpointer d)
{
    gtk_list_store_clear(compat_store);
    char out[32768];
    if (wzp_request("windows -d", out, sizeof out) != 0) {
        GtkTreeIter it;
        gtk_list_store_append(compat_store, &it);
        gtk_list_store_set(compat_store, &it, 0, "", 1, "", 2, _("The service is not running."), -1);
        return;
    }
    char *save = NULL;
    for (char *l = strtok_r(out, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        char **f = g_strsplit(l, "\t", 4);
        /* classe, tiled=N, motivo, título; um serviço antigo responde com
         * classe, instância e título: sem motivo. */
        if (f[0] && f[1] && g_str_has_prefix(f[1], "tiled=") && f[2]) {
            GtkTreeIter it;
            char *why = compat_reason(f[2]);
            gtk_list_store_append(compat_store, &it);
            gtk_list_store_set(compat_store, &it, 0, f[0], 1, f[3] ? f[3] : "", 2, why, -1);
            g_free(why);
        }
        g_strfreev(f);
    }
}

static void
on_compat_float(GtkButton *b, gpointer d)
{
    GtkTreeIter it;
    GtkTreeSelection *s = gtk_tree_view_get_selection(GTK_TREE_VIEW(compat_view));
    if (!gtk_tree_selection_get_selected(s, NULL, &it))
        return;
    char *cls;
    gtk_tree_model_get(GTK_TREE_MODEL(compat_store), &it, 0, &cls, -1);
    if (cls && *cls)
        add_rule(cls); /* Action=float */
    g_free(cls);
}

static GtkWidget *
compat_frame(void)
{
    compat_store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    compat_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(compat_store));
    const char *titles[] = { N_("Window class"), N_("Title"), N_("Tiling") };
    for (int c = 0; c < 3; c++) {
        GtkCellRenderer *r = gtk_cell_renderer_text_new();
        if (c == 1)
            g_object_set(r, "ellipsize", PANGO_ELLIPSIZE_END, "width-chars", 18, NULL);
        gtk_tree_view_append_column(GTK_TREE_VIEW(compat_view),
                                    gtk_tree_view_column_new_with_attributes(_(titles[c]), r, "text", c, NULL));
    }
    compat_fill(NULL, NULL);
    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(sw, -1, 140);
    gtk_container_add(GTK_CONTAINER(sw), compat_view);
    GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_pack_start(GTK_BOX(v), sw, TRUE, TRUE, 0);
    GtkWidget *hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *refresh = gtk_button_new_with_mnemonic(_("_Refresh"));
    g_signal_connect(refresh, "clicked", G_CALLBACK(compat_fill), NULL);
    GtkWidget *flt = gtk_button_new_with_mnemonic(_("Always _float this application"));
    g_signal_connect(flt, "clicked", G_CALLBACK(on_compat_float), NULL);
    gtk_box_pack_start(GTK_BOX(hb), refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hb), flt, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(v), hb, FALSE, FALSE, 0);
    GtkWidget *n = label(_("A window whose minimum size is larger than its cell floats instead of overlapping the "
                           "others (GTK4 and libadwaita applications often ask for large minimum sizes); with "
                           "fewer windows, or with a wider layout, it comes back to the tiling."));
    gtk_label_set_line_wrap(GTK_LABEL(n), TRUE);
    gtk_box_pack_start(GTK_BOX(v), n, FALSE, FALSE, 0);
    return frame(_("Application compatibility"), v);
}

static GtkWidget *
page_rules(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    rules_store = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
    rules_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(rules_store));
    editable_col(_("Window class"), 1);
    editable_col(_("Title contains"), 2);
    GtkCellRenderer *rc = gtk_cell_renderer_combo_new();
    GtkListStore *acts = gtk_list_store_new(1, G_TYPE_STRING);
    for (int i = 0; rule_actions[i]; i += 2) {
        GtkTreeIter it;
        gtk_list_store_append(acts, &it);
        gtk_list_store_set(acts, &it, 0, rule_actions[i], -1);
    }
    g_object_set(rc, "model", acts, "text-column", 0, "editable", TRUE, "has-entry", FALSE, NULL);
    g_signal_connect(rc, "edited", G_CALLBACK(rule_edited), GINT_TO_POINTER(3));
    gtk_tree_view_append_column(GTK_TREE_VIEW(rules_view),
                                gtk_tree_view_column_new_with_attributes(_("Action"), rc, "text", 3, NULL));
    editable_col(_("Zone"), 4);
    rules_fill();
    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(sw, -1, 200);
    gtk_container_add(GTK_CONTAINER(sw), rules_view);
    GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_pack_start(GTK_BOX(v), sw, TRUE, TRUE, 0);
    GtkWidget *hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *add = gtk_button_new_with_mnemonic(_("_Add"));
    g_signal_connect(add, "clicked", G_CALLBACK(on_rule_add), NULL);
    GtkWidget *pick = gtk_button_new_with_mnemonic(_("From an _open window..."));
    g_signal_connect(pick, "clicked", G_CALLBACK(on_rule_pick), NULL);
    GtkWidget *del = gtk_button_new_with_mnemonic(_("_Remove"));
    g_signal_connect(del, "clicked", G_CALLBACK(on_rule_del), NULL);
    gtk_box_pack_start(GTK_BOX(hb), add, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hb), pick, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hb), del, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(v), hb, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(v),
                       check(_("Apply the rules (uncheck to suspend them for now)"), "General", "Rules",
                             wzp.cfg.rules_enabled, NULL),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), frame(_("Per-application rules"), v), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), compat_frame(), TRUE, TRUE, 0);
    GtkWidget *n = label(_("Windows are matched by their class (WM_CLASS on X11, app_id on Wayland), not by the title, "
                           "which changes. What you do by hand wins over a rule until the window is closed."));
    gtk_label_set_line_wrap(GTK_LABEL(n), TRUE);
    gtk_box_pack_start(GTK_BOX(box), n, FALSE, FALSE, 0);
    return box;
}

/* ---- Avançado ------------------------------------------------------------- */

static GtkTextBuffer *diag_buf;

static void
diag_fill(GtkButton *b, gpointer d)
{
    char out[16384] = "", log[16384] = "";
    GString *s = g_string_new(NULL);
    g_string_append_printf(s, "%s: %s\n\n", _("Configuration file"), wzp.path);
    if (wzp_request("status", out, sizeof out) == 0) {
        g_string_append(s, out);
        char why[16384] = "";
        if (wzp_request("windows -d", why, sizeof why) == 0)
            g_string_append_printf(s, "\n%s:\n%s", _("Windows and tiling"), why);
        wzp_request("log", log, sizeof log);
        g_string_append_printf(s, "\n%s:\n%s", _("Recent log"), log);
    } else {
        g_string_append(s, _("The service is not running (Zones and Auto Tiling are off)."));
    }
    gtk_text_buffer_set_text(diag_buf, s->str, -1);
    g_string_free(s, TRUE);
}

static void
on_export_cfg(GtkButton *b, gpointer d)
{
    GtkFileChooserNative *fc = gtk_file_chooser_native_new(_("Export settings"), GTK_WINDOW(wzp.window),
                                                           GTK_FILE_CHOOSER_ACTION_SAVE, _("_Export"), NULL);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(fc), "wmake-zones.conf");
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(fc), TRUE);
    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
        char *dst = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));
        char *txt = NULL;
        gsize len = 0;
        if (g_file_get_contents(wzp.path, &txt, &len, NULL))
            g_file_set_contents(dst, txt, (gssize)len, NULL);
        g_free(txt);
        g_free(dst);
    }
    g_object_unref(fc);
}

static void
on_import_cfg(GtkButton *b, gpointer d)
{
    GtkFileChooserNative *fc = gtk_file_chooser_native_new(_("Import settings"), GTK_WINDOW(wzp.window),
                                                           GTK_FILE_CHOOSER_ACTION_OPEN, _("_Import"), NULL);
    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
        char *src = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));
        char *txt = NULL;
        gsize len = 0;
        if (g_file_get_contents(src, &txt, &len, NULL)) {
            /* Cópia de segurança do atual antes de trocar. */
            char *bak = g_strdup_printf("%s.bak", wzp.path);
            char *cur = NULL;
            gsize cl = 0;
            if (g_file_get_contents(wzp.path, &cur, &cl, NULL))
                g_file_set_contents(bak, cur, (gssize)cl, NULL);
            g_file_set_contents(wzp.path, txt, (gssize)len, NULL);
            g_free(cur);
            g_free(bak);
            wzp_reload_daemon();
            gtk_label_set_text(GTK_LABEL(wzp.status), _("Imported. The previous settings were kept as .bak; reopen "
                                                        "this window to see them."));
        }
        g_free(txt);
        g_free(src);
    }
    g_object_unref(fc);
}

static void
on_reset(GtkButton *b, gpointer d)
{
    GtkWidget *dl = gtk_message_dialog_new(GTK_WINDOW(wzp.window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
                                           GTK_BUTTONS_OK_CANCEL, "%s",
                                           _("Turn everything off and restore the default settings? The current file "
                                             "is kept as wmake-zones.conf.bak."));
    if (gtk_dialog_run(GTK_DIALOG(dl)) == GTK_RESPONSE_OK) {
        run_cmd("--reset", NULL);
        char *bak = g_strdup_printf("%s.bak", wzp.path);
        g_rename(wzp.path, bak);
        g_free(bak);
        gtk_label_set_text(GTK_LABEL(wzp.status), _("Defaults restored; reopen this window."));
    }
    gtk_widget_destroy(dl);
}

static GtkWidget *
page_advanced(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *tv = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(tv), TRUE);
    diag_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv));
    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(sw, -1, 200);
    gtk_container_add(GTK_CONTAINER(sw), tv);
    GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_pack_start(GTK_BOX(v), sw, TRUE, TRUE, 0);
    GtkWidget *hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *rf = gtk_button_new_with_mnemonic(_("_Refresh"));
    g_signal_connect(rf, "clicked", G_CALLBACK(diag_fill), NULL);
    gtk_box_pack_start(GTK_BOX(hb), rf, FALSE, FALSE, 0);
    GtkWidget *ex = gtk_button_new_with_mnemonic(_("_Export settings..."));
    g_signal_connect(ex, "clicked", G_CALLBACK(on_export_cfg), NULL);
    gtk_box_pack_start(GTK_BOX(hb), ex, FALSE, FALSE, 0);
    GtkWidget *im = gtk_button_new_with_mnemonic(_("_Import settings..."));
    g_signal_connect(im, "clicked", G_CALLBACK(on_import_cfg), NULL);
    gtk_box_pack_start(GTK_BOX(hb), im, FALSE, FALSE, 0);
    GtkWidget *rs = gtk_button_new_with_mnemonic(_("Restore _defaults..."));
    g_signal_connect(rs, "clicked", G_CALLBACK(on_reset), NULL);
    gtk_box_pack_end(GTK_BOX(hb), rs, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(v), hb, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), frame(_("Diagnostics"), v), TRUE, TRUE, 0);
    diag_fill(NULL, NULL);
    return box;
}

/* ---- janela --------------------------------------------------------------- */

typedef struct {
    const char *id, *title;
    int icon;
    GtkWidget *(*build)(void);
} section;

static const section sections[] = {
    { "general", N_("General"), 0, page_general },     { "zones", N_("Zones"), 1, page_zones },
    { "tiling", N_("Tiling"), 2, page_tiling },        { "monitors", N_("Monitors"), 3, page_monitors },
    { "appearance", N_("Appearance"), 4, page_appearance }, { "keys", N_("Shortcuts"), 5, page_keys },
    { "rules", N_("Rules"), 6, page_rules },           { "advanced", N_("Advanced"), 7, page_advanced },
};

static GtkWidget *section_label;

static void
on_section(GtkToggleButton *b, gpointer idx)
{
    if (!gtk_toggle_button_get_active(b))
        return;
    const section *s = &sections[GPOINTER_TO_INT(idx)];
    gtk_stack_set_visible_child_name(GTK_STACK(wzp.stack), s->id);
    char *m = g_markup_printf_escaped("<b>%s</b>", _(s->title));
    gtk_label_set_markup(GTK_LABEL(section_label), m);
    g_free(m);
}

static void
about(GtkWindow *parent)
{
    GtkWidget *d = gtk_about_dialog_new();
    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(d), "wmake-zones");
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(d), WZ_VERSION);
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(d),
                                  _("Window zones and tiling for Window Maker and wlmaker.\n"
                                    "Floating first, tiling when you want."));
    gtk_about_dialog_set_website(GTK_ABOUT_DIALOG(d), "https://github.com/big-windowmaker/wmake-zones");
    gtk_about_dialog_set_license_type(GTK_ABOUT_DIALOG(d), GTK_LICENSE_GPL_2_0);
    gtk_about_dialog_set_logo_icon_name(GTK_ABOUT_DIALOG(d), "wmake-zones");
    if (parent)
        gtk_window_set_transient_for(GTK_WINDOW(d), parent);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

static void
on_about(GtkButton *b, gpointer d)
{
    about(GTK_WINDOW(wzp.window));
}

static void
build(const char *first)
{
    wzp.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(wzp.window), _("wmake-zones Preferences"));
    gtk_window_set_icon_name(GTK_WINDOW(wzp.window), "wmake-zones");
    gtk_window_set_default_size(GTK_WINDOW(wzp.window), 640, 520);
    g_signal_connect(wzp.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(v), 8);
    gtk_container_add(GTK_CONTAINER(wzp.window), v);

    /* Fileira de ícones, como a do WPrefs. */
    GtkWidget *strip = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    GtkWidget *sframe = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(sframe), GTK_SHADOW_IN);
    GtkWidget *ssw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(ssw), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_container_add(GTK_CONTAINER(ssw), strip);
    gtk_container_add(GTK_CONTAINER(sframe), ssw);
    gtk_box_pack_start(GTK_BOX(v), sframe, FALSE, FALSE, 0);
    wzp.stack = gtk_stack_new();
    GtkWidget *group = NULL;
    GtkWidget *first_btn = NULL;
    for (size_t i = 0; i < G_N_ELEMENTS(sections); i++) {
        GtkWidget *b = group ? gtk_radio_button_new_from_widget(GTK_RADIO_BUTTON(group)) : gtk_radio_button_new(NULL);
        if (!group)
            group = b;
        gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(b), FALSE);
        GtkWidget *bb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        GtkWidget *ic = wzp_icon_widget(sections[i].icon, 40);
        gtk_widget_set_halign(ic, GTK_ALIGN_CENTER);
        gtk_box_pack_start(GTK_BOX(bb), ic, FALSE, FALSE, 0);
        GtkWidget *lb = gtk_label_new(_(sections[i].title));
        gtk_box_pack_start(GTK_BOX(bb), lb, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(b), bb);
        gtk_widget_set_size_request(b, 74, 70);
        gtk_widget_set_tooltip_text(b, _(sections[i].title));
        g_signal_connect(b, "toggled", G_CALLBACK(on_section), GINT_TO_POINTER((int)i));
        gtk_box_pack_start(GTK_BOX(strip), b, FALSE, FALSE, 0);
        GtkWidget *page = sections[i].build();
        gtk_container_set_border_width(GTK_CONTAINER(page), 4);
        /* Rolagem vertical: a página mais alta pedia 883 linhas e a janela
         * não cabia numa tela de 768. */
        GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);
        gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 600);
        gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_NONE);
        gtk_container_add(GTK_CONTAINER(scroll), page);
        gtk_stack_add_named(GTK_STACK(wzp.stack), scroll, sections[i].id);
        if (first && !strcmp(first, sections[i].id))
            first_btn = b;
    }
    section_label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(section_label), 0);
    gtk_box_pack_start(GTK_BOX(v), section_label, FALSE, FALSE, 0);
    GtkWidget *pf = gtk_frame_new(NULL);
    gtk_container_add(GTK_CONTAINER(pf), wzp.stack);
    gtk_box_pack_start(GTK_BOX(v), pf, TRUE, TRUE, 0);

    GtkWidget *bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(bottom), wzp_icon_widget(8, 32), FALSE, FALSE, 0);
    wzp.status = gtk_label_new(_("Changes are applied at once."));
    gtk_label_set_xalign(GTK_LABEL(wzp.status), 0);
    gtk_label_set_ellipsize(GTK_LABEL(wzp.status), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(bottom), wzp.status, TRUE, TRUE, 0);
    GtkWidget *ab = gtk_button_new_with_mnemonic(_("_About"));
    g_signal_connect(ab, "clicked", G_CALLBACK(on_about), NULL);
    gtk_box_pack_start(GTK_BOX(bottom), ab, FALSE, FALSE, 0);
    GtkWidget *close = gtk_button_new_with_mnemonic(_("_Close"));
    g_signal_connect_swapped(close, "clicked", G_CALLBACK(gtk_widget_destroy), wzp.window);
    gtk_box_pack_start(GTK_BOX(bottom), close, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(v), bottom, FALSE, FALSE, 0);

    gtk_widget_show_all(wzp.window);
    GtkWidget *sel = first_btn ? first_btn : group;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sel), TRUE);
    on_section(GTK_TOGGLE_BUTTON(sel), GINT_TO_POINTER(0));
    for (size_t i = 0; i < G_N_ELEMENTS(sections); i++)
        if (first && !strcmp(first, sections[i].id))
            on_section(GTK_TOGGLE_BUTTON(sel), GINT_TO_POINTER((int)i));
}

int
main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
#ifdef WZ_ENABLE_NLS
    bindtextdomain("wmake-zones", WZ_LOCALEDIR);
    bind_textdomain_codeset("wmake-zones", "UTF-8");
    textdomain("wmake-zones");
#endif
    gtk_init(&argc, &argv);
    wzp.path = wz_config_path();
    wz_config_defaults(&wzp.cfg);
    wz_config_load(&wzp.cfg, wzp.path);
    wz_theme_load(&wzp.theme, NULL);
    const char *first = "general";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--editor")) {
            GtkWidget *w = wzp_editor_window(NULL, argc > i + 1 ? argv[i + 1] : NULL);
            g_signal_connect(w, "destroy", G_CALLBACK(gtk_main_quit), NULL);
            gtk_main();
            return 0;
        }
        if (!strcmp(argv[i], "--about")) {
            about(NULL);
            return 0;
        }
        if (!strncmp(argv[i], "--", 2))
            first = argv[i] + 2;
    }
    build(first);
    gtk_main();
    return 0;
}
