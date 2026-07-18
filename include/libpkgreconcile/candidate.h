// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LIBPKGRECONCILE_CANDIDATE_H
#define LIBPKGRECONCILE_CANDIDATE_H

#include <cstdint>
#include <filesystem>

#include <libpkgreconcile/types.h>
#include <libpkgreconcile/visibility.h>

namespace pkgreconcile {

namespace detail {
struct candidate_access;
}

/**
 * Immutable observation of one installed/staged path pair.
 *
 * Candidate objects are created only by filesystem_reconciler::scan(). They
 * bind the logical path, classification, and private filesystem fingerprints
 * used to reject stale resolution attempts.
 */
class PKGRECONCILE_API candidate {
public:
  candidate(const candidate&) = default;
  candidate(candidate&&) noexcept = default;
  candidate& operator=(const candidate&) = default;
  candidate& operator=(candidate&&) noexcept = default;
  ~candidate() = default;

  /** Return the root-relative logical package path. */
  const std::filesystem::path& relative_path() const noexcept;

  /** Return the path in the selected installed tree. */
  const std::filesystem::path& installed_path() const noexcept;

  /** Return the corresponding path in the staging tree. */
  const std::filesystem::path& staged_path() const noexcept;

  /** Return installed metadata, or node_type::missing for a relic. */
  const metadata& installed_metadata() const noexcept;

  /** Return staged metadata. */
  const metadata& staged_metadata() const noexcept;

  /** Return the classification assigned by the scan. */
  candidate_state state() const noexcept;

  /** Return whether installed and staged contents compare equal. */
  bool content_equal() const noexcept;

  /** Return whether applicable type, ownership, and mode compare equal. */
  bool metadata_equal() const noexcept;

  /** Return whether both sides are regular files without NUL bytes. */
  bool text_mergeable() const noexcept;

private:
  struct fingerprint {
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

  PKGRECONCILE_LOCAL candidate(std::filesystem::path relative_path,
                               std::filesystem::path installed_path,
                               std::filesystem::path staged_path,
                               metadata installed, metadata staged,
                               candidate_state state, bool content_equal,
                               bool metadata_equal, bool text_mergeable,
                               fingerprint installed_fingerprint,
                               fingerprint staged_fingerprint);

  std::filesystem::path relative_path_;
  std::filesystem::path installed_path_;
  std::filesystem::path staged_path_;
  metadata installed_;
  metadata staged_;
  candidate_state state_{candidate_state::changed};
  bool content_equal_{false};
  bool metadata_equal_{false};
  bool text_mergeable_{false};
  fingerprint installed_fingerprint_;
  fingerprint staged_fingerprint_;

  friend struct detail::candidate_access;
};

} // namespace pkgreconcile

#endif
