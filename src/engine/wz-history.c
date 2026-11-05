/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * wmake-zones: histórico de organização. Ver wz-history.h.
 */
#include "wz-history.h"
#include "wz-alloc.h"

#include <stdlib.h>
#include <string.h>

void
wz_hist_entry_free(wz_hist_entry *e)
{
    if (!e)
        return;
    free(e->items);
    for (int i = 0; i < e->ncfg; i++) {
        free(e->cfg[i].section);
        free(e->cfg[i].key);
        free(e->cfg[i].value);
    }
    memset(e, 0, sizeof *e);
}

static void
stack_clear(wz_hist_entry *s, int *n)
{
    for (int i = 0; i < *n; i++)
        wz_hist_entry_free(&s[i]);
    *n = 0;
}

static int
clamp_depth(int depth)
{
    return depth < 1 ? 1 : depth > WZ_HISTORY_MAX ? WZ_HISTORY_MAX : depth;
}

void
wz_history_init(wz_history *h, int depth)
{
    memset(h, 0, sizeof *h);
    h->depth = clamp_depth(depth);
    /* Duas pilhas de tamanho fixo (depth + o passo aberto): nada é
     * realocado durante o uso. */
    h->undo = wz_xcalloc((size_t)h->depth + 1, sizeof *h->undo);
    h->redo = wz_xcalloc((size_t)h->depth + 1, sizeof *h->redo);
}

void
wz_history_free(wz_history *h)
{
    if (!h->undo)
        return;
    stack_clear(h->undo, &h->nundo);
    stack_clear(h->redo, &h->nredo);
    free(h->undo);
    free(h->redo);
    memset(h, 0, sizeof *h);
}

/* Mantém os depth passos mais recentes. */
static void
trim(wz_hist_entry *s, int *n, int depth)
{
    if (*n <= depth)
        return;
    int drop = *n - depth;
    for (int i = 0; i < drop; i++)
        wz_hist_entry_free(&s[i]);
    memmove(s, s + drop, sizeof *s * (size_t)depth);
    memset(s + depth, 0, sizeof *s * (size_t)drop);
    *n = depth;
}

void
wz_history_set_depth(wz_history *h, int depth)
{
    depth = clamp_depth(depth);
    if (!h->undo) {
        wz_history_init(h, depth);
        return;
    }
    if (depth == h->depth)
        return;
    trim(h->undo, &h->nundo, depth);
    trim(h->redo, &h->nredo, depth);
    h->undo = wz_xrealloc(h->undo, sizeof *h->undo * (size_t)(depth + 1));
    h->redo = wz_xrealloc(h->redo, sizeof *h->redo * (size_t)(depth + 1));
    if (depth > h->depth) {
        memset(h->undo + h->depth + 1, 0, sizeof *h->undo * (size_t)(depth - h->depth));
        memset(h->redo + h->depth + 1, 0, sizeof *h->redo * (size_t)(depth - h->depth));
    }
    h->depth = depth;
}

static int entry_empty(const wz_hist_entry *e);

/* Abre um passo vazio no topo de uma pilha. Antes, um passo vazio deixado
 * aberto é reaproveitado e a pilha volta à profundidade: o passo aberto é
 * o único além dela, então um passo que fica vazio nunca expulsa um
 * antigo. */
static wz_hist_entry *
push(wz_hist_entry *s, int *n, int depth)
{
    if (*n > 0 && entry_empty(&s[*n - 1]))
        wz_hist_entry_free(&s[--(*n)]);
    trim(s, n, depth);
    wz_hist_entry *e = &s[(*n)++];
    memset(e, 0, sizeof *e);
    return e;
}

wz_hist_entry *
wz_history_begin(wz_history *h)
{
    if (!h->undo)
        wz_history_init(h, WZ_HISTORY_DEFAULT);
    trim(h->redo, &h->nredo, 0);
    return push(h->undo, &h->nundo, h->depth);
}

wz_hist_entry *
wz_history_push_inverse(wz_history *h, int to_redo)
{
    if (!h->undo)
        wz_history_init(h, WZ_HISTORY_DEFAULT);
    return to_redo ? push(h->redo, &h->nredo, h->depth) : push(h->undo, &h->nundo, h->depth);
}

void
wz_hist_add(wz_hist_entry *e, const wz_hist_item *it)
{
    if (!e || !it || e->n >= WZ_HISTORY_ITEMS)
        return;
    for (int i = 0; i < e->n; i++)
        if (e->items[i].win == it->win)
            return;
    /* Cresce em blocos de 4: a maioria dos passos tem uma ou duas janelas. */
    if (e->n % 4 == 0)
        e->items = wz_xrealloc(e->items, sizeof *e->items * (size_t)(e->n + 4));
    e->items[e->n++] = *it;
}

void
wz_hist_add_cfg(wz_hist_entry *e, const char *section, const char *key, const char *value)
{
    if (!e || !section || !key || e->ncfg >= WZ_HIST_CFG_MAX)
        return;
    for (int i = 0; i < e->ncfg; i++)
        if (!strcmp(e->cfg[i].section, section) && !strcmp(e->cfg[i].key, key))
            return;
    wz_hist_cfg *c = &e->cfg[e->ncfg++];
    c->section = wz_xstrdup(section);
    c->key = wz_xstrdup(key);
    c->value = value ? wz_xstrdup(value) : NULL;
}

void
wz_hist_set_aux(wz_hist_entry *e, long aux)
{
    if (!e)
        return;
    e->aux = aux;
    e->has_aux = 1;
}

static int
entry_empty(const wz_hist_entry *e)
{
    return e->n == 0 && e->ncfg == 0 && !e->has_aux;
}

void
wz_history_commit(wz_history *h)
{
    if (!h->undo)
        return;
    if (h->nundo > 0 && entry_empty(&h->undo[h->nundo - 1]))
        wz_hist_entry_free(&h->undo[--h->nundo]);
    if (h->nredo > 0 && entry_empty(&h->redo[h->nredo - 1]))
        wz_hist_entry_free(&h->redo[--h->nredo]);
    trim(h->undo, &h->nundo, h->depth);
    trim(h->redo, &h->nredo, h->depth);
}

int
wz_history_pop(wz_history *h, int redo, wz_hist_entry *out)
{
    wz_hist_entry *s = redo ? h->redo : h->undo;
    int *n = redo ? &h->nredo : &h->nundo;
    if (!s || *n == 0)
        return 0;
    *out = s[--(*n)];
    memset(&s[*n], 0, sizeof s[*n]);
    return 1;
}

static void
forget_in(wz_hist_entry *s, int *n, uintptr_t win)
{
    int k = 0;
    for (int i = 0; i < *n; i++) {
        wz_hist_entry *e = &s[i];
        int m = 0;
        for (int j = 0; j < e->n; j++)
            if (e->items[j].win != win)
                e->items[m++] = e->items[j];
        e->n = m;
        if (entry_empty(e)) {
            wz_hist_entry_free(e);
            continue;
        }
        if (k != i) {
            s[k] = *e;
            memset(e, 0, sizeof *e);
        }
        k++;
    }
    *n = k;
}

void
wz_history_forget(wz_history *h, uintptr_t win)
{
    if (!h->undo)
        return;
    forget_in(h->undo, &h->nundo, win);
    forget_in(h->redo, &h->nredo, win);
}
