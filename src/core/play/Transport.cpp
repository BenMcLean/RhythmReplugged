#include "core/play/Transport.h"

namespace rhythmreplugged::core
{
	void Transport::reset()
	{
		sample_rate_.store(0);
		frame_remainder_ = 0;
		emitted_frames_.store(0);
	}

	void Transport::configure(int sample_rate)
	{
		if (sample_rate <= 0)
		{
			reset();
			return;
		}

		sample_rate_.store(sample_rate);
		frame_remainder_ = 0;
		emitted_frames_.store(0);
	}

	size_t Transport::frames_for_next_tick(int ticks_per_second)
	{
		const int sample_rate = sample_rate_.load();
		if (sample_rate <= 0 || ticks_per_second <= 0)
			return 0;

		frame_remainder_ += static_cast<size_t>(sample_rate);
		const size_t frame_count = frame_remainder_ / static_cast<size_t>(ticks_per_second);
		frame_remainder_ %= static_cast<size_t>(ticks_per_second);
		return frame_count;
	}

	void Transport::on_audio_rendered(size_t frame_count)
	{
		emitted_frames_.fetch_add(frame_count);
	}

	bool Transport::is_configured() const
	{
		return sample_rate_.load() > 0;
	}

	int Transport::sample_rate() const
	{
		return sample_rate_.load();
	}

	size_t Transport::emitted_frames() const
	{
		return emitted_frames_.load();
	}

	double Transport::seconds_from_frames(size_t frame_count) const
	{
		const int sample_rate = sample_rate_.load();
		return sample_rate > 0 ? static_cast<double>(frame_count) / static_cast<double>(sample_rate) : 0.0;
	}

	double Transport::song_time_seconds() const
	{
		return seconds_from_frames(emitted_frames());
	}

	double Transport::song_time_beats(double beats_per_minute) const
	{
		return beats_per_minute > 0.0 ? song_time_seconds() * (beats_per_minute / 60.0) : 0.0;
	}

	Transport::State Transport::state() const
	{
		State state;
		state.sample_rate = sample_rate_.load();
		state.frame_remainder = frame_remainder_;
		state.emitted_frames = emitted_frames_.load();
		return state;
	}

	void Transport::restore_state(const State &state)
	{
		if (state.sample_rate <= 0)
		{
			reset();
			return;
		}

		sample_rate_.store(state.sample_rate);
		frame_remainder_ = state.frame_remainder;
		emitted_frames_.store(state.emitted_frames);
	}
}
