// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// What Am I Looking At for Fallout 4 (F4SE plugin): the same card as the Skyrim plugin. One DLL for Fallout 4 1.10.163 (OG),
// 1.10.984 (NG) and 1.11.240 (AE). A console tool: click an object, bare ground, water or the sky with the console open, or type
// `lookingat` or `lookingat <refid>`: the report goes to the console, last-pick.json and the card, which is drawn only while the console is open.
// Objects, actors, land, water and sky are read (`lookingat ground|water|sky` ask for one kind).
#include "PCH.h"

#include "Camera.h"
#include "CardLayout.h"
#include "CardState.h"
#include "Config.h"
#include "Inspector.h"
#include "Output.h"
#include "PickScan.h"
#include "Overlay.h"
#include "OverlayHooks.h"
#include "OverlayPlatform.h"
#include "Sample.h"
#include "TextureHold.h"
#include "TextureInfo.h"
#include "WorldFill.h"

#include <charconv>
#include <ctime>
#include <ShlObj.h>
#include <Windows.h>

namespace
{
	constexpr auto kPluginName = "What Am I Looking At 1.1.1"sv;

	ti::Config    g_config;
	std::uint64_t g_counter = 0;
	std::uint32_t g_lastRefId = 0;

	/** What `lookingat next` steps through: the ray of the last pick and every hit along it. */
	struct LastPick
	{
		ti::RayResult            ray;
		std::vector<ti::HitInfo> hits;
		int                      index = -1;
	};
	LastPick g_last;

	const char* RuntimeName(const REL::Version& a_version)
	{
		if (a_version == REL::Version{ 1, 10, 163, 0 })
			return "OG";
		if (a_version[0] == 1 && a_version[1] == 10)
			return "NG";
		if (a_version[0] == 1 && a_version[1] == 11)
			return "AE";
		return "unknown";
	}

	std::filesystem::path DefaultOutputFolder()
	{
		// Documents\My Games\<save folder name>\F4SE\WhatAmILookingAt, next to the F4SE log (the folder name comes from F4SE; Fallout4 on OG).
		std::filesystem::path result = L"WhatAmILookingAt";
		PWSTR                 documents = nullptr;
		if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents)) && documents) {
			std::string folder(F4SE::GetSaveFolderName());
			if (folder.empty())
				folder = "Fallout4";
			result = std::filesystem::path(documents) / L"My Games" / std::filesystem::path(folder) / L"F4SE" / L"WhatAmILookingAt";
			CoTaskMemFree(documents);
		}
		return result;
	}

	std::string LocalTimestamp(std::int64_t& a_unixMs)
	{
		const auto now = std::chrono::system_clock::now();
		a_unixMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
		const std::time_t t = std::chrono::system_clock::to_time_t(now);
		std::tm           local{};
		localtime_s(&local, &t);
		char buffer[40]{};
		std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &local);
		return buffer;
	}

	void Print(const std::string& a_line) { ti::overlay::platform::ConsolePrint(a_line); }

	/** Stamps the pick and sends it to every enabled output. */
	void Publish(ti::PickInfo& a_info, std::string_view a_trigger, const ti::RayResult& a_ray)
	{
		a_info.counter = ++g_counter;
		a_info.timestamp = LocalTimestamp(a_info.unixMs);
		a_info.trigger = std::string(a_trigger);
		a_info.plugin = std::string(kPluginName);
		a_info.cursor = a_ray.cursor;
		a_info.rayCheck = a_ray.check;
		for (const auto& n : a_ray.notes)
			a_info.notes.push_back(n);
		if (!a_info.hasRay && a_ray.ok) {
			a_info.ray = a_ray.ray;
			a_info.hasRay = true;
		}

		g_last.ray = a_ray;
		g_last.hits = a_info.hits;
		g_last.index = a_info.hitIndex;

		if (g_config.printToConsole) {
			// While the card is on it shows the report, so the console gets one short line (and the full report is in the JSON file).
			const bool brief = ti::overlay::CardActive() && !g_config.fullConsoleReport && g_config.writeJson;
			if (brief)
				Print(ti::ConsoleBrief(a_info, "last-pick.json"));
			else
				for (const auto& line : ti::FormatConsole(a_info, g_config.maxListedParts, g_config.showRayLine))
					Print(line);
		}
		if (g_config.writeJson) {
			const auto error = ti::WriteFileAtomic(g_config.outputFolder / L"last-pick.json", ti::ToJson(a_info));
			if (!error.empty()) {
				spdlog::error("could not write last-pick.json: {}", error);
				Print("[What Am I Looking At] could not write last-pick.json (see WhatAmILookingAt.log)");
			}
		}
		// the card: drawn while the console is open (a pick made by command with the console closed shows when it opens)
		ti::overlay::SetCard(a_info, ti::hold::TakeFor(a_info));
		spdlog::info("pick #{} via {}: {} {} '{}' ref {:08X} base {:08X}; part '{}' by {}; {} part(s), {} warning(s), {} fact(s), {} hit(s){}", a_info.counter, a_trigger,
			a_info.identity.kind, a_info.object.baseType, a_info.object.name, a_info.object.refFormId, a_info.object.baseFormId,
			a_info.clicked >= 0 ? a_info.shapes[static_cast<std::size_t>(a_info.clicked)].name : std::string("none"), a_info.method, a_info.shapes.size(),
			a_info.diagnostics.size(), a_info.facts.size(), a_info.hits.size(), a_info.valid ? std::string() : "; " + a_info.message);
	}

	/** Runs a_work on the game thread two task rounds from now, so the console has finished selecting. */
	template <class Fn>
	void RunLater(Fn a_work)
	{
		const auto* tasks = F4SE::GetTaskInterface();
		if (!tasks)
			return;
		tasks->AddTask([a_work]() mutable {
			F4SE::GetTaskInterface()->AddTask([a_work]() mutable {
				try {
					a_work();
				} catch (const std::exception& e) {
					spdlog::error("pick failed: {}", e.what());
				} catch (...) {
					spdlog::error("pick failed: unknown exception");
				}
			});
		});
	}

	/**
	 * True when a click at this window fraction lands on the console's text (the selection line, the history, the command line): its real
	 * rectangle, read from the console movie. The dark backdrop is not counted, so the world beside and below the text still picks.
	 * Movie units are stage pixels; the stage is the visible frame, which maps onto the whole client area.
	 */
	bool ClickOnConsole(float a_fracX, float a_fracY)
	{
		auto* ui = RE::UI::GetSingleton();
		auto  menu = ui ? ui->GetMenu(RE::Console::MENU_NAME) : nullptr;
		if (!menu || !menu->uiMovie)
			return false;
		auto&      movie = *menu->uiMovie;
		const auto frame = movie.GetVisibleFrameRect();
		const float frameW = frame.x2 - frame.x1;
		const float frameH = frame.y2 - frame.y1;
		if (frameW <= 0.0f || frameH <= 0.0f)
			return false;
		const auto number = [](const Scaleform::GFx::Value& a_object, const char* a_name, float& a_out) {
			Scaleform::GFx::Value v;
			if (!a_object.GetMember(a_name, &v) || !v.IsNumber())
				return false;
			a_out = static_cast<float>(v.GetNumber());
			return true;
		};
		Scaleform::GFx::Value holder, panel;
		if (!movie.GetVariable(&holder, "_root.AnimHolder_mc") || !holder.IsObject() || !holder.GetMember("Menu_mc", &panel) || !panel.IsObject())
			return false;
		float offX = 0.0f, offY = 0.0f, v = 0.0f;
		if (number(holder, "x", v))
			offX += v;
		if (number(holder, "y", v))
			offY += v;
		if (number(panel, "x", v))
			offX += v;
		if (number(panel, "y", v))
			offY += v;
		const float px = frame.x1 + a_fracX * frameW;
		const float py = frame.y1 + a_fracY * frameH;
		constexpr float pad = 6.0f;
		for (const char* name : { "CurrentSelection", "CommandHistory", "CommandEntry", "CommandPrompt_tf" }) {
			Scaleform::GFx::Value child;
			float                 x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
			if (!panel.GetMember(name, &child) || !number(child, "x", x) || !number(child, "y", y) || !number(child, "width", w) || !number(child, "height", h))
				continue;
			if (px >= offX + x - pad && px <= offX + x + w + pad && py >= offY + y - pad && py <= offY + y + h + pad)
				return true;
		}
		return false;
	}

	/**
	 * Console path. The console's own selection is only a hint (it can be stale, it toggles off on a second click, and it never selects land,
	 * water or sky): the ray from the cursor decides. The ray's nearest hit within reach is reported (an object part, land, water or the sky); the selection is only logged. Every click that reports nothing logs why.
	 */
	void PickSelected(ti::RayResult a_ray, std::uint32_t a_selectionBefore)
	{
		auto ref = RE::Console::GetCurrentPickREFR().get();
		spdlog::info("click at ({:.3f}, {:.3f}); console-selected ref {:08X}; ray {}", a_ray.cursor.fracX, a_ray.cursor.fracY, ref ? ref->GetFormID() : 0u, a_ray.ok ? a_ray.check : "none: " + a_ray.check);
		if (!a_ray.ok || !a_ray.cursor.used) {
			spdlog::info("click ignored: no ray from the cursor ({})", a_ray.check);
			if (ref) {
				ti::PickInfo info;
				info.valid = true;
				info.message = "could not build a ray from the cursor: " + a_ray.check;
				Publish(info, "console_click", a_ray);
			}
			return;
		}
		if (ClickOnConsole(a_ray.cursor.fracX, a_ray.cursor.fracY)) {
			spdlog::info("click ignored: on the console window ({:.3f}, {:.3f})", a_ray.cursor.fracX, a_ray.cursor.fracY);
			return;
		}
		// Our own ray from the cursor is the truth (within hotkeyMaxDistance): the console's selection may be stale or far away, so it is only logged.
		auto info = ti::PickAlongRay(a_ray.ray, g_config.hotkeyMaxDistance, ti::PickMode::kAuto);
		if (!info.valid) {
			spdlog::info("click ignored: nothing hit ({})", info.message);
			return;
		}
		if (info.identity.kind != "terrain" && info.identity.kind != "water" && info.identity.kind != "sky" && info.method == "approximate") {
			// An object found only by its bounds (a tree's big sphere, say) must not hide the land, water or sky the cursor is on: those win.
			const auto land = ti::PickAlongRay(a_ray.ray, g_config.hotkeyMaxDistance, ti::PickMode::kGround);
			const auto water = ti::PickAlongRay(a_ray.ray, g_config.hotkeyMaxDistance, ti::PickMode::kWater);
			const auto distance = [](const ti::PickInfo& a_pick) {
				return a_pick.valid && a_pick.hitIndex >= 0 && a_pick.hitIndex < static_cast<int>(a_pick.hits.size()) ? a_pick.hits[static_cast<std::size_t>(a_pick.hitIndex)].distance : std::numeric_limits<float>::max();
			};
			const float dl = distance(land);
			const float dw = distance(water);
			spdlog::info("click: the nearest object ({}) is only a bounds hit; land {}, water {}", info.object.name.empty() ? std::string("unnamed") : info.object.name, dl < std::numeric_limits<float>::max() ? "yes" : "no", dw < std::numeric_limits<float>::max() ? "yes" : "no");
			if (dl < std::numeric_limits<float>::max() || dw < std::numeric_limits<float>::max())
				info = dl <= dw ? land : water;
			else
				info = ti::PickAlongRay(a_ray.ray, g_config.hotkeyMaxDistance, ti::PickMode::kSky);
		}
		// The rule for the console's selection: if this click made the console select a NEW reference, it wins when our ray reaches it and nothing
		// nearer was hit; a stale, empty or water-plane selection never does. The log says which rule chose.
		if (ref) {
			const bool changed = ref->GetFormID() != a_selectionBefore;
			auto       selected = ti::InspectRef(ref.get(), a_ray.ray, false);
			const auto here = ref->GetPosition();
			const float fallback = std::sqrt((here.x - a_ray.ray.origin.x) * (here.x - a_ray.ray.origin.x) + (here.y - a_ray.ray.origin.y) * (here.y - a_ray.ray.origin.y) + (here.z - a_ray.ray.origin.z) * (here.z - a_ray.ray.origin.z));
			const auto verdict = ti::JudgeSelection(selected, a_ray.ray, info, changed, g_config.hotkeyMaxDistance, true, fallback);
			spdlog::info("click: console selection {:08X} (before the click {:08X}), {}", ref->GetFormID(), a_selectionBefore, verdict.reason);
			if (verdict.use) {
				selected.fromRay = selected.method == "exact";
				info = std::move(selected);
			}
		} else {
			spdlog::info("click: nothing selected in the console; the ray decides");
		}
		g_lastRefId = info.object.refFormId;
		Publish(info, "console_click", a_ray);
	}

	// ---- the `lookingat` console command --------------------------------------------------------------------------------
	// Lets a test (or a player without a mouse) pick from the console line. It takes over ONE vanilla console command that does
	// nothing in the retail game. First choice: ForceRSXCrash (short name fRSX), a PlayStation-renderer crash test that is a stub
	// (`mov al,1; ret`) in the retail table; the fallbacks are ToggleSPUCulling and ToggleActorMover, also retail stubs. If none is
	// found the plugin carries on without `lookingat`.
	// Usage: lookingat [refid|ground|water|sky|next|shot|json|scale_<75-200>|card_show|card_hide|card_reset|"place:<x>:<y>:<w>:<h>"]. No argument: the
	// console-selected reference, else whatever the crosshair points at.

	constexpr std::array kReplacedCommands{ "ForceRSXCrash"sv, "ToggleSPUCulling"sv, "ToggleActorMover"sv };
	constexpr auto       kCommandHelp = "lookingat [refid|ground|water|sky|next|shot|json|scale_<75-200>|card_show|card_hide|card_reset|\"place:<x>:<y>:<w>:<h>\"]: What Am I Looking At. Inspects the reference id given or selected in the console, else what the crosshair points at (object part, land, water or sky); ground, water or sky ask for that kind only. near_<kind> lists nearby references; ground_probe checks the land height reading. next steps to the next hit along the same ray. shot saves the next frame, card included, as a PNG. scale_150 sets the card size to 150 percent; card_reset puts the card back at its starting place and size; place (in quotes) puts it at x,y (fractions of the screen) with width w and height h (design pixels, 0 = fit the content).";

	/** a_text cut at ':' ' ' '=' and '_' into pieces. */
	std::vector<std::string> SplitWords(const std::string& a_text)
	{
		std::vector<std::string> out;
		std::string              word;
		for (const char c : a_text) {
			if (c == ':' || c == ' ' || c == '=' || c == '_') {
				if (!word.empty())
					out.push_back(std::move(word));
				word.clear();
			} else {
				word += c;
			}
		}
		if (!word.empty())
			out.push_back(std::move(word));
		return out;
	}

	bool ParseFloat(const std::string& a_text, float& a_value)
	{
		const auto result = std::from_chars(a_text.data(), a_text.data() + a_text.size(), a_value);
		return result.ec == std::errc() && result.ptr == a_text.data() + a_text.size() && std::isfinite(a_value);
	}

	/** The card's own commands (size, show/hide, place). False when a_mode is not one of them. */
	bool CardCommand(const std::string& a_mode)
	{
		const auto words = SplitWords(a_mode);
		if (words.empty())
			return false;
		if (words[0] == "scale") {
			float value = 0.0f;
			if (words.size() >= 2 && ParseFloat(words[1], value))
				value = ti::overlay::SetUiScale(value > 5.0f ? value / 100.0f : value);  // "scale_150" means 150 percent
			else
				value = ti::overlay::UiScale();
			Print(std::format("[What Am I Looking At] card size {:.0f}% (the range is {:.0f}% to {:.0f}%)", value * 100.0f, ti::kMinUiScale * 100.0f, ti::kMaxUiScale * 100.0f));
			return true;
		}
		if (words[0] == "card" && words.size() >= 2) {
			if (words[1] == "reset") {
				ti::overlay::ResetLayout();
				Print("[What Am I Looking At] card back at its starting place, size and shown");
			} else if (words[1] == "show" || words[1] == "hide") {
				ti::overlay::SetCardHidden(words[1] == "hide");
				Print(std::format("[What Am I Looking At] card {}", words[1] == "hide" ? "hidden" : "shown"));
			} else {
				return false;
			}
			return true;
		}
		if (words[0] == "place" && words.size() >= 3) {
			float x = 0, y = 0, w = 0, h = 0;
			if (!ParseFloat(words[1], x) || !ParseFloat(words[2], y) || (words.size() > 3 && !ParseFloat(words[3], w)) || (words.size() > 4 && !ParseFloat(words[4], h))) {
				Print("[What Am I Looking At] place:<x>:<y>:<w>:<h> needs numbers (x, y as fractions of the screen, w and h in design pixels)");
				return true;
			}
			ti::overlay::PlaceCard(x, y, w, h);
			Print(std::format("[What Am I Looking At] card placed at {:.3f}, {:.3f}{}", x, y, w > 0 ? std::format(", size {:.0f} x {:.0f}", w, h) : std::string()));
			return true;
		}
		return false;
	}

	RE::SCRIPT_PARAMETER g_commandParams[1];

	/** Copies up to a_bytes from engine memory; a bad pointer returns the bytes read so far (zero) instead of crashing. */
	std::size_t SafeRead(const void* a_from, void* a_to, std::size_t a_bytes)
	{
		__try {
			std::memcpy(a_to, a_from, a_bytes);
			return a_bytes;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return 0;
		}
	}

	/**
	 * The text typed after the command. The engine hands the compiled line over as raw bytes: a 16-bit parameter count, then for a
	 * string parameter a 16-bit length and the characters. Read at the offset the engine names, else at the start; the raw bytes are
	 * logged so a different layout shows up in the log.
	 */
	std::string ReadModeText(const char* a_compiled, std::uint32_t a_offset)
	{
		if (!a_compiled)
			return {};
		const auto try_at = [&](std::size_t a_at, std::string& a_out) {
			unsigned char buffer[96]{};
			if (SafeRead(a_compiled + a_at, buffer, sizeof(buffer)) == 0)
				return false;
			const unsigned count = buffer[0] | (buffer[1] << 8);
			if (count == 0)
				return false;
			if (count > 4)
				return false;
			const unsigned length = buffer[2] | (buffer[3] << 8);
			if (length == 0 || length > 80)
				return false;
			for (unsigned i = 0; i < length; ++i)
				if (buffer[4 + i] < 0x20 || buffer[4 + i] > 0x7E)
					return false;
			a_out.assign(reinterpret_cast<const char*>(buffer) + 4, length);
			return true;
		};
		std::string text;
		unsigned char raw[24]{};
		SafeRead(a_compiled + a_offset, raw, sizeof(raw));
		std::string hex;
		for (const auto b : raw)
			hex += std::format("{:02X} ", b);
		spdlog::info("lookingat: compiled bytes at offset {}: {}", a_offset, hex);
		if (try_at(a_offset, text) || try_at(0, text))
			return text;
		return {};
	}

	/** a_target: a reference named on the command line (`lookingat 000133FB`) or the one the engine ran the command on; null for neither. */
	void PickForCommand(RE::TESObjectREFR* a_target, std::string_view a_targetHow)
	{
		auto               ray = ti::CentreRay();
		RE::TESObjectREFR* ref = a_target;
		std::string_view   how = a_targetHow;
		RE::NiPointer<RE::TESObjectREFR> selected;
		if (!ref) {
			selected = RE::Console::GetCurrentPickREFR().get();
			if (selected) {
				ref = selected.get();
				how = "console-selected ref";
			}
		}
		if (ref && ray.ok) {
			// the object named is the one reported, even when the middle of the screen is not on it
			auto info = ti::InspectRef(ref, ray.ray, true);
			g_lastRefId = info.object.refFormId;
			info.notes.push_back(std::format("target: {}", how));
			spdlog::info("lookingat target: {}", how);
			Publish(info, "console_command", ray);
			return;
		}
		spdlog::info("lookingat target: crosshair (no reference {})", ref ? "ray" : "selected");
		if (!ray.ok) {
			ti::PickInfo info;
			info.valid = true;
			info.message = "could not build the screen-center ray: " + ray.check;
			Publish(info, "console_command", ray);
			return;
		}
		auto info = ti::PickAlongRay(ray.ray, g_config.hotkeyMaxDistance, ti::PickMode::kAuto);
		if (info.valid)
			g_lastRefId = info.object.refFormId;
		Publish(info, "console_command", ray);
	}

	/** `lookingat ground|water|sky`: the crosshair ray, asked for one kind of thing. */
	void PickForMode(ti::PickMode a_mode, std::string_view a_name)
	{
		auto         ray = ti::CentreRay();
		ti::PickInfo info;
		if (a_mode != ti::PickMode::kSky && !ray.ok) {
			info.valid = true;
			info.message = "could not build the screen-center ray: " + ray.check;
		} else {
			info = ti::PickAlongRay(ray.ok ? ray.ray : ti::Ray{}, g_config.hotkeyMaxDistance, a_mode);
		}
		spdlog::info("lookingat {}", a_name);
		Publish(info, "console_command", ray);
	}

	// ---- developer aids: finding things to point at ---------------------------------------------------------------------
	// `lookingat near_<kind>` lists the nearest loaded references of a kind (npc, creature, door, container, light, item, static, any) with
	// their ids, so a test can then `lookingat <id>` one of them. `lookingat ground_probe` logs how the stored land heights compare with the
	// player's own height. Both write to the log and the console and change nothing.

	std::string NearKindOf(RE::TESObjectREFR* a_ref)
	{
		auto* base = a_ref->GetObjectReference();
		if (!base)
			return {};
		using T = RE::ENUM_FORM_ID;
		switch (base->GetFormType()) {
		case T::kNPC_: {
			// a person's race carries the "NPC" actor type; animals, robots and monsters do not
			auto* race = static_cast<RE::TESNPC*>(base)->GetFormRace();
			return race && race->HasKeywordString("ActorTypeNPC") ? "npc" : "creature";
		}
		case T::kDOOR: return "door";
		case T::kCONT: return "container";
		case T::kLIGH: return "light";
		case T::kSTAT: return "static";
		case T::kWEAP:
		case T::kARMO:
		case T::kMISC:
		case T::kALCH:
		case T::kAMMO:
		case T::kBOOK:
		case T::kNOTE:
		case T::kKEYM:
			return "item";
		default: return {};
		}
	}

	void NearCommand(std::string_view a_kind)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* tes = RE::TES::GetSingleton();
		if (!player || !tes) {
			Print("[What Am I Looking At] the world is not loaded");
			return;
		}
		struct Found
		{
			float              distance;
			RE::TESObjectREFR* ref;
			std::string        kind;
		};
		std::vector<Found> found;
		const auto         here = player->GetPosition();
		tes->ForEachReferenceInRange(player, 4000.0f, [&](RE::TESObjectREFR* a_ref) {
			if (!a_ref || a_ref == player || a_ref->IsDisabled() || a_ref->IsDeleted() || !a_ref->Get3D())
				return RE::BSContainer::ForEachResult::kContinue;
			const std::string kind = NearKindOf(a_ref);
			if (kind.empty() || (a_kind != "any" && kind != a_kind))
				return RE::BSContainer::ForEachResult::kContinue;
			found.push_back({ a_ref->GetPosition().GetDistance(here), a_ref, kind });
			return RE::BSContainer::ForEachResult::kContinue;
		});
		std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.distance < b.distance; });
		if (found.size() > 14)
			found.resize(14);
		spdlog::info("lookingat near_{}: {} found", a_kind, found.size());
		for (const auto& f : found) {
			auto*             base = f.ref->GetObjectReference();
			const std::string name = base ? std::string(RE::TESFullName::GetFullName(*base)) : std::string();
			const char*       editor = base ? base->GetFormEditorID() : nullptr;
			const std::string line = std::format("{:08X} {} '{}' base {:08X} {} at {:.0f}", f.ref->GetFormID(), f.kind, name, base ? base->GetFormID() : 0u, editor ? editor : "", f.distance);
			spdlog::info("  {}", line);
			Print("[What Am I Looking At] " + line);
		}
		if (found.empty())
			Print(std::format("[What Am I Looking At] no {} within 4000 units", a_kind));
	}

	/** `lookingat next` and the card's step control: the next (+1) or previous (-1) hit along the ray of the last pick. */
	void StepHit(int a_delta)
	{
		if (g_last.hits.empty()) {
			spdlog::info("lookingat next: nothing to step through");
			Print("[What Am I Looking At] nothing to step through: the last pick has no hits list. Pick with `lookingat` or a click first.");
			return;
		}
		const int count = static_cast<int>(g_last.hits.size());
		const int next = (((g_last.index + a_delta) % count) + count) % count;
		auto      ray = g_last.ray;
		auto      info = ti::PickStep(ray.ray, g_last.hits, next);
		if (count > 1 && a_delta > 0 && next == 0)
			info.notes.push_back("that was the last hit: back to the nearest one");
		if (count == 1)
			info.notes.push_back("only one hit along this ray");
		if (info.valid)
			g_lastRefId = info.object.refFormId;
		Publish(info, "step", ray);
	}

	/** 1 to 8 hex digits: a reference id typed after the command. */
	bool ParseRefId(const std::string& a_text, std::uint32_t& a_id)
	{
		if (a_text.empty() || a_text.size() > 8)
			return false;
		std::uint32_t value = 0;
		const auto    result = std::from_chars(a_text.data(), a_text.data() + a_text.size(), value, 16);
		if (result.ec != std::errc() || result.ptr != a_text.data() + a_text.size())
			return false;
		a_id = value;
		return true;
	}

	bool ExecuteWaila(const RE::SCRIPT_PARAMETER*, const char* a_compiled, RE::TESObjectREFR* a_thisObj, RE::TESObjectREFR*, RE::Script*, RE::ScriptLocals*, float&,
		std::uint32_t& a_offset)
	{
		try {
			std::string mode = ReadModeText(a_compiled, a_offset);
			std::ranges::transform(mode, mode.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			spdlog::info("lookingat: argument '{}'", mode);
			std::uint32_t id = 0;
			if (mode.empty()) {
				PickForCommand(a_thisObj, "the reference the command ran on");
			} else if (CardCommand(mode)) {
				spdlog::info("lookingat {}", mode);
			} else if (mode != "next" && mode != "json" && mode != "shot" && mode != "sky" && mode != "ground" && mode != "water" && mode != "find_water" && mode.rfind("near_", 0) != 0 && ParseRefId(mode, id)) {
				auto* form = RE::TESForm::GetFormByID(id);
				auto* target = form ? form->As<RE::TESObjectREFR>() : nullptr;
				if (target) {
					PickForCommand(target, "reference id typed after the command");
				} else {
					spdlog::info("lookingat: no loaded reference {:08X}", id);
					Print(std::format("[What Am I Looking At] no loaded reference with id {:08X}", id));
				}
			} else if (mode == "sky") {
				PickForMode(ti::PickMode::kSky, mode);
			} else if (mode == "ground") {
				PickForMode(ti::PickMode::kGround, mode);
			} else if (mode == "water") {
				PickForMode(ti::PickMode::kWater, mode);
			} else if (mode.rfind("near_", 0) == 0) {
				NearCommand(std::string_view(mode).substr(5));
			} else if (mode == "find_water") {
				const std::string text = ti::FindWaterNear();
				spdlog::info("lookingat find_water: {}", text);
				Print("[What Am I Looking At] " + text);
			} else if (mode == "ground_probe") {
				const std::string probe = ti::TerrainProbe();
				std::size_t       from = 0;
				while (from < probe.size()) {
					auto to = probe.find('\n', from);
					if (to == std::string::npos)
						to = probe.size();
					Print("[What Am I Looking At] " + probe.substr(from, to - from));
					from = to + 1;
				}
			} else if (mode == "next") {
				StepHit(+1);
			} else if (mode == "shot") {
				if (ti::overlay::RequestShot())
					spdlog::info("lookingat shot: the next presented frame will be saved");
				else
					Print("[What Am I Looking At] no card overlay is running, so there is nothing to save");
			} else if (mode == "json") {
				spdlog::info("lookingat json");
				Print(std::format("[What Am I Looking At] the last pick is in {}", (g_config.outputFolder / L"last-pick.json").string()));
			} else {
				spdlog::info("lookingat: unknown argument '{}'", mode);
				Print(std::format("[What Am I Looking At] unknown argument '{}'. Use: lookingat [refid|ground|water|sky|next|shot|json|scale_<n>|card_show|card_hide|card_reset|\"place:<x>:<y>:<w>:<h>\"]", mode));
			}
		} catch (const std::exception& e) {
			spdlog::error("lookingat command failed: {}", e.what());
		} catch (...) {
			spdlog::error("lookingat command failed: unknown exception");
		}
		return true;
	}

	void RegisterCommand()
	{
		RE::SCRIPT_FUNCTION* command = nullptr;
		std::string_view     replaced;
		for (const auto name : kReplacedCommands) {
			command = RE::SCRIPT_FUNCTION::LocateConsoleCommand(name);
			if (command) {
				replaced = name;
				break;
			}
		}
		if (!command) {
			spdlog::error("none of the console commands {} {} {} found: `lookingat` is unavailable", kReplacedCommands[0], kReplacedCommands[1], kReplacedCommands[2]);
			return;
		}
		g_commandParams[0].paramName = "mode";
		g_commandParams[0].paramType = RE::SCRIPT_PARAM_TYPE::kChar;
		g_commandParams[0].optional = true;
		// the compile function of a vanilla command that has a string parameter, so `lookingat 000133FB` compiles
		if (const auto* donor = RE::SCRIPT_FUNCTION::LocateConsoleCommand("SaveGame"); donor && donor->compileFunction) {
			command->compileFunction = donor->compileFunction;
			spdlog::info("lookingat: compile function taken from SaveGame");
		}
		command->functionName = "lookingat";
		command->shortName = "lookingat";
		command->helpString = kCommandHelp;
		command->referenceFunction = false;
		command->SetParameters(g_commandParams);
		command->executeFunction = ExecuteWaila;
		spdlog::info("console command `lookingat` registered (replaces {})", replaced);
	}

	/** The console's selected reference as of the last frame with the left button up: what was selected before a click. */
	std::atomic<std::uint32_t> g_selectionBefore{ 0 };

	void SampleSelection()
	{
		auto ref = RE::Console::GetCurrentPickREFR().get();
		g_selectionBefore = ref ? ref->GetFormID() : 0u;
	}

	/** Left button released with the console open and the card not involved: the click that selects an object. */
	void OnConsoleClick()
	{
		if (!g_config.consoleClick)
			return;
		auto ray = ti::CursorRay();  // cursor position now, before anything moves
		const std::uint32_t before = g_selectionBefore.load();
		RunLater([ray, before]() { PickSelected(ray, before); });
	}

	void OnMessage(F4SE::MessagingInterface::Message* a_message)
	{
		if (a_message->type != F4SE::MessagingInterface::kGameDataReady)
			return;
		RegisterCommand();
		ti::PreloadArchives();
		if (!g_config.showCard) {
			spdlog::info("card: off in the ini (ShowCard=0)");
			return;
		}
		// the frame hook, then the mouse hooks that hook starts using once the first frame has drawn (no keyboard key does anything)
		ti::fo4::SetClickHandler(&OnConsoleClick);
		ti::fo4::SetSampleHandler(&SampleSelection);
		if (!ti::overlay::Install(g_config, [](int a_delta) { RunLater([a_delta]() { StepHit(a_delta); }); }))
			return;
		spdlog::info("listening: console click {}", g_config.consoleClick ? "on" : "off");
		if (g_config.showSampleCard) {
			const auto sample = ti::fo4::MakeSampleReport();
			ti::overlay::SetCard(sample, ti::hold::TakeFor(sample));
			spdlog::info("card: ShowSampleCard=1, the sample report is on screen");
		}
	}
}

// F4SE 0.7.x (NG and AE) reads this table; address library and structure bits say which runtimes the plugin is built for.
F4SE_PLUGIN_VERSION = []() noexcept {
	F4SE::PluginVersionData v{};
	v.PluginVersion({ 1, 1, 1, 0 });
	v.PluginName("WhatAmILookingAt");
	v.AuthorName("sayngwyn");
	v.UsesAddressLibraryNG(true);
	v.UsesAddressLibraryAE(true);
	v.IsLayoutDependentNG(true);
	v.IsLayoutDependentAE(true);
	return v;
}();

// F4SE 0.6.x (OG 1.10.163) calls this one.
F4SE_PLUGIN_QUERY(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
{
	a_info->infoVersion = F4SE::PluginInfo::kVersion;
	a_info->name = "WhatAmILookingAt";
	a_info->version = 1;
	return !a_f4se->IsEditor() && a_f4se->RuntimeVersion() == REL::Version{ 1, 10, 163, 0 };
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	F4SE::Init(a_f4se, { .logName = "WhatAmILookingAt", .trampoline = true, .trampolineSize = 512 });
	g_config = ti::LoadConfig(L"Data\\F4SE\\Plugins\\WhatAmILookingAt.ini", DefaultOutputFolder());
	const auto runtime = F4SE::GetRuntimeVersion();
	spdlog::info("{} loaded on Fallout 4 {} ({}), F4SE {}; enabled={} showCard={} showSampleCard={} consoleClick={}; output folder {}",
		kPluginName, runtime.string("."), RuntimeName(runtime), F4SE::GetF4SEVersion().string("."), g_config.enabled, g_config.showCard, g_config.showSampleCard, g_config.consoleClick,
		g_config.outputFolder.string());
	if (!g_config.enabled) {
		spdlog::info("disabled in the ini");
		return true;
	}
	if (const auto* messaging = F4SE::GetMessagingInterface(); messaging) {
		messaging->RegisterListener(OnMessage);
	} else {
		spdlog::error("no F4SE messaging interface: the card will not be installed");
	}
	return true;
}
