// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include <libpkgreconcile/libpkgreconcile.h>

#include <type_traits>

int main() {
  static_assert(!std::is_default_constructible_v<pkgreconcile::candidate>);
  static_assert(std::is_copy_constructible_v<pkgreconcile::candidate>);

  return pkgreconcile::to_string(pkgreconcile::node_type::regular) == nullptr ||
                 pkgreconcile::to_string(
                     pkgreconcile::candidate_state::changed) == nullptr
             ? 1
             : 0;
}
