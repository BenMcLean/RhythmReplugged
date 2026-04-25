#pragma once

#include "core/concurrency/BackgroundWorker.h"

#include <imgui.h>

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using GLuint = unsigned int;

namespace rhythmreplugged::core
{
	struct SongBrowserView;
}

namespace rhythmreplugged::ui
{
	class OpenGlCoverTextures
	{
	public:
		struct DecodedImage
		{
			std::vector<std::uint32_t> pixels;
			unsigned width = 0;
			unsigned height = 0;
		};

		~OpenGlCoverTextures();

		void sync_song_browser_directory(const core::SongBrowserView &browser);
		std::optional<ImTextureRef> get_texture_ref(const std::string &cover_path);
		void stop_song_browser_loading();
		void clear();

	private:
		void clear_cached_textures();
		struct CompletedDecode
		{
			std::string cover_path;
			std::uint64_t generation = 0;
			DecodedImage image;
			bool success = false;
		};

		struct TextureEntry
		{
			GLuint texture = 0;
			bool requested = false;
			bool failed = false;
		};

		void start_song_browser_loading();
		void queue_cover_decode(const std::string &cover_path);
		void upload_ready_textures(size_t max_uploads = 2);
		void reset_browser_cache();

		std::string cached_browser_path_;
		std::uint64_t generation_ = 0;
		core::BackgroundWorker decode_worker_;
		std::mutex completed_mutex_;
		std::deque<CompletedDecode> completed_decodes_;
		std::unordered_map<std::string, TextureEntry> texture_entries_;
	};
}
