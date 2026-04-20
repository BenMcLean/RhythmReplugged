#pragma once

#include "libretro_contract/RetroInput.h"

#include <imgui.h>

namespace rhythmreplugged
{
	void initialize_imgui_libretro_platform();
	void begin_imgui_libretro_frame(const RetroInputState &input, ImVec2 display_size, float delta_seconds);
}
