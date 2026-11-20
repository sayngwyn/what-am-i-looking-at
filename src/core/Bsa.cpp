// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
#include "Bsa.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

namespace ti
{
	namespace
	{
		std::string Normalised(std::string_view a_text)
		{
			std::string s(a_text);
			for (auto& c : s)
				c = c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			// trim leading slashes
			while (!s.empty() && s.front() == '\\')
				s.erase(s.begin());
			return s;
		}

		std::uint64_t HashParts(const std::string& a_base, const std::string& a_ext)
		{
			const std::size_t len = a_base.size();
			std::uint32_t     low = 0;
			if (len > 0) {
				low = static_cast<std::uint32_t>(static_cast<unsigned char>(a_base[len - 1]));
				if (len > 2)
					low |= static_cast<std::uint32_t>(static_cast<unsigned char>(a_base[len - 2])) << 8;
				low |= static_cast<std::uint32_t>(len) << 16;
				low |= static_cast<std::uint32_t>(static_cast<unsigned char>(a_base[0])) << 24;
			}
			if (a_ext == ".kf")
				low += 0x80;
			else if (a_ext == ".nif")
				low += 0x8000;
			else if (a_ext == ".dds")
				low += 0x8080;
			else if (a_ext == ".wav")
				low += 0x80000000u;

			std::uint32_t h2 = 0;
			for (std::size_t i = 1; i + 2 < len; ++i)  // characters 1 .. len-3
				h2 = h2 * 0x1003fu + static_cast<unsigned char>(a_base[i]);
			std::uint32_t h3 = 0;
			for (const char c : a_ext)
				h3 = h3 * 0x1003fu + static_cast<unsigned char>(c);
			return (static_cast<std::uint64_t>(h2 + h3) << 32) | low;
		}

		template <class T>
		bool ReadAt(std::ifstream& a_in, std::uint64_t a_pos, T& a_out)
		{
			a_in.clear();
			a_in.seekg(static_cast<std::streamoff>(a_pos), std::ios::beg);
			a_in.read(reinterpret_cast<char*>(&a_out), sizeof(T));
			return static_cast<bool>(a_in);
		}
	}

	std::uint64_t BsaFolderHash(std::string_view a_folder)
	{
		return HashParts(Normalised(a_folder), "");
	}

	std::uint64_t BsaFileHash(std::string_view a_fileName)
	{
		const std::string n = Normalised(a_fileName);
		const auto        dot = n.find_last_of('.');
		if (dot == std::string::npos || dot == 0)
			return HashParts(n, "");
		return HashParts(n.substr(0, dot), n.substr(dot));
	}

	bool ArchiveBelongsToPlugin(std::string_view a_archive, std::string_view a_plugin)
	{
		const auto stem = [](std::string_view a_name) {
			std::string s(a_name);
			const auto  dot = s.find_last_of('.');
			if (dot != std::string::npos)
				s.resize(dot);
			for (auto& c : s)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		};
		std::string archive(a_archive);
		for (auto& c : archive)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		if (archive.size() < 4 || archive.compare(archive.size() - 4, 4, ".bsa") != 0)
			return false;
		const std::string a = stem(a_archive), p = stem(a_plugin);
		if (p.empty())
			return false;
		return a == p || a.rfind(p + " - ", 0) == 0;
	}

	std::optional<BsaIndex> BsaIndex::Open(const std::filesystem::path& a_path)
	{
		std::ifstream in(a_path, std::ios::binary);
		if (!in)
			return std::nullopt;
		struct Header
		{
			char          magic[4];
			std::uint32_t version;
			std::uint32_t folderRecordOffset;
			std::uint32_t archiveFlags;
			std::uint32_t folderCount;
			std::uint32_t fileCount;
			std::uint32_t totalFolderNameLength;
			std::uint32_t totalFileNameLength;
			std::uint16_t fileFlags;
			std::uint16_t pad;
		} h{};
		static_assert(sizeof(Header) == 36);
		if (!ReadAt(in, 0, h) || std::memcmp(h.magic, "BSA\0", 4) != 0 || (h.version != 104 && h.version != 105))
			return std::nullopt;
		if (h.folderCount > 200000)
			return std::nullopt;

		BsaIndex idx;
		idx.path_ = a_path;
		idx.version_ = h.version;
		idx.flags_ = h.archiveFlags;
		idx.fileCount_ = h.fileCount;
		idx.totalFileNameLength_ = h.totalFileNameLength;
		idx.folders_.reserve(h.folderCount);
		const std::size_t recordSize = h.version == 105 ? 24 : 16;
		std::vector<char> raw(recordSize * h.folderCount);
		in.clear();
		in.seekg(h.folderRecordOffset, std::ios::beg);
		in.read(raw.data(), static_cast<std::streamsize>(raw.size()));
		if (!in)
			return std::nullopt;
		for (std::uint32_t i = 0; i < h.folderCount; ++i) {
			const char* r = raw.data() + recordSize * i;
			Folder      f;
			std::memcpy(&f.hash, r, 8);
			std::memcpy(&f.count, r + 8, 4);
			if (h.version == 105)
				std::memcpy(&f.offset, r + 16, 4);
			else
				std::memcpy(&f.offset, r + 12, 4);
			idx.folders_.push_back(f);
		}
		return idx;
	}

	bool BsaIndex::Contains(std::string_view a_path) const
	{
		const std::string n = Normalised(a_path);
		const auto        slash = n.find_last_of('\\');
		if (slash == std::string::npos)
			return false;
		const std::uint64_t folderHash = BsaFolderHash(n.substr(0, slash));
		const std::uint64_t fileHash = BsaFileHash(n.substr(slash + 1));

		// The archive keeps the folder table sorted by hash, but do not rely on it: a linear scan of a few thousand rows is cheap.
		const Folder* folder = nullptr;
		for (const auto& f : folders_)
			if (f.hash == folderHash) {
				folder = &f;
				break;
			}
		if (!folder || folder->count == 0 || folder->offset < totalFileNameLength_)
			return false;

		std::ifstream in(path_, std::ios::binary);
		if (!in)
			return false;
		std::uint64_t pos = static_cast<std::uint64_t>(folder->offset) - totalFileNameLength_;
		if (flags_ & 0x1) {  // directory names included: a length-prefixed string comes first
			std::uint8_t nameLength = 0;
			if (!ReadAt(in, pos, nameLength))
				return false;
			pos += 1u + nameLength;
		}
		std::vector<char> raw(static_cast<std::size_t>(folder->count) * 16u);
		in.clear();
		in.seekg(static_cast<std::streamoff>(pos), std::ios::beg);
		in.read(raw.data(), static_cast<std::streamsize>(raw.size()));
		if (!in)
			return false;
		for (std::uint32_t i = 0; i < folder->count; ++i) {
			std::uint64_t hash = 0;
			std::memcpy(&hash, raw.data() + static_cast<std::size_t>(i) * 16u, 8);
			if (hash == fileHash)
				return true;
		}
		return false;
	}
}
