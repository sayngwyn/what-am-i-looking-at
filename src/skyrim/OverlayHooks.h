// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// The Skyrim half of the overlay: the two hooks into the game (frame end, input) and the game questions the card asks.
// The frame hook is installed by overlay::Install (through core/OverlayPlatform.h); the input hook is installed here, after it.
#pragma once

namespace ti::overlay
{
	/**
	 * Hooks the one call that hands the input events to the game's listeners, so mouse clicks and wheel turns over the card are
	 * kept from the game. Needs the SKSE trampoline allocated (SKSE::AllocTrampoline). False when the call site is not what was
	 * expected: nothing is hooked then and clicks pass through to the game.
	 */
	bool InstallInputFilter();
}
