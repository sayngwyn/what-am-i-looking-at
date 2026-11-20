// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
#include "Report.h"

#include <algorithm>
#include <cctype>
#include <format>

namespace ti
{
	namespace
	{
		struct DxgiRow
		{
			int         id;
			const char* name;
			int         bits;       // bits per pixel (uncompressed) or 0
			int         blockBytes; // bytes per 4x4 block (compressed) or 0
		};

		// DXGI_FORMAT values from the public d3d11 / dxgiformat.h enum. Only formats a game texture can plausibly have.
		constexpr DxgiRow kRows[] = {
			{ 2, "R32G32B32A32_FLOAT", 128, 0 },
			{ 10, "R16G16B16A16_FLOAT", 64, 0 },
			{ 11, "R16G16B16A16", 64, 0 },
			{ 16, "R32G32_FLOAT", 64, 0 },
			{ 24, "R10G10B10A2", 32, 0 },
			{ 26, "R11G11B10_FLOAT", 32, 0 },
			{ 27, "R8G8B8A8", 32, 0 },
			{ 28, "R8G8B8A8", 32, 0 },
			{ 29, "R8G8B8A8 (sRGB)", 32, 0 },
			{ 31, "R8G8B8A8 (snorm)", 32, 0 },
			{ 34, "R16G16_FLOAT", 32, 0 },
			{ 35, "R16G16", 32, 0 },
			{ 41, "R32_FLOAT", 32, 0 },
			{ 48, "R8G8", 16, 0 },
			{ 49, "R8G8", 16, 0 },
			{ 54, "R16_FLOAT", 16, 0 },
			{ 56, "R16", 16, 0 },
			{ 60, "R8", 8, 0 },
			{ 61, "R8", 8, 0 },
			{ 65, "A8", 8, 0 },
			{ 67, "R9G9B9E5", 32, 0 },
			{ 70, "BC1", 0, 8 },
			{ 71, "BC1", 0, 8 },
			{ 72, "BC1 (sRGB)", 0, 8 },
			{ 73, "BC2", 0, 16 },
			{ 74, "BC2", 0, 16 },
			{ 75, "BC2 (sRGB)", 0, 16 },
			{ 76, "BC3", 0, 16 },
			{ 77, "BC3", 0, 16 },
			{ 78, "BC3 (sRGB)", 0, 16 },
			{ 79, "BC4", 0, 8 },
			{ 80, "BC4", 0, 8 },
			{ 81, "BC4 (snorm)", 0, 8 },
			{ 82, "BC5", 0, 16 },
			{ 83, "BC5", 0, 16 },
			{ 84, "BC5 (snorm)", 0, 16 },
			{ 85, "B5G6R5", 16, 0 },
			{ 86, "B5G5R5A1", 16, 0 },
			{ 87, "B8G8R8A8", 32, 0 },
			{ 88, "B8G8R8X8", 32, 0 },
			{ 90, "B8G8R8A8", 32, 0 },
			{ 91, "B8G8R8A8 (sRGB)", 32, 0 },
			{ 92, "B8G8R8X8", 32, 0 },
			{ 93, "B8G8R8X8 (sRGB)", 32, 0 },
			{ 94, "BC6H", 0, 16 },
			{ 95, "BC6H", 0, 16 },
			{ 96, "BC6H (signed)", 0, 16 },
			{ 97, "BC7", 0, 16 },
			{ 98, "BC7", 0, 16 },
			{ 99, "BC7 (sRGB)", 0, 16 },
		};

		const DxgiRow* Row(int a_dxgi)
		{
			for (const auto& r : kRows)
				if (r.id == a_dxgi)
					return &r;
			return nullptr;
		}
	}

	std::string DxgiFormatName(int a_dxgi)
	{
		if (const auto* r = Row(a_dxgi))
			return r->name;
		return a_dxgi == 0 ? "unknown" : std::format("DXGI {}", a_dxgi);
	}

	bool DxgiIsCompressed(int a_dxgi)
	{
		const auto* r = Row(a_dxgi);
		return r && r->blockBytes > 0;
	}

	int DxgiBitsPerPixel(int a_dxgi)
	{
		const auto* r = Row(a_dxgi);
		if (!r)
			return 0;
		return r->bits ? r->bits : r->blockBytes * 8 / 16;
	}

	std::uint64_t EstimateTextureBytes(int a_width, int a_height, int a_mips, int a_array, int a_dxgi)
	{
		const auto* r = Row(a_dxgi);
		if (!r || a_width <= 0 || a_height <= 0 || a_mips <= 0)
			return 0;
		std::uint64_t total = 0;
		int           w = a_width, h = a_height;
		for (int m = 0; m < a_mips; ++m) {
			if (r->blockBytes) {
				total += static_cast<std::uint64_t>((w + 3) / 4) * static_cast<std::uint64_t>((h + 3) / 4) * static_cast<std::uint64_t>(r->blockBytes);
			} else {
				total += static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h) * static_cast<std::uint64_t>(r->bits) / 8u;
			}
			w = std::max(1, w / 2);
			h = std::max(1, h / 2);
		}
		return total * static_cast<std::uint64_t>(std::max(1, a_array));
	}

	std::string HumanBytes(std::uint64_t a_bytes)
	{
		if (a_bytes >= 1024ull * 1024ull)
			return std::format("{:.1f} MB", static_cast<double>(a_bytes) / (1024.0 * 1024.0));
		if (a_bytes >= 1024ull)
			return std::format("{:.0f} KB", static_cast<double>(a_bytes) / 1024.0);
		return std::format("{} B", a_bytes);
	}

	std::string ModFolderFromPath(std::string_view a_realPath)
	{
		std::vector<std::string> parts;
		std::string              cur;
		for (const char c : a_realPath) {
			if (c == '\\' || c == '/') {
				if (!cur.empty())
					parts.push_back(std::move(cur));
				cur.clear();
			} else {
				cur += c;
			}
		}
		if (!cur.empty())
			parts.push_back(std::move(cur));
		for (std::size_t i = 0; i + 2 < parts.size(); ++i) {  // needs a file below the mod folder, so i + 2
			std::string lower = parts[i];
			std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (lower == "mods")
				return parts[i + 1];
		}
		return {};
	}

	std::string BodySlotName(int a_slot)
	{
		switch (a_slot) {
		case 30: return "head";
		case 31: return "hair";
		case 32: return "body";
		case 33: return "hands";
		case 34: return "forearms";
		case 35: return "amulet";
		case 36: return "ring";
		case 37: return "feet";
		case 38: return "calves";
		case 39: return "shield";
		case 40: return "tail";
		case 41: return "long hair";
		case 42: return "circlet";
		case 43: return "ears";
		case 44: return "mouth";
		case 45: return "neck";
		case 46: return "chest (primary)";
		case 47: return "back";
		case 48: return "misc 1";
		case 49: return "pelvis (primary)";
		case 50: return "decapitated head";
		case 51: return "decapitate";
		case 52: return "pelvis (secondary)";
		case 53: return "right leg";
		case 54: return "left leg";
		case 55: return "face jewelry";
		case 56: return "chest (secondary)";
		case 57: return "shoulder";
		case 58: return "left arm";
		case 59: return "right arm";
		case 60: return "misc 2";
		case 61: return "FX";
		// skin partitions of heads use their own numbers (SBP_130_HEAD ...), not the biped slot numbers
		case 130: return "head";
		case 131: return "head (hair cap)";
		case 141: return "head (hair)";
		case 142: return "circlet";
		case 143: return "ears";
		case 150: return "head (decapitated)";
		case 230: return "neck (decapitated)";
		default: return std::format("slot {}", a_slot);
		}
	}

	std::string Fo4BodySlotName(int a_slot)
	{
		// the biped object numbers of Fallout 4 (the slot number is the index plus 30)
		switch (a_slot) {
		case 30: return "hair top";
		case 31: return "hair long";
		case 32: return "head (FaceGen)";
		case 33: return "body";
		case 34: return "left hand";
		case 35: return "right hand";
		case 36: return "torso (under)";
		case 37: return "left arm (under)";
		case 38: return "right arm (under)";
		case 39: return "left leg (under)";
		case 40: return "right leg (under)";
		case 41: return "torso (armor)";
		case 42: return "left arm (armor)";
		case 43: return "right arm (armor)";
		case 44: return "left leg (armor)";
		case 45: return "right leg (armor)";
		case 46: return "headband";
		case 47: return "eyes";
		case 48: return "beard";
		case 49: return "mouth";
		case 50: return "neck";
		case 51: return "ring";
		case 52: return "scalp";
		case 53: return "decapitation";
		case 59: return "shield";
		case 60: return "Pip-Boy";
		case 61: return "FX";
		default: return std::format("slot {}", a_slot);
		}
	}

	std::string SlotNameOf(const WornItem& a_item, int a_slot)
	{
		return a_item.fallout4 ? Fo4BodySlotName(a_slot) : BodySlotName(a_slot);
	}

	std::string HexColor(int a_r, int a_g, int a_b)
	{
		const auto c = [](int v) { return std::clamp(v, 0, 255); };
		return std::format("#{:02X}{:02X}{:02X}", c(a_r), c(a_g), c(a_b));
	}
}
