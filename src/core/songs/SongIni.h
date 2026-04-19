#pragma once

#include "libretro_contract/RetroFileSystem.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace rhythmreplugged
{
	enum class SongIniFieldType
	{
		String,
		Int64,
		UInt32,
		Int32,
		UInt16,
		Int16,
		Bool,
		Int64Pair,
	};

	class SongIniMetadata
	{
	public:
		bool try_get_string(std::string_view key, const std::string *&value) const;
		bool try_get_int64(std::string_view key, std::int64_t &value) const;
		bool try_get_uint32(std::string_view key, std::uint32_t &value) const;
		bool try_get_int32(std::string_view key, std::int32_t &value) const;
		bool try_get_uint16(std::string_view key, std::uint16_t &value) const;
		bool try_get_int16(std::string_view key, std::int16_t &value) const;
		bool try_get_bool(std::string_view key, bool &value) const;
		bool try_get_int64_pair(std::string_view key, std::pair<std::int64_t, std::int64_t> &value) const;

		void set_string(std::string key, std::string value);
		void set_int64(std::string key, std::int64_t value);
		void set_uint32(std::string key, std::uint32_t value);
		void set_int32(std::string key, std::int32_t value);
		void set_uint16(std::string key, std::uint16_t value);
		void set_int16(std::string key, std::int16_t value);
		void set_bool(std::string key, bool value);
		void set_int64_pair(std::string key, std::pair<std::int64_t, std::int64_t> value);

	private:
		std::unordered_map<std::string, std::string> strings_;
		std::unordered_map<std::string, std::int64_t> int64s_;
		std::unordered_map<std::string, std::uint32_t> uint32s_;
		std::unordered_map<std::string, std::int32_t> int32s_;
		std::unordered_map<std::string, std::uint16_t> uint16s_;
		std::unordered_map<std::string, std::int16_t> int16s_;
		std::unordered_map<std::string, bool> bools_;
		std::unordered_map<std::string, std::pair<std::int64_t, std::int64_t>> int64_pairs_;
	};

	struct SongIniParseResult
	{
		SongIniMetadata metadata;
		bool has_song_section = false;
		bool parsed_successfully = false;
		int parse_error_line = 0;
		std::string error_message;
	};

	struct SongMetadataView
	{
		std::string name;
		std::string artist;
		std::string album;
		std::string charter;
		std::string cover_art_path;
	};

	SongIniParseResult parse_song_ini(const IRetroFileSystem &file_system, const std::string &song_ini_path);
	SongMetadataView make_song_metadata_view(const SongIniMetadata &metadata, const std::string &folder_name);
	std::string resolve_cover_art_path(const IRetroFileSystem &file_system, const std::string &song_directory, const SongIniMetadata &metadata);
}
