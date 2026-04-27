#include "platform_libretro/FileSystem.h"

#include "core/utils/TextDecoding.h"

#include <filesystem>
#include <fstream>
#include <vector>

namespace rhythmreplugged::platform_libretro
{
	namespace
	{
		std::optional<int64_t> stat_path_with_vfs(uint32_t vfs_interface_version, const retro_vfs_interface *vfs_interface, const std::string &path, bool &is_directory)
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

		return std::filesystem::path(path).lexically_normal().generic_string();
	}

	std::string FileSystem::parent_path(const std::string &path) const
	{
		return std::filesystem::path(path).parent_path().generic_string();
	}

	bool FileSystem::path_exists(const std::string &path) const
	{
		bool is_directory = false;
		if (stat_path_with_vfs(vfs_interface_version_, vfs_interface_, path, is_directory).has_value())
			return true;

		std::error_code error_code;
		return std::filesystem::exists(std::filesystem::path(path), error_code) && !error_code;
	}

	bool FileSystem::path_is_directory(const std::string &path) const
	{
		bool is_directory = false;
		if (stat_path_with_vfs(vfs_interface_version_, vfs_interface_, path, is_directory).has_value())
			return is_directory;

		std::error_code error_code;
		return std::filesystem::is_directory(std::filesystem::path(path), error_code) && !error_code;
	}

	std::optional<std::uint64_t> FileSystem::file_size(const std::string &path) const
	{
		bool is_directory = false;
		const std::optional<int64_t> size = stat_path_with_vfs(vfs_interface_version_, vfs_interface_, path, is_directory);
		if (size.has_value())
		{
			if (is_directory || *size < 0)
				return std::nullopt;

			return static_cast<std::uint64_t>(*size);
		}

		std::error_code error_code;
		const auto native_size = std::filesystem::file_size(std::filesystem::path(path), error_code);
		if (error_code)
			return std::nullopt;

		return static_cast<std::uint64_t>(native_size);
	}

	std::vector<::rhythmreplugged::frontend_contract::RetroDirectoryEntry> FileSystem::list_directory(const std::string &path) const
	{
		std::vector<::rhythmreplugged::frontend_contract::RetroDirectoryEntry> entries;
		if (vfs_interface_ != nullptr && vfs_interface_->opendir != nullptr && vfs_interface_->readdir != nullptr &&
			vfs_interface_->dirent_get_name != nullptr && vfs_interface_->dirent_is_dir != nullptr &&
			vfs_interface_->closedir != nullptr)
		{
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

		std::error_code error_code;
		for (const auto &entry : std::filesystem::directory_iterator(std::filesystem::path(path), error_code))
		{
			if (error_code)
				break;

			::rhythmreplugged::frontend_contract::RetroDirectoryEntry directory_entry;
			directory_entry.path = entry.path().generic_string();
			directory_entry.name = entry.path().filename().string();
			directory_entry.is_directory = entry.is_directory(error_code) && !error_code;
			entries.push_back(std::move(directory_entry));
		}

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
			std::ifstream stream(std::filesystem::path(path), std::ios::binary);
			if (!stream)
				return std::nullopt;

			const std::vector<char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
			std::vector<std::uint8_t> output(bytes.size());
			for (size_t i = 0; i < bytes.size(); ++i)
				output[i] = static_cast<std::uint8_t>(bytes[i]);

			return output;
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
