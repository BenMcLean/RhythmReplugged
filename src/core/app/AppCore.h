#pragma once

#include "core/app/AppLaunch.h"
#include "core/app/AppTypes.h"
#include "core/menu/DifficultySelectMenu.h"
#include "core/menu/InstrumentSelectMenu.h"
#include "core/menu/SongBrowser.h"
#include "core/play/SongPreloader.h"
#include "core/play/SongSession.h"
#include "frontend_contract/AudioTypes.h"
#include "frontend_contract/RetroInput.h"

#include <atomic>
#include <string>

namespace rhythmreplugged::core
{
	class AppCore : public ::rhythmreplugged::frontend_contract::IAudioStream
	{
	public:
		explicit AppCore(::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system);

		bool retro_init(const std::string &song_root_path, std::string &error_message);
		bool retro_init(const AppLaunchRequest &launch_request, std::string &error_message);
		void retro_run(const ::rhythmreplugged::frontend_contract::RetroInputState &input_state);
		void retro_deinit();
		void set_audio_batch_enabled(bool enabled);
		bool set_browser_selected_index(int index);
		bool activate_browser_selection();
		bool set_instrument_selected_index(int index);
		bool activate_instrument_selection();
		bool set_difficulty_selected_index(int index);
		bool activate_difficulty_selection();
		void return_to_browser();
		void toggle_player_guitar_mute();
		void nudge_timing_offset_seconds(double delta_seconds);
		void reset_timing_offset();
		void finalize_audio_stop();
		int sample_rate() const override;
		void render_interleaved_s16(std::int16_t *output, size_t frame_count) override;

		AppMode mode() const;
		MenuScreen menu_screen() const;
		const SongBrowserView &song_browser_view() const;
		const InstrumentSelectView &instrument_select_view() const;
		const DifficultySelectView &difficulty_select_view() const;
		PrototypePlayerView prototype_player_view() const;
		GameplaySceneView gameplay_scene_view() const;
		const ::rhythmreplugged::frontend_contract::AudioBatch &audio_batch() const;

	private:
		bool activate_browser_selection_unlocked();
		bool activate_instrument_selection_unlocked();
		bool activate_difficulty_selection_unlocked();
		bool begin_song_activation(const std::string &selected_song_path);
		void refresh_difficulty_preload_state();
		bool try_finish_song_preload(std::string &error_message);
		void return_to_browser_unlocked();
		void toggle_player_guitar_mute_unlocked();
		void update_player_status_message();
		bool pressed(bool current, bool previous) const;
		void run_menu(const ::rhythmreplugged::frontend_contract::RetroInputState &input_state);
		void run_song_browser_menu(const ::rhythmreplugged::frontend_contract::RetroInputState &input_state);
		void run_instrument_select_menu(const ::rhythmreplugged::frontend_contract::RetroInputState &input_state);
		void run_difficulty_select_menu(const ::rhythmreplugged::frontend_contract::RetroInputState &input_state);
		void run_loading_menu(const ::rhythmreplugged::frontend_contract::RetroInputState &input_state);
		void run_gameplay(const ::rhythmreplugged::frontend_contract::RetroInputState &input_state);

		::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system_;
		SongBrowser song_browser_;
		InstrumentSelectMenu instrument_select_menu_;
		DifficultySelectMenu difficulty_select_menu_;
		SongPreloader song_preloader_;
		SongSession song_session_;
		std::atomic<AppMode> mode_{AppMode::Menu};
		std::atomic<MenuScreen> menu_screen_{MenuScreen::SongBrowser};
		::rhythmreplugged::frontend_contract::RetroInputState previous_input_{};
		::rhythmreplugged::frontend_contract::AudioBatch audio_batch_{};
		std::string player_status_message_;
		std::string pending_song_path_;
		GameplayOptions pending_gameplay_options_;
		bool pending_instrument_selection_required_ = false;
		bool waiting_for_song_preload_ = false;
		bool session_unload_pending_ = false;
		bool audio_batch_enabled_ = false;
	};
}
