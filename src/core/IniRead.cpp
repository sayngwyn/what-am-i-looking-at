// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
#include "IniRead.h"

#ifndef NOMINMAX
#	define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <iterator>

namespace ti
{
	std::wstring ReadIniString(const std::wstring& a_ini, const wchar_t* a_key)
	{
		wchar_t buffer[1024]{};
		GetPrivateProfileStringW(L"General", a_key, L"", buffer, static_cast<DWORD>(std::size(buffer)), a_ini.c_str());
		std::wstring s = buffer;
		const auto   first = s.find_first_not_of(L" \t\"");
		const auto   last = s.find_last_not_of(L" \t\"");
		return first == std::wstring::npos ? std::wstring{} : s.substr(first, last - first + 1);
	}
}
