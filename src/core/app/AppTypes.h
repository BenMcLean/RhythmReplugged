#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace rhythmreplugged
{
	inline constexpr int kAppFramesPerSecond = 60;

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
		struct ChartNoteView
		{
			int lane = 0;
			float start_offset_seconds = 0.0f;
			float length_seconds = 0.0f;
		};

		struct ChartMeasureLineView
		{
			float offset_seconds = 0.0f;
			bool is_measure = false;
			bool is_strong = false;
		};

		std::string song_title;
		std::string song_artist;
		std::string status_message;
		bool has_guitar = false;
		bool guitar_muted = false;
		std::array<bool, 5> lane_held{};
		size_t loaded_stem_count = 0;
		bool has_chart = false;
		std::string chart_track_name;
		std::string chart_difficulty_name;
		double song_time_seconds = 0.0;
		double chart_beats_per_minute = 120.0;
		std::vector<ChartNoteView> visible_chart_notes;
		std::vector<ChartMeasureLineView> visible_measure_lines;
	};
}
