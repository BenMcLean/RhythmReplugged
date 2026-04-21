#include "ui/OpenGlCoverTextures.h"

#include "core/app/AppTypes.h"

#include <formats/image.h>
#include <formats/rpng.h>
#include <imgui_impl_opengl3_loader.h>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

		struct DecodedPng
		{
			std::vector<std::uint32_t> pixels;
			unsigned width = 0;
			unsigned height = 0;
		};

		bool equals_ignore_case(std::string_view left, std::string_view right)
		{
			if (left.size() != right.size())
				return false;

			for (size_t i = 0; i < left.size(); ++i)
			{
				if (std::tolower(static_cast<unsigned char>(left[i])) !=
					std::tolower(static_cast<unsigned char>(right[i])))
				{
					return false;
				}
			}

			return true;
		}

		bool decode_png_file(const std::string &cover_path, DecodedPng &decoded)
		{
			const std::string extension = std::filesystem::path(cover_path).extension().string();
			if (!equals_ignore_case(extension, ".png"))
				return false;

			std::ifstream stream(std::filesystem::path(cover_path), std::ios::binary);
			if (!stream)
				return false;

			const std::vector<char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
			if (bytes.empty())
				return false;

			rpng_t *rpng = rpng_alloc();
			if (rpng == nullptr)
				return false;

			bool success = false;
			std::uint32_t *pixel_data = nullptr;
			unsigned width = 0;
			unsigned height = 0;
			if (rpng_set_buf_ptr(rpng, const_cast<char *>(bytes.data()), bytes.size()) &&
				rpng_start(rpng))
			{
				while (rpng_iterate_image(rpng))
				{
				}

				if (rpng_is_valid(rpng))
				{
					int result = IMAGE_PROCESS_NEXT;
					do
					{
						result = rpng_process_image(
							rpng,
							reinterpret_cast<void **>(&pixel_data),
							bytes.size(),
							&width,
							&height,
							true);
					} while (result == IMAGE_PROCESS_NEXT);

					if ((result == IMAGE_PROCESS_END || result > IMAGE_PROCESS_END) &&
						pixel_data != nullptr &&
						width > 0 &&
						height > 0)
					{
						decoded.width = width;
						decoded.height = height;
						decoded.pixels.assign(pixel_data, pixel_data + static_cast<size_t>(width) * height);
						success = true;
					}
				}
			}

			free(pixel_data);
			rpng_free(rpng);
			return success;
		}

		bool upload_cover_texture(const DecodedPng &decoded, GLuint &texture)
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
		clear();
	}

	std::optional<ImTextureRef> OpenGlCoverTextures::get_texture_ref(const std::string &cover_path)
	{
		if (cover_path.empty())
			return std::nullopt;

		const auto it = textures_.find(cover_path);
		if (it != textures_.end())
		{
			if (it->second == 0)
				return std::nullopt;

			return make_imgui_texture_ref(it->second);
		}

		DecodedPng decoded;
		GLuint texture = 0;
		if (decode_png_file(cover_path, decoded))
			upload_cover_texture(decoded, texture);
		textures_.emplace(cover_path, texture);
		if (texture == 0)
			return std::nullopt;

		return make_imgui_texture_ref(texture);
	}

	void OpenGlCoverTextures::sync_song_browser_directory(const SongBrowserView &browser)
	{
		if (cached_browser_path_ == browser.current_path)
			return;

		clear();
		cached_browser_path_ = browser.current_path;
		for (const SongListItem &entry : browser.entries)
		{
			if (!entry.cover_art_path.empty())
				get_texture_ref(entry.cover_art_path);
		}
	}

	void OpenGlCoverTextures::clear()
	{
		for (auto &[path, texture] : textures_)
		{
			(void)path;
			if (texture != 0)
				glDeleteTextures(1, &texture);
		}

		textures_.clear();
		cached_browser_path_.clear();
	}
}
