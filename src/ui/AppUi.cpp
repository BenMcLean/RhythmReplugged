#include "ui/AppUi.h"
#include "ui/UiNavigation.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
	using namespace rhythmreplugged::core;
	using namespace rhythmreplugged::ui;

	float lyric_strip_panel_height(const SongPlayerView &player)
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

	int next_choice_index(int current_choice_index, int delta, int choice_count)
	{
		if (choice_count <= 0)
			return current_choice_index;

		int next_index = current_choice_index + delta;
		while (next_index < 0)
			next_index += choice_count;
		while (next_index >= choice_count)
			next_index -= choice_count;
		return next_index;
	}

	int selected_choice_index_for_option(
		const ::rhythmreplugged::frontend_contract::FrontendOptions &options,
		const ::rhythmreplugged::frontend_contract::FrontendOptionDefinition &definition)
	{
		const std::string_view current_value = frontend_option_value(options, definition.id);
		for (size_t choice_index = 0; choice_index < definition.choice_count; ++choice_index)
		{
			if (current_value == definition.choices[choice_index].value)
				return static_cast<int>(choice_index);
		}

		return 0;
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

	void draw_lyric_strip(const SongPlayerView &player, const ImVec2 &canvas_pos, ImVec2 window_size)
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

		auto rendered_token_text = [](const SongPlayerView::LyricTokenView &token)
		{
			std::string rendered;
			if (token.prepend_space)
				rendered.push_back(' ');
			rendered += token.text;
			if (token.append_hyphen)
				rendered.push_back('-');
			return rendered;
		};

		auto token_color = [](const SongPlayerView::LyricTokenView &token)
		{
			if (token.is_current)
				return IM_COL32(255, 238, 154, 255);
			if (token.is_past)
				return IM_COL32(88, 212, 130, 255);
			return IM_COL32(136, 146, 164, 255);
		};

		auto collect_line_tokens = [&](int line_index)
		{
			std::vector<const SongPlayerView::LyricTokenView *> line_tokens;
			for (const SongPlayerView::LyricTokenView &token : player.visible_lyric_tokens)
			{
				if (token.line_index == line_index)
					line_tokens.push_back(&token);
			}
			return line_tokens;
		};

		auto wrap_line = [&](const std::vector<const SongPlayerView::LyricTokenView *> &line_tokens)
		{
			std::vector<std::vector<const SongPlayerView::LyricTokenView *>> rows;
			if (line_tokens.empty())
				return rows;

			const float max_width = panel_size.x - panel_padding_x * 2.0f;
			std::vector<float> token_widths;
			token_widths.reserve(line_tokens.size());
			float total_width = 0.0f;
			for (const SongPlayerView::LyricTokenView *token : line_tokens)
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
				std::vector<const SongPlayerView::LyricTokenView *> first_row;
				std::vector<const SongPlayerView::LyricTokenView *> second_row;
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

			std::vector<const SongPlayerView::LyricTokenView *> current_row;
			float current_width = 0.0f;
			for (size_t index = 0; index < line_tokens.size(); ++index)
			{
				const SongPlayerView::LyricTokenView *token = line_tokens[index];
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

		auto draw_row = [&](const std::vector<const SongPlayerView::LyricTokenView *> &row, float baseline_y)
		{
			if (row.empty())
				return;

			float line_width = 0.0f;
			for (const SongPlayerView::LyricTokenView *token : row)
			{
				const std::string rendered = rendered_token_text(*token);
				line_width += ImGui::CalcTextSize(rendered.c_str()).x;
			}

			float cursor_x = panel_pos.x + (panel_size.x - line_width) * 0.5f;
			for (const SongPlayerView::LyricTokenView *token : row)
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
			: std::vector<std::vector<const SongPlayerView::LyricTokenView *>>{};

		std::vector<std::vector<const SongPlayerView::LyricTokenView *>> display_rows;
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

	void draw_song_countdown(const SongPlayerView &player, const ImVec2 &canvas_pos, ImVec2 window_size)
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

	void draw_replugged_lane_status(const GameplayFrameSnapshot &snapshot, const ImVec2 &canvas_pos, ImVec2 window_size)
	{
		if (snapshot.scene.players.empty())
			return;

		const PlayerGameplayView &player = snapshot.scene.players.front();
		if (player.world.gameplay_mode != GameplayMode::Replugged || player.world.lanes.empty())
			return;

		float span_left = 0.0f;
		float span_right = 0.0f;
		bool have_span = false;
		for (const InstrumentLaneView &lane : player.world.lanes)
		{
			const float left = lane.lane_center_x - lane.lane_width * 0.5f;
			const float right = lane.lane_center_x + lane.lane_width * 0.5f;
			if (!have_span)
			{
				span_left = left;
				span_right = right;
				have_span = true;
			}
			else
			{
				span_left = (std::min)(span_left, left);
				span_right = (std::max)(span_right, right);
			}
		}
		if (!have_span || std::fabs(span_right - span_left) < 0.001f)
			return;

		ImDrawList *draw_list = ImGui::GetWindowDrawList();
		const float content_left = canvas_pos.x + 42.0f;
		const float content_right = canvas_pos.x + window_size.x - 42.0f;
		const float content_width = content_right - content_left;
		const float label_y = canvas_pos.y + window_size.y - 66.0f;
		const float bar_y = canvas_pos.y + window_size.y - 42.0f;
		const float bar_height = 10.0f;
		for (const InstrumentLaneView &lane : player.world.lanes)
		{
			const float normalized_left = (lane.lane_center_x - lane.lane_width * 0.5f - span_left) / (span_right - span_left);
			const float normalized_right = (lane.lane_center_x + lane.lane_width * 0.5f - span_left) / (span_right - span_left);
			const float bar_left = content_left + normalized_left * content_width;
			const float bar_right = content_left + normalized_right * content_width;
			const ImVec2 bar_min(bar_left, bar_y);
			const ImVec2 bar_max(bar_right, bar_y + bar_height);
			draw_list->AddRectFilled(bar_min, bar_max, IM_COL32(22, 28, 40, 224), 4.0f);
			draw_list->AddRect(bar_min, bar_max, IM_COL32(82, 94, 118, 255), 4.0f);

			if (lane.has_scheduled_lock)
			{
				const float fill_progress = lane.lock_state == LaneLockState::Locked
					? std::clamp(lane.lock_progress, 0.0f, 1.0f)
					: 1.0f;
				const float filled_right = bar_left + (bar_right - bar_left) * fill_progress;
				draw_list->AddRectFilled(
					bar_min,
					ImVec2(filled_right, bar_max.y),
					IM_COL32(84, 204, 118, 232),
					4.0f);
			}
			else if (lane.lock_build_progress > 0.0f)
			{
				const float filled_right = bar_left + (bar_right - bar_left) * std::clamp(lane.lock_build_progress, 0.0f, 1.0f);
				draw_list->AddRectFilled(
					bar_min,
					ImVec2(filled_right, bar_max.y),
					IM_COL32(224, 190, 72, 220),
					4.0f);
			}
			else if (lane.should_prompt)
			{
				draw_list->AddRectFilled(bar_min, bar_max, IM_COL32(214, 159, 54, 168), 4.0f);
			}

			const char *state_text = lane.lock_state == LaneLockState::Locked
				? "Locked"
				: (lane.has_scheduled_lock ? "Ready" : (lane.lock_build_progress > 0.0f ? "Building" : (lane.should_prompt ? "Play" : "Unlocked")));
			const std::string label = lane.instrument_label + " - " + state_text;
			const ImVec2 text_size = ImGui::CalcTextSize(label.c_str());
			draw_list->AddText(
				ImVec2((bar_left + bar_right - text_size.x) * 0.5f, label_y),
				IM_COL32(228, 233, 240, 255),
				label.c_str());
		}
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

	void render_preload_progress_overlay(const ModeSelectView &menu, ImVec2 window_size, float ui_scale)
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
		const int wheel_steps = wheel_steps_from_delta(io.MouseWheel);
		const bool browser_changed = browser.current_path != last_browser_path;
		const bool selected_index_changed = browser.selected_index != last_selected_index;
		const bool follow_selection = should_follow_selection(browser_changed, selected_index_changed, io);

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
			if (selected && follow_selection)
				ImGui::SetScrollHereY(0.5f);
			queue_selected_index_change(activated, index, pending_selected_index);

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

		if (wheel_steps != 0 && !browser.entries.empty() && actions.set_selected_index != nullptr)
		{
			pending_selected_index = wrap_menu_index(
				browser.selected_index,
				-wheel_steps,
				static_cast<int>(browser.entries.size()));
		}

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
		const int wheel_steps = wheel_steps_from_delta(ImGui::GetIO().MouseWheel);

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
			const bool activated = ImGui::Selectable(entry.label.c_str(), selected, 0, ImVec2(0.0f, 36.0f * ui_scale));
			queue_selected_index_change(activated, index, pending_selected_index);
		}

		if (!menu.status_message.empty())
		{
			ImGui::Spacing();
			ImGui::TextWrapped("%s", menu.status_message.c_str());
		}

		ImGui::Spacing();
		ImGui::TextDisabled("B: Back    A / Start: Confirm");
		ImGui::End();

		if (wheel_steps != 0 && !menu.entries.empty() && actions.set_selected_index != nullptr)
		{
			pending_selected_index = clamp_menu_index(
				menu.selected_index,
				-wheel_steps,
				static_cast<int>(menu.entries.size()));
		}

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

	void render_mode_select_ui(
		const ModeSelectView &menu,
		const ModeSelectUiActions &actions,
		ImVec2 window_size,
		float ui_scale)
	{
		int pending_selected_index = -1;
		bool pending_activate_selection = false;
		const int wheel_steps = wheel_steps_from_delta(ImGui::GetIO().MouseWheel);

		ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
		ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);
		ImGui::Begin("Mode Select", nullptr,
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoCollapse);

		ImGui::TextUnformatted("Select Mode");
		ImGui::Separator();
		if (!menu.song_title.empty())
			ImGui::TextWrapped("%s", menu.song_title.c_str());
		if (!menu.song_subtitle.empty())
			ImGui::TextDisabled("%s", menu.song_subtitle.c_str());
		ImGui::Spacing();

		for (int index = 0; index < static_cast<int>(menu.entries.size()); ++index)
		{
			const ModeListItem &entry = menu.entries[index];
			const bool selected = index == menu.selected_index;
			const bool activated = ImGui::Selectable(entry.label.c_str(), selected, 0, ImVec2(0.0f, 36.0f * ui_scale));
			queue_selected_index_change(activated, index, pending_selected_index);
		}

		if (!menu.status_message.empty())
		{
			ImGui::Spacing();
			ImGui::TextWrapped("%s", menu.status_message.c_str());
		}

		ImGui::Spacing();
		ImGui::TextDisabled("B: Back    A / Start: Confirm");
		ImGui::End();

		if (wheel_steps != 0 && !menu.entries.empty() && actions.set_selected_index != nullptr)
		{
			pending_selected_index = clamp_menu_index(
				menu.selected_index,
				-wheel_steps,
				static_cast<int>(menu.entries.size()));
		}

		render_preload_progress_overlay(menu, window_size, ui_scale);

		if (pending_selected_index >= 0 && actions.set_selected_index != nullptr)
			actions.set_selected_index(pending_selected_index);

		if (pending_activate_selection && actions.activate_selection != nullptr)
			actions.activate_selection();
	}

	void render_instrument_select_ui(
		const InstrumentSelectView &menu,
		const InstrumentSelectUiActions &actions,
		ImVec2 window_size,
		float ui_scale)
	{
		int pending_selected_index = -1;
		bool pending_activate_selection = false;
		const int wheel_steps = wheel_steps_from_delta(ImGui::GetIO().MouseWheel);

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
			const bool activated = ImGui::Selectable(entry.label.c_str(), selected, 0, ImVec2(0.0f, 36.0f * ui_scale));
			queue_selected_index_change(activated, index, pending_selected_index);
		}

		if (!menu.status_message.empty())
		{
			ImGui::Spacing();
			ImGui::TextWrapped("%s", menu.status_message.c_str());
		}

		ImGui::Spacing();
		ImGui::TextDisabled("B: Back    A / Start: Toggle claim or continue");
		ImGui::End();

		if (wheel_steps != 0 && !menu.entries.empty() && actions.set_selected_index != nullptr)
		{
			pending_selected_index = clamp_menu_index(
				menu.selected_index,
				-wheel_steps,
				static_cast<int>(menu.entries.size()));
		}

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

	void render_song_player_ui(const GameplayFrameSnapshot &snapshot, ImVec2 window_size)
	{
		const SongPlayerView &player = snapshot.player;
		ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
		ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("Multitrack", nullptr,
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoBackground);

		const ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImDrawList *draw_list = ImGui::GetWindowDrawList();

		std::string title = player.song_title.empty() ? "Gameplay" : player.song_title;
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
		draw_replugged_lane_status(snapshot, canvas_pos, window_size);

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

	void render_gameplay_pause_ui(
		const GameplayFrameSnapshot &snapshot,
		const GameplayPauseMenuView &menu,
		const GameplayPauseUiActions &actions,
		ImVec2 window_size,
		float ui_scale)
	{
		int pending_selected_index = -1;
		bool pending_activate_selection = false;
		const SongPlayerView &player = snapshot.player;
		const int wheel_steps = wheel_steps_from_delta(ImGui::GetIO().MouseWheel);

		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
		ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		ImGui::Begin("Gameplay Pause", nullptr,
			ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoScrollbar |
			ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoBringToFrontOnFocus);

		ImDrawList *draw_list = ImGui::GetWindowDrawList();
		const ImVec2 origin = ImGui::GetWindowPos();
		const ImVec2 max(origin.x + window_size.x, origin.y + window_size.y);
		draw_list->AddRectFilled(origin, max, IM_COL32(6, 8, 13, 148));

		const ImVec2 panel_size(360.0f * ui_scale, 268.0f * ui_scale);
		const ImVec2 panel_min(
			origin.x + (window_size.x - panel_size.x) * 0.5f,
			origin.y + (window_size.y - panel_size.y) * 0.5f);
		const ImVec2 panel_max(panel_min.x + panel_size.x, panel_min.y + panel_size.y);
		draw_list->AddRectFilled(panel_min, panel_max, IM_COL32(14, 18, 28, 236), 18.0f);
		draw_list->AddRect(panel_min, panel_max, IM_COL32(86, 104, 138, 255), 18.0f, 0, 2.0f);

		ImGui::SetCursorScreenPos(ImVec2(panel_min.x + 28.0f * ui_scale, panel_min.y + 24.0f * ui_scale));
		ImGui::BeginGroup();
		ImGui::TextUnformatted("Pause");
		if (!player.song_title.empty())
			ImGui::TextWrapped("%s", player.song_title.c_str());
		if (!player.song_artist.empty())
			ImGui::TextDisabled("%s", player.song_artist.c_str());
		ImGui::Spacing();

		const char *labels[] = {"Resume", "End Song"};
		for (int index = 0; index < 2; ++index)
		{
			const bool selected = menu.selected_index == index;
			const bool activated = ImGui::Selectable(labels[index], selected, 0, ImVec2(panel_size.x - 56.0f * ui_scale, 36.0f * ui_scale));
			queue_selected_index_change(activated, index, pending_selected_index);
		}

		ImGui::Spacing();
		ImGui::TextDisabled("B: Resume    A / Start: Confirm");
		ImGui::EndGroup();
		ImGui::End();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(2);

		if (wheel_steps != 0 && actions.set_selected_index != nullptr)
			pending_selected_index = std::clamp(menu.selected_index - wheel_steps, 0, 1);

		if (pending_selected_index >= 0 && actions.set_selected_index != nullptr)
			actions.set_selected_index(pending_selected_index);

		if (pending_activate_selection && actions.activate_selection != nullptr)
			actions.activate_selection();
	}

	void render_frontend_options_ui(
		const ::rhythmreplugged::frontend_contract::FrontendOptions &options,
		FrontendOptionsUiState &ui_state,
		const FrontendOptionsNavInput &nav_input,
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
		const ImGuiIO &io = ImGui::GetIO();
		const int wheel_steps = wheel_steps_from_delta(io.MouseWheel);

		std::vector<const FrontendOptionDefinition *> category_options;
		for (const FrontendOptionDefinition &definition : frontend_option_definitions())
		{
			if (definition.category_id == categories[static_cast<size_t>(ui_state.selected_category_index)].id)
				category_options.push_back(&definition);
		}

		if (category_options.empty())
		{
			ui_state.selected_option_index = 0;
		}
		else
		{
			ui_state.selected_option_index = (std::clamp)(
				ui_state.selected_option_index,
				0,
				static_cast<int>(category_options.size()) - 1);
		}

		const bool categories_were_focused = ui_state.categories_focused;
		if (nav_input.left_pressed)
			ui_state.categories_focused = true;
		if (nav_input.right_pressed)
			ui_state.categories_focused = false;
		const bool category_selection_changed = nav_input.up_pressed || nav_input.down_pressed;
		const bool option_selection_changed = nav_input.up_pressed || nav_input.down_pressed;

		if (ui_state.categories_focused)
		{
			if (nav_input.up_pressed)
			{
				ui_state.selected_category_index = clamp_menu_index(
					ui_state.selected_category_index,
					-1,
					static_cast<int>(categories.size()));
				ui_state.selected_option_index = 0;
			}
			if (nav_input.down_pressed)
			{
				ui_state.selected_category_index = clamp_menu_index(
					ui_state.selected_category_index,
					1,
					static_cast<int>(categories.size()));
				ui_state.selected_option_index = 0;
			}
		}
		else if (!category_options.empty())
		{
			if (nav_input.up_pressed)
			{
				ui_state.selected_option_index = clamp_menu_index(
					ui_state.selected_option_index,
					-1,
					static_cast<int>(category_options.size()));
			}
			if (nav_input.down_pressed)
			{
				ui_state.selected_option_index = clamp_menu_index(
					ui_state.selected_option_index,
					1,
					static_cast<int>(category_options.size()));
			}

			const FrontendOptionDefinition &selected_definition = *category_options[static_cast<size_t>(ui_state.selected_option_index)];
			const int current_choice_index = selected_choice_index_for_option(options, selected_definition);
			int choice_delta = 0;
			const bool consumed_focus_move_right = categories_were_focused && nav_input.right_pressed;
			const bool consumed_focus_move_left = !categories_were_focused && nav_input.left_pressed;
			if (!consumed_focus_move_left && (nav_input.left_pressed || nav_input.previous_value_pressed))
				choice_delta -= 1;
			if (!consumed_focus_move_right && (nav_input.right_pressed || nav_input.next_value_pressed || nav_input.confirm_pressed))
				choice_delta += 1;
			if (choice_delta != 0 && actions.set_option_value != nullptr)
			{
				const int next_index = next_choice_index(
					current_choice_index,
					choice_delta,
					static_cast<int>(selected_definition.choice_count));
				actions.set_option_value(
					selected_definition,
					selected_definition.choices[static_cast<size_t>(next_index)].value);
			}
		}

		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
		ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		ImGui::Begin("Core Options Screen", nullptr,
			ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoScrollbar |
			ImGuiWindowFlags_NoScrollWithMouse |
			ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoBringToFrontOnFocus);

		ImDrawList *screen_draw_list = ImGui::GetWindowDrawList();
		const ImVec2 screen_origin = ImGui::GetWindowPos();
		const ImVec2 screen_max(screen_origin.x + window_size.x, screen_origin.y + window_size.y);
		screen_draw_list->AddRectFilled(screen_origin, screen_max, IM_COL32(10, 13, 20, 255));

		const ImVec2 content_origin(screen_origin.x + 40.0f * ui_scale, screen_origin.y + 28.0f * ui_scale);
		const float header_height = 104.0f * ui_scale;
		const float category_width = (std::max)(260.0f * ui_scale, window_size.x * 0.24f);
		const float panel_gap = 24.0f * ui_scale;
		const float content_height = window_size.y - content_origin.y + screen_origin.y - 34.0f * ui_scale;
		const ImVec2 category_panel_min(content_origin.x, content_origin.y + header_height);
		const ImVec2 category_panel_size(category_width, (std::max)(120.0f * ui_scale, content_height - header_height));
		const ImVec2 values_panel_min(category_panel_min.x + category_panel_size.x + panel_gap, category_panel_min.y);
		const ImVec2 values_panel_size(
			(std::max)(160.0f * ui_scale, window_size.x - (values_panel_min.x - screen_origin.x) - 40.0f * ui_scale),
			category_panel_size.y);

		screen_draw_list->AddText(ImVec2(content_origin.x, content_origin.y), IM_COL32(240, 243, 248, 255), "Core Options");
		if (config_status_message != nullptr && config_status_message[0] != '\0')
			screen_draw_list->AddText(ImVec2(content_origin.x, content_origin.y + 28.0f * ui_scale), IM_COL32(208, 214, 224, 255), config_status_message);

		screen_draw_list->AddRectFilled(
			category_panel_min,
			ImVec2(category_panel_min.x + category_panel_size.x, category_panel_min.y + category_panel_size.y),
			IM_COL32(20, 25, 36, 255),
			12.0f);
		screen_draw_list->AddRectFilled(
			values_panel_min,
			ImVec2(values_panel_min.x + values_panel_size.x, values_panel_min.y + values_panel_size.y),
			IM_COL32(16, 20, 29, 255),
			12.0f);
		screen_draw_list->AddRect(
			category_panel_min,
			ImVec2(category_panel_min.x + category_panel_size.x, category_panel_min.y + category_panel_size.y),
			ui_state.categories_focused ? IM_COL32(126, 168, 255, 255) : IM_COL32(52, 62, 82, 255),
			12.0f,
			0,
			ui_state.categories_focused ? 3.0f : 1.0f);
		screen_draw_list->AddRect(
			values_panel_min,
			ImVec2(values_panel_min.x + values_panel_size.x, values_panel_min.y + values_panel_size.y),
			!ui_state.categories_focused ? IM_COL32(126, 168, 255, 255) : IM_COL32(52, 62, 82, 255),
			12.0f,
			0,
			!ui_state.categories_focused ? 3.0f : 1.0f);

		ImGui::SetCursorScreenPos(ImVec2(category_panel_min.x + 12.0f * ui_scale, category_panel_min.y + 12.0f * ui_scale));
		ImGui::BeginChild("core_option_categories", ImVec2(category_panel_size.x - 24.0f * ui_scale, category_panel_size.y - 24.0f * ui_scale), false);
		const bool category_window_hovered = ImGui::IsWindowHovered();
		for (int index = 0; index < static_cast<int>(categories.size()); ++index)
		{
			const FrontendOptionCategoryDefinition &category = categories[static_cast<size_t>(index)];
			const bool selected = ui_state.selected_category_index == index;
			if (ImGui::Selectable(category.display_name, selected))
			{
				ui_state.categories_focused = true;
				ui_state.selected_category_index = index;
				ui_state.selected_option_index = 0;
			}
			if (selected && ui_state.categories_focused && category_selection_changed)
				ImGui::SetScrollHereY(0.5f);
		}
		ImGui::EndChild();
		if (category_window_hovered && wheel_steps != 0)
		{
			ui_state.categories_focused = true;
			ui_state.selected_category_index = (std::clamp)(
				ui_state.selected_category_index - wheel_steps,
				0,
				static_cast<int>(categories.size()) - 1);
			ui_state.selected_option_index = 0;
		}

		ImGui::SetCursorScreenPos(ImVec2(values_panel_min.x + 18.0f * ui_scale, values_panel_min.y + 16.0f * ui_scale));
		ImGui::BeginChild("core_option_values", ImVec2(values_panel_size.x - 36.0f * ui_scale, values_panel_size.y - 32.0f * ui_scale), false);
		const bool values_window_hovered = ImGui::IsWindowHovered();
		const FrontendOptionCategoryDefinition &selected_category = categories[static_cast<size_t>(ui_state.selected_category_index)];
		ImGui::TextUnformatted(selected_category.display_name);
		ImGui::TextDisabled("%s", ui_state.categories_focused ? "Left panel focused" : "Right panel focused");
		ImGui::Separator();

		bool rendered_any_options = false;
		for (int option_index = 0; option_index < static_cast<int>(category_options.size()); ++option_index)
		{
			const FrontendOptionDefinition &definition = *category_options[static_cast<size_t>(option_index)];
			rendered_any_options = true;
			ImGui::PushID(definition.libretro_key);
			const bool option_selected = ui_state.selected_option_index == option_index;
			if (ImGui::Selectable(definition.display_name, option_selected))
			{
				ui_state.categories_focused = false;
				ui_state.selected_option_index = option_index;
			}
			if (ImGui::IsItemHovered())
			{
				ui_state.categories_focused = false;
				ui_state.selected_option_index = option_index;
			}
			if (definition.description != nullptr && definition.description[0] != '\0')
				ImGui::TextWrapped("%s", definition.description);
			if (option_selected && !ui_state.categories_focused && option_selection_changed)
				ImGui::SetScrollHereY(0.35f);

			const int current_choice_index = selected_choice_index_for_option(options, definition);
			if (ImGui::ArrowButton("##prev", ImGuiDir_Left) &&
				actions.set_option_value != nullptr)
			{
				const int next_index = next_choice_index(current_choice_index, -1, static_cast<int>(definition.choice_count));
				actions.set_option_value(definition, definition.choices[static_cast<size_t>(next_index)].value);
			}
			if (ImGui::IsItemHovered())
			{
				ui_state.categories_focused = false;
				ui_state.selected_option_index = option_index;
			}
			ImGui::SameLine();

			const float value_button_width = 220.0f * ui_scale;
			if (ImGui::Button(definition.choices[static_cast<size_t>(current_choice_index)].label, ImVec2(value_button_width, 0.0f)) &&
				actions.set_option_value != nullptr)
			{
				const int next_index = next_choice_index(current_choice_index, 1, static_cast<int>(definition.choice_count));
				actions.set_option_value(definition, definition.choices[static_cast<size_t>(next_index)].value);
			}
			if (ImGui::IsItemHovered())
			{
				ui_state.categories_focused = false;
				ui_state.selected_option_index = option_index;
			}
			ImGui::SameLine();
			if (ImGui::ArrowButton("##next", ImGuiDir_Right) &&
				actions.set_option_value != nullptr)
			{
				const int next_index = next_choice_index(current_choice_index, 1, static_cast<int>(definition.choice_count));
				actions.set_option_value(definition, definition.choices[static_cast<size_t>(next_index)].value);
			}
			if (ImGui::IsItemHovered())
			{
				ui_state.categories_focused = false;
				ui_state.selected_option_index = option_index;
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

		if (values_window_hovered && wheel_steps != 0 && !category_options.empty() && actions.set_option_value != nullptr)
		{
			ui_state.categories_focused = false;
			const FrontendOptionDefinition &wheel_definition = *category_options[static_cast<size_t>(ui_state.selected_option_index)];
			const int current_choice_index = selected_choice_index_for_option(options, wheel_definition);
			const int next_index = next_choice_index(
				current_choice_index,
				-wheel_steps,
				static_cast<int>(wheel_definition.choice_count));
			if (next_index != current_choice_index)
			{
				actions.set_option_value(
					wheel_definition,
					wheel_definition.choices[static_cast<size_t>(next_index)].value);
			}
		}

		if (!rendered_any_options)
			ImGui::TextDisabled("No options are currently assigned to this category.");

		ImGui::EndChild();
		ImGui::End();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(2);
	}
}
