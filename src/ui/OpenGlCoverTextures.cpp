#include "ui/OpenGlCoverTextures.h"

#include "core/app/AppTypes.h"

#include <algorithm>
#include <cstdlib>
#if defined(RR_RENDER_OPENGL_ES3)
#include <GLES3/gl3.h>
#else
#include <imgui_impl_opengl3_loader.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <utility>
#include <vector>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace rhythmreplugged::ui
{
	using namespace rhythmreplugged::core;

	namespace
	{
		int wrapped_index_distance(int from_index, int to_index, int entry_count)
		{
			if (entry_count <= 0)
				return 0;

			const int direct_distance = std::abs(from_index - to_index);
			return (std::min)(direct_distance, entry_count - direct_distance);
		}

		ImTextureRef make_imgui_texture_ref(GLuint texture)
		{
			return ImTextureRef(static_cast<ImTextureID>(static_cast<uintptr_t>(texture)));
		}

		bool decode_cover_file(const std::vector<std::uint8_t> &bytes, OpenGlCoverTextures::DecodedImage &decoded)
		{
			if (bytes.empty())
				return false;

			int width = 0;
			int height = 0;
			int components = 0;
			stbi_uc *pixel_data = stbi_load_from_memory(
				bytes.data(),
				static_cast<int>(bytes.size()),
				&width,
				&height,
				&components,
				STBI_rgb_alpha);
			if (pixel_data == nullptr || width <= 0 || height <= 0)
			{
				stbi_image_free(pixel_data);
				return false;
			}

			decoded.width = static_cast<unsigned>(width);
			decoded.height = static_cast<unsigned>(height);
			decoded.pixels.assign(
				reinterpret_cast<std::uint32_t *>(pixel_data),
				reinterpret_cast<std::uint32_t *>(pixel_data) + static_cast<size_t>(width) * height);
			stbi_image_free(pixel_data);
			return true;
		}

		bool upload_cover_texture(const OpenGlCoverTextures::DecodedImage &decoded, GLuint &texture)
		{
			texture = 0;
			if (decoded.pixels.empty() || decoded.width == 0 || decoded.height == 0)
				return false;

			glGenTextures(1, &texture);
			if (texture == 0)
				return false;

			glBindTexture(GL_TEXTURE_2D, texture);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glTexImage2D(GL_TEXTURE_2D,
				0,
				GL_RGBA,
				static_cast<GLsizei>(decoded.width),
				static_cast<GLsizei>(decoded.height),
				0,
				GL_RGBA,
				GL_UNSIGNED_BYTE,
				decoded.pixels.data());
			glBindTexture(GL_TEXTURE_2D, 0);
			return true;
		}
	}

	OpenGlCoverTextures::OpenGlCoverTextures(::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system)
		: file_system_(file_system)
	{
	}

	OpenGlCoverTextures::~OpenGlCoverTextures()
	{
		stop_song_browser_loading();
	}

	void OpenGlCoverTextures::sync_song_browser_directory(const SongBrowserView &browser)
	{
		start_song_browser_loading(browser.entries.size());
		upload_ready_textures();

		const bool directory_changed = cached_browser_path_ != browser.current_path;
		const bool selected_index_changed = cached_selected_index_ != browser.selected_index;

		if (directory_changed)
		{
			reset_browser_cache();
			cached_browser_path_ = browser.current_path;
			++generation_;
			queue_browser_cover_decodes(browser);
			reprioritize_pending_decodes(browser);
		}
		else
		{
			queue_browser_cover_decodes(browser);
			if (selected_index_changed)
				reprioritize_pending_decodes(browser);
		}

		cached_selected_index_ = browser.selected_index;
	}

	std::optional<ImTextureRef> OpenGlCoverTextures::get_texture_ref(const std::string &cover_path)
	{
		if (cover_path.empty())
			return std::nullopt;

		upload_ready_textures();

		const auto it = texture_entries_.find(cover_path);
		if (it != texture_entries_.end())
		{
			if (it->second.texture == 0)
				return std::nullopt;

			return make_imgui_texture_ref(it->second.texture);
		}

		queue_cover_decode(cover_path);
		return std::nullopt;
	}

	void OpenGlCoverTextures::stop_song_browser_loading()
	{
		clear();
	}

	void OpenGlCoverTextures::clear()
	{
		reset_browser_cache();
		decode_worker_.stop();
		decode_thread_count_ = 0;
		cached_selected_index_ = -1;
		clear_cached_textures();
		cached_browser_path_.clear();
	}

	void OpenGlCoverTextures::clear_cached_textures()
	{
		for (auto &[path, entry] : texture_entries_)
		{
			(void)path;
			if (entry.texture != 0)
				glDeleteTextures(1, &entry.texture);
		}

		texture_entries_.clear();

		std::scoped_lock lock(completed_mutex_);
		completed_decodes_.clear();
	}

	void OpenGlCoverTextures::start_song_browser_loading(size_t job_count_hint)
	{
		(void)job_count_hint;
		if (!decode_worker_.running())
		{
			decode_thread_count_ = BackgroundWorker::automatic_thread_count();
			decode_worker_.start(decode_thread_count_);
		}
	}

	void OpenGlCoverTextures::queue_cover_decode(const std::string &cover_path)
	{
		if (cover_path.empty())
			return;

		TextureEntry &entry = texture_entries_[cover_path];
		if (entry.texture != 0 || entry.failed || entry.requested)
			return;

		entry.requested = true;
		{
			std::scoped_lock lock(pending_mutex_);
			pending_decodes_.push_back({cover_path, generation_});
		}

		launch_pending_decode_loops();
	}

	void OpenGlCoverTextures::queue_browser_cover_decodes(const SongBrowserView &browser)
	{
		for (const SongListItem &entry : browser.entries)
			queue_cover_decode(entry.cover_art_path);
	}

	void OpenGlCoverTextures::reprioritize_pending_decodes(const SongBrowserView &browser)
	{
		std::unordered_map<std::string, std::pair<int, int>> priorities;
		priorities.reserve(browser.entries.size());

		const bool has_selected_index =
			browser.selected_index >= 0 &&
			browser.selected_index < static_cast<int>(browser.entries.size());
		const int fallback_distance = static_cast<int>(browser.entries.size()) + 1;

		for (int index = 0; index < static_cast<int>(browser.entries.size()); ++index)
		{
			const std::string &cover_path = browser.entries[index].cover_art_path;
			if (cover_path.empty())
				continue;

			const int distance = has_selected_index
				? wrapped_index_distance(index, browser.selected_index, static_cast<int>(browser.entries.size()))
				: fallback_distance;
			const auto [it, inserted] = priorities.emplace(cover_path, std::make_pair(distance, index));
			if (!inserted && std::make_pair(distance, index) < it->second)
				it->second = std::make_pair(distance, index);
		}

		{
			std::scoped_lock lock(pending_mutex_);
			std::stable_sort(pending_decodes_.begin(), pending_decodes_.end(),
				[&](const PendingDecode &left, const PendingDecode &right)
				{
					const auto left_it = priorities.find(left.cover_path);
					const auto right_it = priorities.find(right.cover_path);
					const std::pair<int, int> left_priority = left_it != priorities.end()
						? left_it->second
						: std::make_pair(fallback_distance, fallback_distance);
					const std::pair<int, int> right_priority = right_it != priorities.end()
						? right_it->second
						: std::make_pair(fallback_distance, fallback_distance);
					return left_priority < right_priority;
				});
		}

		launch_pending_decode_loops();
	}

	void OpenGlCoverTextures::launch_pending_decode_loops()
	{
		std::scoped_lock lock(pending_mutex_);
		while (active_decode_loops_ < decode_thread_count_ && active_decode_loops_ < pending_decodes_.size())
		{
			if (!decode_worker_.enqueue([this]() { process_pending_decode_loop(); }))
				return;

			++active_decode_loops_;
		}
	}

	void OpenGlCoverTextures::process_pending_decode_loop()
	{
		for (;;)
		{
			PendingDecode pending;
			{
				std::scoped_lock lock(pending_mutex_);
				if (pending_decodes_.empty())
				{
					if (active_decode_loops_ > 0)
						--active_decode_loops_;
					return;
				}

				pending = std::move(pending_decodes_.front());
				pending_decodes_.pop_front();
			}

			CompletedDecode completed;
			completed.cover_path = std::move(pending.cover_path);
			completed.generation = pending.generation;
			const auto bytes = file_system_.read_binary_file(completed.cover_path);
			completed.success = bytes.has_value() && decode_cover_file(*bytes, completed.image);

			std::scoped_lock lock(completed_mutex_);
			completed_decodes_.push_back(std::move(completed));
		}
	}

	void OpenGlCoverTextures::upload_ready_textures(size_t max_uploads)
	{
		for (size_t upload_count = 0; upload_count < max_uploads; ++upload_count)
		{
			CompletedDecode completed;
			{
				std::scoped_lock lock(completed_mutex_);
				if (completed_decodes_.empty())
					return;

				completed = std::move(completed_decodes_.front());
				completed_decodes_.pop_front();
			}

			if (completed.generation != generation_)
				continue;

			auto it = texture_entries_.find(completed.cover_path);
			if (it == texture_entries_.end())
				continue;

			it->second.requested = false;
			if (!completed.success)
			{
				it->second.failed = true;
				continue;
			}

			GLuint texture = 0;
			if (!upload_cover_texture(completed.image, texture))
			{
				it->second.failed = true;
				continue;
			}

			it->second.texture = texture;
		}
	}

	void OpenGlCoverTextures::reset_browser_cache()
	{
		{
			std::scoped_lock lock(pending_mutex_);
			pending_decodes_.clear();
		}

		clear_cached_textures();
	}
}
