#pragma once

#include <miniaudio.h>

#include "libretro_contract/RetroAudio.h"

#include <atomic>

namespace rhythmreplugged
{
	class Sdl3AudioOutput
	{
	public:
		Sdl3AudioOutput() = default;
		~Sdl3AudioOutput();

		Sdl3AudioOutput(const Sdl3AudioOutput &) = delete;
		Sdl3AudioOutput &operator=(const Sdl3AudioOutput &) = delete;

		bool initialize(IRetroAudioStream *stream);
		void shutdown();
		void set_stream(IRetroAudioStream *stream);

	private:
		static void data_callback(ma_device *device, void *output, const void *input, ma_uint32 frame_count);
		void mix(std::int16_t *output, ma_uint32 frame_count);

		ma_device device_{};
		bool initialized_ = false;
		int sample_rate_ = 0;
		std::atomic<IRetroAudioStream *> stream_{nullptr};
	};
}
