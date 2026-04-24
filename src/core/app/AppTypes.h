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
		std::array<bool, 5> lane_sustaining{};
		size_t loaded_stem_count = 0;
		bool has_chart = false;
		std::string chart_track_name;
		std::string chart_difficulty_name;
		double song_time_seconds = 0.0;
		double chart_beats_per_minute = 120.0;
		std::vector<ChartNoteView> visible_chart_notes;
		std::vector<ChartMeasureLineView> visible_measure_lines;
	};

	struct Color4
	{
		float r = 1.0f;
		float g = 1.0f;
		float b = 1.0f;
		float a = 1.0f;
	};

	struct RectF
	{
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
	};

	enum class HighwayInstrumentType
	{
		FiveFretGuitar,
		FiveLaneDrums,
		Bass,
		Vocals,
	};

	struct HighwayCameraView
	{
		float field_of_view_degrees = 0.0f;
		float pitch_degrees = 0.0f;
		float camera_height = 0.0f;
		float camera_distance = 0.0f;
		float visible_depth_seconds = 0.0f;
		float curve_amount = 0.0f;
	};

	inline HighwayCameraView make_default_guitar_camera_view()
	{
		HighwayCameraView camera;
		camera.field_of_view_degrees = 55.0f;
		camera.pitch_degrees = 28.0f;
		camera.camera_height = 2.5f;
		camera.camera_distance = 2.0f;
		camera.visible_depth_seconds = 3.0f;
		camera.curve_amount = 0.0f;
		return camera;
	}

	struct HighwayStyleView
	{
		Color4 lane_colors[5];
		Color4 lane_border_color;
		Color4 hit_line_color;
		Color4 sustain_color;
		Color4 measure_line_color;
		Color4 beat_line_color;
		Color4 background_top_color;
		Color4 background_bottom_color;
		float lane_gap = 0.02f;
		float note_width = 0.82f;
		float note_height = 0.18f;
		float sustain_width = 0.22f;
	};

	inline HighwayStyleView make_default_guitar_highway_style_view()
	{
		HighwayStyleView style;
		style.lane_colors[0] = {90.0f / 255.0f, 197.0f / 255.0f, 92.0f / 255.0f, 1.0f};
		style.lane_colors[1] = {210.0f / 255.0f, 62.0f / 255.0f, 62.0f / 255.0f, 1.0f};
		style.lane_colors[2] = {226.0f / 255.0f, 209.0f / 255.0f, 63.0f / 255.0f, 1.0f};
		style.lane_colors[3] = {65.0f / 255.0f, 117.0f / 255.0f, 220.0f / 255.0f, 1.0f};
		style.lane_colors[4] = {234.0f / 255.0f, 140.0f / 255.0f, 41.0f / 255.0f, 1.0f};
		style.lane_border_color = {48.0f / 255.0f, 58.0f / 255.0f, 74.0f / 255.0f, 1.0f};
		style.hit_line_color = {245.0f / 255.0f, 245.0f / 255.0f, 245.0f / 255.0f, 1.0f};
		style.sustain_color = {235.0f / 255.0f, 235.0f / 255.0f, 235.0f / 255.0f, 0.70f};
		style.measure_line_color = {235.0f / 255.0f, 240.0f / 255.0f, 250.0f / 255.0f, 0.85f};
		style.beat_line_color = {100.0f / 255.0f, 112.0f / 255.0f, 128.0f / 255.0f, 0.50f};
		style.background_top_color = {18.0f / 255.0f, 24.0f / 255.0f, 34.0f / 255.0f, 1.0f};
		style.background_bottom_color = {8.0f / 255.0f, 10.0f / 255.0f, 16.0f / 255.0f, 1.0f};
		return style;
	}

	struct HighwayNoteView
	{
		int lane = 0;
		float start_offset_seconds = 0.0f;
		float length_seconds = 0.0f;
		bool is_accent = false;
		bool is_star_power = false;
	};

	struct HighwayMeasureLineView
	{
		float offset_seconds = 0.0f;
		bool is_measure = false;
		bool is_strong = false;
	};

	struct InstrumentLaneView
	{
		HighwayInstrumentType instrument_type = HighwayInstrumentType::FiveFretGuitar;
		std::string instrument_label;
		bool is_active = true;
		bool is_muted = false;
		bool has_chart = false;
		float lane_center_x = 0.0f;
		float lane_width = 1.0f;
		float lane_depth_offset = 0.0f;
		std::array<bool, 5> lane_held{};
		std::array<bool, 5> lane_sustaining{};
		std::vector<HighwayNoteView> visible_notes;
		std::vector<HighwayMeasureLineView> visible_measure_lines;
	};

	struct HighwayWorldView
	{
		HighwayStyleView style;
		std::vector<InstrumentLaneView> lanes;
		int focused_lane_index = 0;
		float focus_blend = 1.0f;
	};

	struct PlayerHudView
	{
		std::string player_label;
		std::string status_message;
		double song_time_seconds = 0.0;
		bool failed = false;
	};

	struct PlayerGameplayView
	{
		RectF normalized_rect;
		HighwayCameraView camera;
		HighwayWorldView world;
		PlayerHudView hud;
	};

	struct GameplaySceneView
	{
		Color4 clear_color;
		std::vector<PlayerGameplayView> players;
	};
}
