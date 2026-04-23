#include "core/play/SongSession.h"

	#include "libretro_contract/RetroFileSystem.h"

	#include <cmath>
	#include <algorithm>

namespace rhythmreplugged
{
	bool SongSession::load(IRetroFileSystem &file_system, const std::string &song_directory, std::string &error_message)
	{
		unload();
		if (!prototype_player_.load(file_system, song_directory, error_message))
			return false;

		std::string chart_error_message;
		midi_chart_.load(file_system, song_directory, chart_error_message);
		chart_status_message_ = std::move(chart_error_message);

		transport_.configure(prototype_player_.sample_rate());
		audio_mixer_.set_prototype_player(&prototype_player_);
		lane_held_.fill(false);
		lane_sustain_end_times_.fill(0.0);
		lane_sustain_release_times_.fill(-1.0);
		input_generation_ = 0;
		consumed_input_generation_ = 0;
		next_note_index_ = 0;
		prototype_player_.set_stem_target_gain("guitar", 1.0f);
		loaded_.store(true);
		return true;
	}

	void SongSession::unload()
	{
		audio_mixer_.reset();
		transport_.reset();
		midi_chart_.clear();
		chart_status_message_.clear();
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
			prototype_player_.toggle_guitar_mute();
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
					prototype_player_.set_stem_target_gain("guitar", 0.0f);
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
				prototype_player_.set_stem_target_gain("guitar", 1.0f);
			else
				prototype_player_.set_stem_target_gain("guitar", 0.0f);
			return;
		}

		if (resolved_note_hit || next_note_index_ >= midi_chart_.notes().size())
			prototype_player_.set_stem_target_gain("guitar", 1.0f);
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
		player_view.has_guitar = prototype_player_.has_stem("guitar");
		player_view.guitar_muted = prototype_player_.stem_target_gain("guitar") < 0.5f;
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
		return player_view;
	}

	void SongSession::render_interleaved_s16(std::int16_t *output, size_t frame_count)
	{
		audio_mixer_.render_interleaved_s16(output, frame_count);
		transport_.on_audio_rendered(frame_count);
	}

	AudioBatch SongSession::render_fixed_tick_audio(int ticks_per_second)
	{
		const size_t frame_count = transport_.frames_for_next_tick(ticks_per_second);
		AudioBatch batch = audio_mixer_.render(frame_count);
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
			prototype_player_.set_stem_target_gain("guitar", 0.0f);
	}

	double SongSession::adjusted_song_time_seconds() const
	{
		return (std::max)(0.0, transport_.song_time_seconds() - timing_offset_seconds_);
	}
}
