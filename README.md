libpkgreconcile
===============

`libpkgreconcile` is a C++17 library for inspecting and resolving package
objects staged under `/var/lib/pkg/rejected`.

It currently provides:

* deterministic discovery of staged regular files, directories, symbolic
  links, and special filesystem objects;
* immutable candidates that bind an installed path, a staged path,
  classification, metadata, and private stale-state fingerprints;
* duplicate, metadata-only, changed, type-changed, and relic states;
* byte-preserving regular-text comparison through `liblinediff`;
* independent content and metadata dispositions;
* explicit keep, install, edited-install, restore, relocate, delete, and
  staging-cleanup operations; and
* an optional `rejmerge(8)` reference frontend.

Scanning is read-only. No duplicate, relic, or empty directory is removed as a
side effect of inspection. Every mutation validates that the candidate still
belongs to the selected target root and still names the filesystem objects
observed by `scan()`.

The current backend is the mirrored filesystem staging tree. The library does
not inspect package archives, update the package database, resolve package
dependencies, coordinate a complete package transaction, or decide which
resolution policy a user interface should select.

The implementation is original Zeppe-Lin code. It is not derived from CRUX
`pkgutils` or from the former CRUX-derived shell implementation of
`rejmerge`.

Model
-----

The public flow is deliberately explicit:

```text
filesystem_reconciler::scan()
             |
             v
      immutable candidate
       |             |
       |             `-- compare_text() -> linediff::comparison
       |
       `-- keep / install / install_merged
           restore / relocate_relic / remove_relic
```

`compare_text()` returns an owned `linediff::comparison`. Unified-diff and
conflict-marker presentation remain consumer policy and are implemented by
`liblinediff`, not by the reconciliation engine.

Regular files containing NUL bytes remain valid keep/install candidates but
are not classified as text-mergeable. Invalid UTF-8, CR bytes, and missing
final newlines are otherwise preserved as exact bytes.

Candidate validity
------------------

A candidate is an observation, not a mutable command object. Callers cannot
construct one directly.

Before comparison or mutation, the reconciler verifies:

* that the candidate's logical and physical paths map to this reconciler;
* that installed and staged node identity, type, ownership, and mode still
  match the scan; and
* for non-directory objects, that size and change timestamps still match.

Directory timestamps are deliberately excluded from stale validation because
resolving a child necessarily changes its staging parent's directory
metadata. Directory identity, type, ownership, and mode remain bound.

These checks reject stale or replayed decisions. They do not turn a sequence
of filesystem operations into a complete rollback-capable package
transaction.

Requirements
------------

Build-time requirements:

* Linux;
* a C++17 compiler;
* Meson 1.6.0 or later;
* Ninja;
* pkg-config; and
* `liblinediff` 0.1.0 or later.

Python 3 is required for black-box command tests. Doxygen and `scdoc` are
optional documentation dependencies.

Building
--------

Shared library:

```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

Static library with static dependencies:

```sh
meson setup build-static \
  -Ddefault_library=static \
  -Dlink_mode=static
meson compile -C build-static
meson test -C build-static --print-errorlogs
```

Installation:

```sh
meson install -C build
```

The project rejects `default_library=both`. Shared and static artifacts are
separate builds, and each build links the corresponding form of
`liblinediff`.

Tests may be disabled with:

```sh
meson setup build -Dtests=disabled
```

Reference tool
--------------

`rejmerge` is built by default so the public API is exercised through a real
administrative workflow. It is not installed by default.

```sh
meson setup build-no-tools -Dtools=disabled
meson setup build-install-tools -Dinstall_tools=true
```

The frontend reads `VISUAL`, `EDITOR`, `PAGER`, and `TMPDIR`. It has no
configuration-file interpreter. Dry-run mode is strictly non-mutating.

The tool is a reference and compatibility client. The library is the project
interface.

API documentation
-----------------

Public interfaces are documented with Doxygen comments under
`include/libpkgreconcile`.

```sh
doxygen Doxyfile
```

Generated HTML is written to `build/docs/html`.

Using the library
-----------------

```cpp
#include <libpkgreconcile/libpkgreconcile.h>
#include <liblinediff/render.h>

pkgreconcile::filesystem_options options;
options.root = "/";

pkgreconcile::filesystem_reconciler reconciler(options);
for (const pkgreconcile::candidate& value : reconciler.scan()) {
  if (value.text_mergeable() && !value.content_equal()) {
    linediff::unified_options rendering;
    rendering.old_label = value.installed_path().string();
    rendering.new_label = value.staged_path().string();
    const std::string diff = linediff::render_unified(
        reconciler.compare_text(value), rendering);
    // Present diff and obtain an explicit disposition.
  }
}
```

Compiler and linker flags are available through pkg-config:

```sh
pkg-config --cflags --libs libpkgreconcile
pkg-config --static --libs libpkgreconcile
```

Documentation
-------------

* `DESIGN.md` — current architecture and invariants;
* `TESTING.md` — test doctrine and suite inventory;
* `MIGRATION.md` — behavioral changes from the shell utility;
* `HISTORY.md` — project lineage;
* `libpkgreconcile(3)` — library interface; and
* `rejmerge(8)` — optional reference frontend.

Layout
------

* `include/libpkgreconcile/` — documented public API;
* `src/` — filesystem backend and candidate implementation;
* `tools/` — optional `rejmerge` reference client;
* `tests/` — model, comparison, mutation, stale-state, terminal, and CLI tests;
* `man/` — scdoc manual sources; and
* `.github/workflows/` — shared/static, compiler, and sanitizer CI.

License
-------

`libpkgreconcile` is licensed under the GNU General Public License version 3
or later. See `COPYING` for the license terms and `COPYRIGHT` for notices.
