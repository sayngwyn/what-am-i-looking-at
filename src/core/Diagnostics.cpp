// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
#include "Diagnostics.h"

#include "CardLayout.h"

#include <algorithm>
#include <cctype>
#include <format>

namespace ti
{
	namespace
	{
		std::string Lower(std::string_view a_text)
		{
			std::string s(a_text);
			for (auto& c : s)
				c = c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		}

		bool HasFlag(const ShapeInfo& a_shape, std::string_view a_flag)
		{
			return std::find(a_shape.flags.begin(), a_shape.flags.end(), a_flag) != a_shape.flags.end();
		}

		bool IsNormalSlot(const TextureSlot& a_slot, const ShapeInfo& a_shape)
		{
			if (a_shape.shaderKind.find("Lighting") == std::string::npos)
				return false;
			return a_slot.slot == 1;
		}

		std::string Dims(const GpuInfo& a_gpu) { return std::format("{}x{}", a_gpu.width, a_gpu.height); }

		bool IsUncompressed(const GpuInfo& a_gpu) { return a_gpu.present && !DxgiIsCompressed(a_gpu.dxgi); }

		std::string Name(const TextureSlot& a_slot)
		{
			const auto pos = a_slot.path.find_last_of("\\/");
			return pos == std::string::npos ? a_slot.path : a_slot.path.substr(pos + 1);
		}
	}

	bool PathEndsWith(std::string_view a_path, std::string_view a_suffix)
	{
		const std::string p = Lower(a_path), s = Lower(a_suffix);
		return p.size() >= s.size() && p.compare(p.size() - s.size(), s.size(), s) == 0;
	}

	bool LooksLikePbrPath(std::string_view a_path)
	{
		const std::string p = Lower(a_path);
		return p.find("textures\\pbr\\") != std::string::npos || PathEndsWith(p, "_rmaos.dds");
	}

	namespace
	{
		bool NotFound(const FileOrigin& a_origin) { return a_origin.looked && a_origin.source == "not found"; }

		/**
		 * Fallout 4's rules. A vanilla object must never warn, so each rule fires only on a fact the game's own files cannot explain:
		 *  - material-not-found: the part names a .bgsm/.bgem file and no loose file or loaded archive holds it.
		 *  - texture-not-loaded: the part names a texture the engine has not loaded AND no loose file or archive holds it (a texture that
		 *    exists but is not loaded yet is just not streamed in, so it is not a fault); or the engine loaded it with no GPU resource.
		 *  - env-map-missing: the material names an environment cube map (texture slot 4 of a lit shader) and the file is not there.
		 *    An environment map that names no texture is NOT a fault: Fallout 4 then uses the cell's or the game's default cube map, and
		 *    nearly every vanilla surface with the Environment Map flag works that way.
		 *  - normal-uncompressed, normal-one-mip, one-mip-large: as on Skyrim, but only for textures of 64 pixels or more.
		 * Dropped for Fallout 4, because it has no such thing: parallax-no-height and height-without-flag (parallax is a material
		 * feature, not a flag with a height slot), pbr-without-flag and pbr-flag-without-texture (no TruePBR), env-no-cubemap (see above).
		 */
		std::vector<Diagnostic> DiagnoseFo4(const ShapeInfo& a_shape)
		{
			std::vector<Diagnostic> out;
			const auto              add = [&](const char* a_rule, std::string a_text) { out.push_back({ a_rule, std::move(a_text) }); };
			const bool              lit = a_shape.shaderKind.find("Lighting") != std::string::npos;

			if (!a_shape.materialPath.empty() && NotFound(a_shape.materialOrigin))
				add("material-not-found", std::format("The material file {} is named by this part but is in no loose file or loaded archive, so the part draws with the game's fallback look.", DisplayPath(a_shape.materialPath)));

			for (const auto& t : a_shape.textures) {
				const std::string label = t.label.empty() ? Name(t) : std::format("{} ({})", t.label, Name(t));
				const bool        mustBind = !lit || t.slot == 0 || t.slot == 1;
				if (!t.path.empty() && mustBind && !t.bound && NotFound(t.origin))
					add("texture-not-loaded", std::format("{}: the texture file is in no loose file or loaded archive, so the part draws with a default texture.", label));
				else if (t.bound && !t.gpu.present)
					add("texture-not-loaded", std::format("{}: loaded by the engine but there is no GPU texture ({}).", label, t.gpu.note.empty() ? "unknown reason" : t.gpu.note));
				if (lit && t.slot == 4 && !t.path.empty() && NotFound(t.origin))
					add("env-map-missing", std::format("{}: the environment cube map the material names is in no loose file or loaded archive, so reflections will be wrong.", label));

				if (!t.gpu.present)
					continue;
				const int longest = std::max(t.gpu.width, t.gpu.height);
				if (longest < 64)
					continue;
				if (lit && t.slot == 1) {
					if (IsUncompressed(t.gpu))
						add("normal-uncompressed", std::format("{}: normal map is uncompressed ({}, {}), so it uses far more video memory than needed.", label, t.gpu.format, Dims(t.gpu)));
					if (t.gpu.mips == 1)
						add("normal-one-mip", std::format("{}: normal map has only 1 mip level ({}), so it will shimmer at a distance.", label, Dims(t.gpu)));
				} else if (t.gpu.mips == 1 && longest > 256) {
					add("one-mip-large", std::format("{}: has only 1 mip level at {}, so it will shimmer at a distance.", label, Dims(t.gpu)));
				}
			}
			return out;
		}
	}

	std::vector<Diagnostic> Diagnose(const ShapeInfo& a_shape, DiagGame a_game)
	{
		if (a_game == DiagGame::kFallout4)
			return DiagnoseFo4(a_shape);
		std::vector<Diagnostic> out;
		const auto              add = [&](const char* a_rule, std::string a_text) { out.push_back({ a_rule, std::move(a_text) }); };

		const bool lit = a_shape.shaderKind.find("Lighting") != std::string::npos;
		const bool truePbr = a_shape.featureId == 32 || a_shape.featureId == 33;
		const bool parallaxFlag = HasFlag(a_shape, "Parallax") || HasFlag(a_shape, "ParallaxOcclusion");
		const bool parallaxFeature = a_shape.featureId == 3 || a_shape.featureId == 7;
		// Slot 3 of the texture set only means "height / parallax" for the plain lit shaders: Default, Environment Map, Glow Map and the
		// two Parallax kinds (a part whose feature number is not known, -1, counts as one). FaceGen, skin tint, hair tint, eye and
		// the multilayer shaders use slots 3 to 5 for their own things (a vanilla head carries BlankDetailmap.dds in slot 3), so the
		// parallax and environment-map rules below do not apply to them.
		const bool plainLit = lit && !truePbr &&
		                      (a_shape.featureId < 0 || a_shape.featureId == 0 || a_shape.featureId == 1 || a_shape.featureId == 2 || a_shape.featureId == 3 ||
								  a_shape.featureId == 7);

		bool hasHeightMap = a_shape.heightBound;
		bool hasRmaos = false;
		bool hasCube = false;
		bool hasEnvMask = false;
		const TextureSlot* firstHeight = nullptr;
		const TextureSlot* firstRmaos = nullptr;
		for (const auto& t : a_shape.textures) {
			// The NIF texture-set order of a lit shader: 0 diffuse, 1 normal, 2 glow / skin tint, 3 height, 4 environment cube map,
			// 5 environment mask, 6 multilayer inner layer, 7 backlight / specular. A path in slot 3 is a height map, in slot 4 a cube map.
			const bool litSlot = plainLit;
			if ((litSlot && t.slot == 3 && !t.path.empty()) || PathEndsWith(t.path, "_p.dds")) {
				hasHeightMap = true;
				if (!firstHeight)
					firstHeight = &t;
			}
			if (litSlot && t.slot == 4 && !t.path.empty())
				hasCube = true;
			if (litSlot && t.slot == 5 && !t.path.empty())
				hasEnvMask = true;
			if (PathEndsWith(t.path, "_rmaos.dds")) {
				hasRmaos = true;
				if (!firstRmaos)
					firstRmaos = &t;
			}
			if (t.gpu.cube || Lower(t.path).find("cubemaps\\") != std::string::npos || PathEndsWith(t.path, "_e.dds"))
				hasCube = true;
			if (PathEndsWith(t.path, "_m.dds"))
				hasEnvMask = true;
		}
		(void)hasEnvMask;

		// RULE parallax-no-height: the Parallax / Parallax Occlusion flag (or shader type) is set but the part has no height map
		// (nothing in texture slot 3, no "_p.dds" texture in its set and no height texture bound by the material). The surface will not parallax.
		if (plainLit && (parallaxFlag || parallaxFeature) && !hasHeightMap)
			add("parallax-no-height", "The shader asks for parallax but there is no height map (_p texture), so the surface will look flat.");

		// RULE height-without-flag: a height map (a path in slot 3, or a "_p.dds" texture) is in the set, but neither the Parallax nor the Parallax
		// Occlusion flag is on, and the shader is not TruePBR (where _p is the displacement map), so the height map is never used.
		if (plainLit && !parallaxFlag && !parallaxFeature && firstHeight)
			add("height-without-flag", std::format("A height map ({}) is in the texture set but the Parallax flag is off, so it is not used.", Name(*firstHeight)));

		// RULE pbr-without-flag: an "_rmaos.dds" texture is in the set but the shader is not the TruePBR kind, so it draws as a plain texture.
		if (lit && !truePbr && firstRmaos)
			add("pbr-without-flag", std::format("PBR texture {} is in the set but the shader is not TruePBR, so it renders as an ordinary texture.", Name(*firstRmaos)));

		// RULE pbr-flag-without-texture: the shader is TruePBR but the set has no "_rmaos.dds" texture (nothing gives it roughness or metalness).
		if (lit && truePbr && !hasRmaos)
			add("pbr-flag-without-texture", "The shader is TruePBR but there is no _rmaos texture (roughness, metalness, AO, specular) in the set.");

		// RULE env-no-cubemap: the Environment Map flag is on but no cube map is bound or named (nothing in slot 4, no cube texture,
		// nothing under textures\cubemaps\, no "_e.dds"). Skipped for TruePBR, which does not use one.
		if (plainLit && HasFlag(a_shape, "EnvMap") && !hasCube && !a_shape.textures.empty())
			add("env-no-cubemap", "The shader has the Environment Map flag on but no cube map texture, so reflections will not work.");

		for (const auto& t : a_shape.textures) {
			const std::string label = t.label.empty() ? Name(t) : std::format("{} ({})", t.label, Name(t));

			// RULE texture-not-loaded: the part names a texture, but the engine has no GPU texture for it. Either no texture object
			// was bound (only checked for the slots a lit shader always binds: diffuse and normal, and for effect shaders every slot),
			// or the object has no GPU resource. The part will draw with a default or missing texture.
			const bool mustBind = !lit || t.slot == 0 || t.slot == 1;
			if (!t.path.empty() && mustBind && !t.bound)
				add("texture-not-loaded", std::format("{}: the engine has no texture loaded for this slot (file missing or failed to load).", label));
			else if (t.bound && !t.gpu.present)
				add("texture-not-loaded", std::format("{}: loaded by the engine but there is no GPU texture ({}).", label, t.gpu.note.empty() ? "unknown reason" : t.gpu.note));

			if (!t.gpu.present)
				continue;
			const bool big = std::max(t.gpu.width, t.gpu.height) > 256;
			if (IsNormalSlot(t, a_shape)) {
				// RULE normal-uncompressed: a normal map stored uncompressed (not BC5/BC7/BC3...) costs four times the memory for no gain.
				if (IsUncompressed(t.gpu) && std::max(t.gpu.width, t.gpu.height) >= 64)  // tiny flat defaults are fine
					add("normal-uncompressed", std::format("{}: normal map is uncompressed ({}, {}), so it uses far more video memory than needed.", label, t.gpu.format, Dims(t.gpu)));
				// RULE normal-one-mip: a normal map with a single mip level shimmers and is sharper than it should be at a distance.
				if (t.gpu.mips == 1)
					add("normal-one-mip", std::format("{}: normal map has only 1 mip level ({}), so it will shimmer at a distance.", label, Dims(t.gpu)));
			} else if (t.gpu.mips == 1 && big) {
				// RULE one-mip-large: any other texture larger than 256 pixels with a single mip level (no mip chain) shimmers at a distance.
				add("one-mip-large", std::format("{}: has only 1 mip level at {}, so it will shimmer at a distance.", label, Dims(t.gpu)));
			}
		}
		return out;
	}
}
