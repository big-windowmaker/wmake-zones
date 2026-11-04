# SPDX-License-Identifier: GPL-2.0-or-later
test_engine = executable('test-engine', 'test-engine.c', dependencies: [engine_dep])
test('engine', test_engine, timeout: 120)
x11_func = find_program('x11-functional.sh')
if have_x11
  test('x11-functional', x11_func, args: [meson.project_build_root()], protocol: 'tap', timeout: 240,
       is_parallel: false)
endif
