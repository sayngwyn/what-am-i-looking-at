// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
#include "Sample.h"

namespace ti::fo4
{
	namespace
	{
		TextureSlot Slot(int a_slot, const char* a_label, const char* a_path, int a_w, int a_h, int a_mips, int a_dxgi, const char* a_format, std::uint64_t a_bytes, const char* a_source, const char* a_archive, const char* a_mod)
		{
			TextureSlot t;
			t.slot = a_slot;
			t.label = a_label;
			t.path = a_path;
			t.loadedAs = a_path;
			t.bound = true;
			t.gpu.present = true;
			t.gpu.width = a_w;
			t.gpu.height = a_h;
			t.gpu.mips = a_mips;
			t.gpu.dxgi = a_dxgi;
			t.gpu.format = a_format;
			t.gpu.bytes = a_bytes;
			t.origin.looked = true;
			t.origin.source = a_source;
			t.origin.archive = a_archive;
			t.origin.mod = a_mod;
			return t;
		}
	}

	PickInfo MakeSampleReport()
	{
		PickInfo p;
		p.counter = 0;
		p.timestamp = "sample";
		p.trigger = "sample";
		p.plugin = "What Am I Looking At (Fallout 4 sample report)";
		p.valid = true;
		p.method = "exact";
		p.clicked = 0;
		p.rayCheck = "sample";

		p.object.name = "SAMPLE: not a real pick";
		p.object.refFormId = 0x0001F2A3;
		p.object.baseFormId = 0x0001F2A4;
		p.object.baseType = "STAT";
		p.object.baseEditorId = "SampleReportStatic";
		p.object.refEditorId = "";
		p.object.model = "Meshes\\Sample\\SampleReport.nif";
		p.object.rootNode = "SampleReport:0";

		p.identity.kind = "object";
		p.identity.displayName = p.object.name;
		p.identity.ref = { p.object.refFormId, "REFR", "", "Fallout4.esm", "SamplePlugin.esp", { "Fallout4.esm", "SamplePlugin.esp" } };
		p.identity.base = { p.object.baseFormId, "STAT", p.object.baseEditorId, "Fallout4.esm", "Fallout4.esm", { "Fallout4.esm" } };
		p.identity.cellName = "Sample Cell";
		p.identity.cellEditorId = "SampleCell";
		p.identity.cellFormId = 0x00000DEF;
		p.identity.interior = false;
		p.identity.worldspaceName = "Sample Worldspace";
		p.identity.worldspaceEditorId = "SampleWorld";
		p.identity.hasCoords = true;
		p.identity.cellX = -3;
		p.identity.cellY = 4;
		p.identity.position = { -11890.0f, 17250.0f, 410.0f };
		p.identity.rotationDegrees = { 0.0f, 0.0f, 90.0f };
		p.identity.editorIdSource = "sample";
		p.identity.modelOrigin.looked = true;
		p.identity.modelOrigin.source = "archive";
		p.identity.modelOrigin.archive = "Fallout4 - Meshes.ba2";

		p.facts.push_back({ "Sample", "A fixed sample for checking that the card draws (ShowSampleCard=1 in the ini). It is not a real pick: use `lookingat` for that." });

		ShapeInfo s;
		s.index = 0;
		s.name = "SampleReport:0";
		s.nodeType = "BSTriShape";
		s.shaderKind = "BSLightingShaderProperty";
		s.shaderType = "Default";
		s.vertexFlags = { "UV", "Normals", "Tangents" };
		s.vertices = 1824;
		s.triangles = 2310;
		s.hasMaterial = true;
		s.glossiness = 80.0f;
		s.specularStrength = 1.0f;
		s.exactTested = true;
		s.exactHit = true;
		s.exactDistance = 312.0f;
		s.clicked = true;
		s.rank = 0;
		s.textures.push_back(Slot(0, "diffuse", "Textures\\Sample\\SampleReport_d.dds", 2048, 2048, 12, 71, "BC1", 2796202, "archive", "Fallout4 - Textures1.ba2", ""));
		s.textures.push_back(Slot(1, "normal", "Textures\\Sample\\SampleReport_n.dds", 2048, 2048, 12, 83, "BC5", 5592405, "loose", "", "Sample Texture Mod"));
		s.textures.push_back(Slot(2, "specular", "Textures\\Sample\\SampleReport_s.dds", 1024, 1024, 11, 83, "BC5", 1398101, "archive", "Fallout4 - Textures2.ba2", ""));
		p.shapes.push_back(s);

		p.diagnostics.push_back({ "sample", "SampleReport:0: this is a sample warning line, to show where warnings appear" });

		HitInfo hit;
		hit.kind = "part";
		hit.label = "SampleReport:0";
		hit.distance = 312.0f;
		hit.exact = true;
		hit.refFormId = p.object.refFormId;
		hit.shapeIndex = 0;
		hit.partName = s.name;
		p.hits.push_back(hit);
		p.hitIndex = 0;
		p.fromRay = false;
		return p;
	}
}
