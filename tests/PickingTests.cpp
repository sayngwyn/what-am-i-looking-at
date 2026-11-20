// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// Offline tests for the picking maths. No framework: each CHECK prints on failure and the exit code is the failure count.
#include "Output.h"
#include "Pick.h"
#include "Terrain.h"

#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include <cstdio>

using namespace ti;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                                 \
	do {                                                                            \
		++g_checks;                                                                 \
		if (!(cond)) {                                                              \
			++g_failures;                                                           \
			std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
		}                                                                           \
	} while (0)

static bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
static bool NearV(Vec3 a, Vec3 b, float eps = 1e-3f) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps); }

/** A perspective camera with the same shape as NiCamera::worldToCam (clip = M * p, w = depth along forward). */
static Mat4 MakeCamera(Vec3 eye, Vec3 forward, Vec3 up, float tanHalfFovY, float aspect, float nearZ, float farZ)
{
	const Vec3 f = Normalised(forward);
	const Vec3 r = Normalised(Cross(f, up));
	const Vec3 u = Cross(r, f);
	const float sx = 1.0f / (tanHalfFovY * aspect);
	const float sy = 1.0f / tanHalfFovY;
	const float a = farZ / (farZ - nearZ);
	const float b = -nearZ * farZ / (farZ - nearZ);
	Mat4        m;
	// x_clip = sx * (r.(p-eye)), y_clip = sy * (u.(p-eye)), z_clip = a * f.(p-eye) + b, w = f.(p-eye)
	m.m[0][0] = sx * r.x; m.m[0][1] = sx * r.y; m.m[0][2] = sx * r.z; m.m[0][3] = -sx * Dot(r, eye);
	m.m[1][0] = sy * u.x; m.m[1][1] = sy * u.y; m.m[1][2] = sy * u.z; m.m[1][3] = -sy * Dot(u, eye);
	m.m[2][0] = a * f.x;  m.m[2][1] = a * f.y;  m.m[2][2] = a * f.z;  m.m[2][3] = -a * Dot(f, eye) + b;
	m.m[3][0] = f.x;      m.m[3][1] = f.y;      m.m[3][2] = f.z;      m.m[3][3] = -Dot(f, eye);
	return m;
}

static void TestRayTriangle()
{
	const Vec3 a{ 0, 0, 0 }, b{ 10, 0, 0 }, c{ 0, 10, 0 };  // in the z = 0 plane
	float      t = 0, u = 0, v = 0;

	// straight down onto the interior
	CHECK(RayTriangle({ { 2, 2, 5 }, { 0, 0, -1 } }, a, b, c, t, u, v));
	CHECK(Near(t, 5.0f));
	CHECK(Near(u, 0.2f) && Near(v, 0.2f));

	// from underneath: two-sided
	CHECK(RayTriangle({ { 2, 2, -5 }, { 0, 0, 1 } }, a, b, c, t, u, v));
	CHECK(Near(t, 5.0f));

	// outside the triangle (beyond the hypotenuse)
	CHECK(!RayTriangle({ { 8, 8, 5 }, { 0, 0, -1 } }, a, b, c, t, u, v));
	// pointing away: the plane is behind the origin
	CHECK(!RayTriangle({ { 2, 2, 5 }, { 0, 0, 1 } }, a, b, c, t, u, v));
	// parallel to the plane
	CHECK(!RayTriangle({ { 2, 2, 5 }, { 1, 0, 0 } }, a, b, c, t, u, v));
	// degenerate triangle
	CHECK(!RayTriangle({ { 0, 0, 5 }, { 0, 0, -1 } }, a, a, a, t, u, v));
	// oblique ray hits the right distance
	{
		const Vec3 d = Normalised({ 1, 0, -1 });
		CHECK(RayTriangle({ { 1, 3, 4 }, d }, a, b, c, t, u, v));
		const Vec3 hit = Vec3{ 1, 3, 4 } + d * t;
		CHECK(Near(hit.z, 0.0f) && Near(hit.x, 5.0f) && Near(hit.y, 3.0f));
	}
	// edge shared by two triangles is hit by at least one of them (no cracks)
	{
		const Vec3 d{ 10, 10, 0 };
		const Ray  ray{ { 5, 5, 3 }, { 0, 0, -1 } };  // on the hypotenuse of (a,b,c)
		const bool h1 = RayTriangle(ray, a, b, c, t, u, v);
		const bool h2 = RayTriangle(ray, b, d, c, t, u, v);
		CHECK(h1 || h2);
	}
}

static void TestRaySphere()
{
	float t = 0;
	const Vec3 centre{ 0, 100, 0 };
	// head on: enters at distance 100 - radius
	CHECK(RaySphere({ { 0, 0, 0 }, { 0, 1, 0 } }, centre, 20.0f, t));
	CHECK(Near(t, 80.0f));
	// grazing miss
	CHECK(!RaySphere({ { 0, 0, 0 }, { 0, 1, 0 } }, { 25, 100, 0 }, 20.0f, t));
	// pointing away
	CHECK(!RaySphere({ { 0, 0, 0 }, { 0, -1, 0 } }, centre, 20.0f, t));
	// origin inside: enter distance 0
	CHECK(RaySphere({ { 0, 100, 5 }, { 0, 1, 0 } }, centre, 20.0f, t));
	CHECK(Near(t, 0.0f));
	// zero radius never hits
	CHECK(!RaySphere({ { 0, 0, 0 }, { 0, 1, 0 } }, centre, 0.0f, t));
}

static void TestScreenToRay()
{
	const Vec3 eye{ 1000, -2000, 300 };
	const Mat4 cam = MakeCamera(eye, { 0.3f, 1.0f, -0.1f }, { 0, 0, 1 }, std::tan(0.5f * 1.2f), 16.0f / 9.0f, 15.0f, 353840.0f);

	// the centre of the screen looks straight along the camera forward
	Ray ray;
	CHECK(NdcToRay(cam, 0.0f, 0.0f, ray));
	CHECK(NearV(ray.origin, eye, 0.05f));
	CHECK(NearV(ray.dir, Normalised({ 0.3f, 1.0f, -0.1f }), 1e-4f));

	// round trip: a ray through any NDC point projects back onto that point, in front of the camera
	const float pts[][2] = { { -1, -1 }, { 1, 1 }, { -0.5f, 0.25f }, { 0.9f, -0.7f }, { 0.0f, 0.8f } };
	for (const auto& p : pts) {
		Ray r;
		CHECK(NdcToRay(cam, p[0], p[1], r));
		CHECK(NearV(r.origin, eye, 0.05f));
		for (float dist : { 50.0f, 1000.0f, 20000.0f }) {
			float nx = 0, ny = 0;
			CHECK(ProjectToNdc(cam, r.origin + r.dir * dist, nx, ny));
			CHECK(Near(nx, p[0], 1e-3f) && Near(ny, p[1], 1e-3f));
		}
	}

	// a known world point maps to the ray that passes through it
	{
		const Vec3 target{ 1400, -1500, 250 };
		float      nx = 0, ny = 0;
		CHECK(ProjectToNdc(cam, target, nx, ny));
		Ray r;
		CHECK(NdcToRay(cam, nx, ny, r));
		const Vec3  toTarget = target - r.origin;
		const float along = Dot(toTarget, r.dir);
		CHECK(NearV(r.origin + r.dir * along, target, 0.05f));
	}

	// behind the camera does not project
	{
		float nx = 0, ny = 0;
		CHECK(!ProjectToNdc(cam, eye - Normalised({ 0.3f, 1.0f, -0.1f }) * 50.0f, nx, ny));
	}

	// the cursor -> NDC step: top-left of the window is (-1, +1) with y up
	{
		float nx = 0, ny = 0;
		CursorToNdc(0.0f, 0.0f, true, nx, ny);
		CHECK(Near(nx, -1.0f) && Near(ny, 1.0f));
		CursorToNdc(1.0f, 1.0f, true, nx, ny);
		CHECK(Near(nx, 1.0f) && Near(ny, -1.0f));
		CursorToNdc(0.5f, 0.5f, true, nx, ny);
		CHECK(Near(nx, 0.0f) && Near(ny, 0.0f));
		CursorToNdc(0.25f, 0.25f, false, nx, ny);
		CHECK(Near(nx, -0.5f) && Near(ny, -0.5f));
	}

	// a transposed matrix (wrong convention) is detectable: the round trip fails
	{
		const Mat4 wrong = cam.Transposed();
		Ray        r;
		float      nx = 0, ny = 0;
		bool       roundTrips = false;
		if (NdcToRay(wrong, 0.4f, 0.3f, r) && ProjectToNdc(cam, r.origin + r.dir * 500.0f, nx, ny))
			roundTrips = Near(nx, 0.4f, 1e-2f) && Near(ny, 0.3f, 1e-2f);
		CHECK(!roundTrips);
	}

	// a degenerate matrix is refused
	{
		Mat4 zero;
		Ray  r;
		CHECK(!NdcToRay(zero, 0.0f, 0.0f, r));
	}
}

static void TestHalf()
{
	CHECK(HalfToFloat(0x0000) == 0.0f);
	CHECK(Near(HalfToFloat(0x3C00), 1.0f, 1e-6f));
	CHECK(Near(HalfToFloat(0xC000), -2.0f, 1e-6f));
	CHECK(Near(HalfToFloat(0x3800), 0.5f, 1e-6f));
	CHECK(Near(HalfToFloat(0x4900), 10.0f, 1e-6f));
	CHECK(std::isinf(HalfToFloat(0x7C00)));
	CHECK(std::isnan(HalfToFloat(0x7E00)));
	CHECK(Near(HalfToFloat(0x0001), 5.9604645e-8f, 1e-12f));  // smallest subnormal
}

static void TestChoose()
{
	// exact winner is the nearest triangle hit, runners-up follow in distance order
	{
		std::vector<Candidate> c(4);
		c[0] = { true, true, 300.0f, true, 280.0f, 50.0f };
		c[1] = { true, true, 120.0f, true, 100.0f, 30.0f };
		c[2] = { true, false, 0.0f, true, 90.0f, 400.0f };  // bound touched, triangles missed
		c[3] = { true, true, 200.0f, true, 190.0f, 20.0f };
		const Choice r = Choose(c);
		CHECK(r.index == 1 && r.exact);
		CHECK(r.runnersUp.size() == 2 && r.runnersUp[0] == 3 && r.runnersUp[1] == 0);  // shape 2 was tested and missed: not listed
	}
	// an untestable shape whose bound is nearer makes the answer approximate
	{
		std::vector<Candidate> c(2);
		c[0] = { true, true, 300.0f, true, 280.0f, 50.0f };
		c[1] = { false, false, 0.0f, true, 100.0f, 60.0f };
		const Choice r = Choose(c);
		CHECK(r.index == 0 && !r.exact);
	}
	// an untestable shape that is farther does not spoil exactness
	{
		std::vector<Candidate> c(2);
		c[0] = { true, true, 100.0f, true, 90.0f, 50.0f };
		c[1] = { false, false, 0.0f, true, 500.0f, 60.0f };
		const Choice r = Choose(c);
		CHECK(r.index == 0 && r.exact);
	}
	// nothing testable: nearest bound entry, then smaller bound on ties; approximate
	{
		std::vector<Candidate> c(3);
		c[0] = { false, false, 0.0f, true, 0.0f, 500.0f };  // ray starts inside a big bound
		c[1] = { false, false, 0.0f, true, 0.0f, 40.0f };   // ... and a small one
		c[2] = { false, false, 0.0f, false, 0.0f, 10.0f };
		const Choice r = Choose(c);
		CHECK(r.index == 1 && !r.exact);
		CHECK(r.runnersUp.size() == 1 && r.runnersUp[0] == 0);
	}
	// everything tested and missed: still names the nearest bound, approximate
	{
		std::vector<Candidate> c(2);
		c[0] = { true, false, 0.0f, true, 250.0f, 10.0f };
		c[1] = { true, false, 0.0f, true, 120.0f, 10.0f };
		const Choice r = Choose(c);
		CHECK(r.index == 1 && !r.exact);
	}
	// no shapes / no hit at all
	{
		CHECK(Choose({}).index == -1);
		std::vector<Candidate> c(1);
		CHECK(Choose(c).index == -1);
	}
}

#include "OutputTests.inc"
#include "LandOutputTests.inc"

static void TestNearest()
{
	const Ray                 r{ { 0, 0, 0 }, { 0, 1, 0 } };
	std::vector<SphereBound>  s;
	s.push_back({ { 100, 500, 0 }, 10.0f });   // 0: 90 units off the ray, far ahead
	s.push_back({ { 40, 300, 0 }, 10.0f });    // 1: 30 units off the ray
	s.push_back({ { 0, -500, 0 }, 10.0f });    // 2: directly behind the camera
	float gap = -1.0f;
	CHECK(NearestToRay(r, s, &gap) == 1);
	CHECK(Near(gap, 30.0f, 1e-3f));

	// a ray that passes through a sphere has gap 0 and wins
	s.push_back({ { 2, 800, 0 }, 20.0f });
	CHECK(NearestToRay(r, s, &gap) == 3);
	CHECK(Near(gap, 0.0f, 1e-3f));

	// only parts behind the camera: none chosen
	std::vector<SphereBound> behind{ { { 0, -500, 0 }, 10.0f } };
	CHECK(NearestToRay(r, behind) == -1);
	CHECK(NearestToRay(r, behind, &gap, true) == 0);  // asked to allow it: the only part is named
	CHECK(NearestToRay(r, {}) == -1);

	// equal gaps: the nearer one along the ray wins
	std::vector<SphereBound> tie{ { { 0, 900, 0 }, 10.0f }, { { 0, 200, 0 }, 10.0f } };
	CHECK(NearestToRay(r, tie) == 1);
}


static void TestTerrain()
{
	// a quadrant that slopes: height = 100 + 0.5 * east distance + 0.25 * north distance, so every triangle is planar
	std::vector<float> h(kQuadVerts * kQuadVerts);
	for (int j = 0; j < kQuadVerts; ++j)
		for (int i = 0; i < kQuadVerts; ++i)
			h[static_cast<std::size_t>(j * kQuadVerts + i)] = 100.0f + 0.5f * static_cast<float>(i) * kVertexSpacing + 0.25f * static_cast<float>(j) * kVertexSpacing;
	QuadGrid grid;
	grid.originX = 4096.0f;
	grid.originY = -4096.0f;
	grid.heights = h.data();

	// a ray straight down meets the surface where the plane says
	{
		const GridHit g = RayQuad({ { 4096.0f + 300.0f, -4096.0f + 500.0f, 5000.0f }, { 0, 0, -1 } }, grid);
		CHECK(g.hit);
		CHECK(Near(g.point.z, 100.0f + 0.5f * 300.0f + 0.25f * 500.0f, 0.05f));
		CHECK(g.i == 2 && g.j == 3);
		CHECK(Near(QuadHeightAt(grid, 4096.0f + 300.0f, -4096.0f + 500.0f), g.point.z, 0.05f));
	}
	// a slanted ray agrees with the height under its hit point
	{
		const Ray ray{ { 4096.0f + 100.0f, -4096.0f + 100.0f, 900.0f }, Normalised({ 0.6f, 0.5f, -0.4f }) };
		const GridHit g = RayQuad(ray, grid);
		CHECK(g.hit);
		CHECK(Near(QuadHeightAt(grid, g.point.x, g.point.y), g.point.z, 0.1f));
	}
	// stored heights that sit 84 below the world's: the offset moves the surface, the hit and the box quick-reject with it
	{
		QuadGrid shifted = grid;
		shifted.zOffset = -84.0f;
		const GridHit g = RayQuad({ { 4096.0f + 300.0f, -4096.0f + 500.0f, 5000.0f }, { 0, 0, -1 } }, shifted);
		CHECK(g.hit);
		CHECK(Near(g.point.z, 100.0f + 0.5f * 300.0f + 0.25f * 500.0f - 84.0f, 0.05f));
		CHECK(Near(QuadHeightAt(shifted, 4096.0f + 300.0f, -4096.0f + 500.0f), g.point.z, 0.05f));
		shifted.zOffset = 3000.0f;  // a ray at z 2000 is below the raised surface: from above it misses, from below it hits
		CHECK(!RayQuad({ { 4096.0f + 300.0f, -4096.0f + 500.0f, 5000.0f }, { 0, 0, 1 } }, shifted).hit);
		CHECK(RayQuad({ { 4096.0f + 300.0f, -4096.0f + 500.0f, 2000.0f }, { 0, 0, 1 } }, shifted).hit);
	}
	// a ray beside the quadrant, a ray pointing up from above and a ray without heights miss
	CHECK(!RayQuad({ { 0, 0, 5000 }, { 0, 0, -1 } }, grid).hit);
	CHECK(!RayQuad({ { 4096.0f + 300.0f, -4096.0f + 500.0f, 5000.0f }, { 0, 0, 1 } }, grid).hit);
	CHECK(!RayQuad({ { 4096.0f, -4096.0f, 5000.0f }, { 0, 0, -1 } }, QuadGrid{}).hit);
	// a ray from below the land is a hit too (two-sided)
	CHECK(RayQuad({ { 4096.0f + 300.0f, -4096.0f + 500.0f, -5000.0f }, { 0, 0, 1 } }, grid).hit);
	// outside the quadrant the height is NaN
	CHECK(std::isnan(QuadHeightAt(grid, 0.0f, 0.0f)));

	// height on a bumpy grid: the diagonal both sides use
	{
		std::vector<float> b(kQuadVerts * kQuadVerts, 0.0f);
		b[static_cast<std::size_t>(1 * kQuadVerts + 1)] = 128.0f;  // vertex (1,1) is a peak
		QuadGrid bg{ 0.0f, 0.0f, b.data() };
		// the centre of square (0,0) lies on its diagonal (0,0)-(1,1): the height is the diagonal's midpoint
		CHECK(Near(QuadHeightAt(bg, 64.0f, 64.0f), 64.0f));
		const GridHit g = RayQuad({ { 64.0f, 64.0f, 1000.0f }, { 0, 0, -1 } }, bg);
		CHECK(g.hit && Near(g.point.z, 64.0f, 0.1f));
		// the peak itself
		CHECK(Near(QuadHeightAt(bg, 128.0f, 128.0f), 128.0f));
	}

	// quadrants: 0 south-west, 1 south-east, 2 north-west, 3 north-east; points on cell (1, -1)
	CHECK(QuadrantOf(1, -1, 4096.0f + 10.0f, -4096.0f + 10.0f) == 0);
	CHECK(QuadrantOf(1, -1, 4096.0f + 3000.0f, -4096.0f + 10.0f) == 1);
	CHECK(QuadrantOf(1, -1, 4096.0f + 10.0f, -4096.0f + 3000.0f) == 2);
	CHECK(QuadrantOf(1, -1, 4096.0f + 3000.0f, -4096.0f + 3000.0f) == 3);
	CHECK(QuadrantOf(1, -1, 0.0f, 0.0f) == -1);
	CHECK(std::string(QuadrantName(3)) == "north-east" && std::string(QuadrantName(0)) == "south-west");
	{
		float x = 0, y = 0;
		QuadrantOrigin(1, -1, 3, x, y);
		CHECK(Near(x, 4096.0f + 2048.0f) && Near(y, -4096.0f + 2048.0f));
		QuadrantOrigin(-1, 0, 1, x, y);
		CHECK(Near(x, -4096.0f + 2048.0f) && Near(y, 0.0f));
	}

	// bilinear weights sum to 1 and the nearest vertex rounds to the closest sample
	{
		const Bilinear b = BilinearAt(grid, 4096.0f + 200.0f, -4096.0f + 70.0f);
		CHECK(b.i == 1 && b.j == 0);
		CHECK(Near(b.Weight(0, 0) + b.Weight(1, 0) + b.Weight(0, 1) + b.Weight(1, 1), 1.0f, 1e-5f));
		CHECK(Near(b.fx, 72.0f / 128.0f) && Near(b.fy, 70.0f / 128.0f));
		const float raw[4] = { 0.0f, 127.0f, 0.0f, 127.0f };  // opaque on the east side only
		CHECK(Near(BlendRaw(b, raw), 127.0f * b.fx, 1e-3f));
		// the far corner clamps instead of running off the grid
		const Bilinear e = BilinearAt(grid, 4096.0f + 5000.0f, -4096.0f + 5000.0f);
		CHECK(e.i == kQuadVerts - 2 && e.j == kQuadVerts - 2 && Near(e.fx, 1.0f) && Near(e.fy, 1.0f));
		int i = 0, j = 0;
		NearestVertex(grid, 4096.0f + 200.0f, -4096.0f + 70.0f, i, j);
		CHECK(i == 2 && j == 1);
		NearestVertex(grid, 4096.0f - 500.0f, -4096.0f + 99999.0f, i, j);
		CHECK(i == 0 && j == kQuadVerts - 1);
	}
	// percent: the stored byte as a share of full, clamped
	CHECK(Near(LayerPercent(127.0f), 100.0f) && Near(LayerPercent(0.0f), 0.0f) && Near(LayerPercent(-5.0f), 0.0f) && Near(LayerPercent(500.0f), 100.0f));
	CHECK(Near(LayerPercent(63.5f), 50.0f, 0.01f));
}

int main()
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	TestRayTriangle();
	TestRaySphere();
	TestScreenToRay();
	TestHalf();
	TestChoose();
	try {
		TestOutput();
	} catch (const std::exception& e) {
		std::printf("EXCEPTION in TestOutput: %s\n", e.what());
		++g_failures;
	}
	try {
		TestMaterialOutput();
	} catch (const std::exception& e) {
		std::printf("EXCEPTION in TestMaterialOutput: %s\n", e.what());
		++g_failures;
	}
	try {
		TestReportOutput();
	} catch (const std::exception& e) {
		std::printf("EXCEPTION in TestReportOutput: %s\n", e.what());
		++g_failures;
	}
	try {
		TestLandOutput();
	} catch (const std::exception& e) {
		std::printf("EXCEPTION in TestLandOutput: %s\n", e.what());
		++g_failures;
	}
	TestNearest();
	TestTerrain();
	std::printf("%d checks, %d failed\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
