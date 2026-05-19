#pragma once

#include <imgui.h>

namespace rhythmreplugged::ui
{
	int wheel_steps_from_delta(float wheel_delta);
	bool mouse_scrolling(const ImGuiIO &io);
	bool should_follow_selection(bool content_changed, bool selection_changed, const ImGuiIO &io);
	int wrap_menu_index(int current_index, int delta, int item_count);
	int clamp_menu_index(int current_index, int delta, int item_count);
	void queue_selected_index_change(bool activated, int index, int &pending_selected_index);
	void sync_hovered_index(bool hovered, int index, int &selected_index, int &hovered_index);
}
