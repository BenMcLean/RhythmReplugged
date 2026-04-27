#include "core/menu/DifficultySelectMenu.h"

#include <algorithm>
#include <array>

namespace rhythmreplugged::core
{
	namespace
	{
		constexpr size_t kBytesPerMegabyte = 1024u * 1024u;

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

	void DifficultySelectMenu::open(
		std::string song_title,
		std::string song_subtitle,
		const std::vector<DifficultyOption> &available_difficulties,
		const GameplayOptions &options)
	{
		cached_view_ = {};
		song_title_ = std::move(song_title);
		song_subtitle_ = std::move(song_subtitle);
		status_message_.clear();
		available_difficulties_ = available_difficulties;
		if (available_difficulties_.empty())
		{
			available_difficulties_.reserve(kDifficultyDefinitions.size());
			for (const DifficultyDefinition &definition : kDifficultyDefinitions)
				available_difficulties_.push_back(definition.difficulty);
		}
		selected_index_ = default_index_for(options.difficulty());
		rebuild_view();
	}

	bool DifficultySelectMenu::move_selection(int delta)
	{
		const int previous_index = selected_index_;
		selected_index_ = std::clamp(selected_index_ + delta, 0, static_cast<int>(available_difficulties_.size()) - 1);
		rebuild_view();
		return selected_index_ != previous_index;
	}

	bool DifficultySelectMenu::set_selected_index(int index)
	{
		if (index < 0 || index >= static_cast<int>(available_difficulties_.size()))
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

	void DifficultySelectMenu::set_preload_progress(
		PreloadPhase preload_phase,
		float preload_progress,
		size_t preload_processed_bytes,
		size_t preload_total_bytes,
		size_t completed_stem_count,
		size_t total_stem_count,
		size_t completed_read_file_count,
		size_t total_read_file_count)
	{
		cached_view_.preload_phase = preload_phase;
		cached_view_.preload_progress = preload_progress;
		cached_view_.preload_processed_megabytes = preload_processed_bytes / kBytesPerMegabyte;
		cached_view_.preload_total_megabytes = (preload_total_bytes + kBytesPerMegabyte - 1) / kBytesPerMegabyte;
		cached_view_.completed_stem_count = completed_stem_count;
		cached_view_.total_stem_count = total_stem_count;
		cached_view_.completed_read_file_count = completed_read_file_count;
		cached_view_.total_read_file_count = total_read_file_count;
	}

	void DifficultySelectMenu::apply_to(GameplayOptions &options) const
	{
		options.set_difficulty(available_difficulties_[static_cast<size_t>(selected_index_)]);
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
		cached_view_.entries.reserve(available_difficulties_.size());
		for (DifficultyOption difficulty : available_difficulties_)
		{
			DifficultyListItem item;
			item.difficulty = difficulty;
			for (const DifficultyDefinition &definition : kDifficultyDefinitions)
			{
				if (definition.difficulty == difficulty)
				{
					item.label = definition.label;
					break;
				}
			}
			cached_view_.entries.push_back(std::move(item));
		}
	}

	int DifficultySelectMenu::default_index_for(DifficultyOption difficulty) const
	{
		for (int index = 0; index < static_cast<int>(available_difficulties_.size()); ++index)
		{
			if (available_difficulties_[static_cast<size_t>(index)] == difficulty)
				return index;
		}

		return available_difficulties_.empty() ? 0 : std::min(1, static_cast<int>(available_difficulties_.size()) - 1);
	}
}
