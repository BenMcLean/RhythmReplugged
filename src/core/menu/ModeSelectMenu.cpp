#include "core/menu/ModeSelectMenu.h"

#include <algorithm>
#include <utility>

namespace rhythmreplugged::core
{
	namespace
	{
		constexpr size_t kBytesPerMegabyte = 1024u * 1024u;
	}

	void ModeSelectMenu::open(
		std::string song_title,
		std::string song_subtitle,
		const std::vector<GameplayMode> &available_modes,
		const GameplayOptions &options)
	{
		cached_view_ = {};
		song_title_ = std::move(song_title);
		song_subtitle_ = std::move(song_subtitle);
		status_message_.clear();
		available_modes_ = available_modes;
		if (available_modes_.empty())
			available_modes_.push_back(GameplayMode::Classic);
		selected_index_ = default_index_for(options.gameplay_mode());
		rebuild_view();
	}

	bool ModeSelectMenu::move_selection(int delta)
	{
		if (available_modes_.empty())
			return false;

		const int previous_index = selected_index_;
		selected_index_ = std::clamp(selected_index_ + delta, 0, static_cast<int>(available_modes_.size()) - 1);
		rebuild_view();
		return selected_index_ != previous_index;
	}

	bool ModeSelectMenu::set_selected_index(int index)
	{
		if (index < 0 || index >= static_cast<int>(available_modes_.size()))
			return false;

		selected_index_ = index;
		rebuild_view();
		return true;
	}

	void ModeSelectMenu::clear_status_message()
	{
		status_message_.clear();
		rebuild_view();
	}

	void ModeSelectMenu::set_status_message(std::string message)
	{
		status_message_ = std::move(message);
		rebuild_view();
	}

	void ModeSelectMenu::set_preload_progress(
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

	void ModeSelectMenu::apply_to(GameplayOptions &options) const
	{
		if (selected_index_ < 0 || selected_index_ >= static_cast<int>(available_modes_.size()))
			return;

		options.set_gameplay_mode(available_modes_[static_cast<size_t>(selected_index_)]);
	}

	const ModeSelectView &ModeSelectMenu::view() const
	{
		return cached_view_;
	}

	void ModeSelectMenu::rebuild_view()
	{
		cached_view_.song_title = song_title_;
		cached_view_.song_subtitle = song_subtitle_;
		cached_view_.status_message = status_message_;
		cached_view_.selected_index = selected_index_;
		cached_view_.entries.clear();
		cached_view_.entries.reserve(available_modes_.size());
		for (GameplayMode mode : available_modes_)
		{
			ModeListItem item;
			item.gameplay_mode = mode;
			item.label = label_for(mode);
			cached_view_.entries.push_back(std::move(item));
		}
	}

	std::string ModeSelectMenu::label_for(GameplayMode gameplay_mode)
	{
		switch (gameplay_mode)
		{
		case GameplayMode::Classic:
			return "Classic";
		case GameplayMode::Freeplay:
			return "Freeplay";
		case GameplayMode::Replugged:
			return "Replugged";
		}

		return "Mode";
	}

	int ModeSelectMenu::default_index_for(GameplayMode gameplay_mode) const
	{
		for (int index = 0; index < static_cast<int>(available_modes_.size()); ++index)
		{
			if (available_modes_[static_cast<size_t>(index)] == gameplay_mode)
				return index;
		}

		return 0;
	}
}
