#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Rolagem, memória por modo e Ctrl+Alt dentro do wlmaker (Wayland), com o wlmaker
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
[ -x "$WZ" ] || { echo "1..0 # SKIP sem o cliente wmake-zones (compilado sem X11)"; exit 0; }
# Só o wlmaker do Big WindowMaker tem as zonas (lê o wmake-zones.conf); um
# wlmaker do upstream não é falha deste projeto.
grep -aqF 'wmake-zones/wmake-zones.conf' "$WLMAKER" || { echo "1..0 # SKIP wlmaker sem as zonas"; exit 0; }
[ -x "$XVFB" ] || { echo "1..0 # SKIP sem Xvfb"; exit 0; }
command -v xdotool >/dev/null || { echo "1..0 # SKIP sem xdotool"; exit 0; }
python3 -c "import gi; gi.require_version('Gtk', '3.0')" 2>/dev/null || { echo "1..0 # SKIP sem python3-gobject"; exit 0; }

TMP=$(mktemp -d "${TMPDIR:-/tmp}/wzw.XXXXXX")
# Sockets Unix aceitam no máximo 107 bytes de caminho: TMPDIR longo, /tmp.
[ ${#TMP} -le 60 ] || { rmdir "$TMP"; TMP=$(mktemp -d /tmp/wzw.XXXXXX); }
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
printf '[General]\nZones=true\nLayout=quarters\nDragFlow=layouts\nGap=0\nOuterGap=0\nMinDrag=10\n[Tiling]\nGap=0\nOuterGap=0\n[Scroll]\nWidth=50\nGap=0\nOuterGap=0\n' >"$CONF"
unset WAYLAND_DISPLAY
WLR_BACKENDS=x11 WLR_RENDERER=pixman WLR_X11_OUTPUTS=1 "$WLMAKER" >"$TMP/wlmaker.log" 2>&1 &
PIDS="$PIDS $!"
for _ in $(seq 1 50); do ls "$XDG_RUNTIME_DIR"/wmake-zones/*.sock >/dev/null 2>&1 && break; sleep 0.1; done
export WAYLAND_DISPLAY
WAYLAND_DISPLAY=$(ls "$XDG_RUNTIME_DIR" | grep -m1 '^wayland-[0-9]*$')
export GDK_BACKEND=wayland NO_AT_BRIDGE=1 GSETTINGS_BACKEND=memory GIO_USE_VFS=local

# "windows -l": saída, zone=N, tiled=N, LxA+X+Y, classe, título.
win() { "$WZ" windows -l | grep -P "\t$1$"; }
box() { win "$1" | cut -f4 | sed -E 's/^([0-9]+)x([0-9]+)([+-][0-9]+)([+-][0-9]+)$/\3,\4,\1,\2/; s/(^|,)\+/\1/g'; }
zone() { win "$1" | cut -f2 | sed -n 's/^zone=\(-\?[0-9]*\)$/\1/p'; }
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


onscreen() { [ "$(box "$1" | cut -d, -f1)" -lt 1024 ]; }
st() { "$WZ" status; }
widths() { st | sed -n 's/^widths=//p' | head -1; }
click_title() { local bx by bw bh; IFS=, read -r bx by bw bh <<<"$(box "$1")"; xdotool mousemove $((bx + 60)) $((by + 9)) click 1; sleep 0.5; }

echo "TAP version 13"
check "o wlmaker responde" '"$WZ" ping | grep -q pong'
open_win a
open_win b
open_win c
"$WZ" snap 1 >/dev/null; sleep 0.6
check "c encaixada na zona 1 ($(box c))" '[ "$(zone c)" = 0 ]'
A0=$(box a) B0=$(box b) C0=$(box c)

# --- Ctrl+Alt: mover e redimensionar de qualquer ponto ------------------------
click_title b
IFS=, read -r bx by bw bh <<<"$(box b)"
xdotool mousemove $((bx + bw / 2)) $((by + bh / 2)); sleep 0.2
xdotool keydown ctrl+alt; sleep 0.1; xdotool mousedown 1; sleep 0.2
for _ in $(seq 1 10); do xdotool mousemove_relative -- 10 6; sleep 0.05; done
xdotool mouseup 1; sleep 0.1; xdotool keyup ctrl+alt; sleep 0.6
IFS=, read -r nx ny nw nh <<<"$(box b)"
check "Ctrl+Alt+arrastar move pelo meio ($bx,$by -> $nx,$ny)" '[ $((nx - bx)) -ge 60 ] && [ $((ny - by)) -ge 30 ]'
check "Ctrl+Alt+arrastar mantém o tamanho (${nw}x$nh)" '[ "$nw" = "$bw" ] && [ "$nh" = "$bh" ]'
xdotool mousemove $((nx + nw - 8)) $((ny + nh - 8)); sleep 0.2
xdotool keydown ctrl+alt; sleep 0.1; xdotool mousedown 3; sleep 0.2
for _ in $(seq 1 8); do xdotool mousemove_relative -- 8 5; sleep 0.05; done
xdotool mouseup 3; sleep 0.1; xdotool keyup ctrl+alt; sleep 0.8
IFS=, read -r rx ry rw rh <<<"$(box b)"
check "Ctrl+Alt+botão direito redimensiona pelo canto (${nw}x$nh -> ${rw}x$rh)" '[ "$rw" -gt "$nw" ] && [ "$rh" -gt "$nh" ]'
B0=$(box b)

# --- Mosaico e volta ------------------------------------------------------------
"$WZ" mode tiling >/dev/null; sleep 1
check "mosaico: três" 'st | grep -q "tiled=3"'
"$WZ" mode zones >/dev/null; sleep 1
check "Z -> M -> Z: a volta igual ($(box a) / $A0)" '[ "$(box a)" = "$A0" ]'
check "Z -> M -> Z: b volta igual ($(box b) / $B0)" '[ "$(box b)" = "$B0" ]'
check "Z -> M -> Z: c volta à zona 1 ($(box c) / $C0)" '[ "$(box c)" = "$C0" ] && [ "$(zone c)" = 0 ]'

# --- Rolagem --------------------------------------------------------------------
"$WZ" mode scroll >/dev/null; sleep 1
check "rolagem: três colunas" 'st | grep -q "columns=3"'
check "rolagem: três na faixa" 'st | grep -q "scrolled=3"'
W3=$(widths)
open_win d
sleep 0.8
check "abrir: quatro colunas" 'st | grep -q "columns=4"'
check "abrir não muda as larguras de antes ($W3 -> $(widths))" 'python3 -I -c "import sys; a=sys.argv[1].split(\",\"); b=sys.argv[2].split(\",\"); [b.remove(x) for x in a]" "$W3" "$(widths)"'
check "a nova na tela, com 50% ($(box d))" 'onscreen d && [ "$(box d | cut -d, -f3)" = 512 ]'
open_win e
open_win f
sleep 1
PK=0; for w in a b c d e f; do onscreen $w || PK=$((PK + 1)); done
check "seis colunas, estacionadas fora da tela ($PK)" 'st | grep -q "columns=6" && [ "$PK" -ge 3 ]'
check "a ativa (f) na tela" 'onscreen f'
"$WZ" scroll first >/dev/null; sleep 1
ACT=$(st | sed -n 's/^active=//p')
check "scroll first: a primeira coluna em foco e na tela ($ACT)" '[ -n "$ACT" ] && onscreen "$ACT"'
"$WZ" column width 33% >/dev/null; sleep 0.8
check "largura 33% ($(box "$ACT"))" '[ "$(box "$ACT" | cut -d, -f3)" -ge 330 ] && [ "$(box "$ACT" | cut -d, -f3)" -le 345 ]'
"$WZ" scroll right >/dev/null; sleep 0.6
"$WZ" column merge >/dev/null; sleep 0.8
check "juntar: cinco colunas" 'st | grep -q "columns=5"'
"$WZ" column split >/dev/null; sleep 0.8
check "separar: seis colunas" 'st | grep -q "columns=6"'
"$WZ" scroll last >/dev/null; sleep 1
ACT=$(st | sed -n 's/^active=//p')
check "scroll last: a última em foco e na tela ($ACT)" 'onscreen "$ACT"'
# Arrastar uma coluna visível para antes da outra (Ctrl+Alt) troca a ordem.
VIS=""; for w in a b c d e f; do onscreen $w && VIS="$VIS $w"; done
set -- $VIS
if [ $# -ge 2 ]; then
	L=$1 R=$2
	[ "$(box "$L" | cut -d, -f1)" -gt "$(box "$R" | cut -d, -f1)" ] && { L=$2; R=$1; }
	IFS=, read -r bx by bw bh <<<"$(box "$R")"
	xdotool mousemove $((bx + bw / 2)) $((by + bh / 2)); sleep 0.2
	xdotool keydown ctrl+alt; sleep 0.1; xdotool mousedown 1; sleep 0.2
	for _ in $(seq 1 10); do xdotool mousemove_relative -- -50 0; sleep 0.05; done
	IFS=, read -r lx ly lw lh <<<"$(box "$L")"
	xdotool mousemove 20 300; sleep 0.3
	xdotool mouseup 1; sleep 0.1; xdotool keyup ctrl+alt; sleep 1
	check "arrastar a coluna para antes da outra troca a ordem ($R antes de $L)" '[ "$(box "$R" | cut -d, -f1)" -lt "$(box "$L" | cut -d, -f1)" ]'
fi
WS=$(widths)

# --- Rolagem -> Zonas: tudo de volta ------------------------------------------------
"$WZ" mode zones >/dev/null; sleep 1
check "R -> Z: a volta igual ($(box a) / $A0)" '[ "$(box a)" = "$A0" ]'
check "R -> Z: b volta igual" '[ "$(box b)" = "$B0" ]'
check "R -> Z: c volta à zona 1" '[ "$(box c)" = "$C0" ] && [ "$(zone c)" = 0 ]'
check "R -> Z: nenhuma fora da tela" 'onscreen d && onscreen e && onscreen f'
check "R -> Z: nenhuma na faixa" 'st | grep -q "scrolled=0 parked=0"'
"$WZ" mode scroll >/dev/null; sleep 1
check "Z -> R: as larguras voltam ($(widths) / $WS)" '[ "$(widths)" = "$WS" ]'
"$WZ" mode tiling >/dev/null; sleep 1
check "R -> M: seis no mosaico" 'st | grep -q "tiled=6 snapped=[0-9]* scrolled=0"'
"$WZ" mode scroll >/dev/null; sleep 1
check "M -> R: seis na faixa" 'st | grep -q "scrolled=6"'
"$WZ" mode off >/dev/null; sleep 1
check "R -> desligado: a volta igual" '[ "$(box a)" = "$A0" ]'
check "R -> desligado: nenhuma fora da tela" 'for w in a b c d e f; do onscreen $w || exit 1; done'

echo "1..$n"
[ $fails -eq 0 ] || { echo "# log do wlmaker:"; tail -30 "$TMP/wlmaker.log" | sed 's/^/# /'; }
exit $((fails > 0))
