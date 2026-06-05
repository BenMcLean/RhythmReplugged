#include "core/app/AppCore.h"

#include "core/chart/MidiChart.h"

#include <algorithm>
#include <optional>
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

		std::vector<DifficultyOption> collect_available_difficulties(
			const MidiChart &chart,
			const std::vector<InstrumentOption> &available_instruments)
		{
			bool has_easy = false;
			bool has_medium = false;
			bool has_hard = false;
			bool has_expert = false;
			for (const InstrumentOption instrument : available_instruments)
			{
				const std::vector<DifficultyOption> difficulties =
					collect_available_difficulties(chart, to_midi_chart_track_type(instrument));
				for (const DifficultyOption difficulty : difficulties)
				{
					switch (difficulty)
					{
					case DifficultyOption::Easy:
						has_easy = true;
						break;
					case DifficultyOption::Medium:
						has_medium = true;
						break;
					case DifficultyOption::Hard:
						has_hard = true;
						break;
					case DifficultyOption::Expert:
						has_expert = true;
						break;
					}
				}
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

		int instrument_sort_rank(InstrumentOption instrument)
		{
			switch (instrument)
			{
			case InstrumentOption::Drums:
				return 0;
			case InstrumentOption::Bass:
				return 1;
			case InstrumentOption::Guitar:
				return 2;
			case InstrumentOption::Rhythm:
				return 3;
			case InstrumentOption::CoopGuitar:
				return 4;
			case InstrumentOption::Keys:
				return 5;
			}

			return 99;
		}

		void sort_instruments_canonical(std::vector<InstrumentOption> &available_instruments)
		{
			std::sort(
				available_instruments.begin(),
				available_instruments.end(),
				[](InstrumentOption left, InstrumentOption right)
				{
					return instrument_sort_rank(left) < instrument_sort_rank(right);
				});
		}

	}

	AppCore::AppCore(::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system)
		: file_system_(file_system),
		  song_browser_(file_system),
		  song_preloader_(file_system)
	{
	}

	void AppCore::set_diagnostic_logger(std::function<void(std::string_view)> logger)
	{
		diagnostic_logger_ = std::move(logger);
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
		song_preloader_.set_multithreaded_file_loading_enabled(frontend_options_.multithreaded_file_loading);
		restrict_to_startup_song_ = launch_request.restrict_to_startup_song;
		mode_ = AppMode::Menu;
		menu_screen_ = MenuScreen::SongBrowser;
		song_session_.unload();
		session_unload_pending_ = false;
		audio_batch_.clear();
		audio_batch_.sample_rate = 0;
		player_status_message_.clear();
		pending_song_path_.clear();
		pending_song_chart_.clear();
		pending_song_chart_loaded_ = false;
		pending_gameplay_options_ = make_default_gameplay_options();
		reset_gameplay_pause_menu();
		gameplay_paused_ = false;
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
		song_preloader_.set_multithreaded_file_loading_enabled(frontend_options_.multithreaded_file_loading);
		if (pending_song_path_.empty())
			pending_gameplay_options_ = make_default_gameplay_options();
	}

	void AppCore::log_diagnostic(std::string_view message) const
	{
		if (diagnostic_logger_ != nullptr && !message.empty())
			diagnostic_logger_(message);
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

		if (mode_ == AppMode::Gameplay && !gameplay_paused_)
			song_session_.refresh_frame_snapshot(player_status_message_);

		if (audio_batch_enabled_ && mode_ == AppMode::Gameplay && !gameplay_paused_)
			audio_batch_ = song_session_.render_fixed_tick_audio(kAppFramesPerSecond);
		else
			audio_batch_.clear();

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
		reset_gameplay_pause_menu();
		gameplay_paused_ = false;
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

		log_diagnostic("instrument-select chart inspect start: song='" + pending_song_path_ + "'");
		instrument_select_menu_.apply_to(pending_gameplay_options_);
		instrument_select_menu_.clear_status_message();
		std::string chart_error_message;
		std::vector<DifficultyOption> available_difficulties;
		if (pending_song_chart_loaded_)
		{
			log_diagnostic("instrument-select chart inspect success");
			std::vector<InstrumentOption> available_instruments;
			for (const MidiChartTrackType track_type : pending_song_chart_.available_preview_track_types())
			{
				const std::optional<InstrumentOption> instrument = to_instrument_option(track_type);
				if (instrument.has_value())
					available_instruments.push_back(*instrument);
			}
			sort_instruments_canonical(available_instruments);

			available_difficulties = pending_gameplay_options_.gameplay_mode() == GameplayMode::Freeplay
				? collect_available_difficulties(pending_song_chart_, available_instruments)
				: collect_available_difficulties(
					pending_song_chart_,
					to_midi_chart_track_type(pending_gameplay_options_.instrument()));
		}
		else
		{
			chart_error_message = "Could not load a playable chart for the selected song.";
			log_diagnostic(chart_error_message.empty() ? std::string_view("instrument-select chart inspect failed with empty error") : std::string_view(chart_error_message));
		}
		difficulty_select_menu_.open(
			instrument_select_menu_.view().song_title,
			instrument_select_menu_.view().song_subtitle,
			available_difficulties,
			pending_gameplay_options_);
		if (!chart_error_message.empty())
		{
			difficulty_select_menu_.set_status_message(chart_error_message);
			log_diagnostic(chart_error_message);
		}
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

		log_diagnostic("difficulty-select gameplay start attempt: song='" + pending_song_path_ + "'");
		std::string error_message;
		difficulty_select_menu_.apply_to(pending_gameplay_options_);
		if (song_session_.is_loaded())
		{
			if (!song_session_.reconfigure_loaded(pending_song_chart_, pending_gameplay_options_, error_message))
			{
				log_diagnostic(error_message.empty() ? std::string_view("Song restart failed.") : std::string_view(error_message));
				difficulty_select_menu_.set_status_message(error_message.empty() ? "Song restart failed." : error_message);
				refresh_difficulty_preload_state();
				return false;
			}

			waiting_for_song_preload_ = false;
			mode_ = AppMode::Gameplay;
			session_unload_pending_ = false;
			close_gameplay_pause_menu();
			player_status_message_.clear();
			difficulty_select_menu_.clear_status_message();
			refresh_difficulty_preload_state();
			return true;
		}

		if (!try_finish_song_preload(error_message))
		{
			if (error_message.empty())
			{
				log_diagnostic("difficulty-select gameplay waiting for preload");
				waiting_for_song_preload_ = true;
				menu_screen_ = MenuScreen::Loading;
				refresh_difficulty_preload_state();
				return true;
			}

			log_diagnostic(error_message);
			difficulty_select_menu_.set_status_message(error_message);
			refresh_difficulty_preload_state();
			return false;
		}

		log_diagnostic("difficulty-select gameplay start success");
		waiting_for_song_preload_ = false;
		mode_ = AppMode::Gameplay;
		session_unload_pending_ = false;
		close_gameplay_pause_menu();
		player_status_message_.clear();
		difficulty_select_menu_.clear_status_message();
		refresh_difficulty_preload_state();
		return true;
	}

	bool AppCore::try_finish_song_preload(std::string &error_message)
	{
		error_message.clear();
		SongPlayer::PreloadedSongData preloaded_song_data;
		std::string preloaded_song_path;
		if (!song_preloader_.try_take_ready_data(preloaded_song_path, preloaded_song_data))
		{
			const SongPreloadStatus preload_status = song_preloader_.status();
			if (preload_status.failed)
				error_message = preload_status.error_message.empty() ? "Song preload failed." : preload_status.error_message;
			return false;
		}

		if (preloaded_song_path != pending_song_path_)
		{
			log_diagnostic("try_finish_song_preload ignored stale preload result");
			return false;
		}

		const bool loaded = song_session_.load_preloaded(
			file_system_,
			pending_song_path_,
			pending_song_chart_,
			std::move(preloaded_song_data),
			pending_gameplay_options_,
			error_message);
		if (loaded)
			log_diagnostic("try_finish_song_preload: SongSession load_preloaded succeeded");
		else
			log_diagnostic(error_message.empty() ? std::string_view("try_finish_song_preload: SongSession load_preloaded failed with empty error") : std::string_view(error_message));
		return loaded;
	}

	bool AppCore::begin_song_activation(const std::string &selected_song_path, bool allow_auto_start)
	{
		const SongBrowserView &browser = song_browser_.view();
		const SongListItem *selected_entry = nullptr;
		if (browser.selected_index >= 0 && browser.selected_index < static_cast<int>(browser.entries.size()))
			selected_entry = &browser.entries[static_cast<size_t>(browser.selected_index)];
		const std::string previous_song_path = pending_song_path_;

		pending_song_path_ = selected_song_path;
		pending_song_chart_.clear();
		pending_song_chart_loaded_ = false;
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
		std::string chart_error_message;
		log_diagnostic("song-activation chart inspect start: song='" + selected_song_path + "'");
		pending_song_chart_loaded_ = pending_song_chart_.load(
			file_system_,
			selected_song_path,
			MidiChartDifficulty::Medium,
			to_midi_chart_track_type(pending_gameplay_options_.instrument()),
			chart_error_message);
		if (pending_song_chart_loaded_)
		{
			log_diagnostic("song-activation chart inspect success");
			for (const MidiChartTrackType track_type : pending_song_chart_.available_preview_track_types())
			{
				const std::optional<InstrumentOption> instrument = to_instrument_option(track_type);
				if (instrument.has_value())
					available_instruments.push_back(*instrument);
			}
			sort_instruments_canonical(available_instruments);
		}
		else
		{
			log_diagnostic(chart_error_message.empty() ? std::string_view("song-activation chart inspect failed with empty error") : std::string_view(chart_error_message));
		}

		const std::optional<InstrumentOption> requested_instrument =
			parse_instrument_option(frontend_options_.default_instrument);
		if (available_instruments.size() == 1)
		{
			pending_gameplay_options_.set_gameplay_mode(GameplayMode::Classic);
			pending_gameplay_options_.set_instrument(available_instruments.front());
		}
		else if (requested_instrument.has_value() && contains_instrument(available_instruments, *requested_instrument))
		{
			pending_gameplay_options_.set_gameplay_mode(GameplayMode::Classic);
			pending_gameplay_options_.set_instrument(*requested_instrument);
		}

		available_difficulties = pending_gameplay_options_.gameplay_mode() == GameplayMode::Freeplay
			? collect_available_difficulties(pending_song_chart_, available_instruments)
			: collect_available_difficulties(
				pending_song_chart_,
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
		if (!chart_error_message.empty())
		{
			instrument_select_menu_.set_status_message(chart_error_message);
			log_diagnostic(chart_error_message);
		}

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
			if (!chart_error_message.empty())
			{
				difficulty_select_menu_.set_status_message(chart_error_message);
				log_diagnostic(chart_error_message);
			}
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
		close_gameplay_pause_menu();
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
		close_gameplay_pause_menu();
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

	void AppCore::drop_song()
	{
		if (!session_unload_pending_)
			return;

		song_session_.unload();
		pending_song_chart_.clear();
		pending_song_chart_loaded_ = false;
		session_unload_pending_ = false;
	}

	int AppCore::sample_rate() const
	{
		if (const int gameplay_sample_rate = song_session_.sample_rate(); gameplay_sample_rate > 0)
			return gameplay_sample_rate;

		const SongPreloadStatus preload_status = song_preloader_.status();
		return preload_status.sample_rate;
	}

	size_t AppCore::gameplay_play_state_serialized_size() const
	{
		return mode_ == AppMode::Gameplay ? song_session_.play_state_serialized_size() : 0;
	}

	bool AppCore::serialize_gameplay_play_state(std::vector<std::uint8_t> &bytes, std::string &error_message) const
	{
		if (mode_ != AppMode::Gameplay)
		{
			bytes.clear();
			error_message = "Gameplay is not active.";
			return false;
		}

		return song_session_.serialize_play_state(bytes, error_message);
	}

	bool AppCore::deserialize_gameplay_play_state(const std::uint8_t *data, size_t size, std::string &error_message)
	{
		if (mode_ != AppMode::Gameplay)
		{
			error_message = "Gameplay is not active.";
			return false;
		}

		return song_session_.deserialize_play_state(data, size, error_message);
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

	bool AppCore::gameplay_paused() const
	{
		return mode_ == AppMode::Gameplay && gameplay_paused_;
	}

	const GameplayPauseMenuView &AppCore::gameplay_pause_menu_view() const
	{
		return gameplay_pause_menu_;
	}

	const GameplayFrameSnapshot &AppCore::gameplay_snapshot() const
	{
		return song_session_.frame_snapshot();
	}

	const ::rhythmreplugged::frontend_contract::AudioBatch &AppCore::audio_batch() const
	{
		return audio_batch_;
	}

	void AppCore::render_interleaved_s16(std::int16_t *output, size_t frame_count)
	{
		if (output == nullptr)
			return;

		if (mode_ != AppMode::Gameplay || gameplay_paused_)
		{
			std::fill(output, output + frame_count * 2, static_cast<std::int16_t>(0));
			return;
		}

		song_session_.render_interleaved_s16(output, frame_count);
	}

	bool AppCore::set_gameplay_pause_selected_index(int index)
	{
		if (!gameplay_paused_)
			return false;

		gameplay_pause_menu_.selected_index = std::clamp(index, 0, 1);
		return true;
	}

	bool AppCore::activate_gameplay_pause_selection()
	{
		if (!gameplay_paused_)
			return false;

		if (gameplay_pause_menu_.selected_index <= 0)
		{
			close_gameplay_pause_menu();
			return true;
		}

		return_to_song_setup_unlocked();
		return true;
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
		song_browser_.update();

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
				log_diagnostic("difficulty-select preload finished, entering gameplay");
				waiting_for_song_preload_ = false;
				mode_ = AppMode::Gameplay;
				session_unload_pending_ = false;
				close_gameplay_pause_menu();
				player_status_message_.clear();
				difficulty_select_menu_.clear_status_message();
			}
			else if (!error_message.empty())
			{
				log_diagnostic(error_message);
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
			log_diagnostic("loading-menu preload finished, entering gameplay");
			waiting_for_song_preload_ = false;
			mode_ = AppMode::Gameplay;
			session_unload_pending_ = false;
			close_gameplay_pause_menu();
			player_status_message_.clear();
			difficulty_select_menu_.clear_status_message();
			refresh_difficulty_preload_state();
			return;
		}

		if (!error_message.empty())
		{
			log_diagnostic(error_message);
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

		if (gameplay_paused_)
		{
			run_gameplay_pause_menu(input_state);
			return;
		}

		if (pressed(input_state.start, previous_input_.start))
		{
			open_gameplay_pause_menu();
			return;
		}

		if (pressed(input_state.b, previous_input_.b))
		{
			return_to_song_setup_unlocked();
			return;
		}

		if (pressed(input_state.l, previous_input_.l))
			song_session_.switch_active_lane(-1);

		if (pressed(input_state.r, previous_input_.r))
			song_session_.switch_active_lane(1);

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

	void AppCore::run_gameplay_pause_menu(const ::rhythmreplugged::frontend_contract::RetroInputState &input_state)
	{
		if (pressed(input_state.b, previous_input_.b))
		{
			close_gameplay_pause_menu();
			return;
		}

		if (pressed(input_state.a, previous_input_.a) || pressed(input_state.start, previous_input_.start))
		{
			activate_gameplay_pause_selection();
			return;
		}

		if (pressed(input_state.up, previous_input_.up))
			set_gameplay_pause_selected_index(gameplay_pause_menu_.selected_index - 1);

		if (pressed(input_state.down, previous_input_.down))
			set_gameplay_pause_selected_index(gameplay_pause_menu_.selected_index + 1);
	}

	void AppCore::reset_gameplay_pause_menu()
	{
		gameplay_pause_menu_ = {};
	}

	void AppCore::open_gameplay_pause_menu()
	{
		gameplay_paused_ = true;
		reset_gameplay_pause_menu();
	}

	void AppCore::close_gameplay_pause_menu()
	{
		gameplay_paused_ = false;
		reset_gameplay_pause_menu();
	}
}
