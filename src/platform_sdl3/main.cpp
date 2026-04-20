#include "core/app/AppCore.h"
#include "platform_sdl3/MiniaudioOutput.h"
#include "platform_sdl3/Sdl3FileSystem.h"
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
	using namespace rhythmreplugged;

	constexpr int kWindowWidth = 1280;
	constexpr int kWindowHeight = 720;
	constexpr Uint64 kFrameDurationNs = 1000000000ull / kAppFramesPerSecond;
	constexpr char kOpenGlGlslVersion[] = "#version 130";

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
}

int main(int argc, char *argv[])
{
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
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

	Sdl3FileSystem file_system;
	AppCore app(file_system);
	std::string init_error;
	const std::string songs_root = find_songs_root(argc > 0 ? argv[0] : nullptr);
	if (songs_root.empty() || !app.retro_init(songs_root, init_error))
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
	RetroInputState held_input{};
	OpenGlCoverTextures cover_textures;

	bool running = true;
	Uint64 previous_counter = SDL_GetTicksNS();
	Uint64 retro_time_accumulator = 0;
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
				else if (scancode == SDL_SCANCODE_ESCAPE || scancode == SDL_SCANCODE_BACKSPACE)
					held_input.b = is_down;
				else if (scancode == SDL_SCANCODE_X)
					held_input.x = is_down;
				else if (scancode == SDL_SCANCODE_Y)
					held_input.y = is_down;
				else if (scancode == SDL_SCANCODE_1)
					held_input.left = is_down;
				else if (scancode == SDL_SCANCODE_2)
					held_input.up = is_down;
				else if (scancode == SDL_SCANCODE_3)
					held_input.y = is_down;
				else if (scancode == SDL_SCANCODE_4)
					held_input.x = is_down;
				else if (scancode == SDL_SCANCODE_5)
					held_input.a = is_down;

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

		render_app_ui(
			app,
			ImVec2(static_cast<float>(drawable_width), static_cast<float>(drawable_height)),
			kDefaultUiScale,
			cover_textures);

		ImGui::Render();
		glViewport(0, 0, drawable_width, drawable_height);
		glClearColor(12.0f / 255.0f, 14.0f / 255.0f, 20.0f / 255.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		SDL_GL_SwapWindow(window);

		const Uint64 frame_end_counter = SDL_GetTicksNS();
		if (frame_end_counter > current_counter && frame_end_counter - current_counter < kFrameDurationNs)
		{
			const Uint64 remaining_ns = kFrameDurationNs - (frame_end_counter - current_counter);
			SDL_DelayPrecise(remaining_ns);
		}
	}

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
