#include "core/play/AudioMixer.h"

#include <algorithm>

namespace rhythmreplugged::core
{
	void AudioMixer::reset()
	{
		song_player_ = nullptr;
	}

	void AudioMixer::set_song_player(SongPlayer *player)
	{
		song_player_ = player;
	}

	void AudioMixer::render_interleaved_s16(std::int16_t *output, size_t frame_count) const
	{
		if (output == nullptr || frame_count == 0)
			return;

		if (song_player_ == nullptr || !song_player_->is_loaded())
		{
			std::fill(output, output + frame_count * 2, static_cast<std::int16_t>(0));
			return;
		}

		song_player_->render_interleaved_s16(output, frame_count);
	}

	::rhythmreplugged::frontend_contract::AudioBatch AudioMixer::render(size_t frame_count) const
	{
		if (song_player_ == nullptr || !song_player_->is_loaded())
			return {};

		::rhythmreplugged::frontend_contract::AudioBatch batch;
		batch.sample_rate = song_player_->sample_rate();
		batch.channels = 2;
		batch.samples.resize(frame_count * 2);
		render_interleaved_s16(batch.samples.data(), frame_count);
		return batch;
	}
}
