/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * wmake-zones: Rolagem (scrollable tiling), sem X11 nem Wayland.
 *
 * As janelas de um monitor (e área de trabalho) ficam numa faixa horizontal
 * de colunas; o monitor é uma janela de visualização sobre a faixa. Cada
 * coluna tem largura própria e uma ou mais janelas empilhadas, com alturas
 * relativas. Abrir uma janela acrescenta uma coluna ao lado da ativa sem
 * mudar a largura de nenhuma outra; fechar tira a coluna vazia.
 *
 * Comportamento de referência: o niri (GPL-3.0-or-later). Nada do código
 * dele é usado aqui; os algoritmos são próprios, em C, para o núcleo leve.
 *
 * Só colunas inteiras aparecem: a visualização anda de coluna em coluna e
 * mostra a maior sequência que cabe, sempre com a coluna ativa. As outras
 * ficam fora de todos os monitores (o backend as estaciona), então nada
 * vaza para um monitor vizinho, com ou sem compositor.
 */
#ifndef WZ_SCROLL_H
#define WZ_SCROLL_H
#include "wz-layout.h"

/* Limite de colunas e de janelas por coluna. */
#define WZ_SCROLL_MAX_COLS 256
#define WZ_SCROLL_MAX_ROWS 16

typedef struct {
    long id[WZ_SCROLL_MAX_ROWS];
    double weight[WZ_SCROLL_MAX_ROWS]; /* altura relativa, > 0 */
    int n;
    int active;   /* linha ativa */
    int width;    /* px, com a decoração */
} wz_scroll_col;

typedef struct {
    wz_scroll_col *col;
    int ncol, cap;
    int active;   /* coluna ativa, -1 se vazia */
    int first;    /* primeira coluna mostrada (memória da visualização) */
} wz_scroll;

typedef enum { WZ_SCROLL_INSERT_COLUMN = 0, WZ_SCROLL_INSERT_STACK } wz_scroll_insert_mode;
typedef enum { WZ_SCROLL_ALIGN_LEFT = 0, WZ_SCROLL_ALIGN_CENTER } wz_scroll_align;

typedef struct {
    int width_pct;              /* largura de uma coluna nova, % da área */
    int widths[8];              /* larguras do ciclo (%), crescentes */
    int nwidths;
    int gap_in, gap_out;
    wz_scroll_insert_mode insert;
    wz_scroll_align align;
} wz_scroll_params;

void wz_scroll_init(wz_scroll *s);
void wz_scroll_free(wz_scroll *s);
void wz_scroll_clear(wz_scroll *s);
/* Cópia independente (memória por modo, desfazer). */
int wz_scroll_copy(wz_scroll *dst, const wz_scroll *src);

int wz_scroll_has(const wz_scroll *s, long id);
/* Coluna e linha da janela; -1 se não está. */
int wz_scroll_find(const wz_scroll *s, long id, int *row);
int wz_scroll_count(const wz_scroll *s);
long wz_scroll_active_id(const wz_scroll *s); /* 0 se vazia */

/* Insere a janela. INSERT_COLUMN: coluna nova logo à direita da ativa, com
 * width px; INSERT_STACK: embaixo, na coluna ativa. A nova fica ativa. */
void wz_scroll_insert(wz_scroll *s, long id, int width, wz_scroll_insert_mode mode);
/* Tira a janela; a coluna vazia some. A ativa passa para a vizinha da
 * direita (ou da esquerda, se era a última). 0 se não estava. */
int wz_scroll_remove(wz_scroll *s, long id);
/* Mantém só as janelas de ids (n): tira as que sumiram, acrescenta as novas
 * (na ordem dada, como colunas à direita da ativa). Devolve quantas
 * mudaram. */
int wz_scroll_sync(wz_scroll *s, const long *ids, int n, int width, wz_scroll_insert_mode mode);

/* Ativa a janela (coluna e linha). 0 se não está. */
int wz_scroll_activate(wz_scroll *s, long id);
/* Foco: WZ_LEFT/WZ_RIGHT entre colunas, WZ_UP/WZ_DOWN dentro da coluna.
 * Devolve 1 se mudou. */
int wz_scroll_focus(wz_scroll *s, int dir);
int wz_scroll_focus_first(wz_scroll *s);
int wz_scroll_focus_last(wz_scroll *s);
/* Move a janela ativa: LEFT/RIGHT troca a coluna inteira de lugar quando a
 * janela está sozinha nela; numa coluna com outras, a janela sai para uma
 * coluna nova daquele lado. UP/DOWN troca de lugar dentro da coluna. */
int wz_scroll_move(wz_scroll *s, int dir, int width);
/* Leva a janela para a posição at (0..ncol) entre as colunas: sozinha na
 * coluna, a coluna inteira muda de lugar; com outras, sai para uma coluna
 * nova ali, com a mesma largura. A janela fica ativa. 1 se mudou. */
int wz_scroll_move_to(wz_scroll *s, long id, int at);
/* A janela ativa entra na coluna vizinha (dir LEFT/RIGHT), embaixo. */
int wz_scroll_consume(wz_scroll *s, int dir);
/* A janela ativa sai da coluna para uma coluna nova à direita. */
int wz_scroll_expel(wz_scroll *s, int width);
/* Largura da coluna ativa em px. */
void wz_scroll_set_width(wz_scroll *s, int width);
/* Próxima (step > 0) ou anterior largura do ciclo, em % de area_w. */
void wz_scroll_cycle_width(wz_scroll *s, const wz_scroll_params *p, int area_w, int step);
/* Rola a visualização sem mudar o foco: step colunas. */
void wz_scroll_view(wz_scroll *s, int step);
/* Visualização com a coluna ativa no meio (o que couber dos dois lados). */
void wz_scroll_center(wz_scroll *s, const wz_scroll_params *p, wz_rect area);

/* Geometria. Para cada janela, na ordem das colunas e linhas: ids[i],
 * out[i] e visible[i] (0 = fora da visualização, o backend estaciona).
 * Ajusta s->first para mostrar a coluna ativa mexendo o mínimo, a menos que
 * keep_view (rolagem sem foco). Devolve o número de janelas. *nleft e
 * *nright (opcionais): colunas escondidas de cada lado. */
int wz_scroll_layout(wz_scroll *s, const wz_scroll_params *p, wz_rect area, int keep_view,
                     long *ids, wz_rect *out, int *visible, int max, int *nleft, int *nright);

/* Largura de uma coluna nova (px) para a área. */
int wz_scroll_default_width(const wz_scroll_params *p, int area_w);

/* Forma textual (memória entre sessões e "status"):
 *   "a1 f0 | 960:a0:7*1 | 640:a1:5*1,9*0.5"
 * a = coluna ativa, f = primeira mostrada; cada coluna: largura, linha
 * ativa e janelas id*peso. Ids são do backend (só valem na sessão). */
char *wz_scroll_serialize(const wz_scroll *s);
int wz_scroll_parse(wz_scroll *s, const char *text);

#endif
