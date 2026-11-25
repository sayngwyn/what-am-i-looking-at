// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// Reads the string table out of the header of a Fallout 4 mesh (NIF 20.2.0.7, Bethesda stream version 130 and up): every node, part,
// extra-data and material name the file uses is listed there once. It answers one question: does this mesh file hold a part
// of this name? Format: the public nif.xml description (header string, version, endian, user version, block count, the Bethesda
// stream header of three or four short strings, the block type list, block type indices, block sizes, then the string table).
// No game types, no engine calls: unit-tested offline.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ti
{
	/**
	 * The names in the header's string table, in file order. False when the bytes are not a Fallout 4 NIF this can read (another
	 * game's version, a damaged or cut-off header); a_out is then empty.
	 */
	bool NifHeaderStrings(const std::uint8_t* a_bytes, std::size_t a_size, std::vector<std::string>& a_out);

	/** Case-insensitive: is a_name one of a_strings? */
	bool NifHasString(const std::vector<std::string>& a_strings, std::string_view a_name);
}
