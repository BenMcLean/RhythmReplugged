#pragma once

#include "core/songs/SongIni.h"
#include "libretro_contract/RetroAudio.h"
#include "libretro_contract/RetroFileSystem.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rhythmreplugged
{
	class PrototypePlayer
	{
	public:
		bool load(IRetroFileSystem &file_system, const std::string &song_directory, std::string &error_message);
		void unload();
		bool is_loaded() const;
		void toggle_guitar_mute();
		bool guitar_muted() const;
		int sample_rate() const;
		const SongMetadataView &metadata() const;
		RetroAudioBatch generate_audio_batch(size_t frame_count);

	private:
		struct DecodedTrack
		{
			std::vector<float> samples;
			int channels = 0;
			int sample_rate = 0;
			size_t frame_count = 0;
		};

		static bool decode_vorbis(const std::vector<std::uint8_t> &bytes, DecodedTrack &track, std::string &error_message);
		float sample_track_channel(const DecodedTrack &track, size_t frame_index, int channel) const;

		DecodedTrack backing_track_;
		DecodedTrack guitar_track_;
		size_t frame_index_ = 0;
		bool guitar_muted_ = false;
		SongMetadataView metadata_;
	};
}
