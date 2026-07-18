Project history
===============

`rejmerge` originated in CRUX `pkgutils` as an interactive utility for files
rejected during package upgrades.

Zeppe-Lin maintained a separate shell implementation derived from that
utility. That implementation remains available in repository history with its
original notices.

`libpkgreconcile` is a new C++17 implementation written for Zeppe-Lin. It
separates filesystem reconciliation semantics from the optional `rejmerge`
terminal frontend and delegates byte-preserving line comparison to
`liblinediff`.

The current source tree contains no CRUX implementation source. Historical
lineage is recorded here; current copyright ownership is recorded in
`COPYRIGHT`.
