#pragma once

#include "core/audio/PrototypePlayer.h"
#include "frontend_contract/AudioTypes.h"

#include <cstddef>

namespace rhythmreplugged::core
{
	class AudioMixer
	{
	public:
		void reset();
		void set_prototype_player(PrototypePlayer *player);
		void render_interleaved_s16(std::int16_t *output, size_t frame_count) const;
		::rhythmreplugged::frontend_contract::AudioBatch render(size_t frame_count) const;

	private:
		PrototypePlayer *prototype_player_ = nullptr;
	};
}
