#pragma once

#include "libretro_contract/RetroFileSystem.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rhythmreplugged
{
	enum class MidiChartTrackType
	{
		Unknown,
		Sync,
		Beat,
		Events,
		Venue,
		FiveFretGuitar,
		FiveFretCoop,
		FiveFretBass,
		FiveFretRhythm,
		FiveFretKeys,
		SixFretGuitar,
		SixFretCoop,
		SixFretBass,
		SixFretRhythm,
		Drums,
		EliteDrums,
		Vocals,
		Harmony1,
		Harmony2,
		Harmony3,
		ProGuitar17,
		ProGuitar22,
		ProBass17,
		ProBass22,
		ProKeysEasy,
		ProKeysMedium,
		ProKeysHard,
		ProKeysExpert,
	};

	enum class MidiChartDifficulty
	{
		None,
		Easy,
		Medium,
		Hard,
		Expert,
		All,
	};

	enum class MidiChartNoteCategory
	{
		Generic,
		FiveFret,
		SixFret,
		Drums,
		EliteDrums,
		Vocals,
		ProKeys,
		ProGuitar,
		Animation,
	};

	enum class MidiChartPhraseType
	{
		Unknown,
		StarPower,
		Solo,
		VersusPlayer1,
		VersusPlayer2,
		TremoloLane,
		TrillLane,
		DrumFill,
		BigRockEnding,
		Coda,
		VocalsRangeShift,
		VocalsLyricShift,
		VocalsScoringPhrase,
		VocalsStaticPhrase,
		ProKeysRangeShift0,
		ProKeysRangeShift1,
		ProKeysRangeShift2,
		ProKeysRangeShift3,
		ProKeysRangeShift4,
		ProKeysRangeShift5,
		ProKeysGlissando,
		EliteDrumsDiscoFlip,
		EliteDrumsHatPedalLane,
		EliteDrumsKickLane,
		EliteDrumsSnareLane,
		EliteDrumsHiHatLane,
		EliteDrumsLeftCrashLane,
		EliteDrumsTom1Lane,
		EliteDrumsTom2Lane,
		EliteDrumsTom3Lane,
		EliteDrumsRideLane,
		EliteDrumsRightCrashLane,
	};

	enum class MidiChartTextEventType
	{
		Generic,
		Lyric,
		Marker,
		TrackName,
		Section,
		Crowd,
		ParserDirective,
		MusicStart,
		MusicEnd,
		Coda,
		CodaEnd,
	};

	enum class MidiChartSysExType
	{
		Unknown,
		PhaseShiftPhrase,
	};

	enum MidiChartNoteFlags : std::uint32_t
	{
		MidiChartNoteFlagNone = 0,
		MidiChartNoteFlagForced = 1u << 0,
		MidiChartNoteFlagForcedStrum = 1u << 1,
		MidiChartNoteFlagForcedHopo = 1u << 2,
		MidiChartNoteFlagTap = 1u << 3,
		MidiChartNoteFlagStarPower = 1u << 4,
		MidiChartNoteFlagSolo = 1u << 5,
		MidiChartNoteFlagTremolo = 1u << 6,
		MidiChartNoteFlagTrill = 1u << 7,
		MidiChartNoteFlagBigRockEnding = 1u << 8,
		MidiChartNoteFlagProDrumsCymbal = 1u << 9,
		MidiChartNoteFlagProDrumsAccent = 1u << 10,
		MidiChartNoteFlagProDrumsGhost = 1u << 11,
		MidiChartNoteFlagInstrumentPlus = 1u << 12,
		MidiChartNoteFlagProGuitarMuted = 1u << 13,
		MidiChartNoteFlagVocalsPercussion = 1u << 14,
		MidiChartNoteFlagEliteDrumsFlam = 1u << 15,
		MidiChartNoteFlagEliteDrumsForcedIndifferent = 1u << 16,
		MidiChartNoteFlagEliteDrumsForcedClosed = 1u << 17,
		MidiChartNoteFlagEliteDrumsSplash = 1u << 18,
		MidiChartNoteFlagEliteDrumsInvisibleTerminator = 1u << 19,
		MidiChartNoteFlagEliteDrumsStrictHatState = 1u << 20,
		MidiChartNoteFlagEliteDrumsChannelRed = 1u << 21,
		MidiChartNoteFlagEliteDrumsChannelYellow = 1u << 22,
		MidiChartNoteFlagEliteDrumsChannelBlue = 1u << 23,
		MidiChartNoteFlagEliteDrumsChannelGreen = 1u << 24,
	};

	struct MidiChartNote
	{
		int lane = 0;
		int tick = 0;
		int end_tick = 0;
		double start_seconds = 0.0;
		double end_seconds = 0.0;
	};

	struct MidiChartTempoChange
	{
		int tick = 0;
		double time_seconds = 0.0;
		double beats_per_minute = 120.0;
	};

	struct MidiChartTimeSignature
	{
		int tick = 0;
		double time_seconds = 0.0;
		int numerator = 4;
		int denominator = 4;
	};

	struct MidiChartMeasureLine
	{
		enum class Kind
		{
			Measure,
			Strong,
			Weak,
		};

		int tick = 0;
		double time_seconds = 0.0;
		Kind kind = Kind::Weak;
	};

	struct MidiChartTextEvent
	{
		MidiChartTextEventType type = MidiChartTextEventType::Generic;
		int tick = 0;
		double time_seconds = 0.0;
		std::string text;
	};

	struct MidiChartRawNoteEvent
	{
		int tick = 0;
		int end_tick = 0;
		double start_seconds = 0.0;
		double end_seconds = 0.0;
		int midi_note = 0;
		int velocity = 0;
		int channel = 0;
	};

	struct MidiChartSysExEvent
	{
		MidiChartSysExType type = MidiChartSysExType::Unknown;
		MidiChartDifficulty difficulty = MidiChartDifficulty::None;
		int tick = 0;
		double time_seconds = 0.0;
		int phrase_code = -1;
		int phrase_value = -1;
		std::vector<std::uint8_t> data;
	};

	struct MidiChartParsedNote
	{
		MidiChartNoteCategory category = MidiChartNoteCategory::Generic;
		MidiChartDifficulty difficulty = MidiChartDifficulty::None;
		int tick = 0;
		int end_tick = 0;
		double start_seconds = 0.0;
		double end_seconds = 0.0;
		int raw_midi_note = 0;
		int raw_value = -1;
		int lane = -1;
		int string_index = -1;
		int fret = -1;
		int velocity = 0;
		int channel = 0;
		std::uint32_t flags = MidiChartNoteFlagNone;
	};

	struct MidiChartPhrase
	{
		MidiChartPhraseType type = MidiChartPhraseType::Unknown;
		MidiChartDifficulty difficulty = MidiChartDifficulty::None;
		int tick = 0;
		int end_tick = 0;
		double start_seconds = 0.0;
		double end_seconds = 0.0;
		int raw_midi_note = 0;
	};

	struct MidiChartTrack
	{
		int midi_track_index = -1;
		MidiChartTrackType type = MidiChartTrackType::Unknown;
		std::string name;
		bool uses_enhanced_opens = false;
		bool uses_chart_dynamics = false;
		bool uses_strict_hat_pedal_state = false;
		std::vector<MidiChartTextEvent> text_events;
		std::vector<MidiChartRawNoteEvent> raw_note_events;
		std::vector<MidiChartSysExEvent> sysex_events;
		std::vector<MidiChartParsedNote> parsed_notes;
		std::vector<MidiChartPhrase> phrases;
	};

	class MidiChart
	{
	public:
		bool load(const IRetroFileSystem &file_system, const std::string &song_directory, std::string &error_message);
		void clear();
		bool is_loaded() const;
		std::string_view track_name() const;
		std::string_view difficulty_name() const;
		const std::vector<MidiChartNote> &notes() const;
		const std::vector<MidiChartTempoChange> &tempo_changes() const;
		const std::vector<MidiChartTimeSignature> &time_signatures() const;
		const std::vector<MidiChartMeasureLine> &measure_lines() const;
		const std::vector<MidiChartTextEvent> &global_events() const;
		const std::vector<MidiChartTextEvent> &sections() const;
		const std::vector<MidiChartTextEvent> &lyrics() const;
		const std::vector<MidiChartTrack> &tracks() const;
		int ticks_per_quarter_note() const;
		int star_power_note_override() const;
		double duration_seconds() const;
		double bpm_at_time(double song_time_seconds) const;
		std::vector<MidiChartNote> collect_visible_notes(double song_time_seconds,
			double lookbehind_seconds,
			double lookahead_seconds) const;
		std::vector<MidiChartMeasureLine> collect_visible_measure_lines(double song_time_seconds,
			double lookbehind_seconds,
			double lookahead_seconds) const;

	private:
		static std::string find_case_insensitive_file(const IRetroFileSystem &file_system,
			const std::string &directory_path,
			std::string_view file_name);

		void rebuild_preview_selection();

		int ticks_per_quarter_note_ = 480;
		int star_power_note_override_ = -1;
		std::string track_name_;
		std::string difficulty_name_;
		std::vector<MidiChartNote> notes_;
		std::vector<MidiChartTempoChange> tempo_changes_;
		std::vector<MidiChartTimeSignature> time_signatures_;
		std::vector<MidiChartMeasureLine> measure_lines_;
		std::vector<MidiChartTextEvent> global_events_;
		std::vector<MidiChartTextEvent> sections_;
		std::vector<MidiChartTextEvent> lyrics_;
		std::vector<MidiChartTrack> tracks_;
		double duration_seconds_ = 0.0;
	};
}
