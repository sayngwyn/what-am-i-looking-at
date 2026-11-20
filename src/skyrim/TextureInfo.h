// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// Game-side readers for one texture: what the GPU holds, and where the file came from. Kept apart from the formatting.
#pragma once

#include "Report.h"

namespace RE
{
	class NiSourceTexture;
}

namespace ti
{
	/** Reads the D3D11 resource behind a bound NiSourceTexture. Never throws; a bad pointer gives present = false with a note. */
	GpuInfo ReadGpuInfo(RE::NiSourceTexture* a_texture);

	/**
	 * Where does the file at a_dataPath (Data-relative, e.g. "textures\\plants\\x.dds") come from? A loose file under Data wins over
	 * every archive (as in the game); otherwise the last loaded archive that holds it. Under MO2 the OS path of a loose file or an
	 * archive names the mod folder. Results are cached. Never throws, never fails a pick: unknown parts stay empty.
	 */
	FileOrigin ResolveOrigin(std::string_view a_dataPath);

	/** The first non-empty value of editor ID the engine or po3 Tweaks gives, empty when none. a_source is set to "engine" or "po3 Tweaks". */
	std::string EditorIdOf(std::uint32_t a_formId, const char* a_engineValue, std::string& a_source);
}
