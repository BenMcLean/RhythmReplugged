// rr_sim: headless bot simulator. Plays songs through SongSession without audio or rendering.
// Classic mode, Guitar, driven by a ClassicBot with a configurable skill profile.
//
// usage: rr_sim <song or library directory> [easy|medium|hard|expert|all] [--trace]
//               [--skill perfect|good|sloppy|poor] [--seed N] [--hit-prob P]
// A directory containing song.ini is one song; any other directory is searched recursively for songs.

#include "core/app/AppTypes.h"
#include "core/bot/ClassicBot.h"
#include "core/chart/MidiChart.h"
#include "core/play/GameplayOptions.h"
#include "core/play/SongSession.h"
#include "platform_sdl3/FileSystem.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace
{
	using namespace rhythmreplugged::core;
	using rhythmreplugged::platform_sdl3::FileSystem;

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

	// Each song and difficulty plays from its own random stream, derived from the base seed and the run's
	// identity (FNV-1a over the path), so a bulk run samples independently across songs but any single
	// run is still exactly reproducible from the base seed.
	std::uint64_t derive_run_seed(std::uint64_t base_seed, const std::string &song_directory, const char *difficulty_name)
	{
		std::uint64_t hash = 0xCBF29CE484222325ull;
		for (const char character : song_directory + "|" + difficulty_name)
		{
			hash ^= static_cast<unsigned char>(character);
			hash *= 0x100000001B3ull;
		}
		return BotRng(base_seed ^ hash).next();
	}

	struct SongResult
	{
		bool played = false;
		std::string message;
		size_t ticks = 0;
		size_t note_groups = 0;
		size_t struck_groups = 0;
		size_t groups_hit = 0;
		size_t groups_missed = 0;
		size_t muted_ticks = 0;
		size_t mute_events = 0;
		double first_mute_seconds = -1.0;
	};

	SongResult play_song(FileSystem &file_system, const std::string &song_directory, const DifficultyChoice &difficulty, const SkillProfile &skill, bool trace)
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

		const double tick_seconds = 1.0 / static_cast<double>(kAppFramesPerSecond);
		SkillProfile run_skill = skill;
		run_skill.seed = derive_run_seed(skill.seed, song_directory, difficulty.name);
		ClassicBot bot(chart.notes(), run_skill, tick_seconds);
		bool was_muted = false;

		while (!session.playback_finished())
		{
			const double now = session.song_time_seconds();
			const BotInput input = bot.next_input(now);
			session.update_gameplay_input(input.held, input.pressed);

			const bool muted = session.gameplay_lane_view(0).is_muted;
			if (muted)
			{
				++result.muted_ticks;
				if (result.first_mute_seconds < 0.0)
					result.first_mute_seconds = now;
			}
			if (muted && !was_muted)
				++result.mute_events;
			if (trace && muted != was_muted)
			{
				std::cout << "  " << (muted ? "MUTE   " : "UNMUTE ") << "t=" << now << " held=";
				for (const bool h : input.held)
					std::cout << (h ? '1' : '0');
				std::cout << "\n";
			}
			was_muted = muted;

			session.advance_headless_tick(kAppFramesPerSecond);
			++result.ticks;
		}

		const SongSession::LaneStats stats = session.gameplay_lane_stats(0);
		result.groups_hit = stats.groups_hit;
		result.groups_missed = stats.groups_missed;
		result.struck_groups = bot.struck_count();
		result.played = true;
		result.note_groups = bot.group_count();
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
		std::cerr << "usage: rr_sim <song or library directory> [easy|medium|hard|expert|all] [--trace]\n"
				  << "              [--skill perfect|good|sloppy|poor] [--seed N] [--hit-prob P]\n";
		return EXIT_FAILURE;
	}

	std::vector<const DifficultyChoice *> difficulties = {&kDifficulties[3]};
	std::string skill_name = "perfect";
	SkillProfile skill = *SkillProfile::preset(skill_name);
	std::optional<std::uint64_t> seed;
	std::optional<double> hit_probability;
	bool trace = false;
	for (int arg_index = 2; arg_index < argc; ++arg_index)
	{
		const std::string arg = argv[arg_index];
		auto next_value = [&]() -> const char * { return arg_index + 1 < argc ? argv[++arg_index] : nullptr; };
		if (arg == "--trace")
		{
			trace = true;
		}
		else if (arg == "--skill" || arg == "--seed" || arg == "--hit-prob")
		{
			const char *value = next_value();
			if (value == nullptr)
			{
				std::cerr << arg << " needs a value\n";
				return EXIT_FAILURE;
			}
			if (arg == "--skill")
			{
				const std::optional<SkillProfile> preset = SkillProfile::preset(value);
				if (!preset.has_value())
				{
					std::cerr << "unknown skill: " << value << "\n";
					return EXIT_FAILURE;
				}
				skill_name = value;
				skill = *preset;
			}
			else if (arg == "--seed")
			{
				seed = std::strtoull(value, nullptr, 10);
			}
			else
			{
				hit_probability = std::atof(value);
			}
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
	if (seed.has_value())
		skill.seed = *seed;
	if (hit_probability.has_value())
		skill.hit_probability = *hit_probability;

	FileSystem file_system;
	std::vector<std::string> songs;
	find_songs(file_system, argv[1], songs);
	if (songs.empty())
	{
		std::cerr << "no songs (folders containing song.ini) found under " << argv[1] << "\n";
		return EXIT_FAILURE;
	}

	std::cout << "skill=" << skill_name << " seed=" << skill.seed << " hit_prob=" << skill.hit_probability << "\n";

	size_t played = 0;
	size_t skipped = 0;
	size_t imperfect = 0;
	size_t total_groups = 0;
	size_t total_hit = 0;
	for (const std::string &song : songs)
	{
		for (const DifficultyChoice *difficulty : difficulties)
		{
			const SongResult result = play_song(file_system, song, *difficulty, skill, trace);
			std::cout << song << " [" << difficulty->name << "] ";
			if (!result.played)
			{
				++skipped;
				std::cout << "skipped: " << result.message << "\n";
				continue;
			}

			++played;
			total_groups += result.note_groups;
			total_hit += result.groups_hit;
			if (result.muted_ticks != 0 || result.groups_hit != result.note_groups)
				++imperfect;
			std::cout << "groups=" << result.note_groups
					  << " struck=" << result.struck_groups
					  << " hit=" << result.groups_hit
					  << " missed=" << result.groups_missed
					  << " muted_ticks=" << result.muted_ticks
					  << " mute_events=" << result.mute_events << "\n";
		}
	}

	std::cout << "songs=" << songs.size() << " played=" << played << " skipped=" << skipped
			  << " imperfect=" << imperfect << " hit_rate="
			  << (total_groups == 0 ? 0.0 : static_cast<double>(total_hit) / static_cast<double>(total_groups)) << "\n";
	// A perfect bot must play perfectly, so that run doubles as a regression check. Other skills are just data.
	return skill_name == "perfect" && imperfect != 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
