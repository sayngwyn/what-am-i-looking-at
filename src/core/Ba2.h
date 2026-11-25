// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// Reads the index of a Fallout 4 archive (BA2, versions 1, 7 and 8; GNRL "general" and DX10 "texture" kinds) well enough to answer
// "does this archive hold this path, and what does the entry look like?": the header, the file records and the name table. File
// data is never read. Format: the public UESP/xEdit-community descriptions of the BA2 layout (a 24-byte header, 36-byte GNRL
// records, DX10 records with 24-byte chunks, a name table of length-prefixed strings at the offset the header gives).
// No game types, no engine calls: unit-tested offline against real archives.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ti
{
	/** Lowercase, '/' to '\\', no leading slashes and no leading "data\\": the form every Ba2Index lookup uses. */
	std::string Ba2NormalisedPath(std::string_view a_path);

	/** What the index knows about one entry (no file data). */
	struct Ba2Entry
	{
		std::uint64_t pathHash = 0;       ///< hash of the normalised path (this reader's own, not the archive's name hash)
		std::uint64_t unpackedSize = 0;   ///< all chunks together for a texture
		std::uint64_t packedSize = 0;     ///< 0 when stored uncompressed
		std::uint16_t width = 0;          ///< textures only
		std::uint16_t height = 0;
		std::uint8_t  mips = 0;
		std::uint8_t  dxgi = 0;           ///< DXGI_FORMAT value the archive stores for a texture (BC1 = 71, BC5 = 83, BC7 = 98 ...)
		std::uint8_t  chunks = 0;         ///< textures: number of data chunks; 1 for a general file
		bool          cubemap = false;
		std::uint32_t chunkFirst = 0;     ///< textures: where this entry's chunks start in the index's chunk list
		std::uint64_t dataOffset = 0;     ///< general files: where the stored bytes start in the archive
	};

	/** One stored piece of a texture: some mip levels, zlib-packed (packed > 0) or raw (packed == 0). */
	struct Ba2Chunk
	{
		std::uint64_t offset = 0;    ///< in the archive file
		std::uint32_t packed = 0;
		std::uint32_t unpacked = 0;
		std::uint16_t startMip = 0;
		std::uint16_t endMip = 0;
	};

	class Ba2Index
	{
	public:
		enum class Kind
		{
			kGeneral,  ///< "GNRL": meshes, materials, sounds, scripts ...
			kTexture   ///< "DX10": textures
		};

		/** Opens the archive and reads its records and name table. Empty when it is not a readable BA2 (version 1, 7 or 8). keepNames also keeps every path (tests and listings). */
		static std::optional<Ba2Index> Open(const std::filesystem::path& a_path, bool a_keepNames = false);

		/** True when the archive holds a_path (Data-relative, any case, either slash). */
		bool Contains(std::string_view a_path) const;
		/** The entry for a_path, or null. Valid while this index lives. */
		const Ba2Entry* Find(std::string_view a_path) const;

		const std::filesystem::path& Path() const { return path_; }
		Kind                         ArchiveKind() const { return kind_; }
		std::uint32_t                Version() const { return version_; }
		std::uint32_t                FileCount() const { return static_cast<std::uint32_t>(entries_.size()); }
		/**
		 * A small copy of a texture entry as a complete DDS (a DX10 header, then the data): the first mip level at most a_maxSide on its
		 * long side, and every smaller level after it. Only the stored chunks that hold those levels are read and unpacked. Empty with
		 * a_why filled in when the entry is not a plain 2D texture in a format this knows, or the file cannot be read.
		 */
		std::vector<std::uint8_t> ReadSmallDds(const Ba2Entry& a_entry, int a_maxSide, std::string* a_why = nullptr) const;

		/**
		 * The whole file of a general entry (a mesh, a material), unpacked. Empty with a_why filled in when the entry is a texture, is
		 * larger than a_maxBytes, or cannot be read. The archive is opened, read and closed again.
		 */
		std::vector<std::uint8_t> ReadFile(const Ba2Entry& a_entry, std::size_t a_maxBytes, std::string* a_why = nullptr) const;

		/** Every path (normalised), in archive order; empty unless Open was asked to keep names. */
		const std::vector<std::string>& Names() const { return names_; }

	private:
		std::filesystem::path    path_;
		Kind                     kind_ = Kind::kGeneral;
		std::uint32_t            version_ = 0;
		std::vector<Ba2Entry>    entries_;  // sorted by pathHash
		std::vector<Ba2Chunk>    chunks_;   // every texture entry's chunks, in archive order
		std::vector<std::string> names_;
	};

	/**
	 * Which archive wins a path. The archives are given in load order (earlier first); the last one that holds the path wins, as
	 * in the game. Returns the index into a_archives, or -1.
	 */
	int Ba2Winner(const std::vector<Ba2Index>& a_archives, std::string_view a_path);

	/** Does archive file name a_archive (without folder) belong to plugin a_plugin: "<plugin stem>.ba2" or "<plugin stem> - <anything>.ba2"? Any case. */
	bool Ba2BelongsToPlugin(std::string_view a_archive, std::string_view a_plugin);
}
