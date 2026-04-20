#pragma once

#include "core/app/AppCore.h"
#include "ui/OpenGlCoverTextures.h"

#include <imgui.h>

namespace rhythmreplugged
{
	inline constexpr float kDefaultUiScale = 2.0f;

	void initialize_app_imgui(float ui_scale);
	void render_app_ui(AppCore &app, ImVec2 window_size, float ui_scale, OpenGlCoverTextures &cover_textures);
}
