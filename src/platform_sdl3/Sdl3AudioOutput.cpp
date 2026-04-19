#define MINIAUDIO_IMPLEMENTATION
#include "platform_sdl3/Sdl3AudioOutput.h"

namespace rhythmreplugged
{
	Sdl3AudioOutput::~Sdl3AudioOutput()
	{
		shutdown();
	}

	bool Sdl3AudioOutput::initialize(int sample_rate)
	{
		if (initialized_ && sample_rate_ == sample_rate)
			return true;

		shutdown();

		ma_device_config config = ma_device_config_init(ma_device_type_playback);
		config.playback.format = ma_format_s16;
		config.playback.channels = 2;
		config.sampleRate = static_cast<ma_uint32>(sample_rate);
		config.dataCallback = data_callback;
		config.pUserData = this;
		config.periodSizeInFrames = 128;

		if (ma_device_init(nullptr, &config, &device_) != MA_SUCCESS)
			return false;

		if (ma_device_start(&device_) != MA_SUCCESS)
		{
			ma_device_uninit(&device_);
			return false;
		}

		initialized_ = true;
		sample_rate_ = sample_rate;
		return true;
	}

	void Sdl3AudioOutput::shutdown()
	{
		if (!initialized_)
			return;

		ma_device_uninit(&device_);
		initialized_ = false;
		sample_rate_ = 0;

		std::scoped_lock lock(mutex_);
		queued_samples_.clear();
	}

	void Sdl3AudioOutput::submit(const RetroAudioBatch &batch)
	{
		if (!initialized_ || batch.samples.empty())
			return;

		std::scoped_lock lock(mutex_);
		queued_samples_.insert(queued_samples_.end(), batch.samples.begin(), batch.samples.end());
	}

	void Sdl3AudioOutput::clear_queued_audio()
	{
		std::scoped_lock lock(mutex_);
		queued_samples_.clear();
	}

	size_t Sdl3AudioOutput::queued_frames() const
	{
		std::scoped_lock lock(mutex_);
		return queued_samples_.size() / 2;
	}

	void Sdl3AudioOutput::data_callback(ma_device *device, void *output, const void *input, ma_uint32 frame_count)
	{
		(void)input;
		auto *self = static_cast<Sdl3AudioOutput *>(device->pUserData);
		if (self == nullptr)
			return;

		self->mix(static_cast<std::int16_t *>(output), frame_count);
	}

	void Sdl3AudioOutput::mix(std::int16_t *output, ma_uint32 frame_count)
	{
		std::scoped_lock lock(mutex_);
		for (ma_uint32 frame = 0; frame < frame_count; ++frame)
		{
			for (int channel = 0; channel < 2; ++channel)
			{
				const size_t output_index = static_cast<size_t>(frame) * 2 + static_cast<size_t>(channel);
				if (queued_samples_.empty())
				{
					output[output_index] = 0;
				}
				else
				{
					output[output_index] = queued_samples_.front();
					queued_samples_.pop_front();
				}
			}
		}
	}
}
