// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include <libpkgreconcile/libpkgreconcile.h>

#include <type_traits>

static_assert(std::is_copy_constructible_v<pkgreconcile::candidate>);
static_assert(std::is_move_constructible_v<pkgreconcile::candidate>);
static_assert(!std::is_default_constructible_v<pkgreconcile::candidate>);
static_assert(
    std::is_same_v<decltype(std::declval<const pkgreconcile::candidate&>()
                                .relative_path()),
                   const std::filesystem::path&>);

int main() {
  pkgreconcile::filesystem_options options;
  return options.staging_directory.empty() ? 1 : 0;
}
