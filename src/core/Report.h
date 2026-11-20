// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// The plain data of the Report beyond the per-shape basics in PickData.h: identity, facts, GPU texture info, file origin,
// the NPC section and diagnostics. No game types in here: the game-reading code fills these, the printers and the
// diagnostics only read them, so the offline tests can build them by hand.
#pragma once

#include "Pick.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ti
{
	/** The two FaceGen verdicts (FaceGenInfo::verdict). Other values: "unknown". */
	inline constexpr const char* kFaceGenFound = "file found";
	inline constexpr const char* kFaceGenMissing = "missing: the game builds the face at runtime (dark-face risk)";

	/** Bumped when a field changes meaning or goes away (new fields do not bump it). */
	constexpr int kReportVersion = 1;

	/** What the GPU holds for one texture (read from the D3D11 resource the engine bound). */
	struct GpuInfo
	{
		bool          present = false;  ///< false: not read, see note
		std::string   note;             ///< why nothing was read ("no GPU texture bound", "not a 2D texture" ...)
		int           width = 0;
		int           height = 0;
		int           mips = 0;
		int           arraySize = 1;
		int           dxgi = 0;     ///< DXGI_FORMAT value
		std::string   format;       ///< readable name: BC7, BC5, BC1, BC3, R8G8B8A8 ...
		bool          cube = false;  ///< a cube map
		std::uint64_t bytes = 0;    ///< estimated size of all mips and array slices
	};

	/** Where the file a texture (or model) came from lives. */
	struct FileOrigin
	{
		bool        looked = false;
		std::string source;   ///< "loose", "archive", "not found" or "" when not looked up
		std::string archive;  ///< file name of the winning archive (source == "archive") or of the mod's archive
		std::string mod;      ///< MO2 mod folder name when the OS reveals it; empty otherwise
		std::string realPath; ///< the OS's own path for a loose file; empty when unknown
		std::string note;     ///< why something is missing
	};

	/** A finding in plain English. rule names the check that raised it (see Diagnostics.cpp). */
	struct Diagnostic
	{
		std::string rule;
		std::string text;
	};

	/** One ordered label/value pair of the "facts" list. */
	struct Fact
	{
		std::string label;
		std::string value;
	};

	/** One form (reference or base) with the plugins that touch it. */
	struct FormRecord
	{
		std::uint32_t            formId = 0;
		std::string              type;
		std::string              editorId;  ///< empty when the game dropped it and no editor ID source is present
		std::string              addedBy;   ///< plugin that created the record (first source file)
		std::string              lastChangedBy;  ///< last plugin in the chain
		std::vector<std::string> chain;     ///< every plugin that has a version of it, in load order
	};

	struct Identity
	{
		std::string kind = "object";  ///< object, npc (terrain, water, sky come later)
		std::string displayName;
		FormRecord  ref;
		FormRecord  base;
		std::string baseName;  ///< the base record's own name (may differ from the reference's display name)

		std::string cellName;
		std::string cellEditorId;
		std::uint32_t cellFormId = 0;
		bool        interior = false;
		std::string worldspaceName;
		std::string worldspaceEditorId;
		bool        persistent = false;  ///< the ref lives in the worldspace's persistent cell; the cell fields then name the exterior cell it stands in
		bool        hasCoords = false;  ///< exterior cell grid coordinates known
		int         cellX = 0;
		int         cellY = 0;
		Vec3        position;
		Vec3        rotationDegrees;
		float       scale = 1.0f;
		std::string editorIdSource;  ///< "engine", "po3 Tweaks" or "" (says where editor IDs come from)
		FileOrigin  modelOrigin;     ///< where the base model's file lives
	};

	/** A texture a worn item or head part brings (paths only; GPU detail is in the parts list when it is picked). */
	struct NpcTexture
	{
		std::string shape;
		std::string slotLabel;
		std::string path;
		GpuInfo     gpu;
	};

	struct WornItem
	{
		std::vector<int> slots;  ///< biped slot numbers (Skyrim: 30 head, 32 body ...; Fallout 4: 30 hair top, 33 body ...)
		bool             fallout4 = false;  ///< the slot numbers are Fallout 4's, which name them differently
		std::string      name;
		std::uint32_t    itemFormId = 0;
		std::string      itemPlugin;
		std::uint32_t    addonFormId = 0;
		std::string      addonPlugin;
		std::string      model;  ///< the armor addon's model for the actor's sex
		bool             live3d = false;  ///< textures were read from the live 3D
		std::vector<NpcTexture> textures;
		std::vector<std::string> materials;  ///< Fallout 4: the .bgsm/.bgem files the item's parts were built from
		std::vector<std::string> mods;       ///< Fallout 4: the object modifications (OMODs) on the item, "name (form id)"
	};

	struct HeadPartInfo
	{
		std::string              type;  ///< Misc, Face, Eyes, Hair, FacialHair, Scar, Eyebrows
		std::uint32_t            formId = 0;
		std::string              editorId;
		std::string              name;
		std::string              model;
		std::uint32_t            textureSetFormId = 0;
		std::vector<std::string> textureSetPaths;  ///< non-empty slots of the head part's texture set
	};

	struct FaceGenInfo
	{
		bool        looked = false;
		std::string meshPath;       ///< where the FaceGen mesh would be (Data-relative)
		FileOrigin  meshOrigin;
		std::string tintPath;
		FileOrigin  tintOrigin;
		std::string verdict;        ///< "file found", "missing: the game builds the face at runtime (dark-face risk)", "unknown"
		std::string evidence;       ///< how the verdict was reached, plain words
		std::string liveTintTexture;  ///< name of the tint texture bound on the head shape, when read
		int         renderTargetIndex = -2;  ///< diffuseRenderTargetSourceIndex of the head shape's material (-2 = not read)
	};

	/** The actor's skin: the NPC's own skin armor (WNAM) or else its race's. Never listed among the worn items. */
	struct SkinInfo
	{
		bool                  present = false;
		std::string           name;
		std::uint32_t         formId = 0;
		std::string           plugin;
		std::string           source;  ///< "the NPC's own skin (WNAM)" or "the race's skin"
		std::vector<WornItem> parts;   ///< one entry per armor addon the skin has on this actor (body, hands, feet, head ...)
	};

	struct NpcSection
	{
		bool        present = false;
		std::string sex;
		std::string race;
		std::string skin;        ///< WNAM armor (name and id)
		SkinInfo    skinItem;    ///< the skin armor with its addons, models and live textures
		std::string skinToneColor;  ///< #RRGGBB
		std::string skeleton;
		std::uint32_t faceNpcFormId = 0;  ///< the NPC whose face this one wears (itself when no template)
		FaceGenInfo faceGen;
		std::vector<WornItem>     worn;
		std::vector<HeadPartInfo> headParts;
	};

	// ---- terrain, water, sky, hits ------------------------------------------------------------------------------------

	/** One layer of the land texture blend at the hit: the base texture or one of up to six blended layers. */
	struct TerrainLayer
	{
		std::string              role;  ///< "base" or "layer 1" .. "layer 6"
		std::uint32_t            formId = 0;  ///< the LTEX record
		std::string              editorId;
		std::string              name;        ///< the LTEX's own name when it has one (else the editor ID stands in)
		std::string              addedBy;
		std::string              lastChangedBy;
		std::vector<std::string> chain;
		std::vector<Fact>        textures;  ///< the LTEX's texture set: label (diffuse, normal ...) and path
		std::vector<std::string> grasses;   ///< grass records the layer spawns ("Name (EDID, 00012345)")
		std::vector<std::string> grassNames;  ///< the same grasses by their model's file name ("FieldGrass01"), for the card
		std::string              footstepMaterial;  ///< the LTEX's material type (footstep sounds and impacts follow it)
		int                      rawPercent = -1;   ///< the engine's stored value at the hit (-1 for the base)
		float                    percent = 100.0f;  ///< blend at the hit, 0..100
	};

	struct TerrainSection
	{
		bool          present = false;
		std::uint32_t cellFormId = 0;
		std::string   cellName;
		std::string   cellEditorId;
		int           cellX = 0;
		int           cellY = 0;
		std::string   worldspace;
		int           quadrant = -1;  ///< 0 south-west, 1 south-east, 2 north-west, 3 north-east
		std::string   quadrantName;
		int           vertexX = -1;   ///< the vertex nearest the hit, 0..16 within the quadrant
		int           vertexY = -1;
		Vec3          vertexPosition;
		Vec3          point;          ///< where the ray met the land
		float         heightAtHit = 0.0f;
		std::string   blendMethod;    ///< "interpolated across the four vertices around the hit" or "nearest vertex"
		std::string   heightCheck;    ///< how our ray against the stored heights compares with the engine's own land height there
		std::vector<TerrainLayer> layers;  ///< the base first, then the blended layers, strongest first
		std::string   footstepMaterial;  ///< what the ground sounds like: the strongest layer's material type
		std::string   note;
	};

	struct WaterSection
	{
		bool          present = false;
		FormRecord    type;          ///< the water type (WATR) record, with plugin and chain
		std::string   typeName;
		std::string   source;        ///< "the cell's water type", "the worldspace's default water" or "a placed water plane"
		float         height = 0.0f; ///< water surface height at the hit
		bool          hasCellHeight = false;
		float         cellHeight = 0.0f;  ///< the cell's own water level (XCLW)
		std::string   cellVersusPlane;    ///< says whether the surface is the cell's water or a placed plane, and why
		std::uint32_t planeRefFormId = 0;
		std::uint32_t planeBaseFormId = 0;
		std::string   planeName;
		std::vector<Fact> noiseTextures;  ///< the water type's noise / normal map paths
		std::vector<Fact> liveTextures;   ///< the normal maps the water shader on the surface really holds
		std::string   shallowColor;       ///< "#RRGGBB"
		std::string   deepColor;
		std::string   reflectionColor;
		std::vector<Fact> values;         ///< alpha, fresnel, refraction, specular, flags ...
		Vec3          point;
		std::string   cellName;
		std::uint32_t cellFormId = 0;
		std::string   note;
	};

	struct CloudLayerInfo
	{
		std::string weather;   ///< "current" or "previous"
		int         index = 0;
		std::string texture;
		int         speedX = 0;
		int         speedY = 0;
		float       alpha = -1.0f;  ///< live alpha when the engine's cloud object was read, else -1
	};

	struct SkySection
	{
		bool        present = false;
		std::string mode;  ///< "exterior", "interior with sky", "interior"
		float       hour = 0.0f;
		std::string hourText;  ///< "13:05"
		FormRecord  currentWeather;
		FormRecord  previousWeather;
		float       transitionPercent = -1.0f;  ///< 0..100 how far the game has moved from the previous weather to the current; -1 unknown
		std::string weatherClass;  ///< pleasant, cloudy, rainy, snow ...
		std::string previousClass;  ///< the same for the weather it is changing from
		FormRecord  climate;
		std::vector<CloudLayerInfo> clouds;
		std::vector<Fact>           objects;  ///< sun, sun glare, moons, precipitation: label and texture path
		std::string imageSpace;       ///< the image space in use for this time of day (or the cell's)
		std::string imageSpaceSource; ///< where it comes from
		std::string lightingTemplate; ///< an interior's lighting template (empty outdoors)
		std::string cellName;
		std::string note;
	};

	/** One thing the ray meets, for stepping through. */
	struct HitInfo
	{
		std::string   kind;   ///< "part", "terrain", "water"
		std::string   label;  ///< one line a person can read
		float         distance = 0.0f;  ///< from the camera along the ray
		bool          exact = true;     ///< false: ranked by bounds only
		Vec3          point;
		std::uint32_t refFormId = 0;    ///< for a part: the reference it belongs to
		int           shapeIndex = -1;  ///< for a part: its index in that object's part list
		std::string   partName;
	};

	// ---- pure helpers (Report.cpp) ----------------------------------------------------------------------------------

	/** Plain name for a DXGI_FORMAT value ("BC7", "BC5", "R8G8B8A8" ...); "DXGI 123" for one that is not in the table. */
	std::string DxgiFormatName(int a_dxgi);
	/** True for the block-compressed formats (BC1..BC7). */
	bool DxgiIsCompressed(int a_dxgi);
	/** Bits per pixel for uncompressed formats, bits per 4x4 block / 16 for compressed ones; 0 when unknown. */
	int DxgiBitsPerPixel(int a_dxgi);
	/** Estimated bytes for a full mip chain of a_array slices (block rounding for compressed formats). 0 when the format is unknown. */
	std::uint64_t EstimateTextureBytes(int a_width, int a_height, int a_mips, int a_array, int a_dxgi);
	/** "1.4 MB", "820 KB", "96 B". */
	std::string HumanBytes(std::uint64_t a_bytes);

	/** The MO2 mod folder name when a_realPath lies under "...\mods\<name>\...", else empty. Case-insensitive on "mods". */
	std::string ModFolderFromPath(std::string_view a_realPath);

	/** "body", "head", "hands" ... for a biped/body slot number (30..61); "slot N" when it has no usual name. */
	std::string BodySlotName(int a_slot);

	/** The same for Fallout 4's biped slots (30 hair top, 33 body, 36 [U] torso ...); "slot N" when it has no usual name. */
	std::string Fo4BodySlotName(int a_slot);

	/** The name of one of a worn item's slots, in the item's own game's words. */
	std::string SlotNameOf(const WornItem& a_item, int a_slot);

	/** "#RRGGBB". */
	std::string HexColor(int a_r, int a_g, int a_b);
}
