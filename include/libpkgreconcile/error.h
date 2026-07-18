// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LIBPKGRECONCILE_ERROR_H
#define LIBPKGRECONCILE_ERROR_H

#include <stdexcept>

#include <libpkgreconcile/visibility.h>

namespace pkgreconcile {

/** Base class for semantic errors reported by libpkgreconcile. */
class PKGRECONCILE_API error : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

/** Raised when a scanned candidate no longer matches live state. */
class PKGRECONCILE_API stale_candidate_error : public error {
public:
  using error::error;
};

/** Raised when an operation cannot represent a filesystem node type. */
class PKGRECONCILE_API unsupported_node_error : public error {
public:
  using error::error;
};

} // namespace pkgreconcile

#endif
