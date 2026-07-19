// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_color.cpp
 * @brief Tool-local ANSI rendering and color policy tests.
 */

#include "test_support.hpp"

#include "../tools/color.h"

#include <string>

#include <liblinediff/compare.h>

using pkgreconcile::tool::color_context;
using pkgreconcile::tool::color_enabled;
using pkgreconcile::tool::color_mode;
using pkgreconcile::tool::render_unified_display;
using test_support::runner;

namespace {

linediff::unified_options options() {
  linediff::unified_options result;
  result.old_label = "old";
  result.new_label = "new";
  result.context = 1U;
  return result;
}

} // namespace

int main() {
  runner tests;

  tests.run("automatic color requires a direct terminal", [] {
    TEST_CHECK(color_enabled(color_mode::automatic,
                             color_context{true, false, false}));
    TEST_CHECK(!color_enabled(color_mode::automatic,
                              color_context{false, false, false}));
    TEST_CHECK(!color_enabled(color_mode::automatic,
                              color_context{true, true, false}));
    TEST_CHECK(!color_enabled(color_mode::automatic,
                              color_context{true, false, true}));
  });

  tests.run("explicit modes override runtime context", [] {
    TEST_CHECK(color_enabled(color_mode::always,
                             color_context{false, true, true}));
    TEST_CHECK(!color_enabled(color_mode::never,
                              color_context{true, false, false}));
  });

  tests.run("plain display is byte-identical to liblinediff", [] {
    const auto value = linediff::compare("a\n", "b\n");
    TEST_CHECK_EQ(render_unified_display(value, options(), false),
                  linediff::render_unified(value, options()));
  });

  tests.run("semantic records receive fixed ANSI styles", [] {
    const auto value = linediff::compare(
        "same\nold\r\nlast", "same\nnew\r\nlast2");
    const std::string output = render_unified_display(value, options(), true);
    TEST_CHECK_EQ(
        output,
        std::string("\x1b[1;31m--- old\x1b[0m\n"
                    "\x1b[1;32m+++ new\x1b[0m\n"
                    "\x1b[36m@@ -1,3 +1,3 @@\x1b[0m\n"
                    " same\n"
                    "\x1b[31m-old\x1b[0m\r\n"
                    "\x1b[31m-last\x1b[0m\n"
                    "\x1b[33m\\ No newline at end of file\x1b[0m\n"
                    "\x1b[32m+new\x1b[0m\r\n"
                    "\x1b[32m+last2\x1b[0m\n"
                    "\x1b[33m\\ No newline at end of file\x1b[0m\n"));
  });

  tests.run("context records remain unstyled", [] {
    const std::string output = render_unified_display(
        linediff::compare("a\nb\nc\n", "a\nx\nc\n"), options(), true);
    TEST_CHECK(output.find(" a\n") != std::string::npos);
    TEST_CHECK(output.find(" c\n") != std::string::npos);
    TEST_CHECK(output.find("\x1b[0m a\n") == std::string::npos);
  });

  tests.run("NUL bytes remain exact inside styled records", [] {
    const std::string output = render_unified_display(
        linediff::compare(std::string("a\0b\n", 4),
                          std::string("a\0c\n", 4)),
        options(), true);
    std::string old_record{"\x1b[31m-a"};
    old_record.push_back('\0');
    old_record.append("b\x1b[0m\n");
    std::string new_record{"\x1b[32m+a"};
    new_record.push_back('\0');
    new_record.append("c\x1b[0m\n");
    TEST_CHECK(output.find(old_record) != std::string::npos);
    TEST_CHECK(output.find(new_record) != std::string::npos);
  });

  tests.run("identical input emits no ANSI state", [] {
    TEST_CHECK(render_unified_display(
                   linediff::compare("same\n", "same\n"), options(), true)
                   .empty());
  });

  return tests.finish();
}
