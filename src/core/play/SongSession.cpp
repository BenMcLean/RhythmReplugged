#include "core/play/SongSession.h"

#include "libretro_contract/RetroFileSystem.h"

namespace rhythmreplugged
{
	bool SongSession::load(IRetroFileSystem &file_system, const std::string &song_directory, std::string &error_message)
	{
		unload();
		if (!prototype_player_.load(file_system, song_directory, error_message))
			return false;

		transport_.configure(prototype_player_.sample_rate());
		audio_mixer_.set_prototype_player(&prototype_player_);
		return true;
	}

	void SongSession::unload()
	{
		audio_mixer_.reset();
		transport_.reset();
		prototype_player_.unload();
		mute_change_count_ = 0;
	}

	bool SongSession::is_loaded() const
	{
		return prototype_player_.is_loaded() && transport_.is_configured();
	}

	void SongSession::toggle_guitar_mute()
	{
		if (is_loaded())
		{
			prototype_player_.toggle_guitar_mute();
			++mute_change_count_;
		}
	}

	size_t SongSession::mute_change_count() const
	{
		return mute_change_count_;
	}

	PrototypePlayerView SongSession::view(const std::string &status_message) const
	{
		PrototypePlayerView player_view;
		player_view.song_title = prototype_player_.metadata().name;
		player_view.song_artist = prototype_player_.metadata().artist;
		player_view.status_message = status_message;
		player_view.guitar_muted = prototype_player_.guitar_muted();
		return player_view;
	}

	RetroAudioBatch SongSession::render_audio_tick(int ticks_per_second)
	{
		const size_t frame_count = transport_.frames_per_tick(ticks_per_second);
		RetroAudioBatch batch = audio_mixer_.render(frame_count);
		transport_.on_audio_generated(batch.frame_count());
		return batch;
	}

	double SongSession::song_time_seconds() const
	{
		return transport_.song_time_seconds();
	}
}
