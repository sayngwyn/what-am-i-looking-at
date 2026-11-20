// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
#include "PickScan.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <limits>

namespace ti
{
	namespace
	{
		Vec3 ToWorld(const MeshView& a_mesh, Vec3 a_local)
		{
			const float* r = a_mesh.rotate;
			const Vec3   rotated{ r[0] * a_local.x + r[1] * a_local.y + r[2] * a_local.z, r[3] * a_local.x + r[4] * a_local.y + r[5] * a_local.z,
				r[6] * a_local.x + r[7] * a_local.y + r[8] * a_local.z };
			return rotated * a_mesh.scale + a_mesh.translate;
		}

		bool Readable(const MeshView& a_mesh, std::string& a_reason)
		{
			if (!a_mesh.vertexBytes || !a_mesh.indices) {
				a_reason = "no CPU copy of the mesh";
				return false;
			}
			if (a_mesh.vertexCount == 0 || a_mesh.triangleCount == 0) {
				a_reason = "no vertices or triangles";
				return false;
			}
			const std::uint32_t needed = a_mesh.fullPrecision ? 12u : 6u;
			if (a_mesh.stride < needed) {
				a_reason = "vertex size too small for positions";
				return false;
			}
			if (static_cast<std::size_t>(a_mesh.stride) * a_mesh.vertexCount > a_mesh.vertexByteCount) {
				a_reason = "vertex data shorter than its count";
				return false;
			}
			return true;
		}
	}

	Vec3 MeshWorldPosition(const MeshView& a_mesh, std::uint32_t a_index)
	{
		const std::uint8_t* v = a_mesh.vertexBytes + static_cast<std::size_t>(a_mesh.stride) * a_index;
		Vec3                local;
		if (a_mesh.fullPrecision) {
			std::memcpy(&local, v, sizeof(local));
		} else {
			std::uint16_t h[3];
			std::memcpy(h, v, sizeof(h));
			local = { HalfToFloat(h[0]), HalfToFloat(h[1]), HalfToFloat(h[2]) };
		}
		return ToWorld(a_mesh, local);
	}

	MeshTest TestMesh(const MeshView& a_mesh, const Ray& a_ray)
	{
		MeshTest result;
		if (!Readable(a_mesh, result.reason))
			return result;

		std::vector<Vec3> positions(a_mesh.vertexCount);
		for (std::uint32_t i = 0; i < a_mesh.vertexCount; ++i)
			positions[i] = MeshWorldPosition(a_mesh, i);

		result.tested = true;
		float bestT = std::numeric_limits<float>::max();
		for (std::uint32_t t = 0; t < a_mesh.triangleCount; ++t) {
			const std::uint16_t i0 = a_mesh.indices[t * 3], i1 = a_mesh.indices[t * 3 + 1], i2 = a_mesh.indices[t * 3 + 2];
			if (i0 >= a_mesh.vertexCount || i1 >= a_mesh.vertexCount || i2 >= a_mesh.vertexCount)
				continue;
			float dist = 0, u = 0, v = 0;
			if (RayTriangle(a_ray, positions[i0], positions[i1], positions[i2], dist, u, v) && dist < bestT) {
				bestT = dist;
				result.hit = true;
				result.t = dist;
				result.triangle = static_cast<int>(t);
			}
		}
		if (result.hit)
			result.point = a_ray.origin + a_ray.dir * result.t;
		return result;
	}

	bool PartAlongRay(const ShapeInfo& a_shape, const Ray& a_ray, float& a_score, bool& a_exact)
	{
		if (a_shape.exactHit) {
			a_score = a_shape.exactDistance;
			a_exact = true;
			return true;
		}
		if (a_shape.exactTested || !a_shape.boundHit || a_shape.exactSkipReason == "not a tri shape" || a_shape.exactSkipReason == "not reached by the ray")
			return false;
		const float centre = Dot(a_shape.boundCentre - a_ray.origin, a_ray.dir);
		if (centre <= 0.0f)
			return false;  // the bound's centre is behind the camera: the part is around or behind us, not ahead
		a_score = std::max(a_shape.boundDistance, centre);
		a_exact = false;
		return true;
	}

	void ApplyChoice(PickInfo& a_info, const Ray& a_ray, bool a_nearestIfMissed, int a_forceShape)
	{
		std::vector<Candidate> candidates;
		for (const auto& s : a_info.shapes) {
			Candidate c;
			c.exactTested = s.exactTested;
			c.exactHit = s.exactHit;
			c.exactT = s.exactDistance;
			c.boundHit = s.boundHit;
			c.boundT = s.boundDistance;
			c.boundRadius = s.boundRadius;
			candidates.push_back(c);
		}
		for (auto& s : a_info.shapes) {
			s.clicked = false;
			s.rank = -1;
		}
		a_info.clicked = -1;
		a_info.runnersUp.clear();

		const Choice choice = Choose(candidates);
		if (choice.index >= 0) {
			a_info.clicked = choice.index;
			a_info.shapes[static_cast<std::size_t>(choice.index)].clicked = true;
			a_info.shapes[static_cast<std::size_t>(choice.index)].rank = 0;
			a_info.runnersUp = choice.runnersUp;
			int rank = 1;
			for (const int i : choice.runnersUp)
				a_info.shapes[static_cast<std::size_t>(i)].rank = rank++;
			a_info.method = choice.exact ? "exact" : "approximate";
		} else if (a_nearestIfMissed) {
			// The object was chosen some other way (the console selection): never leave it unanswered. Take the part nearest the ray.
			std::vector<SphereBound> bounds;
			for (const auto& s : a_info.shapes)
				bounds.push_back({ s.boundCentre, s.boundRadius });
			float gap = 0.0f;
			int   nearest = NearestToRay(a_ray, bounds, &gap);
			bool  behind = false;
			if (nearest < 0) {
				nearest = NearestToRay(a_ray, bounds, &gap, true);  // the object is behind the camera: still name its nearest part
				behind = nearest >= 0;
			}
			if (nearest >= 0) {
				a_info.clicked = nearest;
				a_info.shapes[static_cast<std::size_t>(nearest)].clicked = true;
				a_info.shapes[static_cast<std::size_t>(nearest)].rank = 0;
				a_info.method = "nearest_to_center";
				if (behind)
					a_info.notes.push_back(std::format("the object is behind the camera; part nearest to the screen center, {:.1f} units off", gap));
				else
					a_info.notes.push_back(std::format("the screen-center ray misses this object; part nearest to it, {:.1f} units off", gap));
			} else {
				a_info.method = "none";
				a_info.message = "this object has no parts to show";
			}
		} else {
			a_info.method = "none";
			a_info.message = "the ray does not touch any part of this object";
		}

		if (a_forceShape >= 0 && a_forceShape < static_cast<int>(a_info.shapes.size())) {
			// step-through: report the part the hit list named, whatever the ray's own choice was
			for (auto& s : a_info.shapes) {
				s.clicked = false;
				if (s.rank == 0)
					s.rank = -1;
			}
			a_info.clicked = a_forceShape;
			a_info.shapes[static_cast<std::size_t>(a_forceShape)].clicked = true;
			a_info.shapes[static_cast<std::size_t>(a_forceShape)].rank = 0;
			a_info.runnersUp.clear();
			a_info.method = "step_through";
			a_info.message.clear();
		}
	}

	void AppendPartHits(const PickInfo& a_info, const Ray& a_ray, std::vector<HitInfo>& a_hits)
	{
		for (const auto& s : a_info.shapes) {
			float score = 0.0f;
			bool  exact = false;
			if (!PartAlongRay(s, a_ray, score, exact))
				continue;
			HitInfo h;
			h.kind = "part";
			h.partName = s.name;
			h.label = std::format("{} on {}", s.name.empty() ? std::format("part {}", s.index + 1) : s.name, a_info.object.name);
			h.exact = exact;
			h.distance = score;
			h.point = exact ? s.hitPoint : a_ray.origin + a_ray.dir * score;
			h.refFormId = a_info.object.refFormId;
			h.shapeIndex = s.index;
			a_hits.push_back(std::move(h));
		}
	}

	SelectionVerdict JudgeSelection(const PickInfo& a_selected, const Ray& a_ray, const PickInfo& a_rayPick, bool a_changed, float a_maxDistance, bool a_trustApproximate, float a_fallbackDistance)
	{
		const auto ray = [](const char* a_why) { return SelectionVerdict{ false, a_why }; };
		if (!a_changed)
			return ray("rule: the console selection did not change with this click (stale or none); the ray decides");
		if (IsWaterPlane(a_selected))
			return ray("rule: the console selected the cell's water plane; the ray decides");
		if (a_selected.clicked < 0)
			return ray("rule: the ray does not reach a part of the newly selected object; the ray decides");
		if (a_rayPick.valid && a_rayPick.object.refFormId != 0 && a_rayPick.object.refFormId == a_selected.object.refFormId)
			return ray("rule: the ray hit the selected object itself; the ray's pick is used");
		if (a_selected.method != "exact" && !a_trustApproximate)
			return ray("rule: the newly selected object is only a bounds hit and the console selection is not trusted for that here; the ray decides");
		std::vector<HitInfo> parts;
		AppendPartHits(a_selected, a_ray, parts);
		float selectedDistance = a_fallbackDistance;
		if (!parts.empty()) {
			selectedDistance = parts.front().distance;
			for (const auto& h : parts)
				selectedDistance = std::min(selectedDistance, h.distance);
		}
		if (selectedDistance > a_maxDistance)
			return ray("rule: the newly selected object is out of reach along the ray; the ray decides");
		if (a_rayPick.valid && a_rayPick.identity.kind != "sky" && a_rayPick.hitIndex >= 0 && a_rayPick.hitIndex < static_cast<int>(a_rayPick.hits.size())) {
			if (a_rayPick.hits[static_cast<std::size_t>(a_rayPick.hitIndex)].distance < selectedDistance - 1.0f)
				return ray("rule: the ray hit something nearer than the newly selected object; the ray decides");
		}
		return SelectionVerdict{ true, "rule: this click made the console select a new object and nothing nearer was hit; the selected object is used" };
	}

	void SortHits(std::vector<HitInfo>& a_hits)
	{
		std::stable_sort(a_hits.begin(), a_hits.end(), [](const HitInfo& a, const HitInfo& b) { return a.distance < b.distance; });
	}

	int IndexOfPartHit(const std::vector<HitInfo>& a_hits, std::uint32_t a_ref, int a_shape)
	{
		for (std::size_t i = 0; i < a_hits.size(); ++i)
			if (a_hits[i].kind == "part" && a_hits[i].refFormId == a_ref && a_hits[i].shapeIndex == a_shape)
				return static_cast<int>(i);
		return -1;
	}

	bool IsWaterPlane(const PickInfo& a_info)
	{
		if (a_info.object.baseType != "ACTI")
			return false;
		for (const auto& shape : a_info.shapes) {
			std::string name = shape.name;
			for (auto& c : name)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			if (name.find("proceduralwater") != std::string::npos)
				return true;
		}
		return false;
	}
}
