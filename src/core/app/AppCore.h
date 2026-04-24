#pragma once

#include "core/app/AppLaunch.h"
#include "core/app/AppTypes.h"
#include "core/menu/SongBrowser.h"
#include "core/play/SongSession.h"
#include "libretro_contract/AudioTypes.h"
#include "libretro_contract/RetroInput.h"

#include <atomic>
#include <string>

namespace rhythmreplugged
{
	class AppCore : public IAudioStream
	{
	public:
		explicit AppCore(IRetroFileSystem &file_system);

		bool retro_init(const std::string &song_root_path, std::string &error_message);
		bool retro_init(const AppLaunchRequest &launch_request, std::string &error_message);
		void retro_run(const RetroInputState &input_state);
		void retro_deinit();
		void set_audio_batch_enabled(bool enabled);
		bool set_browser_selected_index(int index);
		bool activate_browser_selection();
		void return_to_browser();
		void toggle_player_guitar_mute();
		void nudge_timing_offset_seconds(double delta_seconds);
		void reset_timing_offset();
		void finalize_audio_stop();
		int sample_rate() const override;
		void render_interleaved_s16(std::int16_t *output, size_t frame_count) override;

		AppMode mode() const;
		const SongBrowserView &song_browser_view() const;
		PrototypePlayerView prototype_player_view() const;
		GameplaySceneView gameplay_scene_view() const;
		const AudioBatch &audio_batch() const;

	private:
		bool activate_browser_selection_unlocked();
		void return_to_browser_unlocked();
		void toggle_player_guitar_mute_unlocked();
		void update_player_status_message();
		bool pressed(bool current, bool previous) const;
		void run_song_browser(const RetroInputState &input_state);
		void run_prototype_player(const RetroInputState &input_state);

		IRetroFileSystem &file_system_;
		SongBrowser song_browser_;
		SongSession song_session_;
		std::atomic<AppMode> mode_{AppMode::SongBrowser};
		RetroInputState previous_input_{};
		AudioBatch audio_batch_{};
		std::string player_status_message_;
		bool session_unload_pending_ = false;
		bool audio_batch_enabled_ = false;
	};
}
