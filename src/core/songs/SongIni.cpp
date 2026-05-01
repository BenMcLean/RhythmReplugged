#include "core/songs/SongIni.h"

#include <array>
#include <charconv>
#include <cctype>

namespace rhythmreplugged::core
{
	namespace
	{
		struct SongIniFieldDefinition
		{
			const char *input_name;
			const char *output_name;
			SongIniFieldType type;
		};

#define RHYTHM_REPLUGGED_SONG_INI_FIELDS(X) \
		X("album", "album", SongIniFieldType::String) \
		X("album_track", "album_track", SongIniFieldType::Int32) \
		X("artist", "artist", SongIniFieldType::String) \
		X("background", "background", SongIniFieldType::String) \
		X("bass_type", "bass_type", SongIniFieldType::UInt32) \
		X("charter", "charter", SongIniFieldType::String) \
		X("charter_bass", "charter_bass", SongIniFieldType::String) \
		X("charter_drums", "charter_drums", SongIniFieldType::String) \
		X("charter_elite_drums", "charter_elite_drums", SongIniFieldType::String) \
		X("charter_guitar", "charter_guitar", SongIniFieldType::String) \
		X("charter_keys", "charter_keys", SongIniFieldType::String) \
		X("charter_lower_diff", "charter_lower_diff", SongIniFieldType::String) \
		X("charter_pro_bass", "charter_pro_bass", SongIniFieldType::String) \
		X("charter_pro_keys", "charter_pro_keys", SongIniFieldType::String) \
		X("charter_pro_guitar", "charter_pro_guitar", SongIniFieldType::String) \
		X("charter_venue", "charter_venue", SongIniFieldType::String) \
		X("charter_vocals", "charter_vocals", SongIniFieldType::String) \
		X("count", "count", SongIniFieldType::UInt32) \
		X("cover", "cover", SongIniFieldType::String) \
		X("covered_by", "covered_by", SongIniFieldType::String) \
		X("credit_album_art_by", "credit_album_art_designed_by", SongIniFieldType::String) \
		X("credit_album_art_designed_by", "credit_album_art_designed_by", SongIniFieldType::String) \
		X("credit_album_cover", "credit_album_art_designed_by", SongIniFieldType::String) \
		X("credit_arranged_by", "credit_arranged_by", SongIniFieldType::String) \
		X("credit_composed_by", "credit_composed_by", SongIniFieldType::String) \
		X("credit_courtesy_of", "credit_courtesy_of", SongIniFieldType::String) \
		X("credit_engineered_by", "credit_engineered_by", SongIniFieldType::String) \
		X("credit_license", "credit_license", SongIniFieldType::String) \
		X("credit_mastered_by", "credit_mastered_by", SongIniFieldType::String) \
		X("credit_mixed_by", "credit_mixed_by", SongIniFieldType::String) \
		X("credit_other", "credit_other", SongIniFieldType::String) \
		X("credit_performed_by", "credit_performed_by", SongIniFieldType::String) \
		X("credit_produced_by", "credit_produced_by", SongIniFieldType::String) \
		X("credit_published_by", "credit_published_by", SongIniFieldType::String) \
		X("credit_written_by", "credit_written_by", SongIniFieldType::String) \
		X("dance_type", "dance_type", SongIniFieldType::UInt32) \
		X("delay", "delay", SongIniFieldType::Int64) \
		X("diff_band", "diff_band", SongIniFieldType::Int32) \
		X("diff_bass", "diff_bass", SongIniFieldType::Int32) \
		X("diff_bass_real", "diff_bass_real", SongIniFieldType::Int32) \
		X("diff_bass_real_22", "diff_bass_real_22", SongIniFieldType::Int32) \
		X("diff_bassghl", "diff_bassghl", SongIniFieldType::Int32) \
		X("diff_dance", "diff_dance", SongIniFieldType::Int32) \
		X("diff_drums", "diff_drums", SongIniFieldType::Int32) \
		X("diff_drums_real", "diff_drums_real", SongIniFieldType::Int32) \
		X("diff_drums_real_ps", "diff_drums_real_ps", SongIniFieldType::Int32) \
		X("diff_elite_drums", "diff_elite_drums", SongIniFieldType::Int32) \
		X("diff_guitar", "diff_guitar", SongIniFieldType::Int32) \
		X("diff_guitar_coop", "diff_guitar_coop", SongIniFieldType::Int32) \
		X("diff_guitar_coop_ghl", "diff_guitar_coop_ghl", SongIniFieldType::Int32) \
		X("diff_guitar_real", "diff_guitar_real", SongIniFieldType::Int32) \
		X("diff_guitar_real_22", "diff_guitar_real_22", SongIniFieldType::Int32) \
		X("diff_guitarghl", "diff_guitarghl", SongIniFieldType::Int32) \
		X("diff_keys", "diff_keys", SongIniFieldType::Int32) \
		X("diff_keys_real", "diff_keys_real", SongIniFieldType::Int32) \
		X("diff_keys_real_ps", "diff_keys_real_ps", SongIniFieldType::Int32) \
		X("diff_rhythm", "diff_rhythm", SongIniFieldType::Int32) \
		X("diff_rhythm_ghl", "diff_rhythm_ghl", SongIniFieldType::Int32) \
		X("diff_vocals", "diff_vocals", SongIniFieldType::Int32) \
		X("diff_vocals_harm", "diff_vocals_harm", SongIniFieldType::Int32) \
		X("drum_fallback_blue", "drum_fallback_blue", SongIniFieldType::Bool) \
		X("eighthnote_hopo", "eighthnote_hopo", SongIniFieldType::Bool) \
		X("end_events", "end_events", SongIniFieldType::Bool) \
		X("five_lane_drums", "five_lane_drums", SongIniFieldType::Bool) \
		X("frets", "frets", SongIniFieldType::String) \
		X("genre", "genre", SongIniFieldType::String) \
		X("guitar_type", "guitar_type", SongIniFieldType::UInt32) \
		X("hopo_frequency", "hopo_frequency", SongIniFieldType::Int64) \
		X("hopofreq", "hopofreq", SongIniFieldType::Int32) \
		X("icon", "icon", SongIniFieldType::String) \
		X("keys_type", "keys_type", SongIniFieldType::UInt32) \
		X("kit_type", "kit_type", SongIniFieldType::UInt32) \
		X("link_bandcamp", "link_bandcamp", SongIniFieldType::String) \
		X("link_bluesky", "link_bluesky", SongIniFieldType::String) \
		X("link_facebook", "link_facebook", SongIniFieldType::String) \
		X("link_instagram", "link_instagram", SongIniFieldType::String) \
		X("link_spotify", "link_spotify", SongIniFieldType::String) \
		X("link_twitter", "link_twitter", SongIniFieldType::String) \
		X("link_other", "link_other", SongIniFieldType::String) \
		X("link_youtube", "link_youtube", SongIniFieldType::String) \
		X("loading_phrase", "loading_phrase", SongIniFieldType::String) \
		X("location", "location", SongIniFieldType::String) \
		X("lyrics", "lyrics", SongIniFieldType::Bool) \
		X("modchart", "modchart", SongIniFieldType::Bool) \
		X("multiplier_note", "multiplier_note", SongIniFieldType::Int32) \
		X("name", "name", SongIniFieldType::String) \
		X("playlist", "playlist", SongIniFieldType::String) \
		X("playlist_track", "playlist_track", SongIniFieldType::Int32) \
		X("preview", "preview", SongIniFieldType::Int64Pair) \
		X("preview_end_time", "preview_end_time", SongIniFieldType::Int64) \
		X("preview_start_time", "preview_start_time", SongIniFieldType::Int64) \
		X("pro_drum", "pro_drums", SongIniFieldType::Bool) \
		X("pro_drums", "pro_drums", SongIniFieldType::Bool) \
		X("rating", "rating", SongIniFieldType::UInt32) \
		X("real_bass_22_tuning", "real_bass_22_tuning", SongIniFieldType::UInt32) \
		X("real_bass_tuning", "real_bass_tuning", SongIniFieldType::UInt32) \
		X("real_guitar_22_tuning", "real_guitar_22_tuning", SongIniFieldType::UInt32) \
		X("real_guitar_tuning", "real_guitar_tuning", SongIniFieldType::UInt32) \
		X("real_keys_lane_count_left", "real_keys_lane_count_left", SongIniFieldType::UInt32) \
		X("real_keys_lane_count_right", "real_keys_lane_count_right", SongIniFieldType::UInt32) \
		X("song_length", "song_length", SongIniFieldType::Int64) \
		X("star_power_note", "multiplier_note", SongIniFieldType::Int32) \
		X("sub_genre", "sub_genre", SongIniFieldType::String) \
		X("sub_playlist", "sub_playlist", SongIniFieldType::String) \
		X("sustain_cutoff_threshold", "sustain_cutoff_threshold", SongIniFieldType::Int64) \
		X("tags", "tags", SongIniFieldType::String) \
		X("track", "album_track", SongIniFieldType::Int32) \
		X("tuning_offset_cents", "tuning_offset_cents", SongIniFieldType::Int16) \
		X("tutorial", "tutorial", SongIniFieldType::Bool) \
		X("unlock_completed", "unlock_completed", SongIniFieldType::UInt32) \
		X("unlock_id", "unlock_id", SongIniFieldType::String) \
		X("unlock_require", "unlock_require", SongIniFieldType::String) \
		X("unlock_text", "unlock_text", SongIniFieldType::String) \
		X("version", "version", SongIniFieldType::UInt32) \
		X("video", "video", SongIniFieldType::String) \
		X("video_end_time", "video_end_time", SongIniFieldType::Int64) \
		X("video_loop", "video_loop", SongIniFieldType::Bool) \
		X("video_start_time", "video_start_time", SongIniFieldType::Int64) \
		X("vocal_gender", "vocal_gender", SongIniFieldType::UInt32) \
		X("vocal_scroll_speed", "vocal_scroll_speed", SongIniFieldType::Int16) \
		X("year", "year", SongIniFieldType::String)

		std::string to_lower_copy(std::string_view text)
		{
			std::string lowered;
			lowered.reserve(text.size());
			for (char ch : text)
				lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
			return lowered;
		}

		std::string trim_copy(std::string_view text)
		{
			size_t start = 0;
			while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0)
				++start;

			size_t end = text.size();
			while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0)
				--end;

			return std::string(text.substr(start, end - start));
		}

		bool is_comment_line(std::string_view trimmed)
		{
			return !trimmed.empty() &&
				(trimmed.front() == ';' || trimmed.front() == '#' ||
					(trimmed.size() >= 2 && trimmed[0] == '/' && trimmed[1] == '/'));
		}

		bool has_supported_image_extension(std::string_view file_name)
		{
			static constexpr std::array<std::string_view, 8> k_image_extensions = {
				".png", ".jpg", ".jpeg", ".tga", ".bmp", ".psd", ".gif", ".pic"};

			const std::string lowered_name = to_lower_copy(file_name);
			for (std::string_view extension : k_image_extensions)
			{
				if (lowered_name.size() >= extension.size() &&
					lowered_name.compare(lowered_name.size() - extension.size(), extension.size(), extension) == 0)
				{
					return true;
				}
			}

			return false;
		}

		bool is_album_cover_file(std::string_view lowered_name)
		{
			return lowered_name == "album.png" ||
				lowered_name == "album.jpg" ||
				lowered_name == "album.jpeg" ||
				lowered_name == "album.tga" ||
				lowered_name == "album.bmp" ||
				lowered_name == "album.psd" ||
				lowered_name == "album.gif" ||
				lowered_name == "album.pic";
		}

		template <typename T>
		bool try_parse_integer(std::string_view text, T &value)
		{
			const std::string trimmed = trim_copy(text);
			if (trimmed.empty())
				return false;

			const auto result = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), value);
			return result.ec == std::errc{} && result.ptr == trimmed.data() + trimmed.size();
		}

		bool try_parse_bool(std::string_view text)
		{
			const std::string lowered = to_lower_copy(trim_copy(text));
			return lowered == "1" || lowered == "true";
		}

		std::pair<std::int64_t, std::int64_t> parse_int64_pair(std::string_view text)
		{
			const std::string trimmed = trim_copy(text);
			const size_t first_separator = trimmed.find_first_of(" ,\t");
			const std::string first_token = trimmed.substr(0, first_separator);

			std::int64_t first = -1;
			if (!try_parse_integer(first_token, first))
				return {-1, -1};

			if (first_separator == std::string::npos)
				return {first, -1};

			size_t second_start = first_separator;
			while (second_start < trimmed.size() &&
				(std::isspace(static_cast<unsigned char>(trimmed[second_start])) != 0 || trimmed[second_start] == ','))
			{
				++second_start;
			}

			std::int64_t second = -1;
			if (second_start < trimmed.size())
				try_parse_integer(std::string_view(trimmed).substr(second_start), second);

			return {first, second};
		}

		const auto &song_ini_field_lookup()
		{
			static const std::unordered_map<std::string, SongIniFieldDefinition> lookup = []()
			{
				std::unordered_map<std::string, SongIniFieldDefinition> map;
#define RHYTHM_REPLUGGED_DEFINE_FIELD(input_name, output_name, type) \
				map.emplace(input_name, SongIniFieldDefinition{input_name, output_name, type});
				RHYTHM_REPLUGGED_SONG_INI_FIELDS(RHYTHM_REPLUGGED_DEFINE_FIELD)
#undef RHYTHM_REPLUGGED_DEFINE_FIELD
				return map;
			}();

			return lookup;
		}

		void assign_song_ini_value(SongIniMetadata &metadata, std::string_view name, std::string_view value)
		{
			const auto &lookup = song_ini_field_lookup();
			const auto it = lookup.find(to_lower_copy(name));
			if (it == lookup.end())
				return;

			const SongIniFieldDefinition &definition = it->second;
			switch (definition.type)
			{
			case SongIniFieldType::String:
				metadata.set_string(definition.output_name, trim_copy(value));
				return;
			case SongIniFieldType::Int64:
			{
				std::int64_t parsed = 0;
				try_parse_integer(value, parsed);
				metadata.set_int64(definition.output_name, parsed);
				return;
			}
			case SongIniFieldType::UInt32:
			{
				std::uint32_t parsed = 0;
				try_parse_integer(value, parsed);
				metadata.set_uint32(definition.output_name, parsed);
				return;
			}
			case SongIniFieldType::Int32:
			{
				std::int32_t parsed = 0;
				try_parse_integer(value, parsed);
				metadata.set_int32(definition.output_name, parsed);
				return;
			}
			case SongIniFieldType::UInt16:
			{
				std::uint16_t parsed = 0;
				try_parse_integer(value, parsed);
				metadata.set_uint16(definition.output_name, parsed);
				return;
			}
			case SongIniFieldType::Int16:
			{
				std::int16_t parsed = 0;
				try_parse_integer(value, parsed);
				metadata.set_int16(definition.output_name, parsed);
				return;
			}
			case SongIniFieldType::Bool:
				metadata.set_bool(definition.output_name, try_parse_bool(value));
				return;
			case SongIniFieldType::Int64Pair:
				metadata.set_int64_pair(definition.output_name, parse_int64_pair(value));
				return;
			}
		}

		bool parse_last_song_section(std::string_view ini_text, SongIniParseResult &result)
		{
			size_t last_song_start = std::string_view::npos;
			int last_song_line = 0;

			size_t line_start = 0;
			int line_number = 1;
			while (line_start <= ini_text.size())
			{
				const size_t line_end = ini_text.find('\n', line_start);
				const size_t actual_end = line_end == std::string_view::npos ? ini_text.size() : line_end;
				std::string_view line = ini_text.substr(line_start, actual_end - line_start);
				if (!line.empty() && line.back() == '\r')
					line.remove_suffix(1);

				const std::string trimmed = trim_copy(line);
				if (trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']' &&
					to_lower_copy(trimmed) == "[song]")
				{
					last_song_start = line_start;
					last_song_line = line_number;
				}

				if (line_end == std::string_view::npos)
					break;

				line_start = line_end + 1;
				++line_number;
			}

			if (last_song_start == std::string_view::npos)
				return false;

			result.has_song_section = true;
			result.parsed_successfully = true;

			std::string last_name;
			line_start = last_song_start;
			line_number = last_song_line;

			while (line_start <= ini_text.size())
			{
				const size_t line_end = ini_text.find('\n', line_start);
				const size_t actual_end = line_end == std::string_view::npos ? ini_text.size() : line_end;
				std::string_view line = ini_text.substr(line_start, actual_end - line_start);
				if (!line.empty() && line.back() == '\r')
					line.remove_suffix(1);

				const std::string trimmed = trim_copy(line);
				if (line_number == last_song_line)
				{
					last_name.clear();
				}
				else if (trimmed.empty() || is_comment_line(trimmed))
				{
					last_name.clear();
				}
				else if (trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']')
				{
					break;
				}
				else if (!line.empty() && std::isspace(static_cast<unsigned char>(line.front())) != 0)
				{
					if (!last_name.empty())
						assign_song_ini_value(result.metadata, last_name, trimmed);
				}
				else
				{
					const size_t delimiter = line.find_first_of("=:");
					if (delimiter == std::string_view::npos)
					{
						result.parsed_successfully = false;
						result.parse_error_line = line_number;
						result.error_message = "song.ini has a parse error near line " + std::to_string(line_number) + ".";
						return true;
					}

					last_name = trim_copy(line.substr(0, delimiter));
					assign_song_ini_value(result.metadata, last_name, line.substr(delimiter + 1));
				}

				if (line_end == std::string_view::npos)
					break;

				line_start = line_end + 1;
				++line_number;
			}

			return true;
		}
	}

	bool SongIniMetadata::try_get_string(std::string_view key, const std::string *&value) const
	{
		const auto it = strings_.find(to_lower_copy(key));
		if (it == strings_.end())
			return false;
		value = &it->second;
		return true;
	}

	bool SongIniMetadata::try_get_int64(std::string_view key, std::int64_t &value) const
	{
		const auto it = int64s_.find(to_lower_copy(key));
		if (it == int64s_.end())
			return false;
		value = it->second;
		return true;
	}

	bool SongIniMetadata::try_get_uint32(std::string_view key, std::uint32_t &value) const
	{
		const auto it = uint32s_.find(to_lower_copy(key));
		if (it == uint32s_.end())
			return false;
		value = it->second;
		return true;
	}

	bool SongIniMetadata::try_get_int32(std::string_view key, std::int32_t &value) const
	{
		const auto it = int32s_.find(to_lower_copy(key));
		if (it == int32s_.end())
			return false;
		value = it->second;
		return true;
	}

	bool SongIniMetadata::try_get_uint16(std::string_view key, std::uint16_t &value) const
	{
		const auto it = uint16s_.find(to_lower_copy(key));
		if (it == uint16s_.end())
			return false;
		value = it->second;
		return true;
	}

	bool SongIniMetadata::try_get_int16(std::string_view key, std::int16_t &value) const
	{
		const auto it = int16s_.find(to_lower_copy(key));
		if (it == int16s_.end())
			return false;
		value = it->second;
		return true;
	}

	bool SongIniMetadata::try_get_bool(std::string_view key, bool &value) const
	{
		const auto it = bools_.find(to_lower_copy(key));
		if (it == bools_.end())
			return false;
		value = it->second;
		return true;
	}

	bool SongIniMetadata::try_get_int64_pair(std::string_view key, std::pair<std::int64_t, std::int64_t> &value) const
	{
		const auto it = int64_pairs_.find(to_lower_copy(key));
		if (it == int64_pairs_.end())
			return false;
		value = it->second;
		return true;
	}

	void SongIniMetadata::set_string(std::string key, std::string value)
	{
		strings_[to_lower_copy(key)] = std::move(value);
	}

	void SongIniMetadata::set_int64(std::string key, std::int64_t value)
	{
		int64s_[to_lower_copy(key)] = value;
	}

	void SongIniMetadata::set_uint32(std::string key, std::uint32_t value)
	{
		uint32s_[to_lower_copy(key)] = value;
	}

	void SongIniMetadata::set_int32(std::string key, std::int32_t value)
	{
		int32s_[to_lower_copy(key)] = value;
	}

	void SongIniMetadata::set_uint16(std::string key, std::uint16_t value)
	{
		uint16s_[to_lower_copy(key)] = value;
	}

	void SongIniMetadata::set_int16(std::string key, std::int16_t value)
	{
		int16s_[to_lower_copy(key)] = value;
	}

	void SongIniMetadata::set_bool(std::string key, bool value)
	{
		bools_[to_lower_copy(key)] = value;
	}

	void SongIniMetadata::set_int64_pair(std::string key, std::pair<std::int64_t, std::int64_t> value)
	{
		int64_pairs_[to_lower_copy(key)] = value;
	}

	SongIniParseResult parse_song_ini(const ::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system, const std::string &song_ini_path)
	{
		SongIniParseResult result;

		const auto text = file_system.read_text_file(song_ini_path);
		if (!text.has_value())
		{
			result.error_message = "Could not read song.ini.";
			return result;
		}

		if (!parse_last_song_section(*text, result))
		{
			result.error_message = "song.ini is missing a [song] section.";
			return result;
		}

		return result;
	}

	std::string resolve_cover_art_path(const ::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system, const std::string &song_directory, const SongIniMetadata &metadata)
	{
		const std::string *cover = nullptr;
		std::string lowered_cover_name;
		if (metadata.try_get_string("cover", cover) && cover != nullptr && !cover->empty())
		{
			const std::string direct_path = song_directory + "/" + *cover;
			if (file_system.path_exists(direct_path))
				return direct_path;
			lowered_cover_name = to_lower_copy(*cover);
		}

		std::string matched_cover_path;
		std::string fallback_album_cover_path;
		for (const ::rhythmreplugged::frontend_contract::RetroDirectoryEntry &entry : file_system.list_directory(song_directory))
		{
			if (entry.is_directory || !has_supported_image_extension(entry.name))
				continue;

			const std::string lowered_name = to_lower_copy(entry.name);
			if (!lowered_cover_name.empty() && lowered_name == lowered_cover_name)
				matched_cover_path = entry.path;
			else if (fallback_album_cover_path.empty() && is_album_cover_file(lowered_name))
				fallback_album_cover_path = entry.path;
		}

		if (!matched_cover_path.empty())
			return matched_cover_path;

		return fallback_album_cover_path;
	}

	SongMetadataView make_song_metadata_view(const SongIniMetadata &metadata, const std::string &folder_name)
	{
		SongMetadataView view;
		view.name = folder_name;

		const std::string *value = nullptr;
		if (metadata.try_get_string("name", value) && value != nullptr && !value->empty())
			view.name = *value;

		if (metadata.try_get_string("artist", value) && value != nullptr)
			view.artist = *value;

		if (metadata.try_get_string("album", value) && value != nullptr)
			view.album = *value;

		if (metadata.try_get_string("charter", value) && value != nullptr)
			view.charter = *value;
		else if (metadata.try_get_string("frets", value) && value != nullptr)
			view.charter = *value;

		return view;
	}
}
