// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// Fills the Report (identity, facts, NPC section, warnings) from the game. The formatting lives in Output.cpp and only reads
// what is filled in here; the rules behind the warnings live in Diagnostics.cpp.
#pragma once

#include "PickData.h"

namespace RE
{
	class NiAVObject;
	class TESObjectREFR;
	class TESForm;
	class TESObjectCELL;
}

namespace ti
{
	/**
	 * Fills a_info.identity, facts, npc and diagnostics for a_ref (a_root is its loaded 3D, null when it has none), and the
	 * file origin of the clicked part's textures. Never throws: a part that cannot be read stays empty and a line goes to the log.
	 */
	void FillReport(PickInfo& a_info, RE::TESObjectREFR* a_ref, RE::NiAVObject* a_root);

	/** The record facts of a form: id, type, editor ID, plugin that added it, plugin that last changed it, the whole chain. */
	FormRecord DescribeRecord(RE::TESForm* a_form);
	/** "Name (EDID, 0001234)". */
	std::string DescribeForm(RE::TESForm* a_form);
	/** "Name (EDID)" for a cell. */
	std::string CellDescription(RE::TESObjectCELL* a_cell);
}
