#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Dockapp do X11 com o Window Maker de verdade, num Xvfb próprio: o ladrilho
# mostra o modo do serviço (Zonas, Mosaico, desligado) e as teclas da
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
press() { xdotool mousemove $((tx + $1)) $((ty + $2)) click 1; sleep 0.6; }
# Pixels do ladrilho (tela LCD): LED Z aceso (verde), LED M aceso (âmbar),
# ciano aceso (a zona destacada, a janela principal ou a pausa) e ciano meio
# aceso (as outras zonas e janelas: não há na pausa). Amarelo nunca (era o
# fundo de teste do pixmap, que aparecia sem a máscara do X Shape).
look() {
	import -window root -crop 64x64+"$tx"+"$ty" +repage "$TMP/t.png"
	python3 -I -c 'import sys
from PIL import Image
px = {c for _n, c in Image.open(sys.argv[1]).convert("RGB").getcolors(64 * 64)}
print(" ".join(k for k, c in (("Z", (0x58, 0xd8, 0x58)), ("M", (0xf0, 0xb0, 0x30)),
                              ("tela", (0x20, 0xb2, 0xac)), ("meia", (0x00, 0x49, 0x41)),
                              ("amarelo", (0xff, 0xff, 0x00))) if c in px))' "$TMP/t.png"
}

check "Zonas: LED Z aceso e as zonas na tela ($(look))" '[ "$(look)" = "Z tela meia" ]'
press 41 51
check "tecla M: modo Mosaico ($(mode))" '[ "$(mode)" = tiling ]'
check "Mosaico: LED M aceso e as janelas na tela ($(look))" '[ "$(look)" = "M tela meia" ]'
press 55 51
check "seta ▶ no Mosaico: o algoritmo seguinte ($(algo))" '[ "$(algo)" = grid ]'
press 8 51
check "seta ◀: volta ao anterior ($(algo))" '[ "$(algo)" = master ]'
press 41 51
check "M aceso: desliga ($(mode))" '[ "$(mode)" = off ]'
check "desligado: nenhum LED, só a pausa na tela ($(look))" '[ "$(look)" = "tela" ]'
a=$(algo)
press 55 51
check "seta com tudo desligado não faz nada" '[ "$(mode)" = off ] && [ "$(algo)" = "$a" ]'
press 22 51
check "tecla Z: modo Zonas ($(mode))" '[ "$(mode)" = zones ]'
l0=$("$WZ" status | sed -n 's/^layout=//p' | head -1)
press 55 51
check "seta ▶ nas Zonas: o layout seguinte" '[ "$("$WZ" status | sed -n "s/^layout=//p" | head -1)" != "$l0" ]'
press 32 20
check "clique na tela: alterna Zonas e Mosaico (clique esquerdo padrão) ($(mode))" '[ "$(mode)" = tiling ]'
check "o serviço continua no ar" '"$WZ" status >/dev/null 2>&1'

# As bordas de cada tecla (coordenadas do quadro de 64; o ladrilho de 60 fica
# em +2,+2): a área clicável é a desenhada, e as teclas que chegam à borda
# do ladrilho (3 px) são delas, não do Window Maker. Z 16..31, M 32..47,
# ◀ 4..12, ▶ 51..59; a fileira, y 46..59.
"$WZ" mode zones >/dev/null
sleep 0.4
# O Window Maker reacomoda o ícone solto depois de abri-lo (2 px): aqui cada
# pixel conta, então a posição de agora.
read -r tx ty < <(find_tile)
check "quadro do ladrilho, já assentado ($tx,$ty)" '[ -n "${tx:-}" ]'
press 32 59
check "M, canto de baixo à esquerda (32,59): Mosaico ($(mode))" '[ "$(mode)" = tiling ]'
press 47 46
check "M, canto de cima à direita (47,46): desliga ($(mode))" '[ "$(mode)" = off ]'
press 31 59
check "Z, último pixel (31,59; antes era do M): Zonas ($(mode))" '[ "$(mode)" = zones ]'
press 16 46
check "Z, primeiro pixel (16,46): desliga ($(mode))" '[ "$(mode)" = off ]'
press 31 46
check "Z (31,46): Zonas de novo ($(mode))" '[ "$(mode)" = zones ]'
l0=$("$WZ" status | sed -n 's/^layout=//p' | head -1)
press 59 59
l1=$("$WZ" status | sed -n 's/^layout=//p' | head -1)
check "▶ no último pixel (59,59; na borda do ladrilho): o layout seguinte ($l0 -> $l1)" '[ "$l1" != "$l0" ]'
press 4 46
l2=$("$WZ" status | sed -n 's/^layout=//p' | head -1)
check "◀ no primeiro pixel (4,46; na borda): volta ($l2)" '[ "$l2" = "$l0" ]'
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
# Arrastar o ladrilho solto até a coluna do Dock (no canto de cima, à
# direita, só com o ícone principal): ele entra no Dock.
xdotool mousemove $((tx + 32)) $((ty + 20))
sleep 0.3
xdotool mousedown 1
sleep 0.2
for i in $(seq 1 30); do
	xdotool mousemove $((tx + 32 + (992 - tx - 32) * i / 30)) $((ty + 20 + (84 - ty - 20) * i / 30))
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
check "arrastar o ladrilho até a coluna do Dock: ele entra no Dock (${docked:-não entrou})" '[ -n "$docked" ]'
[ -n "$docked" ] && read -r tx ty <<<"$docked"
mode_before=$(mode)
press 32 20
check "um clique (sem arrastar) continua trocando o modo ($mode_before -> $(mode))" '[ "$(mode)" != "$mode_before" ]'
echo "1..$n"
[ "$fails" = 0 ]
