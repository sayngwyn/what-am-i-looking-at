// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// Fallout 4: reads one reference's 3D into the plain PickInfo (parts, shaders, textures, material files) and works out which part a
// ray touches, plus the land, the water and the sky (WorldFill.cpp) when the ray meets those instead of an object.
#pragma once

#include "PickData.h"

namespace RE
{
	class NiAVObject;
	class TESObjectREFR;
}

namespace ti
{
	/**
	 * Gathers everything about one reference's 3D and works out which part the ray touches. Fills object, shapes, method, clicked, runnersUp.
	 * a_fill false skips the Report (identity, facts, warnings): for scanning many objects. a_forceShape >= 0 reports that part instead of
	 * the one the ray chose (step-through). a_nearestIfMissed: the object was chosen another way (the console), so a part is always named.
	 */
	PickInfo InspectRef(RE::TESObjectREFR* a_ref, const Ray& a_ray, bool a_nearestIfMissed = false, bool a_fill = true, int a_forceShape = -1);

	/** The visible parts under a_root with their shader, material and texture facts (no ray work). For worn armor and head parts. */
	std::vector<ShapeInfo> DescribeParts(RE::NiAVObject* a_root);

	/** What a pick is asked to look at. */
	enum class PickMode
	{
		kAuto,    ///< whatever the ray meets first: a part of an object, the land, water, else the sky
		kGround,  ///< the land under the crosshair
		kWater,   ///< the water surface under the crosshair
		kSky      ///< the sky, wherever the crosshair points
	};

	/**
	 * The crosshair path: scans every object the ray meets within a_maxDistance without the physics engine (exact triangles where the CPU
	 * copy of the mesh exists, bounding spheres otherwise), plus the land and water, reports the one a_mode asks for and keeps every hit,
	 * nearest first, in hits. Nothing hit in auto mode gives the sky.
	 */
	PickInfo PickAlongRay(const Ray& a_ray, float a_maxDistance, PickMode a_mode = PickMode::kAuto);

	/** Re-reports hit a_index of a_hits (from an earlier PickAlongRay along a_ray), with the same hits list. */
	PickInfo PickStep(const Ray& a_ray, const std::vector<HitInfo>& a_hits, int a_index);
}
