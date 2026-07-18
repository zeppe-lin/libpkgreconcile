// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * rejmerge - resolve files rejected during package upgrades
 * See COPYING for license terms and COPYRIGHT for notices.
 */

#include "terminal.h"

#include <liblinediff/liblinediff.h>

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace pkgreconcile::tool {
namespace {

void info(const std::string& message) {
  std::cout << "=======> " << message << '\n';
}

char prompt(const std::string& message) {
  for (;;) {
    std::cout << "=======> " << message << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) {
      throw std::runtime_error("input closed");
    }
    if (!line.empty()) {
      return static_cast<char>(
          std::toupper(static_cast<unsigned char>(line.front())));
    }
  }
}

std::string prompt_line(const std::string& message) {
  std::cout << "=======> " << message << std::flush;
  std::string line;
  if (!std::getline(std::cin, line)) {
    throw std::runtime_error("input closed");
  }
  return line;
}

std::string environment_or(const char* name, const char* fallback) {
  const char* value = std::getenv(name);
  return value != nullptr && *value != '\0' ? value : fallback;
}

class temporary_file {
public:
  explicit temporary_file(const std::string& contents) {
    const fs::path directory = environment_or("TMPDIR", "/tmp");
    std::string pattern = (directory / "rejmerge.XXXXXX").string();
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back('\0');

    descriptor_ = ::mkstemp(name.data());
    if (descriptor_ == -1) {
      throw std::system_error(errno, std::generic_category(), "mkstemp");
    }
    path_ = name.data();

    try {
      std::size_t offset = 0;
      while (offset < contents.size()) {
        const ssize_t count = ::write(descriptor_, contents.data() + offset,
                                      contents.size() - offset);
        if (count == -1) {
          if (errno == EINTR) {
            continue;
          }
          throw std::system_error(errno, std::generic_category(),
                                  "write: " + path_.string());
        }
        offset += static_cast<std::size_t>(count);
      }
      if (::close(descriptor_) == -1) {
        descriptor_ = -1;
        throw std::system_error(errno, std::generic_category(),
                                "close: " + path_.string());
      }
      descriptor_ = -1;
    } catch (...) {
      if (descriptor_ != -1) {
        ::close(descriptor_);
        descriptor_ = -1;
      }
      ::unlink(path_.c_str());
      path_.clear();
      throw;
    }
  }

  ~temporary_file() {
    if (descriptor_ != -1) {
      ::close(descriptor_);
    }
    if (!path_.empty()) {
      ::unlink(path_.c_str());
    }
  }

  temporary_file(const temporary_file&) = delete;
  temporary_file& operator=(const temporary_file&) = delete;

  const fs::path& path() const noexcept {
    return path_;
  }

  std::string read() const {
    std::ifstream input(path_, std::ios::binary);
    if (!input) {
      throw std::runtime_error("cannot open " + path_.string());
    }
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
  }

private:
  fs::path path_;
  int descriptor_{-1};
};

int run_shell_command(const std::string& command, const fs::path& path) {
  const pid_t pid = ::fork();
  if (pid == -1) {
    throw std::system_error(errno, std::generic_category(), "fork");
  }
  if (pid == 0) {
    ::execl("/bin/sh", "sh", "-c", "exec $0 \"$1\"", command.c_str(),
            path.c_str(), nullptr);
    _exit(errno == ENOENT ? 127 : 126);
  }

  int status = 0;
  while (::waitpid(pid, &status, 0) == -1) {
    if (errno != EINTR) {
      throw std::system_error(errno, std::generic_category(), "waitpid");
    }
  }

  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  if (WIFSIGNALED(status)) {
    return 128 + WTERMSIG(status);
  }
  return 1;
}

void view_text(const std::string& text, const std::string& pager) {
  if (!::isatty(STDOUT_FILENO)) {
    std::cout << text;
    return;
  }

  temporary_file file(text);
  const int status = run_shell_command(pager, file.path());
  if (status != 0) {
    throw std::runtime_error("pager exited with status " +
                             std::to_string(status));
  }
}

void edit_file(const fs::path& path, const std::string& editor) {
  const int status = run_shell_command(editor, path);
  if (status != 0) {
    throw std::runtime_error("editor exited with status " +
                             std::to_string(status));
  }
}

std::string metadata_text(const metadata& value) {
  std::ostringstream output;
  output << std::oct << std::setfill('0') << std::setw(4)
         << static_cast<unsigned int>(value.mode) << std::dec
         << " uid=" << value.uid << " gid=" << value.gid;
  return output.str();
}

void show_entry(const candidate& value) {
  info(value.installed_path().string());
  std::cout << "  installed: "
            << pkgreconcile::to_string(value.installed_metadata().type);
  if (value.installed_metadata().type != node_type::missing) {
    std::cout << " (" << metadata_text(value.installed_metadata()) << ')';
  }
  std::cout << '\n';
  std::cout << "  rejected:  "
            << pkgreconcile::to_string(value.staged_metadata().type) << " ("
            << metadata_text(value.staged_metadata()) << ")\n";
  std::cout << "  state:     " << pkgreconcile::to_string(value.state())
            << '\n';
}

metadata_source choose_metadata(const candidate& value, bool& skip) {
  skip = false;
  if (value.metadata_equal()) {
    return metadata_source::installed;
  }

  show_entry(value);
  for (;;) {
    switch (prompt("metadata: [K]eep installed [U]se rejected [S]kip? ")) {
    case 'K':
      return metadata_source::installed;
    case 'U':
      return metadata_source::staged;
    case 'S':
      skip = true;
      return metadata_source::installed;
    default:
      break;
    }
  }
}

bool handle_relic(filesystem_reconciler& core, const candidate& value,
                  run_summary& summary) {
  show_entry(value);
  for (;;) {
    switch (prompt("[R]estore [M]ove [D]elete [S]kip? ")) {
    case 'R':
      core.restore(value);
      ++summary.resolved;
      return true;
    case 'M': {
      const std::string destination =
          prompt_line("new absolute path under target root: ");
      core.relocate_relic(value, destination);
      ++summary.resolved;
      return true;
    }
    case 'D':
      core.remove_relic(value);
      ++summary.resolved;
      return true;
    case 'S':
      ++summary.skipped;
      return false;
    default:
      break;
    }
  }
}

bool handle_merge(filesystem_reconciler& core, const candidate& value,
                  metadata_source source, const std::string& pager,
                  const std::string& editor, run_summary& summary) {
  linediff::conflict_options render_options;
  render_options.old_label = value.installed_path().string();
  render_options.new_label = value.staged_path().string();
  const linediff::conflict_result merged =
      linediff::render_conflicts(core.compare_text(value), render_options);
  temporary_file file(merged.text);

  bool first = true;
  for (;;) {
    if (first) {
      info("merged " + value.installed_path().string());
      first = false;
    }
    view_text(file.read(), pager);
    if (merged.conflicts != 0) {
      info(std::to_string(merged.conflicts) + " merge conflict(s)");
    }

    switch (prompt("[I]nstall [E]dit [V]iew [S]kip? ")) {
    case 'I':
      core.install_merged(value, file.read(), source);
      ++summary.resolved;
      return true;
    case 'E':
      edit_file(file.path(), editor);
      break;
    case 'V':
      break;
    case 'S':
      ++summary.skipped;
      return false;
    default:
      break;
    }
  }
}

bool handle_regular(filesystem_reconciler& core, const candidate& value,
                    metadata_source source, const std::string& pager,
                    const std::string& editor, run_summary& summary) {
  std::string difference;
  if (value.text_mergeable()) {
    linediff::unified_options render_options;
    render_options.old_label = value.installed_path().string();
    render_options.new_label = value.staged_path().string();
    difference =
        linediff::render_unified(core.compare_text(value), render_options);
  } else {
    difference = "Binary files differ: " + value.installed_path().string() +
                 " " + value.staged_path().string() + "\n";
  }
  for (;;) {
    view_text(difference, pager);
    const char choice = value.text_mergeable()
                            ? prompt("[K]eep [U]pgrade [M]erge [D]iff [S]kip? ")
                            : prompt("[K]eep [U]pgrade [D]iff [S]kip? ");
    switch (choice) {
    case 'K':
      core.keep(value, source);
      ++summary.resolved;
      return true;
    case 'U':
      core.install(value, source);
      ++summary.resolved;
      return true;
    case 'M':
      if (value.text_mergeable()) {
        return handle_merge(core, value, source, pager, editor, summary);
      }
      break;
    case 'D':
      break;
    case 'S':
      ++summary.skipped;
      return false;
    default:
      break;
    }
  }
}

bool handle_simple(filesystem_reconciler& core, const candidate& value,
                   metadata_source source, run_summary& summary) {
  show_entry(value);
  for (;;) {
    switch (prompt("[K]eep [U]pgrade [S]kip? ")) {
    case 'K':
      core.keep(value, source);
      ++summary.resolved;
      return true;
    case 'U':
      core.install(value, source);
      ++summary.resolved;
      return true;
    case 'S':
      ++summary.skipped;
      return false;
    default:
      break;
    }
  }
}

void show_dry_run(filesystem_reconciler& core, const candidate& value) {
  show_entry(value);
  if (value.installed_metadata().type == node_type::regular &&
      value.staged_metadata().type == node_type::regular &&
      !value.content_equal()) {
    if (value.text_mergeable()) {
      linediff::unified_options render_options;
      render_options.old_label = value.installed_path().string();
      render_options.new_label = value.staged_path().string();
      std::cout << linediff::render_unified(core.compare_text(value),
                                            render_options);
    } else {
      std::cout << "Binary files differ: " << value.installed_path().string()
                << ' ' << value.staged_path().string() << '\n';
    }
  }
}

} // namespace

run_summary run_terminal(const run_options& options) {
  filesystem_reconciler core(options.filesystem);
  run_summary summary;

  const auto entries = core.scan();
  if (entries.empty()) {
    std::cout << "Nothing to reconcile\n";
    if (!options.dry_run) {
      core.cleanup_empty_directories();
    }
    return summary;
  }

  const std::string pager = options.pager.empty()
                                ? environment_or("PAGER", "/bin/more")
                                : options.pager;
  const std::string fallback_editor = environment_or("EDITOR", "/bin/vi");
  const std::string editor =
      options.editor.empty() ? environment_or("VISUAL", fallback_editor.c_str())
                             : options.editor;

  for (const auto& value : entries) {
    ++summary.inspected;

    if (options.dry_run) {
      show_dry_run(core, value);
      continue;
    }

    if (value.state() == candidate_state::relic) {
      handle_relic(core, value, summary);
      continue;
    }

    bool skip = false;
    const metadata_source source = choose_metadata(value, skip);
    if (skip) {
      ++summary.skipped;
      continue;
    }

    if (value.content_equal()) {
      core.keep(value, source);
      ++summary.resolved;
      continue;
    }

    if (value.installed_metadata().type == node_type::regular &&
        value.staged_metadata().type == node_type::regular) {
      handle_regular(core, value, source, pager, editor, summary);
    } else {
      handle_simple(core, value, source, summary);
    }
  }

  if (!options.dry_run) {
    core.cleanup_empty_directories();
  }

  return summary;
}

} // namespace pkgreconcile::tool
