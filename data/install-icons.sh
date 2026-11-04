#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Rasteriza o ícone SVG nos tamanhos do hicolor (o Dock do Window Maker e
# alguns temas leem só PNG). Chamado pelo meson install; respeita DESTDIR.
set -e
svg=$1
prefix=${MESON_INSTALL_DESTDIR_PREFIX:?}
command -v rsvg-convert >/dev/null || { echo "rsvg-convert ausente: só o SVG foi instalado"; exit 0; }
for s in 16 24 32 48 64 128; do
	d=$prefix/share/icons/hicolor/${s}x$s/apps
	mkdir -p "$d"
	rsvg-convert -w $s -h $s -o "$d/wmake-zones.png" "$svg"
done
