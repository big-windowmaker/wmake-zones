/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Rolagem (scrollable tiling) no X11. O núcleo (wz-scroll) decide as
 * colunas; aqui só se escolhem as janelas e se aplicam as geometrias, como
 * no mosaico (wzx-tile.c), com as mesmas regras de elegibilidade.
 *
 * Colunas fora da visualização ficam estacionadas à direita da raiz, fora
 * de todos os monitores: mapeadas (o aplicativo continua desenhando, o
 * Window Maker não as mostra como minimizadas), mas invisíveis e sem vazar
 * para um monitor vizinho, com ou sem compositor. Focar uma delas (lista de
 * janelas, Alt+Tab) rola a faixa até ela.
 *
 * Ao sair da Rolagem (ou do serviço), cada janela volta para a memória do
 * modo livre (_WMAKE_ZONES_FREE): posição, tamanho, zona e maximização de
 * quando as Zonas foram deixadas. A faixa de cada monitor fica em
 * _WMAKE_ZONES_SCROLL na raiz e volta quando a Rolagem volta, mesmo depois
 * de o serviço reiniciar na mesma sessão.
 */
#define _GNU_SOURCE
#include "wzx.h"
#include "../engine/wz-alloc.h"

#include <X11/Xatom.h>
#include <stdlib.h>
#include <string.h>

typedef struct wzx_sset sset;

static int
park_x(wzx *z)
{
    return DisplayWidth(z->dpy, z->screen) + 256;
}

static sset *
set_named(wzx *z, const char *mon, long desk, int create)
{
    for (int i = 0; i < 64; i++)
        if (z->sset[i].used && z->sset[i].desk == desk && !strcmp(z->sset[i].mon, mon))
            return &z->sset[i];
    if (!create)
        return NULL;
    for (int i = 0; i < 64; i++)
        if (!z->sset[i].used) {
            sset *s = &z->sset[i];
            wz_scroll_free(&s->s);
            memset(s, 0, sizeof *s);
            wz_scroll_init(&s->s);
            s->used = 1;
            snprintf(s->mon, sizeof s->mon, "%s", mon);
            s->desk = desk;
            return s;
        }
    return NULL;
}

static int
mon_index(wzx *z, const char *name)
{
    for (int i = 0; i < z->nmon; i++)
        if (!strcmp(z->mon[i].name, name))
            return i;
    return -1;
}

/* Monitor da janela na faixa: o da faixa dela, se ele ainda existe; senão
 * onde ela está (estacionada: o principal). */
static int
member_mon(wzx *z, wzx_win *w)
{
    if (w->scrolled && w->scroll_set >= 0 && w->scroll_set < 64 && z->sset[w->scroll_set].used) {
        int m = mon_index(z, z->sset[w->scroll_set].mon);
        if (m >= 0)
            return m;
    }
    if (w->parked) {
        for (int i = 0; i < z->nmon; i++)
            if (z->mon[i].primary)
                return i;
        return 0;
    }
    return wzx_monitor_of(z, w->outer);
}

static int
by_center_x(const void *a, const void *b)
{
    const wzx_win *x = *(wzx_win *const *)a, *y = *(wzx_win *const *)b;
    int cx = x->outer.x + x->outer.w / 2, cy = y->outer.x + y->outer.w / 2;
    if (cx != cy)
        return cx < cy ? -1 : 1;
    return x->order < y->order ? -1 : x->order > y->order;
}

static void
save_sets(wzx *z)
{
    /* Uma linha por faixa: "monitor<TAB>área<TAB>forma textual". */
    size_t cap = 256, len = 0;
    char *buf = wz_xmalloc(cap);
    buf[0] = 0;
    for (int i = 0; i < 64; i++) {
        if (!z->sset[i].used || z->sset[i].s.ncol == 0)
            continue;
        char *t = wz_scroll_serialize(&z->sset[i].s);
        if (!t)
            continue;
        size_t need = strlen(z->sset[i].mon) + strlen(t) + 48;
        if (len + need >= cap) {
            cap = (len + need) * 2;
            buf = wz_xrealloc(buf, cap);
        }
        len += (size_t)snprintf(buf + len, cap - len, "%s\t%ld\t%s\n", z->sset[i].mon, z->sset[i].desk, t);
        free(t);
    }
    XChangeProperty(z->dpy, z->root, z->atom[A_WZ_SCROLL], z->atom[A_UTF8_STRING], 8, PropModeReplace,
                    (unsigned char *)buf, (int)len);
    free(buf);
}

static void
load_sets(wzx *z)
{
    static int loaded;
    if (loaded)
        return;
    loaded = 1;
    Atom rt;
    int fmt;
    unsigned long n, after;
    unsigned char *d = NULL;
    if (XGetWindowProperty(z->dpy, z->root, z->atom[A_WZ_SCROLL], 0, 65536, False, AnyPropertyType, &rt, &fmt, &n,
                           &after, &d) != Success || !d) {
        if (d)
            XFree(d);
        return;
    }
    char *text = wz_xstrdup((char *)d);
    XFree(d);
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        char *t1 = strchr(line, '\t');
        char *t2 = t1 ? strchr(t1 + 1, '\t') : NULL;
        if (!t2)
            continue;
        *t1 = 0;
        sset *s = set_named(z, line, atol(t1 + 1), 1);
        if (s && wz_scroll_parse(&s->s, t2 + 1) != 0)
            s->used = 0;
    }
    free(text);
}

static void
apply(wzx *z, wzx_win *w, wz_rect r, int visible)
{
    if (!visible)
        r.x = park_x(z);
    w->parked = !visible;
    if (!w->expect_valid || memcmp(&w->expect, &r, sizeof r) != 0)
        wzx_move_outer(z, w, r);
}

static void
unscroll(wzx *z, wzx_win *w, int restore)
{
    if (restore && !wzx_free_restore(z, w) && w->parked) {
        /* Sem memória (abriu na Rolagem): fica onde estaria, na tela. */
        int m = member_mon(z, w);
        wzx_update_workarea(z);
        wz_rect a = z->mon[m].work, r = w->outer;
        r.w = r.w > a.w ? a.w : r.w;
        r.h = r.h > a.h ? a.h : r.h;
        r.x = a.x + (a.w - r.w) / 2;
        r.y = r.y < a.y || r.y + r.h > a.y + a.h ? a.y + (a.h - r.h) / 2 : r.y;
        wzx_move_outer(z, w, r);
    }
    w->scrolled = 0;
    w->parked = 0;
    w->scroll_set = -1;
    w->expect_valid = 0;
}

int
wzx_scroll_any(wzx *z)
{
    for (int i = 0; i < z->nwin; i++)
        if (z->win[i]->scrolled)
            return 1;
    return 0;
}

void
wzx_scroll_leave(wzx *z, int to_tiling)
{
    /* Por janela: só fica para o mosaico quem está num monitor com o
     * mosaico ligado; nos outros, volta à memória livre (senão ficaria
     * estacionada fora da tela). */
    for (int i = 0; i < z->nwin; i++) {
        wzx_win *w = z->win[i];
        if (!w->scrolled)
            continue;
        int tiles = to_tiling && wzx_tile_active_for(z, member_mon(z, w));
        unscroll(z, w, !tiles);
    }
    XFlush(z->dpy);
}

void
wzx_scroll_run(wzx *z)
{
    load_sets(z);
    wzx_update_workarea(z);
    const wz_scroll_params *p = &z->cfg.scroll_p;
    for (int mon = 0; mon < z->nmon; mon++) {
        wzx_win *ws[WZX_MAX_WIN];
        int n = 0;
        for (int i = 0; i < z->nwin && n < WZX_MAX_WIN; i++) {
            wzx_win *w = z->win[i];
            if (wzx_tileable(z, w) && member_mon(z, w) == mon)
                ws[n++] = w;
        }
        sset *s = set_named(z, z->mon[mon].name, z->current_desktop, n > 0);
        if (!s)
            continue;
        wz_rect area = z->mon[mon].work;
        int width = wz_scroll_default_width(p, area.w);
        if (s->s.ncol == 0 && n > 1) {
            /* Entrando: as janelas viram colunas na ordem em que estão na
             * tela, da esquerda para a direita, cada uma com a largura que
             * tinha (limitada à área). */
            qsort(ws, (size_t)n, sizeof *ws, by_center_x);
            for (int i = 0; i < n; i++) {
                int cw = ws[i]->outer.w < area.w - 2 * p->gap_out ? ws[i]->outer.w : width;
                wz_scroll_insert(&s->s, (long)ws[i]->client, cw, WZ_SCROLL_INSERT_COLUMN);
            }
        } else {
            long ids[WZX_MAX_WIN];
            for (int i = 0; i < n; i++)
                ids[i] = (long)ws[i]->client;
            /* Janelas novas: na ordem em que abriram. */
            for (int i = 1; i < n; i++)
                for (int j = i; j > 0 && ws[j - 1]->order > ws[j]->order; j--) {
                    wzx_win *t = ws[j];
                    ws[j] = ws[j - 1];
                    ws[j - 1] = t;
                    long ti = ids[j];
                    ids[j] = ids[j - 1];
                    ids[j - 1] = ti;
                }
            wz_scroll_sync(&s->s, ids, n, width, p->insert);
        }
        if (s->s.ncol == 0) {
            /* Faixa vazia: o lugar volta a ficar livre (são 64). */
            s->used = 0;
            continue;
        }
        wzx_win *aw = wzx_find(z, z->active);
        if (aw && !s->keep_view)
            wz_scroll_activate(&s->s, (long)aw->client);
        long ids[WZX_MAX_WIN];
        wz_rect rects[WZX_MAX_WIN];
        int vis[WZX_MAX_WIN];
        int k = wz_scroll_layout(&s->s, p, area, s->keep_view, ids, rects, vis, WZX_MAX_WIN, &s->nleft, &s->nright);
        int idx = (int)(s - z->sset);
        for (int i = 0; i < k; i++) {
            wzx_win *w = wzx_find(z, (Window)ids[i]);
            if (!w)
                continue;
            if (!w->scrolled && !w->tiled)
                wzx_free_save(z, w);
            if (w->tiled) {
                /* Do mosaico direto para a Rolagem: a memória livre já foi
                 * guardada ao entrar no mosaico. */
                w->tiled = 0;
                w->tile_mon = -1;
            }
            w->scrolled = 1;
            w->scroll_set = idx;
            apply(z, w, rects[i], vis[i]);
        }
    }
    /* Quem saiu da faixa (minimizada, tela cheia, flutuante): sai da
     * Rolagem; tirada pelo usuário, volta para a memória livre. */
    for (int i = 0; i < z->nwin; i++) {
        wzx_win *w = z->win[i];
        if (!w->scrolled || w->desktop != z->current_desktop)
            continue;
        int member = w->scroll_set >= 0 && z->sset[w->scroll_set].used &&
                     wz_scroll_has(&z->sset[w->scroll_set].s, (long)w->client) && wzx_tileable(z, w);
        if (!member)
            unscroll(z, w, w->user_float || w->parked);
    }
    save_sets(z);
    wzx_dockapp_redraw(z);
    XFlush(z->dpy);
}

void
wzx_scroll_focus_changed(wzx *z, Window active)
{
    if (!z->cfg.scroll)
        return;
    wzx_win *w = wzx_find(z, active);
    if (!w || !w->scrolled || w->scroll_set < 0)
        return;
    sset *s = &z->sset[w->scroll_set];
    s->keep_view = 0;
    long before = wz_scroll_active_id(&s->s);
    int first = s->s.first;
    wz_scroll_activate(&s->s, (long)w->client);
    if (before != (long)w->client || w->parked || first != s->s.first)
        wzx_tile_mark(z);
}

static sset *
active_set(wzx *z, wzx_win **wp)
{
    wzx_win *w = wzx_find(z, z->active);
    if (wp)
        *wp = w;
    if (!z->cfg.scroll || !w || !w->scrolled || w->scroll_set < 0 || !z->sset[w->scroll_set].used)
        return NULL;
    return &z->sset[w->scroll_set];
}

static void
focus_id(wzx *z, sset *s)
{
    wzx_win *t = wzx_find(z, (Window)wz_scroll_active_id(&s->s));
    if (t)
        wzx_activate(z, t);
}

static int
dir_of(const char *a)
{
    if (!a)
        return -1;
    return !strcmp(a, "left") ? WZ_LEFT : !strcmp(a, "right") ? WZ_RIGHT : !strcmp(a, "up") ? WZ_UP
           : !strcmp(a, "down") ? WZ_DOWN : -1;
}

int
wzx_scroll_cmd(wzx *z, const char *op, const char *arg, char *out, size_t outlen)
{
    wzx_win *w;
    sset *s = active_set(z, &w);
    if (!s)
        return 0;
    int mon = member_mon(z, w);
    wz_rect area = z->mon[mon].work;
    const wz_scroll_params *p = &z->cfg.scroll_p;
    int width = wz_scroll_default_width(p, area.w);
    int changed = 0, refocus = 0;
    s->keep_view = 0;
    if (!strcmp(op, "focus")) {
        if (arg && !strcmp(arg, "first"))
            changed = wz_scroll_focus_first(&s->s);
        else if (arg && !strcmp(arg, "last"))
            changed = wz_scroll_focus_last(&s->s);
        else
            changed = dir_of(arg) >= 0 && wz_scroll_focus(&s->s, dir_of(arg));
        refocus = changed;
    } else if (!strcmp(op, "move")) {
        changed = dir_of(arg) >= 0 && wz_scroll_move(&s->s, dir_of(arg), width);
    } else if (!strcmp(op, "consume")) {
        changed = wz_scroll_consume(&s->s, arg && !strcmp(arg, "right") ? WZ_RIGHT : WZ_LEFT);
    } else if (!strcmp(op, "expel")) {
        changed = wz_scroll_expel(&s->s, width);
    } else if (!strcmp(op, "center")) {
        wz_scroll_center(&s->s, p, area);
        s->keep_view = 1;
        changed = 1;
    } else if (!strcmp(op, "width")) {
        if (arg && (!strcmp(arg, "+") || !strcmp(arg, "next")))
            wz_scroll_cycle_width(&s->s, p, area.w, 1);
        else if (arg && (!strcmp(arg, "-") || !strcmp(arg, "prev")))
            wz_scroll_cycle_width(&s->s, p, area.w, -1);
        else if (arg && strchr(arg, '%')) {
            wz_scroll_params q = *p;
            q.width_pct = atoi(arg);
            if (q.width_pct >= 10 && q.width_pct <= 100)
                wz_scroll_set_width(&s->s, wz_scroll_default_width(&q, area.w));
        } else if (arg && atoi(arg) > 0)
            wz_scroll_set_width(&s->s, atoi(arg));
        changed = 1;
    } else if (!strcmp(op, "view")) {
        /* Rola sem mudar o foco. */
        wz_scroll_view(&s->s, arg && !strcmp(arg, "left") ? -1 : 1);
        s->keep_view = 1;
        changed = 1;
    } else {
        return 0;
    }
    if (refocus)
        focus_id(z, s);
    if (changed)
        wzx_tile_mark(z);
    if (out)
        snprintf(out, outlen, "scroll=%s\n", changed ? "ok" : "unchanged");
    return 1;
}

int
wzx_scroll_info(wzx *z, int mon, int *ncol, int *active, int *nleft, int *nright, int *widths, int max)
{
    if (mon < 0 || mon >= z->nmon)
        return 0;
    sset *s = set_named(z, z->mon[mon].name, z->current_desktop, 0);
    if (!s || s->s.ncol == 0)
        return 0;
    *ncol = s->s.ncol;
    *active = s->s.active;
    *nleft = s->nleft;
    *nright = s->nright;
    for (int i = 0; widths && i < s->s.ncol && i < max; i++)
        widths[i] = s->s.col[i].width;
    return 1;
}

void
wzx_scroll_free_all(wzx *z)
{
    for (int i = 0; i < 64; i++)
        wz_scroll_free(&z->sset[i].s);
}

void
wzx_scroll_drop(wzx *z, wzx_win *w, int px, int py)
{
    (void)py;
    if (!w->scrolled || w->scroll_set < 0 || !z->sset[w->scroll_set].used) {
        wzx_tile_mark(z);
        return;
    }
    sset *s = &z->sset[w->scroll_set];
    int mon = member_mon(z, w);
    long ids[WZX_MAX_WIN];
    wz_rect rects[WZX_MAX_WIN];
    int vis[WZX_MAX_WIN];
    wz_scroll tmp;
    wz_scroll_init(&tmp);
    wz_scroll_copy(&tmp, &s->s);
    int k = wz_scroll_layout(&tmp, &z->cfg.scroll_p, z->mon[mon].work, 1, ids, rects, vis, WZX_MAX_WIN, NULL, NULL);
    wz_scroll_free(&tmp);
    /* Antes da primeira coluna visível cuja metade fica à direita do
     * ponteiro; depois de todas, no fim da sequência visível. */
    int at = -1, last = -1;
    for (int i = 0; i < k && at < 0; i++) {
        if (!vis[i] || ids[i] == (long)w->client)
            continue;
        int c = wz_scroll_find(&s->s, ids[i], NULL);
        last = c;
        if (px < rects[i].x + rects[i].w / 2)
            at = c;
    }
    if (at < 0)
        at = last >= 0 ? last + 1 : s->s.ncol;
    s->keep_view = 0;
    wz_scroll_move_to(&s->s, (long)w->client, at);
    w->expect_valid = 0; /* volta para a coluna mesmo sem mudar de lugar */
    wzx_tile_mark(z);
}

void
wzx_scroll_user_resize(wzx *z, wzx_win *w)
{
    if (!w->scrolled || w->scroll_set < 0 || !z->sset[w->scroll_set].used)
        return;
    sset *s = &z->sset[w->scroll_set];
    wzx_refresh_geom(z, w);
    if (wz_scroll_activate(&s->s, (long)w->client))
        wz_scroll_set_width(&s->s, w->outer.w);
    w->expect_valid = 0;
    wzx_tile_mark(z);
}
