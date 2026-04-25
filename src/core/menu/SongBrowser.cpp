#include "core/menu/SongBrowser.h"
#include "core/audio/StemCatalog.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace rhythmreplugged
{
	namespace
	{
		constexpr char kNonAlphabeticGroup = '#';

		std::string to_lower_copy(std::string_view text)
		{
			std::string lowered;
			lowered.reserve(text.size());
			for (char ch : text)
				lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
			return lowered;
		}

		bool case_insensitive_name_less(const RetroDirectoryEntry &left, const RetroDirectoryEntry &right)
		{
			return to_lower_copy(left.name) < to_lower_copy(right.name);
		}
	}

	SongBrowser::SongBrowser(IRetroFileSystem &file_system)
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

		selected_index_ = std::clamp(selected_index_ + delta, 0, static_cast<int>(entries_.size()) - 1);
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

	bool SongBrowser::jump_to_next_letter()
	{
		const int current_index = normalize_letter_navigation_index();
		if (current_index < 0)
			return false;

		const char current_letter = entry_letter(entries_[current_index]);
		for (int index = current_index + 1; index < static_cast<int>(entries_.size()); ++index)
		{
			if (entries_[index].is_parent)
				continue;

			const char candidate_letter = entry_letter(entries_[index]);
			if (candidate_letter == current_letter)
				continue;

			if (current_letter == kNonAlphabeticGroup || candidate_letter > current_letter)
			{
				selected_index_ = index;
				rebuild_view();
				return true;
			}
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

		for (int index = current_letter_start - 1; index >= 0; --index)
		{
			if (entries_[index].is_parent)
				continue;

			const char previous_letter = entry_letter(entries_[index]);
			if (current_letter != kNonAlphabeticGroup && previous_letter >= current_letter)
				continue;

			int previous_letter_start = index;
			for (int scan = index - 1; scan >= 0; --scan)
			{
				if (entries_[scan].is_parent)
					continue;

				if (entry_letter(entries_[scan]) != previous_letter)
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
		return load_directory(file_system_.parent_path(current_path_), error_message, &previous_path);
	}

	bool SongBrowser::activate_selected(std::string &selected_song_path, std::string &error_message)
	{
		selected_song_path.clear();
		error_message.clear();

		if (selected_index_ < 0 || selected_index_ >= static_cast<int>(entries_.size()))
			return false;

		const BrowserEntry &entry = entries_[selected_index_];
		if (entry.is_parent || entry.is_folder)
		{
			return load_directory(entry.path, error_message);
		}

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

	bool SongBrowser::load_directory(const std::string &path, std::string &error_message, const std::string *preferred_selected_path)
	{
		const std::string canonical_path = file_system_.canonicalize_path(path);
		if (canonical_path.empty() || !file_system_.path_is_directory(canonical_path))
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

		std::vector<RetroDirectoryEntry> folders;
		std::vector<RetroDirectoryEntry> songs;
		for (const RetroDirectoryEntry &entry : file_system_.list_directory(canonical_path))
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

		for (const RetroDirectoryEntry &folder : folders)
		{
			BrowserEntry entry;
			entry.path = folder.path;
			entry.name = folder.name;
			entry.sort_name = folder.name;
			entry.is_folder = true;
			entries_.push_back(std::move(entry));
		}

		for (const RetroDirectoryEntry &song : songs)
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

	SongBrowser::BrowserEntry SongBrowser::make_song_entry(const RetroDirectoryEntry &directory_entry) const
	{
		BrowserEntry entry;
		entry.path = directory_entry.path;
		entry.name = directory_entry.name;
		entry.sort_name = directory_entry.name;
		entry.is_song = true;

		const SongIniParseResult parse_result = parse_song_ini(file_system_, directory_entry.path + "/song.ini");
		if (!parse_result.has_song_section)
		{
			entry.error_message = parse_result.error_message;
			return entry;
		}

		if (!parse_result.parsed_successfully)
		{
			entry.error_message = parse_result.error_message;
			return entry;
		}

		const SongMetadataView metadata_view = make_song_metadata_view(parse_result.metadata, directory_entry.name);
		entry.name = metadata_view.name;
		entry.subtitle = metadata_view.artist;
		entry.cover_art_path = resolve_cover_art_path(file_system_, directory_entry.path, parse_result.metadata);

		const std::string *song_name = nullptr;
		if (!parse_result.metadata.try_get_string("name", song_name) || song_name == nullptr || song_name->empty())
		{
			entry.error_message = "song.ini is missing a non-empty name field.";
			return entry;
		}

		if (!contains_supported_chart(directory_entry.path))
		{
			entry.error_message = "No supported chart file was found.";
			return entry;
		}

		if (!contains_supported_audio(directory_entry.path))
		{
			entry.error_message = "No supported audio stem was found.";
			return entry;
		}

		entry.is_valid_song = true;
		return entry;
	}

	bool SongBrowser::contains_supported_chart(const std::string &directory_path) const
	{
		static constexpr std::array<const char *, 4> k_chart_names = {
			"notes.mid", "notes.midi", "notes.chart", "notes.txt"};

		for (const char *name : k_chart_names)
		{
			if (file_system_.path_exists(directory_path + "/" + name))
				return true;
		}

		return false;
	}

	bool SongBrowser::contains_supported_audio(const std::string &directory_path) const
	{
		for (std::string_view stem : kPlayableStemNames)
		{
			if (file_system_.path_exists(directory_path + "/" + std::string(stem) + ".ogg"))
				return true;
		}

		return false;
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
		for (char ch : entry.sort_name)
		{
			const unsigned char unsigned_ch = static_cast<unsigned char>(ch);
			if (std::isalpha(unsigned_ch))
				return static_cast<char>(std::toupper(unsigned_ch));
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
