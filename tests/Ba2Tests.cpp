// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// Offline tests for the BA2 reader (src/core/Ba2). Two parts: small archives built here in a temp folder (both kinds, so the test
// does not depend on any game files), and the real Fallout 4 archives of the shared test copies, read only. The real part is
// skipped with a notice when the shared copies are not on this machine: SKIPPED counts say so, a skip is never a pass.
//
// Usage: Ba2Tests [<Fallout 4 OG Data folder> [<Fallout 4 AE Data folder>]]; without arguments the folders come from the environment
// variables TI_FO4_OG_DATA and TI_FO4_AE_DATA, and without those the real-archive part is skipped.
#include "Ba2.h"
#include "Inflate.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace ti;
namespace fs = std::filesystem;

static int g_failures = 0;
static int g_checks = 0;
static int g_skipped = 0;

#define CHECK(cond)                                                     \
	do {                                                                \
		++g_checks;                                                     \
		if (!(cond)) {                                                  \
			++g_failures;                                               \
			std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
		}                                                               \
	} while (0)

// ---- a small archive builder ------------------------------------------------------------------------------------------

namespace
{
	template <class T>
	void Put(std::vector<unsigned char>& a_out, T a_value)
	{
		const auto* p = reinterpret_cast<const unsigned char*>(&a_value);
		a_out.insert(a_out.end(), p, p + sizeof(T));
	}

	void PutText(std::vector<unsigned char>& a_out, const char* a_text, std::size_t a_n)
	{
		for (std::size_t i = 0; i < a_n; ++i)
			a_out.push_back(i < std::strlen(a_text) ? static_cast<unsigned char>(a_text[i]) : 0);
	}

	struct TexSpec
	{
		const char*   path;
		std::uint16_t w, h;
		std::uint8_t  mips, dxgi;
		int           chunks;
		bool          cube;
	};

	void WriteFile(const fs::path& a_path, const std::vector<unsigned char>& a_bytes)
	{
		std::ofstream out(a_path, std::ios::binary);
		out.write(reinterpret_cast<const char*>(a_bytes.data()), static_cast<std::streamsize>(a_bytes.size()));
	}

	std::vector<unsigned char> NameTable(const std::vector<std::string>& a_names)
	{
		std::vector<unsigned char> t;
		for (const auto& n : a_names) {
			Put<std::uint16_t>(t, static_cast<std::uint16_t>(n.size()));
			t.insert(t.end(), n.begin(), n.end());
		}
		return t;
	}

	void BuildGeneral(const fs::path& a_path, std::uint32_t a_version, const std::vector<std::string>& a_names)
	{
		std::vector<unsigned char> b;
		Put<std::uint32_t>(b, 0x58445442);
		Put<std::uint32_t>(b, a_version);
		PutText(b, "GNRL", 4);
		Put<std::uint32_t>(b, static_cast<std::uint32_t>(a_names.size()));
		Put<std::uint64_t>(b, 24 + 36 * a_names.size());
		std::uint32_t i = 0;
		for (const auto& n : a_names) {
			Put<std::uint32_t>(b, 0x1000u + i);  // name hash (not read)
			PutText(b, "nif", 4);
			Put<std::uint32_t>(b, 0x2000u);  // dir hash
			Put<std::uint32_t>(b, 0);
			Put<std::uint64_t>(b, 4096u + i);  // data offset
			Put<std::uint32_t>(b, i % 2 ? 100u + i : 0u);  // packed size (0 = stored)
			Put<std::uint32_t>(b, 500u + i);  // unpacked size
			Put<std::uint32_t>(b, 0xBAADF00D);
			(void)n;
			++i;
		}
		const auto table = NameTable(a_names);
		b.insert(b.end(), table.begin(), table.end());
		WriteFile(a_path, b);
	}

	void BuildTextures(const fs::path& a_path, std::uint32_t a_version, const std::vector<TexSpec>& a_specs)
	{
		std::vector<unsigned char> records;
		std::vector<std::string>   names;
		for (const auto& s : a_specs) {
			names.emplace_back(s.path);
			Put<std::uint32_t>(records, 0x3000u);
			PutText(records, "dds", 4);
			Put<std::uint32_t>(records, 0x4000u);
			Put<std::uint8_t>(records, 0);
			Put<std::uint8_t>(records, static_cast<std::uint8_t>(s.chunks));
			Put<std::uint16_t>(records, 24);
			Put<std::uint16_t>(records, s.h);
			Put<std::uint16_t>(records, s.w);
			Put<std::uint8_t>(records, s.mips);
			Put<std::uint8_t>(records, s.dxgi);
			Put<std::uint8_t>(records, s.cube ? 1 : 0);
			Put<std::uint8_t>(records, 8);
			for (int c = 0; c < s.chunks; ++c) {
				Put<std::uint64_t>(records, 8192u * (c + 1));
				Put<std::uint32_t>(records, 1000u * (c + 1));   // packed
				Put<std::uint32_t>(records, 4000u * (c + 1));   // unpacked
				Put<std::uint16_t>(records, 0);
				Put<std::uint16_t>(records, 0);
				Put<std::uint32_t>(records, 0xBAADF00D);
			}
		}
		std::vector<unsigned char> b;
		Put<std::uint32_t>(b, 0x58445442);
		Put<std::uint32_t>(b, a_version);
		PutText(b, "DX10", 4);
		Put<std::uint32_t>(b, static_cast<std::uint32_t>(a_specs.size()));
		Put<std::uint64_t>(b, 24 + records.size());
		b.insert(b.end(), records.begin(), records.end());
		const auto table = NameTable(names);
		b.insert(b.end(), table.begin(), table.end());
		WriteFile(a_path, b);
	}
}

// ---- the zlib reader, against streams made with zlib itself (one of each block type) ------------------------------------

static const unsigned char kStored[] = { 0x78, 0x01, 0x01, 0x12, 0x00, 0xED, 0xFF, 0x68, 0x65, 0x6C, 0x6C, 0x6F, 0x20, 0x73, 0x74, 0x6F, 0x72, 0x65, 0x64, 0x20, 0x77, 0x6F, 0x72, 0x6C, 0x64, 0x42, 0xDA, 0x07, 0x0E };
static const unsigned char kFixed[] = { 0x78, 0x01, 0x4B, 0x4C, 0x4A, 0x4E, 0x84, 0x21, 0x85, 0x8C, 0xD4, 0x9C, 0x9C, 0x7C, 0x64, 0x12, 0x00, 0xAC, 0xFF, 0x0B, 0x35 };
static const unsigned char kDynamic[] = { 0x78, 0xDA, 0x8D, 0x56, 0xED, 0x6E, 0x83, 0x30, 0x0C, 0x7C, 0x15, 0x5E, 0x0D, 0x05, 0x2A, 0x55, 0x63, 0x65, 0xA2, 0xA0, 0xED, 0xF1, 0x37, 0x25, 0x84, 0xDC, 0x87, 0xD3, 0xEE, 0x4F, 0x28, 0x89, 0x63, 0xCE, 0xF6, 0x9D, 0xDD, 0xB4, 0xAC, 0xC7, 0x34, 0x4C, 0xF7, 0xDB, 0xED, 0x78, 0xCE, 0xC3, 0xB6, 0xA6, 0x8F, 0x61, 0x9F, 0x7F, 0xF6, 0x63, 0x9B, 0x87, 0xC7, 0xBA, 0x7D, 0x8E, 0x4B, 0x7D, 0xA4, 0x6C, 0x58, 0xCF, 0x96, 0xF1, 0x31, 0x3D, 0xD3, 0xF8, 0x35, 0xAB, 0x75, 0xF6, 0x90, 0x97, 0x73, 0xA3, 0x59, 0xA2, 0x85, 0xDC, 0x72, 0x77, 0x64, 0xE5, 0xC7, 0x15, 0xF0, 0xF7, 0xB8, 0xCF, 0x5B, 0x31, 0xAE, 0x5B, 0xA7, 0xCB, 0x72, 0x22, 0x9B, 0xCD, 0x51, 0x09, 0x87, 0x43, 0xF4, 0xCF, 0x4D, 0xF7, 0x6D, 0x2F, 0xDE, 0xD3, 0x99, 0xA7, 0xBF, 0xF7, 0xBC, 0x94, 0xF7, 0xF2, 0x11, 0xB4, 0x2F, 0x9F, 0xB3, 0xA0, 0x2B, 0x9A, 0xEB, 0x66, 0xFE, 0x59, 0x76, 0x19, 0x04, 0xC5, 0x92, 0xA8, 0x3A, 0x0D, 0x8D, 0xA4, 0xAF, 0x98, 0xE1, 0xDA, 0x80, 0xB2, 0x77, 0xC0, 0x21, 0x41, 0xC3, 0x09, 0x24, 0x95, 0xAB, 0x4E, 0xF8, 0x29, 0xB1, 0xF9, 0xC4, 0xF3, 0xC7, 0x45, 0x68, 0xFB, 0x8D, 0x25, 0x08, 0xC5, 0xE3, 0xE4, 0xFB, 0xB0, 0x05, 0xE0, 0xF2, 0x4F, 0x2F, 0x81, 0xF9, 0x54, 0x0C, 0x11, 0xA9, 0x72, 0xD2, 0x90, 0x59, 0x65, 0x55, 0xD6, 0xC1, 0xD7, 0x39, 0x1B, 0xE4, 0xA9, 0x05, 0x69, 0xA2, 0xF0, 0x4A, 0x1A, 0x63, 0x84, 0x84, 0x12, 0x16, 0xD7, 0x45, 0x4A, 0xF9, 0x1F, 0x5B, 0xA9, 0x05, 0x47, 0x6D, 0x4C, 0x62, 0x12, 0x35, 0x72, 0xB5, 0x85, 0xA8, 0x1C, 0x02, 0x00, 0x82, 0x69, 0xBA, 0x54, 0x96, 0x7A, 0x4E, 0xCE, 0x11, 0x22, 0xEB, 0x44, 0xDD, 0x44, 0xB5, 0x96, 0xB8, 0xA3, 0x03, 0x60, 0x79, 0x05, 0xD0, 0x15, 0x89, 0x23, 0xBF, 0xEE, 0x52, 0x87, 0x79, 0x57, 0xDE, 0x3E, 0x74, 0x52, 0x16, 0xB8, 0x0F, 0xBA, 0x46, 0x87, 0x32, 0xC0, 0x56, 0xEE, 0x31, 0x95, 0x83, 0x1D, 0xA9, 0x38, 0xA9, 0x41, 0x9E, 0xF4, 0xA2, 0x6D, 0x8A, 0xEF, 0x09, 0xF3, 0x08, 0xD6, 0x2B, 0x39, 0x6A, 0x86, 0xE3, 0x8C, 0x07, 0x13, 0xA0, 0x3A, 0x80, 0x92, 0xD0, 0x79, 0xA8, 0xD6, 0x7E, 0xB4, 0xFA, 0xF4, 0xDE, 0xE9, 0x4A, 0x71, 0x66, 0x6B, 0x1C, 0x96, 0x15, 0x28, 0x33, 0xEE, 0x29, 0x1F, 0x48, 0x46, 0xC4, 0x1C, 0x1F, 0x46, 0x4A, 0x02, 0x2B, 0x7F, 0x87, 0xA3, 0xF1, 0xFC, 0x0E, 0x25, 0xCE, 0xA5, 0xE7, 0x7E, 0x2D, 0xF8, 0xEC, 0xEB, 0xDE, 0x93, 0x09, 0x6D, 0xBF, 0xC7, 0x07, 0x02, 0x63, 0xBD, 0xEA, 0x10, 0xA4, 0x7F, 0x15, 0xC8, 0x1B, 0x56, 0x91, 0x61, 0xA5, 0x96, 0x13, 0xCF, 0x82, 0xD7, 0x23, 0x2A, 0x90, 0x09, 0xF9, 0x0C, 0x27, 0x4B, 0x68, 0x41, 0x9B, 0x3D, 0xBC, 0x52, 0x22, 0x88, 0x1C, 0xF2, 0xC8, 0x88, 0xBB, 0x03, 0x88, 0x03, 0x54, 0xA0, 0x4A, 0x8B, 0x58, 0xAC, 0xC1, 0x44, 0xE5, 0x42, 0xB1, 0x73, 0x7D, 0x76, 0x9B, 0x24, 0x4A, 0xED, 0xE2, 0xD3, 0x2F, 0x13, 0xDB, 0x14, 0x55 };
// text length 2763
static const char kDynamicText[] = "cloud diffuse rock texture normal normal cloud texture landscape texture normal rock rock normal landscape normal rock texture normal landscape texture rock texture landscape texture diffuse water rock diffuse normal water diffuse normal landscape cloud normal normal texture landscape dirt rock cloud dirt dirt cloud water landscape diffuse landscape normal water dirt cloud dirt water normal normal rock diffuse cloud diffuse dirt rock texture normal cloud cloud cloud dirt dirt normal normal water dirt normal texture water dirt water rock cloud texture dirt cloud diffuse normal dirt texture landscape water diffuse landscape rock rock dirt normal diffuse dirt rock water diffuse rock water rock cloud rock landscape diffuse normal diffuse diffuse landscape landscape texture dirt diffuse water water texture diffuse rock cloud cloud diffuse texture dirt rock rock rock rock normal dirt rock texture landscape normal landscape dirt diffuse normal cloud texture normal texture diffuse normal cloud texture normal landscape rock diffuse water cloud cloud dirt normal normal dirt dirt dirt dirt water normal diffuse normal cloud water dirt diffuse texture landscape cloud diffuse texture water normal water cloud diffuse cloud landscape cloud landscape landscape landscape rock landscape landscape dirt cloud texture texture water dirt water landscape cloud dirt cloud cloud normal landscape normal landscape dirt landscape cloud landscape dirt texture dirt cloud normal normal rock landscape dirt diffuse rock cloud normal rock dirt rock normal diffuse diffuse diffuse texture diffuse dirt diffuse dirt cloud diffuse diffuse texture texture normal diffuse rock landscape landscape texture water landscape water landscape cloud water rock diffuse texture cloud dirt rock diffuse diffuse texture dirt diffuse texture diffuse diffuse diffuse dirt normal texture cloud dirt normal texture landscape landscape water texture normal dirt texture normal dirt cloud landscape water dirt dirt landscape water landscape dirt diffuse rock normal rock dirt cloud normal landscape rock normal landscape water normal diffuse cloud diffuse water diffuse dirt landscape normal rock dirt diffuse landscape diffuse rock rock cloud rock landscape cloud cloud normal cloud texture cloud dirt dirt texture rock cloud water normal normal landscape normal normal water water texture diffuse water diffuse rock water rock diffuse dirt cloud normal water texture diffuse rock normal water texture normal water normal landscape normal water normal dirt texture cloud rock water diffuse texture landscape normal diffuse water texture diffuse landscape water water landscape water dirt diffuse water cloud texture water texture texture texture landscape dirt landscape dirt normal rock dirt";

static void TestInflate()
{
	std::vector<std::uint8_t> out;
	const std::string stored = "hello stored world";
	CHECK(ZlibInflate(kStored, sizeof(kStored), stored.size(), out) && std::string(out.begin(), out.end()) == stored);
	const std::string fixed = "abcabcabcabc hello hello hello";
	CHECK(ZlibInflate(kFixed, sizeof(kFixed), fixed.size(), out) && std::string(out.begin(), out.end()) == fixed);
	const std::string dynamic = kDynamicText;
	CHECK(ZlibInflate(kDynamic, sizeof(kDynamic), dynamic.size(), out) && std::string(out.begin(), out.end()) == dynamic);
	// the wrong expected size, a cut stream and a damaged header all fail
	CHECK(!ZlibInflate(kDynamic, sizeof(kDynamic), dynamic.size() + 1, out));
	CHECK(!ZlibInflate(kDynamic, sizeof(kDynamic), dynamic.size() - 1, out));
	CHECK(!ZlibInflate(kDynamic, 60, dynamic.size(), out));
	unsigned char bad[sizeof(kDynamic)];
	std::memcpy(bad, kDynamic, sizeof(bad));
	bad[0] = 0x79;
	CHECK(!ZlibInflate(bad, sizeof(bad), dynamic.size(), out));
	CHECK(!ZlibInflate(nullptr, 0, 0, out));
}

static void TestNormalisation()
{
	CHECK(Ba2NormalisedPath("Meshes/Foo\\Bar.NIF") == "meshes\\foo\\bar.nif");
	CHECK(Ba2NormalisedPath("\\\\Meshes\\A.nif") == "meshes\\a.nif");
	CHECK(Ba2NormalisedPath("Data\\Textures\\A_d.dds") == "textures\\a_d.dds");
	CHECK(Ba2NormalisedPath("data/Textures/A_d.dds") == "textures\\a_d.dds");
	CHECK(Ba2NormalisedPath("") == "");
	CHECK(Ba2NormalisedPath("Dataset\\x.nif") == "dataset\\x.nif");  // only the folder "data" is stripped
	// Bethesda's build path, as vanilla meshes and materials carry it, reads as the Data-relative path
	CHECK(Ba2NormalisedPath("C:\\projects\\Fallout4\\Build\\PC\\Data\\materials\\Architecture\\Buildings\\Foo.BGSM") == "materials\\architecture\\buildings\\foo.bgsm");
	CHECK(Ba2NormalisedPath("c:/projects/Fallout4/Build/PC/Data/Materials/x.bgsm") == "materials\\x.bgsm");
	CHECK(Ba2NormalisedPath("D:\\build\\textures\\a_d.dds") == "textures\\a_d.dds");  // no Data folder: cut at the first game folder
	CHECK(Ba2NormalisedPath("materials\\data\\x.bgsm") == "materials\\data\\x.bgsm");  // a relative path is left alone
}

static void TestOwnership()
{
	CHECK(Ba2BelongsToPlugin("Fallout4 - Textures1.ba2", "Fallout4.esm"));
	CHECK(Ba2BelongsToPlugin("fallout4 - materials.BA2", "Fallout4.esm"));
	CHECK(Ba2BelongsToPlugin("DLCRobot - Main.ba2", "DLCRobot.esm"));
	CHECK(Ba2BelongsToPlugin("MyMod.ba2", "MyMod.esp"));
	CHECK(Ba2BelongsToPlugin("MyMod - Main.ba2", "mymod.esl"));
	CHECK(!Ba2BelongsToPlugin("MyModExtra.ba2", "MyMod.esp"));  // the stem must end or be followed by " - "
	CHECK(!Ba2BelongsToPlugin("MyMod - Main.bsa", "MyMod.esp"));  // not a BA2
	CHECK(!Ba2BelongsToPlugin("Fallout4 - Main.ba2", ""));
}

static void TestSynthetic(const fs::path& a_dir)
{
	const fs::path general = a_dir / "synthetic-general.ba2";
	BuildGeneral(general, 1, { "Meshes\\Crates\\Crate01.nif", "Materials\\Crates\\Crate01.BGSM", "meshes\\crates\\crate02.nif" });
	auto g = Ba2Index::Open(general, true);
	CHECK(g.has_value());
	if (g) {
		CHECK(g->ArchiveKind() == Ba2Index::Kind::kGeneral);
		CHECK(g->Version() == 1);
		CHECK(g->FileCount() == 3);
		CHECK(g->Names().size() == 3);
		CHECK(g->Names()[1] == "materials\\crates\\crate01.bgsm");  // archive order, normalised
		CHECK(g->Contains("meshes/crates/CRATE01.NIF"));
		CHECK(g->Contains("Data\\Materials\\Crates\\Crate01.bgsm"));
		CHECK(!g->Contains("meshes\\crates\\crate03.nif"));
		CHECK(!g->Contains(""));
		const auto* e = g->Find("materials\\crates\\crate01.bgsm");
		CHECK(e != nullptr);
		if (e) {
			CHECK(e->unpackedSize == 501);
			CHECK(e->packedSize == 101);
			CHECK(e->chunks == 1);
		}
		const auto* stored = g->Find("meshes\\crates\\crate01.nif");
		CHECK(stored != nullptr && stored->packedSize == 0 && stored->unpackedSize == 500);
	}
	// version 7 and 8 share the layout
	const fs::path general8 = a_dir / "synthetic-general8.ba2";
	BuildGeneral(general8, 8, { "meshes\\a.nif" });
	auto g8 = Ba2Index::Open(general8);
	CHECK(g8.has_value() && g8->Version() == 8 && g8->Contains("Meshes\\A.nif"));
	CHECK(g8 && g8->Names().empty());  // names are not kept unless asked

	const fs::path textures = a_dir / "synthetic-textures.ba2";
	BuildTextures(textures, 1,
		{ { "Textures\\Crates\\Crate01_d.dds", 1024, 512, 11, 71, 2, false },
			{ "Textures\\Crates\\Crate01_n.dds", 1024, 512, 11, 83, 3, false },
			{ "Textures\\Sky\\Cube.dds", 256, 256, 9, 98, 1, true } });
	auto t = Ba2Index::Open(textures);
	CHECK(t.has_value());
	if (t) {
		CHECK(t->ArchiveKind() == Ba2Index::Kind::kTexture);
		CHECK(t->FileCount() == 3);
		const auto* d = t->Find("textures\\crates\\crate01_d.dds");
		CHECK(d != nullptr);
		if (d) {
			CHECK(d->width == 1024 && d->height == 512 && d->mips == 11 && d->dxgi == 71 && d->chunks == 2);
			CHECK(d->unpackedSize == 4000 + 8000);
			CHECK(d->packedSize == 1000 + 2000);
			CHECK(!d->cubemap);
		}
		const auto* n = t->Find("Textures/Crates/Crate01_N.DDS");
		CHECK(n != nullptr && n->dxgi == 83 && n->chunks == 3 && n->unpackedSize == 4000 + 8000 + 12000);
		const auto* c = t->Find("textures\\sky\\cube.dds");
		CHECK(c != nullptr && c->cubemap && c->dxgi == 98);
		CHECK(t->Find("textures\\nothing.dds") == nullptr);
	}

	// the winner is the last archive that holds the path
	std::vector<Ba2Index> list;
	if (g)
		list.push_back(*g);
	const fs::path second = a_dir / "synthetic-second.ba2";
	BuildGeneral(second, 1, { "meshes\\crates\\crate01.nif", "meshes\\only-second.nif" });
	if (auto s = Ba2Index::Open(second))
		list.push_back(std::move(*s));
	CHECK(list.size() == 2);
	CHECK(Ba2Winner(list, "meshes\\crates\\crate01.nif") == 1);
	CHECK(Ba2Winner(list, "meshes\\crates\\crate02.nif") == 0);
	CHECK(Ba2Winner(list, "meshes\\only-second.nif") == 1);
	CHECK(Ba2Winner(list, "meshes\\nowhere.nif") == -1);
	CHECK(Ba2Winner({}, "meshes\\nowhere.nif") == -1);

	// things that are not readable archives
	CHECK(!Ba2Index::Open(a_dir / "does-not-exist.ba2").has_value());
	const fs::path junk = a_dir / "junk.ba2";
	WriteFile(junk, std::vector<unsigned char>(100, 0x41));
	CHECK(!Ba2Index::Open(junk).has_value());
	// a truncated archive (the name table is cut off) is refused, not read past its end
	{
		std::ifstream              in(general, std::ios::binary);
		std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		bytes.resize(bytes.size() - 10);
		const fs::path cut = a_dir / "cut.ba2";
		WriteFile(cut, bytes);
		CHECK(!Ba2Index::Open(cut).has_value());
	}
	// a Starfield-style version is refused
	{
		std::ifstream              in(general, std::ios::binary);
		std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		bytes[4] = 2;
		const fs::path v2 = a_dir / "v2.ba2";
		WriteFile(v2, bytes);
		CHECK(!Ba2Index::Open(v2).has_value());
	}
}

// ---- the real archives ------------------------------------------------------------------------------------------------

static void TestReal(const fs::path& a_og, const fs::path& a_ae)
{
	std::error_code ec;
	if (!fs::is_directory(a_og, ec)) {
		std::printf("SKIPPED: the Fallout 4 OG folder %s is not here; the real-archive checks did not run\n", a_og.string().c_str());
		++g_skipped;
		return;
	}
	// OG 1.10.163: version 1 archives
	auto materials = Ba2Index::Open(a_og / "Fallout4 - Materials.ba2", true);
	CHECK(materials.has_value());
	if (materials) {
		CHECK(materials->ArchiveKind() == Ba2Index::Kind::kGeneral);
		CHECK(materials->Version() == 1);
		CHECK(materials->FileCount() == 6899);
		CHECK(materials->Names().size() == 6899);
		CHECK(materials->Names().front() == "materials\\barnacle.bgsm");
		CHECK(materials->Contains("Materials\\SetDressing\\Crates\\WoodenCrate01Dest.BGSM"));
		CHECK(materials->Contains("materials/setdressing/crates/woodencrate01dest.bgsm"));
		CHECK(materials->Contains("Data\\Materials\\Default.bgsm"));
		CHECK(!materials->Contains("Materials\\Nothing\\Here.bgsm"));
		// every listed name is findable, with a plausible size
		std::size_t missing = 0, zero = 0;
		for (const auto& n : materials->Names()) {
			const auto* e = materials->Find(n);
			if (!e)
				++missing;
			else if (e->unpackedSize == 0)
				++zero;
		}
		CHECK(missing == 0);
		CHECK(zero == 0);
		const auto* first = materials->Find("materials\\barnacle.bgsm");
		CHECK(first != nullptr && first->unpackedSize == 341 && first->packedSize == 122);
	}
	auto meshes = Ba2Index::Open(a_og / "Fallout4 - Meshes.ba2");
	CHECK(meshes.has_value());
	if (meshes) {
		CHECK(meshes->ArchiveKind() == Ba2Index::Kind::kGeneral);
		CHECK(meshes->FileCount() > 10000);
		CHECK(meshes->Contains("Meshes\\SetDressing\\ConcMuseum\\WoodCrate03.nif"));
		CHECK(!meshes->Contains("Materials\\SetDressing\\Crates\\WoodenCrate01Dest.BGSM"));
	}
	auto tex9 = Ba2Index::Open(a_og / "Fallout4 - Textures9.ba2", true);
	CHECK(tex9.has_value());
	if (tex9) {
		CHECK(tex9->ArchiveKind() == Ba2Index::Kind::kTexture);
		CHECK(tex9->FileCount() == 3530);
		CHECK(tex9->Names().front() == "textures\\terrain\\sanctuaryhillsworld\\sanctuaryhillsworld.4.16.24.dds");
		const auto* e = tex9->Find("Textures\\Terrain\\SanctuaryHillsWorld\\SanctuaryHillsWorld.4.16.24.DDS");
		CHECK(e != nullptr);
		if (e) {
			CHECK(e->width == 512 && e->height == 512 && e->mips == 10 && e->dxgi == 77 && e->chunks == 2 && !e->cubemap);
			CHECK(e->unpackedSize > 0);
		}
		std::size_t missing = 0, bad = 0;
		for (const auto& n : tex9->Names()) {
			const auto* t = tex9->Find(n);
			if (!t)
				++missing;
			else if (t->width == 0 || t->height == 0 || t->mips == 0 || t->dxgi == 0 || t->chunks == 0)
				++bad;
		}
		CHECK(missing == 0);
		CHECK(bad == 0);
	}
	// a small copy of a texture: header, size and that every sampled texture of the archive unpacks
	if (tex9) {
		std::string why;
		const auto* e = tex9->Find("Textures\\Terrain\\SanctuaryHillsWorld\\SanctuaryHillsWorld.4.16.24.dds");
		if (e) {
			const auto dds = tex9->ReadSmallDds(*e, 128, &why);
			CHECK(!dds.empty());
			if (dds.size() > 148) {
				std::uint32_t magic = 0, h = 0, w = 0, mips = 0, fmt = 0;
				std::memcpy(&magic, dds.data(), 4);
				std::memcpy(&h, dds.data() + 12, 4);
				std::memcpy(&w, dds.data() + 16, 4);
				std::memcpy(&mips, dds.data() + 28, 4);
				std::memcpy(&fmt, dds.data() + 128, 4);
				CHECK(magic == 0x20534444 && w == 128 && h == 128 && mips == 8 && fmt == 77);
				// BC3 is 16 bytes per 4x4 block: 128 x 128 down to 1 x 1
				CHECK(dds.size() == 148 + (32 * 32 + 16 * 16 + 8 * 8 + 4 * 4 + 2 * 2 + 1 + 1 + 1) * 16);
			}
		}
		std::size_t tried = 0, ok = 0, refusedWhy = 0;
		for (std::size_t i = 0; i < tex9->Names().size(); i += 7) {
			const auto* t = tex9->Find(tex9->Names()[i]);
			if (!t)
				continue;
			++tried;
			why.clear();
			const auto dds = tex9->ReadSmallDds(*t, 128, &why);
			if (!dds.empty())
				++ok;
			else if (!why.empty() && (why == "a cube map" || why == "a pixel format this does not size"))
				++refusedWhy;  // a refusal the reader gives on purpose
			else
				std::printf("small DDS of %s failed: %s\n", tex9->Names()[i].c_str(), why.c_str());
		}
		CHECK(tried > 400);
		CHECK(ok + refusedWhy == tried);
	}
	// which archive holds a path: the first that does, in a list of the OG archives
	{
		std::vector<Ba2Index> list;
		std::vector<std::string> labels;
		for (const char* name : { "Fallout4 - Materials.ba2", "Fallout4 - Meshes.ba2", "Fallout4 - Textures9.ba2" }) {
			if (auto a = Ba2Index::Open(a_og / name)) {
				list.push_back(std::move(*a));
				labels.emplace_back(name);
			}
		}
		CHECK(list.size() == 3);
		if (list.size() == 3) {
			const int m = Ba2Winner(list, "Materials\\SetDressing\\Crates\\WoodenCrate01Dest.BGSM");
			CHECK(m == 0);
			const int n = Ba2Winner(list, "Meshes\\SetDressing\\ConcMuseum\\WoodCrate03.nif");
			CHECK(n == 1);
			const int t = Ba2Winner(list, "Textures\\Terrain\\SanctuaryHillsWorld\\SanctuaryHillsWorld.4.16.24.dds");
			CHECK(t == 2);
			CHECK(Ba2Winner(list, "Textures\\Not\\There.dds") == -1);
		}
	}

	// AE 1.11.240: version 8 archives
	if (!fs::is_directory(a_ae, ec)) {
		std::printf("SKIPPED: the Fallout 4 AE folder %s is not here; the version 8 checks did not run\n", a_ae.string().c_str());
		++g_skipped;
		return;
	}
	auto aeMaterials = Ba2Index::Open(a_ae / "Fallout4 - Materials.ba2");
	CHECK(aeMaterials.has_value());
	if (aeMaterials) {
		CHECK(aeMaterials->Version() == 8);
		CHECK(aeMaterials->FileCount() == 6899);
		CHECK(aeMaterials->Contains("Materials\\Default.bgsm"));
	}
	auto patch = Ba2Index::Open(a_ae / "Fallout4 - TexturesPatch.ba2", true);
	CHECK(patch.has_value());
	if (patch) {
		CHECK(patch->Version() == 8);
		CHECK(patch->ArchiveKind() == Ba2Index::Kind::kTexture);
		CHECK(patch->FileCount() == 7);
		const auto* e = patch->Find("Textures\\Actors\\Character\\FaceCustomization\\Fallout4.esm\\0002A825_d.DDS");
		CHECK(e != nullptr);
		if (e)
			CHECK(e->width == 1024 && e->height == 1024 && e->mips == 11 && e->dxgi == 71 && e->chunks == 3);
	}
}

int main(int argc, char** argv)
{
	const auto envPath = [](const char* name) {
		char*  value = nullptr;
		size_t size = 0;
		fs::path result;
		if (_dupenv_s(&value, &size, name) == 0 && value)
			result = value;
		std::free(value);
		return result;
	};
	const fs::path og = argc > 1 ? fs::path(argv[1]) : envPath("TI_FO4_OG_DATA");
	const fs::path ae = argc > 2 ? fs::path(argv[2]) : envPath("TI_FO4_AE_DATA");

	std::error_code ec;
	const fs::path  dir = fs::absolute(fs::path(argv[0]), ec).parent_path() / "ti-ba2-tests";  // beside the test program (the build cache), not on C:
	fs::remove_all(dir, ec);
	fs::create_directories(dir, ec);

	TestInflate();
	TestNormalisation();
	TestOwnership();
	TestSynthetic(dir);
	TestReal(og, ae);

	fs::remove_all(dir, ec);
	std::printf("%d check(s), %d failure(s), %d part(s) skipped\n", g_checks, g_failures, g_skipped);
	return g_failures == 0 ? 0 : 1;
}
