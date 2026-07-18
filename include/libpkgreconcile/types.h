// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LIBPKGRECONCILE_TYPES_H
#define LIBPKGRECONCILE_TYPES_H

#include <sys/types.h>

#include <libpkgreconcile/visibility.h>

namespace pkgreconcile {

/** Filesystem node classification obtained without following symlinks. */
enum class node_type {
  missing,   /**< No object exists at the path. */
  regular,   /**< Regular file. */
  directory, /**< Directory. */
  symlink,   /**< Symbolic link itself, not its target. */
  other,     /**< FIFO, socket, device node, or another special object. */
};

/** Ownership, permissions, and node type observed during scanning. */
struct metadata {
  node_type type{node_type::missing}; /**< Observed object type. */
  mode_t mode{0};                     /**< Permission and special mode bits. */
  uid_t uid{0};                       /**< Numeric owner identifier. */
  gid_t gid{0};                       /**< Numeric group identifier. */
};

/** Semantic state assigned to one staged package object. */
enum class candidate_state {
  duplicate,     /**< Contents and metadata are equal. */
  metadata_only, /**< Contents are equal but metadata differs. */
  changed,       /**< Comparable contents differ. */
  type_changed,  /**< Installed and staged object types differ. */
  relic,         /**< No installed counterpart exists. */
};

/** Metadata source selected independently from content disposition. */
enum class metadata_source {
  installed, /**< Preserve installed ownership and mode. */
  staged,    /**< Adopt staged ownership and mode. */
};

/** Return a stable human-readable node type. */
PKGRECONCILE_API const char* to_string(node_type value) noexcept;

/** Return a stable human-readable candidate state. */
PKGRECONCILE_API const char* to_string(candidate_state value) noexcept;

} // namespace pkgreconcile

#endif
