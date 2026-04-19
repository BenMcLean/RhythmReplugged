#include "core/chart/MidiChart.h"

#include "MidiFile.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>

namespace rhythmreplugged
{
	namespace
	{
		struct ParsedDifficulty
		{
			std::string_view name;
			int start_note = 0;
		};

		struct ParsedTrack
		{
			std::string_view midi_track_name;
			std::string_view display_name;
		};

		constexpr ParsedTrack kPreferredTracks[] = {
			{"PART GUITAR", "Guitar"},
			{"T1 GEMS", "Guitar"},
			{"PART BASS", "Bass"},
			{"PART RHYTHM", "Rhythm"},
		};

		constexpr ParsedDifficulty kPreferredDifficulties[] = {
			{"Expert", 96},
			{"Hard", 84},
			{"Medium", 72},
			{"Easy", 60},
		};

		std::string to_upper_copy(std::string_view text)
		{
			std::string result;
			result.reserve(text.size());
			for (char ch : text)
				result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
			return result;
		}

		std::string extract_track_name(const smf::MidiEventList &track)
		{
			for (int index = 0; index < track.size(); ++index)
			{
				const smf::MidiEvent &event = track[index];
				if (event.tick != 0)
					break;
				if (event.isTrackName())
					return event.getMetaContent();
			}

			return {};
		}

		bool read_midi_file(const std::vector<std::uint8_t> &bytes, smf::MidiFile &midi_file)
		{
			std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
			stream.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			stream.seekg(0, std::ios::beg);
			return midi_file.read(stream);
		}

		std::vector<MidiChartNote> parse_difficulty_notes(const smf::MidiEventList &track, int start_note)
		{
			std::vector<MidiChartNote> parsed_notes;
			parsed_notes.reserve(track.size() / 4);

			for (int index = 0; index < track.size(); ++index)
			{
				const smf::MidiEvent &event = track[index];
				if (!event.isNoteOn())
					continue;

				const int note_number = event.getKeyNumber();
				if (note_number < start_note || note_number >= start_note + 5)
					continue;

				MidiChartNote note;
				note.lane = note_number - start_note;
				note.tick = event.tick;
				note.end_tick = event.isLinked() ? event.getLinkedEvent()->tick : event.tick;
				note.start_seconds = event.seconds;
				note.end_seconds = event.isLinked() ? event.getLinkedEvent()->seconds : event.seconds;
				parsed_notes.push_back(note);
			}

			return parsed_notes;
		}
	}

	bool MidiChart::load(const IRetroFileSystem &file_system, const std::string &song_directory, std::string &error_message)
	{
		clear();

		const std::string midi_path = find_case_insensitive_file(file_system, song_directory, "notes.mid");
		if (midi_path.empty())
		{
			error_message.clear();
			return true;
		}

		const auto midi_bytes = file_system.read_binary_file(midi_path);
		if (!midi_bytes.has_value())
		{
			error_message = "Could not read notes.mid.";
			return false;
		}

		smf::MidiFile midi_file;
		if (!read_midi_file(*midi_bytes, midi_file) || !midi_file.status())
		{
			error_message = "Could not parse notes.mid.";
			return false;
		}

		midi_file.doTimeAnalysis();
		midi_file.linkNotePairs();

		duration_seconds_ = midi_file.getFileDurationInSeconds();

		if (midi_file.getTrackCount() > 0)
		{
			const smf::MidiEventList &sync_track = midi_file[0];
			tempo_changes_.reserve(sync_track.size() / 8);
			for (int index = 0; index < sync_track.size(); ++index)
			{
				const smf::MidiEvent &event = sync_track[index];
				if (!event.isTempo())
					continue;

				MidiChartTempoChange tempo_change;
				tempo_change.tick = event.tick;
				tempo_change.time_seconds = event.seconds;
				tempo_change.beats_per_minute = event.getTempoBPM();
				tempo_changes_.push_back(tempo_change);
			}
		}

		for (const ParsedTrack &candidate_track : kPreferredTracks)
		{
			for (int track_index = 1; track_index < midi_file.getTrackCount(); ++track_index)
			{
				const smf::MidiEventList &track = midi_file[track_index];
				if (to_upper_copy(extract_track_name(track)) != candidate_track.midi_track_name)
					continue;

				for (const ParsedDifficulty &candidate_difficulty : kPreferredDifficulties)
				{
					std::vector<MidiChartNote> parsed_notes = parse_difficulty_notes(track, candidate_difficulty.start_note);
					if (parsed_notes.empty())
						continue;

					track_name_ = std::string(candidate_track.display_name);
					difficulty_name_ = candidate_difficulty.name;
					notes_ = std::move(parsed_notes);
					return true;
				}
			}
		}

		error_message = "notes.mid loaded, but no supported 5-fret chart was found yet.";
		return false;
	}

	void MidiChart::clear()
	{
		track_name_.clear();
		difficulty_name_.clear();
		notes_.clear();
		tempo_changes_.clear();
		duration_seconds_ = 0.0;
	}

	bool MidiChart::is_loaded() const
	{
		return !notes_.empty();
	}

	std::string_view MidiChart::track_name() const
	{
		return track_name_;
	}

	std::string_view MidiChart::difficulty_name() const
	{
		return difficulty_name_;
	}

	const std::vector<MidiChartNote> &MidiChart::notes() const
	{
		return notes_;
	}

	const std::vector<MidiChartTempoChange> &MidiChart::tempo_changes() const
	{
		return tempo_changes_;
	}

	double MidiChart::duration_seconds() const
	{
		return duration_seconds_;
	}

	double MidiChart::bpm_at_time(double song_time_seconds) const
	{
		if (tempo_changes_.empty())
			return 120.0;

		double bpm = tempo_changes_.front().beats_per_minute;
		for (const MidiChartTempoChange &tempo_change : tempo_changes_)
		{
			if (tempo_change.time_seconds > song_time_seconds)
				break;
			bpm = tempo_change.beats_per_minute;
		}

		return bpm;
	}

	std::vector<MidiChartNote> MidiChart::collect_visible_notes(double song_time_seconds,
		double lookbehind_seconds,
		double lookahead_seconds) const
	{
		std::vector<MidiChartNote> visible_notes;
		if (notes_.empty())
			return visible_notes;

		const double min_time = song_time_seconds - lookbehind_seconds;
		const double max_time = song_time_seconds + lookahead_seconds;

		auto begin = std::lower_bound(notes_.begin(), notes_.end(), min_time,
			[](const MidiChartNote &note, double time_seconds)
			{
				return note.end_seconds < time_seconds;
			});

		for (auto it = begin; it != notes_.end(); ++it)
		{
			if (it->start_seconds > max_time)
				break;
			visible_notes.push_back(*it);
		}

		return visible_notes;
	}

	std::string MidiChart::find_case_insensitive_file(const IRetroFileSystem &file_system,
		const std::string &directory_path,
		std::string_view file_name)
	{
		const std::string target_name = to_upper_copy(file_name);
		for (const RetroDirectoryEntry &entry : file_system.list_directory(directory_path))
		{
			if (entry.is_directory)
				continue;
			if (to_upper_copy(entry.name) == target_name)
				return entry.path;
		}

		return {};
	}
}
