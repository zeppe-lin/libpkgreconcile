libpkgreconcile design
======================

Purpose
-------

`libpkgreconcile` resolves objects represented by the current Zeppe-Lin
filesystem staging contract:

```text
installed: ROOT/<path>
staged:    ROOT/var/lib/pkg/rejected/<path>
```

The library owns discovery, classification, stale-state validation, and the
individual filesystem dispositions implemented by that contract. It does not
own package database updates or complete transaction coordination.

Architectural boundary
----------------------

```text
consumer or rejmerge
        |
        v
filesystem_reconciler
        |-- candidate observation
        |-- explicit disposition
        `-- compare_text()
                 |
                 v
            liblinediff
```

`liblinediff` owns line indexing, Myers comparison, and renderers.
`libpkgreconcile` owns filesystem meaning: which two objects correspond,
whether they are text candidates, and whether an observation remains valid.

Candidate model
---------------

A `candidate` is immutable to callers and is constructible only by
`filesystem_reconciler::scan()`.

It contains:

* a root-relative logical path;
* installed and staged paths;
* installed and staged metadata;
* one semantic state;
* content, metadata, and text-mergeability flags; and
* private fingerprints for stale-state validation.

The states are:

* `duplicate` — contents and metadata match;
* `metadata_only` — contents match, metadata differs;
* `changed` — comparable contents differ;
* `type_changed` — installed and staged node types differ; and
* `relic` — no installed counterpart exists.

A candidate is not a serialized transaction plan. It is valid only against
the reconciler and filesystem state from which it was scanned.

Scan invariants
---------------

Scanning:

* uses `lstat(2)` semantics and does not follow staged symlink directories;
* never mutates installed or staged state;
* retains pathnames containing whitespace, newlines, or leading dots;
* rejects staging-root escape through absolute or `..` components;
* compares regular-file bytes in bounded streaming blocks;
* compares symlink target bytes;
* classifies other node types without attempting to read their payloads; and
* returns non-directories lexically before directories ordered deepest first.

A staged directory without an installed directory counterpart is omitted.
The current mirrored tree cannot distinguish a rejected empty directory from
a structural parent created only to hold rejected children. The library does
not invent semantics where the storage format carries none.

Text comparison
---------------

`compare_text()` accepts paired regular files that contained no NUL byte when
scanned. It reopens them with `O_NOFOLLOW`, validates their fingerprints,
reads them through stable file descriptors, checks for changes during the
read, and passes exact bytes and configured limits to `liblinediff`.

It returns `linediff::comparison`. The reconciliation API does not expose a
private diff format and does not decide presentation labels or context size.

Metadata
--------

Content and metadata decisions are independent.

For regular files and directories, metadata selection controls uid, gid, and
mode. For symlinks, ownership is applied to the link itself and mode is not
followed to the target.

Directory validation ignores size and timestamps because resolving a child
changes those fields on its parent. Device, inode, type, ownership, and mode
remain checked. Non-directory fingerprints retain size, modification time,
and change time.

Dispositions
------------

Paired candidates support:

* `keep()` — preserve installed contents and remove staged state;
* `install()` — move or copy staged contents into the installed path; and
* `install_merged()` — write caller-supplied regular-file bytes through a
  same-directory temporary file and rename.

Relics support:

* `restore()` — install at the original logical path;
* `relocate_relic()` — install at another absolute logical path below root;
  and
* `remove_relic()` — explicitly discard the staged object.

`cleanup_empty_directories()` removes empty real directories below the
staging root and never follows symlinks.

Mutation safety
---------------

Every disposition validates candidate mapping and current fingerprints before
mutation. Relic restore and relocation use `renameat2(RENAME_NOREPLACE)` so a destination appearing after validation is not overwritten.
Same-filesystem renames are preferred; regular files and symlinks have an
explicit cross-filesystem copy fallback.

The implementation does not claim atomicity across multiple candidates, the
package database, or post-installation actions. Consumers requiring a package
transaction must coordinate those layers outside this library.

Reference frontend
------------------

The `rejmerge` executable is a small terminal client. It owns:

* prompts;
* pager and editor execution;
* unified and conflict rendering through `liblinediff`; and
* root privilege checks for mutating operation.

It does not add configuration-language hooks or hidden filesystem policy to
the library.
