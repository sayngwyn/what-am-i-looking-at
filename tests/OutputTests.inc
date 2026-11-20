// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
// Included by PickingTests.cpp: tests for the console and JSON printers.

// ---- a minimal strict JSON validator (so the file other tools will read is proven well formed) ----
struct JsonCheck
{
	const std::string& s;
	std::size_t        i = 0;
	explicit JsonCheck(const std::string& a_text) :
		s(a_text) {}
	void Ws()
	{
		while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t'))
			++i;
	}
	bool Lit(const char* w)
	{
		const std::size_t n = std::strlen(w);
		if (s.compare(i, n, w) == 0) {
			i += n;
			return true;
		}
		return false;
	}
	bool Str()
	{
		if (i >= s.size() || s[i] != '"')
			return false;
		++i;
		while (i < s.size() && s[i] != '"') {
			if (static_cast<unsigned char>(s[i]) < 0x20)
				return false;
			if (s[i] == '\\') {
				++i;
				if (i >= s.size())
					return false;
				if (s[i] == 'u')
					i += 4;
			}
			++i;
		}
		if (i >= s.size())
			return false;
		++i;
		return true;
	}
	bool Num()
	{
		const std::size_t st = i;
		if (i < s.size() && s[i] == '-')
			++i;
		while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '+' || s[i] == '-'))
			++i;
		return i > st;
	}
	bool Value()
	{
		Ws();
		if (i >= s.size())
			return false;
		if (s[i] == '{') {
			++i;
			Ws();
			if (s[i] == '}') {
				++i;
				return true;
			}
			for (;;) {
				Ws();
				if (!Str())
					return false;
				Ws();
				if (s[i++] != ':')
					return false;
				if (!Value())
					return false;
				Ws();
				if (s[i] == ',') {
					++i;
					continue;
				}
				if (s[i] == '}') {
					++i;
					return true;
				}
				return false;
			}
		}
		if (s[i] == '[') {
			++i;
			Ws();
			if (s[i] == ']') {
				++i;
				return true;
			}
			for (;;) {
				if (!Value())
					return false;
				Ws();
				if (s[i] == ',') {
					++i;
					continue;
				}
				if (s[i] == ']') {
					++i;
					return true;
				}
				return false;
			}
		}
		if (s[i] == '"')
			return Str();
		if (Lit("true") || Lit("false") || Lit("null"))
			return true;
		return Num();
	}
	bool Valid()
	{
		if (!Value())
			return false;
		Ws();
		return i == s.size();
	}
};

static ShapeInfo MakeShape(int index, const char* name, const char* diffuse, const char* normal)
{
	ShapeInfo s;
	s.index = index;
	s.name = name;
	s.nodeType = "BSTriShape";
	s.shaderKind = "BSLightingShaderProperty";
	s.shaderType = "Default";
	s.featureId = 0;
	s.pbr = "no";
	s.flags = { "Specular", "VertexColors" };
	s.vertexFlags = { "UV", "Normals", "Tangents", "Colors", "FullPrecision" };
	s.vertices = 410;
	s.triangles = 620;
	s.textures.push_back({ 0, "diffuse", diffuse, "" });
	s.textures.push_back({ 1, "normal", normal, "" });
	s.exactTested = true;
	return s;
}

static PickInfo MakeSample()
{
	PickInfo p;
	p.counter = 3;
	p.timestamp = "2026-11-14T22:40:05";
	p.unixMs = 1700000000000;
	p.trigger = "console_click";
	p.plugin = "What Am I Looking At 1.0.0";
	p.valid = true;
	p.object = { "Farmhouse06", 0x000A1B2C, 0x0001234F, "STAT", "Farmhouse06", "", "Architecture\\Whiterun\\WRFarmhouse06.nif", "Farmhouse06" };
	p.shapes.push_back(MakeShape(0, "WoodWall01:0", "textures\\architecture\\whiterun\\wrwoodwall01.dds", "textures\\architecture\\whiterun\\wrwoodwall01_n.dds"));
	p.shapes.push_back(MakeShape(1, "WoodPost02:0", "textures\\architecture\\whiterun\\wrwoodpost02.dds", "textures\\architecture\\whiterun\\wrwoodpost02_n.dds"));
	p.shapes.push_back(MakeShape(2, "Thatch02:0", "textures\\architecture\\whiterun\\wrthatch02.dds", "textures\\architecture\\whiterun\\wrthatch02_n.dds"));
	p.shapes[1].textures.push_back({ 7, "backlight / specular mask", "textures\\architecture\\whiterun\\wrwoodpost02_s.dds", "" });
	p.shapes[1].clicked = true;
	p.shapes[1].rank = 0;
	p.shapes[1].boundHit = true;
	p.shapes[1].exactHit = true;
	p.shapes[1].exactDistance = 312.4f;
	p.shapes[1].hitTriangle = 118;
	p.shapes[1].hitPoint = { 1.0f, 2.0f, 3.0f };
	p.clicked = 1;
	p.method = "exact";
	p.hasRay = true;
	p.ray = { { 10, 20, 30 }, { 0, 1, 0 } };
	p.cursor = { true, 640, 360, 1280, 720, 0.5f, 0.5f };
	p.rayCheck = "verified";
	return p;
}

static void TestOutput()
{
	const PickInfo sample = MakeSample();

	// console lines
	const auto lines = FormatConsole(sample, 60);
	CHECK(lines.size() >= 9);
	std::string all;
	for (const auto& l : lines) {
		all += l + "\n";
		CHECK(l.rfind("[What Am I Looking At] ", 0) == 0);
	}
	CHECK(all.find("CLICKED PART  \"WoodPost02:0\"") != std::string::npos);
	CHECK(all.find("exact: triangle 118") != std::string::npos);
	CHECK(all.find("ref 000A1B2C") != std::string::npos);
	CHECK(all.find("base 0001234F (STAT)") != std::string::npos);
	CHECK(all.find("wrwoodpost02_s.dds") != std::string::npos);
	CHECK(all.find("other parts (2):") != std::string::npos);
	CHECK(all.find("wrwoodwall01.dds") != std::string::npos);
	CHECK(FormatConsole(sample, 1).size() <= lines.size());  // the cap on "other parts" lines
	CHECK(all.find("ray  ") == std::string::npos);            // a verified ray stays quiet unless asked for
	CHECK(FormatConsole(sample, 60, true).back().find("[verified]") != std::string::npos);
	std::printf("---- example console output ----\n%s-------------------------------\n", all.c_str());

	// approximate output names the runners-up and says why
	{
		PickInfo a = sample;
		a.method = "approximate";
		a.runnersUp = { 2 };
		a.shapes[0].boundHit = true;
		a.shapes[0].exactTested = false;
		a.shapes[0].exactSkipReason = "skinned (bind pose is not the world pose)";
		const auto  l = FormatConsole(a, 60);
		std::string t;
		for (const auto& x : l)
			t += x + "\n";
		CHECK(t.find("APPROXIMATE") != std::string::npos);
		CHECK(t.find("also under the cursor: Thatch02:0") != std::string::npos);
		CHECK(t.find("skinned") != std::string::npos);
	}
	// nothing under the cursor / invalid
	{
		PickInfo n = sample;
		n.clicked = -1;
		n.method = "none";
		n.message = "the ray does not touch any part of this object";
		CHECK(FormatConsole(n, 60).size() == 4);
		PickInfo bad;
		bad.counter = 9;
		bad.message = "nothing in front of the crosshair";
		CHECK(FormatConsole(bad, 60).size() == 1);
	}
	// an unverified ray is called out
	{
		PickInfo u = sample;
		u.rayCheck = "unverified: engine projection disagrees";
		const auto l = FormatConsole(u, 60);
		CHECK(l.back().find("[unverified") != std::string::npos);
	}

	// JSON
	const std::string json = ToJson(sample);
	CHECK(JsonCheck(json).Valid());
	CHECK(json.find("\"counter\": 3") != std::string::npos);
	CHECK(json.find("\"ref_form_id\": \"000A1B2C\"") != std::string::npos);
	CHECK(json.find("\"clicked_shape\": 1") != std::string::npos);
	CHECK(json.find("wrwoodpost02.dds") != std::string::npos);
	CHECK(json.find("Architecture\\\\Whiterun\\\\WRFarmhouse06.nif") != std::string::npos);  // backslashes escaped
	{
		PickInfo weird = sample;
		weird.object.name = "Quote \" and \\ and \n newline \t tab \x01";
		weird.shapes[0].boundRadius = std::numeric_limits<float>::infinity();
		const std::string j = ToJson(weird);
		CHECK(JsonCheck(j).Valid());
		CHECK(j.find("null") != std::string::npos);  // infinity becomes null, never invalid JSON
	}
	CHECK(JsonEscape("a\"b") == "a\\\"b");
	CHECK(JsonEscape("\x1f") == "\\u001f");
	{
		PickInfo empty;
		CHECK(JsonCheck(ToJson(empty)).Valid());
	}

	// atomic write: replaces an existing file, leaves no temp file
	{
		const std::filesystem::path dir = "ti-test-output";
		const auto                  file = dir / "last-pick.json";
		{ std::error_code ec; std::filesystem::remove_all(dir, ec); }
		CHECK(WriteFileAtomic(file, "first").empty());
		CHECK(WriteFileAtomic(file, json).empty());
		std::ifstream in(file, std::ios::binary);
		std::string   back((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		CHECK(back == json);
		CHECK(!std::filesystem::exists(dir / "last-pick.json.tmp"));
		{ std::error_code ec; std::filesystem::remove_all(dir, ec); }
	}
}

// ---- the Report: identity, facts, parts' GPU info, NPC section, warnings ----

static GpuInfo SampleGpu()
{
	GpuInfo g;
	g.present = true;
	g.width = 512;
	g.height = 512;
	g.mips = 10;
	g.dxgi = 98;
	g.format = "BC7";
	g.bytes = 349525;
	return g;
}

/** A pick filled the way the game-side code fills it, by hand. */
static PickInfo MakeReportSample()
{
	PickInfo p = MakeSample();
	auto&    id = p.identity;
	id.kind = "door";
	id.displayName = "Whiterun House";
	id.ref.formId = 0x000A1B2C;
	id.ref.type = "REFR";
	id.ref.addedBy = "Skyrim.esm";
	id.ref.lastChangedBy = "Unofficial Skyrim Special Edition Patch.esp";
	id.ref.chain = { "Skyrim.esm", "Unofficial Skyrim Special Edition Patch.esp" };
	id.base.formId = 0x0001234F;
	id.base.type = "DOOR";
	id.base.editorId = "FarmhouseDoor";
	id.base.addedBy = "Skyrim.esm";
	id.base.lastChangedBy = "Skyrim.esm";
	id.base.chain = { "Skyrim.esm" };
	id.editorIdSource = "po3 Tweaks";
	id.cellName = "Whiterun";
	id.cellEditorId = "WhiterunExterior";
	id.hasCoords = true;
	id.cellX = 6;
	id.cellY = -2;
	id.worldspaceEditorId = "Tamriel";
	id.position = { 100.0f, 200.5f, 300.0f };
	id.scale = 1.0f;
	id.modelOrigin.looked = true;
	id.modelOrigin.source = "archive";
	id.modelOrigin.archive = "Skyrim - Meshes0.bsa";
	p.facts = { { "Lock", "locked (Adept)" }, { "Leads to", "Whiterun Hall \"Jorrvaskr\" (JorrvaskrInterior) via door 0001FFFF" } };

	auto& s = p.shapes[1];
	s.alphaMode = "test";
	s.alphaThreshold = 128;
	s.alphaTestFunction = "greater-equal";
	s.hasMaterial = true;
	s.specularColor[0] = 1.0f;
	s.specularStrength = 2.0f;
	s.glossiness = 80.0f;
	s.hasEmissive = true;
	s.emissiveColor[1] = 0.5f;
	s.emissiveMultiple = 1.5f;
	s.uvScale[0] = 2.0f;
	s.bodyArea = "body, hands";
	s.partitionSlots = { 32, 33 };
	s.textures[0].bound = true;
	s.textures[0].gpu.present = true;
	s.textures[0].gpu.width = 2048;
	s.textures[0].gpu.height = 1024;
	s.textures[0].gpu.mips = 12;
	s.textures[0].gpu.dxgi = 98;
	s.textures[0].gpu.format = "BC7";
	s.textures[0].gpu.bytes = 2796202;
	s.textures[0].origin.looked = true;
	s.textures[0].origin.source = "loose";
	s.textures[0].origin.mod = "Some Texture Mod";
	s.textures[1].bound = false;  // never loaded
	s.diagnostics.push_back({ "texture-not-loaded", "wrwoodpost02_n.dds: the engine has no texture loaded for this slot." });
	p.diagnostics.push_back({ "texture-not-loaded", "WoodPost02:0: wrwoodpost02_n.dds: the engine has no texture loaded for this slot." });

	p.npc.present = true;
	p.npc.sex = "female";
	p.npc.race = "Nord (NordRace, 00013746)";
	p.npc.skin = "SkinNakedFemale (000D7A2F)";
	p.npc.skinToneColor = "#C8A58C";
	p.npc.skeleton = "Actors\\Character\\Character Assets Female\\skeleton_female.nif";
	p.npc.faceNpcFormId = 0x00013475;
	p.npc.faceGen.looked = true;
	p.npc.faceGen.verdict = kFaceGenFound;
	p.npc.faceGen.evidence = "found FaceGeom mesh and FaceTint texture";
	p.npc.faceGen.meshPath = "meshes\\actors\\character\\facegendata\\facegeom\\skyrim.esm\\00013475.nif";
	p.npc.faceGen.meshOrigin.looked = true;
	p.npc.faceGen.meshOrigin.source = "archive";
	p.npc.faceGen.renderTargetIndex = -1;
	WornItem w;
	w.slots = { 32, 33 };
	w.name = "Fine Clothes";
	w.itemFormId = 0x00013ED7;
	w.model = "Armor\\Clothes\\Clothes.nif";
	w.live3d = true;
	w.textures.push_back({ "ClothesBody:0", "diffuse", "textures\\armor\\clothes\\f_body.dds", SampleGpu() });
	p.npc.worn.push_back(w);
	p.npc.skinItem.present = true;
	p.npc.skinItem.name = "SkinNakedFemale";
	p.npc.skinItem.formId = 0x000D7A2F;
	p.npc.skinItem.plugin = "Skyrim.esm";
	p.npc.skinItem.source = "the race's skin";
	WornItem sk;
	sk.slots = { 32 };
	sk.name = "SkinNakedFemale";
	sk.itemFormId = 0x000D7A2F;
	sk.addonFormId = 0x00000D67;
	sk.addonPlugin = "Skyrim.esm";
	sk.model = "Actors\\Character\\Character Assets\\FemaleBody_1.nif";
	sk.live3d = true;
	p.npc.skinItem.parts.push_back(sk);
	HeadPartInfo h;
	h.type = "Hair";
	h.formId = 0x00014EC6;
	h.editorId = "HairFemaleNord01";
	h.model = "Actors\\Character\\Character Assets\\Hair\\Hair01.nif";
	h.textureSetPaths = { "textures\\actors\\character\\female\\hair01.dds" };
	p.npc.headParts.push_back(h);
	return p;
}

static void TestReportOutput()
{
	const PickInfo p = MakeReportSample();

	// JSON: valid, versioned, and carries the new sections with the old ones untouched
	const std::string json = ToJson(p);
	CHECK(JsonCheck(json).Valid());
	CHECK(json.find("\"schema\": 1") != std::string::npos);
	CHECK(json.find("\"report_version\": 1") != std::string::npos);
	CHECK(json.find("\"identity\"") != std::string::npos);
	CHECK(json.find("\"kind\": \"door\"") != std::string::npos);
	CHECK(json.find("\"source_files\": [\"Skyrim.esm\", \"Unofficial Skyrim Special Edition Patch.esp\"]") != std::string::npos);
	CHECK(json.find("\"editor_id\": \"FarmhouseDoor\"") != std::string::npos);
	CHECK(json.find("\"editor_id_source\": \"po3 Tweaks\"") != std::string::npos);
	CHECK(json.find("\"facts\"") != std::string::npos);
	CHECK(json.find("\"label\": \"Lock\"") != std::string::npos);
	CHECK(json.find("\\\"Jorrvaskr\\\"") != std::string::npos);  // quotes inside a fact are escaped
	CHECK(json.find("\"alpha\"") != std::string::npos);
	CHECK(json.find("\"test_threshold\": 128") != std::string::npos);
	CHECK(json.find("\"specular_strength\"") != std::string::npos);
	CHECK(json.find("\"emissive_multiple\"") != std::string::npos);
	CHECK(json.find("\"uv\"") != std::string::npos);
	CHECK(json.find("\"body_area\": \"body, hands\"") != std::string::npos);
	CHECK(json.find("\"gpu\"") != std::string::npos);
	CHECK(json.find("\"format\": \"BC7\"") != std::string::npos);
	CHECK(json.find("\"width\": 2048") != std::string::npos);
	CHECK(json.find("\"mips\": 12") != std::string::npos);
	CHECK(json.find("\"origin\"") != std::string::npos);
	CHECK(json.find("\"mod\": \"Some Texture Mod\"") != std::string::npos);
	CHECK(json.find("\"diagnostics\"") != std::string::npos);
	CHECK(json.find("\"rule\": \"texture-not-loaded\"") != std::string::npos);
	CHECK(json.find("\"npc\"") != std::string::npos);
	CHECK(json.find("\"verdict\": \"file found\"") != std::string::npos);
	CHECK(json.find("\"worn\"") != std::string::npos);
	CHECK(json.find("\"slot_names\": [\"body\", \"hands\"]") != std::string::npos);
	CHECK(json.find("\"head_parts\"") != std::string::npos);
	CHECK(json.find("HairFemaleNord01") != std::string::npos);
	// the old fields keep their place and spelling
	CHECK(json.find("\"ref_form_id\": \"000A1B2C\"") != std::string::npos);
	CHECK(json.find("\"clicked_shape\": 1") != std::string::npos);

	// a pick with no report data (nothing filled) still writes valid JSON with the new sections empty
	{
		const PickInfo    plain = MakeSample();
		const std::string j = ToJson(plain);
		CHECK(JsonCheck(j).Valid());
		CHECK(j.find("\"report_version\": 1") != std::string::npos);
		CHECK(j.find("\"present\": false") != std::string::npos);  // npc absent
	}

	// console: compact summary lines for identity, facts, the clicked part with GPU info, and warnings
	const auto  lines = FormatConsole(p, 60);
	std::string all;
	for (const auto& l : lines) {
		all += l + "\n";
		CHECK(l.rfind("[What Am I Looking At] ", 0) == 0);
	}
	CHECK(all.find("kind   door") != std::string::npos);
	CHECK(all.find("added by Skyrim.esm, last changed by Unofficial Skyrim Special Edition Patch.esp") != std::string::npos);
	CHECK(all.find("where  exterior Whiterun (WhiterunExterior) [6, -2] in Tamriel") != std::string::npos);
	CHECK(all.find("facts  Lock: locked (Adept)  |  Leads to:") != std::string::npos);
	CHECK(all.find("[archive Skyrim - Meshes0.bsa]") != std::string::npos);  // model origin
	CHECK(all.find("2048x1024 BC7 12 mips 2.7 MB") != std::string::npos);
	CHECK(all.find("[loose from mod Some Texture Mod]") != std::string::npos);
	CHECK(all.find("NOT LOADED") != std::string::npos);
	CHECK(all.find("WARNING wrwoodpost02_n.dds") != std::string::npos);
	CHECK(all.find("alpha test (threshold 128, greater-equal)") != std::string::npos);
	CHECK(all.find("body    body, hands") != std::string::npos);
	CHECK(all.find("worn   Fine Clothes [32,33]") != std::string::npos);
	CHECK(all.find("head   Hair HairFemaleNord01") != std::string::npos);
	CHECK(all.find("FaceGen: file found") != std::string::npos);
	CHECK(all.find("skin   SkinNakedFemale (000D7A2F), the race's skin:   [32] addon 00000D67") != std::string::npos);
	CHECK(all.find("skin   ") < all.find("worn   "));
	std::printf("---- example console output (report) ----\n%s-----------------------------------------\n", all.c_str());

	// a pick whose ray missed the object (nearest part chosen) says so
	{
		PickInfo n = p;
		n.method = "nearest_to_center";
		n.notes.push_back("the screen-center ray misses this object; part nearest to it, 12.0 units off");
		const auto  l = FormatConsole(n, 60);
		std::string t;
		for (const auto& x : l)
			t += x + "\n";
		CHECK(t.find("NEAREST TO THE SCREEN CENTER") != std::string::npos);
		CHECK(t.find("12.0 units off") != std::string::npos);
	}
}

// The Fallout 4 material file path of a part: in the console report and in the JSON (and absent when a part has none).
static void TestMaterialOutput()
{
	PickInfo p = MakeSample();
	auto&    s = p.shapes[1];
	s.materialPath = "Materials\\SetDressing\\Crates\\WoodenCrate01Dest.BGSM";
	s.materialSource = "shader property name";
	s.materialOrigin.looked = true;
	s.materialOrigin.source = "archive";
	s.materialOrigin.archive = "Fallout4 - Materials.ba2";
	s.rootMaterial = "Materials\\SetDressing\\Crates\\Root.bgsm";

	std::string all;
	for (const auto& l : FormatConsole(p, 60))
		all += l + "\n";
	CHECK(all.find("material Materials\\SetDressing\\Crates\\WoodenCrate01Dest.BGSM   [archive Fallout4 - Materials.ba2]") != std::string::npos);
	CHECK(all.find("root material Materials\\SetDressing\\Crates\\Root.bgsm") != std::string::npos);

	const std::string j = ToJson(p);
	CHECK(JsonCheck(j).Valid());
	CHECK(j.find("\"material_file\"") != std::string::npos);
	CHECK(j.find("Materials\\\\SetDressing\\\\Crates\\\\WoodenCrate01Dest.BGSM") != std::string::npos);
	CHECK(j.find("\"found_in\": \"shader property name\"") != std::string::npos);
	CHECK(j.find("\"root_material\"") != std::string::npos);
	CHECK(j.find("Fallout4 - Materials.ba2") != std::string::npos);

	// a part with no material file prints none, and says why in the JSON only when something was recorded
	PickInfo q = MakeSample();
	std::string plain;
	for (const auto& l : FormatConsole(q, 60))
		plain += l + "\n";
	CHECK(plain.find("material ") == std::string::npos);
	CHECK(ToJson(q).find("material_file") == std::string::npos);
	q.shapes[1].materialSource = "no name: the material values are stored in the mesh file";
	const std::string jq = ToJson(q);
	CHECK(JsonCheck(jq).Valid() && jq.find("\"material_file\"") != std::string::npos && jq.find("stored in the mesh file") != std::string::npos);
}
