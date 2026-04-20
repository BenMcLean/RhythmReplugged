#pragma once

#include <array>
#include <string_view>

namespace rhythmreplugged
{
	inline constexpr std::array<std::string_view, 14> kKnownStemNames = {
		"song", "guitar", "bass", "rhythm", "keys", "vocals", "vocals_1", "vocals_2",
		"drums", "drums_1", "drums_2", "drums_3", "drums_4", "crowd"};

	inline constexpr std::array<std::string_view, 13> kPlayableStemNames = {
		"song", "guitar", "bass", "rhythm", "keys", "vocals", "vocals_1", "vocals_2",
		"drums", "drums_1", "drums_2", "drums_3", "drums_4"};
}
