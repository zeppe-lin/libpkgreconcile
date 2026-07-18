Contributing
============

Changes must preserve the boundary documented in `DESIGN.md`: filesystem
reconciliation belongs here, line comparison belongs in `liblinediff`, and
package database or transaction coordination belongs elsewhere.

Before submitting a change:

```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

New public interfaces require Doxygen comments and tests. Filesystem behavior
requires both successful postconditions and failure-path coverage. A change to
stale-state validation must demonstrate the legitimate sequence it admits and
the concurrent mutation it still rejects.

Commit messages should describe one semantic change and explain why the
contract changes. Do not mix formatting churn with filesystem behavior.

Contributions are accepted under GPL-3.0-or-later. Add an appropriate SPDX
copyright line to new source files.
