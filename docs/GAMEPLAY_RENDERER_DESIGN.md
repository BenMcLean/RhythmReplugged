# Gameplay Renderer Design

## Goal

Add a shared OpenGL gameplay renderer that can draw:

- a proper slanted 3D note highway
- multiple instruments on screen at once
- split-screen couch co-op
- future netplay views without changing the rendering architecture

The renderer must work with both current frontends:

- `platform_sdl3`
- `platform_libretro`

Dear ImGui stays in the project for menus and overlays, but it stops being responsible for drawing the gameplay highway itself.

## Architectural Fit

This design follows the existing project split:

- `core` stays authoritative about gameplay and timing
- `ui` stays responsible for menus and lightweight overlays
- hosts stay responsible for OpenGL context ownership and frame lifecycle
- a new shared layer owns gameplay-specific OpenGL drawing

Suggested new layer:

- `src/render_gl`

This layer depends on shared app/core view types and on OpenGL, but not on SDL or `libretro.h`.

## Why a Shared Renderer Layer

The gameplay renderer is neither pure UI nor pure platform glue.

It should not live in `platform_sdl3` because the libretro frontend needs the same visuals.
It should not live in `platform_libretro` for the same reason.
It should not live in `core` because the core must remain graphics-library-agnostic.
It should not live in the existing Dear ImGui `ui` code because gameplay geometry is no longer an ImGui draw-list problem.

That leaves a shared rendering layer as the cleanest home.

## Graphics Target

Target a conservative OpenGL feature set that is easy for both standalone OpenGL and libretro OpenGL frontends to satisfy.

Baseline target:

- OpenGL 3.0 core style rendering
- GLSL 130-style shaders
- vertex buffers
- index buffers
- vertex array objects
- alpha blending
- depth testing
- scissor testing
- standard 2D textures

Allowed later if useful, but not required for the first milestone:

- framebuffer objects for post-process or render-to-texture HUD composition
- hardware instancing as an optimization

Do not require:

- geometry shaders
- tessellation shaders
- compute shaders
- SSBOs
- advanced buffer streaming features
- dynamic lighting

## Visual Model

The gameplay renderer should be built around unlit stylized rendering.

Primary visual ingredients:

- lane colors
- note colors
- sustain tails
- hit line
- beat and measure lines
- distance fade
- simple additive glows if desired later
- textured or gradient highway surfaces if desired later

Dynamic lighting is not part of the baseline plan.

## First Milestone

The first vertical slice should render exactly one player viewport containing:

- one 5-lane guitar highway
- note gems
- sustain tails
- measure lines
- a hit line
- a YARG-inspired default camera preset

Everything else can remain as-is for now.

## Proposed Data Model

The existing `PrototypePlayerView` is enough for the current 2D ImGui highway, but not enough for multi-lane 3D gameplay composition.

The renderer-facing data should become explicitly scene-shaped.

For Rock Band Unplugged style play, multiple instrument lanes live in the same 3D world for a given player.
That means the camera is not owned by each instrument lane.
Instead:

- one player has one viewport
- one player viewport has one camera
- that camera looks at one shared 3D highway world
- that world contains one or more instrument lanes

Suggested shared structs:

```cpp
struct Color4
{
	float r = 1.0f;
	float g = 1.0f;
	float b = 1.0f;
	float a = 1.0f;
};

struct RectF
{
	float x = 0.0f;
	float y = 0.0f;
	float width = 0.0f;
	float height = 0.0f;
};

enum class HighwayInstrumentType
{
	FiveFretGuitar,
	FiveLaneDrums,
	Bass,
	Vocals,
};

struct HighwayCameraView
{
	float field_of_view_degrees = 55.0f;
	float pitch_degrees = 17.0f;
	float camera_height = 2.0f;
	float camera_distance = 1.25f;
	float visible_depth_seconds = 1.5f;
	float curve_amount = 0.0f;
};

struct HighwayStyleView
{
	Color4 lane_colors[5];
	Color4 lane_border_color;
	Color4 hit_line_color;
	Color4 sustain_color;
	Color4 measure_line_color;
	Color4 beat_line_color;
	Color4 background_top_color;
	Color4 background_bottom_color;
	float lane_gap = 0.02f;
	float note_width = 0.82f;
	float note_height = 0.18f;
	float sustain_width = 0.22f;
};

struct HighwayNoteView
{
	int lane = 0;
	float start_offset_seconds = 0.0f;
	float length_seconds = 0.0f;
	bool is_accent = false;
	bool is_star_power = false;
};

struct HighwayMeasureLineView
{
	float offset_seconds = 0.0f;
	bool is_measure = false;
	bool is_strong = false;
};

struct InstrumentLaneView
{
	HighwayInstrumentType instrument_type = HighwayInstrumentType::FiveFretGuitar;
	std::string instrument_label;
	bool is_active = true;
	bool is_muted = false;
	bool has_chart = false;
	float lane_center_x = 0.0f;
	float lane_width = 1.0f;
	float lane_depth_offset = 0.0f;
	std::array<bool, 5> lane_held{};
	std::array<bool, 5> lane_sustaining{};
	std::vector<HighwayNoteView> visible_notes;
	std::vector<HighwayMeasureLineView> visible_measure_lines;
};

struct HighwayWorldView
{
	HighwayStyleView style;
	std::vector<InstrumentLaneView> lanes;
	int focused_lane_index = 0;
	float focus_blend = 1.0f;
};

struct PlayerHudView
{
	std::string player_label;
	std::string status_message;
	double song_time_seconds = 0.0;
	bool failed = false;
};

struct PlayerGameplayView
{
	RectF normalized_rect;
	HighwayCameraView camera;
	HighwayWorldView world;
	PlayerHudView hud;
};

struct GameplaySceneView
{
	Color4 clear_color;
	std::vector<PlayerGameplayView> players;
};
```

## Ownership Rules

The shared scene data should be produced by the app/core side, but it must remain renderer-neutral.

Rules:

- `core` may produce gameplay scene/view structs
- `core` must not mention GL concepts like shaders, buffers, textures, or draw calls
- `render_gl` may consume those structs and turn them into OpenGL drawing
- `ui` may still render overlays and menus from the same app state

## Where the Structs Should Live

Suggested location:

- `src/core/app/AppTypes.h`

That keeps gameplay scene structs in the same place as the existing browser/player view types.

If the file grows too large later, split into:

- `src/core/app/MenuViewTypes.h`
- `src/core/app/GameplayViewTypes.h`

This keeps the first implementation simple and avoids paying naming and file-splitting costs too early.

## Renderer API

Suggested shared renderer API:

```cpp
class GameplayRendererGl
{
public:
	bool initialize(std::string &error_message);
	void shutdown();

	void on_context_lost();
	void on_context_restored(std::string &error_message);

	void render(const GameplaySceneView &scene, int framebuffer_width, int framebuffer_height);
};
```

Key rules:

- renderer owns GPU resources
- renderer does not own the OpenGL context
- hosts call `initialize()` only when a valid GL context exists
- libretro context reset/destroy hooks forward into renderer context handlers

## Internal Renderer Responsibilities

`render_gl` should own:

- shader program creation
- static highway mesh generation
- note quad or note-mesh buffers
- sustain quad generation
- camera and projection math
- viewport/scissor setup
- draw ordering

It should not own:

- input polling
- app state updates
- filesystem access
- audio device behavior
- frontend swap/present behavior

## Suggested File Layout

```text
src/render_gl/
  GameplayRendererGl.h
  GameplayRendererGl.cpp
  GlShaderProgram.h
  GlShaderProgram.cpp
  HighwayMesh.h
  HighwayMesh.cpp
  HighwayMath.h
  HighwayMath.cpp
```

Possible helper split later:

- `NoteMesh.cpp`
- `ViewportLayout.cpp`
- `HighwayStyleDefaults.cpp`

## Coordinate Conventions

Use a right-handed world with:

- `X+` = right
- `Y+` = up
- `Z+` = toward the camera

This means the camera looks down the negative Z axis in the usual OpenGL style.

First-pass world anchors:

- the hit line is at world `z = 0`
- notes farther in the future appear at more negative Z values
- lane spacing is expressed in world-space X coordinates

## Viewport Coordinates

Viewport rectangles are normalized screen rectangles with a top-left origin.

That means:

- `x` is measured from the left edge of the full framebuffer
- `y` is measured from the top edge of the full framebuffer
- `width` and `height` are fractions of the full framebuffer size

All values are in the `0.0 .. 1.0` range.

Examples:

- full screen: `{0, 0, 1, 1}`
- left half: `{0, 0, 0.5, 1}`
- right half: `{0.5, 0, 0.5, 1}`
- top half: `{0, 0, 1, 0.5}`
- bottom half: `{0, 0.5, 1, 0.5}`

The renderer converts these normalized rectangles into OpenGL viewport and scissor rectangles per frame.

## Camera Semantics

The camera fields should be interpreted literally and consistently.

For the first implementation:

- `field_of_view_degrees` is the vertical field of view
- `pitch_degrees` is the downward tilt applied to the camera
- `camera_height` is the camera Y position in world space
- `camera_distance` is the camera Z position in world space relative to the hit line
- `visible_depth_seconds` is how far ahead in time notes are considered visible

Recommended first-pass camera placement:

- camera position = `{0, camera_height, camera_distance}`
- camera looks toward the highway and hit line

Because the hit line lives at `z = 0` and future notes move toward more negative Z, the camera should begin on the positive-Z side of the world looking inward.

## Lane Layout Semantics

Lane layout values are stored in world-space units rather than normalized sub-rect coordinates.

That means:

- `lane_center_x` is the horizontal center of an instrument lane block in world space
- `lane_width` is the total width of that instrument lane block in world units
- `lane_depth_offset` is an optional world-space Z offset for that instrument lane block

This keeps lane placement straightforward for:

- one-lane single-player scenes
- multi-lane Unplugged-style shared worlds
- split-screen layouts where each player still has an independent world

Example four-lane shared-world centers:

- `-4.5`
- `-1.5`
- `1.5`
- `4.5`

Exact values are tunable and do not need to be finalized before the first renderer pass.

## Frame Flow

### SDL3 Host

Suggested gameplay frame flow:

1. Poll SDL input
2. Advance `AppCore`
3. Build or fetch `GameplaySceneView`
4. Begin a new frame for gameplay rendering
5. Render gameplay scene through `GameplayRendererGl`
6. Begin Dear ImGui frame
7. Render overlays or menus through existing `ui`
8. Render Dear ImGui draw data
9. Swap window

Important note:

The gameplay renderer should draw before Dear ImGui, so ImGui can remain a clean overlay layer.

### libretro Host

Suggested gameplay frame flow:

1. Poll libretro input
2. Advance `AppCore`
3. Build or fetch `GameplaySceneView`
4. Bind the current libretro framebuffer
5. Render gameplay scene through `GameplayRendererGl`
6. Begin Dear ImGui frame
7. Render overlays or menus through existing `ui`
8. Render Dear ImGui draw data
9. Submit `RETRO_HW_FRAME_BUFFER_VALID`

## Scene Composition Model

The renderer should think in terms of players and viewports first, then a shared 3D highway world inside each player view.

That gives a natural path for:

- single-player guitar
- Rock Band Unplugged style multiple simultaneous instruments in one player view
- couch co-op split-screen with one or more instruments per player

Recommended composition:

- `GameplaySceneView` contains `players`
- each `PlayerGameplayView` owns one screen rectangle and one camera
- each player owns one `HighwayWorldView`
- each `HighwayWorldView` contains one or more `InstrumentLaneView`s

This keeps split-screen and multi-instrument gameplay orthogonal.

Examples:

- single-player guitar: one player view, one lane
- Unplugged-style three-track focus: one player view, three lanes in one world
- couch co-op: two player views, one lane each
- future band scene: two player views, two lanes in each world

## Viewport Layout

The host or app layer may decide viewport rectangles, but the renderer should consume them as data instead of inventing policy internally.

That means `normalized_rect` is part of `PlayerViewportView`.

Examples:

- full screen: `{0, 0, 1, 1}`
- vertical split left player: `{0, 0, 0.5, 1}`
- vertical split right player: `{0.5, 0, 0.5, 1}`
- top/bottom split: `{0, 0, 1, 0.5}` and `{0, 0.5, 1, 0.5}`

## Camera Presets

The renderer should not hardcode a single camera.

Use a view struct carrying camera parameters inspired by YARG's preset approach:

- field of view
- pitch
- camera height
- camera distance
- visible depth
- curve amount

Suggested first preset:

- `field_of_view_degrees = 55`
- `pitch_degrees = 17`
- `camera_height = 2.0`
- `camera_distance = 1.25`
- `visible_depth_seconds = 1.5`
- `curve_amount = 0.0`

These should be treated as a tunable preset, not as engine constants.

## Camera Focus Model

For Unplugged-style gameplay, lane switching should move focus within a player's shared world rather than swap to a completely separate instrument camera.

That means:

- camera is per-player
- lanes are per-world
- lane switching updates focus state inside the world or player view

The first version can keep this simple:

- `focused_lane_index`
- `focus_blend`

Later this can grow into:

- camera target X
- zoom/FOV blending
- lane emphasis values
- transition timing curves

## World Conventions

Keep world conventions simple and stable:

- highway forward direction is positive depth away from the hit line
- hit line is at local depth `0`
- note depth is derived from `offset_seconds / visible_depth_seconds`
- lanes are evenly spaced in local X
- Y is vertical screen/world height used for camera placement

The renderer can map timing offsets into local depth without the core needing to know any world-space values.

## Initial Draw Strategy

First-pass draw order per viewport:

1. clear or draw viewport background
2. draw highway surface
3. draw lane separators
4. draw beat and measure lines
5. draw sustain tails
6. draw note gems
7. draw hit line
8. hand off to ImGui overlay pass

This is sufficient for a convincing first rhythm-game presentation.

## Mesh Strategy

Do not start with a complicated fully procedural mesh system.

Initial approach:

- one static highway mesh for a straight five-lane board
- one dynamic CPU-built vertex buffer for note quads
- one dynamic CPU-built vertex buffer for sustain quads
- optional line or thin-quad geometry for beat lines

This is easy to debug and plenty fast for the expected note counts.

## Shader Strategy

First pass should need only a small shader set:

- unlit color shader for highway, notes, sustains, lines
- optional textured unlit shader later for highway skins

If possible, start with one shader that supports:

- vertex color
- distance fade
- simple UV passthrough even if unused initially

## Overlay Strategy

Dear ImGui stays useful for:

- song browser
- pause menu
- loading or error dialogs
- debug graphs
- temporary HUD labels during development

During gameplay:

- `render_gl` draws the 3D highway scene
- `ui` draws 2D HUD, menus, and debug overlay through Dear ImGui on top

That lets the project transition gradually without throwing away existing UI work.

## Integration Sequence

Implement in this order:

1. Add gameplay scene structs
2. Add `render_gl` target and wire it into CMake
3. Implement one straight highway renderer with one viewport
4. Feed current `PrototypePlayerView` data into one `GameplaySceneView`
5. Render gameplay first, then ImGui
6. Expand scene generation to multiple instruments
7. Expand layout generation to multiple player viewports
8. Add polish such as textures, curve, glow, and presets

## Migration Path from `PrototypePlayerView`

Short-term:

- keep `PrototypePlayerView` alive
- derive `GameplaySceneView` from it inside `AppCore` or host glue

Medium-term:

- move gameplay-specific HUD and note presentation toward `GameplaySceneView`
- reduce `PrototypePlayerView` to compatibility/debug use or replace it entirely

This reduces risk and avoids a large all-at-once refactor.

## Questions to Settle Later

These do not block the first milestone:

- whether overlays should remain in `ui` or partially move into `render_gl`
- how much of camera/style preset selection belongs in app settings versus renderer defaults
- whether curved highways are mesh-deformed on CPU or shader-deformed on GPU
- whether note gems stay simple quads or become small meshes
- whether vocals share the same renderer path or use a different scene type

## Non-Goals for the First Milestone

Do not block the renderer on these:

- dynamic lighting
- stage characters
- post-processing
- particle systems
- material system complexity
- perfect parity with Unity-based reference projects

The initial goal is a solid shared gameplay rendering architecture, not maximum visual completeness.
