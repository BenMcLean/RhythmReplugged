#include "core/app/AppCore.h"

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
		audio_batch_.clear();
		audio_batch_.sample_rate = 0;
		audio_frame_remainder_ = 0;
		player_status_message_.clear();
		return song_browser_.set_root(song_root_path, error_message);
	}

	void AppCore::retro_run(const RetroInputState &input_state)
	{
		audio_batch_.clear();

		switch (mode_)
		{
		case AppMode::SongBrowser:
			run_song_browser(input_state);
			break;
		case AppMode::PrototypePlayer:
			run_prototype_player(input_state);
			break;
		}

		previous_input_ = input_state;
	}

	void AppCore::retro_deinit()
	{
		prototype_player_.unload();
		audio_batch_.clear();
		audio_batch_.sample_rate = 0;
		audio_frame_remainder_ = 0;
		player_status_message_.clear();
	}

	bool AppCore::set_browser_selected_index(int index)
	{
		if (mode_ != AppMode::SongBrowser)
			return false;

		return song_browser_.set_selected_index(index);
	}

	bool AppCore::activate_browser_selection()
	{
		if (mode_ != AppMode::SongBrowser)
			return false;

		std::string selected_song_path;
		std::string error_message;
		if (song_browser_.activate_selected(selected_song_path, error_message) && !selected_song_path.empty())
		{
			if (prototype_player_.load(file_system_, selected_song_path, error_message))
			{
				mode_ = AppMode::PrototypePlayer;
				player_status_message_.clear();
				audio_frame_remainder_ = 0;
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
		mode_ = AppMode::SongBrowser;
		prototype_player_.unload();
		player_status_message_.clear();
	}

	void AppCore::toggle_player_guitar_mute()
	{
		if (mode_ == AppMode::PrototypePlayer)
			prototype_player_.toggle_guitar_mute();
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
		PrototypePlayerView view;
		view.song_title = prototype_player_.metadata().name;
		view.song_artist = prototype_player_.metadata().artist;
		view.status_message = player_status_message_;
		view.guitar_muted = prototype_player_.guitar_muted();
		return view;
	}

	const RetroAudioBatch &AppCore::audio_batch() const
	{
		return audio_batch_;
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
			activate_browser_selection();
	}

	void AppCore::run_prototype_player(const RetroInputState &input_state)
	{
		if (pressed(input_state.b, previous_input_.b))
		{
			return_to_browser();
			return;
		}

		if (pressed(input_state.a, previous_input_.a) || pressed(input_state.x, previous_input_.x))
			toggle_player_guitar_mute();

		if (!prototype_player_.is_loaded())
			return;

		audio_frame_remainder_ += static_cast<size_t>(prototype_player_.sample_rate());
		const size_t frames_to_generate = audio_frame_remainder_ / static_cast<size_t>(kRetroFramesPerSecond);
		audio_frame_remainder_ %= static_cast<size_t>(kRetroFramesPerSecond);
		audio_batch_ = prototype_player_.generate_audio_batch(frames_to_generate);
	}
}
