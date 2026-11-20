// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// Reads just enough of a Skyrim Special Edition archive (BSA, version 104 or 105) to answer "does this archive hold this
// file?": the header, the folder table and, for one folder, its file records. Format and name hash: the public UESP page
// "Tes5Mod:Archive File Format". No game types, no engine calls: unit-tested offline.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ti
{
	/** Hash of a folder path ("meshes\\architecture\\whiterun") as the archive stores it. Case and slash direction do not matter. */
	std::uint64_t BsaFolderHash(std::string_view a_folder);
	/** Hash of a file name with extension ("wrwoodpost02.dds"). */
	std::uint64_t BsaFileHash(std::string_view a_fileName);

	/** Does archive file name a_archive (without folder) belong to plugin a_plugin: "<plugin stem>.bsa" or "<plugin stem> - <anything>.bsa"? */
	bool ArchiveBelongsToPlugin(std::string_view a_archive, std::string_view a_plugin);

	class BsaIndex
	{
	public:
		/** Opens the archive and reads its folder table. Empty when it is not a readable BSA 104/105. */
		static std::optional<BsaIndex> Open(const std::filesystem::path& a_path);

		/** True when the archive holds a_path (Data-relative, any case, either slash). */
		bool Contains(std::string_view a_path) const;

		const std::filesystem::path& Path() const { return path_; }
		std::uint32_t                FileCount() const { return fileCount_; }
		std::uint32_t                Version() const { return version_; }

	private:
		struct Folder
		{
			std::uint64_t hash = 0;
			std::uint32_t count = 0;
			std::uint32_t offset = 0;
		};

		std::filesystem::path path_;
		std::uint32_t         version_ = 0;
		std::uint32_t         flags_ = 0;
		std::uint32_t         fileCount_ = 0;
		std::uint32_t         totalFileNameLength_ = 0;
		std::vector<Folder>   folders_;  // sorted by hash (the archive stores them sorted)
	};
}
