# Painel layer-shell com zona exclusiva na borda direita (como o Dock), para
# o teste de área útil no Wayland. Fica no ar até ser encerrado.
#
#   painel-wl.py LARGURA
import sys
import gi
gi.require_version('Gtk', '3.0')
gi.require_version('GtkLayerShell', '0.1')
from gi.repository import Gtk, GtkLayerShell

w = Gtk.Window()
GtkLayerShell.init_for_window(w)
GtkLayerShell.set_layer(w, GtkLayerShell.Layer.TOP)
for edge in (GtkLayerShell.Edge.RIGHT, GtkLayerShell.Edge.TOP, GtkLayerShell.Edge.BOTTOM):
    GtkLayerShell.set_anchor(w, edge, True)
width = int(sys.argv[1]) if len(sys.argv) > 1 else 64
w.set_size_request(width, 64)
GtkLayerShell.set_exclusive_zone(w, width)
w.show_all()
Gtk.main()
