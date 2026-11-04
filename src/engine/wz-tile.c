/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "wz-tile.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *const algo_names[WZ_TILE_N] = {
    "master", "grid", "columns", "rows", "monocle", "bsp", "dwindle", "spiral",
};

const char *
wz_algo_name(wz_algo a)
{
    return a >= 0 && a < WZ_TILE_N ? algo_names[a] : "master";
}

int
wz_algo_from_name(const char *s)
{
    if (!s)
        return -1;
    for (int i = 0; i < WZ_TILE_N; i++)
        if (strcmp(s, algo_names[i]) == 0)
            return i;
    return -1;
}

static wz_node *
leaf_id(long id, double w)
{
    wz_node *l = wz_leaf_new(w);
    l->id = id;
    return l;
}

static wz_node *
stack(wz_kind k, long first, int count, double w)
{
    if (count == 1)
        return leaf_id(first, w);
    wz_node *n = wz_split_new(k, w);
    for (int i = 0; i < count; i++)
        wz_add_child(n, leaf_id(first + i, 1));
    return n;
}

static double
clampd(double v, double lo, double hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

wz_node *
wz_tile_tree(const wz_tile_params *p, int n, wz_rect area)
{
    if (n <= 0 || p->algo == WZ_TILE_BSP)
        return NULL;
    switch (p->algo) {
    case WZ_TILE_MASTER: {
        int m = p->nmaster < 1 ? 1 : p->nmaster;
        int horiz = p->master_pos == WZ_POS_LEFT || p->master_pos == WZ_POS_RIGHT;
        /* Principais lado a lado quando a principal fica em cima/embaixo. */
        wz_kind mk = horiz ? WZ_VSPLIT : WZ_HSPLIT;
        if (n <= m)
            return stack(mk, 0, n, 1);
        double r = clampd(p->master_ratio, 0.1, 0.9);
        wz_node *root = wz_split_new(horiz ? WZ_HSPLIT : WZ_VSPLIT, 1);
        wz_node *ms = stack(mk, 0, m, r);
        wz_node *st = stack(mk, m, n - m, 1 - r);
        if (p->master_pos == WZ_POS_LEFT || p->master_pos == WZ_POS_TOP) {
            wz_add_child(root, ms);
            wz_add_child(root, st);
        } else {
            wz_add_child(root, st);
            wz_add_child(root, ms);
        }
        return root;
    }
    case WZ_TILE_GRID: {
        int cols = (int)ceil(sqrt((double)n));
        int rows = (n + cols - 1) / cols;
        if (rows == 1)
            return stack(WZ_HSPLIT, 0, n, 1);
        wz_node *root = wz_split_new(WZ_VSPLIT, 1);
        long id = 0;
        for (int r = 0; r < rows; r++) {
            int c = r == rows - 1 ? n - (int)id : cols;
            wz_add_child(root, stack(WZ_HSPLIT, id, c, 1));
            id += c;
        }
        return root;
    }
    case WZ_TILE_COLUMNS:
        return stack(WZ_HSPLIT, 0, n, 1);
    case WZ_TILE_ROWS:
        return stack(WZ_VSPLIT, 0, n, 1);
    case WZ_TILE_DWINDLE:
    case WZ_TILE_SPIRAL: {
        /* Cada janela fica com metade do que sobra, alternando o eixo; na
         * espiral a janela gira (esquerda, cima, direita, baixo). Quando a
         * metade ficaria menor que WZ_TILE_MIN_CELL, as janelas restantes
         * dividem o espaço que sobrou em pilha. */
        wz_node *root = NULL, *hole = NULL;
        double hw = area.w, hh = area.h;
        for (int i = 0; i < n; i++) {
            int left = n - i;
            wz_kind k = i % 2 == 0 ? WZ_HSPLIT : WZ_VSPLIT;
            double along = k == WZ_HSPLIT ? hw : hh;
            wz_node *part;
            int last = left == 1 || along / 2 < WZ_TILE_MIN_CELL;
            if (last) {
                part = left == 1 ? leaf_id(i, 1) : stack(hw >= hh ? WZ_HSPLIT : WZ_VSPLIT, i, left, 1);
            } else {
                part = wz_split_new(k, 1);
                int rest_first = p->algo == WZ_TILE_SPIRAL && (i % 4) >= 2;
                if (!rest_first)
                    wz_add_child(part, leaf_id(i, 1));
                /* lugar do resto: um nó vazio preenchido na volta seguinte */
                wz_node *slot = wz_split_new(WZ_HSPLIT, 1);
                wz_add_child(part, slot);
                if (rest_first)
                    wz_add_child(part, leaf_id(i, 1));
            }
            if (hole) {
                /* troca o nó vazio pelo pedaço novo */
                wz_node *pp = hole->parent;
                for (int c = 0; c < pp->nchild; c++)
                    if (pp->child[c] == hole) {
                        pp->child[c] = part;
                        part->parent = pp;
                    }
                wz_free(hole);
            } else {
                root = part;
            }
            if (last)
                break;
            for (int c = 0; c < part->nchild; c++)
                if (part->child[c]->kind != WZ_LEAF)
                    hole = part->child[c];
            if (k == WZ_HSPLIT)
                hw /= 2;
            else
                hh /= 2;
        }
        return root;
    }
    default:
        return NULL;
    }
}

static void
collect_ids(const wz_node *n, long *ids, int *i, int max)
{
    if (n->kind == WZ_LEAF) {
        if (*i < max)
            ids[*i] = n->id;
        (*i)++;
        return;
    }
    for (int k = 0; k < n->nchild; k++)
        collect_ids(n->child[k], ids, i, max);
}

#define WZ_TILE_MAX 256

int
wz_tile(const wz_tile_params *p, wz_rect area, int n, wz_rect *out, int max)
{
    if (n <= 0)
        return 0;
    if (n > WZ_TILE_MAX)
        n = WZ_TILE_MAX;
    int gi = p->gap_in, go = p->gap_out;
    if (p->smart_gaps && (n == 1 || p->algo == WZ_TILE_MONOCLE))
        gi = go = 0;
    if (p->algo == WZ_TILE_MONOCLE) {
        wz_node *l = wz_leaf_new(1);
        wz_rect r;
        wz_compute(l, area, gi, go, &r, 1);
        wz_free(l);
        for (int i = 0; i < n && i < max; i++)
            out[i] = r;
        return n;
    }
    wz_node *t = wz_tile_tree(p, n, area);
    if (!t)
        return 0;
    wz_rect tmp[WZ_TILE_MAX];
    long ids[WZ_TILE_MAX];
    int c = wz_compute(t, area, gi, go, tmp, WZ_TILE_MAX);
    int k = 0;
    collect_ids(t, ids, &k, WZ_TILE_MAX);
    for (int i = 0; i < c && i < WZ_TILE_MAX; i++)
        if (ids[i] >= 0 && ids[i] < max)
            out[ids[i]] = tmp[i];
    wz_free(t);
    return n;
}

int
wz_fit(wz_rect *r, const wz_limits *lim)
{
    if (!lim)
        return WZ_FIT_OK;
    if ((lim->min_w > 0 && lim->min_w > r->w) || (lim->min_h > 0 && lim->min_h > r->h))
        return WZ_FIT_CONFLICT;
    int res = WZ_FIT_OK;
    if (lim->max_w > 0 && lim->max_w < r->w) {
        r->x += (r->w - lim->max_w) / 2;
        r->w = lim->max_w;
        res = WZ_FIT_SHRUNK;
    }
    if (lim->max_h > 0 && lim->max_h < r->h) {
        r->y += (r->h - lim->max_h) / 2;
        r->h = lim->max_h;
        res = WZ_FIT_SHRUNK;
    }
    return res;
}

/* ---- BSP -------------------------------------------------------------- */

static wz_node *
find_id(wz_node *n, long id)
{
    if (!n)
        return NULL;
    if (n->kind == WZ_LEAF)
        return n->id == id ? n : NULL;
    for (int i = 0; i < n->nchild; i++) {
        wz_node *r = find_id(n->child[i], id);
        if (r)
            return r;
    }
    return NULL;
}

int
wz_bsp_has(const wz_bsp *b, long id)
{
    return find_id(b->root, id) != NULL;
}

int
wz_bsp_compute(const wz_bsp *b, wz_rect area, int gap_in, int gap_out, wz_rect *out, long *ids, int max)
{
    if (!b->root)
        return 0;
    int n = wz_compute(b->root, area, gap_in, gap_out, out, max);
    int k = 0;
    if (ids)
        collect_ids(b->root, ids, &k, max);
    return n;
}

void
wz_bsp_insert(wz_bsp *b, long id, long near_id, wz_rect area)
{
    if (wz_bsp_has(b, id))
        return;
    if (!b->root) {
        b->root = leaf_id(id, 1);
        return;
    }
    wz_node *target = find_id(b->root, near_id);
    wz_rect r = area;
    int n = wz_leaf_count(b->root);
    if (n > WZ_TILE_MAX)
        return;
    wz_rect rects[WZ_TILE_MAX];
    wz_compute(b->root, area, 0, 0, rects, WZ_TILE_MAX);
    if (!target) {
        /* Sem referência: a maior zona. */
        long best = -1;
        for (int i = 0; i < n; i++) {
            long a = (long)rects[i].w * rects[i].h;
            if (a > best) {
                best = a;
                target = wz_leaf_nth(b->root, i);
            }
        }
    }
    int idx = wz_leaf_index(b->root, target);
    if (idx >= 0 && idx < n)
        r = rects[idx];
    if ((r.w >= r.h ? r.w : r.h) / 2 < WZ_TILE_MIN_CELL) {
        /* Pequena demais para dividir: a maior zona recebe a janela. */
        long best = -1;
        for (int i = 0; i < n; i++) {
            long a = (long)rects[i].w * rects[i].h;
            if (a > best) {
                best = a;
                target = wz_leaf_nth(b->root, i);
                r = rects[i];
            }
        }
    }
    wz_node *fresh = wz_split_leaf(target, r.w >= r.h ? WZ_HSPLIT : WZ_VSPLIT, 0);
    fresh->id = id;
}

void
wz_bsp_remove(wz_bsp *b, long id)
{
    wz_node *l = find_id(b->root, id);
    if (l)
        b->root = wz_remove_leaf(b->root, l);
}

void
wz_bsp_swap(wz_bsp *b, long a, long c)
{
    wz_node *x = find_id(b->root, a), *y = find_id(b->root, c);
    if (x && y) {
        x->id = c;
        y->id = a;
    }
}

void
wz_bsp_clear(wz_bsp *b)
{
    wz_free(b->root);
    b->root = NULL;
}

void
wz_bsp_resize(wz_bsp *b, long id, int dir, double delta)
{
    wz_node *n = find_id(b->root, id);
    wz_kind want = dir == WZ_LEFT || dir == WZ_RIGHT ? WZ_HSPLIT : WZ_VSPLIT;
    int forward = dir == WZ_RIGHT || dir == WZ_DOWN;
    while (n && n->parent) {
        wz_node *p = n->parent;
        int k = 0;
        while (k < p->nchild && p->child[k] != n)
            k++;
        if (p->kind == want && (forward ? k + 1 < p->nchild : k > 0)) {
            int other = forward ? k + 1 : k - 1;
            double total = 0;
            for (int i = 0; i < p->nchild; i++)
                total += p->child[i]->weight;
            double pair = p->child[k]->weight + p->child[other]->weight;
            double w = p->child[k]->weight + delta * total;
            double min = 0.1 * total;
            if (2 * min > pair)
                min = pair / 2;
            w = clampd(w, min, pair - min);
            p->child[k]->weight = w;
            p->child[other]->weight = pair - w;
            return;
        }
        n = p;
    }
}
