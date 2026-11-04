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
#include "wzx.h"

#include <X11/XKBlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/keysym.h>
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
    wzx_overlay_hide(z);
    if (z->raw_keys_selected)
        wzx_xi_select(z);
}

/* ---- histórico ---------------------------------------------------------- */

void
wzx_history_push(wzx *z, wzx_win **ws, int n)
{
    if (n <= 0)
        return;
    if (z->nhist == WZX_HISTORY) {
        memmove(&z->hist[0], &z->hist[1], sizeof z->hist[0] * (WZX_HISTORY - 1));
        z->nhist--;
    }
    int k = z->nhist++;
    z->hist[k].n = 0;
    for (int i = 0; i < n && z->hist[k].n < 64; i++) {
        if (!ws[i])
            continue;
        z->hist[k].w[z->hist[k].n] = ws[i]->client;
        z->hist[k].r[z->hist[k].n] = ws[i]->outer;
        z->hist[k].n++;
    }
}

void
wzx_history_undo(wzx *z)
{
    if (z->nhist == 0)
        return;
    int k = --z->nhist;
    for (int i = 0; i < z->hist[k].n; i++) {
        wzx_win *w = wzx_find(z, z->hist[k].w[i]);
        if (!w)
            continue;
        if (w->tiled) {
            /* Desfazer tira do mosaico: a janela volta a flutuar onde estava. */
            w->user_float = 1;
            w->tiled = 0;
        }
        wzx_move_outer(z, w, z->hist[k].r[i]);
    }
    wzx_tile_mark(z);
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
    if (w->tiled) {
        /* Arrastada sem as células na tela (atraso, modificador): volta. */
        wzx_tile_mark(z);
        return;
    }
    if (z->ov.shown && z->ov.has_target) {
        wz_rect pre;
        if (!wzx_get_restore(z, w, &pre))
            wzx_set_restore(z, w, z->drag_start);
        /* Para desfazer, o estado de antes do arrasto. */
        wz_rect now = w->outer;
        w->outer = z->drag_start;
        wzx_history_push(z, &w, 1);
        w->outer = now;
        wzx_snap(z, w, z->ov.target, z->ov.hl_a, 0);
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
    if (getenv("WZ_DEBUG"))
        wzx_log("raw ev=%d detail=%d dev=%d src=%d drag=%d", c->evtype, re->detail, re->deviceid, re->sourceid, z->drag);
    if (z->picker || z->popup)
        return;
    switch (c->evtype) {
    case XI_RawButtonPress: {
        if (re->detail != 1 || z->drag != DRAG_IDLE)
            return;
        Window r, child;
        int rx, ry, wx, wy;
        unsigned mask;
        if (!XQueryPointer(z->dpy, z->root, &r, &child, &rx, &ry, &wx, &wy, &mask) || child == None)
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
        if (re->detail != 1)
            return;
        Window r, child;
        int rx = z->press_x, ry = z->press_y, wx, wy;
        unsigned mask;
        XQueryPointer(z->dpy, z->root, &r, &child, &rx, &ry, &wx, &wy, &mask);
        if (z->drag == DRAG_MOVING)
            finish_move(z, rx, ry);
        else if (z->drag == DRAG_RESIZING && z->drag_w)
            wzx_tile_user_resize(z, z->drag_w);
        reset_drag(z);
        break;
    }
    case XI_RawKeyPress:
    case XI_RawKeyRelease: {
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
    if (getenv("WZ_DEBUG") && z->drag != DRAG_IDLE)
        wzx_log("configure 0x%lx (frame 0x%lx) %d,%d %dx%d", e->window, z->drag_frame, e->x, e->y, e->width, e->height);
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
    if (!z->ov.shown || z->ov.mon != mon || z->ov.mode != (tile_mode ? 1 : 0)) {
        wz_rect zones[WZX_MAX_ZONES];
        char *names[WZX_MAX_ZONES] = { 0 };
        int n;
        if (tile_mode)
            n = wzx_tile_cells(z, mon, zones, NULL, WZX_MAX_ZONES);
        else
            n = wzx_zones_for(z, mon, zones, names, WZX_MAX_ZONES);
        if (n <= 0)
            return;
        wzx_overlay_hide(z);
        wzx_overlay_set_zones(z, zones, names, n);
        for (int i = 0; i < n; i++)
            free(names[i]);
        wzx_overlay_show(z, mon, tile_mode ? 1 : 0);
    }
    wz_rect target;
    int b = -1;
    int a = wz_target(z->ov.zones, z->ov.nzones, px, py, tile_mode ? 0 : z->cfg.span_px, &target, &b);
    if (a != z->ov.hl_a || b != z->ov.hl_b)
        wzx_overlay_highlight(z, a, b, &target);
}

void
wzx_drag_timer(wzx *z)
{
    if (z->drag == DRAG_MOVING) {
        z->drag_dirty = 1;
        wzx_drag_flush(z);
    }
}
