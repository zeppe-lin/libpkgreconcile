# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later

project(
  'libpkgreconcile',
  'cpp',
  version: '0.1.0',
  license: 'GPL-3.0-or-later',
  meson_version: '>=1.6.0',
  default_options: [
    'cpp_std=c++17',
    'warning_level=3',
    'default_library=shared',
    'b_ndebug=if-release',
  ],
)

library_kind = get_option('default_library')
if library_kind == 'both'
  error('libpkgreconcile requires separate shared and static builds')
endif

want_static = get_option('link_mode') == 'static'
if (library_kind == 'static') != want_static
  error('default_library and link_mode must both select shared or static')
endif

liblinediff_dep = dependency(
  'liblinediff',
  version: '>=0.2.0',
  required: true,
  static: want_static,
)

pkgconf = import('pkgconfig')

if get_option('install_tools') and get_option('tools').disabled()
  error('install_tools requires tools to be enabled')
endif
public_inc = include_directories('include')
private_inc = include_directories('src')

libpkgreconcile = library(
  'pkgreconcile',
  [
    'src/types.cpp',
    'src/candidate.cpp',
    'src/filesystem_reconciler.cpp',
  ],
  include_directories: [public_inc, private_inc],
  dependencies: liblinediff_dep,
  install: true,
  version: meson.project_version(),
  soversion: '0',
  gnu_symbol_visibility: 'hidden',
  cpp_args: ['-fvisibility-inlines-hidden'],
)

libpkgreconcile_dep = declare_dependency(
  link_with: libpkgreconcile,
  include_directories: public_inc,
  dependencies: liblinediff_dep,
)
meson.override_dependency('libpkgreconcile', libpkgreconcile_dep)

install_headers(
  'include/libpkgreconcile/visibility.h',
  'include/libpkgreconcile/error.h',
  'include/libpkgreconcile/types.h',
  'include/libpkgreconcile/candidate.h',
  'include/libpkgreconcile/filesystem_reconciler.h',
  'include/libpkgreconcile/libpkgreconcile.h',
  subdir: 'libpkgreconcile',
)

pkgconf.generate(
  libpkgreconcile,
  name: 'libpkgreconcile',
  filebase: 'libpkgreconcile',
  description: 'Filesystem-staged package object reconciliation library',
  subdirs: '.',
  requires: ['liblinediff >= 0.2.0'],
)

if get_option('tools').allowed()
  subdir('tools')
endif
subdir('man')
if get_option('tests').allowed()
  subdir('tests')
endif
