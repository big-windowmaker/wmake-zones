/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * wmake-zones: histórico de organização (desfazer e refazer), comum ao X11
 * e ao wlmaker.
 *
 * Cada passo guarda só o necessário para voltar: para cada janela tocada, a
 * geometria (com a decoração), a zona, a ordem no mosaico e dois estados
 * (no mosaico, tirada pelo usuário); e, se o passo mudou a configuração,
 * uma chave do arquivo com o valor de antes. Nada de cópias do estado do
 * desktop: um passo com uma janela ocupa uns 60 bytes.
 *
 * As janelas são identificadas por um número opaco do backend (Window do X
 * ou o ponteiro da janela no wlmaker). Uma janela que fecha é esquecida
 * (wz_history_forget): o histórico nunca devolve um identificador morto.
 * Passos que ficam vazios somem.
 */
#ifndef WZ_HISTORY_H
#define WZ_HISTORY_H

#include "wz-layout.h"

#include <stdint.h>

#define WZ_HISTORY_DEFAULT 20
#define WZ_HISTORY_MAX 100
/* Janelas por passo (restaurar todas, organizar). */
#define WZ_HISTORY_ITEMS 128

enum {
    WZ_HIST_TILED = 1 << 0,      /* estava no mosaico */
    WZ_HIST_USER_FLOAT = 1 << 1, /* o usuário tinha tirado do mosaico */
    WZ_HIST_HAS_RESTORE = 1 << 2,/* tinha geometria de antes do encaixe */
};

typedef struct {
    uintptr_t win;    /* identificador do backend */
    wz_rect r;        /* geometria com decoração */
    wz_rect restore;  /* geometria de antes do encaixe (se HAS_RESTORE) */
    long order;       /* ordem no mosaico */
    int zone;         /* zona, -1 = nenhuma */
    unsigned flags;
} wz_hist_item;

typedef struct {
    int n;
    wz_hist_item *items;
    /* Mudança de configuração (opcional): seção, chave e o valor que
     * volta (NULL = apagar a chave). */
    char *section, *key, *value;
    int has_cfg;
    /* Dado livre do backend (ex.: proporção da principal x 10000). */
    long aux;
    int has_aux;
} wz_hist_entry;

typedef struct {
    wz_hist_entry *undo;
    int nundo;
    wz_hist_entry *redo;
    int nredo;
    int depth;
} wz_history;

void wz_history_init(wz_history *h, int depth);
void wz_history_free(wz_history *h);
/* Muda a profundidade (descarta os passos mais antigos). */
void wz_history_set_depth(wz_history *h, int depth);

/* Começa um passo novo no desfazer (e esvazia o refazer, como em qualquer
 * editor). O ponteiro vale até a próxima chamada que mexa no histórico. */
wz_hist_entry *wz_history_begin(wz_history *h);
/* Acrescenta uma janela ao passo (repetida: a primeira vale). */
void wz_hist_add(wz_hist_entry *e, const wz_hist_item *it);
void wz_hist_set_cfg(wz_hist_entry *e, const char *section, const char *key, const char *value);
void wz_hist_set_aux(wz_hist_entry *e, long aux);
/* Descarta o passo aberto se ficou vazio (nada a desfazer). */
void wz_history_commit(wz_history *h);

/* Tira o último passo do desfazer (redo = 0) ou do refazer (redo = 1).
 * O chamador aplica, monta o passo inverso com wz_history_push_inverse e
 * libera com wz_hist_entry_free. 0 = nada a tirar. */
int wz_history_pop(wz_history *h, int redo, wz_hist_entry *out);
/* Guarda o passo inverso na outra pilha, sem esvaziar o refazer. */
wz_hist_entry *wz_history_push_inverse(wz_history *h, int to_redo);
void wz_hist_entry_free(wz_hist_entry *e);

/* Esquece uma janela fechada em todos os passos. */
void wz_history_forget(wz_history *h, uintptr_t win);

#endif
