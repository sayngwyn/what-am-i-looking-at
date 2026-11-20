// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// Offline tests for the game-free picking and report-fill logic the Fallout 4 plugin uses: the triangle test on copied mesh
// buffers (full and half precision, transforms), how a ray ranks the parts of one object, the hits list kept for stepping, and
// the Fallout 4 names (material file path, shader flags, vertex attributes, texture slots). No framework: each CHECK prints on
// failure and the exit code is the failure count.
#include "Fo4Names.h"
#include "PickData.h"
#include "PickScan.h"

#include <cmath>
#include <cstdio>
#include <cstring>

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

static bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

// ---- a mesh built by hand ---------------------------------------------------------------------------------------------

namespace
{
	/** Vertices as floats with a stride of 20 (position + 8 bytes of other attributes), like a lit vertex with UV and a normal. */
	struct FloatMesh
	{
		std::vector<std::uint8_t>  bytes;
		std::vector<std::uint16_t> indices;
		std::uint32_t              count = 0;
		static constexpr std::uint32_t kStride = 20;

		void Add(float x, float y, float z)
		{
			std::uint8_t v[kStride]{};
			std::memcpy(v, &x, 4);
			std::memcpy(v + 4, &y, 4);
			std::memcpy(v + 8, &z, 4);
			bytes.insert(bytes.end(), v, v + kStride);
			++count;
		}
		void Tri(std::uint16_t a, std::uint16_t b, std::uint16_t c)
		{
			indices.push_back(a);
			indices.push_back(b);
			indices.push_back(c);
		}
		MeshView View() const
		{
			MeshView m;
			m.vertexBytes = bytes.data();
			m.vertexByteCount = bytes.size();
			m.stride = kStride;
			m.vertexCount = count;
			m.fullPrecision = true;
			m.indices = indices.data();
			m.triangleCount = static_cast<std::uint32_t>(indices.size() / 3);
			return m;
		}
	};

	/** The same with half floats and a stride of 16. */
	struct HalfMesh
	{
		std::vector<std::uint8_t>  bytes;
		std::vector<std::uint16_t> indices;
		std::uint32_t              count = 0;
		static constexpr std::uint32_t kStride = 16;

		void Add(std::uint16_t x, std::uint16_t y, std::uint16_t z)
		{
			std::uint8_t v[kStride]{};
			std::memcpy(v, &x, 2);
			std::memcpy(v + 2, &y, 2);
			std::memcpy(v + 4, &z, 2);
			bytes.insert(bytes.end(), v, v + kStride);
			++count;
		}
		MeshView View() const
		{
			MeshView m;
			m.vertexBytes = bytes.data();
			m.vertexByteCount = bytes.size();
			m.stride = kStride;
			m.vertexCount = count;
			m.fullPrecision = false;
			m.indices = indices.data();
			m.triangleCount = static_cast<std::uint32_t>(indices.size() / 3);
			return m;
		}
	};

	Ray ForwardRay(float x = 0, float y = 0, float z = 0) { return { { x, y, z }, { 1, 0, 0 } }; }

	/** A square wall at x = a_x, facing the ray, covering y,z in -a_half..a_half. */
	FloatMesh Wall(float a_x, float a_half = 5.0f)
	{
		FloatMesh m;
		m.Add(a_x, -a_half, -a_half);
		m.Add(a_x, a_half, -a_half);
		m.Add(a_x, a_half, a_half);
		m.Add(a_x, -a_half, a_half);
		m.Tri(0, 1, 2);
		m.Tri(0, 2, 3);
		return m;
	}
}

static void TestMeshHits()
{
	// a wall 100 units ahead: hit at t = 100, either side of the diagonal
	{
		const auto wall = Wall(100.0f);
		const auto r = TestMesh(wall.View(), ForwardRay());
		CHECK(r.tested && r.hit);
		CHECK(Near(r.t, 100.0f));
		CHECK(Near(r.point.x, 100.0f) && Near(r.point.y, 0.0f));
		CHECK(r.triangle == 0 || r.triangle == 1);
		const auto r2 = TestMesh(wall.View(), { { 0, 3, -2 }, { 1, 0, 0 } });
		CHECK(r2.tested && r2.hit && Near(r2.t, 100.0f));
	}
	// a ray off to the side misses; the mesh was still tested
	{
		const auto wall = Wall(100.0f);
		const auto r = TestMesh(wall.View(), ForwardRay(0, 50, 0));
		CHECK(r.tested && !r.hit && r.triangle == -1);
	}
	// a wall behind the camera is not a hit (two-sided, but forward only)
	{
		const auto wall = Wall(-100.0f);
		const auto r = TestMesh(wall.View(), ForwardRay());
		CHECK(r.tested && !r.hit);
	}
	// back faces count: the same wall wound the other way
	{
		FloatMesh m;
		m.Add(50, -5, -5);
		m.Add(50, 5, 5);
		m.Add(50, 5, -5);
		m.Tri(0, 1, 2);
		const auto r = TestMesh(m.View(), ForwardRay(0, 2, -1));
		CHECK(r.tested && r.hit && Near(r.t, 50.0f));
	}
	// two walls: the nearer one wins whatever the order in the buffer
	{
		FloatMesh m;
		for (const float x : { 200.0f, 80.0f }) {
			const std::uint16_t base = static_cast<std::uint16_t>(m.count);
			m.Add(x, -5, -5);
			m.Add(x, 5, -5);
			m.Add(x, 5, 5);
			m.Tri(base, static_cast<std::uint16_t>(base + 1), static_cast<std::uint16_t>(base + 2));
		}
		const auto r = TestMesh(m.View(), ForwardRay(0, 1, -1));
		CHECK(r.hit && Near(r.t, 80.0f) && r.triangle == 1);
	}
	// an index past the vertex count is skipped, not read
	{
		FloatMesh m = Wall(100.0f);
		m.Tri(0, 1, 40);
		const auto r = TestMesh(m.View(), ForwardRay());
		CHECK(r.tested && r.hit && Near(r.t, 100.0f));
	}
}

static void TestMeshTransform()
{
	// local mesh at x = 10 with the object placed at x + 90 (translate): the hit is at 100
	{
		const auto wall = Wall(10.0f);
		auto       view = wall.View();
		view.translate = { 90, 0, 0 };
		const auto r = TestMesh(view, ForwardRay());
		CHECK(r.hit && Near(r.t, 100.0f));
	}
	// scale: a wall 10 away scaled by 4 is 40 away, and its half-size 5 becomes 20
	{
		const auto wall = Wall(10.0f);
		auto       view = wall.View();
		view.scale = 4.0f;
		CHECK(TestMesh(view, ForwardRay(0, 15, 0)).hit);
		CHECK(!TestMesh(view, ForwardRay(0, 25, 0)).hit);
		CHECK(Near(TestMesh(view, ForwardRay()).t, 40.0f));
	}
	// rotation of 90 degrees about z (row-major): local +x goes to world +y, so a wall at local x = 10 stands at y = 10
	{
		const auto wall = Wall(10.0f);
		auto       view = wall.View();
		const float rot[9] = { 0, -1, 0, 1, 0, 0, 0, 0, 1 };
		std::memcpy(view.rotate, rot, sizeof(rot));
		const Ray up{ { 0, 0, 0 }, { 0, 1, 0 } };
		const auto r = TestMesh(view, up);
		CHECK(r.hit && Near(r.t, 10.0f));
		CHECK(!TestMesh(view, ForwardRay()).hit);  // along +x it is now seen edge-on
	}
	// world position of a vertex
	{
		const auto wall = Wall(10.0f);
		auto       view = wall.View();
		view.scale = 2.0f;
		view.translate = { 1, 2, 3 };
		const Vec3 p = MeshWorldPosition(view, 2);  // (10, 5, 5) local
		CHECK(Near(p.x, 21) && Near(p.y, 12) && Near(p.z, 13));
	}
}

static void TestHalfPrecision()
{
	// 0x3C00 = 1.0, 0x4900 = 10.0, 0xC900 = -10.0, 0x4500 = 5.0, 0xC500 = -5.0
	HalfMesh m;
	m.Add(0x4900, 0xC500, 0xC500);
	m.Add(0x4900, 0x4500, 0xC500);
	m.Add(0x4900, 0x4500, 0x4500);
	m.indices = { 0, 1, 2 };
	auto view = m.View();
	const auto r = TestMesh(view, ForwardRay(0, 2, -1));
	CHECK(r.tested && r.hit && Near(r.t, 10.0f));
	CHECK(Near(MeshWorldPosition(view, 0).x, 10.0f) && Near(MeshWorldPosition(view, 0).y, -5.0f));
	view.fullPrecision = true;  // read as floats the same bytes are nonsense: the stride is the only guard, so the flag matters
	view.stride = 16;
	CHECK(!Near(MeshWorldPosition(view, 0).x, 10.0f));
}

static void TestMeshRefusals()
{
	const auto wall = Wall(100.0f);
	{
		auto v = wall.View();
		v.vertexBytes = nullptr;
		const auto r = TestMesh(v, ForwardRay());
		CHECK(!r.tested && r.reason == "no CPU copy of the mesh");
	}
	{
		auto v = wall.View();
		v.indices = nullptr;
		CHECK(!TestMesh(v, ForwardRay()).tested);
	}
	{
		auto v = wall.View();
		v.vertexCount = 0;
		CHECK(!TestMesh(v, ForwardRay()).tested);
	}
	{
		auto v = wall.View();
		v.triangleCount = 0;
		CHECK(!TestMesh(v, ForwardRay()).tested);
	}
	{
		auto v = wall.View();
		v.stride = 8;  // cannot hold three floats
		const auto r = TestMesh(v, ForwardRay());
		CHECK(!r.tested && !r.reason.empty());
	}
	{
		auto v = wall.View();
		v.vertexCount = 400;  // claims more vertices than the buffer holds: never read past the end
		const auto r = TestMesh(v, ForwardRay());
		CHECK(!r.tested && r.reason == "vertex data shorter than its count");
	}
	{
		auto v = wall.View();
		v.fullPrecision = false;
		v.stride = 4;  // too small even for half floats
		CHECK(!TestMesh(v, ForwardRay()).tested);
	}
}

// ---- ranking the parts of one object ----------------------------------------------------------------------------------

static ShapeInfo Part(int a_index, const char* a_name)
{
	ShapeInfo s;
	s.index = a_index;
	s.name = a_name;
	return s;
}

static ShapeInfo ExactPart(int a_index, const char* a_name, float a_t)
{
	auto s = Part(a_index, a_name);
	s.boundHit = true;
	s.boundDistance = a_t - 1;
	s.boundRadius = 3;
	s.boundCentre = { a_t, 0, 0 };
	s.exactTested = true;
	s.exactHit = true;
	s.exactDistance = a_t;
	s.hitPoint = { a_t, 0, 0 };
	return s;
}

static ShapeInfo UntestedPart(int a_index, const char* a_name, float a_enter, float a_centre, const char* a_reason = "skinned (bind pose is not the world pose)")
{
	auto s = Part(a_index, a_name);
	s.boundHit = true;
	s.boundDistance = a_enter;
	s.boundRadius = 4;
	s.boundCentre = { a_centre, 0, 0 };
	s.exactSkipReason = a_reason;
	return s;
}

static void TestPartAlongRay()
{
	float score = 0;
	bool  exact = false;
	const Ray ray = ForwardRay();
	{
		auto s = ExactPart(0, "a", 30);
		CHECK(PartAlongRay(s, ray, score, exact) && exact && Near(score, 30));
	}
	{
		auto s = UntestedPart(0, "tree", 20, 25);
		CHECK(PartAlongRay(s, ray, score, exact) && !exact && Near(score, 25));  // where the bound's centre projects onto the ray (the later of the two, so a big bound the camera sits in is not 'at the camera')
	}
	{
		auto s = UntestedPart(0, "inside", 0, 3);  // the camera sits inside a large bound
		CHECK(PartAlongRay(s, ray, score, exact) && !exact && Near(score, 3));
	}
	{
		auto s = UntestedPart(0, "behind", 0, -5);  // centre behind the camera
		CHECK(!PartAlongRay(s, ray, score, exact));
	}
	{
		auto s = ExactPart(0, "missed", 30);
		s.exactHit = false;  // tested and every triangle missed
		CHECK(!PartAlongRay(s, ray, score, exact));
	}
	{
		auto s = UntestedPart(0, "fx", 20, 25, "not a tri shape");
		CHECK(!PartAlongRay(s, ray, score, exact));
		s = UntestedPart(0, "far", 20, 25, "not reached by the ray");
		CHECK(!PartAlongRay(s, ray, score, exact));
		s = Part(0, "nobound");
		CHECK(!PartAlongRay(s, ray, score, exact));
	}
}

static PickInfo ObjectWith(std::vector<ShapeInfo> a_shapes)
{
	PickInfo info;
	info.valid = true;
	info.object.name = "Crate";
	info.object.refFormId = 0x0001F2A3;
	info.shapes = std::move(a_shapes);
	return info;
}

static void TestApplyChoice()
{
	const Ray ray = ForwardRay();
	// nearest exact hit wins; the other touched part is a runner-up
	{
		auto info = ObjectWith({ ExactPart(0, "far", 80), ExactPart(1, "near", 30), Part(2, "off") });
		ApplyChoice(info, ray, false, -1);
		CHECK(info.clicked == 1 && info.method == "exact");
		CHECK(info.shapes[1].clicked && info.shapes[1].rank == 0);
		CHECK(info.runnersUp.size() == 1 && info.runnersUp[0] == 0 && info.shapes[0].rank == 1);
		CHECK(info.shapes[2].rank == -1 && !info.shapes[2].clicked);
	}
	// an untestable part in front makes the answer approximate
	{
		auto info = ObjectWith({ ExactPart(0, "post", 80), UntestedPart(1, "rope", 20, 25) });
		ApplyChoice(info, ray, false, -1);
		CHECK(info.clicked == 0 && info.method == "approximate");
	}
	// only untestable parts: ranked by where the ray enters their bound
	{
		auto info = ObjectWith({ UntestedPart(0, "a", 40, 45), UntestedPart(1, "b", 10, 15) });
		ApplyChoice(info, ray, false, -1);
		CHECK(info.clicked == 1 && info.method == "approximate");
	}
	// nothing touched: no choice, and a message
	{
		auto info = ObjectWith({ Part(0, "a"), Part(1, "b") });
		ApplyChoice(info, ray, false, -1);
		CHECK(info.clicked == -1 && info.method == "none" && !info.message.empty());
	}
	// the object was selected another way: the part nearest the ray is named, with a note
	{
		auto a = Part(0, "left");
		a.boundCentre = { 50, 30, 0 };
		a.boundRadius = 5;
		auto b = Part(1, "right");
		b.boundCentre = { 50, 12, 0 };
		b.boundRadius = 5;
		auto info = ObjectWith({ a, b });
		ApplyChoice(info, ray, true, -1);
		CHECK(info.clicked == 1 && info.method == "nearest_to_center");
		CHECK(info.shapes[1].clicked && info.shapes[1].rank == 0);
		CHECK(info.notes.size() == 1);
	}
	// behind the camera: still named, with a note that says so
	{
		auto a = Part(0, "behind");
		a.boundCentre = { -50, 0, 0 };
		a.boundRadius = 5;
		auto info = ObjectWith({ a });
		ApplyChoice(info, ray, true, -1);
		CHECK(info.clicked == 0 && info.method == "nearest_to_center");
		CHECK(info.notes.size() == 1 && info.notes[0].find("behind the camera") != std::string::npos);
	}
	// no parts at all
	{
		auto info = ObjectWith({});
		ApplyChoice(info, ray, true, -1);
		CHECK(info.clicked == -1 && info.method == "none" && !info.message.empty());
	}
	// stepping: the forced part is reported whatever the ray chose
	{
		auto info = ObjectWith({ ExactPart(0, "far", 80), ExactPart(1, "near", 30) });
		ApplyChoice(info, ray, false, 0);
		CHECK(info.clicked == 0 && info.method == "step_through" && info.message.empty());
		CHECK(info.shapes[0].clicked && info.shapes[0].rank == 0);
		CHECK(!info.shapes[1].clicked && info.shapes[1].rank == -1);
		CHECK(info.runnersUp.empty());
	}
	// a forced index out of range is ignored
	{
		auto info = ObjectWith({ ExactPart(0, "only", 30) });
		ApplyChoice(info, ray, false, 7);
		CHECK(info.clicked == 0 && info.method == "exact");
	}
	// running it twice gives the same answer (a re-rank starts clean)
	{
		auto info = ObjectWith({ ExactPart(0, "far", 80), ExactPart(1, "near", 30) });
		ApplyChoice(info, ray, false, -1);
		ApplyChoice(info, ray, false, -1);
		CHECK(info.clicked == 1 && info.runnersUp.size() == 1 && info.shapes[0].rank == 1);
	}
}

static void TestHits()
{
	const Ray ray = ForwardRay();
	auto      info = ObjectWith({ ExactPart(0, "far", 80), ExactPart(1, "near", 30), Part(2, "off"), UntestedPart(3, "", 20, 26) });
	std::vector<HitInfo> hits;
	AppendPartHits(info, ray, hits);
	CHECK(hits.size() == 3);  // the untouched part is left out
	SortHits(hits);
	CHECK(hits.size() == 3 && hits[0].shapeIndex == 3 && Near(hits[0].distance, 26) && !hits[0].exact);
	CHECK(hits[1].shapeIndex == 1 && hits[1].exact && Near(hits[1].distance, 30));
	CHECK(hits[2].shapeIndex == 0 && Near(hits[2].distance, 80));
	CHECK(hits[1].label == "near on Crate");
	CHECK(hits[0].label == "part 4 on Crate");  // an unnamed part is numbered
	CHECK(hits[1].refFormId == 0x0001F2A3 && hits[1].kind == "part" && hits[1].partName == "near");
	CHECK(Near(hits[1].point.x, 30));
	CHECK(Near(hits[0].point.x, 26));  // by bounds: the point on the ray at the score
	CHECK(IndexOfPartHit(hits, 0x0001F2A3, 0) == 2);
	CHECK(IndexOfPartHit(hits, 0x0001F2A3, 9) == -1);
	CHECK(IndexOfPartHit(hits, 0x00000001, 0) == -1);

	// equal distances keep their order
	std::vector<HitInfo> ties(3);
	ties[0].label = "a";
	ties[1].label = "b";
	ties[2].label = "c";
	for (auto& t : ties)
		t.distance = 5;
	SortHits(ties);
	CHECK(ties[0].label == "a" && ties[1].label == "b" && ties[2].label == "c");
}

// ---- Fallout 4 names -----------------------------------------------------------------------------------------------------

static void TestMaterialNames()
{
	CHECK(Fo4MaterialFile("Materials\\SetDressing\\Crates\\WoodenCrate01Dest.BGSM") == "Materials\\SetDressing\\Crates\\WoodenCrate01Dest.BGSM");
	CHECK(Fo4MaterialFile("  materials\\fx\\spark.bgem \t") == "materials\\fx\\spark.bgem");
	CHECK(Fo4MaterialFile("Materials\\x.BgSm") == "Materials\\x.BgSm");
	CHECK(Fo4MaterialFile("WoodCrate03").empty());
	CHECK(Fo4MaterialFile("textures\\x_d.dds").empty());
	CHECK(Fo4MaterialFile("").empty());
	CHECK(Fo4MaterialFile(".bgsm") == ".bgsm");  // degenerate but a suffix match; the engine never names one that way
	CHECK(Fo4MaterialFile("bgsm").empty());

	// the shader property's name holds the path
	{
		ShapeInfo s;
		ApplyFo4Material(s, "Materials\\SetDressing\\Crates\\WoodenCrate01Dest.BGSM", "");
		CHECK(s.materialPath == "Materials\\SetDressing\\Crates\\WoodenCrate01Dest.BGSM");
		CHECK(s.materialSource == kMaterialFromShaderName);
		CHECK(s.rootMaterial.empty());
	}
	// the root name differs: kept beside it
	{
		ShapeInfo s;
		ApplyFo4Material(s, "Materials\\a.bgsm", "Materials\\b.bgsm");
		CHECK(s.materialPath == "Materials\\a.bgsm" && s.rootMaterial == "Materials\\b.bgsm");
	}
	// the root name is the same material in another case: not repeated
	{
		ShapeInfo s;
		ApplyFo4Material(s, "Materials\\a.bgsm", "materials\\A.BGSM");
		CHECK(s.rootMaterial.empty());
	}
	// no usable name: the root name stands in
	{
		ShapeInfo s;
		ApplyFo4Material(s, "WoodCrate03", "Materials\\b.bgsm");
		CHECK(s.materialPath == "Materials\\b.bgsm" && s.materialSource == kMaterialFromRootName);
	}
	// nothing: say why (no name, or a name that is not a material)
	{
		ShapeInfo s;
		ApplyFo4Material(s, "", "");
		CHECK(s.materialPath.empty() && s.materialSource.find("no name") == 0);
		ApplyFo4Material(s, "Fx_Plane", "");
		CHECK(s.materialPath.empty() && s.materialSource.find("Fx_Plane") != std::string::npos);
	}
	// stale values are cleared on a second call
	{
		ShapeInfo s;
		ApplyFo4Material(s, "Materials\\a.bgsm", "Materials\\b.bgsm");
		ApplyFo4Material(s, "", "");
		CHECK(s.materialPath.empty() && s.rootMaterial.empty());
	}
}

static void TestFlagNames()
{
	auto has = [](const std::vector<std::string>& v, const char* n) {
		for (const auto& s : v)
			if (s == n)
				return true;
		return false;
	};
	CHECK(Fo4ShaderFlagNames(0).empty());
	const auto f = Fo4ShaderFlagNames((1ull << 0) | (1ull << 7) | (1ull << 36) | (1ull << 57) | (1ull << 39) /* TransformChanged: bookkeeping, not listed */);
	CHECK(f.size() == 4);
	CHECK(f.size() == 4 && f[0] == "Specular" && f[1] == "EnvMap" && f[2] == "TwoSided" && f[3] == "AlphaTest");
	CHECK(!has(f, "TransformChanged"));
	CHECK(has(Fo4ShaderFlagNames(1ull << 62), "EffectLighting"));
	CHECK(has(Fo4ShaderFlagNames(1ull << 63), "EffectLighting") == false);  // RefractionWritesDepth is not listed

	// VF_ bits: position 1, UV 2, UV2 4, normal 8, tangent 0x10, colors 0x20, skinned 0x40, land 0x80, eye 0x100, full precision 0x400
	const auto v = Fo4VertexFlagNames(0x1 | 0x2 | 0x8 | 0x10);
	CHECK(v.size() == 3 && v[0] == "UV" && v[1] == "Normals" && v[2] == "Tangents");
	const auto v2 = Fo4VertexFlagNames(0x1 | 0x2 | 0x20 | 0x40 | 0x400);
	CHECK(v2.size() == 4 && v2[1] == "Colors" && v2[2] == "Skinned" && v2[3] == "FullPrecision");
	CHECK(Fo4VertexFlagNames(0x1).empty());
	CHECK(Fo4FullPrecision(0x400 | 0x1) && !Fo4FullPrecision(0x1 | 0x2));

	// a vertex description: low nibble = size in 4-byte words, flags from bit 44
	volatile std::uint64_t desc = (0x40ull | 0x2 | 0x1 | 0x8) << 44 | 0x5;  // five words = 20 bytes
	CHECK(Fo4VertexStride(desc) == 20);
	CHECK(Fo4VertexStride(0) == 0);
	const std::uint16_t flagBits = static_cast<std::uint16_t>(static_cast<std::uint64_t>(desc) >> 44);
	CHECK(flagBits == 0x4B);
	CHECK(Fo4VertexFlagNames(flagBits).size() == 3);  // UV, Normals, Skinned

	CHECK(std::string(Fo4LitSlotLabel(0)) == "diffuse");
	CHECK(std::string(Fo4LitSlotLabel(1)) == "normal");
	CHECK(std::string(Fo4LitSlotLabel(7)) == "smooth-spec");
	CHECK(std::string(Fo4LitSlotLabel(99)) == "slot");
	CHECK(std::string(Fo4EffectSlotLabel(0)) == "source");
	CHECK(std::string(Fo4EffectSlotLabel(4)) == "normal");
}

static void TestWaterPlane()
{
	PickInfo info;
	CHECK(!IsWaterPlane(info));
	info.object.baseType = "ACTI";
	ShapeInfo plane;
	plane.name = "DefaultProceduralWater:0";
	info.shapes.push_back(plane);
	CHECK(IsWaterPlane(info));
	info.object.baseType = "STAT";
	CHECK(!IsWaterPlane(info));
	info.object.baseType = "ACTI";
	info.shapes[0].name = "Lever01";
	CHECK(!IsWaterPlane(info));
}

static PickInfo SelectedHouse(bool a_exact, float a_distance)
{
	PickInfo info;
	info.valid = true;
	info.object.refFormId = 0x2A66E;
	info.object.baseType = "STAT";
	ShapeInfo part;
	part.index = 0;
	part.name = "House01:0";
	if (a_exact) {
		part.exactTested = true;
		part.exactHit = true;
		part.exactDistance = a_distance;
	} else {
		part.boundHit = true;
		part.boundDistance = a_distance;
		part.boundCentre = { a_distance, 0.0f, 0.0f };
	}
	info.shapes.push_back(part);
	info.clicked = 0;
	info.method = a_exact ? "exact" : "approximate";
	return info;
}

static PickInfo RayPickAt(const char* a_kind, float a_distance)
{
	PickInfo pick;
	pick.valid = true;
	pick.identity.kind = a_kind;
	HitInfo h;
	h.kind = a_kind;
	h.distance = a_distance;
	pick.hits.push_back(h);
	pick.hitIndex = std::string(a_kind) == "sky" ? -1 : 0;
	if (std::string(a_kind) == "sky")
		pick.hits.clear();
	return pick;
}

static void TestJudgeSelection()
{
	const Ray ray{ { 0, 0, 0 }, { 1, 0, 0 } };
	// a new selection the ray reaches wins over land behind it and over the sky
	CHECK(JudgeSelection(SelectedHouse(true, 500), ray, RayPickAt("terrain", 800), true, 6000, false, 0).use);
	CHECK(JudgeSelection(SelectedHouse(true, 500), ray, RayPickAt("sky", 0), true, 6000, false, 0).use);
	// an unchanged (stale) selection never wins
	CHECK(!JudgeSelection(SelectedHouse(true, 500), ray, RayPickAt("sky", 0), false, 6000, true, 0).use);
	// something nearer along the ray wins over the selection
	CHECK(!JudgeSelection(SelectedHouse(true, 500), ray, RayPickAt("terrain", 300), true, 6000, false, 0).use);
	// the water plane and an out-of-reach object never win
	{
		auto plane = SelectedHouse(true, 500);
		plane.object.baseType = "ACTI";
		plane.shapes[0].name = "DefaultProceduralWater:0";
		CHECK(!JudgeSelection(plane, ray, RayPickAt("sky", 0), true, 6000, true, 0).use);
	}
	CHECK(!JudgeSelection(SelectedHouse(true, 9000), ray, RayPickAt("sky", 0), true, 6000, true, 0).use);
	// the ray does not reach any part of it
	{
		auto missed = SelectedHouse(true, 500);
		missed.clicked = -1;
		CHECK(!JudgeSelection(missed, ray, RayPickAt("sky", 0), true, 6000, true, 0).use);
	}
	// a bounds-only hit wins only where the console selection is trusted for it
	CHECK(JudgeSelection(SelectedHouse(false, 500), ray, RayPickAt("terrain", 800), true, 6000, true, 0).use);
	CHECK(!JudgeSelection(SelectedHouse(false, 500), ray, RayPickAt("terrain", 800), true, 6000, false, 0).use);
	// the ray hit the same object: the ray's own pick is used
	{
		auto same = RayPickAt("object", 500);
		same.object.refFormId = 0x2A66E;
		CHECK(!JudgeSelection(SelectedHouse(true, 500), ray, same, true, 6000, true, 0).use);
	}
	CHECK(!JudgeSelection(SelectedHouse(true, 500), ray, RayPickAt("sky", 0), true, 6000, true, 0).reason.empty());
}

int main()
{
	TestMeshHits();
	TestMeshTransform();
	TestHalfPrecision();
	TestMeshRefusals();
	TestPartAlongRay();
	TestApplyChoice();
	TestHits();
	TestMaterialNames();
	TestFlagNames();
	TestWaterPlane();
	TestJudgeSelection();
	std::printf("%d check(s), %d failure(s)\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
