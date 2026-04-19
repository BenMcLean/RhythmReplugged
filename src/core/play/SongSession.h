#pragma once

#include "core/audio/PrototypePlayer.h"
#include "core/app/AppTypes.h"
#include "core/play/AudioMixer.h"
#include "core/play/Transport.h"
#include "libretro_contract/AudioTypes.h"

#include <atomic>
#include <string>

namespace rhythmreplugged
{
	class IRetroFileSystem;

	class SongSession
	{
	public:
		bool load(IRetroFileSystem &file_system, const std::string &song_directory, std::string &error_message);
		void unload();
		bool is_loaded() const;
		void toggle_guitar_mute();
		bool has_stem(std::string_view stem_name) const;
		size_t loaded_stem_count() const;
		void set_stem_target_gain(std::string_view stem_name, float gain);
		float stem_target_gain(std::string_view stem_name) const;
		int sample_rate() const;
		size_t emitted_frames() const;
		PrototypePlayerView view(const std::string &status_message) const;
		void render_interleaved_s16(std::int16_t *output, size_t frame_count);
		AudioBatch render_fixed_tick_audio(int ticks_per_second);
		double song_time_seconds() const;
		double song_time_beats(double beats_per_minute) const;

	private:
		PrototypePlayer prototype_player_;
		Transport transport_;
		AudioMixer audio_mixer_;
		std::atomic<bool> loaded_{false};
	};
}
