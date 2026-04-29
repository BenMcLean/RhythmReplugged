#include "core/play/SongSession.h"

#include "frontend_contract/RetroFileSystem.h"

#include <algorithm>
#include <cmath>
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

		std::vector<LyricPhraseRange> collect_lyric_phrase_ranges(const MidiChart &chart)
		{
			std::vector<LyricPhraseRange> ranges;
			const MidiChartTrack *preferred_track = nullptr;
			for (const MidiChartTrack &track : chart.tracks())
			{
				if (track.type == MidiChartTrackType::Vocals)
				{
					preferred_track = &track;
					break;
				}
			}

			if (preferred_track == nullptr)
			{
				for (const MidiChartTrack &track : chart.tracks())
				{
					if (track.type == MidiChartTrackType::Harmony1 ||
						track.type == MidiChartTrackType::Harmony2 ||
						track.type == MidiChartTrackType::Harmony3)
					{
						preferred_track = &track;
						break;
					}
				}
			}

			if (preferred_track == nullptr)
				return ranges;

			for (const MidiChartPhrase &phrase : preferred_track->phrases)
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
		loaded_.store(true);
		error_message.clear();
		return true;
	}

	void SongSession::unload()
	{
		audio_mixer_.reset();
		transport_.reset();
		gameplay_mode_ = GameplayMode::Classic;
		gameplay_lanes_.clear();
		active_lane_index_ = 0;
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
		if (GameplayLaneState *lane = active_lane(); lane != nullptr)
			set_lane_stem_target_gain(*lane, lane_stem_target_gain(*lane) > 0.5f ? 0.0f : 1.0f);
	}

	void SongSession::update_gameplay_input(const std::array<bool, 5> &lane_held, const std::array<bool, 5> &lane_pressed)
	{
		GameplayLaneState *lane = active_lane();
		if (lane == nullptr)
			return;
		const double current_time_seconds = song_time_seconds();
		for (size_t index = 0; index < gameplay_lanes_.size(); ++index)
		{
			if (&gameplay_lanes_[index] == lane)
				continue;
			advance_inactive_lane(gameplay_lanes_[index], current_time_seconds);
		}
		if (lane_held != lane->lane_held)
			++lane->input_generation;
		lane->lane_held = lane_held;
		if (!is_loaded() || lane->midi_chart.notes().empty())
		{
			set_lane_stem_target_gain(*lane, 1.0f);
			return;
		}

		consume_missed_note_groups(*lane, current_time_seconds);
		const std::uint8_t held_mask = lane_mask_from_state(lane->lane_held);

		const std::uint8_t pressed_mask = lane_mask_from_state(lane_pressed);
		bool resolved_note_hit = false;
		if (pressed_mask != 0 && lane->next_note_index < lane->midi_chart.notes().size())
		{
			const std::vector<MidiChartNote> &notes = lane->midi_chart.notes();
			const size_t group_end_index = note_group_end_index(*lane, lane->next_note_index);
			const double note_time_seconds = notes[lane->next_note_index].start_seconds;
			if (std::fabs(note_time_seconds - current_time_seconds) <= kNoteHitWindowSeconds)
			{
				const std::uint8_t expected_mask = note_group_lane_mask(*lane, lane->next_note_index, group_end_index);
				if (held_mask_satisfies_expected(held_mask, expected_mask))
				{
					start_sustains_for_note_group(*lane, lane->next_note_index, group_end_index);
					lane->next_note_index = group_end_index;
					lane->consumed_input_generation = lane->input_generation;
					resolved_note_hit = true;
				}
				else
				{
					set_lane_stem_target_gain(*lane, 0.0f);
				}
			}
		}

		if (!resolved_note_hit && lane->next_note_index < lane->midi_chart.notes().size())
		{
			const std::vector<MidiChartNote> &notes = lane->midi_chart.notes();
			const size_t group_end_index = note_group_end_index(*lane, lane->next_note_index);
			const double note_time_seconds = notes[lane->next_note_index].start_seconds;
			const std::uint8_t expected_mask = note_group_lane_mask(*lane, lane->next_note_index, group_end_index);
			if (lane->input_generation > lane->consumed_input_generation &&
				std::fabs(note_time_seconds - current_time_seconds) <= kNoteHitWindowSeconds &&
				held_mask_satisfies_expected(held_mask, expected_mask))
			{
				start_sustains_for_note_group(*lane, lane->next_note_index, group_end_index);
				lane->next_note_index = group_end_index;
				lane->consumed_input_generation = lane->input_generation;
				resolved_note_hit = true;
			}
		}

		refresh_active_sustains(*lane, current_time_seconds, held_mask);
		const std::uint8_t sustain_mask = active_sustain_lane_mask(*lane, current_time_seconds);
		if (sustain_mask != 0)
		{
			const std::uint8_t imminent_note_mask = imminent_note_lane_mask(*lane, current_time_seconds);
			const std::uint8_t required_sustain_mask = static_cast<std::uint8_t>(sustain_mask & ~imminent_note_mask);
			if (required_sustain_mask == 0 || held_mask_satisfies_expected(held_mask, required_sustain_mask))
				set_lane_stem_target_gain(*lane, 1.0f);
			else
				set_lane_stem_target_gain(*lane, 0.0f);
			return;
		}

		if (resolved_note_hit || lane->next_note_index >= lane->midi_chart.notes().size())
			set_lane_stem_target_gain(*lane, 1.0f);
	}

	bool SongSession::switch_active_lane(int delta)
	{
		if (gameplay_mode_ != GameplayMode::Freeplay || gameplay_lanes_.size() < 2 || delta == 0)
			return false;

		const int lane_count = static_cast<int>(gameplay_lanes_.size());
		const int previous_index = active_lane_index_;
		int next_index = (active_lane_index_ + delta) % lane_count;
		if (next_index < 0)
			next_index += lane_count;
		if (next_index == previous_index)
			return false;

		GameplayLaneState &previous_lane = gameplay_lanes_[static_cast<size_t>(previous_index)];
		previous_lane.lane_held.fill(false);
		previous_lane.lane_sustain_end_times_.fill(0.0);
		previous_lane.lane_sustain_release_times_.fill(-1.0);
		set_lane_stem_target_gain(previous_lane, 1.0f);

		active_lane_index_ = next_index;
		GameplayLaneState &next_lane = gameplay_lanes_[static_cast<size_t>(active_lane_index_)];
		next_lane.lane_held.fill(false);
		set_lane_stem_target_gain(next_lane, 1.0f);
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
		timing_offset_seconds_ = std::clamp(offset_seconds, -0.250, 0.250);
	}

	double SongSession::timing_offset_seconds() const
	{
		return timing_offset_seconds_;
	}

	PrototypePlayerView SongSession::view(const std::string &status_message) const
	{
		PrototypePlayerView player_view;
		player_view.song_title = prototype_player_.metadata().name;
		player_view.song_artist = prototype_player_.metadata().artist;
		player_view.status_message = status_message;
		if (player_view.status_message.empty())
			player_view.status_message = chart_status_message_;
		const GameplayLaneState *lane = active_lane();
		player_view.has_playable_stem = lane != nullptr && has_lane_stem(*lane);
		player_view.playable_stem_muted = lane != nullptr && has_lane_stem(*lane) && lane_stem_target_gain(*lane) < 0.5f;
		player_view.playable_stem_label = lane != nullptr ? lane->instrument_label : "Guitar";
		if (lane != nullptr)
			player_view.lane_held = lane->lane_held;
		player_view.loaded_stem_count = prototype_player_.loaded_stem_count();
		player_view.song_time_seconds = song_time_seconds();
		player_view.song_duration_seconds = prototype_player_.duration_seconds();
		if (lane != nullptr)
			player_view.song_duration_seconds = (std::max)(lane->midi_chart.duration_seconds(), player_view.song_duration_seconds);
		player_view.song_time_remaining_seconds = (std::max)(0.0, player_view.song_duration_seconds - player_view.song_time_seconds);
		const std::uint8_t sustain_mask = lane != nullptr ? active_sustain_lane_mask(*lane, player_view.song_time_seconds) : 0;
		for (size_t fret = 0; fret < player_view.lane_sustaining.size(); ++fret)
			player_view.lane_sustaining[fret] = (sustain_mask & static_cast<std::uint8_t>(1u << fret)) != 0;
		player_view.has_chart = lane != nullptr && lane->midi_chart.is_loaded();
		player_view.chart_track_name = lane != nullptr ? std::string(lane->midi_chart.track_name()) : std::string();
		player_view.chart_difficulty_name = lane != nullptr ? std::string(lane->midi_chart.difficulty_name()) : std::string();
		player_view.chart_beats_per_minute = lane != nullptr ? lane->midi_chart.bpm_at_time(player_view.song_time_seconds) : 120.0;

		if (lane != nullptr)
		{
			const std::vector<MidiChartNote> visible_notes = lane->midi_chart.collect_visible_notes(
				player_view.song_time_seconds,
				kChartLookbehindSeconds,
				kChartLookaheadSeconds);
			player_view.visible_chart_notes.reserve(visible_notes.size());
			for (const MidiChartNote &note : visible_notes)
			{
				PrototypePlayerView::ChartNoteView note_view;
				note_view.lane = note.lane;
				note_view.start_offset_seconds = static_cast<float>(note.start_seconds - player_view.song_time_seconds);
				note_view.length_seconds = static_cast<float>((std::max)(note.end_seconds - note.start_seconds, 0.0));
				player_view.visible_chart_notes.push_back(note_view);
			}

			const std::vector<MidiChartMeasureLine> visible_measure_lines = lane->midi_chart.collect_visible_measure_lines(
				player_view.song_time_seconds,
				kChartLookbehindSeconds,
				kChartLookaheadSeconds);
			player_view.visible_measure_lines.reserve(visible_measure_lines.size());
			for (const MidiChartMeasureLine &measure_line : visible_measure_lines)
			{
				PrototypePlayerView::ChartMeasureLineView measure_line_view;
				measure_line_view.offset_seconds = static_cast<float>(measure_line.time_seconds - player_view.song_time_seconds);
				measure_line_view.is_measure = measure_line.kind == MidiChartMeasureLine::Kind::Measure;
				measure_line_view.is_strong =
					measure_line.kind == MidiChartMeasureLine::Kind::Measure ||
					measure_line.kind == MidiChartMeasureLine::Kind::Strong;
				player_view.visible_measure_lines.push_back(measure_line_view);
			}
		}

		const MidiChart *lyrics_chart = lane != nullptr ? &lane->midi_chart : nullptr;
		const std::vector<MidiChartTextEvent> lyrics = lyrics_chart != nullptr ? lyrics_chart->lyrics() : std::vector<MidiChartTextEvent>{};
		const std::vector<LyricPhraseRange> lyric_phrase_ranges = lyrics_chart != nullptr
			? collect_lyric_phrase_ranges(*lyrics_chart)
			: std::vector<LyricPhraseRange>{};
		size_t phrase_index = 0;
		int current_line_index = 0;
		bool previous_join_without_space = false;
		std::vector<PrototypePlayerView::LyricTokenView> all_lyric_tokens;
		for (size_t index = 0; index < lyrics.size(); ++index)
		{
			const MidiChartTextEvent &lyric = lyrics[index];
			if (!is_displayable_lyric(lyric))
				continue;

			const ParsedLyricDisplay parsed_lyric = parse_lyric_display(lyric.text);
			if (parsed_lyric.text.empty())
			{
				continue;
			}

			while (phrase_index < lyric_phrase_ranges.size() &&
				lyric_phrase_ranges[phrase_index].end_seconds <= lyric.time_seconds)
			{
				++phrase_index;
			}

			const bool lyric_in_phrase =
				phrase_index < lyric_phrase_ranges.size() &&
				lyric.time_seconds >= lyric_phrase_ranges[phrase_index].start_seconds &&
				lyric.time_seconds < lyric_phrase_ranges[phrase_index].end_seconds;

			double next_time_seconds = lyric.time_seconds + 0.75;
			for (size_t next_index = index + 1; next_index < lyrics.size(); ++next_index)
			{
				if (!is_displayable_lyric(lyrics[next_index]))
					continue;

				next_time_seconds = lyrics[next_index].time_seconds;
				break;
			}

			if (lyric_in_phrase)
			{
				current_line_index = static_cast<int>(phrase_index);
			}
			else if (!all_lyric_tokens.empty() && parsed_lyric.force_new_line_before)
			{
				++current_line_index;
			}

			PrototypePlayerView::LyricTokenView lyric_view;
			lyric_view.text = parsed_lyric.text;
			lyric_view.start_offset_seconds = static_cast<float>(lyric.time_seconds - player_view.song_time_seconds);
			lyric_view.end_offset_seconds = static_cast<float>(next_time_seconds - player_view.song_time_seconds);
			lyric_view.is_current = lyric_view.start_offset_seconds <= 0.0f && lyric_view.end_offset_seconds > 0.0f;
			lyric_view.is_past = lyric_view.end_offset_seconds <= 0.0f;
			lyric_view.prepend_space =
				!all_lyric_tokens.empty() &&
				all_lyric_tokens.back().line_index == current_line_index &&
				!previous_join_without_space &&
				!starts_with_punctuation(parsed_lyric.text);
			lyric_view.append_hyphen = parsed_lyric.append_hyphen;
			lyric_view.line_index = current_line_index;
			all_lyric_tokens.push_back(std::move(lyric_view));

			previous_join_without_space = parsed_lyric.join_next_without_space;
		}

		int display_line_index = 0;
		if (!lyric_phrase_ranges.empty())
		{
			display_line_index = static_cast<int>(lyric_phrase_ranges.size() - 1);
			for (size_t index = 0; index < lyric_phrase_ranges.size(); ++index)
			{
				if (player_view.song_time_seconds < lyric_phrase_ranges[index].end_seconds)
				{
					display_line_index = static_cast<int>(index);
					break;
				}
			}
		}
		else
		{
			for (const PrototypePlayerView::LyricTokenView &token : all_lyric_tokens)
			{
				if (!token.is_past)
				{
					display_line_index = token.line_index;
					break;
				}
			}
		}

			player_view.current_lyric_line_index = display_line_index;
			bool has_display_line = false;
			for (const PrototypePlayerView::LyricTokenView &token : all_lyric_tokens)
			{
				if (token.line_index == display_line_index)
				{
					has_display_line = true;
					break;
				}
			}
			if (!has_display_line)
			{
				for (const PrototypePlayerView::LyricTokenView &token : all_lyric_tokens)
				{
					if (token.line_index <= display_line_index)
						player_view.current_lyric_line_index = token.line_index;
				}
			}

			int next_line_index = -1;
			for (const PrototypePlayerView::LyricTokenView &token : all_lyric_tokens)
			{
				if (token.line_index > player_view.current_lyric_line_index)
				{
					next_line_index = token.line_index;
					break;
				}
			}
			player_view.next_lyric_line_index = next_line_index;

			for (const PrototypePlayerView::LyricTokenView &token : all_lyric_tokens)
			{
				if (token.line_index == player_view.current_lyric_line_index || token.line_index == next_line_index)
					player_view.visible_lyric_tokens.push_back(token);
			}

		return player_view;
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
		gameplay_mode_ = options.gameplay_mode();
		gameplay_lanes_.clear();
		active_lane_index_ = 0;
		chart_status_message_.clear();

		auto build_lane = [&](InstrumentOption instrument)
		{
			GameplayLaneState lane;
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
			reset_runtime_state(lane);
			return lane;
		};

		if (gameplay_mode_ == GameplayMode::Classic)
		{
			GameplayLaneState lane = build_lane(options.instrument());
			std::string chart_error_message;
			lane.midi_chart.load(
				file_system,
				song_directory,
				to_midi_chart_difficulty(options.difficulty()),
				to_midi_chart_track_type(options.instrument()),
				chart_error_message);
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
				GameplayLaneState lane = build_lane(instrument);
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

				gameplay_lanes_.push_back(std::move(lane));
			}

			if (gameplay_lanes_.empty())
			{
				error_message = "Freeplay mode requires at least one playable instrument at the selected difficulty.";
				return false;
			}
		}

		for (const GameplayLaneState &lane : gameplay_lanes_)
		{
			for (const std::string &stem_name : lane.stem_names)
				prototype_player_.set_stem_target_gain(stem_name, 1.0f);
		}

		return true;
	}

	void SongSession::reset_runtime_state(GameplayLaneState &lane)
	{
		lane.lane_held.fill(false);
		lane.lane_sustain_end_times_.fill(0.0);
		lane.lane_sustain_release_times_.fill(-1.0);
		lane.input_generation = 0;
		lane.consumed_input_generation = 0;
		lane.next_note_index = 0;
	}

	void SongSession::set_lane_stem_target_gain(const GameplayLaneState &lane, float gain)
	{
		for (const std::string &stem_name : lane.stem_names)
			prototype_player_.set_stem_target_gain(stem_name, gain);
	}

	float SongSession::lane_stem_target_gain(const GameplayLaneState &lane) const
	{
		float gain = 1.0f;
		bool found_any = false;
		for (const std::string &stem_name : lane.stem_names)
		{
			gain = (std::min)(gain, prototype_player_.stem_target_gain(stem_name));
			found_any = true;
		}
		return found_any ? gain : 0.0f;
	}

	bool SongSession::has_lane_stem(const GameplayLaneState &lane) const
	{
		return !lane.stem_names.empty();
	}

	size_t SongSession::note_group_end_index(const GameplayLaneState &lane, size_t start_index) const
	{
		const std::vector<MidiChartNote> &notes = lane.midi_chart.notes();
		if (start_index >= notes.size())
			return notes.size();

		size_t end_index = start_index + 1;
		const int group_tick = notes[start_index].tick;
		while (end_index < notes.size() && notes[end_index].tick == group_tick)
			++end_index;

		return end_index;
	}

	std::uint8_t SongSession::note_group_lane_mask(const GameplayLaneState &lane, size_t start_index, size_t end_index) const
	{
		std::uint8_t mask = 0;
		const std::vector<MidiChartNote> &notes = lane.midi_chart.notes();
		for (size_t index = start_index; index < end_index && index < notes.size(); ++index)
		{
			const int lane = notes[index].lane;
			if (lane >= 0 && lane < 5)
				mask |= static_cast<std::uint8_t>(1u << lane);
		}

		return mask;
	}

	std::uint8_t SongSession::imminent_note_lane_mask(const GameplayLaneState &lane, double song_time_seconds) const
	{
		const std::vector<MidiChartNote> &notes = lane.midi_chart.notes();
		if (lane.next_note_index >= notes.size())
			return 0;

		const double note_time_seconds = notes[lane.next_note_index].start_seconds;
		if (std::fabs(note_time_seconds - song_time_seconds) > kNoteHitWindowSeconds)
			return 0;

		const size_t group_end_index = note_group_end_index(lane, lane.next_note_index);
		return note_group_lane_mask(lane, lane.next_note_index, group_end_index);
	}

	void SongSession::refresh_active_sustains(GameplayLaneState &lane, double song_time_seconds, std::uint8_t held_mask)
	{
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

	std::uint8_t SongSession::active_sustain_lane_mask(const GameplayLaneState &lane, double song_time_seconds) const
	{
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

	void SongSession::start_sustains_for_note_group(GameplayLaneState &lane, size_t start_index, size_t end_index)
	{
		const std::vector<MidiChartNote> &notes = lane.midi_chart.notes();
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

	void SongSession::consume_missed_note_groups(GameplayLaneState &lane, double song_time_seconds)
	{
		const std::vector<MidiChartNote> &notes = lane.midi_chart.notes();
		bool missed_any_notes = false;
		while (lane.next_note_index < notes.size())
		{
			const double note_time_seconds = notes[lane.next_note_index].start_seconds;
			if (song_time_seconds <= note_time_seconds + kNoteHitWindowSeconds)
				break;

			lane.next_note_index = note_group_end_index(lane, lane.next_note_index);
			missed_any_notes = true;
		}

		if (missed_any_notes)
			set_lane_stem_target_gain(lane, 0.0f);
	}

	void SongSession::advance_inactive_lane(GameplayLaneState &lane, double song_time_seconds)
	{
		lane.lane_held.fill(false);
		lane.lane_sustain_end_times_.fill(0.0);
		lane.lane_sustain_release_times_.fill(-1.0);
		const std::vector<MidiChartNote> &notes = lane.midi_chart.notes();
		while (lane.next_note_index < notes.size())
		{
			const double note_time_seconds = notes[lane.next_note_index].start_seconds;
			if (song_time_seconds <= note_time_seconds + kNoteHitWindowSeconds)
				break;

			lane.next_note_index = note_group_end_index(lane, lane.next_note_index);
		}
	}

	GameplayMode SongSession::gameplay_mode() const
	{
		return gameplay_mode_;
	}

	size_t SongSession::gameplay_lane_count() const
	{
		return gameplay_lanes_.size();
	}

	int SongSession::active_gameplay_lane_index() const
	{
		return active_lane_index_;
	}

	InstrumentLaneView SongSession::gameplay_lane_view(size_t index) const
	{
		InstrumentLaneView lane_view;
		if (index >= gameplay_lanes_.size())
			return lane_view;

		const GameplayLaneState &lane = gameplay_lanes_[index];
		lane_view.instrument_type = lane.instrument_type;
		lane_view.instrument_label = lane.instrument_label;
		lane_view.is_active = static_cast<int>(index) == active_lane_index_;
		lane_view.is_muted = has_lane_stem(lane) && lane_stem_target_gain(lane) < 0.5f;
		lane_view.has_chart = lane.midi_chart.is_loaded();
		lane_view.lane_held = lane.lane_held;
		const std::uint8_t sustain_mask = active_sustain_lane_mask(lane, song_time_seconds());
		for (size_t fret = 0; fret < lane_view.lane_sustaining.size(); ++fret)
			lane_view.lane_sustaining[fret] = (sustain_mask & static_cast<std::uint8_t>(1u << fret)) != 0;

		const std::vector<MidiChartNote> visible_notes = lane.midi_chart.collect_visible_notes(
			song_time_seconds(),
			kChartLookbehindSeconds,
			kChartLookaheadSeconds);
		lane_view.visible_notes.reserve(visible_notes.size());
		for (const MidiChartNote &note : visible_notes)
		{
			HighwayNoteView note_view;
			note_view.lane = note.lane;
			note_view.start_offset_seconds = static_cast<float>(note.start_seconds - song_time_seconds());
			note_view.length_seconds = static_cast<float>((std::max)(note.end_seconds - note.start_seconds, 0.0));
			lane_view.visible_notes.push_back(note_view);
		}

		const std::vector<MidiChartMeasureLine> visible_measure_lines = lane.midi_chart.collect_visible_measure_lines(
			song_time_seconds(),
			kChartLookbehindSeconds,
			kChartLookaheadSeconds);
		lane_view.visible_measure_lines.reserve(visible_measure_lines.size());
		for (const MidiChartMeasureLine &measure_line : visible_measure_lines)
		{
			HighwayMeasureLineView measure_line_view;
			measure_line_view.offset_seconds = static_cast<float>(measure_line.time_seconds - song_time_seconds());
			measure_line_view.is_measure = measure_line.kind == MidiChartMeasureLine::Kind::Measure;
			measure_line_view.is_strong =
				measure_line.kind == MidiChartMeasureLine::Kind::Measure ||
				measure_line.kind == MidiChartMeasureLine::Kind::Strong;
			lane_view.visible_measure_lines.push_back(measure_line_view);
		}

		return lane_view;
	}

	SongSession::GameplayLaneState *SongSession::active_lane()
	{
		if (active_lane_index_ < 0 || active_lane_index_ >= static_cast<int>(gameplay_lanes_.size()))
			return nullptr;
		return &gameplay_lanes_[static_cast<size_t>(active_lane_index_)];
	}

	const SongSession::GameplayLaneState *SongSession::active_lane() const
	{
		if (active_lane_index_ < 0 || active_lane_index_ >= static_cast<int>(gameplay_lanes_.size()))
			return nullptr;
		return &gameplay_lanes_[static_cast<size_t>(active_lane_index_)];
	}

	double SongSession::adjusted_song_time_seconds() const
	{
		return (std::max)(0.0, transport_.song_time_seconds() - timing_offset_seconds_);
	}
}
