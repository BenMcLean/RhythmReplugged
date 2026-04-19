#include "BitmapFont.h"

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#ifdef DrawText
#undef DrawText
#endif

#include <SDL.h>
#include <SDL_image.h>
#include <vorbis/vorbisfile.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	constexpr int kWindowWidth = 800;
	constexpr int kWindowHeight = 600;
	const std::filesystem::path kSongDirectory = std::filesystem::path("songs") / "Strong Bad - Trogdor";

	struct DecodedTrack
	{
		std::vector<float> samples;
		int channels = 0;
		long sampleRate = 0;
		size_t frameCount = 0;
	};

	std::filesystem::path FindSongDirectory(const char *argv0)
	{
		std::error_code errorCode;
		std::filesystem::path basePath = std::filesystem::current_path(errorCode);
		if (argv0 != nullptr && argv0[0] != '\0')
		{
			const std::filesystem::path executablePath = std::filesystem::absolute(argv0, errorCode);
			if (!errorCode)
				basePath = executablePath.parent_path();
		}

		std::filesystem::path probePath = basePath;
		while (!probePath.empty())
		{
			const std::filesystem::path candidate = probePath / kSongDirectory;
			if (std::filesystem::exists(candidate, errorCode))
				return candidate;

			const std::filesystem::path parent = probePath.parent_path();
			if (parent == probePath)
				break;

			probePath = parent;
		}

		return {};
	}

	bool DecodeVorbisFile(const std::filesystem::path &path, DecodedTrack &track)
	{
		FILE *file = nullptr;
		if (fopen_s(&file, path.string().c_str(), "rb") != 0 || file == nullptr)
		{
			std::cerr << "Could not open " << path.string() << "\n";
			return false;
		}

		OggVorbis_File vorbisFile{};
		if (ov_open(file, &vorbisFile, nullptr, 0) < 0)
		{
			std::cerr << "ov_open failed for " << path.string() << "\n";
			fclose(file);
			return false;
		}

		bool success = false;
		do
		{
			vorbis_info *info = ov_info(&vorbisFile, -1);
			if (info == nullptr)
			{
				std::cerr << "ov_info failed for " << path.string() << "\n";
				break;
			}

			track.channels = info->channels;
			track.sampleRate = info->rate;
			if (track.channels <= 0 || track.sampleRate <= 0)
			{
				std::cerr << "Invalid stream format in " << path.string() << "\n";
				break;
			}

			float **pcmChannels = nullptr;
			for (;;)
			{
				const long framesRead = ov_read_float(&vorbisFile, &pcmChannels, 4096, nullptr);
				if (framesRead == 0)
				{
					success = true;
					break;
				}

				if (framesRead < 0)
				{
					std::cerr << "ov_read_float failed for " << path.string() << "\n";
					break;
				}

				const size_t startIndex = track.samples.size();
				track.samples.resize(startIndex + static_cast<size_t>(framesRead) * static_cast<size_t>(track.channels));

				for (long frame = 0; frame < framesRead; ++frame)
				{
					for (int channel = 0; channel < track.channels; ++channel)
					{
						track.samples[startIndex + static_cast<size_t>(frame) * static_cast<size_t>(track.channels) + static_cast<size_t>(channel)] =
							pcmChannels[channel][frame];
					}
				}
			}
		}
		while (false);

		ov_clear(&vorbisFile);
		track.frameCount = track.channels > 0 ? track.samples.size() / static_cast<size_t>(track.channels) : 0;
		return success;
	}

	struct AudioState
	{
		DecodedTrack backingTrack;
		DecodedTrack guitarTrack;
		size_t frameIndex = 0;
		std::atomic_bool guitarMuted = false;
	};

	float SampleTrackChannel(const DecodedTrack &track, size_t frameIndex, int channel)
	{
		if (frameIndex >= track.frameCount || channel >= track.channels)
			return 0.0f;

		return track.samples[frameIndex * static_cast<size_t>(track.channels) + static_cast<size_t>(channel)];
	}

	void MixAudioFrames(AudioState &audioState, float *output, ma_uint32 frameCount)
	{
		const int channelCount = audioState.backingTrack.channels;
		for (ma_uint32 frame = 0; frame < frameCount; ++frame)
		{
			for (int channel = 0; channel < channelCount; ++channel)
			{
				float mixedSample = SampleTrackChannel(audioState.backingTrack, audioState.frameIndex, channel);
				if (!audioState.guitarMuted.load(std::memory_order_relaxed))
					mixedSample += SampleTrackChannel(audioState.guitarTrack, audioState.frameIndex, channel);

				output[frame * channelCount + channel] = std::clamp(mixedSample, -1.0f, 1.0f);
			}

			audioState.frameIndex += 1;
		}
	}

	void MiniAudioCallback(ma_device *device, void *output, const void *input, ma_uint32 frameCount)
	{
		(void)input;

		auto *audioState = static_cast<AudioState *>(device->pUserData);
		if (audioState == nullptr)
			return;

		MixAudioFrames(*audioState, static_cast<float *>(output), frameCount);
	}
}

int main(int argc, char *argv[])
{
	if (SDL_Init(SDL_INIT_VIDEO) != 0)
	{
		std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
		return 1;
	}

	const int imageInitFlags = IMG_INIT_PNG;
	if ((IMG_Init(imageInitFlags) & imageInitFlags) != imageInitFlags)
	{
		std::cerr << "IMG_Init failed: " << IMG_GetError() << "\n";
		SDL_Quit();
		return 1;
	}

	const std::filesystem::path songDirectory = FindSongDirectory(argc > 0 ? argv[0] : nullptr);
	if (songDirectory.empty())
	{
		std::cerr << "Could not locate song folder: " << kSongDirectory.string() << "\n";
		IMG_Quit();
		SDL_Quit();
		return 1;
	}

	const std::filesystem::path backingTrackPath = songDirectory / "song.ogg";
	const std::filesystem::path guitarTrackPath = songDirectory / "guitar.ogg";

	AudioState audioState;
	if (!DecodeVorbisFile(backingTrackPath, audioState.backingTrack))
	{
		IMG_Quit();
		SDL_Quit();
		return 1;
	}

	if (!DecodeVorbisFile(guitarTrackPath, audioState.guitarTrack))
	{
		IMG_Quit();
		SDL_Quit();
		return 1;
	}

	if (audioState.backingTrack.channels != audioState.guitarTrack.channels ||
		audioState.backingTrack.sampleRate != audioState.guitarTrack.sampleRate)
	{
		std::cerr << "Stem format mismatch between song.ogg and guitar.ogg\n";
		IMG_Quit();
		SDL_Quit();
		return 1;
	}

	ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
	deviceConfig.playback.format = ma_format_f32;
	deviceConfig.playback.channels = static_cast<ma_uint32>(audioState.backingTrack.channels);
	deviceConfig.sampleRate = static_cast<ma_uint32>(audioState.backingTrack.sampleRate);
	deviceConfig.dataCallback = MiniAudioCallback;
	deviceConfig.pUserData = &audioState;
	deviceConfig.periodSizeInFrames = 512;

	ma_device audioDevice{};
	if (ma_device_init(nullptr, &deviceConfig, &audioDevice) != MA_SUCCESS)
	{
		std::cerr << "ma_device_init failed\n";
		IMG_Quit();
		SDL_Quit();
		return 1;
	}

	if (ma_device_start(&audioDevice) != MA_SUCCESS)
	{
		std::cerr << "ma_device_start failed\n";
		ma_device_uninit(&audioDevice);
		IMG_Quit();
		SDL_Quit();
		return 1;
	}

	SDL_Window *window = SDL_CreateWindow(
		"Rhythm Replugged - Multitrack Prototype",
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		kWindowWidth,
		kWindowHeight,
		SDL_WINDOW_SHOWN);

	if (!window)
	{
		std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
		ma_device_uninit(&audioDevice);
		IMG_Quit();
		SDL_Quit();
		return 1;
	}

	SDL_Renderer *renderer = SDL_CreateRenderer(
		window,
		-1,
		SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

	if (!renderer)
	{
		std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
		SDL_DestroyWindow(window);
		ma_device_uninit(&audioDevice);
		IMG_Quit();
		SDL_Quit();
		return 1;
	}

	rhythmreplugged::BitmapFont font;
	if (!font.Load(renderer))
	{
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
		ma_device_uninit(&audioDevice);
		IMG_Quit();
		SDL_Quit();
		return 1;
	}

	bool running = true;
	SDL_Event e;

	while (running)
	{
		while (SDL_PollEvent(&e))
		{
			if (e.type == SDL_QUIT)
			{
				running = false;
			}
			else if (e.type == SDL_KEYDOWN && e.key.repeat == 0 && e.key.keysym.sym == SDLK_SPACE)
			{
				const bool currentlyMuted = audioState.guitarMuted.load(std::memory_order_relaxed);
				audioState.guitarMuted.store(!currentlyMuted, std::memory_order_relaxed);
			}
		}

		const std::string guitarStatus = audioState.guitarMuted.load(std::memory_order_relaxed) ? "OFF" : "ON";
		const std::string title = "Multitrack Prototype";
		const std::string instructions = "Press SPACE to mute/unmute guitar stem";
		const std::string backingLabel = "song.ogg: ON";
		const std::string guitarLabel = "guitar.ogg: " + guitarStatus;
		const int titleScale = 3;
		const int bodyScale = 2;

		SDL_SetRenderDrawColor(renderer, 18, 28, 46, 255);
		SDL_RenderClear(renderer);

		font.DrawText(
			renderer,
			title,
			(kWindowWidth - font.MeasureTextWidth(title, titleScale)) / 2,
			150,
			titleScale);
		font.DrawText(
			renderer,
			instructions,
			(kWindowWidth - font.MeasureTextWidth(instructions, bodyScale)) / 2,
			260,
			bodyScale);
		font.DrawText(
			renderer,
			backingLabel,
			(kWindowWidth - font.MeasureTextWidth(backingLabel, bodyScale)) / 2,
			320,
			bodyScale);
		font.DrawText(
			renderer,
			guitarLabel,
			(kWindowWidth - font.MeasureTextWidth(guitarLabel, bodyScale)) / 2,
			360,
			bodyScale);

		SDL_RenderPresent(renderer);
	}

	ma_device_uninit(&audioDevice);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	IMG_Quit();
	SDL_Quit();

	return 0;
}
