#include "core/chart/MidiChart.h"

#include "core/songs/SongIni.h"

#include "MidiFile.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace rhythmreplugged::core
{
	namespace
	{
		struct ParsedDifficultyPreference
		{
			std::string_view name;
			MidiChartDifficulty difficulty;
		};

		struct InstrumentTrackPreference
		{
			MidiChartTrackType type;
			std::string_view display_name;
		};

		struct DifficultyNoteRange
		{
			MidiChartDifficulty difficulty;
			int start_note;
			int lane_count;
		};

		constexpr ParsedDifficultyPreference kPreferredDifficulties[] = {
			{"Medium", MidiChartDifficulty::Medium},
			{"Hard", MidiChartDifficulty::Hard},
			{"Expert", MidiChartDifficulty::Expert},
			{"Easy", MidiChartDifficulty::Easy},
		};

		constexpr InstrumentTrackPreference kPreviewTrackPreferences[] = {
			{MidiChartTrackType::FiveFretGuitar, "Guitar"},
			{MidiChartTrackType::FiveFretBass, "Bass"},
			{MidiChartTrackType::FiveFretRhythm, "Rhythm"},
			{MidiChartTrackType::FiveFretCoop, "Co-op Guitar"},
			{MidiChartTrackType::FiveFretKeys, "Keys"},
		};

		bool is_supported_preview_track_type(MidiChartTrackType type)
		{
			for (const InstrumentTrackPreference &track_preference : kPreviewTrackPreferences)
			{
				if (track_preference.type == type)
					return true;
			}

			return false;
		}

		std::string_view display_name_for_preview_track_type(MidiChartTrackType type)
		{
			for (const InstrumentTrackPreference &track_preference : kPreviewTrackPreferences)
			{
				if (track_preference.type == type)
					return track_preference.display_name;
			}

			return {};
		}

		constexpr DifficultyNoteRange kFiveFretNoteRanges[] = {
			{MidiChartDifficulty::Easy, 60, 5},
			{MidiChartDifficulty::Medium, 72, 5},
			{MidiChartDifficulty::Hard, 84, 5},
			{MidiChartDifficulty::Expert, 96, 5},
		};

		constexpr DifficultyNoteRange kSixFretNoteRanges[] = {
			{MidiChartDifficulty::Easy, 58, 7},
			{MidiChartDifficulty::Medium, 70, 7},
			{MidiChartDifficulty::Hard, 82, 7},
			{MidiChartDifficulty::Expert, 94, 7},
		};

		constexpr DifficultyNoteRange kDrumsNoteRanges[] = {
			{MidiChartDifficulty::Easy, 60, 6},
			{MidiChartDifficulty::Medium, 72, 6},
			{MidiChartDifficulty::Hard, 84, 6},
			{MidiChartDifficulty::Expert, 96, 6},
		};

		constexpr DifficultyNoteRange kEliteDrumsNoteRanges[] = {
			{MidiChartDifficulty::Easy, 2, 10},
			{MidiChartDifficulty::Medium, 26, 10},
			{MidiChartDifficulty::Hard, 50, 10},
			{MidiChartDifficulty::Expert, 74, 10},
		};

		struct EliteDrumPadOffset
		{
			int pad;
			int offset;
		};

		constexpr EliteDrumPadOffset kEliteDrumPadOffsets[] = {
			{0, -2},
			{1, 0},
			{2, 1},
			{3, 2},
			{4, 3},
			{5, 4},
			{6, 5},
			{7, 6},
			{8, 7},
			{9, 8},
		};

		constexpr DifficultyNoteRange kProGuitarNoteRanges[] = {
			{MidiChartDifficulty::Easy, 24, 24},
			{MidiChartDifficulty::Medium, 48, 24},
			{MidiChartDifficulty::Hard, 72, 24},
			{MidiChartDifficulty::Expert, 96, 24},
		};

		constexpr std::array<std::pair<std::string_view, MidiChartTrackType>, 28> kTrackTypeLookup = {{
			{"BEAT", MidiChartTrackType::Beat},
			{"EVENTS", MidiChartTrackType::Events},
			{"VENUE", MidiChartTrackType::Venue},
			{"PART GUITAR", MidiChartTrackType::FiveFretGuitar},
			{"T1 GEMS", MidiChartTrackType::FiveFretGuitar},
			{"PART GUITAR COOP", MidiChartTrackType::FiveFretCoop},
			{"PART BASS", MidiChartTrackType::FiveFretBass},
			{"PART RHYTHM", MidiChartTrackType::FiveFretRhythm},
			{"PART KEYS", MidiChartTrackType::FiveFretKeys},
			{"PART GUITAR GHL", MidiChartTrackType::SixFretGuitar},
			{"PART GUITAR COOP GHL", MidiChartTrackType::SixFretCoop},
			{"PART BASS GHL", MidiChartTrackType::SixFretBass},
			{"PART RHYTHM GHL", MidiChartTrackType::SixFretRhythm},
			{"PART DRUMS", MidiChartTrackType::Drums},
			{"PART DRUM", MidiChartTrackType::Drums},
			{"PART ELITE_DRUMS", MidiChartTrackType::EliteDrums},
			{"PART VOCALS", MidiChartTrackType::Vocals},
			{"HARM1", MidiChartTrackType::Harmony1},
			{"HARM2", MidiChartTrackType::Harmony2},
			{"HARM3", MidiChartTrackType::Harmony3},
			{"PART HARM1", MidiChartTrackType::Harmony1},
			{"PART HARM2", MidiChartTrackType::Harmony2},
			{"PART HARM3", MidiChartTrackType::Harmony3},
			{"PART REAL_GUITAR", MidiChartTrackType::ProGuitar17},
			{"PART REAL_GUITAR_22", MidiChartTrackType::ProGuitar22},
			{"PART REAL_BASS", MidiChartTrackType::ProBass17},
			{"PART REAL_BASS_22", MidiChartTrackType::ProBass22},
			{"PART REAL_KEYS_X", MidiChartTrackType::ProKeysExpert},
		}};

		std::string to_upper_copy(std::string_view text)
		{
			std::string result;
			result.reserve(text.size());
			for (char ch : text)
				result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
			return result;
		}

		std::string to_lower_copy(std::string_view text)
		{
			std::string result;
			result.reserve(text.size());
			for (char ch : text)
				result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
			return result;
		}

		bool starts_with_ignore_case(std::string_view text, std::string_view prefix)
		{
			if (text.size() < prefix.size())
				return false;
			return to_lower_copy(text.substr(0, prefix.size())) == to_lower_copy(prefix);
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

		int denominator_from_power(int power_of_two)
		{
			if (power_of_two < 0)
				return 4;
			if (power_of_two > 8)
				power_of_two = 8;
			return 1 << power_of_two;
		}

		MidiChartTrackType classify_track_type(std::string_view track_name)
		{
			const std::string upper_name = to_upper_copy(track_name);
			for (const auto &[candidate_name, candidate_type] : kTrackTypeLookup)
			{
				if (upper_name == candidate_name)
					return candidate_type;
			}

			if (upper_name == "PART REAL_KEYS_H")
				return MidiChartTrackType::ProKeysHard;
			if (upper_name == "PART REAL_KEYS_M")
				return MidiChartTrackType::ProKeysMedium;
			if (upper_name == "PART REAL_KEYS_E")
				return MidiChartTrackType::ProKeysEasy;
			return MidiChartTrackType::Unknown;
		}

		bool is_five_fret_track(MidiChartTrackType type)
		{
			switch (type)
			{
			case MidiChartTrackType::FiveFretGuitar:
			case MidiChartTrackType::FiveFretCoop:
			case MidiChartTrackType::FiveFretBass:
			case MidiChartTrackType::FiveFretRhythm:
			case MidiChartTrackType::FiveFretKeys:
				return true;
			default:
				return false;
			}
		}

		bool is_six_fret_track(MidiChartTrackType type)
		{
			switch (type)
			{
			case MidiChartTrackType::SixFretGuitar:
			case MidiChartTrackType::SixFretCoop:
			case MidiChartTrackType::SixFretBass:
			case MidiChartTrackType::SixFretRhythm:
				return true;
			default:
				return false;
			}
		}

		bool is_pro_guitar_track(MidiChartTrackType type)
		{
			switch (type)
			{
			case MidiChartTrackType::ProGuitar17:
			case MidiChartTrackType::ProGuitar22:
			case MidiChartTrackType::ProBass17:
			case MidiChartTrackType::ProBass22:
				return true;
			default:
				return false;
			}
		}

		bool is_pro_keys_track(MidiChartTrackType type)
		{
			switch (type)
			{
			case MidiChartTrackType::ProKeysEasy:
			case MidiChartTrackType::ProKeysMedium:
			case MidiChartTrackType::ProKeysHard:
			case MidiChartTrackType::ProKeysExpert:
				return true;
			default:
				return false;
			}
		}

		MidiChartDifficulty pro_keys_track_difficulty(MidiChartTrackType type)
		{
			switch (type)
			{
			case MidiChartTrackType::ProKeysEasy:
				return MidiChartDifficulty::Easy;
			case MidiChartTrackType::ProKeysMedium:
				return MidiChartDifficulty::Medium;
			case MidiChartTrackType::ProKeysHard:
				return MidiChartDifficulty::Hard;
			case MidiChartTrackType::ProKeysExpert:
				return MidiChartDifficulty::Expert;
			default:
				return MidiChartDifficulty::None;
			}
		}

		std::string difficulty_display_name(MidiChartDifficulty difficulty)
		{
			switch (difficulty)
			{
			case MidiChartDifficulty::Easy:
				return "Easy";
			case MidiChartDifficulty::Medium:
				return "Medium";
			case MidiChartDifficulty::Hard:
				return "Hard";
			case MidiChartDifficulty::Expert:
				return "Expert";
			case MidiChartDifficulty::All:
				return "All";
			default:
				return {};
			}
		}

		bool match_note_range(int midi_note,
			const DifficultyNoteRange *ranges,
			size_t range_count,
			MidiChartDifficulty &difficulty,
			int &raw_value)
		{
			for (size_t index = 0; index < range_count; ++index)
			{
				const DifficultyNoteRange &range = ranges[index];
				if (midi_note >= range.start_note && midi_note < range.start_note + range.lane_count)
				{
					difficulty = range.difficulty;
					raw_value = midi_note - range.start_note;
					return true;
				}
			}

			return false;
		}

		bool match_elite_drums_note(int midi_note, MidiChartDifficulty &difficulty, int &pad, bool &double_kick)
		{
			for (const DifficultyNoteRange &range : kEliteDrumsNoteRanges)
			{
				for (const EliteDrumPadOffset &pad_offset : kEliteDrumPadOffsets)
				{
					if (midi_note == range.start_note + pad_offset.offset)
					{
						difficulty = range.difficulty;
						pad = pad_offset.pad;
						double_kick = false;
						return true;
					}
				}

				if (midi_note == range.start_note - 1)
				{
					difficulty = range.difficulty;
					pad = 1;
					double_kick = true;
					return true;
				}
			}

			return false;
		}

		MidiChartTextEventType classify_text_event_type(const smf::MidiEvent &event, std::string_view text)
		{
			const std::string lowered = to_lower_copy(text);

			if (event.isTrackName())
				return MidiChartTextEventType::TrackName;
			if (event.isLyricText())
				return MidiChartTextEventType::Lyric;
			if (event.isMarkerText())
				return MidiChartTextEventType::Marker;
			if (lowered == "music_start")
				return MidiChartTextEventType::MusicStart;
			if (lowered == "music_end")
				return MidiChartTextEventType::MusicEnd;
			if (lowered == "coda")
				return MidiChartTextEventType::Coda;
			if (lowered == "coda_end")
				return MidiChartTextEventType::CodaEnd;
			if (starts_with_ignore_case(text, "[section ") || starts_with_ignore_case(text, "section "))
				return MidiChartTextEventType::Section;
			if (starts_with_ignore_case(text, "[crowd") || starts_with_ignore_case(text, "crowd"))
				return MidiChartTextEventType::Crowd;
			if (lowered == "enhanced_opens" ||
				lowered == "[enhanced_opens]" ||
				lowered == "enable_chart_dynamics" ||
				lowered == "[enable_chart_dynamics]" ||
				lowered == "strict_hat_pedal_state" ||
				lowered == "[strict_hat_pedal_state]")
			{
				return MidiChartTextEventType::ParserDirective;
			}

			return MidiChartTextEventType::Generic;
		}

		bool is_text_event(const smf::MidiEvent &event)
		{
			return event.isText() || event.isLyricText() || event.isMarkerText() || event.isTrackName();
		}

		void collect_text_event(const smf::MidiEvent &event,
			MidiChartTrack &track,
			std::vector<MidiChartTextEvent> &global_events,
			std::vector<MidiChartTextEvent> &sections,
			std::vector<MidiChartTextEvent> &lyrics)
		{
			if (!is_text_event(event))
				return;

			MidiChartTextEvent text_event;
			text_event.type = classify_text_event_type(event, event.getMetaContent());
			text_event.tick = event.tick;
			text_event.time_seconds = event.seconds;
			text_event.text = event.getMetaContent();
			track.text_events.push_back(text_event);

			if (text_event.type == MidiChartTextEventType::ParserDirective)
			{
				const std::string lowered = to_lower_copy(text_event.text);
				if (lowered == "enhanced_opens" || lowered == "[enhanced_opens]")
					track.uses_enhanced_opens = true;
				if (lowered == "enable_chart_dynamics" || lowered == "[enable_chart_dynamics]")
					track.uses_chart_dynamics = true;
				if (lowered == "strict_hat_pedal_state" || lowered == "[strict_hat_pedal_state]")
					track.uses_strict_hat_pedal_state = true;
			}

			if (track.type == MidiChartTrackType::Events)
			{
				global_events.push_back(text_event);
				if (text_event.type == MidiChartTextEventType::Section)
					sections.push_back(text_event);
			}

			if (text_event.type == MidiChartTextEventType::Lyric ||
				track.type == MidiChartTrackType::Vocals ||
				track.type == MidiChartTrackType::Harmony1 ||
				track.type == MidiChartTrackType::Harmony2 ||
				track.type == MidiChartTrackType::Harmony3)
			{
				if (text_event.type == MidiChartTextEventType::Lyric || !text_event.text.empty())
					lyrics.push_back(text_event);
			}
		}

		void push_raw_note_event(const smf::MidiEvent &event, MidiChartTrack &track)
		{
			if (!event.isNoteOn())
				return;

			MidiChartRawNoteEvent raw_event;
			raw_event.tick = event.tick;
			raw_event.end_tick = event.isLinked() ? event.getLinkedEvent()->tick : event.tick;
			raw_event.start_seconds = event.seconds;
			raw_event.end_seconds = event.isLinked() ? event.getLinkedEvent()->seconds : event.seconds;
			raw_event.midi_note = event.getKeyNumber();
			raw_event.velocity = event.getVelocity();
			raw_event.channel = event.getChannelNibble();
			track.raw_note_events.push_back(raw_event);
		}

		void push_phrase(MidiChartTrack &track,
			MidiChartPhraseType type,
			MidiChartDifficulty difficulty,
			const smf::MidiEvent &event)
		{
			MidiChartPhrase phrase;
			phrase.type = type;
			phrase.difficulty = difficulty;
			phrase.tick = event.tick;
			phrase.end_tick = event.isLinked() ? event.getLinkedEvent()->tick : event.tick;
			phrase.start_seconds = event.seconds;
			phrase.end_seconds = event.isLinked() ? event.getLinkedEvent()->seconds : event.seconds;
			phrase.raw_midi_note = event.getKeyNumber();
			track.phrases.push_back(phrase);
		}

		bool parse_common_phrase(const smf::MidiEvent &event,
			MidiChartTrack &track,
			int star_power_note_override,
			int solo_note,
			bool include_versus,
			bool include_lanes)
		{
			const int note = event.getKeyNumber();
			if (note == star_power_note_override)
			{
				push_phrase(track, MidiChartPhraseType::StarPower, MidiChartDifficulty::All, event);
				return true;
			}

			if (solo_note >= 0 && note == solo_note)
			{
				push_phrase(track, MidiChartPhraseType::Solo, MidiChartDifficulty::All, event);
				return true;
			}

			if (include_versus && note == 105)
			{
				push_phrase(track, MidiChartPhraseType::VersusPlayer1, MidiChartDifficulty::All, event);
				return true;
			}

			if (include_versus && note == 106)
			{
				push_phrase(track, MidiChartPhraseType::VersusPlayer2, MidiChartDifficulty::All, event);
				return true;
			}

			if (include_lanes && note == 126)
			{
				push_phrase(track, MidiChartPhraseType::TremoloLane, MidiChartDifficulty::All, event);
				return true;
			}

			if (include_lanes && note == 127)
			{
				push_phrase(track, MidiChartPhraseType::TrillLane, MidiChartDifficulty::All, event);
				return true;
			}

			return false;
		}

		void push_parsed_note(MidiChartTrack &track, MidiChartParsedNote note)
		{
			track.parsed_notes.push_back(std::move(note));
		}

		bool parse_five_fret_note(const smf::MidiEvent &event, MidiChartTrack &track, int star_power_note_override)
		{
			if (parse_common_phrase(event, track, star_power_note_override, 103, true, true))
				return true;

			if (event.getKeyNumber() == 104)
			{
				push_phrase(track, MidiChartPhraseType::TapModifier, MidiChartDifficulty::All, event);
				return true;
			}

			MidiChartDifficulty difficulty = MidiChartDifficulty::None;
			int raw_value = -1;
			if (match_note_range(event.getKeyNumber(), kFiveFretNoteRanges, std::size(kFiveFretNoteRanges), difficulty, raw_value))
			{
				MidiChartParsedNote note;
				note.category = MidiChartNoteCategory::FiveFret;
				note.difficulty = difficulty;
				note.tick = event.tick;
				note.end_tick = event.isLinked() ? event.getLinkedEvent()->tick : event.tick;
				note.start_seconds = event.seconds;
				note.end_seconds = event.isLinked() ? event.getLinkedEvent()->seconds : event.seconds;
				note.raw_midi_note = event.getKeyNumber();
				note.raw_value = raw_value + 1;
				note.lane = raw_value + 1;
				note.velocity = event.getVelocity();
				note.channel = event.getChannelNibble();
				push_parsed_note(track, note);
				return true;
			}

			for (const DifficultyNoteRange &range : kFiveFretNoteRanges)
			{
				if (event.getKeyNumber() == range.start_note + 5)
				{
					push_phrase(track, MidiChartPhraseType::ForcedHopoModifier, range.difficulty, event);
					return true;
				}
				if (event.getKeyNumber() == range.start_note + 6)
				{
					push_phrase(track, MidiChartPhraseType::ForcedStrumModifier, range.difficulty, event);
					return true;
				}
			}

			if (track.uses_enhanced_opens)
			{
				for (const DifficultyNoteRange &range : kFiveFretNoteRanges)
				{
					if (event.getKeyNumber() == range.start_note - 1)
					{
						MidiChartParsedNote note;
						note.category = MidiChartNoteCategory::FiveFret;
						note.difficulty = range.difficulty;
						note.tick = event.tick;
						note.end_tick = event.isLinked() ? event.getLinkedEvent()->tick : event.tick;
						note.start_seconds = event.seconds;
						note.end_seconds = event.isLinked() ? event.getLinkedEvent()->seconds : event.seconds;
						note.raw_midi_note = event.getKeyNumber();
						note.raw_value = 0;
						note.lane = 0;
						note.velocity = event.getVelocity();
						note.channel = event.getChannelNibble();
						push_parsed_note(track, note);
						return true;
					}
				}
			}

			if (event.getKeyNumber() >= 40 && event.getKeyNumber() <= 59)
			{
				MidiChartParsedNote note;
				note.category = MidiChartNoteCategory::Animation;
				note.tick = event.tick;
				note.end_tick = event.isLinked() ? event.getLinkedEvent()->tick : event.tick;
				note.start_seconds = event.seconds;
				note.end_seconds = event.isLinked() ? event.getLinkedEvent()->seconds : event.seconds;
				note.raw_midi_note = event.getKeyNumber();
				note.raw_value = event.getKeyNumber() - 40;
				push_parsed_note(track, note);
				return true;
			}

			return false;
		}

		bool parse_six_fret_note(const smf::MidiEvent &event, MidiChartTrack &track, int star_power_note_override)
		{
			if (parse_common_phrase(event, track, star_power_note_override, 103, false, false))
				return true;

			if (event.getKeyNumber() == 104)
			{
				push_phrase(track, MidiChartPhraseType::TapModifier, MidiChartDifficulty::All, event);
				return true;
			}

			for (const DifficultyNoteRange &range : kSixFretNoteRanges)
			{
				if (event.getKeyNumber() == range.start_note + 7)
				{
					push_phrase(track, MidiChartPhraseType::ForcedHopoModifier, range.difficulty, event);
					return true;
				}
				if (event.getKeyNumber() == range.start_note + 8)
				{
					push_phrase(track, MidiChartPhraseType::ForcedStrumModifier, range.difficulty, event);
					return true;
				}
			}

			MidiChartDifficulty difficulty = MidiChartDifficulty::None;
			int raw_value = -1;
			if (!match_note_range(event.getKeyNumber(), kSixFretNoteRanges, std::size(kSixFretNoteRanges), difficulty, raw_value))
				return false;

			MidiChartParsedNote note;
			note.category = MidiChartNoteCategory::SixFret;
			note.difficulty = difficulty;
			note.tick = event.tick;
			note.end_tick = event.isLinked() ? event.getLinkedEvent()->tick : event.tick;
			note.start_seconds = event.seconds;
			note.end_seconds = event.isLinked() ? event.getLinkedEvent()->seconds : event.seconds;
			note.raw_midi_note = event.getKeyNumber();
			note.raw_value = raw_value;
			note.lane = raw_value;
			note.velocity = event.getVelocity();
			note.channel = event.getChannelNibble();
			push_parsed_note(track, note);
			return true;
		}

		bool parse_drums_note(const smf::MidiEvent &event, MidiChartTrack &track, int star_power_note_override)
		{
			const int note_number = event.getKeyNumber();
			if (parse_common_phrase(event, track, star_power_note_override, 103, true, true))
				return true;

			if (note_number >= 120 && note_number <= 124)
			{
				push_phrase(track, MidiChartPhraseType::DrumFill, MidiChartDifficulty::All, event);
				return true;
			}

			if (note_number >= 24 && note_number <= 51)
			{
				MidiChartParsedNote note;
				note.category = MidiChartNoteCategory::Animation;
				note.tick = event.tick;
				note.end_tick = event.isLinked() ? event.getLinkedEvent()->tick : event.tick;
				note.start_seconds = event.seconds;
				note.end_seconds = event.isLinked() ? event.getLinkedEvent()->seconds : event.seconds;
				note.raw_midi_note = note_number;
				note.raw_value = note_number - 24;
				push_parsed_note(track, note);
				return true;
			}

			if (note_number >= 110 && note_number <= 112)
			{
				if (note_number == 110)
					push_phrase(track, MidiChartPhraseType::DrumsCymbalYellowModifier, MidiChartDifficulty::All, event);
				else if (note_number == 111)
					push_phrase(track, MidiChartPhraseType::DrumsCymbalBlueModifier, MidiChartDifficulty::All, event);
				else
					push_phrase(track, MidiChartPhraseType::DrumsCymbalOrangeModifier, MidiChartDifficulty::All, event);
				return true;
			}

			MidiChartDifficulty difficulty = MidiChartDifficulty::None;
			int raw_value = -1;
			if (!match_note_range(note_number, kDrumsNoteRanges, std::size(kDrumsNoteRanges), difficulty, raw_value))
				return false;

			MidiChartParsedNote note;
			note.category = MidiChartNoteCategory::Drums;
			note.difficulty = difficulty;
			note.tick = event.tick;
			note.end_tick = event.isLinked() ? event.getLinkedEvent()->tick : event.tick;
			note.start_seconds = event.seconds;
			note.end_seconds = event.isLinked() ? event.getLinkedEvent()->seconds : event.seconds;
			note.raw_midi_note = note_number;
			note.raw_value = raw_value;
			note.lane = raw_value;
			note.velocity = event.getVelocity();
			note.channel = event.getChannelNibble();
			if (track.uses_chart_dynamics)
			{
				if (note.velocity == 127)
					note.flags |= MidiChartNoteFlagProDrumsAccent;
				else if (note.velocity == 1)
					note.flags |= MidiChartNoteFlagProDrumsGhost;
			}
			if (raw_value == 0 && note_number > 0 && (note_number - 1 == 95 || note_number - 1 == 71 || note_number - 1 == 83))
				note.flags |= MidiChartNoteFlagInstrumentPlus;
			push_parsed_note(track, note);
			return true;
		}

		bool parse_vocals_note(const smf::MidiEvent &event, MidiChartTrack &track)
		{
			const int note_number = event.getKeyNumber();
			if (note_number == 0)
			{
				push_phrase(track, MidiChartPhraseType::VocalsRangeShift, MidiChartDifficulty::All, event);
				return true;
			}

			if (note_number == 1)
			{
				push_phrase(track, MidiChartPhraseType::VocalsLyricShift, MidiChartDifficulty::All, event);
				return true;
			}

			if (note_number == 105)
			{
				push_phrase(track, MidiChartPhraseType::VocalsScoringPhrase, MidiChartDifficulty::All, event);
				push_phrase(track, MidiChartPhraseType::VocalsStaticPhrase, MidiChartDifficulty::All, event);
				return true;
			}

			if (note_number == 106)
			{
				push_phrase(track, MidiChartPhraseType::VersusPlayer2, MidiChartDifficulty::All, event);
				push_phrase(track, MidiChartPhraseType::VocalsScoringPhrase, MidiChartDifficulty::All, event);
				push_phrase(track, MidiChartPhraseType::VocalsStaticPhrase, MidiChartDifficulty::All, event);
				return true;
			}

			if (note_number == 96 || note_number == 97)
			{
				MidiChartParsedNote note;
				note.category = MidiChartNoteCategory::Vocals;
				note.tick = event.tick;
				note.end_tick = note_number == 96 ? event.tick : (event.isLinked() ? event.getLinkedEvent()->tick : event.tick);
				note.start_seconds = event.seconds;
				note.end_seconds = note_number == 96 ? event.seconds : (event.isLinked() ? event.getLinkedEvent()->seconds : event.seconds);
				note.raw_midi_note = note_number;
				note.raw_value = note_number;
				note.flags |= MidiChartNoteFlagVocalsPercussion;
				push_parsed_note(track, note);
				return true;
			}

			if (note_number < 36 || note_number > 84)
				return false;

			MidiChartParsedNote note;
			note.category = MidiChartNoteCategory::Vocals;
			note.tick = event.tick;
			note.end_tick = event.isLinked() ? event.getLinkedEvent()->tick : event.tick;
			note.start_seconds = event.seconds;
			note.end_seconds = event.isLinked() ? event.getLinkedEvent()->seconds : event.seconds;
			note.raw_midi_note = note_number;
			note.raw_value = note_number;
			note.lane = note_number;
			note.velocity = event.getVelocity();
			note.channel = event.getChannelNibble();
			push_parsed_note(track, note);
			return true;
		}

		bool parse_pro_keys_note(const smf::MidiEvent &event, MidiChartTrack &track)
		{
			const int note_number = event.getKeyNumber();
			const MidiChartDifficulty difficulty = pro_keys_track_difficulty(track.type);

			switch (note_number)
			{
			case 0:
				push_phrase(track, MidiChartPhraseType::ProKeysRangeShift0, difficulty, event);
				return true;
			case 2:
				push_phrase(track, MidiChartPhraseType::ProKeysRangeShift1, difficulty, event);
				return true;
			case 4:
				push_phrase(track, MidiChartPhraseType::ProKeysRangeShift2, difficulty, event);
				return true;
			case 5:
				push_phrase(track, MidiChartPhraseType::ProKeysRangeShift3, difficulty, event);
				return true;
			case 7:
				push_phrase(track, MidiChartPhraseType::ProKeysRangeShift4, difficulty, event);
				return true;
			case 9:
				push_phrase(track, MidiChartPhraseType::ProKeysRangeShift5, difficulty, event);
				return true;
			case 115:
				push_phrase(track, MidiChartPhraseType::Solo, difficulty, event);
				return true;
			case 120:
				push_phrase(track, MidiChartPhraseType::BigRockEnding, difficulty, event);
				return true;
			case 126:
				push_phrase(track, MidiChartPhraseType::ProKeysGlissando, difficulty, event);
				return true;
			case 127:
				push_phrase(track, MidiChartPhraseType::TrillLane, difficulty, event);
				return true;
			default:
				break;
			}

			if (note_number < 48 || note_number > 72)
				return false;

			MidiChartParsedNote note;
			note.category = MidiChartNoteCategory::ProKeys;
			note.difficulty = difficulty;
			note.tick = event.tick;
			note.end_tick = event.isLinked() ? event.getLinkedEvent()->tick : event.tick;
			note.start_seconds = event.seconds;
			note.end_seconds = event.isLinked() ? event.getLinkedEvent()->seconds : event.seconds;
			note.raw_midi_note = note_number;
			note.raw_value = note_number - 48;
			note.lane = note.raw_value;
			note.velocity = event.getVelocity();
			note.channel = event.getChannelNibble();
			push_parsed_note(track, note);
			return true;
		}

		bool parse_pro_guitar_note(const smf::MidiEvent &event, MidiChartTrack &track, int star_power_note_override)
		{
			if (parse_common_phrase(event, track, star_power_note_override, 115, false, true))
				return true;

			MidiChartDifficulty difficulty = MidiChartDifficulty::None;
			int raw_value = -1;
			if (!match_note_range(event.getKeyNumber(), kProGuitarNoteRanges, std::size(kProGuitarNoteRanges), difficulty, raw_value))
				return false;

			MidiChartParsedNote note;
			note.category = MidiChartNoteCategory::ProGuitar;
			note.difficulty = difficulty;
			note.tick = event.tick;
			note.end_tick = event.isLinked() ? event.getLinkedEvent()->tick : event.tick;
			note.start_seconds = event.seconds;
			note.end_seconds = event.isLinked() ? event.getLinkedEvent()->seconds : event.seconds;
			note.raw_midi_note = event.getKeyNumber();
			note.raw_value = raw_value;
			note.fret = raw_value & 0x1F;
			note.string_index = (raw_value >> 5) & 0x07;
			note.velocity = event.getVelocity();
			note.channel = event.getChannelNibble();
			if (note.channel == 3)
				note.flags |= MidiChartNoteFlagProGuitarMuted;
			push_parsed_note(track, note);
			return true;
		}

		bool parse_elite_drums_note(const smf::MidiEvent &event, MidiChartTrack &track)
		{
			const int note_number = event.getKeyNumber();
			if (parse_common_phrase(event, track, 104, 103, true, false))
				return true;

			if (note_number == 120)
			{
				push_phrase(track, MidiChartPhraseType::DrumFill, MidiChartDifficulty::All, event);
				return true;
			}

			switch (note_number)
			{
			case 108:
				push_phrase(track, MidiChartPhraseType::EliteDrumsHatPedalLane, MidiChartDifficulty::All, event);
				return true;
			case 110:
				push_phrase(track, MidiChartPhraseType::EliteDrumsKickLane, MidiChartDifficulty::All, event);
				return true;
			case 111:
				push_phrase(track, MidiChartPhraseType::EliteDrumsSnareLane, MidiChartDifficulty::All, event);
				return true;
			case 112:
				push_phrase(track, MidiChartPhraseType::EliteDrumsHiHatLane, MidiChartDifficulty::All, event);
				return true;
			case 113:
				push_phrase(track, MidiChartPhraseType::EliteDrumsLeftCrashLane, MidiChartDifficulty::All, event);
				return true;
			case 114:
				push_phrase(track, MidiChartPhraseType::EliteDrumsTom1Lane, MidiChartDifficulty::All, event);
				return true;
			case 115:
				push_phrase(track, MidiChartPhraseType::EliteDrumsTom2Lane, MidiChartDifficulty::All, event);
				return true;
			case 116:
				push_phrase(track, MidiChartPhraseType::EliteDrumsTom3Lane, MidiChartDifficulty::All, event);
				return true;
			case 117:
				push_phrase(track, MidiChartPhraseType::EliteDrumsRideLane, MidiChartDifficulty::All, event);
				return true;
			case 118:
				push_phrase(track, MidiChartPhraseType::EliteDrumsRightCrashLane, MidiChartDifficulty::All, event);
				return true;
			default:
				break;
			}

			MidiChartDifficulty difficulty = MidiChartDifficulty::None;
			int raw_value = -1;
			bool double_kick = false;
			if (!match_elite_drums_note(note_number, difficulty, raw_value, double_kick))
			{
				for (const DifficultyNoteRange &range : kEliteDrumsNoteRanges)
				{
					if (note_number == range.start_note + 13)
					{
						push_phrase(track, MidiChartPhraseType::EliteDrumsFlamModifier, range.difficulty, event);
						return true;
					}
					if (note_number == range.start_note + 14)
					{
						push_phrase(track, MidiChartPhraseType::EliteDrumsForcedIndifferentModifier, range.difficulty, event);
						return true;
					}
					if (note_number == range.start_note + 16)
					{
						push_phrase(track, MidiChartPhraseType::EliteDrumsDiscoFlip, range.difficulty, event);
						return true;
					}
				}
				return false;
			}

			MidiChartParsedNote note;
			note.category = MidiChartNoteCategory::EliteDrums;
			note.difficulty = difficulty;
			note.tick = event.tick;
			note.end_tick = event.isLinked() ? event.getLinkedEvent()->tick : event.tick;
			note.start_seconds = event.seconds;
			note.end_seconds = event.isLinked() ? event.getLinkedEvent()->seconds : event.seconds;
			note.raw_midi_note = note_number;
			note.raw_value = raw_value;
			note.lane = raw_value;
			note.velocity = event.getVelocity();
			note.channel = event.getChannelNibble();
			if (note.lane == 0 && track.uses_strict_hat_pedal_state)
				note.flags |= MidiChartNoteFlagEliteDrumsStrictHatState;
			if (note.lane == 0)
			{
				if (note.velocity == 127)
					note.flags |= MidiChartNoteFlagEliteDrumsSplash;
				else if (note.velocity == 1)
					note.flags |= MidiChartNoteFlagEliteDrumsInvisibleTerminator;
			}
			else
			{
				if (note.velocity == 127)
					note.flags |= MidiChartNoteFlagProDrumsAccent;
				else if (note.velocity == 1)
					note.flags |= MidiChartNoteFlagProDrumsGhost;
			}
			switch (note.channel)
			{
			case 10:
				note.flags |= MidiChartNoteFlagEliteDrumsChannelRed;
				break;
			case 11:
				note.flags |= MidiChartNoteFlagEliteDrumsChannelYellow;
				break;
			case 12:
				note.flags |= MidiChartNoteFlagEliteDrumsChannelBlue;
				break;
			case 13:
				note.flags |= MidiChartNoteFlagEliteDrumsChannelGreen;
				break;
			default:
				break;
			}
			if (raw_value == 0)
				push_phrase(track, MidiChartPhraseType::EliteDrumsForcedClosedModifier, difficulty, event);
			if (double_kick)
				note.flags |= MidiChartNoteFlagInstrumentPlus;
			push_parsed_note(track, note);
			return true;
		}

		void apply_phrase_note_flags(MidiChartTrack &track)
		{
			for (const MidiChartPhrase &phrase : track.phrases)
			{
				std::uint32_t flag = MidiChartNoteFlagNone;
				switch (phrase.type)
				{
				case MidiChartPhraseType::StarPower:
					flag = MidiChartNoteFlagStarPower;
					break;
				case MidiChartPhraseType::Solo:
					flag = MidiChartNoteFlagSolo;
					break;
				case MidiChartPhraseType::TremoloLane:
					flag = MidiChartNoteFlagTremolo;
					break;
				case MidiChartPhraseType::TrillLane:
					flag = MidiChartNoteFlagTrill;
					break;
				case MidiChartPhraseType::BigRockEnding:
					flag = MidiChartNoteFlagBigRockEnding;
					break;
				default:
					break;
				}

				for (MidiChartParsedNote &note : track.parsed_notes)
				{
					if (phrase.difficulty != MidiChartDifficulty::All &&
						phrase.difficulty != MidiChartDifficulty::None &&
						note.difficulty != phrase.difficulty)
					{
						continue;
					}

					if (note.tick < phrase.tick || note.tick >= phrase.end_tick)
						continue;

					if (flag != MidiChartNoteFlagNone)
						note.flags |= flag;

					switch (phrase.type)
					{
					case MidiChartPhraseType::TapModifier:
						note.flags |= MidiChartNoteFlagTap;
						note.flags &= ~(MidiChartNoteFlagForced | MidiChartNoteFlagForcedHopo | MidiChartNoteFlagForcedStrum);
						break;
					case MidiChartPhraseType::ForcedHopoModifier:
						if ((note.flags & MidiChartNoteFlagTap) == 0)
						{
							note.flags |= MidiChartNoteFlagForced | MidiChartNoteFlagForcedHopo;
							note.flags &= ~MidiChartNoteFlagForcedStrum;
						}
						break;
					case MidiChartPhraseType::ForcedStrumModifier:
						if ((note.flags & MidiChartNoteFlagTap) == 0)
						{
							note.flags |= MidiChartNoteFlagForced | MidiChartNoteFlagForcedStrum;
							note.flags &= ~MidiChartNoteFlagForcedHopo;
						}
						break;
					case MidiChartPhraseType::DrumsCymbalYellowModifier:
						if (note.category == MidiChartNoteCategory::Drums && note.raw_value == 2)
							note.flags |= MidiChartNoteFlagProDrumsCymbal;
						break;
					case MidiChartPhraseType::DrumsCymbalBlueModifier:
						if (note.category == MidiChartNoteCategory::Drums && note.raw_value == 3)
							note.flags |= MidiChartNoteFlagProDrumsCymbal;
						break;
					case MidiChartPhraseType::DrumsCymbalOrangeModifier:
						if (note.category == MidiChartNoteCategory::Drums && note.raw_value == 4)
							note.flags |= MidiChartNoteFlagProDrumsCymbal;
						break;
					case MidiChartPhraseType::EliteDrumsForcedClosedModifier:
						if (note.category == MidiChartNoteCategory::EliteDrums && note.raw_value == 3)
							note.flags ^= MidiChartNoteFlagEliteDrumsForcedClosed;
						break;
					case MidiChartPhraseType::EliteDrumsForcedIndifferentModifier:
						if (note.category == MidiChartNoteCategory::EliteDrums && note.raw_value == 3)
							note.flags ^= MidiChartNoteFlagEliteDrumsForcedIndifferent;
						break;
					case MidiChartPhraseType::EliteDrumsFlamModifier:
						if (note.category == MidiChartNoteCategory::EliteDrums && note.raw_value != 0 && note.raw_value != 1)
							note.flags ^= MidiChartNoteFlagEliteDrumsFlam;
						break;
					default:
						break;
					}
				}
			}
		}

		MidiChartMeasureLine::Kind get_yarg_beatline_kind(const MidiChartTimeSignature &time_signature, int beatline_count)
		{
			constexpr int strong_step = 4;
			const int safe_numerator = (std::max)(time_signature.numerator, 1);
			const int measure_beat_count = beatline_count % safe_numerator;
			const int strong_rate = (time_signature.denominator <= 4 || time_signature.denominator < strong_step)
				? 1
				: time_signature.denominator / strong_step;

			if (safe_numerator == 1)
			{
				if (beatline_count < 1)
					return MidiChartMeasureLine::Kind::Measure;
				return (beatline_count % strong_rate) == 0 ? MidiChartMeasureLine::Kind::Strong : MidiChartMeasureLine::Kind::Weak;
			}

			if (measure_beat_count == 0)
				return MidiChartMeasureLine::Kind::Measure;

			if (time_signature.denominator <= 4 || time_signature.denominator < strong_step)
				return MidiChartMeasureLine::Kind::Strong;

			if ((measure_beat_count % strong_rate) == 0)
			{
				if (measure_beat_count == safe_numerator - 1)
					return MidiChartMeasureLine::Kind::Weak;
				return MidiChartMeasureLine::Kind::Strong;
			}

			return MidiChartMeasureLine::Kind::Weak;
		}

		void generate_yarg_measure_lines(smf::MidiFile &midi_file,
			const std::vector<MidiChartTimeSignature> &time_signatures,
			std::vector<MidiChartMeasureLine> &measure_lines)
		{
			const int ticks_per_quarter = midi_file.getTicksPerQuarterNote();
			if (ticks_per_quarter <= 0)
				return;

			const int file_duration_ticks = midi_file.getFileDurationInTicks();
			if (file_duration_ticks <= 0 || time_signatures.empty())
				return;

			for (size_t index = 0; index < time_signatures.size(); ++index)
			{
				const MidiChartTimeSignature &segment = time_signatures[index];
				const int segment_end_tick = index + 1 < time_signatures.size()
					? time_signatures[index + 1].tick - 1
					: file_duration_ticks + 1;
				const int beat_length_ticks = (ticks_per_quarter * 4) / (std::max)(segment.denominator, 1);
				if (beat_length_ticks <= 0)
					continue;

				int beatline_count = 0;
				for (int current_tick = segment.tick; current_tick <= segment_end_tick; current_tick += beat_length_ticks)
				{
					MidiChartMeasureLine beat_line;
					beat_line.tick = current_tick;
					beat_line.time_seconds = midi_file.getTimeInSeconds(current_tick);
					beat_line.kind = get_yarg_beatline_kind(segment, beatline_count);
					measure_lines.push_back(beat_line);
					++beatline_count;
				}
			}
		}

		bool parse_instrument_note(const smf::MidiEvent &event,
			MidiChartTrack &track,
			int star_power_note_override)
		{
			switch (track.type)
			{
			case MidiChartTrackType::FiveFretGuitar:
			case MidiChartTrackType::FiveFretCoop:
			case MidiChartTrackType::FiveFretBass:
			case MidiChartTrackType::FiveFretRhythm:
			case MidiChartTrackType::FiveFretKeys:
				return parse_five_fret_note(event, track, star_power_note_override);
			case MidiChartTrackType::SixFretGuitar:
			case MidiChartTrackType::SixFretCoop:
			case MidiChartTrackType::SixFretBass:
			case MidiChartTrackType::SixFretRhythm:
				return parse_six_fret_note(event, track, star_power_note_override);
			case MidiChartTrackType::Drums:
				return parse_drums_note(event, track, star_power_note_override);
			case MidiChartTrackType::EliteDrums:
				return parse_elite_drums_note(event, track);
			case MidiChartTrackType::Vocals:
			case MidiChartTrackType::Harmony1:
			case MidiChartTrackType::Harmony2:
			case MidiChartTrackType::Harmony3:
				return parse_vocals_note(event, track);
			case MidiChartTrackType::ProKeysEasy:
			case MidiChartTrackType::ProKeysMedium:
			case MidiChartTrackType::ProKeysHard:
			case MidiChartTrackType::ProKeysExpert:
				return parse_pro_keys_note(event, track);
			case MidiChartTrackType::ProGuitar17:
			case MidiChartTrackType::ProGuitar22:
			case MidiChartTrackType::ProBass17:
			case MidiChartTrackType::ProBass22:
				return parse_pro_guitar_note(event, track, star_power_note_override);
			default:
				break;
			}

			if (parse_common_phrase(event, track, star_power_note_override, 103, false, false))
				return true;
			return false;
		}

		std::optional<MidiChartSysExEvent> try_parse_phase_shift_sysex(const smf::MidiEvent &event)
		{
			if (event.empty())
				return std::nullopt;

			int header_offset = -1;
			for (size_t index = 0; index + 7 < event.size(); ++index)
			{
				if (event[index] == 0x50 && event[index + 1] == 0x53 && event[index + 2] == 0x00)
				{
					header_offset = static_cast<int>(index);
					break;
				}
			}

			if (header_offset < 0 || header_offset + 6 >= static_cast<int>(event.size()))
				return std::nullopt;

			MidiChartSysExEvent sysex_event;
			sysex_event.type = MidiChartSysExType::PhaseShiftPhrase;
			sysex_event.tick = event.tick;
			sysex_event.time_seconds = event.seconds;
			sysex_event.difficulty = MidiChartDifficulty::None;
			switch (event[header_offset + 4])
			{
			case 0x00:
				sysex_event.difficulty = MidiChartDifficulty::Easy;
				break;
			case 0x01:
				sysex_event.difficulty = MidiChartDifficulty::Medium;
				break;
			case 0x02:
				sysex_event.difficulty = MidiChartDifficulty::Hard;
				break;
			case 0x03:
				sysex_event.difficulty = MidiChartDifficulty::Expert;
				break;
			case 0xFF:
				sysex_event.difficulty = MidiChartDifficulty::All;
				break;
			default:
				break;
			}
			sysex_event.phrase_code = event[header_offset + 5];
			sysex_event.phrase_value = event[header_offset + 6];
			sysex_event.data.reserve(event.size());
			for (int value : event)
				sysex_event.data.push_back(static_cast<std::uint8_t>(value));
			return sysex_event;
		}

		bool difficulty_matches(MidiChartDifficulty note_difficulty, MidiChartDifficulty target_difficulty)
		{
			return target_difficulty == MidiChartDifficulty::All ||
				target_difficulty == MidiChartDifficulty::None ||
				note_difficulty == target_difficulty;
		}

		std::vector<ParsedDifficultyPreference> build_preview_difficulty_order(MidiChartDifficulty preferred_difficulty)
		{
			std::vector<ParsedDifficultyPreference> order;
			order.reserve(std::size(kPreferredDifficulties));

			if (preferred_difficulty != MidiChartDifficulty::None)
			{
				for (const ParsedDifficultyPreference &preference : kPreferredDifficulties)
				{
					if (preference.difficulty == preferred_difficulty)
					{
						order.push_back(preference);
						break;
					}
				}
			}

			for (const ParsedDifficultyPreference &preference : kPreferredDifficulties)
			{
				if (preference.difficulty == preferred_difficulty)
					continue;
				order.push_back(preference);
			}

			return order;
		}

		bool is_legacy_starpower_fixup_track(MidiChartTrackType type)
		{
			switch (type)
			{
			case MidiChartTrackType::FiveFretGuitar:
			case MidiChartTrackType::FiveFretCoop:
			case MidiChartTrackType::FiveFretBass:
			case MidiChartTrackType::FiveFretRhythm:
				return true;
			default:
				return false;
			}
		}

		void sort_track_content(MidiChartTrack &track)
		{
			std::sort(track.parsed_notes.begin(), track.parsed_notes.end(),
				[](const MidiChartParsedNote &left, const MidiChartParsedNote &right)
				{
					if (left.tick != right.tick)
						return left.tick < right.tick;
					if (left.difficulty != right.difficulty)
						return static_cast<int>(left.difficulty) < static_cast<int>(right.difficulty);
					return left.raw_value < right.raw_value;
				});

			std::sort(track.phrases.begin(), track.phrases.end(),
				[](const MidiChartPhrase &left, const MidiChartPhrase &right)
				{
					if (left.tick != right.tick)
						return left.tick < right.tick;
					if (left.difficulty != right.difficulty)
						return static_cast<int>(left.difficulty) < static_cast<int>(right.difficulty);
					return static_cast<int>(left.type) < static_cast<int>(right.type);
				});
		}

		void clear_phrase_note_flags(MidiChartTrack &track)
		{
			constexpr std::uint32_t kPhraseFlags =
				MidiChartNoteFlagForced |
				MidiChartNoteFlagForcedHopo |
				MidiChartNoteFlagForcedStrum |
				MidiChartNoteFlagStarPower |
				MidiChartNoteFlagSolo |
				MidiChartNoteFlagTremolo |
				MidiChartNoteFlagTrill |
				MidiChartNoteFlagBigRockEnding |
				MidiChartNoteFlagTap |
				MidiChartNoteFlagProDrumsCymbal |
				MidiChartNoteFlagEliteDrumsForcedIndifferent |
				MidiChartNoteFlagEliteDrumsForcedClosed |
				MidiChartNoteFlagEliteDrumsFlam;

			for (MidiChartParsedNote &note : track.parsed_notes)
				note.flags &= ~kPhraseFlags;
		}

		std::vector<std::pair<int, int>> get_coda_ranges(const std::vector<MidiChartTextEvent> &global_events)
		{
			std::vector<std::pair<int, int>> ranges;
			for (const MidiChartTextEvent &event : global_events)
			{
				const std::string lowered = to_lower_copy(event.text);
				if (lowered == "coda")
				{
					if (!ranges.empty() && ranges.back().second == (std::numeric_limits<int>::max)())
						return ranges;

					ranges.emplace_back(event.tick, (std::numeric_limits<int>::max)());
				}
				else if (lowered == "coda_end")
				{
					if (ranges.empty() || ranges.back().second != (std::numeric_limits<int>::max)())
						return ranges;

					ranges.back().second = event.tick;
				}
			}

			return ranges;
		}

		void apply_sysex_open_modifier(MidiChartTrack &track, int start_tick, int end_tick, MidiChartDifficulty difficulty)
		{
			for (MidiChartParsedNote &note : track.parsed_notes)
			{
				if ((note.category != MidiChartNoteCategory::FiveFret && note.category != MidiChartNoteCategory::SixFret) ||
					!difficulty_matches(note.difficulty, difficulty) ||
					note.tick < start_tick || note.tick > end_tick)
				{
					continue;
				}

				note.raw_value = 0;
				note.lane = 0;
			}
		}

		void apply_sysex_tap_modifier(MidiChartTrack &track, int start_tick, int end_tick, MidiChartDifficulty difficulty)
		{
			for (MidiChartParsedNote &note : track.parsed_notes)
			{
				if ((note.category != MidiChartNoteCategory::FiveFret && note.category != MidiChartNoteCategory::SixFret) ||
					!difficulty_matches(note.difficulty, difficulty) ||
					note.tick < start_tick || note.tick > end_tick)
				{
					continue;
				}

				note.flags |= MidiChartNoteFlagTap;
			}
		}

		void apply_sysex_postprocessing(MidiChartTrack &track)
		{
			struct ActiveSysEx
			{
				MidiChartSysExEvent event;
			};

			std::vector<ActiveSysEx> active_events;
			for (const MidiChartSysExEvent &sysex_event : track.sysex_events)
			{
				if (sysex_event.type != MidiChartSysExType::PhaseShiftPhrase)
					continue;

				if (sysex_event.phrase_value == 1)
				{
					active_events.push_back({sysex_event});
					continue;
				}

				if (sysex_event.phrase_value != 0)
					continue;

				for (size_t index = 0; index < active_events.size(); ++index)
				{
					const ActiveSysEx &active = active_events[index];
					if (active.event.phrase_code != sysex_event.phrase_code ||
						active.event.difficulty != sysex_event.difficulty)
					{
						continue;
					}

					if (sysex_event.phrase_code == 0x01)
					{
						int end_tick = sysex_event.tick;
						if (end_tick > active.event.tick)
							--end_tick;
						apply_sysex_open_modifier(track, active.event.tick, end_tick, active.event.difficulty);
					}
					else if (sysex_event.phrase_code == 0x04)
					{
						apply_sysex_tap_modifier(track, active.event.tick, sysex_event.tick, active.event.difficulty);
					}

					active_events.erase(active_events.begin() + static_cast<std::ptrdiff_t>(index));
					break;
				}
			}
		}

		void fixup_starpower_if_needed(MidiChartTrack &track, bool has_starpower_override)
		{
			if (has_starpower_override || !is_legacy_starpower_fixup_track(track.type))
				return;

			bool has_starpower = false;
			bool has_solo = false;
			for (const MidiChartPhrase &phrase : track.phrases)
			{
				if (phrase.difficulty != MidiChartDifficulty::All && phrase.difficulty != MidiChartDifficulty::Expert)
					continue;
				if (phrase.type == MidiChartPhraseType::StarPower)
					has_starpower = true;
				if (phrase.type == MidiChartPhraseType::Solo)
					has_solo = true;
			}

			if (has_starpower || !has_solo)
				return;

			for (MidiChartPhrase &phrase : track.phrases)
			{
				if (phrase.type == MidiChartPhraseType::Solo)
					phrase.type = MidiChartPhraseType::StarPower;
			}
		}

		void replace_drum_fill_during_coda(MidiChartTrack &track, const std::vector<std::pair<int, int>> &coda_ranges)
		{
			if (track.type != MidiChartTrackType::Drums || coda_ranges.empty())
				return;

			bool has_bre = false;
			bool has_drum_fill = false;
			for (const MidiChartPhrase &phrase : track.phrases)
			{
				has_bre |= phrase.type == MidiChartPhraseType::BigRockEnding;
				has_drum_fill |= phrase.type == MidiChartPhraseType::DrumFill;
			}

			if (has_bre || !has_drum_fill)
				return;

			for (MidiChartPhrase &phrase : track.phrases)
			{
				if (phrase.type != MidiChartPhraseType::DrumFill)
					continue;

				for (const auto &[start_tick, end_tick] : coda_ranges)
				{
					if (phrase.tick >= start_tick && phrase.tick <= end_tick)
					{
						phrase.type = MidiChartPhraseType::BigRockEnding;
						break;
					}
				}
			}
		}

		void set_coda_flags(MidiChartTrack &track, const std::vector<std::pair<int, int>> &coda_ranges)
		{
			if (coda_ranges.empty() || track.parsed_notes.empty())
				return;

			std::vector<MidiChartDifficulty> difficulties;
			for (const MidiChartParsedNote &note : track.parsed_notes)
			{
				if (std::find(difficulties.begin(), difficulties.end(), note.difficulty) == difficulties.end())
					difficulties.push_back(note.difficulty);
			}

			for (MidiChartDifficulty difficulty : difficulties)
			{
				size_t coda_index = 0;
				bool coda_started = false;
				int last_matching_note_index = -1;
				int previous_matching_note_index = -1;
				for (size_t i = 0; i < track.parsed_notes.size(); ++i)
				{
					if (track.parsed_notes[i].difficulty == difficulty)
						last_matching_note_index = static_cast<int>(i);
				}

				if (last_matching_note_index < 0)
					continue;

				for (size_t i = 0; i < track.parsed_notes.size() && coda_index < coda_ranges.size(); ++i)
				{
					MidiChartParsedNote &note = track.parsed_notes[i];
					if (note.difficulty != difficulty)
						continue;

					if (note.tick < coda_ranges[coda_index].first)
						continue;

					if (!coda_started)
						coda_started = true;

					if (note.tick >= coda_ranges[coda_index].second && coda_started)
					{
						const int end_index = previous_matching_note_index >= 0
							? previous_matching_note_index
							: static_cast<int>(i);
						track.parsed_notes[static_cast<size_t>(end_index)].flags |= MidiChartNoteFlagCodaEnd;
						coda_started = false;
						++coda_index;
						continue;
					}

					if (static_cast<int>(i) == last_matching_note_index && coda_started)
					{
						note.flags |= MidiChartNoteFlagCodaEnd;
						break;
					}

					previous_matching_note_index = static_cast<int>(i);
				}
			}
		}

		void copy_down_bre_phrases(std::vector<MidiChartTrack> &tracks)
		{
			MidiChartTrack *expert_track = nullptr;
			for (MidiChartTrack &track : tracks)
			{
				if (track.type == MidiChartTrackType::ProKeysExpert)
				{
					expert_track = &track;
					break;
				}
			}

			if (expert_track == nullptr)
				return;

			std::vector<MidiChartPhrase> bre_phrases;
			for (const MidiChartPhrase &phrase : expert_track->phrases)
			{
				if (phrase.type == MidiChartPhraseType::BigRockEnding)
					bre_phrases.push_back(phrase);
			}

			for (MidiChartTrack &track : tracks)
			{
				if (!is_pro_keys_track(track.type) || track.type == MidiChartTrackType::ProKeysExpert)
					continue;

				const MidiChartDifficulty target_difficulty = pro_keys_track_difficulty(track.type);
				for (const MidiChartPhrase &phrase : bre_phrases)
				{
					MidiChartPhrase copied = phrase;
					copied.difficulty = target_difficulty;
					track.phrases.push_back(std::move(copied));
				}
			}
		}

		void copy_down_vocals_phrases(std::vector<MidiChartTrack> &tracks)
		{
			MidiChartTrack *harm1 = nullptr;
			MidiChartTrack *harm2 = nullptr;
			MidiChartTrack *harm3 = nullptr;
			for (MidiChartTrack &track : tracks)
			{
				if (track.type == MidiChartTrackType::Harmony1)
					harm1 = &track;
				else if (track.type == MidiChartTrackType::Harmony2)
					harm2 = &track;
				else if (track.type == MidiChartTrackType::Harmony3)
					harm3 = &track;
			}

			if (harm1 == nullptr)
				return;

			if (harm2 != nullptr)
			{
				std::vector<MidiChartPhrase> new_phrases;
				for (const MidiChartPhrase &phrase : harm2->phrases)
				{
					if (phrase.type == MidiChartPhraseType::VocalsStaticPhrase)
						new_phrases.push_back(phrase);
				}
				harm2->phrases = std::move(new_phrases);
				for (const MidiChartPhrase &phrase : harm1->phrases)
				{
					if (phrase.type == MidiChartPhraseType::VocalsScoringPhrase ||
						phrase.type == MidiChartPhraseType::StarPower)
					{
						harm2->phrases.push_back(phrase);
					}
				}
			}

			if (harm3 != nullptr)
			{
				std::vector<MidiChartPhrase> new_phrases;
				if (harm2 != nullptr)
				{
					for (const MidiChartPhrase &phrase : harm2->phrases)
					{
						if (phrase.type == MidiChartPhraseType::VocalsStaticPhrase)
							new_phrases.push_back(phrase);
					}
				}
				harm3->phrases = std::move(new_phrases);
				for (const MidiChartPhrase &phrase : harm1->phrases)
				{
					if (phrase.type == MidiChartPhraseType::VocalsScoringPhrase ||
						phrase.type == MidiChartPhraseType::StarPower)
					{
						harm3->phrases.push_back(phrase);
					}
				}
			}
		}

		void suppress_non_strict_stomps_and_splashes(MidiChartTrack &track)
		{
			if (track.type != MidiChartTrackType::EliteDrums)
				return;

			for (size_t i = 0; i < track.parsed_notes.size(); ++i)
			{
				MidiChartParsedNote &pedal = track.parsed_notes[i];
				if (pedal.category != MidiChartNoteCategory::EliteDrums ||
					pedal.raw_value != 0 ||
					(pedal.flags & MidiChartNoteFlagEliteDrumsStrictHatState) != 0)
				{
					continue;
				}

				for (size_t j = 0; j < track.parsed_notes.size(); ++j)
				{
					const MidiChartParsedNote &hat = track.parsed_notes[j];
					if (hat.category != MidiChartNoteCategory::EliteDrums ||
						hat.difficulty != pedal.difficulty ||
						hat.tick != pedal.tick ||
						hat.raw_value != 3 ||
						(hat.flags & MidiChartNoteFlagEliteDrumsForcedIndifferent) != 0)
					{
						continue;
					}

					pedal.flags |= MidiChartNoteFlagEliteDrumsInvisibleTerminator;
					break;
				}
			}
		}

		void create_kick_flams(MidiChartTrack &track)
		{
			if (track.type != MidiChartTrackType::EliteDrums)
				return;

			for (MidiChartParsedNote &kick : track.parsed_notes)
			{
				if (kick.category != MidiChartNoteCategory::EliteDrums ||
					kick.raw_value != 1 ||
					(kick.flags & MidiChartNoteFlagInstrumentPlus) == 0)
				{
					continue;
				}

				for (const MidiChartParsedNote &other : track.parsed_notes)
				{
					if (&other == &kick)
						continue;
					if (other.category == MidiChartNoteCategory::EliteDrums &&
						other.difficulty == kick.difficulty &&
						other.tick == kick.tick &&
						other.raw_value == 1 &&
						(other.flags & MidiChartNoteFlagInstrumentPlus) == 0)
					{
						kick.flags |= MidiChartNoteFlagEliteDrumsFlam;
						break;
					}
				}
			}
		}

		MidiChartDrumsType detect_drums_type(const std::vector<MidiChartTrack> &tracks)
		{
			for (const MidiChartTrack &track : tracks)
			{
				if (track.type != MidiChartTrackType::Drums)
					continue;

				for (const MidiChartParsedNote &note : track.parsed_notes)
				{
					if (note.category != MidiChartNoteCategory::Drums)
						continue;
					if (note.raw_value == 5)
						return MidiChartDrumsType::FiveLane;
					if (note.raw_value != 0 && note.raw_value != 1 &&
						(note.flags & MidiChartNoteFlagProDrumsCymbal) == 0)
					{
						return MidiChartDrumsType::FourLane;
					}
				}
			}

			for (const MidiChartTrack &track : tracks)
			{
				if (track.type == MidiChartTrackType::Drums)
					return MidiChartDrumsType::FourLane;
			}

			return MidiChartDrumsType::Unknown;
		}

		void apply_postprocessing(std::vector<MidiChartTrack> &tracks,
			const std::vector<MidiChartTextEvent> &global_events,
			bool has_starpower_override,
			MidiChartDrumsType &detected_drums_type)
		{
			const std::vector<std::pair<int, int>> coda_ranges = get_coda_ranges(global_events);

			for (MidiChartTrack &track : tracks)
			{
				apply_sysex_postprocessing(track);
				fixup_starpower_if_needed(track, has_starpower_override);
				replace_drum_fill_during_coda(track, coda_ranges);
			}

			copy_down_bre_phrases(tracks);
			copy_down_vocals_phrases(tracks);

			for (MidiChartTrack &track : tracks)
			{
				set_coda_flags(track, coda_ranges);
				clear_phrase_note_flags(track);
				apply_phrase_note_flags(track);
				suppress_non_strict_stomps_and_splashes(track);
				create_kick_flams(track);
				sort_track_content(track);
			}

			detected_drums_type = detect_drums_type(tracks);
		}
	}

bool MidiChart::load(const ::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system,
	const std::string &song_directory,
	MidiChartDifficulty preferred_difficulty,
	MidiChartTrackType preferred_track_type,
	std::string &error_message)
	{
		clear();

		const std::string song_ini_path = find_case_insensitive_file(file_system, song_directory, "song.ini");
		if (!song_ini_path.empty())
		{
			const SongIniParseResult song_ini = parse_song_ini(file_system, song_ini_path);
			std::int32_t multiplier_note = 0;
			if (song_ini.parsed_successfully && song_ini.metadata.try_get_int32("multiplier_note", multiplier_note))
				star_power_note_override_ = multiplier_note;
		}

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

		ticks_per_quarter_note_ = midi_file.getTicksPerQuarterNote();
		duration_seconds_ = midi_file.getFileDurationInSeconds();

		time_signatures_.push_back({});

		for (int track_index = 0; track_index < midi_file.getTrackCount(); ++track_index)
		{
			const smf::MidiEventList &midi_track = midi_file[track_index];
			MidiChartTrack track;
			track.midi_track_index = track_index;
			track.name = extract_track_name(midi_track);
			track.type = track_index == 0 ? MidiChartTrackType::Sync : classify_track_type(track.name);

			for (int event_index = 0; event_index < midi_track.size(); ++event_index)
			{
				const smf::MidiEvent &event = midi_track[event_index];

				if (track_index == 0)
				{
					if (event.isTempo())
					{
						MidiChartTempoChange tempo_change;
						tempo_change.tick = event.tick;
						tempo_change.time_seconds = event.seconds;
						tempo_change.beats_per_minute = event.getTempoBPM();
						tempo_changes_.push_back(tempo_change);
					}

					if (event.isTimeSignature() && event.size() >= 6)
					{
						MidiChartTimeSignature time_signature;
						time_signature.tick = event.tick;
						time_signature.time_seconds = event.seconds;
						time_signature.numerator = event[3];
						time_signature.denominator = denominator_from_power(event[4]);
						if (time_signature.tick == 0)
							time_signatures_.front() = time_signature;
						else
							time_signatures_.push_back(time_signature);
					}
				}

				if (is_text_event(event))
					collect_text_event(event, track, global_events_, sections_, lyrics_);

				if (event.isNoteOn())
				{
					push_raw_note_event(event, track);

					if (track.type == MidiChartTrackType::Beat)
					{
						MidiChartMeasureLine beat_line;
						if (event.getKeyNumber() == 12)
							beat_line.kind = MidiChartMeasureLine::Kind::Measure;
						else if (event.getKeyNumber() == 13)
							beat_line.kind = MidiChartMeasureLine::Kind::Strong;
						else if (event.getKeyNumber() == 14)
							beat_line.kind = MidiChartMeasureLine::Kind::Weak;
						else
							goto next_event;

						beat_line.tick = event.tick;
						beat_line.time_seconds = event.seconds;
						measure_lines_.push_back(beat_line);
					}
					else
					{
						parse_instrument_note(event, track,
							star_power_note_override_ >= 0 ? star_power_note_override_ : 116);
					}
				}

				if (!event.empty() && (event[0] == 0xF0 || event[0] == 0xF7))
				{
					const auto parsed_sysex = try_parse_phase_shift_sysex(event);
					if (parsed_sysex.has_value())
						track.sysex_events.push_back(*parsed_sysex);
				}

			next_event:
				;
			}

			sort_track_content(track);
			tracks_.push_back(std::move(track));
		}

		apply_postprocessing(tracks_, global_events_, star_power_note_override_ >= 0, detected_drums_type_);

		std::sort(time_signatures_.begin(), time_signatures_.end(),
			[](const MidiChartTimeSignature &left, const MidiChartTimeSignature &right)
			{
				return left.tick < right.tick;
			});
		std::sort(measure_lines_.begin(), measure_lines_.end(),
			[](const MidiChartMeasureLine &left, const MidiChartMeasureLine &right)
			{
				return left.tick < right.tick;
			});

		if (measure_lines_.empty())
			generate_yarg_measure_lines(midi_file, time_signatures_, measure_lines_);

		rebuild_preview_selection(preferred_difficulty, preferred_track_type);
		if (notes_.empty())
		{
			error_message = "notes.mid loaded, but no supported 5-fret chart was found yet.";
			return false;
		}

		return true;
	}

	void MidiChart::clear()
	{
		ticks_per_quarter_note_ = 480;
		star_power_note_override_ = -1;
		detected_drums_type_ = MidiChartDrumsType::Unknown;
		track_name_.clear();
		difficulty_name_.clear();
		notes_.clear();
		tempo_changes_.clear();
		time_signatures_.clear();
		measure_lines_.clear();
		global_events_.clear();
		sections_.clear();
		lyrics_.clear();
		tracks_.clear();
		duration_seconds_ = 0.0;
	}

	bool MidiChart::is_loaded() const
	{
		return !notes_.empty() || !tracks_.empty();
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

	const std::vector<MidiChartTimeSignature> &MidiChart::time_signatures() const
	{
		return time_signatures_;
	}

	const std::vector<MidiChartMeasureLine> &MidiChart::measure_lines() const
	{
		return measure_lines_;
	}

	const std::vector<MidiChartTextEvent> &MidiChart::global_events() const
	{
		return global_events_;
	}

	const std::vector<MidiChartTextEvent> &MidiChart::sections() const
	{
		return sections_;
	}

	const std::vector<MidiChartTextEvent> &MidiChart::lyrics() const
	{
		return lyrics_;
	}

	const std::vector<MidiChartTrack> &MidiChart::tracks() const
	{
		return tracks_;
	}

	std::vector<MidiChartTrackType> MidiChart::available_preview_track_types() const
	{
		std::vector<MidiChartTrackType> available_track_types;
		for (const InstrumentTrackPreference &track_preference : kPreviewTrackPreferences)
		{
			for (const MidiChartTrack &track : tracks_)
			{
				if (track.type != track_preference.type)
					continue;

				const auto has_preview_notes = std::any_of(track.parsed_notes.begin(), track.parsed_notes.end(),
					[](const MidiChartParsedNote &note)
					{
						return note.category == MidiChartNoteCategory::FiveFret &&
							note.lane >= 1 &&
							note.lane <= 5;
					});
				if (has_preview_notes)
					available_track_types.push_back(track.type);
				break;
			}
		}

		return available_track_types;
	}

	int MidiChart::ticks_per_quarter_note() const
	{
		return ticks_per_quarter_note_;
	}

	int MidiChart::star_power_note_override() const
	{
		return star_power_note_override_;
	}

	MidiChartDrumsType MidiChart::detected_drums_type() const
	{
		return detected_drums_type_;
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

	std::vector<MidiChartMeasureLine> MidiChart::collect_visible_measure_lines(double song_time_seconds,
		double lookbehind_seconds,
		double lookahead_seconds) const
	{
		std::vector<MidiChartMeasureLine> visible_measure_lines;
		if (measure_lines_.empty())
			return visible_measure_lines;

		const double min_time = song_time_seconds - lookbehind_seconds;
		const double max_time = song_time_seconds + lookahead_seconds;

		auto begin = std::lower_bound(measure_lines_.begin(), measure_lines_.end(), min_time,
			[](const MidiChartMeasureLine &measure_line, double time_seconds)
			{
				return measure_line.time_seconds < time_seconds;
			});

		for (auto it = begin; it != measure_lines_.end(); ++it)
		{
			if (it->time_seconds > max_time)
				break;
			visible_measure_lines.push_back(*it);
		}

		return visible_measure_lines;
	}

	std::string MidiChart::find_case_insensitive_file(const ::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system,
		const std::string &directory_path,
		std::string_view file_name)
	{
		const std::string target_name = to_upper_copy(file_name);
		for (const ::rhythmreplugged::frontend_contract::RetroDirectoryEntry &entry : file_system.list_directory(directory_path))
		{
			if (entry.is_directory)
				continue;
			if (to_upper_copy(entry.name) == target_name)
				return entry.path;
		}

		return {};
	}

	void MidiChart::rebuild_preview_selection(MidiChartDifficulty preferred_difficulty, MidiChartTrackType preferred_track_type)
	{
		track_name_.clear();
		difficulty_name_.clear();
		notes_.clear();
		const std::vector<ParsedDifficultyPreference> difficulty_order = build_preview_difficulty_order(preferred_difficulty);
		std::vector<MidiChartTrackType> track_order;
		if (is_supported_preview_track_type(preferred_track_type))
			track_order.push_back(preferred_track_type);
		for (const InstrumentTrackPreference &track_preference : kPreviewTrackPreferences)
		{
			if (track_preference.type != preferred_track_type)
				track_order.push_back(track_preference.type);
		}

		for (const MidiChartTrackType track_type : track_order)
		{
			for (const MidiChartTrack &track : tracks_)
			{
				if (track.type != track_type)
					continue;

				for (const ParsedDifficultyPreference &difficulty_preference : difficulty_order)
				{
					std::vector<MidiChartNote> preview_notes;
					for (const MidiChartParsedNote &note : track.parsed_notes)
					{
						if (note.category != MidiChartNoteCategory::FiveFret ||
							note.difficulty != difficulty_preference.difficulty ||
							note.lane < 1 || note.lane > 5)
						{
							continue;
						}

						MidiChartNote preview_note;
						preview_note.lane = note.lane - 1;
						preview_note.tick = note.tick;
						preview_note.end_tick = note.end_tick;
						preview_note.start_seconds = note.start_seconds;
						preview_note.end_seconds = note.end_seconds;
						preview_notes.push_back(preview_note);
					}

					if (preview_notes.empty())
						continue;

					track_name_ = std::string(display_name_for_preview_track_type(track_type));
					difficulty_name_ = std::string(difficulty_preference.name);
					notes_ = std::move(preview_notes);
					return;
				}
			}
		}
	}
}
