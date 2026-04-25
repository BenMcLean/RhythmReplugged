#define MINIAUDIO_IMPLEMENTATION
#include "platform_sdl3/MiniaudioOutput.h"

#include <algorithm>

namespace rhythmreplugged::platform_sdl3
{
	MiniaudioOutput::~MiniaudioOutput()
	{
		shutdown();
	}

	bool MiniaudioOutput::initialize(::rhythmreplugged::frontend_contract::IAudioStream *stream)
	{
		const int sample_rate = stream != nullptr ? stream->sample_rate() : 0;
		if (sample_rate <= 0)
			return false;

		if (initialized_ && sample_rate_ == sample_rate)
		{
			stream_.store(stream);
			return true;
		}

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
		stream_.store(stream);
		return true;
	}

	void MiniaudioOutput::shutdown()
	{
		if (!initialized_)
			return;

		ma_device_uninit(&device_);
		initialized_ = false;
		sample_rate_ = 0;
		stream_.store(nullptr);
	}

	void MiniaudioOutput::set_stream(::rhythmreplugged::frontend_contract::IAudioStream *stream)
	{
		stream_.store(stream);
	}

	void MiniaudioOutput::data_callback(ma_device *device, void *output, const void *input, ma_uint32 frame_count)
	{
		(void)input;
		auto *self = static_cast<MiniaudioOutput *>(device->pUserData);
		if (self == nullptr)
			return;

		self->mix(static_cast<std::int16_t *>(output), frame_count);
	}

	void MiniaudioOutput::mix(std::int16_t *output, ma_uint32 frame_count)
	{
		::rhythmreplugged::frontend_contract::IAudioStream *stream = stream_.load();
		if (stream == nullptr)
		{
			std::fill(output, output + static_cast<size_t>(frame_count) * 2, static_cast<std::int16_t>(0));
			return;
		}

		stream->render_interleaved_s16(output, frame_count);
	}
}
