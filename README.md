libpkgreconcile
===============

`libpkgreconcile` is a C++17 library for representing package objects that
require an explicit resolution decision.

The initial model provides typed filesystem metadata, immutable candidates,
stable state names, and private stale-state fingerprints. Filesystem discovery
and mutation are added separately so the observation contract remains visible.

The project is licensed under the GNU General Public License version 3 or
later. See `COPYING` for the license terms and `COPYRIGHT` for notices.
