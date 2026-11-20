// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// Draws the card with Dear ImGui (dx11 backend) just before the game's swap chain presents, and keeps mouse clicks that land on the
// card away from the game. Written from the Dear ImGui example for DirectX 11. Game-free: the frame hook, the input hook
// and the few game questions it asks (console open, swap chain, camera) are OverlayPlatform.h, supplied by each game's layer.
#include "Overlay.h"

#include "Card.h"
#include "CardLayout.h"
#include "CardState.h"
#include "Output.h"
#include "OverlayPlatform.h"

#include <Windows.h>

#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <ScreenGrab.h>
#include <wincodec.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <format>
#include <fstream>
#include <memory>
#include <mutex>

namespace ti::overlay
{
	namespace
	{
		Config                 g_config;
		StepHandler            g_step;
		std::atomic<bool>      g_installed{ false };

		std::mutex                       g_mutex;  ///< guards the card state and every ImGui call
		std::shared_ptr<const PickInfo>  g_info;
		hold::HeldPtr                    g_held;
		std::uint64_t                    g_serial = 0;
		std::uint64_t                    g_drawnSerial = 0;
		std::chrono::steady_clock::time_point g_publishedAt;  ///< when SetCard last ran, to tell a card shown late from one published late
		bool                             g_shotPending = false;
		int                              g_shotCounter = 0;
		bool                             g_userHidden = false;  ///< `lookingat card_hide` put the card away
		CardState                        g_state;               ///< what the user changed: position, size, size multiplier
		CardState                        g_savedState;          ///< what card-layout.json holds right now
		std::atomic<int>                 g_wheelSteps{ 0 };     ///< Ctrl + wheel notches over the card, taken up by the next frame
		std::atomic<float>               g_hoverPad{ 6.0f };    ///< how far outside the card the edges still grab (resize), in pixels

		// what the input filter needs to know about the last frame
		std::atomic<bool>  g_showing{ false };
		std::atomic<bool>  g_interactive{ false };  ///< the card is up and the mouse cursor exists (console open)
		std::atomic<float> g_rectX{ 0 }, g_rectY{ 0 }, g_rectW{ 0 }, g_rectH{ 0 };
		std::atomic<float> g_displayW{ 0 }, g_displayH{ 0 };
		bool               g_pressedOnCard[3] = { false, false, false };

		// D3D / ImGui state (touched only under g_mutex, from the render thread)
		bool                  g_ready = false;
		bool                  g_initFailed = false;
		ID3D11Device*         g_device = nullptr;
		ID3D11DeviceContext*  g_context = nullptr;
		ID3D11BlendState*     g_noBlend = nullptr;
		card::Fonts           g_fonts;
		std::vector<std::vector<char>> g_fontData;  ///< the font files, kept alive for the atlas
		std::chrono::steady_clock::time_point g_lastFrame;

		// ---- the remembered layout --------------------------------------------------------------------------------------
		std::filesystem::path StateFile() { return g_config.outputFolder / L"card-layout.json"; }

		void LoadState()
		{
			std::ifstream in(StateFile(), std::ios::binary);
			if (!in)
				return;
			const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			CardState         loaded;
			if (StateFromJson(text, loaded)) {
				g_state = loaded;
				g_savedState = loaded;
				spdlog::info("card: layout loaded from {} (position {}, size {}, scale {})", StateFile().string(), loaded.hasPos ? "set" : "default",
					loaded.hasSize ? "set" : "default", loaded.hasScale ? std::format("{:.2f}", loaded.scale) : std::string("default"));
			} else {
				spdlog::info("card: {} holds no saved layout; the default stands", StateFile().string());
			}
		}

		/** Writes the layout when it changed (call under g_mutex, with no mouse button held, so a drag writes once). */
		void SaveStateIfChanged()
		{
			if (g_state == g_savedState)
				return;
			std::error_code ec;
			std::filesystem::create_directories(g_config.outputFolder, ec);
			if (const auto error = WriteFileAtomic(StateFile(), StateToJson(g_state)); !error.empty()) {
				spdlog::error("card: could not write {}: {}", StateFile().string(), error);
				g_savedState = g_state;  // do not retry every frame
				return;
			}
			g_savedState = g_state;
			spdlog::info("card: layout saved (scale {:.2f}, position {}, size {})", g_state.hasScale ? g_state.scale : g_config.cardScale,
				g_state.hasPos ? std::format("{:.3f},{:.3f}", g_state.x, g_state.y) : std::string("default"),
				g_state.hasSize ? std::format("{:.0f}x{:.0f}", g_state.w, g_state.h) : std::string("default"));
		}

		float UiScaleLocked() { return ClampUiScale(g_state.hasScale ? g_state.scale : g_config.cardScale); }

		// ---- fonts ------------------------------------------------------------------------------------------------------
		ImFont* LoadFont(const std::filesystem::path& a_file)
		{
			std::ifstream in(a_file, std::ios::binary);
			if (!in) {
				spdlog::warn("card font missing: {} (the default font stands in)", a_file.filename().string());
				return nullptr;
			}
			std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			if (bytes.size() < 1000)
				return nullptr;
			g_fontData.push_back(std::move(bytes));
			auto& data = g_fontData.back();
			ImFontConfig cfg;
			cfg.FontDataOwnedByAtlas = false;
			cfg.OversampleH = 2;
			cfg.OversampleV = 1;
			ImFont* font = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(data.data(), static_cast<int>(data.size()), 0.0f, &cfg);
			if (!font)
				spdlog::warn("card font could not be read: {}", a_file.filename().string());
			return font;
		}

		void LoadFonts()
		{
			const auto folder = platform::FontFolder();
			g_fonts.display = LoadFont(folder / L"SayngwynSerif-Regular.ttf");
			g_fonts.text = LoadFont(folder / L"SayngwynSerifText-Regular.ttf");
			g_fonts.bold = LoadFont(folder / L"SayngwynSerifText-Bold.ttf");
			if (!g_fonts.text)
				g_fonts.text = ImGui::GetIO().Fonts->AddFontDefault();
			if (!g_fonts.bold)
				g_fonts.bold = g_fonts.text;
			if (!g_fonts.display)
				g_fonts.display = g_fonts.bold;
		}

		// ---- clipboard (no platform backend: the system clipboard through Win32) -------------------------------------
		bool g_clipboardOk = true;
		void SetClipboardText(ImGuiContext*, const char* a_text)
		{
			if (!a_text)
				return;
			const int wide = MultiByteToWideChar(CP_UTF8, 0, a_text, -1, nullptr, 0);
			if (wide <= 0 || !OpenClipboard(nullptr)) {
				g_clipboardOk = false;
				return;
			}
			EmptyClipboard();
			if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(wide) * sizeof(wchar_t))) {
				if (auto* dst = static_cast<wchar_t*>(GlobalLock(mem))) {
					MultiByteToWideChar(CP_UTF8, 0, a_text, -1, dst, wide);
					GlobalUnlock(mem);
					if (!SetClipboardData(CF_UNICODETEXT, mem))
						GlobalFree(mem);
				} else {
					GlobalFree(mem);
				}
			}
			CloseClipboard();
		}

		// ---- init -------------------------------------------------------------------------------------------------------
		bool EnsureInit(IDXGISwapChain* a_swap)
		{
			if (g_ready)
				return true;
			if (g_initFailed)
				return false;
			ID3D11Device* device = nullptr;
			if (FAILED(a_swap->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device))) || !device) {
				spdlog::error("card: the swap chain has no D3D11 device; no card will be drawn");
				g_initFailed = true;
				return false;
			}
			ID3D11DeviceContext* context = nullptr;
			device->GetImmediateContext(&context);
			if (!context) {
				device->Release();
				g_initFailed = true;
				return false;
			}
			g_device = device;
			g_context = context;
			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			auto& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.LogFilename = nullptr;
			io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
			ImGui::GetPlatformIO().Platform_SetClipboardTextFn = SetClipboardText;
			LoadFonts();
			if (!ImGui_ImplDX11_Init(g_device, g_context)) {
				spdlog::error("card: the ImGui DirectX 11 backend did not start; no card will be drawn");
				ImGui::DestroyContext();
				g_initFailed = true;
				return false;
			}
			D3D11_BLEND_DESC blend{};
			blend.RenderTarget[0].BlendEnable = FALSE;
			blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			g_device->CreateBlendState(&blend, &g_noBlend);
			g_lastFrame = std::chrono::steady_clock::now();
			g_ready = true;
			spdlog::info("card: overlay started (fonts: display {}, text {}, bold {})", g_fonts.display ? "ok" : "none", g_fonts.text ? "ok" : "none", g_fonts.bold ? "ok" : "none");
			return true;
		}

		// ---- the picked part, outlined -----------------------------------------------------------------------------------
		/** A ring drawn as dashes: the thing is behind something nearer along the ray. */
		void DashedCircle(ImDrawList* a_list, ImVec2 a_centre, float a_radius, ImU32 a_col, float a_thick)
		{
			constexpr int   kDashes = 14;
			constexpr float kTau = 6.2831853f;
			for (int i = 0; i < kDashes; ++i) {
				const float from = kTau * (static_cast<float>(i) / kDashes);
				const float to = from + kTau / kDashes * 0.55f;
				a_list->PathArcTo(a_centre, a_radius, from, to, 6);
				a_list->PathStroke(a_col, 0, a_thick);
			}
		}

		void DrawHighlight(const PickInfo& a_info, float a_w, float a_h, float a_scale)
		{
			// only for something a ray found, and never for water or sky (their card is about the surface or the weather, not a spot)
			const auto& kind = a_info.identity.kind;
			if (!a_info.fromRay || !a_info.hasRay || kind == "water" || kind == "sky")
				return;
			// behind something nearer along the ray (a step-through hit past the first): dimmer and dashed, so it is not mistaken for what is in front
			const bool  behind = a_info.hitIndex > 0;
			auto*       dl = ImGui::GetBackgroundDrawList();
			const ImU32 col = behind ? IM_COL32(0xd3, 0x6e, 0x6e, 150) : IM_COL32(0xd3, 0x6e, 0x6e, 235);
			const float thick = std::max(1.5f, 1.6f * a_scale);
			const auto  project = [&](Vec3 a_point, ImVec2& a_out) {
				float fx = 0, fy = 0;
				if (!WorldToScreen(a_point, fx, fy))
					return false;
				a_out = ImVec2(fx * a_w, fy * a_h);
				return true;
			};
			const auto onScreen = [&](ImVec2 a_c) { return a_c.x > 0 && a_c.y > 0 && a_c.x < a_w && a_c.y < a_h; };
			const auto circle = [&](ImVec2 a_c, float a_radius) {
				if (behind)
					DashedCircle(dl, a_c, a_radius, col, thick);
				else
					dl->AddCircle(a_c, a_radius, col, 64, thick);
			};
			const auto ring = [&](Vec3 a_point) {
				ImVec2 c;
				if (project(a_point, c) && onScreen(c)) {
					circle(c, 13.0f * a_scale);
					dl->AddCircleFilled(c, 2.5f * a_scale, col);
				}
			};
			if (kind == "terrain") {
				ring(a_info.terrain.point);
				return;
			}
			if (a_info.clicked < 0 || a_info.clicked >= static_cast<int>(a_info.shapes.size()))
				return;
			const auto& shape = a_info.shapes[static_cast<std::size_t>(a_info.clicked)];
			ImVec2 centre;
			float  radius = 0.0f;
			if (shape.boundRadius > 0.0f && project(shape.boundCentre, centre) && onScreen(centre)) {
				const Vec3 probes[3] = { { shape.boundRadius, 0, 0 }, { 0, shape.boundRadius, 0 }, { 0, 0, shape.boundRadius } };
				for (const auto& offset : probes) {
					ImVec2 q;
					if (project(shape.boundCentre + offset, q))
						radius = std::max(radius, std::hypot(q.x - centre.x, q.y - centre.y));
				}
			}
			const float limit = 0.35f * std::min(a_w, a_h);
			if (radius <= 0.0f || radius > limit) {
				// the part's centre is off screen, or its bound is bigger than a sensible outline: mark where the ray touched it instead
				// (nothing is drawn when that spot is off screen too)
				if (shape.exactHit)
					ring(shape.hitPoint);
				return;
			}
			circle(centre, std::max(radius, 12.0f * a_scale));
		}

		// ---- the frame --------------------------------------------------------------------------------------------------
		void SaveShot(IDXGISwapChain* a_swap, ID3D11Texture2D* a_back)
		{
			static bool comReady = false;
			if (!comReady) {
				CoInitializeEx(nullptr, COINIT_MULTITHREADED);
				comReady = true;
			}
			std::error_code ec;
			std::filesystem::create_directories(g_config.outputFolder, ec);
			std::filesystem::path path;
			do {
				path = g_config.outputFolder / std::format(L"card-{}.png", ++g_shotCounter);
			} while (std::filesystem::exists(path, ec));
			const HRESULT hr = DirectX::SaveWICTextureToFile(g_context, a_back, GUID_ContainerFormatPng, path.c_str(), &GUID_WICPixelFormat24bppBGR);
			(void)a_swap;
			if (SUCCEEDED(hr)) {
				spdlog::info("shot saved: {}", path.string());
				platform::ConsolePrint("[What Am I Looking At] shot saved: " + path.string());
			} else {
				spdlog::error("shot failed (0x{:08X}): {}", static_cast<unsigned>(hr), path.string());
				platform::ConsolePrint("[What Am I Looking At] shot failed (see WhatAmILookingAt.log)");
			}
		}

		void OnFrame(IDXGISwapChain* a_swap)
		{
			std::shared_ptr<const PickInfo> info;
			hold::HeldPtr                   held;
			bool                            shot = false, hidden = false;
			std::uint64_t                   serial = 0;
			{
				std::scoped_lock lock(g_mutex);
				info = g_info;
				held = g_held;
				serial = g_serial;
				shot = g_shotPending;
				hidden = g_userHidden;
			}
			const bool consoleOpen = platform::ConsoleOpen();
			const bool show = g_config.showCard && info && !hidden && consoleOpen;  // nothing is drawn with the console closed
			g_showing = show;
			g_interactive = show;
			if (!show) {
				g_rectW = 0;
				g_rectH = 0;
			}
			if (!show && !shot)
				return;

			ID3D11Texture2D* back = nullptr;
			if (FAILED(a_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back))) || !back)
				return;
			const bool ready = EnsureInit(a_swap);
			if (show && ready) {
				D3D11_TEXTURE2D_DESC desc{};
				back->GetDesc(&desc);
				ID3D11RenderTargetView* rtv = nullptr;
				if (SUCCEEDED(g_device->CreateRenderTargetView(back, nullptr, &rtv)) && rtv) {
					std::scoped_lock lock(g_mutex);
					const float w = static_cast<float>(desc.Width), h = static_cast<float>(desc.Height);
					g_displayW = w;
					g_displayH = h;
					auto& io = ImGui::GetIO();
					io.DisplaySize = ImVec2(w, h);
					const auto now = std::chrono::steady_clock::now();
					io.DeltaTime = std::clamp(std::chrono::duration<float>(now - g_lastFrame).count(), 0.001f, 0.25f);
					g_lastFrame = now;
					float fx = 0, fy = 0;
					const bool cursorKnown = consoleOpen && CursorFraction(fx, fy);
					if (cursorKnown)
						io.AddMousePosEvent(fx * w, fy * h);
					else
						io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
					// The game draws its own menu cursor before this overlay runs, so the card (drawn after) covers it. Over the card, and
					// while a press that began on it is held, ImGui draws the cursor itself, on top of everything. Elsewhere the game's
					// cursor is the only one, so there is never a pair.
					{
						const float pad = g_hoverPad.load();
						const bool  onCard = cursorKnown && g_rectW > 0 && fx * w >= g_rectX - pad && fx * w <= g_rectX + g_rectW + pad && fy * h >= g_rectY - pad &&
						                    fy * h <= g_rectY + g_rectH + pad;
						io.MouseDrawCursor = cursorKnown && (onCard || g_pressedOnCard[0] || g_pressedOnCard[1] || g_pressedOnCard[2]);
					}

					// size multiplier: Ctrl + wheel notches since the last frame
					if (const int notches = g_wheelSteps.exchange(0); notches != 0) {
						g_state.hasScale = true;
						g_state.scale = StepUiScale(UiScaleLocked(), notches, kUiScaleStep);
					}
					const float ui = UiScaleLocked();
					const float autoScale = std::max(0.5f, h / 1080.0f);
					const float cardScale = autoScale * ui;

					ImGui_ImplDX11_NewFrame();
					ImGui::NewFrame();
					card::Params params;
					params.info = info.get();
					params.held = held.get();
					params.fonts = g_fonts;
					params.scale = cardScale;
					params.uiPercent = static_cast<int>(std::lround(ui * 100.0f));
					params.displayW = w;
					params.displayH = h;
					params.corner = g_config.cardCorner;
					if (g_state.hasPos) {
						params.placement.hasPos = true;
						params.placement.x = g_state.x * w;
						params.placement.y = g_state.y * h;
					}
					if (g_state.hasSize) {
						params.placement.hasSize = true;
						params.placement.w = g_state.w * cardScale;
						params.placement.h = g_state.h * cardScale;
					}
					params.newCard = serial != g_drawnSerial;
					if (params.newCard) {
						const auto lateMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - g_publishedAt).count();
						if (lateMs > 750)
							spdlog::info("card: pick {} was first drawn {} ms after it was published", serial, lateMs);
					}
					params.step = g_step;
					g_drawnSerial = serial;
					const auto result = card::Draw(params);
					g_hoverPad = ImGui::GetStyle().WindowBorderHoverPadding;
					// what the user's hands did this frame
					if (result.scaleSteps != 0) {
						g_state.hasScale = true;
						g_state.scale = StepUiScale(ui, result.scaleSteps, 0.1f);
					}
					if (result.dragMoved) {
						const auto at = ClampToScreen({ result.nextX, result.nextY, result.w, result.h }, w, h, 0.0f, 0.0f, 8.0f);
						g_state.hasPos = true;
						g_state.x = at.x / w;
						g_state.y = at.y / h;
					}
					if (result.resizeW || result.resizeH) {
						g_state.hasPos = true;
						g_state.x = result.x / w;
						g_state.y = result.y / h;
						if (!g_state.hasSize) {
							g_state.hasSize = true;
							g_state.h = 0.0f;
						}
						if (result.resizeW)
							g_state.w = result.w / cardScale;
						if (result.resizeH)
							g_state.h = result.h / cardScale;
						if (g_state.w <= 0.0f)
							g_state.w = result.w / cardScale;
					}
					if (!io.MouseDown[0])
						SaveStateIfChanged();
					DrawHighlight(*info, w, h, params.scale);
					ImGui::Render();

					ID3D11RenderTargetView* previous[1] = { nullptr };
					ID3D11DepthStencilView* previousDepth = nullptr;
					g_context->OMGetRenderTargets(1, previous, &previousDepth);
					g_context->OMSetRenderTargets(1, &rtv, nullptr);
					ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
					g_context->OMSetRenderTargets(1, previous, previousDepth);
					if (previous[0])
						previous[0]->Release();
					if (previousDepth)
						previousDepth->Release();

					g_rectX = result.x;
					g_rectY = result.y;
					g_rectW = result.w;
					g_rectH = result.h;
				}
				if (rtv)
					rtv->Release();
			}
			if (shot && ready) {
				{
					std::scoped_lock lock(g_mutex);
					g_shotPending = false;
				}
				SaveShot(a_swap, back);
			}
			back->Release();
		}

		// ---- the input filter --------------------------------------------------------------------------------------------
		bool OverCard()
		{
			float fx = 0, fy = 0;
			if (!CursorFraction(fx, fy))
				return false;
			const float w = g_displayW, h = g_displayH;
			const float x = fx * w, y = fy * h;
			const float pad = g_hoverPad.load();
			return g_rectW > 0 && x >= g_rectX - pad && x <= g_rectX + g_rectW + pad && y >= g_rectY - pad && y <= g_rectY + g_rectH + pad;
		}

	}

	bool Install(const Config& a_config, StepHandler a_step)
	{
		g_config = a_config;
		g_step = std::move(a_step);
		LoadState();
		if (!platform::InstallFrameHook())
			return false;
		g_installed = true;
		return true;
	}

	void DrawFrame(IDXGISwapChain* a_swap)
	{
		try {
			OnFrame(a_swap);
		} catch (const std::exception& e) {
			static bool reported = false;
			if (!reported) {
				reported = true;
				spdlog::error("card: frame failed: {}", e.what());
			}
		} catch (...) {
		}
	}

	bool InputFilterWanted()
	{
		return g_showing.load() || g_pressedOnCard[0] || g_pressedOnCard[1] || g_pressedOnCard[2];
	}

	bool FilterMouse(MouseKind a_kind, unsigned a_button, bool a_active)
	{
		const bool interactive = g_interactive.load();
		const bool over = interactive && OverCard();
		if (a_kind != MouseKind::kButton) {
			if (!SwallowMouseWheel(interactive, over))
				return false;
			if (a_active && g_ready) {
				const bool up = a_kind == MouseKind::kWheelUp;
				if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
					g_wheelSteps += up ? 1 : -1;  // Ctrl + wheel over the card changes its size
				} else {
					std::scoped_lock lock(g_mutex);
					ImGui::GetIO().AddMouseWheelEvent(0.0f, up ? 1.0f : -1.0f);
				}
			}
			return true;
		}
		if (a_button > 2)
			return false;
		const bool swallow = SwallowMouseButton(interactive, over, a_active, g_pressedOnCard[a_button]);
		if (swallow && g_ready) {
			std::scoped_lock lock(g_mutex);
			auto&            io = ImGui::GetIO();
			float            fx = 0, fy = 0;
			if (CursorFraction(fx, fy))
				io.AddMousePosEvent(fx * g_displayW, fy * g_displayH);
			io.AddMouseButtonEvent(static_cast<int>(a_button), a_active);
		}
		return swallow;
	}

	void SetCard(const PickInfo& a_info, hold::HeldPtr a_held)
	{
		std::scoped_lock lock(g_mutex);
		g_info = std::make_shared<const PickInfo>(a_info);
		g_held = std::move(a_held);
		g_publishedAt = std::chrono::steady_clock::now();
		++g_serial;
	}

	ID3D11Device* Device() { return g_device; }

	bool CardShowing() { return g_showing.load(); }

	bool CardActive()
	{
		std::scoped_lock lock(g_mutex);
		return g_installed && g_config.showCard && !g_userHidden;
	}

	void SetCardHidden(bool a_hidden)
	{
		std::scoped_lock lock(g_mutex);
		g_userHidden = a_hidden;
	}

	float UiScale()
	{
		std::scoped_lock lock(g_mutex);
		return UiScaleLocked();
	}

	float SetUiScale(float a_scale)
	{
		std::scoped_lock lock(g_mutex);
		g_state.hasScale = true;
		g_state.scale = ClampUiScale(a_scale);
		return g_state.scale;
	}

	void ResetLayout()
	{
		std::scoped_lock lock(g_mutex);
		g_state = CardState{};
		g_userHidden = false;
	}

	void PlaceCard(float a_x, float a_y, float a_w, float a_h)
	{
		std::scoped_lock lock(g_mutex);
		g_state.hasPos = true;
		g_state.x = a_x;
		g_state.y = a_y;
		if (a_w > 0.0f) {
			g_state.hasSize = true;
			g_state.w = a_w;
			g_state.h = std::max(0.0f, a_h);
		}
	}
	bool RequestShot()
	{
		if (!g_installed)
			return false;
		std::scoped_lock lock(g_mutex);
		g_shotPending = true;
		return true;
	}

	void NoBlendBegin(const ImDrawList*, const ImDrawCmd*)
	{
		if (g_context && g_noBlend) {
			constexpr float factor[4] = { 0, 0, 0, 0 };
			g_context->OMSetBlendState(g_noBlend, factor, 0xFFFFFFFF);
		}
	}
}
