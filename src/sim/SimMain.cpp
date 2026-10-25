// rr_sim: headless bot simulator. Plays songs through SongSession without audio or rendering.
// Step 1: Classic mode, Guitar, perfect-skill bot. Used to prove the headless seam and as a hit-logic baseline.
//
// usage: rr_sim <song or library directory> [easy|medium|hard|expert|all] [--trace]
// A directory containing song.ini is one song; any other directory is searched recursively for songs.

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
	using rhythmreplugged::platform_sdl3::FileSystem;

	struct NoteGroup
	{
		double time_seconds = 0.0;
		std::array<bool, 5> lanes{};
		std::array<double, 5> lane_end_seconds{};
	};

	std::vector<NoteGroup> group_notes(const std::vector<MidiChartNote> &notes)
	{
		std::vector<NoteGroup> groups;
		for (const MidiChartNote &note : notes)
		{
			if (groups.empty() || note.start_seconds != groups.back().time_seconds)
				groups.push_back({note.start_seconds, {}, {}});
			if (note.lane >= 0 && note.lane < 5)
			{
				groups.back().lanes[static_cast<size_t>(note.lane)] = true;
				groups.back().lane_end_seconds[static_cast<size_t>(note.lane)] = note.end_seconds;
			}
		}
		return groups;
	}

	struct DifficultyChoice
	{
		const char *name;
		DifficultyOption option;
		MidiChartDifficulty chart;
	};

	constexpr std::array<DifficultyChoice, 4> kDifficulties = {{
		{"easy", DifficultyOption::Easy, MidiChartDifficulty::Easy},
		{"medium", DifficultyOption::Medium, MidiChartDifficulty::Medium},
		{"hard", DifficultyOption::Hard, MidiChartDifficulty::Hard},
		{"expert", DifficultyOption::Expert, MidiChartDifficulty::Expert},
	}};

	struct SongResult
	{
		bool played = false;
		std::string message;
		size_t ticks = 0;
		size_t note_groups = 0;
		size_t struck_groups = 0;
		size_t muted_ticks = 0;
		double first_mute_seconds = -1.0;
	};

	SongResult play_song(FileSystem &file_system, const std::string &song_directory, const DifficultyChoice &difficulty, bool trace)
	{
		SongResult result;
		std::string error_message;

		MidiChart chart;
		if (!chart.load(file_system, song_directory, difficulty.chart, MidiChartTrackType::FiveFretGuitar, error_message))
		{
			result.message = "chart load failed: " + error_message;
			return result;
		}

		SongPlayer::PreloadedSongData song_data;
		if (!SongPlayer::preload_headless(file_system, song_directory, chart.duration_seconds(), song_data, error_message))
		{
			result.message = "song metadata load failed: " + error_message;
			return result;
		}

		GameplayOptions options;
		options.set_gameplay_mode(GameplayMode::Classic);
		options.set_instrument(InstrumentOption::Guitar);
		options.set_difficulty(difficulty.option);

		SongSession session;
		if (!session.load_preloaded(file_system, song_directory, chart, std::move(song_data), options, error_message))
		{
			result.message = "session load failed: " + error_message;
			return result;
		}

		const std::vector<NoteGroup> groups = group_notes(chart.notes());
		const double tick_seconds = 1.0 / static_cast<double>(kAppFramesPerSecond);

		std::array<bool, 5> previous_held{};
		std::array<double, 5> hold_until{};
		size_t group_index = 0;
		bool was_muted = false;

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
						hold_until[lane] = is_sustain ? group.lane_end_seconds[lane] : 0.0;
					}
					++group_index;
					++result.struck_groups;
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
				++result.muted_ticks;
				if (result.first_mute_seconds < 0.0)
					result.first_mute_seconds = now;
			}
			if (trace && muted != was_muted)
			{
				std::cout << "  " << (muted ? "MUTE   " : "UNMUTE ") << "t=" << now << " next_group=" << group_index;
				if (group_index < groups.size())
					std::cout << " (t=" << groups[group_index].time_seconds << ")";
				std::cout << " held=";
				for (const bool h : held)
					std::cout << (h ? '1' : '0');
				std::cout << "\n";
			}
			was_muted = muted;

			session.advance_headless_tick(kAppFramesPerSecond);
			++result.ticks;
		}

		result.played = true;
		result.note_groups = groups.size();
		return result;
	}

	void find_songs(FileSystem &file_system, const std::string &directory, std::vector<std::string> &songs)
	{
		if (file_system.path_exists(directory + "/song.ini"))
		{
			songs.push_back(directory);
			return;
		}

		std::vector<rhythmreplugged::frontend_contract::RetroDirectoryEntry> entries = file_system.list_directory(directory);
		std::sort(entries.begin(), entries.end(), [](const auto &left, const auto &right) { return left.name < right.name; });
		for (const auto &entry : entries)
		{
			if (entry.is_directory)
				find_songs(file_system, entry.path, songs);
		}
	}
}

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		std::cerr << "usage: rr_sim <song or library directory> [easy|medium|hard|expert|all] [--trace]\n";
		return EXIT_FAILURE;
	}

	std::vector<const DifficultyChoice *> difficulties = {&kDifficulties[3]};
	bool trace = false;
	for (int arg_index = 2; arg_index < argc; ++arg_index)
	{
		const std::string arg = argv[arg_index];
		if (arg == "--trace")
		{
			trace = true;
		}
		else if (arg == "all")
		{
			difficulties.clear();
			for (const DifficultyChoice &choice : kDifficulties)
				difficulties.push_back(&choice);
		}
		else
		{
			const auto found = std::find_if(kDifficulties.begin(), kDifficulties.end(), [&](const DifficultyChoice &choice) { return arg == choice.name; });
			if (found == kDifficulties.end())
			{
				std::cerr << "unknown argument: " << arg << "\n";
				return EXIT_FAILURE;
			}
			difficulties = {&*found};
		}
	}

	FileSystem file_system;
	std::vector<std::string> songs;
	find_songs(file_system, argv[1], songs);
	if (songs.empty())
	{
		std::cerr << "no songs (folders containing song.ini) found under " << argv[1] << "\n";
		return EXIT_FAILURE;
	}

	size_t played = 0;
	size_t skipped = 0;
	size_t imperfect = 0;
	for (const std::string &song : songs)
	{
		for (const DifficultyChoice *difficulty : difficulties)
		{
			const SongResult result = play_song(file_system, song, *difficulty, trace);
			std::cout << song << " [" << difficulty->name << "] ";
			if (!result.played)
			{
				++skipped;
				std::cout << "skipped: " << result.message << "\n";
				continue;
			}

			++played;
			if (result.muted_ticks != 0 || result.struck_groups != result.note_groups)
				++imperfect;
			std::cout << "groups=" << result.note_groups
					  << " struck=" << result.struck_groups
					  << " muted_ticks=" << result.muted_ticks
					  << " first_mute_seconds=" << result.first_mute_seconds << "\n";
		}
	}

	std::cout << "songs=" << songs.size() << " played=" << played << " skipped=" << skipped
			  << " imperfect=" << imperfect << "\n";
	return imperfect == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
