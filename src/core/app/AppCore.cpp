#include "core/app/AppCore.h"

#include "core/chart/MidiChart.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <sstream>
#include <string_view>

namespace rhythmreplugged::core
{
	namespace
	{
		MidiChartTrackType to_midi_chart_track_type(InstrumentOption instrument)
		{
			switch (instrument)
			{
			case InstrumentOption::Guitar:
				return MidiChartTrackType::FiveFretGuitar;
			case InstrumentOption::Bass:
				return MidiChartTrackType::FiveFretBass;
			case InstrumentOption::Rhythm:
				return MidiChartTrackType::FiveFretRhythm;
			case InstrumentOption::CoopGuitar:
				return MidiChartTrackType::FiveFretCoop;
			case InstrumentOption::Keys:
				return MidiChartTrackType::FiveFretKeys;
			case InstrumentOption::Drums:
				return MidiChartTrackType::Drums;
			}

			return MidiChartTrackType::FiveFretGuitar;
		}

		std::optional<InstrumentOption> to_instrument_option(MidiChartTrackType track_type)
		{
			switch (track_type)
			{
			case MidiChartTrackType::FiveFretGuitar:
				return InstrumentOption::Guitar;
			case MidiChartTrackType::FiveFretBass:
				return InstrumentOption::Bass;
			case MidiChartTrackType::FiveFretRhythm:
				return InstrumentOption::Rhythm;
			case MidiChartTrackType::FiveFretCoop:
				return InstrumentOption::CoopGuitar;
			case MidiChartTrackType::FiveFretKeys:
				return InstrumentOption::Keys;
			case MidiChartTrackType::Drums:
				return InstrumentOption::Drums;
			default:
				return std::nullopt;
			}
		}

		std::optional<InstrumentOption> parse_instrument_option(std::string_view value)
		{
			if (value == "guitar")
				return InstrumentOption::Guitar;
			if (value == "bass")
				return InstrumentOption::Bass;
			if (value == "rhythm")
				return InstrumentOption::Rhythm;
			if (value == "coop-guitar")
				return InstrumentOption::CoopGuitar;
			if (value == "keys")
				return InstrumentOption::Keys;
			if (value == "drums")
				return InstrumentOption::Drums;

			return std::nullopt;
		}

		std::optional<DifficultyOption> parse_difficulty_option(std::string_view value)
		{
			if (value == "easy")
				return DifficultyOption::Easy;
			if (value == "medium")
				return DifficultyOption::Medium;
			if (value == "hard")
				return DifficultyOption::Hard;
			if (value == "expert")
				return DifficultyOption::Expert;

			return std::nullopt;
		}

		std::vector<DifficultyOption> collect_available_difficulties(const MidiChart &chart, MidiChartTrackType track_type)
		{
			bool has_easy = false;
			bool has_medium = false;
			bool has_hard = false;
			bool has_expert = false;
			for (const MidiChartTrack &track : chart.tracks())
			{
				if (track.type != track_type)
					continue;

				for (const MidiChartParsedNote &note : track.parsed_notes)
				{
					const bool is_supported_note =
						(track_type == MidiChartTrackType::Drums && note.category == MidiChartNoteCategory::Drums && note.lane >= 0 && note.lane <= 5) ||
						(track_type != MidiChartTrackType::Drums && note.category == MidiChartNoteCategory::FiveFret && note.lane >= 1 && note.lane <= 5);
					if (!is_supported_note)
						continue;

					switch (note.difficulty)
					{
					case MidiChartDifficulty::Easy:
						has_easy = true;
						break;
					case MidiChartDifficulty::Medium:
						has_medium = true;
						break;
					case MidiChartDifficulty::Hard:
						has_hard = true;
						break;
					case MidiChartDifficulty::Expert:
						has_expert = true;
						break;
					default:
						break;
					}
				}
				break;
			}

			std::vector<DifficultyOption> difficulties;
			if (has_easy)
				difficulties.push_back(DifficultyOption::Easy);
			if (has_medium)
				difficulties.push_back(DifficultyOption::Medium);
			if (has_hard)
				difficulties.push_back(DifficultyOption::Hard);
			if (has_expert)
				difficulties.push_back(DifficultyOption::Expert);
			return difficulties;
		}

		bool contains_instrument(const std::vector<InstrumentOption> &available_instruments, InstrumentOption instrument)
		{
			return std::find(available_instruments.begin(), available_instruments.end(), instrument) != available_instruments.end();
		}

		bool contains_difficulty(const std::vector<DifficultyOption> &available_difficulties, DifficultyOption difficulty)
		{
			return std::find(available_difficulties.begin(), available_difficulties.end(), difficulty) != available_difficulties.end();
		}
	}

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
		frontend_options_ = launch_request.frontend_options;
		restrict_to_startup_song_ = launch_request.restrict_to_startup_song;
		mode_ = AppMode::Menu;
		menu_screen_ = MenuScreen::SongBrowser;
		song_session_.unload();
		session_unload_pending_ = false;
		audio_batch_.clear();
		audio_batch_.sample_rate = 0;
		player_status_message_.clear();
		pending_song_path_.clear();
		pending_gameplay_options_ = make_default_gameplay_options();
		pending_instrument_selection_required_ = false;
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

		if (!begin_song_activation(launch_request.startup_song_path))
		{
			if (restrict_to_startup_song_)
			{
				error_message = "Could not open the selected song.";
				return false;
			}

			song_browser_.set_status_message("Could not open the selected song.");
			return true;
		}

		return true;
	}

	void AppCore::set_frontend_options(const ::rhythmreplugged::frontend_contract::FrontendOptions &options)
	{
		frontend_options_ = options;
		if (pending_song_path_.empty())
			pending_gameplay_options_ = make_default_gameplay_options();
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
		pending_gameplay_options_ = make_default_gameplay_options();
		pending_instrument_selection_required_ = false;
		waiting_for_song_preload_ = false;
		song_preloader_.cancel();
		restrict_to_startup_song_ = false;
	}

	GameplayOptions AppCore::make_default_gameplay_options() const
	{
		GameplayOptions options;
		if (const std::optional<InstrumentOption> instrument = parse_instrument_option(frontend_options_.default_instrument))
			options.set_instrument(*instrument);
		if (const std::optional<DifficultyOption> difficulty = parse_difficulty_option(frontend_options_.default_difficulty))
			options.set_difficulty(*difficulty);
		return options;
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

	bool AppCore::set_instrument_selected_index(int index)
	{
		if (mode_ != AppMode::Menu || menu_screen_ != MenuScreen::InstrumentSelect)
			return false;

		return instrument_select_menu_.set_selected_index(index);
	}

	bool AppCore::activate_instrument_selection()
	{
		return activate_instrument_selection_unlocked();
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

	bool AppCore::activate_instrument_selection_unlocked()
	{
		if (mode_ != AppMode::Menu || menu_screen_ != MenuScreen::InstrumentSelect || pending_song_path_.empty())
			return false;

		instrument_select_menu_.apply_to(pending_gameplay_options_);
		instrument_select_menu_.clear_status_message();
		MidiChart inspect_chart;
		std::string chart_error_message;
		std::vector<DifficultyOption> available_difficulties;
		if (inspect_chart.load(
			file_system_,
			pending_song_path_,
			MidiChartDifficulty::Medium,
			to_midi_chart_track_type(pending_gameplay_options_.instrument()),
			chart_error_message))
		{
			available_difficulties = collect_available_difficulties(
				inspect_chart,
				to_midi_chart_track_type(pending_gameplay_options_.instrument()));
		}
		difficulty_select_menu_.open(
			instrument_select_menu_.view().song_title,
			instrument_select_menu_.view().song_subtitle,
			available_difficulties,
			pending_gameplay_options_);
		menu_screen_ = MenuScreen::DifficultySelect;
		refresh_difficulty_preload_state();
		return true;
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
		if (song_session_.is_loaded())
		{
			if (!song_session_.reconfigure_loaded(file_system_, pending_song_path_, pending_gameplay_options_, error_message))
			{
				difficulty_select_menu_.set_status_message(error_message.empty() ? "Song restart failed." : error_message);
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

		if (!try_finish_song_preload(error_message))
		{
			if (error_message.empty())
			{
				waiting_for_song_preload_ = true;
				menu_screen_ = MenuScreen::Loading;
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

	bool AppCore::begin_song_activation(const std::string &selected_song_path, bool allow_auto_start)
	{
		const SongBrowserView &browser = song_browser_.view();
		const SongListItem *selected_entry = nullptr;
		if (browser.selected_index >= 0 && browser.selected_index < static_cast<int>(browser.entries.size()))
			selected_entry = &browser.entries[static_cast<size_t>(browser.selected_index)];
		const std::string previous_song_path = pending_song_path_;

		pending_song_path_ = selected_song_path;
		pending_gameplay_options_ = make_default_gameplay_options();
		pending_instrument_selection_required_ = false;
		waiting_for_song_preload_ = false;
		const bool can_reuse_loaded_song =
			song_session_.is_loaded() &&
			!previous_song_path.empty() &&
			previous_song_path == selected_song_path;
		if (can_reuse_loaded_song)
			song_preloader_.cancel();
		else
			song_preloader_.begin(selected_song_path);
		const std::string song_title = selected_entry != nullptr ? selected_entry->label : std::string();
		const std::string song_subtitle = selected_entry != nullptr ? selected_entry->subtitle : std::string();

		std::vector<InstrumentOption> available_instruments;
		std::vector<DifficultyOption> available_difficulties;
		MidiChart inspect_chart;
		std::string chart_error_message;
		if (inspect_chart.load(
			file_system_,
			selected_song_path,
			MidiChartDifficulty::Medium,
			to_midi_chart_track_type(pending_gameplay_options_.instrument()),
			chart_error_message))
		{
			for (const MidiChartTrackType track_type : inspect_chart.available_preview_track_types())
			{
				const std::optional<InstrumentOption> instrument = to_instrument_option(track_type);
				if (instrument.has_value())
					available_instruments.push_back(*instrument);
			}
		}

		const std::optional<InstrumentOption> requested_instrument =
			parse_instrument_option(frontend_options_.default_instrument);
		if (available_instruments.size() == 1)
			pending_gameplay_options_.set_instrument(available_instruments.front());
		else if (requested_instrument.has_value() && contains_instrument(available_instruments, *requested_instrument))
			pending_gameplay_options_.set_instrument(*requested_instrument);

		available_difficulties = collect_available_difficulties(
			inspect_chart,
			to_midi_chart_track_type(pending_gameplay_options_.instrument()));
		const std::optional<DifficultyOption> requested_difficulty =
			parse_difficulty_option(frontend_options_.default_difficulty);
		if (available_difficulties.size() == 1)
			pending_gameplay_options_.set_difficulty(available_difficulties.front());
		else if (requested_difficulty.has_value() && contains_difficulty(available_difficulties, *requested_difficulty))
			pending_gameplay_options_.set_difficulty(*requested_difficulty);

		const bool should_show_instrument_menu =
			available_instruments.size() > 1 &&
			(!requested_instrument.has_value() || !contains_instrument(available_instruments, *requested_instrument));
		const bool should_show_difficulty_menu =
			available_difficulties.size() > 1 &&
			(!requested_difficulty.has_value() || !contains_difficulty(available_difficulties, *requested_difficulty));

		instrument_select_menu_.open(song_title, song_subtitle, available_instruments, pending_gameplay_options_);

		if (!allow_auto_start)
		{
			menu_screen_ = MenuScreen::InstrumentSelect;
			refresh_difficulty_preload_state();
			return true;
		}

		if (should_show_instrument_menu)
		{
			pending_instrument_selection_required_ = true;
			menu_screen_ = MenuScreen::InstrumentSelect;
		}
		else
		{
			difficulty_select_menu_.open(song_title, song_subtitle, available_difficulties, pending_gameplay_options_);
			if (should_show_difficulty_menu)
			{
				menu_screen_ = MenuScreen::DifficultySelect;
			}
			else if (allow_auto_start)
			{
				menu_screen_ = MenuScreen::DifficultySelect;
				activate_difficulty_selection_unlocked();
			}
			else
			{
				menu_screen_ = MenuScreen::DifficultySelect;
			}
		}
		refresh_difficulty_preload_state();
		return true;
	}

	void AppCore::refresh_difficulty_preload_state()
	{
		const SongPreloadStatus preload_status = song_preloader_.status();
		float preload_progress = 0.0f;
		if (preload_status.phase == PreloadPhase::Reading)
		{
			preload_progress = preload_status.total_read_file_count == 0
				? 0.0f
				: static_cast<float>(preload_status.completed_read_file_count) / static_cast<float>(preload_status.total_read_file_count);
		}
		else if (preload_status.phase == PreloadPhase::Decoding || preload_status.phase == PreloadPhase::Ready)
		{
			preload_progress = preload_status.total_bytes == 0
				? 0.0f
				: static_cast<float>(preload_status.processed_bytes) / static_cast<float>(preload_status.total_bytes);
		}

		instrument_select_menu_.set_preload_progress(
			preload_status.phase,
			std::clamp(preload_progress, 0.0f, 1.0f),
			preload_status.processed_bytes,
			preload_status.total_bytes,
			preload_status.completed_stem_count,
			preload_status.total_stem_count,
			preload_status.completed_read_file_count,
			preload_status.total_read_file_count);
		difficulty_select_menu_.set_preload_progress(
			preload_status.phase,
			std::clamp(preload_progress, 0.0f, 1.0f),
			preload_status.processed_bytes,
			preload_status.total_bytes,
			preload_status.completed_stem_count,
			preload_status.total_stem_count,
			preload_status.completed_read_file_count,
			preload_status.total_read_file_count);
		if (preload_status.failed && !preload_status.error_message.empty())
		{
			instrument_select_menu_.set_status_message(preload_status.error_message);
			difficulty_select_menu_.set_status_message(preload_status.error_message);
		}
	}

	void AppCore::return_to_song_setup_unlocked()
	{
		mode_ = AppMode::Menu;
		session_unload_pending_ = false;
		player_status_message_.clear();
		waiting_for_song_preload_ = false;
		song_preloader_.cancel();

		const std::string song_path = pending_song_path_;
		if (song_path.empty() || begin_song_activation(song_path, false))
			return;

		menu_screen_ = pending_instrument_selection_required_
			? MenuScreen::InstrumentSelect
			: MenuScreen::DifficultySelect;
		instrument_select_menu_.set_status_message("Could not reopen the selected song.");
		difficulty_select_menu_.set_status_message("Could not reopen the selected song.");
	}

	void AppCore::return_to_browser()
	{
		return_to_browser_unlocked();
	}

	void AppCore::return_to_browser_unlocked()
	{
		if (restrict_to_startup_song_)
		{
			return_to_song_setup_unlocked();
			return;
		}

		mode_ = AppMode::Menu;
		menu_screen_ = MenuScreen::SongBrowser;
		session_unload_pending_ = true;
		player_status_message_.clear();
		pending_song_path_.clear();
		pending_instrument_selection_required_ = false;
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
		if (const int gameplay_sample_rate = song_session_.sample_rate(); gameplay_sample_rate > 0)
			return gameplay_sample_rate;

		const SongPreloadStatus preload_status = song_preloader_.status();
		return preload_status.sample_rate;
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

	const InstrumentSelectView &AppCore::instrument_select_view() const
	{
		return instrument_select_menu_.view();
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
		lane.instrument_type = player.chart_track_name == "Drums"
			? HighwayInstrumentType::FiveLaneDrums
			: HighwayInstrumentType::FiveFretGuitar;
		lane.instrument_label = player.chart_track_name.empty() ? "Guitar" : player.chart_track_name;
		lane.is_active = true;
		lane.is_muted = player.playable_stem_muted;
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
		case MenuScreen::InstrumentSelect:
			run_instrument_select_menu(input_state);
			break;
		case MenuScreen::DifficultySelect:
			run_difficulty_select_menu(input_state);
			break;
		case MenuScreen::Loading:
			run_loading_menu(input_state);
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

		for (size_t index = 0; index < input_state.letter_keys.size(); ++index)
		{
			if (!pressed(input_state.letter_keys[index], previous_input_.letter_keys[index]))
				continue;

			song_browser_.jump_to_letter(static_cast<char>('A' + index));
			break;
		}

		if (pressed(input_state.l, previous_input_.l))
			song_browser_.jump_to_previous_letter();

		if (pressed(input_state.r, previous_input_.r))
			song_browser_.jump_to_next_letter();
	}

	void AppCore::run_instrument_select_menu(const ::rhythmreplugged::frontend_contract::RetroInputState &input_state)
	{
		if (pressed(input_state.b, previous_input_.b))
		{
			if (restrict_to_startup_song_)
			{
				instrument_select_menu_.clear_status_message();
				return;
			}

			return_to_browser_unlocked();
			instrument_select_menu_.clear_status_message();
			return;
		}

		if (pressed(input_state.a, previous_input_.a) || pressed(input_state.start, previous_input_.start))
		{
			activate_instrument_selection_unlocked();
			return;
		}

		if (pressed(input_state.up, previous_input_.up))
			instrument_select_menu_.move_selection(-1);

		if (pressed(input_state.down, previous_input_.down))
			instrument_select_menu_.move_selection(1);
	}

	void AppCore::run_difficulty_select_menu(const ::rhythmreplugged::frontend_contract::RetroInputState &input_state)
	{
		if (pressed(input_state.b, previous_input_.b))
		{
			menu_screen_ = MenuScreen::InstrumentSelect;
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

	void AppCore::run_loading_menu(const ::rhythmreplugged::frontend_contract::RetroInputState &input_state)
	{
		if (pressed(input_state.b, previous_input_.b))
		{
			waiting_for_song_preload_ = false;
			menu_screen_ = MenuScreen::DifficultySelect;
			refresh_difficulty_preload_state();
			return;
		}

		std::string error_message;
		if (try_finish_song_preload(error_message))
		{
			waiting_for_song_preload_ = false;
			mode_ = AppMode::Gameplay;
			session_unload_pending_ = false;
			player_status_message_.clear();
			difficulty_select_menu_.clear_status_message();
			refresh_difficulty_preload_state();
			return;
		}

		if (!error_message.empty())
		{
			waiting_for_song_preload_ = false;
			menu_screen_ = MenuScreen::DifficultySelect;
			difficulty_select_menu_.set_status_message(error_message);
			refresh_difficulty_preload_state();
		}
	}

	void AppCore::run_gameplay(const ::rhythmreplugged::frontend_contract::RetroInputState &input_state)
	{
		if (song_session_.playback_finished())
		{
			return_to_song_setup_unlocked();
			return;
		}

		if (pressed(input_state.b, previous_input_.b))
		{
			return_to_song_setup_unlocked();
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
