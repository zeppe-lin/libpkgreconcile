// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include <libpkgreconcile/libpkgreconcile.h>

#include "candidate_access.h"

#include <liblinediff/liblinediff.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <system_error>
#include <utility>

#include <fcntl.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace pkgreconcile {
namespace {

constexpr mode_t permission_mask = 07777;

[[noreturn]] void throw_errno(const std::string& operation,
                              const fs::path& path) {
  throw std::system_error(errno, std::generic_category(),
                          operation + ": " + path.string());
}

fs::path normalize_root(const fs::path& root) {
  if (root.empty()) {
    return fs::path{"/"};
  }

  return fs::absolute(root).lexically_normal();
}

fs::path normalize_staging_root(const fs::path& root,
                                const fs::path& staging_directory) {
  if (staging_directory.empty() || staging_directory == ".")
    throw std::invalid_argument("staging directory must not be empty");
  if (staging_directory.is_absolute())
    throw std::invalid_argument("staging directory must be root-relative");
  for (const auto& part : staging_directory) {
    if (part == "..")
      throw std::invalid_argument("staging directory must not contain '..'");
  }
  return (root / staging_directory).lexically_normal();
}

node_type classify(mode_t mode) noexcept {
  if (S_ISREG(mode)) {
    return node_type::regular;
  }
  if (S_ISDIR(mode)) {
    return node_type::directory;
  }
  if (S_ISLNK(mode)) {
    return node_type::symlink;
  }
  return node_type::other;
}

detail::raw_fingerprint fingerprint_from_stat(const struct stat& value) {
  return detail::raw_fingerprint{
      true,
      static_cast<std::uint64_t>(value.st_dev),
      static_cast<std::uint64_t>(value.st_ino),
      static_cast<std::uint64_t>(value.st_mode),
      static_cast<std::uint64_t>(value.st_uid),
      static_cast<std::uint64_t>(value.st_gid),
      static_cast<std::uint64_t>(value.st_size),
      static_cast<std::int64_t>(value.st_mtim.tv_sec),
      static_cast<std::int64_t>(value.st_mtim.tv_nsec),
      static_cast<std::int64_t>(value.st_ctim.tv_sec),
      static_cast<std::int64_t>(value.st_ctim.tv_nsec)};
}

bool same_fingerprint(const detail::raw_fingerprint& lhs,
                      const detail::raw_fingerprint& rhs) noexcept {
  return lhs.exists == rhs.exists && lhs.device == rhs.device &&
         lhs.inode == rhs.inode && lhs.mode == rhs.mode && lhs.uid == rhs.uid &&
         lhs.gid == rhs.gid && lhs.size == rhs.size &&
         lhs.modification_seconds == rhs.modification_seconds &&
         lhs.modification_nanoseconds == rhs.modification_nanoseconds &&
         lhs.change_seconds == rhs.change_seconds &&
         lhs.change_nanoseconds == rhs.change_nanoseconds;
}

class file_descriptor {
public:
  explicit file_descriptor(int value = -1) noexcept : value_(value) {}

  ~file_descriptor() {
    if (value_ != -1)
      ::close(value_);
  }

  file_descriptor(const file_descriptor&) = delete;
  file_descriptor& operator=(const file_descriptor&) = delete;

  int get() const noexcept {
    return value_;
  }

  int release() noexcept {
    const int value = value_;
    value_ = -1;
    return value;
  }

private:
  int value_;
};

void close_checked(file_descriptor& descriptor, const fs::path& path) {
  const int value = descriptor.release();
  if (value != -1 && ::close(value) == -1)
    throw_errno("close", path);
}

struct stat descriptor_stat(int descriptor, const fs::path& path) {
  struct stat value{};
  if (::fstat(descriptor, &value) == -1)
    throw_errno("fstat", path);
  return value;
}

struct observed_node {
  metadata attributes;
  detail::raw_fingerprint fingerprint;
};

observed_node observe(const fs::path& path, bool allow_missing) {
  struct stat st{};
  if (::lstat(path.c_str(), &st) == -1) {
    if (allow_missing && errno == ENOENT)
      return {};
    throw_errno("lstat", path);
  }

  return {metadata{classify(st.st_mode),
                   static_cast<mode_t>(st.st_mode & permission_mask), st.st_uid,
                   st.st_gid},
          fingerprint_from_stat(st)};
}

bool same_metadata(const metadata& lhs, const metadata& rhs) noexcept {
  if (lhs.type != rhs.type || lhs.uid != rhs.uid || lhs.gid != rhs.gid) {
    return false;
  }

  if (lhs.type == node_type::symlink) {
    return true;
  }

  return lhs.mode == rhs.mode;
}

struct regular_inspection {
  bool equal{true};
  bool text{true};
};

regular_inspection inspect_regular_file(
    const fs::path& lhs, const detail::raw_fingerprint& lhs_expected,
    const fs::path& rhs, const detail::raw_fingerprint& rhs_expected) {
  file_descriptor left(::open(lhs.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
  if (left.get() == -1)
    throw_errno("open", lhs);
  file_descriptor right(::open(rhs.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
  if (right.get() == -1)
    throw_errno("open", rhs);

  const detail::raw_fingerprint left_before =
      fingerprint_from_stat(descriptor_stat(left.get(), lhs));
  const detail::raw_fingerprint right_before =
      fingerprint_from_stat(descriptor_stat(right.get(), rhs));
  if (!same_fingerprint(left_before, lhs_expected) ||
      !same_fingerprint(right_before, rhs_expected) ||
      classify(static_cast<mode_t>(left_before.mode)) != node_type::regular ||
      classify(static_cast<mode_t>(right_before.mode)) != node_type::regular)
    throw stale_candidate_error("regular file changed during scan");

  regular_inspection result;
  std::array<char, 64U * 1024U> left_buffer{};
  std::array<char, 64U * 1024U> right_buffer{};
  bool left_eof = false;
  bool right_eof = false;

  while (!left_eof || !right_eof) {
    ssize_t left_count;
    do {
      left_count = ::read(left.get(), left_buffer.data(), left_buffer.size());
    } while (left_count == -1 && errno == EINTR);
    if (left_count == -1)
      throw_errno("read", lhs);

    ssize_t right_count;
    do {
      right_count =
          ::read(right.get(), right_buffer.data(), right_buffer.size());
    } while (right_count == -1 && errno == EINTR);
    if (right_count == -1)
      throw_errno("read", rhs);

    left_eof = left_count == 0;
    right_eof = right_count == 0;
    const std::size_t left_size = static_cast<std::size_t>(left_count);
    const std::size_t right_size = static_cast<std::size_t>(right_count);

    result.text =
        result.text &&
        std::find(left_buffer.begin(), left_buffer.begin() + left_size, '\0') ==
            left_buffer.begin() + left_size &&
        std::find(right_buffer.begin(), right_buffer.begin() + right_size,
                  '\0') == right_buffer.begin() + right_size;
    if (left_count != right_count ||
        !std::equal(left_buffer.begin(),
                    left_buffer.begin() + std::min(left_size, right_size),
                    right_buffer.begin()))
      result.equal = false;
  }

  const detail::raw_fingerprint left_after =
      fingerprint_from_stat(descriptor_stat(left.get(), lhs));
  const detail::raw_fingerprint right_after =
      fingerprint_from_stat(descriptor_stat(right.get(), rhs));
  if (!same_fingerprint(left_before, left_after) ||
      !same_fingerprint(right_before, right_after))
    throw stale_candidate_error("regular file changed while scanning");

  return result;
}

fs::path read_stable_symlink(const fs::path& path,
                             const detail::raw_fingerprint& expected) {
  const fs::path target = fs::read_symlink(path);
  const observed_node after = observe(path, false);
  if (!same_fingerprint(expected, after.fingerprint) ||
      after.attributes.type != node_type::symlink)
    throw stale_candidate_error("symbolic link changed during scan: " +
                                path.string());
  return target;
}

bool same_contents(const fs::path& installed_path,
                   const detail::raw_fingerprint& installed_fingerprint,
                   const fs::path& staged_path,
                   const detail::raw_fingerprint& staged_fingerprint,
                   node_type installed, node_type staged) {
  if (installed != staged)
    return false;
  switch (installed) {
  case node_type::directory:
    return true;
  case node_type::symlink:
    return read_stable_symlink(installed_path, installed_fingerprint) ==
           read_stable_symlink(staged_path, staged_fingerprint);
  case node_type::regular:
  case node_type::other:
  case node_type::missing:
    return false;
  }
  return false;
}

candidate_state classify_candidate(node_type installed, node_type staged,
                                   bool content_equal,
                                   bool metadata_equal) noexcept {
  if (installed == node_type::missing)
    return candidate_state::relic;
  if (installed != staged)
    return candidate_state::type_changed;
  if (content_equal && metadata_equal)
    return candidate_state::duplicate;
  if (content_equal)
    return candidate_state::metadata_only;
  return candidate_state::changed;
}

std::size_t path_depth(const fs::path& path) {
  return static_cast<std::size_t>(std::distance(path.begin(), path.end()));
}

void apply_metadata(const fs::path& path, const metadata& value) {
  struct stat actual{};
  if (::lstat(path.c_str(), &actual) == -1) {
    throw_errno("lstat", path);
  }

  if (::lchown(path.c_str(), value.uid, value.gid) == -1) {
    throw_errno("chown", path);
  }

  // Metadata source selects ownership and mode values, not node type.
  // Decide whether chmod is safe from the node that now exists.
  if (!S_ISLNK(actual.st_mode) && ::chmod(path.c_str(), value.mode) == -1) {
    throw_errno("chmod", path);
  }
}

void remove_one(const fs::path& path) {
  std::error_code error;
  const bool removed = fs::remove(path, error);
  if (error) {
    throw std::system_error(error, "remove: " + path.string());
  }
  if (!removed) {
    throw std::runtime_error("path disappeared: " + path.string());
  }
}

void remove_directory_if_empty(const fs::path& path) {
  std::error_code error;
  fs::remove(path, error);
  if (error && error != std::errc::directory_not_empty) {
    throw std::system_error(error, "remove directory: " + path.string());
  }
}

void ensure_parent(const fs::path& path) {
  std::error_code error;
  fs::create_directories(path.parent_path(), error);
  if (error) {
    throw std::system_error(error, "create directories: " +
                                       path.parent_path().string());
  }
}

bool rename_noreplace(const fs::path& source, const fs::path& destination) {
#ifdef SYS_renameat2
  if (::syscall(SYS_renameat2, AT_FDCWD, source.c_str(), AT_FDCWD,
                destination.c_str(), RENAME_NOREPLACE) == 0)
    return true;
  if (errno != ENOSYS && errno != EINVAL)
    return false;
#endif
  errno = ENOTSUP;
  return false;
}

void install_temporary(const fs::path& temporary, const fs::path& destination,
                       bool replace) {
  if (replace) {
    if (::rename(temporary.c_str(), destination.c_str()) == -1)
      throw_errno("rename", destination);
    return;
  }

  if (rename_noreplace(temporary, destination))
    return;
  if (errno == EEXIST)
    throw std::runtime_error("destination already exists: " +
                             destination.string());
  throw_errno("renameat2", destination);
}

void copy_regular_across_filesystems(const fs::path& source,
                                     const fs::path& destination,
                                     const metadata& source_metadata,
                                     bool replace) {
  ensure_parent(destination);

  std::string pattern =
      (destination.parent_path() /
       ("." + destination.filename().string() + ".pkgreconcile.XXXXXX"))
          .string();
  std::vector<char> name(pattern.begin(), pattern.end());
  name.push_back('\0');

  file_descriptor output(::mkstemp(name.data()));
  if (output.get() == -1) {
    throw_errno("mkstemp", destination.parent_path());
  }

  const fs::path temporary{name.data()};
  try {
    file_descriptor input(
        ::open(source.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (input.get() == -1) {
      throw_errno("open", source);
    }

    std::array<char, 64 * 1024> buffer{};
    for (;;) {
      const ssize_t count = ::read(input.get(), buffer.data(), buffer.size());
      if (count == 0) {
        break;
      }
      if (count == -1) {
        if (errno == EINTR) {
          continue;
        }
        throw_errno("read", source);
      }

      ssize_t written = 0;
      while (written < count) {
        const ssize_t result =
            ::write(output.get(), buffer.data() + written,
                    static_cast<std::size_t>(count - written));
        if (result == -1) {
          if (errno == EINTR) {
            continue;
          }
          throw_errno("write", temporary);
        }
        written += result;
      }
    }

    if (::fchown(output.get(), source_metadata.uid, source_metadata.gid) ==
        -1) {
      throw_errno("fchown", temporary);
    }
    if (::fchmod(output.get(), source_metadata.mode) == -1) {
      throw_errno("fchmod", temporary);
    }
    if (::fsync(output.get()) == -1) {
      throw_errno("fsync", temporary);
    }

    close_checked(input, source);
    close_checked(output, temporary);

    install_temporary(temporary, destination, replace);
    remove_one(source);
  } catch (...) {
    ::unlink(temporary.c_str());
    throw;
  }
}

void copy_symlink_across_filesystems(const fs::path& source,
                                     const fs::path& destination,
                                     const metadata& source_metadata,
                                     bool replace) {
  ensure_parent(destination);

  const fs::path target = fs::read_symlink(source);
  fs::path temporary = destination.parent_path() /
                       ("." + destination.filename().string() +
                        ".pkgreconcile." + std::to_string(::getpid()));

  for (unsigned int suffix = 0;; ++suffix) {
    std::error_code error;
    fs::create_symlink(target, temporary, error);
    if (!error) {
      break;
    }
    if (error != std::errc::file_exists) {
      throw std::system_error(error, "create symlink: " + temporary.string());
    }
    temporary = destination.parent_path() /
                ("." + destination.filename().string() + ".pkgreconcile." +
                 std::to_string(::getpid()) + "." + std::to_string(suffix + 1));
  }

  try {
    apply_metadata(temporary, source_metadata);
    install_temporary(temporary, destination, replace);
    remove_one(source);
  } catch (...) {
    ::unlink(temporary.c_str());
    throw;
  }
}

void move_replace(const fs::path& source, const fs::path& destination,
                  const metadata& source_metadata) {
  ensure_parent(destination);

  if (::rename(source.c_str(), destination.c_str()) == 0) {
    return;
  }

  if (errno != EXDEV) {
    throw_errno("rename", destination);
  }

  switch (source_metadata.type) {
  case node_type::regular:
    copy_regular_across_filesystems(source, destination, source_metadata, true);
    return;
  case node_type::symlink:
    copy_symlink_across_filesystems(source, destination, source_metadata, true);
    return;
  default:
    throw std::runtime_error(
        "cross-filesystem replacement is supported only for regular "
        "files and symbolic links: " +
        source.string());
  }
}

void move_noreplace(const fs::path& source, const fs::path& destination,
                    const metadata& source_metadata) {
  ensure_parent(destination);

  if (rename_noreplace(source, destination))
    return;
  if (errno == EEXIST)
    throw std::runtime_error("destination already exists: " +
                             destination.string());
  if (errno != EXDEV)
    throw_errno("renameat2", destination);

  switch (source_metadata.type) {
  case node_type::regular:
    copy_regular_across_filesystems(source, destination, source_metadata,
                                    false);
    return;
  case node_type::symlink:
    copy_symlink_across_filesystems(source, destination, source_metadata,
                                    false);
    return;
  default:
    throw unsupported_node_error(
        "cross-filesystem relocation is supported only for regular files "
        "and symbolic links: " +
        source.string());
  }
}

metadata choose_metadata(const candidate& value, metadata_source source) {
  return source == metadata_source::installed ? value.installed_metadata()
                                              : value.staged_metadata();
}

void validate_paired(const candidate& value) {
  if (value.state() == candidate_state::relic) {
    throw std::invalid_argument("operation requires an installed path");
  }
}

void validate_relic(const candidate& value) {
  if (value.state() != candidate_state::relic) {
    throw std::invalid_argument("operation requires a relic");
  }
}

bool path_escaped(const fs::path& relative) {
  if (relative.empty())
    return true;
  return std::any_of(relative.begin(), relative.end(),
                     [](const fs::path& part) { return part == ".."; });
}

void validate_mapping(const candidate& value, const fs::path& root,
                      const fs::path& staging_root) {
  if (path_escaped(value.relative_path()) ||
      value.installed_path() != (root / value.relative_path()) ||
      value.staged_path() != (staging_root / value.relative_path()))
    throw stale_candidate_error("candidate does not belong to this root");
}

void validate_current(const candidate& value) {
  const observed_node installed = observe(value.installed_path(), true);
  const observed_node staged = observe(value.staged_path(), true);
  if (!detail::candidate_access::installed_matches(value,
                                                   installed.fingerprint) ||
      !detail::candidate_access::staged_matches(value, staged.fingerprint))
    throw stale_candidate_error("candidate changed after scanning: " +
                                value.relative_path().string());
}

detail::raw_fingerprint fingerprint_from_fd(int descriptor,
                                            const fs::path& path) {
  return fingerprint_from_stat(descriptor_stat(descriptor, path));
}

std::string read_stable_regular(const fs::path& path, const candidate& value,
                                bool installed_side,
                                const linediff::limits& limits) {
  file_descriptor descriptor(
      ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
  if (descriptor.get() == -1)
    throw_errno("open", path);

  const detail::raw_fingerprint before =
      fingerprint_from_fd(descriptor.get(), path);
  const bool expected =
      installed_side
          ? detail::candidate_access::installed_matches(value, before)
          : detail::candidate_access::staged_matches(value, before);
  if (!expected ||
      classify(static_cast<mode_t>(before.mode)) != node_type::regular)
    throw stale_candidate_error("candidate changed before reading: " +
                                path.string());
  if (before.size > limits.max_input_bytes)
    throw linediff::limit_error("input exceeds max_input_bytes");

  std::string bytes;
  bytes.reserve(static_cast<std::size_t>(before.size));
  std::array<char, 64U * 1024U> buffer{};
  for (;;) {
    const ssize_t count =
        ::read(descriptor.get(), buffer.data(), buffer.size());
    if (count == 0)
      break;
    if (count == -1) {
      if (errno == EINTR)
        continue;
      throw_errno("read", path);
    }
    const std::size_t amount = static_cast<std::size_t>(count);
    if (amount > limits.max_input_bytes - bytes.size())
      throw linediff::limit_error("input exceeds max_input_bytes");
    bytes.append(buffer.data(), amount);
  }

  const detail::raw_fingerprint after =
      fingerprint_from_fd(descriptor.get(), path);
  if (!same_fingerprint(before, after))
    throw stale_candidate_error("file changed while reading: " + path.string());
  return bytes;
}

void write_merged_file(const fs::path& destination, const std::string& contents,
                       const metadata& selected) {
  ensure_parent(destination);

  std::string pattern =
      (destination.parent_path() /
       ("." + destination.filename().string() + ".pkgreconcile.XXXXXX"))
          .string();
  std::vector<char> name(pattern.begin(), pattern.end());
  name.push_back('\0');

  file_descriptor descriptor(::mkstemp(name.data()));
  if (descriptor.get() == -1) {
    throw_errno("mkstemp", destination.parent_path());
  }

  const fs::path temporary{name.data()};
  try {
    std::size_t offset = 0;
    while (offset < contents.size()) {
      const ssize_t count = ::write(descriptor.get(), contents.data() + offset,
                                    contents.size() - offset);
      if (count == -1) {
        if (errno == EINTR) {
          continue;
        }
        throw_errno("write", temporary);
      }
      offset += static_cast<std::size_t>(count);
    }

    if (::fchown(descriptor.get(), selected.uid, selected.gid) == -1) {
      throw_errno("fchown", temporary);
    }
    if (::fchmod(descriptor.get(), selected.mode) == -1) {
      throw_errno("fchmod", temporary);
    }
    if (::fsync(descriptor.get()) == -1) {
      throw_errno("fsync", temporary);
    }
    close_checked(descriptor, temporary);

    if (::rename(temporary.c_str(), destination.c_str()) == -1) {
      throw_errno("rename", destination);
    }
  } catch (...) {
    ::unlink(temporary.c_str());
    throw;
  }
}

} // namespace

filesystem_reconciler::filesystem_reconciler(filesystem_options options)
    : root_(normalize_root(options.root)),
      staging_root_(normalize_staging_root(root_, options.staging_directory)),
      text_limits_(options.text_limits) {
  if (text_limits_.max_input_bytes == 0U || text_limits_.max_lines == 0U ||
      text_limits_.max_trace_bytes == 0U)
    throw std::invalid_argument("text limits must be non-zero");
}

const fs::path& filesystem_reconciler::root() const noexcept {
  return root_;
}

const fs::path& filesystem_reconciler::staging_root() const noexcept {
  return staging_root_;
}

std::vector<candidate> filesystem_reconciler::scan() const {
  const observed_node staging = observe(staging_root_, true);
  if (staging.attributes.type == node_type::missing)
    throw error("staging directory not found: " + staging_root_.string());
  if (staging.attributes.type != node_type::directory)
    throw error("staging root is not a directory: " + staging_root_.string());

  std::error_code walk_error;
  fs::recursive_directory_iterator iterator(
      staging_root_, fs::directory_options::none, walk_error);
  if (walk_error)
    throw std::system_error(walk_error, "open staging directory");

  std::vector<candidate> entries;
  const fs::recursive_directory_iterator end;
  while (iterator != end) {
    const fs::path staged_path = iterator->path();
    const fs::path relative = staged_path.lexically_relative(staging_root_);
    if (path_escaped(relative))
      throw error("path escaped staging directory: " + staged_path.string());

    const fs::path installed_path = root_ / relative;
    const observed_node staged = observe(staged_path, false);
    const observed_node installed = observe(installed_path, true);

    if (staged.attributes.type == node_type::directory &&
        installed.attributes.type == node_type::missing) {
      iterator.increment(walk_error);
      if (walk_error)
        throw std::system_error(walk_error, "walk staging directory");
      continue;
    }

    const bool metadata_equal =
        same_metadata(installed.attributes, staged.attributes);
    bool content_equal = false;
    bool text_mergeable = false;
    if (installed.attributes.type == node_type::regular &&
        staged.attributes.type == node_type::regular) {
      const regular_inspection inspection =
          inspect_regular_file(installed_path, installed.fingerprint,
                               staged_path, staged.fingerprint);
      content_equal = inspection.equal;
      text_mergeable = inspection.text;
    } else {
      content_equal =
          same_contents(installed_path, installed.fingerprint, staged_path,
                        staged.fingerprint, installed.attributes.type,
                        staged.attributes.type);
    }

    entries.push_back(detail::candidate_access::make(
        relative, installed_path, staged_path, installed.attributes,
        staged.attributes,
        classify_candidate(installed.attributes.type, staged.attributes.type,
                           content_equal, metadata_equal),
        content_equal, metadata_equal, text_mergeable, installed.fingerprint,
        staged.fingerprint));

    iterator.increment(walk_error);
    if (walk_error)
      throw std::system_error(walk_error, "walk staging directory");
  }

  std::sort(entries.begin(), entries.end(),
            [](const candidate& lhs, const candidate& rhs) {
              const bool lhs_directory =
                  lhs.staged_metadata().type == node_type::directory;
              const bool rhs_directory =
                  rhs.staged_metadata().type == node_type::directory;
              if (lhs_directory != rhs_directory)
                return !lhs_directory;
              if (lhs_directory) {
                const std::size_t lhs_depth = path_depth(lhs.relative_path());
                const std::size_t rhs_depth = path_depth(rhs.relative_path());
                if (lhs_depth != rhs_depth)
                  return lhs_depth > rhs_depth;
              }
              return lhs.relative_path().generic_string() <
                     rhs.relative_path().generic_string();
            });
  return entries;
}

linediff::comparison
filesystem_reconciler::compare_text(const candidate& value) const {
  validate_mapping(value, root_, staging_root_);
  validate_current(value);
  if (value.installed_metadata().type != node_type::regular ||
      value.staged_metadata().type != node_type::regular ||
      !value.text_mergeable())
    throw std::invalid_argument(
        "text comparison requires two regular non-binary files");

  std::string installed =
      read_stable_regular(value.installed_path(), value, true, text_limits_);
  std::string staged =
      read_stable_regular(value.staged_path(), value, false, text_limits_);
  return linediff::compare(std::move(installed), std::move(staged),
                           text_limits_);
}

void filesystem_reconciler::keep(const candidate& value,
                                 metadata_source source) const {
  validate_mapping(value, root_, staging_root_);
  validate_current(value);
  validate_paired(value);

  if (source == metadata_source::staged && !value.metadata_equal()) {
    apply_metadata(value.installed_path(), value.staged_metadata());
  }

  if (value.staged_metadata().type == node_type::directory) {
    remove_directory_if_empty(value.staged_path());
  } else {
    remove_one(value.staged_path());
  }
}

void filesystem_reconciler::install(const candidate& value,
                                    metadata_source source) const {
  validate_mapping(value, root_, staging_root_);
  validate_current(value);
  validate_paired(value);

  if (value.staged_metadata().type == node_type::directory) {
    if (value.installed_metadata().type != node_type::directory) {
      throw std::runtime_error("directory type replacement is not supported");
    }
    apply_metadata(value.installed_path(), choose_metadata(value, source));
    remove_directory_if_empty(value.staged_path());
    return;
  }

  const metadata selected = choose_metadata(value, source);
  move_replace(value.staged_path(), value.installed_path(),
               value.staged_metadata());
  apply_metadata(value.installed_path(), selected);
}

void filesystem_reconciler::install_merged(const candidate& value,
                                           const std::string& contents,
                                           metadata_source source) const {
  validate_mapping(value, root_, staging_root_);
  validate_current(value);
  validate_paired(value);
  if (value.installed_metadata().type != node_type::regular ||
      value.staged_metadata().type != node_type::regular) {
    throw std::invalid_argument(
        "merged content can replace regular files only");
  }

  write_merged_file(value.installed_path(), contents,
                    choose_metadata(value, source));
  remove_one(value.staged_path());
}

void filesystem_reconciler::restore(const candidate& value) const {
  validate_mapping(value, root_, staging_root_);
  validate_current(value);
  validate_relic(value);
  move_noreplace(value.staged_path(), value.installed_path(),
                 value.staged_metadata());
}

void filesystem_reconciler::relocate_relic(const candidate& value,
                                           const fs::path& destination) const {
  validate_mapping(value, root_, staging_root_);
  validate_current(value);
  validate_relic(value);
  if (!destination.is_absolute()) {
    throw std::invalid_argument("relic destination must be absolute");
  }

  const fs::path relative = destination.relative_path().lexically_normal();
  for (const auto& part : relative) {
    if (part == "..") {
      throw std::invalid_argument(
          "relic destination must remain inside the target root");
    }
  }

  const fs::path target = root_ / relative;
  move_noreplace(value.staged_path(), target, value.staged_metadata());
}

void filesystem_reconciler::remove_relic(const candidate& value) const {
  validate_mapping(value, root_, staging_root_);
  validate_current(value);
  validate_relic(value);
  remove_one(value.staged_path());
}

void filesystem_reconciler::cleanup_empty_directories() const {
  std::vector<fs::path> directories;
  for (const auto& item : fs::recursive_directory_iterator(staging_root_)) {
    std::error_code error;
    const fs::file_status status = item.symlink_status(error);
    if (error) {
      throw std::system_error(error,
                              "inspect directory: " + item.path().string());
    }
    if (fs::is_directory(status)) {
      directories.push_back(item.path());
    }
  }

  std::sort(directories.begin(), directories.end(),
            [](const fs::path& lhs, const fs::path& rhs) {
              return path_depth(lhs) > path_depth(rhs);
            });

  for (const auto& directory : directories) {
    std::error_code error;
    fs::remove(directory, error);
    if (error && error != std::errc::directory_not_empty) {
      throw std::system_error(error, "remove directory: " + directory.string());
    }
  }
}

} // namespace pkgreconcile
