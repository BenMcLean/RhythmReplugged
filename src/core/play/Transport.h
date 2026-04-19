#pragma once

#include <cstddef>

namespace rhythmreplugged
{
	class Transport
	{
	public:
		void reset();
		void configure(int sample_rate);
		size_t frames_per_tick(int ticks_per_second);
		void on_audio_generated(size_t frame_count);
		bool is_configured() const;
		int sample_rate() const;
		size_t emitted_frames() const;
		double song_time_seconds() const;

	private:
		int sample_rate_ = 0;
		size_t frame_remainder_ = 0;
		size_t emitted_frames_ = 0;
	};
}
