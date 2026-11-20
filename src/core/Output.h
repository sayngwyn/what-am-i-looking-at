// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// Printers for a PickInfo. They only read it; gathering lives in Inspector.cpp.
#pragma once

#include "PickData.h"

#include <filesystem>

namespace ti
{
	/** Lines for the in-game console, readable at a glance. At most a_maxListedParts "other part" lines. */
	std::vector<std::string> FormatConsole(const PickInfo& a_info, unsigned a_maxListedParts, bool a_showRayLine = false);

	/** The whole pick as JSON (every part, the ray, transforms and bounds), pretty-printed UTF-8. */
	std::string ToJson(const PickInfo& a_info);

	/** Writes a_content to a_path through a temp file so a reader never sees half a file. Returns an error text, empty on success. */
	std::string WriteFileAtomic(const std::filesystem::path& a_path, const std::string& a_content);

	std::string JsonEscape(std::string_view a_text);
}
