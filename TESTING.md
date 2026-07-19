Testing libpkgreconcile
=======================

Doctrine
--------

Package-state mutation code is tested at its semantic boundaries, not only
through command output.

The suite keeps independent layers for:

* scan classification and deterministic ordering;
* explicit paired and relic dispositions;
* `liblinediff` integration and exact-byte ownership;
* stale-observation rejection;
* terminal workflow policy;
* black-box executable behavior; and
* shared-library symbol-boundary enforcement.

Each filesystem test receives a fresh target root and mirrored staging tree.
Tests use exact byte reads, `lstat(2)`-relevant node types, mode checks, and
postcondition checks on both installed and staged paths.

Current suite
-------------

The C++ suites contain 102 named cases:

* 26 scan and classification cases;
* 29 disposition and cleanup cases;
* 9 `liblinediff` integration cases;
* 10 stale-candidate cases;
* 7 semantic ANSI and color-policy cases; and
* 21 terminal workflow cases.

The comparison suite additionally generates 2,000 deterministic text pairs.
For every pair it checks that:

* the comparison owns the exact installed and staged bytes;
* applying the edit script reconstructs the staged bytes; and
* identity classification agrees with byte equality.

The Python suite contains 20 black-box CLI cases covering option parsing,
strict dry-run immutability, binary output, hostile PATH contents, pathnames
with spaces, privilege boundaries, redirected color modes, `NO_COLOR`, and
real pseudo-terminal detection.

The color suite verifies semantic record styling, exact reset placement before
LF and CRLF endings, plain-renderer identity, context neutrality, NUL-byte
preservation, and the automatic/always/never policy matrix. Terminal tests also
prove that ANSI display state never enters an installed conflict copy.

Every public header is compiled in an independent translation unit, and the
umbrella header is compiled as an unrelated client. Shared builds also inspect
the dynamic symbol table and reject private candidate-construction and detail
names.

Running
-------

```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

Static build:

```sh
meson setup build-static \
  -Ddefault_library=static \
  -Dlink_mode=static
meson compile -C build-static
meson test -C build-static --print-errorlogs
```

Sanitizer build:

```sh
meson setup build-sanitize \
  -Db_sanitize=address,undefined \
  -Db_lundef=false
meson compile -C build-sanitize
meson test -C build-sanitize --print-errorlogs
```

Regression obligations
----------------------

A change to scanning must add cases for the affected node type, path shape,
and order. A change to mutation code must prove installed and staged
postconditions and at least one failure path. A change to stale validation
must prove both the newly accepted legitimate sequence and a rejected
concurrent mutation.

A frontend-only test is not sufficient evidence for a library invariant.
