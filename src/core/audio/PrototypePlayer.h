#pragma once

#include "core/songs/SongIni.h"
#include "libretro_contract/RetroAudio.h"
#include "libretro_contract/RetroFileSystem.h"

#include <atomic>
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
		void set_stem_target_gain(StemId stem_id, float gain);
		float stem_target_gain(StemId stem_id) const;
		int sample_rate() const;
		const SongMetadataView &metadata() const;
		void render_interleaved_s16(std::int16_t *output, size_t frame_count);
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
		float backing_current_gain_ = 1.0f;
		std::atomic<float> backing_target_gain_{1.0f};
		float guitar_current_gain_ = 1.0f;
		std::atomic<float> guitar_target_gain_{1.0f};
		SongMetadataView metadata_;
	};
}
