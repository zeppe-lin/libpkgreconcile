// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_filesystem_reconciler_actions.cpp
 * @brief Mutation, metadata, relic, and cleanup tests for libpkgreconcile.
 */

#include "test_support.hpp"

#include <libpkgreconcile/libpkgreconcile.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include <unistd.h>

namespace fs = std::filesystem;
using test_support::mode;
using test_support::read_file;
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

pkgreconcile::candidate
prepare_changed(test_root& root,
                pkgreconcile::filesystem_reconciler& filesystem_reconciler,
                mode_t installed_mode = 0644, mode_t rejected_mode = 0600) {
  write_file(root.installed("etc/a.conf"), "old\n");
  write_file(root.rejected("etc/a.conf"), "new\n");
  ::chmod(root.installed("etc/a.conf").c_str(), installed_mode);
  ::chmod(root.rejected("etc/a.conf").c_str(), rejected_mode);
  return find_candidate(filesystem_reconciler.scan(), "etc/a.conf");
}

} // namespace

int main() {
  runner tests;

  tests.run("keep installed contents and metadata", [] {
    test_root root;
    pkgreconcile::filesystem_reconciler filesystem_reconciler =
        make_filesystem_reconciler(root);
    const auto value = prepare_changed(root, filesystem_reconciler, 0640, 0600);
    filesystem_reconciler.keep(value, pkgreconcile::metadata_source::installed);
    TEST_CHECK_EQ(read_file(root.installed("etc/a.conf")),
                  std::string("old\n"));
    TEST_CHECK_EQ(mode(root.installed("etc/a.conf")), mode_t{0640});
    TEST_CHECK(!fs::exists(root.rejected("etc/a.conf")));
  });
  tests.run("keep installed contents with rejected metadata", [] {
    test_root root;
    pkgreconcile::filesystem_reconciler filesystem_reconciler =
        make_filesystem_reconciler(root);
    const auto value = prepare_changed(root, filesystem_reconciler, 0644, 0600);
    filesystem_reconciler.keep(value, pkgreconcile::metadata_source::staged);
    TEST_CHECK_EQ(read_file(root.installed("etc/a.conf")),
                  std::string("old\n"));
    TEST_CHECK_EQ(mode(root.installed("etc/a.conf")), mode_t{0600});
  });
  tests.run("keep rejects relic", [] {
    test_root root;
    write_file(root.rejected("etc/relic"), "data\n");
    pkgreconcile::filesystem_reconciler filesystem_reconciler =
        make_filesystem_reconciler(root);
    const auto value =
        find_candidate(filesystem_reconciler.scan(), "etc/relic");
    TEST_CHECK_THROWS(filesystem_reconciler.keep(
                          value, pkgreconcile::metadata_source::installed),
                      std::invalid_argument);
  });
  tests.run("upgrade contents with installed metadata", [] {
    test_root root;
    pkgreconcile::filesystem_reconciler filesystem_reconciler =
        make_filesystem_reconciler(root);
    const auto value = prepare_changed(root, filesystem_reconciler, 0640, 0600);
    filesystem_reconciler.install(value,
                                  pkgreconcile::metadata_source::installed);
    TEST_CHECK_EQ(read_file(root.installed("etc/a.conf")),
                  std::string("new\n"));
    TEST_CHECK_EQ(mode(root.installed("etc/a.conf")), mode_t{0640});
    TEST_CHECK(!fs::exists(root.rejected("etc/a.conf")));
  });
  tests.run("upgrade contents with rejected metadata", [] {
    test_root root;
    pkgreconcile::filesystem_reconciler filesystem_reconciler =
        make_filesystem_reconciler(root);
    const auto value = prepare_changed(root, filesystem_reconciler, 0644, 0600);
    filesystem_reconciler.install(value, pkgreconcile::metadata_source::staged);
    TEST_CHECK_EQ(read_file(root.installed("etc/a.conf")),
                  std::string("new\n"));
    TEST_CHECK_EQ(mode(root.installed("etc/a.conf")), mode_t{0600});
  });
  tests.run("upgrade changed symlink", [] {
    test_root root;
    fs::create_directories(root.installed("etc"));
    fs::create_directories(root.rejected("etc"));
    fs::create_symlink("old-target", root.installed("etc/link"));
    fs::create_symlink("new-target", root.rejected("etc/link"));
    pkgreconcile::filesystem_reconciler filesystem_reconciler =
        make_filesystem_reconciler(root);
    const auto value = find_candidate(filesystem_reconciler.scan(), "etc/link");
    filesystem_reconciler.install(value, pkgreconcile::metadata_source::staged);
    TEST_CHECK_EQ(fs::read_symlink(root.installed("etc/link")),
                  fs::path("new-target"));
    TEST_CHECK(!fs::exists(root.rejected("etc/link")));
  });
  tests.run("upgrade regular to symlink", [] {
    test_root root;
    write_file(root.installed("etc/node"), "old\n");
    fs::create_directories(root.rejected("etc"));
    fs::create_symlink("target", root.rejected("etc/node"));
    pkgreconcile::filesystem_reconciler filesystem_reconciler =
        make_filesystem_reconciler(root);
    const auto value = find_candidate(filesystem_reconciler.scan(), "etc/node");
    filesystem_reconciler.install(value, pkgreconcile::metadata_source::staged);
    TEST_CHECK(fs::is_symlink(root.installed("etc/node")));
    TEST_CHECK_EQ(fs::read_symlink(root.installed("etc/node")),
                  fs::path("target"));
  });
  tests.run("upgrade symlink to regular file", [] {
    test_root root;
    fs::create_directories(root.installed("etc"));
    fs::create_symlink("old-target", root.installed("etc/node"));
    write_file(root.rejected("etc/node"), "new regular\n");
    pkgreconcile::filesystem_reconciler reconciler =
        make_filesystem_reconciler(root);
    const auto value = find_candidate(reconciler.scan(), "etc/node");
    reconciler.install(value, pkgreconcile::metadata_source::staged);
    TEST_CHECK(fs::is_regular_file(root.installed("etc/node")));
    TEST_CHECK_EQ(read_file(root.installed("etc/node")),
                  std::string("new regular\n"));
  });
  tests.run("upgrade regular file over directory is refused", [] {
    test_root root;
    fs::create_directories(root.installed("etc/node"));
    write_file(root.rejected("etc/node"), "new regular\n");
    pkgreconcile::filesystem_reconciler reconciler =
        make_filesystem_reconciler(root);
    const auto value = find_candidate(reconciler.scan(), "etc/node");
    TEST_CHECK_THROWS(
        reconciler.install(value, pkgreconcile::metadata_source::staged),
        std::system_error);
    TEST_CHECK(fs::is_directory(root.installed("etc/node")));
    TEST_CHECK(fs::exists(root.rejected("etc/node")));
  });
  tests.run("upgrade symlink with installed metadata does not chmod target",
            [] {
              test_root root;
              write_file(root.installed("etc/node"), "old\n");
              write_file(root.installed("target"), "target\n");
              ::chmod(root.installed("etc/node").c_str(), 0600);
              ::chmod(root.installed("target").c_str(), 0644);
              fs::create_directories(root.rejected("etc"));
              fs::create_symlink("/target", root.rejected("etc/node"));
              pkgreconcile::filesystem_reconciler filesystem_reconciler =
                  make_filesystem_reconciler(root);
              const auto value =
                  find_candidate(filesystem_reconciler.scan(), "etc/node");
              filesystem_reconciler.install(
                  value, pkgreconcile::metadata_source::installed);
              TEST_CHECK(fs::is_symlink(root.installed("etc/node")));
              TEST_CHECK_EQ(mode(root.installed("target")), mode_t{0644});
            });
  tests.run("upgrade paired directory metadata", [] {
    test_root root;
    fs::create_directories(root.installed("etc/sub"));
    fs::create_directories(root.rejected("etc/sub"));
    ::chmod(root.installed("etc/sub").c_str(), 0755);
    ::chmod(root.rejected("etc/sub").c_str(), 0700);
    pkgreconcile::filesystem_reconciler filesystem_reconciler =
        make_filesystem_reconciler(root);
    const auto value = find_candidate(filesystem_reconciler.scan(), "etc/sub");
    filesystem_reconciler.install(value, pkgreconcile::metadata_source::staged);
    TEST_CHECK_EQ(mode(root.installed("etc/sub")), mode_t{0700});
    TEST_CHECK(!fs::exists(root.rejected("etc/sub")));
  });
  tests.run("upgrade rejected directory over file is refused", [] {
    test_root root;
    write_file(root.installed("etc/node"), "old\n");
    fs::create_directories(root.rejected("etc/node"));
    pkgreconcile::filesystem_reconciler filesystem_reconciler =
        make_filesystem_reconciler(root);
    const auto value = find_candidate(filesystem_reconciler.scan(), "etc/node");
    TEST_CHECK_THROWS(filesystem_reconciler.install(
                          value, pkgreconcile::metadata_source::staged),
                      std::runtime_error);
    TEST_CHECK_EQ(read_file(root.installed("etc/node")), std::string("old\n"));
  });

  tests.run("install merged with installed metadata", [] {
    test_root root;
    pkgreconcile::filesystem_reconciler filesystem_reconciler =
        make_filesystem_reconciler(root);
    const auto value = prepare_changed(root, filesystem_reconciler, 0640, 0600);
    filesystem_reconciler.install_merged(
        value, "merged\n", pkgreconcile::metadata_source::installed);
    TEST_CHECK_EQ(read_file(root.installed("etc/a.conf")),
                  std::string("merged\n"));
    TEST_CHECK_EQ(mode(root.installed("etc/a.conf")), mode_t{0640});
    TEST_CHECK(!fs::exists(root.rejected("etc/a.conf")));
  });
  tests.run("install merged with rejected metadata", [] {
    test_root root;
    pkgreconcile::filesystem_reconciler filesystem_reconciler =
        make_filesystem_reconciler(root);
    const auto value = prepare_changed(root, filesystem_reconciler, 0644, 0600);
    filesystem_reconciler.install_merged(value, "merged",
                                         pkgreconcile::metadata_source::staged);
    TEST_CHECK_EQ(read_file(root.installed("etc/a.conf")),
                  std::string("merged"));
    TEST_CHECK_EQ(mode(root.installed("etc/a.conf")), mode_t{0600});
  });
  tests.run("install merged accepts empty bytes", [] {
    test_root root;
    pkgreconcile::filesystem_reconciler reconciler =
        make_filesystem_reconciler(root);
    const auto value = prepare_changed(root, reconciler);
    reconciler.install_merged(value, "",
                              pkgreconcile::metadata_source::installed);
    TEST_CHECK_EQ(read_file(root.installed("etc/a.conf")), std::string{});
    TEST_CHECK(!fs::exists(root.rejected("etc/a.conf")));
  });
  tests.run("install merged rejects symlink", [] {
    test_root root;
    fs::create_directories(root.installed("etc"));
    fs::create_directories(root.rejected("etc"));
    fs::create_symlink("old", root.installed("etc/link"));
    fs::create_symlink("new", root.rejected("etc/link"));
    pkgreconcile::filesystem_reconciler filesystem_reconciler =
        make_filesystem_reconciler(root);
    const auto value = find_candidate(filesystem_reconciler.scan(), "etc/link");
    TEST_CHECK_THROWS(
        filesystem_reconciler.install_merged(
            value, "data", pkgreconcile::metadata_source::installed),
        std::invalid_argument);
  });

  tests.run("restore regular relic", [] {
    test_root root;
    write_file(root.rejected("etc/relic"), "data\n");
    ::chmod(root.rejected("etc/relic").c_str(), 0600);
    pkgreconcile::filesystem_reconciler filesystem_reconciler =
        make_filesystem_reconciler(root);
    const auto value =
        find_candidate(filesystem_reconciler.scan(), "etc/relic");
    filesystem_reconciler.restore(value);
    TEST_CHECK_EQ(read_file(root.installed("etc/relic")),
                  std::string("data\n"));
    TEST_CHECK_EQ(mode(root.installed("etc/relic")), mode_t{0600});
    TEST_CHECK(!fs::exists(root.rejected("etc/relic")));
  });
  tests.run("restore symlink relic", [] {
    test_root root;
    fs::create_directories(root.rejected("etc"));
    fs::create_symlink("target", root.rejected("etc/link"));
    pkgreconcile::filesystem_reconciler filesystem_reconciler =
        make_filesystem_reconciler(root);
    const auto value = find_candidate(filesystem_reconciler.scan(), "etc/link");
    filesystem_reconciler.restore(value);
    TEST_CHECK_EQ(fs::read_symlink(root.installed("etc/link")),
                  fs::path("target"));
  });
  tests.run("restore refuses newly occupied destination", [] {
    test_root root;
    write_file(root.rejected("etc/relic"), "preserved\n");
    pkgreconcile::filesystem_reconciler filesystem_reconciler =
        make_filesystem_reconciler(root);
    const auto value =
        find_candidate(filesystem_reconciler.scan(), "etc/relic");
    write_file(root.installed("etc/relic"), "new occupant\n");
    TEST_CHECK_THROWS(filesystem_reconciler.restore(value), std::runtime_error);
    TEST_CHECK_EQ(read_file(root.installed("etc/relic")),
                  std::string("new occupant\n"));
    TEST_CHECK(fs::exists(root.rejected("etc/relic")));
  });
  tests.run("move relic below target root", [] {
    test_root root;
    write_file(root.rejected("etc/relic"), "data\n");
    pkgreconcile::filesystem_reconciler filesystem_reconciler =
        make_filesystem_reconciler(root);
    const auto value =
        find_candidate(filesystem_reconciler.scan(), "etc/relic");
    filesystem_reconciler.relocate_relic(value, "/var/backups/relic.conf");
    TEST_CHECK_EQ(read_file(root.installed("var/backups/relic.conf")),
                  std::string("data\n"));
    TEST_CHECK(!fs::exists(root.rejected("etc/relic")));
  });
  tests.run("move relic requires absolute path", [] {
    test_root root;
    write_file(root.rejected("etc/relic"), "data\n");
    pkgreconcile::filesystem_reconciler filesystem_reconciler =
        make_filesystem_reconciler(root);
    const auto value =
        find_candidate(filesystem_reconciler.scan(), "etc/relic");
    TEST_CHECK_THROWS(
        filesystem_reconciler.relocate_relic(value, "relative/path"),
        std::invalid_argument);
  });
  tests.run("move relic rejects root escape", [] {
    test_root root;
    write_file(root.rejected("etc/relic"), "data\n");
    pkgreconcile::filesystem_reconciler filesystem_reconciler =
        make_filesystem_reconciler(root);
    const auto value =
        find_candidate(filesystem_reconciler.scan(), "etc/relic");
    TEST_CHECK_THROWS(
        filesystem_reconciler.relocate_relic(value, "/../../escape"),
        std::invalid_argument);
  });
  tests.run("move relic refuses existing destination", [] {
    test_root root;
    write_file(root.rejected("etc/relic"), "data\n");
    write_file(root.installed("var/backups/relic"), "occupied\n");
    pkgreconcile::filesystem_reconciler filesystem_reconciler =
        make_filesystem_reconciler(root);
    const auto value =
        find_candidate(filesystem_reconciler.scan(), "etc/relic");
    TEST_CHECK_THROWS(
        filesystem_reconciler.relocate_relic(value, "/var/backups/relic"),
        std::runtime_error);
    TEST_CHECK_EQ(read_file(root.installed("var/backups/relic")),
                  std::string("occupied\n"));
  });
  tests.run("remove relic", [] {
    test_root root;
    write_file(root.rejected("etc/relic"), "data\n");
    pkgreconcile::filesystem_reconciler filesystem_reconciler =
        make_filesystem_reconciler(root);
    const auto value =
        find_candidate(filesystem_reconciler.scan(), "etc/relic");
    filesystem_reconciler.remove_relic(value);
    TEST_CHECK(!fs::exists(root.rejected("etc/relic")));
  });
  tests.run("remove relic rejects paired candidate", [] {
    test_root root;
    pkgreconcile::filesystem_reconciler filesystem_reconciler =
        make_filesystem_reconciler(root);
    const auto value = prepare_changed(root, filesystem_reconciler);
    TEST_CHECK_THROWS(filesystem_reconciler.remove_relic(value),
                      std::invalid_argument);
  });

  tests.run("cleanup removes nested empty directories", [] {
    test_root root;
    fs::create_directories(root.rejected("a/b/c"));
    pkgreconcile::filesystem_reconciler filesystem_reconciler =
        make_filesystem_reconciler(root);
    filesystem_reconciler.cleanup_empty_directories();
    TEST_CHECK(!fs::exists(root.rejected("a")));
    TEST_CHECK(fs::exists(root.rejected_root()));
  });
  tests.run("cleanup preserves symlink to directory", [] {
    test_root root;
    fs::create_directories(root.installed("target-dir"));
    fs::create_symlink(root.installed("target-dir"),
                       root.rejected("directory-link"));
    pkgreconcile::filesystem_reconciler filesystem_reconciler =
        make_filesystem_reconciler(root);
    filesystem_reconciler.cleanup_empty_directories();
    TEST_CHECK(fs::is_symlink(root.rejected("directory-link")));
  });
  tests.run("cleanup preserves non-empty directories", [] {
    test_root root;
    write_file(root.rejected("a/b/file"), "data\n");
    pkgreconcile::filesystem_reconciler filesystem_reconciler =
        make_filesystem_reconciler(root);
    filesystem_reconciler.cleanup_empty_directories();
    TEST_CHECK(fs::exists(root.rejected("a/b/file")));
    TEST_CHECK(fs::exists(root.rejected("a/b")));
  });
  tests.run("stale action reports disappeared path", [] {
    test_root root;
    pkgreconcile::filesystem_reconciler filesystem_reconciler =
        make_filesystem_reconciler(root);
    const auto value = prepare_changed(root, filesystem_reconciler);
    fs::remove(root.rejected("etc/a.conf"));
    TEST_CHECK_THROWS(filesystem_reconciler.keep(
                          value, pkgreconcile::metadata_source::installed),
                      std::runtime_error);
  });

  return tests.finish();
}
