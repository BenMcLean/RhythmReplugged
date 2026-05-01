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

	PrototypePlayerView SongSession::view(const std::string &status_message) const
	{
		PrototypePlayerView player_view;
		rebuild_cached_player_view(player_view, status_message);
		return player_view;
	}

	void SongSession::refresh_frame_snapshot(const std::string &status_message)
	{
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
		lane_view.instrument_type = lane.instrument_type;
		lane_view.instrument_label = lane.instrument_label;
		lane_view.is_active = static_cast<int>(index) == play_state_.active_lane_index;
		lane_view.is_muted = has_lane_stem(index) && lane_stem_target_gain(index) < 0.5f;
		lane_view.has_chart = lane.midi_chart.is_loaded();
		lane_view.lane_held = lane_runtime.lane_held;
		const std::uint8_t sustain_mask = active_sustain_lane_mask(index, song_time_seconds());
		for (size_t fret = 0; fret < lane_view.lane_sustaining.size(); ++fret)
			lane_view.lane_sustaining[fret] = (sustain_mask & static_cast<std::uint8_t>(1u << fret)) != 0;

		append_visible_notes(lane_view, lane, song_time_seconds());
		append_visible_measure_lines(lane_view, lane, song_time_seconds());
		return lane_view;
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

		append_visible_chart_data(player_view, lane, player_view.song_time_seconds);
		append_visible_lyrics(player_view, lane, player_view.song_time_seconds);
	}

	void SongSession::append_visible_notes(InstrumentLaneView &lane_view, const GameplayLaneDefinition &lane_definition, double song_time_seconds) const
	{
		const std::vector<MidiChartNote> visible_notes = lane_definition.midi_chart.collect_visible_notes(
			song_time_seconds,
			kChartLookbehindSeconds,
			kChartLookaheadSeconds);
		lane_view.visible_notes.reserve(visible_notes.size());
		for (const MidiChartNote &note : visible_notes)
		{
			HighwayNoteView note_view;
			note_view.lane = note.lane;
			note_view.start_offset_seconds = static_cast<float>(note.start_seconds - song_time_seconds);
			note_view.length_seconds = static_cast<float>((std::max)(note.end_seconds - note.start_seconds, 0.0));
			lane_view.visible_notes.push_back(note_view);
		}
	}

	void SongSession::append_visible_measure_lines(InstrumentLaneView &lane_view, const GameplayLaneDefinition &lane_definition, double song_time_seconds) const
	{
		const std::vector<MidiChartMeasureLine> visible_measure_lines = lane_definition.midi_chart.collect_visible_measure_lines(
			song_time_seconds,
			kChartLookbehindSeconds,
			kChartLookaheadSeconds);
		lane_view.visible_measure_lines.reserve(visible_measure_lines.size());
		for (const MidiChartMeasureLine &measure_line : visible_measure_lines)
		{
			HighwayMeasureLineView measure_line_view;
			measure_line_view.offset_seconds = static_cast<float>(measure_line.time_seconds - song_time_seconds);
			measure_line_view.is_measure = measure_line.kind == MidiChartMeasureLine::Kind::Measure;
			measure_line_view.is_strong =
				measure_line.kind == MidiChartMeasureLine::Kind::Measure ||
				measure_line.kind == MidiChartMeasureLine::Kind::Strong;
			lane_view.visible_measure_lines.push_back(measure_line_view);
		}
	}

	void SongSession::append_visible_chart_data(PrototypePlayerView &player_view, const GameplayLaneDefinition &lane_definition, double song_time_seconds) const
	{
		const std::vector<MidiChartNote> visible_notes = lane_definition.midi_chart.collect_visible_notes(
			song_time_seconds,
			kChartLookbehindSeconds,
			kChartLookaheadSeconds);
		player_view.visible_chart_notes.reserve(visible_notes.size());
		for (const MidiChartNote &note : visible_notes)
		{
			PrototypePlayerView::ChartNoteView note_view;
			note_view.lane = note.lane;
			note_view.start_offset_seconds = static_cast<float>(note.start_seconds - song_time_seconds);
			note_view.length_seconds = static_cast<float>((std::max)(note.end_seconds - note.start_seconds, 0.0));
			player_view.visible_chart_notes.push_back(note_view);
		}

		const std::vector<MidiChartMeasureLine> visible_measure_lines = lane_definition.midi_chart.collect_visible_measure_lines(
			song_time_seconds,
			kChartLookbehindSeconds,
			kChartLookaheadSeconds);
		player_view.visible_measure_lines.reserve(visible_measure_lines.size());
		for (const MidiChartMeasureLine &measure_line : visible_measure_lines)
		{
			PrototypePlayerView::ChartMeasureLineView measure_line_view;
			measure_line_view.offset_seconds = static_cast<float>(measure_line.time_seconds - song_time_seconds);
			measure_line_view.is_measure = measure_line.kind == MidiChartMeasureLine::Kind::Measure;
			measure_line_view.is_strong =
				measure_line.kind == MidiChartMeasureLine::Kind::Measure ||
				measure_line.kind == MidiChartMeasureLine::Kind::Strong;
			player_view.visible_measure_lines.push_back(measure_line_view);
		}
	}

	void SongSession::append_visible_lyrics(PrototypePlayerView &player_view, const GameplayLaneDefinition &lane_definition, double song_time_seconds) const
	{
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

		player_view.current_lyric_line_index = display_line_index;
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
					player_view.current_lyric_line_index = token.line_index;
			}
		}

		int next_line_index = -1;
		for (const CachedLyricToken &token : lane_definition.lyric_tokens)
		{
			if (token.line_index > player_view.current_lyric_line_index)
			{
				next_line_index = token.line_index;
				break;
			}
		}
		player_view.next_lyric_line_index = next_line_index;

		for (const CachedLyricToken &token : lane_definition.lyric_tokens)
		{
			if (token.line_index != player_view.current_lyric_line_index && token.line_index != next_line_index)
				continue;

			PrototypePlayerView::LyricTokenView lyric_view;
			lyric_view.text = token.text;
			lyric_view.start_offset_seconds = static_cast<float>(token.start_seconds - song_time_seconds);
			lyric_view.end_offset_seconds = static_cast<float>(token.end_seconds - song_time_seconds);
			lyric_view.is_current = lyric_view.start_offset_seconds <= 0.0f && lyric_view.end_offset_seconds > 0.0f;
			lyric_view.is_past = lyric_view.end_offset_seconds <= 0.0f;
			lyric_view.prepend_space = token.prepend_space;
			lyric_view.append_hyphen = token.append_hyphen;
			lyric_view.line_index = token.line_index;
			player_view.visible_lyric_tokens.push_back(std::move(lyric_view));
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
}
