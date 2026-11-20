// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// Fallout 4's names for what a part holds: material file path, shader flags, vertex attributes, texture slots. Pure functions, no
// game types: the Fallout 4 reader passes in the engine's numbers and strings, so the offline tests cover the rules.
#pragma once

#include "PickData.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ti
{
	/**
	 * a_text trimmed and, when it names a Fallout 4 material file (ends in .bgsm or .bgem, any case), returned as written; otherwise
	 * empty. In the NIF files the shader property's Name is a string-table entry holding the material path (for example
	 * "Materials\\SetDressing\\Crates\\WoodenCrate01Dest.BGSM"); a shader with no Name has its values stored in the NIF itself.
	 */
	std::string Fo4MaterialFile(std::string_view a_text);

	/** Where a part's material file path was found, in the words shown on the card and in the JSON. */
	inline constexpr const char* kMaterialFromShaderName = "shader property name";
	inline constexpr const char* kMaterialFromRootName = "shader property root name";

	/**
	 * Fills a_shape's material fields from what the engine holds: a_shaderName is the shader property's name, a_rootName the lit
	 * shader's root name (empty for effect shaders). The path is taken from the name when it names a material file, else from the
	 * root name; a root name that differs from the chosen path is kept as rootMaterial. With neither, materialSource says what was
	 * seen (the name text, or "no name": the values live in the NIF).
	 */
	void ApplyFo4Material(ShapeInfo& a_shape, std::string_view a_shaderName, std::string_view a_rootName);

	/** Shader property flag names that matter for texture work, for a 64-bit EShaderPropertyFlag value. Order: bit order. */
	std::vector<std::string> Fo4ShaderFlagNames(std::uint64_t a_flags);
	/** Vertex attributes a vertex description holds. a_vertexFlags is the description's flag bits (desc >> 44): the VF_ values. */
	std::vector<std::string> Fo4VertexFlagNames(std::uint16_t a_vertexFlags);
	/** True when positions are stored as three 32-bit floats (VF_FULLPREC, 0x400); otherwise three half floats. */
	inline bool Fo4FullPrecision(std::uint16_t a_vertexFlags) { return (a_vertexFlags & 0x400u) != 0; }
	/** Bytes of one vertex: the low nibble of the description, times four. */
	inline std::uint32_t Fo4VertexStride(std::uint64_t a_desc) { return static_cast<std::uint32_t>(a_desc & 0xF) * 4u; }

	/** Meaning of a lit shader's texture slot (BSTextureSet index): diffuse, normal, glow, height ... */
	const char* Fo4LitSlotLabel(int a_slot);
	/** Meaning of an effect shader's texture (0 source, 1 greyscale, 2 environment, 3 environment mask, 4 normal). */
	const char* Fo4EffectSlotLabel(int a_slot);
}
