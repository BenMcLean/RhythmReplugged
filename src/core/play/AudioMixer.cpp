#include "core/play/AudioMixer.h"

#include <algorithm>

namespace rhythmreplugged
{
	void AudioMixer::reset()
	{
		prototype_player_ = nullptr;
	}

	void AudioMixer::set_prototype_player(PrototypePlayer *player)
	{
		prototype_player_ = player;
	}

	void AudioMixer::render_interleaved_s16(std::int16_t *output, size_t frame_count) const
	{
		if (output == nullptr || frame_count == 0)
			return;

		if (prototype_player_ == nullptr || !prototype_player_->is_loaded())
		{
			std::fill(output, output + frame_count * 2, static_cast<std::int16_t>(0));
			return;
		}

		prototype_player_->render_interleaved_s16(output, frame_count);
	}

	RetroAudioBatch AudioMixer::render(size_t frame_count) const
	{
		if (prototype_player_ == nullptr || !prototype_player_->is_loaded())
			return {};

		RetroAudioBatch batch;
		batch.sample_rate = prototype_player_->sample_rate();
		batch.channels = 2;
		batch.samples.resize(frame_count * 2);
		render_interleaved_s16(batch.samples.data(), frame_count);
		return batch;
	}
}
