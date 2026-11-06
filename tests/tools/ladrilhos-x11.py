# Coluna de ladrilhos 64x64 override-redirect, como o Dock do Window Maker,
# para os testes de área útil. Fica no ar até ser encerrada.
#
#   ladrilhos-x11.py X Y QUANTOS
import sys
import time
from Xlib import X, display

x, y, n = (int(a) for a in sys.argv[1:4])
d = display.Display()
s = d.screen()
for i in range(n):
    w = s.root.create_window(x, y + 64 * i, 64, 64, 0, s.root_depth, X.InputOutput, X.CopyFromParent,
                             background_pixel=s.white_pixel, override_redirect=True)
    w.map()
d.flush()
while True:
    time.sleep(3600)
