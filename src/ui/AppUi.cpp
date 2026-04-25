#include "ui/AppUi.h"

#include <algorithm>

namespace
{
	using namespace rhythmreplugged::core;
	using namespace rhythmreplugged::ui;

	constexpr ImU32 kLaneColors[5] = {
		IM_COL32(90, 197, 92, 255),
		IM_COL32(210, 62, 62, 255),
		IM_COL32(226, 209, 63, 255),
		IM_COL32(65, 117, 220, 255),
		IM_COL32(234, 140, 41, 255),
	};

	void draw_status_pill(const char *id, const ImVec2 &position, const char *text, ImU32 background_color)
	{
		(void)id;
		ImDrawList *draw_list = ImGui::GetWindowDrawList();
		const ImVec2 text_size = ImGui::CalcTextSize(text);
		const ImVec2 padding(14.0f, 10.0f);
		draw_list->AddRectFilled(
			position,
			ImVec2(position.x + text_size.x + padding.x * 2.0f, position.y + text_size.y + padding.y * 2.0f),
			background_color,
			10.0f);
		draw_list->AddText(
			ImVec2(position.x + padding.x, position.y + padding.y),
			IM_COL32(235, 239, 246, 255),
			text);
	}

	void render_preload_progress_overlay(const DifficultySelectView &menu, ImVec2 window_size, float ui_scale)
	{
		if (menu.preload_phase == PreloadPhase::Idle)
			return;

		const ImVec2 overlay_size(320.0f * ui_scale, 92.0f * ui_scale);
		ImGui::SetNextWindowPos(
			ImVec2(window_size.x - overlay_size.x - 24.0f * ui_scale, window_size.y - overlay_size.y - 24.0f * ui_scale),
			ImGuiCond_Always);
		ImGui::SetNextWindowSize(overlay_size, ImGuiCond_Always);
		ImGui::SetNextWindowFocus();
		ImGui::Begin("Stem Preload Overlay", nullptr,
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoInputs |
			ImGuiWindowFlags_NoNavFocus);

		ImVec4 progress_color = ImVec4(0.28f, 0.72f, 0.32f, 1.0f);
		const char *title = "Song Ready";
		const char *detail = "";
		if (menu.preload_phase == PreloadPhase::Reading)
		{
			title = "Reading Stems";
			detail = "files";
			progress_color = ImVec4(0.92f, 0.78f, 0.20f, 1.0f);
		}
		else if (menu.preload_phase == PreloadPhase::Decoding)
		{
			title = "Decoding Stems";
			detail = "MiB";
		}
		else if (menu.preload_phase == PreloadPhase::Failed)
		{
			title = "Load Failed";
			detail = "";
			progress_color = ImVec4(0.88f, 0.32f, 0.32f, 1.0f);
		}

		ImGui::TextUnformatted(title);
		ImGui::PushStyleColor(ImGuiCol_PlotHistogram, progress_color);
		ImGui::ProgressBar(menu.preload_progress, ImVec2(-1.0f, 0.0f));
		ImGui::PopStyleColor();
		if (menu.preload_phase == PreloadPhase::Reading)
		{
			ImGui::TextDisabled(
				"%zu / %zu files read",
				menu.completed_read_file_count,
				menu.total_read_file_count);
		}
		else
		{
			ImGui::TextDisabled(
				"%zu / %zu stems   %zu / %zu %s",
				menu.completed_stem_count,
				menu.total_stem_count,
				menu.preload_processed_megabytes,
				menu.preload_total_megabytes,
				detail);
		}
		ImGui::End();
	}
}

namespace rhythmreplugged::ui
{
	using namespace rhythmreplugged::core;

	void apply_imgui_style(float ui_scale)
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
		style.ScaleAllSizes(ui_scale);
	}

	void render_song_browser_ui(
		const SongBrowserView &browser,
		const SongBrowserUiActions &actions,
		ImVec2 window_size,
		float ui_scale)
	{
		static std::string last_browser_path;
		static int last_selected_index = -1;

		int pending_selected_index = -1;
		bool pending_activate_selection = false;
		const std::string window_title = browser.current_path.empty()
			? std::string("Song Browser")
			: browser.current_path + "##Song Browser";
		const ImGuiIO &io = ImGui::GetIO();
		const bool browser_changed = browser.current_path != last_browser_path;
		const bool selected_index_changed = browser.selected_index != last_selected_index;
		const bool mouse_scrolling = io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f;
		const bool should_follow_selection = (browser_changed || selected_index_changed) && !mouse_scrolling;

		ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
		ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);
		ImGui::Begin(window_title.c_str(), nullptr,
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoScrollbar |
			ImGuiWindowFlags_NoScrollWithMouse);

		const ImVec2 content_region = ImGui::GetContentRegionAvail();
		const float column_spacing = ImGui::GetStyle().ItemSpacing.x;
		const float list_cover_size = 32.0f * ui_scale;
		const float preview_cover_size = 256.0f * ui_scale;
		const float min_list_width = 420.0f * ui_scale;
		float list_width = content_region.x * 0.52f;
		list_width = (std::min)(list_width, content_region.x - column_spacing - (320.0f * ui_scale));
		list_width = (std::max)(list_width, min_list_width);
		list_width = (std::min)(list_width, content_region.x);

		ImGui::BeginChild("browser_list", ImVec2(list_width, content_region.y), true);
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

			const std::optional<ImTextureRef> row_cover_texture = actions.get_cover_texture_ref != nullptr
				? actions.get_cover_texture_ref(entry.cover_art_path)
				: std::nullopt;

			if (row_cover_texture.has_value())
			{
				ImGui::Image(*row_cover_texture, ImVec2(list_cover_size, list_cover_size));
				ImGui::SameLine();
			}

			ImGui::BeginGroup();
			const float text_start_y = ImGui::GetCursorPosY();
			const bool activated = ImGui::Selectable(label.c_str(), selected, 0, ImVec2(0.0f, 0.0f));
			const bool title_hovered = ImGui::IsItemHovered();
			if (selected && should_follow_selection)
				ImGui::SetScrollHereY(0.5f);
			if (activated &&
				actions.set_selected_index != nullptr)
			{
				pending_selected_index = index;
			}

			if (title_hovered &&
				ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
				actions.activate_selection != nullptr)
			{
				pending_activate_selection = true;
			}

			if (!entry.subtitle.empty())
			{
				ImGui::TextDisabled("%s", entry.subtitle.c_str());
			}
			else if (!entry.error_message.empty())
			{
				ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "%s", entry.error_message.c_str());
			}

			const float text_block_height = ImGui::GetCursorPosY() - text_start_y;
			if (text_block_height < list_cover_size)
			{
				ImGui::Dummy(ImVec2(0.0f, list_cover_size - text_block_height));
			}

			ImGui::EndGroup();
			ImGui::PopID();
		}
		ImGui::EndChild();

		ImGui::SameLine();

		ImGui::BeginGroup();
		const float button_height = ImGui::GetFrameHeight();
		const float button_spacing = ImGui::GetStyle().ItemSpacing.y;
		const float status_height = browser.status_message.empty()
			? 0.0f
			: ImGui::GetTextLineHeightWithSpacing() * 3.0f + button_spacing;
		const float action_row_height = button_height + button_spacing + status_height;
		const float preview_height = (std::max)(0.0f, content_region.y - action_row_height);
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
			const std::optional<ImTextureRef> preview_cover_texture = actions.get_cover_texture_ref != nullptr
				? actions.get_cover_texture_ref(selected_entry->cover_art_path)
				: std::nullopt;
			if (preview_cover_texture.has_value())
			{
				const float cover_size = (std::min)(preview_cover_size, ImGui::GetContentRegionAvail().x);
				ImGui::Image(*preview_cover_texture, ImVec2(cover_size, cover_size));
			}

			ImGui::TextWrapped("%s", selected_entry->label.c_str());
			if (!selected_entry->subtitle.empty())
				ImGui::TextDisabled("%s", selected_entry->subtitle.c_str());
			if (!selected_entry->error_message.empty())
				ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "%s", selected_entry->error_message.c_str());
		}

		ImGui::EndChild();

		if (ImGui::Button("Open / Play", ImVec2(240.0f * ui_scale, 0.0f)) &&
			actions.activate_selection != nullptr)
		{
			pending_activate_selection = true;
		}

		if (!browser.status_message.empty())
		{
			ImGui::Spacing();
			ImGui::TextWrapped("%s", browser.status_message.c_str());
		}

			ImGui::EndGroup();
			ImGui::End();

			if (pending_selected_index >= 0 &&
				actions.set_selected_index != nullptr)
			{
				actions.set_selected_index(pending_selected_index);
			}

			if (pending_activate_selection &&
				actions.activate_selection != nullptr)
			{
				actions.activate_selection();
			}

		last_browser_path = browser.current_path;
		last_selected_index = browser.selected_index;
	}

	void render_difficulty_select_ui(
		const DifficultySelectView &menu,
		const DifficultySelectUiActions &actions,
		ImVec2 window_size,
		float ui_scale)
	{
		int pending_selected_index = -1;
		bool pending_activate_selection = false;

		ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
		ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);
		ImGui::Begin("Difficulty Select", nullptr,
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoCollapse);

		ImGui::TextUnformatted("Select Difficulty");
		ImGui::Separator();
		if (!menu.song_title.empty())
			ImGui::TextWrapped("%s", menu.song_title.c_str());
		if (!menu.song_subtitle.empty())
			ImGui::TextDisabled("%s", menu.song_subtitle.c_str());
		ImGui::Spacing();

		for (int index = 0; index < static_cast<int>(menu.entries.size()); ++index)
		{
			const DifficultyListItem &entry = menu.entries[index];
			const bool selected = index == menu.selected_index;
			if (ImGui::Selectable(entry.label.c_str(), selected, 0, ImVec2(0.0f, 36.0f * ui_scale)) &&
				actions.set_selected_index != nullptr)
			{
				pending_selected_index = index;
			}
		}

		ImGui::Spacing();
		if (ImGui::Button("Start Song", ImVec2(240.0f * ui_scale, 0.0f)) &&
			actions.activate_selection != nullptr)
		{
			pending_activate_selection = true;
		}

		if (!menu.status_message.empty())
		{
			ImGui::Spacing();
			ImGui::TextWrapped("%s", menu.status_message.c_str());
		}

		ImGui::Spacing();
		ImGui::TextDisabled("B: Back    A / Start: Confirm");
		ImGui::End();

		render_preload_progress_overlay(menu, window_size, ui_scale);

		if (pending_selected_index >= 0 &&
			actions.set_selected_index != nullptr)
		{
			actions.set_selected_index(pending_selected_index);
		}

		if (pending_activate_selection &&
			actions.activate_selection != nullptr)
		{
			actions.activate_selection();
		}
	}

	void render_song_loading_ui(
		const DifficultySelectView &menu,
		ImVec2 window_size,
		float ui_scale)
	{
		ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
		ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);
		ImGui::Begin("Song Loading", nullptr,
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoTitleBar);

		const ImVec2 content = ImGui::GetContentRegionAvail();
		ImGui::Dummy(ImVec2(0.0f, content.y * 0.34f));
		ImGui::SetCursorPosX((window_size.x - 420.0f * ui_scale) * 0.5f);
		ImGui::TextUnformatted("Get ready to rock!");
		if (!menu.song_title.empty())
		{
			ImGui::Spacing();
			ImGui::SetCursorPosX((window_size.x - 520.0f * ui_scale) * 0.5f);
			ImGui::TextWrapped("%s", menu.song_title.c_str());
		}
		if (!menu.song_subtitle.empty())
		{
			ImGui::SetCursorPosX((window_size.x - 520.0f * ui_scale) * 0.5f);
			ImGui::TextDisabled("%s", menu.song_subtitle.c_str());
		}
		ImGui::Spacing();
		ImGui::SetCursorPosX((window_size.x - 260.0f * ui_scale) * 0.5f);
		ImGui::TextDisabled("Press B to go back.");
		ImGui::End();

		render_preload_progress_overlay(menu, window_size, ui_scale);
	}

	void render_prototype_player_ui(const PrototypePlayerView &player, ImVec2 window_size)
	{
		ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
		ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("Multitrack Prototype", nullptr,
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoBackground);

		const ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImDrawList *draw_list = ImGui::GetWindowDrawList();

		std::string title = player.song_title.empty() ? "Gameplay Prototype" : player.song_title;
		if (!player.song_artist.empty())
			title += " - " + player.song_artist;

		draw_list->AddText(
			ImVec2(canvas_pos.x + 28.0f, canvas_pos.y + 24.0f),
			IM_COL32(240, 243, 248, 255),
			title.c_str());

		std::string chart_label = player.has_chart
			? (player.chart_track_name + " / " + player.chart_difficulty_name)
			: std::string("No supported 5-fret chart loaded");
		draw_list->AddText(
			ImVec2(canvas_pos.x + 28.0f, canvas_pos.y + 52.0f),
			IM_COL32(180, 188, 202, 255),
			chart_label.c_str());

		if (!player.status_message.empty())
			draw_status_pill("player_status", ImVec2(canvas_pos.x + 20.0f, canvas_pos.y + 76.0f), player.status_message.c_str(), IM_COL32(16, 20, 29, 210));

		if (player.guitar_muted)
			draw_status_pill("mute_status", ImVec2(canvas_pos.x + 20.0f, canvas_pos.y + window_size.y - 62.0f), "Guitar muted", IM_COL32(120, 18, 18, 210));

		const float indicator_y = canvas_pos.y + window_size.y - 54.0f;
		for (int lane = 0; lane < 5; ++lane)
		{
			const float x = canvas_pos.x + 32.0f + static_cast<float>(lane) * 42.0f;
			const bool active = player.lane_held[static_cast<size_t>(lane)] || player.lane_sustaining[static_cast<size_t>(lane)];
			draw_list->AddCircleFilled(ImVec2(x, indicator_y), 14.0f, active ? kLaneColors[lane] : IM_COL32(44, 52, 67, 225));
			draw_list->AddCircle(ImVec2(x, indicator_y), 14.0f, IM_COL32(245, 245, 245, 210), 0, active ? 3.0f : 2.0f);
		}

		ImGui::Dummy(window_size);

		ImGui::End();
		ImGui::PopStyleVar();
	}
}
