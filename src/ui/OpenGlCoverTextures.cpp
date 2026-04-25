#include "ui/OpenGlCoverTextures.h"

#include "core/app/AppTypes.h"

#include <imgui_impl_opengl3_loader.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace rhythmreplugged
{
	namespace
	{
		ImTextureRef make_imgui_texture_ref(GLuint texture)
		{
			return ImTextureRef(static_cast<ImTextureID>(static_cast<uintptr_t>(texture)));
		}

		bool read_file_bytes(const std::string &cover_path, std::vector<char> &bytes)
		{
			std::ifstream stream(std::filesystem::path(cover_path), std::ios::binary);
			if (!stream)
				return false;

			bytes.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
			return !bytes.empty();
		}

		bool decode_cover_file(const std::string &cover_path, OpenGlCoverTextures::DecodedImage &decoded)
		{
			std::vector<char> bytes;
			if (!read_file_bytes(cover_path, bytes))
				return false;

			int width = 0;
			int height = 0;
			int components = 0;
			stbi_uc *pixel_data = stbi_load_from_memory(
				reinterpret_cast<const stbi_uc *>(bytes.data()),
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

	OpenGlCoverTextures::~OpenGlCoverTextures()
	{
		stop_song_browser_loading();
	}

	void OpenGlCoverTextures::sync_song_browser_directory(const SongBrowserView &browser)
	{
		start_song_browser_loading();
		upload_ready_textures();

		if (cached_browser_path_ == browser.current_path)
			return;

		reset_browser_cache();
		cached_browser_path_ = browser.current_path;
		++generation_;

		if (!browser.entries.empty() &&
			browser.selected_index >= 0 &&
			browser.selected_index < static_cast<int>(browser.entries.size()))
		{
			queue_cover_decode(browser.entries[browser.selected_index].cover_art_path);
		}

		for (const SongListItem &entry : browser.entries)
			queue_cover_decode(entry.cover_art_path);
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
		decode_worker_.stop();
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

	void OpenGlCoverTextures::start_song_browser_loading()
	{
		if (!decode_worker_.running())
			decode_worker_.start(1);
	}

	void OpenGlCoverTextures::queue_cover_decode(const std::string &cover_path)
	{
		if (cover_path.empty())
			return;

		TextureEntry &entry = texture_entries_[cover_path];
		if (entry.texture != 0 || entry.failed || entry.requested)
			return;

		entry.requested = true;
		const std::uint64_t generation = generation_;
		if (!decode_worker_.enqueue([this, generation, cover_path]()
		{
			CompletedDecode completed;
			completed.cover_path = cover_path;
			completed.generation = generation;
			completed.success = decode_cover_file(cover_path, completed.image);

			std::scoped_lock lock(completed_mutex_);
			completed_decodes_.push_back(std::move(completed));
		}))
		{
			entry.requested = false;
			entry.failed = true;
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
		clear_cached_textures();
		decode_worker_.clear_pending();
	}
}
