// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include <libpkgreconcile/candidate.h>

#include <type_traits>

static_assert(!std::is_default_constructible_v<pkgreconcile::candidate>);
