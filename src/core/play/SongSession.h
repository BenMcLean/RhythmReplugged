#pragma once

#include "core/audio/PrototypePlayer.h"
#include "core/audio/StemTypes.h"
#include "core/play/AudioMixer.h"
#include "core/play/Transport.h"
#include "libretro_contract/RetroAudio.h"
#include "libretro_contract/RetroTypes.h"

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
		void set_stem_target_gain(StemId stem_id, float gain);
		float stem_target_gain(StemId stem_id) const;
		int sample_rate() const;
		size_t emitted_frames() const;
		PrototypePlayerView view(const std::string &status_message) const;
		void render_interleaved_s16(std::int16_t *output, size_t frame_count);
		RetroAudioBatch render_fixed_tick_audio(int ticks_per_second);
		double song_time_seconds() const;
		double song_time_beats(double beats_per_minute) const;

	private:
		PrototypePlayer prototype_player_;
		Transport transport_;
		AudioMixer audio_mixer_;
		std::atomic<bool> loaded_{false};
	};
}
