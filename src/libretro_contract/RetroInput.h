#pragma once

namespace rhythmreplugged
{
	struct RetroInputState
	{
		bool up = false;
		bool down = false;
		bool left = false;
		bool right = false;
		bool a = false;
		bool b = false;
		bool x = false;
		bool y = false;
		bool start = false;
		bool select = false;
		bool l = false;
		bool r = false;
		bool mouse_active = false;
		float mouse_x = 0.0f;
		float mouse_y = 0.0f;
		bool mouse_left = false;
		bool mouse_right = false;
		bool mouse_middle = false;
		float mouse_wheel_x = 0.0f;
		float mouse_wheel_y = 0.0f;
	};
}
