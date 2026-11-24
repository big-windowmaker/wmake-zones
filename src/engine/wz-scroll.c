/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * wmake-zones: Rolagem (scrollable tiling). Ver wz-scroll.h.
 */
#define _GNU_SOURCE
#include "wz-scroll.h"
#include "wz-alloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN_COL_W 64

void
wz_scroll_init(wz_scroll *s)
{
    memset(s, 0, sizeof *s);
    s->active = -1;
}

void
wz_scroll_free(wz_scroll *s)
{
    free(s->col);
    wz_scroll_init(s);
}

void
wz_scroll_clear(wz_scroll *s)
{
    s->ncol = 0;
    s->active = -1;
    s->first = 0;
}

int
wz_scroll_copy(wz_scroll *dst, const wz_scroll *src)
{
    wz_scroll_free(dst);
    if (src->ncol > 0) {
        dst->col = malloc(sizeof *dst->col * (size_t)src->ncol);
        if (!dst->col)
            return -1;
        memcpy(dst->col, src->col, sizeof *dst->col * (size_t)src->ncol);
        dst->cap = src->ncol;
    }
    dst->ncol = src->ncol;
    dst->active = src->active;
    dst->first = src->first;
    return 0;
}

int
wz_scroll_find(const wz_scroll *s, long id, int *row)
{
    for (int c = 0; c < s->ncol; c++)
        for (int r = 0; r < s->col[c].n; r++)
            if (s->col[c].id[r] == id) {
                if (row)
                    *row = r;
                return c;
            }
    return -1;
}

int
wz_scroll_has(const wz_scroll *s, long id)
{
    return wz_scroll_find(s, id, NULL) >= 0;
}

int
wz_scroll_count(const wz_scroll *s)
{
    int n = 0;
    for (int c = 0; c < s->ncol; c++)
        n += s->col[c].n;
    return n;
}

long
wz_scroll_active_id(const wz_scroll *s)
{
    if (s->active < 0 || s->active >= s->ncol)
        return 0;
    const wz_scroll_col *c = &s->col[s->active];
    return c->n > 0 ? c->id[c->active < c->n ? c->active : 0] : 0;
}

/* Abre um espaço para uma coluna na posição at. */
static wz_scroll_col *
col_insert(wz_scroll *s, int at)
{
    if (s->ncol >= WZ_SCROLL_MAX_COLS)
        return NULL;
    if (s->ncol == s->cap) {
        int cap = s->cap ? s->cap * 2 : 8;
        wz_scroll_col *n = realloc(s->col, sizeof *n * (size_t)cap);
        if (!n)
            return NULL;
        s->col = n;
        s->cap = cap;
    }
    if (at < 0)
        at = 0;
    if (at > s->ncol)
        at = s->ncol;
    memmove(&s->col[at + 1], &s->col[at], sizeof *s->col * (size_t)(s->ncol - at));
    s->ncol++;
    memset(&s->col[at], 0, sizeof s->col[at]);
    /* Uma coluna entrando antes da visualização a empurra junto. */
    if (at <= s->first && s->ncol > 1)
        s->first++;
    return &s->col[at];
}

static void
col_delete(wz_scroll *s, int at)
{
    memmove(&s->col[at], &s->col[at + 1], sizeof *s->col * (size_t)(s->ncol - at - 1));
    s->ncol--;
    if (s->first > at)
        s->first--;
    if (s->first >= s->ncol)
        s->first = s->ncol > 0 ? s->ncol - 1 : 0;
}

void
wz_scroll_insert(wz_scroll *s, long id, int width, wz_scroll_insert_mode mode)
{
    if (wz_scroll_has(s, id))
        return;
    if (mode == WZ_SCROLL_INSERT_STACK && s->active >= 0 && s->active < s->ncol &&
        s->col[s->active].n < WZ_SCROLL_MAX_ROWS) {
        wz_scroll_col *c = &s->col[s->active];
        c->id[c->n] = id;
        c->weight[c->n] = 1.0;
        c->active = c->n++;
        return;
    }
    int at = s->active < 0 ? s->ncol : s->active + 1;
    wz_scroll_col *c = col_insert(s, at);
    if (!c)
        return;
    c->id[0] = id;
    c->weight[0] = 1.0;
    c->n = 1;
    c->active = 0;
    c->width = width > MIN_COL_W ? width : MIN_COL_W;
    s->active = at;
}

int
wz_scroll_remove(wz_scroll *s, long id)
{
    int r, c = wz_scroll_find(s, id, &r);
    if (c < 0)
        return 0;
    wz_scroll_col *col = &s->col[c];
    memmove(&col->id[r], &col->id[r + 1], sizeof col->id[0] * (size_t)(col->n - r - 1));
    memmove(&col->weight[r], &col->weight[r + 1], sizeof col->weight[0] * (size_t)(col->n - r - 1));
    col->n--;
    if (col->active > r || col->active >= col->n)
        col->active = col->active > 0 ? col->active - 1 : 0;
    if (col->n == 0) {
        col_delete(s, c);
        if (s->ncol == 0)
            s->active = -1;
        else if (s->active > c)
            s->active--;
        else if (s->active == c && s->active >= s->ncol)
            s->active = s->ncol - 1;
    }
    return 1;
}

int
wz_scroll_sync(wz_scroll *s, const long *ids, int n, int width, wz_scroll_insert_mode mode)
{
    int changed = 0;
    for (int c = s->ncol - 1; c >= 0; c--)
        for (int r = s->col[c].n - 1; r >= 0; r--) {
            long id = s->col[c].id[r];
            int found = 0;
            for (int i = 0; i < n && !found; i++)
                found = ids[i] == id;
            if (!found) {
                wz_scroll_remove(s, id);
                changed++;
                if (c >= s->ncol)
                    break;
            }
        }
    for (int i = 0; i < n; i++)
        if (!wz_scroll_has(s, ids[i])) {
            wz_scroll_insert(s, ids[i], width, mode);
            changed++;
        }
    return changed;
}

int
wz_scroll_activate(wz_scroll *s, long id)
{
    int r, c = wz_scroll_find(s, id, &r);
    if (c < 0)
        return 0;
    s->active = c;
    s->col[c].active = r;
    return 1;
}

int
wz_scroll_focus(wz_scroll *s, int dir)
{
    if (s->active < 0)
        return 0;
    wz_scroll_col *c = &s->col[s->active];
    switch (dir) {
    case WZ_LEFT:
        if (s->active == 0)
            return 0;
        s->active--;
        return 1;
    case WZ_RIGHT:
        if (s->active + 1 >= s->ncol)
            return 0;
        s->active++;
        return 1;
    case WZ_UP:
        if (c->active <= 0)
            return 0;
        c->active--;
        return 1;
    case WZ_DOWN:
        if (c->active + 1 >= c->n)
            return 0;
        c->active++;
        return 1;
    }
    return 0;
}

int
wz_scroll_focus_first(wz_scroll *s)
{
    if (s->ncol == 0 || s->active == 0)
        return 0;
    s->active = 0;
    return 1;
}

int
wz_scroll_focus_last(wz_scroll *s)
{
    if (s->ncol == 0 || s->active == s->ncol - 1)
        return 0;
    s->active = s->ncol - 1;
    return 1;
}

int
wz_scroll_expel(wz_scroll *s, int width)
{
    if (s->active < 0 || s->col[s->active].n < 2)
        return 0;
    wz_scroll_col *c = &s->col[s->active];
    long id = c->id[c->active];
    wz_scroll_remove(s, id);
    wz_scroll_insert(s, id, width, WZ_SCROLL_INSERT_COLUMN);
    return 1;
}

int
wz_scroll_move(wz_scroll *s, int dir, int width)
{
    if (s->active < 0)
        return 0;
    wz_scroll_col *c = &s->col[s->active];
    if (dir == WZ_UP || dir == WZ_DOWN) {
        int o = c->active + (dir == WZ_UP ? -1 : 1);
        if (o < 0 || o >= c->n)
            return 0;
        long t = c->id[o];
        c->id[o] = c->id[c->active];
        c->id[c->active] = t;
        double tw = c->weight[o];
        c->weight[o] = c->weight[c->active];
        c->weight[c->active] = tw;
        c->active = o;
        return 1;
    }
    if (dir != WZ_LEFT && dir != WZ_RIGHT)
        return 0;
    if (c->n == 1) {
        int o = s->active + (dir == WZ_LEFT ? -1 : 1);
        if (o < 0 || o >= s->ncol)
            return 0;
        wz_scroll_col t = s->col[o];
        s->col[o] = s->col[s->active];
        s->col[s->active] = t;
        s->active = o;
        return 1;
    }
    /* Sai da coluna para uma coluna nova daquele lado. */
    long id = c->id[c->active];
    int at = s->active;
    wz_scroll_remove(s, id);
    s->active = dir == WZ_LEFT ? at - 1 : at; /* insert põe em active + 1 */
    wz_scroll_insert(s, id, width, WZ_SCROLL_INSERT_COLUMN);
    return 1;
}

int
wz_scroll_move_to(wz_scroll *s, long id, int at)
{
    int r, c = wz_scroll_find(s, id, &r);
    if (c < 0)
        return 0;
    if (at < 0)
        at = 0;
    if (at > s->ncol)
        at = s->ncol;
    if (s->col[c].n == 1) {
        if (at == c || at == c + 1) {
            s->active = c;
            return 0;
        }
        wz_scroll_col tmp = s->col[c];
        memmove(&s->col[c], &s->col[c + 1], sizeof *s->col * (size_t)(s->ncol - c - 1));
        if (at > c)
            at--;
        memmove(&s->col[at + 1], &s->col[at], sizeof *s->col * (size_t)(s->ncol - 1 - at));
        s->col[at] = tmp;
        s->active = at;
        return 1;
    }
    int width = s->col[c].width;
    wz_scroll_remove(s, id); /* a coluna c continua (tinha outras) */
    s->active = at - 1;      /* insert põe em active + 1 */
    wz_scroll_insert(s, id, width, WZ_SCROLL_INSERT_COLUMN);
    return 1;
}

int
wz_scroll_consume(wz_scroll *s, int dir)
{
    if (s->active < 0 || (dir != WZ_LEFT && dir != WZ_RIGHT))
        return 0;
    int src = s->active, target = src + (dir == WZ_LEFT ? -1 : 1);
    if (target < 0 || target >= s->ncol || s->col[target].n >= WZ_SCROLL_MAX_ROWS)
        return 0;
    wz_scroll_col *c = &s->col[src];
    long id = c->id[c->active];
    int alone = c->n == 1;
    wz_scroll_remove(s, id);
    if (alone && target > src)
        target--; /* a coluna de origem sumiu */
    s->active = target;
    wz_scroll_insert(s, id, 0, WZ_SCROLL_INSERT_STACK);
    return 1;
}

void
wz_scroll_set_width(wz_scroll *s, int width)
{
    if (s->active < 0)
        return;
    s->col[s->active].width = width > MIN_COL_W ? width : MIN_COL_W;
}

/* Largura em px de pct% da área útil, de modo que colunas que somam 100%
 * caibam exatas com os gaps entre elas. */
static int
pct_width(const wz_scroll_params *p, int area_w, int pct)
{
    int usable = area_w - 2 * p->gap_out;
    int w = (int)((long)(usable + p->gap_in) * pct / 100) - p->gap_in;
    if (w > usable)
        w = usable;
    return w > MIN_COL_W ? w : MIN_COL_W;
}

int
wz_scroll_default_width(const wz_scroll_params *p, int area_w)
{
    return pct_width(p, area_w, p->width_pct > 0 ? p->width_pct : 50);
}

void
wz_scroll_cycle_width(wz_scroll *s, const wz_scroll_params *p, int area_w, int step)
{
    if (s->active < 0 || p->nwidths <= 0)
        return;
    int cur = s->col[s->active].width, pick = -1;
    if (step > 0) {
        for (int i = 0; i < p->nwidths && pick < 0; i++)
            if (pct_width(p, area_w, p->widths[i]) > cur + 2)
                pick = i;
        if (pick < 0)
            pick = 0;
    } else {
        for (int i = p->nwidths - 1; i >= 0 && pick < 0; i--)
            if (pct_width(p, area_w, p->widths[i]) < cur - 2)
                pick = i;
        if (pick < 0)
            pick = p->nwidths - 1;
    }
    s->col[s->active].width = pct_width(p, area_w, p->widths[pick]);
}

void
wz_scroll_view(wz_scroll *s, int step)
{
    if (s->ncol == 0)
        return;
    int f = s->first + step;
    if (f < 0)
        f = 0;
    if (f > s->ncol - 1)
        f = s->ncol - 1;
    s->first = f;
}

static int
colw(const wz_scroll *s, int c, int maxw)
{
    int w = s->col[c].width;
    if (w > maxw)
        w = maxw;
    return w < MIN_COL_W ? MIN_COL_W : w;
}

/* Cabem as colunas a..b na largura útil? */
static int
fits(const wz_scroll *s, int a, int b, int usable, int gap)
{
    long t = 0;
    for (int c = a; c <= b; c++)
        t += colw(s, c, usable) + (c > a ? gap : 0);
    return t <= usable;
}

void
wz_scroll_center(wz_scroll *s, const wz_scroll_params *p, wz_rect area)
{
    if (s->active < 0)
        return;
    int usable = area.w - 2 * p->gap_out;
    int a = s->active, b = s->active;
    long lw = 0, rw = 0;
    for (;;) {
        int grew = 0;
        if (a > 0 && lw <= rw && fits(s, a - 1, b, usable, p->gap_in)) {
            a--;
            lw += colw(s, a, usable);
            grew = 1;
        } else if (b + 1 < s->ncol && fits(s, a, b + 1, usable, p->gap_in)) {
            b++;
            rw += colw(s, b, usable);
            grew = 1;
        } else if (a > 0 && fits(s, a - 1, b, usable, p->gap_in)) {
            a--;
            lw += colw(s, a, usable);
            grew = 1;
        }
        if (!grew)
            break;
    }
    s->first = a;
}

int
wz_scroll_layout(wz_scroll *s, const wz_scroll_params *p, wz_rect area, int keep_view,
                 long *ids, wz_rect *out, int *visible, int max, int *nleft, int *nright)
{
    if (nleft)
        *nleft = 0;
    if (nright)
        *nright = 0;
    if (s->ncol == 0)
        return 0;
    if (s->active < 0 || s->active >= s->ncol)
        s->active = s->ncol - 1;
    int usable = area.w - 2 * p->gap_out;
    int usable_h = area.h - 2 * p->gap_out;
    int gap = p->gap_in;
    int first = s->first < 0 ? 0 : s->first >= s->ncol ? s->ncol - 1 : s->first;
    if (!keep_view) {
        /* A ativa inteira na tela, mexendo o mínimo. */
        if (s->active < first)
            first = s->active;
        while (first < s->active && !fits(s, first, s->active, usable, gap))
            first++;
    }
    int last = first;
    while (last + 1 < s->ncol && fits(s, first, last + 1, usable, gap))
        last++;
    /* No fim da faixa, sobra espaço: puxa colunas da esquerda. */
    while (first > 0 && fits(s, first - 1, last, usable, gap))
        first--;
    s->first = first;
    long runw = 0;
    for (int c = first; c <= last; c++)
        runw += colw(s, c, usable) + (c > first ? gap : 0);
    int x0 = area.x + p->gap_out;
    if (p->align == WZ_SCROLL_ALIGN_CENTER && runw < usable)
        x0 += (int)(usable - runw) / 2;
    /* Posições na faixa: a sequência visível a partir de x0; as outras
     * seguem para os lados (o backend as tira da tela). */
    int xs[WZ_SCROLL_MAX_COLS];
    int x = x0;
    for (int c = first; c <= last; c++) {
        xs[c] = x;
        x += colw(s, c, usable) + gap;
    }
    /* As escondidas continuam a faixa fora da área, nos dois lados. */
    x = area.x + area.w + p->gap_out;
    for (int c = last + 1; c < s->ncol; c++) {
        xs[c] = x;
        x += colw(s, c, usable) + gap;
    }
    x = area.x - p->gap_out;
    for (int c = first - 1; c >= 0; c--) {
        x -= colw(s, c, usable);
        xs[c] = x;
        x -= gap;
    }
    int k = 0;
    for (int c = 0; c < s->ncol; c++) {
        const wz_scroll_col *col = &s->col[c];
        int w = colw(s, c, usable);
        double tot = 0;
        for (int r = 0; r < col->n; r++)
            tot += col->weight[r] > 0 ? col->weight[r] : 1;
        int avail = usable_h - gap * (col->n - 1);
        double acc = 0;
        int y = area.y + p->gap_out, prev = 0;
        for (int r = 0; r < col->n && k < max; r++) {
            acc += col->weight[r] > 0 ? col->weight[r] : 1;
            int end = (int)(avail * acc / tot + 0.5);
            int h = end - prev;
            prev = end;
            ids[k] = col->id[r];
            out[k] = (wz_rect){ xs[c], y, w, h };
            visible[k] = c >= first && c <= last;
            y += h + gap;
            k++;
        }
    }
    if (nleft)
        *nleft = first;
    if (nright)
        *nright = s->ncol - 1 - last;
    return k;
}

char *
wz_scroll_serialize(const wz_scroll *s)
{
    size_t cap = 64 + (size_t)s->ncol * 48 + (size_t)wz_scroll_count(s) * 32, len = 0;
    char *buf = malloc(cap);
    if (!buf)
        return NULL;
    len += (size_t)snprintf(buf + len, cap - len, "a%d f%d", s->active, s->first);
    for (int c = 0; c < s->ncol; c++) {
        len += (size_t)snprintf(buf + len, cap - len, " | %d:a%d:", s->col[c].width, s->col[c].active);
        for (int r = 0; r < s->col[c].n; r++) {
            char w[24];
            wz_fmt_double(w, sizeof w, s->col[c].weight[r]);
            len += (size_t)snprintf(buf + len, cap - len, "%s%ld*%s", r ? "," : "", s->col[c].id[r], w);
        }
    }
    return buf;
}

int
wz_scroll_parse(wz_scroll *s, const char *text)
{
    wz_scroll_clear(s);
    if (!text)
        return -1;
    int a = -1, f = 0;
    if (sscanf(text, "a%d f%d", &a, &f) != 2)
        return -1;
    const char *p = strchr(text, '|');
    while (p) {
        p++;
        char *end;
        long width = strtol(p, &end, 10);
        if (end == p || *end != ':' || end[1] != 'a')
            goto bad;
        p = end + 2;
        long act = strtol(p, &end, 10);
        if (end == p || *end != ':')
            goto bad;
        p = end + 1;
        wz_scroll_col *c = col_insert(s, s->ncol);
        if (!c)
            goto bad;
        c->width = width > MIN_COL_W ? (int)width : MIN_COL_W;
        while (*p == ' ')
            p++;
        while (*p && *p != '|' && *p != ' ') {
            long id = strtol(p, &end, 10);
            if (end == p || *end != '*' || c->n >= WZ_SCROLL_MAX_ROWS)
                goto bad;
            p = end + 1;
            double w = wz_strtod(p, &end);
            if (end == p)
                goto bad;
            p = end;
            c->id[c->n] = id;
            c->weight[c->n] = w > 0 ? w : 1;
            c->n++;
            if (*p == ',')
                p++;
        }
        if (c->n == 0)
            goto bad;
        c->active = act >= 0 && act < c->n ? (int)act : 0;
        p = strchr(p, '|');
    }
    s->active = s->ncol == 0 ? -1 : a >= 0 && a < s->ncol ? a : 0;
    s->first = f >= 0 && f < s->ncol ? f : 0;
    return 0;
bad:
    wz_scroll_clear(s);
    return -1;
}
