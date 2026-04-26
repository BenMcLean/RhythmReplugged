#pragma once

#include "core/app/AppTypes.h"

#include <string>
#include <vector>

namespace rhythmreplugged::core
{
	class InstrumentSelectMenu
	{
	public:
		void open(
			std::string song_title,
			std::string song_subtitle,
			const std::vector<InstrumentOption> &available_instruments,
			const GameplayOptions &options);
		bool move_selection(int delta);
		bool set_selected_index(int index);
		void clear_status_message();
		void set_status_message(std::string message);
		void set_preload_progress(
			PreloadPhase preload_phase,
			float preload_progress,
			size_t preload_processed_bytes,
			size_t preload_total_bytes,
			size_t completed_stem_count,
			size_t total_stem_count,
			size_t completed_read_file_count,
			size_t total_read_file_count);
		void apply_to(GameplayOptions &options) const;
		const InstrumentSelectView &view() const;

	private:
		void rebuild_view();
		static std::string label_for(InstrumentOption instrument);
		int default_index_for(InstrumentOption instrument) const;

		std::string song_title_;
		std::string song_subtitle_;
		std::string status_message_;
		std::vector<InstrumentOption> available_instruments_;
		int selected_index_ = 0;
		mutable InstrumentSelectView cached_view_;
	};
}
