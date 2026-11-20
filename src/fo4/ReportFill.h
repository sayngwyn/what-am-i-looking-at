// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// Fallout 4: fills the Report (identity, facts, NPC, warnings, file origins) from the game. The formatting lives in core/Output.cpp.
#pragma once

#include "PickData.h"

namespace RE
{
	class NiAVObject;
	class TESForm;
	class TESObjectCELL;
	class TESObjectREFR;
}

namespace ti
{
	/**
	 * Fills a_info.identity, facts, the NPC section (actors) and diagnostics for a_ref (a_root is its loaded 3D, null when it has none),
	 * and the file origin of every part's textures and material file. Never throws: a part that cannot be read stays empty and a line
	 * goes to the log.
	 */
	void FillReport(PickInfo& a_info, RE::TESObjectREFR* a_ref, RE::NiAVObject* a_root);

	/** A form as the card names it: plugins, type, editor id (empty at runtime in Fallout 4 for most forms). */
	FormRecord DescribeRecord(RE::TESForm* a_form);

	/** "Name (EDID, 0001234)" for a form named in a fact. */
	std::string DescribeForm(RE::TESForm* a_form);

	/** "Name (EDID)" or the name alone; "(unnamed cell)" for a cell with no name. */
	std::string CellDescription(RE::TESObjectCELL* a_cell);
}
