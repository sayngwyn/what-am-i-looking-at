// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// Fallout 4's game-side readers for one texture or file: what the GPU holds, and where the file came from (loose, or which BA2).
#pragma once

#include "Report.h"

#include <string_view>

namespace RE
{
	class NiTexture;
}

namespace ti
{
	/** Reads the D3D11 resource behind a bound NiTexture. Never throws; a bad pointer gives present = false with a note. */
	GpuInfo ReadGpuInfo(RE::NiTexture* a_texture);

	/**
	 * Where does the file at a_dataPath (Data-relative, e.g. "textures\\x_d.dds" or "Materials\\x.bgsm") come from? A loose file
	 * under Data wins over every archive (as in the game); otherwise the last loaded BA2 that holds it. The BA2s are the ones of the
	 * loaded plugins ("<plugin>.ba2", "<plugin> - <anything>.ba2") in load order; archives the game is told to load some other way
	 * (the ini's archive lists) are not read. Under MO2 the OS path of a loose file or an archive names the mod folder. Results are
	 * cached. Never throws, never fails a pick: unknown parts stay empty.
	 */
	FileOrigin ResolveOrigin(std::string_view a_dataPath);
	// starts reading the BA2 directories on a background thread (call once the plugins are loaded)
	void PreloadArchives();

	/**
	 * A small DDS copy of a texture held by the loaded BA2s (the winning archive; loose files are not looked at): the first mip level at
	 * most a_maxSide on its long side and the smaller ones. Empty with a_why when there is none; a_pending is set when the only reason
	 * is that the archive index is still being built (it never waits for it).
	 */
	std::vector<std::uint8_t> ReadArchiveTexture(std::string_view a_dataPath, int a_maxSide, std::string* a_why, bool* a_pending);
}
