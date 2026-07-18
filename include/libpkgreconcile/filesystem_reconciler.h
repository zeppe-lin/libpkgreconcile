// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LIBPKGRECONCILE_FILESYSTEM_RECONCILER_H
#define LIBPKGRECONCILE_FILESYSTEM_RECONCILER_H

#include <filesystem>
#include <string>
#include <vector>

#include <liblinediff/compare.h>
#include <liblinediff/document.h>

#include <libpkgreconcile/candidate.h>
#include <libpkgreconcile/visibility.h>

namespace pkgreconcile {

/** Paths and liblinediff resource limits for the filesystem backend. */
struct filesystem_options {
  /** Target root containing the installed tree. */
  std::filesystem::path root{"/"};

  /** Staging directory interpreted relative to root. */
  std::filesystem::path staging_directory{"var/lib/pkg/rejected"};

  /** Limits applied when paired regular text files are compared. */
  linediff::limits text_limits{};
};

/**
 * Read-only classifier and explicit mutation engine for a mirrored staging
 * tree.
 *
 * scan() never mutates either tree. Every resolution method validates that
 * the candidate belongs to this reconciler and still names the filesystem
 * objects observed during scanning.
 */
class PKGRECONCILE_API filesystem_reconciler {
public:
  /**
   * Construct a reconciler for one target tree.
   *
   * @param options Root, staging path, and text-comparison limits.
   * @throws std::invalid_argument for an unsafe staging path or zero limit.
   */
  explicit filesystem_reconciler(filesystem_options options = {});

  /** Return the normalized target root. */
  const std::filesystem::path& root() const noexcept;

  /** Return the normalized staging root. */
  const std::filesystem::path& staging_root() const noexcept;

  /**
   * Inspect the staging tree without changing it.
   *
   * Non-directories are returned first in lexical order. Paired directories
   * follow deepest first so child dispositions precede directory metadata and
   * cleanup.
   */
  std::vector<candidate> scan() const;

  /**
   * Read and compare one paired regular text candidate with liblinediff.
   *
   * The returned comparison owns both exact byte documents. Rendering policy
   * remains with the consumer.
   */
  linediff::comparison compare_text(const candidate& value) const;

  /** Keep installed contents and remove the staged object. */
  void keep(const candidate& value, metadata_source source) const;

  /** Install staged contents at the installed path. */
  void install(const candidate& value, metadata_source source) const;

  /** Install caller-edited regular-file bytes and remove the staged file. */
  void install_merged(const candidate& value, const std::string& contents,
                      metadata_source source) const;

  /** Restore a relic to its original logical path below root. */
  void restore(const candidate& value) const;

  /** Relocate a relic to another absolute logical path below root. */
  void relocate_relic(const candidate& value,
                      const std::filesystem::path& destination) const;

  /** Explicitly remove a relic from staging. */
  void remove_relic(const candidate& value) const;

  /** Remove empty real directories below the staging root. */
  void cleanup_empty_directories() const;

private:
  std::filesystem::path root_;
  std::filesystem::path staging_root_;
  linediff::limits text_limits_;
};

} // namespace pkgreconcile

#endif
