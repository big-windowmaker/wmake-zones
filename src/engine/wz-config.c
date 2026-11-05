/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include "wz-alloc.h"
#include "wz-config.h"
#include "wz-i18n.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- atalhos ---------------------------------------------------------- */

static const struct {
    const char *name, *def;
} key_table[WZ_KEY_N] = {
    [WZ_KEY_PICKER] = { "Picker", "Super+z" },
    [WZ_KEY_TOGGLE_ZONES] = { "ToggleZones", "" },
    [WZ_KEY_TOGGLE_AUTOTILE] = { "ToggleAutotile", "Super+Shift+z" },
    [WZ_KEY_EDITOR] = { "Editor", "Super+Alt+z" },
    [WZ_KEY_NEXT_LAYOUT] = { "NextLayout", "Super+Ctrl+z" },
    [WZ_KEY_MOVE_LEFT] = { "MoveLeft", "Super+Ctrl+Left" },
    [WZ_KEY_MOVE_RIGHT] = { "MoveRight", "Super+Ctrl+Right" },
    [WZ_KEY_MOVE_UP] = { "MoveUp", "Super+Ctrl+Up" },
    [WZ_KEY_MOVE_DOWN] = { "MoveDown", "Super+Ctrl+Down" },
    [WZ_KEY_FOCUS_LEFT] = { "FocusLeft", "Super+Alt+Left" },
    [WZ_KEY_FOCUS_RIGHT] = { "FocusRight", "Super+Alt+Right" },
    [WZ_KEY_FOCUS_UP] = { "FocusUp", "Super+Alt+Up" },
    [WZ_KEY_FOCUS_DOWN] = { "FocusDown", "Super+Alt+Down" },
    [WZ_KEY_SWAP_LEFT] = { "SwapLeft", "Super+Shift+Left" },
    [WZ_KEY_SWAP_RIGHT] = { "SwapRight", "Super+Shift+Right" },
    [WZ_KEY_SWAP_UP] = { "SwapUp", "Super+Shift+Up" },
    [WZ_KEY_SWAP_DOWN] = { "SwapDown", "Super+Shift+Down" },
    [WZ_KEY_RESTORE] = { "Restore", "Super+Ctrl+0" },
    [WZ_KEY_TOGGLE_FLOAT] = { "ToggleFloat", "Super+Ctrl+f" },
    [WZ_KEY_PROMOTE] = { "Promote", "Super+Ctrl+Return" },
    [WZ_KEY_GROW] = { "Grow", "Super+Ctrl+equal" },
    [WZ_KEY_SHRINK] = { "Shrink", "Super+Ctrl+minus" },
    [WZ_KEY_UNDO] = { "Undo", "Super+Ctrl+BackSpace" },
    [WZ_KEY_REDO] = { "Redo", "Super+Ctrl+Shift+BackSpace" },
};

const char *
wz_key_name(wz_key_action k)
{
    return k < WZ_KEY_N ? key_table[k].name : "";
}

const char *
wz_key_default(wz_key_action k)
{
    return k < WZ_KEY_N ? key_table[k].def : "";
}

/* ---- layouts internos -------------------------------------------------- */

static const struct {
    const char *id, *label, *tree;
} builtin[] = {
    { "halves", N_("Halves"), "H(1,1)" },
    { "quarters", N_("Quarters"), "V(H(1,1),H(1,1))" },
    { "thirds", N_("Three columns"), "H(1,1,1)" },
    { "main-stack", N_("Main + stack"), "H(3,2:V(1,1))" },
    { "two-top", N_("Two top + one bottom"), "V(H(1,1),1)" },
    { "two-bottom", N_("One top + two bottom"), "V(1,H(1,1))" },
    { "grid-2x3", N_("Grid 2x3"), "V(H(1,1,1),H(1,1,1))" },
    { "grid-3x3", N_("Grid 3x3"), "V(H(1,1,1),H(1,1,1),H(1,1,1))" },
    { "main-70", N_("Main 70/30"), "H(7,3)" },
    { "ultrawide", N_("Ultrawide"), "H(1,2,1)" },
    { "ultrawide-4", N_("Ultrawide: four columns"), "H(1,1,1,1)" },
    { "development", N_("Development"), "H(3:V(3\"Editor\",1\"Terminal\"),2\"Browser\")" },
    { "small-screen", N_("Small screen 60/40"), "H(3,2)" },
    { "portrait", N_("Portrait: halves"), "V(1,1)" },
    { "portrait-thirds", N_("Portrait: three rows"), "V(1,1,1)" },
};
/* Nomes das zonas dos layouts internos, traduzidos na hora de mostrar. */
static const char *const builtin_zone_names[] __attribute__((unused)) = { N_("Editor"), N_("Terminal"), N_("Browser") };

#define NBUILTIN (int)(sizeof builtin / sizeof builtin[0])

#ifndef WZ_VERSION
#define WZ_VERSION "?"
#endif

const char *
wz_version(void)
{
    return WZ_VERSION;
}

const char *
wz_builtin_label(const char *id)
{
    for (int i = 0; i < NBUILTIN; i++)
        if (strcmp(builtin[i].id, id) == 0)
            return builtin[i].label;
    return NULL;
}

/* ---- utilidades -------------------------------------------------------- */

static char *
xstrdup(const char *s)
{
    return s ? wz_xstrdup(s) : NULL;
}

static void
set_str(char **dst, const char *v)
{
    free(*dst);
    *dst = xstrdup(v);
}

static int
parse_bool(const char *v, int def)
{
    if (!v)
        return def;
    if (!strcasecmp(v, "true") || !strcasecmp(v, "yes") || !strcmp(v, "1") || !strcasecmp(v, "on"))
        return 1;
    if (!strcasecmp(v, "false") || !strcasecmp(v, "no") || !strcmp(v, "0") || !strcasecmp(v, "off"))
        return 0;
    return def;
}

static int
parse_int(const char *v, int def, int lo, int hi)
{
    if (!v || !*v)
        return def;
    char *end;
    long n = strtol(v, &end, 10);
    if (*end)
        return def;
    return n < lo ? lo : n > hi ? hi : (int)n;
}

static double
parse_double(const char *v, double def, double lo, double hi)
{
    if (!v || !*v)
        return def;
    char *end;
    double n = wz_strtod(v, &end);
    if (*end || end == v)
        return def;
    return n < lo ? lo : n > hi ? hi : n;
}

char *
wz_config_path(void)
{
    const char *x = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char *p = NULL;
    if (x && *x == '/')
        (void)!asprintf(&p, "%s/wmake-zones/wmake-zones.conf", x);
    else
        (void)!asprintf(&p, "%s/.config/wmake-zones/wmake-zones.conf", home ? home : "/tmp");
    return p;
}

char *
wz_runtime_dir(void)
{
    const char *x = getenv("XDG_RUNTIME_DIR");
    char *p = NULL;
    if (x && *x == '/')
        (void)!asprintf(&p, "%s/wmake-zones", x);
    else
        (void)!asprintf(&p, "/tmp/wmake-zones-%u", (unsigned)getuid());
    return p;
}

/* ---- INI --------------------------------------------------------------- */

static char *
trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        *--e = 0;
    return s;
}

static void
unescape(char *s)
{
    char *w = s;
    for (char *r = s; *r; r++) {
        if (*r == '\\' && r[1]) {
            r++;
            switch (*r) {
            case 's': *w++ = ' '; break;
            case 'n': *w++ = '\n'; break;
            case 't': *w++ = '\t'; break;
            case 'r': *w++ = '\r'; break;
            default: *w++ = *r; break;
            }
        } else {
            *w++ = *r;
        }
    }
    *w = 0;
}

int
wz_ini_parse_string(const char *text, wz_ini_cb cb, void *ud)
{
    char *copy = wz_xstrdup(text ? text : "");
    char section[256] = "";
    char *save = NULL;
    for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *l = trim(line);
        if (!*l || *l == '#' || *l == ';')
            continue;
        if (*l == '[') {
            char *e = strchr(l, ']');
            if (e) {
                *e = 0;
                snprintf(section, sizeof section, "%s", l + 1);
            }
            continue;
        }
        char *eq = strchr(l, '=');
        if (!eq)
            continue;
        *eq = 0;
        char *k = trim(l), *v = trim(eq + 1);
        unescape(v);
        cb(section, k, v, ud);
    }
    free(copy);
    return 0;
}

static char *
read_file(const char *path, int *err)
{
    *err = 0;
    FILE *f = fopen(path, "r");
    if (!f) {
        *err = errno;
        return NULL;
    }
    size_t cap = 4096, len = 0;
    char *buf = wz_xmalloc(cap);
    size_t r;
    while ((r = fread(buf + len, 1, cap - len - 1, f)) > 0) {
        len += r;
        if (len + 1 >= cap) {
            if (cap > (1u << 22)) { /* 4 MiB: não é configuração; nunca truncar */
                free(buf);
                fclose(f);
                *err = EFBIG;
                return NULL;
            }
            cap *= 2;
            buf = wz_xrealloc(buf, cap);
        }
    }
    buf[len] = 0;
    fclose(f);
    return buf;
}

int
wz_ini_parse_file(const char *path, wz_ini_cb cb, void *ud)
{
    int err;
    char *t = read_file(path, &err);
    if (!t)
        return err == ENOENT ? 0 : -1;
    wz_ini_parse_string(t, cb, ud);
    free(t);
    return 0;
}

static int
mkdir_p(const char *dir)
{
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s", dir);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0700);
            *p = '/';
        }
    return mkdir(tmp, 0700) == 0 || errno == EEXIST ? 0 : -1;
}

/* Escapes do GKeyFile: o leitor apara espaços nas pontas, então espaço e
 * tab nas pontas (e tab, CR e LF em qualquer lugar) viram sequências. */
static void
escape_value(FILE *f, const char *v)
{
    size_t n = strlen(v), end = n;
    while (end > 0 && (v[end - 1] == ' ' || v[end - 1] == '\t'))
        end--;
    for (size_t i = 0; i < n; i++) {
        char ch = v[i];
        int edge = i == 0 || i >= end;
        if (ch == '\\')
            fputs("\\\\", f);
        else if (ch == '\n')
            fputs("\\n", f);
        else if (ch == '\r')
            fputs("\\r", f);
        else if (ch == '\t')
            fputs("\\t", f);
        else if (ch == ' ' && edge)
            fputs("\\s", f);
        else
            fputc(ch, f);
    }
}

static int ini_set_unlocked(const char *path, const char *section, const char *key, const char *value);

/*
 * Grava uma chave. O serviço e as preferências gravam o mesmo arquivo: uma
 * trava (arquivo .lock ao lado) faz de ler-alterar-renomear uma operação só,
 * e um link simbólico (dotfiles) é seguido, não trocado por um arquivo.
 */
int
wz_ini_set(const char *path, const char *section, const char *key, const char *value)
{
    char real[PATH_MAX];
    const char *target = path;
    if (realpath(path, real))
        target = real;
    char dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s", target);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = 0;
        if (mkdir_p(dir) < 0)
            return -1;
    }
    char lock[PATH_MAX];
    if (snprintf(lock, sizeof lock, "%s.lock", target) >= (int)sizeof lock)
        return -1;
    int lfd = open(lock, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (lfd >= 0)
        flock(lfd, LOCK_EX);
    int r = ini_set_unlocked(target, section, key, value);
    if (lfd >= 0)
        close(lfd); /* solta a trava */
    return r;
}

static int
ini_set_unlocked(const char *path, const char *section, const char *key, const char *value)
{
    int err;
    char *text = read_file(path, &err);
    if (!text && err != ENOENT)
        return -1;
    char tmp[PATH_MAX + 32];
    snprintf(tmp, sizeof tmp, "%s.tmp.%d", path, (int)getpid());
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) {
        free(text);
        return -1;
    }
    FILE *f = fdopen(fd, "w");
    if (!f) {
        close(fd);
        unlink(tmp);
        free(text);
        return -1;
    }

    int in_section = 0, done = 0, seen_section = 0;
    char *pending_blank = NULL; /* linhas em branco no fim da seção */
    size_t klen = strlen(key);
    char *p = text;
    while (p && *p) {
        char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        char *line = wz_xstrndup(p, len);
        p = nl ? nl + 1 : NULL;
        char *dup = wz_xstrdup(line);
        char *l = trim(dup);
        if (*l == '[') {
            if (in_section && !done && value) {
                fprintf(f, "%s=", key);
                escape_value(f, value);
                fputc('\n', f);
                done = 1;
            }
            if (pending_blank) {
                fputs(pending_blank, f);
                free(pending_blank);
                pending_blank = NULL;
            }
            char *e = strchr(l, ']');
            in_section = e && (size_t)(e - l - 1) == strlen(section) && !strncmp(l + 1, section, strlen(section));
            if (in_section)
                seen_section = 1;
            fprintf(f, "%s\n", line);
        } else if (in_section && !strncmp(l, key, klen) && (l[klen] == '=' || isspace((unsigned char)l[klen]))) {
            char *rest = l + klen;
            while (isspace((unsigned char)*rest))
                rest++;
            if (*rest == '=') {
                if (pending_blank) {
                    fputs(pending_blank, f);
                    free(pending_blank);
                    pending_blank = NULL;
                }
                if (value && !done) {
                    fprintf(f, "%s=", key);
                    escape_value(f, value);
                    fputc('\n', f);
                }
                done = 1;
            } else {
                fprintf(f, "%s\n", line);
            }
        } else if (!*l) {
            /* Adia as linhas em branco para inserir a chave antes delas. */
            size_t old = pending_blank ? strlen(pending_blank) : 0;
            pending_blank = wz_xrealloc(pending_blank, old + strlen(line) + 2);
            if (!old)
                pending_blank[0] = 0;
            strcat(pending_blank, line);
            strcat(pending_blank, "\n");
        } else {
            if (pending_blank) {
                fputs(pending_blank, f);
                free(pending_blank);
                pending_blank = NULL;
            }
            fprintf(f, "%s\n", line);
        }
        free(dup);
        free(line);
    }
    if (in_section && !done && value) {
        fprintf(f, "%s=", key);
        escape_value(f, value);
        fputc('\n', f);
        done = 1;
    }
    if (pending_blank) {
        fputs(pending_blank, f);
        free(pending_blank);
    }
    if (!seen_section && value) {
        if (text && *text)
            fputc('\n', f);
        fprintf(f, "[%s]\n%s=", section, key);
        escape_value(f, value);
        fputc('\n', f);
    }
    free(text);
    int bad = ferror(f);
    if (fflush(f) != 0 || fsync(fileno(f)) != 0)
        bad = 1;
    fclose(f);
    if (bad || rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

/* ---- configuração ------------------------------------------------------ */

void
wz_config_defaults(wz_config *c)
{
    memset(c, 0, sizeof *c);
    c->zones = 1;
    c->autotile = 0;
    c->activation = WZ_ACTIVATE_DRAG;
    c->modifier = WZ_MOD_SUPER;
    c->show_delay_ms = 120;
    c->min_drag_px = 24;
    c->span_px = 24;
    c->edge_px = 48;
    c->show_numbers = 1;
    c->restore_size_on_unsnap = 1;
    c->avoid_dock = 1;
    c->rules_enabled = 1;
    c->layout = wz_xstrdup("halves");
    c->gap_in = 6;
    c->gap_out = 6;
    c->drag_flow = WZ_FLOW_LAYOUTS;
    c->dwell_ms = 250;
    c->drag_layouts = wz_xstrdup("halves,quarters,thirds,main-stack,two-top,grid-2x3,ultrawide");
    c->history_depth = 20;
    c->focus_edge = WZ_FOCUS_CROSS;
    c->last_mode = WZ_MODE_ZONES;
    c->click[0] = WZ_CLICK_MODE;
    c->click[1] = WZ_CLICK_ENABLE;
    c->click[2] = WZ_CLICK_MENU;
    c->click[3] = WZ_CLICK_CYCLE;
    c->tile = (wz_tile_params){ .algo = WZ_TILE_MASTER, .master_ratio = 0.6, .nmaster = 1,
                                .master_pos = WZ_POS_LEFT, .gap_in = 6, .gap_out = 6, .smart_gaps = 1 };
    c->theme = wz_xstrdup("auto");
    c->highlight = WZ_COLOR_AUTO;
    c->opacity = 35;
    for (int i = 0; i < WZ_KEY_N; i++)
        c->keys[i] = wz_xstrdup(key_table[i].def);
    for (int i = 0; i < NBUILTIN; i++) {
        c->layouts = wz_xrealloc(c->layouts, sizeof *c->layouts * (size_t)(c->nlayouts + 1));
        c->layouts[c->nlayouts++] = (wz_layout_def){ wz_xstrdup(builtin[i].id), NULL, wz_xstrdup(builtin[i].tree), 1 };
    }
}

static wz_layout_def *
layout_get(wz_config *c, const char *id, int create)
{
    for (int i = 0; i < c->nlayouts; i++)
        if (!strcmp(c->layouts[i].id, id))
            return &c->layouts[i];
    if (!create || c->nlayouts >= 256)
        return NULL;
    c->layouts = wz_xrealloc(c->layouts, sizeof *c->layouts * (size_t)(c->nlayouts + 1));
    wz_layout_def *l = &c->layouts[c->nlayouts++];
    *l = (wz_layout_def){ wz_xstrdup(id), NULL, NULL, 0 };
    return l;
}

static wz_rule *
rule_get(wz_config *c, const char *id)
{
    for (int i = 0; i < c->nrules; i++)
        if (!strcmp(c->rules[i].id, id))
            return &c->rules[i];
    if (c->nrules >= 256)
        return NULL;
    c->rules = wz_xrealloc(c->rules, sizeof *c->rules * (size_t)(c->nrules + 1));
    wz_rule *r = &c->rules[c->nrules++];
    memset(r, 0, sizeof *r);
    r->id = wz_xstrdup(id);
    r->action = WZ_RULE_FLOAT;
    return r;
}

/* "Monitor DP-1", "Monitor DP-1 Workspace 2", "Workspace 2" */
static wz_scope *
scope_get(wz_config *c, const char *section)
{
    char mon[128] = "";
    int ws = 0;
    if (!strncmp(section, "Monitor ", 8)) {
        const char *rest = section + 8;
        const char *w = strstr(rest, " Workspace ");
        size_t len = w ? (size_t)(w - rest) : strlen(rest);
        if (len >= sizeof mon)
            return NULL;
        memcpy(mon, rest, len);
        mon[len] = 0;
        if (w)
            ws = atoi(w + 11);
    } else if (!strncmp(section, "Workspace ", 10)) {
        ws = atoi(section + 10);
        if (ws <= 0)
            return NULL;
    } else {
        return NULL;
    }
    for (int i = 0; i < c->nscopes; i++) {
        wz_scope *s = &c->scopes[i];
        if (s->workspace == ws && ((!s->monitor && !*mon) || (s->monitor && !strcmp(s->monitor, mon))))
            return s;
    }
    if (c->nscopes >= 256)
        return NULL;
    c->scopes = wz_xrealloc(c->scopes, sizeof *c->scopes * (size_t)(c->nscopes + 1));
    wz_scope *s = &c->scopes[c->nscopes++];
    *s = (wz_scope){ *mon ? wz_xstrdup(mon) : NULL, ws, NULL, -1, -1 };
    return s;
}

static unsigned
parse_color(const char *v, unsigned def)
{
    if (!v || *v != '#' || strlen(v) != 7)
        return def;
    char *end;
    unsigned long n = strtoul(v + 1, &end, 16);
    return *end ? def : (unsigned)n;
}

static void
load_cb(const char *sec, const char *k, const char *v, void *ud)
{
    wz_config *c = ud;
    if (!strcmp(sec, "General")) {
        if (!strcmp(k, "Zones"))
            c->zones = parse_bool(v, c->zones);
        else if (!strcmp(k, "Autotile"))
            c->autotile = parse_bool(v, c->autotile);
        else if (!strcmp(k, "Activation"))
            c->activation = !strcmp(v, "modifier") ? WZ_ACTIVATE_MODIFIER : !strcmp(v, "edge") ? WZ_ACTIVATE_EDGE : WZ_ACTIVATE_DRAG;
        else if (!strcmp(k, "Modifier"))
            c->modifier = !strcmp(v, "alt") ? WZ_MOD_ALT : !strcmp(v, "ctrl") ? WZ_MOD_CTRL : !strcmp(v, "shift") ? WZ_MOD_SHIFT : WZ_MOD_SUPER;
        else if (!strcmp(k, "ShowDelay"))
            c->show_delay_ms = parse_int(v, c->show_delay_ms, 0, 2000);
        else if (!strcmp(k, "MinDrag"))
            c->min_drag_px = parse_int(v, c->min_drag_px, 0, 400);
        else if (!strcmp(k, "Span"))
            c->span_px = parse_int(v, c->span_px, 0, 400);
        else if (!strcmp(k, "Edge"))
            c->edge_px = parse_int(v, c->edge_px, 1, 400);
        else if (!strcmp(k, "ShowNumbers"))
            c->show_numbers = parse_bool(v, c->show_numbers);
        else if (!strcmp(k, "RestoreSizeOnUnsnap"))
            c->restore_size_on_unsnap = parse_bool(v, c->restore_size_on_unsnap);
        else if (!strcmp(k, "AvoidDock"))
            c->avoid_dock = parse_bool(v, c->avoid_dock);
        else if (!strcmp(k, "Rules"))
            c->rules_enabled = parse_bool(v, c->rules_enabled);
        else if (!strcmp(k, "KeepRunning"))
            c->keep_running = parse_bool(v, c->keep_running);
        else if (!strcmp(k, "Layout") && *v) {
            set_str(&c->layout, v);
            c->layout_set = 1;
        } else if (!strcmp(k, "DragFlow"))
            c->drag_flow = !strcmp(v, "zones") ? WZ_FLOW_ZONES
                           : !strcmp(v, "layouts-modifier") ? WZ_FLOW_LAYOUTS_MODIFIER
                           : !strcmp(v, "layouts-edge") ? WZ_FLOW_LAYOUTS_EDGE
                                                        : WZ_FLOW_LAYOUTS;
        else if (!strcmp(k, "LayoutDwell"))
            c->dwell_ms = parse_int(v, c->dwell_ms, 0, 3000);
        else if (!strcmp(k, "DragLayouts") && *v)
            set_str(&c->drag_layouts, v);
        else if (!strcmp(k, "HistoryDepth"))
            c->history_depth = parse_int(v, c->history_depth, 1, 100);
        else if (!strcmp(k, "FocusEdge"))
            c->focus_edge = !strcmp(v, "stop") ? WZ_FOCUS_STOP : !strcmp(v, "wrap") ? WZ_FOCUS_WRAP : WZ_FOCUS_CROSS;
        else if (!strcmp(k, "LastMode"))
            c->last_mode = !strcmp(v, "tiling") ? WZ_MODE_TILING : WZ_MODE_ZONES;
        else if (!strcmp(k, "Gap"))
            c->gap_in = parse_int(v, c->gap_in, 0, 200);
        else if (!strcmp(k, "OuterGap"))
            c->gap_out = parse_int(v, c->gap_out, 0, 200);
    } else if (!strcmp(sec, "Tiling")) {
        if (!strcmp(k, "Algorithm")) {
            int a = wz_algo_from_name(v);
            if (a >= 0)
                c->tile.algo = a;
        } else if (!strcmp(k, "MasterRatio"))
            c->tile.master_ratio = parse_double(v, c->tile.master_ratio, 0.1, 0.9);
        else if (!strcmp(k, "Masters"))
            c->tile.nmaster = parse_int(v, c->tile.nmaster, 1, 16);
        else if (!strcmp(k, "MasterPosition"))
            c->tile.master_pos = !strcmp(v, "right") ? WZ_POS_RIGHT : !strcmp(v, "top") ? WZ_POS_TOP : !strcmp(v, "bottom") ? WZ_POS_BOTTOM : WZ_POS_LEFT;
        else if (!strcmp(k, "Gap"))
            c->tile.gap_in = parse_int(v, c->tile.gap_in, 0, 200);
        else if (!strcmp(k, "OuterGap"))
            c->tile.gap_out = parse_int(v, c->tile.gap_out, 0, 200);
        else if (!strcmp(k, "SmartGaps"))
            c->tile.smart_gaps = parse_bool(v, c->tile.smart_gaps);
    } else if (!strcmp(sec, "Dockapp")) {
        static const char *const keys[4] = { "LeftClick", "MiddleClick", "RightClick", "Wheel" };
        for (int i = 0; i < 4; i++)
            if (!strcmp(k, keys[i])) {
                wz_click a = !strcmp(v, "mode") ? WZ_CLICK_MODE
                             : !strcmp(v, "enable") ? WZ_CLICK_ENABLE
                             : !strcmp(v, "layouts") ? WZ_CLICK_LAYOUTS
                             : !strcmp(v, "menu") ? WZ_CLICK_MENU
                             : !strcmp(v, "zones") ? WZ_CLICK_ZONES
                             : !strcmp(v, "autotile") ? WZ_CLICK_AUTOTILE
                             : !strcmp(v, "layout") || !strcmp(v, "cycle") ? WZ_CLICK_CYCLE
                                                                           : WZ_CLICK_NONE;
                c->click[i] = a;
            }
    } else if (!strcmp(sec, "Appearance")) {
        if (!strcmp(k, "Theme"))
            set_str(&c->theme, v);
        else if (!strcmp(k, "Highlight"))
            c->highlight = !strcmp(v, "auto") ? WZ_COLOR_AUTO : parse_color(v, c->highlight);
        else if (!strcmp(k, "Opacity"))
            c->opacity = parse_int(v, c->opacity, 0, 100);
    } else if (!strcmp(sec, "Keys")) {
        for (int i = 0; i < WZ_KEY_N; i++)
            if (!strcmp(k, key_table[i].name))
                set_str(&c->keys[i], v);
    } else if (!strncmp(sec, "Layout ", 7) && sec[7]) {
        wz_layout_def *l = layout_get(c, sec + 7, 1);
        if (!l)
            return;
        if (!strcmp(k, "Tree")) {
            const char *err;
            wz_node *t = wz_parse(v, &err);
            if (t) {
                wz_free(t);
                set_str(&l->tree, v);
                l->builtin = 0;
            }
        } else if (!strcmp(k, "Name")) {
            set_str(&l->label, v);
        }
    } else if (!strncmp(sec, "Rule ", 5) && sec[5]) {
        wz_rule *r = rule_get(c, sec + 5);
        if (!r)
            return;
        if (!strcmp(k, "Class"))
            set_str(&r->wm_class, v);
        else if (!strcmp(k, "Instance"))
            set_str(&r->instance, v);
        else if (!strcmp(k, "Title"))
            set_str(&r->title, v);
        else if (!strcmp(k, "Action"))
            r->action = !strcmp(v, "tile")     ? WZ_RULE_TILE
                        : !strcmp(v, "zone")   ? WZ_RULE_ZONE
                        : !strcmp(v, "ignore") ? WZ_RULE_IGNORE
                        : !strcmp(v, "none")   ? WZ_RULE_NONE
                                               : WZ_RULE_FLOAT;
        else if (!strcmp(k, "Zone"))
            r->zone = parse_int(v, 0, 0, 64);
        else if (!strcmp(k, "Layout"))
            set_str(&r->layout, v);
        else if (!strcmp(k, "Monitor"))
            set_str(&r->monitor, v);
        else if (!strcmp(k, "Workspace"))
            r->workspace = parse_int(v, 0, 0, 64);
    } else {
        wz_scope *s = scope_get(c, sec);
        if (!s)
            return;
        if (!strcmp(k, "Layout"))
            set_str(&s->layout, *v ? v : NULL);
        else if (!strcmp(k, "Autotile"))
            s->autotile = parse_bool(v, -1);
        else if (!strcmp(k, "Algorithm"))
            s->algo = wz_algo_from_name(v);
    }
}

int
wz_config_load(wz_config *c, const char *path)
{
    char *p = path ? NULL : wz_config_path();
    int r = wz_ini_parse_file(path ? path : p, load_cb, c);
    free(p);
    /* Layouts do usuário sem árvore válida não servem. */
    for (int i = 0; i < c->nlayouts; i++)
        if (!c->layouts[i].tree) {
            free(c->layouts[i].id);
            free(c->layouts[i].label);
            c->layouts[i] = c->layouts[--c->nlayouts];
            i--;
        }
    if (!wz_config_layout(c, c->layout))
        set_str(&c->layout, "halves");
    if (c->tile.nmaster < 1)
        c->tile.nmaster = 1;
    return r;
}

void
wz_config_free(wz_config *c)
{
    free(c->layout);
    free(c->drag_layouts);
    free(c->theme);
    for (int i = 0; i < WZ_KEY_N; i++)
        free(c->keys[i]);
    for (int i = 0; i < c->nlayouts; i++) {
        free(c->layouts[i].id);
        free(c->layouts[i].label);
        free(c->layouts[i].tree);
    }
    free(c->layouts);
    for (int i = 0; i < c->nrules; i++) {
        wz_rule *r = &c->rules[i];
        free(r->id);
        free(r->wm_class);
        free(r->instance);
        free(r->title);
        free(r->layout);
        free(r->monitor);
    }
    free(c->rules);
    for (int i = 0; i < c->nscopes; i++) {
        free(c->scopes[i].monitor);
        free(c->scopes[i].layout);
    }
    free(c->scopes);
    memset(c, 0, sizeof *c);
}

const wz_layout_def *
wz_config_layout(const wz_config *c, const char *id)
{
    if (!id)
        return NULL;
    for (int i = 0; i < c->nlayouts; i++)
        if (!strcmp(c->layouts[i].id, id))
            return &c->layouts[i];
    return NULL;
}

/* Escopo mais específico primeiro: monitor+área, monitor, área. */
static const wz_scope *
scope_for(const wz_config *c, const char *mon, int ws, int pass)
{
    for (int i = 0; i < c->nscopes; i++) {
        const wz_scope *s = &c->scopes[i];
        int mon_ok = s->monitor && mon && !strcmp(s->monitor, mon);
        if (pass == 0 && mon_ok && s->workspace == ws && ws > 0)
            return s;
        if (pass == 1 && mon_ok && s->workspace == 0)
            return s;
        if (pass == 2 && !s->monitor && s->workspace == ws && ws > 0)
            return s;
    }
    return NULL;
}

const wz_layout_def *
wz_config_layout_for(const wz_config *c, const char *monitor, int workspace)
{
    for (int pass = 0; pass < 3; pass++) {
        const wz_scope *s = scope_for(c, monitor, workspace, pass);
        if (s && s->layout) {
            const wz_layout_def *l = wz_config_layout(c, s->layout);
            if (l)
                return l;
        }
    }
    const wz_layout_def *l = wz_config_layout(c, c->layout);
    return l ? l : c->nlayouts ? &c->layouts[0] : NULL;
}

const wz_layout_def *
wz_config_layout_for_size(const wz_config *c, const char *monitor, int workspace, int width, int height)
{
    for (int pass = 0; pass < 3; pass++) {
        const wz_scope *s = scope_for(c, monitor, workspace, pass);
        if (s && s->layout) {
            const wz_layout_def *l = wz_config_layout(c, s->layout);
            if (l)
                return l;
        }
    }
    if (!c->layout_set && width > 0 && height > 0) {
        const char *id = (long)width * 9 >= (long)height * 21 ? "ultrawide"
                         : height > width                    ? "portrait"
                                                             : "halves";
        const wz_layout_def *l = wz_config_layout(c, id);
        if (l)
            return l;
    }
    return wz_config_layout_for(c, monitor, workspace);
}

int
wz_config_drag_layouts(const wz_config *c, const wz_layout_def **out, int max)
{
    int n = 0;
    const char *p = c->drag_layouts ? c->drag_layouts : "";
    while (*p && n < max) {
        while (*p == ',' || *p == ' ')
            p++;
        const char *e = p;
        while (*e && *e != ',')
            e++;
        size_t len = (size_t)(e - p);
        while (len && p[len - 1] == ' ')
            len--;
        if (len) {
            char id[128];
            if (len < sizeof id) {
                memcpy(id, p, len);
                id[len] = 0;
                const wz_layout_def *l = wz_config_layout(c, id);
                int dup = 0;
                for (int i = 0; i < n; i++)
                    dup |= out[i] == l;
                if (l && !dup)
                    out[n++] = l;
            }
        }
        p = e;
    }
    /* Os layouts do usuário vêm sempre, depois dos da lista. */
    for (int i = 0; i < c->nlayouts && n < max; i++) {
        if (c->layouts[i].builtin)
            continue;
        int dup = 0;
        for (int j = 0; j < n; j++)
            dup |= out[j] == &c->layouts[i];
        if (!dup)
            out[n++] = &c->layouts[i];
    }
    return n;
}

wz_mode
wz_config_mode(const wz_config *c)
{
    return c->autotile ? WZ_MODE_TILING : c->zones ? WZ_MODE_ZONES : WZ_MODE_OFF;
}

const char *
wz_mode_name(wz_mode m)
{
    return m == WZ_MODE_TILING ? "tiling" : m == WZ_MODE_ZONES ? "zones" : "off";
}

int
wz_config_set_mode(wz_config *c, const char *path, const char *arg)
{
    wz_mode cur = wz_config_mode(c), m;
    if (!arg)
        return -1;
    if (!strcmp(arg, "zones"))
        m = WZ_MODE_ZONES;
    else if (!strcmp(arg, "tiling") || !strcmp(arg, "mosaic"))
        m = WZ_MODE_TILING;
    else if (!strcmp(arg, "off"))
        m = WZ_MODE_OFF;
    else if (!strcmp(arg, "toggle"))
        m = cur == WZ_MODE_ZONES ? WZ_MODE_TILING : cur == WZ_MODE_TILING ? WZ_MODE_ZONES : c->last_mode;
    else if (!strcmp(arg, "enable"))
        m = cur == WZ_MODE_OFF ? c->last_mode : WZ_MODE_OFF;
    else
        return -1;
    if (m == WZ_MODE_OFF && cur != WZ_MODE_OFF)
        c->last_mode = cur;
    else if (m != WZ_MODE_OFF)
        c->last_mode = m;
    /* No mosaico as zonas continuam valendo para as janelas que o usuário
     * tirou dele. */
    c->zones = m != WZ_MODE_OFF;
    c->autotile = m == WZ_MODE_TILING;
    if (path) {
        wz_ini_set(path, "General", "Zones", c->zones ? "true" : "false");
        wz_ini_set(path, "General", "Autotile", c->autotile ? "true" : "false");
        wz_ini_set(path, "General", "LastMode", wz_mode_name(c->last_mode));
    }
    return m;
}

int
wz_config_autotile_for(const wz_config *c, const char *monitor, int workspace)
{
    for (int pass = 0; pass < 3; pass++) {
        const wz_scope *s = scope_for(c, monitor, workspace, pass);
        if (s && s->autotile >= 0)
            return s->autotile;
    }
    return c->autotile;
}

int
wz_config_algo_for(const wz_config *c, const char *monitor, int workspace)
{
    for (int pass = 0; pass < 3; pass++) {
        const wz_scope *s = scope_for(c, monitor, workspace, pass);
        if (s && s->algo >= 0)
            return s->algo;
    }
    return c->tile.algo;
}

static int
ieq(const char *a, const char *b)
{
    return a && b && !strcasecmp(a, b);
}

const wz_rule *
wz_config_match_rule(const wz_config *c, const char *wm_class, const char *instance, const char *title)
{
    if (!c->rules_enabled)
        return NULL;
    for (int i = 0; i < c->nrules; i++) {
        const wz_rule *r = &c->rules[i];
        if (!r->wm_class && !r->instance && !r->title)
            continue;
        if (r->wm_class && !ieq(r->wm_class, wm_class))
            continue;
        if (r->instance && !ieq(r->instance, instance))
            continue;
        if (r->title && (!title || !strcasestr(title, r->title)))
            continue;
        return r;
    }
    return NULL;
}
