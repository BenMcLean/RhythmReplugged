#include "core/play/SongSession.h"

#include "libretro_contract/RetroFileSystem.h"

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
		loaded_.store(true);
		return true;
	}

	void SongSession::unload()
	{
		audio_mixer_.reset();
		transport_.reset();
		midi_chart_.clear();
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
		if (is_loaded())
			prototype_player_.toggle_guitar_mute();
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
		player_view.loaded_stem_count = prototype_player_.loaded_stem_count();
		player_view.song_time_seconds = song_time_seconds();
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
		return transport_.song_time_seconds();
	}

	double SongSession::song_time_beats(double beats_per_minute) const
	{
		return transport_.song_time_beats(beats_per_minute);
	}
}
