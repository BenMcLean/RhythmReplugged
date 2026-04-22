# Rhythm Replugged

Rhythm Replugged is a C++ game project built with CMake, SDL3, and vcpkg manifest dependencies.

The code under `src/` is currently split into:

- `src/libretro_contract` for host-facing abstractions
- `src/core` for song browsing, metadata parsing, playback state, chart state, and audio logic
- `src/ui` for presentation code that renders core-owned view data with Dear ImGui
- `src/platform_sdl3` for the standalone SDL3 host, input polling, texture loading, and audio output wiring

The current architecture plan is documented in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Host Boundaries

The current build is organized around a strict host split:

- `src/core` must not depend on SDL or the libretro C ABI.
- `src/ui` must not depend on SDL or the libretro C ABI.
- `src/platform_sdl3` is the standalone desktop host and may depend on SDL.
- `src/platform_libretro` is the libretro host and may depend on `libretro.h`.

For Dear ImGui specifically:

- `platform_sdl3` builds the SDL3 platform backend and the OpenGL renderer backend.
- `platform_libretro` builds only the OpenGL renderer backend and uses project-owned libretro platform glue.

This keeps the libretro core free of SDL link dependencies while still allowing the SDL host to reuse the same UI layer.

## Native Windows Build

### Tools

- Visual Studio 2022 or Build Tools for Visual Studio 2022 with the C++ workload
- CMake
- vcpkg
- VS Code with the CMake Tools extension

Set `VCPKG_ROOT` to your local vcpkg checkout before configuring. That is the path the shared CMake presets use on both Windows and Linux.

### Build in VS Code

1. Install the CMake Tools extension if prompted.
2. Let CMake configure on open.
3. Select the `windows-x64` configure preset.
4. Build with the CMake Tools status bar button, or use the `windows-x64-debug` build preset.
5. If the launch target needs to be selected manually, choose `RhythmReplugged`.
6. Use the `Debug CMake Target (Windows/MSVC)` launch configuration to debug on Windows.

### Keeping Windows And Linux Side By Side

This repo keeps both desktop workflows available at the same time:

- Windows configure presets: `windows-x64`
- Linux configure presets: `linux-debug`, `linux-release`
- Windows VS Code debugger: `Debug CMake Target (Windows/MSVC)`
- Linux VS Code debugger: `Debug CMake Target (Linux/GDB)`

Choose the preset and debugger that match the machine you are currently on. The same workspace can support both without removing either path.

### Windows VS Code Quick Start

1. Install the CMake Tools extension if prompted.
2. Run `CMake: Select Configure Preset` and choose `windows-x64`.
3. Run `CMake: Select Launch Target` and choose `RhythmReplugged`.
4. Build with CMake Tools.
5. Start debugging with `Debug CMake Target (Windows/MSVC)`.

### Linux VS Code Quick Start

1. Install the CMake Tools extension if prompted.
2. Run `CMake: Select Configure Preset` and choose `linux-debug`.
3. Run `CMake: Select Launch Target` and choose `RhythmReplugged`.
4. Build with CMake Tools.
5. Start debugging with `Debug CMake Target (Linux/GDB)`.

### Command line build

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64-debug
```

vcpkg manifest dependencies are restored automatically during configure/build.

## Linux build

The standalone SDL3 desktop host is set up to build on Linux too. The repo includes `linux-debug` and `linux-release` CMake presets that use the `x64-linux` vcpkg triplet.

### Tools

- `cmake`
- `make`
- a C++20 compiler such as `g++`
- `vcpkg`
- OpenGL development files for your distro

Set `VCPKG_ROOT` to your local vcpkg checkout before configuring. The shared CMake presets use that environment variable on both Windows and Linux.

### Command line build

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
```

For an optimized build:

```bash
cmake --preset linux-release
cmake --build --preset linux-release
```

This produces the standalone desktop executable under:

- `build/linux-debug/RhythmReplugged`
- `build/linux-release/RhythmReplugged`

### Fresh-machine notes

The first Linux configure may need a few system packages before `vcpkg` can finish restoring dependencies. The exact package names vary by distro, but typically include:

- OpenGL development headers and libraries
- X11 / Xrandr / Xi / Xinerama / Xcursor development packages
- Wayland development packages on Wayland-first distros
- `pkg-config`

## Cross-Platform Notes

These presets are meant for native builds on each platform:

- use `windows-x64` on Windows
- use `linux-debug` or `linux-release` on Linux

The checked-in presets expect `VCPKG_ROOT` to be defined. If you do not want to set it globally on your machine, put it in an untracked `CMakeUserPresets.json` instead.

The repo is set up so both workflows can live side by side in one checkout, but it does not currently provide a cross-compilation toolchain for producing Windows binaries from Linux or Linux binaries from Windows.

## RetroArch / libretro builds

For RetroArch testing on modern Windows, prefer the `windows-x64` presets. They target 64-bit RetroArch and use the static `x64-windows-static` vcpkg triplet so the libretro core can avoid most third-party sidecar DLL dependencies.

For the lowest-friction core drop, build:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64-release --target platform_libretro
```

This produces:

- `build/windows-x64/Release/rhythmreplugged_libretro.dll`

The current x64 release core builds as a single drop-in DLL without extra third-party sidecar DLLs.
