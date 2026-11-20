// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "Output.h"

#include <format>

namespace ti
{
	namespace
	{
		std::string Hex8(std::uint32_t a_id) { return std::format("{:08X}", a_id); }

		std::string Join(const std::vector<std::string>& a_items, std::string_view a_sep)
		{
			std::string out;
			for (std::size_t i = 0; i < a_items.size(); ++i) {
				if (i)
					out += a_sep;
				out += a_items[i];
			}
			return out;
		}

		std::string ShortName(const ShapeInfo& a_shape)
		{
			return a_shape.name.empty() ? std::format("(unnamed #{})", a_shape.index) : a_shape.name;
		}

		std::string Number(float a_value)
		{
			if (!std::isfinite(a_value))
				return "null";
			return std::format("{}", a_value);
		}

		/** Tiny pretty-printing JSON writer: enough for this file, no dependency. */
		class Json
		{
		public:
			void BeginObject(std::string_view a_key = {}) { Open(a_key, '{'); }
			void EndObject() { Close('}'); }
			void BeginArray(std::string_view a_key = {}) { Open(a_key, '['); }
			void EndArray() { Close(']'); }
			void Str(std::string_view a_key, std::string_view a_value) { Item(a_key); out_ += '"' + JsonEscape(a_value) + '"'; }
			void Raw(std::string_view a_key, const std::string& a_value) { Item(a_key); out_ += a_value; }
			void Bool(std::string_view a_key, bool a_value) { Raw(a_key, a_value ? "true" : "false"); }
			void Num(std::string_view a_key, float a_value) { Raw(a_key, Number(a_value)); }
			void Int(std::string_view a_key, std::int64_t a_value) { Raw(a_key, std::to_string(a_value)); }
			void Vec(std::string_view a_key, Vec3 a_value) { Raw(a_key, std::format("[{}, {}, {}]", Number(a_value.x), Number(a_value.y), Number(a_value.z))); }
			void Floats(std::string_view a_key, const float* a_values, int a_count)
			{
				std::string s = "[";
				for (int i = 0; i < a_count; ++i)
					s += (i ? ", " : "") + Number(a_values[i]);
				Raw(a_key, s + "]");
			}
			void Strings(std::string_view a_key, const std::vector<std::string>& a_values)
			{
				std::string s = "[";
				for (std::size_t i = 0; i < a_values.size(); ++i)
					s += (i ? ", \"" : "\"") + JsonEscape(a_values[i]) + "\"";
				Raw(a_key, s + "]");
			}
			std::string Take() { return std::move(out_) + "\n"; }

		private:
			void Indent() { out_.append(static_cast<std::size_t>(depth_) * 2, ' '); }
			void Item(std::string_view a_key)
			{
				if (!first_.empty()) {
					if (!first_.back())
						out_ += ',';
					first_.back() = false;
					out_ += '\n';
					Indent();
				}
				if (!a_key.empty())
					out_ += '"' + JsonEscape(a_key) + "\": ";
			}
			void Open(std::string_view a_key, char a_bracket)
			{
				Item(a_key);
				out_ += a_bracket;
				first_.push_back(true);
				++depth_;
			}
			void Close(char a_bracket)
			{
				const bool empty = first_.back();
				first_.pop_back();
				--depth_;
				if (!empty) {
					out_ += '\n';
					Indent();
				}
				out_ += a_bracket;
			}

			std::string        out_;
			std::vector<bool>  first_;
			int                depth_ = 0;
		};

		std::string Pad(std::string_view a_text, std::size_t a_width)
		{
			std::string s(a_text);
			if (s.size() < a_width)
				s.append(a_width - s.size(), ' ');
			return s;
		}

		// ---- report pieces (JSON) -------------------------------------------------------------------------------------

		void WriteGpu(Json& j, std::string_view a_key, const GpuInfo& a_gpu)
		{
			j.BeginObject(a_key);
			j.Bool("present", a_gpu.present);
			if (a_gpu.present) {
				j.Int("width", a_gpu.width);
				j.Int("height", a_gpu.height);
				j.Int("mips", a_gpu.mips);
				j.Int("array_size", a_gpu.arraySize);
				j.Int("dxgi_format", a_gpu.dxgi);
				j.Str("format", a_gpu.format);
				j.Bool("cube", a_gpu.cube);
				j.Int("estimated_bytes", static_cast<std::int64_t>(a_gpu.bytes));
				j.Str("estimated_size", HumanBytes(a_gpu.bytes));
			} else {
				j.Str("note", a_gpu.note);
			}
			j.EndObject();
		}

		void WriteOrigin(Json& j, std::string_view a_key, const FileOrigin& a_origin)
		{
			j.BeginObject(a_key);
			j.Bool("looked_up", a_origin.looked);
			j.Str("source", a_origin.source);
			j.Str("archive", a_origin.archive);
			j.Str("mod", a_origin.mod);
			j.Str("real_path", a_origin.realPath);
			if (!a_origin.note.empty())
				j.Str("note", a_origin.note);
			j.EndObject();
		}

		void WriteRecord(Json& j, std::string_view a_key, const FormRecord& a_record)
		{
			j.BeginObject(a_key);
			j.Str("form_id", Hex8(a_record.formId));
			j.Str("type", a_record.type);
			j.Str("editor_id", a_record.editorId);
			j.Str("added_by", a_record.addedBy);
			j.Str("last_changed_by", a_record.lastChangedBy);
			j.Strings("source_files", a_record.chain);
			j.EndObject();
		}

		void WriteDiagnostics(Json& j, std::string_view a_key, const std::vector<Diagnostic>& a_items)
		{
			j.BeginArray(a_key);
			for (const auto& d : a_items) {
				j.BeginObject();
				j.Str("rule", d.rule);
				j.Str("text", d.text);
				j.EndObject();
			}
			j.EndArray();
		}

		void WriteIdentity(Json& j, const Identity& a_id)
		{
			j.BeginObject("identity");
			j.Str("kind", a_id.kind);
			j.Str("display_name", a_id.displayName);
			j.Str("base_name", a_id.baseName);
			WriteRecord(j, "ref", a_id.ref);
			WriteRecord(j, "base", a_id.base);
			j.Str("editor_id_source", a_id.editorIdSource);
			j.BeginObject("cell");
			j.Str("form_id", Hex8(a_id.cellFormId));
			j.Str("name", a_id.cellName);
			j.Str("editor_id", a_id.cellEditorId);
			j.Bool("interior", a_id.interior);
			j.Bool("persistent", a_id.persistent);
			if (a_id.hasCoords) {
				j.Int("x", a_id.cellX);
				j.Int("y", a_id.cellY);
			}
			j.EndObject();
			j.BeginObject("worldspace");
			j.Str("name", a_id.worldspaceName);
			j.Str("editor_id", a_id.worldspaceEditorId);
			j.EndObject();
			j.Vec("position", a_id.position);
			j.Vec("rotation_degrees", a_id.rotationDegrees);
			j.Num("scale", a_id.scale);
			WriteOrigin(j, "model_origin", a_id.modelOrigin);
			j.EndObject();
		}


		void WriteFacts(Json& j, std::string_view a_key, const std::vector<Fact>& a_facts)
		{
			j.BeginArray(a_key);
			for (const auto& f : a_facts) {
				j.BeginObject();
				j.Str("label", f.label);
				j.Str("value", f.value);
				j.EndObject();
			}
			j.EndArray();
		}

		void WriteWornItem(Json& j, const WornItem& w)
		{
			j.BeginObject();
			{
				std::string slots = "[";
				for (std::size_t i = 0; i < w.slots.size(); ++i)
					slots += (i ? ", " : "") + std::to_string(w.slots[i]);
				j.Raw("slots", slots + "]");
			}
			{
				std::vector<std::string> names;
				for (const int s : w.slots)
					names.push_back(SlotNameOf(w, s));
				j.Strings("slot_names", names);
			}
			j.Str("name", w.name);
			j.Str("item_form_id", Hex8(w.itemFormId));
			j.Str("item_plugin", w.itemPlugin);
			j.Str("addon_form_id", Hex8(w.addonFormId));
			j.Str("addon_plugin", w.addonPlugin);
			j.Str("model", w.model);
			j.Bool("from_live_3d", w.live3d);
			j.Strings("materials", w.materials);
			j.Strings("mods", w.mods);
			j.BeginArray("textures");
			for (const auto& t : w.textures) {
				j.BeginObject();
				j.Str("shape", t.shape);
				j.Str("slot", t.slotLabel);
				j.Str("path", t.path);
				WriteGpu(j, "gpu", t.gpu);
				j.EndObject();
			}
			j.EndArray();
			j.EndObject();
		}

		void WriteTerrain(Json& j, const TerrainSection& t)
		{
			j.BeginObject("terrain");
			j.Bool("present", t.present);
			if (!t.present) {
				j.EndObject();
				return;
			}
			j.BeginObject("cell");
			j.Str("form_id", Hex8(t.cellFormId));
			j.Str("name", t.cellName);
			j.Str("editor_id", t.cellEditorId);
			j.Int("x", t.cellX);
			j.Int("y", t.cellY);
			j.EndObject();
			j.Str("worldspace", t.worldspace);
			j.Int("quadrant", t.quadrant);
			j.Str("quadrant_name", t.quadrantName);
			j.BeginObject("nearest_vertex");
			j.Int("x", t.vertexX);
			j.Int("y", t.vertexY);
			j.Vec("position", t.vertexPosition);
			j.EndObject();
			j.Vec("hit_point", t.point);
			j.Num("height_at_hit", t.heightAtHit);
			j.Str("blend_method", t.blendMethod);
			j.Str("height_check", t.heightCheck);
			j.Str("footstep_material", t.footstepMaterial);
			if (!t.note.empty())
				j.Str("note", t.note);
			j.BeginArray("layers");
			for (const auto& l : t.layers) {
				j.BeginObject();
				j.Str("role", l.role);
				j.Str("form_id", Hex8(l.formId));
				j.Str("editor_id", l.editorId);
				j.Str("name", l.name);
				j.Str("added_by", l.addedBy);
				j.Str("last_changed_by", l.lastChangedBy);
				j.Strings("source_files", l.chain);
				j.Int("raw_percent", l.rawPercent);
				j.Num("percent", l.percent);
				j.Str("footstep_material", l.footstepMaterial);
				WriteFacts(j, "textures", l.textures);
				j.Strings("grasses", l.grasses);
				j.EndObject();
			}
			j.EndArray();
			j.EndObject();
		}

		void WriteWater(Json& j, const WaterSection& w)
		{
			j.BeginObject("water");
			j.Bool("present", w.present);
			if (!w.present) {
				j.EndObject();
				return;
			}
			WriteRecord(j, "type", w.type);
			j.Str("type_name", w.typeName);
			j.Str("source", w.source);
			j.Num("height", w.height);
			if (w.hasCellHeight)
				j.Num("cell_water_height", w.cellHeight);
			j.Str("cell_vs_placed", w.cellVersusPlane);
			if (w.planeRefFormId) {
				j.BeginObject("placed_plane");
				j.Str("ref_form_id", Hex8(w.planeRefFormId));
				j.Str("base_form_id", Hex8(w.planeBaseFormId));
				j.Str("name", w.planeName);
				j.EndObject();
			}
			j.BeginObject("cell");
			j.Str("form_id", Hex8(w.cellFormId));
			j.Str("name", w.cellName);
			j.EndObject();
			j.Vec("hit_point", w.point);
			WriteFacts(j, "noise_textures", w.noiseTextures);
			WriteFacts(j, "live_textures", w.liveTextures);
			j.Str("shallow_color", w.shallowColor);
			j.Str("deep_color", w.deepColor);
			j.Str("reflection_color", w.reflectionColor);
			WriteFacts(j, "values", w.values);
			if (!w.note.empty())
				j.Str("note", w.note);
			j.EndObject();
		}

		void WriteSky(Json& j, const SkySection& s)
		{
			j.BeginObject("sky");
			j.Bool("present", s.present);
			if (!s.present) {
				j.EndObject();
				return;
			}
			j.Str("mode", s.mode);
			j.Num("hour", s.hour);
			j.Str("time", s.hourText);
			WriteRecord(j, "current_weather", s.currentWeather);
			WriteRecord(j, "previous_weather", s.previousWeather);
			j.Num("transition_percent", s.transitionPercent);
			j.Str("weather_class", s.weatherClass);
			WriteRecord(j, "climate", s.climate);
			j.BeginArray("cloud_layers");
			for (const auto& c : s.clouds) {
				j.BeginObject();
				j.Str("weather", c.weather);
				j.Int("index", c.index);
				j.Str("texture", c.texture);
				j.Int("speed_x", c.speedX);
				j.Int("speed_y", c.speedY);
				if (c.alpha >= 0.0f)
					j.Num("live_alpha", c.alpha);
				j.EndObject();
			}
			j.EndArray();
			WriteFacts(j, "sky_objects", s.objects);
			j.Str("image_space", s.imageSpace);
			j.Str("image_space_source", s.imageSpaceSource);
			j.Str("lighting_template", s.lightingTemplate);
			j.Str("cell", s.cellName);
			if (!s.note.empty())
				j.Str("note", s.note);
			j.EndObject();
		}

		void WriteHits(Json& j, const PickInfo& a_info)
		{
			j.Int("hit_index", a_info.hitIndex);
			j.BeginArray("hits");
			for (std::size_t i = 0; i < a_info.hits.size(); ++i) {
				const auto& h = a_info.hits[i];
				j.BeginObject();
				j.Int("index", static_cast<std::int64_t>(i));
				j.Str("kind", h.kind);
				j.Str("label", h.label);
				j.Num("distance", h.distance);
				j.Bool("exact", h.exact);
				j.Vec("point", h.point);
				if (h.refFormId)
					j.Str("ref_form_id", Hex8(h.refFormId));
				if (h.shapeIndex >= 0)
					j.Int("shape_index", h.shapeIndex);
				if (!h.partName.empty())
					j.Str("part", h.partName);
				j.EndObject();
			}
			j.EndArray();
		}

		void WriteNpc(Json& j, const NpcSection& a_npc)
		{
			j.BeginObject("npc");
			j.Bool("present", a_npc.present);
			if (!a_npc.present) {
				j.EndObject();
				return;
			}
			j.Str("sex", a_npc.sex);
			j.Str("race", a_npc.race);
			j.Str("skin", a_npc.skin);
			j.Str("skin_tone_color", a_npc.skinToneColor);
			j.Str("skeleton", a_npc.skeleton);
			j.Str("face_npc_form_id", Hex8(a_npc.faceNpcFormId));

			j.BeginObject("facegen");
			j.Bool("looked_up", a_npc.faceGen.looked);
			j.Str("verdict", a_npc.faceGen.verdict);
			j.Str("evidence", a_npc.faceGen.evidence);
			j.Str("mesh_path", a_npc.faceGen.meshPath);
			WriteOrigin(j, "mesh_origin", a_npc.faceGen.meshOrigin);
			j.Str("tint_path", a_npc.faceGen.tintPath);
			WriteOrigin(j, "tint_origin", a_npc.faceGen.tintOrigin);
			j.Str("live_tint_texture", a_npc.faceGen.liveTintTexture);
			j.Int("diffuse_render_target_index", a_npc.faceGen.renderTargetIndex);
			j.EndObject();

			j.BeginObject("skin_item");
			j.Bool("present", a_npc.skinItem.present);
			if (a_npc.skinItem.present) {
				j.Str("name", a_npc.skinItem.name);
				j.Str("form_id", Hex8(a_npc.skinItem.formId));
				j.Str("plugin", a_npc.skinItem.plugin);
				j.Str("source", a_npc.skinItem.source);
				j.BeginArray("parts");
				for (const auto& w : a_npc.skinItem.parts)
					WriteWornItem(j, w);
				j.EndArray();
			}
			j.EndObject();

			j.BeginArray("worn");
			for (const auto& w : a_npc.worn)
				WriteWornItem(j, w);
			j.EndArray();

			j.BeginArray("head_parts");
			for (const auto& h : a_npc.headParts) {
				j.BeginObject();
				j.Str("type", h.type);
				j.Str("form_id", Hex8(h.formId));
				j.Str("editor_id", h.editorId);
				j.Str("name", h.name);
				j.Str("model", h.model);
				j.Str("texture_set_form_id", Hex8(h.textureSetFormId));
				j.Strings("texture_set_paths", h.textureSetPaths);
				j.EndObject();
			}
			j.EndArray();
			j.EndObject();
		}

		// ---- report pieces (console) ----------------------------------------------------------------------------------

		std::string OriginText(const FileOrigin& a_origin)
		{
			if (!a_origin.looked)
				return {};
			std::string out;
			if (a_origin.source == "loose")
				out = "loose";
			else if (a_origin.source == "archive")
				out = "archive " + a_origin.archive;
			else
				out = a_origin.source.empty() ? "origin unknown" : a_origin.source;
			if (!a_origin.mod.empty())
				out += " from mod " + a_origin.mod;
			return out;
		}

		std::string GpuText(const TextureSlot& a_slot)
		{
			if (!a_slot.bound)
				return "NOT LOADED";
			if (!a_slot.gpu.present)
				return "no GPU info (" + a_slot.gpu.note + ")";
			const auto& g = a_slot.gpu;
			return std::format("{}x{} {} {} mip{} {}{}", g.width, g.height, g.format, g.mips, g.mips == 1 ? "" : "s", HumanBytes(g.bytes), g.cube ? " cube" : "");
		}

		std::string PluginChain(const FormRecord& a_record)
		{
			if (a_record.chain.empty())
				return a_record.addedBy.empty() ? "(unknown)" : a_record.addedBy;
			std::string chain = Join(a_record.chain, " > ");
			return chain;
		}

		std::string Shortened(const std::string& a_text, std::size_t a_max)
		{
			return a_text.size() <= a_max ? a_text : a_text.substr(0, a_max - 3) + "...";
		}

		using AddLine = std::function<void(std::string)>;

		std::string LayerLine(const TerrainLayer& l)
		{
			std::string name = l.editorId.empty() ? Hex8(l.formId) : l.editorId;
			if (!l.name.empty())
				name = l.name + " (" + name + ")";
			const std::string percent = l.rawPercent < 0 ? "100%" : std::format("{:.0f}%", l.percent);
			const std::string plugin = l.lastChangedBy == l.addedBy || l.lastChangedBy.empty() ? l.addedBy : l.addedBy + " > " + l.lastChangedBy;
			return std::format("  {}  {}  {}  [{}]", Pad(l.role, 8), Pad(percent, 5), name, plugin);
		}

		std::string FactsLine(const std::vector<Fact>& a_facts, std::string_view a_sep)
		{
			std::string out;
			for (const auto& f : a_facts)
				out += std::format("{}{}: {}", out.empty() ? "" : std::string(a_sep), f.label, f.value);
			return out;
		}

		void ConsoleTerrain(const PickInfo& a_info, const AddLine& add)
		{
			const auto& t = a_info.terrain;
			add(std::format("#{}  TERRAIN  {}{} [{}, {}] in {}   quadrant {}   nearest vertex ({}, {})", a_info.counter, t.cellName.empty() ? "(unnamed cell)" : t.cellName,
				t.cellEditorId.empty() ? std::string() : " (" + t.cellEditorId + ")", t.cellX, t.cellY, t.worldspace.empty() ? "?" : t.worldspace, t.quadrantName, t.vertexX,
				t.vertexY));
			const float distance = a_info.hitIndex >= 0 && a_info.hitIndex < static_cast<int>(a_info.hits.size()) ? a_info.hits[static_cast<std::size_t>(a_info.hitIndex)].distance : 0.0f;
			add(std::format("hit    {:.0f}, {:.0f}, {:.0f}   {:.1f} units from the camera   ground height {:.1f}   ({})", t.point.x, t.point.y, t.point.z, distance, t.heightAtHit,
				t.heightCheck));
			add(std::format("blend  {}", t.blendMethod));
			for (const auto& l : t.layers) {
				add(LayerLine(l));
				const std::string textures = FactsLine(l.textures, "   ");
				if (!textures.empty())
					add("            " + textures);
				add(std::format("            grass: {}   footstep material: {}", l.grasses.empty() ? std::string("none") : Join(l.grasses, ", "),
					l.footstepMaterial.empty() ? std::string("(none)") : l.footstepMaterial));
			}
			add(std::format("footstep material here: {}", t.footstepMaterial.empty() ? std::string("(unknown)") : t.footstepMaterial));
			if (!t.note.empty())
				add(t.note);
		}

		void ConsoleWater(const PickInfo& a_info, const AddLine& add)
		{
			const auto& w = a_info.water;
			add(std::format("#{}  WATER  type {} ({})   surface height {:.1f}   in {}", a_info.counter, w.typeName.empty() ? std::string("(unnamed)") : w.typeName,
				w.type.editorId.empty() ? Hex8(w.type.formId) : w.type.editorId + ", " + Hex8(w.type.formId), w.height, w.cellName.empty() ? std::string("(unnamed cell)") : w.cellName));
			add(std::format("type   {}   added by {}, last changed by {}   (chain: {})", w.source, w.type.addedBy.empty() ? "?" : w.type.addedBy,
				w.type.lastChangedBy.empty() ? "?" : w.type.lastChangedBy, PluginChain(w.type)));
			add(std::format("plane  {}", w.cellVersusPlane));
			const std::string noise = FactsLine(w.noiseTextures, "   ");
			add("noise  " + (noise.empty() ? std::string("(none)") : noise));
			if (!w.liveTextures.empty())
				add("live   " + FactsLine(w.liveTextures, "   "));
			add(std::format("color  shallow {}   deep {}   reflection {}", w.shallowColor, w.deepColor, w.reflectionColor));
			const std::string values = FactsLine(w.values, "  |  ");
			if (!values.empty())
				add("values " + values);
			if (!w.note.empty())
				add(w.note);
		}

		std::string WeatherText(const FormRecord& r)
		{
			if (r.formId == 0)
				return "(none)";
			return std::format("{} ({}) by {}", r.editorId.empty() ? std::string("editor ID not loaded") : r.editorId, Hex8(r.formId), r.lastChangedBy.empty() ? "?" : r.lastChangedBy);
		}

		void ConsoleSky(const PickInfo& a_info, const AddLine& add)
		{
			const auto& s = a_info.sky;
			add(std::format("#{}  SKY  {}   time {}   cell {}", a_info.counter, s.mode, s.hourText.empty() ? std::string("?") : s.hourText,
				s.cellName.empty() ? std::string("(none)") : s.cellName));
			add(std::format("weather  now {}{}", WeatherText(s.currentWeather), s.weatherClass.empty() ? std::string() : "   [" + s.weatherClass + "]"));
			add(std::format("         before {}   transition {}", WeatherText(s.previousWeather),
				s.transitionPercent < 0.0f ? std::string("unknown") : std::format("{:.0f}% of the way to the current weather", s.transitionPercent)));
			add(std::format("climate  {}", WeatherText(s.climate)));
			if (s.clouds.empty())
				add("clouds   (no cloud layers in use)");
			for (const auto& c : s.clouds)
				add(std::format("clouds   {} layer {}  {}  speed {},{}{}", c.weather, c.index, c.texture.empty() ? std::string("(no texture)") : c.texture, c.speedX, c.speedY,
					c.alpha >= 0.0f ? std::format("  alpha {:.2f}", c.alpha) : std::string()));
			for (const auto& f : s.objects)
				add(std::format("{}  {}", Pad(f.label, 9), f.value));
			add(std::format("image space  {}   ({})", s.imageSpace.empty() ? std::string("(none)") : s.imageSpace, s.imageSpaceSource));
			if (!s.lightingTemplate.empty())
				add("lighting template  " + s.lightingTemplate);
			if (!s.note.empty())
				add(s.note);
		}

		void ConsoleHits(const PickInfo& a_info, const AddLine& add)
		{
			if (a_info.hits.size() < 2)
				return;
			if (a_info.hitIndex < 0)
				add(std::format("hits   {} along the ray, nearest first; none of them is this answer; `lookingat next` steps through them:", a_info.hits.size()));
			else
				add(std::format("hits   {} along the ray, nearest first; this report is #{}; `lookingat next` steps to the next:", a_info.hits.size(), a_info.hitIndex + 1));
			constexpr std::size_t kMaxShown = 12;
			for (std::size_t i = 0; i < a_info.hits.size() && i < kMaxShown; ++i) {
				const auto& h = a_info.hits[i];
				add(std::format("  {} {}  {:7.1f}  {:7} {}{}", static_cast<int>(i) == a_info.hitIndex ? ">" : " ", i + 1, h.distance, h.kind, h.label, h.exact ? "" : "  (by bounds)"));
			}
			if (a_info.hits.size() > kMaxShown)
				add(std::format("  ... {} more in last-pick.json", a_info.hits.size() - kMaxShown));
		}
	}

	std::string JsonEscape(std::string_view a_text)
	{
		std::string out;
		out.reserve(a_text.size() + 8);
		for (const unsigned char c : a_text) {
			switch (c) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if (c < 0x20)
					out += std::format("\\u{:04x}", static_cast<unsigned>(c));
				else
					out += static_cast<char>(c);
			}
		}
		return out;
	}

	std::vector<std::string> FormatConsole(const PickInfo& a_info, unsigned a_maxListedParts, bool a_showRayLine)
	{
		std::vector<std::string> lines;
		const auto               add = [&](std::string a_line) { lines.push_back("[What Am I Looking At] " + std::move(a_line)); };
		// How the ray was built. Always shown when the engine's own projection disagreed with it.
		const auto rayLine = [&]() {
			const bool verified = a_info.rayCheck.rfind("verified", 0) == 0;
			if (!a_showRayLine && verified)
				return;
			if (a_info.cursor.used)
				add(std::format("ray  cursor {:.0f},{:.0f} = {:.3f},{:.3f} of {:.0f}x{:.0f}  [{}]", a_info.cursor.x, a_info.cursor.y, a_info.cursor.fracX,
					a_info.cursor.fracY, a_info.cursor.areaW, a_info.cursor.areaH, a_info.rayCheck));
			else
				add(std::format("ray  screen centre  [{}]", a_info.rayCheck));
		};

		if (!a_info.valid) {
			add(std::format("#{}  {}", a_info.counter, a_info.message.empty() ? "nothing picked" : a_info.message));
			return lines;
		}

		if ((a_info.identity.kind.empty() || a_info.identity.kind == "object") && a_info.shapes.empty() && !a_info.message.empty() && a_info.object.refFormId == 0) {
			// asked for land or water and there is none under the crosshair: say that, not an empty object
			add(std::format("#{}  {}", a_info.counter, a_info.message));
			ConsoleHits(a_info, add);
			rayLine();
			return lines;
		}

		if (a_info.identity.kind == "terrain" || a_info.identity.kind == "water" || a_info.identity.kind == "sky") {
			const AddLine addLine = add;
			if (a_info.identity.kind == "terrain")
				ConsoleTerrain(a_info, addLine);
			else if (a_info.identity.kind == "water")
				ConsoleWater(a_info, addLine);
			else
				ConsoleSky(a_info, addLine);
			ConsoleHits(a_info, addLine);
			rayLine();
			return lines;
		}

		const auto& o = a_info.object;
		add(std::format("#{}  \"{}\"  ref {}  base {} ({})  editor ID {}", a_info.counter, o.name, Hex8(o.refFormId), Hex8(o.baseFormId),
			o.baseType.empty() ? "?" : o.baseType, o.baseEditorId.empty() ? "(not loaded)" : o.baseEditorId));
		{
			const std::string origin = OriginText(a_info.identity.modelOrigin);
			add(std::format("model  {}{}", o.model.empty() ? "(none)" : o.model, origin.empty() ? "" : "   [" + origin + "]"));
		}
		const auto& id = a_info.identity;
		if (id.ref.formId != 0) {
			add(std::format("kind   {}   base added by {}, last changed by {}   (chain: {})", id.kind, id.base.addedBy.empty() ? "?" : id.base.addedBy,
				id.base.lastChangedBy.empty() ? "?" : id.base.lastChangedBy, PluginChain(id.base)));
			add(std::format("ref    added by {}, last changed by {}   (chain: {})", id.ref.addedBy.empty() ? "?" : id.ref.addedBy,
				id.ref.lastChangedBy.empty() ? "?" : id.ref.lastChangedBy, PluginChain(id.ref)));
			std::string where = id.interior ? "interior " : "exterior ";
			where += id.cellName.empty() ? "(unnamed cell)" : id.cellName;
			if (!id.cellEditorId.empty())
				where += " (" + id.cellEditorId + ")";
			if (id.hasCoords)
				where += std::format(" [{}, {}]", id.cellX, id.cellY);
			if (id.persistent)
				where += " (persistent)";
			if (!id.worldspaceEditorId.empty() || !id.worldspaceName.empty())
				where += " in " + (id.worldspaceEditorId.empty() ? id.worldspaceName : id.worldspaceEditorId);
			add(std::format("where  {}   pos {:.0f}, {:.0f}, {:.0f}   scale {:.2f}", where, id.position.x, id.position.y, id.position.z, id.scale));
			if (!a_info.facts.empty()) {
				std::string facts;
				for (std::size_t i = 0; i < a_info.facts.size(); ++i)
					facts += std::format("{}{}: {}", i ? "  |  " : "", a_info.facts[i].label, a_info.facts[i].value);
				add("facts  " + facts);
			}
			if (a_info.npc.present) {
				const auto& n = a_info.npc;
				std::string worn;
				for (const auto& w : n.worn) {
					std::string slots;
					for (std::size_t i = 0; i < w.slots.size(); ++i)
						slots += (i ? "," : "") + std::to_string(w.slots[i]);
					worn += std::format("{}{} [{}]", worn.empty() ? "" : "; ", w.name, slots);
				}
				if (n.skinItem.present) {
					std::string parts;
					for (const auto& w : n.skinItem.parts) {
						std::string slots;
						for (std::size_t i = 0; i < w.slots.size(); ++i)
							slots += (i ? "," : "") + std::to_string(w.slots[i]);
						parts += std::format("{}[{}] addon {}{}{}", parts.empty() ? "" : "; ", slots, w.addonFormId ? Hex8(w.addonFormId) : std::string("(none)"),
							w.model.empty() ? std::string() : " " + w.model, w.live3d ? "" : " (no live 3D)");
					}
					add(std::format("skin   {} ({}), {}:   {}", n.skinItem.name.empty() ? std::string("(unnamed)") : n.skinItem.name, Hex8(n.skinItem.formId), n.skinItem.source, parts.empty() ? std::string("(no addons found)") : parts));
				}
				add(std::format("worn   {}", worn.empty() ? "(nothing)" : worn));
				std::string heads;
				for (const auto& h : n.headParts)
					heads += std::format("{}{} {}", heads.empty() ? "" : "; ", h.type, h.editorId.empty() ? Hex8(h.formId) : h.editorId);
				add(std::format("head   {}", heads.empty() ? "(none)" : heads));
				add(std::format("face   FaceGen: {}   skin {}   skin tone {}   skeleton {}", n.faceGen.verdict.empty() ? "unknown" : n.faceGen.verdict, n.skin,
					n.skinToneColor, n.skeleton));
			}
		}

		if (a_info.clicked < 0) {
			add(a_info.message.empty() ? "no part under the cursor" : a_info.message);
			add(std::format("{} part(s) in this object; full list in last-pick.json", a_info.shapes.size()));
			rayLine();
			return lines;
		}

		const auto& s = a_info.shapes[static_cast<std::size_t>(a_info.clicked)];
		if (a_info.method == "nearest_to_center") {
			std::string note;
			for (const auto& n : a_info.notes)
				if (n.find("units off") != std::string::npos)
					note = "  (" + n + ")";
			add(std::format("CLICKED PART  \"{}\"  (part {} of {})  NEAREST TO THE SCREEN CENTER: no exact hit on this object{}", ShortName(s), s.index + 1,
				a_info.shapes.size(), note));
		} else if (a_info.method == "exact" || (a_info.method == "step_through" && s.exactHit)) {
			add(std::format("CLICKED PART  \"{}\"  (part {} of {})  exact: triangle {}, {:.1f} units from the camera", ShortName(s), s.index + 1,
				a_info.shapes.size(), s.hitTriangle, s.exactDistance));
		} else {
			std::string why;
			for (const auto& other : a_info.shapes)
				if (other.boundHit && !other.exactTested && !other.exactSkipReason.empty() && other.exactSkipReason != "not reached by the ray") {
					why = " (" + other.exactSkipReason + ")";
					break;
				}
			add(std::format("CLICKED PART  \"{}\"  (part {} of {})  APPROXIMATE: ranked by bounds{}", ShortName(s), s.index + 1, a_info.shapes.size(), why));
			if (!a_info.runnersUp.empty()) {
				std::vector<std::string> names;
				for (const int i : a_info.runnersUp)
					names.push_back(ShortName(a_info.shapes[static_cast<std::size_t>(i)]));
				add("also under the cursor: " + Join(names, ", "));
			}
		}
		add(std::format("  shader  {} / {}   TruePBR: {}", s.shaderKind, s.shaderType.empty() ? "?" : s.shaderType, s.pbr));
		if (!s.flags.empty())
			add("  flags   " + Join(s.flags, ", "));
		if (!s.materialPath.empty()) {
			const std::string origin = OriginText(s.materialOrigin);
			add(std::format("  material {}{}", s.materialPath, origin.empty() ? "" : "   [" + origin + "]"));
			if (!s.rootMaterial.empty())
				add("  root material " + s.rootMaterial);
		}
		add(std::format("  mesh    {} verts, {} tris   vertex data: {}{}", s.vertices, s.triangles, s.vertexFlags.empty() ? "?" : Join(s.vertexFlags, ", "),
			s.skinned ? "   SKINNED" : ""));
		if (s.textures.empty())
			add("  (no textures on this part's shader)");
		if (s.alphaMode != "none" || s.hasMaterial) {
			std::string text = "alpha " + s.alphaMode;
			if (s.alphaThreshold >= 0)
				text += std::format(" (threshold {}, {})", s.alphaThreshold, s.alphaTestFunction);
			if (s.hasMaterial) {
				text += std::format("   specular {} x{:.2f} gloss {:.1f}", HexColor(static_cast<int>(s.specularColor[0] * 255.0f + 0.5f),
												   static_cast<int>(s.specularColor[1] * 255.0f + 0.5f), static_cast<int>(s.specularColor[2] * 255.0f + 0.5f)),
					s.specularStrength, s.glossiness);
				if (s.hasEmissive)
					text += std::format("   emissive {} x{:.2f}", HexColor(static_cast<int>(s.emissiveColor[0] * 255.0f + 0.5f), static_cast<int>(s.emissiveColor[1] * 255.0f + 0.5f),
													static_cast<int>(s.emissiveColor[2] * 255.0f + 0.5f)),
						s.emissiveMultiple);
			}
			add("  " + text);
		}
		if (s.uvScale[0] != 1.0f || s.uvScale[1] != 1.0f || s.uvOffset[0] != 0.0f || s.uvOffset[1] != 0.0f)
			add(std::format("  uv      offset {:.3f}, {:.3f}   scale {:.3f}, {:.3f}", s.uvOffset[0], s.uvOffset[1], s.uvScale[0], s.uvScale[1]));
		if (!s.bodyArea.empty())
			add("  body    " + s.bodyArea);
		for (const auto& t : s.textures) {
			const std::string slot = t.slot >= 0 ? std::format("{} ", t.slot) : "";
			std::string       line = std::format("  {}{}{}", slot, Pad(t.label, 26), t.path) + (t.loadedAs.empty() ? "" : "   [loaded as " + t.loadedAs + "]");
			if (t.gpu.present || t.bound || t.origin.looked || (a_info.identity.ref.formId != 0 && !t.path.empty())) {
				line += "   " + GpuText(t);
				const std::string origin = OriginText(t.origin);
				if (!origin.empty())
					line += "   [" + origin + "]";
			}
			add(line);
		}
		for (const auto& d : s.diagnostics)
			add("  WARNING " + Shortened(d.text, 300));

		std::size_t others = 0;
		for (const auto& other : a_info.shapes)
			if (other.index != s.index)
				++others;
		if (others) {
			add(std::format("other parts ({}):", others));
			unsigned listed = 0;
			for (const auto& other : a_info.shapes) {
				if (other.index == s.index)
					continue;
				if (listed >= a_maxListedParts) {
					add(std::format("  ... {} more in last-pick.json", others - listed));
					break;
				}
				const auto* d = other.Diffuse();
				add(std::format("  {}  {}", Pad(ShortName(other), 28), d ? d->path : std::string("(no texture)")));
				++listed;
			}
		}
		{
			std::size_t elsewhere = 0;
			for (const auto& other : a_info.shapes)
				if (other.index != s.index)
					elsewhere += other.diagnostics.size();
			if (elsewhere)
				add(std::format("{} warning(s) on other parts; see last-pick.json", elsewhere));
		}
		ConsoleHits(a_info, add);
		rayLine();
		return lines;
	}

	std::string ToJson(const PickInfo& a_info)
	{
		Json j;
		j.BeginObject();
		j.Int("schema", 1);
		j.Int("report_version", kReportVersion);
		j.Str("plugin", a_info.plugin);

		j.BeginObject("pick");
		j.Int("counter", static_cast<std::int64_t>(a_info.counter));
		j.Str("timestamp", a_info.timestamp);
		j.Int("unix_ms", a_info.unixMs);
		j.Str("trigger", a_info.trigger);
		j.Bool("valid", a_info.valid);
		j.Str("message", a_info.message);
		j.EndObject();

		j.BeginObject("object");
		j.Str("name", a_info.object.name);
		j.Str("ref_form_id", Hex8(a_info.object.refFormId));
		j.Str("base_form_id", Hex8(a_info.object.baseFormId));
		j.Str("base_type", a_info.object.baseType);
		j.Str("base_editor_id", a_info.object.baseEditorId);
		j.Str("ref_editor_id", a_info.object.refEditorId);
		j.Str("model", a_info.object.model);
		j.Str("root_node", a_info.object.rootNode);
		j.EndObject();

		WriteIdentity(j, a_info.identity);
		j.BeginArray("facts");
		for (const auto& f : a_info.facts) {
			j.BeginObject();
			j.Str("label", f.label);
			j.Str("value", f.value);
			j.EndObject();
		}
		j.EndArray();
		WriteNpc(j, a_info.npc);
		WriteTerrain(j, a_info.terrain);
		WriteWater(j, a_info.water);
		WriteSky(j, a_info.sky);
		WriteHits(j, a_info);
		WriteDiagnostics(j, "diagnostics", a_info.diagnostics);

		j.Str("method", a_info.method);
		j.Int("clicked_shape", a_info.clicked);
		{
			std::string s = "[";
			for (std::size_t i = 0; i < a_info.runnersUp.size(); ++i)
				s += (i ? ", " : "") + std::to_string(a_info.runnersUp[i]);
			j.Raw("runners_up", s + "]");
		}

		if (a_info.hasRay) {
			j.BeginObject("ray");
			j.Str("space", "world");
			j.Vec("origin", a_info.ray.origin);
			j.Vec("direction", a_info.ray.dir);
			j.Str("check", a_info.rayCheck);
			if (a_info.cursor.used) {
				j.BeginObject("cursor");
				j.Num("x", a_info.cursor.x);
				j.Num("y", a_info.cursor.y);
				j.Num("area_width", a_info.cursor.areaW);
				j.Num("area_height", a_info.cursor.areaH);
				j.Num("fraction_x", a_info.cursor.fracX);
				j.Num("fraction_y", a_info.cursor.fracY);
				j.EndObject();
			}
			j.EndObject();
		}

		j.BeginArray("shapes");
		for (const auto& s : a_info.shapes) {
			j.BeginObject();
			j.Int("index", s.index);
			j.Str("name", s.name);
			j.Str("node_type", s.nodeType);
			j.Bool("clicked", s.clicked);
			j.Int("rank", s.rank);

			j.BeginObject("shader");
			j.Str("kind", s.shaderKind);
			j.Str("type", s.shaderType);
			j.Int("feature_id", s.featureId);
			j.Str("true_pbr", s.pbr);
			j.Bool("skinned", s.skinned);
			j.Strings("flags", s.flags);
			j.Strings("vertex_flags", s.vertexFlags);
			j.Bool("height_texture_bound", s.heightBound);
			j.EndObject();

			if (!s.materialPath.empty() || !s.materialSource.empty()) {
				j.BeginObject("material_file");
				j.Str("path", s.materialPath);
				j.Str("found_in", s.materialSource);
				if (!s.rootMaterial.empty())
					j.Str("root_material", s.rootMaterial);
				if (s.materialOrigin.looked)
					WriteOrigin(j, "origin", s.materialOrigin);
				j.EndObject();
			}

			j.BeginObject("alpha");
			j.Str("mode", s.alphaMode);
			if (s.alphaThreshold >= 0) {
				j.Int("test_threshold", s.alphaThreshold);
				j.Str("test_function", s.alphaTestFunction);
			}
			j.EndObject();

			if (s.hasMaterial) {
				j.BeginObject("material");
				j.Floats("specular_color", s.specularColor, 3);
				j.Num("specular_strength", s.specularStrength);
				j.Num("glossiness", s.glossiness);
				j.Num("material_alpha", s.materialAlpha);
				if (s.hasEmissive) {
					j.Floats("emissive_color", s.emissiveColor, 3);
					j.Num("emissive_multiple", s.emissiveMultiple);
				}
				j.EndObject();
			}
			j.BeginObject("uv");
			j.Floats("offset", s.uvOffset, 2);
			j.Floats("scale", s.uvScale, 2);
			j.EndObject();
			if (!s.bodyArea.empty()) {
				j.Str("body_area", s.bodyArea);
				std::string slots = "[";
				for (std::size_t i = 0; i < s.partitionSlots.size(); ++i)
					slots += (i ? ", " : "") + std::to_string(s.partitionSlots[i]);
				j.Raw("partition_slots", slots + "]");
			}
			WriteDiagnostics(j, "diagnostics", s.diagnostics);

			j.BeginArray("textures");
			for (const auto& t : s.textures) {
				j.BeginObject();
				j.Int("slot", t.slot);
				j.Str("label", t.label);
				j.Str("path", t.path);
				if (!t.loadedAs.empty())
					j.Str("loaded_as", t.loadedAs);
				j.Bool("bound", t.bound);
				WriteGpu(j, "gpu", t.gpu);
				if (t.origin.looked)
					WriteOrigin(j, "origin", t.origin);
				j.EndObject();
			}
			j.EndArray();

			j.Int("vertices", s.vertices);
			j.Int("triangles", s.triangles);

			j.BeginObject("world_transform");
			j.Vec("translate", s.translate);
			j.Floats("rotate_row_major", s.rotate, 9);
			j.Num("scale", s.scale);
			j.EndObject();

			j.BeginObject("world_bound");
			j.Vec("center", s.boundCentre);
			j.Num("radius", s.boundRadius);
			j.EndObject();

			j.BeginObject("pick");
			j.Bool("ray_touches_bound", s.boundHit);
			if (s.boundHit)
				j.Num("bound_distance", s.boundDistance);
			j.Bool("triangles_tested", s.exactTested);
			if (!s.exactTested && !s.exactSkipReason.empty())
				j.Str("not_tested_because", s.exactSkipReason);
			j.Bool("triangle_hit", s.exactHit);
			if (s.exactHit) {
				j.Num("hit_distance", s.exactDistance);
				j.Vec("hit_point", s.hitPoint);
				j.Int("hit_triangle", s.hitTriangle);
			}
			j.EndObject();
			j.EndObject();
		}
		j.EndArray();

		{
			std::vector<std::string> notes = a_info.notes;
			j.Strings("notes", notes);
		}
		j.EndObject();
		return j.Take();
	}

	std::string WriteFileAtomic(const std::filesystem::path& a_path, const std::string& a_content)
	{
		std::error_code ec;
		std::filesystem::create_directories(a_path.parent_path(), ec);
		if (ec)
			return "cannot create folder: " + ec.message();
		const auto temp = std::filesystem::path(a_path).replace_extension(".json.tmp");
		{
			std::ofstream file(temp, std::ios::binary | std::ios::trunc);
			if (!file)
				return "cannot open " + temp.string();
			file.write(a_content.data(), static_cast<std::streamsize>(a_content.size()));
			if (!file)
				return "cannot write " + temp.string();
		}
		std::filesystem::rename(temp, a_path, ec);
		if (ec) {
			std::filesystem::remove(temp, ec);
			return "cannot replace " + a_path.string();
		}
		return {};
	}
}
