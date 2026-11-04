# SPDX-License-Identifier: GPL-2.0-or-later
project('wmake-zones', 'c',
  version: '0.1.0',
  license: 'GPL-2.0-or-later',
  meson_version: '>= 0.62',
  default_options: ['c_std=gnu11', 'warning_level=2', 'buildtype=debugoptimized', 'b_ndebug=if-release'])

cc = meson.get_compiler('c')
add_project_arguments(
  '-DWZ_VERSION="@0@"'.format(meson.project_version()),
  '-DWZ_LOCALEDIR="@0@"'.format(get_option('prefix') / get_option('localedir')),
  cc.get_supported_arguments('-Wno-unused-parameter', '-Wno-missing-field-initializers',
                             '-Wshadow', '-Wformat=2', '-Werror=implicit-function-declaration',
                             '-fstack-protector-strong'),
  language: 'c')
if get_option('nls')
  add_project_arguments('-DWZ_ENABLE_NLS', language: 'c')
endif

m = cc.find_library('m', required: false)

# Núcleo: geometria, mosaico, configuração, tema e IPC. Sem X11/Wayland;
# também é instalado como biblioteca estática para o wlmaker usar o mesmo
# cálculo e o mesmo arquivo de configuração.
engine_src = files(
  'src/engine/wz-layout.c',
  'src/engine/wz-tile.c',
  'src/engine/wz-config.c',
  'src/engine/wz-theme.c',
  'src/engine/wz-ipc.c',
)
engine_inc = include_directories('src/engine')
engine_lib = static_library('wmake-zones-engine', engine_src,
  include_directories: engine_inc, dependencies: [m],
  pic: true, install: get_option('engine_lib'))
engine_dep = declare_dependency(link_with: engine_lib, include_directories: engine_inc, dependencies: [m])
if get_option('engine_lib')
  install_headers('src/engine/wz-layout.h', 'src/engine/wz-tile.h', 'src/engine/wz-config.h',
                  'src/engine/wz-theme.h', 'src/engine/wz-ipc.h', subdir: 'wmake-zones')
  pkg = import('pkgconfig')
  pkg.generate(engine_lib, name: 'wmake-zones-engine',
               description: 'Núcleo de geometria e configuração do wmake-zones',
               subdirs: 'wmake-zones', libraries: [m])
endif

x11 = dependency('x11', required: get_option('x11'))
xi = dependency('xi', version: '>= 1.5', required: get_option('x11'))
xrandr = dependency('xrandr', version: '>= 1.5', required: get_option('x11'))
xext = dependency('xext', required: get_option('x11'))
xft = dependency('xft', required: get_option('x11'))
xrender = dependency('xrender', required: get_option('x11'))
have_x11 = x11.found() and xi.found() and xrandr.found() and xext.found() and xft.found() and xrender.found()

wl_client = dependency('wayland-client', required: get_option('wayland_dockapp'))
cairo = dependency('cairo', required: get_option('wayland_dockapp'))
wl_scanner = find_program('wayland-scanner', native: true, required: get_option('wayland_dockapp'))
have_wl = wl_client.found() and cairo.found() and wl_scanner.found()

if have_x11
  executable('wmake-zones',
    files('src/x11/wzx-main.c', 'src/x11/wzx-wm.c', 'src/x11/wzx-drag.c', 'src/x11/wzx-overlay.c',
          'src/x11/wzx-tile.c', 'src/x11/wzx-keys.c', 'src/x11/wzx-ipc.c', 'src/x11/wzx-dockapp.c'),
    dependencies: [engine_dep, x11, xi, xrandr, xext, xft, xrender],
    c_args: have_wl ? ['-DWZ_HAVE_WAYLAND_DOCKAPP'] : [],
    install: true)
endif

gtk = dependency('gtk+-3.0', version: '>= 3.24', required: get_option('prefs'))
if gtk.found()
  executable('wmake-zones-prefs',
    files('src/prefs/wzp-main.c', 'src/prefs/wzp-editor.c', 'src/prefs/wzp-draw.c'),
    dependencies: [engine_dep, gtk, m],
    install: true)
endif

if have_wl
  subdir('src/wayland')
endif

subdir('data')
if get_option('nls')
  subdir('po')
endif
if get_option('tests')
  subdir('tests')
endif

summary({'X11 (Window Maker)': have_x11, 'Preferências (GTK 3)': gtk.found(),
         'Dockapp Wayland (wlmaker)': have_wl}, section: 'Componentes')
