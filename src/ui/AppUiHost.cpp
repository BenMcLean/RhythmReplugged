#include "ui/AppUiHost.h"

#include "ui/AppUi.h"

namespace rhythmreplugged
{
	void initialize_app_imgui(float ui_scale)
	{
		ImGui::StyleColorsDark();
		apply_imgui_style(ui_scale);

		ImGuiIO &io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		ImFontConfig font_config;
		font_config.SizePixels = 13.0f * ui_scale;
		io.FontDefault = io.Fonts->AddFontDefault(&font_config);
	}

	void render_app_ui(AppCore &app, ImVec2 window_size, float ui_scale, OpenGlCoverTextures &cover_textures)
	{
		if (app.mode() == AppMode::SongBrowser)
		{
			SongBrowserUiActions actions;
			actions.set_selected_index = [&](int index) { app.set_browser_selected_index(index); };
			actions.activate_selection = [&]() { app.activate_browser_selection(); };
			actions.get_cover_texture_ref = [&](const std::string &cover_path)
			{
				return cover_textures.get_texture_ref(cover_path);
			};
			render_song_browser_ui(app.song_browser_view(), actions, window_size, ui_scale);
			return;
		}

		render_prototype_player_ui(app.prototype_player_view(), window_size);
	}
}
