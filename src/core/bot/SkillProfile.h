#pragma once

#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

namespace rhythmreplugged::core
{
	// How a bot plays. Every field is a statistical knob; with the same profile and seed a bot makes
	// exactly the same decisions every run.
	struct SkillProfile
	{
		// Chance each note group is attempted at all. 1 - this is the "didn't even try" miss rate.
		double hit_probability = 1.0;
		// Strike time relative to the note: mean and standard deviation. Late/early strikes outside the
		// game's hit window naturally become misses, so this also feeds the miss rate.
		double timing_mean_seconds = 0.0;
		double timing_sigma_seconds = 0.0;
		// Chance a sustain is let go early, at a random point in its first 15%..85%.
		double sustain_drop_probability = 0.0;
		// Standard deviation of the release time around a sustain's end when it is held through.
		double sustain_release_sigma_seconds = 0.0;
		std::uint64_t seed = 1;

		static std::optional<SkillProfile> preset(std::string_view name)
		{
			SkillProfile profile;
			if (name == "perfect")
				return profile;
			if (name == "good")
			{
				profile.hit_probability = 0.97;
				profile.timing_sigma_seconds = 0.020;
				profile.sustain_drop_probability = 0.05;
				profile.sustain_release_sigma_seconds = 0.015;
				return profile;
			}
			if (name == "sloppy")
			{
				profile.hit_probability = 0.88;
				profile.timing_sigma_seconds = 0.040;
				profile.sustain_drop_probability = 0.20;
				profile.sustain_release_sigma_seconds = 0.030;
				return profile;
			}
			if (name == "poor")
			{
				profile.hit_probability = 0.65;
				profile.timing_sigma_seconds = 0.060;
				profile.sustain_drop_probability = 0.40;
				profile.sustain_release_sigma_seconds = 0.050;
				return profile;
			}
			return std::nullopt;
		}
	};

	// Small self-contained generator (splitmix64) so a seed means the same thing on every platform and
	// standard library; <random> distributions are not portable.
	class BotRng
	{
	public:
		explicit BotRng(std::uint64_t seed) : state_(seed) {}

		std::uint64_t next()
		{
			state_ += 0x9E3779B97F4A7C15ull;
			std::uint64_t z = state_;
			z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
			z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
			return z ^ (z >> 31);
		}

		// Uniform in [0, 1).
		double uniform() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }

		// Standard normal via Box-Muller. Always consumes exactly two draws.
		double gaussian()
		{
			const double u1 = 1.0 - uniform();
			const double u2 = uniform();
			return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
		}

	private:
		std::uint64_t state_;
	};
}
