#include "ui/AppUi.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
	using namespace rhythmreplugged::core;
	using namespace rhythmreplugged::ui;

	float lyric_strip_panel_height(const PrototypePlayerView &player)
	{
		if (player.visible_lyric_tokens.empty())
			return 0.0f;

		const float font_size = ImGui::GetFontSize();
		const float panel_padding_y = 12.0f;
		const float line_gap = 6.0f;
		return panel_padding_y * 2.0f + font_size * 2.0f + line_gap;
	}

	std::string format_countdown_text(double remaining_seconds)
	{
		const int total_seconds = static_cast<int>(std::ceil((std::max)(0.0, remaining_seconds)));
		const int minutes = total_seconds / 60;
		const int seconds = total_seconds % 60;

		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%d:%02d", minutes, seconds);
		return std::string(buffer);
	}

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

	void draw_lyric_strip(const PrototypePlayerView &player, const ImVec2 &canvas_pos, ImVec2 window_size)
	{
		if (player.visible_lyric_tokens.empty())
			return;

		ImDrawList *draw_list = ImGui::GetWindowDrawList();
		const int current_line_index = player.current_lyric_line_index;
		const int next_line_index = player.next_lyric_line_index;
		const float font_size = ImGui::GetFontSize();
		const float panel_padding_x = 18.0f;
		const float panel_padding_y = 12.0f;
		const float line_gap = 6.0f;
		const float panel_height = panel_padding_y * 2.0f + font_size * 2.0f + line_gap;
		const ImVec2 panel_pos(canvas_pos.x + 16.0f, canvas_pos.y + 8.0f);
		const ImVec2 panel_size(window_size.x - 32.0f, panel_height);
		draw_list->AddRectFilled(
			panel_pos,
			ImVec2(panel_pos.x + panel_size.x, panel_pos.y + panel_size.y),
			IM_COL32(10, 14, 22, 208),
			12.0f);
		draw_list->AddRect(
			panel_pos,
			ImVec2(panel_pos.x + panel_size.x, panel_pos.y + panel_size.y),
			IM_COL32(80, 92, 116, 180),
			12.0f);

		auto rendered_token_text = [](const PrototypePlayerView::LyricTokenView &token)
		{
			std::string rendered;
			if (token.prepend_space)
				rendered.push_back(' ');
			rendered += token.text;
			if (token.append_hyphen)
				rendered.push_back('-');
			return rendered;
		};

		auto token_color = [](const PrototypePlayerView::LyricTokenView &token)
		{
			if (token.is_current)
				return IM_COL32(255, 238, 154, 255);
			if (token.is_past)
				return IM_COL32(88, 212, 130, 255);
			return IM_COL32(136, 146, 164, 255);
		};

		auto collect_line_tokens = [&](int line_index)
		{
			std::vector<const PrototypePlayerView::LyricTokenView *> line_tokens;
			for (const PrototypePlayerView::LyricTokenView &token : player.visible_lyric_tokens)
			{
				if (token.line_index == line_index)
					line_tokens.push_back(&token);
			}
			return line_tokens;
		};

		auto wrap_line = [&](const std::vector<const PrototypePlayerView::LyricTokenView *> &line_tokens)
		{
			std::vector<std::vector<const PrototypePlayerView::LyricTokenView *>> rows;
			if (line_tokens.empty())
				return rows;

			const float max_width = panel_size.x - panel_padding_x * 2.0f;
			std::vector<float> token_widths;
			token_widths.reserve(line_tokens.size());
			float total_width = 0.0f;
			for (const PrototypePlayerView::LyricTokenView *token : line_tokens)
			{
				const std::string rendered = rendered_token_text(*token);
				const float token_width = ImGui::CalcTextSize(rendered.c_str()).x;
				token_widths.push_back(token_width);
				total_width += token_width;
			}

			if (total_width <= max_width)
			{
				rows.push_back(line_tokens);
				return rows;
			}

			int best_split_index = -1;
			float best_score = std::numeric_limits<float>::max();
			float prefix_width = 0.0f;
			for (size_t index = 0; index + 1 < line_tokens.size(); ++index)
			{
				prefix_width += token_widths[index];
				const float suffix_width = total_width - prefix_width;
				if (prefix_width > max_width || suffix_width > max_width)
					continue;

				const float balance_penalty = std::fabs(prefix_width - suffix_width);
				const float target_penalty = std::fabs(prefix_width - total_width * 0.5f);
				const float score = balance_penalty + target_penalty * 0.25f;
				if (score < best_score)
				{
					best_score = score;
					best_split_index = static_cast<int>(index);
				}
			}

			if (best_split_index >= 0)
			{
				std::vector<const PrototypePlayerView::LyricTokenView *> first_row;
				std::vector<const PrototypePlayerView::LyricTokenView *> second_row;
				first_row.reserve(static_cast<size_t>(best_split_index) + 1);
				second_row.reserve(line_tokens.size() - static_cast<size_t>(best_split_index) - 1);
				for (int index = 0; index <= best_split_index; ++index)
					first_row.push_back(line_tokens[static_cast<size_t>(index)]);
				for (size_t index = static_cast<size_t>(best_split_index) + 1; index < line_tokens.size(); ++index)
					second_row.push_back(line_tokens[index]);
				rows.push_back(std::move(first_row));
				rows.push_back(std::move(second_row));
				return rows;
			}

			std::vector<const PrototypePlayerView::LyricTokenView *> current_row;
			float current_width = 0.0f;
			for (size_t index = 0; index < line_tokens.size(); ++index)
			{
				const PrototypePlayerView::LyricTokenView *token = line_tokens[index];
				const float token_width = token_widths[index];
				if (!current_row.empty() && current_width + token_width > max_width)
				{
					rows.push_back(current_row);
					current_row.clear();
					current_width = 0.0f;
				}

				current_row.push_back(token);
				current_width += token_width;
			}

			if (!current_row.empty())
				rows.push_back(std::move(current_row));
			return rows;
		};

		auto draw_row = [&](const std::vector<const PrototypePlayerView::LyricTokenView *> &row, float baseline_y)
		{
			if (row.empty())
				return;

			float line_width = 0.0f;
			for (const PrototypePlayerView::LyricTokenView *token : row)
			{
				const std::string rendered = rendered_token_text(*token);
				line_width += ImGui::CalcTextSize(rendered.c_str()).x;
			}

			float cursor_x = panel_pos.x + (panel_size.x - line_width) * 0.5f;
			for (const PrototypePlayerView::LyricTokenView *token : row)
			{
				const std::string rendered = rendered_token_text(*token);
				const ImVec2 text_size = ImGui::CalcTextSize(rendered.c_str());
				draw_list->AddText(ImVec2(cursor_x, baseline_y), token_color(*token), rendered.c_str());
				cursor_x += text_size.x;
			}
		};

		const auto current_line_rows = wrap_line(collect_line_tokens(current_line_index));
		const auto next_line_rows = next_line_index >= 0
			? wrap_line(collect_line_tokens(next_line_index))
			: std::vector<std::vector<const PrototypePlayerView::LyricTokenView *>>{};

		std::vector<std::vector<const PrototypePlayerView::LyricTokenView *>> display_rows;
		if (!current_line_rows.empty())
			display_rows.push_back(current_line_rows.front());
		if (current_line_rows.size() > 1)
			display_rows.push_back(current_line_rows[1]);
		else if (!next_line_rows.empty())
			display_rows.push_back(next_line_rows.front());

		if (!display_rows.empty())
			draw_row(display_rows[0], panel_pos.y + panel_padding_y);
		if (display_rows.size() > 1)
			draw_row(display_rows[1], panel_pos.y + panel_padding_y + font_size + line_gap);
	}

	void draw_song_countdown(const PrototypePlayerView &player, const ImVec2 &canvas_pos, ImVec2 window_size)
	{
		if (player.song_duration_seconds <= 0.0)
			return;

		ImDrawList *draw_list = ImGui::GetWindowDrawList();
		const std::string countdown_text = format_countdown_text(player.song_time_remaining_seconds);
		const ImVec2 text_size = ImGui::CalcTextSize(countdown_text.c_str());
		const ImVec2 padding(14.0f, 10.0f);
		const float top_offset = 8.0f + lyric_strip_panel_height(player) + 10.0f;
		const ImVec2 panel_pos(
			canvas_pos.x + window_size.x - text_size.x - padding.x * 2.0f - 18.0f,
			canvas_pos.y + top_offset);
		const ImVec2 panel_size(text_size.x + padding.x * 2.0f, text_size.y + padding.y * 2.0f);

		draw_list->AddRectFilled(
			panel_pos,
			ImVec2(panel_pos.x + panel_size.x, panel_pos.y + panel_size.y),
			IM_COL32(10, 14, 22, 214),
			10.0f);
		draw_list->AddRect(
			panel_pos,
			ImVec2(panel_pos.x + panel_size.x, panel_pos.y + panel_size.y),
			IM_COL32(80, 92, 116, 180),
			10.0f);
		draw_list->AddText(
			ImVec2(panel_pos.x + padding.x, panel_pos.y + padding.y),
			IM_COL32(235, 239, 246, 255),
			countdown_text.c_str());
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

	void render_preload_progress_overlay(const InstrumentSelectView &menu, ImVec2 window_size, float ui_scale)
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
		const float button_spacing = ImGui::GetStyle().ItemSpacing.y;
		const float status_height = browser.status_message.empty()
			? 0.0f
			: ImGui::GetTextLineHeightWithSpacing() * 3.0f + button_spacing;
		const float preview_height = (std::max)(0.0f, content_region.y - status_height);
		const float preview_width = (std::max)(0.0f, content_region.x - list_width - column_spacing);
		ImGui::BeginChild(
			"selection_preview",
			ImVec2(preview_width, preview_height),
			true,
			ImGuiWindowFlags_NoScrollbar);

		const SongListItem *selected_entry = nullptr;
		if (!browser.entries.empty() &&
			browser.selected_index >= 0 &&
			browser.selected_index < static_cast<int>(browser.entries.size()))
		{
			selected_entry = &browser.entries[browser.selected_index];
		}

		if (selected_entry != nullptr)
		{
			const float preview_text_width = (std::max)(
				1.0f,
				preview_width - ImGui::GetStyle().WindowPadding.x * 2.0f);
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + preview_text_width);
			ImGui::TextUnformatted(selected_entry->label.c_str());
			ImGui::PopTextWrapPos();
			if (!selected_entry->subtitle.empty())
			{
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + preview_text_width);
				ImGui::TextDisabled("%s", selected_entry->subtitle.c_str());
				ImGui::PopTextWrapPos();
			}
			if (!selected_entry->error_message.empty())
			{
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + preview_text_width);
				ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "%s", selected_entry->error_message.c_str());
				ImGui::PopTextWrapPos();
			}

			const float preview_image_top = ImGui::GetCursorPosY() + ImGui::GetStyle().ItemSpacing.y;
			const std::optional<ImTextureRef> preview_cover_texture = actions.get_cover_texture_ref != nullptr
				? actions.get_cover_texture_ref(selected_entry->cover_art_path)
				: std::nullopt;
			if (preview_cover_texture.has_value() && preview_image_top < preview_height)
			{
				const float cover_size = (std::min)(preview_cover_size, ImGui::GetContentRegionAvail().x);
				const float cover_top = (std::max)(preview_image_top, preview_height - cover_size - ImGui::GetStyle().WindowPadding.y);
				ImGui::SetCursorPosY(cover_top);
				ImGui::Image(*preview_cover_texture, ImVec2(cover_size, cover_size));
			}
		}

		ImGui::EndChild();

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

	void render_instrument_select_ui(
		const InstrumentSelectView &menu,
		const InstrumentSelectUiActions &actions,
		ImVec2 window_size,
		float ui_scale)
	{
		int pending_selected_index = -1;
		bool pending_activate_selection = false;

		ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
		ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);
		ImGui::Begin("Instrument Select", nullptr,
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoCollapse);

		ImGui::TextUnformatted("Select Instrument");
		ImGui::Separator();
		if (!menu.song_title.empty())
			ImGui::TextWrapped("%s", menu.song_title.c_str());
		if (!menu.song_subtitle.empty())
			ImGui::TextDisabled("%s", menu.song_subtitle.c_str());
		ImGui::Spacing();

		for (int index = 0; index < static_cast<int>(menu.entries.size()); ++index)
		{
			const InstrumentListItem &entry = menu.entries[index];
			const bool selected = index == menu.selected_index;
			if (ImGui::Selectable(entry.label.c_str(), selected, 0, ImVec2(0.0f, 36.0f * ui_scale)) &&
				actions.set_selected_index != nullptr)
			{
				pending_selected_index = index;
			}
		}

		ImGui::Spacing();
		if (ImGui::Button("Next", ImVec2(240.0f * ui_scale, 0.0f)) &&
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

		render_preload_progress_overlay(menu, window_size, ui_scale);
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

	void render_prototype_player_ui(const GameplayFrameSnapshot &snapshot, ImVec2 window_size)
	{
		const PrototypePlayerView &player = snapshot.player;
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
			ImVec2(canvas_pos.x + 28.0f, canvas_pos.y + 92.0f),
			IM_COL32(240, 243, 248, 255),
			title.c_str());

		std::string chart_label = player.has_chart
			? (player.chart_track_name + " / " + player.chart_difficulty_name)
			: std::string("No supported playable chart loaded");
		draw_list->AddText(
			ImVec2(canvas_pos.x + 28.0f, canvas_pos.y + 118.0f),
			IM_COL32(180, 188, 202, 255),
			chart_label.c_str());

		draw_lyric_strip(player, canvas_pos, window_size);
		draw_song_countdown(player, canvas_pos, window_size);

		if (!player.status_message.empty())
			draw_status_pill("player_status", ImVec2(canvas_pos.x + 20.0f, canvas_pos.y + 146.0f), player.status_message.c_str(), IM_COL32(16, 20, 29, 210));

		if (player.playable_stem_muted)
		{
			const std::string muted_label = (player.playable_stem_label.empty() ? std::string("Instrument") : player.playable_stem_label) + " muted";
			draw_status_pill("mute_status", ImVec2(canvas_pos.x + 20.0f, canvas_pos.y + window_size.y - 62.0f), muted_label.c_str(), IM_COL32(120, 18, 18, 210));
		}

		ImGui::Dummy(window_size);

		ImGui::End();
		ImGui::PopStyleVar();
	}

	void render_frontend_options_ui(
		const ::rhythmreplugged::frontend_contract::FrontendOptions &options,
		FrontendOptionsUiState &ui_state,
		const FrontendOptionsUiActions &actions,
		ImVec2 window_size,
		float ui_scale,
		const char *config_path,
		const char *config_status_message)
	{
		using namespace ::rhythmreplugged::frontend_contract;

		const std::span<const FrontendOptionCategoryDefinition> categories = frontend_option_categories();
		if (categories.empty())
			return;

		ui_state.selected_category_index = (std::clamp)(ui_state.selected_category_index, 0, static_cast<int>(categories.size()) - 1);

		ImGui::SetNextWindowPos(ImVec2(window_size.x * 0.12f, window_size.y * 0.10f), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(window_size.x * 0.76f, window_size.y * 0.80f), ImGuiCond_Always);
		ImGui::Begin("Core Options", nullptr,
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoCollapse);

		ImGui::TextUnformatted("Core Options");
		ImGui::TextDisabled("SDL3 frontend menu mirroring libretro core options. Press Esc to resume.");
		if (config_path != nullptr && config_path[0] != '\0')
			ImGui::TextDisabled("Config: %s", config_path);
		if (config_status_message != nullptr && config_status_message[0] != '\0')
			ImGui::TextWrapped("%s", config_status_message);
		ImGui::Spacing();

		const float category_width = 240.0f * ui_scale;
		ImGui::BeginChild("core_option_categories", ImVec2(category_width, 0.0f), true);
		for (int index = 0; index < static_cast<int>(categories.size()); ++index)
		{
			const FrontendOptionCategoryDefinition &category = categories[static_cast<size_t>(index)];
			if (ImGui::Selectable(category.display_name, ui_state.selected_category_index == index))
				ui_state.selected_category_index = index;
		}
		ImGui::EndChild();

		ImGui::SameLine();
		ImGui::BeginChild("core_option_values", ImVec2(0.0f, 0.0f), true);
		const FrontendOptionCategoryDefinition &selected_category = categories[static_cast<size_t>(ui_state.selected_category_index)];
		ImGui::TextUnformatted(selected_category.display_name);
		if (selected_category.description != nullptr && selected_category.description[0] != '\0')
			ImGui::TextWrapped("%s", selected_category.description);
		ImGui::Separator();

		bool rendered_any_options = false;
		for (const FrontendOptionDefinition &definition : frontend_option_definitions())
		{
			if (definition.category_id != selected_category.id)
				continue;

			rendered_any_options = true;
			ImGui::PushID(definition.libretro_key);
			ImGui::TextUnformatted(definition.display_name);
			if (definition.description != nullptr && definition.description[0] != '\0')
				ImGui::TextWrapped("%s", definition.description);

			int current_choice_index = 0;
			const std::string_view current_value = frontend_option_value(options, definition.id);
			for (size_t choice_index = 0; choice_index < definition.choice_count; ++choice_index)
			{
				if (current_value == definition.choices[choice_index].value)
				{
					current_choice_index = static_cast<int>(choice_index);
					break;
				}
			}

			if (ImGui::BeginCombo("##value", definition.choices[current_choice_index].label))
			{
				for (size_t choice_index = 0; choice_index < definition.choice_count; ++choice_index)
				{
					const bool selected = current_choice_index == static_cast<int>(choice_index);
					if (ImGui::Selectable(definition.choices[choice_index].label, selected) &&
						actions.set_option_value != nullptr)
					{
						actions.set_option_value(definition, definition.choices[choice_index].value);
					}
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			const char *timing_text = "Applies immediately.";
			switch (definition.apply_timing)
			{
			case FrontendOptionApplyTiming::Immediate:
				timing_text = "Applies immediately.";
				break;
			case FrontendOptionApplyTiming::NextSong:
				timing_text = "Applies the next time a song setup begins.";
				break;
			case FrontendOptionApplyTiming::NextLaunch:
				timing_text = "Stored now and applied on next launch.";
				break;
			}
			ImGui::TextDisabled("%s", timing_text);
			ImGui::Spacing();
			ImGui::PopID();
		}

		if (!rendered_any_options)
			ImGui::TextDisabled("No options are currently assigned to this category.");

		ImGui::EndChild();
		ImGui::End();
	}
}
