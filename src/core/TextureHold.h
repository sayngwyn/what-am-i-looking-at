// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// Keeps the engine's texture objects alive while a card shows them, so a thumbnail's shader resource view never points at
// freed memory. The Report itself stays plain data: this is the side channel that goes with it.
#pragma once

#include "PickData.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ti::hold
{
	/** What a card keeps: the texture objects, by TextureKey of their name. Opaque here; the card asks Resolve for a view. */
	struct Held;
	using HeldPtr = std::shared_ptr<Held>;

	/** Takes (and forgets) everything gathered since the last call, keeping only the textures the report mentions. */
	HeldPtr TakeFor(const PickInfo& a_info);

	/** The shader resource view of the texture a slot shows, or null when it is not held or has none (cube maps and arrays are not returned). */
	void* ViewFor(const Held* a_held, std::string_view a_pathOrLoadedAs);
}
