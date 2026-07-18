Migrating from rejmerge 5.x
===========================

Version 0.1.0 introduces `libpkgreconcile` and rebuilds `rejmerge` as an
optional client of the library.

Storage
-------

The current staging directory remains:

```text
/var/lib/pkg/rejected
```

An alternate root continues to map both the installed tree and staging tree
below that root.

Configuration
-------------

`/etc/rejmerge.conf` is not read. The former file executed shell code and
could replace internal functions. The library exposes one documented C++
contract instead.

`VISUAL`, `EDITOR`, `PAGER`, and `TMPDIR` remain frontend environment
controls. The `-c` and `--config` options are absent.

Dry-run
-------

Dry-run is strictly read-only. It does not remove duplicate files, relics, or
empty staging directories and does not change content or metadata.

Relics
------

A staged object without an installed counterpart is classified as a relic. It
is not deleted automatically. The frontend offers restore, relocate, delete,
and skip dispositions.

Directories
-----------

Paired directory metadata is represented and processed after children.
Structural staging directories without installed counterparts are not exposed
as relics because the current storage format does not identify whether they
are package objects.

Comparison
----------

Text comparison is provided by `liblinediff`. No `diff(1)` process is
executed. Binary regular files remain keep/install capable but are not offered
for text conflict editing.

Embedding
---------

Native consumers include:

```cpp
#include <libpkgreconcile/libpkgreconcile.h>
```

The `rejmerge` executable is not the library ABI. It is built as a reference
and compatibility frontend and is installed only when requested at build
time.
