// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// Offline tests for the card's text helpers (CardLayout.cpp). No framework: each CHECK prints on failure and the exit
// code is the failure count.
#include "CardLayout.h"
#include "PartSource.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace ti;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                     \
	do {                                                                \
		++g_checks;                                                     \
		if (!(cond)) {                                                  \
			++g_failures;                                               \
			std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
		}                                                               \
	} while (0)

#define CHECK_EQ(actual, expected)                                                                                   \
	do {                                                                                                             \
		++g_checks;                                                                                                  \
		const std::string a_ = (actual);                                                                             \
		const std::string e_ = (expected);                                                                           \
		if (a_ != e_) {                                                                                              \
			++g_failures;                                                                                            \
			std::printf("FAIL %s:%d  %s\n  got      \"%s\"\n  expected \"%s\"\n", __FILE__, __LINE__, #actual, a_.c_str(), e_.c_str()); \
		}                                                                                                            \
	} while (0)

static GpuInfo Gpu(int a_w, int a_h, int a_mips, const char* a_format)
{
	GpuInfo g;
	g.present = true;
	g.width = a_w;
	g.height = a_h;
	g.mips = a_mips;
	g.format = a_format;
	return g;
}

static void TestSizeChip()
{
	CHECK_EQ(SizeChip(Gpu(2048, 2048, 11, "BC7")), "2048\xC2\xB2 BC7 \xC2\xB7 11 mips");
	CHECK_EQ(SizeChip(Gpu(4096, 2048, 13, "BC3")), "4096\xC3\x97" "2048 BC3 \xC2\xB7 13 mips");
	CHECK_EQ(SizeChip(Gpu(64, 64, 1, "R8G8B8A8")), "64\xC2\xB2 R8G8B8A8 \xC2\xB7 1 mip");
	GpuInfo cube = Gpu(256, 256, 9, "BC1");
	cube.cube = true;
	CHECK_EQ(SizeChip(cube), "256\xC2\xB2 BC1 \xC2\xB7 9 mips \xC2\xB7 cube");
	CHECK_EQ(SizeChip(GpuInfo{}), "no GPU info");
	GpuInfo noFormat = Gpu(512, 512, 10, "");
	CHECK_EQ(SizeChip(noFormat), "512\xC2\xB2 ? \xC2\xB7 10 mips");
}

static void TestOrigin()
{
	FileOrigin o;
	CHECK_EQ(OriginLabel(o), "");  // not looked up
	o.looked = true;
	o.source = "loose";
	CHECK_EQ(OriginLabel(o), "loose");
	o.mod = "Skyrim 2020";
	CHECK_EQ(OriginLabel(o), "Skyrim 2020");
	FileOrigin a;
	a.looked = true;
	a.source = "archive";
	a.archive = "Skyrim - Textures0.bsa";
	CHECK_EQ(OriginLabel(a), "Skyrim - Textures0.bsa");
	a.mod = "Some Mod";
	CHECK_EQ(OriginLabel(a), "Skyrim - Textures0.bsa \xC2\xB7 Some Mod");
	FileOrigin n;
	n.looked = true;
	n.source = "not found";
	CHECK_EQ(OriginLabel(n), "not found");
}

static void TestSteps()
{
	CHECK_EQ(StepLabel(0, 5), "1 of 5");
	CHECK_EQ(StepLabel(0, 1), "1 of 1");
	CHECK_EQ(StepLabel(1, 5), "2 of 5 \xC2\xB7 behind");
	CHECK_EQ(PartLabel(1, 3, true), "Part 2 of 3 \xC2\xB7 exact");
	CHECK_EQ(PartLabel(0, 12, false), "Part 1 of 12 \xC2\xB7 by bounds");
	ShapeInfo s;
	s.index = 2;
	CHECK_EQ(PartName(s), "Part 3");
	s.name = "Farmhouse06:6";
	CHECK_EQ(PartName(s), "Farmhouse06:6");
	// a name built from form ids is never shown: the part is known by its diffuse picture
	s.name = "(00000D6C)[0]/ (00000D64) [40%]";
	CHECK_EQ(PartName(s), "Part 3");
	TextureSlot diffuse;
	diffuse.label = "diffuse";
	diffuse.path = "textures\\actors\\character\\female\\femalehands_1.dds";
	s.textures.push_back(diffuse);
	CHECK_EQ(PartName(s), "Hands (skin)");
	s.textures.front().path = "textures\\armor\\iron\\ironhelmet_d.dds";
	CHECK_EQ(PartName(s), "Ironhelmet");
}

static void TestEyebrow()
{
	PickInfo info;
	info.identity.kind = "object";
	info.object.baseType = "STAT";
	CHECK_EQ(Eyebrow(info), "STATIC");
	info.object.baseType = "NPC_";
	info.identity.kind = "npc";
	CHECK_EQ(Eyebrow(info), "CHARACTER");
	info.object.baseType = "ZZZZ";
	CHECK_EQ(Eyebrow(info), "CHARACTER");
	info.object.baseType.clear();
	info.identity.base.type = "DOOR";
	CHECK_EQ(Eyebrow(info), "DOOR");
	info.identity.kind = "terrain";
	CHECK_EQ(Eyebrow(info), "GROUND");
	info.identity.kind = "water";
	CHECK_EQ(Eyebrow(info), "WATER");
	info.identity.kind = "sky";
	CHECK_EQ(Eyebrow(info), "SKY");
}

static void TestPluginLine()
{
	FormRecord r;
	CHECK_EQ(PluginLine(r), "");
	r.addedBy = "Skyrim.esm";
	CHECK_EQ(PluginLine(r), "Skyrim.esm");
	r.lastChangedBy = "Skyrim.esm";
	CHECK_EQ(PluginLine(r), "Skyrim.esm");
	r.lastChangedBy = "USSEP.esp";
	CHECK_EQ(PluginLine(r), "Skyrim.esm \xC2\xB7 last changed by USSEP.esp");
	FormRecord c;
	c.chain = { "A.esm", "B.esp" };
	CHECK_EQ(PluginLine(c), "A.esm \xC2\xB7 last changed by B.esp");
}

static void TestPaths()
{
	std::string folder, file;
	SplitPath("textures\\landscape\\trees\\x.dds", folder, file);
	CHECK_EQ(folder, "textures\\landscape\\trees");
	CHECK_EQ(file, "x.dds");
	SplitPath("x.dds", folder, file);
	CHECK_EQ(folder, "");
	CHECK_EQ(file, "x.dds");
	SplitPath("a/b/c.dds", folder, file);
	CHECK_EQ(folder, "a/b");
	CHECK_EQ(file, "c.dds");
	CHECK_EQ(TextureKey("Data\\Textures\\Landscape\\Trees\\X.dds"), "landscape\\trees\\x.dds");
	CHECK_EQ(TextureKey("/textures/landscape/trees/x.dds"), "landscape\\trees\\x.dds");
	CHECK_EQ(TextureKey("landscape\\trees\\x.dds"), "landscape\\trees\\x.dds");
	CHECK_EQ(DisplayPath("data/Textures/Water/ChurningWaterTile.dds"), "textures\\water\\churningwatertile.dds");
	CHECK_EQ(DisplayPath("Data\\Textures\\Water\\X.dds"), "textures\\water\\x.dds");
	CHECK_EQ(DisplayPath("Water\\X.dds"), "textures\\water\\x.dds");
	CHECK_EQ(DisplayPath("textures\\terrain\\x_d.dds"), "textures\\terrain\\x_d.dds");
	CHECK_EQ(DisplayPath("Materials\\Landscape\\Dirt.BGSM"), "materials\\landscape\\dirt.bgsm");
	CHECK_EQ(DisplayPath("Data\\materials\\Architecture\\Buildings\\WoodTrimWhite01.BGSM"), "materials\\architecture\\buildings\\woodtrimwhite01.bgsm");
	// Bethesda's build path in front of the game folder is cut (seen on Sanctuary parts in game)
	CHECK_EQ(DisplayPath("C:\\projects\\Fallout4\\Build\\PC\\Data\\materials\\Architecture\\Buildings\\WoodTrimWhite01.BGSM"), "materials\\architecture\\buildings\\woodtrimwhite01.bgsm");
	CHECK_EQ(DisplayPath("c:/projects/fallout4/build/pc/data/Materials/Landscape/Dirt.bgsm"), "materials\\landscape\\dirt.bgsm");
	CHECK_EQ(DisplayPath("C:\\projects\\Fallout4\\Build\\PC\\Data\\Textures\\Architecture\\X_d.dds"), "textures\\architecture\\x_d.dds");
	CHECK_EQ(DisplayPath("materials\\a\\materials\\b.bgsm"), "materials\\a\\materials\\b.bgsm");
	CHECK_EQ(DisplayPath("Data\\Textures\\Water\\DefaultWater.dds"), "textures\\water\\defaultwater.dds");
	CHECK_EQ(DisplayPath("data\\Textures\\Water\\RiverFlow.dds"), "textures\\water\\riverflow.dds");
}

static void TestCellLine()
{
	Identity id;
	id.cellEditorId = "Riverwood04";
	id.hasCoords = true;
	id.cellX = 5;
	id.cellY = -12;
	id.worldspaceEditorId = "Tamriel";
	CHECK_EQ(CellLine(id), "Riverwood04 [5, -12] \xC2\xB7 Tamriel \xC2\xB7 exterior");
	id.persistent = true;
	CHECK_EQ(CellLine(id), "Riverwood04 [5, -12] \xC2\xB7 Tamriel \xC2\xB7 exterior");
	Identity in;
	in.cellName = "The Bannered Mare";
	in.interior = true;
	CHECK_EQ(CellLine(in), "The Bannered Mare \xC2\xB7 interior");
	Identity bare;
	CHECK_EQ(CellLine(bare), "");
	// a reference whose cell is not loaded: no name, no coordinates; the worldspace and the grid of its position stand in
	Identity unloaded;
	unloaded.worldspaceEditorId = "Tamriel";
	unloaded.position = { 19983.0f, -46408.0f, -148.0f };
	CHECK_EQ(CellLine(unloaded), "Tamriel [4, -12]");
	CHECK(CellLine(unloaded).find("Exterior cell") == std::string::npos);
	CHECK(CellLine(unloaded).find("exterior") == std::string::npos);
	unloaded.hasCoords = true;  // a cell with no name but known coordinates
	unloaded.cellX = 5;
	unloaded.cellY = -12;
	CHECK_EQ(CellLine(unloaded), "Tamriel [5, -12]");
	Identity noWorld;
	noWorld.position = { 8192.0f, 4096.0f, 0.0f };
	CHECK_EQ(CellLine(noWorld), "Cell [2, 1]");
	Identity unnamedInterior;
	unnamedInterior.interior = true;
	CHECK_EQ(CellLine(unnamedInterior), "Interior cell");
	CHECK_EQ(PositionText({ 21256.785f, -47478.395f, -56.39f }), "21257, -47478, -56");
}

static void TestMisc()
{
	TerrainLayer base;
	CHECK_EQ(OpacityText(base), "100%");
	TerrainLayer l;
	l.rawPercent = 12;
	l.percent = 48.4f;
	CHECK_EQ(OpacityText(l), "48%");
	unsigned rgb = 0;
	CHECK(ParseHexColor("#d36e6e", rgb) && rgb == 0xD36E6E);
	CHECK(ParseHexColor("#00FF7a", rgb) && rgb == 0x00FF7A);
	CHECK(!ParseHexColor("d36e6e", rgb));
	CHECK(!ParseHexColor("#d36e6", rgb));
	CHECK(!ParseHexColor("#d36e6g", rgb));
}

static void TestMouseGate()
{
	bool pressed = false;
	// a press on the card: swallowed, and so is its release even after the pointer left
	CHECK(SwallowMouseButton(true, true, true, pressed));
	CHECK(pressed);
	CHECK(SwallowMouseButton(true, false, false, pressed));
	CHECK(!pressed);
	// a press elsewhere: the game gets it, and the release over the card too (the game saw the press)
	CHECK(!SwallowMouseButton(true, false, true, pressed));
	CHECK(!SwallowMouseButton(true, true, false, pressed));
	// the card is not interactive (no cursor): nothing is swallowed
	CHECK(!SwallowMouseButton(false, true, true, pressed));
	CHECK(!SwallowMouseButton(false, true, false, pressed));
	// the wheel follows the pointer
	CHECK(SwallowMouseWheel(true, true));
	CHECK(!SwallowMouseWheel(true, false));
	CHECK(!SwallowMouseWheel(false, true));
}

static void TestConsoleBrief()
{
	PickInfo info;
	info.counter = 7;
	info.valid = true;
	info.identity.kind = "static";
	info.object.name = "Bannered Mare";
	info.shapes.resize(5);
	info.clicked = 1;
	CHECK_EQ(ConsoleBrief(info, "last-pick.json"), "[What Am I Looking At] #7 \"Bannered Mare\" - part 2 of 5 - full report in last-pick.json");
	CHECK_EQ(ConsoleBrief(info, ""), "[What Am I Looking At] #7 \"Bannered Mare\" - part 2 of 5");
	info.clicked = -1;
	CHECK_EQ(ConsoleBrief(info, "last-pick.json"), "[What Am I Looking At] #7 \"Bannered Mare\" - no single part found - full report in last-pick.json");
	info.object.name = "";
	info.object.baseEditorId = "WRBannerMare01";
	info.clicked = 0;
	CHECK_EQ(ConsoleBrief(info, "x.json"), "[What Am I Looking At] #7 \"WRBannerMare01\" - part 1 of 5 - full report in x.json");
	info.identity.kind = "terrain";
	CHECK_EQ(ConsoleBrief(info, "x.json"), "[What Am I Looking At] #7 land - full report in x.json");
	info.identity.kind = "water";
	CHECK_EQ(ConsoleBrief(info, "x.json"), "[What Am I Looking At] #7 water - full report in x.json");
	info.identity.kind = "sky";
	CHECK_EQ(ConsoleBrief(info, "x.json"), "[What Am I Looking At] #7 sky - full report in x.json");
	PickInfo none;
	none.counter = 8;
	none.message = "could not build the crosshair ray";
	CHECK_EQ(ConsoleBrief(none, "x.json"), "[What Am I Looking At] #8 could not build the crosshair ray - full report in x.json");
}

// ---- which mesh file a part comes from ------------------------------------------------------------------------------------

static MeshSource MakeSource(const char* a_path, const char* a_owner, bool a_isMod, std::vector<std::string> a_names, const char* a_originMod = "")
{
	MeshSource s;
	s.path = a_path;
	s.owner = a_owner;
	s.isMod = a_isMod;
	s.origin.looked = true;
	s.origin.source = a_originMod[0] ? "loose" : "archive";
	s.origin.archive = a_originMod[0] ? "" : "Fallout4 - Meshes.ba2";
	s.origin.mod = a_originMod;
	SetMeshNames(s, a_names);
	return s;
}

static ShapeInfo Shape(int a_index, const char* a_name)
{
	ShapeInfo s;
	s.index = a_index;
	s.name = a_name;
	return s;
}

static void TestPartSource()
{
	const std::vector<MeshSource> sources = {
		MakeSource("meshes\\weapons\\10mmpistol\\10mmpistol.nif", "10mm Pistol (Fallout4.esm)", false, { "WEAPON", "10mmPistol:0", "Glass1:0", "" }),
		MakeSource("meshes\\weapons\\10mmpistol\\10mmnightsights.nif", "Night Sights (Fallout4.esm)", true, { "NightSights001", "NightSights001:0", "Glass1:0" }),
		MakeSource("meshes\\weapons\\10mmpistol\\10mmreflexsight.nif", "Reflex Sight (MyMod.esp)", true, { "10mmReflex:0", "TritiumDot021:0" }, "My Sights Mod"),
	};
	std::vector<ShapeInfo> shapes = { Shape(0, "10mmpistol:0"), Shape(1, "nightsights001:0"), Shape(2, "TritiumDot021:0"), Shape(3, "Glass1:0"), Shape(4, "Unknown:0"), Shape(5, "") };
	AttributeParts(shapes, sources);

	// a part named in exactly one file: that file, its origin and what brings it
	CHECK_EQ(shapes[0].sourceMesh, "meshes\\weapons\\10mmpistol\\10mmpistol.nif");
	CHECK(!shapes[0].sourceIsMod);
	CHECK_EQ(shapes[0].sourceOwner, "10mm Pistol (Fallout4.esm)");
	CHECK_EQ(shapes[1].sourceMesh, "meshes\\weapons\\10mmpistol\\10mmnightsights.nif");
	CHECK(shapes[1].sourceIsMod);
	CHECK_EQ(OriginLabel(shapes[1].sourceOrigin), "Fallout4 - Meshes.ba2");
	CHECK_EQ(shapes[2].sourceMesh, "meshes\\weapons\\10mmpistol\\10mmreflexsight.nif");
	CHECK_EQ(OriginLabel(shapes[2].sourceOrigin), "My Sights Mod");
	// a name two files hold: no pick, both listed
	CHECK(shapes[3].sourceMesh.empty());
	CHECK(shapes[3].sourceChoices.size() == 2);
	// a name no file holds, and a part without a name: nothing
	CHECK(shapes[4].sourceMesh.empty() && shapes[4].sourceChoices.empty());
	CHECK(shapes[5].sourceMesh.empty() && shapes[5].sourceChoices.empty());
	// the same file twice (the base model and a mod that both point at it) is one file
	std::vector<MeshSource> twice = { sources[1], sources[1] };
	std::vector<ShapeInfo>  one = { Shape(0, "NightSights001:0") };
	AttributeParts(one, twice);
	CHECK_EQ(one[0].sourceMesh, "meshes\\weapons\\10mmpistol\\10mmnightsights.nif");
	// no sources at all: nothing, and an earlier answer is cleared
	AttributeParts(one, {});
	CHECK(one[0].sourceMesh.empty() && one[0].sourceOwner.empty());

	// the card rows
	auto rows = PartSourceFacts(shapes[1]);
	CHECK(rows.size() == 3);
	CHECK(rows.size() == 3 && rows[0].label == "Part file" && rows[0].value == "meshes\\weapons\\10mmpistol\\10mmnightsights.nif");
	CHECK(rows.size() == 3 && rows[1].label == "Part file from" && rows[1].value == "Fallout4 - Meshes.ba2");
	CHECK(rows.size() == 3 && rows[2].label == "Added by mod" && rows[2].value == "Night Sights (Fallout4.esm)");
	rows = PartSourceFacts(shapes[0]);
	CHECK(rows.size() == 3 && rows[2].label == "Part of");
	rows = PartSourceFacts(shapes[2]);
	CHECK(rows.size() == 3 && rows[1].value == "My Sights Mod");
	rows = PartSourceFacts(shapes[3]);
	CHECK(rows.size() == 1 && rows[0].label == "Part file");
	CHECK(rows.size() == 1 && rows[0].value == "one of meshes\\weapons\\10mmpistol\\10mmpistol.nif, meshes\\weapons\\10mmpistol\\10mmnightsights.nif");
	CHECK(PartSourceFacts(shapes[4]).empty());
}

int main()
{
	TestPartSource();
	TestConsoleBrief();
	TestSizeChip();
	TestOrigin();
	TestSteps();
	TestEyebrow();
	TestPluginLine();
	TestPaths();
	TestCellLine();
	TestMisc();
	TestMouseGate();
	std::printf("CardLayoutTests: %d checks, %d failures\n", g_checks, g_failures);
	return g_failures;
}
