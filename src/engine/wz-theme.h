/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Cores e fontes do tema ativo do Window Maker, lidas do próprio
 * ~/GNUstep/Defaults/WindowMaker (o big-wm-theme light/dark troca o estilo
 * ali). Assim o overlay, os menus e o dockapp acompanham o tema sem uma
 * identidade própria. Sem o arquivo valem as cores do BigWindowMaker claro.
 */
#ifndef WZ_THEME_H
#define WZ_THEME_H

typedef struct {
    unsigned title_bg1, title_bg2, title_fg; /* MenuTitleBack/Color */
    unsigned menu_bg1, menu_bg2, menu_fg;    /* MenuTextBack/Color */
    unsigned disabled_fg;                    /* MenuDisabledColor */
    unsigned hl_bg, hl_fg;                   /* HighlightColor/TextColor */
    unsigned ftitle1, ftitle2, ftitle_fg;    /* FTitleBack/Color */
    unsigned icon1, icon2;                   /* IconBack */
    char menu_font[160];
    char title_font[160];
    int dark;
} wz_theme;

void wz_theme_defaults(wz_theme *t);
/* path NULL = ~/GNUstep/Defaults/WindowMaker. Devolve 0 se leu o arquivo. */
int wz_theme_load(wz_theme *t, const char *path);
/* Analisa o texto de um defaults (para testes). */
void wz_theme_parse(wz_theme *t, const char *text);

/* Mistura de cores (t de 0 a 255 para b). */
unsigned wz_rgb_mix(unsigned a, unsigned b, int t);
double wz_rgb_luma(unsigned c);

#endif
