#include "core/app/AppCore.h"
#include "core/app/AppLaunch.h"
#include "frontend_contract/FrontendOptions.h"
#include "platform_sdl3/MiniaudioOutput.h"
#include "platform_sdl3/FileSystem.h"
#include "render_gl/GameplayRendererGl.h"
#include "ui/AppUi.h"
#include "ui/AppUiHost.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>

#include <array>
#include <cfloat>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>

namespace
{
	using namespace rhythmreplugged::core;
	using namespace rhythmreplugged::frontend_contract;
	using namespace rhythmreplugged::render_gl;
	using namespace rhythmreplugged::ui;
	using namespace rhythmreplugged::platform_sdl3;

	constexpr int kWindowWidth = 1280;
	constexpr int kWindowHeight = 720;
	constexpr Uint64 kFrameDurationNs = 1000000000ull / kAppFramesPerSecond;
#if defined(RR_RENDER_OPENGL_ES3)
	constexpr char kOpenGlGlslVersion[] = "#version 300 es";
#else
	constexpr char kOpenGlGlslVersion[] = "#version 330 core";
#endif

	struct SdlLaunchArguments
	{
		std::string songs_root_path;
		std::string content_root_path;
		std::string content_path;
		FrontendOptions frontend_options;
		std::string error_message;
	};

	struct SdlFrontendOptionsState
	{
		FrontendOptions persisted_options;
		FrontendOptions runtime_options;
		std::string config_path;
		std::string status_message;
		FrontendOptionsUiState ui_state;
		bool menu_open = false;
	};

	struct SdlGraphicsConfiguration
	{
		GameplayRendererGl::GraphicsApi graphics_api = GameplayRendererGl::GraphicsApi::OpenGl33Core;
		int context_major_version = 3;
		int context_minor_version = 3;
		SDL_GLProfile context_profile = SDL_GL_CONTEXT_PROFILE_CORE;
		const char *graphics_label = "OpenGL 3.3 Core";
	};

	SdlGraphicsConfiguration sdl_graphics_configuration()
	{
#if defined(RR_RENDER_OPENGL_ES3)
		return {
			GameplayRendererGl::GraphicsApi::OpenGlEs3,
			3,
			0,
			SDL_GL_CONTEXT_PROFILE_ES,
			"OpenGL ES 3.0"
		};
#else
		return {
			GameplayRendererGl::GraphicsApi::OpenGl33Core,
			3,
			3,
			SDL_GL_CONTEXT_PROFILE_CORE,
			"OpenGL 3.3 Core"
		};
#endif
	}

	bool is_menu_navigation_scancode(SDL_Scancode scancode)
	{
		switch (scancode)
		{
		case SDL_SCANCODE_UP:
		case SDL_SCANCODE_DOWN:
		case SDL_SCANCODE_LEFT:
		case SDL_SCANCODE_RIGHT:
		case SDL_SCANCODE_RETURN:
		case SDL_SCANCODE_SPACE:
		case SDL_SCANCODE_BACKSPACE:
		case SDL_SCANCODE_0:
		case SDL_SCANCODE_X:
		case SDL_SCANCODE_Y:
		case SDL_SCANCODE_LEFTBRACKET:
		case SDL_SCANCODE_RIGHTBRACKET:
			return true;
		default:
			return scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z;
		}
	}

	bool has_mouse_buttons_down(const std::array<bool, 3> &mouse_buttons_down)
	{
		for (const bool button_down : mouse_buttons_down)
		{
			if (button_down)
				return true;
		}

		return false;
	}

	void suppress_imgui_mouse(const std::array<bool, 3> &mouse_buttons_down)
	{
		ImGuiIO &io = ImGui::GetIO();
		io.MouseDrawCursor = false;
		io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
		for (int button_index = 0; button_index < static_cast<int>(mouse_buttons_down.size()); ++button_index)
			io.AddMouseButtonEvent(button_index, false);
	}

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

	std::string frontend_options_config_path()
	{
		char *pref_path = SDL_GetPrefPath("RhythmReplugged", "RhythmReplugged");
		if (pref_path == nullptr)
			return {};

		std::filesystem::path config_path(pref_path);
		SDL_free(pref_path);
		config_path /= "retroarch-core-options.cfg";
		return config_path.generic_string();
	}

	bool load_frontend_options_config(const std::string &path, FrontendOptions &options, std::string &status_message)
	{
		if (path.empty())
		{
			status_message = "SDL preference path unavailable. Options will not persist.";
			return false;
		}

		std::ifstream stream(std::filesystem::path(path), std::ios::binary);
		if (!stream)
		{
			status_message.clear();
			return false;
		}

		const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
		const bool parsed_all_values = parse_frontend_options_config(text, options);
		status_message = parsed_all_values
			? std::string("Loaded RetroArch-style core options file.")
			: std::string("Loaded core options file with some unrecognized entries.");
		return true;
	}

	bool save_frontend_options_config(const std::string &path, const FrontendOptions &options, std::string &status_message)
	{
		if (path.empty())
		{
			status_message = "SDL preference path unavailable. Changes are session-only.";
			return false;
		}

		std::error_code error_code;
		const std::filesystem::path config_path(path);
		std::filesystem::create_directories(config_path.parent_path(), error_code);
		if (error_code)
		{
			status_message = "Could not create the SDL config directory.";
			return false;
		}

		std::ofstream stream(config_path, std::ios::binary | std::ios::trunc);
		if (!stream)
		{
			status_message = "Could not write the SDL core options config file.";
			return false;
		}

		stream << serialize_frontend_options_config(options);
		if (!stream.good())
		{
			status_message = "Failed while writing the SDL core options config file.";
			return false;
		}

		status_message = "Saved RetroArch-style core options file.";
		return true;
	}

	void apply_frontend_option_change(
		SdlFrontendOptionsState &state,
		AppCore &app,
		const FrontendOptionDefinition &definition,
		std::string_view value)
	{
		if (!set_frontend_option_value(state.persisted_options, definition.id, value))
			return;

		if (definition.apply_timing != FrontendOptionApplyTiming::NextLaunch)
			copy_frontend_option_value(state.runtime_options, state.persisted_options, definition.id);

		app.set_frontend_options(state.runtime_options);
		save_frontend_options_config(state.config_path, state.persisted_options, state.status_message);
	}

	SdlLaunchArguments parse_launch_arguments(int argc, char *argv[], const FrontendOptions &initial_options)
	{
		SdlLaunchArguments arguments;
		arguments.frontend_options = initial_options;
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

			if (!argument.empty() && argument[0] == '-')
			{
				const auto *definition =
					::rhythmreplugged::frontend_contract::find_frontend_option_by_command_line_flag(argument);
				if (definition != nullptr)
				{
					if (index + 1 >= argc)
					{
						arguments.error_message = "Missing value for " + argument + ".";
						return arguments;
					}

					const std::string value = argv[++index] != nullptr ? argv[index] : "";
					if (!::rhythmreplugged::frontend_contract::set_frontend_option_value(
						arguments.frontend_options,
						definition->id,
						value))
					{
						arguments.error_message = "Invalid value '" + value + "' for " + argument + ".";
						return arguments;
					}

					continue;
				}
			}

			if (!argument.empty() && argument[0] != '-' && arguments.content_path.empty())
				arguments.content_path = argument;
		}

		return arguments;
	}
}

int main(int argc, char *argv[])
{
	const SdlGraphicsConfiguration graphics_configuration = sdl_graphics_configuration();

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD))
	{
		std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
		return 1;
	}

	if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, graphics_configuration.context_major_version) ||
		!SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, graphics_configuration.context_minor_version) ||
		!SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, graphics_configuration.context_profile) ||
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
		std::cerr << "SDL_GL_CreateContext failed for " << graphics_configuration.graphics_label << ": " << SDL_GetError() << "\n";
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
		std::cerr << "ImGui OpenGL init failed for " << graphics_configuration.graphics_label << ".\n";
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
	SdlFrontendOptionsState frontend_options_state;
	frontend_options_state.config_path = frontend_options_config_path();
	const bool loaded_frontend_options = load_frontend_options_config(
		frontend_options_state.config_path,
		frontend_options_state.persisted_options,
		frontend_options_state.status_message);
	if (!loaded_frontend_options && !frontend_options_state.config_path.empty())
		save_frontend_options_config(
			frontend_options_state.config_path,
			frontend_options_state.persisted_options,
			frontend_options_state.status_message);
	frontend_options_state.runtime_options = frontend_options_state.persisted_options;
	const SdlLaunchArguments launch_arguments = parse_launch_arguments(argc, argv, frontend_options_state.runtime_options);
	if (!launch_arguments.error_message.empty())
	{
		std::cerr << launch_arguments.error_message << "\n";
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplSDL3_Shutdown();
		ImGui::DestroyContext();
		SDL_GL_DestroyContext(gl_context);
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}
	frontend_options_state.persisted_options = launch_arguments.frontend_options;
	frontend_options_state.runtime_options = launch_arguments.frontend_options;
	AppLaunchInputs launch_inputs;
	launch_inputs.songs_root_path = launch_arguments.songs_root_path;
	launch_inputs.content_root_path = launch_arguments.content_root_path;
	launch_inputs.content_path = launch_arguments.content_path;
	launch_inputs.fallback_songs_root_path = find_songs_root(argc > 0 ? argv[0] : nullptr);
	launch_inputs.frontend_options = launch_arguments.frontend_options;
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
	OpenGlCoverTextures cover_textures(file_system);
	GameplayRendererGl gameplay_renderer;
	gameplay_renderer.set_graphics_api(graphics_configuration.graphics_api);
	std::string gameplay_renderer_error;
	if (!gameplay_renderer.initialize(gameplay_renderer_error))
	{
		std::cerr << "Gameplay renderer init failed for " << graphics_configuration.graphics_label << ": " << gameplay_renderer_error << "\n";
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
		float mouse_wheel_x = 0.0f;
		float mouse_wheel_y = 0.0f;
		bool imgui_mouse_activity_this_frame = false;
		bool imgui_nav_activity_this_frame = false;
		static std::array<bool, 3> imgui_mouse_buttons_down{};

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
				case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
				case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
				case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
				case SDL_GAMEPAD_BUTTON_SOUTH:
				case SDL_GAMEPAD_BUTTON_EAST:
				case SDL_GAMEPAD_BUTTON_WEST:
				case SDL_GAMEPAD_BUTTON_NORTH:
				case SDL_GAMEPAD_BUTTON_BACK:
				case SDL_GAMEPAD_BUTTON_START:
				case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
				case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
					imgui_nav_activity_this_frame = true;
					break;
				default:
					break;
				}

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
				if (is_menu_navigation_scancode(scancode))
					imgui_nav_activity_this_frame = true;
				if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && scancode == SDL_SCANCODE_ESCAPE)
				{
					frontend_options_state.menu_open = !frontend_options_state.menu_open;
					continue;
				}
				if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z)
					held_input.letter_keys[static_cast<size_t>(scancode - SDL_SCANCODE_A)] = is_down;

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

			}
			else if (event.type == SDL_EVENT_MOUSE_MOTION)
			{
				imgui_mouse_activity_this_frame = true;
			}
			else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP)
			{
				imgui_mouse_activity_this_frame = true;
				const bool is_down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
				if (event.button.button >= 1 && event.button.button <= 3)
					imgui_mouse_buttons_down[static_cast<size_t>(event.button.button - 1)] = is_down;
			}
			else if (event.type == SDL_EVENT_MOUSE_WHEEL)
			{
				imgui_mouse_activity_this_frame = true;
				mouse_wheel_x += event.wheel.x;
				mouse_wheel_y += event.wheel.y;
			}
		}

		const bool use_imgui_mouse = !imgui_nav_activity_this_frame &&
			(imgui_mouse_activity_this_frame || has_mouse_buttons_down(imgui_mouse_buttons_down));

		size_t retro_steps = 0;
		while (!frontend_options_state.menu_open && retro_time_accumulator >= kFrameDurationNs && retro_steps < 4)
		{
			RetroInputState frame_input = held_input;
			frame_input.mouse_wheel_x = mouse_wheel_x;
			frame_input.mouse_wheel_y = mouse_wheel_y;
			app.retro_run(frame_input);
			mouse_wheel_x = 0.0f;
			mouse_wheel_y = 0.0f;
			retro_time_accumulator -= kFrameDurationNs;
			++retro_steps;
		}
		if (frontend_options_state.menu_open)
			retro_time_accumulator = 0;

		if (!frontend_options_state.menu_open && app.mode() == AppMode::Gameplay)
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
		if (!use_imgui_mouse)
			suppress_imgui_mouse(imgui_mouse_buttons_down);
		else
			ImGui::GetIO().MouseDrawCursor = true;
		ImGui::NewFrame();

		gameplay_renderer.render(app.gameplay_snapshot().scene, drawable_width, drawable_height);

		render_app_ui(
			app,
			ImVec2(static_cast<float>(drawable_width), static_cast<float>(drawable_height)),
			kDefaultUiScale,
			cover_textures);
		if (frontend_options_state.menu_open)
		{
			FrontendOptionsUiActions actions;
			actions.set_option_value = [&](const FrontendOptionDefinition &definition, std::string_view value)
			{
				apply_frontend_option_change(frontend_options_state, app, definition, value);
				return true;
			};
			render_frontend_options_ui(
				frontend_options_state.persisted_options,
				frontend_options_state.ui_state,
				actions,
				ImVec2(static_cast<float>(drawable_width), static_cast<float>(drawable_height)),
				kDefaultUiScale,
				frontend_options_state.config_path.c_str(),
				frontend_options_state.status_message.c_str());
		}

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
