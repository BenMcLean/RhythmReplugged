#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rhythmreplugged
{
	inline constexpr int kRetroFramesPerSecond = 60;

	enum class AppMode
	{
		SongBrowser,
		PrototypePlayer,
	};

	struct SongListItem
	{
		std::string label;
		std::string subtitle;
		std::string cover_art_path;
		std::string error_message;
		bool is_folder = false;
		bool is_parent = false;
		bool is_song = false;
		bool is_valid_song = false;
	};

	struct SongBrowserView
	{
		std::string root_path;
		std::string current_path;
		std::string status_message;
		std::vector<SongListItem> entries;
		int selected_index = 0;
	};

	struct PrototypePlayerView
	{
		std::string song_title;
		std::string song_artist;
		std::string status_message;
		bool guitar_muted = false;
	};
}
