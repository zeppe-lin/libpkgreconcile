# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later

rejmerge = executable(
  'rejmerge',
  [
    'main.cpp',
    'terminal.cpp',
  ],
  cpp_args: ['-DREJMERGE_VERSION="' + meson.project_version() + '"'],
  include_directories: include_directories('.'),
  dependencies: [libpkgreconcile_dep, liblinediff_dep],
  install: get_option('install_tools'),
  install_dir: get_option('sbindir'),
)
