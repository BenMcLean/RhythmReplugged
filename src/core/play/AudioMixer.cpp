#include "core/play/AudioMixer.h"

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

	RetroAudioBatch AudioMixer::render(size_t frame_count) const
	{
		if (prototype_player_ == nullptr || !prototype_player_->is_loaded())
			return {};

		return prototype_player_->generate_audio_batch(frame_count);
	}
}
