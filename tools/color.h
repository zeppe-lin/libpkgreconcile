// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LIBPKGRECONCILE_TOOL_COLOR_H
#define LIBPKGRECONCILE_TOOL_COLOR_H

#include <string>

#include <liblinediff/render.h>

namespace pkgreconcile::tool {

/** Terminal color policy selected by the reference frontend. */
enum class color_mode {
  /** Color direct terminal output unless NO_COLOR is set. */
  automatic,

  /** Emit color regardless of terminal or pager detection. */
  always,

  /** Never emit color. */
  never,
};

/** Runtime facts used to resolve an automatic color policy. */
struct color_context {
  /** Whether the final output descriptor is a terminal. */
  bool terminal{false};

  /** Whether output will pass through a pager of unknown capabilities. */
  bool pager{false};

  /** Whether the NO_COLOR convention is active. */
  bool no_color{false};
};

/**
 * Resolve a color mode against the current output context.
 *
 * Automatic mode colors only direct terminal output and honors NO_COLOR.
 * Always and never are explicit overrides.
 */
bool color_enabled(color_mode mode,
                   const color_context& context) noexcept;

/**
 * Render a unified difference for terminal presentation.
 *
 * Plain rendering is byte-identical to linediff::render_unified(). Colored
 * rendering styles semantic records supplied by linediff::write_unified().
 * Document bytes are not parsed or normalized.
 */
std::string render_unified_display(
    const linediff::comparison& value,
    const linediff::unified_options& options,
    bool color);

} // namespace pkgreconcile::tool

#endif
