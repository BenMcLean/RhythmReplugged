# Rhythm Replugged Architecture

## Goals

- Keep the gameplay and song browser logic independent from SDL, native filesystem APIs, and OS-specific services.
- Shape the top-level lifecycle around libretro concepts early so a future libretro frontend remains practical.
- Preserve a standalone host with tighter control over rendering and low-latency audio than a libretro frontend can guarantee.
- Keep the current prototype scope small: folder-based song selection plus a multitrack playback prototype for `song.ogg` and `guitar.ogg`.

## Top-Level Split

The project is divided into three layers:

1. `libretro_contract`
   Project-owned interfaces and data types that keep the core compatible with a future libretro host without pretending every abstraction is the libretro ABI.

2. `core`
   Game logic, song browser rules, `song.ini` parsing, and prototype audio decoding/mixing. This layer must not know about SDL, miniaudio, native dialogs, or direct file I/O.

3. `platform_sdl3`
   Standalone host implementation. This layer owns window creation, rendering, real input polling, and audio output.

Later, a fourth layer can be added:

4. `platform_libretro`
   Thin glue between the same `core` layer and the actual libretro ABI.

## Dependency Direction

- `core` depends on `libretro_contract`
- `platform_sdl3` depends on `core` and `libretro_contract`
- `platform_libretro` depends on `core` and `libretro_contract`

No dependency may point back upward.

## Core vs Host Boundary

The separating principle is:

- Core decides what the program means.
- Host decides how the outside world is accessed.

Core responsibilities:

- Song folder classification and navigation rules
- `song.ini` parsing and metadata extraction
- Validation of songs for the current prototype
- App mode transitions between song browser and prototype playback
- Audio decoding and logical mixing decisions
- Producing host-facing view data and audio batches

Host responsibilities:

- Enumerating directories and reading files
- Polling keyboard/gamepad input
- Rendering text and simple visuals
- Feeding audio to the platform device
- Owning the main loop and wall-clock pacing

## Why Not Use Raw libretro Internally

The architecture is constrained by libretro capabilities, but the code does not directly expose the raw libretro C ABI everywhere.

Instead:

- top-level lifecycle names stay close to libretro (`retro_init`, `retro_run`, `retro_deinit`)
- host services are project-owned C++ interfaces
- gameplay and menu systems stay domain-named

This keeps the code readable while still preventing SDL-specific assumptions from leaking into the core.

## Filesystem Rule

The core must not use:

- `std::filesystem`
- `std::ifstream`
- native path enumeration

All content access goes through the contract filesystem interface. This is required to keep a future libretro host viable.

## Audio Rule

The core owns:

- decoding prototype stems from bytes
- mixing `song.ogg` and `guitar.ogg`
- mute/unmute decisions
- generating stereo PCM batches

The host owns:

- platform audio device setup
- buffering and scheduling samples to the device

This means:

- standalone host may use `miniaudio` for low-latency playback
- libretro host may use libretro audio callbacks
- the core remains unchanged

The standalone host is the reference low-latency path. A future libretro host is supported architecturally, but the final latency there will depend on the frontend.

## Current Module Plan

### `libretro_contract`

- `RetroFileSystem.h`
- `RetroInput.h`
- `AudioTypes.h`

This layer defines generic host-facing abstractions for:

- file and directory entry abstractions
- controller-like input state
- audio batch structs

### `core/app`

- `AppTypes.h`

This layer defines:

- app mode enums
- browser and playback view structs for the standalone/core app flow

### `core`

- `AppCore`
- `SongBrowser`
- `SongIni`
- `PrototypePlayer`
- shared domain structs

This layer uses only contract interfaces and portable libraries.

### `platform_sdl3`

- SDL host and main loop
- Dear ImGui rendering for the standalone prototype UI
- miniaudio-backed callback output

Dear ImGui is treated as a renderer choice for the SDL host, not as part of the core contract.

## Internal Core Split

Inside the core, menu and gameplay stay separate subsystems:

- `SongBrowser`
  Folder navigation, metadata display state, selection, and error messages.

- `PrototypePlayer`
  Decoded backing track and guitar track playback, plus mute toggling.

- `AppCore`
  High-level coordinator that switches between browser mode and playback mode.

This is not a second platform boundary. It is just a clean separation of responsibilities inside the core.

## Prototype Scope

The initial rebuilt prototype supports:

- selecting a root song folder
- browsing folders with these rules:
  - `..` omitted at root
  - non-song folders listed first
  - folders containing `song.ini` treated as terminal song entries
  - invalid songs shown with an error state
- selecting a valid song enters playback mode
- playback mode decodes and mixes `song.ogg` plus `guitar.ogg`
- pressing the toggle input mutes/unmutes `guitar.ogg`
- backing out returns to the song browser

## Deferred Work

Not part of this refactor:

- actual chart gameplay and note timing
- libretro export layer
- native root folder picker
- Dear ImGui integration
- generalized stem loading beyond the prototype pair

Those can be layered on top of this structure afterward.
