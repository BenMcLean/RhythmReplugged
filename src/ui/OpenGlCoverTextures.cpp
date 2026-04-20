#include "ui/OpenGlCoverTextures.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3_image/SDL_image.h>

#include <filesystem>

namespace rhythmreplugged
{
	namespace
	{
		ImTextureRef make_imgui_texture_ref(GLuint texture)
		{
			return ImTextureRef(static_cast<ImTextureID>(static_cast<uintptr_t>(texture)));
		}

		bool upload_cover_texture(SDL_Surface *surface, GLuint &texture)
		{
			texture = 0;
			if (surface == nullptr)
				return false;

			SDL_Surface *converted_surface = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ABGR8888);
			if (converted_surface == nullptr)
				return false;

			glGenTextures(1, &texture);
			if (texture == 0)
			{
				SDL_DestroySurface(converted_surface);
				return false;
			}

			glBindTexture(GL_TEXTURE_2D, texture);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glTexImage2D(GL_TEXTURE_2D,
				0,
				GL_RGBA,
				converted_surface->w,
				converted_surface->h,
				0,
				GL_RGBA,
				GL_UNSIGNED_BYTE,
				converted_surface->pixels);
			glBindTexture(GL_TEXTURE_2D, 0);

			SDL_DestroySurface(converted_surface);
			return true;
		}

		bool load_cover_texture(const std::string &cover_path, GLuint &texture)
		{
			texture = 0;

			SDL_Surface *surface = IMG_Load(cover_path.c_str());
			if (surface != nullptr)
			{
				const bool uploaded = upload_cover_texture(surface, texture);
				SDL_DestroySurface(surface);
				return uploaded;
			}

			const std::string extension = std::filesystem::path(cover_path).extension().string();
			if (_stricmp(extension.c_str(), ".png") != 0)
				return false;

			surface = SDL_LoadPNG(cover_path.c_str());
			if (surface == nullptr)
				return false;

			const bool uploaded = upload_cover_texture(surface, texture);
			SDL_DestroySurface(surface);
			return uploaded;
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

		GLuint texture = 0;
		load_cover_texture(cover_path, texture);
		textures_.emplace(cover_path, texture);
		if (texture == 0)
			return std::nullopt;

		return make_imgui_texture_ref(texture);
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
	}
}
