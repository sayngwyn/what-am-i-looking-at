// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// The text the on-screen card shows, as pure functions of the Report: no ImGui, no game types, so the offline tests cover them.
#pragma once

#include "PickData.h"

#include <string>
#include <string_view>

namespace ti
{
	/** Lower case, backslashes, without a leading slash, "data\" or "textures\": two spellings of one texture compare equal. */
	std::string TextureKey(std::string_view a_path);

	/** How a game-data path reads on the card: lower case, backslashes, no leading slash or "data\", and a ".dds" gets its
	 *  "textures\" folder ("data/Textures/Water/X.dds" -> "textures\water\x.dds"). Other files ("materials\x.bgsm") keep their folder. */
	std::string DisplayPath(std::string_view a_path);

	/** "2048²" for a square, "4096×2048" otherwise (UTF-8). */
	std::string DimensionsText(int a_width, int a_height);

	/** "2048² BC7 · 11 mips"; "no GPU info" when nothing was read. A cube map says so. */
	std::string SizeChip(const GpuInfo& a_gpu);

	/** Where a file came from, short: "loose", "Mod name" (a loose file the OS placed in a mod folder), "Skyrim - Textures0.bsa", "not found". Empty when not looked up. */
	std::string OriginLabel(const FileOrigin& a_origin);

	/** "2 of 5" for the step control (a_index is zero-based); "2 of 5 \xC2\xB7 behind" for a hit past the first one along the ray. */
	std::string StepLabel(int a_index, int a_count);

	/** "Part 2 of 3 · exact", "Part 2 of 3 · by bounds" (a_index is zero-based). */
	std::string PartLabel(int a_index, int a_count, bool a_exact);

	/**
	 * The one console line printed while the card is on: "[What Am I Looking At] #3 "Bannered Mare" - part 2 of 5 - full report in last-pick.json".
	 * a_file is the JSON file's name; empty leaves that clause out.
	 */
	std::string ConsoleBrief(const PickInfo& a_info, std::string_view a_file);

	/** The part's name, or "Part N" when the model left it blank. A name made of form ids ("(00000D6C)[0]/ (00000D64) [40%]") is replaced by what the part's diffuse texture says ("Hands (skin)"). */
	std::string PartName(const ShapeInfo& a_shape);

	/** The upper-case eyebrow over the name: "STATIC · STAT", "TERRAIN · LAND", "WATER · WATR", "SKY · WTHR". */
	std::string Eyebrow(const PickInfo& a_info);

	/** "Skyrim.esm" or "Skyrim.esm · last changed by Unofficial Skyrim Special Edition Patch.esp". Empty when nothing is known. */
	std::string PluginLine(const FormRecord& a_record);

	/** Splits "textures\landscape\trees\x.dds" into the folder ("textures\landscape\trees") and the file ("x.dds"). */
	void SplitPath(std::string_view a_path, std::string& a_folder, std::string& a_file);

	/** "Riverwood04 [5, -12] · Tamriel · exterior" or "WhiterunBanneredMare · interior". */
	std::string CellLine(const Identity& a_identity);

	/** "21256, -47478, -56". */
	std::string PositionText(Vec3 a_position);

	/** "98%" for a layer's opacity, "100%" for the base layer. */
	std::string OpacityText(const TerrainLayer& a_layer);

	/** A colour string "#RRGGBB" parsed to 0xRRGGBB; false when it is not one. */
	bool ParseHexColor(std::string_view a_text, unsigned& a_rgb);

	/**
	 * The mouse rule while the card is up: a press that lands on the card goes to the card only (and so does its release, even when the
	 * pointer has moved off), a press elsewhere goes to the game. a_pressedOnCard is the caller's memory for one button.
	 * Returns true when the event must be swallowed (kept from the game).
	 */
	bool SwallowMouseButton(bool a_cardInteractive, bool a_overCard, bool a_isDown, bool& a_pressedOnCard);

	/** The wheel goes to the card only while the pointer is over it. */
	bool SwallowMouseWheel(bool a_cardInteractive, bool a_overCard);
}
