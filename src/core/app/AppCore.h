#pragma once

#include "core/app/AppLaunch.h"
#include "core/app/AppTypes.h"
#include "core/menu/DifficultySelectMenu.h"
#include "core/menu/InstrumentSelectMenu.h"
#include "core/menu/SongBrowser.h"
#include "core/play/SongPreloader.h"
#include "core/play/SongSession.h"
#include "frontend_contract/AudioTypes.h"
#include "frontend_contract/FrontendOptions.h"
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
		void set_frontend_options(const ::rhythmreplugged::frontend_contract::FrontendOptions &options);
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
		void drop_song();
		int sample_rate() const override;
		void render_interleaved_s16(std::int16_t *output, size_t frame_count) override;
		size_t gameplay_play_state_serialized_size() const;
		bool serialize_gameplay_play_state(std::vector<std::uint8_t> &bytes, std::string &error_message) const;
		bool deserialize_gameplay_play_state(const std::uint8_t *data, size_t size, std::string &error_message);

		AppMode mode() const;
		MenuScreen menu_screen() const;
		const SongBrowserView &song_browser_view() const;
		const InstrumentSelectView &instrument_select_view() const;
		const DifficultySelectView &difficulty_select_view() const;
		bool gameplay_paused() const;
		const GameplayPauseMenuView &gameplay_pause_menu_view() const;
		const GameplayFrameSnapshot &gameplay_snapshot() const;
		const ::rhythmreplugged::frontend_contract::AudioBatch &audio_batch() const;
		bool set_gameplay_pause_selected_index(int index);
		bool activate_gameplay_pause_selection();

	private:
		bool activate_browser_selection_unlocked();
		bool activate_instrument_selection_unlocked();
		bool activate_difficulty_selection_unlocked();
		bool begin_song_activation(const std::string &selected_song_path, bool allow_auto_start = true);
		void refresh_difficulty_preload_state();
		GameplayOptions make_default_gameplay_options() const;
		bool try_finish_song_preload(std::string &error_message);
		void return_to_song_setup_unlocked();
		void return_to_browser_unlocked();
		void toggle_player_guitar_mute_unlocked();
		bool pressed(bool current, bool previous) const;
		void run_menu(const ::rhythmreplugged::frontend_contract::RetroInputState &input_state);
		void run_song_browser_menu(const ::rhythmreplugged::frontend_contract::RetroInputState &input_state);
		void run_instrument_select_menu(const ::rhythmreplugged::frontend_contract::RetroInputState &input_state);
		void run_difficulty_select_menu(const ::rhythmreplugged::frontend_contract::RetroInputState &input_state);
		void run_loading_menu(const ::rhythmreplugged::frontend_contract::RetroInputState &input_state);
		void run_gameplay(const ::rhythmreplugged::frontend_contract::RetroInputState &input_state);
		void run_gameplay_pause_menu(const ::rhythmreplugged::frontend_contract::RetroInputState &input_state);
		void reset_gameplay_pause_menu();
		void open_gameplay_pause_menu();
		void close_gameplay_pause_menu();

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
		::rhythmreplugged::frontend_contract::FrontendOptions frontend_options_{};
		std::string player_status_message_;
		std::string pending_song_path_;
		MidiChart pending_song_chart_;
		bool pending_song_chart_loaded_ = false;
		GameplayOptions pending_gameplay_options_;
		GameplayPauseMenuView gameplay_pause_menu_;
		bool pending_instrument_selection_required_ = false;
		bool waiting_for_song_preload_ = false;
		bool session_unload_pending_ = false;
		bool audio_batch_enabled_ = false;
		bool restrict_to_startup_song_ = false;
		bool gameplay_paused_ = false;
	};
}
