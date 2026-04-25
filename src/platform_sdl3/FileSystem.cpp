#include "platform_sdl3/FileSystem.h"

#include "core/utils/TextDecoding.h"

#include <filesystem>
#include <fstream>
#include <vector>

namespace rhythmreplugged::platform_sdl3
{
	std::string FileSystem::canonicalize_path(const std::string &path) const
	{
		std::error_code error_code;
		const std::filesystem::path canonical = std::filesystem::weakly_canonical(std::filesystem::path(path), error_code);
		return error_code ? std::string() : canonical.generic_string();
	}

	std::string FileSystem::parent_path(const std::string &path) const
	{
		return std::filesystem::path(path).parent_path().generic_string();
	}

	bool FileSystem::path_exists(const std::string &path) const
	{
		std::error_code error_code;
		return std::filesystem::exists(std::filesystem::path(path), error_code) && !error_code;
	}

	bool FileSystem::path_is_directory(const std::string &path) const
	{
		std::error_code error_code;
		return std::filesystem::is_directory(std::filesystem::path(path), error_code) && !error_code;
	}

	std::optional<std::uint64_t> FileSystem::file_size(const std::string &path) const
	{
		std::error_code error_code;
		const auto size = std::filesystem::file_size(std::filesystem::path(path), error_code);
		if (error_code)
			return std::nullopt;
		return static_cast<std::uint64_t>(size);
	}

	std::vector<::rhythmreplugged::frontend_contract::RetroDirectoryEntry> FileSystem::list_directory(const std::string &path) const
	{
		std::vector<::rhythmreplugged::frontend_contract::RetroDirectoryEntry> entries;
		std::error_code error_code;
		for (const auto &entry : std::filesystem::directory_iterator(std::filesystem::path(path), error_code))
		{
			if (error_code)
				break;

			::rhythmreplugged::frontend_contract::RetroDirectoryEntry directory_entry;
			directory_entry.path = entry.path().generic_string();
			directory_entry.name = entry.path().filename().string();
			directory_entry.is_directory = entry.is_directory(error_code) && !error_code;
			entries.push_back(std::move(directory_entry));
		}

		return entries;
	}

	std::optional<std::string> FileSystem::read_text_file(const std::string &path) const
	{
		std::ifstream stream(std::filesystem::path(path), std::ios::binary);
		if (!stream)
			return std::nullopt;

		const std::vector<char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
		return core::utils::decode_text_bytes(bytes);
	}

	std::optional<std::vector<std::uint8_t>> FileSystem::read_binary_file(const std::string &path) const
	{
		std::ifstream stream(std::filesystem::path(path), std::ios::binary);
		if (!stream)
			return std::nullopt;

		const std::vector<char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
		std::vector<std::uint8_t> output(bytes.size());
		for (size_t i = 0; i < bytes.size(); ++i)
			output[i] = static_cast<std::uint8_t>(bytes[i]);

		return output;
	}
}
