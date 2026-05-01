#include "core/play/SongSession.h"

#include "frontend_contract/RetroFileSystem.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>

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
		constexpr std::uint32_t kSerializedPlayStateVersion = 3u;

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
			std::uint8_t reserved0 = 0;
			std::uint8_t reserved1 = 0;
			std::uint8_t reserved2 = 0;
			double lane_sustain_end_times[5]{};
			double lane_sustain_release_times[5]{};
			std::uint64_t input_generation = 0;
			std::uint64_t consumed_input_generation = 0;
			std::uint64_t next_note_index = 0;
			float stem_target_gain = 1.0f;
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
		const GameplayOptions &options,
		std::string &error_message)
	{
		return load_preloaded(file_system, song_directory, {}, options, error_message);
	}

	bool SongSession::load_preloaded(::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system,
		const std::string &song_directory,
		PrototypePlayer::PreloadedSongData preloaded_song_data,
		const GameplayOptions &options,
		std::string &error_message)
	{
		unload();
		if (!preloaded_song_data.stems.empty())
		{
			if (!prototype_player_.load_preloaded(std::move(preloaded_song_data), error_message))
				return false;
		}
		else if (!prototype_player_.load(file_system, song_directory, error_message))
		{
			return false;
		}

		return reconfigure_loaded(file_system, song_directory, options, error_message);
	}

	bool SongSession::reconfigure_loaded(::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system,
		const std::string &song_directory,
		const GameplayOptions &options,
		std::string &error_message)
	{
		if (!prototype_player_.is_loaded())
		{
			error_message = "Song audio is not loaded.";
			return false;
		}

		prototype_player_.rewind();
		transport_.configure(prototype_player_.sample_rate());
		audio_mixer_.set_prototype_player(&prototype_player_);
		if (!configure_gameplay_lanes(file_system, song_directory, options, error_message))
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
		prototype_player_.unload();
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
			if (index == lane_index)
				continue;
			advance_inactive_lane(index, current_time_seconds);
		}
		if (lane_held != lane.lane_held)
			++lane.input_generation;
		lane.lane_held = lane_held;
		if (!is_loaded() || gameplay_lanes_[lane_index].midi_chart.notes().empty())
		{
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

		if (resolved_note_hit || lane.next_note_index >= gameplay_lanes_[lane_index].midi_chart.notes().size())
			set_lane_stem_target_gain(lane_index, 1.0f);
	}

	bool SongSession::switch_active_lane(int delta)
	{
		if (play_state_.gameplay_mode != GameplayMode::Freeplay || gameplay_lanes_.size() < 2 || delta == 0)
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
		set_lane_stem_target_gain(static_cast<size_t>(previous_index), 1.0f);

		play_state_.active_lane_index = next_index;
		GameplayLaneRuntimeState &next_lane = play_state_.lanes[static_cast<size_t>(play_state_.active_lane_index)];
		next_lane.lane_held.fill(false);
		set_lane_stem_target_gain(static_cast<size_t>(play_state_.active_lane_index), 1.0f);
		return true;
	}

	bool SongSession::has_stem(std::string_view stem_name) const
	{
		return prototype_player_.has_stem(stem_name);
	}

	size_t SongSession::loaded_stem_count() const
	{
		return prototype_player_.loaded_stem_count();
	}

	void SongSession::set_stem_target_gain(std::string_view stem_name, float gain)
	{
		if (is_loaded())
			prototype_player_.set_stem_target_gain(stem_name, gain);
	}

	float SongSession::stem_target_gain(std::string_view stem_name) const
	{
		return prototype_player_.stem_target_gain(stem_name);
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
		return prototype_player_.playback_finished();
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

		const PrototypePlayer::PlaybackState audio_state = prototype_player_.playback_state();
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

		const PrototypePlayer::PlaybackState audio_state = prototype_player_.playback_state();
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
			for (size_t fret = 0; fret < lane.lane_sustain_end_times_.size(); ++fret)
			{
				serialized_lane.lane_sustain_end_times[fret] = lane.lane_sustain_end_times_[fret];
				serialized_lane.lane_sustain_release_times[fret] = lane.lane_sustain_release_times_[fret];
			}
			serialized_lane.input_generation = lane.input_generation;
			serialized_lane.consumed_input_generation = lane.consumed_input_generation;
			serialized_lane.next_note_index = static_cast<std::uint64_t>(lane.next_note_index);
			serialized_lane.stem_target_gain = lane.stem_target_gain;
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
		if (header.gameplay_mode > static_cast<std::uint32_t>(GameplayMode::Freeplay))
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

		const PrototypePlayer::PlaybackState current_audio_state = prototype_player_.playback_state();
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

			const size_t note_count = gameplay_lanes_[lane_index].midi_chart.notes().size();
			if (lane.next_note_index > note_count)
			{
				error_message = "Serialized play-state note progress is out of range.";
				return false;
			}
		}

		PrototypePlayer::PlaybackState restored_audio_state;
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
		if (!prototype_player_.restore_playback_state(restored_audio_state, error_message))
			return false;

		for (size_t lane_index = 0; lane_index < play_state_.lanes.size(); ++lane_index)
			set_lane_stem_target_gain(lane_index, play_state_.lanes[lane_index].stem_target_gain);

		refresh_frame_snapshot({});
		error_message.clear();
		return true;
	}

	PrototypePlayerView SongSession::view(const std::string &status_message) const
	{
		PrototypePlayerView player_view;
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
		::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system,
		const std::string &song_directory,
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
				if (prototype_player_.has_stem(stem_name))
					lane.stem_names.push_back(stem_name);
			}
			if (lane.stem_names.empty() &&
				(instrument == InstrumentOption::Guitar || instrument == InstrumentOption::CoopGuitar) &&
				prototype_player_.has_stem("guitar"))
			{
				lane.stem_names.push_back("guitar");
			}
			if (lane.stem_names.empty() && instrument == InstrumentOption::Bass &&
				prototype_player_.has_stem("rhythm"))
			{
				lane.stem_names.push_back("rhythm");
			}
			if (lane.stem_names.empty() && instrument == InstrumentOption::Rhythm &&
				prototype_player_.has_stem("bass"))
			{
				lane.stem_names.push_back("bass");
			}
			return lane;
		};

		if (play_state_.gameplay_mode == GameplayMode::Classic)
		{
			GameplayLaneDefinition lane = build_lane(options.instrument());
			std::string chart_error_message;
			lane.midi_chart.load(
				file_system,
				song_directory,
				to_midi_chart_difficulty(options.difficulty()),
				to_midi_chart_track_type(options.instrument()),
				chart_error_message);
			cache_lyric_data(lane);
			chart_status_message_ = std::move(chart_error_message);
			gameplay_lanes_.push_back(std::move(lane));
		}
		else
		{
			MidiChart inspect_chart;
			std::string inspect_error_message;
			inspect_chart.load(
				file_system,
				song_directory,
				to_midi_chart_difficulty(options.difficulty()),
				MidiChartTrackType::FiveFretGuitar,
				inspect_error_message);

			std::vector<InstrumentOption> freeplay_instruments;
			for (const MidiChartTrackType track_type : inspect_chart.available_preview_track_types())
			{
				const std::optional<InstrumentOption> instrument = to_instrument_option(track_type);
				if (!instrument.has_value() || !track_has_exact_difficulty(inspect_chart, *instrument, options.difficulty()))
					continue;

				freeplay_instruments.push_back(*instrument);
			}

			std::sort(
				freeplay_instruments.begin(),
				freeplay_instruments.end(),
				[](InstrumentOption left, InstrumentOption right)
				{
					return instrument_sort_rank(left) < instrument_sort_rank(right);
				});

			for (const InstrumentOption instrument : freeplay_instruments)
			{
				GameplayLaneDefinition lane = build_lane(instrument);
				std::string lane_error_message;
				lane.midi_chart.load(
					file_system,
					song_directory,
					to_midi_chart_difficulty(options.difficulty()),
					to_midi_chart_track_type(instrument),
					lane_error_message);
				if (!lane.midi_chart.is_loaded() ||
					lane.midi_chart.track_name() != instrument_label_for(instrument) ||
					lane.midi_chart.difficulty_name() != difficulty_label_for(options.difficulty()))
				{
					continue;
				}

				cache_lyric_data(lane);
				gameplay_lanes_.push_back(std::move(lane));
			}

			if (gameplay_lanes_.empty())
			{
				error_message = "Freeplay mode requires at least one playable instrument at the selected difficulty.";
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
			prototype_player_.set_stem_target_gain(stem_name, clamped_gain);
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

			lane.next_note_index = note_group_end_index(lane_index, lane.next_note_index);
			missed_any_notes = true;
		}

		if (missed_any_notes)
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
		lane_view.instrument_type = lane.instrument_type;
		lane_view.instrument_label = lane.instrument_label;
		lane_view.is_active = static_cast<int>(index) == play_state_.active_lane_index;
		lane_view.is_muted = has_lane_stem(index) && lane_stem_target_gain(index) < 0.5f;
		lane_view.has_chart = lane.midi_chart.is_loaded();
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

			HighwayNoteView highway_note;
			highway_note.lane = it->lane;
			highway_note.start_offset_seconds = static_cast<float>(it->start_seconds - song_time_seconds);
			highway_note.length_seconds = static_cast<float>((std::max)(it->end_seconds - it->start_seconds, 0.0));
			frame_cache.visible_highway_notes.push_back(highway_note);

			PrototypePlayerView::ChartNoteView chart_note;
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

			PrototypePlayerView::ChartMeasureLineView chart_measure_line;
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

			PrototypePlayerView::LyricTokenView lyric_view;
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
		gameplay_player.world.style = make_default_guitar_highway_style_view();
		for (size_t index = 0; index < gameplay_lanes_.size(); ++index)
			gameplay_player.world.lanes.push_back(gameplay_lane_view(index));
		if (gameplay_player.world.lanes.empty())
			gameplay_player.world.lanes.push_back(gameplay_lane_view(0));
		gameplay_player.world.focused_lane_index = play_state_.active_lane_index;
		gameplay_player.world.focus_blend = play_state_.gameplay_mode == GameplayMode::Freeplay ? 0.35f : 1.0f;
		if (play_state_.gameplay_mode == GameplayMode::Freeplay)
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

	void SongSession::rebuild_cached_player_view(PrototypePlayerView &player_view, const std::string &status_message) const
	{
		player_view = {};
		player_view.song_title = prototype_player_.metadata().name;
		player_view.song_artist = prototype_player_.metadata().artist;
		player_view.status_message = status_message.empty() ? chart_status_message_ : status_message;
		player_view.loaded_stem_count = prototype_player_.loaded_stem_count();
		player_view.song_time_seconds = song_time_seconds();
		player_view.song_duration_seconds = prototype_player_.duration_seconds();

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
		hash_string(hash, prototype_player_.metadata().name);
		hash_string(hash, prototype_player_.metadata().artist);
		hash_string(hash, prototype_player_.metadata().album);
		hash_string(hash, prototype_player_.metadata().charter);
		const double audio_duration_seconds = prototype_player_.duration_seconds();
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
