// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// One plain data struct per pick. The Inspector fills it, the console printer and the JSON writer only read it,
// so another consumer (an info panel, say) can read the same struct without touching the gathering code.
// No game types in here.
#pragma once

#include "Pick.h"
#include "Report.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ti
{
	struct TextureSlot
	{
		int         slot = 0;      ///< index in the shader's texture set (0..7); -1 for effect shaders
		std::string label;         ///< what the slot means for this shader kind ("diffuse", "normal", "RMAOS (PBR)" ...)
		std::string path;          ///< path in the texture set, exactly as loaded
		std::string loadedAs;      ///< name of the NiSourceTexture actually bound, when it differs from path
		bool        bound = false;  ///< the engine has a texture object for this slot
		GpuInfo     gpu;            ///< what the GPU holds (when bound)
		FileOrigin  origin;         ///< loose file or archive, and the mod folder when known
	};

	struct ShapeInfo
	{
		int         index = 0;
		std::string name;
		std::string nodeType;  ///< engine class name, e.g. BSTriShape, BSSubIndexTriShape
		bool        visible = true;

		// shader
		std::string              shaderKind;  ///< BSLightingShaderProperty, BSEffectShaderProperty, "none" ...
		std::string              shaderType;  ///< material feature: Default, EnvironmentMap, Parallax, TruePBR ...
		int                      featureId = -1;
		std::string              pbr;  ///< "no", "material" (TruePBR material), "path" (textures\pbr\ only)
		std::vector<std::string> flags;        ///< shader property flags that matter for texture work
		std::vector<std::string> vertexFlags;  ///< vertex attributes present: UV, Normals, Tangents, Colors, Skinned ...
		bool                     skinned = false;
		std::vector<TextureSlot> textures;  ///< non-empty slots only
		bool                     heightBound = false;  ///< the material class itself holds a height texture (Parallax materials)

		// Fallout 4: the material file (.bgsm lit, .bgem effect) the part's shader was built from
		std::string materialPath;    ///< e.g. "Materials\\SetDressing\\Crates\\WoodenCrate01Dest.BGSM"; empty when the part has none
		std::string materialSource;  ///< where the path was found, or why there is none
		std::string rootMaterial;    ///< the root material path when it differs from materialPath
		FileOrigin  materialOrigin;  ///< which archive (or loose file) the material file comes from

		// alpha (NiAlphaProperty)
		std::string alphaMode = "none";  ///< none, blend, test, blend+test
		int         alphaThreshold = -1; ///< 0..255 when testing
		std::string alphaTestFunction;   ///< e.g. "greater-equal"

		// material values (lit shaders)
		bool  hasMaterial = false;
		float specularColor[3] = { 1, 1, 1 };
		float specularStrength = 1.0f;  ///< specular color scale
		float glossiness = 1.0f;        ///< specular power
		bool  hasEmissive = false;
		float emissiveColor[3] = { 0, 0, 0 };
		float emissiveMultiple = 0.0f;
		float materialAlpha = 1.0f;
		float uvOffset[2] = { 0, 0 };
		float uvScale[2] = { 1, 1 };

		// which body area this part is (NPCs): from the skin partition slots, else the worn item's slot
		std::string      bodyArea;
		std::vector<int> partitionSlots;

		std::vector<Diagnostic> diagnostics;

		// FaceGen evidence (lit shaders): the face-tint texture the material holds and the render-target index it draws its diffuse from
		std::string tintTexture;
		int         renderTargetIndex = -2;  ///< diffuseRenderTargetSourceIndex; -2 = not read, -1 = none

		// geometry
		std::uint32_t vertices = 0;
		std::uint32_t triangles = 0;
		Vec3          translate;
		float         rotate[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };  ///< row-major, local -> world
		float         scale = 1.0f;
		Vec3          boundCentre;  ///< world space
		float         boundRadius = 0.0f;

		// picking
		bool        exactTested = false;
		std::string exactSkipReason;  ///< why triangles were not tested ("skinned", "no CPU copy" ...)
		bool        exactHit = false;
		float       exactDistance = 0.0f;
		Vec3        hitPoint;
		int         hitTriangle = -1;
		bool        boundHit = false;
		float       boundDistance = 0.0f;
		bool        clicked = false;
		int         rank = 0;  ///< 0 = clicked, 1.. = runners-up in order, -1 = not touched by the ray

		const TextureSlot* Find(int slot) const
		{
			for (const auto& t : textures)
				if (t.slot == slot)
					return &t;
			return nullptr;
		}
		/** The slot that stands in for "diffuse": lit slot 0, or an effect shader's source texture. */
		const TextureSlot* Diffuse() const { return textures.empty() ? nullptr : &textures.front(); }
	};

	struct ObjectInfo
	{
		std::string   name;
		std::uint32_t refFormId = 0;
		std::uint32_t baseFormId = 0;
		std::string   baseType;
		std::string   baseEditorId;  ///< empty when editor IDs are not loaded
		std::string   refEditorId;
		std::string   model;
		std::string   rootNode;
	};

	struct CursorInfo
	{
		bool  used = false;  ///< false for the crosshair (the lookingat command)
		float x = 0, y = 0;  ///< as the menu cursor reports them
		float areaW = 0, areaH = 0;
		float fracX = 0, fracY = 0;
	};

	struct PickInfo
	{
		// the pick itself
		std::uint64_t counter = 0;
		std::string   timestamp;  ///< local time, ISO 8601
		std::int64_t  unixMs = 0;
		std::string   trigger;  ///< "console_click", "console_command" or "step"
		std::string   plugin;   ///< name and version

		// what was picked
		bool       valid = false;  ///< false: nothing to report (the message says why)
		std::string message;       ///< reason when !valid
		ObjectInfo object;
		std::string method = "none";  ///< "exact", "approximate" or "none"
		int         clicked = -1;     ///< index into shapes
		std::vector<int> runnersUp;
		std::vector<ShapeInfo> shapes;

		// the Report (identity, facts, NPC, warnings)
		Identity                identity;
		std::vector<Fact>       facts;
		NpcSection              npc;
		TerrainSection          terrain;  ///< kind "terrain"
		WaterSection            water;    ///< kind "water"
		SkySection              sky;      ///< kind "sky"
		std::vector<Diagnostic> diagnostics;  ///< every part's warnings together, "part: text"

		// every hit along the ray, nearest first (parts of objects, terrain, water), for stepping through
		std::vector<HitInfo> hits;
		int                  hitIndex = -1;  ///< which entry of hits this report is about

		// how the ray was built
		Ray        ray;
		bool       hasRay = false;
		bool       fromRay = false;  ///< the thing was found by a ray (crosshair, click, step-through): only then does the card outline where it is
		CursorInfo cursor;
		std::string rayCheck;  ///< "verified", "y-flipped", "unverified" ...
		std::vector<std::string> notes;
	};
}
