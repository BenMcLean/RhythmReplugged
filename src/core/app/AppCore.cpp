#include "core/app/AppCore.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace rhythmreplugged
{
	AppCore::AppCore(IRetroFileSystem &file_system)
		: file_system_(file_system),
		  song_browser_(file_system)
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
		mode_ = AppMode::SongBrowser;
		song_session_.unload();
		session_unload_pending_ = false;
		audio_batch_.clear();
		audio_batch_.sample_rate = 0;
		player_status_message_.clear();
		if (launch_request.songs_root_path.empty())
		{
			song_browser_.clear_root("No songs root is configured. Load a folder or song file from your library.");
			return true;
		}

		if (!song_browser_.set_root(launch_request.songs_root_path, error_message))
			return false;

		if (launch_request.startup_song_path.empty())
			return true;

		if (song_session_.load(file_system_, launch_request.startup_song_path, error_message))
		{
			mode_ = AppMode::PrototypePlayer;
			session_unload_pending_ = false;
			player_status_message_.clear();
			return true;
		}

		song_browser_.set_status_message(error_message);
		return true;
	}

	void AppCore::retro_run(const RetroInputState &input_state)
	{
		audio_batch_.clear();
		audio_batch_.sample_rate = 0;

		switch (mode_)
		{
		case AppMode::SongBrowser:
			run_song_browser(input_state);
			break;
		case AppMode::PrototypePlayer:
			run_prototype_player(input_state);
			break;
		}

		if (audio_batch_enabled_ && mode_ == AppMode::PrototypePlayer)
			audio_batch_ = song_session_.render_fixed_tick_audio(kAppFramesPerSecond);

		previous_input_ = input_state;
	}

	void AppCore::retro_deinit()
	{
		mode_ = AppMode::SongBrowser;
		song_session_.unload();
		session_unload_pending_ = false;
		audio_batch_.clear();
		audio_batch_.sample_rate = 0;
		player_status_message_.clear();
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
		if (mode_ != AppMode::SongBrowser)
			return false;

		return song_browser_.set_selected_index(index);
	}

	bool AppCore::activate_browser_selection()
	{
		return activate_browser_selection_unlocked();
	}

	bool AppCore::activate_browser_selection_unlocked()
	{
		if (mode_ != AppMode::SongBrowser)
			return false;

		std::string selected_song_path;
		std::string error_message;
		if (song_browser_.activate_selected(selected_song_path, error_message) && !selected_song_path.empty())
		{
			if (song_session_.load(file_system_, selected_song_path, error_message))
			{
				mode_ = AppMode::PrototypePlayer;
				session_unload_pending_ = false;
				player_status_message_.clear();
				return true;
			}

			song_browser_.set_status_message(error_message);
			return false;
		}

		if (!error_message.empty())
			song_browser_.set_status_message(error_message);

		return selected_song_path.empty();
	}

	void AppCore::return_to_browser()
	{
		return_to_browser_unlocked();
	}

	void AppCore::return_to_browser_unlocked()
	{
		mode_ = AppMode::SongBrowser;
		session_unload_pending_ = true;
		player_status_message_.clear();
	}

	void AppCore::toggle_player_guitar_mute()
	{
		toggle_player_guitar_mute_unlocked();
	}

	void AppCore::toggle_player_guitar_mute_unlocked()
	{
		if (mode_ == AppMode::PrototypePlayer)
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
		return mode_ == AppMode::PrototypePlayer ? song_session_.sample_rate() : 0;
	}

	AppMode AppCore::mode() const
	{
		return mode_;
	}

	const SongBrowserView &AppCore::song_browser_view() const
	{
		return song_browser_.view();
	}

	PrototypePlayerView AppCore::prototype_player_view() const
	{
		return song_session_.view(player_status_message_);
	}

	GameplaySceneView AppCore::gameplay_scene_view() const
	{
		GameplaySceneView scene;
		scene.clear_color = {12.0f / 255.0f, 14.0f / 255.0f, 20.0f / 255.0f, 1.0f};

		if (mode_ != AppMode::PrototypePlayer)
			return scene;

		const PrototypePlayerView player = prototype_player_view();
		PlayerGameplayView gameplay_player;
		gameplay_player.normalized_rect = {0.0f, 0.0f, 1.0f, 1.0f};
		gameplay_player.camera.field_of_view_degrees = 55.0f;
		gameplay_player.camera.pitch_degrees = 17.0f;
		gameplay_player.camera.camera_height = 2.0f;
		gameplay_player.camera.camera_distance = 4.5f;
		gameplay_player.camera.visible_depth_seconds = 1.5f;
		gameplay_player.camera.curve_amount = 0.0f;

		HighwayStyleView style;
		style.lane_colors[0] = {90.0f / 255.0f, 197.0f / 255.0f, 92.0f / 255.0f, 1.0f};
		style.lane_colors[1] = {210.0f / 255.0f, 62.0f / 255.0f, 62.0f / 255.0f, 1.0f};
		style.lane_colors[2] = {226.0f / 255.0f, 209.0f / 255.0f, 63.0f / 255.0f, 1.0f};
		style.lane_colors[3] = {65.0f / 255.0f, 117.0f / 255.0f, 220.0f / 255.0f, 1.0f};
		style.lane_colors[4] = {234.0f / 255.0f, 140.0f / 255.0f, 41.0f / 255.0f, 1.0f};
		style.lane_border_color = {48.0f / 255.0f, 58.0f / 255.0f, 74.0f / 255.0f, 1.0f};
		style.hit_line_color = {245.0f / 255.0f, 245.0f / 255.0f, 245.0f / 255.0f, 1.0f};
		style.sustain_color = {235.0f / 255.0f, 235.0f / 255.0f, 235.0f / 255.0f, 0.70f};
		style.measure_line_color = {235.0f / 255.0f, 240.0f / 255.0f, 250.0f / 255.0f, 0.85f};
		style.beat_line_color = {100.0f / 255.0f, 112.0f / 255.0f, 128.0f / 255.0f, 0.50f};
		style.background_top_color = {18.0f / 255.0f, 24.0f / 255.0f, 34.0f / 255.0f, 1.0f};
		style.background_bottom_color = {8.0f / 255.0f, 10.0f / 255.0f, 16.0f / 255.0f, 1.0f};
		gameplay_player.world.style = style;

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

	const AudioBatch &AppCore::audio_batch() const
	{
		return audio_batch_;
	}

	void AppCore::render_interleaved_s16(std::int16_t *output, size_t frame_count)
	{
		if (output == nullptr)
			return;

		if (mode_ != AppMode::PrototypePlayer)
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

	void AppCore::run_song_browser(const RetroInputState &input_state)
	{
		if (pressed(input_state.up, previous_input_.up))
			song_browser_.move_selection(-1);

		if (pressed(input_state.down, previous_input_.down))
			song_browser_.move_selection(1);

		if (pressed(input_state.b, previous_input_.b))
		{
			std::string error_message;
			if (!song_browser_.navigate_to_parent(error_message) && !error_message.empty())
				song_browser_.set_status_message(error_message);
		}

		if (pressed(input_state.a, previous_input_.a) || pressed(input_state.start, previous_input_.start))
			activate_browser_selection_unlocked();
	}

	void AppCore::run_prototype_player(const RetroInputState &input_state)
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
