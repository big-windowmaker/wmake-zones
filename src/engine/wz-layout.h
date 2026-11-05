/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * wmake-zones: núcleo de geometria (sem X11 nem Wayland).
 *
 * Um layout é uma árvore de divisões. Folhas são zonas; nós internos dividem
 * o retângulo do pai lado a lado (H, colunas) ou empilhado (V, linhas), com
 * pesos relativos. A forma textual é compacta e cabe numa linha do arquivo
 * de configuração:
 *
 *     H(1,1)                    metades
 *     V(H(1,1),H(1,1))          quadrantes
 *     H(3:V(3,1),2)             editor + terminal embaixo, navegador ao lado
 *     H(1,2,1)                  ultrawide, centro maior
 *     H(1"Editor",1"Web")       folhas com nome
 *
 *   node := 'Z' [name] | ('H'|'V') '(' item (',' item)* ')'
 *   item := weight [name] | [weight ':'] node
 *   name := '"' texto sem aspas '"'
 *
 * O cálculo é inteiro e exato: as divisões usam arredondamento cumulativo, de
 * modo que as zonas cobrem a área sem buracos nem sobreposição, e os gaps
 * internos têm sempre a largura pedida.
 */
#ifndef WZ_LAYOUT_H
#define WZ_LAYOUT_H

#include <stddef.h>

/* Aninhamento máximo que wz_parse aceita (o editor não passa disso). */
#define WZ_MAX_DEPTH 24

typedef struct {
    int x, y, w, h;
} wz_rect;

typedef enum { WZ_LEAF = 0, WZ_HSPLIT, WZ_VSPLIT } wz_kind;

typedef struct wz_node wz_node;
struct wz_node {
    wz_kind kind;
    double weight;     /* relativo aos irmãos, > 0 */
    int nchild;
    wz_node **child;
    wz_node *parent;
    char *name;        /* folha: nome opcional */
    long id;           /* folha: livre para o chamador (BSP: janela) */
};

enum { WZ_LEFT = 0, WZ_RIGHT, WZ_UP, WZ_DOWN };

wz_node *wz_leaf_new(double weight);
wz_node *wz_split_new(wz_kind kind, double weight);
void wz_add_child(wz_node *parent, wz_node *child);
void wz_free(wz_node *n);
wz_node *wz_clone(const wz_node *n);

/* Analisa a forma textual. Em erro devolve NULL e, se err != NULL, uma
 * mensagem estática. */
wz_node *wz_parse(const char *s, const char **err);
/* Forma textual normalizada (malloc). */
char *wz_serialize(const wz_node *n);

int wz_leaf_count(const wz_node *n);
/* i-ésima folha em ordem de profundidade. */
wz_node *wz_leaf_nth(wz_node *n, int i);
int wz_leaf_index(const wz_node *root, const wz_node *leaf);

/* Divide a folha em duas (kind H ou V); a folha vira nó interno com a antiga
 * e uma nova. Devolve a nova folha. */
wz_node *wz_split_leaf(wz_node *leaf, wz_kind kind, int new_first);
/* Remove a folha; o nó pai com um filho só é colapsado. Devolve a nova raiz
 * (pode mudar) ou NULL quando a árvore ficou vazia. */
wz_node *wz_remove_leaf(wz_node *root, wz_node *leaf);
/* Normaliza: colapsa nós internos com um filho e divisões do mesmo tipo
 * aninhadas sem necessidade não são tocadas (preserva a intenção). */
wz_node *wz_normalize(wz_node *root);

/* Calcula as zonas. area: área útil; gap_in: espaço entre zonas; gap_out:
 * margem nas bordas da área. Escreve até max retângulos em out, na ordem
 * das folhas, e devolve o número de folhas. */
int wz_compute(const wz_node *root, wz_rect area, int gap_in, int gap_out,
               wz_rect *out, int max);

/* Zona que contém o ponto; sem nenhuma, a mais próxima (distância à borda).
 * -1 se n == 0. */
int wz_zone_at(const wz_rect *z, int n, int px, int py);

/* Alvo do encaixe com abrangência: perto (span_px) da fronteira entre duas
 * zonas vizinhas, o alvo é a união das duas. Devolve o índice principal; *b
 * recebe a segunda zona ou -1; *target, o retângulo final. */
int wz_target(const wz_rect *z, int n, int px, int py, int span_px,
              wz_rect *target, int *b);

/* Vizinha mais natural na direção (WZ_LEFT...), -1 se não houver. */
int wz_neighbor(const wz_rect *z, int n, int from, int dir);

/* Foco espacial: a janela mais natural na direção dir a partir de from,
 * entre n candidatas (que podem se sobrepor; from não deve estar entre
 * elas). Só valem as que têm o centro adiante do centro de from. A nota
 * soma a distância ao longo do eixo (da borda de from à borda da
 * candidata, zero se sobrepostas, mais metade da distância entre os
 * centros) e o dobro do vão no eixo perpendicular; se as faixas não se
 * cruzam, também o desvio entre os centros: uma janela logo ao lado, mesmo
 * um pouco desalinhada, vence uma alinhada lá longe, e uma que só encosta
 * de lado não conta como "acima". Empate: a primeira da lista (o chamador passa na
 * ordem de empilhamento, a de cima primeiro). -1 se nenhuma. */
int wz_focus_pick(const wz_rect *cand, int n, wz_rect from, int dir);

/* Números sem depender do locale (pt_BR usa vírgula decimal, e a vírgula
 * aqui separa itens). Só dígitos, ponto e sinal; sem expoente. */
double wz_strtod(const char *s, char **end);
/* Até 4 casas decimais, sem zeros sobrando: 0.6, 1, 0.3333. */
void wz_fmt_double(char *buf, size_t len, double v);

/* Utilidades. */
wz_rect wz_rect_union(wz_rect a, wz_rect b);
int wz_rect_intersects(wz_rect a, wz_rect b);
int wz_rect_contains(wz_rect a, int px, int py);
long wz_rect_overlap_area(wz_rect a, wz_rect b);

/* Divisória: o filho k de um nó interno. Ajusta os pesos de k e k+1 de modo
 * que a fronteira entre eles fique na fração f (0..1) do nó, respeitando
 * min_frac para cada um. */
void wz_set_divider(wz_node *split, int k, double f, double min_frac);

#endif
