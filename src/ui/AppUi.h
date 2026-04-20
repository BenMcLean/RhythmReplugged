#pragma once

#include "core/app/AppTypes.h"

#include <imgui.h>

#include <functional>
#include <optional>
#include <string>

namespace rhythmreplugged
{
	struct SongBrowserUiActions
	{
		std::function<void(int)> set_selected_index;
		std::function<void()> activate_selection;
		std::function<std::optional<ImTextureRef>(const std::string &)> get_cover_texture_ref;
	};

	void apply_imgui_style(float ui_scale);
	void render_song_browser_ui(
		const SongBrowserView &browser,
		const SongBrowserUiActions &actions,
		ImVec2 window_size,
		float ui_scale);
	void render_prototype_player_ui(const PrototypePlayerView &player, ImVec2 window_size);
}
