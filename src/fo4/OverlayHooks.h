// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// The Fallout 4 half of the overlay: the frame hook (core/OverlayPlatform.h asks for it), the game window's message hook for the
// mouse, and the few game questions the card asks. Written from commonlibf4's headers and what the running game shows; no other
// plugin's code.
#pragma once

#include <cstdint>

namespace ti::fo4
{
	/** Called on the render thread when the left mouse button is released with the console open and the card did not take the click. */
	using ClickHandler = void (*)();
	void SetClickHandler(ClickHandler a_handler);

	/** Called on the render thread every frame while the left button is up and no click is being decided: samples the console selection before a click. */
	void SetSampleHandler(ClickHandler a_handler);

	/**
	 * Subclasses the game window (found from the swap chain at the first drawn frame) so mouse messages reach the card first.
	 * The left button is also polled each frame while the game window is in front, because the game reads its mouse through DirectInput.
	 */
	bool InstallWindowHook();

	/** The game window's client size in pixels (false before the window is known). */
	bool ClientArea(float& a_width, float& a_height);
}
