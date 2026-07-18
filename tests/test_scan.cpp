// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_filesystem_reconciler_scan.cpp
 * @brief Classification, ordering, binary, and resource-limit tests.
 */

#include "test_support.hpp"

#include <libpkgreconcile/libpkgreconcile.h>
#include <liblinediff/liblinediff.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;
using test_support::runner;
using test_support::test_root;
using test_support::write_file;

namespace {

pkgreconcile::candidate
find_candidate(const std::vector<pkgreconcile::candidate>& entries,
               const fs::path& relative) {
  const auto iterator = std::find_if(entries.begin(), entries.end(),
                                     [&](const pkgreconcile::candidate& value) {
                                       return value.relative_path() == relative;
                                     });
  if (iterator == entries.end()) {
    test_support::fail("candidate exists", __FILE__, __LINE__,
                       relative.string());
  }
  return *iterator;
}

pkgreconcile::filesystem_reconciler
make_filesystem_reconciler(const test_root& root) {
  return pkgreconcile::filesystem_reconciler(
      {root.path(), "var/lib/pkg/rejected"});
}

} // namespace

int main() {
  runner tests;

  tests.run("empty staging tree", [] {
    test_root root;
    TEST_CHECK(make_filesystem_reconciler(root).scan().empty());
  });
  tests.run("missing staging tree is error", [] {
    test_root root;
    fs::remove_all(root.rejected_root());
    pkgreconcile::filesystem_reconciler filesystem_reconciler(
        {root.path(), "var/lib/pkg/rejected"});
    TEST_CHECK_THROWS(filesystem_reconciler.scan(), std::runtime_error);
  });
  tests.run("staging root symlink is rejected", [] {
    test_root root;
    fs::remove_all(root.rejected_root());
    fs::create_directories(root.installed("external"));
    fs::create_symlink(root.installed("external"), root.rejected_root());
    TEST_CHECK_THROWS(make_filesystem_reconciler(root).scan(),
                      pkgreconcile::error);
  });
  tests.run("staged symlink directory is not traversed", [] {
    test_root root;
    write_file(root.installed("external/secret"), "data\n");
    fs::create_symlink(root.installed("external"),
                       root.rejected("directory-link"));
    const auto values = make_filesystem_reconciler(root).scan();
    const auto link = find_candidate(values, "directory-link");
    TEST_CHECK(link.staged_metadata().type == pkgreconcile::node_type::symlink);
    TEST_CHECK(std::none_of(
        values.begin(), values.end(), [](const pkgreconcile::candidate& value) {
          return value.relative_path() == fs::path("directory-link/secret");
        }));
  });
  tests.run("changed text file", [] {
    test_root root;
    write_file(root.installed("etc/a.conf"), "x=1\n");
    write_file(root.rejected("etc/a.conf"), "x=2\n");
    const auto entries = make_filesystem_reconciler(root).scan();
    const auto& value = find_candidate(entries, "etc/a.conf");
    TEST_CHECK(value.state() == pkgreconcile::candidate_state::changed);
    TEST_CHECK(!value.content_equal());
    TEST_CHECK(value.text_mergeable());
  });
  tests.run("duplicate file", [] {
    test_root root;
    write_file(root.installed("etc/a.conf"), "x=1\n");
    write_file(root.rejected("etc/a.conf"), "x=1\n");
    ::chmod(root.installed("etc/a.conf").c_str(), 0644);
    ::chmod(root.rejected("etc/a.conf").c_str(), 0644);
    const auto& value =
        find_candidate(make_filesystem_reconciler(root).scan(), "etc/a.conf");
    TEST_CHECK(value.state() == pkgreconcile::candidate_state::duplicate);
    TEST_CHECK(value.content_equal());
    TEST_CHECK(value.metadata_equal());
    TEST_CHECK(value.text_mergeable());
  });
  tests.run("metadata-only file", [] {
    test_root root;
    write_file(root.installed("etc/a.conf"), "x=1\n");
    write_file(root.rejected("etc/a.conf"), "x=1\n");
    ::chmod(root.installed("etc/a.conf").c_str(), 0644);
    ::chmod(root.rejected("etc/a.conf").c_str(), 0600);
    const auto& value =
        find_candidate(make_filesystem_reconciler(root).scan(), "etc/a.conf");
    TEST_CHECK(value.state() == pkgreconcile::candidate_state::metadata_only);
    TEST_CHECK(value.content_equal());
    TEST_CHECK(!value.metadata_equal());
  });
  tests.run("binary changed file", [] {
    test_root root;
    write_file(root.installed("etc/a.bin"), std::string("a\0b", 3));
    write_file(root.rejected("etc/a.bin"), std::string("a\0c", 3));
    pkgreconcile::filesystem_reconciler filesystem_reconciler =
        make_filesystem_reconciler(root);
    const auto entries = filesystem_reconciler.scan();
    const auto& value = find_candidate(entries, "etc/a.bin");
    TEST_CHECK(value.state() == pkgreconcile::candidate_state::changed);
    TEST_CHECK(!value.text_mergeable());
    TEST_CHECK_THROWS(filesystem_reconciler.compare_text(value),
                      std::invalid_argument);
  });
  tests.run("binary duplicate file", [] {
    test_root root;
    const std::string bytes("a\0b", 3);
    write_file(root.installed("etc/a.bin"), bytes);
    write_file(root.rejected("etc/a.bin"), bytes);
    const auto& value =
        find_candidate(make_filesystem_reconciler(root).scan(), "etc/a.bin");
    TEST_CHECK(value.state() == pkgreconcile::candidate_state::duplicate);
    TEST_CHECK(!value.text_mergeable());
  });
  tests.run("symlink duplicate", [] {
    test_root root;
    fs::create_directories(root.installed("etc"));
    fs::create_directories(root.rejected("etc"));
    fs::create_symlink("target", root.installed("etc/link"));
    fs::create_symlink("target", root.rejected("etc/link"));
    const auto& value =
        find_candidate(make_filesystem_reconciler(root).scan(), "etc/link");
    TEST_CHECK(value.installed_metadata().type ==
               pkgreconcile::node_type::symlink);
    TEST_CHECK(value.state() == pkgreconcile::candidate_state::duplicate);
  });
  tests.run("symlink changed", [] {
    test_root root;
    fs::create_directories(root.installed("etc"));
    fs::create_directories(root.rejected("etc"));
    fs::create_symlink("old", root.installed("etc/link"));
    fs::create_symlink("new", root.rejected("etc/link"));
    const auto& value =
        find_candidate(make_filesystem_reconciler(root).scan(), "etc/link");
    TEST_CHECK(value.state() == pkgreconcile::candidate_state::changed);
    TEST_CHECK(!value.content_equal());
  });
  tests.run("type changed", [] {
    test_root root;
    write_file(root.installed("etc/node"), "file\n");
    fs::create_directories(root.rejected("etc"));
    fs::create_symlink("target", root.rejected("etc/node"));
    const auto& value =
        find_candidate(make_filesystem_reconciler(root).scan(), "etc/node");
    TEST_CHECK(value.state() == pkgreconcile::candidate_state::type_changed);
  });
  tests.run("regular relic", [] {
    test_root root;
    write_file(root.rejected("etc/obsolete.conf"), "preserved\n");
    const auto& value = find_candidate(make_filesystem_reconciler(root).scan(),
                                       "etc/obsolete.conf");
    TEST_CHECK(value.state() == pkgreconcile::candidate_state::relic);
    TEST_CHECK(value.installed_metadata().type ==
               pkgreconcile::node_type::missing);
  });
  tests.run("symlink relic", [] {
    test_root root;
    fs::create_directories(root.rejected("etc"));
    fs::create_symlink("target", root.rejected("etc/link"));
    const auto& value =
        find_candidate(make_filesystem_reconciler(root).scan(), "etc/link");
    TEST_CHECK(value.state() == pkgreconcile::candidate_state::relic);
    TEST_CHECK(value.staged_metadata().type ==
               pkgreconcile::node_type::symlink);
  });
  tests.run("structural absent directory is not relic", [] {
    test_root root;
    write_file(root.rejected("newdir/file"), "value\n");
    const auto entries = make_filesystem_reconciler(root).scan();
    find_candidate(entries, "newdir/file");
    const auto iterator =
        std::find_if(entries.begin(), entries.end(),
                     [](const pkgreconcile::candidate& value) {
                       return value.relative_path() == "newdir";
                     });
    TEST_CHECK(iterator == entries.end());
  });
  tests.run("paired directory metadata", [] {
    test_root root;
    fs::create_directories(root.installed("etc/sub"));
    fs::create_directories(root.rejected("etc/sub"));
    ::chmod(root.installed("etc/sub").c_str(), 0755);
    ::chmod(root.rejected("etc/sub").c_str(), 0700);
    const auto& value =
        find_candidate(make_filesystem_reconciler(root).scan(), "etc/sub");
    TEST_CHECK(value.staged_metadata().type ==
               pkgreconcile::node_type::directory);
    TEST_CHECK(value.state() == pkgreconcile::candidate_state::metadata_only);
  });
  tests.run("special file classification", [] {
    test_root root;
    fs::create_directories(root.installed("run"));
    fs::create_directories(root.rejected("run"));
    TEST_CHECK(::mkfifo(root.installed("run/fifo").c_str(), 0600) == 0);
    TEST_CHECK(::mkfifo(root.rejected("run/fifo").c_str(), 0600) == 0);
    const auto& value =
        find_candidate(make_filesystem_reconciler(root).scan(), "run/fifo");
    TEST_CHECK(value.installed_metadata().type ==
               pkgreconcile::node_type::other);
    TEST_CHECK(value.state() == pkgreconcile::candidate_state::changed);
  });
  tests.run("non-directories before deepest directories", [] {
    test_root root;
    fs::create_directories(root.installed("a/b"));
    fs::create_directories(root.rejected("a/b"));
    write_file(root.installed("a/b/z"), "old\n");
    write_file(root.rejected("a/b/z"), "new\n");
    const auto entries = make_filesystem_reconciler(root).scan();
    const auto file_position =
        std::find_if(entries.begin(), entries.end(),
                     [](const pkgreconcile::candidate& value) {
                       return value.relative_path() == "a/b/z";
                     });
    const auto deep_position =
        std::find_if(entries.begin(), entries.end(),
                     [](const pkgreconcile::candidate& value) {
                       return value.relative_path() == "a/b";
                     });
    const auto shallow_position =
        std::find_if(entries.begin(), entries.end(),
                     [](const pkgreconcile::candidate& value) {
                       return value.relative_path() == "a";
                     });
    TEST_CHECK(file_position < deep_position);
    TEST_CHECK(deep_position < shallow_position);
  });
  tests.run("pathname beginning with two dots", [] {
    test_root root;
    write_file(root.installed("etc/..local"), "old\n");
    write_file(root.rejected("etc/..local"), "new\n");
    find_candidate(make_filesystem_reconciler(root).scan(), "etc/..local");
  });
  tests.run("pathname with spaces", [] {
    test_root root;
    write_file(root.installed("etc/name with spaces"), "old\n");
    write_file(root.rejected("etc/name with spaces"), "new\n");
    find_candidate(make_filesystem_reconciler(root).scan(),
                   "etc/name with spaces");
  });
  tests.run("pathname with newline", [] {
    test_root root;
    const fs::path relative = fs::path("etc") / std::string("name\nline");
    write_file(root.installed(relative), "old\n");
    write_file(root.rejected(relative), "new\n");
    find_candidate(make_filesystem_reconciler(root).scan(), relative);
  });
  tests.run("absolute staging path is rejected", [] {
    test_root root;
    TEST_CHECK_THROWS(pkgreconcile::filesystem_reconciler(
                          {root.path(), "/var/lib/pkg/rejected"}),
                      std::invalid_argument);
  });
  tests.run("staging parent traversal rejected", [] {
    test_root root;
    TEST_CHECK_THROWS(pkgreconcile::filesystem_reconciler(
                          {root.path(), "var/lib/../outside"}),
                      std::invalid_argument);
  });
  tests.run("zero byte limit rejected", [] {
    test_root root;
    pkgreconcile::filesystem_options options;
    options.root = root.path();
    options.text_limits.max_input_bytes = 0U;
    TEST_CHECK_THROWS((void)pkgreconcile::filesystem_reconciler{options},
                      std::invalid_argument);
  });
  tests.run("text byte limit enforced by comparison", [] {
    test_root root;
    write_file(root.installed("etc/a"), "old\n");
    write_file(root.rejected("etc/a"), "new\n");
    pkgreconcile::filesystem_options options;
    options.root = root.path();
    options.text_limits.max_input_bytes = 3U;
    pkgreconcile::filesystem_reconciler reconciler(options);
    const auto value = find_candidate(reconciler.scan(), "etc/a");
    TEST_CHECK_THROWS(reconciler.compare_text(value), linediff::limit_error);
  });
  tests.run("comparison does not execute PATH programs", [] {
    test_root root;
    write_file(root.installed("etc/a"), "old\n");
    write_file(root.rejected("etc/a"), "new\n");
    pkgreconcile::filesystem_reconciler reconciler =
        make_filesystem_reconciler(root);
    const auto value = find_candidate(reconciler.scan(), "etc/a");
    const char* previous = ::getenv("PATH");
    const std::string saved = previous == nullptr ? "" : previous;
    ::setenv("PATH", "", 1);
    const linediff::comparison comparison = reconciler.compare_text(value);
    ::setenv("PATH", saved.c_str(), 1);
    TEST_CHECK_EQ(linediff::apply(comparison), std::string("new\n"));
  });

  return tests.finish();
}
