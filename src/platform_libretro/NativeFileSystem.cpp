#include "platform_libretro/NativeFileSystem.h"

#include <codecvt>
#include <filesystem>
#include <fstream>
#include <locale>
#include <vector>

namespace rhythmreplugged
{
	namespace
	{
		std::optional<std::string> decode_text_bytes(const std::vector<char> &bytes)
		{
			if (bytes.empty())
				return std::string();

			const auto *raw = reinterpret_cast<const unsigned char *>(bytes.data());
			const size_t size = bytes.size();

			try
			{
				if (size >= 4 && raw[0] == 0xFF && raw[1] == 0xFE && raw[2] == 0x00 && raw[3] == 0x00)
				{
					std::u32string utf32;
					for (size_t i = 4; i + 3 < size; i += 4)
					{
						const char32_t codepoint = static_cast<char32_t>(raw[i]) |
							(static_cast<char32_t>(raw[i + 1]) << 8) |
							(static_cast<char32_t>(raw[i + 2]) << 16) |
							(static_cast<char32_t>(raw[i + 3]) << 24);
						utf32.push_back(codepoint);
					}

					std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> converter;
					return converter.to_bytes(utf32);
				}

				if (size >= 2 && raw[0] == 0xFF && raw[1] == 0xFE)
				{
					std::u16string utf16;
					for (size_t i = 2; i + 1 < size; i += 2)
					{
						const char16_t codeunit = static_cast<char16_t>(raw[i]) |
							(static_cast<char16_t>(raw[i + 1]) << 8);
						utf16.push_back(codeunit);
					}

					std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
					return converter.to_bytes(utf16);
				}

				if (size >= 2 && raw[0] == 0xFE && raw[1] == 0xFF)
				{
					std::u16string utf16;
					for (size_t i = 2; i + 1 < size; i += 2)
					{
						const char16_t codeunit = static_cast<char16_t>((raw[i] << 8) | raw[i + 1]);
						utf16.push_back(codeunit);
					}

					std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
					return converter.to_bytes(utf16);
				}

				if (size >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF)
					return std::string(bytes.begin() + 3, bytes.end());

				return std::string(bytes.begin(), bytes.end());
			}
			catch (const std::range_error &)
			{
				return std::nullopt;
			}
		}
	}

	std::string NativeFileSystem::canonicalize_path(const std::string &path) const
	{
		std::error_code error_code;
		const std::filesystem::path canonical =
			std::filesystem::weakly_canonical(std::filesystem::path(path), error_code);
		return error_code ? std::string() : canonical.generic_string();
	}

	std::string NativeFileSystem::parent_path(const std::string &path) const
	{
		return std::filesystem::path(path).parent_path().generic_string();
	}

	bool NativeFileSystem::path_exists(const std::string &path) const
	{
		std::error_code error_code;
		return std::filesystem::exists(std::filesystem::path(path), error_code) && !error_code;
	}

	bool NativeFileSystem::path_is_directory(const std::string &path) const
	{
		std::error_code error_code;
		return std::filesystem::is_directory(std::filesystem::path(path), error_code) && !error_code;
	}

	std::vector<RetroDirectoryEntry> NativeFileSystem::list_directory(const std::string &path) const
	{
		std::vector<RetroDirectoryEntry> entries;
		std::error_code error_code;
		for (const auto &entry : std::filesystem::directory_iterator(std::filesystem::path(path), error_code))
		{
			if (error_code)
				break;

			RetroDirectoryEntry directory_entry;
			directory_entry.path = entry.path().generic_string();
			directory_entry.name = entry.path().filename().string();
			directory_entry.is_directory = entry.is_directory(error_code) && !error_code;
			entries.push_back(std::move(directory_entry));
		}

		return entries;
	}

	std::optional<std::string> NativeFileSystem::read_text_file(const std::string &path) const
	{
		std::ifstream stream(std::filesystem::path(path), std::ios::binary);
		if (!stream)
			return std::nullopt;

		const std::vector<char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
		return decode_text_bytes(bytes);
	}

	std::optional<std::vector<std::uint8_t>> NativeFileSystem::read_binary_file(const std::string &path) const
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
