#pragma once

#include <imgui.h>

#include <optional>
#include <string>
#include <unordered_map>

using GLuint = unsigned int;

namespace rhythmreplugged
{
	struct SongBrowserView;

	class OpenGlCoverTextures
	{
	public:
		~OpenGlCoverTextures();

		std::optional<ImTextureRef> get_texture_ref(const std::string &cover_path);
		void sync_song_browser_directory(const SongBrowserView &browser);
		void clear();

	private:
		std::string cached_browser_path_;
		std::unordered_map<std::string, GLuint> textures_;
	};
}
