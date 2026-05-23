#pragma once

#include "core/audio/SongPlayer.h"
#include "frontend_contract/AudioTypes.h"

#include <cstddef>

namespace rhythmreplugged::core
{
	class AudioMixer
	{
	public:
		void reset();
		void set_song_player(SongPlayer *player);
		void render_interleaved_s16(std::int16_t *output, size_t frame_count) const;
		::rhythmreplugged::frontend_contract::AudioBatch render(size_t frame_count) const;

	private:
		SongPlayer *song_player_ = nullptr;
	};
}
