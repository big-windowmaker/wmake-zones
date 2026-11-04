#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Medições do serviço X11 num Xvfb próprio com o wmaker: PSS e acordadas em
# repouso, tempo do mosaico com 10/30/50 janelas, tempo do overlay e do
# encaixe, memória depois de ciclos de ativação. Imprime "chave=valor".
#
#   tests/perf-x11.sh BUILD_DIR

set -u
B=${1:?uso: $0 BUILD_DIR}
WZ=$B/wmake-zones
XVFB=${XVFB:-$(command -v Xvfb || true)}
for t in wmaker xdotool xprop xterm; do command -v $t >/dev/null || { echo "faltando=$t"; exit 0; }; done
[ -x "$XVFB" ] || { echo "faltando=Xvfb"; exit 0; }

TMP=$(mktemp -d "${TMPDIR:-/tmp}/wzp.XXXXXX")
PIDS=""
cleanup() {
	for p in $PIDS; do kill "$p" 2>/dev/null; done
	sleep 0.3
	for p in $PIDS; do kill -9 "$p" 2>/dev/null; done
	[ -n "${XPID:-}" ] && kill -9 "$XPID" 2>/dev/null
	rm -rf "$TMP"
}
trap cleanup EXIT
export HOME=$TMP/home GNUSTEP_USER_ROOT=$TMP/home/GNUstep XDG_CONFIG_HOME=$TMP/home/.config
export XDG_RUNTIME_DIR=$TMP/run XDG_SESSION_TYPE=x11 WZ_DEBUG=1
unset WAYLAND_DISPLAY
mkdir -p "$GNUSTEP_USER_ROOT/Defaults" "$XDG_CONFIG_HOME/wmake-zones" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
printf '{\n  WindowPlacement = manual;\n}\n' >"$GNUSTEP_USER_ROOT/Defaults/WindowMaker"
printf '[General]\nZones=true\nLayout=quarters\nShowDelay=0\n' >"$XDG_CONFIG_HOME/wmake-zones/wmake-zones.conf"
exec 9>"$TMP/dfd"
"$XVFB" -displayfd 9 -screen 0 1920x1080x24 -nolisten tcp >/dev/null 2>&1 &
XPID=$!
exec 9>&-
for _ in $(seq 1 50); do [ -s "$TMP/dfd" ] && break; sleep 0.1; done
export DISPLAY=:$(head -1 "$TMP/dfd")
wmaker --no-autolaunch >/dev/null 2>&1 &
PIDS="$PIDS $!"
sleep 2
"$WZ" >"$TMP/wz.log" 2>&1 &
ZP=$!
PIDS="$PIDS $ZP"
sleep 1

pss() { awk '/^Pss:/ {print $2}' /proc/$ZP/smaps_rollup; }
cs() { awk '/ctxt_switches/ {s += $2} END {print s}' /proc/$ZP/status; }
ticks() { awk '{print $14 + $15}' /proc/$ZP/stat; }
xterms() {
	local want=$1 have
	for _ in $(seq 1 $((want - $(count)))); do
		xterm -geometry 40x8+$((RANDOM % 1200))+$((RANDOM % 700)) >/dev/null 2>&1 &
		PIDS="$PIDS $!"
	done
	for _ in $(seq 1 100); do
		have=$(count)
		[ "$have" -ge "$want" ] && break
		sleep 0.1
	done
	sleep 1
}
count() { xprop -root _NET_CLIENT_LIST | grep -o '0x[0-9a-f]*' | wc -l; }
last_time() { grep "tempo: $1" "$TMP/wz.log" | tail -1 | sed 's/.* em \([0-9]*\) µs.*/\1/'; }
# Maior tempo registrado depois da linha $2 do log (a passada que moveu tudo).
max_time() { tail -n +"$2" "$TMP/wz.log" | grep "tempo: $1" | sed 's/.* em \([0-9]*\) µs.*/\1/' | sort -n | tail -1; }
loglines() { wc -l <"$TMP/wz.log"; }

echo "pss_inicio_kib=$(pss)"
c0=$(cs) t0=$(ticks)
sleep 10
echo "repouso_10s_acordadas=$(($(cs) - c0)) repouso_10s_ticks=$(($(ticks) - t0))"

for n in 10 30 50; do
	xterms $n
	l0=$(($(loglines) + 1))
	"$WZ" autotile on >/dev/null
	sleep 1.5
	echo "janelas=$n mosaico_max_us=$(max_time mosaico "$l0") pss_kib=$(pss)"
	"$WZ" autotile off >/dev/null
	sleep 1
done

# Arrasto com overlay e encaixe: uma janela nova, por cima das outras.
xterm -geometry 60x10+300+300 >/dev/null 2>&1 &
PIDS="$PIDS $!"
sleep 1.5
W=$(printf %d "$(xprop -root _NET_ACTIVE_WINDOW | grep -o '0x[0-9a-f]*' | tail -1)")
eval "$(xdotool getwindowgeometry --shell "$W")"
xdotool mousemove $((X + 30)) $((Y - 10))
sleep 0.2
xdotool mousedown 1
for _ in $(seq 1 10); do xdotool mousemove_relative -- 30 20; sleep 0.08; done
sleep 0.5
xdotool mouseup 1
sleep 0.5
echo "overlay_us=$(last_time overlay) encaixe_us=$(last_time encaixe)"

p0=$(pss)
ciclos() {
for _ in $(seq 1 "$1"); do
	"$WZ" autotile on >/dev/null
	sleep 0.3
	"$WZ" autotile off >/dev/null
	"$WZ" layout next >/dev/null
	"$WZ" preview 'V(H(1,1),H(1,1))' >/dev/null
	sleep 0.2
done
sleep 2
}
ciclos 20
p20=$(pss)
ciclos 40
echo "pss_antes_ciclos_kib=$p0 pss_depois_20_ciclos_kib=$p20 pss_depois_60_ciclos_kib=$(pss)"
c0=$(cs) t0=$(ticks)
sleep 10
echo "repouso_final_10s_acordadas=$(($(cs) - c0)) repouso_final_10s_ticks=$(($(ticks) - t0))"
