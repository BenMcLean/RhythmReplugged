#pragma once

#include <atomic>
#include <cstddef>

namespace rhythmreplugged::core
{
	class Transport
	{
	public:
		struct State
		{
			int sample_rate = 0;
			size_t frame_remainder = 0;
			size_t emitted_frames = 0;
		};

		void reset();
		void configure(int sample_rate);
		size_t frames_for_next_tick(int ticks_per_second);
		void on_audio_rendered(size_t frame_count);
		bool is_configured() const;
		int sample_rate() const;
		size_t emitted_frames() const;
		double seconds_from_frames(size_t frame_count) const;
		double song_time_seconds() const;
		double song_time_beats(double beats_per_minute) const;
		State state() const;
		void restore_state(const State &state);

	private:
		std::atomic<int> sample_rate_{0};
		size_t frame_remainder_ = 0;
		std::atomic<size_t> emitted_frames_{0};
	};
}
