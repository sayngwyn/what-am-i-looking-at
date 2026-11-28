// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// The Fallout 4 half of the overlay (the card itself is core/Overlay.cpp).
//
// Where the card is drawn. The game's own end-of-frame function, BSGraphics::Renderer::End (the one that leads to
// IDXGISwapChain::Present), is not patched itself and neither is the swap chain's method table: the Steam overlay, ReShade and ENB
// all patch the DXGI functions, and patching the table as well made the Steam overlay call itself until the stack ran out (seen on
// Skyrim). Instead every `call` in the game's code that targets End is re-pointed, through the F4SE trampoline, at a thunk that
// draws the card and then runs End as before. Callers are found by scanning the game's code section for calls to End's address, so
// the same code serves 1.10.163, 1.10.984 and 1.11.240 without per-version offsets beyond commonlibf4's address for End.
#include "PCH.h"

#include "OverlayHooks.h"

#include "Overlay.h"
#include "OverlayPlatform.h"

#include <Windows.h>
#include <dxgi.h>
#include <intrin.h>

namespace ti::fo4
{
	namespace
	{
		using EndFn = void (*)(void*);

		EndFn             g_originalEnd = nullptr;
		bool              g_drawBefore = true;  ///< the hooked function contains the Present: draw before it, else after it
		std::atomic<HWND> g_hwnd{ nullptr };
		std::atomic<int>  g_frames{ 0 };

		WNDPROC           g_originalWndProc = nullptr;
		std::atomic<bool> g_windowHooked{ false };

		std::string HexBytes(std::uintptr_t a_from, std::size_t a_count)
		{
			std::string text;
			for (std::size_t i = 0; i < a_count; ++i)
				text += std::format("{:02X} ", *reinterpret_cast<const std::uint8_t*>(a_from + i));
			return text;
		}

		/** True when the bytes hold `call [reg+0x40]`, the call of IDXGISwapChain::Present through its method table. */
		bool HoldsPresentCall(std::uintptr_t a_from, std::size_t a_count)
		{
			for (std::size_t i = 0; i + 7 < a_count; ++i) {
				const auto* p = reinterpret_cast<const std::uint8_t*>(a_from + i);
				if (p[0] == 0xFF && (p[1] & 0xF8) == 0x50 && p[2] == 0x40)
					return true;
				if (p[0] == 0xFF && (p[1] & 0xF8) == 0x90 && p[2] == 0x40 && p[3] == 0 && p[4] == 0 && p[5] == 0)
					return true;
				if ((p[0] & 0xF0) == 0x40 && p[1] == 0xFF && (p[2] & 0xF8) == 0x50 && p[3] == 0x40)  // REX prefix, then the same call
					return true;
				if ((p[0] & 0xF0) == 0x40 && p[1] == 0xFF && (p[2] & 0xF8) == 0x90 && p[3] == 0x40 && p[4] == 0 && p[5] == 0 && p[6] == 0)
					return true;
			}
			return false;
		}

		/** Every `E8 rel32` in [a_begin, a_begin + a_size) whose target is a_target. */
		std::vector<std::uintptr_t> FindCallers(std::uintptr_t a_begin, std::size_t a_size, std::uintptr_t a_target)
		{
			std::vector<std::uintptr_t> sites;
			const auto*                 bytes = reinterpret_cast<const std::uint8_t*>(a_begin);
			for (std::size_t i = 0; i + 5 <= a_size; ++i) {
				if (bytes[i] != 0xE8)
					continue;
				std::int32_t rel;
				std::memcpy(&rel, bytes + i + 1, sizeof(rel));
				if (a_begin + i + 5 + static_cast<std::intptr_t>(rel) == a_target)
					sites.push_back(a_begin + i);
			}
			return sites;
		}

		IDXGISwapChain* MainSwapChain()
		{
			auto* data = RE::BSGraphics::GetRendererData();
			if (!data)
				return nullptr;
			return reinterpret_cast<IDXGISwapChain*>(data->renderWindow[0].swapChain);
		}

		// ---- the console click -------------------------------------------------------------------------------------------
		// A left-button release with the console open is "the click that selects an object". The press and release are polled (the
		// game reads its mouse through DirectInput, so the window messages are not enough), and a click the card consumed (its
		// buttons, its drag) is told apart by the time the window hook last swallowed a mouse message.
		ClickHandler              g_clickHandler = nullptr;
		ClickHandler              g_sampleHandler = nullptr;
		bool                      g_leftWasDown = false;
		std::atomic<std::int64_t> g_consumedAtMs{ -100000 };
		bool                      g_releasePending = false;
		std::int64_t              g_releaseAtMs = 0;

		std::int64_t NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		/**
		 * Whether the game itself has the left mouse button down: its mouse device keeps a held-down time per button (code 0 is the left
		 * button; 0 when up, above 0 while down; seen in game). Unlike the OS key state this does not depend on the window being in front,
		 * and it also sees clicks that reach the game as raw input rather than as a hardware press (a test tool's, a remote desktop's).
		 */
		bool GameLeftButtonDown()
		{
			auto* manager = RE::BSInputDeviceManager::GetSingleton();
			auto* device = manager ? manager->devices[static_cast<std::size_t>(RE::INPUT_DEVICE::kMouse)] : nullptr;
			if (!device)
				return false;
			auto it = device->deviceButtons.find(0u);
			return it != device->deviceButtons.end() && it->second && it->second->heldDownSecs > 0.0f;
		}

		void PollClick()
		{
			if (!g_clickHandler)
				return;
			const HWND hwnd = g_hwnd.load();
			const bool front = hwnd && GetForegroundWindow() == hwnd;
			const bool down = GameLeftButtonDown() || (front && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
			if (g_releasePending && NowMs() - g_releaseAtMs >= 40) {
				// decided a few frames after the release, when the window hook has had its turn
				g_releasePending = false;
				if (g_consumedAtMs.load() < g_releaseAtMs - 400 && overlay::platform::ConsoleOpen())
					g_clickHandler();
			}
			if (g_leftWasDown && !down) {
				g_releasePending = true;
				g_releaseAtMs = NowMs();
			}
			g_leftWasDown = down;
			if (g_sampleHandler && !down && !g_releasePending)
				g_sampleHandler();
		}

		void Frame()
		{
			auto* swap = MainSwapChain();
			if (!swap)
				return;
			if (g_frames.fetch_add(1) == 0) {
				DXGI_SWAP_CHAIN_DESC desc{};
				if (SUCCEEDED(swap->GetDesc(&desc))) {
					g_hwnd = desc.OutputWindow;
					spdlog::info("card: first frame: swap chain {}x{} format {} window {:X}", desc.BufferDesc.Width, desc.BufferDesc.Height, static_cast<int>(desc.BufferDesc.Format),
						reinterpret_cast<std::uintptr_t>(desc.OutputWindow));
				}
				InstallWindowHook();
			}
			PollClick();
			overlay::DrawFrame(swap);
		}

		// Which of the hooked calls the game really runs, and how often: counted by return address, logged once after about 240 calls
		// of frames, so a second End call per frame (the card then drawn twice) shows up in the log.
		constexpr std::size_t kMaxSites = 16;
		std::uintptr_t        g_siteReturn[kMaxSites]{};
		std::atomic<int>      g_siteCount[kMaxSites]{};
		std::size_t           g_siteTotal = 0;
		std::atomic<int>      g_endCalls{ 0 };
		std::chrono::steady_clock::time_point g_firstEnd;

		void CountCaller(std::uintptr_t a_return)
		{
			for (std::size_t i = 0; i < g_siteTotal; ++i) {
				if (g_siteReturn[i] == a_return) {
					g_siteCount[i].fetch_add(1);
					break;
				}
			}
			const int n = g_endCalls.fetch_add(1) + 1;
			if (n == 1) {
				g_firstEnd = std::chrono::steady_clock::now();
			} else if (n == 240) {
				const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_firstEnd).count();
				std::string text;
				for (std::size_t i = 0; i < g_siteTotal; ++i)
					text += std::format(" {:X}={}", g_siteReturn[i] - 5, g_siteCount[i].load());
				spdlog::info("card: {} calls to Renderer::End in {:.1f} s ({:.1f} per second); per hooked caller:{}", n, seconds, n / std::max(seconds, 0.001), text);
			}
		}

		void EndThunk(void* a_self)
		{
			CountCaller(reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
			if (g_drawBefore)
				Frame();
			g_originalEnd(a_self);
			if (!g_drawBefore)
				Frame();
		}

		// ---- the game window -----------------------------------------------------------------------------------------
		LRESULT CALLBACK WndProcThunk(HWND a_hwnd, UINT a_msg, WPARAM a_wparam, LPARAM a_lparam)
		{
			switch (a_msg) {
			case WM_LBUTTONDOWN:
			case WM_LBUTTONUP:
			case WM_RBUTTONDOWN:
			case WM_RBUTTONUP:
			case WM_MBUTTONDOWN:
			case WM_MBUTTONUP:
				if (overlay::InputFilterWanted()) {
					const unsigned button = (a_msg == WM_LBUTTONDOWN || a_msg == WM_LBUTTONUP) ? 0u : (a_msg == WM_RBUTTONDOWN || a_msg == WM_RBUTTONUP) ? 1u : 2u;
					const bool     down = a_msg == WM_LBUTTONDOWN || a_msg == WM_RBUTTONDOWN || a_msg == WM_MBUTTONDOWN;
					if (overlay::FilterMouse(overlay::MouseKind::kButton, button, down)) {
						g_consumedAtMs = NowMs();
						return 0;
					}
				}
				break;
			case WM_MOUSEWHEEL:
				if (overlay::InputFilterWanted()) {
					const int delta = GET_WHEEL_DELTA_WPARAM(a_wparam);
					if (delta != 0 && overlay::FilterMouse(delta > 0 ? overlay::MouseKind::kWheelUp : overlay::MouseKind::kWheelDown, 0, true))
						return 0;
				}
				break;
			default:
				break;
			}
			return CallWindowProcW(g_originalWndProc, a_hwnd, a_msg, a_wparam, a_lparam);
		}
	}

	void SetClickHandler(ClickHandler a_handler)
	{
		g_clickHandler = a_handler;
	}

	void SetSampleHandler(ClickHandler a_handler)
	{
		g_sampleHandler = a_handler;
	}

	bool InstallWindowHook()
	{
		const HWND hwnd = g_hwnd.load();
		if (!hwnd) {
			spdlog::warn("card: no game window known yet; clicks are polled only");
			return false;
		}
		if (g_windowHooked.exchange(true))
			return true;
		g_originalWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WndProcThunk)));
		if (!g_originalWndProc) {
			spdlog::warn("card: the game window could not be subclassed (error {}); clicks on the card also reach the game", GetLastError());
			g_windowHooked = false;
			return false;
		}
		spdlog::info("card: game window {:X} subclassed (mouse messages)", reinterpret_cast<std::uintptr_t>(hwnd));
		return true;
	}

	bool ClientArea(float& a_width, float& a_height)
	{
		const HWND hwnd = g_hwnd.load();
		RECT       r{};
		if (!hwnd || !GetClientRect(hwnd, &r) || r.right <= 0 || r.bottom <= 0)
			return false;
		a_width = static_cast<float>(r.right);
		a_height = static_cast<float>(r.bottom);
		return true;
	}
}

namespace ti::overlay::platform
{
	bool InstallFrameHook()
	{
		using namespace ti::fo4;
		auto* swap = MainSwapChain();
		if (!swap) {
			spdlog::error("card: no swap chain; no card will be drawn");
			return false;
		}
		spdlog::info("card: swap chain {:X}", reinterpret_cast<std::uintptr_t>(swap));

		const std::uintptr_t target = RE::ID::BSGraphics::Renderer::End.address();
		spdlog::info("card: Renderer::End at {:X}: {}", target, HexBytes(target, 48));
		g_drawBefore = HoldsPresentCall(target, 0x400);

		const auto section = REX::FModule::GetExecutingModule().GetSection(".text");
		if (section.GetSize() == 0) {
			spdlog::error("card: the game's code section was not found; no card will be drawn");
			return false;
		}
		const auto sites = FindCallers(section.GetAddress(), section.GetSize(), target);
		spdlog::info("card: {} call(s) to Renderer::End in the code section {:X}..{:X}", sites.size(), section.GetAddress(), section.GetAddress() + section.GetSize());
		if (sites.empty()) {
			spdlog::error("card: nothing calls Renderer::End; no card will be drawn");
			return false;
		}
		std::size_t hooked = 0;
		for (const auto site : sites) {
			spdlog::info("card: caller {:X}: {}", site, HexBytes(site - 8, 24));
			const auto original = REL::GetTrampoline().write_call<5>(site, &EndThunk);
			if (!original) {
				spdlog::warn("card: caller {:X} could not be hooked", site);
				continue;
			}
			if (!g_originalEnd)
				g_originalEnd = reinterpret_cast<EndFn>(original);
			if (g_siteTotal < kMaxSites)
				g_siteReturn[g_siteTotal++] = site + 5;
			++hooked;
		}
		if (hooked == 0) {
			spdlog::error("card: no call to Renderer::End could be hooked; no card will be drawn");
			return false;
		}
		spdlog::info("card: {} call(s) hooked; the card is drawn {} End ({})", hooked, g_drawBefore ? "before" : "after",
			g_drawBefore ? "a Present call was found in its body, so the card must go in first" : "no Present call was found inside it, so the card is drawn once it returns");
		return true;
	}

	bool ConsoleOpen()
	{
		auto* ui = RE::UI::GetSingleton();
		return ui && ui->GetMenuOpen(RE::Console::MENU_NAME);
	}

	void ConsolePrint(const std::string& a_line)
	{
		// AddString adds no line break of its own (Skyrim's Print does), so consecutive lines ran together in the console: end every line with one.
		if (auto* log = RE::ConsoleLog::GetSingleton()) {
			std::string line = a_line;
			if (line.empty() || line.back() != '\n')
				line += '\n';
			log->AddString(line.c_str());
		}
	}

	ID3D11RenderTargetView* UiRenderTarget()
	{
		// Fallout 4 keeps its old behavior: the card is drawn into the swap chain's back buffer (null here means exactly that).
		return nullptr;
	}

	std::filesystem::path FontFolder()
	{
		wchar_t    buffer[MAX_PATH]{};
		const auto n = GetFullPathNameW(L"Data\\F4SE\\Plugins\\WhatAmILookingAt\\fonts", MAX_PATH, buffer, nullptr);
		return n ? std::filesystem::path(buffer) : std::filesystem::path(L"Data\\F4SE\\Plugins\\WhatAmILookingAt\\fonts");
	}
}

namespace ti
{
	bool CursorFraction(float& a_fractionX, float& a_fractionY)
	{
		const HWND hwnd = fo4::g_hwnd.load();
		if (!hwnd)
			return false;
		RECT r{};
		if (!GetClientRect(hwnd, &r) || r.right <= 0 || r.bottom <= 0)
			return false;
		// The game's own menu cursor is the truth for "where did the click land": it follows the mouse in play and is also what a click made
		// by a test tool or remote session moves (those never touch the OS pointer). Its position is in client pixels.
		if (const auto* cursor = RE::MenuCursor::GetSingleton()) {
			a_fractionX = static_cast<float>(cursor->cursorPosX) / static_cast<float>(r.right);
			a_fractionY = static_cast<float>(cursor->cursorPosY) / static_cast<float>(r.bottom);
			return a_fractionX >= 0.0f && a_fractionX <= 1.0f && a_fractionY >= 0.0f && a_fractionY <= 1.0f;
		}
		POINT p{};
		if (!GetCursorPos(&p) || !ScreenToClient(hwnd, &p))
			return false;
		a_fractionX = static_cast<float>(p.x) / static_cast<float>(r.right);
		a_fractionY = static_cast<float>(p.y) / static_cast<float>(r.bottom);
		return a_fractionX >= 0.0f && a_fractionX <= 1.0f && a_fractionY >= 0.0f && a_fractionY <= 1.0f;
	}

}
