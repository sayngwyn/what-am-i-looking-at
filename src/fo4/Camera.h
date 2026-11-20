// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// Fallout 4's camera: the world ray through the crosshair or the cursor, checked against the engine's own projection.
#pragma once

#include "PickData.h"

namespace ti
{
	struct RayResult
	{
		bool                     ok = false;
		Ray                      ray;
		CursorInfo               cursor;
		std::string              check;  ///< "verified", "verified (y flipped)", "unverified: ..."
		std::vector<std::string> notes;
	};

	/** World ray through the mouse pointer (console open: the pointer is the menu cursor). */
	RayResult CursorRay();
	/** World ray through the middle of the screen (the crosshair). */
	RayResult CentreRay();

	// CursorFraction and WorldToScreen (used by the card overlay) are declared in core/OverlayPlatform.h; CursorFraction lives in
	// OverlayHooks.cpp (the pointer in the game window) and WorldToScreen in Camera.cpp.
}
