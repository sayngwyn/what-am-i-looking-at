// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// Game-side readers for the things that are no object: the land under a point (its texture layers and blend), the water surface
// and the sky. Each fills one section of a PickInfo; the formatting lives in Output.cpp. The land maths is in Terrain.h.
#pragma once

#include "PickData.h"

namespace RE
{
	class TESObjectREFR;
}

namespace ti
{
	/** Where a ray meets something that is no object. */
	struct WorldHit
	{
		bool        hit = false;
		float       distance = 0.0f;
		Vec3        point;
		std::string label;  ///< one line for the hits list
	};

	/** Nearest meeting of the ray with the loaded land (heights of the loaded cells), within a_maxDistance. Never throws. */
	WorldHit RayLand(const Ray& a_ray, float a_maxDistance);

	/** Nearest meeting of the ray with the water surface of a cell that has water, in front of the land. Never throws. */
	WorldHit RayWater(const Ray& a_ray, float a_maxDistance);

	/** Fills a_info as a terrain report for the land at a_point (identity.kind "terrain", valid). Never throws. */
	void FillTerrain(PickInfo& a_info, const Vec3& a_point);

	/**
	 * Fills a_info as a water report for the surface at a_point. a_plane: a placed water plane the ray hit (null for the cell's own
	 * water). Never throws.
	 */
	void FillWater(PickInfo& a_info, const Vec3& a_point, RE::TESObjectREFR* a_plane);

	/** Fills a_info as a sky report (identity.kind "sky", valid). Never throws. */
	void FillSky(PickInfo& a_info);
}
