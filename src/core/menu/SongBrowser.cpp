#include "core/menu/SongBrowser.h"
#include <algorithm>
#include <cctype>

namespace rhythmreplugged::core
{
	namespace
	{
		constexpr char kFolderGroup = '!';
		constexpr char kNonAlphabeticGroup = '#';

		int letter_group_order(char group)
		{
			if (group == kFolderGroup)
				return -1;

			return group == kNonAlphabeticGroup ? 0 : (group - 'A' + 1);
		}

		std::string to_lower_copy(std::string_view text)
		{
			std::string lowered;
			lowered.reserve(text.size());
			for (char ch : text)
				lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
			return lowered;
		}

		bool case_insensitive_name_less(const ::rhythmreplugged::frontend_contract::RetroDirectoryEntry &left, const ::rhythmreplugged::frontend_contract::RetroDirectoryEntry &right)
		{
			return to_lower_copy(left.name) < to_lower_copy(right.name);
		}
	}

	SongBrowser::SongBrowser(::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system)
		: file_system_(file_system)
	{
	}

	bool SongBrowser::set_root(const std::string &root_path, std::string &error_message)
	{
		const std::string canonical_root = file_system_.canonicalize_path(root_path);
		if (canonical_root.empty() || !file_system_.path_is_directory(canonical_root))
		{
			error_message = "Song root is not a valid directory.";
			return false;
		}

		root_path_ = canonical_root;
		return load_directory(root_path_, error_message);
	}

	void SongBrowser::clear_root(std::string status_message)
	{
		root_path_.clear();
		current_path_.clear();
		status_message_ = std::move(status_message);
		entries_.clear();
		selected_index_ = 0;
		rebuild_view();
	}

	bool SongBrowser::move_selection(int delta)
	{
		if (entries_.empty())
			return false;

		const int entry_count = static_cast<int>(entries_.size());
		if (entry_count == 1)
			return true;

		int next_index = (selected_index_ + delta) % entry_count;
		if (next_index < 0)
			next_index += entry_count;

		selected_index_ = next_index;
		rebuild_view();
		return true;
	}

	bool SongBrowser::set_selected_index(int index)
	{
		if (entries_.empty() || index < 0 || index >= static_cast<int>(entries_.size()))
			return false;

		selected_index_ = index;
		rebuild_view();
		return true;
	}

	bool SongBrowser::jump_to_letter(char letter)
	{
		const unsigned char unsigned_letter = static_cast<unsigned char>(letter);
		if (!std::isalpha(unsigned_letter))
			return false;

		const char target_letter = static_cast<char>(std::toupper(unsigned_letter));
		for (int index = 0; index < static_cast<int>(entries_.size()); ++index)
		{
			if (!entries_[index].is_song)
				continue;

			if (entry_letter(entries_[index]) != target_letter)
				continue;

			selected_index_ = index;
			rebuild_view();
			return true;
		}

		return false;
	}

	bool SongBrowser::jump_to_next_letter()
	{
		const int current_index = normalize_letter_navigation_index();
		if (current_index < 0)
			return false;

		const char current_letter = entry_letter(entries_[current_index]);
		int target_order = 0;
		for (int index = current_index + 1; index < static_cast<int>(entries_.size()); ++index)
		{
			if (entries_[index].is_parent)
				continue;

			const int candidate_order = letter_group_order(entry_letter(entries_[index]));
			if (candidate_order <= letter_group_order(current_letter))
				continue;

			if (target_order == 0 || candidate_order < target_order)
				target_order = candidate_order;
		}

		if (target_order == 0)
			return false;

		for (int index = current_index + 1; index < static_cast<int>(entries_.size()); ++index)
		{
			if (entries_[index].is_parent)
				continue;

			if (letter_group_order(entry_letter(entries_[index])) != target_order)
				continue;

			selected_index_ = index;
			rebuild_view();
			return true;
		}

		return false;
	}

	bool SongBrowser::jump_to_previous_letter()
	{
		const int current_index = normalize_letter_navigation_index();
		if (current_index < 0)
			return false;

		const char current_letter = entry_letter(entries_[current_index]);
		int current_letter_start = current_index;
		for (int index = current_index - 1; index >= 0; --index)
		{
			if (entries_[index].is_parent)
				continue;

			if (entry_letter(entries_[index]) != current_letter)
				break;

			current_letter_start = index;
		}

		if (current_index != current_letter_start)
		{
			selected_index_ = current_letter_start;
			rebuild_view();
			return true;
		}

		int target_order = -1;
		for (int index = current_letter_start - 1; index >= 0; --index)
		{
			if (entries_[index].is_parent)
				continue;

			const int candidate_order = letter_group_order(entry_letter(entries_[index]));
			if (candidate_order >= letter_group_order(current_letter))
				continue;

			if (candidate_order > target_order)
				target_order = candidate_order;
		}

		if (target_order < 0)
			return false;

		for (int index = current_letter_start - 1; index >= 0; --index)
		{
			if (entries_[index].is_parent)
				continue;

			if (letter_group_order(entry_letter(entries_[index])) != target_order)
				continue;

			int previous_letter_start = index;
			for (int scan = index - 1; scan >= 0; --scan)
			{
				if (entries_[scan].is_parent)
					continue;

				if (letter_group_order(entry_letter(entries_[scan])) != target_order)
					break;

				previous_letter_start = scan;
			}

			selected_index_ = previous_letter_start;
			rebuild_view();
			return true;
		}

		return false;
	}

	bool SongBrowser::navigate_to_parent(std::string &error_message)
	{
		error_message.clear();
		if (root_path_.empty() || current_path_.empty() || current_path_ == root_path_)
			return false;

		const std::string previous_path = current_path_;
		return load_directory(file_system_.parent_path(current_path_), error_message, &previous_path, true);
	}

	bool SongBrowser::activate_selected(std::string &selected_song_path, std::string &error_message)
	{
		selected_song_path.clear();
		error_message.clear();

		if (selected_index_ < 0 || selected_index_ >= static_cast<int>(entries_.size()))
			return false;

		const BrowserEntry &entry = entries_[selected_index_];
		if (entry.is_parent)
			return navigate_to_parent(error_message);

		if (entry.is_folder)
		{
			return load_directory(entry.path, error_message, nullptr, true);
		}

		if (!entries_[static_cast<size_t>(selected_index_)].metadata_loaded)
			hydrate_song_entry(static_cast<size_t>(selected_index_));

		if (!entry.is_valid_song)
		{
			status_message_ = entry.error_message;
			rebuild_view();
			return false;
		}

		selected_song_path = entry.path;
		status_message_.clear();
		rebuild_view();
		return true;
	}

	void SongBrowser::update()
	{
		static constexpr size_t kHydrationBudgetPerUpdate = 2;

		bool changed = false;
		for (size_t processed = 0; processed < kHydrationBudgetPerUpdate; ++processed)
		{
			const std::optional<size_t> entry_index = next_song_entry_to_hydrate();
			if (!entry_index.has_value())
				break;

			changed = hydrate_song_entry(*entry_index) || changed;
		}

		if (changed)
			rebuild_view();
	}

	void SongBrowser::clear_status_message()
	{
		status_message_.clear();
		rebuild_view();
	}

	void SongBrowser::set_status_message(std::string message)
	{
		status_message_ = std::move(message);
		rebuild_view();
	}

	const SongBrowserView &SongBrowser::view() const
	{
		return cached_view_;
	}

	bool SongBrowser::load_directory(const std::string &path,
		std::string &error_message,
		const std::string *preferred_selected_path,
		bool trust_directory_hint)
	{
		const std::string canonical_path = file_system_.canonicalize_path(path);
		if (canonical_path.empty() || (!trust_directory_hint && !file_system_.path_is_directory(canonical_path)))
		{
			error_message = "Target path is not a directory.";
			return false;
		}

		if (!root_path_.empty())
		{
			const std::string lowered_root = to_lower_copy(root_path_);
			const std::string lowered_target = to_lower_copy(canonical_path);
			if (lowered_target.size() < lowered_root.size() || lowered_target.compare(0, lowered_root.size(), lowered_root) != 0)
			{
				error_message = "Navigation cannot leave the configured song root.";
				return false;
			}
		}

		std::vector<::rhythmreplugged::frontend_contract::RetroDirectoryEntry> folders;
		std::vector<::rhythmreplugged::frontend_contract::RetroDirectoryEntry> songs;
		for (const ::rhythmreplugged::frontend_contract::RetroDirectoryEntry &entry : file_system_.list_directory(canonical_path))
		{
			if (!entry.is_directory)
				continue;

			if (file_system_.path_exists(entry.path + "/song.ini"))
				songs.push_back(entry);
			else
				folders.push_back(entry);
		}

		std::sort(folders.begin(), folders.end(), case_insensitive_name_less);
		std::sort(songs.begin(), songs.end(), case_insensitive_name_less);

		entries_.clear();
		if (!root_path_.empty() && canonical_path != root_path_)
		{
			BrowserEntry parent_entry;
			parent_entry.path = file_system_.parent_path(canonical_path);
			parent_entry.name = "..";
			parent_entry.sort_name = "..";
			parent_entry.is_parent = true;
			entries_.push_back(std::move(parent_entry));
		}

		for (const ::rhythmreplugged::frontend_contract::RetroDirectoryEntry &folder : folders)
		{
			BrowserEntry entry;
			entry.path = folder.path;
			entry.name = folder.name;
			entry.sort_name = folder.name;
			entry.is_folder = true;
			entries_.push_back(std::move(entry));
		}

		for (const ::rhythmreplugged::frontend_contract::RetroDirectoryEntry &song : songs)
			entries_.push_back(make_song_entry(song));

		current_path_ = canonical_path;
		selected_index_ = first_selectable_index();
		if (preferred_selected_path != nullptr && !preferred_selected_path->empty())
		{
			const int preferred_index = find_entry_index_by_path(*preferred_selected_path);
			if (preferred_index >= 0)
				selected_index_ = preferred_index;
		}
		status_message_.clear();
		rebuild_view();
		return true;
	}

	SongBrowser::BrowserEntry SongBrowser::make_song_entry(const ::rhythmreplugged::frontend_contract::RetroDirectoryEntry &directory_entry) const
	{
		BrowserEntry entry;
		entry.path = directory_entry.path;
		entry.name = directory_entry.name;
		entry.sort_name = directory_entry.name;
		entry.is_song = true;
		entry.is_valid_song = true;
		const auto cached_it = song_entry_cache_.find(directory_entry.path);
		if (cached_it != song_entry_cache_.end())
			apply_cached_song_entry_data(entry, cached_it->second);
		return entry;
	}

	bool SongBrowser::hydrate_song_entry(size_t index)
	{
		if (index >= entries_.size())
			return false;

		BrowserEntry &entry = entries_[index];
		if (!entry.is_song || entry.metadata_loaded)
			return false;

		CachedSongEntryData cached_data;
		cached_data.display_name = entry.name;
		cached_data.is_valid_song = false;
		cached_data.metadata_loaded = true;

		const SongIniParseResult parse_result = parse_song_ini(file_system_, entry.path + "/song.ini");
		if (!parse_result.has_song_section)
		{
			cached_data.error_message = parse_result.error_message;
			song_entry_cache_[entry.path] = cached_data;
			apply_cached_song_entry_data(entry, cached_data);
			return true;
		}

		if (!parse_result.parsed_successfully)
		{
			cached_data.error_message = parse_result.error_message;
			song_entry_cache_[entry.path] = cached_data;
			apply_cached_song_entry_data(entry, cached_data);
			return true;
		}

		const SongMetadataView metadata_view = make_song_metadata_view(parse_result.metadata, entry.name);
		cached_data.display_name = metadata_view.name;
		cached_data.subtitle = metadata_view.artist;
		cached_data.cover_art_path = resolve_cover_art_path(file_system_, entry.path, parse_result.metadata);

		const std::string *song_name = nullptr;
		if (!parse_result.metadata.try_get_string("name", song_name) || song_name == nullptr || song_name->empty())
		{
			cached_data.error_message = "song.ini is missing a non-empty name field.";
			song_entry_cache_[entry.path] = cached_data;
			apply_cached_song_entry_data(entry, cached_data);
			return true;
		}

		cached_data.is_valid_song = true;
		song_entry_cache_[entry.path] = cached_data;
		apply_cached_song_entry_data(entry, cached_data);
		return true;
	}

	void SongBrowser::apply_cached_song_entry_data(BrowserEntry &entry, const CachedSongEntryData &cached_data) const
	{
		entry.name = cached_data.display_name.empty() ? entry.name : cached_data.display_name;
		entry.subtitle = cached_data.subtitle;
		entry.cover_art_path = cached_data.cover_art_path;
		entry.error_message = cached_data.error_message;
		entry.is_valid_song = cached_data.is_valid_song;
		entry.metadata_loaded = cached_data.metadata_loaded;
	}

	std::optional<size_t> SongBrowser::next_song_entry_to_hydrate() const
	{
		if (entries_.empty())
			return std::nullopt;

		if (selected_index_ >= 0 && selected_index_ < static_cast<int>(entries_.size()))
		{
			const BrowserEntry &selected = entries_[static_cast<size_t>(selected_index_)];
			if (selected.is_song && !selected.metadata_loaded)
				return static_cast<size_t>(selected_index_);
		}

		for (size_t distance = 1; distance < entries_.size(); ++distance)
		{
			if (selected_index_ >= 0)
			{
				const int forward = selected_index_ + static_cast<int>(distance);
				if (forward >= 0 && forward < static_cast<int>(entries_.size()))
				{
					const BrowserEntry &entry = entries_[static_cast<size_t>(forward)];
					if (entry.is_song && !entry.metadata_loaded)
						return static_cast<size_t>(forward);
				}

				const int backward = selected_index_ - static_cast<int>(distance);
				if (backward >= 0 && backward < static_cast<int>(entries_.size()))
				{
					const BrowserEntry &entry = entries_[static_cast<size_t>(backward)];
					if (entry.is_song && !entry.metadata_loaded)
						return static_cast<size_t>(backward);
				}
			}
		}

		for (size_t index = 0; index < entries_.size(); ++index)
		{
			const BrowserEntry &entry = entries_[index];
			if (entry.is_song && !entry.metadata_loaded)
				return index;
		}

		return std::nullopt;
	}

	int SongBrowser::first_selectable_index() const
	{
		if (entries_.empty())
			return 0;

		return entries_.front().is_parent && entries_.size() > 1 ? 1 : 0;
	}

	int SongBrowser::find_entry_index_by_path(const std::string &path) const
	{
		const std::string canonical_path = file_system_.canonicalize_path(path);
		if (canonical_path.empty())
			return -1;

		for (int index = 0; index < static_cast<int>(entries_.size()); ++index)
		{
			if (entries_[index].path == canonical_path)
				return index;
		}

		return -1;
	}

	int SongBrowser::normalize_letter_navigation_index() const
	{
		if (entries_.empty())
			return -1;

		if (selected_index_ >= 0 && selected_index_ < static_cast<int>(entries_.size()) && !entries_[selected_index_].is_parent)
			return selected_index_;

		for (int index = 0; index < static_cast<int>(entries_.size()); ++index)
		{
			if (!entries_[index].is_parent)
				return index;
		}

		return -1;
	}

	char SongBrowser::entry_letter(const BrowserEntry &entry)
	{
		if (entry.is_folder)
			return kFolderGroup;

		for (char ch : entry.sort_name)
		{
			const unsigned char unsigned_ch = static_cast<unsigned char>(ch);
			if (std::isspace(unsigned_ch))
				continue;

			if (std::isalpha(unsigned_ch))
				return static_cast<char>(std::toupper(unsigned_ch));

			return kNonAlphabeticGroup;
		}

		return kNonAlphabeticGroup;
	}

	void SongBrowser::rebuild_view()
	{
		cached_view_.root_path = root_path_;
		cached_view_.current_path = current_path_;
		cached_view_.status_message = status_message_;
		cached_view_.selected_index = selected_index_;
		cached_view_.entries.clear();
		cached_view_.entries.reserve(entries_.size());

		for (const BrowserEntry &entry : entries_)
		{
			SongListItem item;
			item.label = entry.name;
			item.subtitle = entry.subtitle;
			item.cover_art_path = entry.cover_art_path;
			item.error_message = entry.error_message;
			item.is_parent = entry.is_parent;
			item.is_folder = entry.is_folder;
			item.is_song = entry.is_song;
			item.is_valid_song = entry.is_valid_song;
			cached_view_.entries.push_back(std::move(item));
		}
	}
}
