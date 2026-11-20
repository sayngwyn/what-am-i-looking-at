// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// Offline tests for the ini string reader (blanks and quotes trimmed, [General] only, missing file or key = empty).
#include "IniRead.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace ti;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                      \
	do {                                                                 \
		++g_checks;                                                      \
		if (!(cond)) {                                                   \
			++g_failures;                                                \
			std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);  \
		}                                                                \
	} while (0)

/** Writes a small ini next to the test exe (absolute path: GetPrivateProfile* needs one) and returns its path. */
static std::wstring WriteIni(const char* a_name, const std::string& a_body)
{
	const auto path = std::filesystem::absolute(a_name);
	std::ofstream(path, std::ios::binary) << a_body;
	return path.wstring();
}

int main()
{
	const auto empty = WriteIni("ConfigTests_empty.ini", "[General]\nOutputFolder=\n");
	CHECK(ReadIniString(empty, L"OutputFolder").empty());

	const auto none = WriteIni("ConfigTests_none.ini", "[General]\nEnabled=1\n");
	CHECK(ReadIniString(none, L"OutputFolder").empty());
	CHECK(ReadIniString(L"C:\\this\\file\\does\\not\\exist.ini", L"OutputFolder").empty());

	const auto plain = WriteIni("ConfigTests_plain.ini", "[General]\nOutputFolder=D:\\Out Folder\\picks\n");
	CHECK(ReadIniString(plain, L"OutputFolder") == L"D:\\Out Folder\\picks");

	const auto quoted = WriteIni("ConfigTests_quoted.ini", "[General]\nOutputFolder=  \"D:\\Quoted\\picks\"  \n");
	CHECK(ReadIniString(quoted, L"OutputFolder") == L"D:\\Quoted\\picks");

	const auto otherSection = WriteIni("ConfigTests_other.ini", "[Other]\nOutputFolder=D:\\Nope\n");
	CHECK(ReadIniString(otherSection, L"OutputFolder").empty());

	for (const char* n : { "empty", "none", "plain", "quoted", "other" })
		std::filesystem::remove(std::filesystem::absolute(std::string("ConfigTests_") + n + ".ini"));

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
