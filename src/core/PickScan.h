// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// The game-free half of picking an object's part: the exact triangle test on a copied vertex/index buffer, how a ray ranks the
// parts of one object, and the list of hits kept along the ray for stepping. The game layer reads the engine's buffers and shapes
// into these plain structs; everything here is unit-tested offline. It reuses the ray, triangle and chooser maths of Pick.h.
#pragma once

#include "PickData.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ti
{
	/** A shape's mesh as copied from the engine: raw vertices (position first), 16-bit indices, and the local-to-world transform. */
	struct MeshView
	{
		const std::uint8_t*  vertexBytes = nullptr;
		std::size_t          vertexByteCount = 0;
		std::uint32_t        stride = 0;           ///< bytes per vertex
		std::uint32_t        vertexCount = 0;
		bool                 fullPrecision = true; ///< positions are 3 floats; false = 3 half floats
		const std::uint16_t* indices = nullptr;
		std::uint32_t        triangleCount = 0;
		float                rotate[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };  ///< row-major, local to world
		Vec3                 translate;
		float                scale = 1.0f;
	};

	struct MeshTest
	{
		bool        tested = false;
		std::string reason;  ///< why nothing was tested
		bool        hit = false;
		float       t = 0.0f;
		Vec3        point;
		int         triangle = -1;
	};

	/** The nearest triangle the ray crosses (two-sided). Refuses (tested = false, reason set) a view whose sizes do not add up. */
	MeshTest TestMesh(const MeshView& a_mesh, const Ray& a_ray);

	/** A world-space position of vertex a_index of a_mesh (the transform applied). */
	Vec3 MeshWorldPosition(const MeshView& a_mesh, std::uint32_t a_index);

	/**
	 * Where a part stands along the ray, when the ray reaches it: a triangle hit (exact), or, for a part whose mesh could not be tested,
	 * the nearer of where the ray enters its bound and where the bound's centre projects onto the ray. A part whose triangles were
	 * tested and missed never counts, nor does one the ray did not reach or that is not a tri shape.
	 */
	bool PartAlongRay(const ShapeInfo& a_shape, const Ray& a_ray, float& a_score, bool& a_exact);

	/**
	 * After a_info.shapes are read (bounds and triangle tests done): chooses the clicked part, ranks the runners-up and sets method,
	 * clicked, runnersUp, rank and message. a_nearestIfMissed: the object was chosen another way (the console selection), so a part is
	 * always named, the one nearest the ray. a_forceShape >= 0 reports that part instead (stepping through the hits).
	 */
	void ApplyChoice(PickInfo& a_info, const Ray& a_ray, bool a_nearestIfMissed, int a_forceShape);

	/** Appends one "part" hit per part of a_info the ray reaches (label "<part> on <object>"), unsorted. */
	void AppendPartHits(const PickInfo& a_info, const Ray& a_ray, std::vector<HitInfo>& a_hits);

	/** What a console click should report: the console's selected object, or our own ray's pick. */
	struct SelectionVerdict
	{
		bool        use = false;  ///< true: report the selected object; false: report the ray's pick
		std::string reason;       ///< which rule chose, one line for the log
	};

	/**
	 * The rule for a console click. a_selected is the console's selected reference read along the ray, a_rayPick our own ray's pick, a_changed
	 * whether the click changed the console's selection (a new, non-null reference). The selected object wins only when the click made the
	 * console select it, it is not the cell's water plane, the ray reaches one of its parts (an approximate, bounds-only part only when
	 * a_trustApproximate), it is within a_maxDistance, and nothing else the ray hit is nearer. Otherwise our ray decides.
	 * a_fallbackDistance stands in for the selected object's distance when none of its parts has one.
	 */
	SelectionVerdict JudgeSelection(const PickInfo& a_selected, const Ray& a_ray, const PickInfo& a_rayPick, bool a_changed, float a_maxDistance, bool a_trustApproximate, float a_fallbackDistance);

	/** Nearest first; equal distances keep their order. */
	void SortHits(std::vector<HitInfo>& a_hits);

	/** Index of the hit for part a_shape of reference a_ref, or -1. */
	int IndexOfPartHit(const std::vector<HitInfo>& a_hits, std::uint32_t a_ref, int a_shape);

	/**
	 * True when a pick of an object is the cell's water plane: the game's runtime activator whose one part is "DefaultProceduralWater"
	 * (both games). A click on water selects it as an ordinary object; the card shows the water instead.
	 */
	bool IsWaterPlane(const PickInfo& a_info);
}
