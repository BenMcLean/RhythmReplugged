#pragma once

#include "core/concurrency/BackgroundWorker.h"
#include "frontend_contract/RetroFileSystem.h"

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

		explicit OpenGlCoverTextures(::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system);
		~OpenGlCoverTextures();

		void sync_song_browser_directory(const core::SongBrowserView &browser);
		std::optional<ImTextureRef> get_texture_ref(const std::string &cover_path);
		void stop_song_browser_loading();
		void clear();

	private:
		struct PendingDecode
		{
			std::string cover_path;
			std::uint64_t generation = 0;
		};

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

		void start_song_browser_loading(size_t job_count_hint);
		void queue_cover_decode(const std::string &cover_path);
		void queue_browser_cover_decodes(const core::SongBrowserView &browser);
		void reprioritize_pending_decodes(const core::SongBrowserView &browser);
		void launch_pending_decode_loops();
		void process_pending_decode_loop();
		void upload_ready_textures(size_t max_uploads = 2);
		void reset_browser_cache();

		::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system_;
		std::string cached_browser_path_;
		int cached_selected_index_ = -1;
		std::uint64_t generation_ = 0;
		core::BackgroundWorker decode_worker_;
		size_t decode_thread_count_ = 0;
		std::mutex pending_mutex_;
		std::deque<PendingDecode> pending_decodes_;
		size_t active_decode_loops_ = 0;
		std::mutex completed_mutex_;
		std::deque<CompletedDecode> completed_decodes_;
		std::unordered_map<std::string, TextureEntry> texture_entries_;
	};
}
