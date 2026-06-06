#pragma once

#include "core/app/AppTypes.h"
#include "frontend_contract/FrontendOptions.h"

#include <imgui.h>

#include <functional>
#include <optional>
#include <string>

namespace rhythmreplugged::ui
{
	struct SongBrowserUiActions
	{
		std::function<void(int)> set_selected_index;
		std::function<void()> activate_selection;
		std::function<std::optional<ImTextureRef>(const std::string &)> get_cover_texture_ref;
	};

	struct DifficultySelectUiActions
	{
		std::function<void(int)> set_selected_index;
		std::function<void()> activate_selection;
	};

	struct ModeSelectUiActions
	{
		std::function<void(int)> set_selected_index;
		std::function<void()> activate_selection;
	};

	struct InstrumentSelectUiActions
	{
		std::function<void(int)> set_selected_index;
		std::function<void()> activate_selection;
	};

	struct GameplayPauseUiActions
	{
		std::function<void(int)> set_selected_index;
		std::function<void()> activate_selection;
	};

	struct FrontendOptionsUiState
	{
		bool categories_focused = false;
		int selected_category_index = 0;
		int selected_option_index = 0;
	};

	struct FrontendOptionsNavInput
	{
		bool up_pressed = false;
		bool down_pressed = false;
		bool left_pressed = false;
		bool right_pressed = false;
		bool confirm_pressed = false;
		bool previous_value_pressed = false;
		bool next_value_pressed = false;
	};

	struct FrontendOptionsUiActions
	{
		std::function<bool(
			const ::rhythmreplugged::frontend_contract::FrontendOptionDefinition &definition,
			std::string_view value)> set_option_value;
	};

	void apply_imgui_style(float ui_scale);
	void render_song_browser_ui(
		const core::SongBrowserView &browser,
		const SongBrowserUiActions &actions,
		ImVec2 window_size,
		float ui_scale);
	void render_difficulty_select_ui(
		const core::DifficultySelectView &menu,
		const DifficultySelectUiActions &actions,
		ImVec2 window_size,
		float ui_scale);
	void render_mode_select_ui(
		const core::ModeSelectView &menu,
		const ModeSelectUiActions &actions,
		ImVec2 window_size,
		float ui_scale);
	void render_instrument_select_ui(
		const core::InstrumentSelectView &menu,
		const InstrumentSelectUiActions &actions,
		ImVec2 window_size,
		float ui_scale);
	void render_song_loading_ui(
		const core::DifficultySelectView &menu,
		ImVec2 window_size,
		float ui_scale);
	void render_song_player_ui(
		const core::GameplayFrameSnapshot &snapshot,
		ImVec2 window_size);
	void render_gameplay_pause_ui(
		const core::GameplayFrameSnapshot &snapshot,
		const core::GameplayPauseMenuView &menu,
		const GameplayPauseUiActions &actions,
		ImVec2 window_size,
		float ui_scale);
	void render_frontend_options_ui(
		const ::rhythmreplugged::frontend_contract::FrontendOptions &options,
		FrontendOptionsUiState &ui_state,
		const FrontendOptionsNavInput &nav_input,
		const FrontendOptionsUiActions &actions,
		ImVec2 window_size,
		float ui_scale,
		const char *config_path,
		const char *config_status_message);
}
