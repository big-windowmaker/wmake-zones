# SPDX-License-Identifier: GPL-2.0-or-later
# Dockapp nativo do wlmaker (protocolo de ícones do wlmaker), sem XWayland.
wl_protocols = dependency('wayland-protocols', required: get_option('wayland_dockapp'))
protos = [
  meson.current_source_dir() / 'protocols/wlmaker-icon-unstable-v1.xml',
  wl_protocols.get_variable('pkgdatadir') / 'stable/xdg-shell/xdg-shell.xml',
]
wl_src = []
foreach xml : protos
  base = xml.split('/')[-1].replace('.xml', '')
  wl_src += custom_target(base + '-h', input: xml, output: base + '-client-protocol.h',
                          command: [wl_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
  wl_src += custom_target(base + '-c', input: xml, output: base + '-protocol.c',
                          command: [wl_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
endforeach
executable('wmake-zones-wl', files('wzw-dockapp.c') + wl_src,
  dependencies: [engine_dep, wl_client, cairo, m],
  install: true)
