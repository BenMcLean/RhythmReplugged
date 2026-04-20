# Rhythm Replugged

Rhythm Replugged is a C++ game project built with CMake, SDL3, and vcpkg manifest dependencies.

The code under `src/` is currently split into:

- `src/libretro_contract` for host-facing abstractions
- `src/core` for song browsing, metadata parsing, playback state, chart state, and audio logic
- `src/ui` for presentation code that renders core-owned view data with Dear ImGui
- `src/platform_sdl3` for the standalone SDL3 host, input polling, texture loading, and audio output wiring

The current architecture plan is documented in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## What worked for me on Windows

### Tools

- Visual Studio 2022 or Build Tools for Visual Studio 2022 with the C++ workload
- CMake
- vcpkg
- VS Code with the CMake Tools extension

### Build in VS Code

1. Install the CMake Tools extension if prompted.
2. Let CMake configure on open.
3. Build with the CMake Tools status bar button, or use the `windows-x86-debug` preset.
4. If the launch target needs to be selected manually, choose `RhythmReplugged`.
5. Press the CMake Tools run button to launch it.

### Command line build

```powershell
cmake --preset windows-x86
cmake --build --preset windows-x86-debug
```

vcpkg manifest dependencies are restored automatically during configure/build.

`cmake_minimum_required()` is only a minimum version floor. It does not guarantee that every future CMake release will continue to work unchanged.
