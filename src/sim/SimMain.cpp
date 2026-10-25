// rr_sim: headless bot simulator. Plays a song through SongSession without audio or rendering.
// Step 1: Classic mode, Guitar, perfect-skill bot. Used to prove the headless seam and as a hit-logic baseline.

#include "core/app/AppTypes.h"
#include "core/chart/MidiChart.h"
#include "core/play/GameplayOptions.h"
#include "core/play/SongSession.h"
#include "platform_sdl3/FileSystem.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	using namespace rhythmreplugged::core;

	struct NoteGroup
	{
		double time_seconds = 0.0;
		double end_seconds = 0.0;
		std::array<bool, 5> lanes{};
		std::array<double, 5> lane_end_seconds{};
	};

	std::vector<NoteGroup> group_notes(const std::vector<MidiChartNote> &notes)
	{
		std::vector<NoteGroup> groups;
		for (const MidiChartNote &note : notes)
		{
			if (groups.empty() || note.start_seconds != groups.back().time_seconds)
				groups.push_back({note.start_seconds, note.start_seconds, {}, {}});
			if (note.lane >= 0 && note.lane < 5)
			{
				groups.back().lanes[static_cast<size_t>(note.lane)] = true;
				groups.back().lane_end_seconds[static_cast<size_t>(note.lane)] = note.end_seconds;
			}
		}
		return groups;
	}

	bool parse_difficulty(const std::string &name, DifficultyOption &difficulty, MidiChartDifficulty &chart_difficulty)
	{
		if (name == "easy") { difficulty = DifficultyOption::Easy; chart_difficulty = MidiChartDifficulty::Easy; }
		else if (name == "medium") { difficulty = DifficultyOption::Medium; chart_difficulty = MidiChartDifficulty::Medium; }
		else if (name == "hard") { difficulty = DifficultyOption::Hard; chart_difficulty = MidiChartDifficulty::Hard; }
		else if (name == "expert") { difficulty = DifficultyOption::Expert; chart_difficulty = MidiChartDifficulty::Expert; }
		else return false;
		return true;
	}
}

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		std::cerr << "usage: rr_sim <song directory> [easy|medium|hard|expert]\n";
		return EXIT_FAILURE;
	}

	const std::string song_directory = argv[1];
	DifficultyOption difficulty = DifficultyOption::Expert;
	MidiChartDifficulty chart_difficulty = MidiChartDifficulty::Expert;
	if (argc >= 3 && !parse_difficulty(argv[2], difficulty, chart_difficulty))
	{
		std::cerr << "unknown difficulty: " << argv[2] << "\n";
		return EXIT_FAILURE;
	}

	rhythmreplugged::platform_sdl3::FileSystem file_system;
	std::string error_message;

	MidiChart chart;
	if (!chart.load(file_system, song_directory, chart_difficulty, MidiChartTrackType::FiveFretGuitar, error_message))
	{
		std::cerr << "chart load failed: " << error_message << "\n";
		return EXIT_FAILURE;
	}

	SongPlayer::PreloadedSongData song_data;
	if (!SongPlayer::preload_headless(file_system, song_directory, chart.duration_seconds(), song_data, error_message))
	{
		std::cerr << "song metadata load failed: " << error_message << "\n";
		return EXIT_FAILURE;
	}

	GameplayOptions options;
	options.set_gameplay_mode(GameplayMode::Classic);
	options.set_instrument(InstrumentOption::Guitar);
	options.set_difficulty(difficulty);

	SongSession session;
	if (!session.load_preloaded(file_system, song_directory, chart, std::move(song_data), options, error_message))
	{
		std::cerr << "session load failed: " << error_message << "\n";
		return EXIT_FAILURE;
	}

	const std::vector<NoteGroup> groups = group_notes(chart.notes());
	const double tick_seconds = 1.0 / static_cast<double>(kAppFramesPerSecond);

	std::array<bool, 5> previous_held{};
	std::array<double, 5> hold_until{};
	size_t group_index = 0;
	size_t struck_groups = 0;
	size_t ticks = 0;
	size_t muted_ticks = 0;
	double first_mute_seconds = -1.0;
	bool was_muted = false;
	// Extra hold past a sustain's end. 0 = release on the first tick at/after the end time.
	const double sustain_overhold_seconds = std::getenv("RR_SIM_OVERHOLD") ? std::atof(std::getenv("RR_SIM_OVERHOLD")) : 0.0;
	const bool trace = argc >= 4 && std::string(argv[3]) == "--trace";

	while (!session.playback_finished())
	{
		const double now = session.song_time_seconds();

		// Perfect bot: strike each group on the tick nearest its time, hold sustains to their end.
		// A lane must be released for a tick before it can be struck again (pressed is a rising edge).
		std::array<bool, 5> held{};
		for (size_t lane = 0; lane < 5; ++lane)
			held[lane] = now < hold_until[lane];

		if (group_index < groups.size() && now >= groups[group_index].time_seconds - tick_seconds * 0.5)
		{
			const NoteGroup &group = groups[group_index];
			bool blocked = false;
			for (size_t lane = 0; lane < 5; ++lane)
				blocked = blocked || (group.lanes[lane] && previous_held[lane]);
			if (!blocked)
			{
				for (size_t lane = 0; lane < 5; ++lane)
				{
					if (!group.lanes[lane])
						continue;
					held[lane] = true;
					const bool is_sustain = group.lane_end_seconds[lane] - group.time_seconds >= SongSession::kSustainMinimumSeconds;
					hold_until[lane] = is_sustain ? group.lane_end_seconds[lane] + sustain_overhold_seconds : 0.0;
				}
				++group_index;
				++struck_groups;
			}
			else
			{
				for (size_t lane = 0; lane < 5; ++lane)
				{
					if (group.lanes[lane])
						held[lane] = false;
				}
			}
		}

		std::array<bool, 5> pressed{};
		for (size_t lane = 0; lane < 5; ++lane)
			pressed[lane] = held[lane] && !previous_held[lane];

		session.update_gameplay_input(held, pressed);
		previous_held = held;

		const bool muted = session.gameplay_lane_view(0).is_muted;
		if (muted)
		{
			++muted_ticks;
			if (first_mute_seconds < 0.0)
				first_mute_seconds = now;
		}
		if (trace && muted != was_muted)
		{
			std::cout << (muted ? "MUTE   " : "UNMUTE ") << "t=" << now << " next_group=" << group_index;
			if (group_index < groups.size())
				std::cout << " (t=" << groups[group_index].time_seconds << ")";
			if (group_index > 0)
				std::cout << " prev_group_t=" << groups[group_index - 1].time_seconds
						  << " prev_end=" << *std::max_element(groups[group_index - 1].lane_end_seconds.begin(), groups[group_index - 1].lane_end_seconds.end());
			std::cout << " held=";
			for (const bool h : held)
				std::cout << (h ? '1' : '0');
			std::cout << "\n";
		}
		was_muted = muted;

		session.advance_headless_tick(kAppFramesPerSecond);
		++ticks;
	}

	std::cout << "ticks=" << ticks
			  << " note_groups=" << groups.size()
			  << " struck=" << struck_groups
			  << " muted_ticks=" << muted_ticks
			  << " first_mute_seconds=" << first_mute_seconds << "\n";
	return muted_ticks == 0 && struck_groups == groups.size() ? EXIT_SUCCESS : EXIT_FAILURE;
}
