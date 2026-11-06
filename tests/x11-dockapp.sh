#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Dockapp do X11 com o Window Maker de verdade, num Xvfb próprio: o ladrilho
# mostra o modo do serviço (Zonas, Mosaico, desligado) e as teclas da
# fileira de baixo fazem o que dizem. Saída TAP.
#
#   tests/x11-dockapp.sh BUILD_DIR

set -u
B=${1:?uso: $0 BUILD_DIR}
WZ=$B/wmake-zones
XVFB=${XVFB:-$(command -v Xvfb || true)}
n=0
fails=0
ok() { n=$((n + 1)); echo "ok $n - $*"; }
nok() { n=$((n + 1)); fails=$((fails + 1)); echo "not ok $n - $*"; }
check() { if eval "$2"; then ok "$1"; else nok "$1"; fi; }

for tool in wmaker xdotool import python3; do
	command -v $tool >/dev/null || { echo "1..0 # SKIP sem $tool"; exit 0; }
done
[ -x "$XVFB" ] || { echo "1..0 # SKIP sem Xvfb"; exit 0; }
python3 -c 'import PIL' 2>/dev/null || { echo "1..0 # SKIP sem python-pillow"; exit 0; }

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
# baixo, solto, sem Dock).
read -r tx ty < <(python3 - <<'PY'
import re, subprocess
out = subprocess.run(["xwininfo", "-root", "-children"], capture_output=True, text=True).stdout
for m in re.finditer(r"(0x[0-9a-f]+) .*?\s(\d+)x(\d+)\+(-?\d+)\+(-?\d+)", out):
    w, h, x, y = map(int, m.group(2, 3, 4, 5))
    if (w, h) == (64, 64) and y >= 600:
        print(x, y)
        break
PY
)
check "ladrilho do dockapp aberto" '[ -n "${tx:-}" ]'
[ -n "${tx:-}" ] || { echo "1..$n"; exit 1; }

mode() { "$WZ" status | sed -n 's/^mode=//p' | head -1; }
algo() { "$WZ" status | sed -n 's/^algorithm=//p' | head -1; }
press() { xdotool mousemove $((tx + $1)) $((ty + $2)) click 1; sleep 0.6; }
# Pixels do ladrilho: LED Z aceso (verde), LED M aceso (âmbar), barra âmbar
# da janela principal (Mosaico), azul da zona destacada (Zonas).
look() {
	import -window root -crop 64x64+"$tx"+"$ty" +repage "$TMP/t.png"
	python3 -I -c 'import sys
from PIL import Image
px = {c for _n, c in Image.open(sys.argv[1]).convert("RGB").getcolors(64 * 64)}
print(" ".join(k for k, c in (("Z", (0x58, 0xd8, 0x58)), ("M", (0xf0, 0xb0, 0x30)),
                              ("barra", (0xe0, 0xa0, 0x30)), ("zona", (0x7f, 0xb0, 0xe0)),
                              ("pausa", (0x5e, 0x60, 0x66))) if c in px))' "$TMP/t.png"
}

check "Zonas: LED Z aceso e a zona destacada na tela ($(look))" '[ "$(look)" = "Z zona" ]'
press 41 51
check "tecla M: modo Mosaico ($(mode))" '[ "$(mode)" = tiling ]'
check "Mosaico: LED M aceso e a barra da janela principal ($(look))" '[ "$(look)" = "M barra" ]'
press 55 51
check "seta ▶ no Mosaico: o algoritmo seguinte ($(algo))" '[ "$(algo)" = grid ]'
press 8 51
check "seta ◀: volta ao anterior ($(algo))" '[ "$(algo)" = master ]'
press 41 51
check "M aceso: desliga ($(mode))" '[ "$(mode)" = off ]'
check "desligado: nenhum LED, a pausa na tela ($(look))" '[ "$(look)" = "pausa" ]'
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
echo "1..$n"
[ "$fails" = 0 ]
