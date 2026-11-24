#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Dockapp do X11 com o Window Maker de verdade, num Xvfb próprio: o ladrilho
# mostra o modo do serviço (Zonas, Mosaico, Rolagem, desligado) e as teclas da
# fileira de baixo fazem o que dizem. Saída TAP.
#
#   tests/x11-dockapp.sh BUILD_DIR

# shellcheck disable=SC2016,SC2034,SC2155 # as expressões do check rodam depois, por eval
set -u
B=${1:?uso: $0 BUILD_DIR}
WZ=$B/wmake-zones
XVFB=${XVFB:-$(command -v Xvfb || true)}
n=0
fails=0
ok() { n=$((n + 1)); echo "ok $n - $*"; }
nok() { n=$((n + 1)); fails=$((fails + 1)); echo "not ok $n - $*"; }
check() { if eval "$2"; then ok "$1"; else nok "$1"; fi; }

for tool in wmaker xdotool xwininfo import python3; do
	command -v $tool >/dev/null || { echo "1..0 # SKIP sem $tool"; exit 0; }
done
[ -x "$XVFB" ] || { echo "1..0 # SKIP sem Xvfb"; exit 0; }
python3 -I -c 'import PIL' 2>/dev/null || { echo "1..0 # SKIP sem python-pillow"; exit 0; }

TMP=$(mktemp -d "${TMPDIR:-/tmp}/wzd.XXXXXX")
[ ${#TMP} -le 60 ] || { rmdir "$TMP"; TMP=$(mktemp -d /tmp/wzd.XXXXXX); }
PIDS=""
cleanup() {
	for p in $PIDS; do kill "$p" 2>/dev/null; done
	sleep 0.3
	[ -n "${XPID:-}" ] && kill "$XPID" 2>/dev/null
	rm -rf "$TMP"
}
trap cleanup EXIT

export HOME=$TMP/home GNUSTEP_USER_ROOT=$TMP/home/GNUstep XDG_CONFIG_HOME=$TMP/home/.config
export XDG_RUNTIME_DIR=$TMP/run XDG_SESSION_TYPE=x11 LANG=C
unset WAYLAND_DISPLAY
mkdir -p "$GNUSTEP_USER_ROOT/Defaults" "$XDG_CONFIG_HOME/wmake-zones" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
printf '{\n  WindowPlacement = auto;\n}\n' >"$GNUSTEP_USER_ROOT/Defaults/WindowMaker"
printf '[General]\nZones=true\nAutotile=false\nLayout=quarters\n[Tiling]\nAlgorithm=master\n' \
	>"$XDG_CONFIG_HOME/wmake-zones/wmake-zones.conf"

exec 9>"$TMP/dfd"
"$XVFB" -displayfd 9 -screen 0 1024x700x24 -nolisten tcp >"$TMP/xvfb.log" 2>&1 &
XPID=$!
exec 9>&-
for _ in $(seq 1 50); do [ -s "$TMP/dfd" ] && break; sleep 0.1; done
[ -s "$TMP/dfd" ] || { echo "1..0 # SKIP Xvfb não subiu"; exit 0; }
export DISPLAY=:$(head -1 "$TMP/dfd")
wmaker --no-autolaunch >"$TMP/wm.log" 2>&1 &
PIDS="$PIDS $!"
sleep 2
"$WZ" --dockapp >"$TMP/wz.log" 2>&1 &
PIDS="$PIDS $!"
for _ in $(seq 1 50); do "$WZ" status >/dev/null 2>&1 && break; sleep 0.1; done
sleep 1

# O ladrilho: o ícone de aplicativo do wmaker (quadro 64x64 na fileira de
# baixo, solto, sem Dock). Numa máquina lenta (CI) ele demora: até 10 s.
# O quadro vem da própria janela do dockapp (60x60, centrada no quadro de
# 64): a posição absoluta dela menos a dentro do quadro. Um 64x64 qualquer
# na parte de baixo pode ser outro ícone, ou o quadro antes de o Window
# Maker o assentar (2 px acima).
find_tile() {
	python3 - <<'PY'
import re, subprocess
out = subprocess.run(["xwininfo", "-root", "-tree"], capture_output=True, text=True).stdout
for m in re.finditer(r"(0x[0-9a-f]+) .*?\s(\d+)x(\d+)\+(-?\d+)\+(-?\d+)\s+\+(-?\d+)\+(-?\d+)", out):
    w, h, rx, ry, ax, ay = map(int, m.group(2, 3, 4, 5, 6, 7))
    if (w, h) == (60, 60) and (rx, ry) == (2, 2) and ay >= 600:
        g = subprocess.run(["xwininfo", "-id", m.group(1)], capture_output=True, text=True).stdout
        if "IsViewable" in g:
            print(ax - rx, ay - ry)
            break
PY
}
tx=
for _ in $(seq 1 50); do
	read -r tx ty < <(find_tile)
	[ -n "${tx:-}" ] && break
	sleep 0.2
done
sleep 0.5
check "ladrilho do dockapp aberto (${tx:-?},${ty:-?})" '[ -n "${tx:-}" ]'
if [ -z "${tx:-}" ]; then
	echo "# janelas na raiz:"; xwininfo -root -children | sed 's/^/# /' | head -30
	echo "# log do serviço:"; sed 's/^/# /' "$TMP/wz.log" | tail -10
fi
[ -n "${tx:-}" ] || { echo "1..$n"; exit 1; }

mode() { "$WZ" status | sed -n 's/^mode=//p' | head -1; }
algo() { "$WZ" status | sed -n 's/^algorithm=//p' | head -1; }
layout() { "$WZ" status | sed -n 's/^layout=//p' | head -1; }
press() { xdotool mousemove $((tx + $1)) $((ty + $2)) click 1; sleep 0.6; }
# Pixels do ladrilho: tecla Z acesa (verde), M (amarela), R (azul-claro);
# na tela, o LED ciano aceso (contornos, pausa), o apagado (zonas, janelas,
# colunas) e o médio (coluna ativa e colunas escondidas). Amarelo puro nunca
# (era o fundo de teste do pixmap, que aparecia sem a máscara do X Shape).
look() {
	import -window root -crop 64x64+"$tx"+"$ty" +repage "$TMP/t.png"
	python3 -I -c 'import sys
from PIL import Image
px = {c for _n, c in Image.open(sys.argv[1]).convert("RGB").getcolors(64 * 64)}
print(" ".join(k for k, c in (("Z", (0x6f, 0xdc, 0x6f)), ("M", (0xf2, 0xc8, 0x4b)), ("R", (0x8c, 0xd2, 0xf5)),
                              ("aceso", (0x20, 0xb2, 0xae)), ("apagado", (0x00, 0x49, 0x41)),
                              ("medio", (0x18, 0x8a, 0x86)),
                              ("amarelo", (0xff, 0xff, 0x00))) if c in px))' "$TMP/t.png"
}

# Coordenadas do quadro de 64 (o ladrilho de 60 fica em +2,+2), como o
# wmpulsemixer: 4 px de borda em volta; a tela em 4..59 x 4..43; as teclas
# Z 5..22, M 23..40, R 41..58, y 47..58. Sem setas desde a 0.5.1.
check "Zonas: tecla Z acesa e as zonas na tela ($(look))" '[ "$(look)" = "Z aceso apagado" ]'
press 31 53
check "tecla M: modo Mosaico ($(mode))" '[ "$(mode)" = tiling ]'
check "Mosaico: tecla M acesa e as janelas na tela ($(look))" '[ "$(look)" = "M aceso apagado" ]'
press 31 53
check "M aceso: desliga ($(mode))" '[ "$(mode)" = off ]'
check "desligado: nenhuma tecla acesa, só a pausa na tela ($(look))" '[ "$(look)" = "aceso" ]'
press 13 53
check "tecla Z: modo Zonas ($(mode))" '[ "$(mode)" = zones ]'
# Clique na tela (o padrão): a próxima opção do modo, sem trocar de modo.
l0=$(layout)
press 32 20
check "clique na tela, Zonas: o próximo layout ($l0 -> $(layout))" '[ "$(mode)" = zones ] && [ "$(layout)" != "$l0" ]'
press 31 53
a0=$(algo)
press 32 20
check "clique na tela, Mosaico: o próximo algoritmo ($a0 -> $(algo))" '[ "$(mode)" = tiling ] && [ "$(algo)" != "$a0" ]'
press 50 53
check "tecla R no Mosaico: Rolagem ($(mode))" '[ "$(mode)" = scroll ]'
check "Rolagem: tecla R acesa e as colunas na tela ($(look))" '[ "$(look)" = "R aceso apagado medio" ]'
press 50 53
check "R aceso: desliga ($(mode))" '[ "$(mode)" = off ]'
press 50 53
check "tecla R: modo Rolagem ($(mode))" '[ "$(mode)" = scroll ]'
# Na Rolagem, o clique na tela passa a coluna ativa à próxima largura.
if command -v xterm >/dev/null; then
	xterm -name wzcoluna -geometry 40x10 -e sleep 120 >"$TMP/xterm.log" 2>&1 &
	PIDS="$PIDS $!"
	cw=
	for _ in $(seq 1 30); do
		cw=$(xdotool search --onlyvisible --classname '^wzcoluna$' 2>/dev/null | head -1)
		[ -n "$cw" ] && break
		sleep 0.2
	done
	[ -n "$cw" ] || sed 's/^/# xterm: /' "$TMP/xterm.log"
	sleep 1
	width() { [ -n "$cw" ] && xwininfo -id "$cw" | sed -n 's/^ *Width: //p'; }
	w0=$(width)
	# Solto, o ladrilho fica embaixo da coluna (no Dock, a área útil o
	# deixa de fora): o quadro dele por cima antes do clique.
	read -r tx ty < <(find_tile)
	fr=$(xwininfo -root -children | awk -v p="64x64\\\\+$tx\\\\+$ty" '$0 ~ p {print $1; exit}')
	[ -n "$fr" ] && xdotool windowraise "$fr"
	sleep 0.3
	press 32 20
	sleep 0.5
	w1=$(width)
	check "clique na tela, Rolagem: a próxima largura da coluna ($w0 -> $w1 px)" '[ "$(mode)" = scroll ] && [ -n "$w0" ] && [ "${w1:-0}" -gt "$w0" ]'
	kill "${PIDS##* }" 2>/dev/null
	sleep 0.5
fi
"$WZ" mode tiling >/dev/null; sleep 0.4
check "o serviço continua no ar" '"$WZ" status >/dev/null 2>&1'

# As bordas de cada tecla: a área clicável é a desenhada; fora da tela e das
# teclas (borda, vão, moldura) o clique é do Window Maker e não muda o modo.
"$WZ" mode zones >/dev/null
sleep 0.4
# O Window Maker reacomoda o ícone solto depois de abri-lo (2 px): aqui cada
# pixel conta, então a posição de agora.
read -r tx ty < <(find_tile)
check "quadro do ladrilho, já assentado ($tx,$ty)" '[ -n "${tx:-}" ]'
press 23 58
check "M, canto de baixo à esquerda (23,58): Mosaico ($(mode))" '[ "$(mode)" = tiling ]'
press 40 47
check "M, canto de cima à direita (40,47): desliga ($(mode))" '[ "$(mode)" = off ]'
press 22 58
check "Z, último pixel (22,58): Zonas ($(mode))" '[ "$(mode)" = zones ]'
press 5 47
check "Z, primeiro pixel (5,47): desliga ($(mode))" '[ "$(mode)" = off ]'
press 58 58
check "R, último pixel (58,58): Rolagem ($(mode))" '[ "$(mode)" = scroll ]'
press 41 47
check "R, primeiro pixel (41,47): desliga ($(mode))" '[ "$(mode)" = off ]'
for pt in "4 53" "31 46" "31 45" "31 59" "59 20" "3 20"; do
	read -r px py <<<"$pt"
	press "$px" "$py"
	check "borda/vão ($px,$py): do Window Maker, o modo não muda ($(mode))" '[ "$(mode)" = off ]'
done
press 31 44
check "última linha da tela (31,44)? não: é a moldura ($(mode))" '[ "$(mode)" = off ]'
press 31 42
check "dentro da tela (31,42), desligado: religa ($(mode))" '[ "$(mode)" != off ]'
check "nenhum amarelo no ladrilho ($(look))" '! look | grep -qw amarelo'
"$WZ" mode tiling >/dev/null
sleep 0.4

# O ladrilho cobre o ícone e pede os cliques; arrastar e o menu do Window
# Maker chegam a ele como eventos sintéticos (send_event) na janela do
# ícone. Borda, clique direito (apertado, como a mão): o menu do Window
# Maker (aqui o do ícone solto) muda a tela em volta do ladrilho; soltar
# fora dele o fecha.
around() {
	import -window root -crop 300x300+"$tx"+$((ty - 236)) +repage "$TMP/$1.png"
}
changed() {
	python3 -I -c 'import sys
from PIL import Image, ImageChops
d = ImageChops.difference(Image.open(sys.argv[1]).convert("RGB"), Image.open(sys.argv[2]).convert("RGB"))
print(sum(1 for p in d.getdata() if p != (0, 0, 0)))' "$TMP/$1.png" "$TMP/$2.png"
}
xdotool mousemove 500 200
sleep 2 # o balão da última troca de modo some
around a0
xdotool mousemove $((tx + 1)) $((ty + 30)) mousedown 3
sleep 0.8
around a1
c1=$(changed a0 a1)
check "borda, clique direito: o menu do Window Maker ($c1 pontos mudaram)" '[ "$c1" -gt 2000 ]'
xdotool mousemove 500 200 mouseup 3
sleep 0.8
around a2
c2=$(changed a0 a2)
check "soltar fora do menu o fecha ($c2 pontos diferentes)" '[ "$c2" -lt 2000 ]'
# Arrastar o ladrilho solto pela borda (o vão entre a tela e as teclas, que
# é do Window Maker) até a coluna do Dock (no canto de cima, à direita, só
# com o ícone principal): ele entra no Dock.
xdotool mousemove $((tx + 32)) $((ty + 45))
sleep 0.3
xdotool mousedown 1
sleep 0.2
for i in $(seq 1 30); do
	xdotool mousemove $((tx + 32 + (992 - tx - 32) * i / 30)) $((ty + 45 + (84 - ty - 45) * i / 30))
	sleep 0.06
done
sleep 0.3
xdotool mouseup 1
sleep 1.2
docked=$(python3 - <<'PY'
import re, subprocess
out = subprocess.run(["xwininfo", "-root", "-children"], capture_output=True, text=True).stdout
for m in re.finditer(r"(0x[0-9a-f]+) .*?\s(\d+)x(\d+)\+(-?\d+)\+(-?\d+)", out):
    w, h, x, y = map(int, m.group(2, 3, 4, 5))
    if (w, h) == (64, 64) and x >= 960 and 32 <= y <= 160:
        g = subprocess.run(["xwininfo", "-id", m.group(1)], capture_output=True, text=True).stdout
        if "IsViewable" in g:
            print(x, y)
PY
)
check "arrastar o ladrilho pela borda até a coluna do Dock: ele entra no Dock (${docked:-não entrou})" '[ -n "$docked" ]'
[ -n "$docked" ] && read -r tx ty <<<"$docked"
a0=$(algo)
press 32 20
check "no Dock, um clique (sem arrastar) na tela continua trocando o algoritmo ($a0 -> $(algo))" '[ "$(algo)" != "$a0" ]'
echo "1..$n"
[ "$fails" = 0 ]
