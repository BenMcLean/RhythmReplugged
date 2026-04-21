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

## What worked for me on Windows

### Tools

- Visual Studio 2022 or Build Tools for Visual Studio 2022 with the C++ workload
- CMake
- vcpkg
- VS Code with the CMake Tools extension

### Build in VS Code

1. Install the CMake Tools extension if prompted.
2. Let CMake configure on open.
3. Build with the CMake Tools status bar button, or use the `windows-x64-debug` preset.
4. If the launch target needs to be selected manually, choose `RhythmReplugged`.
5. Press the CMake Tools run button to launch it.

### Command line build

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64-debug
```

vcpkg manifest dependencies are restored automatically during configure/build.

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

### First RetroArch Test

1. Use a 64-bit RetroArch build.
2. In RetroArch, load `build/windows-x64/Release/rhythmreplugged_libretro.dll` as a core.
3. Use `Load Content`, not just `Start Core`, for the first real test path.
4. Point RetroArch at either:
   - a song directory containing `song.ini`, or
   - a file inside such a song directory
5. Make sure the song contains:
   - `song.ini`
   - one supported chart file: `notes.mid`, `notes.midi`, `notes.chart`, or `notes.txt`
   - at least one supported `.ogg` stem such as `song.ogg`, `guitar.ogg`, `bass.ogg`, `drums.ogg`, etc.

Current basic controls:

- `Up` / `Down` browse
- `A` or `Start` open/play
- `B` return to the browser
