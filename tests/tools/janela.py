# Janela GTK 3 mínima e redimensionável, para os testes de encaixe.
#
#   janela.py [TÍTULO [LxA+X+Y [MÍNIMO_LxA]]]
#   janela.py --dialogo TÍTULO      janela principal e um diálogo (TÍTULO)
#                                   filho dela, que fica fora do mosaico
#
# A geometria vira USPosition/USSize (o Window Maker não pede um clique para
# posicionar); o mínimo vira PMinSize, para testar janelas que não cabem.
# O app_id (Wayland) e a classe (X11) são "wmake-zones-teste".
import sys
import gi
gi.require_version('Gtk', '3.0')
from gi.repository import GLib, Gtk
GLib.set_prgname("wmake-zones-teste")
args = sys.argv[1:]
dialogo = bool(args) and args[0] == "--dialogo"
if dialogo:
    args = args[1:]
titulo = args[0] if args else "teste"
w = Gtk.Window(title=(titulo + "-pai") if dialogo else titulo)
w.set_default_size(300, 200)
w.add(Gtk.Label(label="wmake-zones"))
if len(args) > 2:
    mw, mh = (int(v) for v in args[2].split("x"))
    w.set_size_request(mw, mh)
if len(args) > 1:
    w.parse_geometry(args[1])
w.connect("destroy", Gtk.main_quit)
w.show_all()


def abre_dialogo():
    # Depois da principal mapeada: só então o GTK diz ao compositor quem é
    # o pai (xdg_toplevel.set_parent; no X11, WM_TRANSIENT_FOR).
    d = Gtk.Window(title=titulo, transient_for=w)
    d.set_default_size(260, 140)
    d.add(Gtk.Label(label="diálogo"))
    d.show_all()
    return False


if dialogo:
    GLib.timeout_add(400, abre_dialogo)
Gtk.main()
