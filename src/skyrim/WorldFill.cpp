// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
#include "PCH.h"

#include "WorldFill.h"

#include "CardLayout.h"
#include "Inspector.h"
#include "ReportFill.h"
#include "Terrain.h"
#include "TextureInfo.h"

namespace ti
{
	namespace
	{
		std::string Safe(const char* a_text) { return a_text ? std::string(a_text) : std::string(); }

		std::string Hex8(std::uint32_t a_id) { return std::format("{:08X}", a_id); }

		RE::NiPoint3 ToNi(const Vec3& a_v) { return { a_v.x, a_v.y, a_v.z }; }

		bool Plausible(float a_height) { return std::isfinite(a_height) && std::fabs(a_height) < 1.0e7f; }

		const char* const kTextureSetLabels[8] = { "diffuse", "normal", "glow / skin tint", "height (parallax)", "environment (cube map)", "environment mask", "multilayer (inner layer)", "backlight / specular mask" };

		// ---- land ---------------------------------------------------------------------------------------------------------

		struct CellLand
		{
			RE::TESObjectCELL* cell = nullptr;
			RE::TESObjectLAND* land = nullptr;
			int                x = 0;
			int                y = 0;
		};

		bool LandOf(RE::TESObjectCELL* a_cell, CellLand& a_out)
		{
			if (!a_cell || !a_cell->IsExteriorCell())
				return false;
			auto* land = a_cell->GetRuntimeData().cellLand;
			if (!land || !land->loadedData)
				return false;
			auto* coordinates = a_cell->GetCoordinates();
			if (!coordinates)
				return false;
			a_out = { a_cell, land, coordinates->cellX, coordinates->cellY };
			return true;
		}

		QuadGrid GridOf(const CellLand& a_land, int a_quadrant)
		{
			QuadGrid grid;
			QuadrantOrigin(a_land.x, a_land.y, a_quadrant, grid.originX, grid.originY);
			grid.heights = a_land.land->loadedData->heights[a_quadrant];
			// The stored heights are not the world's: read in the game they sit a constant distance below the engine's own land
			// height at the same vertex. Measure that distance at the middle vertex and apply it to the whole quadrant.
			if (auto* tes = RE::TES::GetSingleton()) {
				const Vec3 middle = grid.Vertex(kQuadVerts / 2, kQuadVerts / 2);
				float      engine = 0.0f;
				if (tes->GetLandHeight(ToNi(middle), engine)) {
					const float offset = engine - middle.z;
					if (std::isfinite(offset) && std::fabs(offset) < 8192.0f)
						grid.zOffset = offset;
				}
			}
			return grid;
		}

		bool InInterior()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* cell = player ? player->GetParentCell() : nullptr;
			return cell && cell->IsInteriorCell();
		}

		/** The distinct loaded cells a ray passes over, sampled along it. */
		std::vector<RE::TESObjectCELL*> CellsAlong(const Ray& a_ray, float a_maxDistance)
		{
			std::vector<RE::TESObjectCELL*> cells;
			auto*                           tes = RE::TES::GetSingleton();
			if (!tes)
				return cells;
			constexpr float kStep = 1024.0f;
			for (float t = 0.0f;; t += kStep) {
				const float at = std::min(t, a_maxDistance);
				const Vec3  p = a_ray.origin + a_ray.dir * at;
				if (auto* cell = tes->GetCell(ToNi(p)); cell && std::find(cells.begin(), cells.end(), cell) == cells.end())
					cells.push_back(cell);
				if (at >= a_maxDistance)
					break;
			}
			return cells;
		}

		std::string CellLabel(RE::TESObjectCELL* a_cell)
		{
			if (!a_cell)
				return "(no cell)";
			std::string name = Safe(a_cell->GetName());
			if (name.empty()) {
				// no cell name: the worldspace and the grid say where ("Tamriel [4, -12]")
				if (auto* coordinates = a_cell->IsExteriorCell() ? a_cell->GetCoordinates() : nullptr) {
					auto*       world = a_cell->GetRuntimeData().worldSpace;
					std::string label;
					if (world) {
						std::string source;
						label = EditorIdOf(world->GetFormID(), world->GetFormEditorID(), source);
						if (label.empty())
							label = Safe(world->GetName());
					}
					return std::format("{} [{}, {}]", label.empty() ? std::string("Cell") : label, coordinates->cellX, coordinates->cellY);
				}
				return "(unnamed cell)";
			}
			return name;
		}

		std::string WorldspaceLabel(RE::TESObjectCELL* a_cell)
		{
			auto* world = a_cell ? a_cell->GetRuntimeData().worldSpace : nullptr;
			if (!world)
				return {};
			std::string       source;
			const std::string editor = EditorIdOf(world->GetFormID(), world->GetFormEditorID(), source);
			const std::string name = Safe(world->GetName());
			if (name.empty())
				return editor;
			return editor.empty() ? name : name + " (" + editor + ")";
		}

		std::string MaterialName(RE::BGSMaterialType* a_material)
		{
			if (!a_material)
				return {};
			const std::string name = Safe(a_material->materialName.c_str());
			std::string       source;
			const std::string editor = EditorIdOf(a_material->GetFormID(), a_material->GetFormEditorID(), source);
			if (name.empty())
				return editor.empty() ? Hex8(a_material->GetFormID()) : editor;
			return editor.empty() ? name : name + " (" + editor + ")";
		}

		TerrainLayer MakeLayer(RE::TESLandTexture* a_ltex, std::string a_role)
		{
			TerrainLayer l;
			l.role = std::move(a_role);
			const FormRecord record = DescribeRecord(a_ltex);
			l.formId = record.formId;
			l.editorId = record.editorId;
			l.name = Safe(a_ltex->GetName());
			l.addedBy = record.addedBy;
			l.lastChangedBy = record.lastChangedBy;
			l.chain = record.chain;
			if (auto* set = a_ltex->textureSet) {
				int index = 0;
				for (const auto& texture : set->textures) {
					const std::string path = Safe(texture.textureName.c_str());
					if (!path.empty() && index < 8)
						l.textures.push_back({ kTextureSetLabels[index], path });
					++index;
				}
			}
			for (auto* grass : a_ltex->textureGrassList)
				if (grass)
					{
					l.grasses.push_back(DescribeForm(grass));
					// by model name for the card: "grass\grassplantsfieldgrass01.nif" -> "FieldGrass01"
					std::string model = Safe(grass->GetModel());
					if (const auto cut = model.find_last_of("\\/"); cut != std::string::npos)
						model.erase(0, cut + 1);
					if (const auto dot = model.rfind('.'); dot != std::string::npos)
						model.resize(dot);
					for (const char* prefix : { "GrassPlants", "Grass", "grassplants", "grass" })
						if (model.rfind(prefix, 0) == 0 && model.size() > std::strlen(prefix)) {
							model.erase(0, std::strlen(prefix));
							break;
						}
					if (!model.empty())
						l.grassNames.push_back(model);
				}
			l.footstepMaterial = MaterialName(a_ltex->materialType);
			return l;
		}

		float RawPercent(const RE::TESObjectLAND::LoadedLandData& a_data, int a_quadrant, int a_vertex, int a_layer)
		{
			// the stored byte read as unsigned: whatever its range, a negative number is never a share
			return static_cast<float>(static_cast<std::uint8_t>(a_data.percents[a_quadrant][a_vertex][a_layer]));
		}

		// ---- water --------------------------------------------------------------------------------------------------------

		struct WaterQuery
		{
			bool  has = false;
			float height = 0.0f;
			// what each engine question answered, kept as evidence
			bool  engineOk = false;
			float engineValue = 0.0f;
			bool  cellBool = false;
			float cellValue = 0.0f;
			bool  xclwOk = false;
			float xclw = 0.0f;
		};

		WaterQuery QueryWater(RE::TESObjectCELL* a_cell, const RE::NiPoint3& a_pos)
		{
			WaterQuery q;
			if (!a_cell)
				return q;
			if (auto* tes = RE::TES::GetSingleton()) {
				q.engineValue = tes->GetWaterHeight(a_pos, a_cell);
				q.engineOk = Plausible(q.engineValue);
			}
			float value = 0.0f;
			q.cellBool = a_cell->GetWaterHeight(a_pos, value);
			q.cellValue = value;
			q.xclw = a_cell->GetRuntimeData().waterHeight;
			q.xclwOk = Plausible(q.xclw);
			if (q.engineOk) {
				q.has = true;
				q.height = q.engineValue;
			} else if (q.cellBool && Plausible(q.cellValue)) {
				q.has = true;
				q.height = q.cellValue;
			} else if (q.xclwOk) {
				q.has = true;
				q.height = q.xclw;
			}
			return q;
		}

		RE::TESWaterForm* WaterTypeOf(RE::TESObjectCELL* a_cell, std::string& a_source)
		{
			if (!a_cell)
				return nullptr;
			if (auto* extra = a_cell->extraList.GetByType<RE::ExtraCellWaterType>(); extra && extra->water) {
				a_source = "the cell's own water type";
				return extra->water;
			}
			if (auto* world = a_cell->GetRuntimeData().worldSpace; world && world->worldWater) {
				a_source = "the worldspace's default water";
				return world->worldWater;
			}
			return nullptr;
		}

		bool GeometryHasWaterShader(RE::NiAVObject* a_object, int a_depth = 0)
		{
			if (!a_object || a_depth > 32)
				return false;
			if (auto* node = a_object->AsNode()) {
				for (auto& child : node->GetChildren())
					if (GeometryHasWaterShader(child.get(), a_depth + 1))
						return true;
				return false;
			}
			if (auto* geometry = a_object->AsGeometry())
				return netimmerse_cast<RE::BSWaterShaderProperty*>(geometry->GetGeometryRuntimeData().shaderProperty.get()) != nullptr;
			return false;
		}

		/** A reference placed in the cell near a_point that carries a water shader (a placed water plane), or null. */
		RE::TESObjectREFR* FindPlacedPlane(RE::TESObjectCELL* a_cell, const RE::NiPoint3& a_point)
		{
			RE::TESObjectREFR* found = nullptr;
			if (!a_cell)
				return nullptr;
			a_cell->ForEachReferenceInRange(a_point, 3000.0f, [&](RE::TESObjectREFR* a_ref) {
				if (!a_ref || a_ref->IsDisabled() || a_ref->IsDeleted())
					return RE::BSContainer::ForEachResult::kContinue;
				auto* root = a_ref->Get3D();
				if (!root)
					return RE::BSContainer::ForEachResult::kContinue;
				const auto& b = root->worldBound;
				const float dx = b.center.x - a_point.x, dy = b.center.y - a_point.y;
				if (dx * dx + dy * dy > (b.radius + 64.0f) * (b.radius + 64.0f))
					return RE::BSContainer::ForEachResult::kContinue;
				if (GeometryHasWaterShader(root)) {
					found = a_ref;
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			return found;
		}

		std::string TextureName(const RE::NiPointer<RE::NiSourceTexture>& a_texture)
		{
			return a_texture ? Safe(a_texture->name.c_str()) : std::string();
		}

		// ---- sky ----------------------------------------------------------------------------------------------------------

		std::string WeatherClassText(RE::TESWeather* a_weather)
		{
			if (!a_weather)
				return {};
			using F = RE::TESWeather::WeatherDataFlag;
			std::string out;
			const auto  flags = a_weather->data.flags;
			const auto  add = [&](bool a_on, const char* a_name) {
				if (a_on)
					out += (out.empty() ? "" : ", ") + std::string(a_name);
			};
			add(flags.any(F::kPleasant), "pleasant");
			add(flags.any(F::kCloudy), "cloudy");
			add(flags.any(F::kRainy), "rainy");
			add(flags.any(F::kSnow), "snow");
			add(flags.any(F::kPermAurora), "permanent aurora");
			return out;
		}

		void AddClouds(SkySection& a_sky, RE::TESWeather* a_weather, const char* a_which, RE::Clouds* a_live)
		{
			if (!a_weather)
				return;
			const std::uint32_t layers = std::min<std::uint32_t>(a_weather->numCloudLayers, RE::TESWeather::kTotalLayers);
			for (std::uint32_t i = 0; i < layers; ++i) {
				if (a_weather->cloudLayerDisabledBits & (1u << i))
					continue;
				CloudLayerInfo c;
				c.weather = a_which;
				c.index = static_cast<int>(i);
				c.texture = Safe(a_weather->cloudTextures[i].textureName.c_str());
				c.speedX = a_weather->cloudLayerSpeedX[i];
				c.speedY = a_weather->cloudLayerSpeedY[i];
				if (a_live && i < RE::Clouds::kTotalLayers)
					c.alpha = a_live->alphas[i];
				a_sky.clouds.push_back(std::move(c));
			}
		}

		/** 0 sunrise, 1 day, 2 sunset, 3 night: which of the weather's four colour and image space bands the hour is in, from the climate's timing. */
		int TimeBand(RE::TESClimate* a_climate, float a_hour)
		{
			if (!a_climate)
				return a_hour >= 6.0f && a_hour < 20.0f ? 1 : 3;
			const float t = a_hour * 6.0f;  // the climate counts in 10 minute steps
			const auto& timing = a_climate->timing;
			if (t >= timing.sunrise.begin && t < timing.sunrise.end)
				return 0;
			if (t >= timing.sunrise.end && t < timing.sunset.begin)
				return 1;
			if (t >= timing.sunset.begin && t < timing.sunset.end)
				return 2;
			return 3;
		}

		void AddMoon(SkySection& a_sky, const char* a_label, RE::Moon* a_moon)
		{
			if (!a_moon || !a_moon->moonNode) {
				a_sky.objects.push_back({ a_label, "(not loaded)" });
				return;
			}
			for (const auto& part : DescribeParts(a_moon->moonNode.get())) {
				for (const auto& t : part.textures) {
					if (!t.path.empty()) {
						a_sky.objects.push_back({ a_label, t.path });
						return;
					}
				}
			}
			a_sky.objects.push_back({ a_label, "(texture not readable)" });
		}
	}

	// ---- rays -----------------------------------------------------------------------------------------------------------

	WorldHit RayLand(const Ray& a_ray, float a_maxDistance)
	{
		WorldHit best;
		try {
			if (InInterior())
				return best;
			float          bestT = a_maxDistance;
			RE::TESObjectCELL* bestCell = nullptr;
			for (auto* cell : CellsAlong(a_ray, a_maxDistance)) {
				CellLand land;
				if (!LandOf(cell, land))
					continue;
				for (int q = 0; q < 4; ++q) {
					const GridHit g = RayQuad(a_ray, GridOf(land, q));
					if (g.hit && g.t > 0.0f && g.t <= bestT) {
						bestT = g.t;
						best.hit = true;
						best.distance = g.t;
						best.point = g.point;
						bestCell = cell;
					}
				}
			}
			if (best.hit)
				best.label = "land in " + CellLabel(bestCell);
		} catch (...) {
			spdlog::error("land ray test failed");
			best = {};
		}
		return best;
	}

	WorldHit RayWater(const Ray& a_ray, float a_maxDistance)
	{
		WorldHit best;
		try {
			auto* tes = RE::TES::GetSingleton();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!tes || !player)
				return best;
			auto*      playerCell = player->GetParentCell();
			const bool interior = playerCell && playerCell->IsInteriorCell();
			// march along the ray; a surface is crossed where the point goes from above the water to below it (or back)
			constexpr float kStep = 96.0f;
			float           prevT = 0.0f;
			float           prevSide = 0.0f;  // z above the surface at the previous sample, valid when prevHasWater
			bool            prevHasWater = false;
			for (float t = 0.0f; t <= a_maxDistance + kStep; t += kStep) {
				const float        at = std::min(t, a_maxDistance);
				const Vec3         p = a_ray.origin + a_ray.dir * at;
				RE::TESObjectCELL* cell = interior ? playerCell : tes->GetCell(ToNi(p));
				const auto         q = QueryWater(cell, ToNi(p));
				if (q.has) {
					const float side = p.z - q.height;
					if (prevHasWater && (prevSide > 0.0f) != (side > 0.0f) && prevSide > 0.0f) {  // from above only: the water seen from the air
						const float span = prevSide - side;
						const float cross = span > 1e-6f ? prevT + (at - prevT) * (prevSide / span) : at;
						const Vec3  hit = a_ray.origin + a_ray.dir * cross;
						// land standing higher than the surface at that spot hides the water
						float land = 0.0f;
						if (interior || !tes->GetLandHeight(ToNi(hit), land) || land <= q.height + 0.5f) {
							best.hit = true;
							best.distance = cross;
							best.point = hit;
							best.label = "water in " + CellLabel(cell);
							break;
						}
					}
					prevHasWater = true;
					prevSide = side;
				} else {
					prevHasWater = false;
				}
				prevT = at;
				if (at >= a_maxDistance)
					break;
			}
		} catch (...) {
			spdlog::error("water ray test failed");
			best = {};
		}
		return best;
	}

	// ---- terrain --------------------------------------------------------------------------------------------------------

	void FillTerrain(PickInfo& a_info, const Vec3& a_point)
	{
		a_info.valid = true;
		a_info.identity.kind = "terrain";
		a_info.method = "terrain";
		try {
			auto* tes = RE::TES::GetSingleton();
			auto* cell = tes ? tes->GetCell(ToNi(a_point)) : nullptr;
			CellLand land;
			if (!LandOf(cell, land)) {
				a_info.message = "no loaded land under that point";
				a_info.identity.kind = "object";
				a_info.valid = false;
				return;
			}
			auto& t = a_info.terrain;
			t.present = true;
			t.cellFormId = cell->GetFormID();
			t.cellName = Safe(cell->GetName());
			std::string source;
			t.cellEditorId = EditorIdOf(cell->GetFormID(), cell->GetFormEditorID(), source);
			t.cellX = land.x;
			t.cellY = land.y;
			t.worldspace = WorldspaceLabel(cell);
			t.point = a_point;
			t.quadrant = QuadrantOf(land.x, land.y, a_point.x, a_point.y);
			if (t.quadrant < 0) {
				a_info.message = "the point is outside its cell's land";
				t.present = false;
				a_info.valid = false;
				return;
			}
			t.quadrantName = QuadrantName(t.quadrant);
			const QuadGrid grid = GridOf(land, t.quadrant);
			int            vi = 0, vj = 0;
			NearestVertex(grid, a_point.x, a_point.y, vi, vj);
			t.vertexX = vi;
			t.vertexY = vj;
			t.vertexPosition = grid.Vertex(vi, vj);
			t.heightAtHit = QuadHeightAt(grid, a_point.x, a_point.y);

			// how our reading of the stored heights compares with the engine's own land height at the same spot
			float engineHeight = 0.0f;
			if (tes->GetLandHeight(ToNi(a_point), engineHeight))
				t.heightCheck = std::format("engine land height here {:.1f}, ours {:.1f} ({:+.2f}); the stored heights sit {:.1f} units {} the world's, measured against the engine at the quadrant's middle vertex", engineHeight, t.heightAtHit, t.heightAtHit - engineHeight, std::fabs(grid.zOffset), grid.zOffset >= 0.0f ? "below" : "above");
			else
				t.heightCheck = "the engine gave no land height here to compare with";

			const auto& data = *land.land->loadedData;
			t.blendMethod = "interpolated across the four vertices around the hit";
			const Bilinear b = BilinearAt(grid, a_point.x, a_point.y);

			// base texture of the quadrant, then the blended layers that count at this spot, strongest first
			if (auto* base = data.defQuadTextures[t.quadrant])
				t.layers.push_back(MakeLayer(base, "base"));
			struct Blend
			{
				int   layer;
				float raw;
			};
			std::vector<Blend> blends;
			int                zero = 0;
			for (int k = 0; k < 6; ++k) {
				if (!data.quadTextures[t.quadrant][k])
					continue;
				float raw[4];
				for (int dj = 0; dj < 2; ++dj)
					for (int di = 0; di < 2; ++di)
						raw[dj * 2 + di] = RawPercent(data, t.quadrant, (b.j + dj) * kQuadVerts + (b.i + di), k);
				const float value = BlendRaw(b, raw);
				if (value < 0.5f)
					++zero;
				else
					blends.push_back({ k, value });
			}
			std::sort(blends.begin(), blends.end(), [](const Blend& x, const Blend& y) { return x.raw > y.raw; });
			for (const auto& bl : blends) {
				TerrainLayer l = MakeLayer(data.quadTextures[t.quadrant][bl.layer], std::format("layer {}", bl.layer + 1));
				l.rawPercent = static_cast<int>(std::lround(bl.raw));
				l.percent = LayerPercent(bl.raw);
				t.layers.push_back(std::move(l));
			}
			// the strongest blended layer decides what the ground sounds like; with none, the base does
			const TerrainLayer* strongest = nullptr;
			for (const auto& l : t.layers)
				if (l.rawPercent >= 0 && (!strongest || l.percent > strongest->percent))
					strongest = &l;
			if (!strongest && !t.layers.empty())
				strongest = &t.layers.front();
			if (strongest)
				t.footstepMaterial = strongest->footstepMaterial;

			std::string notes;
			if (zero)
				notes += std::format("{} more layer(s) of this quadrant are 0 at this spot. ", zero);
			if (auto* engineTexture = tes->GetLandTexture(ToNi(a_point)))
				notes += std::format("The engine's own land texture lookup here: {}. ", DescribeForm(engineTexture));
			t.note = notes;

			a_info.identity.displayName = "land in " + CellLabel(cell);
			a_info.identity.cellFormId = t.cellFormId;
			a_info.identity.cellName = t.cellName;
			a_info.identity.cellEditorId = t.cellEditorId;
			a_info.identity.interior = false;
			a_info.identity.hasCoords = true;
			a_info.identity.cellX = land.x;
			a_info.identity.cellY = land.y;
			a_info.identity.position = a_point;
		} catch (const std::exception& e) {
			spdlog::error("terrain report failed: {}", e.what());
			a_info.message = "could not read the land here";
			a_info.valid = false;
		} catch (...) {
			spdlog::error("terrain report failed: unknown exception");
			a_info.message = "could not read the land here";
			a_info.valid = false;
		}
	}

	// ---- water ----------------------------------------------------------------------------------------------------------

	void FillWater(PickInfo& a_info, const Vec3& a_point, RE::TESObjectREFR* a_plane)
	{
		a_info.valid = true;
		a_info.identity.kind = "water";
		a_info.method = "water";
		try {
			auto*              tes = RE::TES::GetSingleton();
			auto*              player = RE::PlayerCharacter::GetSingleton();
			auto*              playerCell = player ? player->GetParentCell() : nullptr;
			RE::TESObjectCELL* cell = playerCell && playerCell->IsInteriorCell() ? playerCell : (tes ? tes->GetCell(ToNi(a_point)) : nullptr);
			auto&              w = a_info.water;
			w.present = true;
			w.point = a_point;
			if (cell) {
				w.cellFormId = cell->GetFormID();
				// as a person says it: the cell's name, else the worldspace and the grid ("Tamriel [4, -12]")
				const std::string named = Safe(cell->GetName());
				if (!named.empty()) {
					w.cellName = named;
				} else if (auto* coordinates = cell->IsExteriorCell() ? cell->GetCoordinates() : nullptr) {
					std::string worldName;
					if (auto* world = cell->GetRuntimeData().worldSpace) {
						std::string editorSource;
						worldName = EditorIdOf(world->GetFormID(), world->GetFormEditorID(), editorSource);
						if (worldName.empty())
							worldName = Safe(world->GetName());
					}
					w.cellName = std::format("{}{}[{}, {}]", worldName, worldName.empty() ? "" : " ", coordinates->cellX, coordinates->cellY);
				} else {
					w.cellName = CellLabel(cell);
				}
			}
			const WaterQuery q = QueryWater(cell, ToNi(a_point));
			w.hasCellHeight = q.has;
			w.cellHeight = q.height;
			w.height = a_plane ? a_point.z : (q.has ? q.height : a_point.z);

			std::string source;
			RE::TESWaterForm* type = WaterTypeOf(cell, source);
			if (a_plane) {
w.source = std::format("a placed water plane; the water type is the cell's: {}", source.empty() ? "none found" : source);
				w.planeRefFormId = a_plane->GetFormID();
				if (auto* base = a_plane->GetBaseObject()) {
					w.planeBaseFormId = base->GetFormID();
					w.planeName = Safe(a_plane->GetDisplayFullName());
					if (w.planeName.empty())
						w.planeName = Safe(base->GetFormEditorID());
				}
			} else {
				w.source = source.empty() ? "no water type found" : source;
			}
			if (a_plane) {
				w.cellVersusPlane = q.has ? std::format("this surface is a placed water plane at height {:.1f}; the cell also has its own water at {:.1f}", a_point.z, q.height) :
				                            std::format("this surface is a placed water plane at height {:.1f}; the cell has no water of its own here", a_point.z);
			} else {
				auto* plane = FindPlacedPlane(cell, ToNi(a_point));
				w.cellVersusPlane = q.has ? std::format("the cell's own water at {:.1f}; {}", q.height,
												plane ? std::format("a placed water plane ({}) is also near this spot, but the ray met the cell's water first", Hex8(plane->GetFormID())) :
														std::string("no placed water plane found near the hit")) :
				                            std::string("no water height found for the cell");
				if (plane) {
					w.planeRefFormId = plane->GetFormID();
					if (auto* base = plane->GetBaseObject())
						w.planeBaseFormId = base->GetFormID();
				}
			}
			w.note = std::format("engine water height queries: TES::GetWaterHeight {}{}, cell GetWaterHeight {} ({}), cell water level (XCLW) {}", q.engineValue,
				q.engineOk ? "" : " (not a height)", q.cellBool ? "true" : "false", q.cellValue, q.xclwOk ? std::format("{:.1f}", q.xclw) : std::string("none"));

			if (type) {
				w.type = DescribeRecord(type);
				w.typeName = Safe(type->GetName());
				for (int i = 0; i < 4; ++i) {
					const std::string path = DisplayPath(Safe(type->noiseTextures[i].textureName.c_str()));
					if (!path.empty())
						w.noiseTextures.push_back({ std::format("noise {}", i + 1), path });
				}
				if (auto* material = type->waterShaderMaterial) {
					const std::string live[4] = { TextureName(material->normalTexture1), TextureName(material->normalTexture2), TextureName(material->normalTexture3),
						TextureName(material->normalTexture4) };
					for (int i = 0; i < 4; ++i)
						if (!live[i].empty())
							w.liveTextures.push_back({ std::format("normal {}", i + 1), live[i] });
					if (auto reflection = TextureName(material->staticReflectionTexture); !reflection.empty())
						w.liveTextures.push_back({ "static reflection", reflection });
				}
				const auto& d = type->data;
				w.shallowColor = HexColor(d.shallowWaterColor.red, d.shallowWaterColor.green, d.shallowWaterColor.blue);
				w.deepColor = HexColor(d.deepWaterColor.red, d.deepWaterColor.green, d.deepWaterColor.blue);
				w.reflectionColor = HexColor(d.reflectionWaterColor.red, d.reflectionWaterColor.green, d.reflectionWaterColor.blue);
				w.values.push_back({ "alpha", std::format("{}", static_cast<int>(type->alpha)) });
				w.values.push_back({ "fresnel", std::format("{:.2f}", d.fresnelAmount) });
				w.values.push_back({ "reflection", std::format("{:.2f}", d.reflectionAmount) });
				w.values.push_back({ "refraction magnitude", std::format("{:.2f}", d.refractionMagnitude) });
				w.values.push_back({ "sun specular power", std::format("{:.1f}", d.sunSpecularPower) });
				w.values.push_back({ "flowmap", type->flags.any(RE::TESWaterForm::Flag::kEnableFlowmap) ? "on" : "off" });
				w.values.push_back({ "damages", type->flags.any(RE::TESWaterForm::Flag::kCauseDamage) ? "yes" : "no" });
				if (auto material = MaterialName(type->materialType); !material.empty())
					w.values.push_back({ "material", material });
			}
			a_info.identity.displayName = "water in " + w.cellName;
		} catch (const std::exception& e) {
			spdlog::error("water report failed: {}", e.what());
			a_info.message = "could not read the water here";
			a_info.valid = false;
		} catch (...) {
			spdlog::error("water report failed: unknown exception");
			a_info.message = "could not read the water here";
			a_info.valid = false;
		}
	}

	// ---- sky ------------------------------------------------------------------------------------------------------------

	void FillSky(PickInfo& a_info)
	{
		a_info.valid = true;
		a_info.identity.kind = "sky";
		a_info.method = "sky";
		try {
			auto* sky = RE::Sky::GetSingleton();
			if (!sky) {
				a_info.valid = false;
				a_info.message = "the sky is not loaded";
				return;
			}
			auto& s = a_info.sky;
			s.present = true;
			switch (sky->mode.get()) {
			case RE::Sky::Mode::kFull: s.mode = "exterior"; break;
			case RE::Sky::Mode::kSkyDomeOnly: s.mode = "interior with sky"; break;
			case RE::Sky::Mode::kInterior: s.mode = "interior"; break;
			default: s.mode = "no sky"; break;
			}
			s.hour = sky->currentGameHour;
			{
				const int minutes = static_cast<int>(std::lround(s.hour * 60.0f)) % (24 * 60);
				s.hourText = std::format("{:02d}:{:02d}", minutes / 60, minutes % 60);
			}
			s.currentWeather = DescribeRecord(sky->currentWeather);
			s.previousWeather = DescribeRecord(sky->lastWeather);
			s.transitionPercent = sky->lastWeather ? std::clamp(sky->currentWeatherPct * 100.0f, 0.0f, 100.0f) : 100.0f;
			s.weatherClass = WeatherClassText(sky->currentWeather);
			s.previousClass = WeatherClassText(sky->lastWeather);
			s.climate = DescribeRecord(sky->currentClimate);
			std::string forced;
			if (sky->overrideWeather)
				forced = "The weather is overridden (by a script or the console): " + DescribeForm(sky->overrideWeather) + ". ";

			AddClouds(s, sky->currentWeather, "current", sky->clouds);
			if (sky->lastWeather && sky->lastWeather != sky->currentWeather)
				AddClouds(s, sky->lastWeather, "previous", nullptr);

			if (sky->currentClimate) {
				const auto addSkyObject = [&](const char* a_label, const RE::TESTexture& a_texture) {
					const std::string path = Safe(a_texture.textureName.c_str());
					if (!path.empty())
						s.objects.push_back({ a_label, path });
				};
				addSkyObject("sun", sky->currentClimate->skyObjects[RE::TESClimate::SkyObjects::kSun]);
				addSkyObject("sun glare", sky->currentClimate->skyObjects[RE::TESClimate::SkyObjects::kSunGlare]);
			}
			AddMoon(s, "masser", sky->masser);
			AddMoon(s, "secunda", sky->secunda);
			if (sky->currentWeather) {
				if (auto* precipitation = sky->currentWeather->precipitationData) {
					const std::string path = Safe(precipitation->GetRuntimeData().particleTexture.textureName.c_str());
					s.objects.push_back({ "precip", path.empty() ? "(none)" : path });
				} else {
					s.objects.push_back({ "precip", "(this weather has no precipitation)" });
				}
			}

			auto*              player = RE::PlayerCharacter::GetSingleton();
			RE::TESObjectCELL* cell = player ? player->GetParentCell() : nullptr;
			if (cell)
				s.cellName = CellDescription(cell);
			// the image space: an interior cell names its own, outdoors the current weather has one per time band
			RE::TESImageSpace* imageSpace = nullptr;
			if (cell && cell->IsInteriorCell()) {
				if (auto* extra = cell->extraList.GetByType<RE::ExtraCellImageSpace>(); extra && extra->imageSpace) {
					imageSpace = extra->imageSpace;
					s.imageSpaceSource = "the interior cell's own image space";
				}
				if (auto* tmpl = cell->GetRuntimeData().lightingTemplate)
					s.lightingTemplate = DescribeForm(tmpl);
			}
			if (!imageSpace && sky->currentWeather) {
				const int         band = TimeBand(sky->currentClimate, s.hour);
				static const char* const kBands[4] = { "sunrise", "day", "sunset", "night" };
				imageSpace = sky->currentWeather->imageSpaces[band];
				s.imageSpaceSource = std::format("the current weather's {} image space (time band picked from the climate's sunrise and sunset times)", kBands[band]);
			}
			s.imageSpace = imageSpace ? DescribeForm(imageSpace) : std::string();
			if (sky->extLightingOverride)
				s.note += "An exterior lighting override is active: " + DescribeForm(sky->extLightingOverride) + ". ";
			s.note += forced;
			a_info.identity.displayName = "sky";
		} catch (const std::exception& e) {
			spdlog::error("sky report failed: {}", e.what());
			a_info.message = "could not read the sky";
			a_info.valid = false;
		} catch (...) {
			spdlog::error("sky report failed: unknown exception");
			a_info.message = "could not read the sky";
			a_info.valid = false;
		}
	}
}
