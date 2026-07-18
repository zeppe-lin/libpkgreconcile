// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LIBPKGRECONCILE_VISIBILITY_H
#define LIBPKGRECONCILE_VISIBILITY_H

#if defined(__GNUC__) || defined(__clang__)
#define PKGRECONCILE_API __attribute__((visibility("default")))
#define PKGRECONCILE_LOCAL __attribute__((visibility("hidden")))
#else
#define PKGRECONCILE_API
#define PKGRECONCILE_LOCAL
#endif

#endif
