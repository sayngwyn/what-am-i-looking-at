// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// Console and JSON output for the terrain, water and sky kinds, the hits list and the FaceGen "missing" wording.
// Included by PickingTests.cpp after OutputTests.inc (uses its CHECK, JsonCheck and MakeReportSample helpers).

static PickInfo LandSample(const std::string& a_kind)
{
	PickInfo p;
	p.valid = true;
	p.counter = 9;
	p.identity.kind = a_kind;
	p.rayCheck = "verified";
	return p;
}

static std::string Joined(const std::vector<std::string>& a_lines)
{
	std::string all;
	for (const auto& l : a_lines) {
		all += l + "\n";
		CHECK(l.rfind("[What Am I Looking At] ", 0) == 0);
	}
	return all;
}

static void TestLandOutput()
{
	// ---- terrain
	{
		PickInfo p = LandSample("terrain");
		auto&    t = p.terrain;
		t.present = true;
		t.cellFormId = 0x0000A1B2;
		t.cellName = "Riverwood";
		t.cellEditorId = "RiverwoodExterior";
		t.cellX = 1;
		t.cellY = -1;
		t.worldspace = "Tamriel";
		t.quadrant = 2;
		t.quadrantName = "north-west";
		t.vertexX = 5;
		t.vertexY = 9;
		t.vertexPosition = { 4800, -3000, 220 };
		t.point = { 4790, -3010, 221.5f };
		t.heightAtHit = 221.5f;
		t.blendMethod = "interpolated across the four vertices around the hit";
		t.heightCheck = "engine land height 221.4, ours 221.5";
		t.footstepMaterial = "MaterialGravel";
		TerrainLayer base;
		base.role = "base";
		base.formId = 0x00000A01;
		base.editorId = "LDirt01";
		base.addedBy = "Skyrim.esm";
		base.lastChangedBy = "Skyrim.esm";
		base.textures.push_back({ "diffuse", "textures\\landscape\\dirt01.dds" });
		base.textures.push_back({ "normal", "textures\\landscape\\dirt01_n.dds" });
		base.footstepMaterial = "MaterialDirt";
		TerrainLayer l1;
		l1.role = "layer 1";
		l1.formId = 0x00000A02;
		l1.editorId = "LGravel01";
		l1.addedBy = "Skyrim.esm";
		l1.lastChangedBy = "Some Landscape Mod.esp";
		l1.rawPercent = 90;
		l1.percent = 70.9f;
		l1.grasses.push_back("GrassPebbles (GrassPebbles01, 00000B01)");
		l1.footstepMaterial = "MaterialGravel";
		t.layers = { base, l1 };
		HitInfo hit;
		hit.kind = "terrain";
		hit.label = "land in Riverwood";
		hit.distance = 812.4f;
		hit.point = t.point;
		p.hits.push_back(hit);
		p.hitIndex = 0;
		const std::string all = Joined(FormatConsole(p, 60));
		CHECK(all.find("TERRAIN  Riverwood (RiverwoodExterior) [1, -1] in Tamriel   quadrant north-west   nearest vertex (5, 9)") != std::string::npos);
		CHECK(all.find("812.4 units from the camera") != std::string::npos);
		CHECK(all.find("blend  interpolated across the four vertices") != std::string::npos);
		CHECK(all.find("base      100%") != std::string::npos);
		CHECK(all.find("layer 1   71%") != std::string::npos);
		CHECK(all.find("[Skyrim.esm > Some Landscape Mod.esp]") != std::string::npos);
		CHECK(all.find("diffuse: textures\\landscape\\dirt01.dds") != std::string::npos);
		CHECK(all.find("grass: GrassPebbles (GrassPebbles01, 00000B01)") != std::string::npos);
		CHECK(all.find("footstep material here: MaterialGravel") != std::string::npos);
		const std::string j = ToJson(p);
		CHECK(JsonCheck(j).Valid());
		CHECK(j.find("\"quadrant_name\": \"north-west\"") != std::string::npos);
		CHECK(j.find("\"raw_percent\": 90") != std::string::npos);
		CHECK(j.find("\"blend_method\"") != std::string::npos);
		CHECK(j.find("\"hit_index\": 0") != std::string::npos);
		std::printf("---- example console output (terrain) ----\n%s------------------------------------------\n", all.c_str());
	}
	// ---- water
	{
		PickInfo p = LandSample("water");
		auto&    w = p.water;
		w.present = true;
		w.type.formId = 0x00000018;
		w.type.editorId = "DefaultWater";
		w.type.addedBy = "Skyrim.esm";
		w.type.lastChangedBy = "Water Mod.esp";
		w.type.chain = { "Skyrim.esm", "Water Mod.esp" };
		w.typeName = "Water";
		w.source = "the cell's water type";
		w.height = -200.0f;
		w.hasCellHeight = true;
		w.cellHeight = -200.0f;
		w.cellVersusPlane = "the cell's own water at -200.0; no placed plane";
		w.noiseTextures.push_back({ "noise 1", "textures\\water\\wave.dds" });
		w.shallowColor = "#406080";
		w.deepColor = "#102030";
		w.reflectionColor = "#FFFFFF";
		w.values.push_back({ "fresnel", "0.30" });
		w.cellName = "Riverwood";
		const std::string all = Joined(FormatConsole(p, 60));
		CHECK(all.find("WATER  type Water (DefaultWater, 00000018)   surface height -200.0   in Riverwood") != std::string::npos);
		CHECK(all.find("added by Skyrim.esm, last changed by Water Mod.esp") != std::string::npos);
		CHECK(all.find("plane  the cell's own water") != std::string::npos);
		CHECK(all.find("noise  noise 1: textures\\water\\wave.dds") != std::string::npos);
		CHECK(all.find("color  shallow #406080   deep #102030   reflection #FFFFFF") != std::string::npos);
		const std::string j = ToJson(p);
		CHECK(JsonCheck(j).Valid());
		CHECK(j.find("\"cell_water_height\": -200") != std::string::npos);
		CHECK(j.find("\"shallow_color\": \"#406080\"") != std::string::npos);
	}
	// ---- sky
	{
		PickInfo p = LandSample("sky");
		auto&    s = p.sky;
		s.present = true;
		s.mode = "exterior";
		s.hour = 13.08f;
		s.hourText = "13:05";
		s.currentWeather.formId = 0x0010A24F;
		s.currentWeather.editorId = "SkyrimClear";
		s.currentWeather.lastChangedBy = "Skyrim.esm";
		s.previousWeather.formId = 0x0010A250;
		s.previousWeather.editorId = "SkyrimCloudy";
		s.previousWeather.lastChangedBy = "Weather Mod.esp";
		s.transitionPercent = 42.0f;
		s.weatherClass = "pleasant";
		s.climate.formId = 0x00000123;
		s.climate.editorId = "SkyrimClimate";
		s.climate.lastChangedBy = "Skyrim.esm";
		CloudLayerInfo c0;
		c0.weather = "current";
		c0.index = 0;
		c0.texture = "textures\\sky\\clouds01.dds";
		c0.speedX = 3;
		c0.speedY = -2;
		c0.alpha = 0.75f;
		CloudLayerInfo c1;
		c1.weather = "previous";
		c1.index = 1;
		c1.texture = "textures\\sky\\clouds02.dds";
		c1.speedX = 1;
		c1.speedY = 1;
		s.clouds = { c0, c1 };
		s.objects.push_back({ "sun", "textures\\sky\\sun.dds" });
		s.imageSpace = "DefaultImageSpace (0000ABCD)";
		s.imageSpaceSource = "the weather's image space for this hour";
		s.cellName = "Tamriel exterior";
		const std::string all = Joined(FormatConsole(p, 60));
		CHECK(all.find("SKY  exterior   time 13:05") != std::string::npos);
		CHECK(all.find("now SkyrimClear (0010A24F) by Skyrim.esm   [pleasant]") != std::string::npos);
		CHECK(all.find("before SkyrimCloudy (0010A250) by Weather Mod.esp   transition 42% of the way") != std::string::npos);
		CHECK(all.find("clouds   current layer 0  textures\\sky\\clouds01.dds  speed 3,-2  alpha 0.75") != std::string::npos);
		CHECK(all.find("clouds   previous layer 1  textures\\sky\\clouds02.dds  speed 1,1\n") != std::string::npos);
		CHECK(all.find("image space  DefaultImageSpace (0000ABCD)") != std::string::npos);
		const std::string j = ToJson(p);
		CHECK(JsonCheck(j).Valid());
		CHECK(j.find("\"transition_percent\": 42") != std::string::npos);
		CHECK(j.find("\"live_alpha\": 0.75") != std::string::npos);
		std::printf("---- example console output (sky) ----\n%s--------------------------------------\n", all.c_str());
	}
	// ---- an unknown transition and no clouds
	{
		PickInfo p = LandSample("sky");
		p.sky.present = true;
		p.sky.mode = "interior";
		p.sky.transitionPercent = -1.0f;
		const std::string all = Joined(FormatConsole(p, 60));
		CHECK(all.find("transition unknown") != std::string::npos);
		CHECK(all.find("(no cloud layers in use)") != std::string::npos);
		CHECK(JsonCheck(ToJson(p)).Valid());
	}
	// ---- the hits list
	{
		PickInfo p = LandSample("terrain");
		p.terrain.present = true;
		HitInfo a;
		a.kind = "part";
		a.label = "Plank:0 on Whiterun house";
		a.distance = 120.0f;
		a.refFormId = 0x000A1234;
		a.shapeIndex = 2;
		a.partName = "Plank:0";
		HitInfo b = a;
		b.label = "Wall:1 on Whiterun house";
		b.distance = 150.0f;
		b.exact = false;
		b.shapeIndex = 3;
		b.partName = "Wall:1";
		HitInfo c;
		c.kind = "terrain";
		c.label = "land";
		c.distance = 900.0f;
		p.hits = { a, b, c };
		p.hitIndex = 1;
		const std::string all = Joined(FormatConsole(p, 60));
		CHECK(all.find("hits   3 along the ray, nearest first; this report is #2") != std::string::npos);
		CHECK(all.find("    1    120.0  part    Plank:0 on Whiterun house") != std::string::npos);
		CHECK(all.find("  > 2    150.0  part    Wall:1 on Whiterun house  (by bounds)") != std::string::npos);
		const std::string j = ToJson(p);
		CHECK(JsonCheck(j).Valid());
		CHECK(j.find("\"hit_index\": 1") != std::string::npos);
		CHECK(j.find("\"shape_index\": 3") != std::string::npos);
		CHECK(j.find("\"ref_form_id\": \"000A1234\"") != std::string::npos);
	}
	// ---- the FaceGen "missing" wording keeps its evidence line
	{
		PickInfo p = MakeReportSample();
		p.npc.faceGen.verdict = kFaceGenMissing;
		p.npc.faceGen.evidence = "no FaceGeom mesh found in loose files or archives";
		const std::string all = Joined(FormatConsole(p, 60));
		CHECK(all.find("FaceGen: missing: the game builds the face at runtime (dark-face risk)") != std::string::npos);
		const std::string j = ToJson(p);
		CHECK(JsonCheck(j).Valid());
		CHECK(j.find("no FaceGeom mesh found") != std::string::npos);
	}
	// ---- asked for water or land and there is none: the answer is the message, not an empty object
	{
		PickInfo p = LandSample("object");  // an object-kind report is what the picker leaves when it finds nothing
		p.message = "no water surface under the crosshair";
		const std::string all = Joined(FormatConsole(p, 60, true));
		CHECK(all.find("#9  no water surface under the crosshair") != std::string::npos);
		CHECK(all.find("model  (none)") == std::string::npos);
		CHECK(all.find("ref 00000000") == std::string::npos);
		CHECK(all.find("ray  screen centre") != std::string::npos);
		// with hits behind the empty answer, none of them is marked as this report
		p.hits.push_back({}); p.hits.push_back({});
		p.hitIndex = -1;
		const std::string withHits = Joined(FormatConsole(p, 60, true));
		CHECK(withHits.find("none of them is this answer") != std::string::npos);
		CHECK(withHits.find("this report is #0") == std::string::npos);
	}
}
