#pragma once

#include <miniaudio.h>

#include "libretro_contract/RetroAudio.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>

namespace rhythmreplugged
{
	class Sdl3AudioOutput
	{
	public:
		Sdl3AudioOutput() = default;
		~Sdl3AudioOutput();

		Sdl3AudioOutput(const Sdl3AudioOutput &) = delete;
		Sdl3AudioOutput &operator=(const Sdl3AudioOutput &) = delete;

		bool initialize(int sample_rate);
		void shutdown();
		void submit(const RetroAudioBatch &batch);
		void clear_queued_audio();
		size_t queued_frames() const;

	private:
		static void data_callback(ma_device *device, void *output, const void *input, ma_uint32 frame_count);
		void mix(std::int16_t *output, ma_uint32 frame_count);

		ma_device device_{};
		bool initialized_ = false;
		int sample_rate_ = 0;
		mutable std::mutex mutex_;
		std::deque<std::int16_t> queued_samples_;
	};
}
