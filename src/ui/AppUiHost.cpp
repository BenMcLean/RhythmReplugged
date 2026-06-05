#include "ui/AppUiHost.h"

#include "ui/AppUi.h"

namespace rhythmreplugged::ui
{
	using namespace rhythmreplugged::core;

	void initialize_app_imgui(float ui_scale)
	{
		ImGui::StyleColorsDark();
		apply_imgui_style(ui_scale);

		ImGuiIO &io = ImGui::GetIO();
		ImFontConfig font_config;
		font_config.SizePixels = 13.0f * ui_scale;
		io.FontDefault = io.Fonts->AddFontDefault(&font_config);
	}

	void render_app_ui(AppCore &app, ImVec2 window_size, float ui_scale, OpenGlCoverTextures &cover_textures)
	{
		if (app.mode() == AppMode::Menu)
		{
			if (app.menu_screen() == MenuScreen::SongBrowser)
			{
				const SongBrowserView &browser_view = app.song_browser_view();
				cover_textures.sync_song_browser_directory(browser_view);

				SongBrowserUiActions actions;
				actions.set_selected_index = [&](int index) { app.set_browser_selected_index(index); };
				actions.activate_selection = [&]() { app.activate_browser_selection(); };
				actions.get_cover_texture_ref = [&](const std::string &cover_path)
				{
					return cover_textures.get_texture_ref(cover_path);
				};
				render_song_browser_ui(browser_view, actions, window_size, ui_scale);
				return;
			}

			if (app.menu_screen() == MenuScreen::Loading)
			{
				cover_textures.stop_song_browser_loading();
				render_song_loading_ui(app.difficulty_select_view(), window_size, ui_scale);
				return;
			}

			if (app.menu_screen() == MenuScreen::InstrumentSelect)
			{
				cover_textures.stop_song_browser_loading();
				InstrumentSelectUiActions actions;
				actions.set_selected_index = [&](int index) { app.set_instrument_selected_index(index); };
				actions.activate_selection = [&]() { app.activate_instrument_selection(); };
				render_instrument_select_ui(app.instrument_select_view(), actions, window_size, ui_scale);
				return;
			}

			cover_textures.stop_song_browser_loading();
			DifficultySelectUiActions actions;
			actions.set_selected_index = [&](int index) { app.set_difficulty_selected_index(index); };
			actions.activate_selection = [&]() { app.activate_difficulty_selection(); };
			render_difficulty_select_ui(app.difficulty_select_view(), actions, window_size, ui_scale);
			return;
		}

		cover_textures.stop_song_browser_loading();
		render_song_player_ui(app.gameplay_snapshot(), window_size);
		if (app.gameplay_paused())
		{
			GameplayPauseUiActions actions;
			actions.set_selected_index = [&](int index) { app.set_gameplay_pause_selected_index(index); };
			actions.activate_selection = [&]() { app.activate_gameplay_pause_selection(); };
			render_gameplay_pause_ui(app.gameplay_snapshot(), app.gameplay_pause_menu_view(), actions, window_size, ui_scale);
		}
	}
}
