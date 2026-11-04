# SPDX-License-Identifier: GPL-2.0-or-later
datadir = get_option('datadir')
install_data('icons/wmake-zones.svg', install_dir: datadir / 'icons/hicolor/scalable/apps')
# PNGs para o Dock do Window Maker (que não lê SVG em todos os temas).
meson.add_install_script('install-icons.sh', meson.current_source_dir() / 'icons/wmake-zones.svg')
install_data('applications/wmake-zones-prefs.desktop', 'applications/wmake-zones-editor.desktop',
             'applications/wmake-zones-dockapp.desktop', install_dir: datadir / 'applications')
install_data('autostart/wmake-zones.desktop', install_dir: get_option('sysconfdir') / 'xdg/autostart')
install_data('wmake-zones.conf.example', install_dir: datadir / 'doc/wmake-zones')
