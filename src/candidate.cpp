// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include <libpkgreconcile/libpkgreconcile.h>

#include "candidate_access.h"

#include <utility>

#include <sys/stat.h>

namespace pkgreconcile {

candidate::candidate(std::filesystem::path relative_path,
                     std::filesystem::path installed_path,
                     std::filesystem::path staged_path, metadata installed,
                     metadata staged, candidate_state state, bool content_equal,
                     bool metadata_equal, bool text_mergeable,
                     fingerprint installed_fingerprint,
                     fingerprint staged_fingerprint)
    : relative_path_(std::move(relative_path)),
      installed_path_(std::move(installed_path)),
      staged_path_(std::move(staged_path)), installed_(installed),
      staged_(staged), state_(state), content_equal_(content_equal),
      metadata_equal_(metadata_equal), text_mergeable_(text_mergeable),
      installed_fingerprint_(installed_fingerprint),
      staged_fingerprint_(staged_fingerprint) {}

const std::filesystem::path& candidate::relative_path() const noexcept {
  return relative_path_;
}
const std::filesystem::path& candidate::installed_path() const noexcept {
  return installed_path_;
}
const std::filesystem::path& candidate::staged_path() const noexcept {
  return staged_path_;
}
const metadata& candidate::installed_metadata() const noexcept {
  return installed_;
}
const metadata& candidate::staged_metadata() const noexcept {
  return staged_;
}
candidate_state candidate::state() const noexcept {
  return state_;
}
bool candidate::content_equal() const noexcept {
  return content_equal_;
}
bool candidate::metadata_equal() const noexcept {
  return metadata_equal_;
}
bool candidate::text_mergeable() const noexcept {
  return text_mergeable_;
}

candidate detail::candidate_access::make(
    std::filesystem::path relative_path, std::filesystem::path installed_path,
    std::filesystem::path staged_path, metadata installed, metadata staged,
    candidate_state state, bool content_equal, bool metadata_equal,
    bool text_mergeable, const raw_fingerprint& i, const raw_fingerprint& s) {
  const candidate::fingerprint installed_fingerprint{i.exists,
                                                     i.device,
                                                     i.inode,
                                                     i.mode,
                                                     i.uid,
                                                     i.gid,
                                                     i.size,
                                                     i.modification_seconds,
                                                     i.modification_nanoseconds,
                                                     i.change_seconds,
                                                     i.change_nanoseconds};
  const candidate::fingerprint staged_fingerprint{s.exists,
                                                  s.device,
                                                  s.inode,
                                                  s.mode,
                                                  s.uid,
                                                  s.gid,
                                                  s.size,
                                                  s.modification_seconds,
                                                  s.modification_nanoseconds,
                                                  s.change_seconds,
                                                  s.change_nanoseconds};

  return candidate(std::move(relative_path), std::move(installed_path),
                   std::move(staged_path), installed, staged, state,
                   content_equal, metadata_equal, text_mergeable,
                   installed_fingerprint, staged_fingerprint);
}

bool detail::candidate_access::fingerprint_matches(
    const candidate::fingerprint& expected,
    const raw_fingerprint& actual) noexcept {
  if (expected.exists != actual.exists || expected.device != actual.device ||
      expected.inode != actual.inode || expected.mode != actual.mode ||
      expected.uid != actual.uid || expected.gid != actual.gid)
    return false;

  if (!expected.exists || S_ISDIR(static_cast<mode_t>(expected.mode)))
    return true;

  return expected.size == actual.size &&
         expected.modification_seconds == actual.modification_seconds &&
         expected.modification_nanoseconds == actual.modification_nanoseconds &&
         expected.change_seconds == actual.change_seconds &&
         expected.change_nanoseconds == actual.change_nanoseconds;
}

bool detail::candidate_access::installed_matches(
    const candidate& value, const raw_fingerprint& actual) noexcept {
  return fingerprint_matches(value.installed_fingerprint_, actual);
}

bool detail::candidate_access::staged_matches(
    const candidate& value, const raw_fingerprint& actual) noexcept {
  return fingerprint_matches(value.staged_fingerprint_, actual);
}

} // namespace pkgreconcile
