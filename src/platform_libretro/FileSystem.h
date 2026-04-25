#pragma once

#include "frontend_contract/RetroFileSystem.h"

#include <libretro.h>

namespace rhythmreplugged::platform_libretro
{
	class FileSystem : public ::rhythmreplugged::frontend_contract::IRetroFileSystem
	{
	public:
		void set_vfs_interface(const retro_vfs_interface *vfs_interface);
		bool has_vfs_interface() const;

		std::string canonicalize_path(const std::string &path) const override;
		std::string parent_path(const std::string &path) const override;
		bool path_exists(const std::string &path) const override;
		bool path_is_directory(const std::string &path) const override;
		std::optional<std::uint64_t> file_size(const std::string &path) const override;
		std::vector<::rhythmreplugged::frontend_contract::RetroDirectoryEntry> list_directory(const std::string &path) const override;
		std::optional<std::string> read_text_file(const std::string &path) const override;
		std::optional<std::vector<std::uint8_t>> read_binary_file(const std::string &path) const override;

	private:
		const retro_vfs_interface *vfs_interface_ = nullptr;
	};
}
