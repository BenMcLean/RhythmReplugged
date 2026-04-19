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
		song_session_.unload();
		audio_batch_.clear();
		audio_batch_.sample_rate = 0;
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
		song_session_.unload();
		audio_batch_.clear();
		audio_batch_.sample_rate = 0;
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
			if (song_session_.load(file_system_, selected_song_path, error_message))
			{
				mode_ = AppMode::PrototypePlayer;
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
		mode_ = AppMode::SongBrowser;
		song_session_.unload();
		player_status_message_.clear();
	}

	void AppCore::toggle_player_guitar_mute()
	{
		if (mode_ == AppMode::PrototypePlayer)
			song_session_.toggle_guitar_mute();
	}

	size_t AppCore::player_mute_change_count() const
	{
		return song_session_.mute_change_count();
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

		if (!song_session_.is_loaded())
			return;

		audio_batch_ = song_session_.render_audio_tick(kRetroFramesPerSecond);
	}
}
