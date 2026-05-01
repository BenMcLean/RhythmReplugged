#pragma once

#include "core/app/AppTypes.h"
#include "core/songs/SongIni.h"
#include "frontend_contract/RetroFileSystem.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rhythmreplugged::core
{
	class SongBrowser
	{
	public:
		explicit SongBrowser(::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system);

		bool set_root(const std::string &root_path, std::string &error_message);
		void clear_root(std::string status_message);
		bool move_selection(int delta);
		bool set_selected_index(int index);
		bool jump_to_letter(char letter);
		bool jump_to_next_letter();
		bool jump_to_previous_letter();
		bool navigate_to_parent(std::string &error_message);
		bool activate_selected(std::string &selected_song_path, std::string &error_message);
		void update();
		void clear_status_message();
		void set_status_message(std::string message);

		const SongBrowserView &view() const;

	private:
		struct BrowserEntry
		{
			std::string path;
			std::string name;
			std::string sort_name;
			std::string subtitle;
			std::string cover_art_path;
			std::string error_message;
			bool is_parent = false;
			bool is_folder = false;
			bool is_song = false;
			bool is_valid_song = false;
			bool metadata_loaded = false;
		};

		struct CachedSongEntryData
		{
			std::string display_name;
			std::string subtitle;
			std::string cover_art_path;
			std::string error_message;
			bool is_valid_song = false;
			bool metadata_loaded = false;
		};

		bool load_directory(const std::string &path, std::string &error_message, const std::string *preferred_selected_path = nullptr);
		BrowserEntry make_song_entry(const ::rhythmreplugged::frontend_contract::RetroDirectoryEntry &directory_entry) const;
		bool hydrate_song_entry(size_t index);
		void apply_cached_song_entry_data(BrowserEntry &entry, const CachedSongEntryData &cached_data) const;
		std::optional<size_t> next_song_entry_to_hydrate() const;
		int first_selectable_index() const;
		int find_entry_index_by_path(const std::string &path) const;
		int normalize_letter_navigation_index() const;
		static char entry_letter(const BrowserEntry &entry);
		void rebuild_view();

		::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system_;
		std::string root_path_;
		std::string current_path_;
		std::string status_message_;
		std::vector<BrowserEntry> entries_;
		int selected_index_ = 0;
		std::unordered_map<std::string, CachedSongEntryData> song_entry_cache_;
		mutable SongBrowserView cached_view_;
	};
}
