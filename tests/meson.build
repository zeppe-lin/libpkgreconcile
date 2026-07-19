# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later

python = import('python').find_installation('python3')

scan_test = executable(
  'test-scan',
  'test_scan.cpp',
  dependencies: [libpkgreconcile_dep, liblinediff_dep],
)
test('scan filesystem staging tree', scan_test)

actions_test = executable(
  'test-actions',
  'test_actions.cpp',
  dependencies: [libpkgreconcile_dep, liblinediff_dep],
)
test('apply explicit dispositions', actions_test)

compare_test = executable(
  'test-compare',
  'test_compare.cpp',
  dependencies: [libpkgreconcile_dep, liblinediff_dep],
)
test('compare candidates with liblinediff', compare_test)

stale_test = executable(
  'test-stale',
  'test_stale.cpp',
  dependencies: [libpkgreconcile_dep, liblinediff_dep],
)
test('reject stale candidates', stale_test)

header_test = executable(
  'test-header',
  [
    'test_header.cpp',
    'header_visibility.cpp',
    'header_error.cpp',
    'header_types.cpp',
    'header_candidate.cpp',
    'header_filesystem_reconciler.cpp',
  ],
  dependencies: [libpkgreconcile_dep, liblinediff_dep],
)
test('compile public umbrella header', header_test)

if library_kind == 'shared'
  test(
    'hide private shared-library symbols',
    python,
    args: [files('test_symbols.py'), libpkgreconcile.full_path()],
  )
endif

if get_option('tools').allowed()
  color_test = executable(
    'test-color',
    [
      'test_color.cpp',
      '../tools/color.cpp',
    ],
    include_directories: include_directories('../tools'),
    dependencies: [libpkgreconcile_dep, liblinediff_dep],
  )
  test('render semantic terminal colors', color_test)

  terminal_test = executable(
    'test-terminal',
    [
      'test_terminal.cpp',
      '../tools/color.cpp',
      '../tools/terminal.cpp',
    ],
    include_directories: include_directories('../tools'),
    dependencies: [libpkgreconcile_dep, liblinediff_dep],
  )
  test('exercise reference terminal workflow', terminal_test)

  test(
    'exercise rejmerge command line',
    python,
    args: [files('test_cli.py'), rejmerge],
    timeout: 120,
  )
endif
