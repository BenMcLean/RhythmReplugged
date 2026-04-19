#include "core/app/AppCore.h"
#include "platform_sdl3/MiniaudioOutput.h"
#include "platform_sdl3/Sdl3FileSystem.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
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
	constexpr float kUiScale = 2.0f;
	constexpr float kListCoverSize = 32.0f * kUiScale;
	constexpr float kPreviewCoverSize = 256.0f * kUiScale;

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
		style.ScaleAllSizes(kUiScale);
	}

	ImTextureRef make_imgui_texture_ref(SDL_Texture *texture)
	{
		return ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(texture)));
	}

	bool load_cover_texture(SDL_Renderer *renderer, const std::string &cover_path, SDL_Texture *&texture)
	{
		texture = IMG_LoadTexture(renderer, cover_path.c_str());
		if (texture != nullptr)
			return true;

		SDL_Surface *surface = IMG_Load(cover_path.c_str());
		if (surface != nullptr)
		{
			texture = SDL_CreateTextureFromSurface(renderer, surface);
			SDL_DestroySurface(surface);
			if (texture != nullptr)
				return true;

			return false;
		}

		const std::string extension = std::filesystem::path(cover_path).extension().string();
		if (_stricmp(extension.c_str(), ".png") == 0)
		{
			surface = SDL_LoadPNG(cover_path.c_str());
			if (surface != nullptr)
			{
				texture = SDL_CreateTextureFromSurface(renderer, surface);
				SDL_DestroySurface(surface);
				if (texture != nullptr)
					return true;

				return false;
			}
		}

		return false;
	}

	void draw_chart_highway(const PrototypePlayerView &player, float width, float height)
	{
		ImGui::BeginChild("chart_highway_panel", ImVec2(width, height), false,
			ImGuiWindowFlags_NoBackground);

		if (!player.has_chart)
		{
			const ImVec2 canvas_size = ImGui::GetContentRegionAvail();
			const ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
			ImDrawList *draw_list = ImGui::GetWindowDrawList();
			draw_list->AddRectFilled(canvas_pos,
				ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
				IM_COL32(11, 14, 19, 255),
				8.0f);
			draw_list->AddText(
				ImVec2(canvas_pos.x + 24.0f, canvas_pos.y + 24.0f),
				IM_COL32(215, 220, 230, 255),
				"No supported 5-fret chart loaded.");
			ImGui::Dummy(canvas_size);
			ImGui::EndChild();
			return;
		}

		const ImVec2 canvas_size = ImGui::GetContentRegionAvail();
		const ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImDrawList *draw_list = ImGui::GetWindowDrawList();

		draw_list->AddRectFilled(canvas_pos,
			ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
			IM_COL32(11, 14, 19, 255),
			8.0f);

		const float lane_padding = 18.0f;
		const float lane_count = 5.0f;
		const float lane_width = (canvas_size.x - lane_padding * 2.0f) / lane_count;
		const float lane_top = canvas_pos.y + 10.0f;
		const float lane_bottom = canvas_pos.y + canvas_size.y - 10.0f;
		const float hit_line_y = lane_bottom - 44.0f;
		const float pixels_per_second = (hit_line_y - lane_top) / 3.0f;
		const ImU32 lane_colors[5] = {
			IM_COL32(90, 197, 92, 255),
			IM_COL32(210, 62, 62, 255),
			IM_COL32(226, 209, 63, 255),
			IM_COL32(65, 117, 220, 255),
			IM_COL32(234, 140, 41, 255),
		};

		for (int lane = 0; lane < 5; ++lane)
		{
			const float lane_left = canvas_pos.x + lane_padding + lane_width * static_cast<float>(lane);
			const float lane_right = lane_left + lane_width;
			const bool is_held = player.lane_held[static_cast<size_t>(lane)];
			const bool is_sustaining = player.lane_sustaining[static_cast<size_t>(lane)];
			draw_list->AddRectFilled(ImVec2(lane_left, lane_top), ImVec2(lane_right, lane_bottom),
				is_held ? IM_COL32(34, 41, 54, 255) : (is_sustaining ? IM_COL32(28, 34, 45, 255) : IM_COL32(22, 27, 35, 255)));
			draw_list->AddLine(ImVec2(lane_right, lane_top), ImVec2(lane_right, lane_bottom),
				IM_COL32(48, 58, 74, 255), 1.0f);
			draw_list->AddCircleFilled(
				ImVec2((lane_left + lane_right) * 0.5f, hit_line_y),
				(std::min)(lane_width * 0.29f, 20.0f),
				is_held || is_sustaining ? lane_colors[lane] : IM_COL32(36, 44, 58, 255));
			draw_list->AddCircle(
				ImVec2((lane_left + lane_right) * 0.5f, hit_line_y),
				(std::min)(lane_width * 0.29f, 20.0f),
				player.guitar_muted ? IM_COL32(230, 92, 92, 220) : (is_sustaining ? IM_COL32(255, 250, 210, 240) : IM_COL32(245, 245, 245, 220)),
				0,
				is_sustaining ? 3.5f : 2.5f);
		}

		draw_list->AddLine(ImVec2(canvas_pos.x + lane_padding, hit_line_y),
			ImVec2(canvas_pos.x + canvas_size.x - lane_padding, hit_line_y),
			IM_COL32(245, 245, 245, 255), 3.0f);

		for (const PrototypePlayerView::ChartMeasureLineView &measure_line : player.visible_measure_lines)
		{
			const float line_y = hit_line_y - measure_line.offset_seconds * pixels_per_second;
			if (line_y < lane_top || line_y > lane_bottom)
				continue;

			draw_list->AddLine(
				ImVec2(canvas_pos.x + lane_padding, line_y),
				ImVec2(canvas_pos.x + canvas_size.x - lane_padding, line_y),
				measure_line.is_measure
					? IM_COL32(235, 240, 250, 220)
					: (measure_line.is_strong ? IM_COL32(170, 185, 205, 170) : IM_COL32(100, 112, 128, 110)),
				measure_line.is_measure ? 3.0f : (measure_line.is_strong ? 2.0f : 1.0f));
		}

		for (const PrototypePlayerView::ChartNoteView &note : player.visible_chart_notes)
		{
			if (note.lane < 0 || note.lane >= 5)
				continue;

			const float lane_left = canvas_pos.x + lane_padding + lane_width * static_cast<float>(note.lane);
			const float lane_right = lane_left + lane_width;
			const float note_center_x = (lane_left + lane_right) * 0.5f;
			const float note_y = hit_line_y - note.start_offset_seconds * pixels_per_second;
			const float sustain_height = note.length_seconds * pixels_per_second;

			if (sustain_height > 6.0f)
			{
				draw_list->AddRectFilled(
					ImVec2(note_center_x - 6.0f, note_y - sustain_height),
					ImVec2(note_center_x + 6.0f, note_y),
					IM_COL32(235, 235, 235, 150),
					4.0f);
			}

			draw_list->AddCircleFilled(ImVec2(note_center_x, note_y),
				(std::min)(lane_width * 0.28f, 18.0f),
				lane_colors[note.lane]);
			draw_list->AddCircle(ImVec2(note_center_x, note_y),
				(std::min)(lane_width * 0.28f, 18.0f),
				IM_COL32(245, 245, 245, 220),
				0,
				2.0f);
		}

		if (player.guitar_muted)
		{
			draw_list->AddRectFilled(canvas_pos,
				ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
				IM_COL32(120, 18, 18, 40),
				8.0f);
		}

		ImGui::Dummy(canvas_size);
		ImGui::EndChild();
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
	ImFontConfig font_config;
	font_config.SizePixels = 13.0f * kUiScale;
	io.FontDefault = io.Fonts->AddFontDefault(&font_config);

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

	MiniaudioOutput audio_output;
	RetroInputState held_input{};
	std::unordered_map<std::string, SDL_Texture *> cover_texture_cache;

	auto destroy_cover_textures = [&]()
	{
		for (auto &[path, texture] : cover_texture_cache)
		{
			if (texture != nullptr)
				SDL_DestroyTexture(texture);
		}
		cover_texture_cache.clear();
	};

	auto get_cover_texture = [&](const std::string &cover_path) -> SDL_Texture *
	{
		if (cover_path.empty())
			return nullptr;

		const auto it = cover_texture_cache.find(cover_path);
		if (it != cover_texture_cache.end())
			return it->second;

		SDL_Texture *texture = nullptr;
		load_cover_texture(renderer, cover_path, texture);

		cover_texture_cache.emplace(cover_path, texture);
		return texture;
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

			const ImVec2 content_region = ImGui::GetContentRegionAvail();
			const float column_spacing = ImGui::GetStyle().ItemSpacing.x;
			const float min_list_width = 420.0f * kUiScale;
			float list_width = content_region.x * 0.52f;
			list_width = (std::min)(list_width, content_region.x - column_spacing - (320.0f * kUiScale));
			list_width = (std::max)(list_width, min_list_width);
			list_width = (std::min)(list_width, content_region.x);

			ImGui::BeginChild("browser_list", ImVec2(list_width, 0.0f), true);
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
				ImGui::PushID(index);
				ImGui::BeginGroup();

				SDL_Texture *row_cover_texture = get_cover_texture(entry.cover_art_path);

				if (row_cover_texture != nullptr)
				{
					ImGui::Image(make_imgui_texture_ref(row_cover_texture), ImVec2(kListCoverSize, kListCoverSize));
					ImGui::SameLine();
				}

				if (ImGui::Selectable(label.c_str(), selected, 0, ImVec2(0.0f, kListCoverSize)))
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

				ImGui::EndGroup();
				ImGui::PopID();
			}
			ImGui::EndChild();

			ImGui::SameLine();

			ImGui::BeginGroup();
			const float action_row_height =
				ImGui::GetFrameHeightWithSpacing() +
				(browser.status_message.empty() ? 0.0f : ImGui::GetTextLineHeightWithSpacing() * 3.0f);
			const float preview_height = (std::max)(220.0f * kUiScale, content_region.y - action_row_height);
			ImGui::BeginChild("selection_preview", ImVec2(0.0f, preview_height), true);
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
				SDL_Texture *preview_cover_texture = get_cover_texture(selected_entry->cover_art_path);
				if (preview_cover_texture != nullptr)
				{
					const float cover_size = (std::min)(kPreviewCoverSize, ImGui::GetContentRegionAvail().x);
					ImGui::Image(make_imgui_texture_ref(preview_cover_texture), ImVec2(cover_size, cover_size));
				}

				ImGui::TextWrapped("%s", selected_entry->label.c_str());
				if (!selected_entry->subtitle.empty())
					ImGui::TextDisabled("%s", selected_entry->subtitle.c_str());
				if (!selected_entry->error_message.empty())
					ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "%s", selected_entry->error_message.c_str());
			}

			ImGui::EndChild();

			if (ImGui::Button("Open / Play", ImVec2(240.0f * kUiScale, 0.0f)))
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
			const PrototypePlayerView player = app.prototype_player_view();
			ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
			ImGui::SetNextWindowSize(ImVec2(static_cast<float>(kWindowWidth), static_cast<float>(kWindowHeight)), ImGuiCond_Always);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
			ImGui::Begin("Multitrack Prototype", nullptr,
				ImGuiWindowFlags_NoResize |
				ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoCollapse |
				ImGuiWindowFlags_NoTitleBar);

			draw_chart_highway(player, ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);

			ImGui::End();
			ImGui::PopStyleVar();
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

	destroy_cover_textures();
	audio_output.shutdown();
	ImGui_ImplSDLRenderer3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
