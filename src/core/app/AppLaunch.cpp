#include "core/app/AppLaunch.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace rhythmreplugged::core
{
	namespace
	{
		std::string to_lower_copy(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(),
				[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
			return text;
		}

		std::string file_name_of(const std::string &path)
		{
			const size_t slash = path.find_last_of("/\\");
			return slash == std::string::npos ? path : path.substr(slash + 1);
		}

		bool is_specific_song_ini_content(
			const ::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system,
			const std::string &content_path)
		{
			if (content_path.empty())
				return false;

			const std::string canonical_path = file_system.canonicalize_path(content_path);
			if (canonical_path.empty() || file_system.path_is_directory(canonical_path))
				return false;

			return to_lower_copy(file_name_of(canonical_path)) == "song.ini";
		}

		bool is_path_absolute(std::string_view path)
		{
			if (path.empty())
				return false;

			if (path.size() >= 1 && (path[0] == '/' || path[0] == '\\'))
				return true;

			return path.size() >= 3 &&
				std::isalpha(static_cast<unsigned char>(path[0])) != 0 &&
				path[1] == ':' &&
				(path[2] == '/' || path[2] == '\\');
		}

		std::string join_paths(std::string_view base, std::string_view child)
		{
			if (base.empty())
				return std::string(child);
			if (child.empty())
				return std::string(base);

			std::string joined(base);
			if (joined.back() != '/' && joined.back() != '\\')
				joined.push_back('/');
			joined.append(child);
			return joined;
		}

		std::string canonicalize_directory_if_valid(const ::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system, const std::string &path)
		{
			if (path.empty())
				return {};

			const std::string canonical_path = file_system.canonicalize_path(path);
			if (canonical_path.empty() || !file_system.path_is_directory(canonical_path))
				return {};

			return canonical_path;
		}

		std::string resolve_directory_path(
			const ::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system,
			const std::string &path,
			const std::string &base_path)
		{
			if (path.empty())
				return {};

			if (is_path_absolute(path))
				return canonicalize_directory_if_valid(file_system, path);

			if (!base_path.empty())
			{
				const std::string resolved = canonicalize_directory_if_valid(file_system, join_paths(base_path, path));
				if (!resolved.empty())
					return resolved;
			}

			return canonicalize_directory_if_valid(file_system, path);
		}

		std::string resolve_path(
			const ::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system,
			const std::string &path,
			const std::string &base_path)
		{
			if (path.empty())
				return {};

			if (is_path_absolute(path))
				return file_system.canonicalize_path(path);

			if (!base_path.empty())
			{
				const std::string resolved = file_system.canonicalize_path(join_paths(base_path, path));
				if (!resolved.empty())
					return resolved;
			}

			return file_system.canonicalize_path(path);
		}

		bool path_has_prefix(std::string_view path, std::string_view prefix)
		{
			if (prefix.empty() || path.size() < prefix.size())
				return false;
			if (path.compare(0, prefix.size(), prefix) != 0)
				return false;
			if (path.size() == prefix.size())
				return true;

			const char next = path[prefix.size()];
			return next == '/' || next == '\\';
		}

		std::string derive_song_directory(const ::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system, const std::string &content_path)
		{
			if (content_path.empty())
				return {};

			const std::string canonical_path = file_system.canonicalize_path(content_path);
			if (canonical_path.empty())
				return {};

			if (file_system.path_is_directory(canonical_path))
			{
				return file_system.path_exists(canonical_path + "/song.ini")
					? canonical_path
					: std::string();
			}

			const std::string parent = file_system.parent_path(canonical_path);
			return file_system.path_exists(parent + "/song.ini")
				? parent
				: std::string();
		}
	}

	AppLaunchRequest resolve_app_launch_request(
		const ::rhythmreplugged::frontend_contract::IRetroFileSystem &file_system,
		const AppLaunchInputs &inputs)
	{
		AppLaunchRequest request;
		request.frontend_options = inputs.frontend_options;
		const std::string working_directory = canonicalize_directory_if_valid(file_system, ".");
		const std::string fallback_root = resolve_directory_path(
			file_system,
			inputs.fallback_songs_root_path,
			working_directory);
		const std::string content_root = resolve_directory_path(
			file_system,
			inputs.content_root_path,
			working_directory);
		const std::string content_base = !content_root.empty() ? content_root : working_directory;
		const std::string resolved_content_path = resolve_path(
			file_system,
			inputs.content_path,
			content_base);
		request.startup_song_path = derive_song_directory(file_system, resolved_content_path);
		request.restrict_to_startup_song = is_specific_song_ini_content(file_system, resolved_content_path);

		const std::string songs_root_base = !content_root.empty() ? content_root : working_directory;
		request.songs_root_path = resolve_directory_path(
			file_system,
			inputs.songs_root_path,
			songs_root_base);
		if (!request.songs_root_path.empty())
			return request;

		if (!request.startup_song_path.empty())
		{
			if (!content_root.empty() && path_has_prefix(request.startup_song_path, content_root))
				request.songs_root_path = content_root;
			else
				request.songs_root_path = request.startup_song_path;
			return request;
		}

		const std::string content_directory = canonicalize_directory_if_valid(file_system, resolved_content_path);
		if (!content_directory.empty())
		{
			request.songs_root_path = content_directory;
			return request;
		}

		request.songs_root_path = !content_root.empty() ? content_root : fallback_root;
		return request;
	}
}
