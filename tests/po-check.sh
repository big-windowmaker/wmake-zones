#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Tradução: a lista de fontes (po/POTFILES) existe e está completa, o POT
# do repositório é o que o código extrai hoje, rótulos do GTK não ficam sem
# marcação e cada catálogo compila. Saída TAP.
#
#   tests/po-check.sh SOURCE_DIR
#
# Falha alto quando falta a lista ou um arquivo dela (um teste que pula em
# silêncio por olhar o diretório errado não guarda nada).

set -u
S=${1:?uso: $0 SOURCE_DIR}
n=0
fails=0
ok() { n=$((n + 1)); echo "ok $n - $*"; }
nok() { n=$((n + 1)); fails=$((fails + 1)); echo "not ok $n - $*"; }

P=$S/po/POTFILES
POT=$S/po/wmake-zones.pot
if [ ! -s "$P" ] || [ ! -s "$POT" ] || [ ! -s "$S/po/LINGUAS" ]; then
	echo "1..1"
	echo "not ok 1 - po/POTFILES, po/LINGUAS e po/wmake-zones.pot existem em $S"
	exit 1
fi

missing=""
while read -r f; do
	case $f in '' | '#'*) continue ;; esac
	[ -f "$S/$f" ] || missing="$missing $f"
done <"$P"
if [ -z "$missing" ]; then ok "todos os arquivos do POTFILES existem"; else nok "faltam no repositório:$missing"; fi

# Todo fonte com texto marcado está na lista.
unlisted=""
while read -r f; do
	rel=${f#"$S"/}
	grep -qxF "$rel" "$P" || unlisted="$unlisted $rel"
done < <(grep -rlE '(^|[^A-Za-z0-9_])(N?_|g_dngettext|ngettext)\(' "$S/src" --include='*.c' | sort)
if [ -z "$unlisted" ]; then ok "todo fonte com _() ou N_() está no POTFILES"; else nok "fora do POTFILES:$unlisted"; fi

# Texto de interface passado cru ao GTK (sem _()): rótulo, botão, título,
# dica, nome acessível. Literais só com pontuação, números ou um nome de
# programa ficam de fora.
raw=$(grep -nE '(gtk_label_new|gtk_button_new_with_(label|mnemonic)|gtk_check_button_new_with_(label|mnemonic)|gtk_window_set_title|gtk_widget_set_tooltip_text|gtk_frame_new|gtk_label_set_text|atk_object_set_name)\(([^,"]*, *)?"[^"]*[A-Za-z]{2}' \
	"$S"/src/prefs/*.c 2>/dev/null | grep -vE '"wmake-zones"' || true)
if [ -z "$raw" ]; then ok "nenhum rótulo do GTK sem marcação"; else nok "rótulos sem _(): $raw"; fi

if ! command -v xgettext >/dev/null || ! command -v msgcmp >/dev/null || ! command -v msgfmt >/dev/null; then
	echo "# SKIP: sem gettext (xgettext, msgcmp, msgfmt)"
	echo "1..$n"
	[ "$fails" -eq 0 ]
	exit
fi

TMP=$(mktemp -d "${TMPDIR:-/tmp}/wzpo.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
# As mesmas palavras-chave do preset "glib" do meson (po/meson.build).
if (cd "$S" && xgettext --package-name=wmake-zones --from-code=UTF-8 --add-comments \
	-k_ -kN_ -kC_:1c,2 -kNC_:1c,2 -kg_dcgettext:2 -kg_dngettext:2,3 -kg_dpgettext2:2c,3 \
	-f po/POTFILES -o "$TMP/fresh.pot" 2>"$TMP/xgettext.log"); then
	ok "xgettext extrai as fontes do POTFILES"
else
	nok "xgettext falhou: $(cat "$TMP/xgettext.log")"
fi
count=$(grep -c '^msgid ' "$TMP/fresh.pot" 2>/dev/null || echo 0)
if [ "$count" -gt 100 ]; then ok "extração com $count mensagens"; else nok "extração quase vazia ($count mensagens)"; fi
# msgcmp: toda mensagem da referência existe no outro arquivo; nos dois
# sentidos, o POT do repositório é igual ao extraído.
if msgcmp --use-untranslated --use-fuzzy "$POT" "$TMP/fresh.pot" 2>"$TMP/a.log" &&
	msgcmp --use-untranslated --use-fuzzy "$TMP/fresh.pot" "$POT" 2>"$TMP/b.log"; then
	ok "po/wmake-zones.pot está em dia com o código (meson compile wmake-zones-pot)"
else
	nok "po/wmake-zones.pot desatualizado; rode 'meson compile -C BUILD wmake-zones-pot': $(cat "$TMP/a.log" "$TMP/b.log" 2>/dev/null | head -c 600)"
fi

while read -r lang; do
	case $lang in '' | '#'*) continue ;; esac
	po=$S/po/$lang.po
	if [ ! -f "$po" ]; then
		nok "po/$lang.po (LINGUAS) existe"
		continue
	fi
	# Um catálogo por chamada: vários juntos se fundem e acusam duplicatas.
	if msgfmt --check -o /dev/null "$po" 2>"$TMP/$lang.log"; then
		ok "msgfmt --check po/$lang.po ($(msgfmt --statistics -o /dev/null "$po" 2>&1))"
	else
		nok "msgfmt --check po/$lang.po: $(cat "$TMP/$lang.log")"
	fi
	if msgcmp --use-untranslated --use-fuzzy "$po" "$POT" 2>/dev/null; then
		ok "po/$lang.po mesclado com o POT"
	else
		nok "po/$lang.po sem as mensagens novas do POT (msgmerge)"
	fi
done <"$S/po/LINGUAS"

echo "1..$n"
[ "$fails" -eq 0 ]
