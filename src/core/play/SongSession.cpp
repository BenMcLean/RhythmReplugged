#include "core/play/SongSession.h"

#include "frontend_contract/RetroFileSystem.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <stdexcept>

namespace rhythmreplugged::core
{
	namespace
	{
		struct ParsedLyricDisplay
		{
			std::string text;
			bool join_next_without_space = false;
			bool force_new_line_before = false;
			bool is_spacer_only = false;
			bool append_hyphen = false;
		};

		struct LyricPhraseRange
		{
			double start_seconds = 0.0;
			double end_seconds = 0.0;
		};

		constexpr std::uint32_t kSerializedPlayStateMagic = 0x52525053u; // RRPS
		constexpr std::uint32_t kSerializedPlayStateVersion = 11u;

		struct SerializedPlayStateHeader
		{
			std::uint32_t magic = kSerializedPlayStateMagic;
			std::uint32_t version = kSerializedPlayStateVersion;
			std::uint32_t payload_size = 0;
			std::uint64_t session_fingerprint = 0;
			std::uint32_t gameplay_mode = 0;
			std::uint32_t lane_count = 0;
			std::int32_t active_lane_index = 0;
			double timing_offset_seconds = 0.0;
			std::int32_t sample_rate = 0;
			std::uint64_t frame_remainder = 0;
			std::uint64_t emitted_frames = 0;
			std::uint64_t audio_frame_index = 0;
			std::uint32_t audio_stem_count = 0;
		};

		struct SerializedLaneRuntimeState
		{
			std::uint8_t lane_held_mask = 0;
			std::uint8_t lock_state = 0;
			std::uint8_t is_actionable = 0;
			std::uint8_t should_prompt = 0;
			double lane_sustain_end_times[5]{};
			double lane_sustain_release_times[5]{};
			std::uint64_t input_generation = 0;
			std::uint64_t consumed_input_generation = 0;
			std::uint64_t next_note_index = 0;
			float stem_target_gain = 1.0f;
			std::int32_t lock_start_measure_index = -1;
			std::int32_t lock_end_measure_index = -1;
			double lock_start_time_seconds = 0.0;
			double lock_end_time_seconds = 0.0;
			std::int32_t ready_lock_start_measure_index = -1;
			std::int32_t ready_lock_end_measure_index = -1;
			double ready_lock_start_time_seconds = 0.0;
			double ready_lock_end_time_seconds = 0.0;
			std::uint64_t ready_lock_note_index = 0;
			float lock_progress = 0.0f;
			double last_missed_note_time_seconds = -1.0;
			std::int32_t successful_replugged_measures = 0;
			std::int32_t count_eligible_measure_index = 0;
			double count_eligible_measure_start_seconds = 0.0;
			std::uint8_t lock_ready = 0;
			std::uint32_t event_flags = 0;
		};

		template <typename T>
		void append_bytes(std::vector<std::uint8_t> &bytes, const T &value)
		{
			const size_t write_offset = bytes.size();
			bytes.resize(write_offset + sizeof(T));
			std::memcpy(bytes.data() + write_offset, &value, sizeof(T));
		}

		template <typename T>
		bool read_bytes(const std::uint8_t *data, size_t size, size_t &offset, T &value)
		{
			if (offset > size || size - offset < sizeof(T))
				return false;

			std::memcpy(&value, data + offset, sizeof(T));
			offset += sizeof(T);
			return true;
		}

		void hash_bytes(std::uint64_t &hash, const void *data, size_t size)
		{
			const auto *bytes = static_cast<const std::uint8_t *>(data);
			for (size_t index = 0; index < size; ++index)
			{
				hash ^= static_cast<std::uint64_t>(bytes[index]);
				hash *= 1099511628211ull;
			}
		}

		template <typename T>
		void hash_value(std::uint64_t &hash, const T &value)
		{
			hash_bytes(hash, &value, sizeof(T));
		}

		void hash_string(std::uint64_t &hash, std::string_view value)
		{
			const std::uint64_t size = static_cast<std::uint64_t>(value.size());
			hash_value(hash, size);
			if (!value.empty())
				hash_bytes(hash, value.data(), value.size());
		}

		bool is_displayable_lyric(const MidiChartTextEvent &event)
		{
			return event.type == MidiChartTextEventType::Lyric && !event.text.empty();
		}

		bool starts_with_punctuation(std::string_view text)
		{
			if (text.empty())
				return false;

			switch (text.front())
			{
			case '.':
			case '!':
			case '?':
			case ',':
			case ':':
			case ';':
			case ')':
			case ']':
				return true;
			default:
				return false;
			}
		}

		ParsedLyricDisplay parse_lyric_display(std::string_view text)
		{
			ParsedLyricDisplay parsed;
			while (!text.empty())
			{
				if (text.size() >= 2 && text[0] == '\\' && text[1] == 'n')
				{
					parsed.force_new_line_before = true;
					text.remove_prefix(2);
					continue;
				}

				if (text.front() == '/' || text.front() == '\\')
				{
					parsed.force_new_line_before = true;
					text.remove_prefix(1);
					continue;
				}

				break;
			}

			parsed.text = std::string(text);
			if (parsed.text == "+")
			{
				parsed.text.clear();
				parsed.is_spacer_only = true;
				return parsed;
			}

			while (!parsed.text.empty() && parsed.text.back() == '#')
				parsed.text.pop_back();

			if (!parsed.text.empty())
			{
				const char tail = parsed.text.back();
				if (tail == '-')
				{
					parsed.join_next_without_space = true;
					parsed.text.pop_back();
				}
				else if (tail == '=')
				{
					parsed.join_next_without_space = true;
					parsed.append_hyphen = true;
					parsed.text.pop_back();
				}
			}

			return parsed;
		}

		bool is_vocal_phrase(const MidiChartPhrase &phrase)
		{
			return phrase.type == MidiChartPhraseType::VocalsPhrase;
		}

		const MidiChartTrack *preferred_vocal_track(const MidiChart &chart)
		{
			for (const MidiChartTrack &track : chart.tracks())
			{
				if (track.type == MidiChartTrackType::Vocals)
					return &track;
			}
			for (const MidiChartTrack &track : chart.tracks())
			{
				if (track.type == MidiChartTrackType::Harmony1 ||
					track.type == MidiChartTrackType::Harmony2 ||
					track.type == MidiChartTrackType::Harmony3)
					return &track;
			}
			return nullptr;
		}

		std::vector<MidiChartTextEvent> collect_preferred_vocal_lyrics(const MidiChart &chart)
		{
			const MidiChartTrack *track = preferred_vocal_track(chart);
			if (track == nullptr)
				return {};
			std::vector<MidiChartTextEvent> lyrics;
			for (const MidiChartTextEvent &event : track->text_events)
			{
				if (event.type == MidiChartTextEventType::Lyric && !event.text.empty())
					lyrics.push_back(event);
			}
			return lyrics;
		}

		std::vector<LyricPhraseRange> collect_lyric_phrase_ranges(const MidiChart &chart)
		{
			std::vector<LyricPhraseRange> ranges;
			const MidiChartTrack *track = preferred_vocal_track(chart);
			if (track == nullptr)
				return ranges;

			for (const MidiChartPhrase &phrase : track->phrases)
			{
				if (!is_vocal_phrase(phrase))
					continue;

				LyricPhraseRange range;
				range.start_seconds = phrase.start_seconds;
				range.end_seconds = phrase.end_seconds;
				ranges.push_back(range);
			}

			std::sort(ranges.begin(), ranges.end(),
				[](const LyricPhraseRange &left, const LyricPhraseRange &right)
				{
					return left.start_seconds < right.start_seconds;
				});
			ranges.erase(std::unique(ranges.begin(), ranges.end(),
				[](const LyricPhraseRange &left, const LyricPhraseRange &right)
				{
					return std::fabs(left.start_seconds - right.start_seconds) < 0.001 &&
						std::fabs(left.end_seconds - right.end_seconds) < 0.001;
				}),
				ranges.end());
			return ranges;
		}

		[[noreturn]] void fail_invalid_replugged_boundary(const char *context)
		{
			throw std::logic_error(std::string("Invalid Replugged lock boundary: ") + context);
		}
		
		MidiChartDifficulty to_midi_chart_difficulty(DifficultyOption difficulty)
		{
			switch (difficulty)
			{
			case DifficultyOption::Easy:
				return MidiChartDifficulty::Easy;
			case DifficultyOption::Medium:
				return MidiChartDifficulty::Medium;
			case DifficultyOption::Hard:
				return MidiChartDifficulty::Hard;
			case DifficultyOption::Expert:
				return MidiChartDifficulty::Expert;
			}

			return MidiChartDifficulty::Medium;
		}

		MidiChartTrackType to_midi_chart_track_type(InstrumentOption instrument)
		{
			switch (instrument)
			{
			case InstrumentOption::Guitar:
				return MidiChartTrackType::FiveFretGuitar;
			case InstrumentOption::Bass:
				return MidiChartTrackType::FiveFretBass;
			case InstrumentOption::Rhythm:
				return MidiChartTrackType::FiveFretRhythm;
			case InstrumentOption::CoopGuitar:
				return MidiChartTrackType::FiveFretCoop;
			case InstrumentOption::Keys:
				return MidiChartTrackType::FiveFretKeys;
			case InstrumentOption::Drums:
				return MidiChartTrackType::Drums;
			}

			return MidiChartTrackType::FiveFretGuitar;
		}

		HighwayInstrumentType to_highway_instrument_type(InstrumentOption instrument)
		{
			switch (instrument)
			{
			case InstrumentOption::Drums:
				return HighwayInstrumentType::FiveLaneDrums;
			case InstrumentOption::Bass:
				return HighwayInstrumentType::Bass;
			case InstrumentOption::Guitar:
			case InstrumentOption::Rhythm:
			case InstrumentOption::CoopGuitar:
			case InstrumentOption::Keys:
			default:
				return HighwayInstrumentType::FiveFretGuitar;
			}
		}

		std::optional<InstrumentOption> to_instrument_option(MidiChartTrackType track_type)
		{
			switch (track_type)
			{
			case MidiChartTrackType::FiveFretGuitar:
				return InstrumentOption::Guitar;
			case MidiChartTrackType::FiveFretBass:
				return InstrumentOption::Bass;
			case MidiChartTrackType::FiveFretRhythm:
				return InstrumentOption::Rhythm;
			case MidiChartTrackType::FiveFretCoop:
				return InstrumentOption::CoopGuitar;
			case MidiChartTrackType::FiveFretKeys:
				return InstrumentOption::Keys;
			case MidiChartTrackType::Drums:
				return InstrumentOption::Drums;
			default:
				return std::nullopt;
			}
		}

		std::vector<std::string> stem_names_for(InstrumentOption instrument)
		{
			switch (instrument)
			{
			case InstrumentOption::Guitar:
				return {"guitar"};
			case InstrumentOption::Bass:
				return {"bass"};
			case InstrumentOption::Rhythm:
				return {"rhythm"};
			case InstrumentOption::CoopGuitar:
				return {"guitar"};
			case InstrumentOption::Keys:
				return {"keys"};
			case InstrumentOption::Drums:
				return {"drums", "drums_1", "drums_2", "drums_3", "drums_4"};
			}

			return {"guitar"};
		}

		std::string instrument_label_for(InstrumentOption instrument)
		{
			switch (instrument)
			{
			case InstrumentOption::Guitar:
				return "Guitar";
			case InstrumentOption::Bass:
				return "Bass";
			case InstrumentOption::Rhythm:
				return "Rhythm";
			case InstrumentOption::CoopGuitar:
				return "Co-op Guitar";
			case InstrumentOption::Keys:
				return "Keys";
			case InstrumentOption::Drums:
				return "Drums";
			}

			return "Guitar";
		}

		std::string difficulty_label_for(DifficultyOption difficulty)
		{
			switch (difficulty)
			{
			case DifficultyOption::Easy:
				return "Easy";
			case DifficultyOption::Medium:
				return "Medium";
			case DifficultyOption::Hard:
				return "Hard";
			case DifficultyOption::Expert:
				return "Expert";
			}

			return "Medium";
		}

		bool track_has_exact_difficulty(const MidiChart &chart, InstrumentOption instrument, DifficultyOption difficulty)
		{
			const MidiChartTrackType track_type = to_midi_chart_track_type(instrument);
			const MidiChartDifficulty midi_difficulty = to_midi_chart_difficulty(difficulty);
			for (const MidiChartTrack &track : chart.tracks())
			{
				if (track.type != track_type)
					continue;

				for (const MidiChartParsedNote &note : track.parsed_notes)
				{
					if (note.difficulty != midi_difficulty)
						continue;

					const bool is_supported_note =
						(track_type == MidiChartTrackType::Drums && note.category == MidiChartNoteCategory::Drums && note.lane >= 0 && note.lane <= 5) ||
						(track_type != MidiChartTrackType::Drums && note.category == MidiChartNoteCategory::FiveFret && note.lane >= 1 && note.lane <= 5);
					if (is_supported_note)
						return true;
				}

				return false;
			}

			return false;
		}

		int instrument_sort_rank(InstrumentOption instrument)
		{
			switch (instrument)
			{
			case InstrumentOption::Drums:
				return 0;
			case InstrumentOption::Bass:
				return 1;
			case InstrumentOption::Guitar:
				return 2;
			case InstrumentOption::Rhythm:
				return 3;
			case InstrumentOption::CoopGuitar:
				return 4;
			case InstrumentOption::Keys:
				return 5;
			}

			return 99;
		}

		void layout_freeplay_lanes(std::vector<InstrumentLaneView> &lanes, int focused_lane_index)
		{
			if (lanes.empty())
				return;

			const float base_width = std::clamp(2.6f - 0.5f * static_cast<float>(lanes.size() - 1), 0.55f, 2.6f);
			const float active_width = std::min(base_width + 0.9f, 3.2f);
			const float gap = std::clamp(0.28f - 0.04f * static_cast<float>(lanes.size() - 1), 0.10f, 0.28f);

			float cursor_x = 0.0f;
			for (size_t index = 0; index < lanes.size(); ++index)
			{
				InstrumentLaneView &lane = lanes[index];
				lane.lane_width = static_cast<int>(index) == focused_lane_index ? active_width : base_width;
				lane.lane_center_x = cursor_x + lane.lane_width * 0.5f;
				cursor_x += lane.lane_width + gap;
				lane.lane_depth_offset = 0.0f;
			}

			const float focus_center = lanes[static_cast<size_t>(std::clamp(
				focused_lane_index,
				0,
				static_cast<int>(lanes.size()) - 1))].lane_center_x;
			for (InstrumentLaneView &lane : lanes)
				lane.lane_center_x -= focus_center;
		}
	}

	bool SongSession::load(::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system,
		const std::string &song_directory,
		const MidiChart &song_chart,
		const GameplayOptions &options,
		std::string &error_message)
	{
		return load_preloaded(file_system, song_directory, song_chart, {}, options, error_message);
	}

	bool SongSession::load_preloaded(::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system,
		const std::string &song_directory,
		const MidiChart &song_chart,
		SongPlayer::PreloadedSongData preloaded_song_data,
		const GameplayOptions &options,
		std::string &error_message)
	{
		unload();
		if (!preloaded_song_data.stems.empty())
		{
			if (!song_player_.load_preloaded(std::move(preloaded_song_data), error_message))
				return false;
		}
		else if (!song_player_.load(file_system, song_directory, error_message))
		{
			return false;
		}

		return reconfigure_loaded(song_chart, options, error_message);
	}

	bool SongSession::reconfigure_loaded(const MidiChart &song_chart,
		const GameplayOptions &options,
		std::string &error_message)
	{
		if (!song_player_.is_loaded())
		{
			error_message = "Song audio is not loaded.";
			return false;
		}

		song_player_.rewind();
		transport_.configure(song_player_.sample_rate());
		audio_mixer_.set_song_player(&song_player_);
		if (!configure_gameplay_lanes(song_chart, options, error_message))
			return false;
		refresh_frame_snapshot({});
		loaded_.store(true);
		error_message.clear();
		return true;
	}

	void SongSession::unload()
	{
		audio_mixer_.reset();
		transport_.reset();
		gameplay_lanes_.clear();
		play_state_ = {};
		lane_frame_cache_.clear();
		frame_snapshot_ = {};
		chart_status_message_.clear();
		transient_gameplay_status_message_.clear();
		transient_gameplay_status_until_seconds_ = -1.0;
		replugged_keep_busy_engaged_ = false;
		song_player_.unload();
		loaded_.store(false);
	}

	bool SongSession::is_loaded() const
	{
		return loaded_.load();
	}

	void SongSession::toggle_guitar_mute()
	{
		const size_t lane_index = active_lane_index();
		if (lane_index < gameplay_lanes_.size())
			set_lane_stem_target_gain(lane_index, lane_stem_target_gain(lane_index) > 0.5f ? 0.0f : 1.0f);
	}

	void SongSession::update_gameplay_input(const std::array<bool, 5> &lane_held, const std::array<bool, 5> &lane_pressed)
	{
		const size_t lane_index = active_lane_index();
		if (lane_index >= play_state_.lanes.size())
			return;
		GameplayLaneRuntimeState &lane = play_state_.lanes[lane_index];
		const double current_time_seconds = song_time_seconds();
		for (size_t index = 0; index < gameplay_lanes_.size(); ++index)
		{
			if (play_state_.gameplay_mode == GameplayMode::Replugged)
				update_replugged_lane_state(index, current_time_seconds, index == lane_index);
			else if (index != lane_index)
				advance_inactive_lane(index, current_time_seconds);
		}
		if (play_state_.gameplay_mode == GameplayMode::Replugged)
			apply_replugged_keep_busy_rule(current_time_seconds);
		if (lane_held != lane.lane_held)
			++lane.input_generation;
		lane.lane_held = lane_held;
		if (!is_loaded() || gameplay_lanes_[lane_index].midi_chart.notes().empty())
		{
			set_lane_stem_target_gain(lane_index, 1.0f);
			return;
		}

		if (play_state_.gameplay_mode == GameplayMode::Replugged &&
			lane.lock_state == LaneLockState::Locked &&
			lane.lock_end_time_seconds > current_time_seconds)
		{
			lane.lane_held.fill(false);
			set_lane_stem_target_gain(lane_index, 1.0f);
			return;
		}

		consume_missed_note_groups(lane_index, current_time_seconds);
		const std::uint8_t held_mask = lane_mask_from_state(lane.lane_held);

		const std::uint8_t pressed_mask = lane_mask_from_state(lane_pressed);
		bool resolved_note_hit = false;
		if (pressed_mask != 0 && lane.next_note_index < gameplay_lanes_[lane_index].midi_chart.notes().size())
		{
			const std::vector<MidiChartNote> &notes = gameplay_lanes_[lane_index].midi_chart.notes();
			const size_t group_end_index = note_group_end_index(lane_index, lane.next_note_index);
			const double note_time_seconds = notes[lane.next_note_index].start_seconds;
			if (std::fabs(note_time_seconds - current_time_seconds) <= kNoteHitWindowSeconds)
			{
				const std::uint8_t expected_mask = note_group_lane_mask(lane_index, lane.next_note_index, group_end_index);
				if (held_mask_satisfies_expected(held_mask, expected_mask))
				{
					start_sustains_for_note_group(lane_index, lane.next_note_index, group_end_index);
					lane.next_note_index = group_end_index;
					lane.consumed_input_generation = lane.input_generation;
					resolved_note_hit = true;
					if (play_state_.gameplay_mode == GameplayMode::Replugged)
					{
						advance_replugged_measure_progress_on_hit(lane_index, note_time_seconds);
						try_commit_replugged_ready_lock(lane_index, current_time_seconds);
						set_lane_stem_target_gain(lane_index, 1.0f);
					}
				}
				else
				{
					set_lane_stem_target_gain(lane_index, 0.0f);
				}
			}
		}

		if (!resolved_note_hit && lane.next_note_index < gameplay_lanes_[lane_index].midi_chart.notes().size())
		{
			const std::vector<MidiChartNote> &notes = gameplay_lanes_[lane_index].midi_chart.notes();
			const size_t group_end_index = note_group_end_index(lane_index, lane.next_note_index);
			const double note_time_seconds = notes[lane.next_note_index].start_seconds;
			const std::uint8_t expected_mask = note_group_lane_mask(lane_index, lane.next_note_index, group_end_index);
			if (lane.input_generation > lane.consumed_input_generation &&
				std::fabs(note_time_seconds - current_time_seconds) <= kNoteHitWindowSeconds &&
				held_mask_satisfies_expected(held_mask, expected_mask))
			{
				start_sustains_for_note_group(lane_index, lane.next_note_index, group_end_index);
				lane.next_note_index = group_end_index;
				lane.consumed_input_generation = lane.input_generation;
				resolved_note_hit = true;
				if (play_state_.gameplay_mode == GameplayMode::Replugged)
				{
					advance_replugged_measure_progress_on_hit(lane_index, note_time_seconds);
					try_commit_replugged_ready_lock(lane_index, current_time_seconds);
					set_lane_stem_target_gain(lane_index, 1.0f);
				}
			}
		}

		refresh_active_sustains(lane_index, current_time_seconds, held_mask);
		const std::uint8_t sustain_mask = active_sustain_lane_mask(lane_index, current_time_seconds);
		if (sustain_mask != 0)
		{
			const std::uint8_t imminent_note_mask = imminent_note_lane_mask(lane_index, current_time_seconds);
			const std::uint8_t required_sustain_mask = static_cast<std::uint8_t>(sustain_mask & ~imminent_note_mask);
			if (required_sustain_mask == 0 || held_mask_satisfies_expected(held_mask, required_sustain_mask))
				set_lane_stem_target_gain(lane_index, 1.0f);
			else
				set_lane_stem_target_gain(lane_index, 0.0f);
			return;
		}

		if (play_state_.gameplay_mode == GameplayMode::Replugged)
		{
			return;
		}

		if (resolved_note_hit || lane.next_note_index >= gameplay_lanes_[lane_index].midi_chart.notes().size())
			set_lane_stem_target_gain(lane_index, 1.0f);
	}

	bool SongSession::switch_active_lane(int delta)
	{
		if ((play_state_.gameplay_mode != GameplayMode::Freeplay &&
			 play_state_.gameplay_mode != GameplayMode::Replugged) ||
			gameplay_lanes_.size() < 2 ||
			delta == 0)
			return false;

		const int lane_count = static_cast<int>(gameplay_lanes_.size());
		const int previous_index = play_state_.active_lane_index;
		int next_index = (play_state_.active_lane_index + delta) % lane_count;
		if (next_index < 0)
			next_index += lane_count;
		if (next_index == previous_index)
			return false;

		GameplayLaneRuntimeState &previous_lane = play_state_.lanes[static_cast<size_t>(previous_index)];
		previous_lane.lane_held.fill(false);
		previous_lane.lane_sustain_end_times_.fill(0.0);
		previous_lane.lane_sustain_release_times_.fill(-1.0);
		if (play_state_.gameplay_mode == GameplayMode::Freeplay)
			set_lane_stem_target_gain(static_cast<size_t>(previous_index), 1.0f);

		play_state_.active_lane_index = next_index;
		GameplayLaneRuntimeState &next_lane = play_state_.lanes[static_cast<size_t>(play_state_.active_lane_index)];
		next_lane.lane_held.fill(false);
		if (play_state_.gameplay_mode == GameplayMode::Freeplay)
			set_lane_stem_target_gain(static_cast<size_t>(play_state_.active_lane_index), 1.0f);
		return true;
	}

	bool SongSession::has_stem(std::string_view stem_name) const
	{
		return song_player_.has_stem(stem_name);
	}

	size_t SongSession::loaded_stem_count() const
	{
		return song_player_.loaded_stem_count();
	}

	void SongSession::set_stem_target_gain(std::string_view stem_name, float gain)
	{
		if (is_loaded())
			song_player_.set_stem_target_gain(stem_name, gain);
	}

	float SongSession::stem_target_gain(std::string_view stem_name) const
	{
		return song_player_.stem_target_gain(stem_name);
	}

	int SongSession::sample_rate() const
	{
		return transport_.sample_rate();
	}

	size_t SongSession::emitted_frames() const
	{
		return transport_.emitted_frames();
	}

	bool SongSession::playback_finished() const
	{
		return song_player_.playback_finished();
	}

	void SongSession::set_timing_offset_seconds(double offset_seconds)
	{
		play_state_.timing_offset_seconds = std::clamp(offset_seconds, -0.250, 0.250);
	}

	double SongSession::timing_offset_seconds() const
	{
		return play_state_.timing_offset_seconds;
	}

	size_t SongSession::play_state_serialized_size() const
	{
		if (!is_loaded())
			return 0;

		const SongPlayer::PlaybackState audio_state = song_player_.playback_state();
		return sizeof(SerializedPlayStateHeader) +
			play_state_.lanes.size() * sizeof(SerializedLaneRuntimeState) +
			audio_state.current_gains.size() * sizeof(float) +
			audio_state.target_gains.size() * sizeof(float);
	}

	bool SongSession::serialize_play_state(std::vector<std::uint8_t> &bytes, std::string &error_message) const
	{
		bytes.clear();
		if (!is_loaded())
		{
			error_message = "Song session is not loaded.";
			return false;
		}

		const SongPlayer::PlaybackState audio_state = song_player_.playback_state();
		if (audio_state.current_gains.size() != audio_state.target_gains.size())
		{
			error_message = "Playback state gain vectors are inconsistent.";
			return false;
		}

		const Transport::State transport_state = transport_.state();
		SerializedPlayStateHeader header;
		header.payload_size = static_cast<std::uint32_t>(play_state_serialized_size());
		header.session_fingerprint = session_fingerprint();
		header.gameplay_mode = static_cast<std::uint32_t>(play_state_.gameplay_mode);
		header.lane_count = static_cast<std::uint32_t>(play_state_.lanes.size());
		header.active_lane_index = play_state_.active_lane_index;
		header.timing_offset_seconds = play_state_.timing_offset_seconds;
		header.sample_rate = transport_state.sample_rate;
		header.frame_remainder = static_cast<std::uint64_t>(transport_state.frame_remainder);
		header.emitted_frames = static_cast<std::uint64_t>(transport_state.emitted_frames);
		header.audio_frame_index = static_cast<std::uint64_t>(audio_state.frame_index);
		header.audio_stem_count = static_cast<std::uint32_t>(audio_state.current_gains.size());

		bytes.reserve(play_state_serialized_size());
		append_bytes(bytes, header);
		for (const GameplayLaneRuntimeState &lane : play_state_.lanes)
		{
			SerializedLaneRuntimeState serialized_lane;
			serialized_lane.lane_held_mask = lane_mask_from_state(lane.lane_held);
			serialized_lane.lock_state = static_cast<std::uint8_t>(lane.lock_state);
			serialized_lane.is_actionable = lane.is_actionable ? 1u : 0u;
			serialized_lane.should_prompt = lane.should_prompt ? 1u : 0u;
			for (size_t fret = 0; fret < lane.lane_sustain_end_times_.size(); ++fret)
			{
				serialized_lane.lane_sustain_end_times[fret] = lane.lane_sustain_end_times_[fret];
				serialized_lane.lane_sustain_release_times[fret] = lane.lane_sustain_release_times_[fret];
			}
			serialized_lane.input_generation = lane.input_generation;
			serialized_lane.consumed_input_generation = lane.consumed_input_generation;
			serialized_lane.next_note_index = static_cast<std::uint64_t>(lane.next_note_index);
			serialized_lane.stem_target_gain = lane.stem_target_gain;
			serialized_lane.lock_start_measure_index = lane.lock_start_measure_index;
			serialized_lane.lock_end_measure_index = lane.lock_end_measure_index;
			serialized_lane.lock_start_time_seconds = lane.lock_start_time_seconds;
			serialized_lane.lock_end_time_seconds = lane.lock_end_time_seconds;
			serialized_lane.ready_lock_start_measure_index = lane.ready_lock_start_measure_index;
			serialized_lane.ready_lock_end_measure_index = lane.ready_lock_end_measure_index;
			serialized_lane.ready_lock_start_time_seconds = lane.ready_lock_start_time_seconds;
			serialized_lane.ready_lock_end_time_seconds = lane.ready_lock_end_time_seconds;
			serialized_lane.ready_lock_note_index = static_cast<std::uint64_t>(lane.ready_lock_note_index);
			serialized_lane.lock_progress = lane.lock_progress;
			serialized_lane.last_missed_note_time_seconds = lane.last_missed_note_time_seconds;
			serialized_lane.successful_replugged_measures = lane.successful_replugged_measures;
			serialized_lane.count_eligible_measure_index = lane.count_eligible_measure_index;
			serialized_lane.count_eligible_measure_start_seconds = lane.count_eligible_measure_start_seconds;
			serialized_lane.lock_ready = lane.lock_ready ? 1u : 0u;
			serialized_lane.event_flags = lane.event_flags;
			append_bytes(bytes, serialized_lane);
		}

		for (const float gain : audio_state.current_gains)
			append_bytes(bytes, gain);
		for (const float gain : audio_state.target_gains)
			append_bytes(bytes, gain);

		error_message.clear();
		return true;
	}

	bool SongSession::deserialize_play_state(const std::uint8_t *data, size_t size, std::string &error_message)
	{
		if (!is_loaded())
		{
			error_message = "Song session is not loaded.";
			return false;
		}
		if (data == nullptr || size == 0)
		{
			error_message = "Serialized play-state data is empty.";
			return false;
		}

		size_t offset = 0;
		SerializedPlayStateHeader header;
		if (!read_bytes(data, size, offset, header))
		{
			error_message = "Serialized play-state header is truncated.";
			return false;
		}
		if (header.magic != kSerializedPlayStateMagic)
		{
			error_message = "Serialized play-state magic does not match.";
			return false;
		}
		if (header.version != kSerializedPlayStateVersion)
		{
			error_message = "Serialized play-state version is not supported.";
			return false;
		}
		if (header.payload_size < sizeof(SerializedPlayStateHeader) || header.payload_size > size)
		{
			error_message = "Serialized play-state payload size is invalid.";
			return false;
		}
		if (header.session_fingerprint != session_fingerprint())
		{
			error_message = "Save state belongs to a different song or chart configuration.";
			return false;
		}
		if (header.gameplay_mode > static_cast<std::uint32_t>(GameplayMode::Replugged))
		{
			error_message = "Serialized play-state gameplay mode is invalid.";
			return false;
		}
		if (header.lane_count != play_state_.lanes.size())
		{
			error_message = "Serialized play-state lane count does not match the loaded song session.";
			return false;
		}
		if (header.active_lane_index < 0 ||
			header.active_lane_index >= static_cast<std::int32_t>(play_state_.lanes.size()))
		{
			error_message = "Serialized play-state active lane index is out of range.";
			return false;
		}
		if (header.sample_rate != transport_.sample_rate())
		{
			error_message = "Serialized play-state sample rate does not match the loaded song session.";
			return false;
		}

		const SongPlayer::PlaybackState current_audio_state = song_player_.playback_state();
		if (header.audio_stem_count != current_audio_state.current_gains.size())
		{
			error_message = "Serialized play-state audio stem count does not match the loaded song session.";
			return false;
		}

		PlayState restored_play_state = play_state_;
		restored_play_state.gameplay_mode = static_cast<GameplayMode>(header.gameplay_mode);
		restored_play_state.active_lane_index = header.active_lane_index;
		restored_play_state.timing_offset_seconds = std::clamp(header.timing_offset_seconds, -0.250, 0.250);
		restored_play_state.lanes.resize(play_state_.lanes.size());

		for (size_t lane_index = 0; lane_index < restored_play_state.lanes.size(); ++lane_index)
		{
			SerializedLaneRuntimeState serialized_lane;
			if (!read_bytes(data, size, offset, serialized_lane))
			{
				error_message = "Serialized play-state lane data is truncated.";
				return false;
			}

			GameplayLaneRuntimeState &lane = restored_play_state.lanes[lane_index];
			for (size_t fret = 0; fret < lane.lane_held.size(); ++fret)
			{
				lane.lane_held[fret] = (serialized_lane.lane_held_mask & static_cast<std::uint8_t>(1u << fret)) != 0;
				lane.lane_sustain_end_times_[fret] = serialized_lane.lane_sustain_end_times[fret];
				lane.lane_sustain_release_times_[fret] = serialized_lane.lane_sustain_release_times[fret];
			}
			lane.input_generation = serialized_lane.input_generation;
			lane.consumed_input_generation = serialized_lane.consumed_input_generation;
			lane.next_note_index = static_cast<size_t>(serialized_lane.next_note_index);
			lane.stem_target_gain = std::clamp(serialized_lane.stem_target_gain, 0.0f, 1.0f);
			lane.lock_state = serialized_lane.lock_state == static_cast<std::uint8_t>(LaneLockState::Locked)
				? LaneLockState::Locked
				: LaneLockState::Unlocked;
			lane.lock_start_measure_index = serialized_lane.lock_start_measure_index;
			lane.lock_end_measure_index = serialized_lane.lock_end_measure_index;
			lane.lock_start_time_seconds = serialized_lane.lock_start_time_seconds;
			lane.lock_end_time_seconds = serialized_lane.lock_end_time_seconds;
			lane.ready_lock_start_measure_index = serialized_lane.ready_lock_start_measure_index;
			lane.ready_lock_end_measure_index = serialized_lane.ready_lock_end_measure_index;
			lane.ready_lock_start_time_seconds = serialized_lane.ready_lock_start_time_seconds;
			lane.ready_lock_end_time_seconds = serialized_lane.ready_lock_end_time_seconds;
			lane.ready_lock_note_index = static_cast<size_t>(serialized_lane.ready_lock_note_index);
			lane.lock_progress = std::clamp(serialized_lane.lock_progress, 0.0f, 1.0f);
			lane.last_missed_note_time_seconds = serialized_lane.last_missed_note_time_seconds;
			lane.successful_replugged_measures = (std::max)(0, serialized_lane.successful_replugged_measures);
			lane.count_eligible_measure_index = (std::max)(0, serialized_lane.count_eligible_measure_index);
			lane.count_eligible_measure_start_seconds = serialized_lane.count_eligible_measure_start_seconds;
			lane.lock_ready = serialized_lane.lock_ready != 0;
			lane.is_actionable = serialized_lane.is_actionable != 0;
			lane.should_prompt = serialized_lane.should_prompt != 0;
			lane.event_flags = serialized_lane.event_flags;

			const size_t note_count = gameplay_lanes_[lane_index].midi_chart.notes().size();
			if (lane.next_note_index > note_count)
			{
				error_message = "Serialized play-state note progress is out of range.";
				return false;
			}
		}

		SongPlayer::PlaybackState restored_audio_state;
		restored_audio_state.frame_index = static_cast<size_t>(header.audio_frame_index);
		restored_audio_state.current_gains.resize(header.audio_stem_count);
		restored_audio_state.target_gains.resize(header.audio_stem_count);
		for (float &gain : restored_audio_state.current_gains)
		{
			if (!read_bytes(data, size, offset, gain))
			{
				error_message = "Serialized play-state current audio gain data is truncated.";
				return false;
			}
		}
		for (float &gain : restored_audio_state.target_gains)
		{
			if (!read_bytes(data, size, offset, gain))
			{
				error_message = "Serialized play-state target audio gain data is truncated.";
				return false;
			}
		}
		if (offset != header.payload_size)
		{
			error_message = "Serialized play-state payload is truncated or malformed.";
			return false;
		}

		play_state_ = std::move(restored_play_state);
		Transport::State restored_transport_state;
		restored_transport_state.sample_rate = header.sample_rate;
		restored_transport_state.frame_remainder = static_cast<size_t>(header.frame_remainder);
		restored_transport_state.emitted_frames = static_cast<size_t>(header.emitted_frames);
		transport_.restore_state(restored_transport_state);
		if (!song_player_.restore_playback_state(restored_audio_state, error_message))
			return false;

		for (size_t lane_index = 0; lane_index < play_state_.lanes.size(); ++lane_index)
		{
			sync_replugged_lane_schedule_times(lane_index);
			set_lane_stem_target_gain(lane_index, play_state_.lanes[lane_index].stem_target_gain);
		}

		refresh_frame_snapshot({});
		error_message.clear();
		return true;
	}

	SongPlayerView SongSession::view(const std::string &status_message) const
	{
		SongPlayerView player_view;
		rebuild_cached_player_view(player_view, status_message);
		return player_view;
	}

	void SongSession::refresh_frame_snapshot(const std::string &status_message)
	{
		refresh_lane_frame_cache(song_time_seconds());
		rebuild_cached_player_view(frame_snapshot_.player, status_message);
		rebuild_cached_scene(frame_snapshot_.scene);
	}

	const GameplayFrameSnapshot &SongSession::frame_snapshot() const
	{
		return frame_snapshot_;
	}

	void SongSession::render_interleaved_s16(std::int16_t *output, size_t frame_count)
	{
		audio_mixer_.render_interleaved_s16(output, frame_count);
		transport_.on_audio_rendered(frame_count);
	}

	::rhythmreplugged::frontend_contract::AudioBatch SongSession::render_fixed_tick_audio(int ticks_per_second)
	{
		const size_t frame_count = transport_.frames_for_next_tick(ticks_per_second);
		::rhythmreplugged::frontend_contract::AudioBatch batch = audio_mixer_.render(frame_count);
		transport_.on_audio_rendered(batch.frame_count());
		return batch;
	}

	double SongSession::song_time_seconds() const
	{
		return adjusted_song_time_seconds();
	}

	double SongSession::song_time_beats(double beats_per_minute) const
	{
		return transport_.song_time_beats(beats_per_minute);
	}

	std::uint8_t SongSession::lane_mask_from_state(const std::array<bool, 5> &lanes)
	{
		std::uint8_t mask = 0;
		for (size_t lane = 0; lane < lanes.size(); ++lane)
		{
			if (lanes[lane])
				mask |= static_cast<std::uint8_t>(1u << lane);
		}

		return mask;
	}

	bool SongSession::held_mask_satisfies_expected(std::uint8_t held_mask, std::uint8_t expected_mask)
	{
		return expected_mask != 0 && (held_mask & expected_mask) == expected_mask;
	}

	bool SongSession::configure_gameplay_lanes(
		const MidiChart &song_chart,
		const GameplayOptions &options,
		std::string &error_message)
	{
		play_state_ = {};
		play_state_.gameplay_mode = options.gameplay_mode();
		gameplay_lanes_.clear();
		chart_status_message_.clear();

		auto build_lane = [&](InstrumentOption instrument)
		{
			GameplayLaneDefinition lane;
			lane.instrument = instrument;
			lane.instrument_type = to_highway_instrument_type(instrument);
			lane.instrument_label = instrument_label_for(instrument);
			for (const std::string &stem_name : stem_names_for(instrument))
			{
				if (song_player_.has_stem(stem_name))
					lane.stem_names.push_back(stem_name);
			}
			if (lane.stem_names.empty() &&
				(instrument == InstrumentOption::Guitar || instrument == InstrumentOption::CoopGuitar) &&
				song_player_.has_stem("guitar"))
			{
				lane.stem_names.push_back("guitar");
			}
			if (lane.stem_names.empty() && instrument == InstrumentOption::Bass &&
				song_player_.has_stem("rhythm"))
			{
				lane.stem_names.push_back("rhythm");
			}
			if (lane.stem_names.empty() && instrument == InstrumentOption::Rhythm &&
				song_player_.has_stem("bass"))
			{
				lane.stem_names.push_back("bass");
			}
			return lane;
		};

		if (play_state_.gameplay_mode == GameplayMode::Classic)
		{
			GameplayLaneDefinition lane = build_lane(options.instrument());
			std::string chart_error_message;
			lane.midi_chart = song_chart;
			if (!lane.midi_chart.select_preview(
				to_midi_chart_difficulty(options.difficulty()),
				to_midi_chart_track_type(options.instrument()),
				chart_error_message))
			{
				error_message = chart_error_message.empty() ? "Could not load a playable chart for the selected instrument and difficulty." : chart_error_message;
				return false;
			}

			if (!lane.midi_chart.is_loaded())
			{
				error_message = "Could not load a playable chart for the selected instrument and difficulty.";
				return false;
			}

			cache_measure_boundaries(lane);
			cache_lyric_data(lane);
			chart_status_message_ = std::move(chart_error_message);
			gameplay_lanes_.push_back(std::move(lane));
		}
		else
		{
			std::vector<InstrumentOption> selected_instruments;
			for (const InstrumentOption instrument : options.claimed_instruments())
			{
				if (std::find(options.reserved_instruments().begin(), options.reserved_instruments().end(), instrument) != options.reserved_instruments().end())
					continue;
				if (!track_has_exact_difficulty(song_chart, instrument, options.difficulty()))
					continue;
				selected_instruments.push_back(instrument);
			}

			std::sort(
				selected_instruments.begin(),
				selected_instruments.end(),
				[](InstrumentOption left, InstrumentOption right)
				{
					return instrument_sort_rank(left) < instrument_sort_rank(right);
				});
			selected_instruments.erase(std::unique(selected_instruments.begin(), selected_instruments.end()), selected_instruments.end());

			for (const InstrumentOption instrument : selected_instruments)
			{
				GameplayLaneDefinition lane = build_lane(instrument);
				std::string lane_error_message;
				lane.midi_chart = song_chart;
				lane.midi_chart.select_preview(
					to_midi_chart_difficulty(options.difficulty()),
					to_midi_chart_track_type(instrument),
					lane_error_message);
				if (!lane.midi_chart.is_loaded() ||
					lane.midi_chart.track_name() != instrument_label_for(instrument) ||
					lane.midi_chart.difficulty_name() != difficulty_label_for(options.difficulty()))
				{
					continue;
				}

				cache_measure_boundaries(lane);
				cache_lyric_data(lane);
				gameplay_lanes_.push_back(std::move(lane));
			}

			if (gameplay_lanes_.size() < 2)
			{
				error_message = play_state_.gameplay_mode == GameplayMode::Freeplay
					? "Freeplay mode requires at least two claimed playable instruments at the selected difficulty."
					: "Replugged mode requires at least two claimed playable instruments at the selected difficulty.";
				return false;
			}
		}

		play_state_.lanes.resize(gameplay_lanes_.size());
		lane_frame_cache_.clear();
		lane_frame_cache_.resize(gameplay_lanes_.size());
		for (GameplayLaneRuntimeState &lane_runtime : play_state_.lanes)
			reset_runtime_state(lane_runtime);

		for (size_t lane_index = 0; lane_index < gameplay_lanes_.size(); ++lane_index)
		{
			set_lane_stem_target_gain(lane_index, 1.0f);
		}

		play_state_.active_lane_index = initial_active_lane_index();
		if (play_state_.gameplay_mode == GameplayMode::Replugged)
		{
			std::vector<size_t> staggered_lane_indexes;
			for (size_t lane_index = 0; lane_index < gameplay_lanes_.size(); ++lane_index)
			{
				if (static_cast<int>(lane_index) == play_state_.active_lane_index)
					continue;
				staggered_lane_indexes.push_back(lane_index);
			}
			std::sort(staggered_lane_indexes.begin(), staggered_lane_indexes.end(),
				[&](size_t left_index, size_t right_index)
				{
					const auto &left_notes = gameplay_lanes_[left_index].midi_chart.notes();
					const auto &right_notes = gameplay_lanes_[right_index].midi_chart.notes();
					const double left_time = left_notes.empty() ? 1.0e12 : left_notes.front().start_seconds;
					const double right_time = right_notes.empty() ? 1.0e12 : right_notes.front().start_seconds;
					if (std::fabs(left_time - right_time) > 0.001)
						return left_time < right_time;
					if (gameplay_lanes_[left_index].instrument == InstrumentOption::Guitar)
						return true;
					if (gameplay_lanes_[right_index].instrument == InstrumentOption::Guitar)
						return false;
					return left_index > right_index;
				});

			for (size_t lane_index = 0; lane_index < gameplay_lanes_.size(); ++lane_index)
			{
				GameplayLaneRuntimeState &lane_runtime = play_state_.lanes[lane_index];
				if (static_cast<int>(lane_index) == play_state_.active_lane_index)
				{
					lane_runtime.lock_state = LaneLockState::Unlocked;
					lane_runtime.count_eligible_measure_index = 0;
					lane_runtime.count_eligible_measure_start_seconds = 0.0;
					lane_runtime.is_actionable = lane_has_actionable_note(lane_index, 0.0);
					lane_runtime.should_prompt = false;
					set_lane_stem_target_gain(lane_index, 1.0f);
					continue;
				}

				const auto initial_lock_range = next_replugged_lock_range(lane_index, lane_runtime.next_note_index, 0.0);
				if (initial_lock_range.has_value())
				{
					size_t stagger_index = 0;
					for (; stagger_index < staggered_lane_indexes.size(); ++stagger_index)
					{
						if (staggered_lane_indexes[stagger_index] == lane_index)
							break;
					}
					double staggered_unlock_time_seconds = initial_lock_range->second;
					for (size_t offset = 0; offset < stagger_index; ++offset)
						staggered_unlock_time_seconds = next_measure_boundary_at_or_after(lane_index, staggered_unlock_time_seconds + 0.001);
					lock_replugged_lane(lane_index, 0.0, staggered_unlock_time_seconds);
				}
				else
					set_lane_stem_target_gain(lane_index, 1.0f);
			}
		}

		return true;
	}

	void SongSession::reset_runtime_state(GameplayLaneRuntimeState &lane)
	{
		lane.lane_held.fill(false);
		lane.lane_sustain_end_times_.fill(0.0);
		lane.lane_sustain_release_times_.fill(-1.0);
		lane.input_generation = 0;
		lane.consumed_input_generation = 0;
		lane.next_note_index = 0;
		lane.stem_target_gain = 1.0f;
		lane.lock_state = LaneLockState::Unlocked;
		lane.lock_start_measure_index = -1;
		lane.lock_end_measure_index = -1;
		lane.lock_start_time_seconds = 0.0;
		lane.lock_end_time_seconds = 0.0;
		lane.ready_lock_start_measure_index = -1;
		lane.ready_lock_end_measure_index = -1;
		lane.ready_lock_start_time_seconds = 0.0;
		lane.ready_lock_end_time_seconds = 0.0;
		lane.ready_lock_note_index = 0;
		lane.lock_progress = 0.0f;
		lane.last_missed_note_time_seconds = -1.0;
		lane.successful_replugged_measures = 0;
		lane.count_eligible_measure_index = 0;
		lane.count_eligible_measure_start_seconds = 0.0;
		lane.lock_ready = false;
		lane.is_actionable = false;
		lane.should_prompt = false;
		lane.event_flags = 0;
	}

	void SongSession::cache_lyric_data(GameplayLaneDefinition &lane)
	{
		lane.lyric_tokens.clear();
		lane.lyric_phrase_ranges.clear();

		const std::vector<MidiChartTextEvent> lyrics = collect_preferred_vocal_lyrics(lane.midi_chart);
		const auto lyric_phrase_ranges = collect_lyric_phrase_ranges(lane.midi_chart);
		lane.lyric_phrase_ranges.reserve(lyric_phrase_ranges.size());
		for (const auto &phrase_range : lyric_phrase_ranges)
			lane.lyric_phrase_ranges.push_back({phrase_range.start_seconds, phrase_range.end_seconds});

		size_t phrase_index = 0;
		int current_line_index = 0;
		bool previous_join_without_space = false;
		for (size_t index = 0; index < lyrics.size(); ++index)
		{
			const MidiChartTextEvent &lyric = lyrics[index];
			if (!is_displayable_lyric(lyric))
				continue;

			const ParsedLyricDisplay parsed_lyric = parse_lyric_display(lyric.text);
			if (parsed_lyric.text.empty())
				continue;

			while (phrase_index < lane.lyric_phrase_ranges.size() &&
				lane.lyric_phrase_ranges[phrase_index].end_seconds <= lyric.time_seconds)
			{
				++phrase_index;
			}

			const bool lyric_in_phrase =
				phrase_index < lane.lyric_phrase_ranges.size() &&
				lyric.time_seconds >= lane.lyric_phrase_ranges[phrase_index].start_seconds &&
				lyric.time_seconds < lane.lyric_phrase_ranges[phrase_index].end_seconds;

			double next_time_seconds = lyric.time_seconds + 0.75;
			for (size_t next_index = index + 1; next_index < lyrics.size(); ++next_index)
			{
				if (!is_displayable_lyric(lyrics[next_index]))
					continue;

				next_time_seconds = lyrics[next_index].time_seconds;
				break;
			}

			if (lyric_in_phrase)
				current_line_index = static_cast<int>(phrase_index);
			else if (!lane.lyric_tokens.empty() && parsed_lyric.force_new_line_before)
				++current_line_index;

			CachedLyricToken lyric_token;
			lyric_token.text = parsed_lyric.text;
			lyric_token.start_seconds = lyric.time_seconds;
			lyric_token.end_seconds = next_time_seconds;
			lyric_token.prepend_space =
				!lane.lyric_tokens.empty() &&
				lane.lyric_tokens.back().line_index == current_line_index &&
				!previous_join_without_space &&
				!starts_with_punctuation(parsed_lyric.text);
			lyric_token.append_hyphen = parsed_lyric.append_hyphen;
			lyric_token.line_index = current_line_index;
			lane.lyric_tokens.push_back(std::move(lyric_token));
			previous_join_without_space = parsed_lyric.join_next_without_space;
		}
	}

	void SongSession::set_lane_stem_target_gain(size_t lane_index, float gain)
	{
		if (lane_index >= gameplay_lanes_.size() || lane_index >= play_state_.lanes.size())
			return;

		const float clamped_gain = std::clamp(gain, 0.0f, 1.0f);
		play_state_.lanes[lane_index].stem_target_gain = clamped_gain;
		for (const std::string &stem_name : gameplay_lanes_[lane_index].stem_names)
			song_player_.set_stem_target_gain(stem_name, clamped_gain);
	}

	float SongSession::lane_stem_target_gain(size_t lane_index) const
	{
		if (lane_index >= play_state_.lanes.size())
			return 0.0f;
		return play_state_.lanes[lane_index].stem_target_gain;
	}

	bool SongSession::has_lane_stem(size_t lane_index) const
	{
		return lane_index < gameplay_lanes_.size() && !gameplay_lanes_[lane_index].stem_names.empty();
	}

	size_t SongSession::note_group_end_index(size_t lane_index, size_t start_index) const
	{
		const std::vector<MidiChartNote> &notes = gameplay_lanes_[lane_index].midi_chart.notes();
		if (start_index >= notes.size())
			return notes.size();

		size_t end_index = start_index + 1;
		const int group_tick = notes[start_index].tick;
		while (end_index < notes.size() && notes[end_index].tick == group_tick)
			++end_index;

		return end_index;
	}

	std::uint8_t SongSession::note_group_lane_mask(size_t lane_index, size_t start_index, size_t end_index) const
	{
		std::uint8_t mask = 0;
		const std::vector<MidiChartNote> &notes = gameplay_lanes_[lane_index].midi_chart.notes();
		for (size_t index = start_index; index < end_index && index < notes.size(); ++index)
		{
			const int lane = notes[index].lane;
			if (lane >= 0 && lane < 5)
				mask |= static_cast<std::uint8_t>(1u << lane);
		}

		return mask;
	}

	std::uint8_t SongSession::imminent_note_lane_mask(size_t lane_index, double song_time_seconds) const
	{
		const GameplayLaneRuntimeState &lane_runtime = play_state_.lanes[lane_index];
		const std::vector<MidiChartNote> &notes = gameplay_lanes_[lane_index].midi_chart.notes();
		if (lane_runtime.next_note_index >= notes.size())
			return 0;

		const double note_time_seconds = notes[lane_runtime.next_note_index].start_seconds;
		if (std::fabs(note_time_seconds - song_time_seconds) > kNoteHitWindowSeconds)
			return 0;

		const size_t group_end_index = note_group_end_index(lane_index, lane_runtime.next_note_index);
		return note_group_lane_mask(lane_index, lane_runtime.next_note_index, group_end_index);
	}

	void SongSession::refresh_active_sustains(size_t lane_index, double song_time_seconds, std::uint8_t held_mask)
	{
		GameplayLaneRuntimeState &lane = play_state_.lanes[lane_index];
		for (size_t fret = 0; fret < lane.lane_sustain_end_times_.size(); ++fret)
		{
			const double sustain_end_time = lane.lane_sustain_end_times_[fret];
			if (sustain_end_time - song_time_seconds <= -kSustainDropLeniencySeconds)
			{
				lane.lane_sustain_end_times_[fret] = 0.0;
				lane.lane_sustain_release_times_[fret] = -1.0;
				continue;
			}

			if (sustain_end_time <= 0.0)
			{
				lane.lane_sustain_release_times_[fret] = -1.0;
				continue;
			}

			const std::uint8_t lane_bit = static_cast<std::uint8_t>(1u << fret);
			if ((held_mask & lane_bit) != 0)
			{
				lane.lane_sustain_release_times_[fret] = -1.0;
				continue;
			}

			double &release_time = lane.lane_sustain_release_times_[fret];
			if (release_time < 0.0)
			{
				release_time = song_time_seconds;
				continue;
			}

			if (song_time_seconds - release_time > kSustainDropLeniencySeconds)
			{
				lane.lane_sustain_end_times_[fret] = 0.0;
				release_time = -1.0;
			}
		}
	}

	std::uint8_t SongSession::active_sustain_lane_mask(size_t lane_index, double song_time_seconds) const
	{
		const GameplayLaneRuntimeState &lane = play_state_.lanes[lane_index];
		std::uint8_t mask = 0;
		for (size_t fret = 0; fret < lane.lane_sustain_end_times_.size(); ++fret)
		{
			const double sustain_end_time = lane.lane_sustain_end_times_[fret];
			if (sustain_end_time - song_time_seconds <= -kSustainDropLeniencySeconds)
				continue;

			const double release_time = lane.lane_sustain_release_times_[fret];
			if (release_time >= 0.0 && song_time_seconds - release_time > kSustainDropLeniencySeconds)
				continue;

			if (sustain_end_time > 0.0)
				mask |= static_cast<std::uint8_t>(1u << fret);
		}

		return mask;
	}

	void SongSession::start_sustains_for_note_group(size_t lane_index, size_t start_index, size_t end_index)
	{
		GameplayLaneRuntimeState &lane = play_state_.lanes[lane_index];
		const std::vector<MidiChartNote> &notes = gameplay_lanes_[lane_index].midi_chart.notes();
		for (size_t index = start_index; index < end_index && index < notes.size(); ++index)
		{
			const MidiChartNote &note = notes[index];
			if (note.lane < 0 || note.lane >= 5)
				continue;

			const double sustain_duration_seconds = note.end_seconds - note.start_seconds;
			if (sustain_duration_seconds < kSustainMinimumSeconds)
				continue;

			const size_t fret = static_cast<size_t>(note.lane);
			lane.lane_sustain_end_times_[fret] = (std::max)(lane.lane_sustain_end_times_[fret], note.end_seconds);
			lane.lane_sustain_release_times_[fret] = -1.0;
		}
	}

	void SongSession::consume_missed_note_groups(size_t lane_index, double song_time_seconds)
	{
		GameplayLaneRuntimeState &lane = play_state_.lanes[lane_index];
		const std::vector<MidiChartNote> &notes = gameplay_lanes_[lane_index].midi_chart.notes();
		bool missed_any_notes = false;
		while (lane.next_note_index < notes.size())
		{
			const double note_time_seconds = notes[lane.next_note_index].start_seconds;
			if (song_time_seconds <= note_time_seconds + kNoteHitWindowSeconds)
				break;

			lane.last_missed_note_time_seconds = note_time_seconds;
			lane.successful_replugged_measures = 0;
			lane.count_eligible_measure_index = replugged_measure_index_for_time(lane_index, note_time_seconds) + 1;
			lane.count_eligible_measure_start_seconds = next_measure_boundary_at_or_after(lane_index, note_time_seconds + 0.001);
			lane.lock_ready = false;
			lane.ready_lock_start_measure_index = -1;
			lane.ready_lock_end_measure_index = -1;
			lane.ready_lock_start_time_seconds = 0.0;
			lane.ready_lock_end_time_seconds = 0.0;
			lane.ready_lock_note_index = 0;
			lane.next_note_index = note_group_end_index(lane_index, lane.next_note_index);
			missed_any_notes = true;
		}

		if (!missed_any_notes)
			return;

		if (play_state_.gameplay_mode == GameplayMode::Replugged)
		{
			const int current_measure_index = replugged_measure_index_for_time(lane_index, song_time_seconds);
			const bool lock_is_active =
				lane.lock_start_measure_index >= 0 &&
				lane.lock_start_measure_index <= current_measure_index &&
				lane.lock_end_measure_index > current_measure_index;
			if (!lock_is_active)
				set_lane_stem_target_gain(lane_index, 0.0f);
			return;
		}

		set_lane_stem_target_gain(lane_index, 0.0f);
	}

	void SongSession::advance_inactive_lane(size_t lane_index, double song_time_seconds)
	{
		GameplayLaneRuntimeState &lane = play_state_.lanes[lane_index];
		lane.lane_held.fill(false);
		lane.lane_sustain_end_times_.fill(0.0);
		lane.lane_sustain_release_times_.fill(-1.0);
		const std::vector<MidiChartNote> &notes = gameplay_lanes_[lane_index].midi_chart.notes();
		while (lane.next_note_index < notes.size())
		{
			const double note_time_seconds = notes[lane.next_note_index].start_seconds;
			if (song_time_seconds <= note_time_seconds + kNoteHitWindowSeconds)
				break;

			lane.next_note_index = note_group_end_index(lane_index, lane.next_note_index);
		}
	}

	void SongSession::cache_measure_boundaries(GameplayLaneDefinition &lane)
	{
		lane.measure_boundaries_seconds.clear();
		for (const MidiChartMeasureLine &measure_line : lane.midi_chart.measure_lines())
		{
			if (measure_line.kind != MidiChartMeasureLine::Kind::Measure)
				continue;
			lane.measure_boundaries_seconds.push_back(measure_line.time_seconds);
		}
	}

	void SongSession::update_replugged_lane_state(size_t lane_index, double song_time_seconds, bool is_active_lane)
	{
		GameplayLaneRuntimeState &lane = play_state_.lanes[lane_index];
		lane.event_flags = 0;
		lane.lane_held.fill(false);
		lane.lane_sustain_end_times_.fill(0.0);
		lane.lane_sustain_release_times_.fill(-1.0);
		sync_replugged_lane_schedule_times(lane_index);

		const auto current_measure = replugged_section_range_for_time(lane_index, song_time_seconds);
		const int current_measure_index = replugged_measure_index_for_time(lane_index, song_time_seconds);
		if (current_measure.has_value() &&
			lane.lock_start_measure_index > current_measure_index &&
			lane.lock_start_time_seconds < current_measure->second - 0.001)
		{
			fail_invalid_replugged_boundary("scheduled lock start entered current measure");
		}
		if (current_measure.has_value() &&
			lane.lock_end_measure_index > current_measure_index &&
			lane.lock_end_time_seconds > current_measure->first + 0.001 &&
			lane.lock_end_time_seconds < current_measure->second - 0.001)
		{
			fail_invalid_replugged_boundary("scheduled lock end entered current measure");
		}
		if (is_active_lane && lane.lock_ready)
			try_commit_replugged_ready_lock(lane_index, song_time_seconds);
		if (is_active_lane)
			advance_replugged_measure_progress(lane_index, song_time_seconds);
		if (current_measure.has_value() &&
			!lane_measure_has_notes(lane_index, current_measure->first, current_measure->second))
		{
			const auto empty_measure_range = next_replugged_empty_measure_range(lane_index, song_time_seconds);
			if (empty_measure_range.has_value() &&
				empty_measure_range->first <= song_time_seconds + 0.001 &&
				lane.lock_end_measure_index < replugged_measure_index_for_time(lane_index, empty_measure_range->second))
			{
				set_replugged_lock_schedule_by_measure(
					lane_index,
					replugged_measure_index_for_time(lane_index, empty_measure_range->first),
					replugged_measure_index_for_time(lane_index, empty_measure_range->second));
				lane.lock_ready = false;
				lane.ready_lock_start_measure_index = -1;
				lane.ready_lock_end_measure_index = -1;
				lane.ready_lock_start_time_seconds = 0.0;
				lane.ready_lock_end_time_seconds = 0.0;
				lane.ready_lock_note_index = 0;
			}
		}

		const bool was_locked = lane.lock_state == LaneLockState::Locked;
		if (lane.lock_end_measure_index > current_measure_index)
		{
			const bool lock_is_active =
				lane.lock_start_measure_index >= 0 &&
				lane.lock_start_measure_index <= current_measure_index;
			lane.lock_state = lock_is_active ? LaneLockState::Locked : LaneLockState::Unlocked;
			if (lock_is_active)
			{
				const double warning_start_time_seconds = retreat_measure_boundary(
					lane_index,
					lane.lock_end_time_seconds,
					kRepluggedMeasuresRequiredToLock);
				if (song_time_seconds <= warning_start_time_seconds + 0.001)
				{
					lane.lock_progress = 1.0f;
				}
				else
				{
					const double warning_duration_seconds = (std::max)(
						lane.lock_end_time_seconds - warning_start_time_seconds,
						0.001);
					lane.lock_progress = static_cast<float>(std::clamp(
						(lane.lock_end_time_seconds - song_time_seconds) / warning_duration_seconds,
						0.0,
						1.0));
				}
			}
			else
			{
				lane.lock_progress = 1.0f;
			}
			if (lock_is_active)
			{
				advance_inactive_lane(lane_index, song_time_seconds);
				set_lane_stem_target_gain(lane_index, 1.0f);
			}
			else
			{
				consume_missed_note_groups(lane_index, song_time_seconds);
			}
		}
		else
		{
			if (was_locked)
				lane.event_flags |= 1u << 3;
			const double completed_lock_end_time_seconds = lane.lock_end_time_seconds;
			lane.lock_state = LaneLockState::Unlocked;
			lane.lock_start_measure_index = -1;
			lane.lock_end_measure_index = -1;
			lane.lock_start_time_seconds = 0.0;
			lane.lock_end_time_seconds = 0.0;
			lane.lock_progress = 0.0f;
			if (was_locked)
			{
				lane.successful_replugged_measures = 0;
				lane.count_eligible_measure_index = replugged_measure_index_for_time(lane_index, completed_lock_end_time_seconds);
				lane.count_eligible_measure_start_seconds = completed_lock_end_time_seconds;
			}
			lane.lock_ready = false;
			lane.ready_lock_start_measure_index = -1;
			lane.ready_lock_end_measure_index = -1;
			lane.ready_lock_start_time_seconds = 0.0;
			lane.ready_lock_end_time_seconds = 0.0;
			lane.ready_lock_note_index = 0;
			consume_missed_note_groups(lane_index, song_time_seconds);
		}

		const bool was_actionable = lane.is_actionable;
		lane.is_actionable =
			lane.lock_end_measure_index <= current_measure_index &&
			lane.lock_state == LaneLockState::Unlocked &&
			lane_has_actionable_note(lane_index, song_time_seconds);
		lane.should_prompt = lane.is_actionable && !is_active_lane;
		if (!was_actionable && lane.is_actionable)
			lane.event_flags |= 1u << 4;
		if (was_actionable && !lane.is_actionable)
			lane.event_flags |= 1u << 5;
	}

	void SongSession::advance_replugged_measure_progress(size_t lane_index, double song_time_seconds)
	{
		if (play_state_.gameplay_mode != GameplayMode::Replugged ||
			lane_index >= play_state_.lanes.size() ||
			lane_index >= gameplay_lanes_.size())
		{
			return;
		}

		GameplayLaneRuntimeState &lane = play_state_.lanes[lane_index];
		const int current_measure_index = replugged_measure_index_for_time(lane_index, song_time_seconds);
		if (lane.lock_end_measure_index > current_measure_index)
			return;

		for (int guard = 0; guard < 64; ++guard)
		{
			if (current_measure_index < lane.count_eligible_measure_index)
				return;

			const auto measure = replugged_section_range_for_measure_index(
				lane_index,
				lane.count_eligible_measure_index);
			if (!measure.has_value())
				return;
			if (song_time_seconds + 0.001 < measure->second)
				return;

			if (lane_measure_has_notes(lane_index, measure->first, measure->second))
				return;

			lane.count_eligible_measure_index = next_replugged_measure_index(lane.count_eligible_measure_index);
			sync_replugged_lane_schedule_times(lane_index);
		}
	}

	void SongSession::advance_replugged_measure_progress_on_hit(size_t lane_index, double note_time_seconds)
	{
		if (play_state_.gameplay_mode != GameplayMode::Replugged ||
			lane_index >= play_state_.lanes.size() ||
			lane_index >= gameplay_lanes_.size())
		{
			return;
		}

		GameplayLaneRuntimeState &lane = play_state_.lanes[lane_index];
		const int current_measure_index = replugged_measure_index_for_time(lane_index, note_time_seconds);
		if (lane.lock_end_measure_index > current_measure_index || lane.lock_ready)
			return;

		const auto current_measure = replugged_section_range_for_measure_index(lane_index, current_measure_index);
		if (!current_measure.has_value())
			return;
		if (!lane_measure_has_notes(lane_index, current_measure->first, current_measure->second))
			return;
		if (current_measure_index != lane.count_eligible_measure_index)
			return;
		if (lane.last_missed_note_time_seconds >= current_measure->first)
			return;

		const auto &notes = gameplay_lanes_[lane_index].midi_chart.notes();
		if (lane.next_note_index < notes.size() &&
			notes[lane.next_note_index].start_seconds < current_measure->second - 0.001)
		{
			return;
		}

		lane.successful_replugged_measures = (std::min)(
			lane.successful_replugged_measures + 1,
			kRepluggedMeasuresRequiredToLock);
		lane.count_eligible_measure_index = next_replugged_measure_index(current_measure_index);
		sync_replugged_lane_schedule_times(lane_index);
		if (lane.successful_replugged_measures < kRepluggedMeasuresRequiredToLock)
			return;

		lane.lock_ready = true;
		lane.ready_lock_start_measure_index = -1;
		lane.ready_lock_end_measure_index = -1;
		lane.ready_lock_start_time_seconds = 0.0;
		lane.ready_lock_end_time_seconds = 0.0;
		lane.ready_lock_note_index = 0;

		const auto lock_range = next_replugged_lock_range(
			lane_index,
			lane.next_note_index,
			earliest_replugged_lock_start_after_required_measure(
				lane_index,
				current_measure->first,
				current_measure->second));
		if (!lock_range.has_value())
			return;

		lane.ready_lock_start_time_seconds = lock_range->first;
		lane.ready_lock_end_time_seconds = lock_range->second;
		lane.ready_lock_start_measure_index = replugged_measure_index_for_time(lane_index, lock_range->first);
		lane.ready_lock_end_measure_index = replugged_measure_index_for_time(lane_index, lock_range->second);
		lane.ready_lock_note_index = first_note_index_at_or_after(lane_index, lock_range->first);
	}

	void SongSession::lock_replugged_lane(size_t lane_index, double lock_start_time_seconds, double lock_end_time_seconds)
	{
		if (lane_index >= play_state_.lanes.size())
			return;
		if (lock_end_time_seconds <= lock_start_time_seconds)
			return;

		GameplayLaneRuntimeState &lane = play_state_.lanes[lane_index];
		lane.lock_start_measure_index = replugged_measure_index_for_time(lane_index, lock_start_time_seconds);
		lane.lock_end_measure_index = replugged_measure_index_for_time(lane_index, lock_end_time_seconds);
		lane.lock_state = lock_start_time_seconds <= song_time_seconds()
			? LaneLockState::Locked
			: LaneLockState::Unlocked;
		sync_replugged_lane_schedule_times(lane_index);
		lane.ready_lock_start_measure_index = -1;
		lane.ready_lock_end_measure_index = -1;
		lane.ready_lock_start_time_seconds = 0.0;
		lane.ready_lock_end_time_seconds = 0.0;
		lane.ready_lock_note_index = 0;
		lane.lock_progress = 1.0f;
		lane.successful_replugged_measures = 0;
		lane.lock_ready = false;
		lane.is_actionable = false;
		lane.should_prompt = false;
		lane.event_flags |= 1u << 2;
		if (lane.lock_state == LaneLockState::Locked)
			set_lane_stem_target_gain(lane_index, 1.0f);
	}

	bool SongSession::try_commit_replugged_ready_lock(size_t lane_index, double song_time_seconds)
	{
		if (lane_index >= play_state_.lanes.size() || lane_index >= gameplay_lanes_.size())
			return false;

		GameplayLaneRuntimeState &lane = play_state_.lanes[lane_index];
		if (!lane.lock_ready)
			return false;
		const int current_measure_index = replugged_measure_index_for_time(lane_index, song_time_seconds);

		if (lane.ready_lock_end_measure_index <= lane.ready_lock_start_measure_index)
		{
			const auto future_measure = replugged_section_range_for_time(
				lane_index,
				song_time_seconds);
			const double earliest_future_lock_start_seconds = future_measure.has_value()
				? future_measure->second
				: song_time_seconds;
			const auto lock_range = next_replugged_lock_range(
				lane_index,
				lane.next_note_index,
				earliest_future_lock_start_seconds);
			if (!lock_range.has_value())
			{
				lane.ready_lock_start_measure_index = -1;
				lane.ready_lock_end_measure_index = -1;
				lane.ready_lock_start_time_seconds = 0.0;
				lane.ready_lock_end_time_seconds = 0.0;
				return false;
			}
			lane.ready_lock_start_measure_index = replugged_measure_index_for_time(lane_index, lock_range->first);
			lane.ready_lock_end_measure_index = replugged_measure_index_for_time(lane_index, lock_range->second);
			lane.ready_lock_start_time_seconds = lock_range->first;
			lane.ready_lock_end_time_seconds = lock_range->second;
			lane.ready_lock_note_index = first_note_index_at_or_after(lane_index, lock_range->first);
		}

		const auto current_measure = replugged_section_range_for_time(lane_index, song_time_seconds);
		if (current_measure.has_value() &&
			lane.ready_lock_start_measure_index > current_measure_index &&
			lane.ready_lock_start_time_seconds < current_measure->second - 0.001)
		{
			fail_invalid_replugged_boundary("commit target entered current measure");
		}

		if (lane.next_note_index < lane.ready_lock_note_index)
		{
			if (lane.ready_lock_start_measure_index <= current_measure_index)
			{
				lane.ready_lock_start_measure_index = -1;
				lane.ready_lock_end_measure_index = -1;
				lane.ready_lock_start_time_seconds = 0.0;
				lane.ready_lock_end_time_seconds = 0.0;
				lane.ready_lock_note_index = 0;
				return false;
			}
			return false;
		}

		lock_replugged_lane(
			lane_index,
			lane.ready_lock_start_time_seconds,
			lane.ready_lock_end_time_seconds);
		return true;
	}

	size_t SongSession::first_note_index_at_or_after(size_t lane_index, double song_time_seconds) const
	{
		if (lane_index >= gameplay_lanes_.size())
			return 0;

		const auto &notes = gameplay_lanes_[lane_index].midi_chart.notes();
		for (size_t note_index = 0; note_index < notes.size(); ++note_index)
		{
			if (notes[note_index].start_seconds >= song_time_seconds - 0.001)
				return note_index;
		}

		return notes.size();
	}

	std::optional<std::pair<double, double>> SongSession::predicted_replugged_lock_range(size_t lane_index, double song_time_seconds) const
	{
		if (play_state_.gameplay_mode != GameplayMode::Replugged ||
			lane_index >= play_state_.lanes.size() ||
			lane_index >= gameplay_lanes_.size())
		{
			return std::nullopt;
		}

		const GameplayLaneRuntimeState &lane = play_state_.lanes[lane_index];
		const int current_measure_index = replugged_measure_index_for_time(lane_index, song_time_seconds);
		if (lane.lock_end_measure_index > current_measure_index)
			return std::nullopt;

		const auto current_measure = replugged_section_range_for_time(lane_index, song_time_seconds);
		const double earliest_visual_start_seconds = current_measure.has_value()
			? current_measure->second
			: song_time_seconds;
		const auto next_empty_measure_range = next_replugged_empty_measure_range(
			lane_index,
			earliest_visual_start_seconds);

		auto choose_earliest_future_range =
			[&](std::optional<std::pair<double, double>> range)
			{
				if (next_empty_measure_range.has_value() &&
					next_empty_measure_range->first >= earliest_visual_start_seconds - 0.001 &&
					(!range.has_value() || next_empty_measure_range->first < range->first - 0.001))
				{
					return next_empty_measure_range;
				}
				return range;
			};

		if (lane.lock_ready &&
			lane.ready_lock_end_measure_index > lane.ready_lock_start_measure_index)
		{
			if (lane.ready_lock_start_measure_index > current_measure_index &&
				lane.ready_lock_start_time_seconds < earliest_visual_start_seconds - 0.001)
				fail_invalid_replugged_boundary("predicted ready span entered current measure");
			return choose_earliest_future_range(std::make_pair(
				lane.ready_lock_start_time_seconds,
				lane.ready_lock_end_time_seconds));
		}

		const auto eligible_measure = replugged_section_range_for_time(
			lane_index,
			replugged_measure_start_time(lane_index, lane.count_eligible_measure_index));
		if (!eligible_measure.has_value())
			return std::nullopt;
		if (replugged_measure_index_for_time(lane_index, eligible_measure->first) != lane.count_eligible_measure_index)
			return std::nullopt;
		if (!lane_measure_has_notes(lane_index, eligible_measure->first, eligible_measure->second))
			return std::nullopt;
		if (lane.last_missed_note_time_seconds >= eligible_measure->first)
			return std::nullopt;

		int remaining_measures = kRepluggedMeasuresRequiredToLock - lane.successful_replugged_measures;
		if (remaining_measures <= 0)
			remaining_measures = 1;

		auto final_required_measure = eligible_measure;
		int final_required_measure_index = lane.count_eligible_measure_index;
		for (int remaining_index = 1; remaining_index < remaining_measures; ++remaining_index)
		{
			bool found_next_required_measure = false;
			for (int guard = 0; guard < 64; ++guard)
			{
				final_required_measure = next_replugged_section_range(
					lane_index,
					final_required_measure->second);
				if (!final_required_measure.has_value())
					return std::nullopt;
				++final_required_measure_index;
				if (lane_measure_has_notes(lane_index, final_required_measure->first, final_required_measure->second))
				{
					found_next_required_measure = true;
					break;
				}
			}
			if (!found_next_required_measure)
				return std::nullopt;
		}

		const double projected_ready_boundary_seconds =
			earliest_replugged_lock_start_after_required_measure(
				lane_index,
				final_required_measure->first,
				final_required_measure->second);

		return choose_earliest_future_range(next_replugged_lock_range(
			lane_index,
			lane.next_note_index,
			(std::max)(projected_ready_boundary_seconds, earliest_visual_start_seconds)));
	}

	bool SongSession::lane_has_actionable_note(size_t lane_index, double song_time_seconds) const
	{
		if (lane_index >= play_state_.lanes.size() || lane_index >= gameplay_lanes_.size())
			return false;

		const GameplayLaneRuntimeState &lane = play_state_.lanes[lane_index];
		const std::vector<MidiChartNote> &notes = gameplay_lanes_[lane_index].midi_chart.notes();
		if (lane.next_note_index >= notes.size())
			return false;
		const auto current_measure = replugged_section_range_for_time(lane_index, song_time_seconds);
		if (!current_measure.has_value())
			return false;
		if (!lane_measure_has_notes(lane_index, current_measure->first, current_measure->second))
			return false;

		const double note_time_seconds = notes[lane.next_note_index].start_seconds;
		return note_time_seconds >= song_time_seconds - kNoteHitWindowSeconds &&
			note_time_seconds <= song_time_seconds + kRepluggedActionableLookaheadSeconds;
	}

	int SongSession::initial_active_lane_index() const
	{
		if (gameplay_lanes_.empty())
			return 0;

		double earliest_note_time_seconds = 0.0;
		bool found_earliest = false;
		std::vector<size_t> candidate_lane_indexes;
		for (size_t lane_index = 0; lane_index < gameplay_lanes_.size(); ++lane_index)
		{
			const auto &notes = gameplay_lanes_[lane_index].midi_chart.notes();
			if (notes.empty())
				continue;

			const double first_note_time_seconds = notes.front().start_seconds;
			if (!found_earliest || first_note_time_seconds < earliest_note_time_seconds - 0.001)
			{
				found_earliest = true;
				earliest_note_time_seconds = first_note_time_seconds;
				candidate_lane_indexes = {lane_index};
			}
			else if (std::fabs(first_note_time_seconds - earliest_note_time_seconds) <= 0.001)
			{
				candidate_lane_indexes.push_back(lane_index);
			}
		}

		if (!found_earliest)
			return 0;

		for (const size_t lane_index : candidate_lane_indexes)
		{
			if (gameplay_lanes_[lane_index].instrument == InstrumentOption::Guitar)
				return static_cast<int>(lane_index);
		}

		return static_cast<int>(candidate_lane_indexes.back());
	}

	int SongSession::replugged_measure_index_for_time(size_t lane_index, double song_time_seconds) const
	{
		if (lane_index >= gameplay_lanes_.size())
			return -1;

		const std::vector<double> &boundaries = gameplay_lanes_[lane_index].measure_boundaries_seconds;
		if (boundaries.empty())
			return -1;

		for (size_t index = 0; index < boundaries.size(); ++index)
		{
			if (boundaries[index] > song_time_seconds + 0.001)
				return static_cast<int>(index);
		}

		return static_cast<int>(boundaries.size());
	}

	double SongSession::replugged_measure_start_time(size_t lane_index, int measure_index) const
	{
		if (lane_index >= gameplay_lanes_.size() || measure_index <= 0)
			return 0.0;

		const std::vector<double> &boundaries = gameplay_lanes_[lane_index].measure_boundaries_seconds;
		if (boundaries.empty())
			return 0.0;
		if (measure_index > static_cast<int>(boundaries.size()))
			return boundaries.back();
		const size_t boundary_index = static_cast<size_t>(measure_index - 1);
		return boundaries[boundary_index];
	}

	std::optional<std::pair<double, double>> SongSession::replugged_section_range_for_measure_index(size_t lane_index, int measure_index) const
	{
		if (lane_index >= gameplay_lanes_.size() || measure_index < 0)
			return std::nullopt;

		const GameplayLaneDefinition &lane = gameplay_lanes_[lane_index];
		const std::vector<double> &boundaries = lane.measure_boundaries_seconds;
		if (boundaries.empty())
			return std::nullopt;
		if (measure_index > static_cast<int>(boundaries.size()))
			return std::nullopt;

		const double start_seconds =
			measure_index <= 0 ? 0.0 : boundaries[static_cast<size_t>(measure_index - 1)];

		if (measure_index < static_cast<int>(boundaries.size()))
			return std::make_pair(start_seconds, boundaries[static_cast<size_t>(measure_index)]);

		const auto &notes = lane.midi_chart.notes();
		const double fallback_end_seconds = !notes.empty()
			? (std::max)(start_seconds + 0.5, notes.back().end_seconds)
			: start_seconds + 0.5;
		if (fallback_end_seconds <= start_seconds + 0.001)
			return std::nullopt;
		return std::make_pair(start_seconds, fallback_end_seconds);
	}

	void SongSession::sync_replugged_lane_schedule_times(size_t lane_index)
	{
		if (lane_index >= play_state_.lanes.size())
			return;

		GameplayLaneRuntimeState &lane = play_state_.lanes[lane_index];
		lane.lock_start_time_seconds =
			lane.lock_start_measure_index >= 0
			? replugged_measure_start_time(lane_index, lane.lock_start_measure_index)
			: 0.0;
		lane.lock_end_time_seconds =
			lane.lock_end_measure_index >= 0
			? replugged_measure_start_time(lane_index, lane.lock_end_measure_index)
			: 0.0;
		lane.ready_lock_start_time_seconds =
			lane.ready_lock_start_measure_index >= 0
			? replugged_measure_start_time(lane_index, lane.ready_lock_start_measure_index)
			: 0.0;
		lane.ready_lock_end_time_seconds =
			lane.ready_lock_end_measure_index >= 0
			? replugged_measure_start_time(lane_index, lane.ready_lock_end_measure_index)
			: 0.0;
		lane.count_eligible_measure_start_seconds = replugged_measure_start_time(
			lane_index,
			lane.count_eligible_measure_index);
	}

	void SongSession::set_replugged_lock_schedule_by_measure(size_t lane_index, int lock_start_measure_index, int lock_end_measure_index)
	{
		if (lane_index >= play_state_.lanes.size())
			return;

		GameplayLaneRuntimeState &lane = play_state_.lanes[lane_index];
		lane.lock_start_measure_index = lock_start_measure_index;
		lane.lock_end_measure_index = lock_end_measure_index;
		sync_replugged_lane_schedule_times(lane_index);
	}

	int SongSession::next_replugged_measure_index(int measure_index) const
	{
		return measure_index < 0 ? -1 : measure_index + 1;
	}

	int SongSession::previous_replugged_measure_index(int measure_index) const
	{
		return measure_index <= 0 ? -1 : measure_index - 1;
	}

	double SongSession::next_measure_boundary_at_or_after(size_t lane_index, double song_time_seconds) const
	{
		if (lane_index >= gameplay_lanes_.size())
			return song_time_seconds;

		const auto &boundaries = gameplay_lanes_[lane_index].measure_boundaries_seconds;
		for (double boundary_seconds : boundaries)
		{
			if (boundary_seconds + 0.001 >= song_time_seconds)
				return boundary_seconds;
		}

		return song_time_seconds;
	}

	bool SongSession::lane_measure_has_notes(size_t lane_index, double measure_start_seconds, double measure_end_seconds) const
	{
		if (lane_index >= gameplay_lanes_.size())
			return false;

		const auto &notes = gameplay_lanes_[lane_index].midi_chart.notes();
		for (const MidiChartNote &note : notes)
		{
			if (note.start_seconds + 0.001 < measure_start_seconds)
				continue;
			if (note.start_seconds >= measure_end_seconds - 0.001)
				break;
			return true;
		}

		return false;
	}

	std::optional<double> SongSession::lane_measure_last_note_time(size_t lane_index, double measure_start_seconds, double measure_end_seconds) const
	{
		if (lane_index >= gameplay_lanes_.size())
			return std::nullopt;

		const auto &notes = gameplay_lanes_[lane_index].midi_chart.notes();
		double last_note_time_seconds = 0.0;
		bool found_note = false;
		for (const MidiChartNote &note : notes)
		{
			if (note.start_seconds + 0.001 < measure_start_seconds)
				continue;
			if (note.start_seconds >= measure_end_seconds - 0.001)
				break;
			last_note_time_seconds = note.start_seconds;
			found_note = true;
		}

		if (!found_note)
			return std::nullopt;
		return last_note_time_seconds;
	}

	std::optional<std::pair<double, double>> SongSession::replugged_lock_build_window(size_t lane_index) const
	{
		if (lane_index >= play_state_.lanes.size() || lane_index >= gameplay_lanes_.size())
			return std::nullopt;

		const GameplayLaneRuntimeState &lane = play_state_.lanes[lane_index];
		auto first_required_measure = replugged_section_range_for_measure_index(
			lane_index,
			lane.count_eligible_measure_index);
		if (!first_required_measure.has_value())
			return std::nullopt;
		if (lane.last_missed_note_time_seconds >= first_required_measure->first)
			return std::nullopt;
		if (!lane_measure_has_notes(lane_index, first_required_measure->first, first_required_measure->second))
			return std::nullopt;

		const int completed_required_measures = (std::clamp)(
			lane.successful_replugged_measures,
			0,
			kRepluggedMeasuresRequiredToLock - 1);
		int first_required_measure_index = lane.count_eligible_measure_index;
		for (int completed_index = 0; completed_index < completed_required_measures; ++completed_index)
		{
			bool found_previous_required_measure = false;
			for (int guard = 0; guard < 64; ++guard)
			{
				const auto previous_measure = previous_replugged_section_range(
					lane_index,
					first_required_measure->first);
				if (!previous_measure.has_value() ||
					previous_measure->second >= first_required_measure->first - 0.001)
				{
					return std::nullopt;
				}
				if (lane_measure_has_notes(lane_index, previous_measure->first, previous_measure->second))
				{
					first_required_measure = previous_measure;
					first_required_measure_index = previous_replugged_measure_index(first_required_measure_index);
					found_previous_required_measure = true;
					break;
				}
				first_required_measure = previous_measure;
				first_required_measure_index = previous_replugged_measure_index(first_required_measure_index);
			}
			if (!found_previous_required_measure)
				return std::nullopt;
		}

		auto final_required_measure = first_required_measure;
		int final_required_measure_index = first_required_measure_index;
		for (int remaining_index = 1; remaining_index < kRepluggedMeasuresRequiredToLock; ++remaining_index)
		{
			bool found_next_required_measure = false;
			for (int guard = 0; guard < 64; ++guard)
			{
				final_required_measure = next_replugged_section_range(
					lane_index,
					final_required_measure->second);
				if (!final_required_measure.has_value())
					return std::nullopt;
				final_required_measure_index = next_replugged_measure_index(final_required_measure_index);
				if (lane_measure_has_notes(lane_index, final_required_measure->first, final_required_measure->second))
				{
					found_next_required_measure = true;
					break;
				}
			}
			if (!found_next_required_measure)
				return std::nullopt;
		}

		const std::optional<double> final_required_note_time = lane_measure_last_note_time(
			lane_index,
			final_required_measure->first,
			final_required_measure->second);
		if (!final_required_note_time.has_value())
			return std::nullopt;

		return std::make_pair(
			first_required_measure->first,
			final_required_note_time.value());
	}

	double SongSession::earliest_replugged_lock_start_after_required_measure(
		size_t lane_index,
		double measure_start_seconds,
		double measure_end_seconds) const
	{
		double earliest_future_lock_start_seconds = measure_end_seconds;
		const std::optional<double> last_note_time_seconds = lane_measure_last_note_time(
			lane_index,
			measure_start_seconds,
			measure_end_seconds);
		if (!last_note_time_seconds.has_value())
			return earliest_future_lock_start_seconds;

		if (measure_end_seconds - last_note_time_seconds.value() <= kNoteHitWindowSeconds + 0.001)
		{
			const auto next_measure = next_replugged_section_range(
				lane_index,
				measure_end_seconds);
			if (next_measure.has_value())
				earliest_future_lock_start_seconds = next_measure->second;
		}

		return earliest_future_lock_start_seconds;
	}

	bool SongSession::any_other_lane_has_notes_in_measure(size_t excluded_lane_index, double measure_start_seconds, double measure_end_seconds) const
	{
		for (size_t lane_index = 0; lane_index < gameplay_lanes_.size(); ++lane_index)
		{
			if (lane_index == excluded_lane_index)
				continue;
			if (lane_measure_has_notes(lane_index, measure_start_seconds, measure_end_seconds))
				return true;
		}

		return false;
	}

	bool SongSession::any_other_lane_is_available_in_measure(
		size_t excluded_lane_index,
		int measure_index,
		double measure_start_seconds,
		double measure_end_seconds) const
	{
		for (size_t lane_index = 0; lane_index < gameplay_lanes_.size(); ++lane_index)
		{
			if (lane_index == excluded_lane_index || lane_index >= play_state_.lanes.size())
				continue;
			if (!lane_measure_has_notes(lane_index, measure_start_seconds, measure_end_seconds))
				continue;

			const GameplayLaneRuntimeState &lane = play_state_.lanes[lane_index];
			const bool lock_covers_measure =
				lane.lock_end_measure_index > measure_index &&
				lane.lock_start_measure_index <= measure_index;
			if (!lock_covers_measure)
				return true;
		}

		return false;
	}

double SongSession::advance_measure_boundary(size_t lane_index, double measure_end_seconds, int measure_count) const
{
	double advanced_time_seconds = measure_end_seconds;
	for (int count = 0; count < measure_count; ++count)
	{
		const auto next_measure = next_replugged_section_range(lane_index, advanced_time_seconds);
		if (!next_measure.has_value() || next_measure->first <= advanced_time_seconds + 0.001)
			break;
		advanced_time_seconds = next_measure->second;
	}

	return advanced_time_seconds;
}

double SongSession::retreat_measure_boundary(size_t lane_index, double measure_end_seconds, int measure_count) const
{
	if (lane_index >= gameplay_lanes_.size())
		return measure_end_seconds;

	const auto &measure_lines = gameplay_lanes_[lane_index].midi_chart.measure_lines();
	double rewound_time_seconds = measure_end_seconds;
	for (int count = 0; count < measure_count; ++count)
	{
		double previous_measure_end_seconds = rewound_time_seconds;
		bool found_previous_measure = false;
		for (const MidiChartMeasureLine &measure_line : measure_lines)
		{
			if (measure_line.kind != MidiChartMeasureLine::Kind::Measure)
				continue;
			if (measure_line.time_seconds >= rewound_time_seconds - 0.001)
				break;

			previous_measure_end_seconds = measure_line.time_seconds;
			found_previous_measure = true;
		}
		if (!found_previous_measure)
			break;
		rewound_time_seconds = previous_measure_end_seconds;
	}

	return rewound_time_seconds;
}

bool SongSession::other_lane_unlocks_too_close(size_t excluded_lane_index, double measure_end_seconds) const
{
	const int this_lane_unlock_measure_index = replugged_measure_index_for_time(excluded_lane_index, measure_end_seconds);
	if (this_lane_unlock_measure_index < 0)
		return false;

	for (size_t lane_index = 0; lane_index < play_state_.lanes.size(); ++lane_index)
	{
		if (lane_index == excluded_lane_index)
			continue;
		const GameplayLaneRuntimeState &lane = play_state_.lanes[lane_index];
		if (lane.lock_end_measure_index < 0)
			continue;
		if (lane.lock_end_measure_index == this_lane_unlock_measure_index)
			return true;
		if (this_lane_unlock_measure_index > lane.lock_end_measure_index &&
			this_lane_unlock_measure_index < lane.lock_end_measure_index + 2)
		{
			return true;
		}
		if (lane.lock_end_measure_index > this_lane_unlock_measure_index &&
			lane.lock_end_measure_index < this_lane_unlock_measure_index + 2)
		{
			return true;
		}
	}

	return false;
}

	float SongSession::replugged_lock_build_progress(size_t lane_index, double song_time_seconds) const
	{
		if (lane_index >= play_state_.lanes.size() || lane_index >= gameplay_lanes_.size())
			return 0.0f;

		const GameplayLaneRuntimeState &lane = play_state_.lanes[lane_index];
		if (lane.lock_end_time_seconds > song_time_seconds + 0.001)
			return 0.0f;
		const auto build_window = replugged_lock_build_window(lane_index);
		if (!build_window.has_value())
			return 0.0f;

		const double build_duration_seconds = (std::max)(build_window->second - build_window->first, 0.001);
		const double elapsed_seconds = std::clamp(song_time_seconds - build_window->first, 0.0, build_duration_seconds);
		return static_cast<float>(std::clamp(elapsed_seconds / build_duration_seconds, 0.0, 1.0));
	}

	std::optional<std::pair<double, double>> SongSession::replugged_section_range_for_time(size_t lane_index, double song_time_seconds) const
	{
		const int measure_index = replugged_measure_index_for_time(lane_index, song_time_seconds);
		return replugged_section_range_for_measure_index(lane_index, measure_index);
	}

	std::optional<std::pair<double, double>> SongSession::replugged_section_range_for_note_index(size_t lane_index, size_t note_index) const
	{
		if (lane_index >= gameplay_lanes_.size())
			return std::nullopt;

		const auto &notes = gameplay_lanes_[lane_index].midi_chart.notes();
		if (note_index >= notes.size())
			return std::nullopt;

		return replugged_section_range_for_time(lane_index, notes[note_index].start_seconds);
	}

	std::optional<std::pair<double, double>> SongSession::next_replugged_section_range(size_t lane_index, double measure_end_seconds) const
	{
		const auto current_measure = replugged_section_range_for_time(lane_index, (std::max)(0.0, measure_end_seconds - 0.001));
		if (!current_measure.has_value())
			return std::nullopt;
		const int current_measure_index = replugged_measure_index_for_time(lane_index, (std::max)(0.0, current_measure->second - 0.001));
		return replugged_section_range_for_measure_index(lane_index, next_replugged_measure_index(current_measure_index));
	}

	std::optional<std::pair<double, double>> SongSession::previous_replugged_section_range(size_t lane_index, double measure_start_seconds) const
	{
		const int current_measure_index = replugged_measure_index_for_time(lane_index, measure_start_seconds);
		return replugged_section_range_for_measure_index(lane_index, previous_replugged_measure_index(current_measure_index));
	}

	std::optional<std::pair<double, double>> SongSession::next_replugged_lock_range(size_t lane_index, size_t note_index, double earliest_measure_start_seconds) const
	{
		if (lane_index >= gameplay_lanes_.size())
			return std::nullopt;

		(void)note_index;
		auto next_measure = replugged_section_range_for_time(lane_index, earliest_measure_start_seconds);
		if (!next_measure.has_value())
			return std::nullopt;
		if (next_measure->first + 0.001 < earliest_measure_start_seconds)
			next_measure = next_replugged_section_range(lane_index, next_measure->second);
		if (!next_measure.has_value())
			return std::nullopt;

		for (int guard = 0; guard < 64; ++guard)
		{
			const int next_measure_index = replugged_measure_index_for_time(lane_index, next_measure->first);
			if (any_other_lane_is_available_in_measure(
				lane_index,
				next_measure_index,
				next_measure->first,
				next_measure->second))
			{
				break;
			}
			const auto later_measure = next_replugged_section_range(lane_index, next_measure->second);
			if (!later_measure.has_value() || later_measure->first <= next_measure->first + 0.001)
				return std::nullopt;
			next_measure = later_measure;
		}

		const auto &measure_lines = gameplay_lanes_[lane_index].midi_chart.measure_lines();
		double lock_end_time_seconds = next_measure->second;
		const int locked_measure_count = kRepluggedMeasuresRequiredToLock * static_cast<int>(gameplay_lanes_.size());
		int measures_found = 1;
		for (const MidiChartMeasureLine &measure_line : measure_lines)
		{
			if (measure_line.kind != MidiChartMeasureLine::Kind::Measure)
				continue;
			if (measure_line.time_seconds <= next_measure->second + 0.001)
				continue;

			lock_end_time_seconds = measure_line.time_seconds;
			++measures_found;
			if (measures_found >= locked_measure_count)
				break;
		}

		const double minimum_lock_end_time_seconds = next_measure->second;
		lock_end_time_seconds = (std::max)(lock_end_time_seconds, minimum_lock_end_time_seconds);
		for (int guard = 0; guard < 64 && other_lane_unlocks_too_close(lane_index, lock_end_time_seconds); ++guard)
		{
			lock_end_time_seconds = advance_measure_boundary(lane_index, lock_end_time_seconds, 1);
		}
		return std::make_pair(next_measure->first, lock_end_time_seconds);
	}

	std::optional<std::pair<double, double>> SongSession::next_replugged_empty_measure_range(size_t lane_index, double song_time_seconds) const
	{
		if (lane_index >= gameplay_lanes_.size())
			return std::nullopt;

		auto measure = replugged_section_range_for_time(lane_index, song_time_seconds);
		for (int guard = 0; guard < 64; ++guard)
		{
			if (!measure.has_value())
				return std::nullopt;
			if (!lane_measure_has_notes(lane_index, measure->first, measure->second))
				break;

			const auto next_measure = next_replugged_section_range(lane_index, measure->second);
			if (!next_measure.has_value() || next_measure->first <= measure->first + 0.001)
			{
				return std::nullopt;
			}
			measure = next_measure;
		}

		double empty_start_seconds = measure->first;
		double empty_end_seconds = measure->second;
		for (int guard = 0; guard < 64; ++guard)
		{
			const auto next_measure = next_replugged_section_range(lane_index, empty_end_seconds);
			if (!next_measure.has_value() ||
				next_measure->first <= empty_end_seconds + 0.001 ||
				lane_measure_has_notes(lane_index, next_measure->first, next_measure->second))
			{
				break;
			}

			empty_end_seconds = next_measure->second;
		}

		return std::make_pair(empty_start_seconds, empty_end_seconds);
	}

	void SongSession::apply_replugged_keep_busy_rule(double song_time_seconds)
	{
		if (play_state_.gameplay_mode != GameplayMode::Replugged || gameplay_lanes_.empty())
			return;

		bool all_tracks_locked = true;
		int earliest_unlock_measure_index = -1;
		bool found_earliest_unlock = false;
		const int current_measure_index = replugged_measure_index_for_time(active_lane_index(), song_time_seconds);
		for (size_t lane_index = 0; lane_index < play_state_.lanes.size(); ++lane_index)
		{
			const GameplayLaneRuntimeState &lane = play_state_.lanes[lane_index];
			const bool has_future_notes = lane.next_note_index < gameplay_lanes_[lane_index].midi_chart.notes().size();
			if (!has_future_notes)
				continue;
			if (lane.lock_end_measure_index <= current_measure_index)
			{
				all_tracks_locked = false;
				break;
			}

			if (!found_earliest_unlock || lane.lock_end_measure_index < earliest_unlock_measure_index)
			{
				earliest_unlock_measure_index = lane.lock_end_measure_index;
				found_earliest_unlock = true;
			}
		}

		if (!all_tracks_locked || !found_earliest_unlock)
		{
			replugged_keep_busy_engaged_ = false;
			return;
		}

		if (replugged_keep_busy_engaged_)
			return;

		const int target_unlock_measure_index = current_measure_index + 8;
		if (earliest_unlock_measure_index <= target_unlock_measure_index)
			return;

		const int measures_to_shift = earliest_unlock_measure_index - target_unlock_measure_index;
		if (measures_to_shift <= 0)
			return;

		for (GameplayLaneRuntimeState &lane : play_state_.lanes)
		{
			const size_t lane_index = static_cast<size_t>(&lane - play_state_.lanes.data());
			if (lane.lock_end_measure_index <= current_measure_index ||
				lane_index >= gameplay_lanes_.size())
			{
				continue;
			}

			int shifted_lock_start_measure_index = (std::max)(current_measure_index, lane.lock_start_measure_index - measures_to_shift);
			int shifted_lock_end_measure_index = (std::max)(
				shifted_lock_start_measure_index,
				lane.lock_end_measure_index - measures_to_shift);
			shifted_lock_end_measure_index = (std::max)(
				shifted_lock_end_measure_index,
				target_unlock_measure_index);
			if (shifted_lock_end_measure_index <= current_measure_index)
				shifted_lock_end_measure_index = next_replugged_measure_index(current_measure_index);

			set_replugged_lock_schedule_by_measure(
				lane_index,
				shifted_lock_start_measure_index,
				shifted_lock_end_measure_index);
		}

		replugged_keep_busy_engaged_ = true;
		set_transient_gameplay_status_message("Keep Busy Rule!", 1.5);
	}

	void SongSession::set_transient_gameplay_status_message(std::string message, double duration_seconds)
	{
		transient_gameplay_status_message_ = std::move(message);
		transient_gameplay_status_until_seconds_ = song_time_seconds() + (std::max)(duration_seconds, 0.0);
	}

	GameplayMode SongSession::gameplay_mode() const
	{
		return play_state_.gameplay_mode;
	}

	size_t SongSession::gameplay_lane_count() const
	{
		return gameplay_lanes_.size();
	}

	int SongSession::active_gameplay_lane_index() const
	{
		return play_state_.active_lane_index;
	}

	InstrumentLaneView SongSession::gameplay_lane_view(size_t index) const
	{
		InstrumentLaneView lane_view;
		if (index >= gameplay_lanes_.size())
			return lane_view;

		const GameplayLaneDefinition &lane = gameplay_lanes_[index];
		const GameplayLaneRuntimeState &lane_runtime = play_state_.lanes[index];
		const LaneFrameCache *frame_cache = index < lane_frame_cache_.size() ? &lane_frame_cache_[index] : nullptr;
		const int current_measure_index = play_state_.gameplay_mode == GameplayMode::Replugged
			? replugged_measure_index_for_time(index, song_time_seconds())
			: -1;
		lane_view.instrument_type = lane.instrument_type;
		lane_view.instrument_label = lane.instrument_label;
		lane_view.is_active = static_cast<int>(index) == play_state_.active_lane_index;
		lane_view.is_muted = has_lane_stem(index) && lane_stem_target_gain(index) < 0.5f;
		lane_view.has_chart = lane.midi_chart.is_loaded();
		lane_view.lock_state = lane_runtime.lock_state;
		lane_view.lock_progress = lane_runtime.lock_progress;
		lane_view.lock_build_progress = play_state_.gameplay_mode == GameplayMode::Replugged
			? replugged_lock_build_progress(index, song_time_seconds())
			: 0.0f;
		lane_view.has_scheduled_lock = play_state_.gameplay_mode == GameplayMode::Replugged &&
			lane_runtime.lock_end_measure_index > current_measure_index;
		lane_view.is_lock_ready = play_state_.gameplay_mode == GameplayMode::Replugged &&
			lane_runtime.lock_ready &&
			lane_runtime.lock_end_measure_index <= current_measure_index;
		lane_view.is_lock_committed = play_state_.gameplay_mode == GameplayMode::Replugged &&
			lane_runtime.lock_end_measure_index > current_measure_index &&
			lane_runtime.lock_start_measure_index > current_measure_index;
		lane_view.is_actionable = lane_runtime.is_actionable;
		lane_view.should_prompt = lane_runtime.should_prompt;
		lane_view.hide_note_visuals = false;
		lane_view.hide_lane_colors = false;
		const auto predicted_lock_range = play_state_.gameplay_mode == GameplayMode::Replugged
			? predicted_replugged_lock_range(index, song_time_seconds())
			: std::nullopt;
		const auto current_measure = play_state_.gameplay_mode == GameplayMode::Replugged
			? replugged_section_range_for_time(index, song_time_seconds())
			: std::nullopt;
		const double current_measure_start_seconds = current_measure.has_value()
			? current_measure->first
			: song_time_seconds();
		const double earliest_future_visual_start_seconds = current_measure.has_value()
			? current_measure->second
			: song_time_seconds();
		const auto next_empty_measure_range = play_state_.gameplay_mode == GameplayMode::Replugged
			? next_replugged_empty_measure_range(index, earliest_future_visual_start_seconds)
			: std::nullopt;
		if (play_state_.gameplay_mode == GameplayMode::Replugged &&
			lane_runtime.lock_end_measure_index > current_measure_index)
		{
			const bool empty_measure_precedes_scheduled_lock =
				next_empty_measure_range.has_value() &&
				next_empty_measure_range->first >= earliest_future_visual_start_seconds - 0.001 &&
				replugged_measure_index_for_time(index, next_empty_measure_range->first) < lane_runtime.lock_start_measure_index;
			if (empty_measure_precedes_scheduled_lock)
			{
				lane_view.locked_visual_start_offset_seconds = static_cast<float>((std::max)(0.0, next_empty_measure_range->first - song_time_seconds()));
				lane_view.locked_visual_end_offset_seconds = static_cast<float>((std::max)(0.0, next_empty_measure_range->second - song_time_seconds()));
			}
			else
			{
			if (lane_runtime.lock_start_measure_index > current_measure_index &&
				lane_runtime.lock_start_time_seconds < earliest_future_visual_start_seconds - 0.001)
				fail_invalid_replugged_boundary("scheduled lock span entered current measure");
			const double visual_lock_start_seconds =
				lane_runtime.lock_start_measure_index <= current_measure_index
				? current_measure_start_seconds
				: lane_runtime.lock_start_time_seconds;
			lane_view.locked_visual_start_offset_seconds = static_cast<float>((std::max)(0.0, visual_lock_start_seconds - song_time_seconds()));
			lane_view.locked_visual_end_offset_seconds = static_cast<float>((std::max)(0.0, lane_runtime.lock_end_time_seconds - song_time_seconds()));
			}
		}
		else if (predicted_lock_range.has_value())
		{
			if (predicted_lock_range->first < earliest_future_visual_start_seconds - 0.001)
				fail_invalid_replugged_boundary("predicted lock span entered current measure");
			lane_view.locked_visual_start_offset_seconds = static_cast<float>((std::max)(0.0, predicted_lock_range->first - song_time_seconds()));
			lane_view.locked_visual_end_offset_seconds = static_cast<float>((std::max)(0.0, predicted_lock_range->second - song_time_seconds()));
		}
		lane_view.lane_held = lane_runtime.lane_held;
		const std::uint8_t sustain_mask = active_sustain_lane_mask(index, song_time_seconds());
		for (size_t fret = 0; fret < lane_view.lane_sustaining.size(); ++fret)
			lane_view.lane_sustaining[fret] = (sustain_mask & static_cast<std::uint8_t>(1u << fret)) != 0;

		if (frame_cache != nullptr)
		{
			lane_view.visible_notes = frame_cache->visible_highway_notes;
			lane_view.visible_measure_lines = frame_cache->visible_highway_measure_lines;
		}
		return lane_view;
	}

	void SongSession::refresh_lane_frame_cache(double song_time_seconds)
	{
		lane_frame_cache_.resize(gameplay_lanes_.size());
		for (size_t lane_index = 0; lane_index < gameplay_lanes_.size(); ++lane_index)
			populate_lane_frame_cache(lane_index, song_time_seconds);
	}

	void SongSession::populate_lane_frame_cache(size_t lane_index, double song_time_seconds)
	{
		if (lane_index >= gameplay_lanes_.size() || lane_index >= lane_frame_cache_.size())
			return;

		const GameplayLaneDefinition &lane_definition = gameplay_lanes_[lane_index];
		LaneFrameCache &frame_cache = lane_frame_cache_[lane_index];
		frame_cache = {};

		const double min_time = song_time_seconds - kChartLookbehindSeconds;
		const double max_time = song_time_seconds + kChartLookaheadSeconds;

		const auto &notes = lane_definition.midi_chart.notes();
		auto note_begin = std::lower_bound(notes.begin(), notes.end(), min_time,
			[](const MidiChartNote &note, double time_seconds)
			{
				return note.end_seconds < time_seconds;
			});
		for (auto it = note_begin; it != notes.end(); ++it)
		{
			if (it->start_seconds > max_time)
				break;

			if (play_state_.gameplay_mode == GameplayMode::Replugged &&
				lane_index < play_state_.lanes.size())
			{
				const GameplayLaneRuntimeState &lane_runtime = play_state_.lanes[lane_index];
				const int current_measure_index = replugged_measure_index_for_time(lane_index, song_time_seconds);
				double hidden_start_time_seconds = lane_runtime.lock_start_time_seconds;
				const auto current_measure = replugged_section_range_for_time(lane_index, song_time_seconds);
				if (current_measure.has_value())
				{
					if (lane_runtime.lock_end_measure_index > current_measure_index &&
						lane_runtime.lock_start_measure_index > current_measure_index &&
						lane_runtime.lock_start_time_seconds < current_measure->second - 0.001)
					{
						fail_invalid_replugged_boundary("hidden note span entered current measure");
					}
					if (lane_runtime.lock_start_measure_index <= current_measure_index)
						hidden_start_time_seconds = current_measure->first;
				}
				if (lane_runtime.lock_end_measure_index > current_measure_index &&
					it->start_seconds >= hidden_start_time_seconds &&
					it->start_seconds < lane_runtime.lock_end_time_seconds)
				{
					continue;
				}
			}

			HighwayNoteView highway_note;
			highway_note.lane = it->lane;
			highway_note.start_offset_seconds = static_cast<float>(it->start_seconds - song_time_seconds);
			highway_note.length_seconds = static_cast<float>((std::max)(it->end_seconds - it->start_seconds, 0.0));
			frame_cache.visible_highway_notes.push_back(highway_note);

			SongPlayerView::ChartNoteView chart_note;
			chart_note.lane = it->lane;
			chart_note.start_offset_seconds = static_cast<float>(it->start_seconds - song_time_seconds);
			chart_note.length_seconds = static_cast<float>((std::max)(it->end_seconds - it->start_seconds, 0.0));
			frame_cache.visible_chart_notes.push_back(chart_note);
		}

		const auto &measure_lines = lane_definition.midi_chart.measure_lines();
		auto measure_begin = std::lower_bound(measure_lines.begin(), measure_lines.end(), min_time,
			[](const MidiChartMeasureLine &measure_line, double time_seconds)
			{
				return measure_line.time_seconds < time_seconds;
			});
		for (auto it = measure_begin; it != measure_lines.end(); ++it)
		{
			if (it->time_seconds > max_time)
				break;

			const bool is_measure = it->kind == MidiChartMeasureLine::Kind::Measure;
			const bool is_strong = is_measure || it->kind == MidiChartMeasureLine::Kind::Strong;

			HighwayMeasureLineView highway_measure_line;
			highway_measure_line.offset_seconds = static_cast<float>(it->time_seconds - song_time_seconds);
			highway_measure_line.is_measure = is_measure;
			highway_measure_line.is_strong = is_strong;
			frame_cache.visible_highway_measure_lines.push_back(highway_measure_line);

			SongPlayerView::ChartMeasureLineView chart_measure_line;
			chart_measure_line.offset_seconds = static_cast<float>(it->time_seconds - song_time_seconds);
			chart_measure_line.is_measure = is_measure;
			chart_measure_line.is_strong = is_strong;
			frame_cache.visible_chart_measure_lines.push_back(chart_measure_line);
		}

		int display_line_index = 0;
		if (!lane_definition.lyric_phrase_ranges.empty())
		{
			display_line_index = static_cast<int>(lane_definition.lyric_phrase_ranges.size() - 1);
			for (size_t index = 0; index < lane_definition.lyric_phrase_ranges.size(); ++index)
			{
				if (song_time_seconds < lane_definition.lyric_phrase_ranges[index].end_seconds)
				{
					display_line_index = static_cast<int>(index);
					break;
				}
			}
		}
		else
		{
			for (const CachedLyricToken &token : lane_definition.lyric_tokens)
			{
				if (token.end_seconds > song_time_seconds)
				{
					display_line_index = token.line_index;
					break;
				}
			}
		}

		frame_cache.current_lyric_line_index = display_line_index;
		bool has_display_line = false;
		for (const CachedLyricToken &token : lane_definition.lyric_tokens)
		{
			if (token.line_index == display_line_index)
			{
				has_display_line = true;
				break;
			}
		}
		if (!has_display_line)
		{
			for (const CachedLyricToken &token : lane_definition.lyric_tokens)
			{
				if (token.line_index <= display_line_index)
					frame_cache.current_lyric_line_index = token.line_index;
			}
		}

		for (const CachedLyricToken &token : lane_definition.lyric_tokens)
		{
			if (token.line_index > frame_cache.current_lyric_line_index)
			{
				frame_cache.next_lyric_line_index = token.line_index;
				break;
			}
		}

		for (const CachedLyricToken &token : lane_definition.lyric_tokens)
		{
			if (token.line_index != frame_cache.current_lyric_line_index &&
				token.line_index != frame_cache.next_lyric_line_index)
			{
				continue;
			}

			SongPlayerView::LyricTokenView lyric_view;
			lyric_view.text = token.text;
			lyric_view.start_offset_seconds = static_cast<float>(token.start_seconds - song_time_seconds);
			lyric_view.end_offset_seconds = static_cast<float>(token.end_seconds - song_time_seconds);
			lyric_view.is_current = lyric_view.start_offset_seconds <= 0.0f && lyric_view.end_offset_seconds > 0.0f;
			lyric_view.is_past = lyric_view.end_offset_seconds <= 0.0f;
			lyric_view.prepend_space = token.prepend_space;
			lyric_view.append_hyphen = token.append_hyphen;
			lyric_view.line_index = token.line_index;
			frame_cache.visible_lyric_tokens.push_back(std::move(lyric_view));
		}
	}

	void SongSession::rebuild_cached_scene(GameplaySceneView &scene) const
	{
		scene = {};
		scene.clear_color = {12.0f / 255.0f, 14.0f / 255.0f, 20.0f / 255.0f, 1.0f};

		if (!is_loaded())
			return;

		PlayerGameplayView gameplay_player;
		gameplay_player.normalized_rect = {0.0f, 0.0f, 1.0f, 1.0f};
		gameplay_player.camera = make_default_guitar_camera_view();
		gameplay_player.world.gameplay_mode = play_state_.gameplay_mode;
		gameplay_player.world.style = make_default_guitar_highway_style_view();
		for (size_t index = 0; index < gameplay_lanes_.size(); ++index)
			gameplay_player.world.lanes.push_back(gameplay_lane_view(index));
		if (gameplay_player.world.lanes.empty())
			gameplay_player.world.lanes.push_back(gameplay_lane_view(0));
		gameplay_player.world.focused_lane_index = play_state_.active_lane_index;
		gameplay_player.world.focus_blend =
			(play_state_.gameplay_mode == GameplayMode::Freeplay ||
			 play_state_.gameplay_mode == GameplayMode::Replugged) ? 0.35f : 1.0f;
		if (play_state_.gameplay_mode == GameplayMode::Freeplay ||
			play_state_.gameplay_mode == GameplayMode::Replugged)
			layout_freeplay_lanes(gameplay_player.world.lanes, gameplay_player.world.focused_lane_index);
		else if (!gameplay_player.world.lanes.empty())
		{
			gameplay_player.world.lanes[0].lane_center_x = 0.0f;
			gameplay_player.world.lanes[0].lane_width = 5.0f;
			gameplay_player.world.lanes[0].lane_depth_offset = 0.0f;
		}

		gameplay_player.hud.player_label = frame_snapshot_.player.song_title.empty() ? "Player 1" : frame_snapshot_.player.song_title;
		gameplay_player.hud.status_message = frame_snapshot_.player.status_message;
		gameplay_player.hud.song_time_seconds = frame_snapshot_.player.song_time_seconds;
		gameplay_player.hud.failed = false;
		scene.players.push_back(std::move(gameplay_player));
	}

	void SongSession::rebuild_cached_player_view(SongPlayerView &player_view, const std::string &status_message) const
	{
		player_view = {};
		player_view.song_title = song_player_.metadata().name;
		player_view.song_artist = song_player_.metadata().artist;
		player_view.status_message = status_message.empty() ? chart_status_message_ : status_message;
		if (status_message.empty() &&
			!transient_gameplay_status_message_.empty() &&
			song_time_seconds() <= transient_gameplay_status_until_seconds_ + 0.001)
		{
			player_view.status_message = transient_gameplay_status_message_;
		}
		player_view.loaded_stem_count = song_player_.loaded_stem_count();
		player_view.song_time_seconds = song_time_seconds();
		player_view.song_duration_seconds = song_player_.duration_seconds();

		const size_t lane_index = active_lane_index();
		if (lane_index >= gameplay_lanes_.size() || lane_index >= play_state_.lanes.size())
		{
			player_view.song_time_remaining_seconds = (std::max)(0.0, player_view.song_duration_seconds - player_view.song_time_seconds);
			return;
		}

		const GameplayLaneDefinition &lane = gameplay_lanes_[lane_index];
		const GameplayLaneRuntimeState &lane_runtime = play_state_.lanes[lane_index];
		const LaneFrameCache *frame_cache = lane_index < lane_frame_cache_.size() ? &lane_frame_cache_[lane_index] : nullptr;
		player_view.has_playable_stem = has_lane_stem(lane_index);
		player_view.playable_stem_muted = has_lane_stem(lane_index) && lane_stem_target_gain(lane_index) < 0.5f;
		player_view.playable_stem_label = lane.instrument_label;
		player_view.lane_held = lane_runtime.lane_held;
		player_view.song_duration_seconds = (std::max)(lane.midi_chart.duration_seconds(), player_view.song_duration_seconds);
		player_view.song_time_remaining_seconds = (std::max)(0.0, player_view.song_duration_seconds - player_view.song_time_seconds);
		const std::uint8_t sustain_mask = active_sustain_lane_mask(lane_index, player_view.song_time_seconds);
		for (size_t fret = 0; fret < player_view.lane_sustaining.size(); ++fret)
			player_view.lane_sustaining[fret] = (sustain_mask & static_cast<std::uint8_t>(1u << fret)) != 0;
		player_view.has_chart = lane.midi_chart.is_loaded();
		player_view.chart_track_name = std::string(lane.midi_chart.track_name());
		player_view.chart_difficulty_name = std::string(lane.midi_chart.difficulty_name());
		player_view.chart_beats_per_minute = lane.midi_chart.bpm_at_time(player_view.song_time_seconds);

		if (frame_cache != nullptr)
		{
			player_view.visible_chart_notes = frame_cache->visible_chart_notes;
			player_view.visible_measure_lines = frame_cache->visible_chart_measure_lines;
			player_view.current_lyric_line_index = frame_cache->current_lyric_line_index;
			player_view.next_lyric_line_index = frame_cache->next_lyric_line_index;
			player_view.visible_lyric_tokens = frame_cache->visible_lyric_tokens;
		}
	}

	size_t SongSession::active_lane_index() const
	{
		if (play_state_.active_lane_index < 0 || play_state_.active_lane_index >= static_cast<int>(gameplay_lanes_.size()))
			return gameplay_lanes_.size();
		return static_cast<size_t>(play_state_.active_lane_index);
	}

	double SongSession::adjusted_song_time_seconds() const
	{
		return (std::max)(0.0, transport_.song_time_seconds() - play_state_.timing_offset_seconds);
	}

	std::uint64_t SongSession::session_fingerprint() const
	{
		std::uint64_t hash = 1469598103934665603ull;
		hash_string(hash, song_player_.metadata().name);
		hash_string(hash, song_player_.metadata().artist);
		hash_string(hash, song_player_.metadata().album);
		hash_string(hash, song_player_.metadata().charter);
		const double audio_duration_seconds = song_player_.duration_seconds();
		hash_value(hash, audio_duration_seconds);
		hash_value(hash, play_state_.gameplay_mode);
		const std::uint64_t lane_count = static_cast<std::uint64_t>(gameplay_lanes_.size());
		hash_value(hash, lane_count);

		for (const GameplayLaneDefinition &lane : gameplay_lanes_)
		{
			hash_value(hash, lane.instrument);
			hash_value(hash, lane.instrument_type);
			hash_string(hash, lane.instrument_label);
			hash_string(hash, lane.midi_chart.track_name());
			hash_string(hash, lane.midi_chart.difficulty_name());
			const double lane_duration_seconds = lane.midi_chart.duration_seconds();
			hash_value(hash, lane_duration_seconds);

			const std::uint64_t stem_count = static_cast<std::uint64_t>(lane.stem_names.size());
			hash_value(hash, stem_count);
			for (const std::string &stem_name : lane.stem_names)
				hash_string(hash, stem_name);

			const std::vector<MidiChartNote> &notes = lane.midi_chart.notes();
			const std::uint64_t note_count = static_cast<std::uint64_t>(notes.size());
			hash_value(hash, note_count);
			for (const MidiChartNote &note : notes)
			{
				hash_value(hash, note.lane);
				hash_value(hash, note.tick);
				hash_value(hash, note.end_tick);
				hash_value(hash, note.start_seconds);
				hash_value(hash, note.end_seconds);
			}
		}

		return hash;
	}
}
