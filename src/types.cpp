// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include <libpkgreconcile/types.h>

namespace pkgreconcile {

const char* to_string(node_type value) noexcept {
  switch (value) {
  case node_type::missing:
    return "missing";
  case node_type::regular:
    return "regular file";
  case node_type::directory:
    return "directory";
  case node_type::symlink:
    return "symbolic link";
  case node_type::other:
    return "special file";
  }
  return "unknown";
}

const char* to_string(candidate_state value) noexcept {
  switch (value) {
  case candidate_state::duplicate:
    return "duplicate";
  case candidate_state::metadata_only:
    return "metadata differs";
  case candidate_state::changed:
    return "contents differ";
  case candidate_state::type_changed:
    return "type differs";
  case candidate_state::relic:
    return "no installed counterpart";
  }
  return "unknown";
}

} // namespace pkgreconcile
