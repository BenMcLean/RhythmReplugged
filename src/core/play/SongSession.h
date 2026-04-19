#pragma once

#include "core/audio/PrototypePlayer.h"
#include "core/play/AudioMixer.h"
#include "core/play/Transport.h"
#include "libretro_contract/RetroAudio.h"
#include "libretro_contract/RetroTypes.h"

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
		size_t mute_change_count() const;
		PrototypePlayerView view(const std::string &status_message) const;
		RetroAudioBatch render_audio_tick(int ticks_per_second);
		double song_time_seconds() const;

	private:
		PrototypePlayer prototype_player_;
		Transport transport_;
		AudioMixer audio_mixer_;
		size_t mute_change_count_ = 0;
	};
}
