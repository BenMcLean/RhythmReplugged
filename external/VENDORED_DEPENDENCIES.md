# Vendored Dependencies

This directory contains third-party source code vendored into Rhythm Replugged.

## midifile

Source location:
- `external/midifile`

Upstream source:
- <https://github.com/craigsapp/midifile>

Integration method:
- Added as a git subtree.
- Built from the top-level project with `add_subdirectory(external/midifile)`.

Local downstream changes:
- `external/midifile/CMakeLists.txt`
  - Raised `cmake_minimum_required(...)` to a modern supported version.

Reason for local change:
- CMake 4 no longer supports compatibility behavior requested by very old
  `cmake_minimum_required()` values such as `2.8`.
- Without this patch, a normal configure fails before the vendored library can
  be built.

Upstream maintenance note:
- This is an intentional local compatibility patch for modern CMake.
- When pulling future upstream changes into the subtree, keep this patch if
  upstream has not raised the minimum yet; otherwise drop it.
