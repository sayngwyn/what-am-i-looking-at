// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
#include "PCH.h"

#include "TextureInfo.h"

#include "Bsa.h"

#include <d3d11.h>
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
			BsaIndex     index;
			std::string  name;
			std::wstring full;
		};

		std::vector<ArchiveEntry>& Archives()
		{
			static std::vector<ArchiveEntry> list;
			static bool                      built = false;
			if (built)
				return list;
			built = true;
			const auto started = std::chrono::steady_clock::now();
			try {
				// every .bsa the Data folder shows (under MO2 that includes each enabled mod's archives)
				std::vector<std::filesystem::path> found;
				for (const auto& entry : std::filesystem::directory_iterator(DataDir())) {
					std::error_code ec;
					if (entry.is_regular_file(ec) && _wcsicmp(entry.path().extension().c_str(), L".bsa") == 0)  // any case: mods ship ".BSA" too
						found.push_back(entry.path());
				}
				// The engine loads an archive when a loaded plugin owns it ("<plugin>.bsa", "<plugin> - <anything>.bsa"); archives of
				// later plugins override earlier ones. Walk the plugins in load order.
				std::vector<std::string> plugins;
				if (auto* handler = RE::TESDataHandler::GetSingleton()) {
					for (auto* file : handler->files)
						if (file)
							plugins.emplace_back(file->GetFilename());
				}
				std::vector<bool> used(found.size(), false);
				for (const auto& plugin : plugins) {
					std::vector<std::size_t> mine;
					for (std::size_t i = 0; i < found.size(); ++i)
						if (!used[i] && ArchiveBelongsToPlugin(found[i].filename().string(), plugin))
							mine.push_back(i);
					std::sort(mine.begin(), mine.end(), [&](std::size_t a, std::size_t b) { return found[a].filename().wstring() < found[b].filename().wstring(); });
					for (const auto i : mine) {
						used[i] = true;
						if (auto index = BsaIndex::Open(found[i]))
							list.push_back({ std::move(*index), found[i].filename().string(), found[i].wstring() });
					}
				}
				const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
				spdlog::info("archive index: {} archive(s) read from {} found in Data for {} plugin(s), {} ms", list.size(), found.size(), plugins.size(), ms);
			} catch (const std::exception& e) {
				spdlog::error("archive index failed: {}", e.what());
			} catch (...) {
				spdlog::error("archive index failed: unknown exception");
			}
			return list;
		}

		std::string NormalisedDataPath(std::string_view a_path)
		{
			std::string p(a_path);
			for (auto& c : p)
				c = c == '/' ? '\\' : c;
			while (!p.empty() && p.front() == '\\')
				p.erase(p.begin());
			if (p.size() > 5 && (p.compare(0, 5, "Data\\") == 0 || p.compare(0, 5, "data\\") == 0))
				p.erase(0, 5);
			return p;
		}
	}

	GpuInfo ReadGpuInfo(RE::NiSourceTexture* a_texture)
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

	FileOrigin ResolveOrigin(std::string_view a_dataPath)
	{
		static std::unordered_map<std::string, FileOrigin> cache;
		FileOrigin                                         o;
		try {
			std::string path = NormalisedDataPath(a_dataPath);
			if (path.empty()) {
				o.note = "no path";
				return o;
			}
			std::string key = path;
			for (auto& c : key)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			if (const auto it = cache.find(key); it != cache.end())
				return it->second;

			o.looked = true;
			const std::wstring full = DataDir() + L"\\" + std::filesystem::path(path).wstring();
			std::error_code    ec;
			if (std::filesystem::is_regular_file(full, ec)) {
				o.source = "loose";
				o.realPath = RealPathOf(full);
				o.mod = ModFolderFromPath(o.realPath);
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
			cache[key] = o;
		} catch (const std::exception& e) {
			o.note = std::string("origin lookup failed: ") + e.what();
		} catch (...) {
			o.note = "origin lookup failed";
		}
		return o;
	}

	namespace
	{
		using GetFormEditorIdFn = const char* (*)(std::uint32_t);

		/** Calls po3 Tweaks' exported function. Guarded: a wrong signature or a bad pointer must not take the game down. */
		bool CallPo3(GetFormEditorIdFn a_fn, std::uint32_t a_id, char* a_out, std::size_t a_size)
		{
			__try {
				const char* text = a_fn(a_id);
				if (!text || !text[0])
					return false;
				std::size_t n = 0;
				for (; n + 1 < a_size && text[n]; ++n) {
					const auto c = static_cast<unsigned char>(text[n]);
					if (c < 0x20 || c > 0x7e)
						return false;  // not an identifier: refuse
					a_out[n] = text[n];
				}
				a_out[n] = '\0';
				return n > 0;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}
	}

	std::string EditorIdOf(std::uint32_t a_formId, const char* a_engineValue, std::string& a_source)
	{
		if (a_engineValue && a_engineValue[0]) {
			a_source = "engine";
			return a_engineValue;
		}
		// po3's Tweaks keeps the editor IDs the engine drops and exports GetFormEditorID(formID): optional, never required.
		static const auto fn = []() -> GetFormEditorIdFn {
			const HMODULE m = GetModuleHandleW(L"po3_Tweaks.dll");
			return m ? reinterpret_cast<GetFormEditorIdFn>(GetProcAddress(m, "GetFormEditorID")) : nullptr;
		}();
		if (fn) {
			char buffer[256]{};
			if (CallPo3(fn, a_formId, buffer, sizeof(buffer))) {
				a_source = "po3 Tweaks";
				return buffer;
			}
		}
		return {};
	}
}
