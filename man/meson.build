# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later

scdoc = find_program('scdoc', required: get_option('man_pages'))
if scdoc.found()
  custom_target(
    'libpkgreconcile.3',
    input: 'libpkgreconcile.3.scdoc',
    output: 'libpkgreconcile.3',
    command: [scdoc],
    feed: true,
    capture: true,
    install: true,
    install_dir: get_option('mandir') / 'man3',
  )

  custom_target(
    'rejmerge.8',
    input: 'rejmerge.8.scdoc',
    output: 'rejmerge.8',
    command: [scdoc],
    feed: true,
    capture: true,
    install: get_option('install_tools'),
    install_dir: get_option('mandir') / 'man8',
  )
endif
