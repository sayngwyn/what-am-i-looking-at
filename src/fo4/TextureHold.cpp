// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
//
// Fallout 4's side of the texture hold (core/TextureHold.h): keeps the engine texture objects a pick found bound, and reads a small
// copy (at most 128 pixels) of every other texture the card lists through the game's own file system (loose files and BA2s alike),
// so land, water, sky and worn-item thumbnails show too. A texture neither way finds shows a blank swatch.
#include "PCH.h"

#include "TextureGather.h"

#include "CardLayout.h"
#include "TextureInfo.h"
#include "Overlay.h"

#include <d3d11.h>
#include <DDSTextureLoader.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <unordered_set>

namespace ti::hold
{
	using Blob = std::vector<std::uint8_t>;

	struct Held
	{
		std::unordered_map<std::string, RE::NiPointer<RE::NiTexture>> textures;  ///< live texture objects the engine had bound
		std::unordered_map<std::string, std::shared_ptr<const Blob>>        files;     ///< a small DDS read through the game's own file system, per texture
		std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> made;  ///< views of the small ones, made on first draw
	};

	namespace
	{
		std::mutex                                                          g_mutex;
		std::unordered_map<std::string, RE::NiPointer<RE::NiTexture>> g_gathered;

		std::mutex                                                   g_cacheMutex;
		std::unordered_map<std::string, std::shared_ptr<const Blob>> g_cache;  ///< key -> small DDS (null: could not be read)

		void Want(std::unordered_set<std::string>& a_keys, const std::string& a_path)
		{
			// only pictures: a land layer's facts also hold a material file (.bgsm) and a texture set's form id
			if (a_path.size() > 4 && a_path.front() != '(' && _stricmp(a_path.c_str() + a_path.size() - 4, ".dds") == 0)
				a_keys.insert(TextureKey(a_path));
		}

		void WantShapes(std::unordered_set<std::string>& a_keys, const std::vector<ShapeInfo>& a_shapes)
		{
			for (const auto& s : a_shapes)
				for (const auto& t : s.textures) {
					Want(a_keys, t.path);
					Want(a_keys, t.loadedAs);
				}
		}

		void WantWorn(std::unordered_set<std::string>& a_keys, const std::vector<WornItem>& a_items)
		{
			for (const auto& w : a_items)
				for (const auto& t : w.textures)
					Want(a_keys, t.path);
		}

		void WantFacts(std::unordered_set<std::string>& a_keys, const std::vector<Fact>& a_facts)
		{
			for (const auto& f : a_facts)
				Want(a_keys, f.value);
		}

		// ---- a small copy of a texture, read through the game's own file system (loose files and archives alike) -------------
		constexpr std::uint32_t kMagic = 0x20534444;  // "DDS "
		constexpr std::uint32_t kFourCC(char a, char b, char c, char d)
		{
			return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) | (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8) |
			       (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16) | (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24);
		}

		std::uint32_t U32(const Blob& a_bytes, std::size_t a_at)
		{
			std::uint32_t v = 0;
			std::memcpy(&v, a_bytes.data() + a_at, 4);
			return v;
		}

		void SetU32(Blob& a_bytes, std::size_t a_at, std::uint32_t a_value) { std::memcpy(a_bytes.data() + a_at, &a_value, 4); }

		/** Bytes per 4x4 block for the block-compressed DXGI formats; 0 when it is not one. */
		std::uint32_t DxgiBlockBytes(std::uint32_t a_dxgi)
		{
			if ((a_dxgi >= 70 && a_dxgi <= 72) || (a_dxgi >= 79 && a_dxgi <= 81))
				return 8;
			if ((a_dxgi >= 73 && a_dxgi <= 78) || (a_dxgi >= 82 && a_dxgi <= 84) || (a_dxgi >= 94 && a_dxgi <= 99))
				return 16;
			return 0;
		}

		std::uint64_t LevelBytes(std::uint32_t a_w, std::uint32_t a_h, std::uint32_t a_blockBytes)
		{
			if (a_blockBytes == 0)
				return static_cast<std::uint64_t>(a_w) * a_h * 4;
			return static_cast<std::uint64_t>(std::max(1u, (a_w + 3) / 4)) * std::max(1u, (a_h + 3) / 4) * a_blockBytes;
		}

		/** Skips a_bytes of the stream by reading them (the archive stream's seek is not trusted). */
		bool Skip(RE::BSResourceNiBinaryStream& a_stream, std::uint64_t a_bytes)
		{
			if (a_bytes > 48u * 1024 * 1024)
				return false;
			std::vector<std::uint8_t> scratch(64 * 1024);
			while (a_bytes > 0) {
				const auto step = static_cast<std::size_t>(std::min<std::uint64_t>(a_bytes, scratch.size()));
				if (!a_stream.read(scratch.data(), step))
					return false;
				a_bytes -= step;
			}
			return true;
		}

		/** Why the last small read gave nothing: logged for the first few, so a different DDS layout can be told from a missing file. */
		thread_local std::string t_why;
		int                      g_failLogged = 0;
		bool                     g_spellingLogged = false;
		thread_local bool        t_pending = false;  ///< the last read failed only because the archive index is not built yet

		/**
		 * Reads the first mip level at most 128 on its long side (and the smaller ones after it) of a texture the game would load, and
		 * returns them as a complete DDS. Null when the file is missing or in a layout this does not handle (cube maps, arrays, other
		 * pixel formats): the card then shows a blank swatch.
		 */
		std::shared_ptr<const Blob> ReadSmall(const std::string& a_key)
		{
			// the game's file system is asked with the path as the engine itself spells it; which spelling it takes is found out once
			std::unique_ptr<RE::BSResourceNiBinaryStream> opened;
			for (const char* prefix : { "textures\\", "Data\\Textures\\", "" }) {
				const std::string name = prefix + a_key;
				auto              candidate = std::make_unique<RE::BSResourceNiBinaryStream>(name.c_str());
				if (static_cast<bool>(*candidate)) {
					opened = std::move(candidate);
					if (!g_spellingLogged) {
						g_spellingLogged = true;
						spdlog::info("card: the game's file system opened \"{}\" for a thumbnail", name);
					}
					break;
				}
			}
			if (!opened) {
				// not a loose file: the game's own stream does not serve texture archives, so the entry is read from the BA2 itself
				std::string why;
				bool        pending = false;
				auto        bytes = ReadArchiveTexture("textures\\" + a_key, 128, &why, &pending);
				if (bytes.empty()) {
					t_pending = pending;
					t_why = why;
					return nullptr;
				}
				return std::make_shared<const Blob>(std::move(bytes));
			}
			RE::BSResourceNiBinaryStream& stream = *opened;
			Blob head(128);
			if (!stream.read(head.data(), 128)) {
				t_why = "stream shorter than a DDS header";
				return nullptr;
			}
			if (U32(head, 0) != kMagic || U32(head, 4) != 124) {
				t_why = std::format("stream does not start with a DDS header (first bytes {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X})", head[0], head[1], head[2], head[3], head[4], head[5], head[6], head[7]);
				return nullptr;
			}
			const std::uint32_t height = U32(head, 12), width = U32(head, 16), mips = std::max(1u, U32(head, 28));
			const std::uint32_t pfFlags = U32(head, 80), fourCC = U32(head, 84), bits = U32(head, 88), caps2 = U32(head, 112);
			if (width == 0 || height == 0 || (caps2 & 0x200)) {  // empty, or a cube map
				t_why = "empty or a cube map";
				return nullptr;
			}
			std::uint32_t blockBytes = 0;
			bool          dx10 = false;
			if (pfFlags & 0x4) {  // a FourCC
				if (fourCC == kFourCC('D', 'X', 'T', '1') || fourCC == kFourCC('B', 'C', '4', 'U') || fourCC == kFourCC('A', 'T', 'I', '1'))
					blockBytes = 8;
				else if (fourCC == kFourCC('D', 'X', 'T', '3') || fourCC == kFourCC('D', 'X', 'T', '5') || fourCC == kFourCC('A', 'T', 'I', '2') || fourCC == kFourCC('B', 'C', '5', 'U'))
					blockBytes = 16;
				else if (fourCC == kFourCC('D', 'X', '1', '0'))
					dx10 = true;
				else {
					t_why = "a four-character pixel format this does not handle";
					return nullptr;
				}
			} else if (bits == 32 && (pfFlags & 0x41)) {  // uncompressed 32 bit: RGB with or without alpha
				blockBytes = 0;
			} else {
				t_why = "a pixel format this does not handle";
				return nullptr;
			}
			Blob ext;
			if (dx10) {
				ext.resize(20);
				if (!stream.read(ext.data(), 20)) {
					t_why = "stream ends inside the DX10 header";
					return nullptr;
				}
				blockBytes = DxgiBlockBytes(U32(ext, 0));
				const bool rgba8 = U32(ext, 0) == 28 || U32(ext, 0) == 29 || U32(ext, 0) == 87 || U32(ext, 0) == 91;
				if ((blockBytes == 0 && !rgba8) || U32(ext, 12) > 1 || U32(ext, 4) != 3 || (U32(ext, 8) & 4)) {  // not a plain 2D texture, or an array
					t_why = std::format("DXGI format {} or layout not handled", U32(ext, 0));
					return nullptr;
				}
			}
			// the first level that is small enough (or the last one there is)
			std::uint32_t level = 0;
			while (level + 1 < mips && std::max(width >> level, height >> level) > 128)
				++level;
			std::uint64_t skip = 0, keep = 0;
			for (std::uint32_t i = 0; i < mips; ++i) {
				const auto size = LevelBytes(std::max(1u, width >> i), std::max(1u, height >> i), blockBytes);
				if (i < level)
					skip += size;
				else
					keep += size;
			}
			if (keep > 4u * 1024 * 1024) {  // a big texture with no small mips: not worth reading for a thumbnail
				t_why = "a big texture with no small mip levels";
				return nullptr;
			}
			if (!Skip(stream, skip)) {
				t_why = "could not read past the large mip levels";
				return nullptr;
			}
			Blob data(static_cast<std::size_t>(keep));
			if (!stream.read(data.data(), static_cast<std::size_t>(keep))) {
				t_why = "stream ended before the small mip levels";
				return nullptr;
			}

			auto out = std::make_shared<Blob>();
			out->reserve(128 + ext.size() + data.size());
			Blob h = head;
			SetU32(h, 12, std::max(1u, height >> level));
			SetU32(h, 16, std::max(1u, width >> level));
			SetU32(h, 28, mips - level);
			SetU32(h, 20, static_cast<std::uint32_t>(LevelBytes(std::max(1u, width >> level), std::max(1u, height >> level), blockBytes)));
			out->insert(out->end(), h.begin(), h.end());
			out->insert(out->end(), ext.begin(), ext.end());
			out->insert(out->end(), data.begin(), data.end());
			return out;
		}

		std::shared_ptr<const Blob> SmallFor(const std::string& a_key)
		{
			{
				std::scoped_lock lock(g_cacheMutex);
				if (const auto it = g_cache.find(a_key); it != g_cache.end())
					return it->second;
			}
			std::shared_ptr<const Blob> blob;
			t_why = "";
			t_pending = false;
			try {
				blob = ReadSmall(a_key);
			} catch (...) {
				blob = nullptr;
				t_why = "an exception while reading";
			}
			if (!blob && !t_pending && g_failLogged < 16) {
				++g_failLogged;
				spdlog::info("card: no thumbnail copy of \"{}\": {}", a_key, t_why);
			}
			if (!blob && t_pending)
				return nullptr;  // not remembered: the archive index was still being built, the next pick tries again
			std::scoped_lock lock(g_cacheMutex);
			if (g_cache.size() > 400)
				g_cache.clear();
			g_cache[a_key] = blob;
			return blob;
		}
	}

	void Gather(RE::NiTexture* a_texture, std::string_view a_slotPath)
	{
		if (!a_texture)
			return;
		std::scoped_lock lock(g_mutex);
		if (g_gathered.size() > 4000)
			g_gathered.clear();
		// the slot's own path is the key the card asks by; the texture object's name is not always set
		if (!a_slotPath.empty())
			g_gathered[TextureKey(a_slotPath)] = RE::NiPointer<RE::NiTexture>(a_texture);
		if (a_texture->name.c_str() && a_texture->name.c_str()[0])
			g_gathered[TextureKey(a_texture->name.c_str())] = RE::NiPointer<RE::NiTexture>(a_texture);
	}

	HeldPtr TakeFor(const PickInfo& a_info)
	{
		std::unordered_set<std::string> keys;
		WantShapes(keys, a_info.shapes);
		WantWorn(keys, a_info.npc.worn);
		WantWorn(keys, a_info.npc.skinItem.parts);
		for (const auto& h : a_info.npc.headParts)
			for (const auto& p : h.textureSetPaths)
				Want(keys, p);
		for (const auto& l : a_info.terrain.layers)
			WantFacts(keys, l.textures);
		WantFacts(keys, a_info.water.liveTextures);
		WantFacts(keys, a_info.water.noiseTextures);
		for (const auto& c : a_info.sky.clouds)
			Want(keys, c.texture);
		WantFacts(keys, a_info.sky.objects);

		auto held = std::make_shared<Held>();
		int  fromEngine = 0, fromFile = 0;
		{
			std::scoped_lock lock(g_mutex);
			for (const auto& key : keys) {
				const auto it = g_gathered.find(key);
				if (it != g_gathered.end()) {
					held->textures.emplace(key, it->second);
					++fromEngine;
				}
			}
			g_gathered.clear();
		}
		// what the engine did not have bound while the pick was read (land, water and sky textures, head parts): read a small copy of the file
		for (const auto& key : keys) {
			if (held->textures.contains(key))
				continue;
			if (auto blob = SmallFor(key)) {
				held->files.emplace(key, std::move(blob));
				++fromFile;
			}
		}
		spdlog::info("card: thumbnails for {} textures: {} from the engine's bound textures, {} read from the files, {} blank", keys.size(), fromEngine, fromFile, static_cast<int>(keys.size()) - fromEngine - fromFile);
		return held;
	}

	void* ViewFor(const Held* a_held, std::string_view a_pathOrLoadedAs)
	{
		if (!a_held)
			return nullptr;
		const std::string key = TextureKey(a_pathOrLoadedAs);
		if (const auto it = a_held->textures.find(key); it != a_held->textures.end() && it->second) {
			auto* renderer = it->second->rendererTexture;
			if (renderer && renderer->srView) {
				const GpuInfo gpu = ReadGpuInfo(it->second.get());
				if (gpu.present && !gpu.cube && gpu.arraySize == 1)  // only plain 2D textures draw as a thumbnail
					return renderer->srView;
			}
		}
		auto* self = const_cast<Held*>(a_held);
		if (const auto made = self->made.find(key); made != self->made.end())
			return made->second.Get();
		const auto blob = a_held->files.find(key);
		if (blob == a_held->files.end() || !blob->second)
			return nullptr;
		auto* device = overlay::Device();
		if (!device)
			return nullptr;
		Microsoft::WRL::ComPtr<ID3D11Resource>           resource;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
		const HRESULT hr = DirectX::CreateDDSTextureFromMemory(device, blob->second->data(), blob->second->size(), resource.GetAddressOf(), view.GetAddressOf());
		if (FAILED(hr))
			view.Reset();
		self->made[key] = view;  // a failed one is remembered too (a blank swatch, no retry every frame)
		return view.Get();
	}
}
