#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Testes funcionais do backend X11 com o Window Maker de verdade, num Xvfb
# próprio (nunca no display do usuário). Saída TAP.
#
#   tests/x11-functional.sh BUILD_DIR
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
printf '[General]\nZones=true\nLayout=quarters\nDragFlow=zones\nShowDelay=0\nGap=0\nOuterGap=0\n' >"$CONF"

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

echo "TAP version 13"
check "o serviço sobe e responde" '"$WZ" ping | grep -q pong'
check "WindowSnapping do wmaker desligado com as zonas" '[ "$(wdread WindowMaker WindowSnapping)" = NO ]'

new_xterm 80x24+300+200 um; W1=$NEW
eval "$(geom "$W1")"
X0=$X Y0=$Y
drag "$W1" 25 20 20
check "overlay aparece durante o arrasto" '[ "$(overlay_count)" -ge 1 ]'
xdotool mousemove 1300 700
sleep 0.4
xdotool mouseup 1
sleep 0.8
check "solto no quadrante 4: zona 3 gravada" '[ "$(prop "$W1" _WMAKE_ZONES_ZONE)" = 3 ]'
eval "$(geom "$W1")"
check "janela dentro do quadrante 4 ($X,$Y)" '[ "$X" -gt 800 ] && [ "$Y" -gt 450 ]'
check "overlay some ao soltar" '[ "$(overlay_count)" -eq 0 ]'
check "geometria anterior guardada" '[ -n "$(prop "$W1" _WMAKE_ZONES_RESTORE)" ]'

"$WZ" restore >/dev/null
sleep 0.6
eval "$(geom "$W1")"
check "restaurar devolve a posição de antes ($X,$Y vs $X0,$Y0)" '[ $((X - X0)) -le 2 ] && [ $((X0 - X)) -le 2 ] && [ $((Y - Y0)) -le 2 ] && [ $((Y0 - Y)) -le 2 ]'
check "restaurar limpa as marcas" '[ -z "$(prop "$W1" _WMAKE_ZONES_ZONE)" ]'

eval "$(geom "$W1")"
X0=$X Y0=$Y
drag "$W1" 30 10 12 Escape
check "Esc esconde o overlay" '[ "$(overlay_count)" -eq 0 ]'
xdotool mouseup 1
sleep 0.8
eval "$(geom "$W1")"
check "Esc: a janela volta para onde estava ($X,$Y)" '[ $((X - X0)) -le 3 ] && [ $((X0 - X)) -le 3 ] && [ $((Y - Y0)) -le 3 ] && [ $((Y0 - Y)) -le 3 ]'
check "Esc: nada encaixado" '[ -z "$(prop "$W1" _WMAKE_ZONES_ZONE)" ]'

xdotool windowactivate --sync "$W1" 2>/dev/null
sleep 0.4
"$WZ" snap 1 >/dev/null
sleep 0.5
check "snap 1 pelo comando" '[ "$(prop "$W1" _WMAKE_ZONES_ZONE)" = 0 ]'
"$WZ" move right >/dev/null
sleep 0.5
check "move right vai para a zona vizinha" '[ "$(prop "$W1" _WMAKE_ZONES_ZONE)" = 1 ]'
"$WZ" move down >/dev/null
sleep 0.5
check "move down" '[ "$(prop "$W1" _WMAKE_ZONES_ZONE)" = 3 ]'
"$WZ" undo >/dev/null
sleep 0.5
eval "$(geom "$W1")"
check "desfazer volta para a zona anterior ($X,$Y)" '[ "$X" -gt 800 ] && [ "$Y" -lt 450 ]'

xdotool key super+z
sleep 0.6
check "Super+Z abre o seletor" '[ "$(overlay_count)" -ge 1 ]'
xdotool key 3
sleep 0.6
check "tecla 3 encaixa na zona 3" '[ "$(prop "$W1" _WMAKE_ZONES_ZONE)" = 2 ]'
check "seletor fechado" '[ "$(overlay_count)" -eq 0 ]'

# Seletor esquecido aberto: fecha sozinho e solta o teclado (a tela de
# bloqueio precisa pegá-lo).
xdotool key super+z
sleep 0.6
check "seletor aberto de novo" '[ "$(overlay_count)" -ge 1 ]'
sleep 2.5
check "seletor parado fecha sozinho" '[ "$(overlay_count)" -eq 0 ]'
check "teclado livre depois de fechar sozinho" 'python3 -c "
from Xlib import display, X
d = display.Display(); r = d.screen().root
s = r.grab_keyboard(True, X.GrabModeAsync, X.GrabModeAsync, X.CurrentTime)
d.ungrab_keyboard(X.CurrentTime); d.sync(); raise SystemExit(s != X.GrabSuccess)"'

# Primeiro o layout, depois a zona (padrão da 0.3): o painel de miniaturas
# aparece no arrasto; soltar numa zona de uma miniatura encaixa direto, e
# parar sobre uma miniatura mostra as zonas daquele layout.
sed -i 's/^DragFlow=zones$/DragFlow=layouts\nLayoutDwell=150/' "$CONF"
"$WZ" reload >/dev/null
cell() { "$WZ" overlay | awk -v id="$1" -v f="$2" '$1 == "cell=" id { split(f == "thumb" ? substr($3, 7) : $2, a, /[x+]/); print a[1], a[2], a[3], a[4] }'; }
eval "$(geom "$W1")"
drag "$W1" 15 15 12
check "painel de layouts no arrasto, ainda sem zonas" '"$WZ" overlay | grep -q "^overlay=shown .*zones=0 strip=1"'
read -r tw th tx ty <<<"$(cell halves thumb)"
echo "# miniatura de metades: ${tw}x${th}+${tx}+${ty}"
xdotool mousemove $((tx + tw * 3 / 4)) $((ty + th / 2))
sleep 0.4
xdotool mousemove $((tx + tw * 3 / 4 + 1)) $((ty + th / 2))
sleep 0.3
xdotool mouseup 1
sleep 0.8
eval "$(geom "$W1")"
check "soltar na metade direita da miniatura encaixa na metade direita ($X,$WIDTH)" '[ "$(prop "$W1" _WMAKE_ZONES_ZONE)" = 1 ] && [ "$X" -ge 790 ] && [ "$WIDTH" -le 810 ]'
check "o layout escolhido no painel vira o do monitor" 'grep -q "^Layout=halves" "$CONF"'
"$WZ" undo >/dev/null
sleep 0.6
check "desfazer: o layout de antes volta junto (um passo só)" 'grep -q "^Layout=quarters" "$CONF"'
check "desfazer: a zona de antes volta" '[ "$(prop "$W1" _WMAKE_ZONES_ZONE)" = 2 ]'
"$WZ" redo >/dev/null
sleep 0.6
check "refazer: metade direita e layout de novo" '[ "$(prop "$W1" _WMAKE_ZONES_ZONE)" = 1 ] && grep -q "^Layout=halves" "$CONF"'
drag "$W1" -15 15 12
read -r cw chh cx cy <<<"$(cell thirds cell)"
xdotool mousemove $((cx + cw / 2)) $((cy + chh - 6))
sleep 0.5
xdotool mousemove $((cx + cw / 2 + 1)) $((cy + chh - 6))
sleep 0.4
check "parado sobre a miniatura: as zonas daquele layout aparecem" '"$WZ" overlay | grep -q "zones=3 strip=1 sel=thirds"'
xdotool mousemove 120 450
sleep 0.4
xdotool mouseup 1
sleep 0.8
eval "$(geom "$W1")"
check "e a zona escolhida depois: terço da esquerda ($X,$WIDTH)" '[ "$(prop "$W1" _WMAKE_ZONES_ZONE)" = 0 ] && [ "$X" -le 5 ] && [ "$WIDTH" -le 540 ]'
drag "$W1" 15 15 12
xdotool key Escape
sleep 0.3
check "Esc fecha o painel" '[ "$(overlay_count)" -eq 0 ]'
xdotool mouseup 1
sleep 0.6

"$WZ" layout thirds >/dev/null
check "layout trocado e gravado" 'grep -q "^Layout=thirds" "$CONF"'
"$WZ" snap 3 >/dev/null
sleep 0.5
eval "$(geom "$W1")"
check "três colunas: zona 3 à direita ($X)" '[ "$X" -gt 1000 ]'

# Mosaico
new_xterm 70x20+500+300 dois; W2=$NEW
new_xterm 60x18+700+400 tres; W3=$NEW
for w in $W1 $W2 $W3; do eval "$(geom "$w")"; echo "# antes $w $X,$Y ${WIDTH}x$HEIGHT"; done
declare -A PRE
for w in $W2 $W3; do eval "$(geom "$w")"; PRE[$w]="$X,$Y"; done
"$WZ" autotile on >/dev/null
sleep 1.2
overlap=0
inside=1
declare -a R
for w in $W1 $W2 $W3; do
	eval "$(geom "$w")"
	echo "# mosaico $w $X,$Y ${WIDTH}x$HEIGHT"
	R+=("$X $Y $WIDTH $HEIGHT")
	{ [ "$X" -lt 0 ] || [ $((X + WIDTH)) -gt 1600 ] || [ $((Y + HEIGHT)) -gt 900 ]; } && inside=0
done
for i in 0 1 2; do for j in 0 1 2; do
	[ $i -ge $j ] && continue
	read -r ax ay aw ah <<<"${R[$i]}"
	read -r bx by bw bh <<<"${R[$j]}"
	[ $ax -lt $((bx + bw)) ] && [ $bx -lt $((ax + aw)) ] && [ $ay -lt $((by + bh)) ] && [ $by -lt $((ay + ah)) ] && overlap=1
done; done
check "mosaico: três janelas sem sobreposição" '[ $overlap -eq 0 ]'
check "mosaico: tudo dentro da tela" '[ $inside -eq 1 ]'
check "status conta 3 no mosaico" '"$WZ" status | grep -q "tiled=3"'
new_xterm 50x10+100+100 quatro; W4=$NEW
sleep 1
check "janela nova entra no mosaico" '"$WZ" status | grep -q "tiled=4"'
xdotool windowactivate --sync "$W4" 2>/dev/null
sleep 0.3
"$WZ" float >/dev/null
sleep 0.8
check "float tira a janela do mosaico" '"$WZ" status | grep -q "tiled=3"'
kill_w4=$(xprop -id "$W4" _NET_WM_PID | sed -n 's/.*= //p')
[ -n "$kill_w4" ] && kill "$kill_w4" 2>/dev/null
sleep 0.8

# Janelas que não cabem saem uma por vez, a que está mais longe de caber
# primeiro: sem a "grande" (mínimo 800 de altura), a "media" (220) cabe.
if python3 -c 'import gi; gi.require_version("Gtk", "3.0")' 2>/dev/null; then
	JAN=$(dirname "$0")/tools/janela.py
	python3 "$JAN" grande 300x800+900+50 300x800 >/dev/null 2>&1 &
	PIDS="$PIDS $!"
	python3 "$JAN" media 300x220+950+100 300x220 >/dev/null 2>&1 &
	PIDS="$PIDS $!"
	sleep 2.5
	check "uma janela grande demais não tira as outras do mosaico" '"$WZ" status | grep -q "tiled=4"'
	check "a grande demais fica flutuante" '"$WZ" windows -l | grep -P "\tgrande$" | grep -q "tiled=0"'
	for p in $(pgrep -f "janela.py (grande|media)"); do
		grep -qz "^DISPLAY=$DISPLAY$" /proc/"$p"/environ 2>/dev/null && kill "$p"
	done
	sleep 0.8
fi
"$WZ" autotile off >/dev/null
sleep 1
eval "$(geom "$W2")"
check "desligar o mosaico devolve a posição (${PRE[$W2]} -> $X,$Y)" '[ "${PRE[$W2]}" = "$X,$Y" ]'

# Regra: XTerm sempre flutuante
printf '[Rule xterm]\nClass=XTerm\nAction=float\n' >>"$CONF"
"$WZ" reload >/dev/null
new_xterm 40x10+200+600 cinco; W5=$NEW
"$WZ" autotile on >/dev/null
sleep 1
check "regra Action=float mantém o XTerm novo fora do mosaico" '"$WZ" status | grep -q "tiled=3"'
"$WZ" autotile off >/dev/null
sleep 0.5

# Um cliente que conecta e não manda nada não pode travar o serviço.
SOCK=$(ls "$XDG_RUNTIME_DIR"/wmake-zones/*.sock | head -1)
python3 -c 'import socket,sys,time; s=socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); time.sleep(3)' "$SOCK" &
MUDO=$!
sleep 0.3
t0=$(date +%s%N)
"$WZ" ping >/dev/null
dt=$((($(date +%s%N) - t0) / 1000000))
kill "$MUDO" 2>/dev/null
wait "$MUDO" 2>/dev/null
check "cliente mudo não trava o serviço (ping em ${dt} ms)" '[ "$dt" -lt 250 ]'
check "windows -l mostra monitor, zona e geometria" '"$WZ" windows -l | grep -qP "^screen\tzone=-?[0-9]+\ttiled=[01]\t[0-9]+x[0-9]+[+-][0-9]+[+-][0-9]+\t"'

# Repouso: nenhum despertar em 5 s.
cs() { awk '/^voluntary_ctxt_switches|^nonvoluntary_ctxt_switches/ {s += $2} END {print s}' /proc/$ZP/status; }
t0=$(awk '{print $14 + $15}' /proc/$ZP/stat)
c0=$(cs)
sleep 5
t1=$(awk '{print $14 + $15}' /proc/$ZP/stat)
c1=$(cs)
check "repouso: zero acordadas em 5 s ($c0 -> $c1)" '[ "$c0" = "$c1" ]'
check "repouso: zero tique de CPU em 5 s" '[ "$t0" = "$t1" ]'
pss=$(awk '/^Pss:/ {print $2}' /proc/$ZP/smaps_rollup)
echo "# PSS do serviço: ${pss} KiB"
check "PSS do serviço abaixo de 8 MiB (${pss} KiB)" '[ "$pss" -lt 8192 ]'

"$WZ" zones off >/dev/null
sleep 1
check "zones off: o serviço sai sozinho" '! kill -0 $ZP 2>/dev/null'
check "zones off: WindowSnapping do usuário de volta" '[ "$(wdread WindowMaker WindowSnapping)" = YES ]'
check "nenhum overlay esquecido" '[ "$(overlay_count)" -eq 0 ]'

# Fim da sessão: o servidor X some com o serviço rodando; o socket de
# controle não pode ficar para trás.
"$WZ" zones on >/dev/null
for _ in $(seq 1 30); do ls "$XDG_RUNTIME_DIR"/wmake-zones/x11-*.sock >/dev/null 2>&1 && break; sleep 0.1; done
ZP2=""
for p in $(pgrep -x wmake-zones); do # só o deste teste (o do usuário tem outro runtime)
	grep -qz "^XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR$" /proc/"$p"/environ 2>/dev/null && ZP2=$p
done
PIDS="$PIDS $ZP2"
# Vivo = existe e não é zumbi (num contêiner o PID 1 pode não recolher
# órfãos, e o serviço solto pelo "zones on" é um órfão).
alive() { [ -n "$1" ] && [ -r /proc/"$1"/stat ] && ! awk '{ exit $3 == "Z" ? 0 : 1 }' /proc/"$1"/stat 2>/dev/null; }
kill "$XPID" 2>/dev/null
for _ in $(seq 1 30); do alive "$ZP2" || break; sleep 0.1; done
check "servidor X fechou: o serviço sai" '! alive "$ZP2"'
check "servidor X fechou: o socket de controle foi apagado" '! ls "$XDG_RUNTIME_DIR"/wmake-zones/x11-*.sock >/dev/null 2>&1'

echo "1..$n"
[ $fails -eq 0 ] || { echo "# log do serviço:"; sed 's/^/# /' "$TMP/wz.log"; }
exit $((fails > 0))
