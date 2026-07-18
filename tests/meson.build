# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later

header_test = executable(
  'test-header',
  [
    'test_header.cpp',
    'header_visibility.cpp',
    'header_error.cpp',
    'header_types.cpp',
    'header_candidate.cpp',
  ],
  dependencies: libpkgreconcile_dep,
)
test('compile public model headers', header_test)
