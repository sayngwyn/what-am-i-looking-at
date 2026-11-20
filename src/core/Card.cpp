// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
#include "Card.h"

#include "CardLayout.h"
#include "CardState.h"
#include "Overlay.h"

#include <d3d11.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>

namespace ti::card
{
	namespace
	{
		// ---- the house look ---------------------------------------------------------------------------------------------
		constexpr unsigned kBg = 0x0f0d0e;
		constexpr unsigned kSurface = 0x211a1a;
		constexpr unsigned kRaised = 0x2b2324;
		constexpr unsigned kBorder = 0x403839;
		constexpr unsigned kText = 0xdededb;
		constexpr unsigned kMuted = 0xaca3a2;
		constexpr unsigned kRed = 0xd36e6e;
		constexpr unsigned kAccent = 0x812727;
		constexpr unsigned kAccentHover = 0x963030;
		constexpr unsigned kDeep = 0x601d2c;
		constexpr unsigned kAlarm = 0xff7993;

		ImU32 U(unsigned a_rgb, float a_alpha = 1.0f)
		{
			return IM_COL32((a_rgb >> 16) & 0xFF, (a_rgb >> 8) & 0xFF, a_rgb & 0xFF, static_cast<int>(a_alpha * 255.0f + 0.5f));
		}
		ImVec4 V(unsigned a_rgb, float a_alpha = 1.0f)
		{
			return ImVec4(static_cast<float>((a_rgb >> 16) & 0xFF) / 255.0f, static_cast<float>((a_rgb >> 8) & 0xFF) / 255.0f, static_cast<float>(a_rgb & 0xFF) / 255.0f, a_alpha);
		}

		// ---- per-frame context ------------------------------------------------------------------------------------------
		struct Frame
		{
			const Params* p = nullptr;
			float         s = 1.0f;
			ImFont*       text = nullptr;
			ImFont*       bold = nullptr;
			ImFont*       display = nullptr;
			int           rowId = 0;
			int           scaleSteps = 0;  ///< set by the header's size buttons
			bool          headerHot = false;
			bool          dragStarted = false;
		};
		Frame* F = nullptr;

		float g_lastAutoH = 0.0f;  ///< the card's height last frame while it fits its content: keeps a tall card on the screen
		bool g_dragging = false;  ///< a press that began on the header is held: the card follows the pointer

		std::map<std::string, double> g_copiedAt;  ///< copy button / row id -> time it was clicked

		float Px(float a_value) { return a_value * F->s; }
		float Width() { return ImGui::GetContentRegionAvail().x; }
		ImDrawList* DL() { return ImGui::GetWindowDrawList(); }
		ImFont* Pick(ImFont* a_font) { return a_font ? a_font : ImGui::GetFont(); }

		constexpr float kBody = 14.5f;
		constexpr float kSmall = 12.5f;

		ImVec2 Measure(ImFont* a_font, float a_size, float a_wrap, const std::string& a_text)
		{
			if (a_text.empty())
				return ImVec2(0, 0);
			return Pick(a_font)->CalcTextSizeA(a_size, FLT_MAX, a_wrap, a_text.c_str());
		}

		/** Draws wrapped text at a_pos and returns its height (never less than one line). */
		float Put(ImVec2 a_pos, ImFont* a_font, float a_size, ImU32 a_col, float a_wrap, const std::string& a_text)
		{
			if (a_text.empty())
				return 0.0f;
			DL()->AddText(Pick(a_font), a_size, a_pos, a_col, a_text.c_str(), nullptr, a_wrap);
			return Measure(a_font, a_size, a_wrap, a_text).y;
		}

		void Gap(float a_px) { ImGui::Dummy(ImVec2(1.0f, Px(a_px))); }

		void Copy(const std::string& a_text, const std::string& a_id)
		{
			ImGui::SetClipboardText(a_text.c_str());
			g_copiedAt[a_id] = ImGui::GetTime();
		}

		bool JustCopied(const std::string& a_id)
		{
			const auto it = g_copiedAt.find(a_id);
			return it != g_copiedAt.end() && ImGui::GetTime() - it->second < 1.4;
		}

		void Tooltip(const std::string& a_text)
		{
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(Px(440));
			ImGui::PushFont(Pick(F->text), Px(kSmall));
			ImGui::TextUnformatted(a_text.c_str());
			ImGui::PopFont();
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}

		// ---- szSmall widgets --------------------------------------------------------------------------------------------
		/** A collapsible section header: bold text and a thin deep-red line under it. */
		bool Section(const char* a_label, bool a_defaultOpen)
		{
			ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, V(kRaised));
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, V(kBorder));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(Px(2), Px(5)));
			ImGui::PushFont(Pick(F->bold), Px(16.0f));
			const bool open = ImGui::CollapsingHeader(a_label, a_defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
			ImGui::PopFont();
			ImGui::PopStyleVar();
			ImGui::PopStyleColor(3);
			const ImVec2 lo = ImGui::GetItemRectMin();
			const ImVec2 hi = ImGui::GetItemRectMax();
			DL()->AddLine(ImVec2(lo.x, hi.y + 1.0f), ImVec2(hi.x, hi.y + 1.0f), U(kDeep), std::max(1.0f, Px(1.0f)));
			Gap(5);
			return open;
		}

		/** A sub-heading inside a section (muted, bold, szSmall). */
		void SubHeading(const std::string& a_text)
		{
			Gap(3);
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const float  h = Put(p, F->bold, Px(12.5f), U(kMuted), Width(), a_text);
			ImGui::Dummy(ImVec2(Width(), h + Px(3)));
		}

		void Row(const std::string& a_label, const std::string& a_value, bool a_copy = false, unsigned a_valueColour = kText)
		{
			const float W = Width();
			const float labelW = W * 0.31f;
			const float gap = Px(8);
			const float valueW = W - labelW - gap;
			const float size = Px(kBody);
			const float szSmall = Px(13.0f);
			const float h = std::max(Measure(F->text, szSmall, labelW, a_label).y, Measure(F->text, size, valueW, a_value).y) + Px(5);
			const std::string id = "row" + std::to_string(F->rowId++);
			ImVec2            p = ImGui::GetCursorScreenPos();
			bool              hovered = false;
			if (a_copy) {
				ImGui::InvisibleButton(id.c_str(), ImVec2(W, h));
				hovered = ImGui::IsItemHovered();
				if (ImGui::IsItemClicked())
					Copy(a_value, id);
			} else {
				ImGui::Dummy(ImVec2(W, h));
			}
			if (hovered)
				DL()->AddRectFilled(p, ImVec2(p.x + W, p.y + h), U(kRaised), Px(3));
			Put(ImVec2(p.x + Px(2), p.y + Px(2)), F->text, szSmall, U(kMuted), labelW, a_label);
			Put(ImVec2(p.x + labelW + gap, p.y + Px(2)), F->text, size, U(a_valueColour), valueW, a_value);
			if (a_copy && JustCopied(id)) {
				const ImVec2 t = Measure(F->bold, Px(12), FLT_MAX, "Copied");
				DL()->AddRectFilled(ImVec2(p.x + W - t.x - Px(12), p.y), ImVec2(p.x + W, p.y + t.y + Px(4)), U(kAccent), Px(3));
				Put(ImVec2(p.x + W - t.x - Px(6), p.y + Px(2)), F->bold, Px(12), U(kText), FLT_MAX, "Copied");
			} else if (hovered) {
				ImGui::SetTooltip("%s", "Click to copy");
			}
		}

		void Rows(const std::vector<Fact>& a_facts, bool a_copy = false)
		{
			for (const auto& f : a_facts)
				Row(f.label, f.value.empty() ? std::string("-") : f.value, a_copy);
		}

		/** The warning triangle with the exclamation mark, drawn as shapes (the fonts have no U+26A0). */
		void AlarmIcon(ImVec2 a_pos, float a_size)
		{
			auto*      dl = DL();
			const ImU32 col = U(kAlarm);
			const float t = std::max(1.2f, a_size * 0.1f);
			const ImVec2 a(a_pos.x + a_size * 0.5f, a_pos.y + a_size * 0.06f);
			const ImVec2 b(a_pos.x + a_size * 0.98f, a_pos.y + a_size * 0.94f);
			const ImVec2 c(a_pos.x + a_size * 0.02f, a_pos.y + a_size * 0.94f);
			dl->AddTriangle(a, b, c, col, t);
			dl->AddLine(ImVec2(a_pos.x + a_size * 0.5f, a_pos.y + a_size * 0.36f), ImVec2(a_pos.x + a_size * 0.5f, a_pos.y + a_size * 0.64f), col, t);
			dl->AddCircleFilled(ImVec2(a_pos.x + a_size * 0.5f, a_pos.y + a_size * 0.79f), t * 0.6f, col);
		}

		void Alarm(const std::string& a_text)
		{
			const float W = Width();
			const float icon = Px(15);
			const float gap = Px(8);
			const float size = Px(13.5f);
			const float textW = W - icon - gap - Px(14);
			const float h = std::max(icon, Measure(F->text, size, textW, a_text).y) + Px(10);
			const ImVec2 p = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(W, h));
			DL()->AddRectFilled(p, ImVec2(p.x + W, p.y + h), U(kBg), Px(3));
			DL()->AddRectFilled(p, ImVec2(p.x + W, p.y + h), U(kDeep, 0.55f), Px(3));
			AlarmIcon(ImVec2(p.x + Px(7), p.y + Px(5)), icon);
			Put(ImVec2(p.x + Px(7) + icon + gap, p.y + Px(5)), F->text, size, U(kAlarm), textW, a_text);
			Gap(3);
		}

		/** A square 40 px thumbnail: the texture's live view when held, else a quiet placeholder. */
		void Thumbnail(ImVec2 a_pos, float a_size, void* a_view, const char* a_placeholder)
		{
			auto*       dl = DL();
			const ImVec2 hi(a_pos.x + a_size, a_pos.y + a_size);
			bool drawn = false;
			if (a_view) {
				auto*                          view = static_cast<ID3D11ShaderResourceView*>(a_view);
				D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
				view->GetDesc(&desc);
				{
					static bool reported = false;
					if (!reported) {
						reported = true;
						spdlog::info("card: first thumbnail view {:p} dimension {} format {}", a_view, static_cast<int>(desc.ViewDimension), static_cast<int>(desc.Format));
					}
				}
				if (desc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D) {
					dl->AddRectFilled(a_pos, hi, U(kBg), Px(3));
					dl->AddCallback(overlay::NoBlendBegin, nullptr);
					dl->AddImage(ImTextureRef(reinterpret_cast<ImTextureID>(a_view)), a_pos, hi);
					dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
					drawn = true;
				}
			}
			if (!drawn) {
				dl->AddRectFilled(a_pos, hi, U(kBg), Px(3));
				if (a_placeholder && a_placeholder[0]) {
					const ImVec2 t = Measure(F->text, Px(11), FLT_MAX, a_placeholder);
					Put(ImVec2(a_pos.x + (a_size - t.x) * 0.5f, a_pos.y + (a_size - t.y) * 0.5f), F->text, Px(11), U(kMuted), FLT_MAX, a_placeholder);
				}
			}
			dl->AddRect(a_pos, hi, U(kBorder), Px(3));
		}

		/** One texture: thumbnail, slot label, file name over its folder, size chip, where it came from, a Copy button. */
		void TextureRow(const TextureSlot& a_slot, const std::string& a_sub)
		{
			const float W = Width();
			const float thumb = Px(40);
			const float gap = Px(10);
			const float btnW = Px(48);
			const float btnH = Px(22);
			const float textW = W - thumb - gap - btnW - Px(8);
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const float  x = p.x + thumb + gap;
			float        y = p.y;

			std::string folder, file;
			SplitPath(a_slot.path, folder, file);
			if (file.empty())
				file = a_slot.path;

			// text block first, so the row's height is known
			const float szSmall = Px(kSmall);
			if (!a_slot.label.empty())
				y += Put(ImVec2(x, y), F->text, szSmall, U(kMuted), textW, a_slot.label) + Px(1);
			y += Put(ImVec2(x, y), F->bold, Px(14.0f), U(kText), textW, file) + Px(1);
			if (!folder.empty())
				y += Put(ImVec2(x, y), F->text, Px(12.0f), U(kMuted), textW, folder) + Px(2);
			float chipH = 0;
			if (a_slot.gpu.present) {
				const std::string chip = SizeChip(a_slot.gpu);
				const ImVec2      t = Measure(F->text, szSmall, FLT_MAX, chip);
				const float       padX = Px(7), padY = Px(2);
				DL()->AddRectFilled(ImVec2(x, y), ImVec2(x + t.x + 2 * padX, y + t.y + 2 * padY), U(kRaised), Px(3));
				Put(ImVec2(x + padX, y + padY), F->text, szSmall, U(kText), FLT_MAX, chip);
				const std::string origin = OriginLabel(a_slot.origin);
				float             ox = x + t.x + 2 * padX + Px(8);
				if (!origin.empty() && ox < x + textW - Px(30))
					Put(ImVec2(ox, y + padY), F->text, szSmall, U(kMuted), x + textW - ox, origin);
				chipH = t.y + 2 * padY;
			} else {
				const std::string origin = OriginLabel(a_slot.origin);
				if (!origin.empty())
					chipH = Put(ImVec2(x, y), F->text, szSmall, U(kMuted), textW, origin);
			}
			y += chipH;
			const float rowH = std::max(thumb, y - p.y) + Px(8);
			const std::string id = "tex" + std::to_string(F->rowId++) + a_sub;
			ImGui::Dummy(ImVec2(W, rowH));

			// thumbnail
			void*       view = nullptr;
			const char* placeholder = "";  // a blank swatch when the texture could not be loaded
			if (a_slot.gpu.cube)
				placeholder = "cube";
			else if (a_slot.gpu.arraySize > 1)
				placeholder = "array";
			if (F->p->held) {
				view = hold::ViewFor(F->p->held, a_slot.loadedAs);
				if (!view)
					view = hold::ViewFor(F->p->held, a_slot.path);
			}
			Thumbnail(p, thumb, view, placeholder);

			// the Copy button
			const ImVec2 bp(p.x + W - btnW, p.y);
			ImGui::SetCursorScreenPos(bp);
			ImGui::InvisibleButton(id.c_str(), ImVec2(btnW, btnH));
			const bool hovered = ImGui::IsItemHovered();
			const bool active = ImGui::IsItemActive();
			if (ImGui::IsItemClicked())
				Copy(a_slot.path, id);
			DL()->AddRectFilled(bp, ImVec2(bp.x + btnW, bp.y + btnH), U(active ? kAccent : (hovered ? kAccentHover : kRaised)), Px(4));
			const std::string label = JustCopied(id) ? "Copied" : "Copy";
			const ImVec2      lt = Measure(F->bold, Px(12), FLT_MAX, label);
			Put(ImVec2(bp.x + (btnW - lt.x) * 0.5f, bp.y + (btnH - lt.y) * 0.5f), F->bold, Px(12), U(kText), FLT_MAX, label);
			if (hovered)
				Tooltip(a_slot.path);
			// back below the row
			ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rowH));
			ImGui::Dummy(ImVec2(W, 0));
			DL()->AddLine(ImVec2(p.x, p.y + rowH - Px(3)), ImVec2(p.x + W, p.y + rowH - Px(3)), U(kRaised), 1.0f);
		}

		TextureSlot SlotFromPath(const std::string& a_label, const std::string& a_path)
		{
			TextureSlot s;
			s.label = a_label;
			s.path = a_path;
			return s;
		}

		TextureSlot SlotFromNpc(const NpcTexture& a_texture)
		{
			TextureSlot s;
			s.label = a_texture.slotLabel;
			s.path = a_texture.path;
			s.gpu = a_texture.gpu;
			return s;
		}

		// ---- facts the card shows, per kind -----------------------------------------------------------------------------
		std::string FileNameOf(const std::string& a_path)
		{
			std::string folder, file;
			SplitPath(a_path, folder, file);
			return file;
		}

		std::string Hex8(std::uint32_t a_id) { return std::format("{:08X}", a_id); }

		std::string Join(const std::vector<std::string>& a_items, std::string_view a_separator)
		{
			std::string out;
			for (const auto& item : a_items) {
				if (!out.empty())
					out += a_separator;
				out += item;
			}
			return out;
		}

		std::string Chain(const FormRecord& a_record)
		{
			return a_record.chain.empty() ? a_record.addedBy : Join(a_record.chain, " > ");
		}

		std::string WithEditorId(std::uint32_t a_id, const std::string& a_editorId)
		{
			return a_id == 0 ? std::string("none") : Hex8(a_id) + (a_editorId.empty() ? std::string() : "  " + a_editorId);
		}

		const TerrainLayer* StrongestLayer(const TerrainSection& a_terrain)
		{
			const TerrainLayer* best = nullptr;
			for (const auto& l : a_terrain.layers)
				if (!best || l.percent > best->percent)
					best = &l;
			return best;
		}

		bool IsHex8(const std::string& a_text, std::size_t a_at)
		{
			if (a_at + 8 > a_text.size())
				return false;
			for (std::size_t i = a_at; i < a_at + 8; ++i)
				if (!std::isxdigit(static_cast<unsigned char>(a_text[i])))
					return false;
			return true;
		}

		/** A fact's value for a player: form ids and the developer's "(unnamed)" placeholders taken out (they stay in Details and the log). Empty when nothing readable is left. */
		std::string PlainValue(std::string a_value)
		{
			for (std::size_t at = 0; at + 11 <= a_value.size();) {
				if (a_value[at] == ',' && a_value[at + 1] == ' ' && IsHex8(a_value, at + 2) && a_value[at + 10] == ')') {
					a_value.erase(at, 10);
					continue;
				}
				++at;
			}
			for (std::size_t at = 0; at + 11 <= a_value.size();) {
				if (a_value[at] == ' ' && a_value[at + 1] == '(' && IsHex8(a_value, at + 2) && a_value[at + 10] == ')') {
					a_value.erase(at, 11);
					continue;
				}
				++at;
			}
			if (const auto via = a_value.find(" via door "); via != std::string::npos)
				a_value.erase(via);
			for (const char* placeholder : { "(unnamed cell) ", "(unnamed) " })
				if (a_value.rfind(placeholder, 0) == 0) {
					a_value.erase(0, std::strlen(placeholder));
					if (a_value.size() > 2 && a_value.front() == '(' && a_value.back() == ')')
						a_value = a_value.substr(1, a_value.size() - 2);
					break;
				}
			return a_value;
		}

		std::string Cap(std::string a_text)
		{
			if (!a_text.empty())
				a_text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(a_text[0])));
			return a_text;
		}

		/** "DefaultWater" -> "Default water": an editor id read as words. */
		std::string Spaced(const std::string& a_editorId)
		{
			std::string out;
			for (std::size_t i = 0; i < a_editorId.size(); ++i) {
				const char c = a_editorId[i];
				if (c == '_') {
					out += ' ';
					continue;
				}
				const bool upper = std::isupper(static_cast<unsigned char>(c)) != 0;
				if (upper && i > 0 && std::islower(static_cast<unsigned char>(a_editorId[i - 1])))
					out += ' ';
				out += (upper && !out.empty()) ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : c;
			}
			return out;
		}

		/** The picture a land texture is known by: its diffuse file name without folder or extension ("Dirt02"). */
		std::string DiffuseStem(const TerrainLayer& a_layer)
		{
			const Fact* pick = nullptr;
			for (const auto& t : a_layer.textures) {
				if (t.value.empty() || t.label == "texture set" || t.label == "material file")
					continue;
				std::string label = t.label;
				for (auto& c : label)
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				if (label.find("diffuse") != std::string::npos) {
					pick = &t;
					break;
				}
				if (!pick)
					pick = &t;
			}
			if (!pick)
				return {};
			std::string file = FileNameOf(pick->value);
			if (const auto dot = file.rfind('.'); dot != std::string::npos)
				file.resize(dot);
			return Cap(file);
		}

		std::string LayerName(const TerrainLayer& a_layer)
		{
			if (!a_layer.name.empty())
				return a_layer.name;
			if (const auto stem = DiffuseStem(a_layer); !stem.empty())
				return stem;
			return a_layer.editorId.empty() ? std::string("Ground layer") : Spaced(a_layer.editorId);
		}

		/** The weather in words: its class ("Pleasant"), else its editor id read as words. */
		std::string PlainWeather(const FormRecord& a_record, const std::string& a_class)
		{
			if (!a_class.empty())
				return Cap(a_class);
			return a_record.editorId.empty() ? std::string("Unknown") : Spaced(a_record.editorId);
		}

		std::string WaterTypeName(const WaterSection& a_water)
		{
			if (!a_water.typeName.empty())
				return a_water.typeName;
			if (!a_water.type.editorId.empty())
				return Spaced(a_water.type.editorId);
			// a water type with no name of its own is known by its first normal map: "DefaultWater.dds" -> "Default water"
			for (const auto& t : a_water.noiseTextures) {
				std::string file = FileNameOf(t.value);
				if (const auto dot = file.rfind('.'); dot != std::string::npos)
					file.resize(dot);
				if (!file.empty())
					return Spaced(file);
			}
			return a_water.source.find("default") != std::string::npos ? std::string("Default water") : std::string("Water");
		}

		/** A worn item's title: its name, else the file name of its model; never the form id. */
		std::string ItemTitle(const WornItem& a_item)
		{
			if (!a_item.name.empty() && a_item.name.rfind("(unnamed", 0) != 0)
				return a_item.name;
			if (!a_item.model.empty()) {
				std::string file = FileNameOf(a_item.model);
				if (const auto dot = file.rfind('.'); dot != std::string::npos)
					file.resize(dot);
				if (!file.empty())
					return Cap(file);
			}
			return "Worn item";
		}

		std::string WeatherName(const FormRecord& a_record)
		{
			if (a_record.formId == 0)
				return "none";
			return a_record.editorId.empty() ? Hex8(a_record.formId) : a_record.editorId;
		}

		std::string HeaderName(const PickInfo& a_info)
		{
			if (!a_info.valid)
				return "Nothing to show";
			const auto& kind = a_info.identity.kind;
			if (kind == "terrain") {
				const auto* layer = StrongestLayer(a_info.terrain);
				if (!layer)
						return "Ground";
					// a land texture is best known by its picture: "Dirt02 ground"
					if (const auto stem = DiffuseStem(*layer); !stem.empty())
						return stem + " ground";
					return LayerName(*layer);
				}
				if (kind == "water")
					return WaterTypeName(a_info.water);
				if (kind == "sky")
					return a_info.sky.weatherClass.empty() ? std::string("Sky") : Cap(a_info.sky.weatherClass) + " sky";
			std::string name = a_info.object.name;
			if (name.empty() || name == "(unnamed)") {
				if (!a_info.object.baseEditorId.empty())
					name = a_info.object.baseEditorId;
				else if (!a_info.object.model.empty())
					name = FileNameOf(a_info.object.model);
					else
						{
							// no name, editor id or model (a light, a marker): say what kind of thing it is, "Light"
							name = Eyebrow(a_info);
							if (name.empty() || name == "NOTHING")
								name = "Nothing here";
							else
								for (std::size_t i = 1; i < name.size(); ++i)
									name[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[i])));
						}
			}
			return name;
		}

		std::string HeaderPluginLine(const PickInfo& a_info)
		{
			const auto& kind = a_info.identity.kind;
			if (kind == "terrain") {
				const auto* layer = StrongestLayer(a_info.terrain);
				if (!layer)
					return {};
				FormRecord r;
				r.addedBy = layer->addedBy;
				r.lastChangedBy = layer->lastChangedBy;
				r.chain = layer->chain;
				return PluginLine(r);
			}
			if (kind == "water")
				return PluginLine(a_info.water.type);
			if (kind == "sky")
				return PluginLine(a_info.sky.currentWeather);
			return PluginLine(a_info.identity.base);
		}

		std::vector<Fact> WhatItIsFacts(const PickInfo& a_info)
		{
			const auto&       kind = a_info.identity.kind;
			std::vector<Fact> out;
			const auto        add = [&](std::string a_label, std::string a_value) { out.push_back({ std::move(a_label), std::move(a_value) }); };
			if (kind == "terrain") {
				const auto& t = a_info.terrain;
				std::string cell = !t.cellName.empty() ? t.cellName : t.cellEditorId;
				if (cell.empty())
					cell = t.worldspace.empty() ? std::string("Cell") : t.worldspace;  // no cell name: the worldspace and the grid say where
				cell +=std::format(" [{}, {}]", t.cellX, t.cellY);
				add("Cell", cell);
				add("Worldspace", t.worldspace.empty() ? std::string("?") : t.worldspace);
				add("Quadrant", t.quadrantName.empty() ? std::string("?") : t.quadrantName);
				add("Ground height", std::format("{:.1f}", t.heightAtHit));
				if (!t.footstepMaterial.empty())
					add("Sounds like", t.footstepMaterial);
				add("Layers", std::format("{}", t.layers.size()));
				return out;
			}
			if (kind == "water") {
				const auto& w = a_info.water;
					add("Water type", WaterTypeName(w));
				add("Surface height", std::format("{:.1f}", w.height));
					if (!w.source.empty())
						add("Comes from", PlainValue(w.source));
				if (!w.cellName.empty() && w.cellName != "(no cell)")
					add("Cell", w.cellName);
				return out;
			}
			if (kind == "sky") {
				const auto& s = a_info.sky;
				add("Where", s.mode.empty() ? std::string("?") : s.mode);
				add("Time", s.hourText.empty() ? std::string("?") : s.hourText);
					add("Weather now", PlainWeather(s.currentWeather, s.weatherClass));
			if (s.previousWeather.formId != 0 && s.transitionPercent >= 0.0f && s.transitionPercent < 100.0f &&
						PlainWeather(s.previousWeather, s.previousClass) != PlainWeather(s.currentWeather, s.weatherClass))
						add("Changing", PlainWeather(s.previousWeather, s.previousClass) + " \xE2\x86\x92 " + PlainWeather(s.currentWeather, s.weatherClass) + std::format(", {:.0f}% done", s.transitionPercent));
					return out;
			}
				for (const auto& f : a_info.facts) {
					std::string value = PlainValue(f.value);
					if (!value.empty() && value.front() != '(')
						add(f.label, std::move(value));
				}
				const auto where = CellLine(a_info.identity);
			if (!where.empty())
				add("Cell", where);
			add("Position", PositionText(a_info.identity.position));
			return out;
		}

		const HitInfo* CurrentHit(const PickInfo& a_info)
		{
			if (a_info.hitIndex >= 0 && a_info.hitIndex < static_cast<int>(a_info.hits.size()))
				return &a_info.hits[static_cast<std::size_t>(a_info.hitIndex)];
			return nullptr;
		}

		std::vector<Fact> DetailFacts(const PickInfo& a_info)
		{
			const auto&       kind = a_info.identity.kind;
			const auto&       id = a_info.identity;
			std::vector<Fact> out;
			const auto        add = [&](std::string a_label, std::string a_value) {
                if (!a_value.empty())
                    out.push_back({ std::move(a_label), std::move(a_value) });
			};
				if (kind == "terrain") {
					const auto& t = a_info.terrain;
					add("Cell ID", Hex8(t.cellFormId));
					add("Note", t.note);
				add("Hit point", PositionText(t.point));
				add("Nearest vertex", std::format("{}, {} in the {} quadrant", t.vertexX, t.vertexY, t.quadrantName));
				add("Blend", t.blendMethod);
				add("Height check", t.heightCheck);
				for (const auto& l : t.layers)
					add("Record, " + l.role, WithEditorId(l.formId, l.editorId) + (l.chain.empty() ? std::string() : "  [" + Join(l.chain, " > ") + "]"));
				for (const auto& l : t.layers)
					for (const auto& x : l.textures)
						if (x.label == "texture set")
							add("Texture set, " + l.role, x.value);
			} else if (kind == "water") {
				const auto& w = a_info.water;
					add("Note", w.note);
					add("Water cell ID", w.cellFormId != 0 ? Hex8(w.cellFormId) : std::string());
					add("Water type", WithEditorId(w.type.formId, w.type.editorId));
				add("Water type plugins", Chain(w.type));
				add("Hit point", PositionText(w.point));
				add("Surface", w.cellVersusPlane);
				if (w.planeRefFormId != 0)
					add("Placed plane", Hex8(w.planeRefFormId) + " (base " + Hex8(w.planeBaseFormId) + ")");
				for (const auto& v : w.values)
					add(v.label, v.value);
			} else if (kind == "sky") {
				const auto& s = a_info.sky;
					add("Note", s.note);
					add("Weather now", WithEditorId(s.currentWeather.formId, s.currentWeather.editorId));
				add("Weather plugins", Chain(s.currentWeather));
				if (s.previousWeather.formId != 0) {
					add("Weather before", WithEditorId(s.previousWeather.formId, s.previousWeather.editorId));
					add("Before plugins", Chain(s.previousWeather));
				}
				add("Climate", WithEditorId(s.climate.formId, s.climate.editorId));
				add("Image space", s.imageSpace + (s.imageSpaceSource.empty() ? std::string() : " (" + s.imageSpaceSource + ")"));
				add("Lighting template", s.lightingTemplate);
			} else {
					add("Record type", a_info.object.baseType.empty() ? a_info.identity.base.type : a_info.object.baseType);
					add("Persistent reference", id.persistent ? std::string("yes: it lives in the worldspace's persistent cell") : std::string());
					add("Reference ID", a_info.object.refFormId != 0 ? Hex8(a_info.object.refFormId) : std::string());
				add("Base ID", a_info.object.baseFormId != 0 ? Hex8(a_info.object.baseFormId) + (a_info.object.baseType.empty() ? std::string() : " (" + a_info.object.baseType + ")") : std::string());
				add("Reference editor ID", a_info.object.refEditorId);
				add("Base editor ID", a_info.object.baseEditorId);
				add("Reference plugins", Chain(id.ref));
				add("Base plugins", Chain(id.base));
				add("Mesh", a_info.object.model);
				add("Mesh comes from", OriginLabel(id.modelOrigin));
				add("Scale", std::format("{:.2f}", id.scale));
			}
			if (const auto* hit = CurrentHit(a_info))
				add("Hit", std::format("{:.1f} units from the camera \xC2\xB7 {}", hit->distance, hit->exact ? "exact" : "by bounds"));
			if (!a_info.method.empty() && a_info.method != "none")
				add("Pick method", a_info.method);
			if (a_info.clicked >= 0 && a_info.clicked < static_cast<int>(a_info.shapes.size())) {
				const auto& s = a_info.shapes[static_cast<std::size_t>(a_info.clicked)];
				if (s.exactHit)
					add("Triangle", std::format("{} of {}", s.hitTriangle, s.triangles));
			}
			add("Ray", a_info.rayCheck);
			return out;
		}

		std::vector<Fact> ShaderFacts(const ShapeInfo& a_shape)
		{
			std::vector<Fact> out;
			if (a_shape.shaderKind.empty() || a_shape.shaderKind == "none") {
				out.push_back({ "Shader", "none" });
			} else {
				std::string kind = a_shape.shaderKind;
				if (kind.rfind("BS", 0) == 0)
					kind.erase(0, 2);
				if (const auto cut = kind.find("ShaderProperty"); cut != std::string::npos)
					kind.erase(cut);
				std::string text = kind;
				if (!a_shape.shaderType.empty())
					text += " \xC2\xB7 " + a_shape.shaderType;
				if (a_shape.pbr != "no" && !a_shape.pbr.empty())
					text += " \xC2\xB7 PBR (" + a_shape.pbr + ")";
				out.push_back({ "Shader", text });
			}
			if (!a_shape.materialPath.empty()) {
				out.push_back({ "Material", DisplayPath(a_shape.materialPath) });
				if (const std::string origin = OriginLabel(a_shape.materialOrigin); !origin.empty())
					out.push_back({ "Material file", origin });
				if (!a_shape.rootMaterial.empty())
					out.push_back({ "Root material", DisplayPath(a_shape.rootMaterial) });
			}
			if (a_shape.alphaMode != "none" && !a_shape.alphaMode.empty()) {
				std::string text = a_shape.alphaMode;
				if (a_shape.alphaThreshold >= 0)
					text += std::format(" \xC2\xB7 threshold {}", a_shape.alphaThreshold);
				out.push_back({ "Alpha", text });
			}
			if (!a_shape.flags.empty())
				out.push_back({ "Flags", Join(a_shape.flags, ", ") });
			if (!a_shape.vertexFlags.empty())
				out.push_back({ "Vertex data", Join(a_shape.vertexFlags, ", ") });
			if (a_shape.hasMaterial)
				out.push_back({ "Surface", std::format("glossiness {:.1f} \xC2\xB7 specular {:.2f}", a_shape.glossiness, a_shape.specularStrength) });
			if (a_shape.hasEmissive && a_shape.emissiveMultiple > 0.0f)
				out.push_back({ "Glow", std::format("emissive x{:.2f}", a_shape.emissiveMultiple) });
			if (a_shape.uvScale[0] != 1.0f || a_shape.uvScale[1] != 1.0f || a_shape.uvOffset[0] != 0.0f || a_shape.uvOffset[1] != 0.0f)
				out.push_back({ "UV", std::format("scale {:.2f}, {:.2f} \xC2\xB7 offset {:.2f}, {:.2f}", a_shape.uvScale[0], a_shape.uvScale[1], a_shape.uvOffset[0], a_shape.uvOffset[1]) });
			if (a_shape.vertices > 0)
				out.push_back({ "Mesh", std::format("{} vertices \xC2\xB7 {} triangles", a_shape.vertices, a_shape.triangles) });
			return out;
		}

		// ---- header -----------------------------------------------------------------------------------------------------
		bool Chevron(const char* a_id, bool a_left, float a_size)
		{
			ImGui::InvisibleButton(a_id, ImVec2(a_size, a_size));
			const bool  hovered = ImGui::IsItemHovered();
			const bool  clicked = ImGui::IsItemClicked();
			const ImVec2 lo = ImGui::GetItemRectMin();
			const float  cx = lo.x + a_size * 0.5f, cy = lo.y + a_size * 0.5f;
			const float  d = a_size * 0.2f;
			const float  dir = a_left ? -1.0f : 1.0f;
			const ImU32  col = U(hovered ? kText : kRed);
			const float  t = std::max(1.5f, a_size * 0.08f);
			DL()->AddLine(ImVec2(cx - dir * d * 0.6f, cy - d * 1.3f), ImVec2(cx + dir * d * 0.6f, cy), col, t);
			DL()->AddLine(ImVec2(cx + dir * d * 0.6f, cy), ImVec2(cx - dir * d * 0.6f, cy + d * 1.3f), col, t);
			return clicked;
		}

		/** A small "A" button that makes the card text and layout smaller (a_larger = false) or larger. */
		bool SizeButton(const char* a_id, bool a_larger, float a_size)
		{
			ImGui::InvisibleButton(a_id, ImVec2(a_size, a_size));
			const bool   hovered = ImGui::IsItemHovered();
			const bool   clicked = ImGui::IsItemClicked();
			const ImVec2 lo = ImGui::GetItemRectMin();
			const ImVec2 hi = ImGui::GetItemRectMax();
			if (hovered)
				DL()->AddRectFilled(lo, hi, U(kRaised), Px(4));
			const float  fontSize = Px(a_larger ? 17.0f : 11.5f);
			const ImVec2 ts = Measure(F->bold, fontSize, FLT_MAX, "A");
			DL()->AddText(Pick(F->bold), fontSize, ImVec2(lo.x + (a_size - ts.x) * 0.5f, lo.y + (a_size - ts.y) * 0.5f), U(hovered ? kText : kRed), "A");
			if (hovered)
				Tooltip(a_larger ? "Larger card (Ctrl + mouse wheel up)" : "Smaller card (Ctrl + mouse wheel down)");
			return clicked;
		}

		void Header(const PickInfo& a_info)
		{
			const float W = Width();
			const ImVec2 p0 = ImGui::GetCursorScreenPos();
			// line 1: the eyebrow on the left, the step control on the right
			const std::string eyebrow = a_info.valid ? Eyebrow(a_info) : "NOTHING";
			const float       rowH = Px(22);
			Put(ImVec2(p0.x, p0.y + Px(4)), F->bold, Px(12.0f), U(kRed), W, eyebrow);
			// the size control, far right: smaller / percent / larger
			const std::string percent = std::format("{}%", F->p->uiPercent);
			const float       percentW = Measure(F->text, Px(12.5f), FLT_MAX, percent).x;
			const float       sizeTotal = rowH * 2 + percentW + Px(10);
			{
				ImGui::SetCursorScreenPos(ImVec2(p0.x + W - sizeTotal, p0.y));
				if (SizeButton("##smaller", false, rowH))
					F->scaleSteps = -1;
				ImGui::SameLine(0, 0);
				const ImVec2 pp = ImGui::GetCursorScreenPos();
				Put(ImVec2(pp.x + Px(5), pp.y + Px(3.5f)), F->text, Px(12.5f), U(kMuted), FLT_MAX, percent);
				ImGui::SetCursorScreenPos(ImVec2(pp.x + percentW + Px(10), pp.y));
				if (SizeButton("##larger", true, rowH))
					F->scaleSteps = +1;
			}
			const int hits = static_cast<int>(a_info.hits.size());
			if (hits > 1 && a_info.hitIndex >= 0) {
				const std::string label = StepLabel(a_info.hitIndex, hits);
				const float       labelW = Measure(F->text, Px(13), FLT_MAX, label).x;
				const float       total = rowH * 2 + labelW + Px(8);
				ImGui::SetCursorScreenPos(ImVec2(p0.x + W - sizeTotal - Px(12) - total, p0.y));
				if (Chevron("##prev", true, rowH) && F->p->step)
					F->p->step(-1);
				ImGui::SameLine(0, 0);
				const ImVec2 lp = ImGui::GetCursorScreenPos();
				Put(ImVec2(lp.x + Px(4), lp.y + Px(3)), F->text, Px(13), U(kText), FLT_MAX, label);
				ImGui::SetCursorScreenPos(ImVec2(lp.x + labelW + Px(8), lp.y));
				if (Chevron("##next", false, rowH) && F->p->step)
					F->p->step(+1);
			}
			ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + rowH));
			ImGui::Dummy(ImVec2(W, 0));

			// line 2: the display name with its deep-red offset shadow
			const std::string name = HeaderName(a_info);
			const float       nameSize = std::max(18.0f, Px(27.0f));
			const ImVec2      np = ImGui::GetCursorScreenPos();
			const ImVec2      ns = Measure(F->display, nameSize, W, name);
			DL()->AddText(Pick(F->display), nameSize, ImVec2(np.x + Px(1.5f), np.y + Px(2.5f)), U(kDeep), name.c_str(), nullptr, W);
			DL()->AddText(Pick(F->display), nameSize, np, U(kText), name.c_str(), nullptr, W);
			ImGui::Dummy(ImVec2(W, ns.y + Px(5)));

			// line 3: which plugins
			std::string plugins = HeaderPluginLine(a_info);
			if (!a_info.valid)
				plugins = a_info.message;
			if (!plugins.empty()) {
				const ImVec2 pp = ImGui::GetCursorScreenPos();
				const float  h = Put(pp, F->text, Px(13.0f), U(kMuted), W, plugins);
				ImGui::Dummy(ImVec2(W, h + Px(4)));
			}
			const ImVec2 sp = ImGui::GetCursorScreenPos();
			DL()->AddLine(ImVec2(sp.x, sp.y + Px(2)), ImVec2(sp.x + W, sp.y + Px(2)), U(kBorder), 1.0f);
			Gap(8);

			// the whole header strip drags the card (not the buttons in it, and not the window edges, which resize it)
			const ImVec2 wp = ImGui::GetWindowPos();
			const ImVec2 ws = ImGui::GetWindowSize();
			const ImVec2 hdrMax(wp.x + ws.x, sp.y + Px(4));
			const bool   over = ImGui::IsMouseHoveringRect(wp, hdrMax, false) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered();
			if (over && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::GetActiveID() == 0) {
				g_dragging = true;
				F->dragStarted = true;
			}
			if (over || g_dragging) {
				F->headerHot = true;
				ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
			}
		}

		// ---- sections ---------------------------------------------------------------------------------------------------
		void PartBanner(const PickInfo& a_info, const ShapeInfo& a_shape)
		{
			const float W = Width();
			const float h = Px(28);
			const ImVec2 p = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(W, h));
			DL()->AddRectFilled(p, ImVec2(p.x + W, p.y + h), U(kDeep), Px(4));
			const std::string right = PartLabel(a_shape.index, static_cast<int>(a_info.shapes.size()), a_info.method == "exact" || a_shape.exactHit);
			const float       rw = Measure(F->text, Px(12.5f), FLT_MAX, right).x;
			Put(ImVec2(p.x + W - rw - Px(10), p.y + Px(7)), F->text, Px(12.5f), U(kText, 0.85f), FLT_MAX, right);
			const float nameW = W - rw - Px(34);
			const std::string name = PartName(a_shape);
			// one line: clip a long part name to the room
			DL()->PushClipRect(ImVec2(p.x, p.y), ImVec2(p.x + nameW + Px(10), p.y + h), true);
			Put(ImVec2(p.x + Px(10), p.y + Px(6)), F->bold, Px(14.0f), U(kText), FLT_MAX, name);
			DL()->PopClipRect();
			Gap(6);
		}

		void ShapeBody(const ShapeInfo& a_shape, const std::string& a_sub)
		{
			for (const auto& t : a_shape.textures)
				TextureRow(t, a_sub);
			const auto facts = ShaderFacts(a_shape);
			if (!facts.empty()) {
				SubHeading("Shader and material");
				Rows(facts);
			}
		}

		/** A collapsed line for one part or item: its name, a muted hint, expandable. Returns true when open. */
		bool Expander(const std::string& a_label, const std::string& a_hint, const std::string& a_id)
		{
			ImGui::PushStyleColor(ImGuiCol_Header, V(kRaised));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, V(kBorder));
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, V(kBorder));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(Px(6), Px(4)));
			ImGui::PushFont(Pick(F->bold), Px(13.5f));
			const std::string text = a_label;
			const bool        open = ImGui::CollapsingHeader((text + "##" + a_id).c_str());
			ImGui::PopFont();
			ImGui::PopStyleVar();
			ImGui::PopStyleColor(3);
			if (!a_hint.empty()) {
				const ImVec2 lo = ImGui::GetItemRectMin();
				const ImVec2 hi = ImGui::GetItemRectMax();
				const float  labelW = Measure(F->bold, Px(13.5f), FLT_MAX, text).x;
				// ImGui puts a frame header's text after the arrow: the font size (of the font the header was drawn with) plus three paddings
				const float  x = lo.x + Px(13.5f) + 3.0f * Px(6) + labelW + Px(16);
				const float  room = hi.x - x - Px(6);
				if (room > Px(30)) {
					DL()->PushClipRect(ImVec2(x, lo.y), ImVec2(hi.x - Px(4), hi.y), true);
					Put(ImVec2(x, lo.y + Px(4)), F->text, Px(12.5f), U(kMuted), FLT_MAX, a_hint);
					DL()->PopClipRect();
				}
			}
			return open;
		}

		std::string DiffuseHint(const ShapeInfo& a_shape)
		{
			const auto* d = a_shape.Diffuse();
			return d ? FileNameOf(d->path) : std::string();
		}

		void WornItemBlock(const WornItem& a_item, const std::string& a_id)
		{
			std::string slots;
			for (const int slot : a_item.slots) {
				if (!slots.empty())
					slots += ", ";
				slots += SlotNameOf(a_item, slot);
			}
			const std::string name = ItemTitle(a_item);
				const std::string hint = slots + (a_item.itemPlugin.empty() ? std::string() : (slots.empty() ? "" : " \xC2\xB7 ") + a_item.itemPlugin);
			if (Expander(name, hint, a_id)) {
				ImGui::Indent(Px(6));
				if (a_item.textures.empty())
					Row("Textures", "none read");
				for (const auto& t : a_item.textures)
					TextureRow(SlotFromNpc(t), a_id);
				for (const auto& m : a_item.materials)
					Row("Material", DisplayPath(m), true);
				for (const auto& m : a_item.mods)
					Row("Mod", m);
				if (!a_item.model.empty())
					Row("Model", a_item.model, true);
				ImGui::Unindent(Px(6));
			}
		}

		void Swatch(const std::string& a_label, const std::string& a_hex)
		{
			if (a_hex.empty())
				return;
			unsigned rgb = 0;
			if (!ParseHexColor(a_hex, rgb))
				return;
			const float W = Width();
			const float h = Px(22);
			const ImVec2 p = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(W, h));
			DL()->AddRectFilled(p, ImVec2(p.x + Px(22), p.y + h - Px(2)), U(rgb), Px(3));
			DL()->AddRect(p, ImVec2(p.x + Px(22), p.y + h - Px(2)), U(kBorder), Px(3));
			Put(ImVec2(p.x + Px(32), p.y + Px(1)), F->text, Px(13.0f), U(kMuted), W, a_label);
			Put(ImVec2(p.x + W * 0.31f + Px(8), p.y + Px(1)), F->text, Px(kBody), U(kText), W, a_hex);
		}

		void LayerBlock(const TerrainLayer& a_layer, int a_index)
		{
			const float W = Width();
			const std::string id = "layer" + std::to_string(a_index);
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const float  nameH = Put(p, F->bold, Px(14.5f), U(kText), W * 0.62f, LayerName(a_layer));
			const std::string right = a_layer.role;
			const float       rw = Measure(F->text, Px(12.5f), FLT_MAX, right).x;
			Put(ImVec2(p.x + W - rw, p.y + Px(2)), F->text, Px(12.5f), U(kMuted), FLT_MAX, right);
			ImGui::Dummy(ImVec2(W, nameH + Px(3)));
			// the opacity bar
			const ImVec2 bp = ImGui::GetCursorScreenPos();
			const float  bh = Px(8);
			const float  frac = std::clamp(a_layer.rawPercent < 0 ? 1.0f : a_layer.percent / 100.0f, 0.0f, 1.0f);
			const std::string pct = OpacityText(a_layer);
			const float       labelW = Measure(F->text, Px(12.5f), FLT_MAX, "opacity 100%").x + Px(8);
			const float       barW = W - labelW;
			DL()->AddRectFilled(bp, ImVec2(bp.x + barW, bp.y + bh), U(kRaised), Px(3));
			if (frac > 0.0f)
				DL()->AddRectFilled(bp, ImVec2(bp.x + std::max(Px(4), barW * frac), bp.y + bh), U(kAccent), Px(3));
			Put(ImVec2(bp.x + barW + Px(8), bp.y - Px(4)), F->text, Px(12.5f), U(kMuted), FLT_MAX, "opacity " + pct);
			ImGui::Dummy(ImVec2(W, bh + Px(6)));
			for (const auto& t : a_layer.textures) {
				// the texture set's form id belongs in Details; a material file has no picture of its own
				if (t.label == "texture set")
					continue;
				if (t.label == "material file") {
					Row("Material file", t.value, true);
					continue;
				}
				TextureRow(SlotFromPath(t.label, t.value), id);
			}
				if (!a_layer.grassNames.empty()) {
					std::vector<std::string> names;
					for (const auto& n : a_layer.grassNames)
						if (std::find(names.begin(), names.end(), n) == names.end())
							names.push_back(n);
					Row("Grass", Join(names, ", "));
				} else if (!a_layer.grasses.empty()) {
					Row("Grass", std::format("{} kind{}", a_layer.grasses.size(), a_layer.grasses.size() == 1 ? "" : "s"));
				}
			Gap(4);
		}

		/** An alarm line whose part is named by a form id (skin and armor parts) gets the part's readable name instead. */
		std::string PlainAlarm(const PickInfo& a_info, const std::string& a_text)
		{
			for (const auto& s : a_info.shapes) {
				if (s.name.empty())
					continue;
				const std::string prefix = s.name + ": ";
				if (a_text.rfind(prefix, 0) == 0)
					return PartName(s) + ": " + a_text.substr(prefix.size());
			}
			return a_text;
		}

		void ObjectBody(const PickInfo& a_info)
		{
			const bool hasClicked = a_info.clicked >= 0 && a_info.clicked < static_cast<int>(a_info.shapes.size());
			if (hasClicked) {
				const auto& shape = a_info.shapes[static_cast<std::size_t>(a_info.clicked)];
				// alarms first: the picked part's own, then any that belong to other parts
				std::vector<std::string> alarms;
				for (const auto& d : shape.diagnostics)
					alarms.push_back(d.text);
				const std::string own = shape.name + ": ";
				for (const auto& d : a_info.diagnostics) {
					if (!shape.name.empty() && d.text.rfind(own, 0) == 0)
						continue;
					alarms.push_back(d.text);
				}
					for (const auto& a : alarms)
						Alarm(PlainAlarm(a_info, a));
					PartBanner(a_info, shape);
				ShapeBody(shape, "p");
			} else {
					for (const auto& d : a_info.diagnostics)
						Alarm(PlainAlarm(a_info, d.text));
					const ImVec2 p = ImGui::GetCursorScreenPos();
					const float  h = Put(p, F->text, Px(kBody), U(kMuted), Width(), a_info.shapes.empty() ? "No parts were read for this object." : "The cursor is not on a part of this object; its parts are listed below.");
				ImGui::Dummy(ImVec2(Width(), h + Px(6)));
			}

			// the other parts
			const int total = static_cast<int>(a_info.shapes.size());
			int       listed = 0, hidden = 0;
			bool      headed = false;
			for (const auto& s : a_info.shapes) {
				if (s.index == a_info.clicked)
					continue;
				if (s.textures.empty() && !s.visible)
					continue;
				if (listed >= 40) {
					++hidden;
					continue;
				}
				if (!headed) {
					SubHeading(std::format("Other parts ({})", total - (hasClicked ? 1 : 0)));
					headed = true;
				}
				if (Expander(PartName(s), DiffuseHint(s), "s" + std::to_string(s.index))) {
					ImGui::Indent(Px(6));
					ShapeBody(s, "s" + std::to_string(s.index));
					ImGui::Unindent(Px(6));
				}
				++listed;
			}
			if (hidden > 0) {
				const ImVec2 p = ImGui::GetCursorScreenPos();
				const float  h = Put(p, F->text, Px(12.5f), U(kMuted), Width(), std::format("and {} more parts (all of them are in last-pick.json)", hidden));
				ImGui::Dummy(ImVec2(Width(), h + Px(4)));
			}
		}

		void NpcBody(const PickInfo& a_info)
		{
			ObjectBody(a_info);
			const auto& npc = a_info.npc;
			if (!npc.present)
				return;
			if (npc.skinItem.present) {
SubHeading("Skin: " + (npc.skinItem.name.empty() || npc.skinItem.name.rfind("(unnamed", 0) == 0 ? std::string("body") : npc.skinItem.name) + (npc.skinItem.source.empty() ? std::string() : " \xC2\xB7 " + npc.skinItem.source));
				int i = 0;
				for (const auto& part : npc.skinItem.parts)
					WornItemBlock(part, "skin" + std::to_string(i++));
			}
			if (!npc.worn.empty()) {
				SubHeading(std::format("Worn ({})", npc.worn.size()));
				int i = 0;
				for (const auto& w : npc.worn)
					WornItemBlock(w, "worn" + std::to_string(i++));
			}
			if (!npc.headParts.empty()) {
				SubHeading(std::format("Head parts ({})", npc.headParts.size()));
				int i = 0;
				for (const auto& h : npc.headParts) {
					const std::string id = "head" + std::to_string(i++);
const std::string name = !h.name.empty() ? h.name : (!h.editorId.empty() ? Spaced(h.editorId) : (h.type.empty() ? std::string("Head part") : Cap(h.type)));
					if (Expander(name, h.type, id)) {
						ImGui::Indent(Px(6));
						for (const auto& path : h.textureSetPaths)
							TextureRow(SlotFromPath("", path), id);
						if (!h.model.empty())
							Row("Model", h.model, true);
						ImGui::Unindent(Px(6));
					}
				}
			}
			if (npc.faceGen.looked) {
				SubHeading("FaceGen");
				const bool found = npc.faceGen.verdict == kFaceGenFound;
				Row("Face data", npc.faceGen.verdict, false, found ? kText : kAlarm);
				if (!npc.faceGen.meshPath.empty())
					Row("Mesh", npc.faceGen.meshPath, true);
				if (!npc.faceGen.tintPath.empty())
					Row("Tint", npc.faceGen.tintPath, true);
			}
		}

		void TerrainBody(const PickInfo& a_info)
		{
			const auto& t = a_info.terrain;
				int i = 0;
			for (const auto& layer : t.layers)
				LayerBlock(layer, i++);
		}

		void WaterBody(const PickInfo& a_info)
		{
			const auto& w = a_info.water;
				SubHeading("Colors");
			Swatch("Shallow", w.shallowColor);
			Swatch("Deep", w.deepColor);
			Swatch("Reflection", w.reflectionColor);
			if (!w.liveTextures.empty()) {
				SubHeading("Normal maps on the surface");
				for (const auto& t : w.liveTextures)
					TextureRow(SlotFromPath(t.label, t.value), "live");
			}
			if (!w.noiseTextures.empty()) {
				SubHeading("The water type's textures");
				for (const auto& t : w.noiseTextures)
					TextureRow(SlotFromPath(t.label, t.value), "noise");
			}
		}

		void SkyBody(const PickInfo& a_info)
		{
			const auto& s = a_info.sky;
				const auto  clouds = [&](const std::string& a_weather, const std::string& a_id) {
					// one row per picture: the layers that use it are named together ("layers 8-11")
					struct Group
					{
						std::string      texture;
						std::vector<int> layers;
					};
					std::vector<Group> groups;
					for (const auto& c : s.clouds) {
						if (c.weather != a_weather)
							continue;
						auto it = std::find_if(groups.begin(), groups.end(), [&](const Group& g) { return g.texture == c.texture; });
						if (it == groups.end()) {
							groups.push_back({ c.texture, {} });
							it = groups.end() - 1;
						}
						it->layers.push_back(c.index);
					}
					for (std::size_t g = 0; g < groups.size(); ++g) {
						auto&       layers = groups[g].layers;
						std::sort(layers.begin(), layers.end());
						bool contiguous = true;
						for (std::size_t i = 1; i < layers.size(); ++i)
							contiguous = contiguous && layers[i] == layers[i - 1] + 1;
						std::string which;
						if (layers.size() == 1)
							which = std::format("layer {}", layers.front());
						else if (contiguous)
							which = std::format("layers {}\xE2\x80\x93{}", layers.front(), layers.back());
						else {
							which = "layers ";
							for (std::size_t i = 0; i < layers.size(); ++i)
								which += (i ? ", " : "") + std::to_string(layers[i]);
						}
						TextureRow(SlotFromPath(which + " \xC2\xB7 " + a_weather + " weather", groups[g].texture), a_id + std::to_string(g));
					}
					return groups.size();
				};
				std::size_t currentCount = 0, previousCount = 0;
				for (const auto& c : s.clouds)
					(c.weather == "current" ? currentCount : previousCount) += 1;
				if (currentCount > 0) {
					SubHeading("Cloud layers");
					clouds("current", "cloudc");
				}
				if (previousCount > 0 && Expander("Clouds of the weather it is leaving", std::format("{} layers", previousCount), "cloudprev")) {
					ImGui::Indent(Px(6));
					clouds("previous", "cloudp");
					ImGui::Unindent(Px(6));
				}
			if (!s.objects.empty()) {
				SubHeading("Sun, moons and weather effects");
				for (const auto& o : s.objects) {
						if (o.value.empty() || o.value.front() == '(')
							continue;  // a moon the engine does not let us read, or "this weather has no precipitation": nothing a player can use
						TextureRow(SlotFromPath(o.label, o.value), "sky");
					}
			}
		}

		void Content(const PickInfo& a_info)
		{
			Header(a_info);
			if (!a_info.valid)
				return;
			if (Section("What it is##s1", true)) {
				Rows(WhatItIsFacts(a_info));
				Gap(4);
			}
			if (Section("How it looks##s2", true)) {
				const auto& kind = a_info.identity.kind;
				if (kind == "terrain")
					TerrainBody(a_info);
				else if (kind == "water")
					WaterBody(a_info);
				else if (kind == "sky")
					SkyBody(a_info);
				else if (kind == "npc")
					NpcBody(a_info);
				else
					ObjectBody(a_info);
				Gap(4);
			}
			if (Section("Details##s3", false)) {
				Rows(DetailFacts(a_info), true);
				Gap(4);
			}
		}
	}

	Result Draw(const Params& a_params)
	{
		Result out;
		if (!a_params.info)
			return out;
		Frame frame;
		frame.p = &a_params;
		frame.s = a_params.scale;
		frame.text = a_params.fonts.text;
		frame.bold = a_params.fonts.bold;
		frame.display = a_params.fonts.display;
		F = &frame;

		const float s = frame.s;
		const float margin = 16.0f * s;
		const float edge = 8.0f;  // the least the card keeps from the screen edge, in pixels
		const float minW = 320.0f * s;
		const float minH = 120.0f * s;
		const auto& pl = a_params.placement;
		float       width = pl.hasSize ? pl.w : 460.0f * s;
		float       height = pl.hasSize ? pl.h : 0.0f;
		const bool  fixedHeight = pl.hasSize && pl.h > 0.0f;
		const auto  fitted = ClampToScreen({ pl.x, pl.y, width, fixedHeight ? height : g_lastAutoH }, a_params.displayW, a_params.displayH, minW, minH, edge);
		width = fitted.w;
		height = fixedHeight ? fitted.h : 0.0f;
		if (pl.hasPos) {
			ImGui::SetNextWindowPos(ImVec2(fitted.x, fitted.y), ImGuiCond_Always);
		} else {
			const bool   right = a_params.corner.find("right") != std::string::npos;
			const bool   bottom = a_params.corner.find("bottom") != std::string::npos;
			const ImVec2 pos(right ? a_params.displayW - margin : margin, bottom ? a_params.displayH - margin : margin);
			ImGui::SetNextWindowPos(pos, ImGuiCond_Always, ImVec2(right ? 1.0f : 0.0f, bottom ? 1.0f : 0.0f));
		}
		ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
		ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(minW, a_params.displayW - 2 * edge), minH),
			ImVec2(a_params.displayW - 2 * edge, fixedHeight ? a_params.displayH - 2 * edge : 0.8f * a_params.displayH));

		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowRounding = 4.0f * s;
		style.ChildRounding = 4.0f * s;
		style.FrameRounding = 4.0f * s;
		style.PopupRounding = 4.0f * s;
		style.ScrollbarRounding = 4.0f * s;
		style.GrabRounding = 4.0f * s;
		style.WindowBorderSize = std::max(1.0f, s);
		style.WindowBorderHoverPadding = std::max(5.0f, 6.0f * s);
		style.PopupBorderSize = 1.0f;
		style.ScrollbarSize = 6.0f * s;
		style.ItemSpacing = ImVec2(6.0f * s, 3.0f * s);
		style.IndentSpacing = 10.0f * s;
		style.MouseCursorScale = std::max(1.3f, 1.5f * s);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, V(kSurface));  // fully opaque: the HUD (location name) must not show through
		ImGui::PushStyleColor(ImGuiCol_Border, V(kBorder));
		ImGui::PushStyleColor(ImGuiCol_Text, V(kText));
		ImGui::PushStyleColor(ImGuiCol_PopupBg, V(kSurface));
		ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, V(kBorder));
		ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, V(kRaised));
		ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, V(kAccent));
		ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, V(kAccentHover));
		ImGui::PushStyleColor(ImGuiCol_ResizeGrip, V(kRaised));
		ImGui::PushStyleColor(ImGuiCol_ResizeGripHovered, V(kAccent));
		ImGui::PushStyleColor(ImGuiCol_ResizeGripActive, V(kAccentHover));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f * s, 14.0f * s));

		constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
		                                   ImGuiWindowFlags_NoFocusOnAppearing;
		if (ImGui::Begin("##lookingat-card", nullptr, flags)) {
			if (a_params.newCard)
				ImGui::SetScrollY(0.0f);
			ImGui::PushFont(Pick(frame.text), 14.5f * s);
			Content(*a_params.info);
			ImGui::PopFont();
			ImGuiWindow* win = ImGui::GetCurrentWindow();
			const ImVec2 wp = ImGui::GetWindowPos();
			const ImVec2 ws = ImGui::GetWindowSize();
			out.x = wp.x;
			out.y = wp.y;
			out.w = ws.x;
			out.h = ws.y;
			if (!fixedHeight)
				g_lastAutoH = ws.y;
			out.hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_ChildWindows);
			// an edge or corner is held: ImGui is resizing the window (the new size and position are already in the window)
			if (const ImGuiID active = ImGui::GetActiveID(); active != 0) {
				for (int corner = 0; corner < 4; ++corner)
					if (active == ImGui::GetWindowResizeCornerID(win, corner))
						out.resizeW = out.resizeH = true;
				if (active == ImGui::GetWindowResizeBorderID(win, ImGuiDir_Left) || active == ImGui::GetWindowResizeBorderID(win, ImGuiDir_Right))
					out.resizeW = true;
				if (active == ImGui::GetWindowResizeBorderID(win, ImGuiDir_Up) || active == ImGui::GetWindowResizeBorderID(win, ImGuiDir_Down))
					out.resizeH = true;
			}
			// the header drag: the card follows the pointer from the next frame on
			if (g_dragging) {
				if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && !out.resizeW && !out.resizeH) {
					const ImVec2 delta = ImGui::GetIO().MouseDelta;
					if (!frame.dragStarted && (delta.x != 0.0f || delta.y != 0.0f)) {
						out.dragMoved = true;
						out.nextX = wp.x + delta.x;
						out.nextY = wp.y + delta.y;
					}
				} else {
					g_dragging = false;
				}
			}
			out.scaleSteps = frame.scaleSteps;
			out.headerHot = frame.headerHot;
		}
		ImGui::End();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor(11);
		F = nullptr;
		return out;
	}
}
