#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Rolagem, memória por modo e arrasto que mantém o tamanho, com o Window
# Maker de verdade num Xvfb próprio (nunca no display do usuário). TAP.
#
#   tests/x11-scroll.sh BUILD_DIR
#
# Precisa de Xvfb, wmaker, xdotool, xprop e xterm; sem eles, "skip".
# XVFB=/caminho/Xvfb escolhe o binário. COMPOSITOR=picom testa o overlay
# ARGB (picom --backend xrender).

set -u
B=${1:?uso: $0 BUILD_DIR}
WZ=$B/wmake-zones
XVFB=${XVFB:-$(command -v Xvfb || true)}
n=0
fails=0
ok() { n=$((n + 1)); echo "ok $n - $*"; }
nok() { n=$((n + 1)); fails=$((fails + 1)); echo "not ok $n - $*"; }
check() { if eval "$2"; then ok "$1"; else nok "$1"; fi; }

for tool in wmaker xdotool xprop xterm wdread; do
	command -v $tool >/dev/null || { echo "1..0 # SKIP sem $tool"; exit 0; }
done
[ -x "$XVFB" ] || { echo "1..0 # SKIP sem Xvfb"; exit 0; }

TMP=$(mktemp -d "${TMPDIR:-/tmp}/wzt.XXXXXX")
# Sockets Unix aceitam no máximo 107 bytes de caminho: TMPDIR longo, /tmp.
[ ${#TMP} -le 60 ] || { rmdir "$TMP"; TMP=$(mktemp -d /tmp/wzt.XXXXXX); }
PIDS=""
cleanup() {
	# Só os processos que este teste iniciou.
	for p in $PIDS; do kill "$p" 2>/dev/null; done
	sleep 0.3
	for p in $PIDS; do kill -9 "$p" 2>/dev/null; done
	[ -n "${XPID:-}" ] && kill -9 "$XPID" 2>/dev/null
	rm -rf "$TMP"
}
trap cleanup EXIT

export HOME=$TMP/home GNUSTEP_USER_ROOT=$TMP/home/GNUstep XDG_CONFIG_HOME=$TMP/home/.config
export XDG_RUNTIME_DIR=$TMP/run XDG_SESSION_TYPE=x11
unset WAYLAND_DISPLAY
mkdir -p "$GNUSTEP_USER_ROOT/Defaults" "$XDG_CONFIG_HOME/wmake-zones" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
cat >"$GNUSTEP_USER_ROOT/Defaults/WindowMaker" <<'EOF'
{
  WindowSnapping = YES;
  WindowPlacement = manual;
  OpaqueMove = YES;
}
EOF
CONF=$XDG_CONFIG_HOME/wmake-zones/wmake-zones.conf
printf '[General]\nZones=true\nLayout=halves\nDragFlow=layouts\nShowDelay=0\nGap=0\nOuterGap=0\n[Scroll]\nWidth=50\nGap=0\nOuterGap=0\n' >"$CONF"

# O servidor escolhe um display livre (-displayfd): nunca o de outro.
exec 9>"$TMP/displayfd"
"$XVFB" -displayfd 9 -screen 0 1600x900x24 -nolisten tcp >"$TMP/xvfb.log" 2>&1 &
XPID=$!
exec 9>&-
for _ in $(seq 1 50); do [ -s "$TMP/displayfd" ] && break; sleep 0.1; done
[ -s "$TMP/displayfd" ] || { echo "1..0 # SKIP Xvfb não subiu"; exit 0; }
export DISPLAY=:$(head -1 "$TMP/displayfd")
wmaker --no-autolaunch >"$TMP/wm.log" 2>&1 &
PIDS="$PIDS $!"
sleep 2
if [ "${COMPOSITOR:-}" = picom ] && command -v picom >/dev/null; then
	picom --backend xrender --no-fading-openclose >/dev/null 2>&1 &
	PIDS="$PIDS $!"
	sleep 1
fi

# Seletor e menu do dockapp fecham sozinhos depois de parados (30 s no uso
# normal); aqui, 2 s.
WZX_GRAB_IDLE_MS=2000 "$WZ" >"$TMP/wz.log" 2>&1 &
ZP=$!
PIDS="$PIDS $ZP"
sleep 1

clients() { xprop -root _NET_CLIENT_LIST | grep -o '0x[0-9a-f]*'; }
geom() { xdotool getwindowgeometry --shell "$1"; }
prop() { xprop -id "$1" "$2" 2>/dev/null | sed -n 's/.*= //p'; }
new_xterm() { # resultado em $NEW (sem subshell: o xterm herdaria o pipe)
	local before after
	before=$(clients | sort)
	xterm -geometry "$1" -T "$2" >/dev/null 2>&1 &
	PIDS="$PIDS $!"
	for _ in $(seq 1 40); do
		after=$(clients | sort)
		[ "$before" != "$after" ] && break
		sleep 0.1
	done
	sleep 0.5
	NEW=$(comm -13 <(echo "$before") <(echo "$after") | head -1)
}
drag() { # janela dx dy passos [tecla-no-meio]
	local w=$1 X Y WIDTH HEIGHT SCREEN WINDOW
	eval "$(geom "$w")"
	xdotool mousemove $((X + 40)) $((Y - 10))
	sleep 0.2
	xdotool mousedown 1
	sleep 0.2
	for _ in $(seq 1 "$4"); do
		xdotool mousemove_relative -- "$2" "$3"
		sleep 0.08
	done
	[ -n "${5:-}" ] && { xdotool key "$5"; sleep 0.2; }
	sleep 0.4
}
overlay_count() { xdotool search --classname wmake-zones-overlay 2>/dev/null | wc -l; }


G() { xdotool getwindowgeometry --shell "$1" | awk -F= '/^(X|Y|WIDTH|HEIGHT)=/{printf "%s ", $2}'; }
status() { "$WZ" status; }
xpos() { xdotool getwindowgeometry --shell "$1" | sed -n 's/^X=//p'; }
onscreen() { [ "$(xpos "$1")" -lt 1600 ]; }

echo "TAP version 13"
check "o serviço sobe e responde" '"$WZ" ping | grep -q pong'

# --- Zonas: arrastar mantém o tamanho ajustado à mão ---------------------
new_xterm 80x24+300+200 um; W1=$NEW
new_xterm 60x20+900+150 dois; W2=$NEW
new_xterm 70x15+500+500 tres; W3=$NEW
xdotool windowactivate --sync "$W1"; sleep 0.3
"$WZ" snap 1 >/dev/null; sleep 0.8
check "encaixada na zona 1" '[ "$(prop "$W1" _WMAKE_ZONES_ZONE)" = 0 ]'
xdotool windowsize --sync "$W1" 600 400; sleep 0.6
eval "$(geom "$W1")"; SW=$WIDTH SH=$HEIGHT
# Solta longe do painel de layouts (fora das zonas): o tamanho fica.
drag "$W1" 20 15 12
xdotool mousemove 700 600; sleep 0.4
xdotool mouseup 1; sleep 0.8
eval "$(geom "$W1")"
check "arrastar não muda o tamanho ajustado (${WIDTH}x$HEIGHT vs ${SW}x$SH)" '[ "$WIDTH" = "$SW" ] && [ "$HEIGHT" = "$SH" ]'
"$WZ" restore >/dev/null 2>&1; sleep 0.5
G1=$(G "$W1") G2=$(G "$W2") G3=$(G "$W3")

# --- Mosaico e volta: cada janela no lugar de antes -----------------------
"$WZ" mode tiling >/dev/null; sleep 1
check "mosaico organizou as três" 'status | grep -q "tiled=3"'
"$WZ" mode zones >/dev/null; sleep 1
check "Z -> M -> Z: W1 volta igual ($(G "$W1")/ $G1)" '[ "$(G "$W1")" = "$G1" ]'
check "Z -> M -> Z: W2 volta igual" '[ "$(G "$W2")" = "$G2" ]'
check "Z -> M -> Z: W3 volta igual" '[ "$(G "$W3")" = "$G3" ]'

# --- Rolagem ---------------------------------------------------------------
"$WZ" mode scroll >/dev/null; sleep 1
check "rolagem: três colunas" 'status | grep -q "columns=3"'
check "rolagem: três na faixa" 'status | grep -q "scrolled=3"'
# Entrando, cada janela vira uma coluna com a largura que já tinha.
check "rolagem: W1 com a largura de antes ($(G "$W1"))" '[ "$(echo "$(G "$W1")" | cut -d" " -f3)" = "$(echo "$G1" | cut -d" " -f3)" ]'
STRIP1=$(xprop -root _WMAKE_ZONES_SCROLL | sed 's/.*= //')
xdotool windowactivate --sync "$W2"; sleep 0.8
W2W=$(xdotool getwindowgeometry --shell "$W2" | sed -n 's/^WIDTH=//p')
new_xterm 50x20+100+100 quatro; W4=$NEW
sleep 0.8
check "abrir uma janela: quatro colunas" 'status | grep -q "columns=4"'
check "abrir não muda a largura das outras" '[ "$(xdotool getwindowgeometry --shell "$W2" | sed -n "s/^WIDTH=//p")" = "$W2W" ]'
check "a nova está na tela" 'onscreen "$W4"'
eval "$(geom "$W4")"
check "a nova tem 50% (${WIDTH})" '[ "$WIDTH" -gt 700 ] && [ "$WIDTH" -lt 800 ]'
new_xterm 50x20+100+100 cinco; W5=$NEW
new_xterm 50x20+100+100 seis; W6=$NEW
sleep 1
check "seis colunas, algumas estacionadas" 'status | grep -q "columns=6" && ! status | grep -q "parked=0"'
PK=0; for w in "$W1" "$W2" "$W3" "$W4" "$W5" "$W6"; do onscreen "$w" || PK=$((PK + 1)); done
check "estacionadas fora da tela ($PK)" '[ "$PK" -ge 3 ]'
check "a ativa está na tela" 'onscreen "$W6"'
"$WZ" scroll first >/dev/null; sleep 1
ACT=$(xprop -root _NET_ACTIVE_WINDOW | grep -o '0x[0-9a-f]*')
check "scroll first: foco na primeira coluna, na tela" '[ -n "$ACT" ] && onscreen "$ACT"'
"$WZ" scroll right >/dev/null; sleep 0.8
"$WZ" column width 33% >/dev/null; sleep 0.8
ACT=$(xprop -root _NET_ACTIVE_WINDOW | grep -o '0x[0-9a-f]*')
eval "$(geom "$ACT")"
check "largura 33% (${WIDTH})" '[ "$WIDTH" -gt 480 ] && [ "$WIDTH" -lt 560 ]'
"$WZ" column merge >/dev/null; sleep 0.8
check "juntar: cinco colunas" 'status | grep -q "columns=5"'
"$WZ" column split >/dev/null; sleep 0.8
check "separar: seis colunas" 'status | grep -q "columns=6"'
# Focar uma estacionada (lista de janelas, Alt+Tab) rola até ela.
for w in "$W1" "$W2" "$W3" "$W4" "$W5" "$W6"; do onscreen "$w" || HID=$w; done
xdotool windowactivate "$HID" 2>/dev/null; sleep 1
check "focar uma estacionada a traz para a tela" 'onscreen "$HID"'
STRIP2=$(xprop -root _WMAKE_ZONES_SCROLL | sed 's/.*= //')

# --- Rolagem -> Zonas: tudo de volta; as novas, na tela ----------------------
"$WZ" mode zones >/dev/null; sleep 1
check "R -> Z: W1 volta igual" '[ "$(G "$W1")" = "$G1" ]'
check "R -> Z: W2 volta igual" '[ "$(G "$W2")" = "$G2" ]'
check "R -> Z: W3 volta igual" '[ "$(G "$W3")" = "$G3" ]'
check "R -> Z: nenhuma fora da tela" 'onscreen "$W4" && onscreen "$W5" && onscreen "$W6"'
check "R -> Z: nenhuma na faixa" 'status | grep -q "scrolled=0 parked=0"'
"$WZ" mode scroll >/dev/null; sleep 1
STRIP3=$(xprop -root _WMAKE_ZONES_SCROLL | sed 's/.*= //')
check "Z -> R: a faixa volta igual (colunas, larguras, ordem)" '[ "$(echo "$STRIP3" | sed "s/a[0-9]* f[0-9]*//")" = "$(echo "$STRIP2" | sed "s/a[0-9]* f[0-9]*//")" ]'
"$WZ" mode tiling >/dev/null; sleep 1
check "R -> M: seis no mosaico" 'status | grep -q "tiled=6 snapped=[0-9]* scrolled=0"'
"$WZ" mode scroll >/dev/null; sleep 1
check "M -> R: seis na faixa" 'status | grep -q "scrolled=6"'
"$WZ" mode off >/dev/null; sleep 1.5
check "R -> desligado: W1 volta igual" '[ "$(G "$W1")" = "$G1" ]'
check "R -> desligado: nenhuma fora da tela" 'for w in $W1 $W2 $W3 $W4 $W5 $W6; do onscreen $w || exit 1; done'

# --- O serviço encerrado na Rolagem não deixa janelas escondidas --------------
"$WZ" mode scroll >/dev/null 2>&1; sleep 1.5
for _ in $(seq 1 30); do "$WZ" ping >/dev/null 2>&1 && break; sleep 0.1; done
"$WZ" mode scroll >/dev/null; sleep 1
"$WZ" quit >/dev/null 2>&1; sleep 1.5
check "quit na Rolagem: nenhuma fora da tela" 'for w in $W1 $W2 $W3 $W4 $W5 $W6; do onscreen $w || exit 1; done'

echo "1..$n"
[ $fails -eq 0 ] || { echo "# log do serviço:"; sed 's/^/# /' "$TMP/wz.log"; }
exit $((fails > 0))
