/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Auto Tiling no X11. O Window Maker continua decorando, focando e
 * empilhando; aqui só se decide a geometria das janelas elegíveis do
 * monitor/área de trabalho com o mosaico ligado.
 *
 * Elegível: janela normal, mapeada, na área de trabalho atual, sem tamanho
 * fixo, sem tela cheia, não fixa em todas as áreas, sem regra "flutuante" e
 * não tirada do mosaico pelo usuário. Diálogos, menus e afins ficam fora.
 *
 * Conflito de restrições (o mínimo do aplicativo não cabe na célula): a
 * janela sai do mosaico e flutua, e o layout é recalculado sem ela.
 *
 * A geometria de antes do mosaico fica em _WMAKE_ZONES_RESTORE na própria
 * janela; desligar o mosaico devolve cada janela para lá.
 */
#define _GNU_SOURCE
#include "wzx.h"

#include <stdlib.h>
#include <string.h>

typedef struct wzx_tset tset;

static tset *
set_for(wzx *z, int mon, int create)
{
    const char *name = z->mon[mon].name;
    for (int i = 0; i < 64; i++)
        if (z->tset[i].used && z->tset[i].desk == z->current_desktop && !strcmp(z->tset[i].mon, name))
            return &z->tset[i];
    if (!create)
        return NULL;
    for (int i = 0; i < 64; i++)
        if (!z->tset[i].used) {
            tset *s = &z->tset[i];
            memset(s, 0, sizeof *s);
            s->used = 1;
            { char nm[64]; memcpy(nm, name, sizeof nm); nm[63] = 0; memcpy(s->mon, nm, sizeof nm); }
            s->desk = z->current_desktop;
            s->ratio = z->cfg.tile.master_ratio;
            return s;
        }
    return NULL;
}

static void record_ratio(wzx *z, wzx_win *w, tset *s);

int
wzx_tile_active_for(wzx *z, int mon)
{
    if (mon < 0 || mon >= z->nmon)
        return 0;
    return wz_config_autotile_for(&z->cfg, z->mon[mon].name, (int)z->current_desktop + 1);
}

void
wzx_tile_mark(wzx *z)
{
    if (!z->tile_dirty) {
        z->tile_dirty = 1;
        z->tile_at = wzx_now_ms() + 30; /* agrupa rajadas de eventos */
    }
}

static int
by_order(const void *a, const void *b)
{
    const wzx_win *x = *(wzx_win *const *)a, *y = *(wzx_win *const *)b;
    return x->order < y->order ? -1 : x->order > y->order;
}

static int
on_this_desktop(wzx *z, wzx_win *w)
{
    return w->desktop == z->current_desktop;
}

/* Janelas do conjunto, em ordem. */
static int
members(wzx *z, int mon, wzx_win **out, int max)
{
    int n = 0;
    for (int i = 0; i < z->nwin && n < max; i++) {
        wzx_win *w = z->win[i];
        if (!wzx_tileable(z, w))
            continue;
        int m = wzx_monitor_of(z, w->outer);
        /* No mosaico, conta onde nós a pusemos (o wmaker pode ainda não ter
         * aplicado o pedido), a menos que outro (o usuário pelo teclado ou
         * pelo menu do Window Maker) a tenha levado para outro monitor: aí
         * ela entra no mosaico de lá. */
        if (w->tiled && w->tile_mon >= 0 && w->tile_mon < z->nmon &&
            !(w->expect_valid && m != w->tile_mon && m != wzx_monitor_of(z, w->expect)))
            m = w->tile_mon;
        if (m == mon)
            out[n++] = w;
    }
    qsort(out, (size_t)n, sizeof *out, by_order);
    return n;
}

static wz_tile_params
params(wzx *z, int mon, tset *s)
{
    wz_tile_params p = z->cfg.tile;
    p.algo = wz_config_algo_for(&z->cfg, z->mon[mon].name, (int)z->current_desktop + 1);
    if (s)
        p.master_ratio = s->ratio;
    return p;
}

static wz_limits
limits(wzx_win *w)
{
    wz_limits l = { 0 };
    int ew = w->ext[0] + w->ext[1], eh = w->ext[2] + w->ext[3];
    if (w->hints.min_w > 0)
        l.min_w = w->hints.min_w + ew;
    if (w->hints.min_h > 0)
        l.min_h = w->hints.min_h + eh;
    if (w->hints.max_w > 0)
        l.max_w = w->hints.max_w + ew;
    if (w->hints.max_h > 0)
        l.max_h = w->hints.max_h + eh;
    return l;
}

/* Calcula as células do conjunto. ws/rects em ordem do mosaico. */
static int
compute(wzx *z, int mon, wzx_win **ws, wz_rect *rects, int max)
{
    wzx_update_workarea(z);
    tset *s = set_for(z, mon, 1);
    int n = members(z, mon, ws, max);
    if (n == 0) {
        if (s)
            wz_bsp_clear(&s->bsp);
        return 0;
    }
    wz_tile_params p = params(z, mon, s);
    wz_rect area = z->mon[mon].work;
    if (p.algo == WZ_TILE_BSP && s) {
        /* Sincroniza a árvore: sai quem não é mais membro, entra quem chegou
         * (ao lado da janela ativa). */
        long ids[WZX_MAX_WIN];
        wz_rect tmp[WZX_MAX_WIN];
        int k = wz_bsp_compute(&s->bsp, area, 0, 0, tmp, ids, WZX_MAX_WIN);
        for (int i = 0; i < k && i < WZX_MAX_WIN; i++) {
            int found = 0;
            for (int j = 0; j < n; j++)
                found |= (long)ws[j]->client == ids[i];
            if (!found)
                wz_bsp_remove(&s->bsp, ids[i]);
        }
        for (int j = 0; j < n; j++)
            if (!wz_bsp_has(&s->bsp, (long)ws[j]->client))
                wz_bsp_insert(&s->bsp, (long)ws[j]->client, (long)z->active, area);
        int gi = p.gap_in, go = p.gap_out;
        if (p.smart_gaps && n == 1)
            gi = go = 0;
        k = wz_bsp_compute(&s->bsp, area, gi, go, tmp, ids, WZX_MAX_WIN);
        /* Reordena ws na ordem das folhas. */
        wzx_win *ord[WZX_MAX_WIN];
        int m = 0;
        for (int i = 0; i < k && m < max; i++)
            for (int j = 0; j < n; j++)
                if ((long)ws[j]->client == ids[i]) {
                    ord[m] = ws[j];
                    rects[m] = tmp[i];
                    m++;
                }
        memcpy(ws, ord, sizeof *ws * (size_t)m);
        return m;
    }
    /* O núcleo calcula no máximo WZ_TILE_MAX: as demais ficariam com
     * retângulos não inicializados. */
    int k = wz_tile(&p, area, n, rects, max);
    return k <= 0 ? 0 : k < n ? k : n;
}

/* Por que a janela está (ou não) no mosaico: para "windows -d" e para a
 * seção de compatibilidade das preferências. A ordem segue wzx_tileable. */
const char *
wzx_tile_reason(wzx *z, wzx_win *w, char *buf, size_t len)
{
    int mon = wzx_monitor_of(z, w->outer);
    if (w->tiled)
        snprintf(buf, len, "mosaico");
    else if (w->rule_ignore)
        snprintf(buf, len, "regra: ignorar");
    else if (w->special)
        snprintf(buf, len, "tipo especial (diálogo, utilitário, menu)");
    else if (w->fixed)
        snprintf(buf, len, "tamanho fixo (%dx%d)", w->hints.min_w, w->hints.min_h);
    else if (w->state & WS_FULLSCREEN)
        snprintf(buf, len, "tela cheia");
    else if (w->state & (WS_SHADED | WS_HIDDEN) || !w->mapped)
        snprintf(buf, len, "minimizada ou enrolada");
    else if (w->transient)
        snprintf(buf, len, "janela filha (transient)");
    else if (w->state & WS_MODAL)
        snprintf(buf, len, "modal");
    else if (!w->rule_tile && (w->state & (WS_STICKY | WS_SKIP_TASKBAR | WS_ABOVE)))
        snprintf(buf, len, "em todas as áreas, sempre acima ou fora da barra de tarefas");
    else if (w->user_float)
        snprintf(buf, len, "flutuante por escolha do usuário");
    else if (w->rule_float)
        snprintf(buf, len, "regra: flutuante");
    else if (w->fit_float)
        snprintf(buf, len, "não cabe: mínimo %dx%d, célula %dx%d", w->fit_min.min_w, w->fit_min.min_h,
                 w->fit_cell.w, w->fit_cell.h);
    else if (w->desktop != z->current_desktop)
        snprintf(buf, len, "outra área de trabalho");
    else if (!wzx_tile_active_for(z, mon))
        snprintf(buf, len, "mosaico desligado neste monitor");
    else
        snprintf(buf, len, "aguardando");
    return buf;
}

int
wzx_tile_cells(wzx *z, int mon, wz_rect *out, wzx_win **ws, int max)
{
    wzx_win *tmp[WZX_MAX_WIN];
    wz_rect r[WZX_MAX_WIN];
    int n = compute(z, mon, tmp, r, WZX_MAX_WIN);
    if (n > max)
        n = max;
    memcpy(out, r, sizeof *out * (size_t)n);
    if (ws)
        memcpy(ws, tmp, sizeof *ws * (size_t)n);
    return n;
}

static void
untile(wzx *z, wzx_win *w, int restore)
{
    wz_rect r;
    if (restore && wzx_get_restore(z, w, &r))
        wzx_move_outer(z, w, r);
    if (restore)
        wzx_clear_restore(z, w);
    w->tiled = 0;
    w->tile_mon = -1;
    w->expect_valid = 0;
}

/* Regras na primeira vez que a janela aparece. Primeiro o destino (área de
 * trabalho, monitor), depois a ação. Diálogos e menus não chegam aqui
 * (wzx_snappable). */
static void
apply_rules(wzx *z, wzx_win *w)
{
    w->rule_done = 1;
    char title[256];
    const wz_rule *r = wz_config_match_rule(&z->cfg, w->wm_class, w->instance, wzx_title(z, w, title, sizeof title));
    if (!r)
        return;
    if (r->action == WZ_RULE_IGNORE) {
        w->rule_ignore = 1;
        return;
    }
    if (w->special || w->transient)
        return;
    if (r->workspace > 0 && w->desktop >= 0 && w->desktop != r->workspace - 1)
        wzx_send_to_desktop(z, w, r->workspace - 1);
    int mon = wzx_monitor_of(z, w->outer);
    if (r->monitor) {
        int target = -1;
        for (int i = 0; i < z->nmon; i++)
            if (!strcmp(z->mon[i].name, r->monitor))
                target = i;
        if (target >= 0 && target != mon && !w->fixed) {
            /* No mesmo ponto relativo da área útil do outro monitor. */
            wzx_update_workarea(z);
            wz_rect a = z->mon[mon].work, b = z->mon[target].work, o = w->outer;
            wz_rect n = o;
            n.w = o.w < b.w ? o.w : b.w;
            n.h = o.h < b.h ? o.h : b.h;
            n.x = b.x + (a.w > n.w ? (int)((long)(o.x - a.x) * (b.w - n.w) / (a.w - n.w > 0 ? a.w - n.w : 1)) : 0);
            n.y = b.y + (a.h > n.h ? (int)((long)(o.y - a.y) * (b.h - n.h) / (a.h - n.h > 0 ? a.h - n.h : 1)) : 0);
            if (n.x < b.x || n.x + n.w > b.x + b.w)
                n.x = b.x + (b.w - n.w) / 2;
            if (n.y < b.y || n.y + n.h > b.y + b.h)
                n.y = b.y + (b.h - n.h) / 2;
            wzx_move_outer(z, w, n);
            w->outer = n;
            mon = target;
        }
    }
    switch (r->action) {
    case WZ_RULE_FLOAT:
        w->rule_float = 1;
        break;
    case WZ_RULE_TILE:
        w->rule_tile = 1;
        break;
    case WZ_RULE_ZONE: {
        if (r->zone <= 0 || !wzx_snappable(z, w) || wzx_tile_active_for(z, mon))
            break; /* com o mosaico, ele decide */
        wz_rect zones[WZX_MAX_ZONES];
        int n;
        if (r->layout && wz_config_layout(&z->cfg, r->layout)) {
            wz_node *t = wz_parse(wz_config_layout(&z->cfg, r->layout)->tree, NULL);
            wzx_update_workarea(z);
            n = t ? wz_compute(t, z->mon[mon].work, z->cfg.gap_in, z->cfg.gap_out, zones, WZX_MAX_ZONES) : 0;
            wz_free(t);
        } else {
            n = wzx_zones_for(z, mon, zones, NULL, WZX_MAX_ZONES);
        }
        if (r->zone <= n) {
            wzx_set_restore(z, w, w->outer);
            wzx_snap(z, w, zones[r->zone - 1], r->zone - 1, 0);
            wzx_log("regra \"%s\": %s na zona %d", r->id, w->wm_class ? w->wm_class : "?", r->zone);
        }
        break;
    }
    default:
        break;
    }
}

void
wzx_tile_run(wzx *z)
{
    long t0 = wzx_now_us();
    z->tile_dirty = 0;
    for (int i = 0; i < z->nwin; i++) {
        if (!z->win[i]->rule_done && z->win[i]->mapped)
            apply_rules(z, z->win[i]);
        /* "Não coube" vale só para esta rodada: com outro monitor, outra área
         * útil ou menos janelas ela pode caber agora. Antes ficava fora do
         * mosaico até o serviço reiniciar. */
        z->win[i]->fit_prev = z->win[i]->fit_float;
        z->win[i]->fit_float = 0;
    }

    for (int mon = 0; mon < z->nmon; mon++) {
        int active = wzx_tile_active_for(z, mon);
        if (!active) {
            /* Mosaico desligado aqui: quem estava no mosaico volta a flutuar. */
            for (int i = 0; i < z->nwin; i++) {
                wzx_win *w = z->win[i];
                /* tile_mon < 0: o conjunto se desfez numa troca de
                 * monitores; vale o monitor onde a janela está agora. */
                int here = w->tile_mon == mon || (w->tile_mon < 0 && wzx_monitor_of(z, w->outer) == mon);
                if (w->tiled && here && on_this_desktop(z, w))
                    untile(z, w, 1);
            }
            continue;
        }
        wzx_win *ws[WZX_MAX_WIN];
        wz_rect rects[WZX_MAX_WIN];
        int n = 0;
        /* Quem não cabe sai, uma janela por vez (a que fica mais longe de
         * caber), e o layout é refeito: com uma janela a menos as células
         * crescem e as outras podem passar a caber. Tirar todas as que não
         * cabiam numa passada só deixava de fora quem cabia na seguinte. */
        tset *ts = set_for(z, mon, 0);
        for (int pass = 0; pass < 16; pass++) {
            n = compute(z, mon, ws, rects, WZX_MAX_WIN);
            /* Uma única janela não cabe: antes de tirá-la, ela troca de
             * lugar com outra do conjunto quando assim as duas cabem (a
             * célula maior para quem pede mais; GTK4/libadwaita declaram
             * mínimos grandes: 760 de largura no Pamac). Com mais de uma
             * fora, sai primeiro a mais longe de caber (abaixo): a troca
             * ali só mudaria quem fica de fora e tiraria a principal do
             * lugar. */
            int conflicts = 0;
            for (int i = 0; i < n; i++) {
                wz_limits li = limits(ws[i]);
                wz_rect ri = rects[i];
                conflicts += wz_fit(&ri, &li) == WZ_FIT_CONFLICT;
            }
            int swapped = 0;
            for (int i = 0; i < n && !swapped && conflicts == 1; i++) {
                wz_limits li = limits(ws[i]);
                wz_rect ri = rects[i];
                if (wz_fit(&ri, &li) != WZ_FIT_CONFLICT)
                    continue;
                for (int j = 0; j < n && !swapped; j++) {
                    if (j == i)
                        continue;
                    wz_limits lj = limits(ws[j]);
                    wz_rect a = rects[j], b = rects[i];
                    if (wz_fit(&a, &li) == WZ_FIT_CONFLICT || wz_fit(&b, &lj) == WZ_FIT_CONFLICT)
                        continue;
                    long o = ws[i]->order;
                    ws[i]->order = ws[j]->order;
                    ws[j]->order = o;
                    if (ts)
                        wz_bsp_swap(&ts->bsp, (long)ws[i]->client, (long)ws[j]->client);
                    wzx_log("mosaico: %s troca de lugar com %s para caber (mínimo %dx%d)",
                            ws[i]->wm_class ? ws[i]->wm_class : "?", ws[j]->wm_class ? ws[j]->wm_class : "?",
                            li.min_w, li.min_h);
                    swapped = 1;
                }
            }
            if (swapped)
                continue;
            int worst = -1;
            long worst_def = 0;
            wz_rect worst_cell = { 0 };
            for (int i = 0; i < n; i++) {
                wz_limits l = limits(ws[i]);
                wz_rect r = rects[i];
                if (wz_fit(&r, &l) == WZ_FIT_CONFLICT) {
                    long def = (long)(l.min_w > rects[i].w ? l.min_w - rects[i].w : 0) +
                               (l.min_h > rects[i].h ? l.min_h - rects[i].h : 0);
                    if (worst < 0 || def > worst_def) {
                        worst = i;
                        worst_def = def;
                        worst_cell = rects[i];
                    }
                }
                rects[i] = r;
            }
            if (worst < 0)
                break;
            wz_limits l = limits(ws[worst]);
            ws[worst]->fit_float = 1;
            ws[worst]->fit_min = l;
            ws[worst]->fit_cell = worst_cell;
            /* Uma vez por mudança, não a cada rodada. */
            if (!ws[worst]->fit_prev)
                wzx_log("mosaico: %s não cabe na célula %dx%d (mínimo %dx%d); fica flutuante",
                        ws[worst]->wm_class ? ws[worst]->wm_class : "?", worst_cell.w, worst_cell.h, l.min_w,
                        l.min_h);
            if (ws[worst]->tiled)
                untile(z, ws[worst], 1);
        }
        for (int i = 0; i < n; i++) {
            wzx_win *w = ws[i];
            if (!w->tiled) {
                wz_rect pre;
                if (!wzx_get_restore(z, w, &pre))
                    wzx_set_restore(z, w, w->outer);
                w->tiled = 1;
            }
            w->tile_mon = mon;
            /* Só pede de novo quando o alvo mudou: o wmaker arredonda pelos
             * incrementos do cliente e isso não pode virar um laço. */
            if (!w->expect_valid || memcmp(&w->expect, &rects[i], sizeof rects[i]) != 0)
                wzx_move_outer(z, w, rects[i]);
        }
        /* Quem estava no mosaico deste monitor e deixou de ser elegível
         * (minimizada, tela cheia, outro monitor) só perde a marca. */
        for (int i = 0; i < z->nwin; i++) {
            wzx_win *w = z->win[i];
            if (!w->tiled || w->tile_mon != mon || !on_this_desktop(z, w))
                continue;
            int member = 0;
            for (int k = 0; k < n; k++)
                member |= ws[k] == w;
            if (!member)
                untile(z, w, w->user_float);
        }
    }
    XFlush(z->dpy);
    if (getenv("WZ_DEBUG"))
        wzx_log("tempo: mosaico calculado e pedido em %ld µs (%d janelas)", wzx_now_us() - t0, z->nwin);
}

/* O quadro de uma janela mudou de lugar (ConfigureNotify na raiz). Uma
 * janela do mosaico que foi parar em outro monitor sem ser por nós entra no
 * mosaico de lá na próxima rodada. */
void
wzx_frame_moved(wzx *z, Window frame)
{
    wzx_win *w = wzx_find_frame(z, frame);
    if (!w || !w->tiled || w->tile_mon < 0 || w->tile_mon >= z->nmon)
        return;
    wzx_refresh_geom(z, w);
    int m = wzx_monitor_of(z, w->outer);
    if (m != w->tile_mon && (!w->expect_valid || m != wzx_monitor_of(z, w->expect)))
        wzx_tile_mark(z);
}

void
wzx_tile_disable_all(wzx *z)
{
    for (int i = 0; i < z->nwin; i++)
        if (z->win[i]->tiled)
            untile(z, z->win[i], 1);
    for (int i = 0; i < 64; i++)
        if (z->tset[i].used) {
            wz_bsp_clear(&z->tset[i].bsp);
            z->tset[i].used = 0;
        }
}

void
wzx_tile_forget(wzx *z, wzx_win *w)
{
    for (int i = 0; i < 64; i++)
        if (z->tset[i].used)
            wz_bsp_remove(&z->tset[i].bsp, (long)w->client);
}

/* Reordena: w passa a ocupar a posição idx do conjunto do monitor mon. */
static void
place_at(wzx *z, wzx_win *w, int mon, int idx)
{
    wzx_win *ws[WZX_MAX_WIN];
    int n = members(z, mon, ws, WZX_MAX_WIN);
    wzx_win *ord[WZX_MAX_WIN];
    int m = 0;
    for (int i = 0; i < n; i++)
        if (ws[i] != w)
            ord[m++] = ws[i];
    if (idx > m)
        idx = m;
    memmove(&ord[idx + 1], &ord[idx], sizeof *ord * (size_t)(m - idx));
    ord[idx] = w;
    m++;
    long base = ++z->order_seq;
    z->order_seq += m + 1;
    for (int i = 0; i < m; i++)
        ord[i]->order = base + i;
    w->tile_mon = mon;
}

void
wzx_tile_drop(wzx *z, wzx_win *w, int px, int py)
{
    int mon = z->ov.mon;
    wz_rect cells[WZX_MAX_ZONES];
    wzx_win *ws[WZX_MAX_ZONES];
    int n = wzx_tile_cells(z, mon, cells, ws, WZX_MAX_ZONES);
    int idx = n > 0 ? wz_zone_at(cells, n, px, py) : -1;
    if (idx >= 0 && ws[idx] != w) {
        wzx_win *other = ws[idx];
        tset *s = set_for(z, mon, 0);
        if (w->tile_mon == mon) {
            /* Troca de lugar com a janela da célula (Window Swap). */
            wzx_history_push(z, (wzx_win *[]){ w, other }, 2);
            long o = w->order;
            w->order = other->order;
            other->order = o;
            if (s)
                wz_bsp_swap(&s->bsp, (long)w->client, (long)other->client);
        } else {
            /* Outro monitor: entra na posição da célula. */
            wzx_history_push(z, &w, 1);
            wzx_tile_forget(z, w);
            place_at(z, w, mon, idx);
        }
    }
    w->expect_valid = 0;
    wzx_tile_mark(z);
}

void
wzx_tile_user_resize(wzx *z, wzx_win *w)
{
    int mon = w->tile_mon;
    if (mon < 0 || !w->tiled)
        return;
    /* Sombrear ou maximizar (duplo clique, botão) muda o tamanho do quadro,
     * mas não é o usuário puxando a borda: a proporção fica. */
    wzx_refresh_win(z, w);
    if (w->state & (WS_SHADED | WS_MAXV | WS_MAXH | WS_FULLSCREEN))
        return;
    tset *s = set_for(z, mon, 0);
    wz_tile_params p = params(z, mon, s);
    wz_rect area = z->mon[mon].work;
    wzx_refresh_geom(z, w);
    if (p.algo == WZ_TILE_MASTER && s) {
        wzx_win *ws[WZX_MAX_WIN];
        wz_rect rects[WZX_MAX_WIN];
        int n = compute(z, mon, ws, rects, WZX_MAX_WIN);
        int idx = -1;
        for (int i = 0; i < n; i++)
            if (ws[i] == w)
                idx = i;
        if (idx >= 0 && n > p.nmaster) {
            int horiz = p.master_pos == WZ_POS_LEFT || p.master_pos == WZ_POS_RIGHT;
            double span = horiz ? area.w : area.h;
            double size = horiz ? w->outer.w : w->outer.h;
            double ratio = idx < p.nmaster ? size / span : 1 - size / span;
            record_ratio(z, w, s);
            s->ratio = ratio < 0.1 ? 0.1 : ratio > 0.9 ? 0.9 : ratio;
        }
    } else if (p.algo == WZ_TILE_BSP && s && w->expect_valid) {
        wz_rect c = w->expect;
        if (w->outer.w != c.w)
            wz_bsp_resize(&s->bsp, (long)w->client, w->outer.x != c.x ? WZ_LEFT : WZ_RIGHT,
                          (double)(w->outer.w - c.w) / (area.w > 0 ? area.w : 1));
        if (w->outer.h != c.h)
            wz_bsp_resize(&s->bsp, (long)w->client, w->outer.y != c.y ? WZ_UP : WZ_DOWN,
                          (double)(w->outer.h - c.h) / (area.h > 0 ? area.h : 1));
    }
    w->expect_valid = 0;
    wzx_tile_mark(z);
}

void
wzx_tile_swap_dir(wzx *z, wzx_win *w, int dir)
{
    if (!w || !w->tiled)
        return;
    int mon = w->tile_mon;
    wz_rect cells[WZX_MAX_ZONES];
    wzx_win *ws[WZX_MAX_ZONES];
    int n = wzx_tile_cells(z, mon, cells, ws, WZX_MAX_ZONES);
    int idx = -1;
    for (int i = 0; i < n; i++)
        if (ws[i] == w)
            idx = i;
    int nb = idx >= 0 ? wz_neighbor(cells, n, idx, dir) : -1;
    if (nb < 0)
        return;
    wzx_win *other = ws[nb];
    wzx_history_push(z, (wzx_win *[]){ w, other }, 2);
    long o = w->order;
    w->order = other->order;
    other->order = o;
    tset *s = set_for(z, mon, 0);
    if (s)
        wz_bsp_swap(&s->bsp, (long)w->client, (long)other->client);
    wzx_tile_mark(z);
}

void
wzx_tile_promote(wzx *z, wzx_win *w)
{
    if (!w || !w->tiled)
        return;
    wzx_win *ws[WZX_MAX_WIN];
    int n = members(z, w->tile_mon, ws, WZX_MAX_WIN);
    if (n < 2)
        return;
    wzx_win *first = ws[0] == w ? ws[1] : ws[0];
    wzx_history_push(z, (wzx_win *[]){ w, first }, 2);
    long o = w->order;
    w->order = first->order;
    first->order = o;
    tset *s = set_for(z, w->tile_mon, 0);
    if (s)
        wz_bsp_swap(&s->bsp, (long)w->client, (long)first->client);
    wzx_tile_mark(z);
}

double *
wzx_tile_ratio_of(wzx *z, wzx_win *w)
{
    int mon = w && w->tile_mon >= 0 ? w->tile_mon : w ? wzx_monitor_of(z, w->outer) : -1;
    if (mon < 0 || mon >= z->nmon)
        return NULL;
    tset *s = set_for(z, mon, 1);
    return s ? &s->ratio : NULL;
}

/* Passo do histórico com a proporção de antes e a janela (que diz o
 * conjunto). */
static void
record_ratio(wzx *z, wzx_win *w, tset *s)
{
    wz_hist_entry *e = wzx_history_begin(z);
    wz_hist_set_aux(e, (long)(s->ratio * 10000 + 0.5));
    wzx_history_add(z, e, w);
    wzx_history_end(z);
}

void
wzx_tile_ratio(wzx *z, wzx_win *w, double delta)
{
    int mon = w && w->tiled ? w->tile_mon : -1;
    if (mon < 0)
        return;
    tset *s = set_for(z, mon, 1);
    if (!s)
        return;
    wz_tile_params p = params(z, mon, s);
    if (p.algo != WZ_TILE_BSP)
        record_ratio(z, w, s);
    if (p.algo == WZ_TILE_BSP) {
        wz_bsp_resize(&s->bsp, (long)w->client, WZ_RIGHT, delta);
        wz_bsp_resize(&s->bsp, (long)w->client, WZ_LEFT, delta);
    } else {
        double r = s->ratio + delta;
        s->ratio = r < 0.1 ? 0.1 : r > 0.9 ? 0.9 : r;
    }
    wzx_tile_mark(z);
}

void
wzx_tile_toggle_float(wzx *z, wzx_win *w)
{
    if (!w)
        return;
    wzx_history_push(z, &w, 1);
    if (w->tiled) {
        w->user_float = 1;
        untile(z, w, 1);
    } else if (w->user_float || w->rule_float) {
        w->user_float = 0;
        w->rule_float = 0;
        w->order = ++z->order_seq;
    }
    wzx_tile_mark(z);
}
