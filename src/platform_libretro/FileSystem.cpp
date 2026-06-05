#include "platform_libretro/FileSystem.h"

#include "core/utils/TextDecoding.h"

#include <cstring>
#include <vector>

namespace rhythmreplugged::platform_libretro
{
	namespace
	{
		std::string normalize_generic_path(std::string path)
		{
			if (path.empty())
				return {};

			for (char &ch : path)
			{
				if (ch == '\\')
					ch = '/';
			}

			std::vector<std::string> parts;
			parts.reserve(16);
			const bool is_absolute = !path.empty() && path.front() == '/';
			size_t index = 0;
			while (index < path.size())
			{
				while (index < path.size() && path[index] == '/')
					++index;
				if (index >= path.size())
					break;

				size_t next = index;
				while (next < path.size() && path[next] != '/')
					++next;

				const std::string_view component(path.data() + index, next - index);
				if (component == ".")
				{
					index = next;
					continue;
				}

				if (component == "..")
				{
					if (!parts.empty() && parts.back() != "..")
						parts.pop_back();
					else if (!is_absolute)
						parts.emplace_back(component);
					index = next;
					continue;
				}

				parts.emplace_back(component);
				index = next;
			}

			std::string normalized;
			if (is_absolute)
				normalized.push_back('/');

			for (size_t part_index = 0; part_index < parts.size(); ++part_index)
			{
				if (part_index > 0)
					normalized.push_back('/');
				normalized.append(parts[part_index]);
			}

			if (normalized.empty())
				return is_absolute ? std::string("/") : std::string(".");

			return normalized;
		}

		std::string join_generic_paths(std::string_view base, std::string_view child)
		{
			if (base.empty())
				return normalize_generic_path(std::string(child));
			if (child.empty())
				return normalize_generic_path(std::string(base));

			std::string joined(base);
			if (joined.back() != '/' && joined.back() != '\\')
				joined.push_back('/');
			joined.append(child);
			return normalize_generic_path(std::move(joined));
		}

		std::string generic_parent_path(const std::string &path)
		{
			const std::string normalized = normalize_generic_path(path);
			if (normalized.empty() || normalized == "." || normalized == "/")
				return normalized;

			const size_t slash = normalized.find_last_of('/');
			if (slash == std::string::npos)
				return ".";
			if (slash == 0)
				return "/";
			return normalized.substr(0, slash);
		}

		std::optional<int64_t> stat_path_with_vfs(
			uint32_t vfs_interface_version,
			const retro_vfs_interface *vfs_interface,
			const std::string &path,
			bool &is_directory)
		{
			if (vfs_interface == nullptr)
				return std::nullopt;

			if (vfs_interface_version >= 4 && vfs_interface->stat_64 != nullptr)
			{
				int64_t size = 0;
				const int flags = vfs_interface->stat_64(path.c_str(), &size);
				if ((flags & RETRO_VFS_STAT_IS_VALID) == 0)
					return std::nullopt;

				is_directory = (flags & RETRO_VFS_STAT_IS_DIRECTORY) != 0;
				return size;
			}

			if (vfs_interface->stat != nullptr)
			{
				int32_t size = 0;
				const int flags = vfs_interface->stat(path.c_str(), &size);
				if ((flags & RETRO_VFS_STAT_IS_VALID) == 0)
					return std::nullopt;

				is_directory = (flags & RETRO_VFS_STAT_IS_DIRECTORY) != 0;
				return static_cast<int64_t>(size);
			}

			return std::nullopt;
		}

		bool can_open_directory_with_vfs(const retro_vfs_interface *vfs_interface, const std::string &path)
		{
			if (vfs_interface == nullptr || vfs_interface->opendir == nullptr || vfs_interface->closedir == nullptr)
				return false;

			retro_vfs_dir_handle *directory = vfs_interface->opendir(path.c_str(), false);
			if (directory == nullptr && !path.empty() && path.back() != '/')
			{
				const std::string slash_path = path + "/";
				directory = vfs_interface->opendir(slash_path.c_str(), false);
			}
			if (directory == nullptr)
				return false;

			vfs_interface->closedir(directory);
			return true;
		}

		bool can_open_file_with_vfs(const retro_vfs_interface *vfs_interface, const std::string &path)
		{
			if (vfs_interface == nullptr || vfs_interface->open == nullptr || vfs_interface->close == nullptr)
				return false;

			retro_vfs_file_handle *file = vfs_interface->open(
				path.c_str(),
				RETRO_VFS_FILE_ACCESS_READ,
				RETRO_VFS_FILE_ACCESS_HINT_NONE);
			if (file == nullptr)
				return false;

			vfs_interface->close(file);
			return true;
		}
	}

	void FileSystem::set_vfs_interface(uint32_t vfs_interface_version, const retro_vfs_interface *vfs_interface)
	{
		vfs_interface_version_ = vfs_interface_version;
		vfs_interface_ = vfs_interface;
	}

	bool FileSystem::has_vfs_interface() const
	{
		return vfs_interface_ != nullptr;
	}

	std::string FileSystem::canonicalize_path(const std::string &path) const
	{
		if (path.empty())
			return {};

		return normalize_generic_path(path);
	}

	std::string FileSystem::parent_path(const std::string &path) const
	{
		return generic_parent_path(path);
	}

	bool FileSystem::path_exists(const std::string &path) const
	{
		bool is_directory = false;
		if (stat_path_with_vfs(vfs_interface_version_, vfs_interface_, path, is_directory).has_value())
			return true;

		return can_open_file_with_vfs(vfs_interface_, path);
	}

	bool FileSystem::path_is_directory(const std::string &path) const
	{
		bool is_directory = false;
		if (can_open_directory_with_vfs(vfs_interface_, path))
			return true;

		const std::optional<int64_t> size = stat_path_with_vfs(vfs_interface_version_, vfs_interface_, path, is_directory);
		return size.has_value() && is_directory;
	}

	std::optional<std::uint64_t> FileSystem::file_size(const std::string &path) const
	{
		bool is_directory = false;
		const std::optional<int64_t> size = stat_path_with_vfs(vfs_interface_version_, vfs_interface_, path, is_directory);
		if (!size.has_value() || is_directory || *size < 0)
			return std::nullopt;

		return static_cast<std::uint64_t>(*size);
	}

	std::vector<::rhythmreplugged::frontend_contract::RetroDirectoryEntry> FileSystem::list_directory(const std::string &path) const
	{
		std::vector<::rhythmreplugged::frontend_contract::RetroDirectoryEntry> entries;
		// Do not fall back to host directory APIs here. Libretro content access
		// is defined entirely in terms of VFS, and missing VFS support should be
		// surfaced as a frontend contract failure by higher-level startup code.
		if (vfs_interface_ == nullptr || vfs_interface_->opendir == nullptr || vfs_interface_->readdir == nullptr ||
			vfs_interface_->dirent_get_name == nullptr || vfs_interface_->dirent_is_dir == nullptr ||
			vfs_interface_->closedir == nullptr)
		{
			return entries;
		}

		retro_vfs_dir_handle *directory = vfs_interface_->opendir(path.c_str(), false);
		if (directory == nullptr && !path.empty() && path.back() != '/')
		{
			const std::string slash_path = path + "/";
			directory = vfs_interface_->opendir(slash_path.c_str(), false);
		}
		if (directory == nullptr)
			return entries;

		while (vfs_interface_->readdir(directory))
		{
			const char *name = vfs_interface_->dirent_get_name(directory);
			if (name == nullptr)
				continue;
			if (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0)
				continue;

			::rhythmreplugged::frontend_contract::RetroDirectoryEntry entry;
			entry.name = name;
			entry.path = join_generic_paths(path, entry.name);
			entry.is_directory = vfs_interface_->dirent_is_dir(directory);
			entries.push_back(std::move(entry));
		}

		vfs_interface_->closedir(directory);
		return entries;
	}

	std::optional<std::string> FileSystem::read_text_file(const std::string &path) const
	{
		const std::optional<std::vector<std::uint8_t>> bytes = read_binary_file(path);
		if (!bytes.has_value())
			return std::nullopt;

		std::vector<char> text_bytes(bytes->size());
		for (size_t index = 0; index < bytes->size(); ++index)
			text_bytes[index] = static_cast<char>((*bytes)[index]);
		return core::utils::decode_text_bytes(text_bytes);
	}

	std::optional<std::vector<std::uint8_t>> FileSystem::read_binary_file(const std::string &path) const
	{
		// The libretro core must only read files through the frontend-provided
		// VFS interface. Returning nullopt here allows startup/load code to log
		// and fail hard instead of silently escaping to native file APIs.
		if (vfs_interface_ == nullptr || vfs_interface_->open == nullptr || vfs_interface_->close == nullptr ||
			vfs_interface_->size == nullptr || vfs_interface_->read == nullptr)
		{
			return std::nullopt;
		}

		retro_vfs_file_handle *file = vfs_interface_->open(
			path.c_str(),
			RETRO_VFS_FILE_ACCESS_READ,
			RETRO_VFS_FILE_ACCESS_HINT_FREQUENT_ACCESS);
		if (file == nullptr)
			return std::nullopt;

		const int64_t raw_size = vfs_interface_->size(file);
		if (raw_size < 0)
		{
			vfs_interface_->close(file);
			return std::nullopt;
		}

		std::vector<std::uint8_t> output(static_cast<size_t>(raw_size));
		size_t total_read = 0;
		while (total_read < output.size())
		{
			const int64_t read_count = vfs_interface_->read(
				file,
				output.data() + total_read,
				static_cast<uint64_t>(output.size() - total_read));
			if (read_count <= 0)
			{
				vfs_interface_->close(file);
				return std::nullopt;
			}

			total_read += static_cast<size_t>(read_count);
		}

		vfs_interface_->close(file);
		return output;
	}
}
