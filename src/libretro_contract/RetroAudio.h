#pragma once

#include <cstdint>
#include <vector>

namespace rhythmreplugged
{
	struct RetroAudioBatch
	{
		int sample_rate = 0;
		int channels = 2;
		std::vector<std::int16_t> samples;

		size_t frame_count() const
		{
			return channels > 0 ? samples.size() / static_cast<size_t>(channels) : 0;
		}

		void clear()
		{
			samples.clear();
		}
	};
}
