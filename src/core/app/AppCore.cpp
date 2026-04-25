#include "core/app/AppCore.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace rhythmreplugged::core
{
	AppCore::AppCore(::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system)
		: file_system_(file_system),
		  song_browser_(file_system),
		  song_preloader_(file_system)
	{
	}

	bool AppCore::retro_init(const std::string &song_root_path, std::string &error_message)
	{
		AppLaunchRequest request;
		request.songs_root_path = song_root_path;
		return retro_init(request, error_message);
	}

	bool AppCore::retro_init(const AppLaunchRequest &launch_request, std::string &error_message)
	{
		mode_ = AppMode::Menu;
		menu_screen_ = MenuScreen::SongBrowser;
		song_session_.unload();
		session_unload_pending_ = false;
		audio_batch_.clear();
		audio_batch_.sample_rate = 0;
		player_status_message_.clear();
		pending_song_path_.clear();
		pending_gameplay_options_ = GameplayOptions{};
		waiting_for_song_preload_ = false;
		song_preloader_.cancel();
		if (launch_request.songs_root_path.empty())
		{
			song_browser_.clear_root("No songs root is configured. Load a folder or song file from your library.");
			return true;
		}

		if (!song_browser_.set_root(launch_request.songs_root_path, error_message))
			return false;

		if (launch_request.startup_song_path.empty())
			return true;

		if (song_session_.load(file_system_, launch_request.startup_song_path, pending_gameplay_options_, error_message))
		{
			mode_ = AppMode::Gameplay;
			session_unload_pending_ = false;
			player_status_message_.clear();
			return true;
		}

		song_browser_.set_status_message(error_message);
		return true;
	}

	void AppCore::retro_run(const ::rhythmreplugged::frontend_contract::RetroInputState &input_state)
	{
		audio_batch_.clear();
		audio_batch_.sample_rate = 0;

		switch (mode_)
		{
		case AppMode::Menu:
			refresh_difficulty_preload_state();
			run_menu(input_state);
			break;
		case AppMode::Gameplay:
			run_gameplay(input_state);
			break;
		}

		if (audio_batch_enabled_ && mode_ == AppMode::Gameplay)
			audio_batch_ = song_session_.render_fixed_tick_audio(kAppFramesPerSecond);

		previous_input_ = input_state;
	}

	void AppCore::retro_deinit()
	{
		mode_ = AppMode::Menu;
		menu_screen_ = MenuScreen::SongBrowser;
		song_session_.unload();
		session_unload_pending_ = false;
		audio_batch_.clear();
		audio_batch_.sample_rate = 0;
		player_status_message_.clear();
		pending_song_path_.clear();
		pending_gameplay_options_ = GameplayOptions{};
		waiting_for_song_preload_ = false;
		song_preloader_.cancel();
	}

	void AppCore::set_audio_batch_enabled(bool enabled)
	{
		audio_batch_enabled_ = enabled;
		if (!enabled)
		{
			audio_batch_.clear();
			audio_batch_.sample_rate = 0;
		}
	}

	bool AppCore::set_browser_selected_index(int index)
	{
		if (mode_ != AppMode::Menu || menu_screen_ != MenuScreen::SongBrowser)
			return false;

		return song_browser_.set_selected_index(index);
	}

	bool AppCore::activate_browser_selection()
	{
		return activate_browser_selection_unlocked();
	}

	bool AppCore::activate_browser_selection_unlocked()
	{
		if (mode_ != AppMode::Menu || menu_screen_ != MenuScreen::SongBrowser)
			return false;

		std::string selected_song_path;
		std::string error_message;
		if (song_browser_.activate_selected(selected_song_path, error_message) && !selected_song_path.empty())
			return begin_song_activation(selected_song_path);

		if (!error_message.empty())
			song_browser_.set_status_message(error_message);

		return selected_song_path.empty();
	}

	bool AppCore::set_difficulty_selected_index(int index)
	{
		if (mode_ != AppMode::Menu || menu_screen_ != MenuScreen::DifficultySelect)
			return false;

		return difficulty_select_menu_.set_selected_index(index);
	}

	bool AppCore::activate_difficulty_selection()
	{
		return activate_difficulty_selection_unlocked();
	}

	bool AppCore::activate_difficulty_selection_unlocked()
	{
		if (mode_ != AppMode::Menu || menu_screen_ != MenuScreen::DifficultySelect || pending_song_path_.empty())
			return false;

		std::string error_message;
		difficulty_select_menu_.apply_to(pending_gameplay_options_);
		if (!try_finish_song_preload(error_message))
		{
			if (error_message.empty())
			{
				waiting_for_song_preload_ = true;
				refresh_difficulty_preload_state();
				return true;
			}

			difficulty_select_menu_.set_status_message(error_message);
			refresh_difficulty_preload_state();
			return false;
		}

		waiting_for_song_preload_ = false;
		mode_ = AppMode::Gameplay;
		session_unload_pending_ = false;
		player_status_message_.clear();
		difficulty_select_menu_.clear_status_message();
		refresh_difficulty_preload_state();
		return true;
	}

	bool AppCore::try_finish_song_preload(std::string &error_message)
	{
		error_message.clear();
		PrototypePlayer::PreloadedSongData preloaded_song_data;
		std::string preloaded_song_path;
		if (!song_preloader_.try_take_ready_data(preloaded_song_path, preloaded_song_data))
		{
			const SongPreloadStatus preload_status = song_preloader_.status();
			if (preload_status.failed)
				error_message = preload_status.error_message.empty() ? "Song preload failed." : preload_status.error_message;
			return false;
		}

		if (preloaded_song_path != pending_song_path_)
			return false;

		return song_session_.load_preloaded(file_system_, pending_song_path_, std::move(preloaded_song_data), pending_gameplay_options_, error_message);
	}

	bool AppCore::begin_song_activation(const std::string &selected_song_path)
	{
		const SongBrowserView &browser = song_browser_.view();
		const SongListItem *selected_entry = nullptr;
		if (browser.selected_index >= 0 && browser.selected_index < static_cast<int>(browser.entries.size()))
			selected_entry = &browser.entries[static_cast<size_t>(browser.selected_index)];

		pending_song_path_ = selected_song_path;
		pending_gameplay_options_ = GameplayOptions{};
		waiting_for_song_preload_ = false;
		song_preloader_.begin(selected_song_path);
		menu_screen_ = MenuScreen::DifficultySelect;
		difficulty_select_menu_.open(
			selected_entry != nullptr ? selected_entry->label : std::string(),
			selected_entry != nullptr ? selected_entry->subtitle : std::string(),
			pending_gameplay_options_);
		refresh_difficulty_preload_state();
		return true;
	}

	void AppCore::refresh_difficulty_preload_state()
	{
		const SongPreloadStatus preload_status = song_preloader_.status();
		const float preload_progress = preload_status.total_bytes == 0
			? 0.0f
			: static_cast<float>(preload_status.processed_bytes) / static_cast<float>(preload_status.total_bytes);
		difficulty_select_menu_.set_preload_progress(
			preload_status.active,
			preload_status.ready,
			waiting_for_song_preload_,
			std::clamp(preload_progress, 0.0f, 1.0f),
			preload_status.processed_bytes,
			preload_status.total_bytes,
			preload_status.completed_stem_count,
			preload_status.total_stem_count);
		if (preload_status.failed && !preload_status.error_message.empty())
			difficulty_select_menu_.set_status_message(preload_status.error_message);
	}

	void AppCore::return_to_browser()
	{
		return_to_browser_unlocked();
	}

	void AppCore::return_to_browser_unlocked()
	{
		mode_ = AppMode::Menu;
		menu_screen_ = MenuScreen::SongBrowser;
		session_unload_pending_ = true;
		player_status_message_.clear();
		pending_song_path_.clear();
		waiting_for_song_preload_ = false;
		song_preloader_.cancel();
	}

	void AppCore::toggle_player_guitar_mute()
	{
		toggle_player_guitar_mute_unlocked();
	}

	void AppCore::toggle_player_guitar_mute_unlocked()
	{
		if (mode_ == AppMode::Gameplay)
			song_session_.toggle_guitar_mute();
	}

	void AppCore::nudge_timing_offset_seconds(double delta_seconds)
	{
		song_session_.set_timing_offset_seconds(song_session_.timing_offset_seconds() + delta_seconds);
		update_player_status_message();
	}

	void AppCore::reset_timing_offset()
	{
		song_session_.set_timing_offset_seconds(0.0);
		update_player_status_message();
	}

	void AppCore::finalize_audio_stop()
	{
		if (!session_unload_pending_)
			return;

		song_session_.unload();
		session_unload_pending_ = false;
	}

	int AppCore::sample_rate() const
	{
		return mode_ == AppMode::Gameplay ? song_session_.sample_rate() : 0;
	}

	AppMode AppCore::mode() const
	{
		return mode_;
	}

	MenuScreen AppCore::menu_screen() const
	{
		return menu_screen_;
	}

	const SongBrowserView &AppCore::song_browser_view() const
	{
		return song_browser_.view();
	}

	const DifficultySelectView &AppCore::difficulty_select_view() const
	{
		return difficulty_select_menu_.view();
	}

	PrototypePlayerView AppCore::prototype_player_view() const
	{
		return song_session_.view(player_status_message_);
	}

	GameplaySceneView AppCore::gameplay_scene_view() const
	{
		GameplaySceneView scene;
		scene.clear_color = {12.0f / 255.0f, 14.0f / 255.0f, 20.0f / 255.0f, 1.0f};

		if (mode_ != AppMode::Gameplay)
			return scene;

		const PrototypePlayerView player = prototype_player_view();
		PlayerGameplayView gameplay_player;
		gameplay_player.normalized_rect = {0.0f, 0.0f, 1.0f, 1.0f};
		gameplay_player.camera = make_default_guitar_camera_view();
		gameplay_player.world.style = make_default_guitar_highway_style_view();

		InstrumentLaneView lane;
		lane.instrument_type = HighwayInstrumentType::FiveFretGuitar;
		lane.instrument_label = player.chart_track_name.empty() ? "Guitar" : player.chart_track_name;
		lane.is_active = true;
		lane.is_muted = player.guitar_muted;
		lane.has_chart = player.has_chart;
		lane.lane_center_x = 0.0f;
		lane.lane_width = 5.0f;
		lane.lane_depth_offset = 0.0f;
		lane.lane_held = player.lane_held;
		lane.lane_sustaining = player.lane_sustaining;
		lane.visible_notes.reserve(player.visible_chart_notes.size());
		for (const PrototypePlayerView::ChartNoteView &note : player.visible_chart_notes)
		{
			HighwayNoteView note_view;
			note_view.lane = note.lane;
			note_view.start_offset_seconds = note.start_offset_seconds;
			note_view.length_seconds = note.length_seconds;
			lane.visible_notes.push_back(note_view);
		}

		lane.visible_measure_lines.reserve(player.visible_measure_lines.size());
		for (const PrototypePlayerView::ChartMeasureLineView &measure_line : player.visible_measure_lines)
		{
			HighwayMeasureLineView measure_line_view;
			measure_line_view.offset_seconds = measure_line.offset_seconds;
			measure_line_view.is_measure = measure_line.is_measure;
			measure_line_view.is_strong = measure_line.is_strong;
			lane.visible_measure_lines.push_back(measure_line_view);
		}

		gameplay_player.world.lanes.push_back(std::move(lane));
		gameplay_player.world.focused_lane_index = 0;
		gameplay_player.world.focus_blend = 1.0f;

		gameplay_player.hud.player_label = player.song_title.empty() ? "Player 1" : player.song_title;
		gameplay_player.hud.status_message = player.status_message;
		gameplay_player.hud.song_time_seconds = player.song_time_seconds;
		gameplay_player.hud.failed = false;

		scene.players.push_back(std::move(gameplay_player));
		return scene;
	}

	const ::rhythmreplugged::frontend_contract::AudioBatch &AppCore::audio_batch() const
	{
		return audio_batch_;
	}

	void AppCore::render_interleaved_s16(std::int16_t *output, size_t frame_count)
	{
		if (output == nullptr)
			return;

		if (mode_ != AppMode::Gameplay)
		{
			std::fill(output, output + frame_count * 2, static_cast<std::int16_t>(0));
			return;
		}

		song_session_.render_interleaved_s16(output, frame_count);
	}

	void AppCore::update_player_status_message()
	{
		std::ostringstream status;
		const long long offset_milliseconds = std::llround(song_session_.timing_offset_seconds() * 1000.0);
		status << "Timing offset: ";
		if (offset_milliseconds >= 0)
			status << '+';
		status << offset_milliseconds << " ms  ([ / ] adjust, \\ reset)";
		player_status_message_ = status.str();
	}

	bool AppCore::pressed(bool current, bool previous) const
	{
		return current && !previous;
	}

	void AppCore::run_menu(const ::rhythmreplugged::frontend_contract::RetroInputState &input_state)
	{
		switch (menu_screen_)
		{
		case MenuScreen::SongBrowser:
			run_song_browser_menu(input_state);
			break;
		case MenuScreen::DifficultySelect:
			run_difficulty_select_menu(input_state);
			break;
		}
	}

	void AppCore::run_song_browser_menu(const ::rhythmreplugged::frontend_contract::RetroInputState &input_state)
	{
		if (pressed(input_state.b, previous_input_.b))
		{
			std::string error_message;
			if (!song_browser_.navigate_to_parent(error_message) && !error_message.empty())
				song_browser_.set_status_message(error_message);
			return;
		}

		if (pressed(input_state.a, previous_input_.a) || pressed(input_state.start, previous_input_.start))
		{
			activate_browser_selection_unlocked();
			return;
		}

		if (pressed(input_state.up, previous_input_.up))
			song_browser_.move_selection(-1);

		if (pressed(input_state.down, previous_input_.down))
			song_browser_.move_selection(1);

		if (pressed(input_state.l, previous_input_.l))
			song_browser_.jump_to_previous_letter();

		if (pressed(input_state.r, previous_input_.r))
			song_browser_.jump_to_next_letter();
	}

	void AppCore::run_difficulty_select_menu(const ::rhythmreplugged::frontend_contract::RetroInputState &input_state)
	{
		if (pressed(input_state.b, previous_input_.b))
		{
			if (waiting_for_song_preload_)
			{
				waiting_for_song_preload_ = false;
				refresh_difficulty_preload_state();
				return;
			}

			menu_screen_ = MenuScreen::SongBrowser;
			pending_song_path_.clear();
			waiting_for_song_preload_ = false;
			song_preloader_.cancel();
			difficulty_select_menu_.clear_status_message();
			refresh_difficulty_preload_state();
			return;
		}

		if (waiting_for_song_preload_)
		{
			std::string error_message;
			if (try_finish_song_preload(error_message))
			{
				waiting_for_song_preload_ = false;
				mode_ = AppMode::Gameplay;
				session_unload_pending_ = false;
				player_status_message_.clear();
				difficulty_select_menu_.clear_status_message();
			}
			else if (!error_message.empty())
			{
				waiting_for_song_preload_ = false;
				difficulty_select_menu_.set_status_message(error_message);
			}

			refresh_difficulty_preload_state();
			return;
		}

		if (pressed(input_state.a, previous_input_.a) || pressed(input_state.start, previous_input_.start))
		{
			activate_difficulty_selection_unlocked();
			return;
		}

		if (pressed(input_state.up, previous_input_.up))
			difficulty_select_menu_.move_selection(-1);

		if (pressed(input_state.down, previous_input_.down))
			difficulty_select_menu_.move_selection(1);
	}

	void AppCore::run_gameplay(const ::rhythmreplugged::frontend_contract::RetroInputState &input_state)
	{
		if (song_session_.playback_finished())
		{
			return_to_browser_unlocked();
			return;
		}

		if (pressed(input_state.b, previous_input_.b))
		{
			return_to_browser_unlocked();
			return;
		}

		const std::array<bool, 5> previous_lane_held = {
			previous_input_.left || previous_input_.lane_1,
			previous_input_.up || previous_input_.lane_2,
			previous_input_.y || previous_input_.lane_3,
			previous_input_.x || previous_input_.lane_4,
			previous_input_.a || previous_input_.lane_5,
		};
		const std::array<bool, 5> lane_held = {
			input_state.left || input_state.lane_1,
			input_state.up || input_state.lane_2,
			input_state.y || input_state.lane_3,
			input_state.x || input_state.lane_4,
			input_state.a || input_state.lane_5,
		};
		const std::array<bool, 5> lane_pressed = {
			pressed(lane_held[0], previous_lane_held[0]),
			pressed(lane_held[1], previous_lane_held[1]),
			pressed(lane_held[2], previous_lane_held[2]),
			pressed(lane_held[3], previous_lane_held[3]),
			pressed(lane_held[4], previous_lane_held[4]),
		};
		song_session_.update_gameplay_input(lane_held, lane_pressed);
	}
}
