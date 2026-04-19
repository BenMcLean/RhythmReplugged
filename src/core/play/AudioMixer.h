#pragma once

#include "core/audio/PrototypePlayer.h"
#include "libretro_contract/RetroAudio.h"

#include <cstddef>

namespace rhythmreplugged
{
	class AudioMixer
	{
	public:
		void reset();
		void set_prototype_player(PrototypePlayer *player);
		RetroAudioBatch render(size_t frame_count) const;

	private:
		PrototypePlayer *prototype_player_ = nullptr;
	};
}
