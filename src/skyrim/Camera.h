// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
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

	/** World ray through the menu cursor (console open). */
	RayResult CursorRay();
	/** World ray through the middle of the screen (the crosshair). */
	RayResult CentreRay();

	// CursorFraction and WorldToScreen (used by the card overlay) are declared in core/OverlayPlatform.h and defined in Camera.cpp.
}
