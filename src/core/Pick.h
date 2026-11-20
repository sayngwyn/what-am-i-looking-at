// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// Picking maths: no game or CommonLib dependency, so it is unit-tested offline.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace ti
{
	struct Vec3
	{
		float x = 0.0f, y = 0.0f, z = 0.0f;
	};

	inline Vec3  operator+(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
	inline Vec3  operator-(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
	inline Vec3  operator*(Vec3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
	inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	inline Vec3  Cross(Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
	inline float Length(Vec3 a) { return std::sqrt(Dot(a, a)); }
	inline Vec3  Normalised(Vec3 a)
	{
		const float l = Length(a);
		return l > 0.0f ? a * (1.0f / l) : Vec3{};
	}

	struct Ray
	{
		Vec3 origin;
		Vec3 dir;  // unit length
	};

	/** Row-major 4x4, laid out like NiCamera::worldToCam: clip = m * (x, y, z, 1). */
	struct Mat4
	{
		float m[4][4] = {};
		Mat4 Transposed() const
		{
			Mat4 r;
			for (int i = 0; i < 4; ++i)
				for (int j = 0; j < 4; ++j)
					r.m[i][j] = m[j][i];
			return r;
		}
	};

	/**
	 * Moller-Trumbore. Two-sided (a texture inspector must hit back faces of one-sided walls too).
	 * On a hit, t is the distance along the (unit) ray, u/v the barycentric weights of b and c.
	 */
#ifdef _MSC_VER
#	pragma warning(push)
#	pragma warning(disable : 4723)  /* false positive: inlined degenerate-triangle path, guarded by the epsilon test */
#endif
	inline bool RayTriangle(const Ray& r, Vec3 a, Vec3 b, Vec3 c, float& t, float& u, float& v)
	{
		constexpr float kEpsilon = 1e-9f;
		const Vec3      e1 = b - a;
		const Vec3      e2 = c - a;
		const Vec3      p = Cross(r.dir, e2);
		const float     det = Dot(e1, p);
		const float inv = std::fabs(det) < kEpsilon ? 0.0f : 1.0f / det;
		if (inv == 0.0f)
			return false;  // parallel or degenerate
		const Vec3  s = r.origin - a;
		u = Dot(s, p) * inv;
		if (u < 0.0f || u > 1.0f)
			return false;
		const Vec3 q = Cross(s, e1);
		v = Dot(r.dir, q) * inv;
		if (v < 0.0f || u + v > 1.0f)
			return false;
		t = Dot(e2, q) * inv;
		return t > 0.0f;
	}
#ifdef _MSC_VER
#	pragma warning(pop)
#endif

	/**
	 * Ray against a bounding sphere. True when the ray (forward half only) touches it.
	 * tEnter is the distance where the ray enters, or 0 when the origin is already inside.
	 */
	inline bool RaySphere(const Ray& r, Vec3 centre, float radius, float& tEnter)
	{
		if (!(radius > 0.0f))
			return false;
		const Vec3  oc = r.origin - centre;
		const float b = Dot(oc, r.dir);
		const float c = Dot(oc, oc) - radius * radius;
		if (c <= 0.0f) {
			tEnter = 0.0f;
			return true;
		}
		if (b > 0.0f)
			return false;  // outside and pointing away
		const float disc = b * b - c;
		if (disc < 0.0f)
			return false;
		tEnter = -b - std::sqrt(disc);
		return true;
	}

	/** Projects a world point with a worldToCam matrix to NDC (x right, y up, both -1..1). False when behind the camera. */
	inline bool ProjectToNdc(const Mat4& m, Vec3 p, float& ndcX, float& ndcY)
	{
		const float x = m.m[0][0] * p.x + m.m[0][1] * p.y + m.m[0][2] * p.z + m.m[0][3];
		const float y = m.m[1][0] * p.x + m.m[1][1] * p.y + m.m[1][2] * p.z + m.m[1][3];
		const float w = m.m[3][0] * p.x + m.m[3][1] * p.y + m.m[3][2] * p.z + m.m[3][3];
		if (!(w > 1e-6f))
			return false;
		ndcX = x / w;
		ndcY = y / w;
		return true;
	}

	/**
	 * World ray through an NDC point, derived from the worldToCam matrix alone (no depth convention needed).
	 * The eye is where rows 0, 1 and 3 all vanish; the direction is the intersection line of the planes
	 * row0 - X*row3 = 0 and row1 - Y*row3 = 0, signed so that it points in front of the camera (w grows).
	 */
	inline bool NdcToRay(const Mat4& m, float ndcX, float ndcY, Ray& out)
	{
		const Vec3 r0{ m.m[0][0], m.m[0][1], m.m[0][2] };
		const Vec3 r1{ m.m[1][0], m.m[1][1], m.m[1][2] };
		const Vec3 r3{ m.m[3][0], m.m[3][1], m.m[3][2] };

		// eye: r0.e = -m03, r1.e = -m13, r3.e = -m33  (Cramer's rule)
		const float det = Dot(r0, Cross(r1, r3));
		if (std::fabs(det) < 1e-12f)
			return false;
		const Vec3 rhs{ -m.m[0][3], -m.m[1][3], -m.m[3][3] };
		const Vec3 eye = (Cross(r1, r3) * rhs.x + Cross(r3, r0) * rhs.y + Cross(r0, r1) * rhs.z) * (1.0f / det);

		const Vec3 n1 = r0 - r3 * ndcX;
		const Vec3 n2 = r1 - r3 * ndcY;
		Vec3       d = Cross(n1, n2);
		if (Length(d) < 1e-12f)
			return false;
		d = Normalised(d);
		if (Dot(r3, d) < 0.0f)
			d = d * -1.0f;
		out.origin = eye;
		out.dir = d;
		return true;
	}

	/** Cursor as a fraction of the window (0,0 top-left, 1,1 bottom-right) to NDC. yUp picks the usual orientation. */
	inline void CursorToNdc(float fracX, float fracY, bool yUp, float& ndcX, float& ndcY)
	{
		ndcX = fracX * 2.0f - 1.0f;
		ndcY = yUp ? (1.0f - fracY * 2.0f) : (fracY * 2.0f - 1.0f);
	}

	/** Half-precision float to float (vertex data of non-full-precision shapes). */
	inline float HalfToFloat(std::uint16_t h)
	{
		const std::uint32_t sign = (h >> 15) & 1u;
		const std::uint32_t exp = (h >> 10) & 0x1Fu;
		const std::uint32_t man = h & 0x3FFu;
		float               value;
		if (exp == 0)
			value = std::ldexp(static_cast<float>(man), -24);
		else if (exp == 31)
			value = man ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
		else
			value = std::ldexp(static_cast<float>(man | 0x400u), static_cast<int>(exp) - 25);
		return sign ? -value : value;
	}

	struct SphereBound
	{
		Vec3  centre;
		float radius = 0.0f;
	};

	/**
	 * For a selected object the ray missed: the index of the part nearest the ray. The gap is the distance from the ray (the
	 * forward half, starting at the camera) to the sphere's surface, 0 when the ray passes through it. Parts wholly behind the
	 * camera are skipped unless allowBehind (then the part nearest the camera wins); ties go to the nearer part along the ray.
	 * -1 when no part is in front (never with allowBehind and at least one part).
	 */
	inline int NearestToRay(const Ray& r, const std::vector<SphereBound>& spheres, float* gap = nullptr, bool allowBehind = false)
	{
		int   best = -1;
		float bestGap = std::numeric_limits<float>::max();
		float bestAlong = std::numeric_limits<float>::max();
		for (int i = 0; i < static_cast<int>(spheres.size()); ++i) {
			const Vec3  toCentre = spheres[i].centre - r.origin;
			const float along = Dot(toCentre, r.dir);
			if (!allowBehind && along < -spheres[i].radius)
				continue;  // behind the camera
			const float distance = along > 0.0f ? Length(toCentre - r.dir * along) : Length(toCentre);
			const float g = std::max(0.0f, distance - spheres[i].radius);
			if (g < bestGap || (g == bestGap && along < bestAlong)) {
				bestGap = g;
				bestAlong = along;
				best = i;
			}
		}
		if (best >= 0 && gap)
			*gap = bestGap;
		return best;
	}

	/** What one shape offers the chooser. */
	struct Candidate
	{
		bool  exactTested = false;  ///< triangles were read and tested
		bool  exactHit = false;
		float exactT = 0.0f;
		bool  boundHit = false;
		float boundT = 0.0f;
		float boundRadius = 0.0f;  ///< tie-break: smaller bound is the more specific part
	};

	struct Choice
	{
		int              index = -1;
		bool             exact = false;  ///< false = ranked by bounds ("approximate")
		std::vector<int> runnersUp;      ///< other shapes the ray touches, nearest first
	};

	/**
	 * Nearest exact triangle hit wins. It is only "exact" when no shape that could not be tested
	 * (skinned, no CPU copy) has its bound hit nearer than the winner; otherwise the answer is flagged approximate.
	 * With no triangle hit at all, shapes are ranked by where the ray enters their bound.
	 */
	inline Choice Choose(const std::vector<Candidate>& c)
	{
		Choice      out;
		int         best = -1;
		float       bestT = std::numeric_limits<float>::max();
		for (int i = 0; i < static_cast<int>(c.size()); ++i) {
			if (c[i].exactTested && c[i].exactHit && c[i].exactT < bestT) {
				bestT = c[i].exactT;
				best = i;
			}
		}
		if (best >= 0) {
			out.index = best;
			out.exact = true;
			for (int i = 0; i < static_cast<int>(c.size()); ++i)
				if (i != best && !c[i].exactTested && c[i].boundHit && c[i].boundT < bestT)
					out.exact = false;  // an untestable part may sit in front
		} else {
			float bestBound = std::numeric_limits<float>::max();
			float bestRadius = std::numeric_limits<float>::max();
			for (int i = 0; i < static_cast<int>(c.size()); ++i) {
				if (!c[i].boundHit)
					continue;
				// A tested shape whose triangles all missed cannot be the clicked one.
				if (c[i].exactTested && !c[i].exactHit)
					continue;
				if (c[i].boundT < bestBound || (c[i].boundT == bestBound && c[i].boundRadius < bestRadius)) {
					bestBound = c[i].boundT;
					bestRadius = c[i].boundRadius;
					best = i;
				}
			}
			if (best < 0) {  // everything tested and missed, or nothing hit: still name the nearest bound
				for (int i = 0; i < static_cast<int>(c.size()); ++i) {
					if (c[i].boundHit && (c[i].boundT < bestBound || (c[i].boundT == bestBound && c[i].boundRadius < bestRadius))) {
						bestBound = c[i].boundT;
						bestRadius = c[i].boundRadius;
						best = i;
					}
				}
			}
			out.index = best;
			out.exact = false;
		}

		// runners-up: every other touched shape, ordered by exact distance when known, else by bound entry
		std::vector<std::pair<float, int>> rest;
		for (int i = 0; i < static_cast<int>(c.size()); ++i) {
			if (i == out.index)
				continue;
			if (c[i].exactTested && c[i].exactHit)
				rest.emplace_back(c[i].exactT, i);
			else if (c[i].boundHit && !(c[i].exactTested && !c[i].exactHit))
				rest.emplace_back(c[i].boundT, i);
		}
		std::sort(rest.begin(), rest.end());
		for (const auto& r : rest)
			out.runnersUp.push_back(r.second);
		return out;
	}
}
