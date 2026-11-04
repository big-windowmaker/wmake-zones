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
