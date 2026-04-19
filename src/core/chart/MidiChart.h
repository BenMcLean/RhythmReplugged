#pragma once

#include "libretro_contract/RetroFileSystem.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rhythmreplugged
{
	struct MidiChartNote
	{
		int lane = 0;
		int tick = 0;
		int end_tick = 0;
		double start_seconds = 0.0;
		double end_seconds = 0.0;
	};

	struct MidiChartTempoChange
	{
		int tick = 0;
		double time_seconds = 0.0;
		double beats_per_minute = 120.0;
	};

	struct MidiChartMeasureLine
	{
		enum class Kind
		{
			Measure,
			Strong,
			Weak,
		};

		int tick = 0;
		double time_seconds = 0.0;
		Kind kind = Kind::Weak;
	};

	class MidiChart
	{
	public:
		bool load(const IRetroFileSystem &file_system, const std::string &song_directory, std::string &error_message);
		void clear();
		bool is_loaded() const;
		std::string_view track_name() const;
		std::string_view difficulty_name() const;
		const std::vector<MidiChartNote> &notes() const;
		const std::vector<MidiChartTempoChange> &tempo_changes() const;
		const std::vector<MidiChartMeasureLine> &measure_lines() const;
		double duration_seconds() const;
		double bpm_at_time(double song_time_seconds) const;
		std::vector<MidiChartNote> collect_visible_notes(double song_time_seconds,
			double lookbehind_seconds,
			double lookahead_seconds) const;
		std::vector<MidiChartMeasureLine> collect_visible_measure_lines(double song_time_seconds,
			double lookbehind_seconds,
			double lookahead_seconds) const;

	private:
		static std::string find_case_insensitive_file(const IRetroFileSystem &file_system,
			const std::string &directory_path,
			std::string_view file_name);

		std::string track_name_;
		std::string difficulty_name_;
		std::vector<MidiChartNote> notes_;
		std::vector<MidiChartTempoChange> tempo_changes_;
		std::vector<MidiChartMeasureLine> measure_lines_;
		double duration_seconds_ = 0.0;
	};
}
