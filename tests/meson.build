# SPDX-License-Identifier: GPL-2.0-or-later
test_engine = executable('test-engine', 'test-engine.c', dependencies: [engine_dep])
test('engine', test_engine, timeout: 120)
x11_func = find_program('x11-functional.sh')
if have_x11
  test('x11-functional', x11_func, args: [meson.project_build_root()], protocol: 'tap', timeout: 240,
       is_parallel: false)
endif
x11_multi = find_program('x11-multimonitor.sh')
if have_x11
  # Dois heads num Xephyr (pulado sem Xephyr): Xinerama, Dock, janela que
  # passa a caber, janela levada para outro monitor.
  test('x11-multimonitor', x11_multi, args: [meson.project_build_root()], protocol: 'tap', timeout: 180,
       is_parallel: false)
endif
x11_dock = find_program('x11-dockapp.sh')
if have_x11
  # Ladrilho do dockapp: o modo na tela e as teclas da fileira de baixo.
  test('x11-dockapp', x11_dock, args: [meson.project_build_root()], protocol: 'tap', timeout: 120,
       is_parallel: false)
  # O mesmo sem o X Shape (servidores sem a extensão): o fundo do pai e a
  # cópia só das partes desenhadas, o mesmo visual.
  test('x11-dockapp-noshape', x11_dock, args: [meson.project_build_root()], protocol: 'tap', timeout: 120,
       is_parallel: false, env: ['WZX_NO_SHAPE=1'])
endif

# Wayland: o wlmaker do Big WindowMaker aninhado num Xvfb (WLMAKER=caminho,
# senão o do PATH). Sem ele, ou com um wlmaker sem as zonas, SKIP: isso não
# prova nada, só não bloqueia quem não tem o compositor.
# Usa o cliente wmake-zones, que só existe com o X11.
if have_x11
  wl_func = find_program('wayland-functional.sh')
  test('wayland-functional', wl_func, args: [meson.project_build_root()], protocol: 'tap', timeout: 300,
       is_parallel: false, suite: 'wayland')
endif
# Tradução: POTFILES completo, POT em dia com o código, catálogos válidos.
po_check = find_program('po-check.sh')
test('po', po_check, args: [meson.project_source_root()], protocol: 'tap')
