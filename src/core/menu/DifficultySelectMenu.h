#pragma once

#include "core/app/AppTypes.h"

#include <string>

namespace rhythmreplugged::core
{
	class DifficultySelectMenu
	{
	public:
		void open(std::string song_title, std::string song_subtitle, const GameplayOptions &options);
		bool move_selection(int delta);
		bool set_selected_index(int index);
		void clear_status_message();
		void set_status_message(std::string message);
		void apply_to(GameplayOptions &options) const;
		const DifficultySelectView &view() const;

	private:
		void rebuild_view();
		static int default_index_for(DifficultyOption difficulty);

		std::string song_title_;
		std::string song_subtitle_;
		std::string status_message_;
		int selected_index_ = 1;
		mutable DifficultySelectView cached_view_;
	};
}
