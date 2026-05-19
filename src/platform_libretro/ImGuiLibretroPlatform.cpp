#include "platform_libretro/ImGuiLibretroPlatform.h"

#include <cfloat>

namespace rhythmreplugged::platform_libretro
{
	namespace
	{
		void set_key(ImGuiIO &io, ImGuiKey key, bool pressed)
		{
			io.AddKeyEvent(key, pressed);
		}

		bool has_navigation_input(const ::rhythmreplugged::frontend_contract::RetroInputState &input)
		{
			if (input.up || input.down || input.left || input.right ||
				input.a || input.b || input.x || input.y ||
				input.start || input.select || input.l || input.r)
			{
				return true;
			}

			for (const bool key_pressed : input.letter_keys)
			{
				if (key_pressed)
					return true;
			}

			return false;
		}
	}

	void initialize_imgui_libretro_platform()
	{
		ImGuiIO &io = ImGui::GetIO();
		io.BackendPlatformName = "rhythmreplugged_libretro";
	}

	void begin_imgui_libretro_frame(const ::rhythmreplugged::frontend_contract::RetroInputState &input, ImVec2 display_size, float delta_seconds)
	{
		ImGuiIO &io = ImGui::GetIO();
		io.DisplaySize = display_size;
		io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
		io.DeltaTime = delta_seconds > 0.0f ? delta_seconds : (1.0f / 60.0f);

		set_key(io, ImGuiKey_GamepadDpadUp, input.up);
		set_key(io, ImGuiKey_GamepadDpadDown, input.down);
		set_key(io, ImGuiKey_GamepadDpadLeft, input.left);
		set_key(io, ImGuiKey_GamepadDpadRight, input.right);
		set_key(io, ImGuiKey_GamepadFaceDown, input.a);
		set_key(io, ImGuiKey_GamepadFaceRight, input.b);
		set_key(io, ImGuiKey_GamepadFaceLeft, input.y);
		set_key(io, ImGuiKey_GamepadFaceUp, input.x);
		set_key(io, ImGuiKey_GamepadStart, input.start);
		set_key(io, ImGuiKey_GamepadBack, input.select);
		set_key(io, ImGuiKey_GamepadL1, input.l);
		set_key(io, ImGuiKey_GamepadR1, input.r);

		const bool nav_active = has_navigation_input(input);
		const bool mouse_active = input.mouse_active ||
			input.mouse_left ||
			input.mouse_right ||
			input.mouse_middle ||
			input.mouse_wheel_x != 0.0f ||
			input.mouse_wheel_y != 0.0f;
		const bool use_mouse = mouse_active && !nav_active;

		io.MouseDrawCursor = use_mouse;
		if (use_mouse)
		{
			io.AddMousePosEvent(input.mouse_x, input.mouse_y);
			io.AddMouseWheelEvent(input.mouse_wheel_x, input.mouse_wheel_y);
		}
		else
		{
			io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
		}

		io.AddMouseButtonEvent(0, use_mouse && input.mouse_left);
		io.AddMouseButtonEvent(1, use_mouse && input.mouse_right);
		io.AddMouseButtonEvent(2, use_mouse && input.mouse_middle);

		ImGui::NewFrame();
	}
}
