#pragma once

#include <miniaudio.h>

#include "libretro_contract/AudioTypes.h"

#include <atomic>

namespace rhythmreplugged
{
	class MiniaudioOutput
	{
	public:
		MiniaudioOutput() = default;
		~MiniaudioOutput();

		MiniaudioOutput(const MiniaudioOutput &) = delete;
		MiniaudioOutput &operator=(const MiniaudioOutput &) = delete;

		bool initialize(IAudioStream *stream);
		void shutdown();
		void set_stream(IAudioStream *stream);

	private:
		static void data_callback(ma_device *device, void *output, const void *input, ma_uint32 frame_count);
		void mix(std::int16_t *output, ma_uint32 frame_count);

		ma_device device_{};
		bool initialized_ = false;
		int sample_rate_ = 0;
		std::atomic<IAudioStream *> stream_{nullptr};
	};
}
