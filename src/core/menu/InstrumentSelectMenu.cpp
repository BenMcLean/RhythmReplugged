#include "core/menu/InstrumentSelectMenu.h"

#include <algorithm>
#include <utility>

namespace rhythmreplugged::core
{
	namespace
	{
		constexpr size_t kBytesPerMegabyte = 1024u * 1024u;
	}

	void InstrumentSelectMenu::open(
		std::string song_title,
		std::string song_subtitle,
		const std::vector<InstrumentOption> &available_instruments,
		const GameplayOptions &options)
	{
		cached_view_ = {};
		song_title_ = std::move(song_title);
		song_subtitle_ = std::move(song_subtitle);
		status_message_.clear();
		available_instruments_ = available_instruments;
		selected_index_ = default_index_for(options.instrument());
		rebuild_view();
	}

	bool InstrumentSelectMenu::move_selection(int delta)
	{
		if (available_instruments_.empty())
			return false;

		const int previous_index = selected_index_;
		selected_index_ = std::clamp(selected_index_ + delta, 0, static_cast<int>(available_instruments_.size()) - 1);
		rebuild_view();
		return selected_index_ != previous_index;
	}

	bool InstrumentSelectMenu::set_selected_index(int index)
	{
		if (index < 0 || index >= static_cast<int>(available_instruments_.size()))
			return false;

		selected_index_ = index;
		rebuild_view();
		return true;
	}

	void InstrumentSelectMenu::clear_status_message()
	{
		status_message_.clear();
		rebuild_view();
	}

	void InstrumentSelectMenu::set_status_message(std::string message)
	{
		status_message_ = std::move(message);
		rebuild_view();
	}

	void InstrumentSelectMenu::set_preload_progress(
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

	void InstrumentSelectMenu::apply_to(GameplayOptions &options) const
	{
		if (selected_index_ < 0 || selected_index_ >= static_cast<int>(available_instruments_.size()))
			return;

		options.set_instrument(available_instruments_[static_cast<size_t>(selected_index_)]);
	}

	const InstrumentSelectView &InstrumentSelectMenu::view() const
	{
		return cached_view_;
	}

	void InstrumentSelectMenu::rebuild_view()
	{
		cached_view_.song_title = song_title_;
		cached_view_.song_subtitle = song_subtitle_;
		cached_view_.status_message = status_message_;
		cached_view_.selected_index = selected_index_;
		cached_view_.entries.clear();
		cached_view_.entries.reserve(available_instruments_.size());
		for (const InstrumentOption instrument : available_instruments_)
		{
			InstrumentListItem item;
			item.instrument = instrument;
			item.label = label_for(instrument);
			cached_view_.entries.push_back(std::move(item));
		}
	}

	std::string InstrumentSelectMenu::label_for(InstrumentOption instrument)
	{
		switch (instrument)
		{
		case InstrumentOption::Guitar:
			return "Guitar";
		case InstrumentOption::Bass:
			return "Bass";
		case InstrumentOption::Rhythm:
			return "Rhythm";
		case InstrumentOption::CoopGuitar:
			return "Co-op Guitar";
		case InstrumentOption::Keys:
			return "Keys";
		}

		return "Instrument";
	}

	int InstrumentSelectMenu::default_index_for(InstrumentOption instrument) const
	{
		for (int index = 0; index < static_cast<int>(available_instruments_.size()); ++index)
		{
			if (available_instruments_[static_cast<size_t>(index)] == instrument)
				return index;
		}

		return 0;
	}
}
