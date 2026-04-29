#pragma once

#include "core/chart/MidiChart.h"
#include "core/audio/PrototypePlayer.h"
#include "core/app/AppTypes.h"
#include "core/play/AudioMixer.h"
#include "core/play/Transport.h"
#include "frontend_contract/AudioTypes.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace rhythmreplugged::core
{
	class SongSession
	{
	public:
		bool load(::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system,
			const std::string &song_directory,
			const GameplayOptions &options,
			std::string &error_message);
		bool load_preloaded(::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system,
			const std::string &song_directory,
			PrototypePlayer::PreloadedSongData preloaded_song_data,
			const GameplayOptions &options,
			std::string &error_message);
		bool reconfigure_loaded(::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system,
			const std::string &song_directory,
			const GameplayOptions &options,
			std::string &error_message);
		void unload();
		bool is_loaded() const;
		void toggle_guitar_mute();
		void update_gameplay_input(const std::array<bool, 5> &lane_held, const std::array<bool, 5> &lane_pressed);
		bool switch_active_lane(int delta);
		bool has_stem(std::string_view stem_name) const;
		size_t loaded_stem_count() const;
		void set_stem_target_gain(std::string_view stem_name, float gain);
		float stem_target_gain(std::string_view stem_name) const;
		int sample_rate() const;
		size_t emitted_frames() const;
		bool playback_finished() const;
		void set_timing_offset_seconds(double offset_seconds);
		double timing_offset_seconds() const;
		PrototypePlayerView view(const std::string &status_message) const;
		void render_interleaved_s16(std::int16_t *output, size_t frame_count);
		::rhythmreplugged::frontend_contract::AudioBatch render_fixed_tick_audio(int ticks_per_second);
		double song_time_seconds() const;
		double song_time_beats(double beats_per_minute) const;
		GameplayMode gameplay_mode() const;
		size_t gameplay_lane_count() const;
		int active_gameplay_lane_index() const;
		InstrumentLaneView gameplay_lane_view(size_t index) const;

	private:
		struct GameplayLaneState
		{
			InstrumentOption instrument = InstrumentOption::Guitar;
			HighwayInstrumentType instrument_type = HighwayInstrumentType::FiveFretGuitar;
			std::vector<std::string> stem_names;
			std::string instrument_label = "Guitar";
			MidiChart midi_chart;
			std::array<bool, 5> lane_held{};
			std::array<double, 5> lane_sustain_end_times_{};
			std::array<double, 5> lane_sustain_release_times_{};
			std::uint64_t input_generation = 0;
			std::uint64_t consumed_input_generation = 0;
			size_t next_note_index = 0;
		};

		static constexpr double kChartLookbehindSeconds = 0.35;
		static constexpr double kChartLookaheadSeconds = 3.0;
		static constexpr double kNoteHitWindowSeconds = 0.125;
		static constexpr double kSustainMinimumSeconds = 0.08;
		static constexpr double kSustainDropLeniencySeconds = 0.025;

		static std::uint8_t lane_mask_from_state(const std::array<bool, 5> &lanes);
		static bool held_mask_satisfies_expected(std::uint8_t held_mask, std::uint8_t expected_mask);
		bool configure_gameplay_lanes(
			::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system,
			const std::string &song_directory,
			const GameplayOptions &options,
			std::string &error_message);
		void reset_runtime_state(GameplayLaneState &lane);
		void set_lane_stem_target_gain(const GameplayLaneState &lane, float gain);
		float lane_stem_target_gain(const GameplayLaneState &lane) const;
		bool has_lane_stem(const GameplayLaneState &lane) const;
		size_t note_group_end_index(const GameplayLaneState &lane, size_t start_index) const;
		std::uint8_t note_group_lane_mask(const GameplayLaneState &lane, size_t start_index, size_t end_index) const;
		std::uint8_t imminent_note_lane_mask(const GameplayLaneState &lane, double song_time_seconds) const;
		void refresh_active_sustains(GameplayLaneState &lane, double song_time_seconds, std::uint8_t held_mask);
		std::uint8_t active_sustain_lane_mask(const GameplayLaneState &lane, double song_time_seconds) const;
		void start_sustains_for_note_group(GameplayLaneState &lane, size_t start_index, size_t end_index);
		void consume_missed_note_groups(GameplayLaneState &lane, double song_time_seconds);
		void advance_inactive_lane(GameplayLaneState &lane, double song_time_seconds);
		GameplayLaneState *active_lane();
		const GameplayLaneState *active_lane() const;
		double adjusted_song_time_seconds() const;

		PrototypePlayer prototype_player_;
		Transport transport_;
		AudioMixer audio_mixer_;
		GameplayMode gameplay_mode_ = GameplayMode::Classic;
		std::vector<GameplayLaneState> gameplay_lanes_;
		int active_lane_index_ = 0;
		std::string chart_status_message_;
		double timing_offset_seconds_ = 0.0;
		std::atomic<bool> loaded_{false};
	};
}
