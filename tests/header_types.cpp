// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include <libpkgreconcile/types.h>

static_assert(sizeof(pkgreconcile::metadata) >= sizeof(mode_t));
