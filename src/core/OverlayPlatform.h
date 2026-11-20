// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// What the overlay (Overlay.cpp) needs from the game, written once per game (skyrim/OverlayHooks.cpp and the camera there).
// No game types in here. The overlay calls these; the game layer calls overlay::DrawFrame, InputFilterWanted and FilterMouse back
// from its hooks (Overlay.h).
#pragma once

#include "Pick.h"

#include <filesystem>
#include <string>

struct IDXGISwapChain;

namespace ti::overlay::platform
{
	/**
	 * Hooks the game's end-of-frame call so overlay::DrawFrame runs once per frame with the swap chain. Logs why when it cannot.
	 * Called by overlay::Install after the remembered layout is loaded.
	 */
	bool InstallFrameHook();

	/** True while the in-game console is open (the card is then interactive and the menu cursor exists). */
	bool ConsoleOpen();

	/** One line into the in-game console. */
	void ConsolePrint(const std::string& a_line);

	/** The folder holding the card's fonts (absolute, from the game folder). */
	std::filesystem::path FontFolder();
}

namespace ti
{
	/** The menu cursor as a fraction of the window (0,0 = top-left); false when there is no cursor. Cheap: no camera, no ray. */
	bool CursorFraction(float& a_fractionX, float& a_fractionY);

	/** Where the engine's camera puts a world point on the screen, as a fraction of the window (0,0 = top-left). False when the point is behind the camera or there is no camera. */
	bool WorldToScreen(Vec3 a_point, float& a_fractionX, float& a_fractionY);
}
