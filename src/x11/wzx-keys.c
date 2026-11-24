/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Atalhos de teclado (passive grabs na raiz). Um atalho que outro programa
 * já agarrou (BadAccess: o Window Maker, o menu raiz, outro daemon) não é
 * tomado: fica marcado como conflito e aparece em "wmake-zones status" e
 * nas preferências.
 */
#define _GNU_SOURCE
#include "wzx.h"

#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *const key_cmd[WZ_KEY_N] = {
    [WZ_KEY_PICKER] = "picker",
    [WZ_KEY_TOGGLE_ZONES] = "zones toggle",
    [WZ_KEY_TOGGLE_AUTOTILE] = "autotile toggle",
    [WZ_KEY_EDITOR] = "editor",
    [WZ_KEY_NEXT_LAYOUT] = "layout next",
    [WZ_KEY_MOVE_LEFT] = "move left",
    [WZ_KEY_MOVE_RIGHT] = "move right",
    [WZ_KEY_MOVE_UP] = "move up",
    [WZ_KEY_MOVE_DOWN] = "move down",
    [WZ_KEY_FOCUS_LEFT] = "focus left",
    [WZ_KEY_FOCUS_RIGHT] = "focus right",
    [WZ_KEY_FOCUS_UP] = "focus up",
    [WZ_KEY_FOCUS_DOWN] = "focus down",
    [WZ_KEY_SWAP_LEFT] = "swap left",
    [WZ_KEY_SWAP_RIGHT] = "swap right",
    [WZ_KEY_SWAP_UP] = "swap up",
    [WZ_KEY_SWAP_DOWN] = "swap down",
    [WZ_KEY_RESTORE] = "restore",
    [WZ_KEY_TOGGLE_FLOAT] = "float",
    [WZ_KEY_PROMOTE] = "promote",
    [WZ_KEY_GROW] = "grow",
    [WZ_KEY_SHRINK] = "shrink",
    [WZ_KEY_UNDO] = "undo",
    [WZ_KEY_REDO] = "redo",
    [WZ_KEY_MODE_ZONES] = "mode zones",
    [WZ_KEY_MODE_TILING] = "mode tiling",
    [WZ_KEY_MODE_SCROLL] = "mode scroll",
    [WZ_KEY_SCROLL_FIRST] = "scroll first",
    [WZ_KEY_SCROLL_LAST] = "scroll last",
    [WZ_KEY_SCROLL_CENTER] = "scroll center",
    [WZ_KEY_SCROLL_CONSUME] = "column merge",
    [WZ_KEY_SCROLL_EXPEL] = "column split",
};

int
wzx_parse_key(Display *dpy, const char *spec, unsigned *mods, KeyCode *code)
{
    *mods = 0;
    *code = 0;
    if (!spec || !*spec)
        return 0;
    char buf[128];
    snprintf(buf, sizeof buf, "%s", spec);
    char *save = NULL, *last = NULL;
    for (char *t = strtok_r(buf, "+", &save); t; t = strtok_r(NULL, "+", &save)) {
        if (last) {
            if (!strcasecmp(last, "Super") || !strcasecmp(last, "Mod4") || !strcasecmp(last, "Win"))
                *mods |= Mod4Mask;
            else if (!strcasecmp(last, "Alt") || !strcasecmp(last, "Mod1"))
                *mods |= Mod1Mask;
            else if (!strcasecmp(last, "Ctrl") || !strcasecmp(last, "Control"))
                *mods |= ControlMask;
            else if (!strcasecmp(last, "Shift"))
                *mods |= ShiftMask;
            else
                return 0;
        }
        last = t;
    }
    if (!last)
        return 0;
    KeySym ks = XStringToKeysym(last);
    if (ks == NoSymbol && strlen(last) == 1) {
        char lower[2] = { (char)(last[0] | 0x20), 0 };
        ks = XStringToKeysym(lower);
    }
    if (ks == NoSymbol)
        return 0;
    *code = XKeysymToKeycode(dpy, ks);
    return *code != 0;
}

static int grab_error;

static int
grab_handler(Display *d, XErrorEvent *e)
{
    (void)d;
    if (e->error_code == BadAccess)
        grab_error = 1;
    return 0;
}

/* NumLock e CapsLock não podem impedir o atalho. */
static const unsigned ignore[] = { 0, LockMask, Mod2Mask, LockMask | Mod2Mask };

void
wzx_keys_ungrab(wzx *z)
{
    for (int i = 0; i < WZ_KEY_N; i++) {
        if (!z->keys[i].grabbed)
            continue;
        for (size_t k = 0; k < sizeof ignore / sizeof *ignore; k++)
            XUngrabKey(z->dpy, z->keys[i].code, z->keys[i].mods | ignore[k], z->root);
        z->keys[i].grabbed = 0;
    }
}

void
wzx_keys_grab(wzx *z)
{
    wzx_keys_ungrab(z);
    XSync(z->dpy, False);
    int (*old)(Display *, XErrorEvent *) = XSetErrorHandler(grab_handler);
    for (int i = 0; i < WZ_KEY_N; i++) {
        z->keys[i].conflict = 0;
        if (!wzx_parse_key(z->dpy, z->cfg.keys[i], &z->keys[i].mods, &z->keys[i].code))
            continue;
        /* O mesmo atalho em duas ações: vale a primeira. */
        int dup = 0;
        for (int j = 0; j < i; j++)
            dup |= z->keys[j].grabbed && z->keys[j].code == z->keys[i].code && z->keys[j].mods == z->keys[i].mods;
        if (dup) {
            z->keys[i].conflict = 1;
            continue;
        }
        grab_error = 0;
        for (size_t k = 0; k < sizeof ignore / sizeof *ignore; k++)
            XGrabKey(z->dpy, z->keys[i].code, z->keys[i].mods | ignore[k], z->root, False, GrabModeAsync,
                     GrabModeAsync);
        XSync(z->dpy, False);
        if (grab_error) {
            for (size_t k = 0; k < sizeof ignore / sizeof *ignore; k++)
                XUngrabKey(z->dpy, z->keys[i].code, z->keys[i].mods | ignore[k], z->root);
            z->keys[i].conflict = 1;
            wzx_log("atalho %s (%s) já está em uso por outro programa", z->cfg.keys[i], wz_key_name(i));
        } else {
            z->keys[i].grabbed = 1;
        }
    }
    XSync(z->dpy, False);
    XSetErrorHandler(old);
}

int
wzx_keys_event(wzx *z, XKeyEvent *e)
{
    unsigned state = e->state & (ShiftMask | ControlMask | Mod1Mask | Mod4Mask);
    for (int i = 0; i < WZ_KEY_N; i++)
        if (z->keys[i].grabbed && z->keys[i].code == e->keycode && z->keys[i].mods == state) {
            wzx_command(z, key_cmd[i], NULL, 0);
            return 1;
        }
    return 0;
}

/* ---- mover e redimensionar com o modificador (Ctrl+Alt) -------------- */

static unsigned
parse_mods(const char *spec)
{
    unsigned m = 0;
    if (!spec)
        return 0;
    char buf[64];
    snprintf(buf, sizeof buf, "%s", spec);
    for (char *t = strtok(buf, "+ "); t; t = strtok(NULL, "+ ")) {
        if (!strcasecmp(t, "Ctrl") || !strcasecmp(t, "Control"))
            m |= ControlMask;
        else if (!strcasecmp(t, "Alt") || !strcasecmp(t, "Mod1"))
            m |= Mod1Mask;
        else if (!strcasecmp(t, "Super") || !strcasecmp(t, "Mod4") || !strcasecmp(t, "Win"))
            m |= Mod4Mask;
        else if (!strcasecmp(t, "Shift"))
            m |= ShiftMask;
        else
            return 0; /* desconhecido: não pega nada por engano */
    }
    return m;
}

static unsigned drag_mods;

void
wzx_moddrag_ungrab(wzx *z)
{
    if (!drag_mods)
        return;
    for (size_t k = 0; k < sizeof ignore / sizeof *ignore; k++) {
        XUngrabButton(z->dpy, Button1, drag_mods | ignore[k], z->root);
        XUngrabButton(z->dpy, Button3, drag_mods | ignore[k], z->root);
    }
    drag_mods = 0;
}

void
wzx_moddrag_grab(wzx *z)
{
    wzx_moddrag_ungrab(z);
    unsigned m = parse_mods(z->cfg.drag_modifier);
    if (!m)
        return;
    /* O movimento é do Window Maker (_NET_WM_MOVERESIZE, no git dele desde
     * 2026-02; a 0.96.0 não tem). Sem isso, pegar o clique só o engoliria:
     * fica sem a grab e o clique vai para o aplicativo. Conferido de novo
     * quando o _NET_SUPPORTED muda (o Window Maker pode subir depois). */
    static int warned;
    if (!wzx_wm_supports(z, z->atom[A_NET_WM_MOVERESIZE])) {
        if (!warned++)
            wzx_log("o gerenciador de janelas não anuncia _NET_WM_MOVERESIZE: %s+arrastar fica desligado",
                    z->cfg.drag_modifier);
        return;
    }
    /* Na raiz: a grab passiva mais externa vence, então o clique nunca chega
     * ao aplicativo (não seleciona texto, não aperta botões). Sem o
     * modificador, nada muda. */
    XSync(z->dpy, False);
    int (*old)(Display *, XErrorEvent *) = XSetErrorHandler(grab_handler);
    grab_error = 0;
    for (size_t k = 0; k < sizeof ignore / sizeof *ignore; k++) {
        XGrabButton(z->dpy, Button1, m | ignore[k], z->root, False, ButtonPressMask | ButtonReleaseMask,
                    GrabModeAsync, GrabModeAsync, None, None);
        XGrabButton(z->dpy, Button3, m | ignore[k], z->root, False, ButtonPressMask | ButtonReleaseMask,
                    GrabModeAsync, GrabModeAsync, None, None);
    }
    XSync(z->dpy, False);
    XSetErrorHandler(old);
    if (grab_error) {
        wzx_log("%s+clique já está em uso por outro programa", z->cfg.drag_modifier);
        for (size_t k = 0; k < sizeof ignore / sizeof *ignore; k++) {
            XUngrabButton(z->dpy, Button1, m | ignore[k], z->root);
            XUngrabButton(z->dpy, Button3, m | ignore[k], z->root);
        }
        return;
    }
    drag_mods = m;
}

/* _NET_WM_MOVERESIZE */
enum { MR_TOPLEFT = 0, MR_TOP, MR_TOPRIGHT, MR_RIGHT, MR_BOTTOMRIGHT, MR_BOTTOM, MR_BOTTOMLEFT, MR_LEFT, MR_MOVE };

int
wzx_moddrag_event(wzx *z, XEvent *e)
{
    if (!drag_mods || (e->type != ButtonPress && e->type != ButtonRelease) || e->xbutton.window != z->root)
        return 0;
    XButtonEvent *b = &e->xbutton;
    if ((b->state & (ShiftMask | ControlMask | Mod1Mask | Mod4Mask)) != drag_mods ||
        (b->button != Button1 && b->button != Button3))
        return 0;
    if (e->type == ButtonRelease)
        return 1;
    /* Solta a grab antes: o Window Maker pega o ponteiro para o movimento. */
    XUngrabPointer(z->dpy, b->time);
    wzx_win *w = b->subwindow ? wzx_find_frame(z, b->subwindow) : NULL;
    if (!w || w->special || (w->state & WS_FULLSCREEN))
        return 1;
    long dir = MR_MOVE;
    if (b->button == Button3) {
        if (w->fixed)
            return 1;
        wzx_refresh_geom(z, w);
        int fx = (b->x_root - w->outer.x) * 3 / (w->outer.w > 0 ? w->outer.w : 1);
        int fy = (b->y_root - w->outer.y) * 3 / (w->outer.h > 0 ? w->outer.h : 1);
        static const long map[3][3] = { { MR_TOPLEFT, MR_TOP, MR_TOPRIGHT },
                                        { MR_LEFT, MR_BOTTOMRIGHT, MR_RIGHT },
                                        { MR_BOTTOMLEFT, MR_BOTTOM, MR_BOTTOMRIGHT } };
        dir = map[fy < 0 ? 0 : fy > 2 ? 2 : fy][fx < 0 ? 0 : fx > 2 ? 2 : fx];
    }
    XEvent ev = { 0 };
    ev.xclient.type = ClientMessage;
    ev.xclient.window = w->client;
    ev.xclient.message_type = z->atom[A_NET_WM_MOVERESIZE];
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = b->x_root;
    ev.xclient.data.l[1] = b->y_root;
    ev.xclient.data.l[2] = dir;
    ev.xclient.data.l[3] = (long)b->button;
    ev.xclient.data.l[4] = 1;
    XSendEvent(z->dpy, z->root, False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    XFlush(z->dpy);
    return 1;
}
