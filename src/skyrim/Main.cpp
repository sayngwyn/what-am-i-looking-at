// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// What Am I Looking At: click an object with the console open (or type `lookingat`) and see which part of it you hit,
// with that part's live textures and shader flags. A console tool: the card is drawn only while the console is open. Output: [What Am I Looking At] console lines and last-pick.json.
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
#include "TextureHold.h"

#include <charconv>
#include <ctime>
#include <ShlObj.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace
{
	constexpr auto kPluginName = "What Am I Looking At 1.1.0"sv;

	ti::Config       g_config;
	std::uint64_t    g_counter = 0;
	std::uint32_t    g_lastRefId = 0;

	/** What `lookingat next` steps through: the ray of the last pick and every hit along it. */
	struct LastPick
	{
		ti::RayResult            ray;
		std::vector<ti::HitInfo> hits;
		int                      index = -1;
	};
	LastPick g_last;

	void SetupLog()
	{
		auto folder = SKSE::log::log_directory();
		if (!folder)
			return;
		auto path = *folder / L"WhatAmILookingAt.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
		auto log = std::make_shared<spdlog::logger>("WhatAmILookingAt", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S] [%l] %v");
	}

	std::filesystem::path DefaultOutputFolder()
	{
		// Same folder the SKSE log lives in: Documents\My Games\<game edition>\SKSE (right for GOG/VR editions too).
		if (const auto logs = SKSE::log::log_directory())
			return *logs / L"WhatAmILookingAt";
		PWSTR documents = nullptr;
		std::filesystem::path result = L"WhatAmILookingAt";
		if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents)) && documents) {
			result = std::filesystem::path(documents) / L"My Games" / L"Skyrim Special Edition" / L"SKSE" / L"WhatAmILookingAt";
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

	void Print(const std::string& a_line)
	{
		if (auto* log = RE::ConsoleLog::GetSingleton())
			log->Print("%s", a_line.c_str());
	}

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
		// one line per pick: what, how, result (never per frame)
		spdlog::info("pick #{} via {}: {} {} '{}' ref {:08X} base {:08X}; part '{}' by {}; {} part(s), {} warning(s), {} fact(s), {} hit(s){}", a_info.counter, a_trigger,
			a_info.identity.kind, a_info.object.baseType, a_info.object.name, a_info.object.refFormId, a_info.object.baseFormId,
			a_info.clicked >= 0 ? a_info.shapes[static_cast<std::size_t>(a_info.clicked)].name : std::string("none"), a_info.method, a_info.shapes.size(),
			a_info.diagnostics.size(), a_info.facts.size(), a_info.hits.size(), a_info.valid ? std::string() : "; " + a_info.message);
	}

	bool ConsoleOpen()
	{
		auto* ui = RE::UI::GetSingleton();
		return ui && ui->IsMenuOpen(RE::Console::MENU_NAME);
	}

	struct TextRect
	{
		std::string name;
		float       x1{ 0 }, y1{ 0 }, x2{ 0 }, y2{ 0 };
	};

	/** Reads x, y, width, height of a display object (stage pixels, top-left registration). */
	bool ReadBox(const RE::GFxValue& a_object, float& a_x, float& a_y, float& a_w, float& a_h)
	{
		// the movie answers to the ActionScript 2 names (_x ...) in Skyrim's console
		const auto number = [&](const char* a_name, const char* a_alt, float& a_out) {
			RE::GFxValue v;
			if ((!a_object.GetMember(a_name, &v) || !v.IsNumber()) && (!a_object.GetMember(a_alt, &v) || !v.IsNumber()))
				return false;
			a_out = static_cast<float>(v.GetNumber());
			return true;
		};
		return number("_x", "x", a_x) && number("_y", "y", a_y) && number("_width", "width", a_w) && number("_height", "height", a_h);
	}

	/** The console movie's text widgets (selection line, history, command line) as stage rectangles, found by name anywhere in the movie. */
	std::vector<TextRect> ConsoleTextRects(RE::GFxMovieView& a_movie)
	{
		std::vector<TextRect> found;
		RE::GFxValue          root;
		if (!a_movie.GetVariable(&root, "_root") || !root.IsObject())
			return found;
		std::function<void(const RE::GFxValue&, const std::string&, float, float, int)> walk = [&](const RE::GFxValue& a_value, const std::string& a_path, float a_offX, float a_offY, int a_depth) {
			if (!a_value.IsDisplayObject() || a_depth > 5)
				return;
			a_value.VisitMembers([&](const char* a_name, const RE::GFxValue& a_child) {
				if (!a_name || !a_child.IsDisplayObject())
					return;
				float x = 0, y = 0, w = 0, h = 0;
				const bool box = ReadBox(a_child, x, y, w, h);
				const std::string name = a_name;
				if (box && (name == "CurrentSelection" || name == "CommandHistory" || name == "CommandEntry" || name == "CommandPrompt_tf"))
					found.push_back({ a_path + "." + name, a_offX + x, a_offY + y, a_offX + x + w, a_offY + y + h });
				walk(a_child, a_path + "." + name, a_offX + (box ? x : 0.0f), a_offY + (box ? y : 0.0f), a_depth + 1);
			});
		};
		walk(root, "_root", 0.0f, 0.0f, 0);
		return found;
	}

	/** One-time log of the movie's members when the text widgets are not found, so the next build can name them. */
	void LogConsoleTree(RE::GFxMovieView& a_movie)
	{
		RE::GFxValue root;
		if (!a_movie.GetVariable(&root, "_root") || !root.IsObject()) {
			spdlog::info("console layout: no _root");
			return;
		}
		int                                                                           lines = 0;
		std::function<void(const RE::GFxValue&, const std::string&, int)> walk = [&](const RE::GFxValue& a_value, const std::string& a_path, int a_depth) {
			if (!a_value.IsDisplayObject() || a_depth > 3)
				return;
			a_value.VisitMembers([&](const char* a_name, const RE::GFxValue& a_child) {
				if (!a_name || !a_child.IsDisplayObject() || ++lines > 60)
					return;
				float x = 0, y = 0, w = 0, h = 0;
				const bool box = ReadBox(a_child, x, y, w, h);
				spdlog::info("console layout: {}.{} {}", a_path, a_name, box ? std::format("x={:.1f} y={:.1f} w={:.1f} h={:.1f}", x, y, w, h) : "no box");
				walk(a_child, a_path + "." + a_name, a_depth + 1);
			});
		};
		walk(root, "_root", 0);
	}

	/**
	 * True when a click at this window fraction lands on the console's text (its real rectangles, read from the console movie; the dark
	 * backdrop is not counted, so the world beside and below the text still picks). If the movie does not show the text widgets, the lower
	 * left block where Skyrim draws the console text stands in, and the movie's members are logged once.
	 */
	bool ClickOnConsole(float a_fracX, float a_fracY)
	{
		auto* ui = RE::UI::GetSingleton();
		auto  menu = ui ? ui->GetMenu(RE::Console::MENU_NAME) : nullptr;
		if (!menu || !menu->uiMovie)
			return false;
		auto&      movie = *menu->uiMovie;
		const auto frame = movie.GetVisibleFrameRect();
		const float frameW = frame.right - frame.left;
		const float frameH = frame.bottom - frame.top;
		if (frameW <= 0.0f || frameH <= 0.0f)
			return false;
		const auto  rects = ConsoleTextRects(movie);
		static bool logged = false;
		if (!logged) {
			logged = true;
			spdlog::info("console layout: visible frame {} {} {} {}; {} text widget(s)", frame.left, frame.top, frame.right, frame.bottom, rects.size());
			for (const auto& r : rects)
				spdlog::info("console layout: {} {:.1f},{:.1f} to {:.1f},{:.1f}", r.name, r.x1, r.y1, r.x2, r.y2);
			if (rects.empty())
				LogConsoleTree(movie);
		}
		if (rects.empty())
			return a_fracX < 0.55f && a_fracY > 0.6f;
		// stage pixels of the click: the visible frame (Skyrim's console stage is centred on y = 0, e.g. 0..2560 by -720..720) maps onto the window
		const float px = frame.left + a_fracX * frameW;
		const float py = frame.top + a_fracY * frameH;
		constexpr float pad = 6.0f;
		for (const auto& r : rects)
			if (px >= r.x1 - pad && px <= r.x2 + pad && py >= r.y1 - pad && py <= r.y2 + pad)
				return true;
		return false;
	}

	/**
	 * Console path. The console's own selection is only a hint (it can be stale, it toggles off on a second click, and it never selects land,
	 * water or sky): the ray from the cursor decides. The ray's nearest hit within reach is reported (an object part, land, water or the sky); the selection is only logged. Every click that reports nothing logs why.
	 */
	void PickSelected(ti::RayResult a_ray, std::uint32_t a_selectionBefore)
	{
		auto ref = RE::Console::GetSelectedRef();
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
			spdlog::info("click ignored: on the console text ({:.3f}, {:.3f})", a_ray.cursor.fracX, a_ray.cursor.fracY);
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
			const auto verdict = ti::JudgeSelection(selected, a_ray.ray, info, changed, g_config.hotkeyMaxDistance, false, fallback);
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
	// Lets a test (or a player without a mouse) pick from the console line. It takes over ONE vanilla console command that
	// does nothing in the retail game: ToggleVerletDebug (short name tvd). UESP: "In release builds of Skyrim, this command
	// is a no-op and does absolutely nothing" (a Dawnguard vampire-lord-cape physics test). Chosen because it has no function in
	// the game and is obscure: none of the popular console-touching mods we know of (po3's Tweaks, ConsoleUtilSSE, More
	// Informative Console) is known to take it; that is from their public descriptions, not a scan of their code. ForceRSXCrash was the first choice but is not in the SE 1.6.1170 table (proved in
	// game: the table was dumped and it is absent), so there are two fallbacks that are also retail no-ops per UESP and present
	// in the table: ToggleActorMover (tam) and ToggleMapCam (tmc). If none is found the plugin carries on without `lookingat`.
	// Usage: lookingat [refid|ground|water|sky|next|shot|json]. No argument: the console-selected reference (picked with the ray through the
	// middle of the screen), else whatever the crosshair points at (part of an object, land, water, else the sky). The report and
	// last-pick.json are written exactly as for a click.

	constexpr std::array kReplacedCommands{ "ToggleVerletDebug"sv, "ToggleActorMover"sv, "ToggleMapCam"sv };
	constexpr auto kCommandHelp = "lookingat [refid|ground|water|sky|next|shot|json|scale_<75-200>|card_show|card_hide|card_reset|\"place:<x>:<y>:<w>:<h>\"]: What Am I Looking At. Inspects the reference id given or selected in the console, else what the crosshair points at (object part, land, water or sky). next steps to the next hit along the same ray. shot saves the next frame, card included, as a PNG. scale_150 sets the card size to 150 percent; card_reset puts the card back at its starting place and size; place (in quotes, so the console takes the colons and dots) puts it at x,y (fractions of the screen) with width w and height h (design pixels, 0 = fit the content).";

	/** a_text cut at ':' ' ' '=' and '_' into pieces. The console's own compiler only passes '_' through unquoted, so `lookingat scale_150` and `lookingat card_reset` work as typed and `lookingat "scale:150"` works quoted. */
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

	RE::SCRIPT_PARAMETER g_commandParams[1] = {
		{ "mode", RE::SCRIPT_PARAM_TYPE::kChar, true }
	};

	/** a_target: a reference named on the command line (`lookingat 000133FB`) or the one the engine ran the command on; null for neither. */
	void PickForCommand(RE::TESObjectREFR* a_target, std::string_view a_targetHow)
	{
		auto ray = ti::CentreRay();
		RE::TESObjectREFR* ref = a_target;
		std::string_view   how = a_targetHow;
		if (!ref) {
			if (auto selected = RE::Console::GetSelectedRef()) {
				ref = selected.get();
				how = "console-selected ref";
			}
		}
		if (ref && ray.ok) {
			// the object the console selected is the one reported, even when the middle of the screen is not on it
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

	/** `lookingat next` and the card's step control: the next (+1) or previous (-1) hit along the ray of the last pick (parts of the object, what is behind it, the land, the water). */
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

	bool ExecuteWaila(const RE::SCRIPT_PARAMETER*, RE::SCRIPT_FUNCTION::ScriptData* a_data, RE::TESObjectREFR* a_thisObj, RE::TESObjectREFR*, RE::Script*,
		RE::ScriptLocals*, double&, std::uint32_t&)
	{
		try {
			std::string mode;
			if (a_data && a_data->numParams > 0)
				mode = a_data->GetStringChunk()->GetString();
			std::ranges::transform(mode, mode.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			std::uint32_t id = 0;
			if (mode.empty()) {
				PickForCommand(a_thisObj, "the reference the command ran on");
			} else if (CardCommand(mode)) {
				spdlog::info("lookingat {}", mode);
			} else if (mode != "sky" && mode != "ground" && mode != "water" && mode != "next" && mode != "json" && ParseRefId(mode, id)) {
				auto* form = RE::TESForm::LookupByID(id);
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
		// Take the compile function of a vanilla command that has a string parameter, so `lookingat sky` compiles.
		if (const auto* donor = RE::SCRIPT_FUNCTION::LocateConsoleCommand("SaveGame"); donor && donor->compileFunction)
			command->compileFunction = donor->compileFunction;
		command->functionName = "lookingat";
		command->shortName = "lookingat";
		command->helpString = kCommandHelp;
		command->referenceFunction = false;
		command->SetParameters(g_commandParams);
		command->executeFunction = ExecuteWaila;
		spdlog::info("console command `lookingat` registered (replaces {})", replaced);
	}

	/** Runs a_work on the game thread two frames from now, so the console has finished selecting. */
	template <class Fn>
	void RunLater(Fn a_work)
	{
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks)
			return;
		tasks->AddTask([a_work]() mutable {
			SKSE::GetTaskInterface()->AddTask([a_work]() mutable {
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

	/** The console's selected reference when the left button went down: what was selected before the click. */
	std::uint32_t g_selectionBefore = 0;

	class InputSink : public RE::BSTEventSink<RE::InputEvent*>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
		{
			if (!a_event || !g_config.enabled)
				return RE::BSEventNotifyControl::kContinue;
			for (auto* e = *a_event; e; e = e->next) {
				if (e->GetEventType() != RE::INPUT_EVENT_TYPE::kButton)
					continue;
				auto* button = static_cast<RE::ButtonEvent*>(e);
				const auto device = button->GetDevice();
				const auto id = button->GetIDCode();
				if (device == RE::INPUT_DEVICE::kMouse && (id == 0 || id == 256) && button->Value() != 0.0f && ConsoleOpen()) {
					auto selected = RE::Console::GetSelectedRef();
					g_selectionBefore = selected ? selected->GetFormID() : 0u;
				}
				// release of the left button with the console open: the click that selects an object (no keyboard key does anything)
				if (device == RE::INPUT_DEVICE::kMouse && (id == 0 || id == 256) && g_config.consoleClick && button->Value() == 0.0f && ConsoleOpen()) {
					auto ray = ti::CursorRay();  // cursor position now, before anything moves
					const std::uint32_t before = g_selectionBefore;
					RunLater([ray, before]() { PickSelected(ray, before); });
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	InputSink g_sink;

	void OnMessage(SKSE::MessagingInterface::Message* a_message)
	{
		if (a_message->type != SKSE::MessagingInterface::kDataLoaded)
			return;
		RegisterCommand();
		if (g_config.showCard) {
			if (ti::overlay::Install(g_config, [](int a_delta) { RunLater([a_delta]() { StepHit(a_delta); }); }))
				ti::overlay::InstallInputFilter();
		} else {
			spdlog::info("card: off in the ini (ShowCard=0)");
		}
		if (auto* input = RE::BSInputDeviceManager::GetSingleton()) {
			input->AddEventSink(&g_sink);
			spdlog::info("listening: console click {}", g_config.consoleClick ? "on" : "off");
		} else {
			spdlog::error("no input device manager: nothing will trigger");
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse, { .trampoline = true, .trampolineSize = 64 });
	SetupLog();
	g_config = ti::LoadConfig(L"Data\\SKSE\\Plugins\\WhatAmILookingAt.ini", DefaultOutputFolder());
	spdlog::info("{} loaded on Skyrim {}; enabled={} consoleClick={} printToConsole={} fullConsoleReport={} writeJson={} maxListedParts={} hotkeyMaxDistance={}; output folder {}",
		kPluginName, REL::Module::get().version().string("."), g_config.enabled, g_config.consoleClick, g_config.printToConsole,
		g_config.fullConsoleReport, g_config.writeJson, g_config.maxListedParts, g_config.hotkeyMaxDistance, g_config.outputFolder.string());
	if (!g_config.enabled) {
		spdlog::info("disabled in the ini");
		return true;
	}
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}
