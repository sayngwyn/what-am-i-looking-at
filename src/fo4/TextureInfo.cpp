// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
#include "PCH.h"

#include "TextureInfo.h"

#include "Ba2.h"

#include <Windows.h>
#include <d3d11.h>
#include <mutex>
#include <atomic>
#include <thread>
#include <unordered_map>

namespace ti
{
	namespace
	{
		// ---- GPU info ---------------------------------------------------------------------------------------------------

		struct RawDesc
		{
			bool         ok = false;
			bool         is2d = false;
			unsigned int width = 0, height = 0, mips = 0, array = 0, format = 0, misc = 0;
		};

		/** The only place that calls into a COM object the engine owns: a bad pointer returns ok = false instead of crashing. */
		RawDesc ReadDesc(void* a_resource)
		{
			RawDesc raw;
			__try {
				auto*                    resource = static_cast<ID3D11Resource*>(a_resource);
				D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
				resource->GetType(&dimension);
				if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
					D3D11_TEXTURE2D_DESC desc{};
					static_cast<ID3D11Texture2D*>(resource)->GetDesc(&desc);
					raw.is2d = true;
					raw.width = desc.Width;
					raw.height = desc.Height;
					raw.mips = desc.MipLevels;
					raw.array = desc.ArraySize;
					raw.format = static_cast<unsigned int>(desc.Format);
					raw.misc = desc.MiscFlags;
				}
				raw.ok = true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				raw.ok = false;
			}
			return raw;
		}

		// ---- OS paths ---------------------------------------------------------------------------------------------------

		std::string ToUtf8(const wchar_t* a_text, int a_length)
		{
			if (a_length <= 0)
				return {};
			const int n = WideCharToMultiByte(CP_UTF8, 0, a_text, a_length, nullptr, 0, nullptr, nullptr);
			if (n <= 0)
				return {};
			std::string out(static_cast<std::size_t>(n), '\0');
			WideCharToMultiByte(CP_UTF8, 0, a_text, a_length, out.data(), n, nullptr, nullptr);
			return out;
		}

		std::wstring DataDir()
		{
			wchar_t    buffer[MAX_PATH]{};
			const auto n = GetFullPathNameW(L"Data", MAX_PATH, buffer, nullptr);
			return n ? std::wstring(buffer, n) : std::wstring(L"Data");
		}

		struct IoStatus
		{
			union {
				long  status;
				void* pointer;
			};
			std::uintptr_t information;
		};
		struct FileNameInfo
		{
			unsigned long length;
			wchar_t       name[1];
		};
		using NtQueryInformationFileFn = long(__stdcall*)(HANDLE, IoStatus*, void*, unsigned long, unsigned long);

		/** The OS's own path of a file as the handle sees it (under MO2: the real mod folder, not the virtual Data folder). */
		std::string RealPathOf(const std::wstring& a_fullPath)
		{
			HANDLE h = CreateFileW(a_fullPath.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
				FILE_FLAG_BACKUP_SEMANTICS, nullptr);
			if (h == INVALID_HANDLE_VALUE)
				return {};
			std::string result;
			wchar_t     buffer[2048]{};
			const auto  n = GetFinalPathNameByHandleW(h, buffer, 2048, VOLUME_NAME_DOS);
			if (n > 0 && n < 2048) {
				std::wstring p(buffer, n);
				if (p.rfind(L"\\\\?\\", 0) == 0)
					p.erase(0, 4);
				result = ToUtf8(p.c_str(), static_cast<int>(p.size()));
			}
			// MO2's virtual file system answers the call above with the virtual path; the handle itself points at the real file,
			// and the native call (which the file system does not intercept) says where.
			if (ModFolderFromPath(result).empty()) {
				static const auto query = reinterpret_cast<NtQueryInformationFileFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationFile"));
				if (query) {
					alignas(8) char info[4096]{};
					IoStatus        status{};
					if (query(h, &status, info, sizeof(info), 9 /* FileNameInformation */) >= 0) {
						const auto* f = reinterpret_cast<const FileNameInfo*>(info);
						const auto  chars = static_cast<int>(f->length / sizeof(wchar_t));
						const auto  native = ToUtf8(f->name, chars);
						if (!ModFolderFromPath(native).empty())
							result = native;
					}
				}
			}
			CloseHandle(h);
			return result;
		}

		// ---- archives ---------------------------------------------------------------------------------------------------

		struct ArchiveEntry
		{
			Ba2Index     index;
			std::string  name;
			std::wstring full;
		};

		std::atomic<bool> g_indexReady{ false };  ///< the archive index is built: set by the index thread, read by every pick

		/** Builds the index (seconds) the first time it is called and returns it; only the index thread calls this, never a pick. */
		std::vector<ArchiveEntry>& Archives()
		{
			static std::vector<ArchiveEntry> list;
			static std::once_flag            once;
			std::call_once(once, [] {
			const auto started = std::chrono::steady_clock::now();
			try {
				// every .ba2 the Data folder shows (under MO2 that includes each enabled mod's archives)
				std::vector<std::filesystem::path> found;
				for (const auto& entry : std::filesystem::directory_iterator(DataDir())) {
					std::error_code ec;
					if (entry.is_regular_file(ec) && entry.path().extension() == L".ba2")
						found.push_back(entry.path());
				}
				// The engine loads an archive when a loaded plugin owns it ("<plugin>.ba2", "<plugin> - <anything>.ba2"); archives of
				// later plugins override earlier ones. Walk the plugins in load order.
				std::vector<std::string> plugins;
				if (auto* handler = RE::TESDataHandler::GetSingleton()) {
					for (auto* file : handler->compiledFileCollection.files)
						if (file)
							plugins.emplace_back(file->GetFilename());
					for (auto* file : handler->compiledFileCollection.smallFiles)
						if (file)
							plugins.emplace_back(file->GetFilename());
				}
				std::vector<bool> used(found.size(), false);
				for (const auto& plugin : plugins) {
					std::vector<std::size_t> mine;
					for (std::size_t i = 0; i < found.size(); ++i)
						if (!used[i] && Ba2BelongsToPlugin(found[i].filename().string(), plugin))
							mine.push_back(i);
					std::sort(mine.begin(), mine.end(), [&](std::size_t a, std::size_t b) { return found[a].filename().wstring() < found[b].filename().wstring(); });
					for (const auto i : mine) {
						used[i] = true;
						if (auto index = Ba2Index::Open(found[i]))
							list.push_back({ std::move(*index), found[i].filename().string(), found[i].wstring() });
					}
				}
				const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
				spdlog::info("archive index: {} BA2(s) read from {} found in Data for {} plugin(s), {} ms", list.size(), found.size(), plugins.size(), ms);
			} catch (const std::exception& e) {
				spdlog::error("archive index failed: {}", e.what());
			} catch (...) {
				spdlog::error("archive index failed: unknown exception");
			}
			});
			g_indexReady = true;
			return list;
		}

		/** Starts the index thread once (the game-data message does, but a pick must never be the one to wait for it). */
		void StartIndex()
		{
			static std::once_flag started;
			std::call_once(started, [] {
				std::thread([] {
					try {
						Archives();
					} catch (...) {
					}
				}).detach();
			});
		}
	}

	GpuInfo ReadGpuInfo(RE::NiTexture* a_texture)
	{
		GpuInfo g;
		if (!a_texture) {
			g.note = "no texture object";
			return g;
		}
		auto* renderer = a_texture->rendererTexture;
		if (!renderer || !renderer->texture) {
			g.note = "no GPU texture bound";
			return g;
		}
		const RawDesc raw = ReadDesc(renderer->texture);
		if (!raw.ok) {
			g.note = "GPU texture not readable";
			return g;
		}
		if (!raw.is2d) {
			g.note = "not a 2D texture";
			return g;
		}
		g.present = true;
		g.width = static_cast<int>(raw.width);
		g.height = static_cast<int>(raw.height);
		g.mips = static_cast<int>(raw.mips);
		g.arraySize = static_cast<int>(raw.array);
		g.dxgi = static_cast<int>(raw.format);
		g.format = DxgiFormatName(g.dxgi);
		g.cube = (raw.misc & D3D11_RESOURCE_MISC_TEXTURECUBE) != 0;
		g.bytes = EstimateTextureBytes(g.width, g.height, g.mips, g.arraySize, g.dxgi);
		return g;
	}

	void PreloadArchives()
	{
		// reading the BA2 directories takes seconds (37 archives, about 16 s): do it off the game's main thread at start-up. A pick
		// that comes before it has finished does not wait for it (that froze the game, and the card with it, for the whole time):
		// it reads loose files only and says the archives are still being indexed.
		StartIndex();
	}

	std::vector<std::uint8_t> ReadArchiveTexture(std::string_view a_dataPath, int a_maxSide, std::string* a_why, bool* a_pending)
	{
		try {
			if (!g_indexReady) {
				StartIndex();
				if (a_pending)
					*a_pending = true;
				if (a_why)
					*a_why = "the archive index is still being built";
				return {};
			}
			const std::string path = Ba2NormalisedPath(a_dataPath);
			auto&             archives = Archives();
			for (auto it = archives.rbegin(); it != archives.rend(); ++it) {  // the last loaded archive wins
				if (const auto* entry = it->index.Find(path))
					return it->index.ReadSmallDds(*entry, a_maxSide, a_why);
			}
			if (a_why)
				*a_why = "in no loaded archive";
		} catch (const std::exception& e) {
			if (a_why)
				*a_why = std::string("archive read failed: ") + e.what();
		} catch (...) {
			if (a_why)
				*a_why = "archive read failed";
		}
		return {};
	}

	FileOrigin ResolveOrigin(std::string_view a_dataPath)
	{
		static std::unordered_map<std::string, FileOrigin> cache;
		FileOrigin                                         o;
		try {
			const std::string path = Ba2NormalisedPath(a_dataPath);  // lower case, backslashes, no leading "data\"
			if (path.empty()) {
				o.note = "no path";
				return o;
			}
			if (const auto it = cache.find(path); it != cache.end())
				return it->second;

			o.looked = true;
			const std::wstring full = DataDir() + L"\\" + std::filesystem::path(path).wstring();
			std::error_code    ec;
			if (std::filesystem::is_regular_file(full, ec)) {
				o.source = "loose";
				o.realPath = RealPathOf(full);
				o.mod = ModFolderFromPath(o.realPath);
			} else if (!g_indexReady) {
				StartIndex();
				o.looked = false;
				o.note = "the archive index is still being built; pick again in a moment";
				return o;  // not remembered: the next pick looks again
			} else {
				auto& archives = Archives();
				for (auto it = archives.rbegin(); it != archives.rend(); ++it) {  // the last loaded archive wins
					if (it->index.Contains(path)) {
						o.source = "archive";
						o.archive = it->name;
						o.realPath = RealPathOf(it->full);
						o.mod = ModFolderFromPath(o.realPath);
						break;
					}
				}
				if (o.source.empty()) {
					o.source = "not found";
					o.note = archives.empty() ? "no archive index" : "";
				}
			}
			if (cache.size() > 4096)
				cache.clear();
			cache[path] = o;
		} catch (const std::exception& e) {
			o.note = std::string("origin lookup failed: ") + e.what();
		} catch (...) {
			o.note = "origin lookup failed";
		}
		return o;
	}
}
