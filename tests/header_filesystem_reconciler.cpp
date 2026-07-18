// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include <libpkgreconcile/filesystem_reconciler.h>

#include <type_traits>

static_assert(std::is_constructible_v<pkgreconcile::filesystem_reconciler,
                                      pkgreconcile::filesystem_options>);
