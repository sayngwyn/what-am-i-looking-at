// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// What the user's hands change about the card and what is remembered between sessions: where it sits, how big it is, its size
// multiplier. Pure functions (clamping, the small JSON file), no game or ImGui types, so the offline tests cover them.
#pragma once

#include <string>
#include <string_view>

namespace ti
{
	constexpr float kMinUiScale = 0.75f;
	constexpr float kMaxUiScale = 2.0f;
	constexpr float kUiScaleStep = 0.05f;

	/** The remembered layout. Position is the card's top-left as a fraction of the screen, so it survives a resolution change; the size is in
	 *  design pixels (what it measures at 1080p and a size multiplier of 1), so it follows the size multiplier. */
	struct CardState
	{
		bool  hasPos = false;
		float x = 0.0f, y = 0.0f;
		bool  hasSize = false;
		float w = 0.0f, h = 0.0f;  ///< h = 0: the height fits the content
		bool  hasScale = false;
		float scale = 1.0f;  ///< the user's size multiplier, kMinUiScale..kMaxUiScale

		bool operator==(const CardState&) const = default;
	};

	struct PxRect
	{
		float x = 0, y = 0, w = 0, h = 0;
	};

	/** a_scale limited to kMinUiScale..kMaxUiScale and snapped to kUiScaleStep. A value that is not a number gives 1. */
	float ClampUiScale(float a_scale);

	/** a_scale moved by a_steps of a_stepSize (e.g. -2, 0.05) and clamped. */
	float StepUiScale(float a_scale, int a_steps, float a_stepSize);

	/**
	 * Keeps a rectangle on the screen: the width and height are limited to the screen less a_margin each side (and to at least
	 * a_minW x a_minH, unless the screen is smaller), then the position is moved until the whole rectangle is inside.
	 * A height of 0 (fit the content) stays 0.
	 */
	PxRect ClampToScreen(PxRect a_rect, float a_displayW, float a_displayH, float a_minW, float a_minH, float a_margin);

	/** The state as the file holds it (small, hand-readable JSON). */
	std::string StateToJson(const CardState& a_state);

	/** Reads that JSON. False when nothing usable is in it (a_state untouched); numbers that are not finite or out of range are ignored. */
	bool StateFromJson(std::string_view a_json, CardState& a_state);
}
