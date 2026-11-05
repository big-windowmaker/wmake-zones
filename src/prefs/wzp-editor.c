/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Editor visual de zonas. Um monitor desenhado com a proporção do monitor
 * principal; clicar numa zona a seleciona, arrastar a fronteira entre duas
 * zonas muda a proporção. À direita, as ações da zona (dividir, excluir,
 * mover, nome e proporção) e as margens. Desfazer/refazer guardam a forma
 * textual da árvore.
 */
#define _GNU_SOURCE
#include "wz-alloc.h"
#include "wzp.h"
#include "wz-ipc.h"

#include <math.h>
#include <string.h>

typedef struct {
    wz_node *node;
    int k;
    int vertical; /* divisória vertical (nó H) */
    double pos;   /* coordenada no canvas */
    double a, b;  /* extensão */
    wz_rect box;  /* retângulo do nó */
} divider;

typedef struct {
    GtkWidget *win, *canvas, *combo, *name, *weight, *undo, *redo, *gap, *outer, *status;
    GtkWidget *b_split_h, *b_split_v, *b_delete, *b_before, *b_after;
    char *id;
    wz_node *tree;
    int sel;
    GPtrArray *undo_stack, *redo_stack;
    divider div[256];
    int ndiv;
    int drag; /* índice da divisória arrastada, -1 */
    char *drag_before;
    wz_rect screen;
    int dirty;
    int updating;
} editor;

static void refresh(editor *e);

/* ---- geometria no canvas ------------------------------------------------- */

static void
collect(editor *e, wz_node *n, wz_rect r)
{
    if (n->kind == WZ_LEAF)
        return;
    double total = 0;
    for (int i = 0; i < n->nchild; i++)
        total += n->child[i]->weight;
    int span = n->kind == WZ_HSPLIT ? r.w : r.h;
    int base = n->kind == WZ_HSPLIT ? r.x : r.y;
    double cum = 0;
    int prev = base;
    for (int i = 0; i < n->nchild; i++) {
        cum += n->child[i]->weight;
        int next = i == n->nchild - 1 ? base + span : base + (int)lround(span * (cum / total));
        wz_rect c = r;
        if (n->kind == WZ_HSPLIT) {
            c.x = prev;
            c.w = next - prev;
        } else {
            c.y = prev;
            c.h = next - prev;
        }
        if (i < n->nchild - 1 && e->ndiv < 256) {
            divider *d = &e->div[e->ndiv++];
            d->node = n;
            d->k = i;
            d->vertical = n->kind == WZ_HSPLIT;
            d->pos = next;
            d->a = d->vertical ? r.y : r.x;
            d->b = d->vertical ? r.y + r.h : r.x + r.w;
            d->box = r;
        }
        collect(e, n->child[i], c);
        prev = next;
    }
}

static void
layout_screen(editor *e)
{
    int w = gtk_widget_get_allocated_width(e->canvas), h = gtk_widget_get_allocated_height(e->canvas);
    /* Proporção do monitor principal. */
    double aspect = 16.0 / 9.0;
    GdkDisplay *d = gdk_display_get_default();
    GdkMonitor *m = d ? gdk_display_get_primary_monitor(d) : NULL;
    if (!m && d && gdk_display_get_n_monitors(d) > 0)
        m = gdk_display_get_monitor(d, 0);
    if (m) {
        GdkRectangle g;
        gdk_monitor_get_geometry(m, &g);
        if (g.height > 0)
            aspect = (double)g.width / g.height;
    }
    double mw = w - 40, mh = h - 60;
    if (mw / mh > aspect)
        mw = mh * aspect;
    else
        mh = mw / aspect;
    double x = (w - mw) / 2, y = (h - mh - 34) / 2;
    if (y < 6)
        y = 6;
    e->screen = (wz_rect){ (int)x + 10, (int)y + 10, (int)mw - 20, (int)mh - 24 };
    e->ndiv = 0;
    if (e->tree)
        collect(e, e->tree, e->screen);
}

static gboolean
draw(GtkWidget *w, cairo_t *cr, gpointer data)
{
    editor *e = data;
    int W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w);
    wzp_rgb(cr, 0x8a888d, 1);
    cairo_paint(cr);
    wzp_bevel(cr, 0, 0, W, H, 0);
    layout_screen(e);
    wz_rect s = e->screen;
    /* Moldura do monitor e pé. */
    wzp_tile(cr, s.x - 10, s.y - 10, s.w + 20, s.h + 24, 0xd0ced3, 0x9a989d);
    wzp_rgb(cr, 0x1c2024, 1);
    cairo_rectangle(cr, s.x, s.y, s.w, s.h);
    cairo_fill(cr);
    wzp_bevel(cr, s.x - 1, s.y - 1, s.w + 2, s.h + 2, 0);
    wzp_rgb(cr, 0x2a6a2a, 1);
    cairo_rectangle(cr, s.x + s.w - 14, s.y + s.h + 6, 6, 4);
    cairo_fill(cr);
    wzp_tile(cr, s.x + s.w / 2 - 40, s.y + s.h + 14, 80, 10, 0xc2c0c5, 0x828085);
    wzp_tile(cr, s.x + s.w / 2 - 70, s.y + s.h + 24, 140, 8, 0xc2c0c5, 0x828085);
    if (e->tree) {
        char *t = wz_serialize(e->tree);
        int gap = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(e->gap));
        (void)gap;
        wzp_draw_layout(cr, t, s.x + 3, s.y + 3, s.w - 6, s.h - 6, e->sel, 0x7fb0e0, 1);
        free(t);
    }
    /* Divisória sob o arrasto. */
    if (e->drag >= 0) {
        divider *d = &e->div[e->drag];
        wzp_rgb(cr, 0xe0a030, 0.9);
        if (d->vertical)
            cairo_rectangle(cr, d->pos - 2, d->a, 4, d->b - d->a);
        else
            cairo_rectangle(cr, d->a, d->pos - 2, d->b - d->a, 4);
        cairo_fill(cr);
    }
    return TRUE;
}

static int
divider_at(editor *e, double x, double y)
{
    int best = -1;
    double bd = 7;
    for (int i = 0; i < e->ndiv; i++) {
        divider *d = &e->div[i];
        double along = d->vertical ? y : x, across = d->vertical ? x : y;
        if (along < d->a || along > d->b)
            continue;
        double dist = fabs(across - d->pos);
        if (dist < bd) {
            bd = dist;
            best = i;
        }
    }
    return best;
}

static int
zone_at(editor *e, double x, double y)
{
    if (!e->tree)
        return -1;
    wz_rect zr[128];
    int n = wz_compute(e->tree, (wz_rect){ e->screen.x + 3, e->screen.y + 3, e->screen.w - 6, e->screen.h - 6 }, 2, 0,
                       zr, 128);
    for (int i = 0; i < n && i < 128; i++)
        if (wz_rect_contains(zr[i], (int)x, (int)y))
            return i;
    return -1;
}

/* ---- desfazer ------------------------------------------------------------ */

static void
push_undo(editor *e)
{
    g_ptr_array_add(e->undo_stack, wz_serialize(e->tree));
    g_ptr_array_set_size(e->redo_stack, 0);
    e->dirty = 1;
}

static void
restore_from(editor *e, GPtrArray *from, GPtrArray *to)
{
    if (from->len == 0)
        return;
    g_ptr_array_add(to, wz_serialize(e->tree));
    char *s = g_ptr_array_steal_index(from, from->len - 1);
    wz_node *t = wz_parse(s, NULL);
    free(s);
    if (t) {
        wz_free(e->tree);
        e->tree = t;
    }
    if (e->sel >= wz_leaf_count(e->tree))
        e->sel = wz_leaf_count(e->tree) - 1;
    e->dirty = 1;
    refresh(e);
}

static void
on_undo(GtkButton *b, editor *e)
{
    restore_from(e, e->undo_stack, e->redo_stack);
}

static void
on_redo(GtkButton *b, editor *e)
{
    restore_from(e, e->redo_stack, e->undo_stack);
}

/* ---- ações da zona ------------------------------------------------------- */

static wz_node *
sel_leaf(editor *e)
{
    return e->tree && e->sel >= 0 ? wz_leaf_nth(e->tree, e->sel) : NULL;
}

static void
split(editor *e, wz_kind kind)
{
    wz_node *l = sel_leaf(e);
    if (!l || wz_leaf_count(e->tree) >= 64)
        return;
    wz_node *p = l->parent;
    /* Dividir no outro sentido aninha um nível; além do que o leitor aceita,
     * o layout gravado seria descartado ao carregar. */
    int depth = 0;
    for (wz_node *a = l; a; a = a->parent)
        depth++;
    if (!(p && p->kind == kind) && depth + 1 >= WZ_MAX_DEPTH) {
        gtk_widget_error_bell(e->canvas);
        return;
    }
    push_undo(e);
    if (p && p->kind == kind) {
        /* Mesmo sentido do pai: vira irmã, dividindo o peso. */
        int k = 0;
        while (p->child[k] != l)
            k++;
        wz_node *n = wz_leaf_new(l->weight / 2);
        l->weight /= 2;
        wz_add_child(p, n);
        memmove(&p->child[k + 2], &p->child[k + 1], sizeof *p->child * (size_t)(p->nchild - k - 2));
        p->child[k + 1] = n;
    } else {
        wz_split_leaf(l, kind, 0);
    }
    refresh(e);
}

static void
on_split_h(GtkButton *b, editor *e)
{
    split(e, WZ_HSPLIT);
}

static void
on_split_v(GtkButton *b, editor *e)
{
    split(e, WZ_VSPLIT);
}

static void
on_delete(GtkButton *b, editor *e)
{
    wz_node *l = sel_leaf(e);
    if (!l || wz_leaf_count(e->tree) <= 1)
        return;
    push_undo(e);
    e->tree = wz_remove_leaf(e->tree, l);
    if (e->sel >= wz_leaf_count(e->tree))
        e->sel = wz_leaf_count(e->tree) - 1;
    refresh(e);
}

static void
move(editor *e, int d)
{
    wz_node *l = sel_leaf(e);
    if (!l || !l->parent)
        return;
    wz_node *p = l->parent;
    int k = 0;
    while (p->child[k] != l)
        k++;
    int o = k + d;
    if (o < 0 || o >= p->nchild)
        return;
    push_undo(e);
    p->child[k] = p->child[o];
    p->child[o] = l;
    e->sel = wz_leaf_index(e->tree, l);
    refresh(e);
}

static void
on_before(GtkButton *b, editor *e)
{
    move(e, -1);
}

static void
on_after(GtkButton *b, editor *e)
{
    move(e, 1);
}

static void
on_name(GtkEntry *en, editor *e)
{
    if (e->updating)
        return;
    wz_node *l = sel_leaf(e);
    if (!l)
        return;
    const char *t = gtk_entry_get_text(en);
    if (g_strcmp0(t, l->name ? l->name : "") == 0)
        return;
    push_undo(e);
    free(l->name);
    /* Sem aspas: elas delimitam o nome na forma textual. */
    char *clean = g_strdup(t);
    g_strdelimit(clean, "\"", '\'');
    l->name = *clean ? wz_xstrndup(clean, 64) : NULL;
    g_free(clean);
    gtk_widget_queue_draw(e->canvas);
}

static void
on_weight(GtkSpinButton *sb, editor *e)
{
    if (e->updating)
        return;
    wz_node *l = sel_leaf(e);
    if (!l || !l->parent)
        return;
    double pct = gtk_spin_button_get_value(sb);
    double others = 0;
    for (int i = 0; i < l->parent->nchild; i++)
        if (l->parent->child[i] != l)
            others += l->parent->child[i]->weight;
    if (others <= 0 || pct >= 100)
        return;
    push_undo(e);
    l->weight = pct * others / (100 - pct);
    gtk_widget_queue_draw(e->canvas);
}

/* ---- mouse --------------------------------------------------------------- */

static gboolean
on_press(GtkWidget *w, GdkEventButton *ev, editor *e)
{
    if (ev->button != 1)
        return FALSE;
    int d = divider_at(e, ev->x, ev->y);
    if (d >= 0) {
        e->drag = d;
        e->drag_before = wz_serialize(e->tree);
        return TRUE;
    }
    int z = zone_at(e, ev->x, ev->y);
    if (z >= 0) {
        e->sel = z;
        refresh(e);
        if (ev->type == GDK_2BUTTON_PRESS)
            gtk_widget_grab_focus(e->name);
    }
    return TRUE;
}

static gboolean
on_motion(GtkWidget *w, GdkEventMotion *ev, editor *e)
{
    GdkWindow *win = gtk_widget_get_window(w);
    if (e->drag >= 0) {
        divider *d = &e->div[e->drag];
        wz_node *n = d->node;
        int k = d->k;
        double f = d->vertical ? (ev->x - d->box.x) / d->box.w : (ev->y - d->box.y) / d->box.h;
        wz_set_divider(n, k, f < 0 ? 0 : f > 1 ? 1 : f, 0.04);
        layout_screen(e);
        /* A lista de divisórias foi refeita: reencontra a arrastada. */
        for (int i = 0; i < e->ndiv; i++)
            if (e->div[i].node == n && e->div[i].k == k)
                e->drag = i;
        gtk_widget_queue_draw(w);
        return TRUE;
    }
    int d = divider_at(e, ev->x, ev->y);
    const char *name = d < 0 ? "default" : e->div[d].vertical ? "col-resize" : "row-resize";
    GdkCursor *c = gdk_cursor_new_from_name(gdk_window_get_display(win), name);
    gdk_window_set_cursor(win, c);
    if (c)
        g_object_unref(c);
    return TRUE;
}

static gboolean
on_release(GtkWidget *w, GdkEventButton *ev, editor *e)
{
    if (e->drag >= 0) {
        char *now = wz_serialize(e->tree);
        if (e->drag_before && strcmp(now, e->drag_before) != 0) {
            g_ptr_array_add(e->undo_stack, e->drag_before);
            g_ptr_array_set_size(e->redo_stack, 0);
            e->drag_before = NULL;
            e->dirty = 1;
        }
        free(now);
        free(e->drag_before);
        e->drag_before = NULL;
        e->drag = -1;
        refresh(e);
    }
    return TRUE;
}

/* ---- layouts ------------------------------------------------------------- */

static void
load(editor *e, const char *id)
{
    const wz_layout_def *l = wz_config_layout(&wzp.cfg, id);
    if (!l)
        l = &wzp.cfg.layouts[0];
    g_free(e->id);
    e->id = g_strdup(l->id);
    wz_free(e->tree);
    e->tree = wz_parse(l->tree, NULL);
    if (!e->tree)
        e->tree = wz_leaf_new(1);
    e->sel = 0;
    g_ptr_array_set_size(e->undo_stack, 0);
    g_ptr_array_set_size(e->redo_stack, 0);
    e->dirty = 0;
    refresh(e);
}

static void
fill_combo(editor *e)
{
    e->updating = 1;
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(e->combo));
    for (int i = 0; i < wzp.cfg.nlayouts; i++)
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(e->combo), wzp.cfg.layouts[i].id,
                                  wzp_layout_label(&wzp.cfg.layouts[i]));
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(e->combo), e->id);
    e->updating = 0;
}

static int
confirm(editor *e, const char *msg)
{
    GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(e->win), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                                          GTK_BUTTONS_YES_NO, "%s", msg);
    int r = gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
    return r == GTK_RESPONSE_YES;
}

static char *
ask(editor *e, const char *title, const char *initial)
{
    GtkWidget *d = gtk_dialog_new_with_buttons(title, GTK_WINDOW(e->win), GTK_DIALOG_MODAL, _("_Cancel"),
                                               GTK_RESPONSE_CANCEL, _("_OK"), GTK_RESPONSE_OK, NULL);
    GtkWidget *en = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(en), initial ? initial : "");
    gtk_entry_set_activates_default(GTK_ENTRY(en), TRUE);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(d));
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);
    gtk_box_pack_start(GTK_BOX(box), en, FALSE, FALSE, 0);
    gtk_widget_show_all(d);
    char *r = NULL;
    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        r = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(en))));
        if (!*r) {
            g_free(r);
            r = NULL;
        }
    }
    gtk_widget_destroy(d);
    return r;
}

static char *
new_id(const char *name)
{
    /* Id estável e seguro para o nome da seção; único. */
    GString *s = g_string_new(NULL);
    for (const char *p = name; *p && s->len < 40; p++)
        g_string_append_c(s, g_ascii_isalnum(*p) ? g_ascii_tolower(*p) : '-');
    if (!s->len)
        g_string_append(s, "layout");
    char *base = g_string_free(s, FALSE);
    char *id = g_strdup(base);
    for (int i = 2; wz_config_layout(&wzp.cfg, id); i++) {
        g_free(id);
        id = g_strdup_printf("%s-%d", base, i);
    }
    g_free(base);
    return id;
}

static void
save_layout(editor *e)
{
    char sec[160];
    g_snprintf(sec, sizeof sec, "Layout %s", e->id);
    char *t = wz_serialize(e->tree);
    wzp_set(sec, "Tree", t);
    free(t);
    wzp_set_int("General", "Gap", gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(e->gap)));
    wzp_set_int("General", "OuterGap", gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(e->outer)));
    wzp_reload_config();
    wzp_reload_daemon();
    e->dirty = 0;
    gtk_label_set_text(GTK_LABEL(e->status), _("Saved."));
}

static void
on_save(GtkButton *b, editor *e)
{
    save_layout(e);
}

static void
on_combo(GtkComboBox *c, editor *e)
{
    if (e->updating)
        return;
    const char *id = gtk_combo_box_get_active_id(c);
    if (!id || !g_strcmp0(id, e->id))
        return;
    if (e->dirty && !confirm(e, _("Discard the changes to this layout?"))) {
        e->updating = 1;
        gtk_combo_box_set_active_id(c, e->id);
        e->updating = 0;
        return;
    }
    load(e, id);
}

static void
create(editor *e, const char *name, const char *tree)
{
    char *id = new_id(name);
    char sec[160];
    g_snprintf(sec, sizeof sec, "Layout %s", id);
    wzp_set(sec, "Name", name);
    wzp_set(sec, "Tree", tree);
    wzp_reload_config();
    g_free(e->id);
    e->id = id;
    fill_combo(e);
    load(e, id);
}

static void
on_new(GtkButton *b, editor *e)
{
    char *n = ask(e, _("New layout"), _("My layout"));
    if (n)
        create(e, n, "H(1,1)");
    g_free(n);
}

static void
on_dup(GtkButton *b, editor *e)
{
    char *def = g_strdup_printf(_("Copy of %s"), wzp_layout_label(wz_config_layout(&wzp.cfg, e->id)));
    char *n = ask(e, _("Duplicate layout"), def);
    if (n) {
        char *t = wz_serialize(e->tree);
        create(e, n, t);
        free(t);
    }
    g_free(n);
    g_free(def);
}

static void
on_rename(GtkButton *b, editor *e)
{
    char *n = ask(e, _("Rename layout"), wzp_layout_label(wz_config_layout(&wzp.cfg, e->id)));
    if (n) {
        char sec[160];
        g_snprintf(sec, sizeof sec, "Layout %s", e->id);
        wzp_set(sec, "Name", n);
        wzp_reload_config();
        fill_combo(e);
        wzp_reload_daemon();
    }
    g_free(n);
}

static void
on_remove(GtkButton *b, editor *e)
{
    const wz_layout_def *l = wz_config_layout(&wzp.cfg, e->id);
    if (!l)
        return;
    char sec[160];
    g_snprintf(sec, sizeof sec, "Layout %s", e->id);
    if (wz_builtin_label(e->id)) {
        if (!confirm(e, _("Restore this built-in layout to its original form?")))
            return;
    } else if (!confirm(e, _("Delete this layout?"))) {
        return;
    }
    wzp_set(sec, "Tree", NULL);
    wzp_set(sec, "Name", NULL);
    wzp_reload_config();
    if (!wz_config_layout(&wzp.cfg, e->id)) {
        g_free(e->id);
        e->id = g_strdup(wzp.cfg.layouts[0].id);
    }
    fill_combo(e);
    load(e, e->id);
    wzp_reload_daemon();
}

static void
on_preview(GtkButton *b, editor *e)
{
    char *t = wz_serialize(e->tree);
    char cmd[WZ_IPC_MAX];
    g_snprintf(cmd, sizeof cmd, "preview %s", t);
    free(t);
    char out[256];
    if (wzp_request(cmd, out, sizeof out) != 0)
        gtk_label_set_text(GTK_LABEL(e->status), _("The wmake-zones service is not running: turn Zones on to preview."));
}

static void
on_export(GtkButton *b, editor *e)
{
    GtkFileChooserNative *fc = gtk_file_chooser_native_new(_("Export layout"), GTK_WINDOW(e->win),
                                                           GTK_FILE_CHOOSER_ACTION_SAVE, _("_Export"), NULL);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(fc), TRUE);
    char *fn = g_strdup_printf("%s.wzlayout", e->id);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(fc), fn);
    g_free(fn);
    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));
        char *t = wz_serialize(e->tree);
        char *txt = g_strdup_printf("# wmake-zones layout\n[Layout %s]\nName=%s\nTree=%s\n", e->id,
                                    wzp_layout_label(wz_config_layout(&wzp.cfg, e->id)), t);
        GError *err = NULL;
        if (!g_file_set_contents(path, txt, -1, &err)) {
            gtk_label_set_text(GTK_LABEL(e->status), err->message);
            g_error_free(err);
        } else {
            gtk_label_set_text(GTK_LABEL(e->status), _("Exported."));
        }
        g_free(txt);
        free(t);
        g_free(path);
    }
    g_object_unref(fc);
}

typedef struct {
    editor *e;
    int count;
    char *last;
} import_ctx;

static void
import_cb(const char *sec, const char *k, const char *v, void *ud)
{
    import_ctx *c = ud;
    if (strncmp(sec, "Layout ", 7) != 0 || !sec[7])
        return;
    if (!strcmp(k, "Tree")) {
        wz_node *t = wz_parse(v, NULL);
        if (!t)
            return;
        wz_free(t);
        wzp_set(sec, "Tree", v);
        c->count++;
        g_free(c->last);
        c->last = g_strdup(sec + 7);
    } else if (!strcmp(k, "Name")) {
        wzp_set(sec, "Name", v);
    }
}

static void
on_import(GtkButton *b, editor *e)
{
    GtkFileChooserNative *fc = gtk_file_chooser_native_new(_("Import layouts"), GTK_WINDOW(e->win),
                                                           GTK_FILE_CHOOSER_ACTION_OPEN, _("_Import"), NULL);
    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));
        import_ctx c = { e, 0, NULL };
        wz_ini_parse_file(path, import_cb, &c);
        wzp_reload_config();
        if (c.last) {
            g_free(e->id);
            e->id = c.last;
        }
        fill_combo(e);
        load(e, e->id);
        char *msg = g_strdup_printf(g_dngettext(NULL, "%d layout imported.", "%d layouts imported.", c.count),
                                    c.count);
        gtk_label_set_text(GTK_LABEL(e->status), msg);
        g_free(msg);
        g_free(path);
        wzp_reload_daemon();
    }
    g_object_unref(fc);
}

static gboolean
on_delete_event(GtkWidget *w, GdkEvent *ev, editor *e)
{
    if (e->dirty) {
        GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(e->win), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                                              GTK_BUTTONS_NONE, "%s", _("Save the changes to this layout?"));
        gtk_dialog_add_buttons(GTK_DIALOG(d), _("_Discard"), GTK_RESPONSE_NO, _("_Cancel"), GTK_RESPONSE_CANCEL,
                               _("_Save"), GTK_RESPONSE_YES, NULL);
        int r = gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);
        if (r == GTK_RESPONSE_CANCEL || r == GTK_RESPONSE_DELETE_EVENT)
            return TRUE;
        if (r == GTK_RESPONSE_YES)
            save_layout(e);
    }
    return FALSE;
}

static void
on_destroy(GtkWidget *w, editor *e)
{
    wz_free(e->tree);
    g_free(e->id);
    g_ptr_array_unref(e->undo_stack);
    g_ptr_array_unref(e->redo_stack);
    free(e->drag_before);
    g_free(e);
}

static gboolean
on_key(GtkWidget *w, GdkEventKey *ev, editor *e)
{
    if (gtk_widget_has_focus(e->name) || gtk_widget_has_focus(e->weight))
        return FALSE;
    /* Com uma divisória sendo arrastada, nada que troque a árvore: o
     * arrasto guarda ponteiros para os nós dela. */
    if (e->drag >= 0)
        return TRUE;
    gboolean ctrl = (ev->state & GDK_CONTROL_MASK) != 0;
    if (ctrl && (ev->keyval == GDK_KEY_z || ev->keyval == GDK_KEY_Z)) {
        if (ev->state & GDK_SHIFT_MASK)
            on_redo(NULL, e);
        else
            on_undo(NULL, e);
        return TRUE;
    }
    if (ctrl && ev->keyval == GDK_KEY_y) {
        on_redo(NULL, e);
        return TRUE;
    }
    if (ctrl && ev->keyval == GDK_KEY_s) {
        save_layout(e);
        return TRUE;
    }
    if (ev->keyval == GDK_KEY_Delete) {
        on_delete(NULL, e);
        return TRUE;
    }
    return FALSE;
}

static void
refresh(editor *e)
{
    e->updating = 1;
    wz_node *l = sel_leaf(e);
    gtk_entry_set_text(GTK_ENTRY(e->name), l && l->name ? l->name : "");
    double pct = 100;
    if (l && l->parent) {
        double tot = 0;
        for (int i = 0; i < l->parent->nchild; i++)
            tot += l->parent->child[i]->weight;
        pct = 100 * l->weight / tot;
    }
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(e->weight), pct);
    gtk_widget_set_sensitive(e->weight, l && l->parent);
    gtk_widget_set_sensitive(e->b_delete, e->tree && wz_leaf_count(e->tree) > 1);
    int k = -1;
    if (l && l->parent)
        for (int i = 0; i < l->parent->nchild; i++)
            if (l->parent->child[i] == l)
                k = i;
    gtk_widget_set_sensitive(e->b_before, k > 0);
    gtk_widget_set_sensitive(e->b_after, l && l->parent && k < l->parent->nchild - 1);
    gtk_widget_set_sensitive(e->undo, e->undo_stack->len > 0);
    gtk_widget_set_sensitive(e->redo, e->redo_stack->len > 0);
    char *msg = g_strdup_printf(_("Zone %d of %d"), e->sel + 1, e->tree ? wz_leaf_count(e->tree) : 0);
    gtk_label_set_text(GTK_LABEL(e->status), msg);
    g_free(msg);
    e->updating = 0;
    gtk_widget_queue_draw(e->canvas);
}

static GtkWidget *
button(const char *label, const char *tip, GCallback cb, editor *e)
{
    GtkWidget *b = gtk_button_new_with_mnemonic(label);
    if (tip)
        gtk_widget_set_tooltip_text(b, tip);
    g_signal_connect(b, "clicked", cb, e);
    return b;
}

static GtkWidget *
frame(const char *title, GtkWidget *child)
{
    GtkWidget *f = gtk_frame_new(title);
    gtk_container_set_border_width(GTK_CONTAINER(child), 6);
    gtk_container_add(GTK_CONTAINER(f), child);
    return f;
}

GtkWidget *
wzp_editor_window(GtkWindow *parent, const char *layout_id)
{
    editor *e = g_new0(editor, 1);
    e->drag = -1;
    e->undo_stack = g_ptr_array_new_with_free_func(free);
    e->redo_stack = g_ptr_array_new_with_free_func(free);
    e->id = g_strdup(layout_id ? layout_id : wzp.cfg.layout);

    e->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(e->win), _("Zone editor — wmake-zones"));
    gtk_window_set_default_size(GTK_WINDOW(e->win), 860, 560);
    gtk_window_set_icon_name(GTK_WINDOW(e->win), "wmake-zones");
    if (parent)
        gtk_window_set_transient_for(GTK_WINDOW(e->win), parent);
    g_signal_connect(e->win, "delete-event", G_CALLBACK(on_delete_event), e);
    g_signal_connect(e->win, "destroy", G_CALLBACK(on_destroy), e);
    g_signal_connect(e->win, "key-press-event", G_CALLBACK(on_key), e);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
    gtk_container_add(GTK_CONTAINER(e->win), vbox);

    /* Linha dos layouts. */
    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(top), gtk_label_new(_("Layout:")), FALSE, FALSE, 0);
    e->combo = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(top), e->combo, TRUE, TRUE, 0);
    g_signal_connect(e->combo, "changed", G_CALLBACK(on_combo), e);
    gtk_box_pack_start(GTK_BOX(top), button(_("_New"), _("Create a new layout"), G_CALLBACK(on_new), e), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), button(_("D_uplicate"), NULL, G_CALLBACK(on_dup), e), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), button(_("_Rename"), NULL, G_CALLBACK(on_rename), e), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top),
                       button(_("Delete / _Restore"), _("Delete a layout you created, or restore a built-in one"),
                              G_CALLBACK(on_remove), e),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), button(_("_Import..."), NULL, G_CALLBACK(on_import), e), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), button(_("E_xport..."), NULL, G_CALLBACK(on_export), e), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), top, FALSE, FALSE, 0);

    GtkWidget *mid = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(vbox), mid, TRUE, TRUE, 0);
    e->canvas = gtk_drawing_area_new();
    gtk_widget_set_size_request(e->canvas, 520, 360);
    gtk_widget_add_events(e->canvas, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
    g_signal_connect(e->canvas, "draw", G_CALLBACK(draw), e);
    g_signal_connect(e->canvas, "button-press-event", G_CALLBACK(on_press), e);
    g_signal_connect(e->canvas, "motion-notify-event", G_CALLBACK(on_motion), e);
    g_signal_connect(e->canvas, "button-release-event", G_CALLBACK(on_release), e);
    gtk_box_pack_start(GTK_BOX(mid), e->canvas, TRUE, TRUE, 0);

    GtkWidget *side = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_size_request(side, 230, -1);
    gtk_box_pack_start(GTK_BOX(mid), side, FALSE, FALSE, 0);

    GtkWidget *zb = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(zb), 4);
    gtk_grid_set_column_spacing(GTK_GRID(zb), 4);
    gtk_grid_set_column_homogeneous(GTK_GRID(zb), TRUE);
    e->b_split_h = button(_("Split ◫"), _("Split the zone side by side"), G_CALLBACK(on_split_h), e);
    e->b_split_v = button(_("Split ⊟"), _("Split the zone top and bottom"), G_CALLBACK(on_split_v), e);
    e->b_delete = button(_("Delete zone"), _("Remove the zone; its neighbours take the space"), G_CALLBACK(on_delete), e);
    e->b_before = button(_("◀ Earlier"), _("Swap with the previous zone (numbering)"), G_CALLBACK(on_before), e);
    e->b_after = button(_("Later ▶"), _("Swap with the next zone (numbering)"), G_CALLBACK(on_after), e);
    gtk_grid_attach(GTK_GRID(zb), e->b_split_h, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(zb), e->b_split_v, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(zb), e->b_before, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(zb), e->b_after, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(zb), e->b_delete, 0, 2, 2, 1);
    GtkWidget *nl = gtk_label_new(_("Name:"));
    gtk_widget_set_halign(nl, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(zb), nl, 0, 3, 2, 1);
    e->name = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(e->name), 64);
    gtk_entry_set_placeholder_text(GTK_ENTRY(e->name), _("optional, e.g. Editor"));
    g_signal_connect(e->name, "changed", G_CALLBACK(on_name), e);
    gtk_grid_attach(GTK_GRID(zb), e->name, 0, 4, 2, 1);
    GtkWidget *wl = gtk_label_new(_("Share of the row (%):"));
    gtk_widget_set_halign(wl, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(zb), wl, 0, 5, 2, 1);
    e->weight = gtk_spin_button_new_with_range(5, 95, 1);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(e->weight), 0);
    g_signal_connect(e->weight, "value-changed", G_CALLBACK(on_weight), e);
    gtk_grid_attach(GTK_GRID(zb), e->weight, 0, 6, 2, 1);
    gtk_box_pack_start(GTK_BOX(side), frame(_("Selected zone"), zb), FALSE, FALSE, 0);

    GtkWidget *mg = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(mg), 4);
    gtk_grid_set_column_spacing(GTK_GRID(mg), 6);
    gtk_grid_attach(GTK_GRID(mg), gtk_label_new(_("Between zones (px)")), 0, 0, 1, 1);
    e->gap = gtk_spin_button_new_with_range(0, 64, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(e->gap), wzp.cfg.gap_in);
    gtk_grid_attach(GTK_GRID(mg), e->gap, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(mg), gtk_label_new(_("At the edges (px)")), 0, 1, 1, 1);
    e->outer = gtk_spin_button_new_with_range(0, 64, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(e->outer), wzp.cfg.gap_out);
    gtk_grid_attach(GTK_GRID(mg), e->outer, 1, 1, 1, 1);
    gtk_box_pack_start(GTK_BOX(side), frame(_("Spacing"), mg), FALSE, FALSE, 0);

    GtkWidget *ur = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    e->undo = button(_("_Undo"), _("Ctrl+Z"), G_CALLBACK(on_undo), e);
    e->redo = button(_("Re_do"), _("Ctrl+Shift+Z"), G_CALLBACK(on_redo), e);
    gtk_box_pack_start(GTK_BOX(ur), e->undo, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(ur), e->redo, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(side), ur, FALSE, FALSE, 0);

    GtkWidget *hint = gtk_label_new(_("Click a zone to select it. Drag the line between two zones to resize them. "
                                      "Double-click to name a zone."));
    gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
    gtk_label_set_xalign(GTK_LABEL(hint), 0);
    gtk_box_pack_start(GTK_BOX(side), hint, FALSE, FALSE, 0);

    GtkWidget *bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    e->status = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(e->status), 0);
    gtk_label_set_ellipsize(GTK_LABEL(e->status), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(bottom), e->status, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(bottom),
                       button(_("_Preview on screen"), _("Show the zones on the screen for a moment"),
                              G_CALLBACK(on_preview), e),
                       FALSE, FALSE, 0);
    GtkWidget *save = button(_("_Save"), _("Ctrl+S"), G_CALLBACK(on_save), e);
    gtk_box_pack_start(GTK_BOX(bottom), save, FALSE, FALSE, 0);
    GtkWidget *close = gtk_button_new_with_mnemonic(_("_Close"));
    g_signal_connect_swapped(close, "clicked", G_CALLBACK(gtk_window_close), e->win);
    gtk_box_pack_start(GTK_BOX(bottom), close, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), bottom, FALSE, FALSE, 0);

    fill_combo(e);
    load(e, e->id);
    gtk_widget_show_all(e->win);
    return e->win;
}
