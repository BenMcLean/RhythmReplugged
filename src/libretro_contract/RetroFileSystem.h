#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rhythmreplugged
{
	struct RetroDirectoryEntry
	{
		std::string path;
		std::string name;
		bool is_directory = false;
	};

	class IRetroFileSystem
	{
	public:
		virtual ~IRetroFileSystem() = default;

		virtual std::string canonicalize_path(const std::string &path) const = 0;
		virtual std::string parent_path(const std::string &path) const = 0;
		virtual bool path_exists(const std::string &path) const = 0;
		virtual bool path_is_directory(const std::string &path) const = 0;
		virtual std::vector<RetroDirectoryEntry> list_directory(const std::string &path) const = 0;
		virtual std::optional<std::string> read_text_file(const std::string &path) const = 0;
		virtual std::optional<std::vector<std::uint8_t>> read_binary_file(const std::string &path) const = 0;
	};
}
