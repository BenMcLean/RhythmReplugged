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
		mode_ = AppMode::SongBrowser;
		song_session_.unload();
		session_unload_pending_ = false;
		audio_batch_.clear();
		audio_batch_.sample_rate = 0;
		player_status_message_.clear();
		return song_browser_.set_root(song_root_path, error_message);
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

		if (pressed(input_state.a, previous_input_.a) || pressed(input_state.start, previous_input_.start))
			activate_browser_selection_unlocked();
	}

	void AppCore::run_prototype_player(const RetroInputState &input_state)
	{
		if (pressed(input_state.b, previous_input_.b))
		{
			return_to_browser_unlocked();
			return;
		}

		const std::array<bool, 5> lane_held = {
			input_state.left,
			input_state.up,
			input_state.y,
			input_state.x,
			input_state.a,
		};
		const std::array<bool, 5> lane_pressed = {
			pressed(input_state.left, previous_input_.left),
			pressed(input_state.up, previous_input_.up),
			pressed(input_state.y, previous_input_.y),
			pressed(input_state.x, previous_input_.x),
			pressed(input_state.a, previous_input_.a),
		};
		song_session_.update_gameplay_input(lane_held, lane_pressed);
	}
}
