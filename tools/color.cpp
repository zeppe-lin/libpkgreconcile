// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include "color.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace pkgreconcile::tool {
namespace {

constexpr std::string_view reset{"\x1b[0m"};

std::string_view style_for(linediff::unified_record_kind kind) noexcept {
  switch (kind) {
  case linediff::unified_record_kind::old_header:
    return "\x1b[1;31m";
  case linediff::unified_record_kind::new_header:
    return "\x1b[1;32m";
  case linediff::unified_record_kind::hunk_header:
    return "\x1b[36m";
  case linediff::unified_record_kind::context:
    return {};
  case linediff::unified_record_kind::deletion:
    return "\x1b[31m";
  case linediff::unified_record_kind::insertion:
    return "\x1b[32m";
  case linediff::unified_record_kind::no_newline:
    return "\x1b[33m";
  }
  return {};
}

class ansi_unified_sink final : public linediff::unified_sink {
public:
  void write(linediff::unified_record_kind kind,
             std::string_view bytes) override {
    const std::string_view style = style_for(kind);
    if (style.empty()) {
      output_.append(bytes.data(), bytes.size());
      return;
    }

    std::size_t content_size = bytes.size();
    std::string_view line_ending;
    if (!bytes.empty() && bytes.back() == '\n') {
      content_size = bytes.size() - 1U;
      line_ending = "\n";
      if (content_size != 0U && bytes[content_size - 1U] == '\r') {
        --content_size;
        line_ending = "\r\n";
      }
    }

    output_.append(style.data(), style.size());
    output_.append(bytes.data(), content_size);
    output_.append(reset.data(), reset.size());
    output_.append(line_ending.data(), line_ending.size());
  }

  std::string take() {
    return std::move(output_);
  }

private:
  std::string output_;
};

} // namespace

bool color_enabled(color_mode mode,
                   const color_context& context) noexcept {
  switch (mode) {
  case color_mode::always:
    return true;
  case color_mode::never:
    return false;
  case color_mode::automatic:
    return context.terminal && !context.pager && !context.no_color;
  }
  return false;
}

std::string render_unified_display(
    const linediff::comparison& value,
    const linediff::unified_options& options,
    bool color) {
  if (!color) {
    return linediff::render_unified(value, options);
  }

  ansi_unified_sink sink;
  linediff::write_unified(value, sink, options);
  return sink.take();
}

} // namespace pkgreconcile::tool
