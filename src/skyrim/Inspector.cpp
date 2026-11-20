// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
#include "PCH.h"

#include "Inspector.h"

#include "Diagnostics.h"
#include "PickScan.h"
#include "ReportFill.h"
#include "TextureGather.h"
#include "TextureInfo.h"
#include "WorldFill.h"

namespace ti
{
	namespace
	{
		constexpr std::size_t kMaxVertexBytes = 64u * 1024u * 1024u;

		// ---- names ------------------------------------------------------------------------------------------------

		std::string FeatureName(int a_feature)
		{
			switch (a_feature) {
			case 0: return "Default";
			case 1: return "EnvironmentMap";
			case 2: return "GlowMap";
			case 3: return "Parallax";
			case 4: return "FaceGen";
			case 5: return "FaceGenRGBTint";
			case 6: return "HairTint";
			case 7: return "ParallaxOcc";
			case 8: return "MultiTexLand";
			case 9: return "LODLand";
			case 11: return "MultilayerParallax";
			case 12: return "TreeAnim";
			case 14: return "MultiIndexTriShapeSnow";
			case 15: return "LODObjectsHD";
			case 16: return "Eye";
			case 17: return "Cloud";
			case 18: return "LODLandNoise";
			case 19: return "MultiTexLandLODBlend";
			case 32: return "TruePBR";
			case 33: return "TruePBR (land)";
			default: return std::format("feature {}", a_feature);
			}
		}

		const char* LitSlotLabel(int a_slot)
		{
			switch (a_slot) {
			case 0: return "diffuse";
			case 1: return "normal";
			case 2: return "glow / skin tint";
			case 3: return "height (parallax)";
			case 4: return "environment (cube map)";
			case 5: return "environment mask";
			case 6: return "multilayer (inner layer)";
			case 7: return "backlight / specular mask";
			default: return "unused";
			}
		}

		const char* PbrSlotLabel(int a_slot)
		{
			switch (a_slot) {
			case 0: return "albedo (diffuse)";
			case 1: return "normal";
			case 2: return "emissive";
			case 3: return "displacement";
			case 4: return "slot 4";
			case 5: return "RMAOS (rough/metal/AO/spec)";
			case 6: return "features 1";
			case 7: return "features 0";
			default: return "unused";
			}
		}

		using F = RE::BSShaderProperty::EShaderPropertyFlag;

		struct FlagName
		{
			F           flag;
			const char* name;
		};

		// Only flags that matter when judging or making textures; shadow, z-buffer and similar bookkeeping flags are left out.
		constexpr FlagName kFlagNames[] = {
			{ F::kSpecular, "Specular" },
			{ F::kSkinned, "Skinned" },
			{ F::kVertexAlpha, "VertexAlpha" },
			{ F::kVertexColors, "VertexColors" },
			{ F::kEnvMap, "EnvMap" },
			{ F::kGlowMap, "GlowMap" },
			{ F::kParallax, "Parallax" },
			{ F::kParallaxOcclusion, "ParallaxOcclusion" },
			{ F::kMultiLayerParallax, "MultiLayerParallax" },
			{ F::kModelSpaceNormals, "ModelSpaceNormals" },
			{ F::kFace, "Face" },
			{ F::kFaceGenRGBTint, "FaceGenRGBTint" },
			{ F::kHairTint, "HairTint" },
			{ F::kEyeReflect, "EyeReflect" },
			{ F::kRefraction, "Refraction" },
			{ F::kTwoSided, "TwoSided" },
			{ F::kDecal, "Decal" },
			{ F::kDynamicDecal, "DynamicDecal" },
			{ F::kSoftLighting, "SoftLighting" },
			{ F::kRimLighting, "RimLighting" },
			{ F::kBackLighting, "BackLighting" },
			{ F::kAnisotropicLighting, "Anisotropic" },
			{ F::kSnow, "Snow" },
			{ F::kMultiIndexSnow, "MultiIndexSnow" },
			{ F::kTreeAnim, "TreeAnim" },
			{ F::kBillboard, "Billboard" },
			{ F::kProjectedUV, "ProjectedUV" },
			{ F::kRemappableTextures, "RemappableTextures" },
			{ F::kOwnEmit, "OwnEmit" },
			{ F::kExternalEmittance, "ExternalEmittance" },
			{ F::kMultiTextureLandscape, "MultiTextureLandscape" },
			{ F::kLODObjects, "LODObjects" },
			{ F::kLODLandscape, "LODLandscape" },
			{ F::kGrayscaleToPaletteColor, "GrayscaleToPaletteColor" },
			{ F::kGrayscaleToPaletteAlpha, "GrayscaleToPaletteAlpha" },
			{ F::kFalloff, "Falloff" },
			{ F::kSoftEffect, "SoftEffect" },
			{ F::kEffectLighting, "EffectLighting" },
			{ F::kPremultAlpha, "PremultAlpha" },
		};

		std::string NormalisedPath(std::string_view a_path)
		{
			std::string out(a_path);
			for (auto& c : out)
				c = c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return out;
		}

		std::string Safe(const char* a_text) { return a_text ? std::string(a_text) : std::string(); }

		// ---- CPU mesh read ----------------------------------------------------------------------------------------

		/** The only function that touches engine buffers it does not own: a bad pointer returns false instead of crashing. */
		bool SafeCopy(void* a_dst, const void* a_src, std::size_t a_bytes)
		{
			__try {
				std::memcpy(a_dst, a_src, a_bytes);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		struct MeshTest
		{
			bool        tested = false;
			std::string reason;
			bool        hit = false;
			float       t = 0.0f;
			Vec3        point;
			int         triangle = -1;
		};

		MeshTest TestTriangles(RE::BSGeometry* a_geometry, const Ray& a_ray)
		{
			MeshTest result;
			auto&    runtime = a_geometry->GetGeometryRuntimeData();
			auto*    triShape = a_geometry->AsTriShape();
			if (!triShape) {
				result.reason = "not a tri shape";
				return result;
			}
			if (a_geometry->GetType().get() == RE::BSGeometry::Type::kDynamicTriShape) {
				result.reason = "dynamic tri shape";
				return result;
			}
			auto* data = runtime.rendererData;
			if (!data || !data->rawVertexData || !data->rawIndexData) {
				result.reason = "no CPU copy of the mesh";
				return result;
			}
			auto desc = data->vertexDesc;
			if (runtime.skinInstance || desc.HasFlag(RE::BSGraphics::Vertex::VF_SKINNED)) {
				result.reason = "skinned (bind pose is not the world pose)";
				return result;
			}
			if (!desc.HasFlag(RE::BSGraphics::Vertex::VF_VERTEX)) {
				result.reason = "no positions";
				return result;
			}
			const auto&         tri = triShape->GetTrishapeRuntimeData();
			const std::uint32_t vertexCount = tri.vertexCount;
			const std::uint32_t triangleCount = tri.triangleCount;
			const std::uint32_t stride = desc.GetSize();
			// Skyrim's vertex layout always stores positions as three full floats (plus a bitangent float), whatever the
			// full-precision flag says; that flag only means half floats in Fallout 4. Reading halves here gave nonsense
			// triangles that every part "hit" at one spot (WalkwayCWallGate01, 2026-11-15: the rope won over the post in front).
			const bool          fullPrecision = true;
			if (vertexCount == 0 || triangleCount == 0 || stride < (fullPrecision ? 12u : 6u) ||
				static_cast<std::size_t>(stride) * vertexCount > kMaxVertexBytes) {
				result.reason = "mesh size not readable";
				return result;
			}

			std::vector<std::uint8_t>  vertexBytes(static_cast<std::size_t>(stride) * vertexCount);
			std::vector<std::uint16_t> indices(static_cast<std::size_t>(triangleCount) * 3u);
			if (!SafeCopy(vertexBytes.data(), data->rawVertexData, vertexBytes.size()) ||
				!SafeCopy(indices.data(), data->rawIndexData, indices.size() * sizeof(std::uint16_t))) {
				result.reason = "CPU copy not readable";
				return result;
			}

			const auto&       world = a_geometry->world;
			std::vector<Vec3> positions(vertexCount);
			for (std::uint32_t i = 0; i < vertexCount; ++i) {
				const std::uint8_t* v = vertexBytes.data() + static_cast<std::size_t>(stride) * i;
				RE::NiPoint3        local;
				if (fullPrecision) {
					std::memcpy(&local, v, sizeof(local));
				} else {
					std::uint16_t h[3];
					std::memcpy(h, v, sizeof(h));
					local = { HalfToFloat(h[0]), HalfToFloat(h[1]), HalfToFloat(h[2]) };
				}
				const RE::NiPoint3 w = world * local;
				positions[i] = { w.x, w.y, w.z };
			}

			result.tested = true;
			float bestT = std::numeric_limits<float>::max();
			for (std::uint32_t t = 0; t < triangleCount; ++t) {
				const std::uint16_t i0 = indices[t * 3], i1 = indices[t * 3 + 1], i2 = indices[t * 3 + 2];
				if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
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

		// ---- shader and textures ----------------------------------------------------------------------------------

		void AddLoadedName(TextureSlot& a_slot, const RE::NiPointer<RE::NiSourceTexture>& a_texture)
		{
			if (!a_texture || !a_texture->name.c_str())
				return;
			const std::string loaded = a_texture->name.c_str();
			if (!loaded.empty() && NormalisedPath(loaded) != NormalisedPath(a_slot.path))
				a_slot.loadedAs = loaded;
		}

		/** The path without "data\", a leading slash or "textures\", lower case: so engine and texture-set spellings compare equal. */
		std::string TextureKey(std::string_view a_path)
		{
			std::string p = NormalisedPath(a_path);
			while (!p.empty() && p.front() == '\\')
				p.erase(p.begin());
			if (p.rfind("data\\", 0) == 0)
				p.erase(0, 5);
			if (p.rfind("textures\\", 0) == 0)
				p.erase(0, 9);
			return p;
		}

		using Bound = std::vector<RE::NiSourceTexture*>;

		void AddBound(Bound& a_out, RE::NiSourceTexture* a_texture)
		{
			if (a_texture && std::find(a_out.begin(), a_out.end(), a_texture) == a_out.end())
				a_out.push_back(a_texture);
		}

		/** The engine's own list of textures a lit material holds. Guarded: a material class we do not know must not crash a pick. */
		bool SafeGetTextures(RE::BSLightingShaderMaterialBase* a_material, RE::NiSourceTexture** a_out, std::uint32_t& a_count)
		{
			__try {
				a_count = a_material->GetTextures(a_out);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				a_count = 0;
				return false;
			}
		}

		/**
		 * The a_index-th texture a material subclass adds to the base four (null when it has no such one). Guarded: the class is picked from the
		 * material's feature number, and a plugin that reuses a number for another layout must not take a pick down. No C++ objects live here
		 * (a structured-exception guard cannot sit beside destructors).
		 */
		RE::NiSourceTexture* SubclassTexture(RE::BSLightingShaderMaterialBase* a_lit, int a_feature, int a_index)
		{
			__try {
				switch (a_feature) {
				case 1: {
					auto* m = static_cast<RE::BSLightingShaderMaterialEnvmap*>(a_lit);
					return a_index == 0 ? m->envTexture.get() : a_index == 1 ? m->envMaskTexture.get() : nullptr;
				}
				case 2:
					return a_index == 0 ? static_cast<RE::BSLightingShaderMaterialGlowmap*>(a_lit)->glowTexture.get() : nullptr;
				case 3:
				case 7:
					return a_index == 0 ? static_cast<RE::BSLightingShaderMaterialParallax*>(a_lit)->heightTexture.get() : nullptr;
				case 4: {  // FaceGen only: the tint-color kinds (5 skin tint, 6 hair tint) hold colors, not textures
					auto* m = static_cast<RE::BSLightingShaderMaterialFacegen*>(a_lit);
					return a_index == 0 ? m->tintTexture.get() : a_index == 1 ? m->detailTexture.get() : a_index == 2 ? m->subsurfaceTexture.get() : nullptr;
				}
				case 11: {
					auto* m = static_cast<RE::BSLightingShaderMaterialMultiLayerParallax*>(a_lit);
					return a_index == 0 ? m->layerTexture.get() : a_index == 1 ? m->envTexture.get() : a_index == 2 ? m->envMaskTexture.get() : nullptr;
				}
				case 16: {
					auto* m = static_cast<RE::BSLightingShaderMaterialEye*>(a_lit);
					return a_index == 0 ? m->envTexture.get() : a_index == 1 ? m->envMaskTexture.get() : nullptr;
				}
				default:
					return nullptr;
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return nullptr;
			}
		}

		/** Every texture object a lit material has bound: the base four, the ones its subclass adds, and the engine's own list. */
		Bound BoundTextures(RE::BSLightingShaderMaterialBase* a_lit, int a_feature, bool& a_heightBound)
		{
			Bound out;
			AddBound(out, a_lit->diffuseTexture.get());
			AddBound(out, a_lit->normalTexture.get());
			AddBound(out, a_lit->rimSoftLightingTexture.get());
			AddBound(out, a_lit->specularBackLightingTexture.get());
			for (int i = 0; i < 3; ++i) {
				auto* texture = SubclassTexture(a_lit, a_feature, i);
				if ((a_feature == 3 || a_feature == 7) && i == 0)
					a_heightBound = texture != nullptr;
				AddBound(out, texture);
			}
			// The engine's own enumeration (covers material classes other plugins add, such as TruePBR): a zeroed buffer far larger
			// than any material's list; only non-null entries are kept.
			RE::NiSourceTexture* listed[32]{};
			std::uint32_t        count = 0;
			if (SafeGetTextures(a_lit, listed, count)) {
				for (std::uint32_t i = 0; i < std::min<std::uint32_t>(count, 32u); ++i)
					AddBound(out, listed[i]);
			}
			return out;
		}

		/** The bound texture whose name matches a slot's path, else the positional one for slots 0 and 1. */
		RE::NiSourceTexture* MatchBound(const Bound& a_bound, const std::string& a_path, int a_slot, const RE::BSLightingShaderMaterialBase* a_lit)
		{
			const std::string key = TextureKey(a_path);
			for (auto* t : a_bound)
				if (t->name.c_str() && TextureKey(t->name.c_str()) == key)
					return t;
			if (a_slot == 0)
				return a_lit->diffuseTexture.get();
			if (a_slot == 1)
				return a_lit->normalTexture.get();
			return nullptr;
		}

		void FillGpu(TextureSlot& a_slot, RE::NiSourceTexture* a_texture)
		{
			a_slot.bound = a_texture != nullptr;
			if (!a_texture)
				return;
			a_slot.gpu = ReadGpuInfo(a_texture);
			hold::Gather(a_texture, a_slot.path);
			if (a_texture->name.c_str()) {
				const std::string loaded = a_texture->name.c_str();
				if (!loaded.empty() && TextureKey(loaded) != TextureKey(a_slot.path))
					a_slot.loadedAs = loaded;
			}
		}

		std::string AlphaTestName(int a_function)
		{
			switch (a_function) {
			case 0: return "always";
			case 1: return "less";
			case 2: return "equal";
			case 3: return "less-equal";
			case 4: return "greater";
			case 5: return "not-equal";
			case 6: return "greater-equal";
			case 7: return "never";
			default: return std::format("function {}", a_function);
			}
		}

		void ReadAlpha(RE::BSGeometry* a_geometry, ShapeInfo& a_shape)
		{
			auto* alpha = a_geometry->GetGeometryRuntimeData().alphaProperty.get();
			if (!alpha)
				return;
			const bool blend = alpha->GetAlphaBlending();
			const bool test = alpha->GetAlphaTesting();
			a_shape.alphaMode = blend && test ? "blend+test" : (blend ? "blend" : (test ? "test" : "none"));
			if (test) {
				a_shape.alphaThreshold = alpha->alphaThreshold;
				// NiAlphaProperty flags (NIF format): bit 0 blend, bits 1-4 source blend, 5-8 dest blend, bit 9 test, bits 10-12 test function
				a_shape.alphaTestFunction = AlphaTestName((alpha->alphaFlags >> 10) & 7);
			}
		}

		/** Texture coordinate offset and scale of the first UV set (every shader material has them). */
		void ReadUv(const RE::BSShaderMaterial* a_material, ShapeInfo& a_shape)
		{
			a_shape.uvOffset[0] = a_material->texCoordOffset[0].x;
			a_shape.uvOffset[1] = a_material->texCoordOffset[0].y;
			a_shape.uvScale[0] = a_material->texCoordScale[0].x;
			a_shape.uvScale[1] = a_material->texCoordScale[0].y;
		}

		/** Reads a colour the engine owns through SEH: a wild pointer gives false. */
		bool SafeColor(const RE::NiColor* a_color, float* a_out)
		{
			__try {
				a_out[0] = a_color->red;
				a_out[1] = a_color->green;
				a_out[2] = a_color->blue;
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		void ReadShader(RE::BSGeometry* a_geometry, ShapeInfo& a_shape)
		{
			auto& runtime = a_geometry->GetGeometryRuntimeData();
			auto* property = runtime.shaderProperty.get();
			auto  desc = runtime.rendererData ? runtime.rendererData->vertexDesc : runtime.vertexDesc;
			using V = RE::BSGraphics::Vertex;

			if (desc.HasFlag(V::VF_UV)) a_shape.vertexFlags.emplace_back("UV");
			if (desc.HasFlag(V::VF_UV_2)) a_shape.vertexFlags.emplace_back("UV2");
			if (desc.HasFlag(V::VF_NORMAL)) a_shape.vertexFlags.emplace_back("Normals");
			if (desc.HasFlag(V::VF_TANGENT)) a_shape.vertexFlags.emplace_back("Tangents");
			if (desc.HasFlag(V::VF_COLORS)) a_shape.vertexFlags.emplace_back("Colors");
			if (desc.HasFlag(V::VF_SKINNED)) a_shape.vertexFlags.emplace_back("Skinned");
			if (desc.HasFlag(V::VF_LANDDATA)) a_shape.vertexFlags.emplace_back("LandData");
			if (desc.HasFlag(V::VF_EYEDATA)) a_shape.vertexFlags.emplace_back("EyeData");
			if (desc.HasFlag(V::VF_FULLPREC)) a_shape.vertexFlags.emplace_back("FullPrecision");
			a_shape.skinned = runtime.skinInstance || desc.HasFlag(V::VF_SKINNED);

			a_shape.shaderKind = "none";
			a_shape.pbr = "no";
			if (!property)
				return;
			if (const auto* rtti = property->GetRTTI())
				a_shape.shaderKind = Safe(rtti->GetName());
			for (const auto& f : kFlagNames)
				if (property->flags.any(f.flag))
					a_shape.flags.emplace_back(f.name);
			if (property->flags.any(F::kSkinned))
				a_shape.skinned = true;

			auto* material = property->material;
			if (!material)
				return;
			ReadUv(material, a_shape);

			if (material->GetType() == RE::BSShaderMaterial::Type::kLighting) {
				auto* lit = static_cast<RE::BSLightingShaderMaterialBase*>(material);
				a_shape.featureId = static_cast<int>(material->GetFeature());
				a_shape.shaderType = FeatureName(a_shape.featureId);
				const bool pbrMaterial = a_shape.featureId == 32 || a_shape.featureId == 33;
				const bool modelSpace = property->flags.any(F::kModelSpaceNormals);
				bool       pbrPath = false;

				a_shape.renderTargetIndex = lit->diffuseRenderTargetSourceIndex;
				if (a_shape.featureId == 4) {
					auto* tint = SubclassTexture(lit, 4, 0);
					if (tint && tint->name.c_str())
						a_shape.tintTexture = tint->name.c_str();
				}
				a_shape.hasMaterial = true;
				a_shape.specularColor[0] = lit->specularColor.red;
				a_shape.specularColor[1] = lit->specularColor.green;
				a_shape.specularColor[2] = lit->specularColor.blue;
				a_shape.specularStrength = lit->specularColorScale;
				a_shape.glossiness = lit->specularPower;
				a_shape.materialAlpha = lit->materialAlpha;
				if (auto* lighting = netimmerse_cast<RE::BSLightingShaderProperty*>(property)) {
					float colour[3]{};
					if (lighting->emissiveColor && SafeColor(lighting->emissiveColor, colour)) {
						a_shape.hasEmissive = true;
						std::copy(colour, colour + 3, a_shape.emissiveColor);
						a_shape.emissiveMultiple = lighting->emissiveMult;
					}
				}

				bool        heightBound = false;
				const Bound bound = BoundTextures(lit, a_shape.featureId, heightBound);
				a_shape.heightBound = heightBound;
				if (auto* set = lit->textureSet.get()) {
					for (int slot = 0; slot < 8; ++slot) {
						const char* path = set->GetTexturePath(static_cast<RE::BSTextureSet::Texture>(slot));
						if (!path || !path[0])
							continue;
						TextureSlot t;
						t.slot = slot;
						t.label = pbrMaterial ? PbrSlotLabel(slot) : LitSlotLabel(slot);
						if (slot == 1 && modelSpace && !pbrMaterial)
							t.label = "normal (model-space)";
						t.path = path;
						if (NormalisedPath(path).find("textures\\pbr\\") != std::string::npos)
							pbrPath = true;
						FillGpu(t, MatchBound(bound, t.path, slot, lit));
						a_shape.textures.push_back(std::move(t));
					}
				}
				a_shape.pbr = pbrMaterial ? "material" : (pbrPath ? "path" : "no");
			} else if (material->GetType() == RE::BSShaderMaterial::Type::kEffect) {
				auto* effect = static_cast<RE::BSEffectShaderMaterial*>(material);
				a_shape.shaderType = "Effect";
				const auto addEffect = [&](const char* a_label, const RE::BSFixedString& a_path, const RE::NiPointer<RE::NiSourceTexture>& a_loaded) {
					TextureSlot t;
					t.slot = -1;
					t.label = a_label;
					t.path = Safe(a_path.c_str());
					if (t.path.empty() && a_loaded && a_loaded->name.c_str())
						t.path = a_loaded->name.c_str();
					if (!t.path.empty()) {
						FillGpu(t, a_loaded.get());
						a_shape.textures.push_back(std::move(t));
					}
				};
				addEffect("source", effect->sourceTexturePath, effect->sourceTexture);
				addEffect("greyscale / palette", effect->greyscaleTexturePath, effect->greyscaleTexture);
			} else {
				a_shape.shaderType = std::format("material type {}", static_cast<int>(material->GetType()));
			}
		}

		/** Body partitions of a skinned part (BSDismemberSkinInstance): the biped slots it covers, "body", "hands" ... */
		void ReadPartitions(RE::BSGeometry* a_geometry, ShapeInfo& a_shape)
		{
			auto* skin = a_geometry->GetGeometryRuntimeData().skinInstance.get();
			auto* dismember = netimmerse_cast<RE::BSDismemberSkinInstance*>(skin);
			if (!dismember)
				return;
			auto& data = dismember->GetRuntimeData();
			if (data.numPartitions <= 0 || data.numPartitions > 64 || !data.partitions)
				return;
			std::vector<std::string> names;
			for (int i = 0; i < data.numPartitions; ++i) {
				const int slot = data.partitions[i].slot;
				if (std::find(a_shape.partitionSlots.begin(), a_shape.partitionSlots.end(), slot) != a_shape.partitionSlots.end())
					continue;
				a_shape.partitionSlots.push_back(slot);
				names.push_back(BodySlotName(slot));
			}
			for (std::size_t i = 0; i < names.size(); ++i)
				a_shape.bodyArea += (i ? ", " : "") + names[i];
		}

		// ---- scene walk -------------------------------------------------------------------------------------------

		void CollectGeometries(RE::NiAVObject* a_object, std::vector<RE::BSGeometry*>& a_out, std::size_t& a_hidden, int a_depth = 0)
		{
			if (!a_object || a_depth > 64)
				return;
			if (a_object->GetFlags().any(RE::NiAVObject::Flag::kHidden)) {
				++a_hidden;
				return;
			}
			if (auto* node = a_object->AsNode()) {
				for (auto& child : node->GetChildren())
					CollectGeometries(child.get(), a_out, a_hidden, a_depth + 1);
			} else if (auto* geometry = a_object->AsGeometry()) {
				a_out.push_back(geometry);
			}
		}

		ShapeInfo BuildShape(int a_index, RE::BSGeometry* a_geometry, const Ray& a_ray)
		{
			ShapeInfo s;
			s.index = a_index;
			s.name = Safe(a_geometry->name.c_str());
			if (const auto* rtti = a_geometry->GetRTTI())
				s.nodeType = Safe(rtti->GetName());
			ReadShader(a_geometry, s);
			ReadAlpha(a_geometry, s);
			ReadPartitions(a_geometry, s);

			if (auto* triShape = a_geometry->AsTriShape()) {
				const auto& tri = triShape->GetTrishapeRuntimeData();
				s.vertices = tri.vertexCount;
				s.triangles = tri.triangleCount;
			}

			const auto& world = a_geometry->world;
			s.translate = { world.translate.x, world.translate.y, world.translate.z };
			for (int r = 0; r < 3; ++r)
				for (int c = 0; c < 3; ++c)
					s.rotate[r * 3 + c] = world.rotate.entry[r][c];
			s.scale = world.scale;
			s.boundCentre = { a_geometry->worldBound.center.x, a_geometry->worldBound.center.y, a_geometry->worldBound.center.z };
			s.boundRadius = a_geometry->worldBound.radius;

			float tEnter = 0.0f;
			if (RaySphere(a_ray, s.boundCentre, s.boundRadius, tEnter)) {
				s.boundHit = true;
				s.boundDistance = tEnter;
				// Triangle work only for shapes the ray can reach.
				const auto mesh = TestTriangles(a_geometry, a_ray);
				s.exactTested = mesh.tested;
				s.exactSkipReason = mesh.reason;
				s.exactHit = mesh.hit;
				s.exactDistance = mesh.t;
				s.hitPoint = mesh.point;
				s.hitTriangle = mesh.triangle;
			} else {
				s.exactSkipReason = "not reached by the ray";
			}
			s.rank = -1;
			return s;
		}

		ObjectInfo DescribeObject(RE::TESObjectREFR* a_ref, RE::NiAVObject* a_root)
		{
			ObjectInfo o;
			o.refFormId = a_ref->GetFormID();
			const auto* base = a_ref->GetBaseObject();
			if (base) {
				o.baseFormId = base->GetFormID();
				o.baseType = std::string(RE::FormTypeToString(base->GetFormType()));
				o.baseEditorId = Safe(base->GetFormEditorID());
				if (const auto* model = base->As<RE::TESModel>())
					o.model = Safe(model->GetModel());
			}
			o.refEditorId = Safe(a_ref->GetFormEditorID());
			o.name = Safe(a_ref->GetDisplayFullName());
			if (o.name.empty() && base)
				o.name = Safe(base->GetName());
			if (o.name.empty())
				o.name = !o.baseEditorId.empty() ? o.baseEditorId : (!o.refEditorId.empty() ? o.refEditorId : std::string("(unnamed)"));
			if (a_root)
				o.rootNode = Safe(a_root->name.c_str());
			return o;
		}
	}

	std::vector<ShapeInfo> DescribeParts(RE::NiAVObject* a_root)
	{
		std::vector<ShapeInfo>       parts;
		std::vector<RE::BSGeometry*> geometries;
		std::size_t                  hidden = 0;
		CollectGeometries(a_root, geometries, hidden);
		for (std::size_t i = 0; i < geometries.size(); ++i) {
			ShapeInfo s;
			s.index = static_cast<int>(i);
			s.name = Safe(geometries[i]->name.c_str());
			if (const auto* rtti = geometries[i]->GetRTTI())
				s.nodeType = Safe(rtti->GetName());
			ReadShader(geometries[i], s);
			ReadAlpha(geometries[i], s);
			ReadPartitions(geometries[i], s);
			parts.push_back(std::move(s));
		}
		return parts;
	}

	PickInfo InspectRef(RE::TESObjectREFR* a_ref, const Ray& a_ray, bool a_nearestIfMissed, bool a_fill, int a_forceShape)
	{
		PickInfo info;
		info.ray = a_ray;
		info.hasRay = true;
		if (!a_ref) {
			info.message = "no object";
			return info;
		}
		RE::NiAVObject* root = a_ref->Get3D();
		info.object = DescribeObject(a_ref, root);
		info.valid = true;
		if (!root) {
			info.message = "the object has no loaded 3D";
			if (a_fill)
				FillReport(info, a_ref, nullptr);
			return info;
		}

		std::vector<RE::BSGeometry*> geometries;
		std::size_t                  hidden = 0;
		CollectGeometries(root, geometries, hidden);
		if (hidden)
			info.notes.push_back(std::format("{} hidden node(s) skipped", hidden));

		std::vector<Candidate> candidates;
		for (std::size_t i = 0; i < geometries.size(); ++i) {
			info.shapes.push_back(BuildShape(static_cast<int>(i), geometries[i], a_ray));
			const auto& s = info.shapes.back();
			Candidate   c;
			c.exactTested = s.exactTested;
			c.exactHit = s.exactHit;
			c.exactT = s.exactDistance;
			c.boundHit = s.boundHit;
			c.boundT = s.boundDistance;
			c.boundRadius = s.boundRadius;
			candidates.push_back(c);
		}

		const Choice choice = Choose(candidates);
		if (choice.index >= 0) {
			info.clicked = choice.index;
			info.shapes[choice.index].clicked = true;
			info.shapes[choice.index].rank = 0;
			info.runnersUp = choice.runnersUp;
			int rank = 1;
			for (const int i : choice.runnersUp)
				info.shapes[i].rank = rank++;
			info.method = choice.exact ? "exact" : "approximate";
		} else if (a_nearestIfMissed) {
			// The object was chosen some other way (the console selection): never leave it unanswered. Take the part nearest the ray.
			std::vector<SphereBound> bounds;
			for (const auto& s : info.shapes)
				bounds.push_back({ s.boundCentre, s.boundRadius });
			float     gap = 0.0f;
			int       nearest = NearestToRay(a_ray, bounds, &gap);
			bool      behind = false;
			if (nearest < 0) {
				nearest = NearestToRay(a_ray, bounds, &gap, true);  // the object is behind the camera: still name its nearest part
				behind = nearest >= 0;
			}
			if (nearest >= 0) {
				info.clicked = nearest;
				info.shapes[static_cast<std::size_t>(nearest)].clicked = true;
				info.shapes[static_cast<std::size_t>(nearest)].rank = 0;
				info.method = "nearest_to_center";
				if (behind)
					info.notes.push_back(std::format("the object is behind the camera; part nearest to the screen center, {:.1f} units off", gap));
				else
					info.notes.push_back(std::format("the screen-center ray misses this object; part nearest to it, {:.1f} units off", gap));
			} else {
				info.method = "none";
				info.message = "this object has no parts to show";
			}
		} else {
			info.method = "none";
			info.message = "the ray does not touch any part of this object";
		}
		if (a_forceShape >= 0 && a_forceShape < static_cast<int>(info.shapes.size())) {
			// step-through: report the part the hit list named, whatever the ray's own choice was
			for (auto& s : info.shapes) {
				s.clicked = false;
				if (s.rank == 0)
					s.rank = -1;
			}
			info.clicked = a_forceShape;
			info.shapes[static_cast<std::size_t>(a_forceShape)].clicked = true;
			info.shapes[static_cast<std::size_t>(a_forceShape)].rank = 0;
			info.runnersUp.clear();
			info.method = "step_through";
			info.message.clear();
		}
		if (a_fill)
			FillReport(info, a_ref, root);
		return info;
	}

	namespace
	{
		/** One object the ray may reach, with its parts already read (no Report yet). */
		struct ScannedObject
		{
			RE::TESObjectREFR* ref = nullptr;
			PickInfo           info;
			bool               exact = false;
			float              score = 0.0f;  ///< distance of the part the ray chose
			int                shape = -1;    ///< index in info.shapes of the part the ray chose
		};

		/**
		 * Where a part stands along the ray, when the ray reaches it: a triangle hit (exact), or for a part whose mesh cannot be
		 * tested (skinned, no CPU copy: trees, bodies) the nearer of where the ray enters its bound and where the bound's centre
		 * projects onto the ray (the camera can sit inside a large bound). Effects and particles (not a tri shape) never count,
		 * and a part whose triangles were tested and missed is not a hit.
		 */
		bool ShapeAlongRay(const ShapeInfo& a_shape, const Ray& a_ray, float& a_score, bool& a_exact)
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

		struct Scan
		{
			std::vector<ScannedObject> objects;
			int                        best = -1;  ///< index in objects of the part the ray meets first, -1 when none
			WorldHit                   land;
			WorldHit                   water;
			std::vector<HitInfo>       hits;  ///< sorted nearest first
		};

		void AddPartHits(const ScannedObject& a_object, std::vector<HitInfo>& a_hits, const Ray& a_ray)
		{
			const auto& info = a_object.info;
			for (const auto& s : info.shapes) {
				float score = 0.0f;
				bool  exact = false;
				if (!ShapeAlongRay(s, a_ray, score, exact))
					continue;
				HitInfo h;
				h.kind = "part";
				h.partName = s.name;
				h.label = std::format("{} on {}", s.name.empty() ? std::format("part {}", s.index + 1) : s.name, info.object.name);
				h.exact = exact;
				h.distance = score;
				h.point = exact ? s.hitPoint : a_ray.origin + a_ray.dir * score;
				h.refFormId = info.object.refFormId;
				h.shapeIndex = s.index;
				a_hits.push_back(std::move(h));
			}
		}

		Scan ScanRay(const Ray& a_ray, float a_maxDistance)
		{
			Scan  scan;
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* tes = RE::TES::GetSingleton();
			if (!player || !tes)
				return scan;

			// Cheap pass: references whose root bound the ray enters. The expensive pass reads their meshes.
			struct Near
			{
				RE::TESObjectREFR* ref;
				float              t;
			};
			std::vector<Near> candidates;
			tes->ForEachReferenceInRange(player, a_maxDistance + 2000.0f, [&](RE::TESObjectREFR* a_ref) {
				if (!a_ref || a_ref == player || a_ref->IsDisabled() || a_ref->IsDeleted())
					return RE::BSContainer::ForEachResult::kContinue;
				auto* root = a_ref->Get3D();
				if (!root)
					return RE::BSContainer::ForEachResult::kContinue;
				float       t = 0.0f;
				const auto& b = root->worldBound;
				if (RaySphere(a_ray, { b.center.x, b.center.y, b.center.z }, b.radius, t) && t <= a_maxDistance)
					candidates.push_back({ a_ref, t });
				return RE::BSContainer::ForEachResult::kContinue;
			});
			std::sort(candidates.begin(), candidates.end(), [](const Near& a, const Near& b) { return a.t < b.t; });
			constexpr std::size_t kMaxRefsRead = 40;
			if (candidates.size() > kMaxRefsRead)
				candidates.resize(kMaxRefsRead);

			for (const auto& n : candidates) {
				ScannedObject o;
				o.ref = n.ref;
				o.info = InspectRef(n.ref, a_ray, false, false);
				float best = std::numeric_limits<float>::max();
				for (const auto& shape : o.info.shapes) {
					float score = 0.0f;
					bool  exact = false;
					if (ShapeAlongRay(shape, a_ray, score, exact) && score < best) {
						best = score;
						o.score = score;
						o.exact = exact;
						o.shape = shape.index;
					}
				}
				if (o.shape < 0)
					continue;
				scan.objects.push_back(std::move(o));
			}
			// the part the ray meets first: the nearest, whether its triangles were hit (exact) or only its bound (a part with
			// no CPU mesh, such as a tree). A bounds-only part in front of an exact one is the pick, flagged approximate; the
			// exact one behind it stays in the hits list.
			for (std::size_t i = 0; i < scan.objects.size(); ++i) {
				if (scan.best < 0 || scan.objects[i].score < scan.objects[static_cast<std::size_t>(scan.best)].score)
					scan.best = static_cast<int>(i);
			}
			for (const auto& o : scan.objects)
				AddPartHits(o, scan.hits, a_ray);

			scan.land = RayLand(a_ray, a_maxDistance);
			if (scan.land.hit) {
				HitInfo h;
				h.kind = "terrain";
				h.label = scan.land.label;
				h.distance = scan.land.distance;
				h.point = scan.land.point;
				scan.hits.push_back(std::move(h));
			}
			scan.water = RayWater(a_ray, a_maxDistance);
			if (scan.water.hit) {
				HitInfo h;
				h.kind = "water";
				h.label = scan.water.label;
				h.distance = scan.water.distance;
				h.point = scan.water.point;
				scan.hits.push_back(std::move(h));
			}
			std::stable_sort(scan.hits.begin(), scan.hits.end(), [](const HitInfo& a, const HitInfo& b) { return a.distance < b.distance; });
			return scan;
		}

		int IndexOfHit(const std::vector<HitInfo>& a_hits, const std::string& a_kind, std::uint32_t a_ref = 0, int a_shape = -1)
		{
			for (std::size_t i = 0; i < a_hits.size(); ++i)
				if (a_hits[i].kind == a_kind && a_hits[i].refFormId == a_ref && a_hits[i].shapeIndex == a_shape)
					return static_cast<int>(i);
			return -1;
		}

		/** The reference behind a part hit when its part carries a water shader: a placed water plane, not an object to read. */
		bool IsWaterPart(const ScannedObject& a_object)
		{
			if (a_object.shape < 0)
				return false;
			// the cell's own runtime water plane (an activator with the one part DefaultProceduralWater) counts too
			return a_object.info.shapes[static_cast<std::size_t>(a_object.shape)].shaderKind.find("BSWaterShaderProperty") != std::string::npos || IsWaterPlane(a_object.info);
		}
	}

	PickInfo PickAlongRay(const Ray& a_ray, float a_maxDistance, PickMode a_mode)
	{
		PickInfo result;
		result.ray = a_ray;
		result.hasRay = true;
		if (a_mode == PickMode::kSky) {
			FillSky(result);
			return result;
		}
		Scan scan = ScanRay(a_ray, a_maxDistance);

		// what the ray meets first among the three kinds of thing
		enum class Chosen
		{
			kNone,
			kPart,
			kLand,
			kWater
		};
		Chosen      chosen = Chosen::kNone;
		const float partDistance = scan.best >= 0 ? scan.objects[static_cast<std::size_t>(scan.best)].score : std::numeric_limits<float>::max();
		if (a_mode == PickMode::kGround) {
			chosen = scan.land.hit ? Chosen::kLand : Chosen::kNone;
		} else if (a_mode == PickMode::kWater) {
			chosen = scan.water.hit ? Chosen::kWater : Chosen::kNone;
		} else {
			float nearest = std::numeric_limits<float>::max();
			if (scan.best >= 0) {
				chosen = Chosen::kPart;
				nearest = partDistance;
			}
			if (scan.land.hit && scan.land.distance < nearest) {
				chosen = Chosen::kLand;
				nearest = scan.land.distance;
			}
			if (scan.water.hit && scan.water.distance < nearest) {
				chosen = Chosen::kWater;
				nearest = scan.water.distance;
			}
		}

		switch (chosen) {
		case Chosen::kPart: {
			auto& o = scan.objects[static_cast<std::size_t>(scan.best)];
			if (IsWaterPart(o)) {
				const auto& shape = o.info.shapes[static_cast<std::size_t>(o.shape)];
				const Vec3  point = o.exact ? shape.hitPoint : a_ray.origin + a_ray.dir * o.score;
				FillWater(result, point, o.ref);
				result.hits = scan.hits;
				result.hitIndex = IndexOfHit(result.hits, "part", o.info.object.refFormId, shape.index);
				result.notes.push_back(std::format("the ray met a placed water plane: {} ({})", o.info.object.name, std::format("{:08X}", o.info.object.refFormId)));
				return result;
			}
			PickInfo info = std::move(o.info);
			if (info.clicked != o.shape) {
				// the part this scan chose is not the one the object's own ranking would have (an effect or a tested miss was in the way): report the chosen one
				for (auto& sh : info.shapes) {
					sh.clicked = false;
					if (sh.rank == 0)
						sh.rank = -1;
				}
				info.clicked = o.shape;
				info.shapes[static_cast<std::size_t>(o.shape)].clicked = true;
				info.shapes[static_cast<std::size_t>(o.shape)].rank = 0;
				info.runnersUp.clear();
				info.message.clear();
			}
			info.method = o.exact ? "exact" : "approximate";
			FillReport(info, o.ref, o.ref->Get3D());
			info.notes.push_back(std::format("crosshair pick: {} candidate object(s) along the ray, {} hit(s) in all", scan.objects.size(), scan.hits.size()));
			info.hits = scan.hits;
			info.hitIndex = IndexOfHit(info.hits, "part", info.object.refFormId, info.clicked);
			info.fromRay = true;
			return info;
		}
		case Chosen::kLand:
			FillTerrain(result, scan.land.point);
			result.hits = scan.hits;
			result.hitIndex = IndexOfHit(result.hits, "terrain");
			result.fromRay = true;
			return result;
		case Chosen::kWater:
			FillWater(result, scan.water.point, nullptr);
			result.hits = scan.hits;
			result.hitIndex = IndexOfHit(result.hits, "water");
			return result;
		case Chosen::kNone:
			break;
		}
		if (a_mode == PickMode::kGround || a_mode == PickMode::kWater) {
			result.valid = true;
			result.message = a_mode == PickMode::kGround ? "no loaded land under the crosshair" : "no water surface under the crosshair";
			result.hits = scan.hits;
			return result;
		}
		// nothing in front of the crosshair: the sky
		FillSky(result);
		result.hits = scan.hits;
		return result;
	}

	PickInfo PickStep(const Ray& a_ray, const std::vector<HitInfo>& a_hits, int a_index)
	{
		PickInfo result;
		result.ray = a_ray;
		result.hasRay = true;
		if (a_index < 0 || a_index >= static_cast<int>(a_hits.size())) {
			result.message = "no such hit to step to";
			return result;
		}
		const HitInfo& hit = a_hits[static_cast<std::size_t>(a_index)];
		if (hit.kind == "terrain") {
			FillTerrain(result, hit.point);
		} else if (hit.kind == "water") {
			FillWater(result, hit.point, nullptr);
		} else {
			auto* form = RE::TESForm::LookupByID(hit.refFormId);
			auto* ref = form ? form->As<RE::TESObjectREFR>() : nullptr;
			if (!ref) {
				result.valid = true;
				result.message = std::format("the object {:08X} is no longer loaded", hit.refFormId);
				return result;
			}
			result = InspectRef(ref, a_ray, false, true, hit.shapeIndex);
			result.notes.push_back(std::format("step-through: hit {} of {} along the ray", a_index + 1, a_hits.size()));
		}
		result.hits = a_hits;
		result.hitIndex = a_index;
		result.fromRay = true;
		return result;
	}
}
