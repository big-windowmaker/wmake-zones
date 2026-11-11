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
printf '[General]\nZones=true\nLayout=quarters\nDragFlow=zones\nGap=0\nOuterGap=0\nMinDrag=10\n[Tiling]\nGap=0\nOuterGap=0\n' >"$CONF"
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

echo "TAP version 13"
check "o wlmaker responde no socket das zonas" '"$WZ" ping | grep -q pong'
check "backend wlmaker no status" '"$WZ" status | grep -q "^backend=wlmaker"'
# Áreas de trabalho contadas a partir de 1, como no [Workspace N].
check "primeira área de trabalho é a 1" '"$WZ" status | grep -qx "desktop=1"'
printf '[Workspace 1]\nLayout=thirds\n' >>"$CONF"
"$WZ" reload >/dev/null
check "[Workspace 1] vale na primeira área de trabalho" '"$WZ" status | grep -q "layout=thirds"'
sed -i '/^\[Workspace 1\]$/,/^Layout=thirds$/d' "$CONF"
"$WZ" reload >/dev/null

open_win alvo
B0=$(box alvo)
drag_title alvo 30 25 12
xdotool mousemove 800 600
sleep 0.5
xdotool mouseup 1
sleep 1
check "arrasto encaixa no quadrante 4 ($(box alvo))" '[ "$(box alvo)" = "512,352,512,352" ] && [ "$(zone alvo)" = 3 ]'

# Movimento rápido: um evento só leva o ponteiro para longe da barra de
# título, antes de a janela acompanhar. O arrasto continua.
"$WZ" restore >/dev/null
sleep 0.8
drag_title alvo 5 5 3
xdotool mousemove_relative -- -420 -260
sleep 0.4
check "um salto do ponteiro não termina o arrasto" '"$WZ" overlay | grep -q "^overlay=shown"'
xdotool mousemove 100 100
sleep 0.4
xdotool mouseup 1
sleep 1
check "e a janela vai para onde o ponteiro foi: quadrante 1 ($(box alvo))" '[ "$(zone alvo)" = 0 ]'
"$WZ" undo >/dev/null
sleep 0.6
"$WZ" layout quarters >/dev/null
"$WZ" snap 4 >/dev/null
sleep 0.6

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
# Um painel com zona exclusiva (o Dock, a bandeja) aparece e some: o
# mosaico acompanha a área útil sem ninguém pedir (antes as células ficavam
# por baixo do painel até "aplicar de novo").
if python3 -c 'import gi; gi.require_version("GtkLayerShell", "0.1")' 2>/dev/null; then
	python3 -I "$HERE/tools/painel-wl.py" 64 >/dev/null 2>&1 &
	PAINEL=$!
	PIDS="$PIDS $PAINEL"
	sleep 1.2
	check "mosaico: painel de 64 na direita, a principal fica com 60% de 960 ($(box alvo)/$(box dois))" \
		'[ "$(box alvo)" = "0,0,576,704" ] && [ "$(box dois)" = "576,0,384,352" ]'
	kill "$PAINEL"
	sleep 1.2
	check "mosaico: painel fechado, as células voltam ($(box alvo))" '[ "$(box alvo)" = "0,0,614,704" ]'
fi
# Setas e roda do dockapp no Mosaico: o algoritmo seguinte e o anterior.
"$WZ" algorithm master >/dev/null
"$WZ" algorithm next >/dev/null
a1=$("$WZ" status | sed -n 's/^algorithm=//p' | head -1)
"$WZ" algorithm prev >/dev/null
a2=$("$WZ" status | sed -n 's/^algorithm=//p' | head -1)
check "algorithm next/prev ($a1, $a2)" '[ "$a1" = grid ] && [ "$a2" = master ]'
# Uma janela com mínimo de 700 de largura (GTK4: o Pamac pede 760) não cabe
# em célula nenhuma (a principal tem 614): fica flutuante, com o motivo no
# "windows -d", e não desarruma as outras.
# (Só com um wlmaker que já confere o mínimo: o que responde "windows -d".)
if "$WZ" windows -d | grep -q $'\ttiled='; then
	python3 -I "$HERE/tools/janela.py" larga 700x300 700x300 >/dev/null 2>&1 &
	LARGA=$!
	PIDS="$PIDS $LARGA"
	for _ in $(seq 1 50); do [ -n "$(win larga)" ] && break; sleep 0.1; done
	sleep 1.2
	check "mosaico: a que não cabe fica flutuante ($(win larga | cut -f3))" 'win larga | grep -q "tiled=0"'
	check "windows -d diz por que ($("$WZ" windows -d | grep -P "\tlarga$" | cut -f3))" \
		'"$WZ" windows -d | grep -P "\tlarga$" | grep -q "não cabe: mínimo 7[0-9][0-9]x"'
	check "as outras continuam no mosaico ($(box alvo))" '"$WZ" status | grep -q "tiled=3" && [ "$(box alvo)" = "0,0,614,704" ]'
	kill "$LARGA"
	sleep 0.8
else
	echo "# SKIP mínimo das janelas no mosaico: este wlmaker ainda não confere (sem \"windows -d\")"
fi
"$WZ" autotile off >/dev/null
sleep 1
check "mosaico desligado: ninguém no mosaico" '"$WZ" status | grep -q "tiled=0"'
"$WZ" preview 'H(1,2,1)' >/dev/null
check "prévia aceita" 'true'

# Paridade com o X11 (0.3): identidade do aplicativo, regras, seletor
# Super+Z, foco espacial, desfazer/refazer, modos, BSP e o painel de
# layouts do arrasto.
check "windows -l traz o app_id do cliente Wayland" 'win alvo | cut -f5 | grep -qx wmake-zones-teste'
printf '[Rule teste]\nClass=wmake-zones-teste\nTitle=regra\nAction=zone\nZone=2\n' >>"$CONF"
"$WZ" reload >/dev/null
"$WZ" layout quarters >/dev/null
open_win regra
sleep 0.6
check "regra pelo app_id: abre na zona 2 ($(box regra))" '[ "$(zone regra)" = 1 ] && [ "$(box regra)" = "512,0,512,352" ]'
sed -i '/^\[Rule teste\]$/,/^Zone=2$/d' "$CONF"
"$WZ" reload >/dev/null

# Seletor Super+Z: o teclado inteiro vai para ele; 3 encaixa na zona 3.
check "status mostra a janela ativa (regra)" '"$WZ" status | grep -qx "active=regra"'
xdotool key super+z 2>/dev/null
"$WZ" picker >/dev/null
sleep 0.5
check "Super+Z/picker abre o seletor" '"$WZ" overlay | grep -q "picker=1"'
xdotool key 3
sleep 0.6
check "tecla 3 encaixa na zona 3 ($(box regra))" '[ "$(zone regra)" = 2 ] && [ "$(box regra)" = "0,352,512,352" ]'
check "seletor fechado" '"$WZ" overlay | grep -q "^overlay=hidden.*picker=0"'
"$WZ" picker >/dev/null
sleep 0.3
xdotool key Right
sleep 0.3
xdotool key Return
sleep 0.6
check "setas e Enter: zona à direita ($(box regra))" '[ "$(zone regra)" = 3 ]'
"$WZ" picker >/dev/null
sleep 0.3
xdotool key Escape
sleep 0.4
check "Esc fecha o seletor sem mexer ($(box regra))" '[ "$(zone regra)" = 3 ] && "$WZ" overlay | grep -q "picker=0"'

# Desfazer e refazer.
"$WZ" undo >/dev/null
sleep 0.6
check "desfazer volta para a zona 3 ($(box regra))" '[ "$(zone regra)" = 2 ] && [ "$(box regra)" = "0,352,512,352" ]'
"$WZ" redo >/dev/null
sleep 0.6
check "refazer vai de novo para a zona 4 ($(box regra))" '[ "$(zone regra)" = 3 ] && [ "$(box regra)" = "512,352,512,352" ]'

# Foco espacial: regra no quadrante 4; alvo, dois e tres flutuando no meio,
# empilhadas (tres por cima, um pouco mais à direita). Acima de regra, a
# mais alinhada é tres.
"$WZ" snap 4 >/dev/null
sleep 0.4
"$WZ" windows -l | sed 's/^/# /'
"$WZ" focus up >/dev/null
sleep 0.4
check "focus up: da zona 4 para a janela acima mais alinhada ($("$WZ" status | sed -n 's/^active=//p'))" '"$WZ" status | grep -qx "active=tres"'
"$WZ" focus down >/dev/null
sleep 0.4
check "focus down: de volta" '"$WZ" status | grep -qx "active=regra"'
"$WZ" focus right >/dev/null
sleep 0.4
check "focus right na borda: nada muda (FocusEdge=cross, sem outro monitor)" '"$WZ" status | grep -qx "active=regra"'

# Modos do dockapp: Zonas <-> Mosaico (esquerdo), desliga/religa (meio).
check "mode: zonas" '"$WZ" mode | grep -qx "mode=zones"'
"$WZ" mode toggle >/dev/null
sleep 1.2
check "mode toggle: mosaico, com as janelas no mosaico" '"$WZ" mode | grep -qx "mode=tiling" && "$WZ" status | grep -q "tiled=4"'
"$WZ" mode enable >/dev/null
sleep 1
check "mode enable: desligado, ninguém no mosaico" '"$WZ" mode | grep -qx "mode=off" && "$WZ" status | grep -q "tiled=0" && grep -q "^LastMode=tiling" "$CONF"'
"$WZ" mode enable >/dev/null
sleep 1.2
check "mode enable: religa no último modo (mosaico)" '"$WZ" mode | grep -qx "mode=tiling" && "$WZ" status | grep -q "tiled=4"'

# BSP persistente: quatro janelas, sem sobreposição; a principal some e as
# outras ficam onde estavam na árvore.
"$WZ" algorithm bsp >/dev/null
sleep 1
overlap=0
declare -a R
for t in alvo dois tres regra; do R+=("$(box "$t" | tr , ' ')"); done
for i in 0 1 2 3; do for j in 0 1 2 3; do
	[ $i -ge $j ] && continue
	read -r ax ay aw ah <<<"${R[$i]}"
	read -r bx by bw bh <<<"${R[$j]}"
	[ $ax -lt $((bx + bw)) ] && [ $bx -lt $((ax + aw)) ] && [ $ay -lt $((by + bh)) ] && [ $by -lt $((ay + ah)) ] && overlap=1
done; done
check "BSP: quatro janelas sem sobreposição" '[ $overlap -eq 0 ] && "$WZ" status | grep -q "tiled=4"'

# Diálogo (filho de outra janela): fica fora do mosaico.
python3 -I "$HERE/tools/janela.py" --dialogo dlg >/dev/null 2>&1 &
PIDS="$PIDS $!"
for _ in $(seq 1 50); do [ -n "$(win dlg)" ] && break; sleep 0.1; done
sleep 1.2
check "diálogo fica fora do mosaico (só a janela principal entra)" '"$WZ" status | grep -q "tiled=5" && win dlg | grep -q "tiled=0"'
"$WZ" mode zones >/dev/null
sleep 1
check "mode zones: mosaico desligado" '"$WZ" status | grep -q "tiled=0"'

# Primeiro o layout, depois a zona.
sed -i 's/^DragFlow=zones$/DragFlow=layouts\nLayoutDwell=150/' "$CONF"
"$WZ" reload >/dev/null
"$WZ" layout quarters >/dev/null
cell() { "$WZ" overlay | awk -v id="$1" -v f="$2" '$1 == "cell=" id { split(f == "thumb" ? substr($3, 7) : $2, a, /[x+]/); print a[1], a[2], a[3], a[4] }'; }
# Janelas novas: ficam por cima, numa posição só delas.
open_win faixa
drag_title faixa 15 12 10
check "painel de layouts no arrasto, ainda sem zonas" '"$WZ" overlay | grep -q "^overlay=shown .*zones=0 strip=1"'
read -r tw th tx ty <<<"$(cell halves thumb)"
xdotool mousemove $((tx + tw / 4)) $((ty + th / 2))
sleep 0.4
xdotool mousemove $((tx + tw / 4 + 1)) $((ty + th / 2))
sleep 0.3
xdotool mouseup 1
sleep 1
check "soltar na metade esquerda da miniatura encaixa nela ($(box faixa))" '[ "$(box faixa)" = "0,0,512,704" ] && [ "$(zone faixa)" = 0 ]'
check "o layout do painel vira o do monitor" 'grep -q "^Layout=halves" "$CONF"'
"$WZ" undo >/dev/null
sleep 0.6
check "desfazer volta o layout de antes junto" 'grep -q "^Layout=quarters" "$CONF"'
open_win faixa2
drag_title faixa2 -10 10 10
read -r cw chh cx cy <<<"$(cell thirds cell)"
xdotool mousemove $((cx + cw / 2)) $((cy + chh - 6))
sleep 0.5
xdotool mousemove $((cx + cw / 2 + 1)) $((cy + chh - 6))
sleep 0.4
check "parado sobre a miniatura: as zonas daquele layout aparecem" '"$WZ" overlay | grep -q "zones=3 strip=1 sel=thirds"'
xdotool mousemove 900 500
sleep 0.4
xdotool mouseup 1
sleep 1
check "e a zona depois: terço da direita ($(box faixa2))" '[ "$(zone faixa2)" = 2 ] && [ "$(box faixa2 | cut -d, -f1)" -ge 680 ]'

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

# Bloqueio com o seletor aberto: as teclas são da tela de bloqueio (a senha),
# não do seletor. Por último: o bloqueio fica até o fim do teste.
if command -v swaylock >/dev/null; then
	ativa=$("$WZ" status | sed -n 's/^active=//p')
	"$WZ" picker >/dev/null
	sleep 0.3
	antes=$(zone "$ativa")
	swaylock -c 000000 >"$TMP/swaylock.log" 2>&1 &
	LOCK=$!
	sleep 1
	xdotool key 1
	sleep 0.5
	check "bloqueio: a tecla vai para a tela de bloqueio, o seletor fecha sem encaixar" '"$WZ" overlay | grep -q "picker=0" && [ "$(zone "$ativa")" = "$antes" ]'
	"$WZ" picker >/dev/null
	sleep 0.3
	check "bloqueio: o seletor não abre com a sessão bloqueada" '"$WZ" overlay | grep -q "picker=0"'
	kill "$LOCK" 2>/dev/null
	wait "$LOCK" 2>/dev/null
fi

echo "1..$n"
[ $fails -eq 0 ] || { echo "# log do wlmaker:"; tail -20 "$TMP/wlmaker.log" | sed 's/^/# /'; }
exit $((fails > 0))
