#pragma once

#include <cstdint>
#include <vector>

namespace rhythmreplugged
{
	class IAudioStream
	{
	public:
		virtual ~IAudioStream() = default;
		virtual int sample_rate() const = 0;
		virtual void render_interleaved_s16(std::int16_t *output, size_t frame_count) = 0;
	};

	struct AudioBatch
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
