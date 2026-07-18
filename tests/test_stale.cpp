// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test_support.hpp"

#include <libpkgreconcile/libpkgreconcile.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;
using test_support::read_file;
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
                                pkgreconcile::filesystem_reconciler& engine) {
  write_file(root.installed("etc/value"), "old\n");
  write_file(root.rejected("etc/value"), "new\n");
  return find_candidate(engine.scan(), "etc/value");
}

} // namespace

int main() {
  runner tests;

  tests.run("staged byte change invalidates comparison", [] {
    test_root root;
    auto engine = make_reconciler(root);
    const auto value = prepare(root, engine);
    write_file(root.rejected("etc/value"), "changed later\n");
    TEST_CHECK_THROWS(engine.compare_text(value),
                      pkgreconcile::stale_candidate_error);
  });

  tests.run("installed byte change invalidates keep", [] {
    test_root root;
    auto engine = make_reconciler(root);
    const auto value = prepare(root, engine);
    write_file(root.installed("etc/value"), "local later\n");
    TEST_CHECK_THROWS(
        engine.keep(value, pkgreconcile::metadata_source::installed),
        pkgreconcile::stale_candidate_error);
    TEST_CHECK_EQ(read_file(root.installed("etc/value")),
                  std::string("local later\n"));
    TEST_CHECK(fs::exists(root.rejected("etc/value")));
  });

  tests.run("staged metadata change invalidates install", [] {
    test_root root;
    auto engine = make_reconciler(root);
    const auto value = prepare(root, engine);
    ::chmod(root.rejected("etc/value").c_str(), 0600);
    TEST_CHECK_THROWS(
        engine.install(value, pkgreconcile::metadata_source::staged),
        pkgreconcile::stale_candidate_error);
    TEST_CHECK_EQ(read_file(root.installed("etc/value")), std::string("old\n"));
  });

  tests.run("inode replacement invalidates action even with same bytes", [] {
    test_root root;
    auto engine = make_reconciler(root);
    const auto value = prepare(root, engine);
    const fs::path replacement = root.rejected("etc/replacement");
    write_file(replacement, "new\n");
    fs::rename(replacement, root.rejected("etc/value"));
    TEST_CHECK_THROWS(
        engine.install(value, pkgreconcile::metadata_source::staged),
        pkgreconcile::stale_candidate_error);
  });

  tests.run("disappeared installed path invalidates paired action", [] {
    test_root root;
    auto engine = make_reconciler(root);
    const auto value = prepare(root, engine);
    fs::remove(root.installed("etc/value"));
    TEST_CHECK_THROWS(
        engine.keep(value, pkgreconcile::metadata_source::installed),
        pkgreconcile::stale_candidate_error);
  });

  tests.run("new relic destination invalidates restore", [] {
    test_root root;
    write_file(root.rejected("etc/relic"), "preserved\n");
    auto engine = make_reconciler(root);
    const auto value = find_candidate(engine.scan(), "etc/relic");
    write_file(root.installed("etc/relic"), "occupant\n");
    TEST_CHECK_THROWS(engine.restore(value),
                      pkgreconcile::stale_candidate_error);
    TEST_CHECK_EQ(read_file(root.installed("etc/relic")),
                  std::string("occupant\n"));
  });

  tests.run("candidate from another root is rejected", [] {
    test_root first;
    test_root second;
    auto first_engine = make_reconciler(first);
    auto second_engine = make_reconciler(second);
    const auto value = prepare(first, first_engine);
    TEST_CHECK_THROWS(
        second_engine.keep(value, pkgreconcile::metadata_source::installed),
        pkgreconcile::stale_candidate_error);
  });

  tests.run("directory child churn does not stale metadata candidate", [] {
    test_root root;
    fs::create_directories(root.installed("etc/sub"));
    fs::create_directories(root.rejected("etc/sub"));
    ::chmod(root.installed("etc/sub").c_str(), 0755);
    ::chmod(root.rejected("etc/sub").c_str(), 0700);
    auto engine = make_reconciler(root);
    const auto value = find_candidate(engine.scan(), "etc/sub");
    write_file(root.rejected("etc/sub/temporary"), "x\n");
    fs::remove(root.rejected("etc/sub/temporary"));
    engine.install(value, pkgreconcile::metadata_source::staged);
    TEST_CHECK_EQ(test_support::mode(root.installed("etc/sub")), mode_t{0700});
  });

  tests.run("directory mode change invalidates candidate", [] {
    test_root root;
    fs::create_directories(root.installed("etc/sub"));
    fs::create_directories(root.rejected("etc/sub"));
    auto engine = make_reconciler(root);
    const auto value = find_candidate(engine.scan(), "etc/sub");
    ::chmod(root.rejected("etc/sub").c_str(), 0700);
    TEST_CHECK_THROWS(
        engine.install(value, pkgreconcile::metadata_source::staged),
        pkgreconcile::stale_candidate_error);
  });

  tests.run("resolved candidate cannot be replayed", [] {
    test_root root;
    auto engine = make_reconciler(root);
    const auto value = prepare(root, engine);
    engine.keep(value, pkgreconcile::metadata_source::installed);
    TEST_CHECK_THROWS(
        engine.keep(value, pkgreconcile::metadata_source::installed),
        pkgreconcile::stale_candidate_error);
  });

  return tests.finish();
}
