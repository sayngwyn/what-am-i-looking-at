// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
#include "Fo4Names.h"

#include <algorithm>
#include <cctype>

namespace ti
{
	namespace
	{
		std::string_view Trimmed(std::string_view a_text)
		{
			while (!a_text.empty() && (a_text.front() == ' ' || a_text.front() == '\t' || a_text.front() == '\0'))
				a_text.remove_prefix(1);
			while (!a_text.empty() && (a_text.back() == ' ' || a_text.back() == '\t' || a_text.back() == '\0'))
				a_text.remove_suffix(1);
			return a_text;
		}

		bool EndsWithNoCase(std::string_view a_text, std::string_view a_suffix)
		{
			if (a_text.size() < a_suffix.size())
				return false;
			const auto tail = a_text.substr(a_text.size() - a_suffix.size());
			return std::equal(tail.begin(), tail.end(), a_suffix.begin(), [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; });
		}

		bool Same(std::string_view a, std::string_view b)
		{
			return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); });
		}

		struct FlagName
		{
			int         bit;
			const char* name;
		};

		// EShaderPropertyFlag bits (CommonLibF4's names) that say something about how a part looks; engine bookkeeping bits are left out.
		constexpr FlagName kShaderFlags[] = {
			{ 0, "Specular" },
			{ 1, "Skinned" },
			{ 2, "TempRefraction" },
			{ 3, "VertexAlpha" },
			{ 4, "GrayscaleToPaletteColor" },
			{ 5, "GrayscaleToPaletteAlpha" },
			{ 6, "Falloff" },
			{ 7, "EnvMap" },
			{ 8, "RGBFalloff" },
			{ 9, "CastShadows" },
			{ 10, "Face" },
			{ 12, "ModelSpaceNormals" },
			{ 14, "MultiTextureLandscape" },
			{ 15, "Refraction" },
			{ 17, "EyeReflect" },
			{ 18, "HairTint" },
			{ 19, "ScreendoorAlphaFade" },
			{ 21, "FaceGenRGBTint" },
			{ 22, "OwnEmit" },
			{ 23, "ProjectedUV" },
			{ 24, "MultipleTextures" },
			{ 25, "Tesselate" },
			{ 26, "Decal" },
			{ 27, "DynamicDecal" },
			{ 29, "ExternalEmittance" },
			{ 30, "SoftEffect" },
			{ 33, "LODLandscape" },
			{ 34, "LODObjects" },
			{ 36, "TwoSided" },
			{ 37, "VertexColors" },
			{ 38, "GlowMap" },
			{ 41, "Tint" },
			{ 45, "Billboard" },
			{ 51, "PremultAlpha" },
			{ 53, "AnisotropicLighting" },
			{ 56, "MultiLayerParallax" },
			{ 57, "AlphaTest" },
			{ 61, "TreeAnim" },
			{ 62, "EffectLighting" },
		};
	}

	std::string Fo4MaterialFile(std::string_view a_text)
	{
		const auto t = Trimmed(a_text);
		if (t.empty())
			return {};
		if (EndsWithNoCase(t, ".bgsm") || EndsWithNoCase(t, ".bgem"))
			return std::string(t);
		return {};
	}

	void ApplyFo4Material(ShapeInfo& a_shape, std::string_view a_shaderName, std::string_view a_rootName)
	{
		a_shape.materialPath.clear();
		a_shape.materialSource.clear();
		a_shape.rootMaterial.clear();
		const std::string fromName = Fo4MaterialFile(a_shaderName);
		const std::string fromRoot = Fo4MaterialFile(a_rootName);
		if (!fromName.empty()) {
			a_shape.materialPath = fromName;
			a_shape.materialSource = kMaterialFromShaderName;
			if (!fromRoot.empty() && !Same(fromRoot, fromName))
				a_shape.rootMaterial = fromRoot;
		} else if (!fromRoot.empty()) {
			a_shape.materialPath = fromRoot;
			a_shape.materialSource = kMaterialFromRootName;
		} else {
			const auto name = Trimmed(a_shaderName);
			a_shape.materialSource = name.empty() ? std::string("no name: the material values are stored in the mesh file") : "the shader name is not a material file: \"" + std::string(name) + "\"";
		}
	}

	std::vector<std::string> Fo4ShaderFlagNames(std::uint64_t a_flags)
	{
		std::vector<std::string> out;
		for (const auto& f : kShaderFlags)
			if ((a_flags >> f.bit) & 1u)
				out.emplace_back(f.name);
		return out;
	}

	std::vector<std::string> Fo4VertexFlagNames(std::uint16_t a_vertexFlags)
	{
		std::vector<std::string> out;
		if (a_vertexFlags & 0x002u)
			out.emplace_back("UV");
		if (a_vertexFlags & 0x004u)
			out.emplace_back("UV2");
		if (a_vertexFlags & 0x008u)
			out.emplace_back("Normals");
		if (a_vertexFlags & 0x010u)
			out.emplace_back("Tangents");
		if (a_vertexFlags & 0x020u)
			out.emplace_back("Colors");
		if (a_vertexFlags & 0x040u)
			out.emplace_back("Skinned");
		if (a_vertexFlags & 0x080u)
			out.emplace_back("LandData");
		if (a_vertexFlags & 0x100u)
			out.emplace_back("EyeData");
		if (a_vertexFlags & 0x400u)
			out.emplace_back("FullPrecision");
		return out;
	}

	const char* Fo4LitSlotLabel(int a_slot)
	{
		switch (a_slot) {
		case 0: return "diffuse";
		case 1: return "normal";
		case 2: return "glow";
		case 3: return "height / greyscale";
		case 4: return "environment";
		case 5: return "wrinkles";
		case 6: return "multilayer";
		case 7: return "smooth-spec";
		case 8: return "displacement";
		default: return "slot";
		}
	}

	const char* Fo4EffectSlotLabel(int a_slot)
	{
		switch (a_slot) {
		case 0: return "source";
		case 1: return "greyscale";
		case 2: return "environment";
		case 3: return "environment mask";
		case 4: return "normal";
		default: return "slot";
		}
	}
}
