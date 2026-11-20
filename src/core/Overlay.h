// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// The overlay: draws the card on top of the game's frame, just before the swap chain presents it, and keeps clicks on the card
// away from the game. It owns its own Dear ImGui context and touches nothing until a card is to be shown.
#pragma once

#include "Config.h"
#include "PickData.h"
#include "TextureHold.h"

#include <functional>

struct ImDrawList;
struct ImDrawCmd;
struct ID3D11Device;
struct IDXGISwapChain;

namespace ti::overlay
{
	/** a_delta is -1 / +1: step to the previous / next hit along the ray of the last pick (run later, on the game thread). */
	using StepHandler = std::function<void(int a_delta)>;

	/** Loads the remembered layout and asks the game layer to hook its frame end (OverlayPlatform.h). Call once the renderer exists (after the data is loaded). False when it could not. */
	bool Install(const Config& a_config, StepHandler a_step);

	/** Called by the game layer's frame hook, once per frame, with the swap chain the frame is about to present (or has just presented). */
	void DrawFrame(IDXGISwapChain* a_swap);

	/** What the game layer's input hook asks of the card for a mouse event. */
	enum class MouseKind
	{
		kButton,
		kWheelUp,
		kWheelDown,
	};

	/** True when a card is up or a press that began on it is still held: only then does the input hook need to look at events. */
	bool InputFilterWanted();

	/**
	 * Feeds one mouse button (a_button 0..2) or wheel event to the card and says whether the game must not see it. a_active: the
	 * button is pressed, or the wheel moved (the event's value is not 0). a_button is ignored for the wheel.
	 */
	bool FilterMouse(MouseKind a_kind, unsigned a_button, bool a_active);

	/** The card to show. It is drawn only while the console is open; closing the console hides it and reopening shows the last pick again. */
	void SetCard(const PickInfo& a_info, hold::HeldPtr a_held);

	/** True while a card is on screen. */
	bool CardShowing();

	/** True while the card feature is on: installed, ShowCard=1 and not put away with `lookingat card_hide`. */
	bool CardActive();
	void SetCardHidden(bool a_hidden);

	/** The user's size multiplier (0.75 to 2). SetUiScale clamps and returns what was set. */
	float UiScale();
	float SetUiScale(float a_scale);
	/** Forgets the remembered position, size and size multiplier (the ini's CardCorner and CardScale apply again) and shows the card. */
	void ResetLayout();
	/** a_x, a_y: the card's top-left as a fraction of the screen; a_w, a_h: its size in design pixels (a_w 0 leaves the size alone, a_h 0 fits the content). */
	void PlaceCard(float a_x, float a_y, float a_w, float a_h);

	/** Saves the next presented frame, card included, as card-<n>.png in the output folder. False when the overlay is not installed. */
	bool RequestShot();

	/** The game's D3D11 device once the overlay runs (the card makes thumbnail views with it); null before. */
	ID3D11Device* Device();

	/** Draw callbacks the card uses around a thumbnail so its alpha channel (normal maps carry other data there) cannot make it see-through. */
	void NoBlendBegin(const ImDrawList* a_list, const ImDrawCmd* a_cmd);
}
