/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Janelas, monitores e área útil no X11 (EWMH/ICCCM, XRandR).
 *
 * O Window Maker publica só um _NET_WORKAREA (a união das cabeças) e não
 * reserva o Dock nem o Clip para clientes externos, então a área útil de
 * cada monitor é calculada aqui: monitor do XRandR, menos os struts
 * (_NET_WM_STRUT_PARTIAL, como o Dock com abas), menos a coluna de ladrilhos
 * do Dock do Window Maker encostada na borda.
 */
#define _GNU_SOURCE
#include "wzx.h"

#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const atom_names[A_COUNT] = {
    "_NET_SUPPORTED", "_NET_CLIENT_LIST", "_NET_ACTIVE_WINDOW", "_NET_CURRENT_DESKTOP",
    "_NET_WM_DESKTOP", "_NET_WM_STATE", "_NET_WM_STATE_FULLSCREEN", "_NET_WM_STATE_MAXIMIZED_VERT",
    "_NET_WM_STATE_MAXIMIZED_HORZ", "_NET_WM_STATE_HIDDEN", "_NET_WM_STATE_SHADED",
    "_NET_WM_STATE_STICKY", "_NET_WM_STATE_SKIP_TASKBAR", "_NET_WM_STATE_MODAL", "_NET_WM_STATE_ABOVE",
    "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_NORMAL", "_NET_WM_WINDOW_TYPE_DIALOG",
    "_NET_WM_WINDOW_TYPE_DOCK", "_NET_WM_WINDOW_TYPE_DESKTOP", "_NET_WM_WINDOW_TYPE_UTILITY",
    "_NET_WM_WINDOW_TYPE_SPLASH", "_NET_WM_WINDOW_TYPE_TOOLBAR", "_NET_WM_WINDOW_TYPE_MENU",
    "_NET_WM_WINDOW_TYPE_NOTIFICATION", "_NET_FRAME_EXTENTS", "_NET_WM_STRUT", "_NET_WM_STRUT_PARTIAL",
    "_NET_WM_NAME", "_NET_WM_PID", "_NET_WM_CM_S0", "UTF8_STRING", "WM_STATE", "WM_DELETE_WINDOW",
    "WM_PROTOCOLS", "_WMAKE_ZONES_RESTORE", "_WMAKE_ZONES_ZONE", "_WMAKE_ZONES_STATE",
};

static int
get_cardinals(wzx *z, Window w, Atom prop, Atom type, long *out, int max)
{
    Atom rt;
    int fmt;
    unsigned long n, after;
    unsigned char *data = NULL;
    if (XGetWindowProperty(z->dpy, w, prop, 0, max, False, type, &rt, &fmt, &n, &after, &data) != Success || !data)
        return -1;
    int c = 0;
    if (fmt == 32 && rt == type) {
        long *l = (long *)data;
        for (unsigned long i = 0; i < n && c < max; i++)
            out[c++] = l[i];
    }
    XFree(data);
    return c;
}

void
wzx_wm_init(wzx *z)
{
    char name[32];
    snprintf(name, sizeof name, "_NET_WM_CM_S%d", z->screen);
    const char *names[A_COUNT];
    memcpy(names, atom_names, sizeof names);
    names[A_NET_WM_CM] = name;
    XInternAtoms(z->dpy, (char **)names, A_COUNT, False, z->atom);
    long d;
    z->current_desktop = get_cardinals(z, z->root, z->atom[A_NET_CURRENT_DESKTOP], XA_CARDINAL, &d, 1) == 1 ? d : 0;
    long a;
    z->active = get_cardinals(z, z->root, z->atom[A_NET_ACTIVE_WINDOW], XA_WINDOW, &a, 1) == 1 ? (Window)a : None;

    /* Janelas override-redirect já mapeadas (ladrilhos do Dock). Uma vez
     * só; depois, os eventos de estrutura mantêm a lista. */
    Window r, p, *kids = NULL;
    unsigned n = 0;
    if (XQueryTree(z->dpy, z->root, &r, &p, &kids, &n)) {
        for (unsigned i = 0; i < n; i++) {
            XWindowAttributes wa;
            if (XGetWindowAttributes(z->dpy, kids[i], &wa) && wa.override_redirect && wa.map_state == IsViewable)
                wzx_or_event(z, kids[i], wa.x, wa.y, wa.width + 2 * wa.border_width,
                             wa.height + 2 * wa.border_width, 1, 0);
        }
        XFree(kids);
    }
}

int
wzx_compositing(wzx *z)
{
    return XGetSelectionOwner(z->dpy, z->atom[A_NET_WM_CM]) != None;
}

/* ---- monitores ---------------------------------------------------------- */

void
wzx_update_monitors(wzx *z)
{
    z->nmon = 0;
    if (z->has_randr) {
        int n = 0;
        XRRMonitorInfo *m = XRRGetMonitors(z->dpy, z->root, True, &n);
        for (int i = 0; m && i < n && z->nmon < WZX_MAX_MON; i++) {
            wzx_monitor *o = &z->mon[z->nmon++];
            memset(o, 0, sizeof *o);
            char *nm = m[i].name ? XGetAtomName(z->dpy, m[i].name) : NULL;
            snprintf(o->name, sizeof o->name, "%s", nm ? nm : "?");
            if (nm)
                XFree(nm);
            o->geom = (wz_rect){ m[i].x, m[i].y, m[i].width, m[i].height };
            o->primary = m[i].primary;
        }
        if (m)
            XRRFreeMonitors(m);
    }
    if (z->nmon == 0) {
        wzx_monitor *o = &z->mon[z->nmon++];
        memset(o, 0, sizeof *o);
        snprintf(o->name, sizeof o->name, "default");
        o->geom = (wz_rect){ 0, 0, DisplayWidth(z->dpy, z->screen), DisplayHeight(z->dpy, z->screen) };
        o->primary = 1;
    }
    z->work_dirty = 1;
}

int
wzx_monitor_at(wzx *z, int x, int y)
{
    for (int i = 0; i < z->nmon; i++)
        if (wz_rect_contains(z->mon[i].geom, x, y))
            return i;
    /* fora de todos: o mais perto */
    int best = 0;
    long bd = LONG_MAX;
    for (int i = 0; i < z->nmon; i++) {
        wz_rect g = z->mon[i].geom;
        long dx = x < g.x ? g.x - x : x >= g.x + g.w ? x - g.x - g.w : 0;
        long dy = y < g.y ? g.y - y : y >= g.y + g.h ? y - g.y - g.h : 0;
        if (dx * dx + dy * dy < bd) {
            bd = dx * dx + dy * dy;
            best = i;
        }
    }
    return best;
}

int
wzx_monitor_of(wzx *z, wz_rect r)
{
    int best = 0;
    long ba = -1;
    for (int i = 0; i < z->nmon; i++) {
        long a = wz_rect_overlap_area(z->mon[i].geom, r);
        if (a > ba) {
            ba = a;
            best = i;
        }
    }
    if (ba <= 0)
        return wzx_monitor_at(z, r.x + r.w / 2, r.y + r.h / 2);
    return best;
}

/* Tira de m o retângulo r encostado numa borda (o lado com menos perda). */
static void
cut(wz_rect *m, wz_rect r)
{
    if (!wz_rect_intersects(*m, r))
        return;
    int x1 = m->x, y1 = m->y, x2 = m->x + m->w, y2 = m->y + m->h;
    int rx2 = r.x + r.w, ry2 = r.y + r.h;
    /* Quanto cada corte tiraria. */
    long left = rx2 > x1 && r.x <= x1 ? (long)(rx2 - x1) * m->h : LONG_MAX;
    long right = r.x < x2 && rx2 >= x2 ? (long)(x2 - r.x) * m->h : LONG_MAX;
    long top = ry2 > y1 && r.y <= y1 ? (long)(ry2 - y1) * m->w : LONG_MAX;
    long bottom = r.y < y2 && ry2 >= y2 ? (long)(y2 - r.y) * m->w : LONG_MAX;
    long best = left;
    int which = 0;
    if (right < best) {
        best = right;
        which = 1;
    }
    if (top < best) {
        best = top;
        which = 2;
    }
    if (bottom < best) {
        best = bottom;
        which = 3;
    }
    if (best == LONG_MAX)
        return; /* no meio da tela: não é reserva de borda */
    switch (which) {
    case 0: x1 = rx2; break;
    case 1: x2 = r.x; break;
    case 2: y1 = ry2; break;
    default: y2 = r.y; break;
    }
    if (x2 - x1 < 64 || y2 - y1 < 64)
        return; /* não deixa a área sumir */
    *m = (wz_rect){ x1, y1, x2 - x1, y2 - y1 };
}

void
wzx_or_event(wzx *z, Window w, int x, int y, int wd, int ht, int mapped, int destroyed)
{
    int i;
    for (i = 0; i < z->nor; i++)
        if (z->ors[i].w == w)
            break;
    if (destroyed || !mapped) {
        if (i < z->nor) {
            int was = z->ors[i].mapped;
            z->ors[i] = z->ors[--z->nor];
            if (was)
                z->work_dirty = 1;
        }
        return;
    }
    if (w == z->ov.win || w == z->popup)
        return;
    /* Só interessam quadrados do tamanho de ladrilhos (Dock, Clip). */
    if (wd != ht || wd < 32 || wd > 160) {
        if (i < z->nor)
            z->ors[i] = z->ors[--z->nor];
        return;
    }
    if (i == z->nor) {
        if (z->nor >= WZX_MAX_OR)
            return;
        z->nor++;
    }
    z->ors[i] = (wzx_or){ w, { x, y, wd, ht }, 1 };
    z->work_dirty = 1;
}

/* Coluna do Dock: dois ou mais ladrilhos contíguos, um embaixo do outro,
 * encostados na borda esquerda ou direita do monitor. O Clip sozinho num
 * canto e os ícones de aplicativos soltos (área de ícones) não contam: o
 * maximizar do próprio Window Maker também não os evita. */
static int
by_y(const void *a, const void *b)
{
    const wz_rect *x = a, *y = b;
    return x->y - y->y;
}

static void
cut_dock(wzx *z, wzx_monitor *m)
{
    for (int side = 0; side < 2; side++) {
        wz_rect t[WZX_MAX_OR];
        int n = 0;
        for (int i = 0; i < z->nor; i++) {
            wz_rect r = z->ors[i].r;
            if (!z->ors[i].mapped || !wz_rect_intersects(r, m->geom))
                continue;
            int touches = side == 0 ? abs(r.x - m->geom.x) <= 2 : abs(r.x + r.w - (m->geom.x + m->geom.w)) <= 2;
            if (touches && !wzx_find_frame(z, z->ors[i].w))
                t[n++] = r;
        }
        qsort(t, (size_t)n, sizeof *t, by_y);
        int best = 0, run = 1, width = 0;
        for (int i = 1; i < n; i++) {
            run = abs(t[i].y - (t[i - 1].y + t[i - 1].h)) <= 2 ? run + 1 : 1;
            if (run > best)
                best = run;
        }
        if (best < 2)
            continue;
        for (int i = 0; i < n; i++)
            if (t[i].w > width)
                width = t[i].w;
        wz_rect col = { side == 0 ? m->geom.x : m->geom.x + m->geom.w - width, m->geom.y, width, m->geom.h };
        cut(&m->work, col);
    }
}

void
wzx_update_workarea(wzx *z)
{
    if (!z->work_dirty)
        return;
    z->work_dirty = 0;
    int sw = DisplayWidth(z->dpy, z->screen), sh = DisplayHeight(z->dpy, z->screen);
    for (int i = 0; i < z->nmon; i++)
        z->mon[i].work = z->mon[i].geom;

    /* Struts de todos os clientes (o Window Maker publica as janelas do tipo
     * DOCK na _NET_CLIENT_LIST). */
    long *list = NULL;
    Atom rt;
    int fmt;
    unsigned long n = 0, after;
    unsigned char *data = NULL;
    if (XGetWindowProperty(z->dpy, z->root, z->atom[A_NET_CLIENT_LIST], 0, 4096, False, XA_WINDOW, &rt, &fmt, &n,
                           &after, &data) == Success && data)
        list = (long *)data;
    for (unsigned long k = 0; list && k < n; k++) {
        long s[12];
        int c = get_cardinals(z, (Window)list[k], z->atom[A_NET_WM_STRUT_PARTIAL], XA_CARDINAL, s, 12);
        if (c < 12) {
            c = get_cardinals(z, (Window)list[k], z->atom[A_NET_WM_STRUT], XA_CARDINAL, s, 4);
            if (c < 4)
                continue;
            s[4] = 0;
            s[5] = sh - 1;
            s[6] = 0;
            s[7] = sh - 1;
            s[8] = 0;
            s[9] = sw - 1;
            s[10] = 0;
            s[11] = sw - 1;
        }
        wz_rect rr[4] = {
            { 0, (int)s[4], (int)s[0], (int)(s[5] - s[4] + 1) },            /* esquerda */
            { sw - (int)s[1], (int)s[6], (int)s[1], (int)(s[7] - s[6] + 1) }, /* direita */
            { (int)s[8], 0, (int)(s[9] - s[8] + 1), (int)s[2] },            /* cima */
            { (int)s[10], sh - (int)s[3], (int)(s[11] - s[10] + 1), (int)s[3] }, /* baixo */
        };
        for (int e = 0; e < 4; e++) {
            if (rr[e].w <= 0 || rr[e].h <= 0)
                continue;
            for (int i = 0; i < z->nmon; i++)
                cut(&z->mon[i].work, rr[e]);
        }
    }
    if (data)
        XFree(data);
    if (z->cfg.avoid_dock)
        for (int i = 0; i < z->nmon; i++)
            cut_dock(z, &z->mon[i]);
}

/* ---- janelas ------------------------------------------------------------ */

wzx_win *
wzx_find(wzx *z, Window w)
{
    for (int i = 0; i < z->nwin; i++)
        if (z->win[i]->client == w)
            return z->win[i];
    return NULL;
}

wzx_win *
wzx_find_frame(wzx *z, Window frame)
{
    for (int i = 0; i < z->nwin; i++)
        if (z->win[i]->frame == frame)
            return z->win[i];
    return NULL;
}

static Window
frame_of(wzx *z, Window w)
{
    Window r, p, *kids;
    unsigned n;
    for (int guard = 0; guard < 16; guard++) {
        if (!XQueryTree(z->dpy, w, &r, &p, &kids, &n))
            return None;
        if (kids)
            XFree(kids);
        if (p == z->root || p == None)
            return w;
        w = p;
    }
    return w;
}

void
wzx_refresh_geom(wzx *z, wzx_win *w)
{
    Window r;
    int x, y;
    unsigned wd, ht, bw, d;
    if (w->frame && XGetGeometry(z->dpy, w->frame, &r, &x, &y, &wd, &ht, &bw, &d))
        w->outer = (wz_rect){ x, y, (int)(wd + 2 * bw), (int)(ht + 2 * bw) };
}

static char *
get_string(wzx *z, Window w, Atom prop, Atom type)
{
    Atom rt;
    int fmt;
    unsigned long n, after;
    unsigned char *data = NULL;
    if (XGetWindowProperty(z->dpy, w, prop, 0, 256, False, type, &rt, &fmt, &n, &after, &data) != Success || !data)
        return NULL;
    char *s = fmt == 8 ? strndup((char *)data, n) : NULL;
    XFree(data);
    return s;
}

const char *
wzx_title(wzx *z, wzx_win *w, char *buf, size_t len)
{
    char *s = get_string(z, w->client, z->atom[A_NET_WM_NAME], z->atom[A_UTF8_STRING]);
    if (!s) {
        char *t = NULL;
        if (XFetchName(z->dpy, w->client, &t) && t) {
            s = strdup(t);
            XFree(t);
        }
    }
    snprintf(buf, len, "%s", s ? s : "");
    free(s);
    return buf;
}

void
wzx_refresh_win(wzx *z, wzx_win *w)
{
    /* Tipo e transitoriedade. */
    long types[16];
    int nt = get_cardinals(z, w->client, z->atom[A_NET_WM_WINDOW_TYPE], XA_ATOM, types, 16);
    w->special = 0;
    for (int i = 0; i < nt; i++) {
        Atom a = (Atom)types[i];
        if (a == z->atom[A_NET_WM_WINDOW_TYPE_NORMAL])
            break;
        if (a == z->atom[A_NET_WM_WINDOW_TYPE_DIALOG] || a == z->atom[A_NET_WM_WINDOW_TYPE_DOCK] ||
            a == z->atom[A_NET_WM_WINDOW_TYPE_DESKTOP] || a == z->atom[A_NET_WM_WINDOW_TYPE_UTILITY] ||
            a == z->atom[A_NET_WM_WINDOW_TYPE_SPLASH] || a == z->atom[A_NET_WM_WINDOW_TYPE_TOOLBAR] ||
            a == z->atom[A_NET_WM_WINDOW_TYPE_MENU] || a == z->atom[A_NET_WM_WINDOW_TYPE_NOTIFICATION]) {
            w->special = 1;
            break;
        }
    }
    Window tf = None;
    w->transient = XGetTransientForHint(z->dpy, w->client, &tf) && tf != None;

    /* Limites do cliente. */
    XSizeHints sh;
    long supplied;
    memset(&w->hints, 0, sizeof w->hints);
    w->fixed = 0;
    w->gravity = NorthWestGravity;
    if (XGetWMNormalHints(z->dpy, w->client, &sh, &supplied)) {
        if (sh.flags & PMinSize) {
            w->hints.min_w = sh.min_width;
            w->hints.min_h = sh.min_height;
        }
        if (sh.flags & PMaxSize) {
            w->hints.max_w = sh.max_width;
            w->hints.max_h = sh.max_height;
        }
        if ((sh.flags & PMinSize) && (sh.flags & PMaxSize) && sh.min_width == sh.max_width &&
            sh.min_height == sh.max_height && sh.min_width > 0)
            w->fixed = 1;
        if (sh.flags & PWinGravity)
            w->gravity = sh.win_gravity;
    }

    /* Classe. */
    XClassHint ch = { 0 };
    free(w->wm_class);
    free(w->instance);
    w->wm_class = w->instance = NULL;
    if (XGetClassHint(z->dpy, w->client, &ch)) {
        w->instance = ch.res_name ? strdup(ch.res_name) : NULL;
        w->wm_class = ch.res_class ? strdup(ch.res_class) : NULL;
        if (ch.res_name)
            XFree(ch.res_name);
        if (ch.res_class)
            XFree(ch.res_class);
    }

    /* Bordas do quadro (o Window Maker publica sempre). */
    long ext[4];
    if (get_cardinals(z, w->client, z->atom[A_NET_FRAME_EXTENTS], XA_CARDINAL, ext, 4) == 4)
        for (int i = 0; i < 4; i++)
            w->ext[i] = (int)ext[i];

    long d;
    w->desktop = get_cardinals(z, w->client, z->atom[A_NET_WM_DESKTOP], XA_CARDINAL, &d, 1) == 1 ? d : 0;
    if (w->desktop == 0xFFFFFFFFL)
        w->desktop = -1;

    long st[32];
    int ns = get_cardinals(z, w->client, z->atom[A_NET_WM_STATE], XA_ATOM, st, 32);
    w->state = 0;
    for (int i = 0; i < ns; i++) {
        Atom a = (Atom)st[i];
        if (a == z->atom[A_NET_WM_STATE_FULLSCREEN])
            w->state |= WS_FULLSCREEN;
        else if (a == z->atom[A_NET_WM_STATE_MAXIMIZED_VERT])
            w->state |= WS_MAXV;
        else if (a == z->atom[A_NET_WM_STATE_MAXIMIZED_HORZ])
            w->state |= WS_MAXH;
        else if (a == z->atom[A_NET_WM_STATE_HIDDEN])
            w->state |= WS_HIDDEN;
        else if (a == z->atom[A_NET_WM_STATE_SHADED])
            w->state |= WS_SHADED;
        else if (a == z->atom[A_NET_WM_STATE_STICKY])
            w->state |= WS_STICKY;
        else if (a == z->atom[A_NET_WM_STATE_SKIP_TASKBAR])
            w->state |= WS_SKIP_TASKBAR;
        else if (a == z->atom[A_NET_WM_STATE_MODAL])
            w->state |= WS_MODAL;
        else if (a == z->atom[A_NET_WM_STATE_ABOVE])
            w->state |= WS_ABOVE;
    }
    if (!w->frame)
        w->frame = frame_of(z, w->client);
    wzx_refresh_geom(z, w);
}

long
wzx_desktop_of(wzx *z, wzx_win *w)
{
    (void)z;
    return w->desktop;
}

static void
win_free(wzx_win *w)
{
    free(w->wm_class);
    free(w->instance);
    free(w);
}

void
wzx_sync_clients(wzx *z)
{
    Atom rt;
    int fmt;
    unsigned long n = 0, after;
    unsigned char *data = NULL;
    if (XGetWindowProperty(z->dpy, z->root, z->atom[A_NET_CLIENT_LIST], 0, 4096, False, XA_WINDOW, &rt, &fmt, &n,
                           &after, &data) != Success)
        return;
    long *list = (long *)data;
    for (int i = 0; i < z->nwin; i++)
        z->win[i]->seen = 0;
    int changed = 0;
    for (unsigned long k = 0; data && k < n; k++) {
        Window c = (Window)list[k];
        wzx_win *w = wzx_find(z, c);
        if (!w) {
            if (z->nwin >= WZX_MAX_WIN)
                continue;
            w = calloc(1, sizeof *w);
            w->client = c;
            w->tile_mon = -1;
            w->order = ++z->order_seq;
            w->mapped = 1;
            XSelectInput(z->dpy, c, PropertyChangeMask | StructureNotifyMask);
            wzx_refresh_win(z, w);
            z->win[z->nwin++] = w;
            changed = 1;
        }
        w->seen = 1;
    }
    if (data)
        XFree(data);
    for (int i = 0; i < z->nwin; i++) {
        if (z->win[i]->seen)
            continue;
        wzx_win *w = z->win[i];
        if (z->drag_w == w) {
            z->drag_w = NULL;
            z->drag = DRAG_IDLE;
        }
        wzx_tile_forget(z, w);
        win_free(w);
        z->win[i] = z->win[--z->nwin];
        i--;
        changed = 1;
    }
    if (changed) {
        z->work_dirty = 1;
        wzx_tile_mark(z);
    }
}

/* ---- ações ------------------------------------------------------------- */

static void
send_state(wzx *z, wzx_win *w, long action, Atom a1, Atom a2)
{
    XEvent ev = { 0 };
    ev.xclient.type = ClientMessage;
    ev.xclient.window = w->client;
    ev.xclient.message_type = z->atom[A_NET_WM_STATE];
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = action;
    ev.xclient.data.l[1] = (long)a1;
    ev.xclient.data.l[2] = (long)a2;
    ev.xclient.data.l[3] = 2;
    XSendEvent(z->dpy, z->root, False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
}

/* Coloca o quadro (com bordas) em r. O Window Maker não trata
 * _NET_MOVERESIZE_WINDOW; um ConfigureRequest normal funciona: com
 * gravidade NorthWest x/y são a origem do quadro, nas outras o y é o do
 * cliente (wClientConfigure). Largura e altura são do cliente. */
void
wzx_move_outer(wzx *z, wzx_win *w, wz_rect r)
{
    if (w->state & (WS_MAXV | WS_MAXH)) {
        send_state(z, w, 0, z->atom[A_NET_WM_STATE_MAXIMIZED_VERT], z->atom[A_NET_WM_STATE_MAXIMIZED_HORZ]);
        w->state &= ~(unsigned)(WS_MAXV | WS_MAXH);
    }
    if (w->state & WS_SHADED)
        return;
    int cw = r.w - w->ext[0] - w->ext[1];
    int chh = r.h - w->ext[2] - w->ext[3];
    if (cw < 1)
        cw = 1;
    if (chh < 1)
        chh = 1;
    int north = w->gravity == NorthWestGravity || w->gravity == NorthGravity || w->gravity == NorthEastGravity;
    int bw = w->ext[0];
    int y = north ? r.y : r.y + w->ext[2] - bw;
    XMoveResizeWindow(z->dpy, w->client, r.x, y, (unsigned)cw, (unsigned)chh);
    w->expect = r;
    w->expect_valid = 1;
}

void
wzx_set_restore(wzx *z, wzx_win *w, wz_rect r)
{
    long v[4] = { r.x, r.y, r.w, r.h };
    XChangeProperty(z->dpy, w->client, z->atom[A_WZ_RESTORE], XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)v, 4);
}

int
wzx_get_restore(wzx *z, wzx_win *w, wz_rect *r)
{
    long v[4];
    if (get_cardinals(z, w->client, z->atom[A_WZ_RESTORE], XA_CARDINAL, v, 4) != 4)
        return 0;
    /* CARDINAL não tem sinal: posições negativas voltam como 2^32 - n. */
    *r = (wz_rect){ (int)(int32_t)v[0], (int)(int32_t)v[1], (int)v[2], (int)v[3] };
    return r->w > 0 && r->h > 0;
}

void
wzx_clear_restore(wzx *z, wzx_win *w)
{
    XDeleteProperty(z->dpy, w->client, z->atom[A_WZ_RESTORE]);
    XDeleteProperty(z->dpy, w->client, z->atom[A_WZ_ZONE]);
}

void
wzx_set_zone_prop(wzx *z, wzx_win *w, long zone)
{
    XChangeProperty(z->dpy, w->client, z->atom[A_WZ_ZONE], XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&zone, 1);
}

long
wzx_get_zone_prop(wzx *z, wzx_win *w)
{
    long v;
    return get_cardinals(z, w->client, z->atom[A_WZ_ZONE], XA_CARDINAL, &v, 1) == 1 ? v : -1;
}

/* Janelas que podem ser encaixadas numa zona. */
int
wzx_snappable(wzx *z, wzx_win *w)
{
    (void)z;
    if (!w || !w->frame || w->special || w->fixed)
        return 0;
    if (w->state & (WS_FULLSCREEN | WS_SHADED | WS_HIDDEN))
        return 0;
    return 1;
}

int
wzx_tileable(wzx *z, wzx_win *w)
{
    if (!wzx_snappable(z, w) || w->transient || !w->mapped)
        return 0;
    if (w->state & (WS_STICKY | WS_SKIP_TASKBAR | WS_MODAL | WS_ABOVE))
        return 0;
    if (w->user_float || w->rule_float)
        return 0;
    if (w->desktop != z->current_desktop)
        return 0;
    return 1;
}

void
wzx_activate(wzx *z, wzx_win *w)
{
    XEvent ev = { 0 };
    ev.xclient.type = ClientMessage;
    ev.xclient.window = w->client;
    ev.xclient.message_type = z->atom[A_NET_ACTIVE_WINDOW];
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = 2; /* pedido do usuário (pager) */
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(z->dpy, z->root, False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
}

/* Zonas do monitor (layout efetivo do monitor/área de trabalho). */
int
wzx_zones_for(wzx *z, int mon, wz_rect *out, char **names, int max)
{
    wzx_update_workarea(z);
    const wz_layout_def *l = wz_config_layout_for(&z->cfg, z->mon[mon].name, (int)z->current_desktop + 1);
    if (!l)
        return 0;
    wz_node *t = wz_parse(l->tree, NULL);
    if (!t)
        return 0;
    int n = wz_compute(t, z->mon[mon].work, z->cfg.gap_in, z->cfg.gap_out, out, max);
    if (n > max)
        n = max;
    if (names)
        for (int i = 0; i < n; i++) {
            wz_node *leaf = wz_leaf_nth(t, i);
            names[i] = leaf && leaf->name ? strdup(leaf->name) : NULL;
        }
    wz_free(t);
    return n;
}

/* O encaixe nas bordas do próprio Window Maker (WindowSnapping) mostraria
 * uma segunda prévia e maximizaria pela metade ao soltar. Com as zonas
 * ligadas ele é desligado, e o valor do usuário volta quando elas desligam.
 * A marca fica no arquivo de configuração ([State]), para sobreviver a um
 * crash ou ao fim da sessão; "wmake-zones --reset" também restaura. */
static void
saved_cb(const char *sec, const char *k, const char *v, void *ud)
{
    if (!strcmp(sec, "State") && !strcmp(k, "RestoreWindowSnapping"))
        *(int *)ud = !strcmp(v, "true");
}

int
wzx_snapping_marked(const char *cfg_path)
{
    int saved = 0;
    wz_ini_parse_file(cfg_path, saved_cb, &saved);
    return saved;
}

void
wzx_wm_snapping(wzx *z, int disable)
{
    int saved = wzx_snapping_marked(z->cfg_path);
    if (disable) {
        if (saved)
            return; /* já desligado por nós */
        FILE *p = popen("wdread WindowMaker WindowSnapping 2>/dev/null", "r");
        char buf[32] = "";
        if (p) {
            if (!fgets(buf, sizeof buf, p))
                buf[0] = 0;
            pclose(p);
        }
        if (strncasecmp(buf, "YES", 3) != 0)
            return;
        wz_ini_set(z->cfg_path, "State", "RestoreWindowSnapping", "true");
        if (system("wdwrite WindowMaker WindowSnapping NO >/dev/null 2>&1") != 0)
            wzx_log("não consegui desligar o WindowSnapping do Window Maker");
        else
            wzx_log("WindowSnapping do Window Maker desligado enquanto as zonas estão ativas");
    } else {
        if (!saved)
            return;
        if (system("wdwrite WindowMaker WindowSnapping YES >/dev/null 2>&1") == 0) {
            wz_ini_set(z->cfg_path, "State", "RestoreWindowSnapping", NULL);
            wzx_log("WindowSnapping do Window Maker restaurado");
        }
    }
}
