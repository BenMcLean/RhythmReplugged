#pragma once

#include "core/audio/StemCatalog.h"
#include "core/songs/SongIni.h"
#include "frontend_contract/AudioTypes.h"
#include "frontend_contract/RetroFileSystem.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rhythmreplugged::core
{
	class SongPlayer
	{
	public:
		static constexpr int kRuntimeSampleRate = 48000;
		static constexpr int kRuntimeChannelCount = 2;

		struct PlaybackState
		{
			size_t frame_index = 0;
			std::vector<float> current_gains;
			std::vector<float> target_gains;
		};

		struct PreloadedStemTrack
		{
			std::string stem_name;
			std::vector<std::int16_t> samples;
			int channels = kRuntimeChannelCount;
			int sample_rate = kRuntimeSampleRate;
			size_t frame_count = 0;
		};

		struct PreloadedSongData
		{
			SongMetadataView metadata;
			std::vector<PreloadedStemTrack> stems;
		};

		using DecodeProgressCallback = std::function<void(size_t processed_bytes, size_t total_bytes)>;

		bool load(::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system, const std::string &song_directory, std::string &error_message);
		bool load_preloaded(PreloadedSongData preloaded_song_data, std::string &error_message);
		void unload();
		bool is_loaded() const;
		void toggle_guitar_mute();
		bool guitar_muted() const;
		bool has_stem(std::string_view stem_name) const;
		size_t loaded_stem_count() const;
		void set_stem_target_gain(std::string_view stem_name, float gain);
		float stem_target_gain(std::string_view stem_name) const;
		int sample_rate() const;
		double duration_seconds() const;
		const SongMetadataView &metadata() const;
		bool playback_finished() const;
		void rewind();
		PlaybackState playback_state() const;
		bool restore_playback_state(const PlaybackState &state, std::string &error_message);
		void render_interleaved_s16(std::int16_t *output, size_t frame_count);
		::rhythmreplugged::frontend_contract::AudioBatch generate_audio_batch(size_t frame_count);
		static bool preload(::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system,
			const std::string &song_directory,
			PreloadedSongData &preloaded_song_data,
			std::string &error_message);
		static bool decode_preloaded_stem(const std::vector<std::uint8_t> &bytes,
			PreloadedStemTrack &track,
			std::string &error_message,
			const DecodeProgressCallback &progress_callback = {});

	private:
		struct StemTrack
		{
			std::string stem_name;
			std::vector<std::int16_t> samples;
			int channels = kRuntimeChannelCount;
			int sample_rate = kRuntimeSampleRate;
			size_t frame_count = 0;
			float current_gain = 1.0f;
			std::atomic<float> target_gain{1.0f};

			StemTrack() = default;
			StemTrack(const StemTrack &) = delete;
			StemTrack &operator=(const StemTrack &) = delete;
			StemTrack(StemTrack &&other) noexcept;
			StemTrack &operator=(StemTrack &&other) noexcept;
		};

		static bool decode_vorbis(const std::vector<std::uint8_t> &bytes,
			PreloadedStemTrack &track,
			std::string &error_message,
			const DecodeProgressCallback &progress_callback = {});
		static bool normalize_runtime_audio(PreloadedStemTrack &track, std::string &error_message);
		bool adopt_preloaded(PreloadedSongData preloaded_song_data, std::string &error_message);
		StemTrack *find_stem(std::string_view stem_name);
		const StemTrack *find_stem(std::string_view stem_name) const;
		std::int16_t sample_track_channel(const StemTrack &track, size_t frame_index, int channel) const;
		size_t longest_track_frame_count() const;

		std::vector<StemTrack> stems_;
		size_t frame_index_ = 0;
		SongMetadataView metadata_;
	};
}
