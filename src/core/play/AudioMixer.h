#pragma once

#include "core/audio/PrototypePlayer.h"
#include "libretro_contract/AudioTypes.h"

#include <cstddef>

namespace rhythmreplugged
{
	class AudioMixer
	{
	public:
		void reset();
		void set_prototype_player(PrototypePlayer *player);
		void render_interleaved_s16(std::int16_t *output, size_t frame_count) const;
		AudioBatch render(size_t frame_count) const;

	private:
		PrototypePlayer *prototype_player_ = nullptr;
	};
}
