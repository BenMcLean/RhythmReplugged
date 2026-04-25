#include "core/play/Transport.h"
#include "core/app/AppTypes.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
	using namespace rhythmreplugged::core;

	bool nearly_equal(double left, double right, double epsilon)
	{
		return std::fabs(left - right) <= epsilon;
	}
}

int main()
{
	Transport fixed_tick_transport;
	fixed_tick_transport.configure(48000);

	size_t fixed_tick_total_frames = 0;
	for (int tick = 0; tick < kAppFramesPerSecond * 10; ++tick)
	{
		const size_t frame_count = fixed_tick_transport.frames_for_next_tick(kAppFramesPerSecond);
		fixed_tick_transport.on_audio_rendered(frame_count);
		fixed_tick_total_frames += frame_count;
	}

	Transport callback_transport;
	callback_transport.configure(48000);

	const std::vector<size_t> callback_chunks = {128, 256, 192, 320, 64, 96, 224};
	size_t callback_total_frames = 0;
	size_t chunk_index = 0;
	while (callback_total_frames < fixed_tick_total_frames)
	{
		size_t frame_count = callback_chunks[chunk_index % callback_chunks.size()];
		++chunk_index;

		if (callback_total_frames + frame_count > fixed_tick_total_frames)
			frame_count = fixed_tick_total_frames - callback_total_frames;

		callback_transport.on_audio_rendered(frame_count);
		callback_total_frames += frame_count;
	}

	if (fixed_tick_transport.emitted_frames() != callback_transport.emitted_frames())
	{
		std::cerr << "Frame parity failed.\n";
		return EXIT_FAILURE;
	}

	if (!nearly_equal(fixed_tick_transport.song_time_seconds(), callback_transport.song_time_seconds(), 1e-9))
	{
		std::cerr << "Second parity failed.\n";
		return EXIT_FAILURE;
	}

	if (!nearly_equal(fixed_tick_transport.song_time_beats(120.0), callback_transport.song_time_beats(120.0), 1e-9))
	{
		std::cerr << "Beat parity failed.\n";
		return EXIT_FAILURE;
	}

	std::cout << "Transport parity harness passed.\n";
	return EXIT_SUCCESS;
}
