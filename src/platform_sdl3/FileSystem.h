#pragma once

#include "frontend_contract/RetroFileSystem.h"

namespace rhythmreplugged::platform_sdl3
{
	class FileSystem : public ::rhythmreplugged::frontend_contract::IRetroFileSystem
	{
	public:
		std::string canonicalize_path(const std::string &path) const override;
		std::string parent_path(const std::string &path) const override;
		bool path_exists(const std::string &path) const override;
		bool path_is_directory(const std::string &path) const override;
		std::vector<::rhythmreplugged::frontend_contract::RetroDirectoryEntry> list_directory(const std::string &path) const override;
		std::optional<std::string> read_text_file(const std::string &path) const override;
		std::optional<std::vector<std::uint8_t>> read_binary_file(const std::string &path) const override;
	};
}
