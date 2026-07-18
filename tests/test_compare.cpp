// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test_support.hpp"

#include <libpkgreconcile/libpkgreconcile.h>
#include <liblinediff/liblinediff.h>

#include <algorithm>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using test_support::runner;
using test_support::test_root;
using test_support::write_file;

namespace {

pkgreconcile::candidate
find_candidate(const std::vector<pkgreconcile::candidate>& values,
               const fs::path& relative) {
  const auto iterator = std::find_if(values.begin(), values.end(),
                                     [&](const pkgreconcile::candidate& value) {
                                       return value.relative_path() == relative;
                                     });
  if (iterator == values.end())
    test_support::fail("candidate exists", __FILE__, __LINE__,
                       relative.string());
  return *iterator;
}

pkgreconcile::filesystem_reconciler make_reconciler(const test_root& root) {
  pkgreconcile::filesystem_options options;
  options.root = root.path();
  return pkgreconcile::filesystem_reconciler(options);
}

pkgreconcile::candidate prepare(test_root& root,
                                pkgreconcile::filesystem_reconciler& engine,
                                const std::string& installed,
                                const std::string& staged) {
  write_file(root.installed("etc/value"), installed);
  write_file(root.rejected("etc/value"), staged);
  return find_candidate(engine.scan(), "etc/value");
}

std::string random_text(std::mt19937_64& generator) {
  static const std::vector<std::string> records{
      "", "a", "b", "alpha", "beta", "x\r", "\xff", "spaces here"};
  std::uniform_int_distribution<std::size_t> line_count(0U, 24U);
  std::uniform_int_distribution<std::size_t> record(0U, records.size() - 1U);
  std::bernoulli_distribution terminated(0.85);

  std::string output;
  const std::size_t count = line_count(generator);
  for (std::size_t index = 0; index < count; ++index) {
    output += records[record(generator)];
    if (index + 1U != count || terminated(generator))
      output.push_back('\n');
  }
  return output;
}

} // namespace

int main() {
  runner tests;

  tests.run("comparison owns exact input bytes", [] {
    test_root root;
    auto engine = make_reconciler(root);
    const auto value = prepare(root, engine, "old\r\nlast", "new\r\nlast\n");
    const linediff::comparison comparison = engine.compare_text(value);
    TEST_CHECK_EQ(comparison.old_document().bytes(),
                  std::string("old\r\nlast"));
    TEST_CHECK_EQ(comparison.new_document().bytes(),
                  std::string("new\r\nlast\n"));
    TEST_CHECK_EQ(linediff::apply(comparison),
                  comparison.new_document().bytes());
  });

  tests.run("consumer renders timestamp-free unified output", [] {
    test_root root;
    auto engine = make_reconciler(root);
    const auto value = prepare(root, engine, "a\nb\nc\n", "a\nx\nc\n");
    linediff::unified_options options;
    options.old_label = value.installed_path().string();
    options.new_label = value.staged_path().string();
    options.context = 0U;
    const std::string output =
        linediff::render_unified(engine.compare_text(value), options);
    TEST_CHECK(output.find("--- " + value.installed_path().string()) == 0U);
    TEST_CHECK(output.find("@@ -2 +2 @@\n-b\n+x\n") != std::string::npos);
  });

  tests.run("consumer renders literal conflict regions", [] {
    test_root root;
    auto engine = make_reconciler(root);
    const auto value = prepare(root, engine, "old\n", "new\n");
    linediff::conflict_options options;
    options.old_label = "installed";
    options.new_label = "staged";
    const auto rendered =
        linediff::render_conflicts(engine.compare_text(value), options);
    TEST_CHECK_EQ(rendered.conflicts, 1U);
    TEST_CHECK(rendered.text.find("<<<<<<< installed\nold\n=======\nnew\n") ==
               0U);
  });

  tests.run("invalid UTF-8 remains opaque bytes", [] {
    test_root root;
    auto engine = make_reconciler(root);
    const auto value = prepare(root, engine, std::string("a\xff\n", 3),
                               std::string("b\xfe\n", 3));
    const auto comparison = engine.compare_text(value);
    TEST_CHECK_EQ(linediff::apply(comparison), std::string("b\xfe\n", 3));
  });

  tests.run("binary candidates reject text comparison", [] {
    test_root root;
    auto engine = make_reconciler(root);
    const auto value =
        prepare(root, engine, std::string("a\0b", 3), std::string("a\0c", 3));
    TEST_CHECK(!value.text_mergeable());
    TEST_CHECK_THROWS(engine.compare_text(value), std::invalid_argument);
  });

  tests.run("non-regular candidates reject text comparison", [] {
    test_root root;
    fs::create_directories(root.installed("etc"));
    fs::create_directories(root.rejected("etc"));
    fs::create_symlink("old", root.installed("etc/value"));
    fs::create_symlink("new", root.rejected("etc/value"));
    auto engine = make_reconciler(root);
    const auto value = find_candidate(engine.scan(), "etc/value");
    TEST_CHECK_THROWS(engine.compare_text(value), std::invalid_argument);
  });

  tests.run("line limit propagates from liblinediff", [] {
    test_root root;
    write_file(root.installed("etc/value"), "a\nb\n");
    write_file(root.rejected("etc/value"), "a\nc\n");
    pkgreconcile::filesystem_options options;
    options.root = root.path();
    options.text_limits.max_lines = 1U;
    pkgreconcile::filesystem_reconciler engine(options);
    const auto value = find_candidate(engine.scan(), "etc/value");
    TEST_CHECK_THROWS(engine.compare_text(value), linediff::limit_error);
  });

  tests.run("trace limit propagates from liblinediff", [] {
    test_root root;
    write_file(root.installed("etc/value"), "a\nb\nc\nd\n");
    write_file(root.rejected("etc/value"), "w\nx\ny\nz\n");
    pkgreconcile::filesystem_options options;
    options.root = root.path();
    options.text_limits.max_trace_bytes = 1U;
    pkgreconcile::filesystem_reconciler engine(options);
    const auto value = find_candidate(engine.scan(), "etc/value");
    TEST_CHECK_THROWS(engine.compare_text(value), linediff::limit_error);
  });

  tests.run("random textual candidates reconstruct staged bytes", [] {
    std::mt19937_64 generator(0x5a17c0deULL);
    for (std::size_t iteration = 0; iteration < 2000U; ++iteration) {
      test_root root;
      auto engine = make_reconciler(root);
      std::string installed = random_text(generator);
      std::string staged = random_text(generator);
      const auto value = prepare(root, engine, installed, staged);
      const auto comparison = engine.compare_text(value);
      TEST_CHECK_EQ(comparison.old_document().bytes(), installed);
      TEST_CHECK_EQ(comparison.new_document().bytes(), staged);
      TEST_CHECK_EQ(linediff::apply(comparison), staged);
      TEST_CHECK_EQ(comparison.identical(), installed == staged);
    }
  });

  return tests.finish();
}
