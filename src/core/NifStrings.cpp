// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
#include "NifStrings.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace ti
{
	namespace
	{
		constexpr std::string_view kHeaderStart = "Gamebryo File Format, Version 20.2.0.7";
		constexpr std::uint32_t    kVersion = 0x14020007;
		constexpr std::uint32_t    kMaxBlocks = 1'000'000;
		constexpr std::uint32_t    kMaxStrings = 1'000'000;

		/** A cursor over the bytes that fails (and stays failed) instead of reading past the end. */
		struct Reader
		{
			const std::uint8_t* bytes;
			std::size_t         size;
			std::size_t         at = 0;
			bool                ok = true;

			bool Skip(std::size_t a_count)
			{
				if (!ok || a_count > size - at)
					return ok = false;
				at += a_count;
				return true;
			}

			template <class T>
			T Get()
			{
				T value{};
				if (!ok || sizeof(T) > size - at) {
					ok = false;
					return value;
				}
				std::memcpy(&value, bytes + at, sizeof(T));
				at += sizeof(T);
				return value;
			}

			/** A string with a one-byte length (the export strings of the Bethesda header). */
			void SkipShortString() { Skip(Get<std::uint8_t>()); }

			/** A string with a four-byte length. */
			std::string SizedString()
			{
				const auto length = Get<std::uint32_t>();
				if (!ok || length > size - at) {
					ok = false;
					return {};
				}
				std::string text(reinterpret_cast<const char*>(bytes + at), length);
				at += length;
				return text;
			}
		};

		std::string Lower(std::string_view a_text)
		{
			std::string s(a_text);
			for (auto& c : s)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		}
	}

	bool NifHeaderStrings(const std::uint8_t* a_bytes, std::size_t a_size, std::vector<std::string>& a_out)
	{
		a_out.clear();
		if (!a_bytes || a_size < kHeaderStart.size() + 1 || std::memcmp(a_bytes, kHeaderStart.data(), kHeaderStart.size()) != 0)
			return false;
		Reader r{ a_bytes, a_size };
		// the header line ends with a line feed
		while (r.at < r.size && a_bytes[r.at] != 0x0A)
			++r.at;
		if (r.at >= r.size)
			return false;
		++r.at;
		if (r.Get<std::uint32_t>() != kVersion)
			return false;
		r.Skip(1);  // endianness
		const auto userVersion = r.Get<std::uint32_t>();
		const auto blockCount = r.Get<std::uint32_t>();
		if (!r.ok || userVersion < 3 || blockCount > kMaxBlocks)
			return false;
		// the Bethesda stream header
		const auto bsVersion = r.Get<std::uint32_t>();
		if (!r.ok || bsVersion < 100)
			return false;
		r.SkipShortString();  // author
		if (bsVersion > 130)
			r.Skip(4);
		if (bsVersion < 131)
			r.SkipShortString();  // process script
		r.SkipShortString();      // export script
		if (bsVersion >= 103)
			r.SkipShortString();  // max file path
		// block types, each block's type index and size
		const auto typeCount = r.Get<std::uint16_t>();
		for (std::uint16_t i = 0; i < typeCount && r.ok; ++i)
			r.SizedString();
		r.Skip(static_cast<std::size_t>(blockCount) * 2);  // block type index
		r.Skip(static_cast<std::size_t>(blockCount) * 4);  // block size
		const auto stringCount = r.Get<std::uint32_t>();
		r.Skip(4);  // longest string
		if (!r.ok || stringCount > kMaxStrings)
			return false;
		std::vector<std::string> strings;
		strings.reserve(stringCount);
		for (std::uint32_t i = 0; i < stringCount && r.ok; ++i)
			strings.push_back(r.SizedString());
		if (!r.ok)
			return false;
		a_out = std::move(strings);
		return true;
	}

	bool NifHasString(const std::vector<std::string>& a_strings, std::string_view a_name)
	{
		const std::string wanted = Lower(a_name);
		return std::any_of(a_strings.begin(), a_strings.end(), [&](const std::string& s) { return s.size() == wanted.size() && Lower(s) == wanted; });
	}
}
