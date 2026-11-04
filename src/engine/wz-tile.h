/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * wmake-zones: algoritmos de mosaico (Auto Tiling), sem X11 nem Wayland.
 * Cada algoritmo monta uma árvore de layout (wz-layout) e reaproveita o
 * mesmo cálculo exato das zonas; o BSP guarda a própria árvore.
 */
#ifndef WZ_TILE_H
#define WZ_TILE_H

#include "wz-layout.h"

typedef enum {
    WZ_TILE_MASTER = 0, /* principal + pilha */
    WZ_TILE_GRID,
    WZ_TILE_COLUMNS,
    WZ_TILE_ROWS,
    WZ_TILE_MONOCLE,
    WZ_TILE_BSP,
    WZ_TILE_DWINDLE,
    WZ_TILE_SPIRAL,
    WZ_TILE_N
} wz_algo;

typedef enum { WZ_POS_LEFT = 0, WZ_POS_RIGHT, WZ_POS_TOP, WZ_POS_BOTTOM } wz_master_pos;

typedef struct {
    wz_algo algo;
    double master_ratio;   /* 0.1 .. 0.9 */
    int nmaster;           /* >= 1 */
    wz_master_pos master_pos;
    int gap_in, gap_out;
    int smart_gaps;        /* uma janela só: sem gaps */
} wz_tile_params;

const char *wz_algo_name(wz_algo a);  /* "master", "grid"... */
int wz_algo_from_name(const char *s); /* -1 se desconhecido */

/* Abaixo disso (px) uma célula não é mais dividida ao meio. */
#define WZ_TILE_MIN_CELL 64

/* Árvore equivalente ao algoritmo para n janelas (BSP: NULL); ids das
 * folhas = índice da janela. */
wz_node *wz_tile_tree(const wz_tile_params *p, int n, wz_rect area);

/* Calcula n retângulos (n <= max). Para WZ_TILE_BSP use wz_bsp_*. */
int wz_tile(const wz_tile_params *p, wz_rect area, int n, wz_rect *out, int max);

/* Restrições do cliente (WM_NORMAL_HINTS / xdg min/max), em pixels do
 * retângulo final (já com a decoração somada pelo chamador). 0 = sem. */
typedef struct {
    int min_w, min_h, max_w, max_h;
} wz_limits;

enum { WZ_FIT_OK = 0, WZ_FIT_SHRUNK = 1, WZ_FIT_CONFLICT = 2 };

/* Ajusta r aos limites. Se o máximo é menor, centraliza na célula
 * (WZ_FIT_SHRUNK). Se o mínimo não cabe, devolve WZ_FIT_CONFLICT e não mexe
 * em r: a política documentada é deixar a janela flutuante. */
int wz_fit(wz_rect *r, const wz_limits *lim);

/* ---- BSP persistente ---------------------------------------------------
 * Folhas com id = janela. Inserir divide a folha alvo pelo lado maior.
 */
typedef struct {
    wz_node *root;
} wz_bsp;

void wz_bsp_insert(wz_bsp *b, long id, long near_id, wz_rect area);
void wz_bsp_remove(wz_bsp *b, long id);
int wz_bsp_has(const wz_bsp *b, long id);
void wz_bsp_swap(wz_bsp *b, long a, long c);
/* Calcula; ids[i] recebe o id da folha do retângulo out[i]. */
int wz_bsp_compute(const wz_bsp *b, wz_rect area, int gap_in, int gap_out,
                   wz_rect *out, long *ids, int max);
void wz_bsp_clear(wz_bsp *b);
/* Ajusta a divisão que separa a janela id da vizinha na direção dir, em
 * delta (fração do nó, positiva cresce a janela). */
void wz_bsp_resize(wz_bsp *b, long id, int dir, double delta);

#endif
