/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include "wz-alloc.h"
#include "wz-theme.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
wz_theme_defaults(wz_theme *t)
{
    memset(t, 0, sizeof *t);
    t->title_bg1 = 0x505a5e;
    t->title_bg2 = 0x202a2e;
    t->title_fg = 0xffffff;
    t->menu_bg1 = 0xc2c0c5;
    t->menu_bg2 = 0x828085;
    t->menu_fg = 0x000000;
    t->disabled_fg = 0x666666;
    t->hl_bg = 0xffffff;
    t->hl_fg = 0x000000;
    t->ftitle1 = 0x505a5e;
    t->ftitle2 = 0x202a2e;
    t->ftitle_fg = 0xffffff;
    t->icon1 = 0xa6a6b6;
    t->icon2 = 0x515561;
    snprintf(t->menu_font, sizeof t->menu_font, "Noto Sans,sans serif:pixelsize=12");
    snprintf(t->title_font, sizeof t->title_font, "Noto Sans,sans serif:bold:pixelsize=12");
}

static const struct {
    const char *name;
    unsigned rgb;
} named[] = {
    { "white", 0xffffff }, { "black", 0x000000 }, { "gray", 0xbebebe }, { "grey", 0xbebebe },
    { "darkgray", 0xa9a9a9 }, { "lightgray", 0xd3d3d3 }, { "red", 0xff0000 }, { "blue", 0x0000ff },
};

/* Lê uma cor X11 ("#rrggbb", "rgb:r/g/b", nome). 0 se não reconheceu. */
static int
parse_color(const char *s, size_t len, unsigned *out)
{
    char buf[64];
    if (len >= sizeof buf)
        return 0;
    memcpy(buf, s, len);
    buf[len] = 0;
    if (buf[0] == '#' && len == 7) {
        char *end;
        unsigned long v = strtoul(buf + 1, &end, 16);
        if (*end)
            return 0;
        *out = (unsigned)v;
        return 1;
    }
    if (!strncmp(buf, "rgb:", 4)) {
        unsigned c[3];
        char *p = buf + 4;
        for (int i = 0; i < 3; i++) {
            char *end;
            unsigned long v = strtoul(p, &end, 16);
            size_t digits = (size_t)(end - p);
            if (!digits || digits > 4)
                return 0;
            /* normaliza para 8 bits */
            while (digits < 2) {
                v = v * 17;
                digits = 2;
            }
            while (digits > 2) {
                v >>= 4;
                digits--;
            }
            c[i] = (unsigned)v & 0xff;
            p = end + (i < 2 ? 1 : 0);
            if (i < 2 && end[0] != '/')
                return 0;
        }
        *out = c[0] << 16 | c[1] << 8 | c[2];
        return 1;
    }
    for (size_t i = 0; i < sizeof named / sizeof *named; i++)
        if (!strcasecmp(buf, named[i].name)) {
            *out = named[i].rgb;
            return 1;
        }
    return 0;
}

/* Valor de uma chave no plist do GNUstep: texto entre "=" e ";" no nível 0. */
static int
find_value(const char *text, const char *key, const char **start, size_t *len)
{
    size_t kl = strlen(key);
    for (const char *p = text; (p = strstr(p, key)); p += kl) {
        if (p > text && (isalnum((unsigned char)p[-1]) || p[-1] == '_'))
            continue;
        const char *q = p + kl;
        while (isspace((unsigned char)*q))
            q++;
        if (*q != '=')
            continue;
        q++;
        int depth = 0, quote = 0;
        const char *v = q;
        for (; *q; q++) {
            if (*q == '"')
                quote = !quote;
            else if (!quote && *q == '(')
                depth++;
            else if (!quote && *q == ')')
                depth--;
            else if (!quote && depth == 0 && *q == ';')
                break;
        }
        *start = v;
        *len = (size_t)(q - v);
        return 1;
    }
    return 0;
}

/* Cores de um valor: "#fff", (hgradient, c1, c2), (solid, c). Devolve
 * quantas leu (até 2). */
static int
value_colors(const char *v, size_t len, unsigned *c1, unsigned *c2)
{
    int n = 0;
    const char *end = v + len;
    for (const char *p = v; p < end && n < 2;) {
        while (p < end && (isspace((unsigned char)*p) || *p == '(' || *p == ',' || *p == ')'))
            p++;
        if (p >= end)
            break;
        const char *s = p;
        size_t l;
        if (*p == '"') {
            s = ++p;
            while (p < end && *p != '"')
                p++;
            l = (size_t)(p - s);
            if (p < end)
                p++;
        } else {
            while (p < end && !isspace((unsigned char)*p) && *p != ',' && *p != ')')
                p++;
            l = (size_t)(p - s);
        }
        unsigned c;
        if (parse_color(s, l, &c)) {
            if (n == 0)
                *c1 = *c2 = c;
            else
                *c2 = c;
            n++;
        }
    }
    return n;
}

static void
key_colors(const char *text, const char *key, unsigned *c1, unsigned *c2)
{
    const char *v;
    size_t len;
    unsigned a, b;
    if (find_value(text, key, &v, &len) && value_colors(v, len, &a, &b)) {
        *c1 = a;
        if (c2)
            *c2 = b;
    }
}

static void
key_font(const char *text, const char *key, char *out, size_t outlen)
{
    const char *v;
    size_t len;
    if (!find_value(text, key, &v, &len))
        return;
    while (len && isspace((unsigned char)*v)) {
        v++;
        len--;
    }
    if (len < 2 || *v != '"')
        return;
    const char *e = memchr(v + 1, '"', len - 1);
    if (!e)
        return;
    size_t l = (size_t)(e - v - 1);
    if (l == 0 || l >= outlen)
        return;
    memcpy(out, v + 1, l);
    out[l] = 0;
}

void
wz_theme_parse(wz_theme *t, const char *text)
{
    key_colors(text, "MenuTitleBack", &t->title_bg1, &t->title_bg2);
    key_colors(text, "MenuTitleColor", &t->title_fg, NULL);
    key_colors(text, "MenuTextBack", &t->menu_bg1, &t->menu_bg2);
    key_colors(text, "MenuTextColor", &t->menu_fg, NULL);
    key_colors(text, "MenuDisabledColor", &t->disabled_fg, NULL);
    key_colors(text, "HighlightColor", &t->hl_bg, NULL);
    key_colors(text, "HighlightTextColor", &t->hl_fg, NULL);
    key_colors(text, "FTitleBack", &t->ftitle1, &t->ftitle2);
    key_colors(text, "FTitleColor", &t->ftitle_fg, NULL);
    key_colors(text, "IconBack", &t->icon1, &t->icon2);
    key_font(text, "MenuTextFont", t->menu_font, sizeof t->menu_font);
    key_font(text, "MenuTitleFont", t->title_font, sizeof t->title_font);
    t->dark = wz_rgb_luma(t->menu_bg1) < 0.45;
}

static char *
slurp(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    char *buf = wz_xmalloc(1 << 18);
    size_t n = fread(buf, 1, (1 << 18) - 1, f);
    buf[n] = 0;
    fclose(f);
    return buf;
}

int
wz_theme_load(wz_theme *t, const char *path)
{
    wz_theme_defaults(t);
    char *p = NULL;
    if (!path) {
        const char *root = getenv("GNUSTEP_USER_ROOT");
        const char *home = getenv("HOME");
        if (root && *root)
            (void)!asprintf(&p, "%s/Defaults/WindowMaker", root);
        else
            (void)!asprintf(&p, "%s/GNUstep/Defaults/WindowMaker", home ? home : "");
    }
    char *text = slurp(path ? path : p);
    free(p);
    if (!text)
        return -1;
    wz_theme_parse(t, text);
    free(text);
    /* A escolha explícita do big-wm-theme vence a heurística. */
    const char *x = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char *mode = NULL;
    if (x && *x == '/')
        (void)!asprintf(&mode, "%s/big-wm/theme", x);
    else
        (void)!asprintf(&mode, "%s/.config/big-wm/theme", home ? home : "");
    char *m = slurp(mode);
    free(mode);
    if (m) {
        if (!strncmp(m, "dark", 4))
            t->dark = 1;
        else if (!strncmp(m, "light", 5))
            t->dark = 0;
        free(m);
    }
    return 0;
}

unsigned
wz_rgb_mix(unsigned a, unsigned b, int t)
{
    unsigned r = 0;
    for (int s = 0; s <= 16; s += 8) {
        int ca = (int)(a >> s & 0xff), cb = (int)(b >> s & 0xff);
        r |= (unsigned)((ca * (255 - t) + cb * t) / 255) << s;
    }
    return r;
}

double
wz_rgb_luma(unsigned c)
{
    return (0.2126 * (c >> 16 & 0xff) + 0.7152 * (c >> 8 & 0xff) + 0.0722 * (c & 0xff)) / 255.0;
}
