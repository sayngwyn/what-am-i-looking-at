// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// The on-screen card: one Dear ImGui window that shows a Report in the house look. It only reads the PickInfo it is given and
// draws; the overlay owns the ImGui context, the fonts and the input.
#pragma once

#include "PickData.h"
#include "TextureHold.h"

#include <functional>
#include <string>

struct ImFont;

namespace ti::card
{
	struct Fonts
	{
		ImFont* display = nullptr;  ///< SayngwynSerif-Regular: the big name only
		ImFont* text = nullptr;     ///< SayngwynSerifText-Regular
		ImFont* bold = nullptr;     ///< SayngwynSerifText-Bold
	};

	/** Where the user put the card, in screen pixels. A height of 0 means "as tall as the content needs". */
	struct Placement
	{
		bool  hasPos = false;
		float x = 0, y = 0;  ///< the card's top-left
		bool  hasSize = false;
		float w = 0, h = 0;
	};

	struct Params
	{
		const PickInfo*     info = nullptr;
		const hold::Held*   held = nullptr;
		Fonts               fonts;
		float               scale = 1.0f;  ///< screen height / 1080 times the ini's CardScale
		float               displayW = 1920.0f;
		float               displayH = 1080.0f;
		std::string         corner = "top-right";  ///< where the card sits until the user moves it
		Placement           placement;             ///< what the user set (remembered between sessions)
		int                 uiPercent = 100;       ///< the user's size multiplier, shown in the header control
		bool                newCard = false;  ///< a different pick than last frame: scroll back to the top
		std::function<void(int)> step;     ///< called with -1 / +1 when the step control is clicked
	};

	struct Result
	{
		float x = 0, y = 0, w = 0, h = 0;  ///< the card's rectangle in screen pixels
		bool  hovered = false;
		bool  dragMoved = false;    ///< the header is being dragged: (nextX, nextY) is where the card goes
		float nextX = 0, nextY = 0;
		bool  resizeW = false;      ///< the user is resizing the width (an edge or corner is held); x, y, w, h are the new rectangle
		bool  resizeH = false;
		int   scaleSteps = 0;       ///< -1 / +1 when the smaller / larger text button was clicked
		bool  headerHot = false;    ///< the pointer is on the header (drag handle) or a drag is running
	};

	/** Draws the card (call between ImGui::NewFrame and ImGui::Render). */
	Result Draw(const Params& a_params);
}
