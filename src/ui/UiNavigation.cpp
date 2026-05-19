#include "ui/UiNavigation.h"

#include <algorithm>

namespace rhythmreplugged::ui
{
	int wheel_steps_from_delta(float wheel_delta)
	{
		if (wheel_delta == 0.0f)
			return 0;

		const int truncated_steps = static_cast<int>(wheel_delta);
		if (truncated_steps != 0)
			return truncated_steps;

		return wheel_delta > 0.0f ? 1 : -1;
	}

	bool mouse_scrolling(const ImGuiIO &io)
	{
		return io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f;
	}

	bool should_follow_selection(bool content_changed, bool selection_changed, const ImGuiIO &io)
	{
		return (content_changed || selection_changed) && !mouse_scrolling(io);
	}

	int wrap_menu_index(int current_index, int delta, int item_count)
	{
		if (item_count <= 0)
			return 0;

		int next_index = (current_index + delta) % item_count;
		if (next_index < 0)
			next_index += item_count;
		return next_index;
	}

	int clamp_menu_index(int current_index, int delta, int item_count)
	{
		if (item_count <= 0)
			return 0;

		return (std::clamp)(current_index + delta, 0, item_count - 1);
	}

	void queue_selected_index_change(bool activated, int index, int &pending_selected_index)
	{
		if (activated)
			pending_selected_index = index;
	}

	void sync_hovered_index(bool hovered, int index, int &selected_index, int &hovered_index)
	{
		if (!hovered)
			return;

		hovered_index = index;
		selected_index = index;
	}
}
