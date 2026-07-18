// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LIBPKGRECONCILE_CANDIDATE_ACCESS_H
#define LIBPKGRECONCILE_CANDIDATE_ACCESS_H

#include <libpkgreconcile/libpkgreconcile.h>

#include <utility>

namespace pkgreconcile {
namespace detail {

struct raw_fingerprint {
  bool exists{false};
  std::uint64_t device{0};
  std::uint64_t inode{0};
  std::uint64_t mode{0};
  std::uint64_t uid{0};
  std::uint64_t gid{0};
  std::uint64_t size{0};
  std::int64_t modification_seconds{0};
  std::int64_t modification_nanoseconds{0};
  std::int64_t change_seconds{0};
  std::int64_t change_nanoseconds{0};
};

struct candidate_access {
  static candidate make(std::filesystem::path relative_path,
                        std::filesystem::path installed_path,
                        std::filesystem::path staged_path, metadata installed,
                        metadata staged, candidate_state state,
                        bool content_equal, bool metadata_equal,
                        bool text_mergeable,
                        const raw_fingerprint& installed_fingerprint,
                        const raw_fingerprint& staged_fingerprint);

  static bool installed_matches(const candidate& value,
                                const raw_fingerprint& actual) noexcept;
  static bool staged_matches(const candidate& value,
                             const raw_fingerprint& actual) noexcept;

private:
  static bool fingerprint_matches(const candidate::fingerprint& expected,
                                  const raw_fingerprint& actual) noexcept;
};

} // namespace detail
} // namespace pkgreconcile

#endif
