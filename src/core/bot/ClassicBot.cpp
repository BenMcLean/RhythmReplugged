#include "core/bot/ClassicBot.h"

#include "core/play/SongSession.h"

#include <algorithm>

namespace rhythmreplugged::core
{
	ClassicBot::ClassicBot(const std::vector<MidiChartNote> &notes, const SkillProfile &profile, double tick_seconds)
		: tick_seconds_(tick_seconds)
	{
		BotRng rng(profile.seed);

		struct RawGroup
		{
			double time_seconds = 0.0;
			std::array<bool, 5> lanes{};
			std::array<double, 5> end_seconds{};
		};
		std::vector<RawGroup> raw_groups;
		for (const MidiChartNote &note : notes)
		{
			if (raw_groups.empty() || note.start_seconds != raw_groups.back().time_seconds)
				raw_groups.push_back({note.start_seconds, {}, {}});
			if (note.lane >= 0 && note.lane < 5)
			{
				raw_groups.back().lanes[static_cast<size_t>(note.lane)] = true;
				raw_groups.back().end_seconds[static_cast<size_t>(note.lane)] = note.end_seconds;
			}
		}

		double previous_strike_seconds = 0.0;
		groups_.reserve(raw_groups.size());
		for (const RawGroup &raw : raw_groups)
		{
			PlannedGroup group;
			group.lanes = raw.lanes;

			// Draw order is fixed per group, whether or not each value ends up used.
			const double attempt_roll = rng.uniform();
			const double timing_roll = rng.gaussian();
			group.attempt = attempt_roll < profile.hit_probability;
			// A player can't strike notes out of order, so a late strike pushes later ones back with it.
			group.strike_seconds = std::max(
				previous_strike_seconds,
				raw.time_seconds + profile.timing_mean_seconds + timing_roll * profile.timing_sigma_seconds);
			previous_strike_seconds = group.strike_seconds;

			for (size_t lane = 0; lane < 5; ++lane)
			{
				const double drop_roll = rng.uniform();
				const double drop_fraction_roll = rng.uniform();
				const double release_roll = rng.gaussian();
				if (!raw.lanes[lane])
					continue;

				const double length_seconds = raw.end_seconds[lane] - raw.time_seconds;
				if (length_seconds < SongSession::kSustainMinimumSeconds)
					continue;

				if (drop_roll < profile.sustain_drop_probability)
					group.release_seconds[lane] = group.strike_seconds + length_seconds * (0.15 + 0.70 * drop_fraction_roll);
				else
					group.release_seconds[lane] = raw.end_seconds[lane] + release_roll * profile.sustain_release_sigma_seconds;
			}

			groups_.push_back(group);
		}
	}

	BotInput ClassicBot::next_input(double song_time_seconds)
	{
		BotInput input;
		for (size_t lane = 0; lane < 5; ++lane)
			input.held[lane] = song_time_seconds < hold_until_[lane];

		// Strike on the tick nearest the planned time. A lane must be released for a tick before it can
		// be struck again (pressed is a rising edge), so a group waits while its lanes are still down.
		if (next_group_ < groups_.size() && song_time_seconds >= groups_[next_group_].strike_seconds - tick_seconds_ * 0.5)
		{
			const PlannedGroup &group = groups_[next_group_];
			if (!group.attempt)
			{
				++next_group_;
			}
			else
			{
				bool blocked = false;
				for (size_t lane = 0; lane < 5; ++lane)
					blocked = blocked || (group.lanes[lane] && previous_held_[lane]);

				if (blocked)
				{
					for (size_t lane = 0; lane < 5; ++lane)
					{
						if (group.lanes[lane])
							input.held[lane] = false;
					}
				}
				else
				{
					for (size_t lane = 0; lane < 5; ++lane)
					{
						if (!group.lanes[lane])
							continue;
						input.held[lane] = true;
						hold_until_[lane] = group.release_seconds[lane];
					}
					++next_group_;
					++struck_count_;
				}
			}
		}

		for (size_t lane = 0; lane < 5; ++lane)
			input.pressed[lane] = input.held[lane] && !previous_held_[lane];
		previous_held_ = input.held;
		return input;
	}
}
