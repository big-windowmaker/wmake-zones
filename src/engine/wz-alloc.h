/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Alocação que não volta NULL: sem memória, o processo aborta (como a
 * GLib). O serviço guarda só poucos KiB; tratar cada falha de malloc
 * espalharia caminhos de erro que nunca são exercitados.
 */
#ifndef WZ_ALLOC_H
#define WZ_ALLOC_H

#include <stdlib.h>
#include <string.h>

static inline void *
wz_xcheck(void *p)
{
    if (!p)
        abort();
    return p;
}

static inline void *
wz_xmalloc(size_t n)
{
    return wz_xcheck(malloc(n ? n : 1));
}

static inline void *
wz_xcalloc(size_t n, size_t s)
{
    return wz_xcheck(calloc(n ? n : 1, s ? s : 1));
}

static inline void *
wz_xrealloc(void *p, size_t n)
{
    return wz_xcheck(realloc(p, n ? n : 1));
}

static inline char *
wz_xstrdup(const char *s)
{
    return wz_xcheck(strdup(s));
}

static inline char *
wz_xstrndup(const char *s, size_t n)
{
    return wz_xcheck(strndup(s, n));
}

#endif
