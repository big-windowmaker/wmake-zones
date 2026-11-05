/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Testes do núcleo: propriedades das zonas e dos mosaicos com entradas
 * aleatórias (semente fixa), análise/serialização e o INI.
 */
#define _GNU_SOURCE
#include "wz-config.h"
#include "wz-history.h"
#include "wz-layout.h"
#include "wz-tile.h"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <unistd.h>

static int failures, checks;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        checks++;                                                              \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "FALHOU %s:%d: ", __FILE__, __LINE__);             \
            fprintf(stderr, __VA_ARGS__);                                      \
            fputc('\n', stderr);                                               \
        }                                                                      \
    } while (0)

static unsigned long rng = 0x9e3779b97f4a7c15UL;
static unsigned
rnd(unsigned n)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return n ? (unsigned)(rng % n) : 0;
}

static wz_node *
random_tree(int depth)
{
    if (depth <= 0 || rnd(3) == 0)
        return wz_leaf_new(1 + rnd(5));
    wz_node *n = wz_split_new(rnd(2) ? WZ_HSPLIT : WZ_VSPLIT, 1 + rnd(4));
    int k = 2 + rnd(3);
    for (int i = 0; i < k; i++)
        wz_add_child(n, random_tree(depth - 1));
    return n;
}

static int
inside(wz_rect r, wz_rect a)
{
    return r.x >= a.x && r.y >= a.y && r.x + r.w <= a.x + a.w && r.y + r.h <= a.y + a.h;
}

static void
test_parse(void)
{
    const char *err = NULL;
    const char *ok[] = { "Z", "H(1,1)", "V(H(1,1),H(1,1))", "H(3:V(3\"Editor\",1\"Terminal\"),2\"Browser\")",
                         "H(1,2,1)", " h ( 1 , v(1,1) ) ", "H(0.5,1.5)" };
    for (size_t i = 0; i < sizeof ok / sizeof *ok; i++) {
        wz_node *n = wz_parse(ok[i], &err);
        CHECK(n != NULL, "parse '%s': %s", ok[i], err ? err : "?");
        if (!n)
            continue;
        char *s = wz_serialize(n);
        wz_node *m = wz_parse(s, &err);
        CHECK(m != NULL, "reparse '%s'", s);
        char *s2 = m ? wz_serialize(m) : NULL;
        CHECK(s2 && !strcmp(s, s2), "ida e volta estável: '%s' vs '%s'", s, s2 ? s2 : "(null)");
        free(s);
        free(s2);
        wz_free(m);
        wz_free(n);
    }
    const char *bad[] = { "", "X", "H(", "H(1,", "H(1,1", "H()", "H(1,1))", "H(-1,1)", "H(0,1)",
                          "H(1\"sem fim)", "H(nan,1)" };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        err = NULL;
        wz_node *n = wz_parse(bad[i], &err);
        CHECK(n == NULL && err != NULL, "deveria recusar '%s'", bad[i]);
        wz_free(n);
    }
    /* Profundidade absurda é recusada sem estourar a pilha. */
    char deep[400];
    int p = 0;
    for (int i = 0; i < 60; i++)
        p += sprintf(deep + p, "H(1,");
    deep[p++] = '1';
    for (int i = 0; i < 60; i++)
        deep[p++] = ')';
    deep[p] = 0;
    CHECK(wz_parse(deep, &err) == NULL, "profundidade limitada");
    /* Nó com um filho é colapsado. */
    wz_node *one = wz_parse("H(V(1,1))", &err);
    char *s = wz_serialize(one);
    CHECK(!strcmp(s, "V(1,1)"), "colapsa nó de um filho: %s", s);
    free(s);
    wz_free(one);
}

static void
check_partition(const wz_node *t, wz_rect area, int gi, int go, const char *what)
{
    wz_rect z[512];
    int n = wz_compute(t, area, gi, go, z, 512);
    CHECK(n == wz_leaf_count(t), "%s: número de zonas", what);
    if (n > 512)
        return;
    /* Mais zonas que pixels (célula de largura 0) é degenerado: o layout
     * aleatório não serve para testar sobreposição. */
    wz_rect raw[512];
    wz_compute(t, area, 0, 0, raw, 512);
    for (int i = 0; i < n; i++)
        if (raw[i].w <= 1 || raw[i].h <= 1)
            return;
    long sum = 0;
    for (int i = 0; i < n; i++) {
        CHECK(z[i].w >= 1 && z[i].h >= 1, "%s: área positiva", what);
        CHECK(inside(z[i], area), "%s: zona %d dentro da área (%d,%d %dx%d)", what, i, z[i].x, z[i].y, z[i].w, z[i].h);
        sum += (long)z[i].w * z[i].h;
        for (int j = i + 1; j < n; j++)
            CHECK(!wz_rect_intersects(z[i], z[j]), "%s: zonas %d e %d não se sobrepõem", what, i, j);
    }
    if (gi == 0 && go == 0) {
        /* Sem gaps a cobertura é exata (se nenhuma zona foi forçada a 1px). */
        CHECK(sum == (long)area.w * area.h, "%s: cobertura exata (%ld de %ld)", what, sum, (long)area.w * area.h);
    }
    /* Estável: calcular de novo dá o mesmo. */
    wz_rect z2[512];
    wz_compute(t, area, gi, go, z2, 512);
    CHECK(!memcmp(z, z2, sizeof(wz_rect) * (size_t)n), "%s: determinístico", what);
}

static void
test_gaps_exact(void)
{
    const char *err;
    wz_node *t = wz_parse("H(1,1,1)", &err);
    wz_rect area = { 100, 50, 1000, 600 };
    for (int g = 0; g <= 13; g++) {
        wz_rect z[3];
        wz_compute(t, area, g, 4, z, 3);
        CHECK(z[0].x == 104 && z[0].y == 54, "margem externa (g=%d)", g);
        CHECK(z[1].x - (z[0].x + z[0].w) == g, "gap interno exato entre 0 e 1 (g=%d): %d", g, z[1].x - (z[0].x + z[0].w));
        CHECK(z[2].x - (z[1].x + z[1].w) == g, "gap interno exato entre 1 e 2 (g=%d)", g);
        CHECK(z[2].x + z[2].w == 1096, "última zona encosta na margem (g=%d)", g);
        /* Mesmo peso, mesmo tamanho: a do meio não perde gap a mais. */
        int mn = z[0].w, mx = z[0].w;
        for (int i = 1; i < 3; i++) {
            mn = z[i].w < mn ? z[i].w : mn;
            mx = z[i].w > mx ? z[i].w : mx;
        }
        CHECK(mx - mn <= 1, "zonas iguais com gap (g=%d): %d..%d", g, mn, mx);
    }
    wz_free(t);
    /* Pesos valem para o tamanho da zona, não da célula com o gap. */
    t = wz_parse("H(2,1)", &err);
    wz_rect h[2];
    wz_compute(t, (wz_rect){ 0, 0, 1206, 600 }, 6, 0, h, 2);
    CHECK(h[0].w == 800 && h[1].w == 400 && h[1].x == 806, "H(2,1) com gap 6: %d e %d", h[0].w, h[1].w);
    wz_free(t);
    /* Terços em 3440 com gap 6 (ultrawide): 1139/1138/1139. */
    t = wz_parse("H(1,1,1)", &err);
    wz_rect u[3];
    wz_compute(t, (wz_rect){ 0, 0, 3440, 1440 }, 6, 6, u, 3);
    CHECK(u[0].w + u[1].w + u[2].w == 3440 - 12 - 12, "terços 3440: soma");
    CHECK(abs(u[0].w - u[1].w) <= 1 && abs(u[1].w - u[2].w) <= 1, "terços 3440: %d %d %d", u[0].w, u[1].w, u[2].w);
    wz_free(t);
    /* Margem maior que a área é ignorada em vez de produzir lixo. */
    t = wz_parse("Z", &err);
    wz_rect z;
    wz_compute(t, (wz_rect){ 0, 0, 10, 10 }, 0, 50, &z, 1);
    CHECK(z.w == 10 && z.h == 10, "margem absurda ignorada");
    wz_free(t);
}

static void
test_random_layouts(void)
{
    for (int iter = 0; iter < 400; iter++) {
        wz_node *t = random_tree(4);
        wz_rect area = { (int)rnd(3000) - 1000, (int)rnd(2000), 320 + (int)rnd(7000), 240 + (int)rnd(3000) };
        int gi = rnd(4) ? (int)rnd(20) : 0, go = rnd(4) ? (int)rnd(20) : 0;
        char what[64];
        snprintf(what, sizeof what, "aleatório #%d", iter);
        check_partition(t, area, gi, go, what);
        check_partition(t, area, 0, 0, what);
        char *s = wz_serialize(t);
        const char *err;
        wz_node *m = wz_parse(s, &err);
        CHECK(m != NULL, "%s: reparse '%s' (%s)", what, s, m ? "" : err);
        if (m) {
            /* Mesmo cálculo depois da ida e volta (pesos com 4 dígitos). */
            wz_rect a[512], b[512];
            int n = wz_compute(t, area, gi, go, a, 512);
            int k = wz_compute(m, area, gi, go, b, 512);
            CHECK(n == k, "%s: mesmas zonas após reparse", what);
            int maxdiff = 0;
            for (int i = 0; i < n && i < 512; i++) {
                int d = abs(a[i].x - b[i].x) + abs(a[i].w - b[i].w) + abs(a[i].y - b[i].y) + abs(a[i].h - b[i].h);
                if (d > maxdiff)
                    maxdiff = d;
            }
            CHECK(maxdiff <= 4, "%s: geometria preservada no reparse (dif %d)", what, maxdiff);
        }
        free(s);
        wz_free(m);
        wz_free(t);
    }
}

static void
test_builtin_layouts(void)
{
    wz_config c;
    wz_config_defaults(&c);
    wz_rect screens[] = { { 0, 0, 1366, 768 }, { 0, 0, 1920, 1080 }, { 0, 0, 2560, 1080 },
                          { 0, 0, 3440, 1440 }, { 1920, 0, 1080, 1920 }, { 0, 0, 800, 600 } };
    for (int i = 0; i < c.nlayouts; i++) {
        const char *err;
        wz_node *t = wz_parse(c.layouts[i].tree, &err);
        CHECK(t != NULL, "layout interno '%s' válido", c.layouts[i].id);
        CHECK(wz_builtin_label(c.layouts[i].id) != NULL, "layout '%s' tem nome", c.layouts[i].id);
        for (size_t s = 0; t && s < sizeof screens / sizeof *screens; s++) {
            char what[96];
            snprintf(what, sizeof what, "%s em %dx%d", c.layouts[i].id, screens[s].w, screens[s].h);
            check_partition(t, screens[s], c.gap_in, c.gap_out, what);
            check_partition(t, screens[s], 0, 0, what);
        }
        wz_free(t);
    }
    wz_config_free(&c);
}

static void
test_targets(void)
{
    const char *err;
    wz_node *t = wz_parse("V(H(1,1),H(1,1))", &err);
    wz_rect z[4];
    wz_compute(t, (wz_rect){ 0, 0, 1000, 800 }, 10, 0, z, 4);
    wz_rect r;
    int b;
    CHECK(wz_target(z, 4, 100, 100, 24, &r, &b) == 0 && b == -1, "ponto no meio da zona 0");
    CHECK(wz_target(z, 4, 490, 100, 24, &r, &b) == 0 && b == 1, "perto da fronteira 0|1 abrange as duas");
    CHECK(r.x == z[0].x && r.x + r.w == z[1].x + z[1].w, "união 0+1 correta");
    CHECK(wz_target(z, 4, 100, 400, 24, &r, &b) >= 0 && (b == 2 || b == 0), "fronteira 0/2 abrange");
    CHECK(wz_target(z, 4, 700, 700, 0, &r, &b) == 3 && b == -1, "span 0 desliga a abrangência");
    /* No gap entre zonas: a mais próxima. */
    int g = wz_zone_at(z, 4, 500, 100);
    CHECK(g == 0 || g == 1, "ponto no gap vai para uma vizinha");
    CHECK(wz_zone_at(z, 4, -50, -50) == 0, "fora da área: a mais próxima");
    CHECK(wz_neighbor(z, 4, 0, WZ_RIGHT) == 1, "vizinha à direita");
    CHECK(wz_neighbor(z, 4, 0, WZ_DOWN) == 2, "vizinha embaixo");
    CHECK(wz_neighbor(z, 4, 0, WZ_LEFT) == -1, "sem vizinha à esquerda");
    CHECK(wz_neighbor(z, 4, 3, WZ_UP) == 1, "vizinha em cima");
    wz_free(t);

    t = wz_parse("H(3:V(3,1),2)", &err);
    wz_rect d[3];
    wz_compute(t, (wz_rect){ 0, 0, 1920, 1080 }, 0, 0, d, 3);
    CHECK(wz_neighbor(d, 3, 1, WZ_RIGHT) == 2, "terminal -> navegador");
    CHECK(wz_neighbor(d, 3, 2, WZ_LEFT) == 0, "navegador -> editor (faixa maior)");
    wz_free(t);
}

static void
test_divider(void)
{
    const char *err;
    wz_node *t = wz_parse("H(1,1,1)", &err);
    wz_set_divider(t, 0, 0.5, 0.05);
    wz_rect z[3];
    wz_compute(t, (wz_rect){ 0, 0, 900, 100 }, 0, 0, z, 3);
    CHECK(z[0].w == 450, "divisória em 50%% (%d)", z[0].w);
    CHECK(z[2].w == 300, "terceira zona intacta");
    wz_set_divider(t, 0, 0.99, 0.05);
    wz_compute(t, (wz_rect){ 0, 0, 900, 100 }, 0, 0, z, 3);
    CHECK(z[1].w >= 45 - 1, "mínimo respeitado (%d)", z[1].w);
    wz_free(t);
}

static void
test_editor_ops(void)
{
    wz_node *root = wz_leaf_new(1);
    wz_node *b = wz_split_leaf(root, WZ_HSPLIT, 0);
    CHECK(wz_leaf_count(root) == 2, "dividir folha");
    wz_node *c = wz_split_leaf(b, WZ_VSPLIT, 0);
    CHECK(wz_leaf_count(root) == 3, "dividir de novo");
    char *s = wz_serialize(root);
    CHECK(!strcmp(s, "H(1,V(1,1))"), "forma esperada: %s", s);
    free(s);
    root = wz_remove_leaf(root, c);
    s = wz_serialize(root);
    CHECK(!strcmp(s, "H(1,1)"), "remover colapsa: %s", s);
    free(s);
    root = wz_remove_leaf(root, wz_leaf_nth(root, 0));
    CHECK(root && root->kind == WZ_LEAF, "sobra uma folha");
    root = wz_remove_leaf(root, root);
    CHECK(root == NULL, "árvore vazia");
}

static void
test_tiling(void)
{
    wz_rect area = { 0, 32, 1920, 1048 };
    wz_rect out[64];
    for (int a = 0; a < WZ_TILE_N; a++) {
        if (a == WZ_TILE_BSP)
            continue;
        for (int pos = 0; pos < 4; pos++) {
            for (int n = 1; n <= 50; n++) {
                wz_tile_params p = { .algo = a, .master_ratio = 0.6, .nmaster = 1 + (n % 3), .master_pos = pos,
                                     .gap_in = 8, .gap_out = 8, .smart_gaps = 1 };
                memset(out, 0, sizeof out);
                int k = wz_tile(&p, area, n, out, 64);
                CHECK(k == n, "%s n=%d", wz_algo_name(a), n);
                for (int i = 0; i < n; i++) {
                    CHECK(out[i].w > 0 && out[i].h > 0, "%s n=%d janela %d com área", wz_algo_name(a), n, i);
                    CHECK(inside(out[i], area), "%s n=%d janela %d dentro", wz_algo_name(a), n, i);
                    if (a != WZ_TILE_MONOCLE)
                        for (int j = i + 1; j < n; j++)
                            CHECK(!wz_rect_intersects(out[i], out[j]), "%s pos=%d n=%d janelas %d/%d sem sobreposição",
                                  wz_algo_name(a), pos, n, i, j);
                }
                if (n == 1)
                    CHECK(out[0].x == area.x && out[0].w == area.w, "%s: smart gaps com uma janela", wz_algo_name(a));
            }
        }
    }
    /* Principal à esquerda com 60%. */
    wz_tile_params p = { .algo = WZ_TILE_MASTER, .master_ratio = 0.6, .nmaster = 1, .gap_in = 0, .gap_out = 0 };
    wz_tile(&p, (wz_rect){ 0, 0, 1000, 600 }, 3, out, 64);
    CHECK(out[0].x == 0 && out[0].w == 600 && out[0].h == 600, "principal 60%%");
    CHECK(out[1].x == 600 && out[2].y == 300, "pilha à direita");
    p.master_pos = WZ_POS_RIGHT;
    wz_tile(&p, (wz_rect){ 0, 0, 1000, 600 }, 3, out, 64);
    CHECK(out[0].x == 400 && out[0].w == 600, "principal à direita (%d)", out[0].x);
}

static void
test_bsp(void)
{
    wz_bsp b = { 0 };
    wz_rect area = { 0, 0, 1600, 900 };
    for (long id = 10; id < 40; id++)
        wz_bsp_insert(&b, id, id - 1, area);
    CHECK(wz_leaf_count(b.root) == 30, "30 janelas no BSP");
    wz_bsp_insert(&b, 15, 12, area);
    CHECK(wz_leaf_count(b.root) == 30, "inserir de novo não duplica");
    wz_rect out[64];
    long ids[64];
    int n = wz_bsp_compute(&b, area, 4, 4, out, ids, 64);
    CHECK(n == 30, "compute");
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            CHECK(!wz_rect_intersects(out[i], out[j]), "BSP sem sobreposição %d/%d", i, j);
    wz_bsp_swap(&b, 10, 39);
    wz_bsp_compute(&b, area, 0, 0, out, ids, 64);
    CHECK(ids[0] == 39, "troca de posição");
    /* Redimensionar a primeira para a direita aumenta a largura dela. */
    wz_bsp_compute(&b, area, 0, 0, out, ids, 64);
    int w0 = out[0].w;
    wz_bsp_resize(&b, 39, WZ_RIGHT, 0.1);
    wz_bsp_compute(&b, area, 0, 0, out, ids, 64);
    CHECK(out[0].w > w0, "divisória ajustada (%d > %d)", out[0].w, w0);
    for (long id = 10; id < 40; id++)
        wz_bsp_remove(&b, id);
    CHECK(b.root == NULL, "BSP vazio depois de remover tudo");
    wz_bsp_clear(&b);
}

static void
test_fit(void)
{
    wz_rect r = { 0, 0, 800, 600 };
    wz_limits l = { .min_w = 900 };
    CHECK(wz_fit(&r, &l) == WZ_FIT_CONFLICT && r.w == 800, "mínimo maior que a zona: conflito");
    l = (wz_limits){ .max_w = 400, .max_h = 300 };
    CHECK(wz_fit(&r, &l) == WZ_FIT_SHRUNK && r.w == 400 && r.x == 200 && r.y == 150, "máximo: centraliza");
    l = (wz_limits){ 0 };
    r = (wz_rect){ 0, 0, 800, 600 };
    CHECK(wz_fit(&r, &l) == WZ_FIT_OK, "sem limites");
}

static void
test_ini(void)
{
    char dir[] = "/tmp/wz-test-XXXXXX";
    if (!mkdtemp(dir)) {
        CHECK(0, "mkdtemp");
        return;
    }
    char path[256];
    snprintf(path, sizeof path, "%s/sub/wmake-zones.conf", dir);
    CHECK(wz_ini_set(path, "General", "Zones", "true") == 0, "cria arquivo e diretório");
    CHECK(wz_ini_set(path, "General", "Layout", "quarters") == 0, "segunda chave");
    CHECK(wz_ini_set(path, "Layout Meu", "Tree", "H(2,1)") == 0, "seção nova");
    CHECK(wz_ini_set(path, "Layout Meu", "Name", "Meu layout \\ com barra") == 0, "valor com barra");
    FILE *f = fopen(path, "a");
    fputs("# comentário do usuário\n[Rule obs]\nClass=obs\nAction=float\n", f);
    fclose(f);
    CHECK(wz_ini_set(path, "General", "Zones", "false") == 0, "troca valor");
    CHECK(wz_ini_set(path, "General", "Layout", NULL) == 0, "apaga chave");
    wz_config c;
    wz_config_defaults(&c);
    CHECK(wz_config_load(&c, path) == 0, "carrega");
    CHECK(c.zones == 0, "Zones=false");
    CHECK(!strcmp(c.layout, "halves"), "Layout apagado volta ao padrão");
    const wz_layout_def *l = wz_config_layout(&c, "Meu");
    CHECK(l && !strcmp(l->tree, "H(2,1)") && l->label && !strcmp(l->label, "Meu layout \\ com barra"),
          "layout do usuário com nome");
    const wz_rule *r = wz_config_match_rule(&c, "OBS", "obs", "OBS 30");
    CHECK(r && r->action == WZ_RULE_FLOAT, "regra por classe, sem diferenciar maiúsculas");
    CHECK(wz_config_match_rule(&c, "firefox", "Navigator", "x") == NULL, "sem regra para outro app");
    wz_config_free(&c);
    /* O comentário sobreviveu. */
    f = fopen(path, "r");
    char buf[2048] = "";
    size_t rd = fread(buf, 1, sizeof buf - 1, f);
    buf[rd] = 0;
    fclose(f);
    CHECK(strstr(buf, "# comentário do usuário") != NULL, "comentários preservados");
    CHECK(strstr(buf, "Zones=false") != NULL && !strstr(buf, "Zones=true"), "valor trocado no lugar");

    /* Escopos por monitor e área de trabalho. */
    wz_ini_set(path, "Monitor HDMI-1", "Layout", "thirds");
    wz_ini_set(path, "Monitor HDMI-1 Workspace 2", "Layout", "grid-3x3");
    wz_ini_set(path, "Workspace 3", "Autotile", "true");
    wz_config_defaults(&c);
    wz_config_load(&c, path);
    CHECK(!strcmp(wz_config_layout_for(&c, "HDMI-1", 1)->id, "thirds"), "layout do monitor");
    CHECK(!strcmp(wz_config_layout_for(&c, "HDMI-1", 2)->id, "grid-3x3"), "layout do monitor+área");
    CHECK(!strcmp(wz_config_layout_for(&c, "eDP-1", 2)->id, "halves"), "outro monitor: geral");
    CHECK(wz_config_autotile_for(&c, "eDP-1", 3) == 1 && wz_config_autotile_for(&c, "eDP-1", 1) == 0,
          "Auto Tiling por área de trabalho");
    wz_config_free(&c);

    /* Entradas inválidas não derrubam a leitura. */
    f = fopen(path, "w");
    fputs("[General]\nGap=abc\nLayout=nao-existe\n[Layout X]\nTree=H(\n[Tiling]\nMasterRatio=7\n", f);
    fclose(f);
    wz_config_defaults(&c);
    wz_config_load(&c, path);
    CHECK(c.gap_in == 6, "número inválido mantém o padrão");
    CHECK(!strcmp(c.layout, "halves"), "layout inexistente volta ao padrão");
    CHECK(!wz_config_layout(&c, "X"), "árvore inválida descartada");
    CHECK(c.tile.master_ratio <= 0.9, "proporção limitada");
    wz_config_free(&c);

    /* Espaços nas pontas, tab e CR sobrevivem à ida e volta. */
    wz_ini_set(path, "Rule t", "Title", "  dois espaços\tno meio e no fim  ");
    wz_ini_set(path, "Rule t", "Class", "a\r\nb");
    struct rt { const char *title, *klass; } got = { 0 };
    wz_config_defaults(&c);
    wz_config_load(&c, path);
    for (int i = 0; i < c.nrules; i++)
        if (!strcmp(c.rules[i].id, "t")) {
            got.title = c.rules[i].title;
            got.klass = c.rules[i].wm_class;
        }
    CHECK(got.title && !strcmp(got.title, "  dois espaços\tno meio e no fim  "), "espaços e tab preservados: [%s]",
          got.title ? got.title : "(nada)");
    CHECK(got.klass && !strcmp(got.klass, "a\r\nb"), "CR e LF preservados");
    wz_config_free(&c);

    /* Arquivo com CRLF (editado no Windows) é lido. */
    f = fopen(path, "w");
    fputs("[General]\r\nZones=true\r\nGap=10\r\n", f);
    fclose(f);
    wz_config_defaults(&c);
    wz_config_load(&c, path);
    CHECK(c.zones == 1 && c.gap_in == 10, "CRLF");
    wz_config_free(&c);

    /* Link simbólico (dotfiles) continua link; o alvo é que muda. */
    char real[300], link[320];
    snprintf(real, sizeof real, "%s/real.conf", dir);
    snprintf(link, sizeof link, "%s/link.conf", dir);
    f = fopen(real, "w");
    fputs("[General]\nZones=false\n", f);
    fclose(f);
    CHECK(symlink(real, link) == 0, "symlink");
    CHECK(wz_ini_set(link, "General", "Zones", "true") == 0, "grava pelo link");
    struct stat st;
    CHECK(lstat(link, &st) == 0 && S_ISLNK(st.st_mode), "o link não foi trocado por arquivo");
    wz_config_defaults(&c);
    wz_config_load(&c, real);
    CHECK(c.zones == 1, "o alvo recebeu a mudança");
    wz_config_free(&c);
    unlink(link);
    unlink(real);
    snprintf(link, sizeof link, "%s.lock", real);
    unlink(link);

    unlink(path);
    snprintf(link, sizeof link, "%s.lock", path);
    unlink(link);
    snprintf(path, sizeof path, "%s/sub", dir);
    rmdir(path);
    rmdir(dir);
}

static void
test_locale(void)
{
    /* Com vírgula decimal (pt_BR) o layout continua o mesmo. */
    if (!setlocale(LC_NUMERIC, "pt_BR.UTF-8") && !setlocale(LC_NUMERIC, "de_DE.UTF-8")) {
        printf("(sem locale com vírgula decimal; teste de locale pulado)\n");
        return;
    }
    const char *err;
    wz_node *t = wz_parse("V(H(1,1),H(1,1))", &err);
    CHECK(t && wz_leaf_count(t) == 4, "quadrantes com locale de vírgula");
    wz_node *u = wz_parse("H(0.6,0.4)", &err);
    char *s = u ? wz_serialize(u) : NULL;
    CHECK(s && !strcmp(s, "H(0.6,0.4)"), "serializa com ponto: %s", s ? s : "(null)");
    free(s);
    wz_free(u);
    wz_free(t);
    char buf[32];
    wz_fmt_double(buf, sizeof buf, 0.33333);
    CHECK(!strcmp(buf, "0.3333"), "formato 0.3333: %s", buf);
    wz_fmt_double(buf, sizeof buf, 2.0);
    CHECK(!strcmp(buf, "2"), "formato 2: %s", buf);
    setlocale(LC_NUMERIC, "C");
}

/* Foco espacial: a vizinha natural, sem saltos para janelas distantes. */
static void
test_focus(void)
{
    /* Mosaico principal + pilha: principal à esquerda, duas à direita. */
    wz_rect main = { 0, 0, 960, 1080 };
    wz_rect c1[] = { { 960, 0, 960, 540 }, { 960, 540, 960, 540 } };
    CHECK(wz_focus_pick(c1, 2, main, WZ_RIGHT) == 0, "direita: a de cima (empate, ordem)");
    CHECK(wz_focus_pick(c1, 2, main, WZ_LEFT) < 0, "esquerda: nada");
    wz_rect c2[] = { { 0, 0, 960, 1080 }, { 960, 0, 960, 540 } };
    CHECK(wz_focus_pick(c2, 2, c1[1], WZ_UP) == 1, "para cima: a de cima da pilha");
    CHECK(wz_focus_pick(c2, 2, c1[1], WZ_LEFT) == 0, "esquerda: a principal");
    /* Uma logo ao lado, um pouco desalinhada, vence uma alinhada longe. */
    wz_rect from = { 100, 100, 400, 300 };
    wz_rect c3[] = { { 3000, 100, 400, 300 }, { 560, 450, 400, 300 } };
    CHECK(wz_focus_pick(c3, 2, from, WZ_RIGHT) == 1, "perto vence alinhada longe");
    /* Sobrepostas: o centro decide a direção. */
    wz_rect c4[] = { { 300, 120, 400, 300 } };
    CHECK(wz_focus_pick(c4, 1, from, WZ_RIGHT) == 0, "sobreposta à direita");
    CHECK(wz_focus_pick(c4, 1, from, WZ_LEFT) < 0, "sobreposta não está à esquerda");
    /* Mesmo centro (maximizada atrás): nunca é escolhida. */
    wz_rect c5[] = { { 0, 0, 600, 500 } };
    wz_rect mid = { 100, 100, 400, 300 };
    for (int d = WZ_LEFT; d <= WZ_DOWN; d++)
        CHECK(wz_focus_pick(c5, 1, mid, d) < 0, "mesmo centro, direção %d", d);
    /* Outro monitor à direita: alcançável (o chamador filtra se não quer). */
    wz_rect c6[] = { { 1920, 0, 1920, 1080 } };
    CHECK(wz_focus_pick(c6, 1, (wz_rect){ 960, 0, 960, 1080 }, WZ_RIGHT) == 0, "monitor ao lado");
    /* Determinismo: entradas aleatórias, a escolha está na direção certa. */
    for (int k = 0; k < 20000; k++) {
        wz_rect f = { (int)rnd(3000), (int)rnd(2000), 50 + (int)rnd(900), 50 + (int)rnd(900) };
        wz_rect c[8];
        int n = 1 + (int)rnd(8);
        for (int i = 0; i < n; i++)
            c[i] = (wz_rect){ (int)rnd(3000), (int)rnd(2000), 50 + (int)rnd(900), 50 + (int)rnd(900) };
        int d = (int)rnd(4);
        int a = wz_focus_pick(c, n, f, d), b = wz_focus_pick(c, n, f, d);
        CHECK(a == b, "determinístico");
        if (a >= 0) {
            long fc = d < 2 ? f.x * 2L + f.w : f.y * 2L + f.h;
            long cc = d < 2 ? c[a].x * 2L + c[a].w : c[a].y * 2L + c[a].h;
            CHECK(d == WZ_LEFT || d == WZ_UP ? cc < fc : cc > fc, "direção certa");
        }
    }
}

/* Histórico: desfazer, refazer, limite de memória e janelas fechadas. */
static void
test_history(void)
{
    wz_history h;
    wz_history_init(&h, 3);
    for (int i = 0; i < 5; i++) {
        wz_hist_entry *e = wz_history_begin(&h);
        wz_hist_add(e, &(wz_hist_item){ .win = 10 + (uintptr_t)i, .r = { i, i, 10, 10 }, .zone = i });
        wz_history_commit(&h);
    }
    CHECK(h.nundo == 3, "profundidade 3: %d passos", h.nundo);
    wz_hist_entry e;
    CHECK(wz_history_pop(&h, 0, &e) && e.n == 1 && e.items[0].win == 14, "último primeiro");
    wz_hist_entry *inv = wz_history_push_inverse(&h, 1);
    wz_hist_add(inv, &(wz_hist_item){ .win = 14, .r = { 99, 99, 10, 10 } });
    wz_history_commit(&h);
    wz_hist_entry_free(&e);
    CHECK(h.nredo == 1 && h.nundo == 2, "refazer guardado");
    /* Um passo novo esvazia o refazer. */
    wz_hist_entry *n = wz_history_begin(&h);
    wz_hist_add(n, &(wz_hist_item){ .win = 20 });
    wz_hist_add(n, &(wz_hist_item){ .win = 20, .zone = 7 });
    CHECK(n->n == 1 && n->items[0].zone == 0, "janela repetida: vale a primeira");
    CHECK(h.nredo == 0, "passo novo esvazia o refazer");
    wz_history_commit(&h);
    /* Passo vazio some. */
    wz_history_begin(&h);
    wz_history_commit(&h);
    CHECK(h.nundo == 3, "passo vazio descartado: %d", h.nundo);
    /* Janela fechada: some de todos os passos; passos vazios somem. */
    wz_history_forget(&h, 12);
    wz_history_forget(&h, 13);
    CHECK(h.nundo == 1, "janelas fechadas esquecidas: %d", h.nundo);
    /* Passo só de configuração não some ao esquecer janelas. */
    wz_hist_entry *c = wz_history_begin(&h);
    wz_hist_set_cfg(c, "General", "Layout", "thirds");
    wz_history_forget(&h, 20);
    CHECK(h.nundo == 1 && h.undo[0].has_cfg && !strcmp(h.undo[0].value, "thirds"), "configuração mantida");
    /* Profundidade menor descarta os mais antigos; maior não perde nada. */
    wz_history_commit(&h);
    for (int i = 0; i < 3; i++) {
        wz_hist_add(wz_history_begin(&h), &(wz_hist_item){ .win = 100 + (uintptr_t)i });
        wz_history_commit(&h);
    }
    wz_history_set_depth(&h, 2);
    CHECK(h.nundo == 2 && h.undo[1].items[0].win == 102, "profundidade reduzida");
    wz_history_set_depth(&h, 50);
    CHECK(h.nundo == 2 && h.depth == 50, "profundidade aumentada");
    for (int i = 0; i < 200; i++) {
        wz_hist_entry *x = wz_history_begin(&h);
        for (int j = 0; j < 200; j++)
            wz_hist_add(x, &(wz_hist_item){ .win = (uintptr_t)j });
        CHECK(x->n == WZ_HISTORY_ITEMS, "limite de janelas por passo");
        wz_history_commit(&h);
    }
    CHECK(h.nundo == 50, "nunca passa da profundidade");
    wz_history_free(&h);
}

/* Padrões da 0.3, modos e layout escolhido pelo monitor. */
static void
test_modes(void)
{
    char dir[] = "/tmp/wz-modes-XXXXXX";
    CHECK(mkdtemp(dir) != NULL, "mkdtemp");
    char path[256];
    snprintf(path, sizeof path, "%s/wmake-zones.conf", dir);
    wz_config c;
    wz_config_defaults(&c);
    CHECK(c.zones == 1 && c.autotile == 0, "padrão: zonas ligadas, mosaico desligado");
    CHECK(wz_config_mode(&c) == WZ_MODE_ZONES, "modo padrão: zonas");
    CHECK(!strcmp(wz_config_layout_for_size(&c, "DP-1", 1, 3440, 1440)->id, "ultrawide"), "21:9 -> ultrawide");
    CHECK(!strcmp(wz_config_layout_for_size(&c, "DP-1", 1, 2560, 1080)->id, "ultrawide"), "2560x1080 -> ultrawide");
    CHECK(!strcmp(wz_config_layout_for_size(&c, "DP-1", 1, 1080, 1920)->id, "portrait"), "em pé -> portrait");
    CHECK(!strcmp(wz_config_layout_for_size(&c, "DP-1", 1, 1920, 1080)->id, "halves"), "16:9 -> metades");
    CHECK(!strcmp(wz_config_layout_for_size(&c, "DP-1", 1, 1280, 1024)->id, "halves"), "5:4 -> metades");
    /* Clique esquerdo: Zonas <-> Mosaico; meio: desliga e religa o último. */
    CHECK(wz_config_set_mode(&c, path, "toggle") == WZ_MODE_TILING && c.zones && c.autotile, "zonas -> mosaico");
    CHECK(wz_config_set_mode(&c, path, "enable") == WZ_MODE_OFF && !c.zones && !c.autotile, "desliga");
    CHECK(wz_config_set_mode(&c, path, "enable") == WZ_MODE_TILING, "religa no último (mosaico)");
    CHECK(wz_config_set_mode(&c, path, "toggle") == WZ_MODE_ZONES, "mosaico -> zonas");
    CHECK(wz_config_set_mode(&c, path, "off") == WZ_MODE_OFF, "off");
    CHECK(wz_config_set_mode(&c, path, "toggle") == WZ_MODE_ZONES, "toggle desligado liga o último (zonas)");
    CHECK(wz_config_set_mode(&c, path, "xyz") == -1, "modo desconhecido");
    wz_config_set_mode(&c, path, "off");
    wz_config_free(&c);
    /* O que foi gravado volta igual; quem desligou continua desligado. */
    wz_config_defaults(&c);
    wz_config_load(&c, path);
    CHECK(wz_config_mode(&c) == WZ_MODE_OFF && c.last_mode == WZ_MODE_ZONES, "desligado persiste");
    wz_config_free(&c);
    /* Layout escolhido pelo usuário vence o automático. */
    wz_ini_set(path, "General", "Layout", "thirds");
    wz_ini_set(path, "General", "DragLayouts", "thirds, halves,nada,thirds");
    wz_ini_set(path, "Layout meu", "Tree", "H(1,1)");
    wz_ini_set(path, "Dockapp", "LeftClick", "layouts");
    wz_ini_set(path, "General", "DragFlow", "layouts-modifier");
    wz_config_defaults(&c);
    wz_config_load(&c, path);
    CHECK(!strcmp(wz_config_layout_for_size(&c, "DP-1", 1, 3440, 1440)->id, "thirds"), "layout gravado vence");
    const wz_layout_def *dl[16];
    int n = wz_config_drag_layouts(&c, dl, 16);
    CHECK(n == 3 && !strcmp(dl[0]->id, "thirds") && !strcmp(dl[1]->id, "halves") && !strcmp(dl[2]->id, "meu"),
          "painel: lista sem repetidos e o layout do usuário (%d)", n);
    CHECK(c.click[0] == WZ_CLICK_LAYOUTS && c.click[1] == WZ_CLICK_ENABLE && c.click[3] == WZ_CLICK_CYCLE,
          "cliques do dockapp");
    CHECK(c.drag_flow == WZ_FLOW_LAYOUTS_MODIFIER, "fluxo do arrasto");
    wz_config_free(&c);
    unlink(path);
    char lock[300];
    snprintf(lock, sizeof lock, "%s.lock", path);
    unlink(lock);
    rmdir(dir);
}

int
main(void)
{
    test_focus();
    test_history();
    test_modes();
    test_locale();
    test_parse();
    test_gaps_exact();
    test_random_layouts();
    test_builtin_layouts();
    test_targets();
    test_divider();
    test_editor_ops();
    test_tiling();
    test_bsp();
    test_fit();
    test_ini();
    printf("%d verificações, %d falhas\n", checks, failures);
    return failures ? 1 : 0;
}
