#include "platform_libretro/FileSystem.h"

#include "core/utils/TextDecoding.h"

#include <filesystem>
#include <vector>

namespace rhythmreplugged::platform_libretro
{
	void FileSystem::set_vfs_interface(const retro_vfs_interface *vfs_interface)
	{
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
		return std::filesystem::path(path).lexically_normal().generic_string();
	}

	std::string FileSystem::parent_path(const std::string &path) const
	{
		return std::filesystem::path(path).parent_path().generic_string();
	}

	bool FileSystem::path_exists(const std::string &path) const
	{
		if (vfs_interface_ == nullptr || vfs_interface_->stat_64 == nullptr)
			return false;

		int64_t ignored_size = 0;
		return (vfs_interface_->stat_64(path.c_str(), &ignored_size) & RETRO_VFS_STAT_IS_VALID) != 0;
	}

	bool FileSystem::path_is_directory(const std::string &path) const
	{
		if (vfs_interface_ == nullptr || vfs_interface_->stat_64 == nullptr)
			return false;

		int64_t ignored_size = 0;
		return (vfs_interface_->stat_64(path.c_str(), &ignored_size) & RETRO_VFS_STAT_IS_DIRECTORY) != 0;
	}

	std::optional<std::uint64_t> FileSystem::file_size(const std::string &path) const
	{
		if (vfs_interface_ == nullptr || vfs_interface_->stat_64 == nullptr)
			return std::nullopt;

		int64_t size = 0;
		const int flags = vfs_interface_->stat_64(path.c_str(), &size);
		if ((flags & RETRO_VFS_STAT_IS_VALID) == 0 || (flags & RETRO_VFS_STAT_IS_DIRECTORY) != 0 || size < 0)
			return std::nullopt;

		return static_cast<std::uint64_t>(size);
	}

	std::vector<::rhythmreplugged::frontend_contract::RetroDirectoryEntry> FileSystem::list_directory(const std::string &path) const
	{
		std::vector<::rhythmreplugged::frontend_contract::RetroDirectoryEntry> entries;
		if (vfs_interface_ == nullptr || vfs_interface_->opendir == nullptr || vfs_interface_->readdir == nullptr ||
			vfs_interface_->dirent_get_name == nullptr || vfs_interface_->dirent_is_dir == nullptr ||
			vfs_interface_->closedir == nullptr)
		{
			return entries;
		}

		retro_vfs_dir_handle *directory = vfs_interface_->opendir(path.c_str(), false);
		if (directory == nullptr)
			return entries;

		while (vfs_interface_->readdir(directory))
		{
			const char *name = vfs_interface_->dirent_get_name(directory);
			if (name == nullptr)
				continue;

			::rhythmreplugged::frontend_contract::RetroDirectoryEntry entry;
			entry.name = name;
			entry.path = (std::filesystem::path(path) / entry.name).generic_string();
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
