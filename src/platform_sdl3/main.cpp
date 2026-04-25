#include "core/app/AppCore.h"
#include "core/app/AppLaunch.h"
#include "platform_sdl3/MiniaudioOutput.h"
#include "platform_sdl3/FileSystem.h"
#include "render_gl/GameplayRendererGl.h"
#include "ui/AppUiHost.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_map>

namespace
{
	using namespace rhythmreplugged::core;
	using namespace rhythmreplugged::render_gl;
	using namespace rhythmreplugged::ui;
	using namespace rhythmreplugged::platform_sdl3;

	constexpr int kWindowWidth = 1280;
	constexpr int kWindowHeight = 720;
	constexpr Uint64 kFrameDurationNs = 1000000000ull / kAppFramesPerSecond;
	constexpr char kOpenGlGlslVersion[] = "#version 130";

	struct SdlLaunchArguments
	{
		std::string songs_root_path;
		std::string content_root_path;
		std::string content_path;
	};

	std::string find_songs_root(const char *argv0)
	{
		std::error_code error_code;
		std::filesystem::path base_path = std::filesystem::current_path(error_code);
		if (argv0 != nullptr && argv0[0] != '\0')
		{
			const std::filesystem::path executable_path = std::filesystem::absolute(argv0, error_code);
			if (!error_code)
				base_path = executable_path.parent_path();
		}

		std::filesystem::path probe_path = base_path;
		while (!probe_path.empty())
		{
			const std::filesystem::path candidate = probe_path / "songs";
			if (std::filesystem::exists(candidate, error_code))
				return candidate.generic_string();

			const std::filesystem::path parent = probe_path.parent_path();
			if (parent == probe_path)
				break;

			probe_path = parent;
		}

		return {};
	}

	SdlLaunchArguments parse_launch_arguments(int argc, char *argv[])
	{
		SdlLaunchArguments arguments;
		for (int index = 1; index < argc; ++index)
		{
			const std::string argument = argv[index] != nullptr ? argv[index] : "";
			if (argument == "--songs-root" && index + 1 < argc)
			{
				arguments.songs_root_path = argv[++index] != nullptr ? argv[index] : "";
				continue;
			}

			if (argument == "--content-root" && index + 1 < argc)
			{
				arguments.content_root_path = argv[++index] != nullptr ? argv[index] : "";
				continue;
			}

			if (argument == "--content" && index + 1 < argc)
			{
				arguments.content_path = argv[++index] != nullptr ? argv[index] : "";
				continue;
			}

			if (!argument.empty() && argument[0] != '-' && arguments.content_path.empty())
				arguments.content_path = argument;
		}

		return arguments;
	}
}

int main(int argc, char *argv[])
{
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD))
	{
		std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
		return 1;
	}

	if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3) ||
		!SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0) ||
		!SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) ||
		!SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1) ||
		!SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24) ||
		!SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8))
	{
		std::cerr << "SDL_GL_SetAttribute failed: " << SDL_GetError() << "\n";
		SDL_Quit();
		return 1;
	}

	SDL_Window *window = SDL_CreateWindow(
		"Rhythm Replugged - SDL3 Prototype",
		kWindowWidth,
		kWindowHeight,
		SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
	if (window == nullptr)
	{
		std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
		SDL_Quit();
		return 1;
	}

	SDL_GLContext gl_context = SDL_GL_CreateContext(window);
	if (gl_context == nullptr)
	{
		std::cerr << "SDL_GL_CreateContext failed: " << SDL_GetError() << "\n";
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	if (!SDL_GL_MakeCurrent(window, gl_context))
	{
		std::cerr << "SDL_GL_MakeCurrent failed: " << SDL_GetError() << "\n";
		SDL_GL_DestroyContext(gl_context);
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	if (!SDL_GL_SetSwapInterval(1))
	{
		std::cerr << "SDL_GL_SetSwapInterval failed: " << SDL_GetError() << "\n";
		SDL_GL_DestroyContext(gl_context);
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	initialize_app_imgui(kDefaultUiScale);

	if (!ImGui_ImplSDL3_InitForOpenGL(window, gl_context))
	{
		std::cerr << "ImGui SDL3 init failed.\n";
		ImGui::DestroyContext();
		SDL_GL_DestroyContext(gl_context);
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	if (!ImGui_ImplOpenGL3_Init(kOpenGlGlslVersion))
	{
		std::cerr << "ImGui OpenGL init failed.\n";
		ImGui_ImplSDL3_Shutdown();
		ImGui::DestroyContext();
		SDL_GL_DestroyContext(gl_context);
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	FileSystem file_system;
	AppCore app(file_system);
	std::string init_error;
	const SdlLaunchArguments launch_arguments = parse_launch_arguments(argc, argv);
	AppLaunchInputs launch_inputs;
	launch_inputs.songs_root_path = launch_arguments.songs_root_path;
	launch_inputs.content_root_path = launch_arguments.content_root_path;
	launch_inputs.content_path = launch_arguments.content_path;
	launch_inputs.fallback_songs_root_path = find_songs_root(argc > 0 ? argv[0] : nullptr);
	const AppLaunchRequest launch_request = resolve_app_launch_request(file_system, launch_inputs);
	if (!app.retro_init(launch_request, init_error))
	{
		std::cerr << "Failed to initialize app: " << init_error << "\n";
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplSDL3_Shutdown();
		ImGui::DestroyContext();
		SDL_GL_DestroyContext(gl_context);
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	MiniaudioOutput audio_output;
	::rhythmreplugged::frontend_contract::RetroInputState held_input{};
	std::unordered_map<SDL_JoystickID, SDL_Gamepad *> open_gamepads;
	OpenGlCoverTextures cover_textures;
	GameplayRendererGl gameplay_renderer;
	std::string gameplay_renderer_error;
	if (!gameplay_renderer.initialize(gameplay_renderer_error))
	{
		std::cerr << "Gameplay renderer init failed: " << gameplay_renderer_error << "\n";
		cover_textures.clear();
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplSDL3_Shutdown();
		ImGui::DestroyContext();
		SDL_GL_DestroyContext(gl_context);
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	bool running = true;
	Uint64 previous_counter = SDL_GetTicksNS();
	Uint64 retro_time_accumulator = 0;
	auto open_gamepad = [&](SDL_JoystickID instance_id)
	{
		if (open_gamepads.contains(instance_id))
			return;

		SDL_Gamepad *gamepad = SDL_OpenGamepad(instance_id);
		if (gamepad != nullptr)
			open_gamepads.emplace(instance_id, gamepad);
	};
	auto close_gamepad = [&](SDL_JoystickID instance_id)
	{
		const auto it = open_gamepads.find(instance_id);
		if (it == open_gamepads.end())
			return;

		SDL_CloseGamepad(it->second);
		open_gamepads.erase(it);
	};

	int gamepad_count = 0;
	SDL_JoystickID *gamepad_ids = SDL_GetGamepads(&gamepad_count);
	if (gamepad_ids != nullptr)
	{
		for (int index = 0; index < gamepad_count; ++index)
			open_gamepad(gamepad_ids[index]);
		SDL_free(gamepad_ids);
	}

	while (running)
	{
		const Uint64 current_counter = SDL_GetTicksNS();
		retro_time_accumulator += current_counter - previous_counter;
		previous_counter = current_counter;
		int drawable_width = kWindowWidth;
		int drawable_height = kWindowHeight;
		SDL_GetWindowSizeInPixels(window, &drawable_width, &drawable_height);

		SDL_Event event{};
		while (SDL_PollEvent(&event))
		{
			ImGui_ImplSDL3_ProcessEvent(&event);

			if (event.type == SDL_EVENT_QUIT)
			{
				running = false;
			}
			else if (event.type == SDL_EVENT_GAMEPAD_ADDED)
			{
				open_gamepad(event.gdevice.which);
			}
			else if (event.type == SDL_EVENT_GAMEPAD_REMOVED)
			{
				close_gamepad(event.gdevice.which);
			}
			else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type == SDL_EVENT_GAMEPAD_BUTTON_UP)
			{
				const bool is_down = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
				switch (static_cast<SDL_GamepadButton>(event.gbutton.button))
				{
				case SDL_GAMEPAD_BUTTON_DPAD_UP:
					held_input.up = is_down;
					break;
				case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
					held_input.down = is_down;
					break;
				case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
					held_input.left = is_down;
					break;
				case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
					held_input.right = is_down;
					break;
				case SDL_GAMEPAD_BUTTON_SOUTH:
					held_input.a = is_down;
					break;
				case SDL_GAMEPAD_BUTTON_EAST:
					held_input.b = is_down;
					break;
				case SDL_GAMEPAD_BUTTON_WEST:
					held_input.x = is_down;
					break;
				case SDL_GAMEPAD_BUTTON_NORTH:
					held_input.y = is_down;
					break;
				case SDL_GAMEPAD_BUTTON_BACK:
					held_input.select = is_down;
					break;
				case SDL_GAMEPAD_BUTTON_START:
					held_input.start = is_down;
					break;
				case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
					held_input.l = is_down;
					break;
				case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
					held_input.r = is_down;
					break;
				default:
					break;
				}
			}
			else if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
			{
				const bool is_down = event.type == SDL_EVENT_KEY_DOWN;
				const SDL_Scancode scancode = event.key.scancode;
				if (scancode == SDL_SCANCODE_UP)
					held_input.up = is_down;
				else if (scancode == SDL_SCANCODE_DOWN)
					held_input.down = is_down;
				else if (scancode == SDL_SCANCODE_LEFT)
					held_input.left = is_down;
				else if (scancode == SDL_SCANCODE_RIGHT)
					held_input.right = is_down;
				else if (scancode == SDL_SCANCODE_RETURN || scancode == SDL_SCANCODE_SPACE)
					held_input.a = is_down;
				else if (scancode == SDL_SCANCODE_BACKSPACE || scancode == SDL_SCANCODE_0)
					held_input.b = is_down;
				else if (scancode == SDL_SCANCODE_X)
					held_input.x = is_down;
				else if (scancode == SDL_SCANCODE_Y)
					held_input.y = is_down;
				else if (scancode == SDL_SCANCODE_LEFTBRACKET)
					held_input.l = is_down;
				else if (scancode == SDL_SCANCODE_RIGHTBRACKET)
					held_input.r = is_down;
				else if (scancode == SDL_SCANCODE_1)
					held_input.lane_1 = is_down;
				else if (scancode == SDL_SCANCODE_2)
					held_input.lane_2 = is_down;
				else if (scancode == SDL_SCANCODE_3)
					held_input.lane_3 = is_down;
				else if (scancode == SDL_SCANCODE_4)
					held_input.lane_4 = is_down;
				else if (scancode == SDL_SCANCODE_5)
					held_input.lane_5 = is_down;

				if (is_down && app.mode() == AppMode::PrototypePlayer)
				{
					if (scancode == SDL_SCANCODE_LEFTBRACKET)
						app.nudge_timing_offset_seconds(-0.005);
					else if (scancode == SDL_SCANCODE_RIGHTBRACKET)
						app.nudge_timing_offset_seconds(0.005);
					else if (scancode == SDL_SCANCODE_BACKSLASH)
						app.reset_timing_offset();
				}
			}
		}

		size_t retro_steps = 0;
		while (retro_time_accumulator >= kFrameDurationNs && retro_steps < 4)
		{
			app.retro_run(held_input);
			retro_time_accumulator -= kFrameDurationNs;
			++retro_steps;
		}

		if (app.mode() == AppMode::PrototypePlayer)
		{
			audio_output.set_stream(&app);
			audio_output.initialize(&app);
		}
		else
		{
			audio_output.shutdown();
			app.finalize_audio_stop();
		}

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();

		gameplay_renderer.render(app.gameplay_scene_view(), drawable_width, drawable_height);

		render_app_ui(
			app,
			ImVec2(static_cast<float>(drawable_width), static_cast<float>(drawable_height)),
			kDefaultUiScale,
			cover_textures);

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		SDL_GL_SwapWindow(window);

		const Uint64 frame_end_counter = SDL_GetTicksNS();
		if (frame_end_counter > current_counter && frame_end_counter - current_counter < kFrameDurationNs)
		{
			const Uint64 remaining_ns = kFrameDurationNs - (frame_end_counter - current_counter);
			SDL_DelayPrecise(remaining_ns);
		}
	}

	for (const auto &[instance_id, gamepad] : open_gamepads)
		SDL_CloseGamepad(gamepad);

	gameplay_renderer.shutdown();
	cover_textures.clear();
	audio_output.shutdown();
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();
	SDL_GL_DestroyContext(gl_context);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
