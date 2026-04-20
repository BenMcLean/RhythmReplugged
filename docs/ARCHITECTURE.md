# Rhythm Replugged Architecture

## Goals

- Keep the gameplay and song browser logic independent from SDL, native filesystem APIs, and OS-specific services.
- Keep presentation code separate from both gameplay rules and platform glue so the UI can change without rewriting the core.
- Shape the top-level lifecycle around libretro concepts early so a future libretro frontend remains practical.
- Preserve a standalone host with tighter control over rendering and low-latency audio than a libretro frontend can guarantee.
- Keep the current prototype scope small: folder-based song selection plus a multitrack playback prototype for `song.ogg` and `guitar.ogg`.

## Top-Level Split

The source tree under `src/` is divided into four layers:

1. `libretro_contract`
   Project-owned interfaces and data types that keep the core compatible with a future libretro host without pretending every abstraction is the libretro ABI.

2. `core`
   Game logic, song browser rules, `song.ini` parsing, and prototype audio decoding/mixing. This layer must not know about SDL, miniaudio, native dialogs, or direct file I/O.

3. `ui`
   Presentation code that turns core-owned view structs into visible screens. This layer may use Dear ImGui today, but it does not own input polling, filesystem access, or audio devices.

4. `platform_sdl3`
   Standalone host implementation. This layer owns window creation, input polling, image loading, and audio output, then calls into `ui` to draw the current frame.

Later, a fifth layer can be added:

5. `platform_libretro`
   Thin glue between the same `core` layer and the actual libretro ABI.

## Dependency Direction

- `core` depends on `libretro_contract`
- `ui` depends on `core`
- `platform_sdl3` depends on `core`, `ui`, and `libretro_contract`
- `platform_libretro` depends on `core` and `libretro_contract`

No dependency may point back upward.

## Physical Layout

These layer names are architectural names and also correspond to the current on-disk layout:

- `src/libretro_contract`
- `src/core`
- `src/ui`
- `src/platform_sdl3`

## Core vs UI vs Host Boundary

The separating principle is:

- Core decides what the program means.
- UI decides how core state is presented to the player.
- Host decides how the outside world is accessed.

Core responsibilities:

- Song folder classification and navigation rules
- `song.ini` parsing and metadata extraction
- Validation of songs for the current prototype
- App mode transitions between song browser and prototype playback
- Audio decoding and logical mixing decisions
- Producing host-facing view data and audio batches

UI responsibilities:

- Rendering the song browser and playback screens from core-owned view structs
- Applying presentation-only styling, layout, colors, and visual affordances
- Translating UI interactions into calls back into host-owned actions

Host responsibilities:

- Enumerating directories and reading files
- Polling keyboard/gamepad input
- Creating the render context and frame lifecycle
- Loading textures and other platform-managed visual resources
- Feeding audio to the platform device
- Owning the main loop and wall-clock pacing

The current SDL host uses Dear ImGui, but that choice lives in the `ui` layer rather than in the core.

## Why Not Use Raw libretro Internally

The architecture is constrained by libretro capabilities, but the code does not directly expose the raw libretro C ABI everywhere.

Instead:

- top-level lifecycle names stay close to libretro (`retro_init`, `retro_run`, `retro_deinit`)
- host services are project-owned C++ interfaces
- gameplay and menu systems stay domain-named

This keeps the code readable while still preventing SDL-specific assumptions from leaking into the core.

Separating `ui` from `platform_sdl3` also prevents rendering code from becoming the place where gameplay state starts to leak outward. The host should provide capabilities; the UI should arrange visuals; the core should stay authoritative about behavior.

## Filesystem Rule

The core must not use:

- `std::filesystem`
- `std::ifstream`
- native path enumeration

All content access goes through the contract filesystem interface. This is required to keep a future libretro host viable.

## UI Rule

The UI must not own:

- song discovery rules
- playback state transitions
- chart timing logic
- direct platform polling or audio device management

The UI may:

- inspect immutable core view structs
- call callbacks supplied by the host
- use rendering libraries such as Dear ImGui to present those views

This keeps presentation code replaceable without changing gameplay code.

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
- browser and playback view structs consumed by the UI layer

### `core`

- `AppCore`
- `SongBrowser`
- `SongIni`
- `PrototypePlayer`
- shared domain structs

This layer uses only contract interfaces and portable libraries.

### `ui`

- `AppUi`

This layer defines:

- Dear ImGui styling
- song browser rendering from `SongBrowserView`
- prototype playback rendering from `PrototypePlayerView`
- callback-shaped action hooks that let the host wire user interactions back into the core

### `platform_sdl3`

- SDL host and main loop
- Dear ImGui setup and frame orchestration
- platform texture loading for cover art
- miniaudio-backed callback output

Dear ImGui is treated as a renderer choice for the current UI implementation, not as part of the core contract.

## Internal Core Split

Inside the core, menu and gameplay stay separate subsystems:

- `SongBrowser`
  Folder navigation, metadata display state, selection, and error messages.

- `PrototypePlayer`
  Decoded backing track and guitar track playback, plus mute toggling.

- `AppCore`
  High-level coordinator that switches between browser mode and playback mode.

This is not a second platform boundary. It is just a clean separation of responsibilities inside the core.

## UI Flow

The current standalone flow is:

1. `platform_sdl3` polls input, enumerates files, and owns audio output.
2. `core` updates app state and produces view structs such as `SongBrowserView` and `PrototypePlayerView`.
3. `platform_sdl3` passes those views plus action callbacks into `ui`.
4. `ui` renders the frame and reports interactions back through those callbacks.

This means the UI can stay fairly rich without teaching the core about Dear ImGui, textures, or SDL window state.
