// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
#pragma once

#include <filesystem>
#include <string>

namespace ti
{
	struct Config
	{
		bool          enabled = true;
		bool          consoleClick = true;
		bool          printToConsole = true;  ///< [What Am I Looking At] lines in the in-game console
		bool          fullConsoleReport = false;  ///< with the card on, print the whole report too (otherwise one short line)
		bool          writeJson = true;
		unsigned      maxListedParts = 60;
		bool          showRayLine = true;  ///< print how the ray was built (useful until the picking is trusted)
		float         hotkeyMaxDistance = 6000.0f;  ///< game units
		bool          showCard = true;              ///< the on-screen card
		float         cardScale = 1.0f;             ///< first-run size multiplier (0.75 to 2); the in-game control remembers its own
		std::string   cardCorner = "top-right";     ///< top-right, top-left, bottom-left, bottom-right
		bool          showSampleCard = false;       ///< ShowSampleCard=1: a fixed, clearly marked sample report stays on screen (tests and screenshots; the Fallout 4 plugin reads it, Skyrim ignores it)
		std::filesystem::path outputFolder;         ///< resolved: never empty after Load()
	};

	/**
	 * Reads the plugin's ini (a_iniRelativePath, relative to the game folder, e.g. L"Data\\SKSE\\Plugins\\WhatAmILookingAt.ini"; a missing
	 * file or key keeps the defaults) and resolves the output folder: the ini's OutputFolder, else a_defaultOutputFolder.
	 */
	Config LoadConfig(const wchar_t* a_iniRelativePath, const std::filesystem::path& a_defaultOutputFolder);
}
