/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Detecção do arrasto de janelas e encaixe nas zonas.
 *
 * O Window Maker não avisa clientes externos do início e do fim de um
 * movimento. No movimento opaco (o padrão, OpaqueMove=YES) ele move o quadro
 * a cada passo, então a raiz recebe ConfigureNotify do quadro; os eventos
 * brutos do XInput2 (XI_RawButtonPress/Release, entregues mesmo com o
 * ponteiro e o teclado agarrados pelo wmaker) dizem quando o botão foi
 * apertado e solto. Nada disso exige grabs nossos nem polling:
 *
 *   botão 1 apertado sobre um quadro   -> ARMADO
 *   o quadro anda (mesmo tamanho)      -> MOVENDO: overlay depois do atraso
 *   o quadro muda de tamanho           -> REDIMENSIONANDO (só no mosaico)
 *   botão solto                        -> encaixa, restaura ou nada
 *
 * Esc durante o arrasto (o wmaker ignora o Esc no movimento com o mouse)
 * cancela: as zonas somem e, ao soltar, a janela volta para onde estava.
 * Eventos de teclado brutos só são pedidos enquanto há um arrasto.
 */
#define _GNU_SOURCE
#include "wz-alloc.h"
#include "wzx.h"

#include <X11/XKBlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static KeyCode kc_esc, kc_super[2], kc_alt[2], kc_ctrl[2], kc_shift[2];

void
wzx_drag_init(wzx *z)
{
    kc_esc = XKeysymToKeycode(z->dpy, XK_Escape);
    kc_super[0] = XKeysymToKeycode(z->dpy, XK_Super_L);
    kc_super[1] = XKeysymToKeycode(z->dpy, XK_Super_R);
    kc_alt[0] = XKeysymToKeycode(z->dpy, XK_Alt_L);
    kc_alt[1] = XKeysymToKeycode(z->dpy, XK_Alt_R);
    kc_ctrl[0] = XKeysymToKeycode(z->dpy, XK_Control_L);
    kc_ctrl[1] = XKeysymToKeycode(z->dpy, XK_Control_R);
    kc_shift[0] = XKeysymToKeycode(z->dpy, XK_Shift_L);
    kc_shift[1] = XKeysymToKeycode(z->dpy, XK_Shift_R);
}

void
wzx_xi_select(wzx *z)
{
    if (!z->has_xi)
        return;
    unsigned char bits[XIMaskLen(XI_LASTEVENT)];
    memset(bits, 0, sizeof bits);
    int active = z->cfg.zones || z->cfg.autotile;
    for (int i = 0; i < z->nmon && !active; i++)
        active = wzx_tile_active_for(z, i);
    if (active) {
        XISetMask(bits, XI_RawButtonPress);
        XISetMask(bits, XI_RawButtonRelease);
    } else if (z->drag != DRAG_IDLE) {
        /* Desligado no meio de um arrasto: o soltar não viria mais. */
        z->drag = DRAG_IDLE;
        z->drag_w = NULL;
        z->drag_frame = None;
        wzx_overlay_hide(z);
    }
    int keys = z->drag == DRAG_MOVING;
    if (keys) {
        XISetMask(bits, XI_RawKeyPress);
        XISetMask(bits, XI_RawKeyRelease);
    }
    XIEventMask m = { XIAllMasterDevices, sizeof bits, bits };
    XISelectEvents(z->dpy, z->root, &m, 1);
    z->raw_keys_selected = keys;
}

static int
is_mod_down(wzx *z)
{
    switch (z->cfg.modifier) {
    case WZ_MOD_ALT: return z->alt_down;
    case WZ_MOD_CTRL: return z->ctrl_down;
    case WZ_MOD_SHIFT: return z->shift_down;
    default: return z->super_down;
    }
}

static void
reset_drag(wzx *z)
{
    z->drag = DRAG_IDLE;
    z->drag_w = NULL;
    z->drag_frame = None;
    z->drag_cancel = 0;
    z->drag_dirty = 0;
    z->drag_show_at = 0;
    z->dwell_at = 0;
    z->strip_wait_leave = 0;
    z->strip_latched = 0;
    free(z->drag_layout);
    free(z->drag_tree);
    z->drag_layout = z->drag_tree = NULL;
    wzx_overlay_hide(z);
    if (z->raw_keys_selected)
        wzx_xi_select(z);
}

/* ---- histórico ---------------------------------------------------------- */

static void
item_of(wzx *z, wzx_win *w, wz_hist_item *it)
{
    memset(it, 0, sizeof *it);
    it->win = (uintptr_t)w->client;
    it->r = w->outer;
    it->zone = (int)wzx_get_zone_prop(z, w);
    it->order = w->order;
    it->flags = (w->tiled ? WZ_HIST_TILED : 0) | (w->user_float ? WZ_HIST_USER_FLOAT : 0);
    if (wzx_get_restore(z, w, &it->restore))
        it->flags |= WZ_HIST_HAS_RESTORE;
}

wz_hist_entry *
wzx_history_begin(wzx *z)
{
    wz_history_set_depth(&z->history, z->cfg.history_depth);
    return wz_history_begin(&z->history);
}

void
wzx_history_add(wzx *z, wz_hist_entry *e, wzx_win *w)
{
    if (!e || !w)
        return;
    wz_hist_item it;
    item_of(z, w, &it);
    wz_hist_add(e, &it);
}

void
wzx_history_end(wzx *z)
{
    wz_history_commit(&z->history);
}

void
wzx_history_push(wzx *z, wzx_win **ws, int n)
{
    if (n <= 0)
        return;
    wz_hist_entry *e = wzx_history_begin(z);
    for (int i = 0; i < n; i++)
        wzx_history_add(z, e, ws[i]);
    wzx_history_end(z);
}

/* Um retângulo de um monitor que saiu volta para dentro do mais perto: o
 * histórico nunca deixa uma janela fora de alcance. */
static wz_rect
reachable(wzx *z, wz_rect r)
{
    for (int i = 0; i < z->nmon; i++)
        if (wz_rect_contains(z->mon[i].geom, r.x + r.w / 2, r.y + r.h / 2))
            return r;
    int m = wzx_monitor_of(z, r);
    if (m < 0 || m >= z->nmon)
        return r;
    wz_rect a = z->mon[m].work;
    if (r.w > a.w)
        r.w = a.w;
    if (r.h > a.h)
        r.h = a.h;
    r.x = r.x < a.x ? a.x : r.x + r.w > a.x + a.w ? a.x + a.w - r.w : r.x;
    r.y = r.y < a.y ? a.y : r.y + r.h > a.y + a.h ? a.y + a.h - r.h : r.y;
    return r;
}

int
wzx_history_step(wzx *z, int redo)
{
    wz_hist_entry e;
    if (!wz_history_pop(&z->history, redo, &e))
        return 0;
    wz_hist_entry *inv = wz_history_push_inverse(&z->history, !redo);
    for (int i = 0; i < e.ncfg; i++) {
        char *cur = wzx_cfg_get(z, e.cfg[i].section, e.cfg[i].key);
        wz_hist_add_cfg(inv, e.cfg[i].section, e.cfg[i].key, cur);
        free(cur);
        wzx_save_cfg(z, e.cfg[i].section, e.cfg[i].key, e.cfg[i].value);
    }
    if (e.has_aux && e.n > 0) {
        wzx_win *w = wzx_find(z, (Window)e.items[0].win);
        double *ratio = w ? wzx_tile_ratio_of(z, w) : NULL;
        if (ratio) {
            wz_hist_set_aux(inv, (long)(*ratio * 10000 + 0.5));
            *ratio = e.aux / 10000.0;
        }
    }
    if (e.ncfg)
        wzx_apply_config(z);
    wzx_update_workarea(z);
    for (int i = 0; i < e.n; i++) {
        const wz_hist_item *it = &e.items[i];
        wzx_win *w = wzx_find(z, (Window)it->win);
        if (!w)
            continue;
        wzx_refresh_geom(z, w);
        wzx_history_add(z, inv, w);
        w->order = it->order;
        w->expect_valid = 0;
        if (it->flags & WZ_HIST_TILED) {
            /* Volta para o mosaico: a célula vem do layout, na ordem antiga. */
            w->user_float = 0;
            w->rule_float = 0;
            continue;
        }
        wz_rect r = reachable(z, it->r);
        int mon = wzx_monitor_of(z, r);
        w->user_float = (it->flags & WZ_HIST_USER_FLOAT) || (wzx_tile_active_for(z, mon) && !w->rule_float);
        if (w->tiled) {
            w->tiled = 0;
            w->tile_mon = -1;
            wzx_tile_forget(z, w);
        }
        wzx_move_outer(z, w, r);
        wzx_set_zone_prop(z, w, it->zone);
        if (it->flags & WZ_HIST_HAS_RESTORE)
            wzx_set_restore(z, w, it->restore);
        else
            wzx_clear_restore(z, w);
    }
    wz_history_commit(&z->history);
    wz_hist_entry_free(&e);
    wzx_tile_mark(z);
    wzx_dockapp_redraw(z);
    return 1;
}

/* ---- encaixe ------------------------------------------------------------ */

void
wzx_snap(wzx *z, wzx_win *w, wz_rect target, long zone, int record)
{
    if (!w)
        return;
    if (record)
        wzx_history_push(z, &w, 1);
    wz_limits lim = { 0 };
    int ew = w->ext[0] + w->ext[1], eh = w->ext[2] + w->ext[3];
    if (w->hints.min_w > 0)
        lim.min_w = w->hints.min_w + ew;
    if (w->hints.min_h > 0)
        lim.min_h = w->hints.min_h + eh;
    if (w->hints.max_w > 0)
        lim.max_w = w->hints.max_w + ew;
    if (w->hints.max_h > 0)
        lim.max_h = w->hints.max_h + eh;
    wz_rect r = target;
    if (wz_fit(&r, &lim) == WZ_FIT_CONFLICT) {
        /* Política: o mínimo do aplicativo vence; a janela fica na origem
         * da zona com o menor tamanho que ele aceita. */
        r.w = r.w < lim.min_w ? lim.min_w : r.w;
        r.h = r.h < lim.min_h ? lim.min_h : r.h;
    }
    wzx_move_outer(z, w, r);
    wzx_set_zone_prop(z, w, zone);
}

static void
finish_move(wzx *z, int px, int py)
{
    wzx_win *w = z->drag_w;
    if (!w)
        return;
    wzx_refresh_geom(z, w);
    if (z->drag_cancel) {
        if (w->tiled)
            wzx_tile_mark(z);
        else
            wzx_move_outer(z, w, z->drag_start);
        return;
    }
    if (z->ov.shown && z->ov.mode == 1) {
        wzx_tile_drop(z, w, px, py);
        return;
    }
    /* Solta sobre o painel de layouts: numa zona de uma miniatura, encaixa
     * direto nela; no resto do painel, fica onde foi solta. */
    if (z->ov.shown && z->ov.strip && !w->tiled) {
        int mini = -1;
        int i = wzx_strip_hit(z, px, py, &mini);
        if (i >= 0 && mini >= 0) {
            wz_rect zones[WZX_MAX_ZONES];
            int n = wzx_zones_of_tree(z, z->ov.mon, z->ov.strip_tree[i], zones, NULL, WZX_MAX_ZONES);
            if (mini < n) {
                free(z->drag_layout);
                z->drag_layout = wz_xstrdup(z->ov.strip_id[i]);
                z->ov.target = zones[mini];
                z->ov.hl_a = mini;
                z->ov.has_target = 1;
            }
        } else if (i != -1) {
            return;
        }
    }
    if (w->tiled) {
        /* Arrastada sem as células na tela (atraso, modificador): volta. */
        wzx_tile_mark(z);
        return;
    }
    if (z->ov.shown && z->ov.has_target) {
        wz_rect pre;
        if (!wzx_get_restore(z, w, &pre))
            wzx_set_restore(z, w, z->drag_start);
        /* Para desfazer, o estado de antes do arrasto e, se o layout veio
         * do painel e mudou, o layout de antes: um passo só. */
        wz_rect now = w->outer;
        w->outer = z->drag_start;
        wz_hist_entry *he = wzx_history_begin(z);
        wzx_history_add(z, he, w);
        const wz_layout_def *cur = wzx_layout_of(z, z->ov.mon);
        int change = z->drag_layout && (!cur || strcmp(cur->id, z->drag_layout));
        const char *sec = "General";
        char msec[96];
        if (change) {
            for (int i = 0; i < z->cfg.nscopes; i++) {
                wz_scope *sc = &z->cfg.scopes[i];
                if (sc->monitor && sc->workspace == 0 && sc->layout && !strcmp(sc->monitor, z->mon[z->ov.mon].name)) {
                    snprintf(msec, sizeof msec, "Monitor %s", sc->monitor);
                    sec = msec;
                }
            }
            char *before = wzx_cfg_get(z, sec, "Layout");
            wz_hist_add_cfg(he, sec, "Layout", before);
            free(before);
        }
        wzx_history_end(z);
        w->outer = now;
        /* Aplicar a configuração esconde o overlay: o alvo vai antes. */
        wz_rect target = z->ov.target;
        long zone = z->ov.hl_a;
        if (change) {
            wzx_save_cfg(z, sec, "Layout", z->drag_layout);
            wzx_apply_config(z);
        }
        long t0 = wzx_now_us();
        wzx_snap(z, w, target, zone, 0);
        if (getenv("WZ_DEBUG")) {
            XSync(z->dpy, False);
            wzx_log("tempo: encaixe pedido em %ld µs", wzx_now_us() - t0);
        }
        return;
    }
    /* Solta fora das zonas: uma janela encaixada volta ao tamanho de antes,
     * com o ponteiro no mesmo ponto relativo da barra de título. */
    wz_rect pre;
    if (z->cfg.restore_size_on_unsnap && wzx_get_restore(z, w, &pre) && wzx_get_zone_prop(z, w) >= 0) {
        double fx = w->outer.w > 0 ? (double)(px - w->outer.x) / w->outer.w : 0.5;
        wz_rect r = { px - (int)(fx * pre.w), w->outer.y, pre.w, pre.h };
        wzx_clear_restore(z, w);
        wzx_move_outer(z, w, r);
    }
}

void
wzx_drag_raw(wzx *z, XGenericEventCookie *c)
{
    XIRawEvent *re = c->data;
    /*
     * O detail de um evento bruto pode ser o botão físico, antes do
     * mapeamento (canhotos com "pointer = 3 2 1"). Quem decide é o estado
     * do botão lógico 1 no XQueryPointer, que sempre vem mapeado: o servidor
     * já processou o evento quando a consulta chega.
     */
    switch (c->evtype) {
    case XI_RawButtonPress: {
        if (z->picker || z->popup || z->drag != DRAG_IDLE)
            return;
        Window r, child;
        int rx, ry, wx, wy;
        unsigned mask;
        if (!XQueryPointer(z->dpy, z->root, &r, &child, &rx, &ry, &wx, &wy, &mask) || child == None ||
            !(mask & Button1Mask))
            return;
        wzx_win *w = wzx_find_frame(z, child);
        if (getenv("WZ_DEBUG"))
            wzx_log("press em 0x%lx: janela %s", child, w ? "conhecida" : "desconhecida");
        if (!w || !wzx_snappable(z, w))
            return;
        z->drag = DRAG_ARMED;
        z->drag_w = w;
        z->drag_frame = child;
        z->press_x = rx;
        z->press_y = ry;
        wzx_refresh_geom(z, w);
        z->drag_start = w->outer;
        z->drag_cancel = 0;
        z->super_down = !!(mask & Mod4Mask);
        z->alt_down = !!(mask & Mod1Mask);
        z->ctrl_down = !!(mask & ControlMask);
        z->shift_down = !!(mask & ShiftMask);
        break;
    }
    case XI_RawButtonRelease: {
        /* Sempre tratado (mesmo com o seletor ou um menu aberto): perder o
         * soltar deixaria o arrasto preso e ignoraria os próximos. */
        if (z->drag == DRAG_IDLE)
            return;
        Window r, child;
        int rx = z->press_x, ry = z->press_y, wx, wy;
        unsigned mask = 0;
        if (XQueryPointer(z->dpy, z->root, &r, &child, &rx, &ry, &wx, &wy, &mask) && (mask & Button1Mask))
            return; /* soltou outro botão; o 1 continua apertado */
        if (z->drag == DRAG_MOVING)
            finish_move(z, rx, ry);
        else if (z->drag == DRAG_RESIZING && z->drag_w)
            wzx_tile_user_resize(z, z->drag_w);
        reset_drag(z);
        break;
    }
    case XI_RawKeyPress:
    case XI_RawKeyRelease: {
        if (z->picker || z->popup)
            return;
        int down = c->evtype == XI_RawKeyPress;
        KeyCode k = (KeyCode)re->detail;
        if (k == kc_esc && down && z->drag == DRAG_MOVING) {
            z->drag_cancel = 1;
            wzx_overlay_hide(z);
            return;
        }
        if (k == kc_super[0] || k == kc_super[1])
            z->super_down = down;
        else if (k == kc_alt[0] || k == kc_alt[1])
            z->alt_down = down;
        else if (k == kc_ctrl[0] || k == kc_ctrl[1])
            z->ctrl_down = down;
        else if (k == kc_shift[0] || k == kc_shift[1])
            z->shift_down = down;
        else
            return;
        z->drag_dirty = 1;
        break;
    }
    }
}

void
wzx_drag_configure(wzx *z, XConfigureEvent *e)
{
    if (z->drag == DRAG_IDLE || e->window != z->drag_frame || !z->drag_w)
        return;
    wzx_win *w = z->drag_w;
    w->outer = (wz_rect){ e->x, e->y, e->width + 2 * e->border_width, e->height + 2 * e->border_width };
    if (z->drag == DRAG_ARMED) {
        if (w->outer.w != z->drag_start.w || w->outer.h != z->drag_start.h) {
            z->drag = w->tiled ? DRAG_RESIZING : DRAG_IDLE;
            if (z->drag == DRAG_IDLE)
                z->drag_w = NULL;
            return;
        }
        int dx = w->outer.x - z->drag_start.x, dy = w->outer.y - z->drag_start.y;
        if (dx * dx + dy * dy < z->cfg.min_drag_px * z->cfg.min_drag_px)
            return;
        z->drag = DRAG_MOVING;
        z->drag_show_at = wzx_now_ms() + z->cfg.show_delay_ms;
        wzx_xi_select(z); /* agora sim, teclas brutas (Esc, modificador) */
    }
    if (z->drag == DRAG_MOVING)
        z->drag_dirty = 1;
}

/* Depois de esvaziar a fila de eventos: um único cálculo por lote. */
void
wzx_drag_flush(wzx *z)
{
    if (z->drag != DRAG_MOVING || !z->drag_dirty || z->drag_cancel)
        return;
    if (wzx_now_ms() < z->drag_show_at)
        return; /* o timer do laço principal chama de novo */
    z->drag_dirty = 0;
    wzx_win *w = z->drag_w;
    Window r, child;
    int px, py, wx, wy;
    unsigned mask;
    if (!XQueryPointer(z->dpy, z->root, &r, &child, &px, &py, &wx, &wy, &mask))
        return;
    int mon = wzx_monitor_at(z, px, py);
    int tile_mode = w->tiled && wzx_tile_active_for(z, mon);
    int active;
    if (tile_mode)
        active = 1;
    else if (!z->cfg.zones)
        active = 0;
    else if (z->cfg.activation == WZ_ACTIVATE_MODIFIER)
        active = is_mod_down(z);
    else if (z->cfg.activation == WZ_ACTIVATE_EDGE) {
        wz_rect g = z->mon[mon].geom;
        int e = z->cfg.edge_px;
        active = px < g.x + e || py < g.y + e || px >= g.x + g.w - e || py >= g.y + g.h - e || is_mod_down(z);
    } else
        active = 1;
    if (!active) {
        wzx_overlay_hide(z);
        return;
    }
    /* Primeiro o layout (painel de miniaturas), depois a zona. */
    int strip = 0;
    if (!tile_mode) {
        switch (z->cfg.drag_flow) {
        case WZ_FLOW_LAYOUTS:
            strip = 1;
            break;
        case WZ_FLOW_LAYOUTS_MODIFIER:
            strip = is_mod_down(z) || z->drag_layout;
            break;
        case WZ_FLOW_LAYOUTS_EDGE:
            if (py < z->mon[mon].geom.y + z->cfg.edge_px)
                z->strip_latched = 1;
            strip = z->strip_latched;
            break;
        default:
            break;
        }
    }
    /* O layout escolhido vale no monitor onde foi escolhido. */
    if (z->drag_layout && z->ov.shown && z->ov.mon != mon) {
        free(z->drag_layout);
        free(z->drag_tree);
        z->drag_layout = z->drag_tree = NULL;
    }
    const char *tree = NULL;
    if (tile_mode)
        tree = NULL;
    else if (strip)
        tree = z->drag_tree; /* nenhum ainda: só o painel */
    else {
        const wz_layout_def *l = wzx_layout_of(z, mon);
        tree = l ? l->tree : NULL;
    }
    int mode = tile_mode ? 1 : 0;
    int rebuild = !z->ov.shown || z->ov.mon != mon || z->ov.mode != mode || z->ov.strip != strip;
    static const char *shown_tree;
    static char *shown_copy;
    int zones_changed = rebuild || (tree != shown_tree && (!tree || !shown_copy || strcmp(tree, shown_copy)));
    if (zones_changed) {
        wz_rect zones[WZX_MAX_ZONES];
        char *names[WZX_MAX_ZONES] = { 0 };
        int n = 0;
        if (tile_mode)
            n = wzx_tile_cells(z, mon, zones, NULL, WZX_MAX_ZONES);
        else if (tree)
            n = wzx_zones_of_tree(z, mon, tree, zones, names, WZX_MAX_ZONES);
        if (n <= 0 && !strip)
            return;
        long t0 = wzx_now_us();
        if (rebuild) {
            int keep_sel = z->ov.strip_sel, keep_hover = z->ov.strip_hover;
            wzx_overlay_hide(z);
            wzx_overlay_set_zones(z, zones, names, n);
            if (strip) {
                /* Longe do ponteiro: embaixo se ele está no terço de cima
                 * (a não ser pela borda de cima, que pede o painel lá). */
                wz_rect g = z->mon[mon].geom;
                int bottom = z->cfg.drag_flow != WZ_FLOW_LAYOUTS_EDGE && py < g.y + g.h / 3;
                wzx_strip_build(z, mon, bottom);
                z->strip_wait_leave = wz_rect_contains(z->ov.strip_box, px, py);
                if (z->drag_layout)
                    for (int i = 0; i < z->ov.strip_n; i++)
                        if (!strcmp(z->ov.strip_id[i], z->drag_layout))
                            z->ov.strip_sel = i;
                (void)keep_sel;
                (void)keep_hover;
            }
            wzx_overlay_show(z, mon, mode);
        } else {
            wzx_overlay_set_zones(z, zones, names, n);
        }
        for (int i = 0; i < n; i++)
            free(names[i]);
        free(shown_copy);
        shown_copy = tree ? wz_xstrdup(tree) : NULL;
        shown_tree = tree;
        if (getenv("WZ_DEBUG")) {
            XSync(z->dpy, False);
            wzx_log("tempo: overlay criado e desenhado em %ld µs (%d zonas%s)", wzx_now_us() - t0, n,
                    strip ? ", painel" : "");
        }
    }
    if (strip) {
        int mini = -1;
        int hit = wzx_strip_hit(z, px, py, &mini);
        if (z->strip_wait_leave) {
            if (hit != -1)
                hit = -2, mini = -1;
            else
                z->strip_wait_leave = 0;
        }
        if (hit != -1) {
            /* Sobre o painel: nada de zona destacada embaixo dele. */
            if (z->ov.hl_a >= 0)
                wzx_overlay_highlight(z, -1, -1, NULL);
            int hover = hit >= 0 ? hit : -1;
            if (hover != z->ov.strip_hover)
                z->dwell_at = hover >= 0 ? wzx_now_ms() + z->cfg.dwell_ms : 0;
            int sel = z->ov.strip_sel;
            if (hover >= 0 && hover != sel && z->dwell_at && wzx_now_ms() >= z->dwell_at) {
                sel = hover;
                z->dwell_at = 0;
                free(z->drag_layout);
                free(z->drag_tree);
                z->drag_layout = wz_xstrdup(z->ov.strip_id[sel]);
                z->drag_tree = wz_xstrdup(z->ov.strip_tree[sel]);
                z->drag_dirty = 1; /* refaz as zonas no próximo lote */
            }
            wzx_strip_set(z, hover, hover >= 0 ? mini : -1, sel);
            return;
        }
        z->dwell_at = 0;
        wzx_strip_set(z, -1, -1, z->ov.strip_sel);
    }
    wz_rect target;
    int b = -1;
    int a = z->ov.nzones > 0
                ? wz_target(z->ov.zones, z->ov.nzones, px, py, tile_mode ? 0 : z->cfg.span_px, &target, &b)
                : -1;
    if (a != z->ov.hl_a || b != z->ov.hl_b)
        wzx_overlay_highlight(z, a, b, a >= 0 ? &target : NULL);
}

void
wzx_drag_timer(wzx *z)
{
    if (z->drag == DRAG_MOVING) {
        z->drag_dirty = 1;
        wzx_drag_flush(z);
    }
}

long
wzx_drag_due(wzx *z)
{
    if (z->drag != DRAG_MOVING)
        return -1;
    if (z->drag_dirty && z->drag_show_at > wzx_now_ms())
        return z->drag_show_at;
    return z->dwell_at ? z->dwell_at : -1;
}
