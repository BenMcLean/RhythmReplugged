#pragma once

#include <imgui.h>

#include <optional>
#include <string>
#include <unordered_map>

using GLuint = unsigned int;

namespace rhythmreplugged
{
	class OpenGlCoverTextures
	{
	public:
		~OpenGlCoverTextures();

		std::optional<ImTextureRef> get_texture_ref(const std::string &cover_path);
		void clear();

	private:
		std::unordered_map<std::string, GLuint> textures_;
	};
}
