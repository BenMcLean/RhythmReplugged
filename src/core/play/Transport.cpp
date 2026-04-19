#include "core/play/Transport.h"

namespace rhythmreplugged
{
	void Transport::reset()
	{
		sample_rate_ = 0;
		frame_remainder_ = 0;
		emitted_frames_ = 0;
	}

	void Transport::configure(int sample_rate)
	{
		if (sample_rate <= 0)
		{
			reset();
			return;
		}

		sample_rate_ = sample_rate;
		frame_remainder_ = 0;
		emitted_frames_ = 0;
	}

	size_t Transport::frames_per_tick(int ticks_per_second)
	{
		if (sample_rate_ <= 0 || ticks_per_second <= 0)
			return 0;

		frame_remainder_ += static_cast<size_t>(sample_rate_);
		const size_t frame_count = frame_remainder_ / static_cast<size_t>(ticks_per_second);
		frame_remainder_ %= static_cast<size_t>(ticks_per_second);
		return frame_count;
	}

	void Transport::on_audio_generated(size_t frame_count)
	{
		emitted_frames_ += frame_count;
	}

	bool Transport::is_configured() const
	{
		return sample_rate_ > 0;
	}

	int Transport::sample_rate() const
	{
		return sample_rate_;
	}

	size_t Transport::emitted_frames() const
	{
		return emitted_frames_;
	}

	double Transport::song_time_seconds() const
	{
		return sample_rate_ > 0 ? static_cast<double>(emitted_frames_) / static_cast<double>(sample_rate_) : 0.0;
	}
}
