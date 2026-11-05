/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "wz-alloc.h"
#include "wz-layout.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WZ_MAX_CHILDREN 64
#define WZ_NAME_MAX 64

wz_node *
wz_leaf_new(double weight)
{
    wz_node *n = wz_xcalloc(1, sizeof *n);
    if (!n)
        return NULL;
    n->kind = WZ_LEAF;
    n->weight = weight > 0 ? weight : 1;
    n->id = -1;
    return n;
}

wz_node *
wz_split_new(wz_kind kind, double weight)
{
    wz_node *n = wz_leaf_new(weight);
    if (n)
        n->kind = kind;
    return n;
}

void
wz_add_child(wz_node *parent, wz_node *child)
{
    wz_node **c = wz_xrealloc(parent->child, sizeof *c * (size_t)(parent->nchild + 1));
    if (!c)
        abort();
    parent->child = c;
    parent->child[parent->nchild++] = child;
    child->parent = parent;
}

void
wz_free(wz_node *n)
{
    if (!n)
        return;
    for (int i = 0; i < n->nchild; i++)
        wz_free(n->child[i]);
    free(n->child);
    free(n->name);
    free(n);
}

wz_node *
wz_clone(const wz_node *n)
{
    if (!n)
        return NULL;
    wz_node *c = wz_leaf_new(n->weight);
    c->kind = n->kind;
    c->id = n->id;
    c->name = n->name ? wz_xstrdup(n->name) : NULL;
    for (int i = 0; i < n->nchild; i++)
        wz_add_child(c, wz_clone(n->child[i]));
    return c;
}

/* ---- números ----------------------------------------------------------- */

double
wz_strtod(const char *s, char **end)
{
    const char *p = s;
    int neg = 0, digits = 0;
    if (*p == '+' || *p == '-')
        neg = *p++ == '-';
    double v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p++ - '0');
        digits++;
    }
    if (*p == '.') {
        p++;
        double scale = 0.1;
        while (*p >= '0' && *p <= '9') {
            v += (*p++ - '0') * scale;
            scale /= 10;
            digits++;
        }
    }
    if (!digits) {
        if (end)
            *end = (char *)s;
        return 0;
    }
    if (end)
        *end = (char *)p;
    return neg ? -v : v;
}

void
wz_fmt_double(char *buf, size_t len, double v)
{
    if (!isfinite(v))
        v = 0;
    long long scaled = llround(v * 10000.0);
    int neg = scaled < 0;
    if (neg)
        scaled = -scaled;
    long long ip = scaled / 10000, fp = scaled % 10000;
    if (fp == 0) {
        snprintf(buf, len, "%s%lld", neg ? "-" : "", ip);
        return;
    }
    char frac[8];
    snprintf(frac, sizeof frac, "%04lld", fp);
    for (int i = 3; i > 0 && frac[i] == '0'; i--)
        frac[i] = 0;
    snprintf(buf, len, "%s%lld.%s", neg ? "-" : "", ip, frac);
}

/* ---- análise ---------------------------------------------------------- */

typedef struct {
    const char *p;
    const char *err;
    int depth;
} parser;

static void
skip_ws(parser *ps)
{
    while (*ps->p && isspace((unsigned char)*ps->p))
        ps->p++;
}

static int
parse_number(parser *ps, double *out)
{
    skip_ws(ps);
    char *end;
    double v = wz_strtod(ps->p, &end);
    if (end == ps->p)
        return 0;
    if (!(v > 0) || v > 1e6 || !isfinite(v)) {
        ps->err = "peso inválido (precisa ser > 0)";
        return -1;
    }
    ps->p = end;
    *out = v;
    return 1;
}

static int
parse_name(parser *ps, char **out)
{
    skip_ws(ps);
    if (*ps->p != '"')
        return 0;
    ps->p++;
    const char *start = ps->p;
    while (*ps->p && *ps->p != '"')
        ps->p++;
    if (*ps->p != '"') {
        ps->err = "nome sem aspas de fechamento";
        return -1;
    }
    size_t len = (size_t)(ps->p - start);
    if (len > WZ_NAME_MAX)
        len = WZ_NAME_MAX;
    *out = wz_xstrndup(start, len);
    ps->p++;
    return 1;
}

static wz_node *parse_node(parser *ps, double weight);

static wz_node *
parse_item(parser *ps)
{
    double w = 1;
    int r = parse_number(ps, &w);
    if (r < 0)
        return NULL;
    skip_ws(ps);
    if (r == 1 && *ps->p == ':') {
        ps->p++;
        return parse_node(ps, w);
    }
    if (r == 1) {
        wz_node *leaf = wz_leaf_new(w);
        if (parse_name(ps, &leaf->name) < 0) {
            wz_free(leaf);
            return NULL;
        }
        return leaf;
    }
    return parse_node(ps, 1);
}

static wz_node *
parse_node(parser *ps, double weight)
{
    skip_ws(ps);
    char c = (char)toupper((unsigned char)*ps->p);
    if (c == 'Z') {
        ps->p++;
        wz_node *leaf = wz_leaf_new(weight);
        if (parse_name(ps, &leaf->name) < 0) {
            wz_free(leaf);
            return NULL;
        }
        return leaf;
    }
    if (c != 'H' && c != 'V') {
        ps->err = "esperava Z, H( ou V(";
        return NULL;
    }
    if (++ps->depth > WZ_MAX_DEPTH) {
        ps->err = "layout profundo demais";
        return NULL;
    }
    ps->p++;
    skip_ws(ps);
    if (*ps->p != '(') {
        ps->err = "esperava ( depois de H ou V";
        return NULL;
    }
    ps->p++;
    wz_node *n = wz_split_new(c == 'H' ? WZ_HSPLIT : WZ_VSPLIT, weight);
    for (;;) {
        if (n->nchild >= WZ_MAX_CHILDREN) {
            ps->err = "divisões demais num nó";
            wz_free(n);
            return NULL;
        }
        wz_node *ch = parse_item(ps);
        if (!ch) {
            if (!ps->err)
                ps->err = "item inválido";
            wz_free(n);
            return NULL;
        }
        wz_add_child(n, ch);
        skip_ws(ps);
        if (*ps->p == ',') {
            ps->p++;
            continue;
        }
        if (*ps->p == ')') {
            ps->p++;
            break;
        }
        ps->err = "esperava , ou )";
        wz_free(n);
        return NULL;
    }
    ps->depth--;
    return n;
}

wz_node *
wz_parse(const char *s, const char **err)
{
    parser ps = { .p = s ? s : "", .err = NULL, .depth = 0 };
    wz_node *n = parse_node(&ps, 1);
    if (n) {
        skip_ws(&ps);
        if (*ps.p) {
            ps.err = "texto sobrando depois do layout";
            wz_free(n);
            n = NULL;
        }
    }
    if (!n && err)
        *err = ps.err ? ps.err : "layout inválido";
    return n ? wz_normalize(n) : NULL;
}

/* ---- serialização ----------------------------------------------------- */

typedef struct {
    char *buf;
    size_t len, cap;
} sbuf;

static void
sb_put(sbuf *b, const char *s)
{
    size_t l = strlen(s);
    if (b->len + l + 1 > b->cap) {
        b->cap = (b->len + l + 1) * 2;
        b->buf = wz_xrealloc(b->buf, b->cap);
        if (!b->buf)
            abort();
    }
    memcpy(b->buf + b->len, s, l + 1);
    b->len += l;
}

static void
sb_weight(sbuf *b, double w)
{
    char tmp[32];
    wz_fmt_double(tmp, sizeof tmp, w);
    sb_put(b, tmp);
}

static void
ser(sbuf *b, const wz_node *n, int in_parent)
{
    if (n->kind == WZ_LEAF) {
        if (in_parent)
            sb_weight(b, n->weight);
        else
            sb_put(b, "Z");
        if (n->name && *n->name) {
            sb_put(b, "\"");
            sb_put(b, n->name);
            sb_put(b, "\"");
        }
        return;
    }
    if (in_parent && fabs(n->weight - 1) > 1e-9) {
        sb_weight(b, n->weight);
        sb_put(b, ":");
    }
    sb_put(b, n->kind == WZ_HSPLIT ? "H(" : "V(");
    for (int i = 0; i < n->nchild; i++) {
        if (i)
            sb_put(b, ",");
        ser(b, n->child[i], 1);
    }
    sb_put(b, ")");
}

char *
wz_serialize(const wz_node *n)
{
    sbuf b = { 0 };
    sb_put(&b, "");
    if (n)
        ser(&b, n, 0);
    return b.buf;
}

/* ---- árvore ----------------------------------------------------------- */

int
wz_leaf_count(const wz_node *n)
{
    if (!n)
        return 0;
    if (n->kind == WZ_LEAF)
        return 1;
    int c = 0;
    for (int i = 0; i < n->nchild; i++)
        c += wz_leaf_count(n->child[i]);
    return c;
}

static wz_node *
leaf_nth(wz_node *n, int *i)
{
    if (n->kind == WZ_LEAF)
        return (*i)-- == 0 ? n : NULL;
    for (int k = 0; k < n->nchild; k++) {
        wz_node *r = leaf_nth(n->child[k], i);
        if (r)
            return r;
    }
    return NULL;
}

wz_node *
wz_leaf_nth(wz_node *n, int i)
{
    if (!n || i < 0)
        return NULL;
    return leaf_nth(n, &i);
}

static int
leaf_index(const wz_node *n, const wz_node *leaf, int *i)
{
    if (n == leaf)
        return 1;
    if (n->kind == WZ_LEAF) {
        (*i)++;
        return 0;
    }
    for (int k = 0; k < n->nchild; k++)
        if (leaf_index(n->child[k], leaf, i))
            return 1;
    return 0;
}

int
wz_leaf_index(const wz_node *root, const wz_node *leaf)
{
    int i = 0;
    if (!root || !leaf)
        return -1;
    return leaf_index(root, leaf, &i) ? i : -1;
}

wz_node *
wz_split_leaf(wz_node *leaf, wz_kind kind, int new_first)
{
    if (!leaf || leaf->kind != WZ_LEAF || kind == WZ_LEAF)
        return NULL;
    /* A folha vira nó interno; o conteúdo dela desce para um filho. */
    wz_node *old = wz_leaf_new(1);
    old->name = leaf->name;
    old->id = leaf->id;
    leaf->name = NULL;
    leaf->id = -1;
    leaf->kind = kind;
    wz_node *fresh = wz_leaf_new(1);
    if (new_first) {
        wz_add_child(leaf, fresh);
        wz_add_child(leaf, old);
    } else {
        wz_add_child(leaf, old);
        wz_add_child(leaf, fresh);
    }
    return fresh;
}

static void
replace_child(wz_node *parent, wz_node *old, wz_node *repl)
{
    for (int i = 0; i < parent->nchild; i++)
        if (parent->child[i] == old) {
            parent->child[i] = repl;
            repl->parent = parent;
            return;
        }
}

wz_node *
wz_remove_leaf(wz_node *root, wz_node *leaf)
{
    if (!root || !leaf)
        return root;
    if (leaf == root) {
        wz_free(root);
        return NULL;
    }
    wz_node *p = leaf->parent;
    int k = 0;
    while (k < p->nchild && p->child[k] != leaf)
        k++;
    if (k == p->nchild)
        return root;
    memmove(&p->child[k], &p->child[k + 1], sizeof *p->child * (size_t)(p->nchild - k - 1));
    p->nchild--;
    wz_free(leaf);
    return wz_normalize(root);
}

wz_node *
wz_normalize(wz_node *n)
{
    if (!n || n->kind == WZ_LEAF)
        return n;
    for (int i = 0; i < n->nchild; i++) {
        wz_node *c = wz_normalize(n->child[i]);
        n->child[i] = c;
        c->parent = n;
    }
    if (n->nchild == 1) {
        wz_node *only = n->child[0];
        only->weight = n->weight;
        only->parent = n->parent;
        if (n->parent)
            replace_child(n->parent, n, only);
        free(n->child);
        free(n->name);
        free(n);
        return only;
    }
    if (n->nchild == 0) {
        n->kind = WZ_LEAF;
        free(n->child);
        n->child = NULL;
    }
    return n;
}

/* ---- cálculo ---------------------------------------------------------- */

/*
 * Os filhos de um nó dividem o espaço que sobra depois dos gaps entre eles:
 * zonas de mesmo peso ficam do mesmo tamanho (a do meio não perde um gap a
 * mais que as das pontas). Arredondamento cumulativo: a soma é exata.
 */
static void
compute(const wz_node *n, wz_rect r, int gap, wz_rect *out, int max, int *idx, int *degenerate)
{
    if (n->kind == WZ_LEAF) {
        if (*idx < max)
            out[*idx] = r;
        (*idx)++;
        return;
    }
    double total = 0;
    for (int i = 0; i < n->nchild; i++)
        total += n->child[i]->weight;
    if (!(total > 0))
        total = 1;
    int span = n->kind == WZ_HSPLIT ? r.w : r.h;
    int base = n->kind == WZ_HSPLIT ? r.x : r.y;
    /* Bordas de cada filho com o gap; se alguma célula sumir, esse nível
     * fica sem gap (a zona vale mais que o espaçamento). */
    /* O mosaico monta nós com um filho por janela (até WZ_TILE_MAX). */
    int cell_edges[2 * WZ_MAX_CHILDREN];
    int *start = n->nchild <= WZ_MAX_CHILDREN ? cell_edges : wz_xmalloc(sizeof(int) * 2 * (size_t)n->nchild);
    int *end = start + n->nchild;
    for (int pass = 0; pass < 2; pass++) {
        int g = pass ? 0 : gap;
        long avail = (long)span - (long)(n->nchild - 1) * g;
        double cum = 0;
        int prev = base, bad = avail < n->nchild;
        for (int i = 0; i < n->nchild && !bad; i++) {
            cum += n->child[i]->weight;
            int next = i == n->nchild - 1 ? base + span
                                          : base + (int)lround((double)avail * (cum / total)) + i * g;
            start[i] = prev;
            end[i] = next;
            if (next - prev < 1)
                bad = 1;
            prev = next + g;
        }
        if (!bad || pass)
            break;
    }
    for (int i = 0; i < n->nchild; i++) {
        wz_rect c = r;
        int len = end[i] - start[i];
        if (len < 1) {
            len = 1;
            *degenerate = 1;
        }
        if (n->kind == WZ_HSPLIT) {
            c.x = start[i];
            c.w = len;
        } else {
            c.y = start[i];
            c.h = len;
        }
        compute(n->child[i], c, gap, out, max, idx, degenerate);
    }
    if (start != cell_edges)
        free(start);
}

int
wz_compute(const wz_node *root, wz_rect area, int gap_in, int gap_out, wz_rect *out, int max)
{
    if (!root)
        return 0;
    if (gap_in < 0)
        gap_in = 0;
    if (gap_out < 0)
        gap_out = 0;
    /* A margem externa não pode engolir a área. */
    if (2 * gap_out >= area.w || 2 * gap_out >= area.h)
        gap_out = 0;
    /* Se alguma célula não coube, os gaps saem (primeiro o interno, depois
     * a margem): melhor zonas sem espaçamento que zonas sobrepostas. */
    int n = 0;
    for (int pass = 0; pass < 3; pass++) {
        int gi = pass ? 0 : gap_in, go = pass == 2 ? 0 : gap_out;
        wz_rect inner = { area.x + go, area.y + go, area.w - 2 * go, area.h - 2 * go };
        int degenerate = 0;
        n = 0;
        compute(root, inner, gi, out, max, &n, &degenerate);
        if (!degenerate)
            break;
    }
    return n;
}

/* ---- consultas -------------------------------------------------------- */

int
wz_rect_contains(wz_rect a, int px, int py)
{
    return px >= a.x && py >= a.y && px < a.x + a.w && py < a.y + a.h;
}

int
wz_rect_intersects(wz_rect a, wz_rect b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

long
wz_rect_overlap_area(wz_rect a, wz_rect b)
{
    int x1 = a.x > b.x ? a.x : b.x, y1 = a.y > b.y ? a.y : b.y;
    int x2 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    int y2 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    if (x2 <= x1 || y2 <= y1)
        return 0;
    return (long)(x2 - x1) * (y2 - y1);
}

wz_rect
wz_rect_union(wz_rect a, wz_rect b)
{
    int x1 = a.x < b.x ? a.x : b.x, y1 = a.y < b.y ? a.y : b.y;
    int x2 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
    int y2 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
    return (wz_rect){ x1, y1, x2 - x1, y2 - y1 };
}

static long
dist_to_rect(wz_rect r, int px, int py)
{
    long dx = px < r.x ? r.x - px : px >= r.x + r.w ? px - (r.x + r.w - 1) : 0;
    long dy = py < r.y ? r.y - py : py >= r.y + r.h ? py - (r.y + r.h - 1) : 0;
    return dx * dx + dy * dy;
}

int
wz_zone_at(const wz_rect *z, int n, int px, int py)
{
    int best = -1;
    long bestd = 0;
    for (int i = 0; i < n; i++) {
        long d = dist_to_rect(z[i], px, py);
        if (best < 0 || d < bestd) {
            best = i;
            bestd = d;
        }
    }
    return best;
}

static int
ranges_overlap(int a1, int a2, int b1, int b2)
{
    return a1 < b2 && b1 < a2;
}

int
wz_target(const wz_rect *z, int n, int px, int py, int span_px, wz_rect *target, int *b)
{
    int a = wz_zone_at(z, n, px, py);
    if (b)
        *b = -1;
    if (a < 0)
        return -1;
    if (target)
        *target = z[a];
    if (span_px <= 0)
        return a;
    wz_rect A = z[a];
    int best = -1;
    long bestd = 0;
    for (int i = 0; i < n; i++) {
        if (i == a)
            continue;
        wz_rect B = z[i];
        long d = -1;
        int tol = span_px;
        /* B à direita/esquerda de A, com faixa vertical em comum. */
        if (ranges_overlap(A.y, A.y + A.h, B.y, B.y + B.h)) {
            if (abs(B.x - (A.x + A.w)) <= tol && px >= A.x + A.w - span_px && py >= B.y && py < B.y + B.h)
                d = labs((long)A.x + A.w - px);
            else if (abs(A.x - (B.x + B.w)) <= tol && px < A.x + span_px && py >= B.y && py < B.y + B.h)
                d = labs((long)px - A.x);
        }
        if (d < 0 && ranges_overlap(A.x, A.x + A.w, B.x, B.x + B.w)) {
            if (abs(B.y - (A.y + A.h)) <= tol && py >= A.y + A.h - span_px && px >= B.x && px < B.x + B.w)
                d = labs((long)A.y + A.h - py);
            else if (abs(A.y - (B.y + B.h)) <= tol && py < A.y + span_px && px >= B.x && px < B.x + B.w)
                d = labs((long)py - A.y);
        }
        if (d >= 0 && (best < 0 || d < bestd)) {
            best = i;
            bestd = d;
        }
    }
    if (best >= 0) {
        if (b)
            *b = best;
        if (target)
            *target = wz_rect_union(A, z[best]);
    }
    return a;
}

int
wz_neighbor(const wz_rect *z, int n, int from, int dir)
{
    if (from < 0 || from >= n)
        return -1;
    wz_rect f = z[from];
    long fcx = f.x * 2L + f.w, fcy = f.y * 2L + f.h; /* centros x2 */
    int best = -1;
    long bests = 0;
    for (int i = 0; i < n; i++) {
        if (i == from)
            continue;
        wz_rect c = z[i];
        long ccx = c.x * 2L + c.w, ccy = c.y * 2L + c.h;
        long along, perp;
        int overlap;
        switch (dir) {
        case WZ_LEFT:
            if (c.x + c.w > f.x + f.w / 2 || ccx >= fcx)
                continue;
            along = fcx - ccx;
            perp = labs(fcy - ccy);
            overlap = ranges_overlap(f.y, f.y + f.h, c.y, c.y + c.h);
            break;
        case WZ_RIGHT:
            if (c.x < f.x + f.w / 2 || ccx <= fcx)
                continue;
            along = ccx - fcx;
            perp = labs(fcy - ccy);
            overlap = ranges_overlap(f.y, f.y + f.h, c.y, c.y + c.h);
            break;
        case WZ_UP:
            if (c.y + c.h > f.y + f.h / 2 || ccy >= fcy)
                continue;
            along = fcy - ccy;
            perp = labs(fcx - ccx);
            overlap = ranges_overlap(f.x, f.x + f.w, c.x, c.x + c.w);
            break;
        default:
            if (c.y < f.y + f.h / 2 || ccy <= fcy)
                continue;
            along = ccy - fcy;
            perp = labs(fcx - ccx);
            overlap = ranges_overlap(f.x, f.x + f.w, c.x, c.x + c.w);
            break;
        }
        /* Zonas na mesma faixa vencem; depois, a mais perto. */
        long score = (overlap ? 0 : 1L << 40) + along + perp / 2;
        if (best < 0 || score < bests) {
            best = i;
            bests = score;
        }
    }
    return best;
}

void
wz_set_divider(wz_node *split, int k, double f, double min_frac)
{
    if (!split || split->kind == WZ_LEAF || k < 0 || k + 1 >= split->nchild)
        return;
    double total = 0, before = 0;
    for (int i = 0; i < split->nchild; i++) {
        if (i < k)
            before += split->child[i]->weight;
        total += split->child[i]->weight;
    }
    double pair = split->child[k]->weight + split->child[k + 1]->weight;
    double min = min_frac * total;
    if (2 * min > pair)
        min = pair / 2;
    double wk = f * total - before;
    if (wk < min)
        wk = min;
    if (wk > pair - min)
        wk = pair - min;
    split->child[k]->weight = wk;
    split->child[k + 1]->weight = pair - wk;
}
