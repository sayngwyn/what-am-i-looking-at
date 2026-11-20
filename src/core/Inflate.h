// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// A small zlib decompressor (RFC 1950 wrapper around RFC 1951 deflate): the chunks of a Fallout 4 texture archive are stored this
// way. Written for reading a few thumbnails, not for speed. No game types.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ti
{
	/**
	 * Decompresses a zlib stream into exactly a_expected bytes. False when the stream is damaged, ends early, or does not produce
	 * a_expected bytes. The Adler-32 trailer is not checked (a short or damaged stream already fails).
	 */
	bool ZlibInflate(const std::uint8_t* a_in, std::size_t a_inSize, std::size_t a_expected, std::vector<std::uint8_t>& a_out);
}
