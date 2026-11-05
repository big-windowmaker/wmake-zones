# Janela GTK 3 mínima e redimensionável, para os testes de encaixe.
#
#   janela.py [TÍTULO [LxA+X+Y [MÍNIMO_LxA]]]
#
# A geometria vira USPosition/USSize (o Window Maker não pede um clique para
# posicionar); o mínimo vira PMinSize, para testar janelas que não cabem.
import sys
import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk
w = Gtk.Window(title=sys.argv[1] if len(sys.argv) > 1 else "teste")
w.set_default_size(300, 200)
w.add(Gtk.Label(label="wmake-zones"))
if len(sys.argv) > 3:
    mw, mh = (int(v) for v in sys.argv[3].split("x"))
    w.set_size_request(mw, mh)
if len(sys.argv) > 2:
    w.parse_geometry(sys.argv[2])
w.connect("destroy", Gtk.main_quit)
w.show_all()
Gtk.main()
