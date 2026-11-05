/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * wmake-zones: configuração. Arquivo INI compatível com GKeyFile em
 * $XDG_CONFIG_HOME/wmake-zones/wmake-zones.conf. Sem o arquivo valem os
 * padrões daqui: Zonas ligadas (desde a 0.3; quem desligou tem
 * Zones=false gravado e continua assim), Auto Tiling desligado: as janelas
 * só mudam de lugar quando o usuário as arrasta para uma zona.
 */
#ifndef WZ_CONFIG_H
#define WZ_CONFIG_H

#include "wz-tile.h"

#define WZ_COLOR_AUTO 0xFFFFFFFFu

typedef enum { WZ_ACTIVATE_DRAG = 0, WZ_ACTIVATE_MODIFIER, WZ_ACTIVATE_EDGE } wz_activation;
typedef enum { WZ_MOD_SUPER = 0, WZ_MOD_ALT, WZ_MOD_CTRL, WZ_MOD_SHIFT } wz_modifier;
typedef enum { WZ_RULE_FLOAT = 0, WZ_RULE_TILE, WZ_RULE_ZONE, WZ_RULE_IGNORE, WZ_RULE_NONE } wz_rule_action;
/* Arrasto: primeiro o layout (painel de miniaturas), depois a zona. */
typedef enum {
    WZ_FLOW_LAYOUTS = 0,      /* sempre o painel de layouts primeiro */
    WZ_FLOW_ZONES,            /* direto as zonas do layout atual */
    WZ_FLOW_LAYOUTS_MODIFIER, /* painel só com o modificador; sem ele, zonas */
    WZ_FLOW_LAYOUTS_EDGE,     /* painel ao levar o ponteiro à borda de cima */
} wz_drag_flow;
/* Foco espacial ao chegar à borda do monitor. */
typedef enum { WZ_FOCUS_CROSS = 0, WZ_FOCUS_STOP, WZ_FOCUS_WRAP } wz_focus_edge;
/* Modo de organização (dockapp, comando mode). */
typedef enum { WZ_MODE_OFF = 0, WZ_MODE_ZONES, WZ_MODE_TILING } wz_mode;
/* Ações dos botões do dockapp. */
typedef enum {
    WZ_CLICK_NONE = 0,
    WZ_CLICK_MODE,      /* Zonas <-> Mosaico */
    WZ_CLICK_ENABLE,    /* Desligado <-> último modo */
    WZ_CLICK_LAYOUTS,   /* painel de layouts */
    WZ_CLICK_MENU,
    WZ_CLICK_ZONES,     /* liga/desliga só as zonas */
    WZ_CLICK_AUTOTILE,  /* liga/desliga só o mosaico */
    WZ_CLICK_CYCLE,     /* roda: layout anterior/próximo */
} wz_click;

typedef enum {
    WZ_KEY_PICKER = 0,
    WZ_KEY_TOGGLE_ZONES,
    WZ_KEY_TOGGLE_AUTOTILE,
    WZ_KEY_EDITOR,
    WZ_KEY_NEXT_LAYOUT,
    WZ_KEY_MOVE_LEFT, WZ_KEY_MOVE_RIGHT, WZ_KEY_MOVE_UP, WZ_KEY_MOVE_DOWN,
    WZ_KEY_FOCUS_LEFT, WZ_KEY_FOCUS_RIGHT, WZ_KEY_FOCUS_UP, WZ_KEY_FOCUS_DOWN,
    WZ_KEY_SWAP_LEFT, WZ_KEY_SWAP_RIGHT, WZ_KEY_SWAP_UP, WZ_KEY_SWAP_DOWN,
    WZ_KEY_RESTORE,
    WZ_KEY_TOGGLE_FLOAT,
    WZ_KEY_PROMOTE,
    WZ_KEY_GROW, WZ_KEY_SHRINK,
    WZ_KEY_UNDO,
    WZ_KEY_REDO,
    WZ_KEY_N
} wz_key_action;

typedef struct {
    char *id;     /* chave estável ("halves") ou nome do usuário */
    char *label;  /* nome mostrado; NULL = traduzir o id dos internos */
    char *tree;
    int builtin;
} wz_layout_def;

typedef struct {
    char *id;
    char *wm_class, *instance, *title; /* title: trecho (opcional) */
    wz_rule_action action;
    int zone;        /* 1.. */
    char *layout;    /* opcional: zona deste layout */
    char *monitor;   /* opcional: abre neste monitor (e nele a zona) */
    int workspace;   /* opcional: abre nesta área de trabalho (1..; 0 = não muda) */
} wz_rule;

typedef struct {
    char *monitor;   /* NULL = qualquer */
    int workspace;   /* 0 = qualquer */
    char *layout;    /* NULL = herda */
    int autotile;    /* -1 = herda */
    int algo;        /* -1 = herda */
} wz_scope;

typedef struct {
    /* Geral */
    int zones;
    int autotile;
    wz_activation activation;
    wz_modifier modifier;
    int show_delay_ms;
    int min_drag_px;
    int span_px;
    int edge_px;
    int show_numbers;
    int restore_size_on_unsnap;
    int avoid_dock;
    int rules_enabled;
    int keep_running;     /* serviço ativo mesmo com tudo desligado (atalhos) */
    char *layout;
    int layout_set;       /* Layout gravado; sem ele, escolhido pelo monitor */
    int gap_in, gap_out;
    wz_drag_flow drag_flow;
    int dwell_ms;         /* tempo sobre uma miniatura até mostrar as zonas */
    char *drag_layouts;   /* layouts do painel, separados por vírgula */
    int history_depth;
    wz_focus_edge focus_edge;
    wz_mode last_mode;    /* para religar (clique do meio) */
    wz_click click[4];    /* 0 esquerdo, 1 meio, 2 direito, 3 roda */
    /* Mosaico */
    wz_tile_params tile;
    /* Aparência */
    char *theme;          /* auto, light, dark */
    unsigned highlight;   /* 0xRRGGBB ou WZ_COLOR_AUTO (cor do tema) */
    int opacity;          /* 0..100 */
    /* Atalhos ("Super+Ctrl+Left"; vazio = sem atalho) */
    char *keys[WZ_KEY_N];
    /* Listas */
    wz_layout_def *layouts;
    int nlayouts;
    wz_rule *rules;
    int nrules;
    wz_scope *scopes;
    int nscopes;
} wz_config;

const char *wz_key_name(wz_key_action k);       /* "Picker"... */
const char *wz_key_default(wz_key_action k);    /* "Super+z"... */

void wz_config_defaults(wz_config *c);
/* Lê o arquivo (NULL = caminho padrão). Arquivo ausente não é erro.
 * Devolve 0, ou -1 se o arquivo existe e não pôde ser lido. */
int wz_config_load(wz_config *c, const char *path);
void wz_config_free(wz_config *c);

char *wz_config_path(void);      /* malloc */
char *wz_runtime_dir(void);      /* malloc: $XDG_RUNTIME_DIR/wmake-zones */

const wz_layout_def *wz_config_layout(const wz_config *c, const char *id);
/* Layout efetivo do monitor/área de trabalho (escopos > geral). */
const wz_layout_def *wz_config_layout_for(const wz_config *c, const char *monitor, int workspace);
/* O mesmo, sabendo o tamanho do monitor: sem layout escolhido pelo usuário,
 * um padrão previsível pela proporção (ultrawide a partir de 21:9,
 * metades em cima e embaixo num monitor em pé, metades lado a lado nos
 * outros). */
const wz_layout_def *wz_config_layout_for_size(const wz_config *c, const char *monitor, int workspace,
                                               int width, int height);
/* Layouts do painel do arrasto, na ordem; max no máximo. */
int wz_config_drag_layouts(const wz_config *c, const wz_layout_def **out, int max);

/* Modo efetivo (geral) e troca de modo: grava Zones, Autotile e LastMode
 * em path (NULL = não grava). arg: zones, tiling, off, toggle (Zonas <->
 * Mosaico; desligado liga o último), enable (desligado <-> último modo).
 * Devolve o novo modo, ou -1 se arg é desconhecido. */
wz_mode wz_config_mode(const wz_config *c);
int wz_config_set_mode(wz_config *c, const char *path, const char *arg);
const char *wz_mode_name(wz_mode m);
int wz_config_autotile_for(const wz_config *c, const char *monitor, int workspace);
int wz_config_algo_for(const wz_config *c, const char *monitor, int workspace);

/* Regra para a janela (NULL se nenhuma). */
const wz_rule *wz_config_match_rule(const wz_config *c, const char *wm_class,
                                    const char *instance, const char *title);

/* Grava uma chave preservando o resto do arquivo (comentários, outras
 * chaves). value NULL apaga a chave. Escrita atômica. 0 = ok. */
int wz_ini_set(const char *path, const char *section, const char *key, const char *value);

/* Leitura genérica: cb(section, key, value, ud) para cada par. */
typedef void (*wz_ini_cb)(const char *section, const char *key, const char *value, void *ud);
int wz_ini_parse_file(const char *path, wz_ini_cb cb, void *ud);
int wz_ini_parse_string(const char *text, wz_ini_cb cb, void *ud);

/* Nome dos layouts internos (para traduzir com gettext no chamador). */
const char *wz_builtin_label(const char *id);

#endif
