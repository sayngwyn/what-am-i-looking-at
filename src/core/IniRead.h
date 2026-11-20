// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
#pragma once

#include <filesystem>
#include <string>

namespace ti
{
	/** One string of [General] in an ini file, with surrounding blanks and quotes trimmed. Empty when absent. `a_ini` must be an absolute path. */
	std::wstring ReadIniString(const std::wstring& a_ini, const wchar_t* a_key);
}
