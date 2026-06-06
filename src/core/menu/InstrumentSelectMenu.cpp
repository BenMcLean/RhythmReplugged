#include "core/menu/InstrumentSelectMenu.h"

#include <algorithm>
#include <utility>

namespace rhythmreplugged::core
{
	namespace
	{
		constexpr size_t kBytesPerMegabyte = 1024u * 1024u;

		const char *instrument_name(InstrumentOption instrument)
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
			case InstrumentOption::Drums:
				return "Drums";
			}

			return "Instrument";
		}
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
		working_options_ = options;
		if (options.gameplay_mode() != GameplayMode::Classic)
			working_options_.set_claimed_instruments(available_instruments_);
		available_entries_.clear();
		if (options.gameplay_mode() == GameplayMode::Classic)
		{
			available_entries_.reserve(available_instruments.size());
			for (const InstrumentOption instrument : available_instruments)
				available_entries_.push_back({InstrumentListItem::Kind::ClassicPlay, instrument});
		}
		else
		{
			available_entries_.reserve(available_instruments.size() + 1u);
			for (const InstrumentOption instrument : available_instruments)
				available_entries_.push_back({InstrumentListItem::Kind::ClaimToggle, instrument});
			available_entries_.push_back({InstrumentListItem::Kind::Continue, InstrumentOption::Guitar});
		}
		selected_index_ = default_index_for(working_options_);
		rebuild_view();
	}

	bool InstrumentSelectMenu::move_selection(int delta)
	{
		if (available_entries_.empty())
			return false;

		const int previous_index = selected_index_;
		selected_index_ = std::clamp(selected_index_ + delta, 0, static_cast<int>(available_entries_.size()) - 1);
		rebuild_view();
		return selected_index_ != previous_index;
	}

	bool InstrumentSelectMenu::set_selected_index(int index)
	{
		if (index < 0 || index >= static_cast<int>(available_entries_.size()))
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

	bool InstrumentSelectMenu::activate(GameplayOptions &options, std::string &error_message)
	{
		error_message.clear();
		if (selected_index_ < 0 || selected_index_ >= static_cast<int>(available_entries_.size()))
		{
			error_message = "No instrument selection is currently highlighted.";
			return false;
		}

		const SelectionEntry &entry = available_entries_[static_cast<size_t>(selected_index_)];
		switch (entry.kind)
		{
		case InstrumentListItem::Kind::ClaimToggle:
			working_options_.toggle_claimed_instrument(entry.instrument);
			rebuild_view();
			return false;
		case InstrumentListItem::Kind::ClassicPlay:
			working_options_.set_instrument(entry.instrument);
			working_options_.set_claimed_instruments({entry.instrument});
			options = working_options_;
			return true;
		case InstrumentListItem::Kind::Continue:
			break;
		}

		if (playable_claim_count(working_options_) < 2)
		{
			error_message = "Claim at least two playable instruments for multi-instrument modes.";
			return false;
		}
		options = working_options_;
		return true;
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
		cached_view_.entries.reserve(available_entries_.size());
		for (const SelectionEntry &entry : available_entries_)
		{
			InstrumentListItem item;
			item.kind = entry.kind;
			item.gameplay_mode = working_options_.gameplay_mode();
			item.instrument = entry.instrument;
			item.label = label_for(entry);
			item.is_claimed = working_options_.has_claimed_instrument(entry.instrument);
			cached_view_.entries.push_back(std::move(item));
		}
	}

	std::string InstrumentSelectMenu::label_for(const SelectionEntry &entry) const
	{
		if (entry.kind == InstrumentListItem::Kind::ClaimToggle)
		{
			const bool claimed = working_options_.has_claimed_instrument(entry.instrument);
			return claimed
				? "[x] Claim " + std::string(instrument_name(entry.instrument))
				: "[ ] Claim " + std::string(instrument_name(entry.instrument));
		}
		if (entry.kind == InstrumentListItem::Kind::Continue)
			return "Continue";

		switch (entry.instrument)
		{
		case InstrumentOption::Guitar:
			return entry.kind == InstrumentListItem::Kind::ClassicPlay ? "Play Classic Guitar" : "Guitar";
		case InstrumentOption::Bass:
			return entry.kind == InstrumentListItem::Kind::ClassicPlay ? "Play Classic Bass" : "Bass";
		case InstrumentOption::Rhythm:
			return entry.kind == InstrumentListItem::Kind::ClassicPlay ? "Play Classic Rhythm" : "Rhythm";
		case InstrumentOption::CoopGuitar:
			return entry.kind == InstrumentListItem::Kind::ClassicPlay ? "Play Classic Co-op Guitar" : "Co-op Guitar";
		case InstrumentOption::Keys:
			return entry.kind == InstrumentListItem::Kind::ClassicPlay ? "Play Classic Keys" : "Keys";
		case InstrumentOption::Drums:
			return entry.kind == InstrumentListItem::Kind::ClassicPlay ? "Play Classic Drums" : "Drums";
		}

		return "Instrument";
	}

	size_t InstrumentSelectMenu::playable_claim_count(const GameplayOptions &options) const
	{
		size_t count = 0;
		for (const InstrumentOption instrument : options.claimed_instruments())
		{
			if (std::find(available_instruments_.begin(), available_instruments_.end(), instrument) == available_instruments_.end())
				continue;
			if (std::find(options.reserved_instruments().begin(), options.reserved_instruments().end(), instrument) != options.reserved_instruments().end())
				continue;
			++count;
		}

		return count;
	}

	int InstrumentSelectMenu::default_index_for(const GameplayOptions &options) const
	{
		if (options.gameplay_mode() != GameplayMode::Classic)
		{
			for (int index = 0; index < static_cast<int>(available_entries_.size()); ++index)
			{
				const SelectionEntry &entry = available_entries_[static_cast<size_t>(index)];
				if (entry.kind == InstrumentListItem::Kind::Continue)
					return index;
			}
		}

		for (int index = 0; index < static_cast<int>(available_entries_.size()); ++index)
		{
			const SelectionEntry &entry = available_entries_[static_cast<size_t>(index)];
			if (entry.kind == InstrumentListItem::Kind::ClassicPlay &&
				options.gameplay_mode() == GameplayMode::Classic &&
				entry.instrument == options.instrument())
			{
				return index;
			}
			if (entry.kind == InstrumentListItem::Kind::ClaimToggle &&
				options.has_claimed_instrument(entry.instrument))
				return index;
		}

		return 0;
	}
}
