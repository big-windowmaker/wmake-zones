/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * wmake-zones-wl: o ladrilho do wmake-zones no Clip/Dock do wlmaker, como
 * cliente Wayland do protocolo de ícones do wlmaker (sem XWayland).
 *
 *   clique esquerdo   próxima opção do modo       ([Dockapp] LeftClick)
 *   clique do meio    desliga / religa o último   ([Dockapp] MiddleClick)
 *   roda              layout anterior / seguinte, com uma prévia breve das
 *                     zonas no monitor                ([Dockapp] Wheel)
 *   clique direito    menu do wlmaker (ações do Dockapps.plist)
 *
 * O desenho é o mesmo do dockapp do X11: a miniatura do layout do monitor
 * e as teclas Z (zonas) e M (mosaico) com LED. O estado vem do arquivo de
 * configuração, vigiado por inotify (o wlmaker grava nele a cada troca), e
 * o layout em efeito, do status do wlmaker (o automático depende do
 * monitor): parado, o processo dorme em poll() sem timeout.
 */
#define _GNU_SOURCE
#include "wz-config.h"
#include "wz-i18n.h"
#include "wz-ipc.h"
#include "wz-layout.h"
#include "wz-theme.h"
#include "wz-tile.h"

#include <cairo.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wayland-client.h>

#include "wlmaker-icon-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

typedef struct {
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct zwlmaker_icon_manager_v1 *icon_manager;
    struct wl_surface *surface;
    struct zwlmaker_toplevel_icon_v1 *icon;
    int width, height;
    int configured;
    int pointer_inside;
    double px, py;      /* ponteiro no ladrilho, em coordenadas de 64x64 */
    wz_config cfg;
    char *cfg_path;
    wz_theme theme;
    char layout[128];   /* layout em efeito no primeiro monitor (status) */
    /* Rolagem: faixa do primeiro monitor (status: strip= e widths=). */
    int ncol, active, nleft, nright;
    int widths[WZ_SCROLL_MAX_COLS];
    int quit;
} app;

static void
rgb(cairo_t *cr, unsigned c)
{
    cairo_set_source_rgb(cr, (c >> 16 & 0xff) / 255.0, (c >> 8 & 0xff) / 255.0, (c & 0xff) / 255.0);
}

/* Visual dos outros dockapps do Dock (o wmpulsemixer), como no X11: o
 * ladrilho do tema em volta (4 px, por onde se arrasta), a tela afundada e
 * escura com o LED ciano, as teclas cinza em relevo; a do modo aceso na cor
 * dele (Z verde, M amarelo, R azul-claro). Unidades de 64. */
#define LED_HIGH 0x20b2ae
#define LED_MED 0x188a86
#define LED_LOW 0x004941
#define PANEL 0x282828
#define BEVEL_DARK 0x000000
#define BEVEL_LIGHT 0xaeaaae
#define BTN_FACE 0xaeaaae
#define BTN_HILITE 0xf7f3ff
#define BTN_TEXT 0x000000
#define FKEY_Z 0x6fdc6f
#define FKEY_M 0xf2c84b
#define FKEY_R 0x8cd2f5

/* A tela em 4..59 x 4..43; o poço das teclas em 4..59 x 46..59, com Z, M e
 * R de 18 cada (5..22, 23..40, 41..58), y 47..58. */
#define SCR_Y 4
#define SCR_H 40
#define WELL_Y 46
#define ROW_Y 47
#define ROW_H 12
enum { HIT_NONE, HIT_Z, HIT_M, HIT_R };
static const struct {
    int x, w;
} row_btn[] = { [HIT_Z] = { 5, 18 }, [HIT_M] = { 23, 18 }, [HIT_R] = { 41, 18 } };

static int
row_hit(double x, double y)
{
    if (y < ROW_Y || y >= ROW_Y + ROW_H)
        return HIT_NONE;
    for (int i = HIT_Z; i <= HIT_R; i++)
        if (x >= row_btn[i].x && x < row_btn[i].x + row_btn[i].w)
            return i;
    return HIT_NONE;
}

static void
fill(cairo_t *cr, double x, double y, double w, double h, unsigned c)
{
    rgb(cr, c);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
}

static void
bevel(cairo_t *cr, double x, double y, double w, double h, unsigned tl, unsigned br)
{
    fill(cr, x, y, w - 1, 1, tl);
    fill(cr, x, y, 1, h - 1, tl);
    fill(cr, x + 1, y + h - 1, w - 1, 1, br);
    fill(cr, x + w - 1, y, 1, h, br);
}

/* Caixa com contorno de 1 (tracejado nas zonas). */
static void
box(cairo_t *cr, double x, double y, double w, double h, unsigned f, unsigned line, int dash)
{
    fill(cr, x, y, w, h, f);
    if (w < 3 || h < 3)
        return;
    static const double d[] = { 2, 1 };
    cairo_set_dash(cr, d, dash ? 2 : 0, 0);
    rgb(cr, line);
    cairo_set_line_width(cr, 1);
    cairo_rectangle(cr, x + 0.5, y + 0.5, w - 1, h - 1);
    cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0);
}

/* Fontes de pixels: 3x5 (números das zonas) e 5x7 (letras das teclas),
 * as mesmas do dockapp do X11. */
static const unsigned char digit3x5[10][5] = {
    { 7, 5, 5, 5, 7 }, { 2, 6, 2, 2, 7 }, { 7, 1, 7, 4, 7 }, { 7, 1, 3, 1, 7 }, { 5, 5, 7, 1, 1 },
    { 7, 4, 7, 1, 7 }, { 7, 4, 7, 5, 7 }, { 7, 1, 1, 2, 2 }, { 7, 5, 7, 5, 7 }, { 7, 5, 7, 1, 7 },
};
static const unsigned char key_glyph[][7] = {
    { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f }, /* Z */
    { 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 }, /* M */
    { 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 }, /* R */
};

static void
number(cairo_t *cr, int n, wz_rect b, unsigned c)
{
    char s[8];
    int len = snprintf(s, sizeof s, "%d", n);
    int w = len * 4 - 1;
    if (b.w < w + 3 || b.h < 8)
        return;
    double x = b.x + (b.w - w) / 2, y = b.y + (b.h - 5) / 2;
    rgb(cr, c);
    for (int i = 0; i < len; i++, x += 4)
        for (int r = 0; r < 5; r++)
            for (int k = 0; k < 3; k++)
                if (digit3x5[s[i] - '0'][r] & (4 >> k))
                    cairo_rectangle(cr, x + k, y + r, 1, 1);
    cairo_fill(cr);
}

/* Mosaico: as janelas na disposição do algoritmo, calculada numa área 16:9
 * (o núcleo não divide abaixo de 64 px) e reduzida à tela. */
static void
draw_windows(app *a, cairo_t *cr, wz_rect b)
{
    wz_tile_params p = a->cfg.tile;
    if (p.algo == WZ_TILE_BSP)
        p.algo = WZ_TILE_DWINDLE; /* a árvore é do compositor; ela começa assim */
    wz_rect area = { 0, 0, 1600, 900 };
    p.gap_in = 2 * area.w / b.w;
    p.gap_out = 0;
    p.smart_gaps = 0;
    wz_rect r[8];
    int n = wz_tile(&p, area, 4, r, 8);
    for (int i = n - 1; i >= 0; i--) {
        int x0 = b.x + r[i].x * b.w / area.w, y0 = b.y + r[i].y * b.h / area.h;
        int x1 = b.x + (r[i].x + r[i].w) * b.w / area.w, y1 = b.y + (r[i].y + r[i].h) * b.h / area.h;
        box(cr, x0, y0, x1 - x0, y1 - y0, LED_LOW, LED_HIGH, 0);
    }
}

static void
chevron(cairo_t *cr, int x, int cy, int dir)
{
    rgb(cr, LED_HIGH);
    for (int i = 0; i < 3; i++) {
        int px = dir < 0 ? x + i : x - i;
        cairo_rectangle(cr, px, cy - i, 1, 1);
        cairo_rectangle(cr, px, cy + i, 1, 1);
    }
    cairo_fill(cr);
}

/* Rolagem: as colunas visíveis na proporção das larguras (a ativa acesa), e
 * de cada lado com colunas escondidas um pedaço de coluna apagada com a
 * seta. */
static void
draw_strip(app *a, cairo_t *cr, wz_rect b)
{
    int ncol = a->ncol, active = a->active, nl = a->nleft, nr = a->nright;
    /* Sem faixa: uma de exemplo, três colunas e mais uma de cada lado. */
    static const int ex[] = { 2, 2, 3, 2, 2 };
    const int *w = a->widths;
    if (ncol <= 0) {
        ncol = 5, active = 2, nl = nr = 1;
        w = ex;
    }
    int first = nl, last = ncol - 1 - nr;
    if (last - first + 1 > 5) {
        first = active - 2 < nl ? nl : active - 2;
        last = first + 4 > ncol - 1 - nr ? ncol - 1 - nr : first + 4;
    }
    int stub = 7, gap = 2;
    int x0 = b.x + (nl > 0 ? stub + gap : 0), x1 = b.x + b.w - (nr > 0 ? stub + gap : 0);
    if (nl > 0) {
        box(cr, b.x, b.y + 3, stub, b.h - 6, LED_LOW, LED_MED, 0);
        chevron(cr, b.x + 2, b.y + b.h / 2, -1);
    }
    if (nr > 0) {
        box(cr, b.x + b.w - stub, b.y + 3, stub, b.h - 6, LED_LOW, LED_MED, 0);
        chevron(cr, b.x + b.w - 3, b.y + b.h / 2, 1);
    }
    long tot = 0;
    for (int c = first; c <= last; c++)
        tot += w[c] > 0 ? w[c] : 1;
    int avail = x1 - x0 - gap * (last - first), x = x0;
    for (int c = first; c <= last; c++) {
        int cw = tot > 0 ? (int)((long)avail * (w[c] > 0 ? w[c] : 1) / tot) : 4;
        if (cw < 4)
            cw = 4;
        if (c == active)
            box(cr, x, b.y, cw, b.h, LED_MED, LED_HIGH, 0);
        else
            box(cr, x, b.y + 3, cw, b.h - 6, LED_LOW, LED_MED, 0);
        x += cw + gap;
    }
}

/* Tecla do modo, em relevo: cinza apagada, acesa na cor do modo. */
static void
led_key(cairo_t *cr, int which, int glyph, int lit, unsigned color)
{
    int x = row_btn[which].x, w = row_btn[which].w;
    fill(cr, x, ROW_Y, w, ROW_H, lit ? color : BTN_FACE);
    bevel(cr, x, ROW_Y, w, ROW_H, lit ? wz_rgb_mix(color, 0xffffff, 140) : BTN_HILITE, BEVEL_DARK);
    rgb(cr, BTN_TEXT);
    int x0 = x + (w - 5) / 2, y0 = ROW_Y + (ROW_H - 7) / 2;
    for (int r = 0; r < 7; r++)
        for (int k = 0; k < 5; k++)
            if (key_glyph[glyph][r] & (0x10 >> k))
                cairo_rectangle(cr, x0 + k, y0 + r, 1, 1);
    cairo_fill(cr);
}

static void
draw_face(app *a, cairo_t *cr, int W, int H)
{
    cairo_save(cr);
    cairo_scale(cr, W / 64.0, H / 64.0);
    /* O ladrilho do tema, como os ícones do wlmaker. */
    cairo_pattern_t *pat = cairo_pattern_create_linear(0, 0, 64, 64);
    unsigned c1 = a->theme.icon1, c2 = a->theme.icon2;
    cairo_pattern_add_color_stop_rgb(pat, 0, (c1 >> 16 & 0xff) / 255.0, (c1 >> 8 & 0xff) / 255.0, (c1 & 0xff) / 255.0);
    cairo_pattern_add_color_stop_rgb(pat, 1, (c2 >> 16 & 0xff) / 255.0, (c2 >> 8 & 0xff) / 255.0, (c2 & 0xff) / 255.0);
    cairo_rectangle(cr, 0, 0, 64, 64);
    cairo_set_source(cr, pat);
    cairo_fill(cr);
    cairo_pattern_destroy(pat);
    bevel(cr, 0, 0, 64, 64, 0xffffff, 0x000000);

    fill(cr, 4, SCR_Y, 56, SCR_H, PANEL);
    bevel(cr, 4, SCR_Y, 56, SCR_H, BEVEL_DARK, BEVEL_LIGHT);
    fill(cr, 4, WELL_Y, 56, 14, PANEL);
    bevel(cr, 4, WELL_Y, 56, 14, BEVEL_DARK, BEVEL_LIGHT);
    /* Zonas: as regiões do layout, numeradas; Mosaico: as janelas
     * organizadas pelo algoritmo; Rolagem: as colunas; desligado: a pausa. */
    wz_mode m = wz_config_mode(&a->cfg);
    const wz_layout_def *l = wz_config_layout(&a->cfg, a->layout[0] ? a->layout : a->cfg.layout);
    wz_rect in = { 7, SCR_Y + 3, 51, SCR_H - 5 };
    if (m == WZ_MODE_ZONES && l) {
        wz_node *t = wz_parse(l->tree, NULL);
        wz_rect zr[64];
        int n = t ? wz_compute(t, in, 2, 0, zr, 64) : 0;
        wz_free(t);
        for (int i = 0; i < n && i < 64; i++) {
            box(cr, zr[i].x, zr[i].y, zr[i].w, zr[i].h, LED_LOW, LED_HIGH, 1);
            number(cr, i + 1, zr[i], LED_HIGH);
        }
    } else if (m == WZ_MODE_TILING) {
        draw_windows(a, cr, in);
    } else if (m == WZ_MODE_SCROLL) {
        draw_strip(a, cr, in);
    } else {
        int cx = 32, cy = SCR_Y + SCR_H / 2;
        fill(cr, cx - 6, cy - 7, 4, 14, LED_HIGH);
        fill(cr, cx + 2, cy - 7, 4, 14, LED_HIGH);
    }
    led_key(cr, HIT_Z, 0, m == WZ_MODE_ZONES, FKEY_Z);
    led_key(cr, HIT_M, 1, m == WZ_MODE_TILING, FKEY_M);
    led_key(cr, HIT_R, 2, m == WZ_MODE_SCROLL, FKEY_R);
    cairo_restore(cr);
}

static void
buffer_release(void *data, struct wl_buffer *b)
{
    (void)data;
    wl_buffer_destroy(b);
}

static const struct wl_buffer_listener buffer_listener = { buffer_release };

static void
redraw(app *a)
{
    if (!a->configured)
        return;
    int W = a->width > 0 ? a->width : 64, H = a->height > 0 ? a->height : 64;
    int stride = W * 4, size = stride * H;
    int fd = memfd_create("wmake-zones-wl", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, size) != 0) {
        if (fd >= 0)
            close(fd);
        return;
    }
    void *data = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return;
    }
    cairo_surface_t *cs = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32, W, H, stride);
    cairo_t *cr = cairo_create(cs);
    draw_face(a, cr, W, H);
    cairo_destroy(cr);
    cairo_surface_destroy(cs);
    munmap(data, (size_t)size);
    struct wl_shm_pool *pool = wl_shm_create_pool(a->shm, fd, size);
    struct wl_buffer *b = wl_shm_pool_create_buffer(pool, 0, W, H, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    wl_buffer_add_listener(b, &buffer_listener, NULL);
    wl_surface_attach(a->surface, b, 0, 0);
    wl_surface_damage_buffer(a->surface, 0, 0, W, H);
    wl_surface_commit(a->surface);
}

static void
reload(app *a)
{
    wz_config_free(&a->cfg);
    wz_config_defaults(&a->cfg);
    wz_config_load(&a->cfg, a->cfg_path);
    wz_theme_load(&a->theme, NULL);
}

/* Layout em efeito no primeiro monitor, pelo status do wlmaker (sem
 * resposta, o geral do arquivo). */
static void
query_layout(app *a)
{
    char out[4096];
    a->layout[0] = 0;
    a->ncol = 0;
    if (wz_ipc_request_session("status", out, sizeof out) != 0)
        return;
    const char *sp = strstr(out, "strip=");
    if (sp) {
        const char *q = strstr(sp, " columns=");
        if (q && sscanf(q, " columns=%d active=%d left=%d right=%d", &a->ncol, &a->active, &a->nleft,
                        &a->nright) == 4) {
            a->active--;
            const char *wl = strstr(sp, "widths=");
            int k = 0;
            for (const char *c = wl ? wl + 7 : NULL; c && *c && *c != '\n' && k < WZ_SCROLL_MAX_COLS; k++) {
                a->widths[k] = atoi(c);
                c = strpbrk(c, ",\n");
                if (!c || *c == '\n')
                    break;
                c++;
            }
            /* Sem as larguras de todas as colunas, a prévia não confia nelas. */
            if (a->ncol > WZ_SCROLL_MAX_COLS || a->ncol > k + 1 || a->active < 0 || a->active >= a->ncol)
                a->ncol = 0;
        } else {
            a->ncol = 0;
        }
    }
    const char *p = strstr(out, " layout=");
    if (!p)
        return;
    p += 8;
    size_t n = strcspn(p, " \n");
    if (n < sizeof a->layout) {
        memcpy(a->layout, p, n);
        a->layout[n] = 0;
    }
}

static void
command(app *a, const char *cmd)
{
    char out[512];
    if (wz_ipc_request_session(cmd, out, sizeof out) != 0)
        fprintf(stderr, "wmake-zones-wl: %s\n", _("wlmaker did not answer (zones disabled at build time?)"));
    reload(a);
    query_layout(a);
    redraw(a);
}

/* Troca de layout pela roda: uma prévia breve das zonas mostra a escolha
 * (o ladrilho é pequeno demais para o nome). */
static void
cycle(app *a, int step)
{
    if (wz_config_mode(&a->cfg) == WZ_MODE_TILING) {
        /* No Mosaico, a roda e as setas trocam o algoritmo. */
        command(a, step < 0 ? "algorithm prev" : "algorithm next");
        return;
    }
    if (wz_config_mode(&a->cfg) == WZ_MODE_SCROLL) {
        /* Na Rolagem, andam entre as colunas. */
        command(a, step < 0 ? "scroll left" : "scroll right");
        return;
    }
    command(a, step < 0 ? "layout prev" : "layout next");
    const wz_layout_def *l = wz_config_layout(&a->cfg, a->layout[0] ? a->layout : a->cfg.layout);
    if (l) {
        char cmd[WZ_IPC_MAX];
        char out[64];
        snprintf(cmd, sizeof cmd, "preview %s", l->tree);
        wz_ipc_request_session(cmd, out, sizeof out);
    }
}

static void
click(app *a, wz_click action, int step)
{
    switch (action) {
    case WZ_CLICK_MODE: command(a, "mode toggle"); break;
    case WZ_CLICK_ENABLE: command(a, "mode enable"); break;
    case WZ_CLICK_ZONES: command(a, "zones toggle"); break;
    case WZ_CLICK_AUTOTILE: command(a, "autotile toggle"); break;
    case WZ_CLICK_CYCLE: cycle(a, step); break;
    case WZ_CLICK_NEXT:
        /* A próxima opção do modo: layout das Zonas (com a prévia),
         * algoritmo do Mosaico, largura da coluna ativa na Rolagem;
         * desligado, religa. */
        switch (wz_config_mode(&a->cfg)) {
        case WZ_MODE_ZONES:
        case WZ_MODE_TILING: cycle(a, 1); break;
        case WZ_MODE_SCROLL: command(a, "column width +"); break;
        default: command(a, "mode enable"); break;
        }
        break;
    case WZ_CLICK_LAYOUTS: command(a, "picker"); break;
    default: break; /* o menu é do wlmaker (clique direito) */
    }
}

/* ---- protocolo de ícones ---------------------------------------------- */

static void
icon_configure(void *data, struct zwlmaker_toplevel_icon_v1 *icon, int32_t w, int32_t h, uint32_t serial)
{
    app *a = data;
    /* O ladrilho inteiro, com o relevo próprio, como o dockapp do X11 (a
     * janela de ícone de 64x64): o wlmaker sugere aos dockapps nativos o
     * ladrilho menos a borda de 3 px de cada lado (58 no de 64). Nunca
     * menos de 64: um wlmaker sem essa regra sugere o ContentSize (48). */
    a->width = w + 6 > 64 ? w + 6 : 64;
    a->height = h + 6 > 64 ? h + 6 : 64;
    /* Só a tela e as teclas recebem o ponteiro; fora delas (a borda, o vão
     * entre a tela e as teclas, a moldura delas) o ponteiro é do ladrilho, e
     * o wlmaker arrasta o dockapp para o Dock e o Clip, como o Window Maker
     * no X11. O desenho continua no ladrilho inteiro. */
    double sx = a->width / 64.0, sy = a->height / 64.0;
    struct wl_region *input = wl_compositor_create_region(a->compositor);
    wl_region_add(input, (int)(5 * sx), (int)((SCR_Y + 1) * sy), (int)(54 * sx), (int)((SCR_H - 2) * sy));
    wl_region_add(input, (int)(5 * sx), (int)(ROW_Y * sy), (int)(54 * sx), (int)(ROW_H * sy));
    wl_surface_set_input_region(a->surface, input);
    wl_region_destroy(input);
    zwlmaker_toplevel_icon_v1_ack_configure(icon, serial);
    a->configured = 1;
    redraw(a);
}

static const struct zwlmaker_toplevel_icon_v1_listener icon_listener = { icon_configure };

/* ---- ponteiro ----------------------------------------------------------- */

static void
pointer_at(app *a, wl_fixed_t x, wl_fixed_t y)
{
    int W = a->width > 0 ? a->width : 64, H = a->height > 0 ? a->height : 64;
    a->px = wl_fixed_to_double(x) * 64.0 / W;
    a->py = wl_fixed_to_double(y) * 64.0 / H;
}

static void
p_enter(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *s, wl_fixed_t x, wl_fixed_t y)
{
    app *a = data;
    a->pointer_inside = s == a->surface;
    pointer_at(a, x, y);
    /* A faixa muda sem mexer no arquivo (janelas, foco): o status de agora. */
    if (a->pointer_inside && wz_config_mode(&a->cfg) == WZ_MODE_SCROLL) {
        query_layout(a);
        redraw(a);
    }
}

static void
p_leave(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *s)
{
    ((app *)data)->pointer_inside = 0;
}

static void
p_motion(void *data, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
    pointer_at(data, x, y);
}

/* Teclas da fileira de baixo: Z, M e R escolhem o modo, e a acesa desliga. */
static int
row_click(app *a)
{
    int hit = row_hit(a->px, a->py);
    wz_mode m = wz_config_mode(&a->cfg);
    if (hit == HIT_Z) {
        command(a, m == WZ_MODE_ZONES ? "mode off" : "mode zones");
    } else if (hit == HIT_M) {
        command(a, m == WZ_MODE_TILING ? "mode off" : "mode tiling");
    } else if (hit == HIT_R) {
        command(a, m == WZ_MODE_SCROLL ? "mode off" : "mode scroll");
    }
    return hit != HIT_NONE;
}

static void
p_button(void *data, struct wl_pointer *p, uint32_t serial, uint32_t t, uint32_t button, uint32_t state)
{
    app *a = data;
    if (!a->pointer_inside || state != WL_POINTER_BUTTON_STATE_RELEASED)
        return;
    if (button == BTN_LEFT && row_click(a))
        return;
    if (button == BTN_LEFT)
        click(a, a->cfg.click[0], 1);
    else if (button == BTN_MIDDLE)
        click(a, a->cfg.click[1], 1);
}

static void
p_axis(void *data, struct wl_pointer *p, uint32_t t, uint32_t axis, wl_fixed_t v)
{
    app *a = data;
    if (a->pointer_inside && axis == WL_POINTER_AXIS_VERTICAL_SCROLL && v != 0)
        click(a, a->cfg.click[3], v > 0 ? 1 : -1);
}

static void p_frame(void *d, struct wl_pointer *p) {}
static void p_axis_source(void *d, struct wl_pointer *p, uint32_t s) {}
static void p_axis_stop(void *d, struct wl_pointer *p, uint32_t t, uint32_t a) {}
static void p_axis_discrete(void *d, struct wl_pointer *p, uint32_t a, int32_t v) {}

static const struct wl_pointer_listener pointer_listener = {
    p_enter, p_leave, p_motion, p_button, p_axis, p_frame, p_axis_source, p_axis_stop, p_axis_discrete,
};

static void
seat_caps(void *data, struct wl_seat *seat, uint32_t caps)
{
    app *a = data;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !a->pointer) {
        a->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(a->pointer, &pointer_listener, a);
    } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && a->pointer) {
        wl_pointer_destroy(a->pointer);
        a->pointer = NULL;
    }
}

static void seat_name(void *d, struct wl_seat *s, const char *n) {}

static const struct wl_seat_listener seat_listener = { seat_caps, seat_name };

static void
reg_global(void *data, struct wl_registry *r, uint32_t name, const char *iface, uint32_t version)
{
    app *a = data;
    if (!strcmp(iface, wl_compositor_interface.name))
        a->compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
    else if (!strcmp(iface, wl_shm_interface.name))
        a->shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, wl_seat_interface.name) && !a->seat) {
        a->seat = wl_registry_bind(r, name, &wl_seat_interface, version < 5 ? version : 5);
        wl_seat_add_listener(a->seat, &seat_listener, a);
    } else if (!strcmp(iface, zwlmaker_icon_manager_v1_interface.name))
        a->icon_manager = wl_registry_bind(r, name, &zwlmaker_icon_manager_v1_interface, 1);
}

static void reg_remove(void *d, struct wl_registry *r, uint32_t n) {}

static const struct wl_registry_listener reg_listener = { reg_global, reg_remove };

/* Vigia o diretório de um arquivo (renomeações atômicas também contam). */
static void
watch_dir_of(int ifd, const char *path)
{
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (!slash)
        return;
    *slash = 0;
    mkdir(dir, 0700);
    inotify_add_watch(ifd, dir, IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
}

int
main(void)
{
    setlocale(LC_ALL, "");
#ifdef WZ_ENABLE_NLS
    bindtextdomain("wmake-zones", WZ_LOCALEDIR);
    textdomain("wmake-zones");
#endif
    app a = { 0 };
    a.cfg_path = wz_config_path();
    wz_config_defaults(&a.cfg);
    reload(&a);
    query_layout(&a);
    a.display = wl_display_connect(NULL);
    if (!a.display) {
        fprintf(stderr, "wmake-zones-wl: %s\n", _("no Wayland compositor"));
        return 1;
    }
    struct wl_registry *reg = wl_display_get_registry(a.display);
    wl_registry_add_listener(reg, &reg_listener, &a);
    wl_display_roundtrip(a.display);
    if (!a.compositor || !a.shm || !a.icon_manager) {
        fprintf(stderr, "wmake-zones-wl: %s\n", _("the compositor does not offer the wlmaker icon protocol"));
        return 1;
    }
    a.surface = wl_compositor_create_surface(a.compositor);
    a.icon = zwlmaker_icon_manager_v1_get_toplevel_icon(a.icon_manager, NULL, a.surface);
    zwlmaker_toplevel_icon_v1_add_listener(a.icon, &icon_listener, &a);
    wl_surface_commit(a.surface);
    wl_display_roundtrip(a.display);

    int ifd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    watch_dir_of(ifd, a.cfg_path);
    char tpath[1024];
    const char *home = getenv("HOME");
    snprintf(tpath, sizeof tpath, "%s/GNUstep/Defaults/WindowMaker", home ? home : "");
    watch_dir_of(ifd, tpath);

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, NULL);
    int sfd = signalfd(-1, &mask, SFD_CLOEXEC);

    while (!a.quit) {
        while (wl_display_prepare_read(a.display) != 0)
            wl_display_dispatch_pending(a.display);
        wl_display_flush(a.display);
        struct pollfd p[3] = { { wl_display_get_fd(a.display), POLLIN, 0 }, { ifd, POLLIN, 0 }, { sfd, POLLIN, 0 } };
        if (poll(p, 3, -1) < 0 && errno != EINTR) {
            wl_display_cancel_read(a.display);
            break;
        }
        if (p[0].revents & POLLIN) {
            if (wl_display_read_events(a.display) < 0)
                break;
        } else {
            wl_display_cancel_read(a.display);
        }
        if (p[0].revents & (POLLERR | POLLHUP))
            break;
        wl_display_dispatch_pending(a.display);
        if (p[1].revents & POLLIN) {
            char buf[4096];
            while (read(ifd, buf, sizeof buf) > 0)
                ;
            reload(&a);
            query_layout(&a);
            redraw(&a);
        }
        if (p[2].revents & POLLIN)
            a.quit = 1;
    }
    if (a.icon)
        zwlmaker_toplevel_icon_v1_destroy(a.icon);
    if (a.surface)
        wl_surface_destroy(a.surface);
    wl_display_disconnect(a.display);
    wz_config_free(&a.cfg);
    free(a.cfg_path);
    return 0;
}
