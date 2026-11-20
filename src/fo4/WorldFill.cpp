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

		std::string FullName(RE::TESForm* a_form) { return a_form ? std::string(RE::TESFullName::GetFullName(*a_form)) : std::string(); }

		const char* const kTextureSetLabels[8] = { "diffuse", "normal", "glow / skin tint", "height (parallax)", "environment (cube map)", "environment mask", "multilayer (inner layer)", "specular / smoothness" };

		/** Colours are stored as four packed bytes, red in the lowest. */
		std::string PackedColour(std::uint32_t a_packed) { return HexColor(static_cast<int>(a_packed & 0xFF), static_cast<int>((a_packed >> 8) & 0xFF), static_cast<int>((a_packed >> 16) & 0xFF)); }

		// ---- cells and land -----------------------------------------------------------------------------------------------

		struct CellLand
		{
			RE::TESObjectCELL* cell = nullptr;
			RE::TESObjectLAND* land = nullptr;
			int                x = 0;
			int                y = 0;
		};

		bool InInterior()
		{
			auto* tes = RE::TES::GetSingleton();
			return tes && tes->interiorCell;
		}

		bool LandOf(RE::TESObjectCELL* a_cell, CellLand& a_out)
		{
			if (!a_cell || !a_cell->IsExterior())
				return false;
			auto* land = a_cell->cellLand;
			if (!land || !land->loadedData)
				return false;
			auto* coordinates = a_cell->GetCoordinates();
			if (!coordinates)
				return false;
			a_out = { a_cell, land, coordinates->cellX, coordinates->cellY };
			return true;
		}

		/**
		 * The height the stored vertex heights need added to be the world's. Fallout 4 has no engine call for the land height, so this is
		 * what `lookingat ground-probe` measures against the player standing on the ground: see TerrainProbe.
		 */
		float LandZOffset(const RE::TESObjectLAND::LoadedLandData& a_data) { return a_data.baseHeight; }

		QuadGrid GridOf(const CellLand& a_land, int a_quadrant)
		{
			QuadGrid grid;
			QuadrantOrigin(a_land.x, a_land.y, a_quadrant, grid.originX, grid.originY);
			grid.heights = a_land.land->loadedData->heights + static_cast<std::size_t>(a_quadrant) * (kQuadVerts * kQuadVerts);
			grid.zOffset = LandZOffset(*a_land.land->loadedData);
			return grid;
		}

		std::string WorldspaceLabel(RE::TESObjectCELL* a_cell)
		{
			auto* world = a_cell && a_cell->IsExterior() ? a_cell->worldSpace : nullptr;
			if (!world)
				return {};
			const std::string editor = Safe(world->GetFormEditorID());
			const std::string name = FullName(world);
			if (name.empty())
				return editor;
			return editor.empty() ? name : name + " (" + editor + ")";
		}

		std::string CellLabel(RE::TESObjectCELL* a_cell)
		{
			if (!a_cell)
				return "(no cell)";
			std::string name = FullName(a_cell);
			if (!name.empty())
				return name;
			if (auto* coordinates = a_cell->GetCoordinates()) {
				std::string label;
				if (auto* world = a_cell->worldSpace) {
					label = FullName(world);
					if (label.empty())
						label = Safe(world->GetFormEditorID());
				}
				return std::format("{}[{}, {}]", label.empty() ? std::string("Cell ") : label + " ", coordinates->cellX, coordinates->cellY);
			}
			const std::string editor = Safe(a_cell->GetFormEditorID());
			return editor.empty() ? "(unnamed cell)" : editor;
		}

		/** The distinct loaded exterior cells a ray passes over, sampled along it. */
		std::vector<RE::TESObjectCELL*> CellsAlong(const Ray& a_ray, float a_maxDistance)
		{
			std::vector<RE::TESObjectCELL*> cells;
			constexpr float                 kStep = 1024.0f;
			for (float t = 0.0f;; t += kStep) {
				const float at = std::min(t, a_maxDistance);
				const Vec3  p = a_ray.origin + a_ray.dir * at;
				if (auto* cell = ExteriorCellAt(p.x, p.y); cell && std::find(cells.begin(), cells.end(), cell) == cells.end())
					cells.push_back(cell);
				if (at >= a_maxDistance)
					break;
			}
			return cells;
		}

		std::string MaterialName(RE::BGSMaterialType* a_material)
		{
			if (!a_material)
				return {};
			const std::string name = Safe(a_material->materialName.c_str());
			const std::string editor = Safe(a_material->GetFormEditorID());
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
			l.name = FullName(a_ltex);
			l.addedBy = record.addedBy;
			l.lastChangedBy = record.lastChangedBy;
			l.chain = record.chain;
			// Fallout 4 land textures name a texture set (TXST), and that set names a material file (.bgsm) as well as its images
			if (auto* set = a_ltex->textureSet) {
				// the engine's built-in default land texture carries a set with no form id of its own: nothing to name
				if (set->GetFormID() != 0)
					l.textures.push_back({ "texture set", DescribeForm(set) });
				if (const std::string material = DisplayPath(Safe(set->materialName.c_str())); !material.empty())
					l.textures.push_back({ "material file", material });
				for (int i = 0; i < 8; ++i) {
					const std::string path = DisplayPath(Safe(set->textures[i].textureName.c_str()));
					if (!path.empty())
						l.textures.push_back({ kTextureSetLabels[i], path });
				}
			}
			for (auto* grass : a_ltex->textureGrassList) {
				if (!grass)
					continue;
				// the grass record's class is not in the game library; its form header is, so it is read as a form
				auto* form = reinterpret_cast<RE::TESForm*>(grass);
				l.grasses.push_back(DescribeForm(form));
				if (const std::string editor = Safe(form->GetFormEditorID()); !editor.empty())
					l.grassNames.push_back(editor);
			}
			l.footstepMaterial = MaterialName(a_ltex->materialType);
			return l;
		}

		float RawPercent(const RE::TESObjectLAND::LoadedLandData& a_data, int a_quadrant, int a_vertex, int a_layer)
		{
			return static_cast<float>(a_data.percentArrays[a_quadrant][a_vertex][a_layer]);
		}

		constexpr int kFo4LayerFull = 255;

		float Fo4Percent(float a_raw) { return std::clamp(a_raw * 100.0f / static_cast<float>(kFo4LayerFull), 0.0f, 100.0f); }

		// ---- water --------------------------------------------------------------------------------------------------------

		struct WaterQuery
		{
			bool  has = false;
			float height = 0.0f;
			float cellValue = 0.0f;    // the cell's own water level (XCLW)
			bool  cellOk = false;
			float worldValue = 0.0f;   // the worldspace's default water height
			bool  worldOk = false;
			bool  flagged = false;     // the cell says it has water
		};

		WaterQuery QueryWater(RE::TESObjectCELL* a_cell)
		{
			WaterQuery q;
			if (!a_cell)
				return q;
			q.flagged = a_cell->HasWater();
			q.cellValue = a_cell->waterHeight;
			q.cellOk = Plausible(q.cellValue);
			if (a_cell->IsExterior() && a_cell->worldSpace) {
				q.worldValue = a_cell->worldSpace->defaultWaterHeight;
				q.worldOk = Plausible(q.worldValue);
			}
			if (!q.flagged)
				return q;
			if (q.cellOk) {
				q.has = true;
				q.height = q.cellValue;
			} else if (q.worldOk) {
				q.has = true;
				q.height = q.worldValue;
			}
			return q;
		}

		RE::TESWaterForm* WaterTypeOf(RE::TESObjectCELL* a_cell, std::string& a_source)
		{
			if (!a_cell)
				return nullptr;
			if (a_cell->extraList) {
				if (auto* extra = a_cell->extraList->GetByType<RE::ExtraCellWaterType>(); extra && extra->water) {
					a_source = "the cell's own water type";
					return extra->water;
				}
			}
			if (a_cell->IsExterior() && a_cell->worldSpace) {
				if (auto* type = a_cell->worldSpace->GetWaterType()) {
					a_source = "the worldspace's water";
					return type;
				}
			}
			if (auto* type = a_cell->GetWaterType()) {
				a_source = "the game's default water";
				return type;
			}
			return nullptr;
		}

		std::string TextureName(const RE::NiPointer<RE::NiTexture>& a_texture) { return a_texture ? DisplayPath(Safe(a_texture->name.c_str())) : std::string(); }

		// ---- sky ----------------------------------------------------------------------------------------------------------

		std::string WeatherClassText(RE::TESWeather* a_weather)
		{
			if (!a_weather)
				return {};
			const auto  flags = static_cast<std::uint8_t>(a_weather->weatherData[static_cast<int>(RE::TESWeather::WeatherData::kFlags)]);
			std::string out;
			const auto  add = [&](RE::TESWeather::WeatherDataFlags a_flag, const char* a_name) {
				if (flags & static_cast<std::uint8_t>(a_flag))
					out += (out.empty() ? "" : ", ") + std::string(a_name);
			};
			using F = RE::TESWeather::WeatherDataFlags;
			add(F::kPleasant, "pleasant");
			add(F::kCloudy, "cloudy");
			add(F::kRainy, "rainy");
			add(F::kSnow, "snow");
			add(F::kPermAurora, "permanent aurora");
			add(F::kAuroraFollowsSun, "aurora follows the sun");
			return out;
		}

		void AddClouds(SkySection& a_sky, RE::TESWeather* a_weather, const char* a_which, RE::Clouds* a_live)
		{
			if (!a_weather)
				return;
			const std::uint32_t layers = std::min<std::uint32_t>(a_weather->numCloudLayers, 32);
			for (std::uint32_t i = 0; i < layers; ++i) {
				if (a_weather->cloudLayerDisabledBits & (1u << i))
					continue;
				CloudLayerInfo c;
				c.weather = a_which;
				c.index = static_cast<int>(i);
				c.texture = DisplayPath(Safe(a_weather->cloudTexture[i].textureName.c_str()));
				c.speedX = a_weather->cloudLayerSpeedX[i];
				c.speedY = a_weather->cloudLayerSpeedY[i];
				if (a_live && i < RE::Clouds::kMaxLayers)
					c.alpha = a_live->alphas[i];
				a_sky.clouds.push_back(std::move(c));
			}
		}

		/** 0 sunrise, 1 day, 2 sunset, 3 night: which of the weather's image space bands the hour is in, from the climate's timing (10 minute steps). */
		int TimeBand(RE::TESClimate* a_climate, float a_hour)
		{
			if (!a_climate)
				return a_hour >= 6.0f && a_hour < 20.0f ? 1 : 3;
			const float t = a_hour * 6.0f;
			if (t >= a_climate->sunriseBegin && t < a_climate->sunriseEnd)
				return 0;
			if (t >= a_climate->sunriseEnd && t < a_climate->sunsetBegin)
				return 1;
			if (t >= a_climate->sunsetBegin && t < a_climate->sunsetEnd)
				return 2;
			return 3;
		}
	}

	// ---- cell lookup ------------------------------------------------------------------------------------------------------

	RE::TESObjectCELL* ExteriorCellAt(float a_x, float a_y)
	{
		auto* tes = RE::TES::GetSingleton();
		if (!tes || tes->interiorCell || !tes->gridCells)
			return nullptr;
		const int      gx = static_cast<int>(std::floor(a_x / kCellSize));
		const int      gy = static_cast<int>(std::floor(a_y / kCellSize));
		const auto*    grid = tes->gridCells;
		const auto     count = static_cast<std::size_t>(grid->dimension) * grid->dimension;
		if (!grid->gridCell || grid->dimension == 0 || grid->dimension > 64)
			return nullptr;
		for (std::size_t i = 0; i < count; ++i) {
			auto* cell = grid->gridCell[i].cell;
			if (!cell || !cell->IsExterior())
				continue;
			if (auto* coordinates = cell->GetCoordinates(); coordinates && coordinates->cellX == gx && coordinates->cellY == gy)
				return cell;
		}
		return nullptr;
	}

	// ---- rays -------------------------------------------------------------------------------------------------------------

	WorldHit RayLand(const Ray& a_ray, float a_maxDistance)
	{
		WorldHit best;
		try {
			if (InInterior())
				return best;
			float              bestT = a_maxDistance;
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
			auto*      tes = RE::TES::GetSingleton();
			auto*      player = RE::PlayerCharacter::GetSingleton();
			if (!tes || !player)
				return best;
			auto*      interiorCell = tes->interiorCell;
			// march along the ray; a surface is crossed where the point goes from above the water to below it
			constexpr float kStep = 96.0f;
			float           prevT = 0.0f;
			float           prevSide = 0.0f;
			bool            prevHasWater = false;
			for (float t = 0.0f; t <= a_maxDistance + kStep; t += kStep) {
				const float        at = std::min(t, a_maxDistance);
				const Vec3         p = a_ray.origin + a_ray.dir * at;
				RE::TESObjectCELL* cell = interiorCell ? interiorCell : ExteriorCellAt(p.x, p.y);
				const auto         q = QueryWater(cell);
				if (q.has) {
					const float side = p.z - q.height;
					if (prevHasWater && prevSide > 0.0f && side <= 0.0f) {  // from above only: the water seen from the air
						const float span = prevSide - side;
						const float cross = span > 1e-6f ? prevT + (at - prevT) * (prevSide / span) : at;
						const Vec3  hit = a_ray.origin + a_ray.dir * cross;
						// land standing higher than the surface at that spot hides the water
						bool hidden = false;
						if (!interiorCell) {
							CellLand land;
							if (LandOf(cell, land)) {
								const int quadrant = QuadrantOf(land.x, land.y, hit.x, hit.y);
								if (quadrant >= 0 && QuadHeightAt(GridOf(land, quadrant), hit.x, hit.y) > q.height + 0.5f)
									hidden = true;
							}
						}
						if (!hidden) {
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

	// ---- terrain ----------------------------------------------------------------------------------------------------------

	void FillTerrain(PickInfo& a_info, const Vec3& a_point)
	{
		a_info.valid = true;
		a_info.identity.kind = "terrain";
		a_info.method = "terrain";
		try {
			auto*    cell = ExteriorCellAt(a_point.x, a_point.y);
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
			t.cellName = FullName(cell);
			t.cellEditorId = Safe(cell->GetFormEditorID());
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
			t.heightCheck = std::format("land height from the stored vertex heights (plus the cell's base height {:.1f}); Fallout 4 has no engine call to compare with", grid.zOffset);

			const auto& data = *land.land->loadedData;
			t.blendMethod = "interpolated across the four vertices around the hit";
			const Bilinear b = BilinearAt(grid, a_point.x, a_point.y);

			// the base texture of the quadrant, then the blended layers that count at this spot, strongest first
			if (auto* base = data.defQuadTexture[t.quadrant])
				t.layers.push_back(MakeLayer(base, "base"));
			struct Blend
			{
				int   layer;
				float raw;
			};
			std::vector<Blend> blends;
			int                zero = 0, used = 0;
			for (int k = 0; k < 12; ++k) {
				if (!data.quadTextureArray[t.quadrant][k])
					continue;
				++used;
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
				TerrainLayer l = MakeLayer(data.quadTextureArray[t.quadrant][bl.layer], std::format("layer {}", bl.layer + 1));
				l.rawPercent = static_cast<int>(std::lround(bl.raw));
				l.percent = Fo4Percent(bl.raw);
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

			std::string notes = std::format("This quadrant has {} blended layer(s) set (Fallout 4 allows 12 per quadrant, Skyrim 6", used);
			if (zero)
				notes += std::format("; {} of them are 0 at this spot", zero);
			notes += "). ";
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

	// ---- water ------------------------------------------------------------------------------------------------------------

	void FillWater(PickInfo& a_info, const Vec3& a_point)
	{
		a_info.valid = true;
		a_info.identity.kind = "water";
		a_info.method = "water";
		try {
			auto*              tes = RE::TES::GetSingleton();
			RE::TESObjectCELL* cell = tes && tes->interiorCell ? tes->interiorCell : ExteriorCellAt(a_point.x, a_point.y);
			auto&              w = a_info.water;
			w.present = true;
			w.point = a_point;
			if (cell) {
				w.cellFormId = cell->GetFormID();
				w.cellName = CellLabel(cell);
			}
			const WaterQuery q = QueryWater(cell);
			w.hasCellHeight = q.has;
			w.cellHeight = q.height;
			w.height = q.has ? q.height : a_point.z;

			std::string       source;
			RE::TESWaterForm* type = WaterTypeOf(cell, source);
			w.source = source.empty() ? "no water type found" : source;
			w.cellVersusPlane = q.has ? std::format("the cell's water level {:.1f}", q.height) : std::string("no water height found for the cell");
			w.note = std::format("water level sources: cell water level (XCLW) {}, worldspace default {}, the cell is {}flagged as having water", q.cellOk ? std::format("{:.1f}", q.cellValue) : std::string("none"),
				q.worldOk ? std::format("{:.1f}", q.worldValue) : std::string("none"), q.flagged ? "" : "not ");
			if (cell && cell->IsExterior())
				w.note += std::format("; the engine's own exterior water height call says {:.1f}", cell->GetExteriorWaterHeight());

			if (type) {
				w.type = DescribeRecord(type);
				w.typeName = FullName(type);
				// Fallout 4 water types rarely carry a name or a readable editor id: a plain title; the record's id is in Details
				if (w.typeName.empty() && w.type.editorId.empty())
					w.typeName = "Water";
				const RE::TESTexture* noise[3] = { &type->noiseTexture0, &type->noiseTexture1, &type->noiseTexture2 };
				for (int i = 0; i < 3; ++i) {
					const std::string path = DisplayPath(Safe(noise[i]->textureName.c_str()));
					if (!path.empty())
						w.noiseTextures.push_back({ std::format("noise {}", i + 1), path });
				}
				const std::string live[3] = { TextureName(type->niNoiseTexture0), TextureName(type->niNoiseTexture1), TextureName(type->niNoiseTexture2) };
				for (int i = 0; i < 3; ++i)
					if (!live[i].empty())
						w.liveTextures.push_back({ std::format("noise {}", i + 1), live[i] });
				const auto& d = type->data;
				w.shallowColor = PackedColour(d.shallowWaterColor);
				w.deepColor = PackedColour(d.deepWaterColor);
				w.reflectionColor = PackedColour(d.reflectionWaterColor);
				w.values.push_back({ "alpha", std::format("{}", static_cast<int>(type->alpha)) });
				w.values.push_back({ "max depth", std::format("{:.1f}", d.maxDepth) });
				w.values.push_back({ "shallow / deep alpha", std::format("{:.2f} / {:.2f}", d.shallowAlpha, d.deepAlpha) });
				w.values.push_back({ "fresnel", std::format("{:.2f}", d.fresnelAmount) });
				w.values.push_back({ "reflection", std::format("{:.2f}", d.reflectionAmount) });
				w.values.push_back({ "normal magnitude", std::format("{:.2f}", d.normalMagnitude) });
				w.values.push_back({ "sun specular power", std::format("{:.1f}", d.sunSpecularPower) });
				w.values.push_back({ "screen-space reflections", d.ssrEnabled ? "on" : "off" });
				w.values.push_back({ "underwater fog", std::format("{} at {:.0f}-{:.0f}", PackedColour(d.underwaterFogColor), d.underwaterFogDistNear, d.underwaterFogDistFar) });
				if (auto material = MaterialName(type->materialType); !material.empty())
					w.values.push_back({ "material", material });
				if (type->imageSpace)
					w.values.push_back({ "image space", DescribeForm(type->imageSpace) });
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

	// ---- sky --------------------------------------------------------------------------------------------------------------

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
					const std::string path = DisplayPath(Safe(a_texture.textureName.c_str()));
					if (!path.empty())
						s.objects.push_back({ a_label, path });
				};
				addSkyObject("sun", sky->currentClimate->skyObjects[static_cast<int>(RE::TESClimate::TextureType::kSun)]);
				addSkyObject("sun glare", sky->currentClimate->skyObjects[static_cast<int>(RE::TESClimate::TextureType::kGlare)]);
			}
			if (sky->currentWeather) {
				if (auto* precipitation = sky->currentWeather->precipitationData) {
					const std::string path = DisplayPath(Safe(precipitation->particleTexture.textureName.c_str()));
					s.objects.push_back({ "precip", path.empty() ? "(none)" : path });
				} else {
					s.objects.push_back({ "precip", "(this weather has no precipitation)" });
				}
			}

			auto*              player = RE::PlayerCharacter::GetSingleton();
			RE::TESObjectCELL* cell = player ? player->GetParentCell() : nullptr;
			if (cell)
				s.cellName = CellDescription(cell);
			// the image space: an interior names a lighting template; outdoors the current weather has one per time band
			RE::TESImageSpace* imageSpace = nullptr;
			if (cell && cell->IsInterior()) {
				if (auto* tmpl = cell->lightingTemplate)
					s.lightingTemplate = DescribeForm(reinterpret_cast<RE::TESForm*>(tmpl));
				s.imageSpaceSource = "an interior: the cell's own image space is not read in Fallout 4";
			} else if (sky->currentWeather) {
				const int          band = TimeBand(sky->currentClimate, s.hour);
				static const char* const kBands[4] = { "sunrise", "day", "sunset", "night" };
				imageSpace = sky->currentWeather->imageSpace[band];
				s.imageSpaceSource = std::format("the current weather's {} image space (time band picked from the climate's sunrise and sunset times)", kBands[band]);
			}
			s.imageSpace = imageSpace ? DescribeForm(reinterpret_cast<RE::TESForm*>(imageSpace)) : std::string();
			if (sky->extLightingOverride)
				s.note += "An exterior lighting override is active: " + DescribeForm(reinterpret_cast<RE::TESForm*>(sky->extLightingOverride)) + ". ";
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

	// ---- developer aid ----------------------------------------------------------------------------------------------------

	std::string TerrainProbe()
	{
		std::string out;
		try {
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return "no player";
			}
			const auto pos = player->GetPosition();
			auto*      cell = ExteriorCellAt(pos.x, pos.y);
			out = std::format("player at ({:.1f}, {:.1f}, {:.1f}); exterior cell found: {}\n", pos.x, pos.y, pos.z, cell ? CellLabel(cell) : std::string("none"));
			CellLand land;
			if (!LandOf(cell, land)) {
				out += "no loaded land data for that cell";
				spdlog::info("ground probe: {}", out);
				return out;
			}
			const auto& d = *land.land->loadedData;
			out += std::format("land cell {},{}; baseHeight {:.2f}; heightExtends ({:.2f}, {:.2f}); loaded data cell {},{}\n", land.x, land.y, d.baseHeight, d.heightExtends.x, d.heightExtends.y, d.cellX, d.cellY);
			const int quadrant = QuadrantOf(land.x, land.y, pos.x, pos.y);
			out += std::format("quadrant by position: {} ({})\n", quadrant, quadrant >= 0 ? QuadrantName(quadrant) : "?");
			for (int q = 0; q < 4; ++q) {
				QuadGrid g;
				QuadrantOrigin(land.x, land.y, q, g.originX, g.originY);
				g.heights = d.heights + static_cast<std::size_t>(q) * (kQuadVerts * kQuadVerts);
				const bool inside = pos.x >= g.originX && pos.x <= g.originX + kQuadSize && pos.y >= g.originY && pos.y <= g.originY + kQuadSize;
				float      lo = g.heights[0], hi = g.heights[0];
				for (int i = 1; i < kQuadVerts * kQuadVerts; ++i) {
					lo = std::min(lo, g.heights[i]);
					hi = std::max(hi, g.heights[i]);
				}
				const char* geom = d.geom[q] ? "yes" : "no";
				float       meshZ = 0.0f;
				if (d.mesh[q])
					meshZ = d.mesh[q]->world.translate.z;
				out += std::format("  quadrant {} origin ({:.0f}, {:.0f}) stored heights {:.1f}..{:.1f}; mesh node world z {:.1f}; geometry {}{}", q, g.originX, g.originY, lo, hi, meshZ, geom, inside ? " <- player here" : "");
				if (inside) {
					const float raw = QuadHeightAt(g, pos.x, pos.y);
					out += std::format("\n    stored height at the player's spot {:.1f}; the player stands at z {:.1f}: stored + baseHeight = {:.1f}; stored + mesh node z = {:.1f}", raw, pos.z, raw + d.baseHeight, raw + meshZ);
				}
				out += "\n";
			}
			if (quadrant >= 0) {
				QuadGrid g = GridOf(land, quadrant);
				const Bilinear b = BilinearAt(g, pos.x, pos.y);
				int      maxRaw = 0;
				for (int v = 0; v < kQuadVerts * kQuadVerts; ++v)
					for (int k = 0; k < 12; ++k)
						maxRaw = std::max<int>(maxRaw, d.percentArrays[quadrant][v][k]);
				out += std::format("blend bytes: largest stored value in this quadrant {}; at the nearest vertices:", maxRaw);
				for (int k = 0; k < 12; ++k)
					if (d.quadTextureArray[quadrant][k])
						out += std::format(" L{}={}", k + 1, d.percentArrays[quadrant][b.j * kQuadVerts + b.i][k]);
				out += "\n";
			}
		} catch (...) {
			out += "probe failed with an exception";
		}
		spdlog::info("ground probe:\n{}", out);
		return out;
	}

	std::string FindWaterNear()
	{
		try {
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* tes = RE::TES::GetSingleton();
			if (!player || !tes || tes->interiorCell || !tes->gridCells || !tes->gridCells->gridCell)
				return "not in an exterior with loaded land";
			const auto  pos = player->GetPosition();
			float       bestD = std::numeric_limits<float>::max();
			Vec3        best;
			float       bestLevel = 0.0f;
			std::string bestCell;
			const auto  count = static_cast<std::size_t>(tes->gridCells->dimension) * tes->gridCells->dimension;
			for (std::size_t c = 0; c < count; ++c) {
				auto*    cell = tes->gridCells->gridCell[c].cell;
				CellLand land;
				if (!LandOf(cell, land))
					continue;
				const auto q = QueryWater(cell);
				if (!q.has)
					continue;
				for (int quadrant = 0; quadrant < 4; ++quadrant) {
					const QuadGrid grid = GridOf(land, quadrant);
					for (int j = 0; j < kQuadVerts; ++j)
						for (int i = 0; i < kQuadVerts; ++i) {
							const Vec3 v = grid.Vertex(i, j);
							if (v.z > q.height - 40.0f)
								continue;
							const float dx = v.x - pos.x, dy = v.y - pos.y;
							const float d = std::sqrt(dx * dx + dy * dy);
							if (d < bestD) {
								bestD = d;
								best = v;
								bestLevel = q.height;
								bestCell = CellLabel(cell);
							}
						}
				}
			}
			if (bestD == std::numeric_limits<float>::max())
				return "no loaded land lies 40 or more units under its cell's water";
			return std::format("nearest land under water: ({:.0f}, {:.0f}) ground z {:.0f}, water level {:.0f}, {:.0f} units away, in {}", best.x, best.y, best.z, bestLevel, bestD, bestCell);
		} catch (...) {
			return "water search failed";
		}
	}
}
