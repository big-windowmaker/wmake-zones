# Janela GTK 3 mínima e redimensionável, para os testes de encaixe.
import sys
import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk
w = Gtk.Window(title=sys.argv[1] if len(sys.argv) > 1 else "teste")
w.set_default_size(300, 200)
w.add(Gtk.Label(label="wmake-zones"))
w.connect("destroy", Gtk.main_quit)
w.show_all()
Gtk.main()
