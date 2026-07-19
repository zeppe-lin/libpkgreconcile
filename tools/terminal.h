// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LIBPKGRECONCILE_TOOL_TERMINAL_H
#define LIBPKGRECONCILE_TOOL_TERMINAL_H

#include <cstddef>
#include <string>

#include <libpkgreconcile/libpkgreconcile.h>

#include "color.h"

namespace pkgreconcile::tool {

struct run_options {
  filesystem_options filesystem;
  bool dry_run{false};
  color_mode color{color_mode::automatic};
  std::string editor;
  std::string pager;
};

struct run_summary {
  std::size_t inspected{0};
  std::size_t resolved{0};
  std::size_t skipped{0};
};

run_summary run_terminal(const run_options& options = {});

} // namespace pkgreconcile::tool

#endif
