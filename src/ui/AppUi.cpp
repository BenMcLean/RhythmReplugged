#include "ui/AppUi.h"

#include <algorithm>

namespace
{
	using namespace rhythmreplugged;

	constexpr ImU32 kLaneColors[5] = {
		IM_COL32(90, 197, 92, 255),
		IM_COL32(210, 62, 62, 255),
		IM_COL32(226, 209, 63, 255),
		IM_COL32(65, 117, 220, 255),
		IM_COL32(234, 140, 41, 255),
	};

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
				is_held || is_sustaining ? kLaneColors[lane] : IM_COL32(36, 44, 58, 255));
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
				kLaneColors[note.lane]);
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

		if (!player.status_message.empty())
		{
			draw_list->AddText(
				ImVec2(canvas_pos.x + 20.0f, canvas_pos.y + 18.0f),
				IM_COL32(230, 235, 245, 255),
				player.status_message.c_str());
		}

		ImGui::Dummy(canvas_size);
		ImGui::EndChild();
	}
}

namespace rhythmreplugged
{
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
		ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
		ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);
		ImGui::Begin("Song Browser", nullptr,
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoCollapse);

		ImGui::TextUnformatted("Song Browser");
		ImGui::Separator();
		ImGui::TextWrapped("Enter/Space: open or play   Backspace/0: back   Up/Down: move");
		ImGui::TextWrapped("Root: %s", browser.root_path.c_str());
		ImGui::TextWrapped("Path: %s", browser.current_path.c_str());

		const ImVec2 content_region = ImGui::GetContentRegionAvail();
		const float column_spacing = ImGui::GetStyle().ItemSpacing.x;
		const float list_cover_size = 32.0f * ui_scale;
		const float preview_cover_size = 256.0f * ui_scale;
		const float min_list_width = 420.0f * ui_scale;
		float list_width = content_region.x * 0.52f;
		list_width = (std::min)(list_width, content_region.x - column_spacing - (320.0f * ui_scale));
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

			const std::optional<ImTextureRef> row_cover_texture = actions.get_cover_texture_ref != nullptr
				? actions.get_cover_texture_ref(entry.cover_art_path)
				: std::nullopt;

			if (row_cover_texture.has_value())
			{
				ImGui::Image(*row_cover_texture, ImVec2(list_cover_size, list_cover_size));
				ImGui::SameLine();
			}

			if (ImGui::Selectable(label.c_str(), selected, 0, ImVec2(0.0f, list_cover_size)) &&
				actions.set_selected_index != nullptr)
			{
				actions.set_selected_index(index);
			}

			if (ImGui::IsItemHovered() &&
				ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
				actions.activate_selection != nullptr)
			{
				actions.activate_selection();
			}

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
		const float preview_height = (std::max)(220.0f * ui_scale, content_region.y - action_row_height);
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
			actions.activate_selection();
		}

		if (!browser.status_message.empty())
		{
			ImGui::Spacing();
			ImGui::TextWrapped("%s", browser.status_message.c_str());
		}

		ImGui::EndGroup();
		ImGui::End();
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
			ImGuiWindowFlags_NoTitleBar);

		draw_chart_highway(player, ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);

		ImGui::End();
		ImGui::PopStyleVar();
	}
}
