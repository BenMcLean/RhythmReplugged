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
		loaded_.store(true);
		return true;
	}

	void SongSession::unload()
	{
		audio_mixer_.reset();
		transport_.reset();
		prototype_player_.unload();
		loaded_.store(false);
	}

	bool SongSession::is_loaded() const
	{
		return loaded_.load();
	}

	void SongSession::toggle_guitar_mute()
	{
		if (is_loaded())
			prototype_player_.toggle_guitar_mute();
	}

	void SongSession::set_stem_target_gain(StemId stem_id, float gain)
	{
		if (is_loaded())
			prototype_player_.set_stem_target_gain(stem_id, gain);
	}

	float SongSession::stem_target_gain(StemId stem_id) const
	{
		return prototype_player_.stem_target_gain(stem_id);
	}

	int SongSession::sample_rate() const
	{
		return transport_.sample_rate();
	}

	size_t SongSession::emitted_frames() const
	{
		return transport_.emitted_frames();
	}

	PrototypePlayerView SongSession::view(const std::string &status_message) const
	{
		PrototypePlayerView player_view;
		player_view.song_title = prototype_player_.metadata().name;
		player_view.song_artist = prototype_player_.metadata().artist;
		player_view.status_message = status_message;
		player_view.guitar_muted = prototype_player_.stem_target_gain(StemId::Guitar) < 0.5f;
		return player_view;
	}

	void SongSession::render_interleaved_s16(std::int16_t *output, size_t frame_count)
	{
		audio_mixer_.render_interleaved_s16(output, frame_count);
		transport_.on_audio_rendered(frame_count);
	}

	RetroAudioBatch SongSession::render_fixed_tick_audio(int ticks_per_second)
	{
		const size_t frame_count = transport_.frames_for_next_tick(ticks_per_second);
		RetroAudioBatch batch = audio_mixer_.render(frame_count);
		transport_.on_audio_rendered(batch.frame_count());
		return batch;
	}

	double SongSession::song_time_seconds() const
	{
		return transport_.song_time_seconds();
	}

	double SongSession::song_time_beats(double beats_per_minute) const
	{
		return transport_.song_time_beats(beats_per_minute);
	}
}
