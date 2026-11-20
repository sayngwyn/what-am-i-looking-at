// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// Plain-English warnings about one part's shader and textures. Pure logic: reads a ShapeInfo, no game types.
#pragma once

#include "PickData.h"

namespace ti
{
	/** Which game's rules apply: Skyrim's texture-set rules, or Fallout 4's (materials in .bgsm/.bgem files, no parallax flags, no TruePBR). */
	enum class DiagGame
	{
		kSkyrim,
		kFallout4
	};

	/** Every rule that fires for this part, in a fixed order. Each rule is named in Diagnostics.cpp. */
	std::vector<Diagnostic> Diagnose(const ShapeInfo& a_shape, DiagGame a_game = DiagGame::kSkyrim);

	/** True when a_path (any case, any slashes) ends with a_suffix such as "_p.dds". */
	bool PathEndsWith(std::string_view a_path, std::string_view a_suffix);
	/** True when a_path lies under textures\pbr\ or is named like a TruePBR texture. */
	bool LooksLikePbrPath(std::string_view a_path);
}
