// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include <libpkgreconcile/error.h>

#include <type_traits>

static_assert(std::is_base_of_v<std::runtime_error, pkgreconcile::error>);
