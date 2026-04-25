#include "core/menu/DifficultySelectMenu.h"

#include <algorithm>
#include <array>

namespace rhythmreplugged::core
{
	namespace
	{
		struct DifficultyDefinition
		{
			DifficultyOption difficulty;
			const char *label;
		};

		constexpr std::array<DifficultyDefinition, 4> kDifficultyDefinitions = {{
			{DifficultyOption::Easy, "Easy"},
			{DifficultyOption::Medium, "Medium"},
			{DifficultyOption::Hard, "Hard"},
			{DifficultyOption::Expert, "Expert"},
		}};
	}

	void DifficultySelectMenu::open(std::string song_title, std::string song_subtitle, const GameplayOptions &options)
	{
		song_title_ = std::move(song_title);
		song_subtitle_ = std::move(song_subtitle);
		status_message_.clear();
		selected_index_ = default_index_for(options.difficulty());
		rebuild_view();
	}

	bool DifficultySelectMenu::move_selection(int delta)
	{
		const int previous_index = selected_index_;
		selected_index_ = std::clamp(selected_index_ + delta, 0, static_cast<int>(kDifficultyDefinitions.size()) - 1);
		rebuild_view();
		return selected_index_ != previous_index;
	}

	bool DifficultySelectMenu::set_selected_index(int index)
	{
		if (index < 0 || index >= static_cast<int>(kDifficultyDefinitions.size()))
			return false;

		selected_index_ = index;
		rebuild_view();
		return true;
	}

	void DifficultySelectMenu::clear_status_message()
	{
		status_message_.clear();
		rebuild_view();
	}

	void DifficultySelectMenu::set_status_message(std::string message)
	{
		status_message_ = std::move(message);
		rebuild_view();
	}

	void DifficultySelectMenu::apply_to(GameplayOptions &options) const
	{
		options.set_difficulty(kDifficultyDefinitions[static_cast<size_t>(selected_index_)].difficulty);
	}

	const DifficultySelectView &DifficultySelectMenu::view() const
	{
		return cached_view_;
	}

	void DifficultySelectMenu::rebuild_view()
	{
		cached_view_.song_title = song_title_;
		cached_view_.song_subtitle = song_subtitle_;
		cached_view_.status_message = status_message_;
		cached_view_.selected_index = selected_index_;
		cached_view_.entries.clear();
		cached_view_.entries.reserve(kDifficultyDefinitions.size());
		for (const DifficultyDefinition &definition : kDifficultyDefinitions)
		{
			DifficultyListItem item;
			item.difficulty = definition.difficulty;
			item.label = definition.label;
			cached_view_.entries.push_back(std::move(item));
		}
	}

	int DifficultySelectMenu::default_index_for(DifficultyOption difficulty)
	{
		for (int index = 0; index < static_cast<int>(kDifficultyDefinitions.size()); ++index)
		{
			if (kDifficultyDefinitions[static_cast<size_t>(index)].difficulty == difficulty)
				return index;
		}

		return 1;
	}
}
