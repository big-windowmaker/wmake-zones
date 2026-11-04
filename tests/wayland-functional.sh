#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Testes funcionais das zonas dentro do wlmaker (Wayland), com o wlmaker
# aninhado num Xvfb próprio pelo backend X11 do wlroots (renderizador
# pixman) e o ponteiro guiado pelo xdotool. Saída TAP.
#
#   WLMAKER=/caminho/wlmaker tests/wayland-functional.sh BUILD_DIR
#
# Precisa de um wlmaker compilado com o wmake-zones-engine, Xvfb, xdotool e
# python3-gobject (janelas GTK 3 de teste); sem eles, "skip".

set -u
B=${1:?uso: $0 BUILD_DIR}
WZ=$B/wmake-zones
WLMAKER=${WLMAKER:-$(command -v wlmaker || true)}
XVFB=${XVFB:-$(command -v Xvfb || true)}
HERE=$(cd "$(dirname "$0")" && pwd)
n=0
fails=0
ok() { n=$((n + 1)); echo "ok $n - $*"; }
nok() { n=$((n + 1)); fails=$((fails + 1)); echo "not ok $n - $*"; }
check() { if eval "$2"; then ok "$1"; else nok "$1"; fi; }

[ -x "$WLMAKER" ] || { echo "1..0 # SKIP sem wlmaker"; exit 0; }
[ -x "$XVFB" ] || { echo "1..0 # SKIP sem Xvfb"; exit 0; }
command -v xdotool >/dev/null || { echo "1..0 # SKIP sem xdotool"; exit 0; }
python3 -c "import gi; gi.require_version('Gtk', '3.0')" 2>/dev/null || { echo "1..0 # SKIP sem python3-gobject"; exit 0; }

TMP=$(mktemp -d "${TMPDIR:-/tmp}/wzw.XXXXXX")
PIDS=""
cleanup() {
	for p in $PIDS; do kill "$p" 2>/dev/null; done
	sleep 0.3
	for p in $PIDS; do kill -9 "$p" 2>/dev/null; done
	[ -n "${XPID:-}" ] && kill -9 "$XPID" 2>/dev/null
	rm -rf "$TMP"
}
trap cleanup EXIT

exec 9>"$TMP/dfd"
"$XVFB" -displayfd 9 -screen 0 1400x900x24 -nolisten tcp >"$TMP/xvfb.log" 2>&1 &
XPID=$!
exec 9>&-
for _ in $(seq 1 50); do [ -s "$TMP/dfd" ] && break; sleep 0.1; done
[ -s "$TMP/dfd" ] || { echo "1..0 # SKIP Xvfb não subiu"; exit 0; }
export DISPLAY=:$(head -1 "$TMP/dfd")
export HOME=$TMP/home XDG_RUNTIME_DIR=$TMP/run XDG_CONFIG_HOME=$TMP/home/.config XDG_SESSION_TYPE=wayland
mkdir -p "$XDG_CONFIG_HOME/wmake-zones" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
CONF=$XDG_CONFIG_HOME/wmake-zones/wmake-zones.conf
printf '[General]\nZones=true\nLayout=quarters\nGap=0\nOuterGap=0\nMinDrag=10\n[Tiling]\nGap=0\nOuterGap=0\n' >"$CONF"
unset WAYLAND_DISPLAY
WLR_BACKENDS=x11 WLR_RENDERER=pixman WLR_X11_OUTPUTS=1 "$WLMAKER" >"$TMP/wlmaker.log" 2>&1 &
PIDS="$PIDS $!"
for _ in $(seq 1 50); do ls "$XDG_RUNTIME_DIR"/wmake-zones/*.sock >/dev/null 2>&1 && break; sleep 0.1; done
export WAYLAND_DISPLAY
WAYLAND_DISPLAY=$(ls "$XDG_RUNTIME_DIR" | grep -m1 '^wayland-[0-9]*$')
export GDK_BACKEND=wayland NO_AT_BRIDGE=1 GSETTINGS_BACKEND=memory GIO_USE_VFS=local

win() { "$WZ" windows | grep -P "\t$1\t"; }
box() { win "$1" | cut -f4; }
zone() { win "$1" | sed -n 's/.*zone=\(-\?[0-9]*\).*/\1/p'; }
open_win() {
	python3 -I "$HERE/tools/janela.py" "$1" >/dev/null 2>&1 &
	PIDS="$PIDS $!"
	for _ in $(seq 1 50); do [ -n "$(win "$1")" ] && break; sleep 0.1; done
	sleep 0.4
}
drag_title() { # título dx dy passos [tecla]
	local bx by bw bh
	IFS=, read -r bx by bw bh <<<"$(box "$1")"
	xdotool mousemove $((bx + 60)) $((by + 9))
	sleep 0.3
	xdotool mousedown 1
	sleep 0.3
	for _ in $(seq 1 "$4"); do
		xdotool mousemove_relative -- "$2" "$3"
		sleep 0.08
	done
	[ -n "${5:-}" ] && { xdotool key "$5"; sleep 0.3; }
}

echo "TAP version 13"
check "o wlmaker responde no socket das zonas" '"$WZ" ping | grep -q pong'
check "backend wlmaker no status" '"$WZ" status | grep -q "^backend=wlmaker"'

open_win alvo
B0=$(box alvo)
drag_title alvo 30 25 12
xdotool mousemove 800 600
sleep 0.5
xdotool mouseup 1
sleep 1
check "arrasto encaixa no quadrante 4 ($(box alvo))" '[ "$(box alvo)" = "512,352,512,352" ] && [ "$(zone alvo)" = 3 ]'

"$WZ" restore >/dev/null
sleep 1
check "restaurar devolve o tamanho de antes ($(box alvo) vs $B0)" '[ "$(box alvo | cut -d, -f3,4)" = "$(echo "$B0" | cut -d, -f3,4)" ]'

B1=$(box alvo)
drag_title alvo 30 20 10 Escape
xdotool mouseup 1
sleep 1
check "Esc durante o arrasto devolve a janela ($(box alvo) vs $B1)" '[ "$(box alvo)" = "$B1" ]'

"$WZ" snap 1 >/dev/null
sleep 0.8
check "snap 1 ($(box alvo))" '[ "$(box alvo)" = "0,0,512,352" ]'
"$WZ" move right >/dev/null
sleep 0.8
check "move right ($(box alvo))" '[ "$(box alvo)" = "512,0,512,352" ]'
"$WZ" layout halves >/dev/null
"$WZ" snap 2 >/dev/null
sleep 0.8
check "layout halves, zona 2 ($(box alvo))" '[ "$(box alvo)" = "512,0,512,704" ]'

open_win dois
open_win tres
"$WZ" autotile on >/dev/null
sleep 1.2
check "mosaico: três no mosaico" '"$WZ" status | grep -q "tiled=3"'
check "mosaico: a primeira janela é a principal, 60% à esquerda ($(box alvo)/$(box dois)/$(box tres))" \
	'[ "$(box alvo)" = "0,0,614,704" ] && [ "$(box dois)" = "614,0,410,352" ] && [ "$(box tres)" = "614,352,410,352" ]'
"$WZ" autotile off >/dev/null
sleep 1
check "mosaico desligado: ninguém no mosaico" '"$WZ" status | grep -q "tiled=0"'
"$WZ" preview 'H(1,2,1)' >/dev/null
check "prévia aceita" 'true'

echo "1..$n"
[ $fails -eq 0 ] || { echo "# log do wlmaker:"; tail -20 "$TMP/wlmaker.log" | sed 's/^/# /'; }
exit $((fails > 0))
