// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// Offline tests for the Report's pure parts: the diagnostics rules, the texture-format helpers, the archive reader and
// the small text helpers. No framework: each CHECK prints on failure and the exit code is the failure count.
//
// Optional: ReportTests <archive.bsa> <data-relative path> [<path that is not inside>] checks the archive reader against
// a real archive (for the first path it expects "found", for the third "not found"). Not part of the normal run.
#include "Bsa.h"
#include "Diagnostics.h"
#include "PickData.h"
#include "Report.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

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

// ---- helpers to build a part by hand -----------------------------------------------------------------------------------

static GpuInfo Gpu(int a_w, int a_h, int a_mips, int a_dxgi)
{
	GpuInfo g;
	g.present = true;
	g.width = a_w;
	g.height = a_h;
	g.mips = a_mips;
	g.dxgi = a_dxgi;
	g.format = DxgiFormatName(a_dxgi);
	g.bytes = EstimateTextureBytes(a_w, a_h, a_mips, 1, a_dxgi);
	return g;
}

static TextureSlot Slot(int a_slot, const char* a_label, const char* a_path, GpuInfo a_gpu)
{
	TextureSlot t;
	t.slot = a_slot;
	t.label = a_label;
	t.path = a_path;
	t.bound = a_gpu.present;
	t.gpu = std::move(a_gpu);
	return t;
}

/** A healthy lit part: BC7 diffuse and BC5 normal, both with full mip chains. */
static ShapeInfo Healthy()
{
	ShapeInfo s;
	s.name = "Wall:0";
	s.shaderKind = "BSLightingShaderProperty";
	s.shaderType = "Default";
	s.featureId = 0;
	s.textures.push_back(Slot(0, "diffuse", "textures\\a\\wall.dds", Gpu(1024, 1024, 11, 98)));
	s.textures.push_back(Slot(1, "normal", "textures\\a\\wall_n.dds", Gpu(1024, 1024, 11, 83)));
	return s;
}

static bool Has(const std::vector<Diagnostic>& a_list, const char* a_rule)
{
	for (const auto& d : a_list)
		if (d.rule == a_rule)
			return true;
	return false;
}

static void TestDiagnostics()
{
	CHECK(Diagnose(Healthy()).empty());

	// parallax-no-height: the flag is on, no height map anywhere
	{
		ShapeInfo s = Healthy();
		s.flags = { "Parallax" };
		CHECK(Has(Diagnose(s), "parallax-no-height"));
		s.textures.push_back(Slot(3, "height (parallax)", "textures\\a\\wall_p.dds", Gpu(1024, 1024, 11, 80)));
		CHECK(!Has(Diagnose(s), "parallax-no-height"));
		ShapeInfo bound = Healthy();
		bound.flags = { "ParallaxOcclusion" };
		bound.heightBound = true;  // the material itself holds a height texture
		CHECK(!Has(Diagnose(bound), "parallax-no-height"));
	}
	// height-without-flag: a _p texture with no parallax flag
	{
		ShapeInfo s = Healthy();
		s.textures.push_back(Slot(3, "height (parallax)", "textures\\a\\wall_p.dds", Gpu(1024, 1024, 11, 80)));
		CHECK(Has(Diagnose(s), "height-without-flag"));
		s.flags = { "Parallax" };
		CHECK(!Has(Diagnose(s), "height-without-flag"));
		ShapeInfo pbr = Healthy();
		pbr.featureId = 32;  // TruePBR uses _p as displacement
		pbr.textures.push_back(Slot(3, "displacement", "textures\\pbr\\a\\wall_p.dds", Gpu(1024, 1024, 11, 80)));
		pbr.textures.push_back(Slot(5, "RMAOS", "textures\\pbr\\a\\wall_rmaos.dds", Gpu(1024, 1024, 11, 98)));
		CHECK(!Has(Diagnose(pbr), "height-without-flag"));
	}
	// shader kinds where slot 3 is not parallax: a vanilla head (FaceGen) carries BlankDetailmap.dds in slot 3 and must stay quiet,
	// as must skin tint, hair tint, eye and multilayer parts, whatever their slots 3 to 5 hold
	{
		const int kinds[] = { 4, 5, 6, 16, 11 };
		for (const int feature : kinds) {
			ShapeInfo head = Healthy();
			head.name = "MaleHeadNord";
			head.featureId = feature;
			head.textures.push_back(Slot(2, "glow / skin tint", "textures\\actors\\character\\male\\malehead_sk.dds", Gpu(1024, 1024, 11, 80)));
			head.textures.push_back(Slot(3, "height (parallax)", "textures\\actors\\character\\male\\BlankDetailmap.dds", Gpu(256, 256, 9, 98)));
			head.textures.push_back(Slot(4, "environment (cube map)", "textures\\cubemaps\\EyeCubeMap.dds", Gpu(256, 256, 9, 98)));
			CHECK(Diagnose(head).empty());
			head.flags = { "EnvMap" };
			head.textures.pop_back();
			CHECK(!Has(Diagnose(head), "env-no-cubemap"));
			head.flags = { "Parallax" };
			head.textures.pop_back();
			CHECK(!Has(Diagnose(head), "parallax-no-height"));
		}
		// the same height map on a plain Default shader still counts
		ShapeInfo plain = Healthy();
		plain.textures.push_back(Slot(3, "height (parallax)", "textures\\actors\\character\\male\\BlankDetailmap.dds", Gpu(256, 256, 9, 98)));
		CHECK(Has(Diagnose(plain), "height-without-flag"));
	}
	// pbr-without-flag / pbr-flag-without-texture
	{
		ShapeInfo s = Healthy();
		s.textures.push_back(Slot(5, "RMAOS", "textures\\pbr\\a\\wall_rmaos.dds", Gpu(1024, 1024, 11, 98)));
		CHECK(Has(Diagnose(s), "pbr-without-flag"));
		s.featureId = 32;
		CHECK(!Has(Diagnose(s), "pbr-without-flag"));
		CHECK(!Has(Diagnose(s), "pbr-flag-without-texture"));
		ShapeInfo flagOnly = Healthy();
		flagOnly.featureId = 32;
		CHECK(Has(Diagnose(flagOnly), "pbr-flag-without-texture"));
	}
	// env-no-cubemap
	{
		ShapeInfo s = Healthy();
		s.flags = { "EnvMap" };
		CHECK(Has(Diagnose(s), "env-no-cubemap"));
		s.textures.push_back(Slot(4, "environment (cube map)", "textures\\cubemaps\\mcgrutesilver_e.dds", Gpu(256, 256, 9, 98)));
		CHECK(!Has(Diagnose(s), "env-no-cubemap"));
		ShapeInfo cube = Healthy();
		cube.flags = { "EnvMap" };
		auto env = Slot(4, "environment (cube map)", "textures\\weird\\sky.dds", Gpu(256, 256, 9, 98));
		env.gpu.cube = true;
		cube.textures.push_back(env);
		CHECK(!Has(Diagnose(cube), "env-no-cubemap"));
	}
	// slot order of a lit shader (NIF order): a height map is read from slot 3 whatever its file name, a cube map from slot 4,
	// and an environment mask in slot 5 is not a cube map
	{
		ShapeInfo s = Healthy();
		s.flags = { "Parallax" };
		s.textures.push_back(Slot(3, "height (parallax)", "textures\\a\\wall_height.dds", Gpu(1024, 1024, 11, 80)));
		CHECK(!Has(Diagnose(s), "parallax-no-height"));
		ShapeInfo noFlag = Healthy();
		noFlag.textures.push_back(Slot(3, "height (parallax)", "textures\\a\\wall_height.dds", Gpu(1024, 1024, 11, 80)));
		CHECK(Has(Diagnose(noFlag), "height-without-flag"));
		ShapeInfo maskOnly = Healthy();
		maskOnly.flags = { "EnvMap" };
		maskOnly.textures.push_back(Slot(5, "environment mask", "textures\\a\\wall_m.dds", Gpu(512, 512, 10, 80)));
		CHECK(Has(Diagnose(maskOnly), "env-no-cubemap"));
		ShapeInfo slot4 = Healthy();
		slot4.flags = { "EnvMap" };
		slot4.textures.push_back(Slot(4, "environment (cube map)", "textures\\x\\room.dds", Gpu(256, 256, 9, 98)));
		CHECK(!Has(Diagnose(slot4), "env-no-cubemap"));
	}
	// texture-not-loaded: no texture object for diffuse, or an object without a GPU resource
	{
		ShapeInfo s = Healthy();
		s.textures[0].bound = false;
		s.textures[0].gpu = GpuInfo{};
		CHECK(Has(Diagnose(s), "texture-not-loaded"));
		ShapeInfo noGpu = Healthy();
		noGpu.textures[1].gpu = GpuInfo{};
		noGpu.textures[1].gpu.note = "no GPU texture bound";
		CHECK(Has(Diagnose(noGpu), "texture-not-loaded"));
		ShapeInfo optional = Healthy();  // an unbound optional slot (not diffuse/normal) is not an error by itself
		auto      glow = Slot(2, "glow / skin tint", "textures\\a\\wall_g.dds", GpuInfo{});
		glow.bound = false;
		optional.textures.push_back(glow);
		CHECK(!Has(Diagnose(optional), "texture-not-loaded"));
	}
	// normal-uncompressed and normal-one-mip
	{
		ShapeInfo s = Healthy();
		s.textures[1].gpu = Gpu(1024, 1024, 11, 28);  // R8G8B8A8
		CHECK(Has(Diagnose(s), "normal-uncompressed"));
		ShapeInfo tiny = Healthy();
		tiny.textures[1].gpu = Gpu(4, 4, 1, 28);  // a flat default normal: not worth a warning
		CHECK(!Has(Diagnose(tiny), "normal-uncompressed"));
		ShapeInfo oneMip = Healthy();
		oneMip.textures[1].gpu = Gpu(512, 512, 1, 83);
		CHECK(Has(Diagnose(oneMip), "normal-one-mip"));
		CHECK(!Has(Diagnose(oneMip), "one-mip-large"));  // the normal map has its own rule
	}
	// one-mip-large: over 256 and a single mip, for a non-normal texture
	{
		ShapeInfo s = Healthy();
		s.textures[0].gpu = Gpu(512, 512, 1, 98);
		CHECK(Has(Diagnose(s), "one-mip-large"));
		s.textures[0].gpu = Gpu(256, 256, 1, 98);
		CHECK(!Has(Diagnose(s), "one-mip-large"));
		s.textures[0].gpu = Gpu(2048, 2048, 12, 98);
		CHECK(!Has(Diagnose(s), "one-mip-large"));
	}
	// effect shaders have no normal slot: every slot must be bound, nothing is "normal"
	{
		ShapeInfo fx;
		fx.name = "Glow";
		fx.shaderKind = "BSEffectShaderProperty";
		fx.textures.push_back(Slot(-1, "source", "textures\\fx\\glow.dds", Gpu(128, 128, 8, 98)));
		CHECK(Diagnose(fx).empty());
		fx.textures[0].bound = false;
		fx.textures[0].gpu = GpuInfo{};
		CHECK(Has(Diagnose(fx), "texture-not-loaded"));
	}
	CHECK(PathEndsWith("Textures\\A\\Wall_P.DDS", "_p.dds"));
	CHECK(!PathEndsWith("a", "_p.dds"));
	CHECK(LooksLikePbrPath("textures\\pbr\\a\\x.dds"));
	CHECK(LooksLikePbrPath("textures\\a\\x_rmaos.dds"));
	CHECK(!LooksLikePbrPath("textures\\a\\x_n.dds"));
}

// ---- Fallout 4's rules ---------------------------------------------------------------------------------------------------

static FileOrigin Found(const char* a_source = "archive")
{
	FileOrigin o;
	o.looked = true;
	o.source = a_source;
	return o;
}

static FileOrigin Missing()
{
	FileOrigin o;
	o.looked = true;
	o.source = "not found";
	return o;
}

static std::vector<Diagnostic> Fo4(const ShapeInfo& a_shape) { return Diagnose(a_shape, DiagGame::kFallout4); }

static void TestDiagnosticsFo4()
{
	// the vanilla maple: Environment Map flag on, the material names no cube map, no parallax flag, a height-looking slot 3: no warning
	{
		ShapeInfo s = Healthy();
		s.flags = { "EnvMap" };
		s.materialPath = "Materials\\Landscape\\Trees\\MapleAtlas01.BGSM";
		s.materialOrigin = Found();
		s.textures.push_back(Slot(3, "height / greyscale", "Landscape\\Trees\\maple_h.dds", Gpu(512, 512, 10, 83)));
		CHECK(Fo4(s).empty());
		CHECK(Has(Diagnose(s), "env-no-cubemap"));  // Skyrim's rule still fires on the same data: Skyrim is unchanged
	}
	// the Skyrim-only rules never fire on Fallout 4
	{
		ShapeInfo s = Healthy();
		s.flags = { "Parallax" };
		s.featureId = 3;
		s.textures.push_back(Slot(-1, "x", "textures\\a\\x_rmaos.dds", Gpu(128, 128, 8, 98)));
		CHECK(Has(Diagnose(s), "pbr-without-flag"));
		CHECK(Fo4(s).empty());
		ShapeInfo pbr = Healthy();
		pbr.featureId = 32;
		CHECK(Has(Diagnose(pbr), "pbr-flag-without-texture"));
		CHECK(Fo4(pbr).empty());
	}
	// material-not-found
	{
		ShapeInfo s = Healthy();
		s.materialPath = "Materials\\Mod\\Gone.BGSM";
		s.materialOrigin = Missing();
		CHECK(Has(Fo4(s), "material-not-found"));
		s.materialOrigin = Found("loose");
		CHECK(!Has(Fo4(s), "material-not-found"));
		s.materialOrigin = FileOrigin{};  // never looked up: no claim
		CHECK(!Has(Fo4(s), "material-not-found"));
		s.materialPath.clear();
		s.materialOrigin = Missing();
		CHECK(!Has(Fo4(s), "material-not-found"));
		// a path in the warning text is shown as the game path, never Bethesda's build path
		s.materialPath = "C:\\projects\\Fallout4\\Build\\PC\\Data\\materials\\Architecture\\Foo.BGSM";
		const auto warned = Fo4(s);
		CHECK(Has(warned, "material-not-found"));
		for (const auto& d : warned)
			if (d.rule == "material-not-found") {
				CHECK(d.text.find("materials\\architecture\\foo.bgsm") != std::string::npos);
				CHECK(d.text.find("projects") == std::string::npos);
			}
	}
	// texture-not-loaded: only when the file is also in no loose file or archive
	{
		ShapeInfo s = Healthy();
		s.textures[0].bound = false;
		s.textures[0].gpu = GpuInfo{};
		s.textures[0].origin = Found();  // exists, the engine has not streamed it in: not a fault
		CHECK(!Has(Fo4(s), "texture-not-loaded"));
		s.textures[0].origin = Missing();
		CHECK(Has(Fo4(s), "texture-not-loaded"));
		s.textures[0].origin = FileOrigin{};  // unknown: no claim
		CHECK(!Has(Fo4(s), "texture-not-loaded"));
		// loaded but no GPU resource: always a fault
		ShapeInfo noGpu = Healthy();
		noGpu.textures[0].gpu.present = false;
		CHECK(Has(Fo4(noGpu), "texture-not-loaded"));
	}
	// env-map-missing: a named cube map that is not there; an empty env slot is fine
	{
		ShapeInfo s = Healthy();
		s.flags = { "EnvMap" };
		TextureSlot cube = Slot(4, "environment", "Shared\\Cubemaps\\gone.dds", GpuInfo{});
		cube.origin = Missing();
		s.textures.push_back(cube);
		CHECK(Has(Fo4(s), "env-map-missing"));
		s.textures.back().origin = Found();
		CHECK(!Has(Fo4(s), "env-map-missing"));
		CHECK(Fo4(s).empty());
	}
	// texture format rules need 64 pixels or more; one-mip-large keeps its 256 line
	{
		ShapeInfo s = Healthy();
		s.textures[1].gpu = Gpu(32, 32, 1, 28);
		CHECK(!Has(Fo4(s), "normal-uncompressed"));
		CHECK(!Has(Fo4(s), "normal-one-mip"));
		CHECK(Has(Diagnose(s), "normal-one-mip"));  // Skyrim has no size guard on the mip rule
		s.textures[1].gpu = Gpu(512, 512, 1, 83);
		CHECK(Has(Fo4(s), "normal-one-mip"));
		s.textures[1].gpu = Gpu(512, 512, 10, 28);
		CHECK(Has(Fo4(s), "normal-uncompressed"));
		ShapeInfo big = Healthy();
		big.textures[0].gpu = Gpu(1024, 1024, 1, 98);
		CHECK(Has(Fo4(big), "one-mip-large"));
		big.textures[0].gpu = Gpu(128, 128, 1, 98);
		CHECK(!Has(Fo4(big), "one-mip-large"));
	}
	// effect shaders (.bgem): every named texture is bound or its file is missing
	{
		ShapeInfo fx;
		fx.name = "Glow";
		fx.shaderKind = "BSEffectShaderProperty";
		fx.textures.push_back(Slot(-1, "source", "fx\\glow.dds", Gpu(128, 128, 8, 98)));
		CHECK(Fo4(fx).empty());
		fx.textures[0].bound = false;
		fx.textures[0].gpu = GpuInfo{};
		fx.textures[0].origin = Missing();
		CHECK(Has(Fo4(fx), "texture-not-loaded"));
	}
}

static void TestHelpers()
{
	CHECK(DxgiFormatName(98) == "BC7");
	CHECK(DxgiFormatName(99) == "BC7 (sRGB)");
	CHECK(DxgiFormatName(83) == "BC5");
	CHECK(DxgiFormatName(71) == "BC1");
	CHECK(DxgiFormatName(77) == "BC3");
	CHECK(DxgiFormatName(28) == "R8G8B8A8");
	CHECK(DxgiFormatName(9999) == "DXGI 9999");
	CHECK(DxgiIsCompressed(98) && DxgiIsCompressed(71) && !DxgiIsCompressed(28));
	CHECK(DxgiBitsPerPixel(28) == 32);
	CHECK(DxgiBitsPerPixel(98) == 8);  // BC7: 16 bytes per 16 pixels
	CHECK(DxgiBitsPerPixel(71) == 4);  // BC1: 8 bytes per 16 pixels

	CHECK(EstimateTextureBytes(4, 4, 1, 1, 71) == 8);
	CHECK(EstimateTextureBytes(8, 8, 1, 1, 28) == 256);
	CHECK(EstimateTextureBytes(8, 8, 1, 1, 98) == 64);  // four blocks of 16 bytes
	CHECK(EstimateTextureBytes(8, 8, 4, 1, 28) == 8 * 8 * 4 + 4 * 4 * 4 + 2 * 2 * 4 + 1 * 1 * 4);
	CHECK(EstimateTextureBytes(2, 2, 1, 1, 71) == 8);  // a block is never smaller than 4x4
	{
		// a 1024x1024 BC7 texture with a full 11-level chain, summed by hand: blocks per level times 16 bytes
		std::uint64_t expected = 0;
		for (int w = 1024; w >= 1; w /= 2)
			expected += static_cast<std::uint64_t>((w + 3) / 4) * static_cast<std::uint64_t>((w + 3) / 4) * 16u;
		CHECK(EstimateTextureBytes(1024, 1024, 11, 1, 98) == expected);
		CHECK(expected == 1398128u);
	}
	CHECK(EstimateTextureBytes(4, 4, 1, 6, 71) == 48);  // six array slices (a cube)
	CHECK(EstimateTextureBytes(4, 4, 1, 1, 9999) == 0);  // unknown format

	CHECK(HumanBytes(96) == "96 B");
	CHECK(HumanBytes(2048) == "2 KB");
	CHECK(HumanBytes(1024ull * 1024ull * 3 / 2) == "1.5 MB");

	CHECK(ModFolderFromPath("D:\\Lists\\X\\mods\\Some Mod\\textures\\a.dds") == "Some Mod");
	CHECK(ModFolderFromPath("d:/lists/x/MODS/Other/meshes/a.nif") == "Other");
	CHECK(ModFolderFromPath("D:\\Game\\Data\\textures\\a.dds").empty());
	CHECK(ModFolderFromPath("D:\\Lists\\X\\mods\\Only Folder").empty());  // nothing below the mod folder
	CHECK(ModFolderFromPath("").empty());

	CHECK(BodySlotName(32) == "body");
	CHECK(BodySlotName(30) == "head");
	CHECK(BodySlotName(33) == "hands");
	CHECK(BodySlotName(37) == "feet");
	CHECK(BodySlotName(131) == "head (hair cap)");
	CHECK(BodySlotName(99) == "slot 99");
	// Fallout 4 numbers its biped slots differently: 33 is the body (Skyrim's hands), 30 the hair top
	CHECK(Fo4BodySlotName(33) == "body");
	CHECK(Fo4BodySlotName(30) == "hair top");
	CHECK(Fo4BodySlotName(41) == "torso (armor)");
	CHECK(Fo4BodySlotName(60) == "Pip-Boy");
	CHECK(Fo4BodySlotName(99) == "slot 99");
	{
		WornItem w;
		CHECK(SlotNameOf(w, 33) == "hands");
		w.fallout4 = true;
		CHECK(SlotNameOf(w, 33) == "body");
	}
	CHECK(HexColor(255, 0, 16) == "#FF0010");
	CHECK(HexColor(300, -4, 0) == "#FF0000");
}

// ---- the archive reader: a synthetic v105 archive, laid out as the public format describes ------------------------------

static void Put32(std::vector<char>& a_out, std::uint32_t a_value) { a_out.insert(a_out.end(), reinterpret_cast<const char*>(&a_value), reinterpret_cast<const char*>(&a_value) + 4); }
static void Put64(std::vector<char>& a_out, std::uint64_t a_value) { a_out.insert(a_out.end(), reinterpret_cast<const char*>(&a_value), reinterpret_cast<const char*>(&a_value) + 8); }

/** One folder, with the named files; v104 or v105. Returns the archive bytes. */
static std::vector<char> MakeArchive(std::uint32_t a_version, const std::string& a_folder, const std::vector<std::string>& a_files, bool a_names)
{
	std::vector<char> out;
	const std::uint32_t recordSize = a_version == 105 ? 24 : 16;
	const std::uint32_t folderOffset = 36;
	std::uint32_t       totalFileNames = 0;
	for (const auto& f : a_files)
		totalFileNames += static_cast<std::uint32_t>(f.size() + 1);

	out.insert(out.end(), { 'B', 'S', 'A', '\0' });
	Put32(out, a_version);
	Put32(out, folderOffset);
	Put32(out, a_names ? 0x3u : 0x2u);  // flags: bit 0 directory names, bit 1 file names
	Put32(out, 1);                       // folders
	Put32(out, static_cast<std::uint32_t>(a_files.size()));
	Put32(out, static_cast<std::uint32_t>(a_folder.size() + 1));
	Put32(out, totalFileNames);
	Put32(out, 0);  // file flags + pad

	// folder record: hash, count, (v105: pad, offset, pad) / (v104: offset)
	// offset = where the folder's file block starts, plus total file name length (the format counts it that way)
	const std::uint32_t blockStart = folderOffset + recordSize;
	Put64(out, BsaFolderHash(a_folder));
	Put32(out, static_cast<std::uint32_t>(a_files.size()));
	if (a_version == 105) {
		Put32(out, 0);
		Put32(out, blockStart + totalFileNames);
		Put32(out, 0);
	} else {
		Put32(out, blockStart + totalFileNames);
	}
	// file record block: length-prefixed folder name, then 16-byte file records
	if (a_names) {
		out.push_back(static_cast<char>(a_folder.size() + 1));
		out.insert(out.end(), a_folder.begin(), a_folder.end());
		out.push_back('\0');
	}
	for (const auto& f : a_files) {
		Put64(out, BsaFileHash(f));
		Put32(out, 100);
		Put32(out, 0);
	}
	return out;
}

static void TestArchive()
{
	// hash invariants: case and slash direction do not matter, different names differ
	CHECK(BsaFileHash("WRWoodPost02.DDS") == BsaFileHash("wrwoodpost02.dds"));
	CHECK(BsaFolderHash("Textures/Architecture/Whiterun") == BsaFolderHash("textures\\architecture\\whiterun"));
	CHECK(BsaFileHash("a.dds") != BsaFileHash("b.dds"));
	CHECK(BsaFileHash("a.dds") != BsaFileHash("a.nif"));
	// the extension is added into the low half as the format says (.dds adds 0x8080)
	CHECK((BsaFileHash("ab.dds") & 0xFFFFFFFFull) ==
		  (static_cast<std::uint32_t>('b') | (2u << 16) | (static_cast<std::uint32_t>('a') << 24)) + 0x8080u);  // two letters: no second-last character
	// "a.dds": single-letter base: low = last | len << 16 | first << 24, plus ext bonus
	CHECK((BsaFileHash("a.dds") & 0xFFFFFFFFull) == (static_cast<std::uint32_t>('a') | (1u << 16) | (static_cast<std::uint32_t>('a') << 24)) + 0x8080u);

	CHECK(ArchiveBelongsToPlugin("Skyrim - Textures0.bsa", "Skyrim.esm"));
	CHECK(ArchiveBelongsToPlugin("skyrim.bsa", "Skyrim.esm"));
	CHECK(ArchiveBelongsToPlugin("Update.bsa", "Update.esm"));
	CHECK(ArchiveBelongsToPlugin("My Mod - Textures.bsa", "my mod.esp"));
	CHECK(!ArchiveBelongsToPlugin("My Mod 2.bsa", "My Mod.esp"));
	CHECK(!ArchiveBelongsToPlugin("My Mod.ba2", "My Mod.esp"));

	const auto dir = std::filesystem::temp_directory_path() / "lookingat_report_tests";
	std::filesystem::create_directories(dir);
	for (const std::uint32_t version : { 104u, 105u }) {
		for (const bool names : { true, false }) {
			const auto path = dir / ("t" + std::to_string(version) + (names ? "n" : "x") + ".bsa");
			{
				const auto    bytes = MakeArchive(version, "textures\\test\\folder", { "wall.dds", "wall_n.dds", "roof.dds" }, names);
				std::ofstream f(path, std::ios::binary | std::ios::trunc);
				f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
			}
			const auto index = BsaIndex::Open(path);
			CHECK(index.has_value());
			if (!index)
				continue;
			CHECK(index->Version() == version);
			CHECK(index->FileCount() == 3);
			CHECK(index->Contains("textures\\test\\folder\\wall.dds"));
			CHECK(index->Contains("Textures/Test/Folder/WALL_N.DDS"));
			CHECK(index->Contains("textures\\test\\folder\\roof.dds"));
			CHECK(!index->Contains("textures\\test\\folder\\floor.dds"));
			CHECK(!index->Contains("textures\\test\\other\\wall.dds"));
			CHECK(!index->Contains("wall.dds"));
			std::error_code ec;
			std::filesystem::remove(path, ec);
		}
	}
	// not an archive: refused, not crashed
	{
		const auto path = dir / "junk.bsa";
		{
			std::ofstream f(path, std::ios::binary | std::ios::trunc);
			f << "this is not a bsa file at all, just text";
		}
		CHECK(!BsaIndex::Open(path).has_value());
		CHECK(!BsaIndex::Open(dir / "missing.bsa").has_value());
		std::error_code ec;
		std::filesystem::remove(path, ec);
	}
	std::error_code ec;
	std::filesystem::remove(dir, ec);
}

int main(int argc, char** argv)
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	if (argc >= 3) {
		// manual check against a real archive
		const auto index = BsaIndex::Open(argv[1]);
		if (!index) {
			std::printf("cannot open %s\n", argv[1]);
			return 2;
		}
		std::printf("%s: version %u, %u files\n", argv[1], index->Version(), index->FileCount());
		std::printf("%s -> %s\n", argv[2], index->Contains(argv[2]) ? "found" : "not found");
		if (argc >= 4)
			std::printf("%s -> %s\n", argv[3], index->Contains(argv[3]) ? "found" : "not found");
		return 0;
	}
	TestDiagnostics();
	TestDiagnosticsFo4();
	TestHelpers();
	TestArchive();
	std::printf("%d checks, %d failed\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
