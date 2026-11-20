// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// The Skyrim half of the texture hold: the call that notes an engine texture while a pick is being read. The card's half (what
// it keeps and how it gets a view) is core/TextureHold.h.
#pragma once

#include "TextureHold.h"

#include <string_view>

namespace RE
{
	class NiSourceTexture;
}

namespace ti::hold
{
	/** A texture the engine has bound, noted while a pick is being read (called from the code that fills a slot's GPU info). */
	void Gather(RE::NiSourceTexture* a_texture, std::string_view a_slotPath);
}
