// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
#include "Config.h"
#include "CardState.h"
#include "IniRead.h"

#include <Windows.h>

#include <spdlog/spdlog.h>

#include <algorithm>

namespace ti
{
	namespace
	{
		std::filesystem::path IniFullPath(const wchar_t* a_relative)
		{
			// GetPrivateProfile* treats a bare relative path as a Windows-folder file: make it absolute from the game folder.
			wchar_t buffer[MAX_PATH]{};
			const auto n = GetFullPathNameW(a_relative, MAX_PATH, buffer, nullptr);
			return n ? std::filesystem::path(buffer) : std::filesystem::path(a_relative);
		}

		int ReadInt(const std::wstring& a_ini, const wchar_t* a_key, int a_default)
		{
			return static_cast<int>(GetPrivateProfileIntW(L"General", a_key, a_default, a_ini.c_str()));
		}
	}

	Config LoadConfig(const wchar_t* a_iniRelativePath, const std::filesystem::path& a_defaultOutputFolder)
	{
		Config      c;
		const auto  iniPath = IniFullPath(a_iniRelativePath);
		const auto  ini = iniPath.wstring();
		const bool  iniExists = std::filesystem::exists(iniPath);
		if (iniExists) {
			c.enabled = ReadInt(ini, L"Enabled", 1) != 0;
			c.consoleClick = ReadInt(ini, L"ConsoleClick", 1) != 0;
			// The card has no hotkeys any more (it is a console tool). Old ini lines for them are ignored; one info line says so.
			for (const wchar_t* key : { L"PickKey", L"HotkeyScanCode", L"ToggleCardKey", L"PrevHitKey", L"NextHitKey" }) {
				if (!ReadIniString(ini, key).empty()) {
					spdlog::info("ini: the old hotkey keys (PickKey, ToggleCardKey, PrevHitKey, NextHitKey) are no longer used and are ignored");
					break;
				}
			}
			c.printToConsole = ReadInt(ini, L"PrintToConsole", 1) != 0;
			c.fullConsoleReport = ReadInt(ini, L"FullConsoleReport", 0) != 0;
			c.writeJson = ReadInt(ini, L"WriteJson", 1) != 0;
			c.showRayLine = ReadInt(ini, L"ShowRayLine", 1) != 0;
			c.maxListedParts = static_cast<unsigned>(std::clamp(ReadInt(ini, L"MaxListedParts", 60), 0, 1000));
			c.hotkeyMaxDistance = static_cast<float>(std::clamp(ReadInt(ini, L"HotkeyMaxDistance", 6000), 100, 100000));
			c.showCard = ReadInt(ini, L"ShowCard", 1) != 0;
			c.showSampleCard = ReadInt(ini, L"ShowSampleCard", 0) != 0;
			if (const auto scale = ReadIniString(ini, L"CardScale"); !scale.empty()) {
				try {
					c.cardScale = ClampUiScale(std::stof(scale));
				} catch (...) {
				}
			}
			if (const auto corner = ReadIniString(ini, L"CardCorner"); !corner.empty()) {
				std::string text;
				for (const wchar_t ch : corner)
					text += static_cast<char>(ch < 128 ? std::tolower(static_cast<int>(ch)) : '?');
				if (text == "top-right" || text == "top-left" || text == "bottom-left" || text == "bottom-right")
					c.cardCorner = text;
			}
			if (const auto folder = ReadIniString(ini, L"OutputFolder"); !folder.empty())
				c.outputFolder = folder;
		}
		if (c.outputFolder.empty())
			c.outputFolder = a_defaultOutputFolder;
		return c;
	}
}
