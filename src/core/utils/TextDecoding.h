#pragma once

#include <optional>
#include <string>
#include <vector>

namespace rhythmreplugged::core::utils
{
	std::optional<std::string> decode_text_bytes(const std::vector<char> &bytes);
}
