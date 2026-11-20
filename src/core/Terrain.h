// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// Land maths: no game or CommonLib dependency, so it is unit-tested offline. A loaded exterior cell keeps its land as four
// quadrants of 17 x 17 height samples, 128 game units apart (so a quadrant is 2048 units wide and a cell 4096).
// Quadrant 0 is the south-west one, 1 south-east, 2 north-west, 3 north-east; sample (i, j) is i steps east and j steps north
// of the quadrant's south-west corner and is stored at index j * 17 + i.
#pragma once

#include "Pick.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace ti
{
	constexpr int   kQuadVerts = 17;
	constexpr float kVertexSpacing = 128.0f;
	constexpr float kQuadSize = kVertexSpacing * (kQuadVerts - 1);  // 2048
	constexpr float kCellSize = kQuadSize * 2.0f;                  // 4096

	/** One quadrant's heights in world units, with the world position of its south-west corner. */
	struct QuadGrid
	{
		float        originX = 0.0f;
		float        originY = 0.0f;
		const float* heights = nullptr;  ///< kQuadVerts * kQuadVerts values
		float        zOffset = 0.0f;     ///< added to every stored height (the stored heights sit this far below the world's)
		float Height(int a_i, int a_j) const { return heights[a_j * kQuadVerts + a_i] + zOffset; }
		Vec3  Vertex(int a_i, int a_j) const { return { originX + static_cast<float>(a_i) * kVertexSpacing, originY + static_cast<float>(a_j) * kVertexSpacing, Height(a_i, a_j) }; }
	};

	/** World position of a quadrant's south-west corner, given the cell's grid coordinates. */
	inline void QuadrantOrigin(int a_cellX, int a_cellY, int a_quadrant, float& a_x, float& a_y)
	{
		a_x = static_cast<float>(a_cellX) * kCellSize + static_cast<float>(a_quadrant & 1) * kQuadSize;
		a_y = static_cast<float>(a_cellY) * kCellSize + static_cast<float>((a_quadrant >> 1) & 1) * kQuadSize;
	}

	/** Which quadrant of the cell holds the world point; -1 when the point is outside the cell. */
	inline int QuadrantOf(int a_cellX, int a_cellY, float a_x, float a_y)
	{
		const float lx = a_x - static_cast<float>(a_cellX) * kCellSize;
		const float ly = a_y - static_cast<float>(a_cellY) * kCellSize;
		if (lx < 0.0f || ly < 0.0f || lx > kCellSize || ly > kCellSize)
			return -1;
		return (lx >= kQuadSize ? 1 : 0) + (ly >= kQuadSize ? 2 : 0);
	}

	inline const char* QuadrantName(int a_quadrant)
	{
		switch (a_quadrant) {
		case 0: return "south-west";
		case 1: return "south-east";
		case 2: return "north-west";
		case 3: return "north-east";
		default: return "unknown";
		}
	}

	struct GridHit
	{
		bool  hit = false;
		float t = 0.0f;
		Vec3  point;
		int   i = 0;  ///< the grid square's south-west vertex
		int   j = 0;
	};

	/** Nearest hit of the ray on the quadrant's surface (two triangles per grid square, two-sided). */
	inline GridHit RayQuad(const Ray& a_ray, const QuadGrid& a_grid)
	{
		GridHit result;
		if (!a_grid.heights)
			return result;
		// quick reject on the box around the whole quadrant
		float zMin = a_grid.heights[0], zMax = a_grid.heights[0];
		for (int k = 1; k < kQuadVerts * kQuadVerts; ++k) {
			zMin = std::min(zMin, a_grid.heights[k]);
			zMax = std::max(zMax, a_grid.heights[k]);
		}
		zMin += a_grid.zOffset;
		zMax += a_grid.zOffset;
		{
			float       tNear = 0.0f, tFar = std::numeric_limits<float>::max();
			const float lo[3] = { a_grid.originX, a_grid.originY, zMin }, hi[3] = { a_grid.originX + kQuadSize, a_grid.originY + kQuadSize, zMax };
			const float o[3] = { a_ray.origin.x, a_ray.origin.y, a_ray.origin.z }, d[3] = { a_ray.dir.x, a_ray.dir.y, a_ray.dir.z };
			for (int axis = 0; axis < 3; ++axis) {
				if (std::fabs(d[axis]) < 1e-9f) {
					if (o[axis] < lo[axis] || o[axis] > hi[axis])
						return result;
					continue;
				}
				float t1 = (lo[axis] - o[axis]) / d[axis], t2 = (hi[axis] - o[axis]) / d[axis];
				if (t1 > t2)
					std::swap(t1, t2);
				tNear = std::max(tNear, t1);
				tFar = std::min(tFar, t2);
				if (tNear > tFar)
					return result;
			}
		}
		float best = std::numeric_limits<float>::max();
		for (int j = 0; j < kQuadVerts - 1; ++j) {
			for (int i = 0; i < kQuadVerts - 1; ++i) {
				const Vec3 a = a_grid.Vertex(i, j), b = a_grid.Vertex(i + 1, j), c = a_grid.Vertex(i + 1, j + 1), d = a_grid.Vertex(i, j + 1);
				float      t = 0, u = 0, v = 0;
				if (RayTriangle(a_ray, a, b, c, t, u, v) && t < best) {
					best = t;
					result = { true, t, a_ray.origin + a_ray.dir * t, i, j };
				}
				if (RayTriangle(a_ray, a, c, d, t, u, v) && t < best) {
					best = t;
					result = { true, t, a_ray.origin + a_ray.dir * t, i, j };
				}
			}
		}
		return result;
	}

	/** The land surface's height straight under (x, y) in the quadrant, interpolated across the grid square's two triangles. NaN outside. */
	inline float QuadHeightAt(const QuadGrid& a_grid, float a_x, float a_y)
	{
		const float gx = (a_x - a_grid.originX) / kVertexSpacing, gy = (a_y - a_grid.originY) / kVertexSpacing;
		if (!(gx >= 0.0f && gy >= 0.0f && gx <= kQuadVerts - 1 && gy <= kQuadVerts - 1))
			return std::numeric_limits<float>::quiet_NaN();
		const int   i = std::min(static_cast<int>(gx), kQuadVerts - 2), j = std::min(static_cast<int>(gy), kQuadVerts - 2);
		const float fx = gx - static_cast<float>(i), fy = gy - static_cast<float>(j);
		const float h00 = a_grid.Height(i, j), h10 = a_grid.Height(i + 1, j), h11 = a_grid.Height(i + 1, j + 1), h01 = a_grid.Height(i, j + 1);
		// the same two triangles RayQuad uses: (00, 10, 11) below the diagonal (fx >= fy), (00, 11, 01) above it
		if (fx >= fy)
			return h00 + (h10 - h00) * fx + (h11 - h10) * fy;
		return h00 + (h11 - h01) * fx + (h01 - h00) * fy;
	}

	/** The four vertices around a point and their bilinear weights (they sum to 1). */
	struct Bilinear
	{
		int   i = 0, j = 0;  ///< south-west vertex of the square
		float fx = 0.0f, fy = 0.0f;
		float Weight(int a_di, int a_dj) const { return (a_di ? fx : 1.0f - fx) * (a_dj ? fy : 1.0f - fy); }
	};

	inline Bilinear BilinearAt(const QuadGrid& a_grid, float a_x, float a_y)
	{
		Bilinear    b;
		const float gx = std::clamp((a_x - a_grid.originX) / kVertexSpacing, 0.0f, static_cast<float>(kQuadVerts - 1));
		const float gy = std::clamp((a_y - a_grid.originY) / kVertexSpacing, 0.0f, static_cast<float>(kQuadVerts - 1));
		b.i = std::min(static_cast<int>(gx), kQuadVerts - 2);
		b.j = std::min(static_cast<int>(gy), kQuadVerts - 2);
		b.fx = gx - static_cast<float>(b.i);
		b.fy = gy - static_cast<float>(b.j);
		return b;
	}

	/** The vertex nearest (x, y): indices 0..16. */
	inline void NearestVertex(const QuadGrid& a_grid, float a_x, float a_y, int& a_i, int& a_j)
	{
		a_i = std::clamp(static_cast<int>(std::lround((a_x - a_grid.originX) / kVertexSpacing)), 0, kQuadVerts - 1);
		a_j = std::clamp(static_cast<int>(std::lround((a_y - a_grid.originY) / kVertexSpacing)), 0, kQuadVerts - 1);
	}

	/**
	 * The engine stores each layer's opacity at a vertex as a signed byte. The layer is fully opaque at kLayerFull; the stored
	 * range was read from the game (see the proof notes). A value is shown as a percent of that, never above 100.
	 */
	constexpr int kLayerFull = 127;
	inline float LayerPercent(float a_raw) { return std::clamp(a_raw * 100.0f / static_cast<float>(kLayerFull), 0.0f, 100.0f); }

	/** A layer's blend at a point: bilinear across the four vertices (raw values per vertex in a_raw: 00, 10, 01, 11 order = (di,dj) (0,0) (1,0) (0,1) (1,1)). */
	inline float BlendRaw(const Bilinear& a_b, const float a_raw[4])
	{
		return a_raw[0] * a_b.Weight(0, 0) + a_raw[1] * a_b.Weight(1, 0) + a_raw[2] * a_b.Weight(0, 1) + a_raw[3] * a_b.Weight(1, 1);
	}
}
