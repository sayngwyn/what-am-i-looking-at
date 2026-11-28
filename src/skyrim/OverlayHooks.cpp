// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// The Skyrim half of the overlay (the card itself is core/Overlay.cpp): hooks the game's end-of-frame call and its input dispatch,
// and answers the few game questions the card asks. Written from the CommonLibSSE-NG headers.
#include "PCH.h"

#include "OverlayHooks.h"

#include "Overlay.h"
#include "OverlayPlatform.h"

#include <d3d11.h>
#include <dxgi.h>

#include <atomic>

namespace ti::overlay
{
	namespace
	{
		// The game's own call at the end of a frame (the one that leads to IDXGISwapChain::Present) is hooked, not the swap chain's
		// method table: the Steam overlay, ReShade and ENB all patch the DXGI functions themselves, and patching the table as well
		// made the Steam overlay call itself until the stack ran out.
		using SiteFn = std::uint64_t (*)(std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t);
		using DispatchFn = void (*)(RE::BSTEventSource<RE::InputEvent*>*, RE::InputEvent* const*);

		SiteFn     g_originalSite = nullptr;
		bool       g_drawBefore = true;  ///< the hooked call contains the Present: draw before it, else after it
		DispatchFn g_originalDispatch = nullptr;
		std::atomic<bool> g_filterInstalled{ false };

		void Frame()
		{
			auto* window = RE::BSGraphics::Renderer::GetCurrentRenderWindow();
			auto* swap = window ? reinterpret_cast<IDXGISwapChain*>(window->swapChain) : nullptr;
			if (!swap)
				return;
			DrawFrame(swap);
		}

		std::uint64_t SiteThunk(std::uint64_t a_1, std::uint64_t a_2, std::uint64_t a_3, std::uint64_t a_4)
		{
			if (g_drawBefore)
				Frame();
			const auto result = g_originalSite(a_1, a_2, a_3, a_4);
			if (!g_drawBefore)
				Frame();
			return result;
		}

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
			for (std::size_t i = 0; i + 6 < a_count; ++i) {
				const auto* p = reinterpret_cast<const std::uint8_t*>(a_from + i);
				if (p[0] == 0xFF && (p[1] & 0xF8) == 0x50 && p[2] == 0x40)
					return true;
				if (p[0] == 0xFF && (p[1] & 0xF8) == 0x90 && p[2] == 0x40 && p[3] == 0 && p[4] == 0 && p[5] == 0)
					return true;
			}
			return false;
		}

		// ---- the input filter --------------------------------------------------------------------------------------------
		/** Feeds a mouse button or wheel event to the card, and says whether the game must not see it. */
		bool FeedMouse(const RE::ButtonEvent* a_event)
		{
			const auto          id = a_event->GetIDCode();
			const std::uint32_t code = id >= 256 ? id - 256 : id;
			if (code == RE::BSWin32MouseDevice::Key::kWheelUp || code == RE::BSWin32MouseDevice::Key::kWheelDown)
				return FilterMouse(code == RE::BSWin32MouseDevice::Key::kWheelUp ? MouseKind::kWheelUp : MouseKind::kWheelDown, 0, a_event->Value() != 0.0f);
			return FilterMouse(MouseKind::kButton, code, a_event->Value() != 0.0f);
		}

		void DispatchThunk(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent* const* a_events)
		{
			// Only the game's own input manager is filtered, and only when a card is up or a press that began on it is still held.
			bool relevant = a_events && *a_events && InputFilterWanted();
			if (relevant && static_cast<void*>(a_dispatcher) != static_cast<void*>(RE::BSInputDeviceManager::GetSingleton()))
				relevant = false;
			if (!relevant) {
				g_originalDispatch(a_dispatcher, a_events);
				return;
			}
			// Unlink the swallowed events for the duration of the call, then put the chain back exactly as the game built it.
			constexpr std::size_t kMax = 64;
			RE::InputEvent*       all[kMax];
			RE::InputEvent*       nextOf[kMax];
			bool                  drop[kMax];
			std::size_t           count = 0;
			bool                  anyDropped = false;
			for (auto* e = *a_events; e && count < kMax; e = e->next, ++count) {
				all[count] = e;
				nextOf[count] = e->next;
				drop[count] = false;
				if (e->GetEventType() == RE::INPUT_EVENT_TYPE::kButton && e->GetDevice() == RE::INPUT_DEVICE::kMouse) {
					drop[count] = FeedMouse(static_cast<const RE::ButtonEvent*>(e));
					anyDropped = anyDropped || drop[count];
				}
			}
			if (!anyDropped) {
				g_originalDispatch(a_dispatcher, a_events);
				return;
			}
			RE::InputEvent* head = nullptr;
			RE::InputEvent* tail = nullptr;
			for (std::size_t i = 0; i < count; ++i) {
				if (drop[i])
					continue;
				all[i]->next = nullptr;
				if (tail)
					tail->next = all[i];
				else
					head = all[i];
				tail = all[i];
			}
			RE::InputEvent* const filtered[1] = { head };
			g_originalDispatch(a_dispatcher, filtered);
			for (std::size_t i = 0; i < count; ++i)
				all[i]->next = nextOf[i];
		}
	}

	bool InstallInputFilter()
	{
		const REL::RelocationID id(67315, 68617);
		const auto              site = id.address() + 0x7B;
		const auto              opcode = *reinterpret_cast<const std::uint8_t*>(site);
		if (opcode != 0xE8) {
			spdlog::warn("card: input call site is not a call (byte {:02X} at {:X}); clicks on the card will also reach the game", opcode, site);
			return false;
		}
		g_originalDispatch = reinterpret_cast<DispatchFn>(SKSE::GetTrampoline().write_call<5>(site, &DispatchThunk));
		if (!g_originalDispatch) {
			spdlog::warn("card: input call could not be hooked");
			return false;
		}
		g_filterInstalled = true;
		spdlog::info("card: input filter installed");
		return true;
	}

	namespace platform
	{
		bool InstallFrameHook()
		{
			auto* window = RE::BSGraphics::Renderer::GetCurrentRenderWindow();
			if (!window || !window->swapChain) {
				spdlog::error("card: no swap chain; no card will be drawn");
				return false;
			}
			const REL::RelocationID id(75461, 77246);
			const auto              site = id.address() + 0x9;
			spdlog::info("card: frame call site {:X}: {}", site, HexBytes(site - 8, 24));
			if (*reinterpret_cast<const std::uint8_t*>(site) != 0xE8) {
				spdlog::error("card: the frame call site is not a call; no card will be drawn");
				return false;
			}
			const auto target = site + 5 + *reinterpret_cast<const std::int32_t*>(site + 1);
			spdlog::info("card: frame call target {:X}: {}", target, HexBytes(target, 48));
			g_drawBefore = HoldsPresentCall(target, 0x300);
			g_originalSite = reinterpret_cast<SiteFn>(SKSE::GetTrampoline().write_call<5>(site, &SiteThunk));
			if (!g_originalSite) {
				spdlog::error("card: the frame call could not be hooked; no card will be drawn");
				return false;
			}
			spdlog::info("card: frame call hooked; the card is drawn {} that call ({})", g_drawBefore ? "before" : "after",
				g_drawBefore ? "a Present call was found in its body, so the card must go in first" : "no Present call was found inside it, so the card is drawn once it returns");
			return true;
		}

		bool ConsoleOpen()
		{
			auto* ui = RE::UI::GetSingleton();
			return ui && ui->IsMenuOpen(RE::Console::MENU_NAME);
		}

		void ConsolePrint(const std::string& a_line)
		{
			if (auto* log = RE::ConsoleLog::GetSingleton())
				log->Print("%s", a_line.c_str());
		}

		ID3D11RenderTargetView* UiRenderTarget()
		{
			// The renderer's framebuffer target: the back buffer in an unmodified game; where Community Shaders' HDR or frame generation is on,
			// that target is pointed at the buffer CS composes over the back buffer at Present, so the card goes where the game's own UI goes.
			auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
			if (!renderer)
				return nullptr;
			return reinterpret_cast<ID3D11RenderTargetView*>(renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER].RTV);
		}

		std::filesystem::path FontFolder()
		{
			wchar_t    buffer[MAX_PATH]{};
			const auto n = GetFullPathNameW(L"Data\\SKSE\\Plugins\\WhatAmILookingAt\\fonts", MAX_PATH, buffer, nullptr);
			return n ? std::filesystem::path(buffer) : std::filesystem::path(L"Data\\SKSE\\Plugins\\WhatAmILookingAt\\fonts");
		}
	}
}
