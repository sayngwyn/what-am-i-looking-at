// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
#include "Ba2.h"

#include "Inflate.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

namespace ti
{
	namespace
	{
		constexpr std::uint32_t kMagic = 0x58445442;  // "BTDX"
		constexpr std::uint32_t kGeneral = 0x4C524E47;  // "GNRL"
		constexpr std::uint32_t kTexture = 0x30315844;  // "DX10"
		constexpr std::size_t   kHeaderSize = 24;
		constexpr std::size_t   kGeneralRecord = 36;
		constexpr std::size_t   kTextureRecord = 24;
		constexpr std::size_t   kChunkRecord = 24;

		std::uint64_t HashPath(std::string_view a_normalised)
		{
			// FNV-1a, 64 bit, over the normalised path
			std::uint64_t h = 1469598103934665603ull;
			for (const char c : a_normalised) {
				h ^= static_cast<unsigned char>(c);
				h *= 1099511628211ull;
			}
			return h;
		}

		template <class T>
		T Get(const std::uint8_t* a_bytes, std::size_t a_offset)
		{
			T value;
			std::memcpy(&value, a_bytes + a_offset, sizeof(T));
			return value;
		}
	}

	std::string Ba2NormalisedPath(std::string_view a_path)
	{
		std::string s(a_path);
		for (auto& c : s)
			c = c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		// Bethesda's own build path ("C:\projects\Fallout4\Build\PC\Data\materials\x.bgsm", written into vanilla meshes and materials)
		// is not a game path: cut it back to the Data-relative path, or a vanilla file reads as missing.
		if (s.size() > 2 && s[1] == ':') {
			if (const auto at = s.find("\\data\\"); at != std::string::npos) {
				s.erase(0, at + 6);
			} else {
				for (const std::string_view folder : { "\\materials\\", "\\textures\\", "\\meshes\\" }) {
					if (const auto at2 = s.find(folder); at2 != std::string::npos) {
						s.erase(0, at2);
						break;
					}
				}
			}
		}
		while (!s.empty() && s.front() == '\\')
			s.erase(s.begin());
		if (s.size() > 5 && s.compare(0, 5, "data\\") == 0)
			s.erase(0, 5);
		return s;
	}

	std::optional<Ba2Index> Ba2Index::Open(const std::filesystem::path& a_path, bool a_keepNames)
	{
		std::ifstream in(a_path, std::ios::binary);
		if (!in)
			return std::nullopt;
		std::uint8_t header[kHeaderSize]{};
		in.read(reinterpret_cast<char*>(header), sizeof(header));
		if (!in || Get<std::uint32_t>(header, 0) != kMagic)
			return std::nullopt;
		const auto version = Get<std::uint32_t>(header, 4);
		if (version != 1 && version != 7 && version != 8)
			return std::nullopt;
		const auto type = Get<std::uint32_t>(header, 8);
		if (type != kGeneral && type != kTexture)
			return std::nullopt;
		const auto fileCount = Get<std::uint32_t>(header, 12);
		const auto nameTable = Get<std::uint64_t>(header, 16);
		if (nameTable < kHeaderSize || fileCount > 4'000'000u)
			return std::nullopt;

		in.seekg(0, std::ios::end);
		const auto fileSize = static_cast<std::uint64_t>(in.tellg());
		if (nameTable >= fileSize && fileCount > 0)
			return std::nullopt;  // no name table: nothing to look paths up in

		// the record region, in one read
		const std::uint64_t        recordBytes = nameTable - kHeaderSize;
		std::vector<std::uint8_t>  records(static_cast<std::size_t>(recordBytes));
		in.clear();
		in.seekg(static_cast<std::streamoff>(kHeaderSize), std::ios::beg);
		in.read(reinterpret_cast<char*>(records.data()), static_cast<std::streamsize>(records.size()));
		if (!in)
			return std::nullopt;

		Ba2Index index;
		index.path_ = a_path;
		index.kind_ = type == kGeneral ? Kind::kGeneral : Kind::kTexture;
		index.version_ = version;
		std::vector<Ba2Entry> inOrder;
		inOrder.reserve(fileCount);

		std::size_t pos = 0;
		for (std::uint32_t i = 0; i < fileCount; ++i) {
			Ba2Entry e;
			if (type == kGeneral) {
				if (pos + kGeneralRecord > records.size())
					return std::nullopt;
				e.packedSize = Get<std::uint32_t>(records.data(), pos + 24);
				e.unpackedSize = Get<std::uint32_t>(records.data(), pos + 28);
				e.chunks = 1;
				pos += kGeneralRecord;
			} else {
				if (pos + kTextureRecord > records.size())
					return std::nullopt;
				const auto chunks = Get<std::uint8_t>(records.data(), pos + 13);
				const auto chunkHeader = Get<std::uint16_t>(records.data(), pos + 14);
				e.height = Get<std::uint16_t>(records.data(), pos + 16);
				e.width = Get<std::uint16_t>(records.data(), pos + 18);
				e.mips = Get<std::uint8_t>(records.data(), pos + 20);
				e.dxgi = Get<std::uint8_t>(records.data(), pos + 21);
				e.cubemap = (Get<std::uint8_t>(records.data(), pos + 22) & 1) != 0;
				e.chunks = chunks;
				e.chunkFirst = static_cast<std::uint32_t>(index.chunks_.size());
				const std::size_t chunkSize = chunkHeader ? chunkHeader : kChunkRecord;
				if (pos + kTextureRecord + static_cast<std::size_t>(chunks) * chunkSize > records.size())
					return std::nullopt;
				for (std::uint8_t c = 0; c < chunks; ++c) {
					const std::size_t at = pos + kTextureRecord + static_cast<std::size_t>(c) * chunkSize;
					Ba2Chunk chunk;
					chunk.offset = Get<std::uint64_t>(records.data(), at);
					chunk.packed = Get<std::uint32_t>(records.data(), at + 8);
					chunk.unpacked = Get<std::uint32_t>(records.data(), at + 12);
					chunk.startMip = Get<std::uint16_t>(records.data(), at + 16);
					chunk.endMip = Get<std::uint16_t>(records.data(), at + 18);
					index.chunks_.push_back(chunk);
					e.packedSize += chunk.packed;
					e.unpackedSize += chunk.unpacked;
				}
				pos += kTextureRecord + static_cast<std::size_t>(chunks) * chunkSize;
			}
			inOrder.push_back(e);
		}

		// the name table: u16 length, then that many characters, one per file in the same order
		if (fileCount > 0) {
			const std::uint64_t        nameBytes = fileSize - nameTable;
			std::vector<std::uint8_t>  names(static_cast<std::size_t>(nameBytes));
			in.clear();
			in.seekg(static_cast<std::streamoff>(nameTable), std::ios::beg);
			in.read(reinterpret_cast<char*>(names.data()), static_cast<std::streamsize>(names.size()));
			if (!in)
				return std::nullopt;
			std::size_t at = 0;
			for (std::uint32_t i = 0; i < fileCount; ++i) {
				if (at + 2 > names.size())
					return std::nullopt;
				const std::size_t length = Get<std::uint16_t>(names.data(), at);
				at += 2;
				if (at + length > names.size())
					return std::nullopt;
				const std::string path = Ba2NormalisedPath(std::string_view(reinterpret_cast<const char*>(names.data()) + at, length));
				inOrder[i].pathHash = HashPath(path);
				if (a_keepNames)
					index.names_.push_back(path);
				at += length;
			}
		}

		index.entries_ = std::move(inOrder);
		std::stable_sort(index.entries_.begin(), index.entries_.end(), [](const Ba2Entry& a, const Ba2Entry& b) { return a.pathHash < b.pathHash; });
		return index;
	}

	namespace
	{
		/** Bytes per 4x4 block for the block-compressed DXGI formats; 0 when it is not one. */
		std::uint32_t BlockBytes(std::uint32_t a_dxgi)
		{
			if ((a_dxgi >= 70 && a_dxgi <= 72) || (a_dxgi >= 79 && a_dxgi <= 81))
				return 8;
			if ((a_dxgi >= 73 && a_dxgi <= 78) || (a_dxgi >= 82 && a_dxgi <= 84) || (a_dxgi >= 94 && a_dxgi <= 99))
				return 16;
			return 0;
		}

		/** Bytes of one mip level; 0 for a format this does not size. */
		std::uint64_t LevelSize(std::uint32_t a_w, std::uint32_t a_h, std::uint32_t a_dxgi)
		{
			if (const auto block = BlockBytes(a_dxgi))
				return static_cast<std::uint64_t>(std::max(1u, (a_w + 3) / 4)) * std::max(1u, (a_h + 3) / 4) * block;
			if (a_dxgi == 28 || a_dxgi == 29 || a_dxgi == 87 || a_dxgi == 91)
				return static_cast<std::uint64_t>(a_w) * a_h * 4;
			if (a_dxgi == 61)
				return static_cast<std::uint64_t>(a_w) * a_h;
			return 0;
		}

		void Put32(std::vector<std::uint8_t>& a_out, std::uint32_t a_value)
		{
			for (int i = 0; i < 4; ++i)
				a_out.push_back(static_cast<std::uint8_t>(a_value >> (8 * i)));
		}
	}

	std::vector<std::uint8_t> Ba2Index::ReadSmallDds(const Ba2Entry& a_entry, int a_maxSide, std::string* a_why) const
	{
		const auto fail = [&](const char* a_text) {
			if (a_why)
				*a_why = a_text;
			return std::vector<std::uint8_t>();
		};
		if (kind_ != Kind::kTexture || a_entry.chunks == 0 || a_entry.width == 0 || a_entry.height == 0 || a_entry.mips == 0)
			return fail("not a texture entry");
		if (a_entry.cubemap)
			return fail("a cube map");
		const std::uint32_t w = a_entry.width, h = a_entry.height, mips = a_entry.mips, dxgi = a_entry.dxgi;
		if (LevelSize(w, h, dxgi) == 0)
			return fail("a pixel format this does not size");
		std::uint32_t level = 0;
		while (level + 1 < mips && std::max(w >> level, h >> level) > static_cast<std::uint32_t>(a_maxSide))
			++level;
		const auto size = [&](std::uint32_t a_level) { return LevelSize(std::max(1u, w >> a_level), std::max(1u, h >> a_level), dxgi); };
		std::uint64_t wanted = 0;
		for (std::uint32_t i = level; i < mips; ++i)
			wanted += size(i);
		if (wanted == 0 || wanted > 8u * 1024 * 1024)
			return fail("the small levels are missing or too large");

		std::ifstream in(path_, std::ios::binary);
		if (!in)
			return fail("the archive cannot be opened");
		std::vector<std::uint8_t> data;
		data.reserve(static_cast<std::size_t>(wanted));
		std::uint32_t nextMip = level;  // the next level the data still needs
		for (std::uint32_t c = 0; c < a_entry.chunks && nextMip < mips; ++c) {
			const Ba2Chunk& chunk = chunks_[a_entry.chunkFirst + c];
			if (chunk.endMip < nextMip)
				continue;
			if (chunk.startMip > nextMip)
				return fail("the chunks leave a gap in the mip chain");
			std::uint64_t chunkLevels = 0, skip = 0;
			for (std::uint32_t i = chunk.startMip; i <= chunk.endMip && i < mips; ++i) {
				chunkLevels += size(i);
				if (i < nextMip)
					skip += size(i);
			}
			if (chunkLevels != chunk.unpacked)
				return fail("a chunk size does not match its mip levels");
			const std::size_t         stored = chunk.packed ? chunk.packed : chunk.unpacked;
			std::vector<std::uint8_t> raw(stored);
			in.clear();
			in.seekg(static_cast<std::streamoff>(chunk.offset), std::ios::beg);
			in.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
			if (!in)
				return fail("the archive ends inside a chunk");
			if (chunk.packed) {
				std::vector<std::uint8_t> unpacked;
				if (!ZlibInflate(raw.data(), raw.size(), chunk.unpacked, unpacked))
					return fail("a chunk does not unpack");
				raw = std::move(unpacked);
			}
			data.insert(data.end(), raw.begin() + static_cast<std::ptrdiff_t>(skip), raw.end());
			nextMip = static_cast<std::uint32_t>(chunk.endMip) + 1;
		}
		if (data.size() != wanted)
			return fail("the unpacked data is not the size of the small levels");

		const std::uint32_t       outW = std::max(1u, w >> level), outH = std::max(1u, h >> level);
		std::vector<std::uint8_t> out;
		out.reserve(148 + data.size());
		Put32(out, 0x20534444);  // "DDS "
		Put32(out, 124);
		Put32(out, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000);  // caps, height, width, pixel format, mip count, linear size
		Put32(out, outH);
		Put32(out, outW);
		Put32(out, static_cast<std::uint32_t>(size(level)));
		Put32(out, 0);  // depth
		Put32(out, mips - level);
		for (int i = 0; i < 11; ++i)
			Put32(out, 0);
		Put32(out, 32);  // the pixel format: a DX10 extension follows
		Put32(out, 0x4);
		Put32(out, 0x30315844);  // "DX10"
		for (int i = 0; i < 5; ++i)
			Put32(out, 0);
		Put32(out, 0x1000 | 0x400000 | 0x8);  // texture, mip map, complex
		for (int i = 0; i < 4; ++i)
			Put32(out, 0);
		Put32(out, dxgi);
		Put32(out, 3);  // a 2D texture
		Put32(out, 0);
		Put32(out, 1);
		Put32(out, 0);
		out.insert(out.end(), data.begin(), data.end());
		return out;
	}

	const Ba2Entry* Ba2Index::Find(std::string_view a_path) const
	{
		const auto hash = HashPath(Ba2NormalisedPath(a_path));
		const auto it = std::lower_bound(entries_.begin(), entries_.end(), hash, [](const Ba2Entry& e, std::uint64_t h) { return e.pathHash < h; });
		return it != entries_.end() && it->pathHash == hash ? &*it : nullptr;
	}

	bool Ba2Index::Contains(std::string_view a_path) const
	{
		return Find(a_path) != nullptr;
	}

	int Ba2Winner(const std::vector<Ba2Index>& a_archives, std::string_view a_path)
	{
		for (int i = static_cast<int>(a_archives.size()) - 1; i >= 0; --i)
			if (a_archives[static_cast<std::size_t>(i)].Contains(a_path))
				return i;
		return -1;
	}

	bool Ba2BelongsToPlugin(std::string_view a_archive, std::string_view a_plugin)
	{
		const auto lower = [](std::string_view a_text) {
			std::string s(a_text);
			for (auto& c : s)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		};
		const auto stem = [&](std::string_view a_name) {
			std::string s = lower(a_name);
			const auto  dot = s.find_last_of('.');
			if (dot != std::string::npos)
				s.resize(dot);
			return s;
		};
		const std::string archive = lower(a_archive);
		if (archive.size() < 4 || archive.compare(archive.size() - 4, 4, ".ba2") != 0)
			return false;
		const std::string a = stem(a_archive), p = stem(a_plugin);
		if (p.empty())
			return false;
		return a == p || a.rfind(p + " - ", 0) == 0;
	}
}
