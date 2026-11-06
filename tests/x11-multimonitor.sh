#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Mosaico em dois monitores com o Window Maker de verdade: um Xephyr com
# dois heads (+xinerama) dentro de um Xvfb próprio (nunca o display do
# usuário). Saída TAP.
#
#   tests/x11-multimonitor.sh BUILD_DIR
#
# XEPHYR=/caminho/Xephyr escolhe o binário; sem Xephyr, "skip".
#
# Casos (cada um já falhou de verdade):
#   - o RandR do Xephyr descreve um monitor só (metade da tela); os heads
#     vêm do Xinerama, e cada monitor tem o seu mosaico;
#   - o Dock que muda de lugar (ladrilhos) refaz o mosaico na área útil nova;
#   - uma janela que não cabia (tamanho mínimo) entra quando passa a caber;
#   - uma janela do mosaico levada para o outro monitor (como pelo teclado)
#     entra no mosaico de lá e o de origem se refaz.

set -u
B=${1:?uso: $0 BUILD_DIR}
WZ=$B/wmake-zones
HERE=$(cd "$(dirname "$0")" && pwd)
XVFB=${XVFB:-$(command -v Xvfb || true)}
XEPHYR=${XEPHYR:-$(command -v Xephyr || true)}
n=0
fails=0
ok() { n=$((n + 1)); echo "ok $n - $*"; }
nok() { n=$((n + 1)); fails=$((fails + 1)); echo "not ok $n - $*"; }
check() { if eval "$2"; then ok "$1"; else nok "$1"; fi; }

for tool in wmaker xdotool xprop python3; do
	command -v $tool >/dev/null || { echo "1..0 # SKIP sem $tool"; exit 0; }
done
[ -x "$XVFB" ] || { echo "1..0 # SKIP sem Xvfb"; exit 0; }
[ -x "$XEPHYR" ] || { echo "1..0 # SKIP sem Xephyr"; exit 0; }
python3 -c 'import Xlib, gi' 2>/dev/null || { echo "1..0 # SKIP sem python-xlib ou PyGObject"; exit 0; }

TMP=$(mktemp -d "${TMPDIR:-/tmp}/wzm.XXXXXX")
[ ${#TMP} -le 60 ] || { rmdir "$TMP"; TMP=$(mktemp -d /tmp/wzm.XXXXXX); }
PIDS=""
cleanup() {
	for p in $PIDS; do kill "$p" 2>/dev/null; done
	sleep 0.3
	for p in $PIDS; do kill -9 "$p" 2>/dev/null; done
	[ -n "${EPID:-}" ] && kill "$EPID" 2>/dev/null
	sleep 0.3
	[ -n "${XPID:-}" ] && kill "$XPID" 2>/dev/null
	rm -rf "$TMP"
}
trap cleanup EXIT

export HOME=$TMP/home GNUSTEP_USER_ROOT=$TMP/home/GNUstep XDG_CONFIG_HOME=$TMP/home/.config
export XDG_RUNTIME_DIR=$TMP/run XDG_SESSION_TYPE=x11
unset WAYLAND_DISPLAY
mkdir -p "$GNUSTEP_USER_ROOT/Defaults" "$XDG_CONFIG_HOME/wmake-zones" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
# Sem Dock e sem Clip: os ladrilhos do teste fazem o papel do Dock.
cat >"$GNUSTEP_USER_ROOT/Defaults/WindowMaker" <<'EOF'
{
  WindowPlacement = auto;
  DisableDock = YES;
  DisableClip = YES;
}
EOF
CONF=$XDG_CONFIG_HOME/wmake-zones/wmake-zones.conf
printf '[General]\nZones=false\nAutotile=true\nGap=0\nOuterGap=0\n[Tiling]\nAlgorithm=columns\nGap=0\nOuterGap=0\nSmartGaps=true\n' >"$CONF"

exec 9>"$TMP/dfd"
"$XVFB" -displayfd 9 -screen 0 2700x900x24 -nolisten tcp >"$TMP/xvfb.log" 2>&1 &
XPID=$!
exec 9>&-
for _ in $(seq 1 50); do [ -s "$TMP/dfd" ] && break; sleep 0.1; done
[ -s "$TMP/dfd" ] || { echo "1..0 # SKIP Xvfb não subiu"; exit 0; }
exec 9>"$TMP/efd"
DISPLAY=:$(head -1 "$TMP/dfd") "$XEPHYR" -displayfd 9 +xinerama -screen 1280x800 -screen 1280x800 -nolisten tcp \
	>"$TMP/xephyr.log" 2>&1 &
EPID=$!
exec 9>&-
for _ in $(seq 1 50); do [ -s "$TMP/efd" ] && break; sleep 0.1; done
[ -s "$TMP/efd" ] || { echo "1..0 # SKIP Xephyr não subiu"; exit 0; }
export DISPLAY=:$(head -1 "$TMP/efd")

wmaker --no-autolaunch >"$TMP/wm.log" 2>&1 &
PIDS="$PIDS $!"
sleep 2
"$WZ" >"$TMP/wz.log" 2>&1 &
PIDS="$PIDS $!"
for _ in $(seq 1 50); do "$WZ" status >/dev/null 2>&1 && break; sleep 0.1; done

# Janela de teste: título, posição, tamanho mínimo opcional.
open_win() { # título LxA+X+Y [MÍNIMO_LxA]
	python3 -I "$HERE/tools/janela.py" "$@" >/dev/null 2>&1 &
	PIDS="$PIDS $!"
	eval "WIN_PID_$(printf %s "$1" | tr -c 'A-Za-z0-9' _)=$!"
	for _ in $(seq 1 50); do win "$1" >/dev/null && break; sleep 0.1; done
}
win() { "$WZ" windows -l | grep -P "\t$1$"; }
mon_of() { win "$1" | cut -f1; }
tiled() { win "$1" | cut -f3; }
geom() { win "$1" | cut -f4; }
settle() { sleep "${1:-1.2}"; }

# 1. Dois monitores (Xinerama), cada um com o seu mosaico.
mons=$("$WZ" status | grep -c '^monitor')
check "dois monitores, pelos heads do Xinerama" '[ "$mons" = 2 ]'
open_win A1 400x300+100+100
open_win A2 400x300+300+200
open_win B1 400x300+1500+100
open_win B2 400x300+1700+200
settle 1.5
check "A1 e A2 no mosaico do primeiro monitor" \
	'[ "$(tiled A1)" = tiled=1 ] && [ "$(tiled A2)" = tiled=1 ] && [ "$(mon_of A1)" = "$(mon_of A2)" ]'
check "B1 e B2 no mosaico do segundo" \
	'[ "$(tiled B1)" = tiled=1 ] && [ "$(tiled B2)" = tiled=1 ] && [ "$(mon_of B1)" != "$(mon_of A1)" ]'
check "colunas do primeiro monitor: 640 de largura cada" \
	'[[ "$(geom A1)" == 640x800+* ]] && [[ "$(geom A2)" == 640x800+* ]]'
check "colunas do segundo começam em x>=1280" \
	'[[ "$(geom B1)" =~ \+(12[89][0-9]|1[3-9][0-9][0-9]|2[0-9]{3})\+ ]]'

# 2. Um "Dock" (coluna de ladrilhos) aparece na borda direita do primeiro
#    monitor: o mosaico de lá encolhe sem ninguém pedir.
python3 -I "$HERE/tools/ladrilhos-x11.py" 1216 0 4 >/dev/null 2>&1 &
DOCK=$!
PIDS="$PIDS $DOCK"
settle 1.5
check "Dock novo: as colunas do primeiro monitor saem de baixo dele (largura 608)" \
	'[[ "$(geom A1)" == 608x800+* ]] && [[ "$(geom A2)" == 608x800+* ]]'
kill $DOCK
settle 1.5
check "Dock saiu: as colunas voltam a 640" '[[ "$(geom A1)" == 640x800+* ]]'

# 3. Uma janela que não cabe (mínimo 700 de largura numa coluna de 426)
#    fica flutuando enquanto não couber (duas colunas de 640 também não);
#    sozinha no monitor (caso 4) ela cabe e entra. Antes ela ficava fora do
#    mosaico para sempre.
open_win A3 400x300+500+300 700x200
settle 1.5
check "A3 (mínimo 700) não cabe com três colunas: flutua" '[ "$(tiled A3)" = tiled=0 ]'
kill "$WIN_PID_A2"
settle 1.5
check "A2 fechada: com duas colunas de 640, A3 (700) ainda flutua" '[ "$(tiled A3)" = tiled=0 ]'

# 4. A1 levada para o outro monitor como faz o teclado do Window Maker
#    (pedido de movimento ao gerenciador, não do wmake-zones).
a1=$(xdotool search --name '^A1$' | head -1)
xdotool windowmove "$a1" 1500 300
settle 1.8
check "A1 entra no mosaico do segundo monitor" \
	'[ "$(tiled A1)" = tiled=1 ] && [ "$(mon_of A1)" = "$(mon_of B1)" ]'
check "segundo monitor com três colunas" '[[ "$(geom B1)" == 426x800+* || "$(geom B1)" == 427x800+* ]]'
check "primeiro monitor refeito: A3 sozinha agora cabe e entra no mosaico (tela toda)" \
	'[ "$(tiled A3)" = tiled=1 ] && [[ "$(geom A3)" == 1280x800+0+0 ]]'

check "o serviço continua no ar" '"$WZ" status >/dev/null 2>&1'
echo "1..$n"
[ "$fails" = 0 ]
