// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
#include "PCH.h"

#include "Inspector.h"

#include "Diagnostics.h"
#include "Fo4Names.h"
#include "PickScan.h"
#include "ReportFill.h"
#include "WorldFill.h"
#include "TextureGather.h"
#include "TextureInfo.h"

#include <Windows.h>

namespace ti
{
	namespace
	{
		constexpr std::size_t kMaxVertexBytes = 64u * 1024u * 1024u;

		std::string Safe(const char* a_text) { return a_text ? std::string(a_text) : std::string(); }

		std::string NormalisedPath(std::string_view a_path)
		{
			std::string out(a_path);
			for (auto& c : out)
				c = c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return out;
		}

		// ---- guarded reads of what the engine owns (no C++ objects with destructors in a __try function) ----------------

		/** Copies bytes from engine memory: a bad pointer returns false instead of crashing. */
		bool SafeCopy(void* a_dst, const void* a_src, std::size_t a_bytes)
		{
			__try {
				std::memcpy(a_dst, a_src, a_bytes);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		/** What a shape's renderer data points at: the description and the CPU copies of the vertex and index buffers, if any. */
		struct BufferPointers
		{
			bool          ok = false;
			bool          hasRenderer = false;
			std::uint64_t desc = 0;
			const void*   vertexData = nullptr;
			std::uint32_t vertexSize = 0;
			bool          vertexInvalid = false;
			const void*   indexData = nullptr;
			std::uint32_t indexSize = 0;
			bool          indexInvalid = false;
		};

		BufferPointers ReadBufferPointers(void* a_rendererData)
		{
			BufferPointers out;
			__try {
				out.ok = true;
				if (!a_rendererData)
					return out;
				out.hasRenderer = true;
				const auto* shape = static_cast<const RE::BSGraphics::TriShape*>(a_rendererData);
				out.desc = shape->vertexDesc.desc;
				if (shape->vertexBuffer) {
					out.vertexData = shape->vertexBuffer->data;
					out.vertexSize = shape->vertexBuffer->dataSize;
					out.vertexInvalid = shape->vertexBuffer->invalidCpuData;
				}
				if (shape->indexBuffer) {
					out.indexData = shape->indexBuffer->data;
					out.indexSize = shape->indexBuffer->dataSize;
					out.indexInvalid = shape->indexBuffer->invalidCpuData;
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				out = BufferPointers{};
			}
			return out;
		}

		/** The shader material's type and feature, asked through its virtual functions; false when the pointer is not a material. */
		bool ReadMaterialKind(RE::BSShaderMaterial* a_material, int& a_type, int& a_feature)
		{
			__try {
				a_type = static_cast<int>(a_material->GetType());
				a_feature = static_cast<int>(a_material->GetFeature());
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		/** The path a texture set holds for a slot (null when none or unreadable). */
		const char* SafeTextureFilename(RE::BSTextureSet* a_set, int a_slot)
		{
			__try {
				return a_set->GetTextureFilename(static_cast<RE::BSShaderProperty::TextureTypeEnum>(a_slot));
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return nullptr;
			}
		}

		bool SafeColor(const RE::NiColor* a_color, float* a_out)
		{
			__try {
				a_out[0] = a_color->r;
				a_out[1] = a_color->g;
				a_out[2] = a_color->b;
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		// ---- CPU mesh read ----------------------------------------------------------------------------------------

		MeshTest TestTriangles(RE::BSGeometry* a_geometry, const Ray& a_ray)
		{
			MeshTest result;
			if (a_geometry->IsDynamicTriShape()) {
				result.reason = "dynamic tri shape (vertices live elsewhere)";
				return result;
			}
			auto* triShape = a_geometry->IsTriShape();
			if (!triShape) {
				result.reason = "not a tri shape";
				return result;
			}
			if (a_geometry->IsBSCombinedTriShape() || a_geometry->IsBSMergeInstancedTriShape() || a_geometry->IsMultiIndexTriShape()) {
				result.reason = "combined or instanced tri shape (not read)";
				return result;
			}
			const auto buffers = ReadBufferPointers(a_geometry->rendererData);
			if (!buffers.ok) {
				result.reason = "renderer data not readable";
				return result;
			}
			const std::uint64_t desc = buffers.hasRenderer ? buffers.desc : a_geometry->vertexDesc.desc;
			const auto          flags = static_cast<std::uint16_t>(desc >> 44);
			if (a_geometry->skinInstance || (flags & static_cast<std::uint16_t>(RE::BSGraphics::Vertex::VF_SKINNED))) {
				result.reason = "skinned (bind pose is not the world pose)";
				return result;
			}
			if (!(flags & static_cast<std::uint16_t>(RE::BSGraphics::Vertex::VF_VERTEX))) {
				result.reason = "no positions";
				return result;
			}
			if (!buffers.vertexData || !buffers.indexData) {
				result.reason = "no CPU copy of the mesh";
				return result;
			}
			if (buffers.vertexInvalid || buffers.indexInvalid) {
				result.reason = "CPU copy of the mesh is marked invalid";
				return result;
			}
			const std::uint32_t vertexCount = triShape->numVertices;
			const std::uint32_t triangleCount = triShape->numTriangles;
			const bool          full = Fo4FullPrecision(flags);
			const std::uint32_t stride = Fo4VertexStride(desc);
			if (vertexCount == 0 || triangleCount == 0 || stride < (full ? 12u : 6u) || static_cast<std::size_t>(stride) * vertexCount > kMaxVertexBytes ||
				triangleCount > 4u * 1024u * 1024u) {
				result.reason = "mesh size not readable";
				return result;
			}
			const std::size_t vertexBytes = static_cast<std::size_t>(stride) * vertexCount;
			const std::size_t indexBytes = static_cast<std::size_t>(triangleCount) * 3u * sizeof(std::uint16_t);
			if (buffers.vertexSize < vertexBytes || buffers.indexSize < indexBytes) {
				result.reason = "CPU copy smaller than the mesh";
				return result;
			}

			std::vector<std::uint8_t>  vertexCopy(vertexBytes);
			std::vector<std::uint16_t> indexCopy(static_cast<std::size_t>(triangleCount) * 3u);
			if (!SafeCopy(vertexCopy.data(), buffers.vertexData, vertexBytes) || !SafeCopy(indexCopy.data(), buffers.indexData, indexBytes)) {
				result.reason = "CPU copy not readable";
				return result;
			}

			MeshView view;
			view.vertexBytes = vertexCopy.data();
			view.vertexByteCount = vertexCopy.size();
			view.stride = stride;
			view.vertexCount = vertexCount;
			view.fullPrecision = full;
			view.indices = indexCopy.data();
			view.triangleCount = triangleCount;
			const auto& world = a_geometry->world;
			for (int r = 0; r < 3; ++r)
				for (int c = 0; c < 3; ++c)
					view.rotate[r * 3 + c] = world.rotate.entry[r][c];
			view.translate = { world.translate.x, world.translate.y, world.translate.z };
			view.scale = world.scale;
			return TestMesh(view, a_ray);
		}

		// ---- shader and textures ----------------------------------------------------------------------------------

		RE::BSShaderProperty* FindShader(RE::BSGeometry* a_geometry)
		{
			for (auto& slot : a_geometry->properties)
				if (auto* shader = netimmerse_cast<RE::BSShaderProperty*>(slot.get()))
					return shader;
			return nullptr;
		}

		RE::NiAlphaProperty* FindAlpha(RE::BSGeometry* a_geometry)
		{
			for (auto& slot : a_geometry->properties)
				if (auto* alpha = netimmerse_cast<RE::NiAlphaProperty*>(slot.get()))
					return alpha;
			return nullptr;
		}

		std::string FeatureName(int a_feature)
		{
			switch (a_feature) {
			case 0: return "Default";
			case 1: return "EnvironmentMap";
			case 2: return "GlowMap";
			case 3: return "Parallax";
			case 4: return "FaceGen";
			case 5: return "SkinTint";
			case 6: return "HairTint";
			case 7: return "ParallaxOcc";
			case 8: return "Landscape";
			case 9: return "LODLandscape";
			case 10: return "Snow";
			case 11: return "MultiLayerParallax";
			case 12: return "TreeAnim";
			case 13: return "LODObjects";
			case 14: return "MultiIndexSnow";
			case 15: return "LODObjectsHD";
			case 16: return "Eye";
			case 17: return "Cloud";
			case 18: return "LODLandscapeNoise";
			case 19: return "LODLandscapeBlend";
			case 20: return "Dismemberment";
			default: return std::format("feature {}", a_feature);
			}
		}

		void FillGpu(TextureSlot& a_slot, RE::NiTexture* a_texture)
		{
			a_slot.bound = a_texture != nullptr;
			if (!a_texture)
				return;
			a_slot.gpu = ReadGpuInfo(a_texture);
			hold::Gather(a_texture, a_slot.path);
			const std::string loaded = Safe(a_texture->name.c_str());
			if (!loaded.empty() && NormalisedPath(loaded) != NormalisedPath(a_slot.path))
				a_slot.loadedAs = loaded;
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
			auto* alpha = FindAlpha(a_geometry);
			if (!alpha)
				return;
			const bool blend = alpha->GetAlphaBlending();
			const bool test = alpha->GetAlphaTesting();
			a_shape.alphaMode = blend && test ? "blend+test" : (blend ? "blend" : (test ? "test" : "none"));
			if (test) {
				a_shape.alphaThreshold = static_cast<int>(static_cast<std::uint8_t>(alpha->alphaTestRef));
				// NiAlphaProperty flags: bit 0 blend, bits 1-4 source blend, 5-8 dest blend, bit 9 test, bits 10-12 test function
				a_shape.alphaTestFunction = AlphaTestName((alpha->flags.flags >> 10) & 7);
			}
		}

		/** One bound texture of a lit material and the texture-set slot it stands for. */
		struct LitBinding
		{
			int           slot;
			RE::NiTexture* texture;
		};

		void ReadShader(RE::BSGeometry* a_geometry, ShapeInfo& a_shape)
		{
			const auto buffers = ReadBufferPointers(a_geometry->rendererData);
			const std::uint64_t desc = buffers.ok && buffers.hasRenderer ? buffers.desc : a_geometry->vertexDesc.desc;
			const auto          vertexFlags = static_cast<std::uint16_t>(desc >> 44);
			a_shape.vertexFlags = Fo4VertexFlagNames(vertexFlags);
			a_shape.skinned = a_geometry->skinInstance || (vertexFlags & static_cast<std::uint16_t>(RE::BSGraphics::Vertex::VF_SKINNED));

			a_shape.shaderKind = "none";
			a_shape.pbr = "no";
			auto* property = FindShader(a_geometry);
			if (!property)
				return;
			if (const auto* rtti = property->GetRTTI())
				a_shape.shaderKind = Safe(rtti->GetName());
			a_shape.flags = Fo4ShaderFlagNames(property->flags.underlying());
			if (property->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kSkinned))
				a_shape.skinned = true;

			auto* lighting = netimmerse_cast<RE::BSLightingShaderProperty*>(property);
			ApplyFo4Material(a_shape, Safe(property->name.c_str()), lighting ? Safe(lighting->rootName.c_str()) : std::string());

			auto* material = property->material;
			if (!material)
				return;
			int type = 0, feature = 0;
			if (!ReadMaterialKind(material, type, feature))
				return;
			a_shape.uvOffset[0] = material->texCoordOffset[0].x;
			a_shape.uvOffset[1] = material->texCoordOffset[0].y;
			a_shape.uvScale[0] = material->texCoordScale[0].x;
			a_shape.uvScale[1] = material->texCoordScale[0].y;

			if (type == static_cast<int>(RE::BSShaderMaterial::Type::kLighting)) {
				auto* lit = static_cast<RE::BSLightingShaderMaterialBase*>(material);
				a_shape.featureId = feature;
				a_shape.shaderType = FeatureName(feature);
				a_shape.hasMaterial = true;
				a_shape.specularColor[0] = lit->specularColor.r;
				a_shape.specularColor[1] = lit->specularColor.g;
				a_shape.specularColor[2] = lit->specularColor.b;
				a_shape.specularStrength = lit->specularColorScale;
				a_shape.glossiness = lit->smoothness;
				a_shape.materialAlpha = lit->materialAlpha;
				if (lighting && lighting->emitColor) {
					float colour[3]{};
					if (SafeColor(lighting->emitColor, colour)) {
						a_shape.hasEmissive = true;
						std::copy(colour, colour + 3, a_shape.emissiveColor);
						a_shape.emissiveMultiple = lighting->emitColorScale;
					}
				}

				// the textures the material holds, by texture-set slot (the ones the engine keeps bound as texture objects)
				const LitBinding bound[] = {
					{ 0, lit->diffuseTexture.get() },
					{ 1, lit->normalTexture.get() },
					{ 2, lit->rimSoftLightingTexture.get() },
					{ 3, lit->lookupTexture.get() },
					{ 7, lit->smoothnessSpecMaskTexture.get() },
				};
				auto*       set = lit->textureSet.get();
				bool        anyPath = false;
				if (set) {
					for (int slot = 0; slot < 9; ++slot) {
						const char* path = SafeTextureFilename(set, slot);
						if (!path || !path[0])
							continue;
						anyPath = true;
						TextureSlot t;
						t.slot = slot;
						t.label = Fo4LitSlotLabel(slot);
						if (slot == 1 && property->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kModelSpaceNormals))
							t.label = "normal (model-space)";
						t.path = path;
						RE::NiTexture* texture = nullptr;
						for (const auto& b : bound)
							if (b.slot == slot)
								texture = b.texture;
						FillGpu(t, texture);
						a_shape.textures.push_back(std::move(t));
					}
				}
				if (!anyPath) {
					// no texture set on the material (values come from the NIF): fall back to the bound texture objects' own names
					for (const auto& b : bound) {
						if (!b.texture || !b.texture->name.c_str()[0])
							continue;
						TextureSlot t;
						t.slot = b.slot;
						t.label = Fo4LitSlotLabel(b.slot);
						t.path = b.texture->name.c_str();
						FillGpu(t, b.texture);
						a_shape.textures.push_back(std::move(t));
					}
				}
			} else if (type == static_cast<int>(RE::BSShaderMaterial::Type::kEffect)) {
				auto*      effect = static_cast<RE::BSEffectShaderMaterial*>(material);
				a_shape.shaderType = "Effect";
				const auto add = [&](int a_index, const RE::BSFixedString& a_path, RE::NiTexture* a_loaded) {
					TextureSlot t;
					t.slot = -1;
					t.label = Fo4EffectSlotLabel(a_index);
					t.path = Safe(a_path.c_str());
					if (t.path.empty() && a_loaded)
						t.path = Safe(a_loaded->name.c_str());
					if (!t.path.empty()) {
						FillGpu(t, a_loaded);
						a_shape.textures.push_back(std::move(t));
					}
				};
				add(0, effect->sourceTexturePath, effect->baseTexture.get());
				add(1, effect->greyscaleTexturePath, effect->greyscaleTexture.get());
				add(2, effect->environmentTexturePath, effect->environmentTexture.get());
				add(3, effect->environmentMaskTexturePath, effect->environmentMaskTexture.get());
				add(4, effect->normalTexturePath, effect->normalTexture.get());
			} else {
				a_shape.shaderType = std::format("material type {}", type);
			}
		}

		// ---- scene walk -------------------------------------------------------------------------------------------

		void CollectGeometries(RE::NiAVObject* a_object, std::vector<RE::BSGeometry*>& a_out, std::size_t& a_hidden, int a_depth = 0)
		{
			if (!a_object || a_depth > 64)
				return;
			if (a_object->GetAppCulled()) {
				++a_hidden;
				return;
			}
			if (auto* node = a_object->IsNode()) {
				const auto count = node->children.capacity();
				for (std::uint16_t i = 0; i < count; ++i)
					CollectGeometries(node->children[i].get(), a_out, a_hidden, a_depth + 1);
			} else if (auto* geometry = a_object->IsGeometry()) {
				a_out.push_back(geometry);
			}
		}

		/** Everything about a part except what a ray does to it (shader, textures, transform, bounds). */
		ShapeInfo BuildShapeBase(int a_index, RE::BSGeometry* a_geometry)
		{
			ShapeInfo s;
			s.index = a_index;
			s.name = Safe(a_geometry->name.c_str());
			if (const auto* rtti = a_geometry->GetRTTI())
				s.nodeType = Safe(rtti->GetName());
			ReadShader(a_geometry, s);
			ReadAlpha(a_geometry, s);

			if (auto* triShape = a_geometry->IsTriShape()) {
				s.vertices = triShape->numVertices;
				s.triangles = triShape->numTriangles;
			}

			const auto& world = a_geometry->world;
			s.translate = { world.translate.x, world.translate.y, world.translate.z };
			for (int r = 0; r < 3; ++r)
				for (int c = 0; c < 3; ++c)
					s.rotate[r * 3 + c] = world.rotate.entry[r][c];
			s.scale = world.scale;
			s.boundCentre = { a_geometry->worldBound.center.x, a_geometry->worldBound.center.y, a_geometry->worldBound.center.z };
			s.boundRadius = a_geometry->worldBound.fRadius;
			s.rank = -1;
			return s;
		}

		ShapeInfo BuildShape(int a_index, RE::BSGeometry* a_geometry, const Ray& a_ray)
		{
			ShapeInfo s = BuildShapeBase(a_index, a_geometry);
			float     tEnter = 0.0f;
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

		// The runtime has no usable RTTI for dynamic_cast on forms, so the base form is cast by its form type to the classes that carry
		// a model (each is a TESModel through BGSModelMaterialSwap or TESModel directly).
		template <class T>
		const RE::TESModel* ModelAs(RE::TESBoundObject* a_base)
		{
			return a_base->GetFormType() == T::FORM_ID ? static_cast<const RE::TESModel*>(static_cast<T*>(a_base)) : nullptr;
		}

		std::string ModelOf(RE::TESBoundObject* a_base)
		{
			if (!a_base)
				return {};
			try {
				const RE::TESModel* model = nullptr;
				(void)(                                                                                          //
					(model = ModelAs<RE::TESObjectSTAT>(a_base)) || (model = ModelAs<RE::TESObjectACTI>(a_base)) ||  //
					(model = ModelAs<RE::TESObjectCONT>(a_base)) || (model = ModelAs<RE::TESObjectDOOR>(a_base)) ||  //
					(model = ModelAs<RE::TESFurniture>(a_base)) || (model = ModelAs<RE::TESObjectMISC>(a_base)) ||   //
					(model = ModelAs<RE::TESObjectLIGH>(a_base)) || (model = ModelAs<RE::TESObjectBOOK>(a_base)) ||  //
					(model = ModelAs<RE::TESFlora>(a_base)) || (model = ModelAs<RE::TESObjectTREE>(a_base)) ||       //
					(model = ModelAs<RE::BGSMovableStatic>(a_base)) || (model = ModelAs<RE::TESAmmo>(a_base)) ||     //
					(model = ModelAs<RE::AlchemyItem>(a_base)) || (model = ModelAs<RE::BGSNote>(a_base)) ||
						(model = ModelAs<RE::TESObjectWEAP>(a_base)));
				if (model)
					return Safe(model->GetModel());
			} catch (...) {
			}
			return {};
		}

		ObjectInfo DescribeObject(RE::TESObjectREFR* a_ref, RE::NiAVObject* a_root)
		{
			ObjectInfo o;
			o.refFormId = a_ref->GetFormID();
			auto* base = a_ref->GetObjectReference();
			if (base) {
				o.baseFormId = base->GetFormID();
				o.baseType = Safe(base->GetFormTypeString());
				o.baseEditorId = Safe(base->GetFormEditorID());
				o.model = ModelOf(base);
			}
			o.refEditorId = Safe(a_ref->GetFormEditorID());
			o.name = Safe(a_ref->GetDisplayFullName());
			if (o.name.empty() && base)
				o.name = std::string(RE::TESFullName::GetFullName(*base));
			if (o.name.empty())
				o.name = !o.baseEditorId.empty() ? o.baseEditorId : (!o.refEditorId.empty() ? o.refEditorId : std::string("(unnamed)"));
			if (a_root)
				o.rootNode = Safe(a_root->name.c_str());
			return o;
		}

		void LogMaterialProbe(const PickInfo& a_info, RE::BSGeometry* a_geometry)
		{
			auto* property = FindShader(a_geometry);
			if (!property)
				return;
			auto* lighting = netimmerse_cast<RE::BSLightingShaderProperty*>(property);
			const auto& s = a_info.shapes[static_cast<std::size_t>(a_info.clicked)];
			spdlog::info("material probe: part '{}' ({}): shader name '{}', root name '{}' -> material path '{}' (from {})", s.name, s.shaderKind, Safe(property->name.c_str()),
				lighting ? Safe(lighting->rootName.c_str()) : std::string("(not a lit shader)"), s.materialPath, s.materialSource);
		}
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
		for (std::size_t i = 0; i < geometries.size(); ++i)
			info.shapes.push_back(BuildShape(static_cast<int>(i), geometries[i], a_ray));

		ApplyChoice(info, a_ray, a_nearestIfMissed, a_forceShape);
		if (a_fill) {
			FillReport(info, a_ref, root);
			if (info.clicked >= 0 && static_cast<std::size_t>(info.clicked) < geometries.size())
				LogMaterialProbe(info, geometries[static_cast<std::size_t>(info.clicked)]);
		}
		return info;
	}

	std::vector<ShapeInfo> DescribeParts(RE::NiAVObject* a_root)
	{
		std::vector<ShapeInfo> shapes;
		try {
			std::vector<RE::BSGeometry*> geometries;
			std::size_t                  hidden = 0;
			CollectGeometries(a_root, geometries, hidden);
			for (std::size_t i = 0; i < geometries.size(); ++i)
				shapes.push_back(BuildShapeBase(static_cast<int>(i), geometries[i]));
		} catch (...) {
			spdlog::error("reading the parts of a 3D failed");
		}
		return shapes;
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
	}

	namespace
	{
		int IndexOfHit(const std::vector<HitInfo>& a_hits, const std::string& a_kind)
		{
			for (std::size_t i = 0; i < a_hits.size(); ++i)
				if (a_hits[i].kind == a_kind)
					return static_cast<int>(i);
			return -1;
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
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* tes = RE::TES::GetSingleton();
		if (!player || !tes) {
			result.message = "the world is not loaded";
			return result;
		}

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
			if (RaySphere(a_ray, { b.center.x, b.center.y, b.center.z }, b.fRadius, t) && t <= a_maxDistance)
				candidates.push_back({ a_ref, t });
			return RE::BSContainer::ForEachResult::kContinue;
		});
		std::sort(candidates.begin(), candidates.end(), [](const Near& a, const Near& b) { return a.t < b.t; });
		constexpr std::size_t kMaxRefsRead = 120;
		const std::size_t     candidateCount = candidates.size();
		if (candidates.size() > kMaxRefsRead)
			candidates.resize(kMaxRefsRead);

		std::vector<ScannedObject> objects;
		int                        best = -1;
		if (a_mode == PickMode::kAuto) {
			for (const auto& n : candidates) {
				ScannedObject o;
				o.ref = n.ref;
				o.info = InspectRef(n.ref, a_ray, false, false);
				float bestScore = std::numeric_limits<float>::max();
				for (const auto& shape : o.info.shapes) {
					float score = 0.0f;
					bool  exact = false;
					if (PartAlongRay(shape, a_ray, score, exact) && score < bestScore) {
						bestScore = score;
						o.score = score;
						o.exact = exact;
						o.shape = shape.index;
					}
				}
				if (o.shape < 0)
					continue;
				objects.push_back(std::move(o));
			}
			for (std::size_t i = 0; i < objects.size(); ++i)
				if (best < 0 || objects[i].score < objects[static_cast<std::size_t>(best)].score)
					best = static_cast<int>(i);
		}

		spdlog::info("ray: {} reference(s) whose bounds the ray enters, {} read, {} with a part on the ray", candidateCount, candidates.size(), objects.size());
		std::vector<HitInfo> hits;
		for (const auto& o : objects)
			AppendPartHits(o.info, a_ray, hits);
		const WorldHit land = a_mode == PickMode::kWater ? WorldHit{} : RayLand(a_ray, a_maxDistance);
		const WorldHit water = a_mode == PickMode::kGround ? WorldHit{} : RayWater(a_ray, a_maxDistance);
		if (land.hit) {
			HitInfo h;
			h.kind = "terrain";
			h.label = land.label;
			h.distance = land.distance;
			h.point = land.point;
			hits.push_back(std::move(h));
		}
		if (water.hit) {
			HitInfo h;
			h.kind = "water";
			h.label = water.label;
			h.distance = water.distance;
			h.point = water.point;
			hits.push_back(std::move(h));
		}
		SortHits(hits);

		const float partDistance = best >= 0 ? objects[static_cast<std::size_t>(best)].score : std::numeric_limits<float>::max();
		enum class Chosen { kNone, kPart, kLand, kWater };
		Chosen chosen = Chosen::kNone;
		float  nearest = std::numeric_limits<float>::max();
		if (best >= 0) {
			chosen = Chosen::kPart;
			nearest = partDistance;
		}
		if (land.hit && land.distance < nearest) {
			chosen = Chosen::kLand;
			nearest = land.distance;
		}
		if (water.hit && water.distance < nearest) {
			chosen = Chosen::kWater;
			nearest = water.distance;
		}

		switch (chosen) {
		case Chosen::kPart: {
			auto& o = objects[static_cast<std::size_t>(best)];
			// re-read the winner with its report, reporting the part this scan chose
			PickInfo info = InspectRef(o.ref, a_ray, false, true, o.shape);
			if (IsWaterPlane(info)) {
				// the cell's runtime water plane (an activator whose one part is DefaultProceduralWater): shown as water, not as an object
				const Vec3 point = a_ray.origin + a_ray.dir * partDistance;
				FillWater(result, point);
				result.hitIndex = IndexOfPartHit(hits, info.object.refFormId, info.clicked);
				result.hits = std::move(hits);
				result.fromRay = true;
				result.notes.push_back(std::format("the ray met the cell's water plane: {} ({:08X})", info.object.name, info.object.refFormId));
				return result;
			}
			info.method = o.exact ? "exact" : "approximate";
			info.notes.push_back(std::format("crosshair pick: {} candidate object(s) along the ray, {} hit(s) in all", objects.size(), hits.size()));
			info.hits = std::move(hits);
			info.hitIndex = IndexOfPartHit(info.hits, info.object.refFormId, info.clicked);
			info.fromRay = true;
			return info;
		}
		case Chosen::kLand:
			FillTerrain(result, land.point);
			result.hits = std::move(hits);
			result.hitIndex = IndexOfHit(result.hits, "terrain");
			result.fromRay = true;
			return result;
		case Chosen::kWater:
			FillWater(result, water.point);
			result.hits = std::move(hits);
			result.hitIndex = IndexOfHit(result.hits, "water");
			result.fromRay = true;
			return result;
		case Chosen::kNone:
			break;
		}
		if (a_mode == PickMode::kGround || a_mode == PickMode::kWater) {
			result.valid = false;
			result.message = a_mode == PickMode::kGround ? "no loaded land under the crosshair" : "no water surface under the crosshair";
			result.hits = std::move(hits);
			return result;
		}
		// nothing in front of the crosshair: the sky
		FillSky(result);
		result.hits = std::move(hits);
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
			FillWater(result, hit.point);
		} else {
			auto* form = RE::TESForm::GetFormByID(hit.refFormId);
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
