#include "core/play/SongSession.h"

#include "frontend_contract/RetroFileSystem.h"

#include <algorithm>
#include <cmath>

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

		std::string stem_name_for(InstrumentOption instrument)
		{
			switch (instrument)
			{
			case InstrumentOption::Guitar:
				return "guitar";
			case InstrumentOption::Bass:
				return "bass";
			case InstrumentOption::Rhythm:
				return "rhythm";
			case InstrumentOption::CoopGuitar:
				return "guitar";
			case InstrumentOption::Keys:
				return "keys";
			case InstrumentOption::Drums:
				return "drums";
			}

			return "guitar";
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

		std::string chart_error_message;
		midi_chart_.load(
			file_system,
			song_directory,
			to_midi_chart_difficulty(options.difficulty()),
			to_midi_chart_track_type(options.instrument()),
			chart_error_message);
		chart_status_message_ = std::move(chart_error_message);

		prototype_player_.rewind();
		transport_.configure(prototype_player_.sample_rate());
		audio_mixer_.set_prototype_player(&prototype_player_);
		apply_gameplay_options(options);
		reset_runtime_state();
		set_selected_stem_target_gain(1.0f);
		loaded_.store(true);
		error_message.clear();
		return true;
	}

	void SongSession::unload()
	{
		audio_mixer_.reset();
		transport_.reset();
		midi_chart_.clear();
		chart_status_message_.clear();
		selected_stem_name_ = "guitar";
		selected_instrument_label_ = "Guitar";
		lane_held_.fill(false);
		lane_sustain_end_times_.fill(0.0);
		lane_sustain_release_times_.fill(-1.0);
		input_generation_ = 0;
		consumed_input_generation_ = 0;
		next_note_index_ = 0;
		prototype_player_.unload();
		loaded_.store(false);
	}

	bool SongSession::is_loaded() const
	{
		return loaded_.load();
	}

	void SongSession::toggle_guitar_mute()
	{
		if (is_loaded())
			set_selected_stem_target_gain(selected_stem_target_gain() > 0.5f ? 0.0f : 1.0f);
	}

	void SongSession::update_gameplay_input(const std::array<bool, 5> &lane_held, const std::array<bool, 5> &lane_pressed)
	{
		if (lane_held != lane_held_)
			++input_generation_;
		lane_held_ = lane_held;
		if (!is_loaded() || midi_chart_.notes().empty())
			return;

		const double current_time_seconds = song_time_seconds();
		consume_missed_note_groups(current_time_seconds);
		const std::uint8_t held_mask = lane_mask_from_state(lane_held_);

		const std::uint8_t pressed_mask = lane_mask_from_state(lane_pressed);
		bool resolved_note_hit = false;
		if (pressed_mask != 0 && next_note_index_ < midi_chart_.notes().size())
		{
			const std::vector<MidiChartNote> &notes = midi_chart_.notes();
			const size_t group_end_index = note_group_end_index(next_note_index_);
			const double note_time_seconds = notes[next_note_index_].start_seconds;
			if (std::fabs(note_time_seconds - current_time_seconds) <= kNoteHitWindowSeconds)
			{
				const std::uint8_t expected_mask = note_group_lane_mask(next_note_index_, group_end_index);
				if (held_mask_satisfies_expected(held_mask, expected_mask))
				{
					start_sustains_for_note_group(next_note_index_, group_end_index);
					next_note_index_ = group_end_index;
					consumed_input_generation_ = input_generation_;
					resolved_note_hit = true;
				}
				else
				{
					set_selected_stem_target_gain(0.0f);
				}
			}
		}

		if (!resolved_note_hit && next_note_index_ < midi_chart_.notes().size())
		{
			const std::vector<MidiChartNote> &notes = midi_chart_.notes();
			const size_t group_end_index = note_group_end_index(next_note_index_);
			const double note_time_seconds = notes[next_note_index_].start_seconds;
			const std::uint8_t expected_mask = note_group_lane_mask(next_note_index_, group_end_index);
			if (input_generation_ > consumed_input_generation_ &&
				std::fabs(note_time_seconds - current_time_seconds) <= kNoteHitWindowSeconds &&
				held_mask_satisfies_expected(held_mask, expected_mask))
			{
				start_sustains_for_note_group(next_note_index_, group_end_index);
				next_note_index_ = group_end_index;
				consumed_input_generation_ = input_generation_;
				resolved_note_hit = true;
			}
		}

		refresh_active_sustains(current_time_seconds, held_mask);
		const std::uint8_t sustain_mask = active_sustain_lane_mask(current_time_seconds);
		if (sustain_mask != 0)
		{
			const std::uint8_t imminent_note_mask = imminent_note_lane_mask(current_time_seconds);
			const std::uint8_t required_sustain_mask = static_cast<std::uint8_t>(sustain_mask & ~imminent_note_mask);
			if (required_sustain_mask == 0 || held_mask_satisfies_expected(held_mask, required_sustain_mask))
				set_selected_stem_target_gain(1.0f);
			else
				set_selected_stem_target_gain(0.0f);
			return;
		}

		if (resolved_note_hit || next_note_index_ >= midi_chart_.notes().size())
			set_selected_stem_target_gain(1.0f);
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
		player_view.has_playable_stem = has_selected_stem();
		player_view.playable_stem_muted = has_selected_stem() && selected_stem_target_gain() < 0.5f;
		player_view.playable_stem_label = selected_instrument_label_;
		player_view.lane_held = lane_held_;
		player_view.loaded_stem_count = prototype_player_.loaded_stem_count();
		player_view.song_time_seconds = song_time_seconds();
		const std::uint8_t sustain_mask = active_sustain_lane_mask(player_view.song_time_seconds);
		for (size_t lane = 0; lane < player_view.lane_sustaining.size(); ++lane)
			player_view.lane_sustaining[lane] = (sustain_mask & static_cast<std::uint8_t>(1u << lane)) != 0;
		player_view.has_chart = midi_chart_.is_loaded();
		player_view.chart_track_name = std::string(midi_chart_.track_name());
		player_view.chart_difficulty_name = std::string(midi_chart_.difficulty_name());
		player_view.chart_beats_per_minute = midi_chart_.bpm_at_time(player_view.song_time_seconds);

		const std::vector<MidiChartNote> visible_notes = midi_chart_.collect_visible_notes(
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

		const std::vector<MidiChartMeasureLine> visible_measure_lines = midi_chart_.collect_visible_measure_lines(
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

		const std::vector<MidiChartTextEvent> &lyrics = midi_chart_.lyrics();
		const std::vector<LyricPhraseRange> lyric_phrase_ranges = collect_lyric_phrase_ranges(midi_chart_);
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

	void SongSession::apply_gameplay_options(const GameplayOptions &options)
	{
		selected_stem_name_ = stem_name_for(options.instrument());
		selected_instrument_label_ = instrument_label_for(options.instrument());
		if (!prototype_player_.has_stem(selected_stem_name_) && prototype_player_.has_stem("guitar"))
			selected_stem_name_ = "guitar";
	}

	void SongSession::reset_runtime_state()
	{
		lane_held_.fill(false);
		lane_sustain_end_times_.fill(0.0);
		lane_sustain_release_times_.fill(-1.0);
		input_generation_ = 0;
		consumed_input_generation_ = 0;
		next_note_index_ = 0;
	}

	void SongSession::set_selected_stem_target_gain(float gain)
	{
		prototype_player_.set_stem_target_gain(selected_stem_name_, gain);
	}

	float SongSession::selected_stem_target_gain() const
	{
		return prototype_player_.stem_target_gain(selected_stem_name_);
	}

	bool SongSession::has_selected_stem() const
	{
		return prototype_player_.has_stem(selected_stem_name_);
	}

	size_t SongSession::note_group_end_index(size_t start_index) const
	{
		const std::vector<MidiChartNote> &notes = midi_chart_.notes();
		if (start_index >= notes.size())
			return notes.size();

		size_t end_index = start_index + 1;
		const int group_tick = notes[start_index].tick;
		while (end_index < notes.size() && notes[end_index].tick == group_tick)
			++end_index;

		return end_index;
	}

	std::uint8_t SongSession::note_group_lane_mask(size_t start_index, size_t end_index) const
	{
		std::uint8_t mask = 0;
		const std::vector<MidiChartNote> &notes = midi_chart_.notes();
		for (size_t index = start_index; index < end_index && index < notes.size(); ++index)
		{
			const int lane = notes[index].lane;
			if (lane >= 0 && lane < 5)
				mask |= static_cast<std::uint8_t>(1u << lane);
		}

		return mask;
	}

	std::uint8_t SongSession::imminent_note_lane_mask(double song_time_seconds) const
	{
		const std::vector<MidiChartNote> &notes = midi_chart_.notes();
		if (next_note_index_ >= notes.size())
			return 0;

		const double note_time_seconds = notes[next_note_index_].start_seconds;
		if (std::fabs(note_time_seconds - song_time_seconds) > kNoteHitWindowSeconds)
			return 0;

		const size_t group_end_index = note_group_end_index(next_note_index_);
		return note_group_lane_mask(next_note_index_, group_end_index);
	}

	void SongSession::refresh_active_sustains(double song_time_seconds, std::uint8_t held_mask)
	{
		for (size_t lane = 0; lane < lane_sustain_end_times_.size(); ++lane)
		{
			const double sustain_end_time = lane_sustain_end_times_[lane];
			if (sustain_end_time - song_time_seconds <= -kSustainDropLeniencySeconds)
			{
				lane_sustain_end_times_[lane] = 0.0;
				lane_sustain_release_times_[lane] = -1.0;
				continue;
			}

			if (sustain_end_time <= 0.0)
			{
				lane_sustain_release_times_[lane] = -1.0;
				continue;
			}

			const std::uint8_t lane_bit = static_cast<std::uint8_t>(1u << lane);
			if ((held_mask & lane_bit) != 0)
			{
				lane_sustain_release_times_[lane] = -1.0;
				continue;
			}

			double &release_time = lane_sustain_release_times_[lane];
			if (release_time < 0.0)
			{
				release_time = song_time_seconds;
				continue;
			}

			if (song_time_seconds - release_time > kSustainDropLeniencySeconds)
			{
				lane_sustain_end_times_[lane] = 0.0;
				release_time = -1.0;
			}
		}
	}

	std::uint8_t SongSession::active_sustain_lane_mask(double song_time_seconds) const
	{
		std::uint8_t mask = 0;
		for (size_t lane = 0; lane < lane_sustain_end_times_.size(); ++lane)
		{
			const double sustain_end_time = lane_sustain_end_times_[lane];
			if (sustain_end_time - song_time_seconds <= -kSustainDropLeniencySeconds)
				continue;

			const double release_time = lane_sustain_release_times_[lane];
			if (release_time >= 0.0 && song_time_seconds - release_time > kSustainDropLeniencySeconds)
				continue;

			if (sustain_end_time > 0.0)
				mask |= static_cast<std::uint8_t>(1u << lane);
		}	

		return mask;
	}

	void SongSession::start_sustains_for_note_group(size_t start_index, size_t end_index)
	{
		const std::vector<MidiChartNote> &notes = midi_chart_.notes();
		for (size_t index = start_index; index < end_index && index < notes.size(); ++index)
		{
			const MidiChartNote &note = notes[index];
			if (note.lane < 0 || note.lane >= 5)
				continue;

			const double sustain_duration_seconds = note.end_seconds - note.start_seconds;
			if (sustain_duration_seconds < kSustainMinimumSeconds)
				continue;

			const size_t lane = static_cast<size_t>(note.lane);
			lane_sustain_end_times_[lane] = (std::max)(lane_sustain_end_times_[lane], note.end_seconds);
			lane_sustain_release_times_[lane] = -1.0;
		}
	}

	void SongSession::consume_missed_note_groups(double song_time_seconds)
	{
		const std::vector<MidiChartNote> &notes = midi_chart_.notes();
		bool missed_any_notes = false;
		while (next_note_index_ < notes.size())
		{
			const double note_time_seconds = notes[next_note_index_].start_seconds;
			if (song_time_seconds <= note_time_seconds + kNoteHitWindowSeconds)
				break;

			next_note_index_ = note_group_end_index(next_note_index_);
			missed_any_notes = true;
		}

		if (missed_any_notes)
			set_selected_stem_target_gain(0.0f);
	}

	double SongSession::adjusted_song_time_seconds() const
	{
		return (std::max)(0.0, transport_.song_time_seconds() - timing_offset_seconds_);
	}
}
