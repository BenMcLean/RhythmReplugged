#pragma once

#include "core/bot/SkillProfile.h"
#include "core/chart/MidiChart.h"

#include <array>
#include <cstddef>
#include <vector>

namespace rhythmreplugged::core
{
	// What a player's hands do on one tick: the same two arrays AppCore feeds SongSession.
	struct BotInput
	{
		std::array<bool, 5> held{};
		std::array<bool, 5> pressed{};
	};

	// Plays a single five-fret lane the way a Classic mode player would. The whole performance is planned
	// up front from the chart and the profile's seeded RNG, so a run is reproducible and a profile change
	// only changes the decisions it governs (the RNG draws for each note are always made in the same order).
	class ClassicBot
	{
	public:
		ClassicBot(const std::vector<MidiChartNote> &notes, const SkillProfile &profile, double tick_seconds);

		// Call once per game tick, before the session's update_gameplay_input, with the song time.
		BotInput next_input(double song_time_seconds);

		size_t group_count() const { return groups_.size(); }
		// Groups the bot actually pressed buttons for (the rest were deliberately skipped).
		size_t struck_count() const { return struck_count_; }

	private:
		struct PlannedGroup
		{
			bool attempt = true;
			double strike_seconds = 0.0;
			std::array<bool, 5> lanes{};
			// Absolute song time each lane is let go; 0 means a tap (held for one tick).
			std::array<double, 5> release_seconds{};
		};

		std::vector<PlannedGroup> groups_;
		double tick_seconds_ = 0.0;
		std::array<bool, 5> previous_held_{};
		std::array<double, 5> hold_until_{};
		size_t next_group_ = 0;
		size_t struck_count_ = 0;
	};
}
