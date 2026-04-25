#pragma once

#include "frontend_contract/RetroInput.h"

#include <imgui.h>

namespace rhythmreplugged::platform_libretro
{
	void initialize_imgui_libretro_platform();
	void begin_imgui_libretro_frame(const ::rhythmreplugged::frontend_contract::RetroInputState &input, ImVec2 display_size, float delta_seconds);
}
