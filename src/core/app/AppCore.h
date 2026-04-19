#pragma once

#include "core/menu/SongBrowser.h"
#include "core/play/SongSession.h"
#include "libretro_contract/RetroAudio.h"
#include "libretro_contract/RetroInput.h"
#include "libretro_contract/RetroTypes.h"

#include <string>

namespace rhythmreplugged
{
	class AppCore
	{
	public:
		explicit AppCore(IRetroFileSystem &file_system);

		bool retro_init(const std::string &song_root_path, std::string &error_message);
		void retro_run(const RetroInputState &input_state);
		void retro_deinit();
		bool set_browser_selected_index(int index);
		bool activate_browser_selection();
		void return_to_browser();
		void toggle_player_guitar_mute();
		size_t player_mute_change_count() const;

		AppMode mode() const;
		const SongBrowserView &song_browser_view() const;
		PrototypePlayerView prototype_player_view() const;
		const RetroAudioBatch &audio_batch() const;

	private:
		bool pressed(bool current, bool previous) const;
		void run_song_browser(const RetroInputState &input_state);
		void run_prototype_player(const RetroInputState &input_state);

		IRetroFileSystem &file_system_;
		SongBrowser song_browser_;
		SongSession song_session_;
		AppMode mode_ = AppMode::SongBrowser;
		RetroInputState previous_input_{};
		RetroAudioBatch audio_batch_{};
		std::string player_status_message_;
	};
}
