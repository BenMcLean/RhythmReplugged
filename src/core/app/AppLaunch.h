#pragma once

#include "frontend_contract/RetroFileSystem.h"

#include <string>

namespace rhythmreplugged::core
{
	struct AppLaunchInputs
	{
		std::string songs_root_path;
		std::string content_root_path;
		std::string content_path;
		std::string fallback_songs_root_path;
	};

	struct AppLaunchRequest
	{
		std::string songs_root_path;
		std::string startup_song_path;
	};

	AppLaunchRequest resolve_app_launch_request(
		const ::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system,
		const AppLaunchInputs &inputs);
}
