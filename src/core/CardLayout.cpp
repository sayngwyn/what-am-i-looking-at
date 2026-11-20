// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
#include "CardLayout.h"

#include <cctype>
#include <cstring>
#include <format>

namespace ti
{
	namespace
	{
		std::string Lower(std::string_view a_text)
		{
			std::string out(a_text);
			for (auto& c : out)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return out;
		}

		/** The record type's everyday name, for the eyebrow. */
		std::string_view FriendlyType(std::string_view a_code)
		{
			struct Pair
			{
				std::string_view code, name;
			};
			static constexpr Pair table[] = {
				{ "STAT", "Static" }, { "MSTT", "Moveable static" }, { "DOOR", "Door" }, { "CONT", "Container" }, { "TREE", "Tree" },
				{ "NPC_", "Character" }, { "ACTI", "Activator" }, { "FURN", "Furniture" }, { "LIGH", "Light" }, { "WEAP", "Weapon" },
				{ "ARMO", "Armor" }, { "MISC", "Misc item" }, { "ALCH", "Potion" }, { "INGR", "Ingredient" }, { "BOOK", "Book" },
				{ "AMMO", "Ammo" }, { "SCRL", "Scroll" }, { "SLGM", "Soul gem" }, { "KEYM", "Key" }, { "FLOR", "Flora" },
				{ "GRAS", "Grass" }, { "TACT", "Talking activator" }, { "ANIO", "Animated object" }, { "HAZD", "Hazard" }, { "IDLM", "Idle marker" },
				{ "SCOL", "Static collection" }, { "PWAT", "Placed water" }
			};
			for (const auto& p : table)
				if (p.code == a_code)
					return p.name;
			return {};
		}

		std::string Upper(std::string_view a_text)
		{
			std::string out(a_text);
			for (auto& c : out)
				c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
			return out;
		}
	}

	std::string TextureKey(std::string_view a_path)
	{
		std::string p = Lower(a_path);
		for (auto& c : p)
			if (c == '/')
				c = '\\';
		while (!p.empty() && p.front() == '\\')
			p.erase(p.begin());
		if (p.rfind("data\\", 0) == 0)
			p.erase(0, 5);
		if (p.rfind("textures\\", 0) == 0)
			p.erase(0, 9);
		return p;
	}

	std::string DisplayPath(std::string_view a_path)
	{
		std::string p = Lower(a_path);
		for (auto& c : p)
			if (c == '/')
				c = '\\';
		while (!p.empty() && p.front() == '\\')
			p.erase(p.begin());
		// Bethesda's own build path ("C:\projects\Fallout4\Build\PC\Data\materials\...") is shown from the game folder name on.
		for (const std::string_view folder : { "materials\\", "textures\\" }) {
			if (p.rfind(folder, 0) == 0)
				break;
			const std::string key = "\\" + std::string(folder);
			if (const auto at = p.find(key); at != std::string::npos) {
				p.erase(0, at + 1);
				break;
			}
		}
		if (p.rfind("data\\", 0) == 0)
			p.erase(0, 5);
		if (p.size() > 4 && p.compare(p.size() - 4, 4, ".dds") == 0 && p.rfind("textures\\", 0) != 0)
			p.insert(0, "textures\\");
		return p;
	}

	std::string DimensionsText(int a_width, int a_height)
	{
		if (a_width == a_height)
			return std::format("{}\xC2\xB2", a_width);
		return std::format("{}\xC3\x97{}", a_width, a_height);
	}

	std::string SizeChip(const GpuInfo& a_gpu)
	{
		if (!a_gpu.present)
			return "no GPU info";
		std::string out = DimensionsText(a_gpu.width, a_gpu.height);
		out += ' ';
		out += a_gpu.format.empty() ? "?" : a_gpu.format;
		out += std::format(" \xC2\xB7 {} mip{}", a_gpu.mips, a_gpu.mips == 1 ? "" : "s");
		if (a_gpu.cube)
			out += " \xC2\xB7 cube";
		return out;
	}

	std::string OriginLabel(const FileOrigin& a_origin)
	{
		if (!a_origin.looked)
			return {};
		if (a_origin.source == "loose")
			return a_origin.mod.empty() ? "loose" : a_origin.mod;
		if (a_origin.source == "archive") {
			if (a_origin.archive.empty())
				return "archive";
			return a_origin.mod.empty() ? a_origin.archive : a_origin.archive + " \xC2\xB7 " + a_origin.mod;
		}
		return a_origin.source.empty() ? std::string() : a_origin.source;
	}

	std::string StepLabel(int a_index, int a_count)
	{
		if (a_index > 0)
			return std::format("{} of {} \xC2\xB7 behind", a_index + 1, a_count);
		return std::format("{} of {}", a_index + 1, a_count);
	}

	std::string PartLabel(int a_index, int a_count, bool a_exact)
	{
		return std::format("Part {} of {} \xC2\xB7 {}", a_index + 1, a_count, a_exact ? "exact" : "by bounds");
	}

	std::string ConsoleBrief(const PickInfo& a_info, std::string_view a_file)
	{
		std::string line = std::format("[What Am I Looking At] #{} ", a_info.counter);
		if (!a_info.valid) {
			line += a_info.message.empty() ? "nothing picked" : a_info.message;
		} else {
			const auto& kind = a_info.identity.kind;
			if (kind == "terrain") {
				line += "land";
			} else if (kind == "water") {
				line += "water";
			} else if (kind == "sky") {
				line += "sky";
			} else {
				std::string name = a_info.object.name;
				if (name.empty() || name == "(unnamed)")
					name = !a_info.object.baseEditorId.empty() ? a_info.object.baseEditorId : a_info.object.model;
				if (name.empty())
					name = "unnamed object";
				line += "\"" + name + "\"";
				if (a_info.clicked >= 0 && a_info.clicked < static_cast<int>(a_info.shapes.size()))
					line += std::format(" - part {} of {}", a_info.clicked + 1, a_info.shapes.size());
				else
					line += " - no single part found";
			}
		}
		if (!a_file.empty())
			line += std::format(" - full report in {}", a_file);
		return line;
	}

	namespace
	{
		/** True for a node name that holds a form id in parentheses: "(00000D6C)[0]/ (00000D64) [40%]". */
		bool HoldsFormId(const std::string& a_name)
		{
			for (std::size_t at = a_name.find('('); at != std::string::npos; at = a_name.find('(', at + 1)) {
				if (at + 9 >= a_name.size() || a_name[at + 9] != ')')
					continue;
				bool hex = true;
				for (std::size_t i = at + 1; i < at + 9; ++i)
					hex = hex && std::isxdigit(static_cast<unsigned char>(a_name[i]));
				if (hex)
					return true;
			}
			return false;
		}
	}

	std::string PartName(const ShapeInfo& a_shape)
	{
		const std::string fallback = std::format("Part {}", a_shape.index + 1);
		if (a_shape.name.empty())
			return fallback;
		if (!HoldsFormId(a_shape.name))
			return a_shape.name;
		const auto* diffuse = a_shape.Diffuse();
		if (!diffuse)
			return fallback;
		std::string folder, file;
		SplitPath(diffuse->path, folder, file);
		if (const auto dot = file.rfind('.'); dot != std::string::npos)
			file.resize(dot);
		const std::string lower = Lower(file);
		for (const char* prefix : { "female", "male" })
			if (lower.rfind(prefix, 0) == 0 && file.size() > std::strlen(prefix)) {
				file.erase(0, std::strlen(prefix));
				break;
			}
		if (const auto cut = file.find('_'); cut != std::string::npos)
			file.resize(cut);
		if (file.empty())
			return fallback;
		file[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(file[0])));
		return Lower(folder).find("actors\\character") != std::string::npos ? file + " (skin)" : file;
	}

	std::string Eyebrow(const PickInfo& a_info)
	{
		const auto& kind = a_info.identity.kind;
		if (kind == "terrain")
			return "GROUND";
		if (kind == "water")
			return "WATER";
		if (kind == "sky")
			return "SKY";
		std::string code = a_info.object.baseType.empty() ? a_info.identity.base.type : a_info.object.baseType;
		const auto  friendly = FriendlyType(code);
		if (friendly.empty())
			return kind == "npc" ? std::string("CHARACTER") : Upper(kind.empty() ? std::string("object") : kind);  // one plain word; the record signature lives in Details
		return Upper(friendly);
	}

	std::string PluginLine(const FormRecord& a_record)
	{
		const std::string& added = a_record.addedBy.empty() && !a_record.chain.empty() ? a_record.chain.front() : a_record.addedBy;
		const std::string& last = a_record.lastChangedBy.empty() && !a_record.chain.empty() ? a_record.chain.back() : a_record.lastChangedBy;
		if (added.empty() && last.empty())
			return {};
		if (last.empty() || last == added)
			return added;
		if (added.empty())
			return "last changed by " + last;
		return added + " \xC2\xB7 last changed by " + last;
	}

	void SplitPath(std::string_view a_path, std::string& a_folder, std::string& a_file)
	{
		const auto cut = a_path.find_last_of("\\/");
		if (cut == std::string_view::npos) {
			a_folder.clear();
			a_file = std::string(a_path);
			return;
		}
		a_folder = std::string(a_path.substr(0, cut));
		a_file = std::string(a_path.substr(cut + 1));
	}

	std::string CellLine(const Identity& a_id)
	{
		std::string out;
		const auto  add = [&](const std::string& a_part) {
            if (a_part.empty())
                return;
            if (!out.empty())
                out += " \xC2\xB7 ";
            out += a_part;
		};
		const std::string cell = !a_id.cellName.empty() ? a_id.cellName : a_id.cellEditorId;
		const std::string world = a_id.worldspaceEditorId.empty() ? a_id.worldspaceName : a_id.worldspaceEditorId;
		if (a_id.interior) {
			add(cell.empty() ? "Interior cell" : cell);
			if (!cell.empty())
				add("interior");
			return out;
		}
		// the grid the cell sits at: the cell's own coordinates, else the one the position falls in (4096 units a cell)
		int  gridX = a_id.cellX;
		int  gridY = a_id.cellY;
		bool haveGrid = a_id.hasCoords;
		if (!haveGrid && (!world.empty() || a_id.position.x != 0.0f || a_id.position.y != 0.0f)) {
			gridX = static_cast<int>(std::floor(a_id.position.x / 4096.0f));
			gridY = static_cast<int>(std::floor(a_id.position.y / 4096.0f));
			haveGrid = true;
		}
		if (cell.empty()) {
			// no cell name (not loaded, or a wilderness cell that has none): the worldspace and the grid say where
			if (!haveGrid)
				return world;
			return (world.empty() ? std::string("Cell") : world) + std::format(" [{}, {}]", gridX, gridY);
		}
		add(a_id.hasCoords ? cell + std::format(" [{}, {}]", a_id.cellX, a_id.cellY) : cell);
		add(world);
		add("exterior");
		return out;
	}

	std::string PositionText(Vec3 a_position)
	{
		return std::format("{:.0f}, {:.0f}, {:.0f}", a_position.x, a_position.y, a_position.z);
	}

	std::string OpacityText(const TerrainLayer& a_layer)
	{
		return a_layer.rawPercent < 0 ? "100%" : std::format("{:.0f}%", a_layer.percent);
	}

	bool ParseHexColor(std::string_view a_text, unsigned& a_rgb)
	{
		if (a_text.size() != 7 || a_text[0] != '#')
			return false;
		unsigned value = 0;
		for (std::size_t i = 1; i < 7; ++i) {
			const char c = a_text[i];
			unsigned   d = 0;
			if (c >= '0' && c <= '9')
				d = static_cast<unsigned>(c - '0');
			else if (c >= 'a' && c <= 'f')
				d = static_cast<unsigned>(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F')
				d = static_cast<unsigned>(c - 'A' + 10);
			else
				return false;
			value = value * 16 + d;
		}
		a_rgb = value;
		return true;
	}

	bool SwallowMouseButton(bool a_cardInteractive, bool a_overCard, bool a_isDown, bool& a_pressedOnCard)
	{
		if (a_isDown) {
			a_pressedOnCard = a_cardInteractive && a_overCard;
			return a_pressedOnCard;
		}
		const bool swallow = a_pressedOnCard;
		a_pressedOnCard = false;
		return swallow;
	}

	bool SwallowMouseWheel(bool a_cardInteractive, bool a_overCard)
	{
		return a_cardInteractive && a_overCard;
	}
}
