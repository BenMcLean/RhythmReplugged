#pragma once

#include "core/chart/MidiChart.h"
#include "core/audio/SongPlayer.h"
#include "core/app/AppTypes.h"
#include "core/play/AudioMixer.h"
#include "core/play/Transport.h"
#include "frontend_contract/AudioTypes.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rhythmreplugged::core
{
	class SongSession
	{
	public:
		struct GameplayLaneRuntimeState
		{
			std::array<bool, 5> lane_held{};
			std::array<double, 5> lane_sustain_end_times_{};
			std::array<double, 5> lane_sustain_release_times_{};
			std::uint64_t input_generation = 0;
			std::uint64_t consumed_input_generation = 0;
			size_t next_note_index = 0;
			float stem_target_gain = 1.0f;
			LaneLockState lock_state = LaneLockState::Unlocked;
			double lock_start_time_seconds = 0.0;
			double lock_end_time_seconds = 0.0;
			double ready_lock_start_time_seconds = 0.0;
			double ready_lock_end_time_seconds = 0.0;
			size_t ready_lock_note_index = 0;
			float lock_progress = 0.0f;
			double last_missed_note_time_seconds = -1.0;
			int successful_replugged_measures = 0;
			double count_eligible_measure_start_seconds = 0.0;
			bool lock_ready = false;
			bool is_actionable = false;
			bool should_prompt = false;
			std::uint32_t event_flags = 0;
		};

		struct PlayState
		{
			GameplayMode gameplay_mode = GameplayMode::Classic;
			int active_lane_index = 0;
			double timing_offset_seconds = 0.0;
			std::vector<GameplayLaneRuntimeState> lanes;
		};

		bool load(::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system,
			const std::string &song_directory,
			const MidiChart &song_chart,
			const GameplayOptions &options,
			std::string &error_message);
		bool load_preloaded(::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system,
			const std::string &song_directory,
			const MidiChart &song_chart,
			SongPlayer::PreloadedSongData preloaded_song_data,
			const GameplayOptions &options,
			std::string &error_message);
		bool reconfigure_loaded(const MidiChart &song_chart,
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
		size_t play_state_serialized_size() const;
		bool serialize_play_state(std::vector<std::uint8_t> &bytes, std::string &error_message) const;
		bool deserialize_play_state(const std::uint8_t *data, size_t size, std::string &error_message);
		SongPlayerView view(const std::string &status_message) const;
		void refresh_frame_snapshot(const std::string &status_message);
		const GameplayFrameSnapshot &frame_snapshot() const;
		void render_interleaved_s16(std::int16_t *output, size_t frame_count);
		::rhythmreplugged::frontend_contract::AudioBatch render_fixed_tick_audio(int ticks_per_second);
		double song_time_seconds() const;
		double song_time_beats(double beats_per_minute) const;
		GameplayMode gameplay_mode() const;
		size_t gameplay_lane_count() const;
		int active_gameplay_lane_index() const;
		InstrumentLaneView gameplay_lane_view(size_t index) const;

	private:
		struct LyricPhraseRange
		{
			double start_seconds = 0.0;
			double end_seconds = 0.0;
		};

		struct CachedLyricToken
		{
			std::string text;
			double start_seconds = 0.0;
			double end_seconds = 0.0;
			bool prepend_space = false;
			bool append_hyphen = false;
			int line_index = 0;
		};

		struct GameplayLaneDefinition
		{
			InstrumentOption instrument = InstrumentOption::Guitar;
			HighwayInstrumentType instrument_type = HighwayInstrumentType::FiveFretGuitar;
			std::vector<std::string> stem_names;
			std::string instrument_label = "Guitar";
			MidiChart midi_chart;
			std::vector<CachedLyricToken> lyric_tokens;
			std::vector<LyricPhraseRange> lyric_phrase_ranges;
		};

		struct LaneFrameCache
		{
			std::vector<HighwayNoteView> visible_highway_notes;
			std::vector<HighwayMeasureLineView> visible_highway_measure_lines;
			std::vector<SongPlayerView::ChartNoteView> visible_chart_notes;
			std::vector<SongPlayerView::ChartMeasureLineView> visible_chart_measure_lines;
			std::vector<SongPlayerView::LyricTokenView> visible_lyric_tokens;
			int current_lyric_line_index = 0;
			int next_lyric_line_index = -1;
		};

		static constexpr double kChartLookbehindSeconds = 0.35;
		static constexpr double kChartLookaheadSeconds = 3.0;
		static constexpr double kNoteHitWindowSeconds = 0.125;
		static constexpr double kSustainMinimumSeconds = 0.08;
		static constexpr double kSustainDropLeniencySeconds = 0.025;
		static constexpr int kRepluggedMeasuresRequiredToLock = 2;
		static constexpr double kRepluggedActionableLookaheadSeconds = 1.2;

		static std::uint8_t lane_mask_from_state(const std::array<bool, 5> &lanes);
		static bool held_mask_satisfies_expected(std::uint8_t held_mask, std::uint8_t expected_mask);
		bool configure_gameplay_lanes(
			const MidiChart &song_chart,
			const GameplayOptions &options,
			std::string &error_message);
		void reset_runtime_state(GameplayLaneRuntimeState &lane);
		void cache_lyric_data(GameplayLaneDefinition &lane);
		void refresh_lane_frame_cache(double song_time_seconds);
		void populate_lane_frame_cache(size_t lane_index, double song_time_seconds);
		void rebuild_cached_scene(GameplaySceneView &scene) const;
		void rebuild_cached_player_view(SongPlayerView &player_view, const std::string &status_message) const;
		void set_lane_stem_target_gain(size_t lane_index, float gain);
		float lane_stem_target_gain(size_t lane_index) const;
		bool has_lane_stem(size_t lane_index) const;
		size_t note_group_end_index(size_t lane_index, size_t start_index) const;
		std::uint8_t note_group_lane_mask(size_t lane_index, size_t start_index, size_t end_index) const;
		std::uint8_t imminent_note_lane_mask(size_t lane_index, double song_time_seconds) const;
		void refresh_active_sustains(size_t lane_index, double song_time_seconds, std::uint8_t held_mask);
		std::uint8_t active_sustain_lane_mask(size_t lane_index, double song_time_seconds) const;
		void start_sustains_for_note_group(size_t lane_index, size_t start_index, size_t end_index);
		void consume_missed_note_groups(size_t lane_index, double song_time_seconds);
		void advance_inactive_lane(size_t lane_index, double song_time_seconds);
		void update_replugged_lane_state(size_t lane_index, double song_time_seconds, bool is_active_lane);
		void advance_replugged_measure_progress(size_t lane_index, double song_time_seconds);
		void advance_replugged_measure_progress_on_hit(size_t lane_index, double note_time_seconds);
		void lock_replugged_lane(size_t lane_index, double lock_start_time_seconds, double lock_end_time_seconds);
		bool try_commit_replugged_ready_lock(size_t lane_index, double song_time_seconds);
		size_t first_note_index_at_or_after(size_t lane_index, double song_time_seconds) const;
		std::optional<std::pair<double, double>> predicted_replugged_lock_range(size_t lane_index, double song_time_seconds) const;
		bool lane_has_actionable_note(size_t lane_index, double song_time_seconds) const;
		int initial_active_lane_index() const;
		void apply_replugged_keep_busy_rule(double song_time_seconds);
		std::optional<std::pair<double, double>> replugged_section_range_for_time(size_t lane_index, double song_time_seconds) const;
		std::optional<std::pair<double, double>> replugged_section_range_for_note_index(size_t lane_index, size_t note_index) const;
		std::optional<std::pair<double, double>> next_replugged_lock_range(size_t lane_index, size_t note_index, double earliest_measure_start_seconds) const;
		std::optional<std::pair<double, double>> next_replugged_empty_measure_range(size_t lane_index, double song_time_seconds) const;
		double next_measure_boundary_at_or_after(size_t lane_index, double song_time_seconds) const;
		bool lane_measure_has_notes(size_t lane_index, double measure_start_seconds, double measure_end_seconds) const;
		bool any_other_lane_has_notes_in_measure(size_t excluded_lane_index, double measure_start_seconds, double measure_end_seconds) const;
		std::optional<double> lane_measure_last_note_time(size_t lane_index, double measure_start_seconds, double measure_end_seconds) const;
		std::optional<std::pair<double, double>> replugged_lock_build_window(size_t lane_index) const;
		double earliest_replugged_lock_start_after_required_measure(size_t lane_index, double measure_start_seconds, double measure_end_seconds) const;
		double advance_measure_boundary(size_t lane_index, double measure_end_seconds, int measure_count) const;
		double retreat_measure_boundary(size_t lane_index, double measure_end_seconds, int measure_count) const;
		bool other_lane_unlocks_too_close(size_t excluded_lane_index, double measure_end_seconds) const;
		float replugged_lock_build_progress(size_t lane_index, double song_time_seconds) const;
		size_t active_lane_index() const;
		double adjusted_song_time_seconds() const;
		std::uint64_t session_fingerprint() const;

		SongPlayer song_player_;
		Transport transport_;
		AudioMixer audio_mixer_;
		std::vector<GameplayLaneDefinition> gameplay_lanes_;
		PlayState play_state_;
		std::vector<LaneFrameCache> lane_frame_cache_;
		GameplayFrameSnapshot frame_snapshot_;
		std::string chart_status_message_;
		std::atomic<bool> loaded_{false};
	};
}
