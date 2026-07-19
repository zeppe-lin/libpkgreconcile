// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_terminal.cpp
 * @brief Stock terminal-runner behavior and dry-run immutability tests.
 */

#include "test_support.hpp"

#include "../tools/terminal.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <sys/stat.h>

namespace fs = std::filesystem;
using test_support::mode;
using test_support::read_file;
using test_support::runner;
using test_support::stream_redirect;
using test_support::test_root;
using test_support::write_file;

namespace {

struct terminal_result {
  pkgreconcile::tool::run_summary summary;
  std::string output;
};

terminal_result run_with_input(const pkgreconcile::tool::run_options& options,
                               const std::string& input) {
  std::istringstream input_stream(input);
  std::ostringstream output_stream;
  stream_redirect input_redirect(std::cin, input_stream.rdbuf());
  stream_redirect output_redirect(std::cout, output_stream.rdbuf());
  std::cin.clear();
  std::cout.clear();
  terminal_result result;
  try {
    result.summary = pkgreconcile::tool::run_terminal(options);
    result.output = output_stream.str();
  } catch (...) {
    std::cin.clear();
    std::cout.clear();
    throw;
  }
  std::cin.clear();
  std::cout.clear();
  return result;
}

pkgreconcile::tool::run_options options_for(const test_root& root,
                                            bool dry_run = false) {
  pkgreconcile::tool::run_options options;
  options.filesystem.root = root.path();
  options.dry_run = dry_run;
  return options;
}

void prepare_changed(test_root& root, mode_t installed_mode = 0644,
                     mode_t rejected_mode = 0644) {
  write_file(root.installed("etc/a.conf"), "old\n");
  write_file(root.rejected("etc/a.conf"), "new\n");
  ::chmod(root.installed("etc/a.conf").c_str(), installed_mode);
  ::chmod(root.rejected("etc/a.conf").c_str(), rejected_mode);
}

} // namespace

int main() {
  runner tests;

  tests.run("empty tree reports nothing", [] {
    test_root root;
    const terminal_result result = run_with_input(options_for(root, true), "");
    TEST_CHECK_EQ(result.summary.inspected, 0U);
    TEST_CHECK_EQ(result.output, std::string("Nothing to reconcile\n"));
  });
  tests.run("normal empty run cleans structural directories", [] {
    test_root root;
    fs::create_directories(root.rejected("empty/a/b"));
    const terminal_result result = run_with_input(options_for(root), "");
    TEST_CHECK_EQ(result.output, std::string("Nothing to reconcile\n"));
    TEST_CHECK(!fs::exists(root.rejected("empty")));
    TEST_CHECK(fs::exists(root.rejected_root()));
  });
  tests.run("dry-run empty tree preserves structural directories", [] {
    test_root root;
    fs::create_directories(root.rejected("empty/a/b"));
    run_with_input(options_for(root, true), "");
    TEST_CHECK(fs::exists(root.rejected("empty/a/b")));
  });
  tests.run("dry-run changed file is immutable", [] {
    test_root root;
    prepare_changed(root, 0644, 0600);
    const std::string before_installed =
        read_file(root.installed("etc/a.conf"));
    const std::string before_rejected = read_file(root.rejected("etc/a.conf"));
    const terminal_result result = run_with_input(options_for(root, true), "");
    TEST_CHECK(result.summary.inspected >= 1U);
    TEST_CHECK_EQ(result.summary.resolved, 0U);
    TEST_CHECK_EQ(read_file(root.installed("etc/a.conf")), before_installed);
    TEST_CHECK_EQ(read_file(root.rejected("etc/a.conf")), before_rejected);
    TEST_CHECK_EQ(mode(root.installed("etc/a.conf")), mode_t{0644});
    TEST_CHECK_EQ(mode(root.rejected("etc/a.conf")), mode_t{0600});
    TEST_CHECK(result.output.find("-old\n+new\n") != std::string::npos);
  });
  tests.run("dry-run always colorizes semantic diff records", [] {
    test_root root;
    prepare_changed(root);
    pkgreconcile::tool::run_options options = options_for(root, true);
    options.color = pkgreconcile::tool::color_mode::always;
    const terminal_result result = run_with_input(options, "");
    TEST_CHECK(result.output.find("\x1b[31m-old\x1b[0m\n") !=
               std::string::npos);
    TEST_CHECK(result.output.find("\x1b[32m+new\x1b[0m\n") !=
               std::string::npos);
  });
  tests.run("dry-run relic is immutable", [] {
    test_root root;
    write_file(root.rejected("etc/relic"), "preserved\n");
    const terminal_result result = run_with_input(options_for(root, true), "");
    TEST_CHECK(result.output.find("no installed counterpart") !=
               std::string::npos);
    TEST_CHECK(fs::exists(root.rejected("etc/relic")));
    TEST_CHECK(!fs::exists(root.installed("etc/relic")));
  });
  tests.run("keep changed file", [] {
    test_root root;
    prepare_changed(root);
    const terminal_result result = run_with_input(options_for(root), "K\n");
    TEST_CHECK_EQ(read_file(root.installed("etc/a.conf")),
                  std::string("old\n"));
    TEST_CHECK(!fs::exists(root.rejected("etc/a.conf")));
    TEST_CHECK(result.summary.resolved >= 1U);
  });
  tests.run("upgrade contents and rejected metadata", [] {
    test_root root;
    prepare_changed(root, 0644, 0600);
    const terminal_result result = run_with_input(options_for(root), "U\nU\n");
    TEST_CHECK_EQ(read_file(root.installed("etc/a.conf")),
                  std::string("new\n"));
    TEST_CHECK_EQ(mode(root.installed("etc/a.conf")), mode_t{0600});
    TEST_CHECK(result.summary.resolved >= 1U);
  });
  tests.run("metadata skip skips whole entry", [] {
    test_root root;
    prepare_changed(root, 0644, 0600);
    const terminal_result result = run_with_input(options_for(root), "S\n");
    TEST_CHECK_EQ(read_file(root.installed("etc/a.conf")),
                  std::string("old\n"));
    TEST_CHECK_EQ(read_file(root.rejected("etc/a.conf")), std::string("new\n"));
    TEST_CHECK_EQ(result.summary.skipped, 1U);
  });
  tests.run("merge and install never persists terminal color", [] {
    test_root root;
    prepare_changed(root);
    pkgreconcile::tool::run_options options = options_for(root);
    options.color = pkgreconcile::tool::color_mode::always;
    const terminal_result result = run_with_input(options, "M\nI\n");
    const std::string installed = read_file(root.installed("etc/a.conf"));
    TEST_CHECK(installed.find("<<<<<<< ") == 0U);
    TEST_CHECK(installed.find("=======\nnew\n") != std::string::npos);
    TEST_CHECK(installed.find('\x1b') == std::string::npos);
    TEST_CHECK(!fs::exists(root.rejected("etc/a.conf")));
    TEST_CHECK(result.output.find("\x1b[31m-old\x1b[0m\n") !=
               std::string::npos);
    TEST_CHECK(result.output.find("1 merge conflict(s)") != std::string::npos);
  });
  tests.run("merge edit and install", [] {
    test_root root;
    prepare_changed(root);
    const fs::path editor = root.path() / "editor.sh";
    write_file(editor, "#!/bin/sh\nprintf 'edited\\n' > \"$1\"\n");
    ::chmod(editor.c_str(), 0755);
    pkgreconcile::tool::run_options options = options_for(root);
    options.editor = editor.string();
    run_with_input(options, "M\nE\nI\n");
    TEST_CHECK_EQ(read_file(root.installed("etc/a.conf")),
                  std::string("edited\n"));
  });
  tests.run("editor failure aborts without resolution", [] {
    test_root root;
    prepare_changed(root);
    pkgreconcile::tool::run_options options = options_for(root);
    options.editor = "/bin/false";
    TEST_CHECK_THROWS(run_with_input(options, "M\nE\n"), std::runtime_error);
    TEST_CHECK_EQ(read_file(root.installed("etc/a.conf")),
                  std::string("old\n"));
    TEST_CHECK(fs::exists(root.rejected("etc/a.conf")));
  });
  tests.run("binary menu omits merge", [] {
    test_root root;
    write_file(root.installed("etc/a.bin"), std::string("a\0b", 3));
    write_file(root.rejected("etc/a.bin"), std::string("a\0c", 3));
    const terminal_result result = run_with_input(options_for(root), "K\n");
    TEST_CHECK(result.output.find("Binary files ") != std::string::npos);
    TEST_CHECK(result.output.find("[M]erge") == std::string::npos);
  });
  tests.run("restore relic", [] {
    test_root root;
    write_file(root.rejected("etc/relic"), "data\n");
    const terminal_result result = run_with_input(options_for(root), "R\n");
    TEST_CHECK_EQ(read_file(root.installed("etc/relic")),
                  std::string("data\n"));
    TEST_CHECK(result.summary.resolved >= 1U);
  });
  tests.run("move relic", [] {
    test_root root;
    write_file(root.rejected("etc/relic"), "data\n");
    run_with_input(options_for(root), "M\n/var/backups/relic\n");
    TEST_CHECK_EQ(read_file(root.installed("var/backups/relic")),
                  std::string("data\n"));
  });
  tests.run("delete relic", [] {
    test_root root;
    write_file(root.rejected("etc/relic"), "data\n");
    run_with_input(options_for(root), "D\n");
    TEST_CHECK(!fs::exists(root.rejected("etc/relic")));
  });
  tests.run("skip relic", [] {
    test_root root;
    write_file(root.rejected("etc/relic"), "data\n");
    const terminal_result result = run_with_input(options_for(root), "S\n");
    TEST_CHECK(fs::exists(root.rejected("etc/relic")));
    TEST_CHECK_EQ(result.summary.skipped, 1U);
  });
  tests.run("duplicate is removed automatically", [] {
    test_root root;
    write_file(root.installed("etc/a"), "same\n");
    write_file(root.rejected("etc/a"), "same\n");
    ::chmod(root.installed("etc/a").c_str(), 0644);
    ::chmod(root.rejected("etc/a").c_str(), 0644);
    const terminal_result result = run_with_input(options_for(root), "");
    TEST_CHECK(!fs::exists(root.rejected("etc/a")));
    TEST_CHECK(result.summary.resolved >= 1U);
  });
  tests.run("resolved children allow directory cleanup", [] {
    test_root root;
    prepare_changed(root);
    run_with_input(options_for(root), "K\n");
    TEST_CHECK(!fs::exists(root.rejected("etc")));
    TEST_CHECK(fs::exists(root.rejected_root()));
  });
  tests.run("skipped child preserves staging directory", [] {
    test_root root;
    prepare_changed(root);
    run_with_input(options_for(root), "S\n");
    TEST_CHECK(fs::exists(root.rejected("etc/a.conf")));
    TEST_CHECK(fs::exists(root.rejected("etc")));
  });
  tests.run("closed input is an error", [] {
    test_root root;
    prepare_changed(root);
    TEST_CHECK_THROWS(run_with_input(options_for(root), ""),
                      std::runtime_error);
  });

  return tests.finish();
}
