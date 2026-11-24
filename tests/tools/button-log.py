#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Janela de teste que escreve "ButtonPress N" a cada clique que recebe.

    button-log.py NOME LARGURAxALTURA+X+Y
"""
import re
import sys

from Xlib import X, Xutil, display

name, geom = sys.argv[1], sys.argv[2]
w, h, x, y = (int(v) for v in re.match(r"(\d+)x(\d+)\+(-?\d+)\+(-?\d+)", geom).groups())
d = display.Display()
s = d.screen()
win = s.root.create_window(x, y, w, h, 0, s.root_depth, X.InputOutput, X.CopyFromParent,
                           background_pixel=s.white_pixel,
                           event_mask=X.ButtonPressMask | X.ButtonReleaseMask | X.StructureNotifyMask)
win.set_wm_name(name)
win.set_wm_class(name, name)
# Posição dada pelo usuário: sem ela, WindowPlacement=manual espera um clique.
win.set_wm_normal_hints(flags=Xutil.USPosition | Xutil.USSize, x=x, y=y, width=w, height=h)
win.map()
d.flush()
while True:
    e = d.next_event()
    if e.type == X.ButtonPress:
        print(f"ButtonPress {e.detail}", flush=True)
    elif e.type == X.DestroyNotify:
        break
