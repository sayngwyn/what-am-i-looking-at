// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
#include "Inflate.h"

#include <array>
#include <cstring>

namespace ti
{
	namespace
	{
		struct Bits
		{
			const std::uint8_t* in;
			std::size_t         size;
			std::size_t         pos = 0;
			std::uint32_t       hold = 0;
			int                 count = 0;
			bool                bad = false;

			std::uint32_t Take(int a_n)
			{
				while (count < a_n) {
					if (pos >= size) {
						bad = true;
						return 0;
					}
					hold |= static_cast<std::uint32_t>(in[pos++]) << count;
					count += 8;
				}
				const std::uint32_t v = hold & ((1u << a_n) - 1u);
				hold >>= a_n;
				count -= a_n;
				return v;
			}
		};

		/** A canonical Huffman code: how many codes of each length, and the symbols in code order. */
		struct Huffman
		{
			std::array<std::uint16_t, 16>  counts{};
			std::array<std::uint16_t, 320> symbols{};

			bool Build(const std::uint8_t* a_lengths, int a_n)
			{
				counts.fill(0);
				for (int i = 0; i < a_n; ++i)
					++counts[a_lengths[i]];
				counts[0] = 0;
				int left = 1;
				for (int len = 1; len < 16; ++len) {
					left <<= 1;
					left -= counts[len];
					if (left < 0)
						return false;  // over-subscribed
				}
				std::array<std::uint16_t, 16> offsets{};
				for (int len = 1; len < 15; ++len)
					offsets[len + 1] = static_cast<std::uint16_t>(offsets[len] + counts[len]);
				for (int i = 0; i < a_n; ++i)
					if (a_lengths[i] != 0)
						symbols[offsets[a_lengths[i]]++] = static_cast<std::uint16_t>(i);
				return true;
			}

			int Decode(Bits& a_bits) const
			{
				int code = 0, first = 0, index = 0;
				for (int len = 1; len < 16; ++len) {
					code |= static_cast<int>(a_bits.Take(1));
					if (a_bits.bad)
						return -1;
					const int count = counts[len];
					if (code - count < first)
						return symbols[static_cast<std::size_t>(index + (code - first))];
					index += count;
					first += count;
					first <<= 1;
					code <<= 1;
				}
				return -1;
			}
		};

		constexpr std::uint16_t kLengthBase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
		constexpr std::uint16_t kLengthExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
		constexpr std::uint16_t kDistBase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
		constexpr std::uint16_t kDistExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

		bool Codes(Bits& a_bits, std::vector<std::uint8_t>& a_out, std::size_t a_limit, const Huffman& a_lit, const Huffman& a_dist)
		{
			for (;;) {
				const int sym = a_lit.Decode(a_bits);
				if (sym < 0)
					return false;
				if (sym < 256) {
					if (a_out.size() >= a_limit)
						return false;
					a_out.push_back(static_cast<std::uint8_t>(sym));
				} else if (sym == 256) {
					return true;
				} else {
					const int li = sym - 257;
					if (li >= 29)
						return false;
					const std::size_t length = kLengthBase[li] + a_bits.Take(kLengthExtra[li]);
					const int         ds = a_dist.Decode(a_bits);
					if (ds < 0 || ds >= 30)
						return false;
					const std::size_t distance = kDistBase[ds] + a_bits.Take(kDistExtra[ds]);
					if (a_bits.bad || distance > a_out.size() || a_out.size() + length > a_limit)
						return false;
					for (std::size_t i = 0; i < length; ++i)
						a_out.push_back(a_out[a_out.size() - distance]);
				}
			}
		}

		bool Stored(Bits& a_bits, std::vector<std::uint8_t>& a_out, std::size_t a_limit)
		{
			a_bits.hold = 0;  // drop the rest of the current byte
			a_bits.count = 0;
			if (a_bits.pos + 4 > a_bits.size)
				return false;
			const std::size_t len = a_bits.in[a_bits.pos] | (a_bits.in[a_bits.pos + 1] << 8);
			const std::size_t nlen = a_bits.in[a_bits.pos + 2] | (a_bits.in[a_bits.pos + 3] << 8);
			a_bits.pos += 4;
			if ((len ^ 0xFFFF) != nlen || a_bits.pos + len > a_bits.size || a_out.size() + len > a_limit)
				return false;
			a_out.insert(a_out.end(), a_bits.in + a_bits.pos, a_bits.in + a_bits.pos + len);
			a_bits.pos += len;
			return true;
		}

		bool Fixed(Bits& a_bits, std::vector<std::uint8_t>& a_out, std::size_t a_limit)
		{
			std::uint8_t lengths[320];
			int          i = 0;
			for (; i < 144; ++i)
				lengths[i] = 8;
			for (; i < 256; ++i)
				lengths[i] = 9;
			for (; i < 280; ++i)
				lengths[i] = 7;
			for (; i < 288; ++i)
				lengths[i] = 8;
			Huffman lit, dist;
			if (!lit.Build(lengths, 288))
				return false;
			for (i = 0; i < 30; ++i)
				lengths[i] = 5;
			if (!dist.Build(lengths, 30))
				return false;
			return Codes(a_bits, a_out, a_limit, lit, dist);
		}

		bool Dynamic(Bits& a_bits, std::vector<std::uint8_t>& a_out, std::size_t a_limit)
		{
			const int nlen = static_cast<int>(a_bits.Take(5)) + 257;
			const int ndist = static_cast<int>(a_bits.Take(5)) + 1;
			const int ncode = static_cast<int>(a_bits.Take(4)) + 4;
			if (a_bits.bad || nlen > 286 || ndist > 30)
				return false;
			static constexpr std::uint8_t order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
			std::uint8_t                  lengths[320]{};
			for (int i = 0; i < ncode; ++i)
				lengths[order[i]] = static_cast<std::uint8_t>(a_bits.Take(3));
			Huffman codeLengths;
			if (a_bits.bad || !codeLengths.Build(lengths, 19))
				return false;
			std::uint8_t all[320]{};
			int          index = 0;
			while (index < nlen + ndist) {
				const int sym = codeLengths.Decode(a_bits);
				if (sym < 0)
					return false;
				if (sym < 16) {
					all[index++] = static_cast<std::uint8_t>(sym);
				} else {
					int          repeat = 0;
					std::uint8_t value = 0;
					if (sym == 16) {
						if (index == 0)
							return false;
						value = all[index - 1];
						repeat = 3 + static_cast<int>(a_bits.Take(2));
					} else if (sym == 17) {
						repeat = 3 + static_cast<int>(a_bits.Take(3));
					} else {
						repeat = 11 + static_cast<int>(a_bits.Take(7));
					}
					if (a_bits.bad || index + repeat > nlen + ndist)
						return false;
					while (repeat--)
						all[index++] = value;
				}
			}
			if (all[256] == 0)
				return false;  // no end-of-block code
			Huffman lit, dist;
			if (!lit.Build(all, nlen) || !dist.Build(all + nlen, ndist))
				return false;
			return Codes(a_bits, a_out, a_limit, lit, dist);
		}
	}

	bool ZlibInflate(const std::uint8_t* a_in, std::size_t a_inSize, std::size_t a_expected, std::vector<std::uint8_t>& a_out)
	{
		a_out.clear();
		if (!a_in || a_inSize < 2)
			return false;
		// the two-byte zlib header: deflate, a window of at most 32 KiB, a valid check value, no preset dictionary
		if ((a_in[0] & 0x0F) != 8 || (a_in[0] >> 4) > 7 || ((a_in[0] << 8) | a_in[1]) % 31 != 0 || (a_in[1] & 0x20))
			return false;
		a_out.reserve(a_expected);
		Bits bits{ a_in, a_inSize };
		bits.pos = 2;
		bool last = false;
		while (!last) {
			last = bits.Take(1) != 0;
			const std::uint32_t type = bits.Take(2);
			if (bits.bad)
				break;
			bool ok = false;
			if (type == 0)
				ok = Stored(bits, a_out, a_expected);
			else if (type == 1)
				ok = Fixed(bits, a_out, a_expected);
			else if (type == 2)
				ok = Dynamic(bits, a_out, a_expected);
			if (!ok || bits.bad) {
				a_out.clear();
				return false;
			}
		}
		if (bits.bad || a_out.size() != a_expected) {
			a_out.clear();
			return false;
		}
		return true;
	}
}
