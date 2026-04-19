#include "core/app/AppCore.h"
#include "platform_sdl3/Sdl3AudioOutput.h"
#include "platform_sdl3/Sdl3FileSystem.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <filesystem>
#include <iostream>
#include <string>

namespace
{
	using namespace rhythmreplugged;

	constexpr int kWindowWidth = 1280;
	constexpr int kWindowHeight = 720;
	constexpr Uint64 kFrameDurationNs = 1000000000ull / kAppFramesPerSecond;

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

	void apply_imgui_style()
	{
		ImGuiStyle &style = ImGui::GetStyle();
		style.WindowRounding = 0.0f;
		style.FrameRounding = 0.0f;
		style.GrabRounding = 0.0f;
		style.ScrollbarRounding = 0.0f;
		style.TabRounding = 0.0f;
		style.FrameBorderSize = 1.0f;
		style.WindowBorderSize = 1.0f;
		style.ItemSpacing = ImVec2(8.0f, 6.0f);
		style.Colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.08f, 0.12f, 1.0f);
		style.Colors[ImGuiCol_Header] = ImVec4(0.16f, 0.21f, 0.30f, 1.0f);
		style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.22f, 0.30f, 0.43f, 1.0f);
		style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.28f, 0.38f, 0.54f, 1.0f);
		style.Colors[ImGuiCol_Button] = ImVec4(0.16f, 0.21f, 0.30f, 1.0f);
		style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.32f, 0.45f, 1.0f);
		style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.30f, 0.40f, 0.56f, 1.0f);
		style.Colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.12f, 0.18f, 1.0f);
	}
}

int main(int argc, char *argv[])
{
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
	{
		std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
		return 1;
	}

	SDL_Window *window = SDL_CreateWindow("Rhythm Replugged - SDL3 Prototype", kWindowWidth, kWindowHeight, 0);
	if (window == nullptr)
	{
		std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
		SDL_Quit();
		return 1;
	}

	SDL_Renderer *renderer = SDL_CreateRenderer(window, nullptr);
	if (renderer == nullptr)
	{
		std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	apply_imgui_style();

	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer))
	{
		std::cerr << "ImGui SDL3 init failed.\n";
		ImGui::DestroyContext();
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	if (!ImGui_ImplSDLRenderer3_Init(renderer))
	{
		std::cerr << "ImGui SDL renderer init failed.\n";
		ImGui_ImplSDL3_Shutdown();
		ImGui::DestroyContext();
		SDL_DestroyRenderer(renderer);
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
		ImGui_ImplSDLRenderer3_Shutdown();
		ImGui_ImplSDL3_Shutdown();
		ImGui::DestroyContext();
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	Sdl3AudioOutput audio_output;
	RetroInputState held_input{};
	SDL_Texture *cover_texture = nullptr;
	std::string loaded_cover_path;

	auto destroy_cover = [&]()
	{
		if (cover_texture != nullptr)
		{
			SDL_DestroyTexture(cover_texture);
			cover_texture = nullptr;
		}
		loaded_cover_path.clear();
	};

	bool running = true;
	Uint64 previous_counter = SDL_GetTicksNS();
	Uint64 retro_time_accumulator = 0;
	while (running)
	{
		const Uint64 current_counter = SDL_GetTicksNS();
		retro_time_accumulator += current_counter - previous_counter;
		previous_counter = current_counter;

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

		ImGui_ImplSDLRenderer3_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();

		if (app.mode() == AppMode::SongBrowser)
		{
			const SongBrowserView &browser = app.song_browser_view();
			ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
			ImGui::SetNextWindowSize(ImVec2(static_cast<float>(kWindowWidth), static_cast<float>(kWindowHeight)), ImGuiCond_Always);
			ImGui::Begin("Song Browser", nullptr,
				ImGuiWindowFlags_NoResize |
				ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoCollapse);

			ImGui::TextUnformatted("Song Browser");
			ImGui::Separator();
			ImGui::TextWrapped("Enter/Space: open or play   Esc/Backspace: back   Up/Down: move");
			ImGui::TextWrapped("Root: %s", browser.root_path.c_str());
			ImGui::TextWrapped("Path: %s", browser.current_path.c_str());

			ImGui::BeginChild("browser_list", ImVec2(720.0f, 0.0f), true);
			for (int index = 0; index < static_cast<int>(browser.entries.size()); ++index)
			{
				const SongListItem &entry = browser.entries[index];
				std::string label = entry.label;
				if (entry.is_parent)
					label = "..";
				else if (entry.is_folder)
					label = "[DIR] " + label;
				else if (!entry.is_valid_song)
					label = "[X] " + label;

				const bool selected = index == browser.selected_index;
				if (ImGui::Selectable(label.c_str(), selected))
					app.set_browser_selected_index(index);

				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					app.activate_browser_selection();

				if (!entry.subtitle.empty())
				{
					ImGui::Indent();
					ImGui::TextDisabled("%s", entry.subtitle.c_str());
					ImGui::Unindent();
				}
				else if (!entry.error_message.empty())
				{
					ImGui::Indent();
					ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "%s", entry.error_message.c_str());
					ImGui::Unindent();
				}
			}
			ImGui::EndChild();

			ImGui::SameLine();

			ImGui::BeginGroup();
			ImGui::BeginChild("selection_preview", ImVec2(0.0f, 520.0f), true);
			ImGui::TextUnformatted("Selection");
			ImGui::Separator();

			const SongListItem *selected_entry = nullptr;
			if (!browser.entries.empty() &&
				browser.selected_index >= 0 &&
				browser.selected_index < static_cast<int>(browser.entries.size()))
			{
				selected_entry = &browser.entries[browser.selected_index];
			}

			if (selected_entry != nullptr)
			{
				if (selected_entry->cover_art_path != loaded_cover_path)
				{
					destroy_cover();
					if (!selected_entry->cover_art_path.empty())
					{
						cover_texture = IMG_LoadTexture(renderer, selected_entry->cover_art_path.c_str());
						if (cover_texture != nullptr)
							loaded_cover_path = selected_entry->cover_art_path;
					}
				}

				if (cover_texture != nullptr)
				{
					ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(cover_texture)), ImVec2(256.0f, 256.0f));
				}

				ImGui::TextWrapped("%s", selected_entry->label.c_str());
				if (!selected_entry->subtitle.empty())
					ImGui::TextDisabled("%s", selected_entry->subtitle.c_str());
				if (!selected_entry->error_message.empty())
					ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "%s", selected_entry->error_message.c_str());
			}

			ImGui::EndChild();

			if (ImGui::Button("Open / Play", ImVec2(180.0f, 0.0f)))
				app.activate_browser_selection();

			if (!browser.status_message.empty())
			{
				ImGui::Spacing();
				ImGui::TextWrapped("%s", browser.status_message.c_str());
			}

			ImGui::EndGroup();
			ImGui::End();
		}
		else
		{
			destroy_cover();
			const PrototypePlayerView player = app.prototype_player_view();
			ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
			ImGui::SetNextWindowSize(ImVec2(static_cast<float>(kWindowWidth), static_cast<float>(kWindowHeight)), ImGuiCond_Always);
			ImGui::Begin("Multitrack Prototype", nullptr,
				ImGuiWindowFlags_NoResize |
				ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoCollapse);

			ImGui::TextUnformatted("Multitrack Prototype");
			ImGui::Separator();
			ImGui::TextWrapped("%s", player.song_title.c_str());
			if (!player.song_artist.empty())
				ImGui::TextDisabled("%s", player.song_artist.c_str());

			ImGui::Spacing();
			if (player.has_guitar &&
				ImGui::Button(player.guitar_muted ? "Unmute guitar.ogg" : "Mute guitar.ogg", ImVec2(220.0f, 0.0f)))
				app.toggle_player_guitar_mute();

			if (player.has_guitar)
				ImGui::SameLine();
			if (ImGui::Button("Back to browser", ImVec2(180.0f, 0.0f)))
				app.return_to_browser();

			ImGui::Spacing();
			ImGui::Text("Loaded stems: %d", static_cast<int>(player.loaded_stem_count));
			if (player.has_guitar)
				ImGui::TextUnformatted(player.guitar_muted ? "guitar.ogg: OFF" : "guitar.ogg: ON");

			if (!player.status_message.empty())
			{
				ImGui::Spacing();
				ImGui::TextWrapped("%s", player.status_message.c_str());
			}

			ImGui::End();
		}

		ImGui::Render();
		SDL_SetRenderDrawColor(renderer, 12, 14, 20, 255);
		SDL_RenderClear(renderer);
		ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
		SDL_RenderPresent(renderer);

		const Uint64 frame_end_counter = SDL_GetTicksNS();
		if (frame_end_counter > current_counter && frame_end_counter - current_counter < kFrameDurationNs)
		{
			const Uint64 remaining_ns = kFrameDurationNs - (frame_end_counter - current_counter);
			SDL_DelayPrecise(remaining_ns);
		}
	}

	destroy_cover();
	audio_output.shutdown();
	ImGui_ImplSDLRenderer3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
