# Rhythm Replugged

*Rhythm Replugged* is an open source music game designed to run as a cross-platform [libretro core](https://www.libretro.com/) to play existing [YARG](https://github.com/YARC-Official/YARG) format songs unmodified. It is focused on gamepad controls for casual players first (not on specialized music controllers) to provide an easy setup experience, especially on SBCs like the Raspberry Pi 4 or on handhelds like the Steam Deck.

The game is planned to have at least three modes of play. Gameplay modes are an individual player choice, so a band could be assembled with a mix of different players in different modes all playing together:

* **Replugged Mode** is inspired by the multi-instrument juggling gameplay of *Rock Band Unplugged* for the PSP. Players claim more than one instrument and are required to periodically switch instruments to keep them all "locked."
* **Freeplay Mode** players claim multiple instruments, but are not required to switch.
* **Classic Mode** players claim only one instrument and get a typical *Guitar Hero* style experience.

## Technical Info

*Rhythm Replugged* is a C++ game project built with CMake and vcpkg manifest dependencies.

The code under `src/` is currently split into:

- `src/frontend_contract` for host-facing filesystem, input, and audio interfaces shared by both frontends
- `src/core` for launch flow, song browsing, metadata parsing, chart loading, preload orchestration, gameplay state, and audio mixing
- `src/ui` for Dear ImGui menus, overlays, and OpenGL cover-art texture management
- `src/render_gl` for the shared GLES3-compatible gameplay highway renderer used by both hosts
- `src/platform_sdl3` for the standalone SDL3 host, OpenGL context, gamepad polling, and miniaudio output
- `src/platform_libretro` for the libretro host, frontend integration, and OpenGL-backed core rendering

### Host Boundaries

The current build is organized around a strict host split:

- `src/core` must not depend on SDL or `libretro.h`.
- `src/ui` must not depend on SDL or `libretro.h`.
- `src/render_gl` may depend on GLES3-compatible OpenGL calls and core view types, but not on SDL or `libretro.h`.
- `src/platform_sdl3` is the standalone desktop host and may depend on SDL.
- `src/platform_libretro` is the libretro host and may depend on `libretro.h`.

Rendering code is written to the OpenGL ES 3.0 feature set. The SDL desktop host uses an OpenGL 3.3 Core context as a compatibility backend. Desktop libretro builds ask RetroArch for OpenGL ES 3.0 first and fall back to OpenGL 3.3 Core, while ARM/SBC libretro builds use an actual OpenGL ES 3.0 context only.

For Dear ImGui specifically:

- `platform_sdl3` builds the SDL3 platform backend and the OpenGL renderer backend for OpenGL 3.3 Core.
- `platform_libretro` builds only the OpenGL renderer backend and uses project-owned libretro platform glue.

This keeps the libretro core free of SDL link dependencies while still allowing both hosts to share the same menu UI and gameplay renderer.

### Runtime Shape

The current app flow is:

1. launch into the song browser using either an explicit songs root, frontend-provided content path, or a discovered local `songs/` folder
2. select a song, then choose instrument and difficulty from dedicated menu screens
3. preload stems and chart data on a background worker while the UI reports progress
4. transition into gameplay, where `core` produces both menu-facing state and a renderer-neutral `GameplaySceneView`
5. draw gameplay through `render_gl` and draw menus or overlays through `ui`

The important ownership split is:

- `core::AppCore` owns app state, menu state, preload state, gameplay state, and audio generation
- `ui::render_app_ui(...)` owns Dear ImGui presentation for menus and overlays
- `render_gl::GameplayRendererGl` owns the slanted highway scene rendering
- each host owns input polling, filesystem implementation, frame pacing, and final audio/video handoff

### Native Windows Build

#### Tools

- Visual Studio 2022 or Build Tools for Visual Studio 2022 with the C++ workload
- CMake
- vcpkg
- VS Code with the CMake Tools extension

Set `VCPKG_ROOT` to your local vcpkg checkout before configuring. That is the path the shared CMake presets use on both Windows and Linux.

#### Build in VS Code

1. Install the CMake Tools extension if prompted.
2. Let CMake configure on open.
3. Select the `windows-x64` configure preset.
4. Build with the CMake Tools status bar button, or use the `windows-x64-debug` build preset.
5. If the launch target needs to be selected manually, choose `RhythmReplugged`.
6. Use the `Debug CMake Target (Windows/MSVC)` launch configuration to debug on Windows.

#### Keeping Windows And Linux Side By Side

This repo keeps both desktop workflows available at the same time:

- Windows configure presets: `windows-x64`
- Linux configure presets: `linux-debug`, `linux-release`
- Windows VS Code debugger: `Debug CMake Target (Windows/MSVC)`
- Linux VS Code debugger: `Debug CMake Target (Linux/GDB)`

Choose the preset and debugger that match the machine you are currently on. The same workspace can support both without removing either path.

#### Windows VS Code Quick Start

1. Install the CMake Tools extension if prompted.
2. Run `CMake: Select Configure Preset` and choose `windows-x64`.
3. Run `CMake: Select Launch Target` and choose `RhythmReplugged`.
4. Build with CMake Tools.
5. Start debugging with `Debug CMake Target (Windows/MSVC)`.

#### Linux VS Code Quick Start

1. Install the CMake Tools extension if prompted.
2. Make sure `VCPKG_ROOT` points at your local vcpkg checkout before VS Code configures the project.
3. Run `CMake: Select Configure Preset` and choose `linux-debug`.
4. Run `CMake: Select Launch Target` and choose `RhythmReplugged`.
5. Build with CMake Tools.
6. Start debugging with `Debug CMake Target (Linux/GDB)`.

If CMake Tools fails with a toolchain path like `/scripts/buildsystems/vcpkg.cmake`, VS Code did not inherit `VCPKG_ROOT` even if your interactive shell has it. In that case, create an untracked `CMakeUserPresets.json` that inherits from `linux-debug` or `linux-release` and sets `environment.VCPKG_ROOT` explicitly for the local machine.

#### Command line build

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64-debug
```

vcpkg manifest dependencies are restored automatically during configure/build.

### Linux Build

The standalone SDL3 desktop host is set up to build on Linux too. The repo includes `linux-debug` and `linux-release` CMake presets that use the `x64-linux` vcpkg triplet.

#### Tools

- `cmake`
- `ninja` or `make`
- a C++20 compiler such as `g++`
- `pkg-config`
- `vcpkg`
- OpenGL development files for your distro

Set `VCPKG_ROOT` to your local vcpkg checkout before configuring. The shared CMake presets use that environment variable on both Windows and Linux.

#### Command line build

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

It also stages the final desktop artifact under:

- `dist/Debug/desktop/` for debug builds
- `dist/Release/desktop/` for release builds

The SDL3 host also accepts a few startup flags:

- `--content <path>` to open a specific song or content path
- `--songs-root <path>` to choose the song browser root
- `--content-root <path>` to resolve relative content paths
- `--instrument <ask|guitar|bass|rhythm|coop-guitar|keys>` to pick the startup instrument preference
- `--difficulty <ask|easy|medium|hard|expert>` to pick the startup difficulty preference

#### Fresh-machine notes

The first Linux configure may need a few system packages before `vcpkg` can finish restoring dependencies. The exact package names vary by distro, but typically include:

- OpenGL development headers and libraries
- X11 / Xrandr / Xi / Xinerama / Xcursor development packages
- Wayland development packages on Wayland-first distros
- `pkg-config`
- `ninja-build` if you want to use the checked-in Linux presets as written

### Cross-Platform Notes

These presets are meant for native builds on each platform:

- use `windows-x64` on Windows
- use `linux-debug` or `linux-release` on Linux

The checked-in presets expect `VCPKG_ROOT` to be defined. If you do not want to set it globally on your machine, put it in an untracked `CMakeUserPresets.json` instead. That is also the safest fallback if VS Code configures with a broken toolchain path such as `/scripts/buildsystems/vcpkg.cmake`.

The repo is set up so both workflows can live side by side in one checkout, but it does not currently provide a cross-compilation toolchain for producing Windows binaries from Linux or Linux binaries from Windows.

### RetroArch / libretro builds

For RetroArch testing on modern Windows, prefer the `windows-x64` presets. They target 64-bit RetroArch and use the static `x64-windows-static` vcpkg triplet so the libretro core can avoid most third-party sidecar DLL dependencies.

For the lowest-friction core drop, build:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64-release --target platform_libretro
```

This produces:

- `build/windows-x64/Release/rhythmreplugged.dll`

It also stages the libretro drop under:

- `dist/Release/libretro/rhythmreplugged.dll`
- `dist/Release/libretro/rhythmreplugged.info`

The current x64 release core builds as a single drop-in DLL without extra third-party sidecar DLLs. It is compiled against the desktop OpenGL loader, requests OpenGL ES 3 first at runtime, and falls back to OpenGL 3.3 Core if the frontend rejects GLES.

### Raspberry Pi / ARM64 libretro builds

The project also includes native Linux ARM64 libretro presets for Raspberry Pi 4/5 class systems running a 64-bit OS. These presets are libretro-only, disable the SDL3 desktop frontend, and compile the renderer against OpenGL ES 3 headers.

On the target ARM64 machine, set `VCPKG_ROOT` and build:

```bash
cmake --preset linux-arm64-libretro-release
cmake --build --preset linux-arm64-libretro-release --target stage_libretro
```

This produces and stages:

- `build/linux-arm64-libretro-release/rhythmreplugged.so`
- `dist/Release/libretro/rhythmreplugged.so`
- `dist/Release/libretro/rhythmreplugged.info`

The ARM64 presets assume a 64-bit Linux userland and the vcpkg `arm64-linux` triplet. A 32-bit RetroPie image may need a separate `arm-linux` triplet or a custom vcpkg triplet/toolchain file.

For ARM/SBC targets, OpenGL ES 3 is the expected path and the ARM64 libretro presets are GLES-only. Desktop x64 libretro builds are more flexible: they try GLES3 first, then OpenGL 3.3 Core, while renderer features stay inside the OpenGL ES 3.0 feature set.
