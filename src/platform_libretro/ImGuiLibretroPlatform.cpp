#include "platform_libretro/ImGuiLibretroPlatform.h"

namespace rhythmreplugged
{
	namespace
	{
		void set_key(ImGuiIO &io, ImGuiKey key, bool pressed)
		{
			io.AddKeyEvent(key, pressed);
		}
	}

	void initialize_imgui_libretro_platform()
	{
		ImGuiIO &io = ImGui::GetIO();
		io.BackendPlatformName = "rhythmreplugged_libretro";
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
		io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
	}

	void begin_imgui_libretro_frame(const RetroInputState &input, ImVec2 display_size, float delta_seconds)
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

		ImGui::NewFrame();
	}
}
