// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
#include "PCH.h"

#include "ReportFill.h"

#include "Diagnostics.h"
#include "Inspector.h"
#include "NifStrings.h"
#include "PartSource.h"
#include "TextureInfo.h"
#include "WorldFill.h"

#include <unordered_map>

namespace ti
{
	namespace
	{
		constexpr float kRadiansToDegrees = 57.29577951308232f;

		std::string Safe(const char* a_text) { return a_text ? std::string(a_text) : std::string(); }

		std::string Hex8(std::uint32_t a_id) { return std::format("{:08X}", a_id); }

		std::string Lower(std::string a_text)
		{
			for (auto& c : a_text)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return a_text;
		}

		std::vector<std::string> ChainOf(const RE::TESForm* a_form)
		{
			std::vector<std::string> chain;
			const auto*              array = a_form->sourceFiles.array;
			if (!array)
				return chain;
			for (std::uint32_t i = 0; i < array->size(); ++i) {
				const auto* file = (*array)[i];
				if (file)
					chain.emplace_back(file->GetFilename());
			}
			return chain;
		}

		std::string NameOf(RE::TESForm* a_form)
		{
			if (!a_form)
				return {};
			return std::string(RE::TESFullName::GetFullName(*a_form));
		}

		std::string PluginOf(RE::TESForm* a_form)
		{
			if (!a_form)
				return {};
			if (auto* file = a_form->GetFile(0))
				return std::string(file->GetFilename());
			return {};
		}

		/**
		 * The plugin that defines a form, plus the last plugin that changes it when that is another one ("Fallout4.esm, changed by
		 * Mod.esp"): a mod that overrides a vanilla modification and points it at its own mesh is the one that brings that mesh.
		 */
		std::string PluginTextOf(RE::TESForm* a_form)
		{
			std::string plugin = PluginOf(a_form);
			if (plugin.empty())
				return plugin;
			const auto chain = ChainOf(a_form);
			if (chain.size() > 1 && chain.back() != plugin)
				plugin += ", changed by " + chain.back();
			return plugin;
		}

		void AddFact(PickInfo& a_info, std::string a_label, std::string a_value) { a_info.facts.push_back({ std::move(a_label), std::move(a_value) }); }

		// ---- identity ---------------------------------------------------------------------------------------------------

		std::string KindOf(RE::ENUM_FORM_ID a_base)
		{
			using T = RE::ENUM_FORM_ID;
			switch (a_base) {
			case T::kNPC_: return "npc";
			case T::kDOOR: return "door";
			case T::kCONT: return "container";
			case T::kLIGH: return "light";
			case T::kTREE: return "tree";
			case T::kWEAP:
			case T::kARMO:
			case T::kMISC:
			case T::kALCH:
			case T::kAMMO:
			case T::kBOOK:
			case T::kNOTE:
			case T::kKEYM:
				return "item";
			default: return "object";
			}
		}

		void FillIdentity(PickInfo& a_info, RE::TESObjectREFR* a_ref, RE::TESBoundObject* a_base)
		{
			auto& id = a_info.identity;
			id.displayName = a_info.object.name;
			id.ref = DescribeRecord(a_ref);
			id.base = DescribeRecord(a_base);
			if (a_base)
				id.baseName = std::string(RE::TESFullName::GetFullName(*a_base));
			id.kind = a_base ? KindOf(a_base->GetFormType()) : "object";
			id.editorIdSource = "engine";

			const auto position = a_ref->GetPosition();
			if (auto* cell = a_ref->GetParentCell()) {
				// A reference in a worldspace's persistent cell would report that cell (grid 0, 0). Say where it really stands instead:
				// the loaded exterior cell at its position, and that it is persistent.
				bool stillPersistentCell = false;
				if (cell->IsExterior()) {
					const int gridX = static_cast<int>(std::floor(position.x / 4096.0f));
					const int gridY = static_cast<int>(std::floor(position.y / 4096.0f));
					if (auto* coordinates = cell->GetCoordinates(); coordinates && (coordinates->cellX != gridX || coordinates->cellY != gridY)) {
						id.persistent = true;
						auto* real = ExteriorCellAt(position.x, position.y);
						if (real && real != cell)
							cell = real;
						else
							stillPersistentCell = true;
					}
				}
				id.cellFormId = cell->GetFormID();
				id.cellName = std::string(RE::TESFullName::GetFullName(*cell));
				id.cellEditorId = Safe(cell->GetFormEditorID());
				id.interior = cell->IsInterior();
				if (!id.interior && !stillPersistentCell) {
					if (auto* coordinates = cell->GetCoordinates()) {
						id.hasCoords = true;
						id.cellX = coordinates->cellX;
						id.cellY = coordinates->cellY;
					}
				}
			}
			if (auto* world = a_ref->GetWorldSpace()) {
				id.worldspaceName = std::string(RE::TESFullName::GetFullName(*world));
				id.worldspaceEditorId = Safe(world->GetFormEditorID());
			}
			id.position = { position.x, position.y, position.z };
			const auto& angle = a_ref->data.angle;
			id.rotationDegrees = { angle.x * kRadiansToDegrees, angle.y * kRadiansToDegrees, angle.z * kRadiansToDegrees };
			id.scale = a_ref->refScale ? static_cast<float>(a_ref->refScale) / 100.0f : 1.0f;
			if (!a_info.object.model.empty()) {
				const std::string lower = Lower(a_info.object.model);
				id.modelOrigin = ResolveOrigin(lower.rfind("meshes", 0) == 0 ? a_info.object.model : "meshes\\" + a_info.object.model);
			}
		}

		// ---- facts ------------------------------------------------------------------------------------------------------

		std::string ColourText(int a_r, int a_g, int a_b) { return std::format("{} (R{} G{} B{})", HexColor(a_r, a_g, a_b), a_r, a_g, a_b); }

		std::string LockLevelName(RE::LOCK_LEVEL a_level)
		{
			switch (a_level) {
			case RE::LOCK_LEVEL::kEasy: return "Novice";
			case RE::LOCK_LEVEL::kAverage: return "Advanced";
			case RE::LOCK_LEVEL::kHard: return "Expert";
			case RE::LOCK_LEVEL::kVeryHard: return "Master";
			case RE::LOCK_LEVEL::kRequiresKey: return "requires key";
			case RE::LOCK_LEVEL::kInaccessible: return "inaccessible";
			case RE::LOCK_LEVEL::kTerminal: return "terminal";
			case RE::LOCK_LEVEL::kBarred: return "barred";
			case RE::LOCK_LEVEL::kChained: return "chained";
			default: return "unknown level";
			}
		}

		std::string LockText(RE::TESObjectREFR* a_ref)
		{
			auto* lock = a_ref->GetLock();
			if (!lock)
				return "no lock";
			if ((lock->flags & static_cast<std::uint8_t>(RE::REFR_LOCK::Flags::kLocked)) == 0)
				return "unlocked";
			return "locked (" + LockLevelName(lock->GetLockLevel(a_ref)) + ")";
		}

		void FactsForDoor(PickInfo& a_info, RE::TESObjectREFR* a_ref)
		{
			AddFact(a_info, "Lock", LockText(a_ref));
			if (auto* lock = a_ref->GetLock(); lock && lock->key)
				AddFact(a_info, "Key", DescribeForm(lock->key));
			auto* teleport = a_ref->extraList ? a_ref->extraList->GetByType<RE::ExtraTeleport>() : nullptr;
			if (teleport && teleport->teleportData) {
				auto               linked = teleport->teleportData->linkedDoor.get();
				RE::TESObjectCELL* cell = linked ? linked->GetParentCell() : nullptr;
				if (!cell)
					cell = teleport->teleportData->transitionCell;
				if (linked) {
					const std::string where = cell ? CellDescription(cell) : std::string("a cell that is not loaded");
					AddFact(a_info, "Leads to", std::format("{} via door {}", where, Hex8(linked->GetFormID())));
				} else if (cell) {
					AddFact(a_info, "Leads to", std::format("{} (destination door not loaded)", CellDescription(cell)));
				} else {
					AddFact(a_info, "Leads to", "(destination not loaded)");
				}
			} else {
				AddFact(a_info, "Leads to", "(not a teleport door)");
			}
		}

		void FactsForContainer(PickInfo& a_info, RE::TESObjectREFR* a_ref, RE::TESBoundObject* a_base)
		{
			if (a_base && a_base->GetFormType() == RE::ENUM_FORM_ID::kCONT) {
				const auto* container = static_cast<RE::TESObjectCONT*>(a_base);
				AddFact(a_info, "Respawns", (container->data.contFlags & static_cast<std::int8_t>(RE::TESObjectCONT::Flags::kRespawn)) != 0 ? "yes" : "no");
			}
			AddFact(a_info, "Lock", LockText(a_ref));
			if (auto* lock = a_ref->GetLock(); lock && lock->key)
				AddFact(a_info, "Key", DescribeForm(lock->key));
		}

		void FactsForItem(PickInfo& a_info, RE::TESBoundObject* a_base)
		{
			if (!a_base)
				return;
			AddFact(a_info, "Value", std::format("{} caps", RE::TESValueForm::GetFormValue(a_base, nullptr)));
			AddFact(a_info, "Weight", std::format("{:.2f}", RE::TESWeightForm::GetFormWeight(a_base, nullptr)));
		}

		void FactsForLight(PickInfo& a_info, RE::TESBoundObject* a_base)
		{
			if (!a_base || a_base->GetFormType() != RE::ENUM_FORM_ID::kLIGH)
				return;
			const auto* light = static_cast<RE::TESObjectLIGH*>(a_base);
			using Flag = RE::OBJ_LIGH::Flag;
			const auto& d = light->data;
			AddFact(a_info, "Color", ColourText(static_cast<int>(d.color & 0xFF), static_cast<int>((d.color >> 8) & 0xFF), static_cast<int>((d.color >> 16) & 0xFF)));
			AddFact(a_info, "Radius", std::format("{}", d.radius));
			AddFact(a_info, "Fade", std::format("{:.2f}", light->fade));
			std::string flicker = "none";
			if (d.flags.any(Flag::kFlicker))
				flicker = std::format("flicker (period {:.2f}, intensity {:.2f})", d.flickerPeriodRecip, d.flickerIntensityAmplitude);
			else if (d.flags.any(Flag::kPulse))
				flicker = "pulse";
			AddFact(a_info, "Flicker", flicker);
			std::string shadow = "none";
			if (d.flags.any(Flag::kShadowSpotlight))
				shadow = "spot";
			else if (d.flags.any(Flag::kShadowHemisphere))
				shadow = "hemisphere";
			else if (d.flags.any(Flag::kShadowOmnidirectional))
				shadow = "omni";
			AddFact(a_info, "Shadows", shadow);
			AddFact(a_info, "Field of view", std::format("{:.0f} degrees{}", d.fov, d.flags.any(Flag::kShadowSpotlight, Flag::kNonShadowSpotlight) ? " (spotlight)" : ""));
			AddFact(a_info, "Falloff exponent", std::format("{:.2f}", d.fallOffExponent));
			if (d.flags.any(Flag::kOffByDefault))
				AddFact(a_info, "Initially off", "yes");
		}

		/** The race's and the NPC's "ActorType..." keywords without the prefix: Animal, Robot, SuperMutant, NPC ... */
		std::vector<std::string> ActorTypes(RE::TESRace* a_race, RE::TESNPC* a_npc)
		{
			std::vector<std::string> types;
			const auto               collect = [&](RE::BGSKeyword* a_keyword) {
                const std::string name = Safe(a_keyword->formEditorID.c_str());
                if (name.rfind("ActorType", 0) == 0 && name.size() > 9) {
                    const std::string shortName = name.substr(9);
                    if (std::find(types.begin(), types.end(), shortName) == types.end())
                        types.push_back(shortName);
                }
                return RE::BSContainer::ForEachResult::kContinue;
			};
			if (a_race)
				a_race->ForEachKeyword(collect);
			if (a_npc)
				a_npc->ForEachKeyword(collect);
			return types;
		}

		void FactsForNpc(PickInfo& a_info, RE::Actor* a_actor, RE::TESNPC* a_npc)
		{
			auto* race = a_actor->race ? a_actor->race : a_npc->GetFormRace();
			if (race)
				AddFact(a_info, "Race", DescribeForm(race));
			const auto types = ActorTypes(race, a_npc);
			const bool human = std::find(types.begin(), types.end(), "NPC") != types.end();
			std::string joined;
			for (const auto& t : types)
				joined += (joined.empty() ? "" : ", ") + t;
			AddFact(a_info, "Kind", human ? "humanoid" : (types.empty() ? "creature" : "creature (" + joined + ")"));
			if (human && types.size() > 1)
				AddFact(a_info, "Actor types", joined);
			AddFact(a_info, "Level", std::format("{}", a_actor->GetLevel()));
			if (a_npc->cl)
				AddFact(a_info, "Class", DescribeForm(a_npc->cl));
			AddFact(a_info, "Sex", a_npc->IsFemale() ? "female" : "male");
			std::string flags;
			if (a_npc->IsEssential())
				flags = "essential";
			if (a_npc->IsProtected())
				flags += std::string(flags.empty() ? "" : ", ") + "protected";
			AddFact(a_info, "Essential / protected", flags.empty() ? "neither" : flags);
			if (a_npc->Respawns())
				AddFact(a_info, "Respawns", "yes");
			if (a_npc->UsesTemplate() && a_npc->baseTemplateForm)
				AddFact(a_info, "Template", DescribeForm(a_npc->baseTemplateForm));
			if (std::abs(a_npc->height - 1.0f) > 0.001f && a_npc->height > 0.0f)
				AddFact(a_info, "Height", std::format("{:.3f}", a_npc->height));
		}

		void FactsForAnyRef(PickInfo& a_info, RE::TESObjectREFR* a_ref)
		{
			if (auto* owner = a_ref->GetOwner())
				AddFact(a_info, "Owner", DescribeForm(owner));
			const float scale = a_info.identity.scale;
			if (std::abs(scale - 1.0f) > 0.001f)
				AddFact(a_info, "Scale", std::format("{:.3f}", scale));
			if (!a_info.identity.base.type.empty())
				AddFact(a_info, "Form type", a_info.identity.base.type);
		}

		void FillFacts(PickInfo& a_info, RE::TESObjectREFR* a_ref, RE::TESBoundObject* a_base)
		{
			const auto& kind = a_info.identity.kind;
			if (kind == "door")
				FactsForDoor(a_info, a_ref);
			else if (kind == "container")
				FactsForContainer(a_info, a_ref, a_base);
			else if (kind == "item")
				FactsForItem(a_info, a_base);
			else if (kind == "light")
				FactsForLight(a_info, a_base);
			else if (kind == "tree")
				AddFact(a_info, "Tree record", DescribeForm(a_base));
			else if (kind == "npc" && a_ref->IsActor())
				FactsForNpc(a_info, static_cast<RE::Actor*>(a_ref), static_cast<RE::Actor*>(a_ref)->GetNPC());
			FactsForAnyRef(a_info, a_ref);
		}

		// ---- NPC section ------------------------------------------------------------------------------------------------

		NpcTexture ToNpcTexture(const ShapeInfo& a_shape, const TextureSlot& a_slot)
		{
			NpcTexture t;
			t.shape = a_shape.name;
			t.slotLabel = a_slot.label;
			t.path = a_slot.path;
			t.gpu = a_slot.gpu;
			return t;
		}

		const char* const kTextureSetLabels[8] = { "diffuse", "normal", "glow", "height", "environment", "reflectivity/specular", "wrinkles", "multilayer" };

		/** The object modifications (OMODs) an instance carries, as forms. Empty when it has none or they cannot be read. */
		std::vector<RE::TESForm*> OmodsOf(const RE::BGSObjectInstanceExtra* a_extra)
		{
			std::vector<RE::TESForm*> mods;
			try {
				if (!a_extra || !a_extra->values)
					return mods;
				for (const auto& entry : a_extra->GetIndexData()) {
					if (entry.disabled || !entry.objectID)
						continue;
					if (auto* form = RE::TESForm::GetFormByID(entry.objectID))
						mods.push_back(form);
				}
			} catch (...) {
			}
			return mods;
		}

		/** "Name (EDID, 0012AB34) from Plugin.esm": one object modification as the card lists it. */
		std::string ModText(RE::TESForm* a_mod)
		{
			std::string text = DescribeForm(a_mod);
			if (const std::string plugin = PluginTextOf(a_mod); !plugin.empty())
				text += " from " + plugin;
			return text;
		}

		std::vector<std::string> ModTexts(const std::vector<RE::TESForm*>& a_mods)
		{
			std::vector<std::string> texts;
			for (auto* mod : a_mods) {
				std::string text = ModText(mod);
				// Bethesda's include/filter helpers ("(unnamed)", "***INCLUDE a Standard Sight***") are not parts a player sees: leave them off the card
				if (text.starts_with("(unnamed)") || text.starts_with("***"))
					continue;
				texts.push_back(std::move(text));
			}
			return texts;
		}

		// ---- which mesh file each part comes from ------------------------------------------------------------------------

		/** A mesh that may have contributed parts to the picked object, and who brings it. */
		struct MeshCandidate
		{
			std::string path;   ///< as the form's model names it (relative to Meshes)
			std::string owner;  ///< "Name (Plugin.esm)"
			bool        isMod = false;
		};

		/** "Name (Plugin.esm)": what the card says brings a mesh. A nameless form falls back on its editor ID, then its form ID. */
		std::string OwnerText(RE::TESForm* a_form)
		{
			std::string name = NameOf(a_form);
			if (name.empty())
				name = Safe(a_form->GetFormEditorID());
			if (name.empty())
				name = Hex8(a_form->GetFormID());
			const std::string plugin = PluginTextOf(a_form);
			return plugin.empty() ? name : name + " (" + plugin + ")";
		}

		/** The model path a form's model component holds; empty when it has none. */
		std::string ModelPathOf(const RE::TESModel* a_model)
		{
			try {
				return a_model ? Safe(a_model->GetModel()) : std::string();
			} catch (...) {
				return {};
			}
		}

		/**
		 * Adds an item's own model and the model of each of its modifications as candidates. Items without modifications add nothing:
		 * only a built-up object (a weapon, a modded armor piece) has parts whose file the card cannot take from the one model.
		 */
		void AddItemCandidates(std::vector<MeshCandidate>& a_out, RE::TESForm* a_item, const std::string& a_itemModel, const std::vector<RE::TESForm*>& a_mods)
		{
			bool any = false;
			for (auto* mod : a_mods) {
				if (mod->GetFormType() != RE::ENUM_FORM_ID::kOMOD)
					continue;
				const std::string model = ModelPathOf(static_cast<RE::BGSMod::Attachment::Mod*>(mod));
				if (model.empty())
					continue;
				if (!any && a_item && !a_itemModel.empty())
					a_out.push_back({ a_itemModel, OwnerText(a_item), false });
				any = true;
				a_out.push_back({ model, OwnerText(mod), true });
			}
		}

		/** The object modifications on a worn item (an armor piece's or a weapon's mods): "name (form id) from plugin". Empty when it has none or they cannot be read. */
		std::vector<std::string> ModsOf(const RE::BIPOBJECT& a_object) { return ModTexts(OmodsOf(a_object.modExtra)); }

		std::string MeshPathOf(const std::string& a_modelPath)
		{
			const std::string lower = Lower(a_modelPath);
			return lower.rfind("meshes", 0) == 0 ? a_modelPath : "meshes\\" + a_modelPath;
		}

		/**
		 * Works out which candidate file each part is from, by reading the names in each file's header. When any candidate cannot be read
		 * (the archives are still being indexed, a file is missing) nothing is claimed: a part's name might be in the file that could
		 * not be read.
		 */
		void ResolvePartSources(PickInfo& a_info, const std::vector<MeshCandidate>& a_candidates)
		{
			if (a_candidates.empty() || a_info.shapes.empty())
				return;
			static std::unordered_map<std::string, std::vector<std::string>> cache;  // lower-case path -> the header's names
			std::vector<MeshSource>                                          sources;
			for (const auto& c : a_candidates) {
				const std::string path = MeshPathOf(c.path);
				const std::string key = Lower(path);
				auto              known = cache.find(key);
				if (known == cache.end()) {
					std::string              why;
					bool                     pending = false;
					std::vector<std::string> names;
					const auto               bytes = ReadDataFile(path, 16u * 1024u * 1024u, &why, &pending);
					if (bytes.empty()) {
						spdlog::info("part source: {} {}: {}", pending ? "not read yet" : "cannot read", path, why);
						return;
					}
					if (!NifHeaderStrings(bytes.data(), bytes.size(), names)) {
						spdlog::info("part source: {} is not a mesh header this reads", path);
						return;
					}
					if (cache.size() > 512)
						cache.clear();
					known = cache.emplace(key, std::move(names)).first;
				}
				MeshSource s;
				s.path = path;
				s.owner = c.owner;
				s.isMod = c.isMod;
				s.origin = ResolveOrigin(path);
				SetMeshNames(s, known->second);
				sources.push_back(std::move(s));
			}
			AttributeParts(a_info.shapes, sources);
			int found = 0, several = 0;
			for (const auto& shape : a_info.shapes) {
				found += shape.sourceMesh.empty() ? 0 : 1;
				several += shape.sourceChoices.empty() ? 0 : 1;
			}
			spdlog::info("part source: {} candidate file(s), {} of {} part(s) placed, {} named in several files", sources.size(), found, a_info.shapes.size(), several);
		}

		/** One worn item (or the skin) as the biped holds it. */
		WornItem MakeWornItem(const RE::BIPOBJECT& a_object, int a_slot)
		{
			WornItem w;
			RE::TESForm* item = a_object.parent.object;
			w.slots.push_back(a_slot);
			w.fallout4 = true;
			w.itemFormId = item->GetFormID();
			w.name = NameOf(item);
			if (w.name.empty())
				w.name = std::format("(unnamed item {})", Hex8(w.itemFormId));
			w.itemPlugin = PluginOf(item);
			if (auto* addon = a_object.armorAddon) {
				w.addonFormId = addon->GetFormID();
				w.addonPlugin = PluginOf(addon);
			}
			if (a_object.part)
				w.model = Safe(a_object.part->GetModel());
			// the biped's own skin texture set (the body and head textures the race or NPC puts on a skin part)
			if (auto* set = a_object.skinTexture) {
				if (const std::string material = Safe(set->materialName.c_str()); !material.empty())
					w.materials.push_back(material);
				for (int i = 0; i < 8; ++i) {
					const std::string path = Safe(set->textures[i].textureName.c_str());
					if (!path.empty()) {
						NpcTexture t;
						t.shape = "skin texture set " + Hex8(set->GetFormID());
						t.slotLabel = kTextureSetLabels[i];
						t.path = path;
						w.textures.push_back(std::move(t));
					}
				}
			}
			if (a_object.partClone) {
				w.live3d = true;
				for (const auto& shape : DescribeParts(a_object.partClone.get())) {
					for (const auto& slotTexture : shape.textures)
						w.textures.push_back(ToNpcTexture(shape, slotTexture));
					if (!shape.materialPath.empty() && std::find(w.materials.begin(), w.materials.end(), shape.materialPath) == w.materials.end())
						w.materials.push_back(shape.materialPath);
				}
			}
			w.mods = ModsOf(a_object);
			return w;
		}

		/** Adds the item's slot to the entry already holding the same item and addon, else a new entry. */
		void AddOrMerge(std::vector<WornItem>& a_list, WornItem a_item)
		{
			auto it = std::find_if(a_list.begin(), a_list.end(), [&](const WornItem& w) { return w.itemFormId == a_item.itemFormId && w.addonFormId == a_item.addonFormId; });
			if (it != a_list.end()) {
				it->slots.push_back(a_item.slots.front());
				for (auto& t : a_item.textures)
					it->textures.push_back(std::move(t));
				for (auto& m : a_item.materials)
					if (std::find(it->materials.begin(), it->materials.end(), m) == it->materials.end())
						it->materials.push_back(std::move(m));
				return;
			}
			a_list.push_back(std::move(a_item));
		}

		void FillWorn(NpcSection& a_npc, RE::Actor* a_actor, const RE::TESForm* a_skin, std::vector<MeshCandidate>& a_candidates)
		{
			if (!a_actor->biped)
				return;
			const auto& biped = a_actor->biped;
			// the worn slots and, after them, the weapon slots (a weapon in hand or holstered is built from its own model and its modifications' models)
			const auto count = static_cast<std::uint32_t>(RE::BIPED_OBJECT::kTotal);
			for (std::uint32_t i = 0; i < count; ++i) {
				const auto& object = biped->object[i];
				if (!object.parent.object)
					continue;
				const bool isSkin = a_skin && object.parent.object == a_skin;
				WornItem   w = MakeWornItem(object, 30 + static_cast<int>(i));
				if (!isSkin)
					AddItemCandidates(a_candidates, object.parent.object, ModelPathOf(object.part), OmodsOf(object.modExtra));
				AddOrMerge(isSkin ? a_npc.skinItem.parts : a_npc.worn, std::move(w));
			}
		}

		void FillHeadParts(NpcSection& a_npc, RE::TESNPC* a_npcBase)
		{
			RE::TESNPC* source = a_npcBase->GetRootFaceNPC();
			if (!source)
				source = a_npcBase;
			auto parts = source->GetHeadParts();
			if (parts.empty() && source != a_npcBase)
				parts = a_npcBase->GetHeadParts();
			for (auto* part : parts) {
				if (!part)
					continue;
				HeadPartInfo h;
				switch (part->type.get()) {
				case RE::BGSHeadPart::HeadPartType::kMisc: h.type = "Misc"; break;
				case RE::BGSHeadPart::HeadPartType::kFace: h.type = "Face"; break;
				case RE::BGSHeadPart::HeadPartType::kEyes: h.type = "Eyes"; break;
				case RE::BGSHeadPart::HeadPartType::kHair: h.type = "Hair"; break;
				case RE::BGSHeadPart::HeadPartType::kFacialHair: h.type = "FacialHair"; break;
				case RE::BGSHeadPart::HeadPartType::kScar: h.type = "Scar"; break;
				case RE::BGSHeadPart::HeadPartType::kEyebrows: h.type = "Eyebrows"; break;
				case RE::BGSHeadPart::HeadPartType::kMeatcaps: h.type = "Meatcaps"; break;
				case RE::BGSHeadPart::HeadPartType::kTeeth: h.type = "Teeth"; break;
				case RE::BGSHeadPart::HeadPartType::kHeadRear: h.type = "HeadRear"; break;
				default: h.type = "Unknown"; break;
				}
				h.formId = part->GetFormID();
				h.editorId = Safe(part->formEditorID.c_str());
				h.name = NameOf(part);
				h.model = Safe(part->GetModel());
				if (auto* set = part->textureSet) {
					h.textureSetFormId = set->GetFormID();
					// Fallout 4's texture sets carry a material file (.bgsm) of their own: it comes first
					if (const std::string material = Safe(set->materialName.c_str()); !material.empty())
						h.textureSetPaths.push_back("material: " + material);
					for (int i = 0; i < 8; ++i) {
						const std::string path = Safe(set->textures[i].textureName.c_str());
						if (!path.empty())
							h.textureSetPaths.push_back(path);
					}
				}
				a_npc.headParts.push_back(std::move(h));
			}
		}

		/** The id a plugin's own record carries (the load order byte, and for a light plugin the slot, dropped). */
		std::uint32_t LocalId(RE::TESForm* a_form)
		{
			const std::uint32_t id = a_form->GetFormID();
			if (auto* file = a_form->GetFile(0); file && file->IsLight())
				return id & 0x00000FFFu;
			return id & 0x00FFFFFFu;
		}

		std::string FaceGenName(RE::TESNPC* a_face, const std::string& a_plugin, bool a_tint)
		{
			const std::uint32_t local = LocalId(a_face);
			return a_tint ? std::format("textures\\actors\\character\\facecustomization\\{}\\{:08x}_d.dds", a_plugin, local) :
			                std::format("meshes\\actors\\character\\facegendata\\facegeom\\{}\\{:08x}.nif", a_plugin, local);
		}

		/**
		 * Is this NPC's face a baked FaceGen file (made in the Creation Kit) or generated when the game ran? Fallout 4 looks for
		 * "FaceGeom\<plugin>\<form id>.nif" and the face tint "FaceCustomization\<plugin>\<form id>_d.dds" in loose files and archives
		 * (the FaceGeom meshes ship in the Meshes archives, the face textures in the Textures archives). Absent both, it builds the face
		 * from the head parts and morphs.
		 */
		void FillFaceGen(NpcSection& a_npc, RE::TESNPC* a_npcBase)
		{
			auto& g = a_npc.faceGen;
			g.looked = true;
			auto* face = a_npcBase->GetRootFaceNPC();
			if (!face)
				face = a_npcBase;
			a_npc.faceNpcFormId = face->GetFormID();
			std::vector<std::string> plugins = ChainOf(face);
			std::reverse(plugins.begin(), plugins.end());  // the last plugin to change the NPC first
			if (plugins.empty()) {
				g.verdict = kFaceGenMissing;
				g.evidence = "the face NPC has no source plugin (created in game), so there is no FaceGen file for it";
				return;
			}
			for (const auto& plugin : plugins) {
				if (g.meshPath.empty() || g.meshOrigin.source == "not found") {
					const std::string path = FaceGenName(face, plugin, false);
					const auto        origin = ResolveOrigin(path);
					if (g.meshPath.empty() || origin.source != "not found") {
						g.meshPath = path;
						g.meshOrigin = origin;
					}
				}
				if (g.tintPath.empty() || g.tintOrigin.source == "not found") {
					const std::string path = FaceGenName(face, plugin, true);
					const auto        origin = ResolveOrigin(path);
					if (g.tintPath.empty() || origin.source != "not found") {
						g.tintPath = path;
						g.tintOrigin = origin;
					}
				}
			}
			const bool meshFile = g.meshOrigin.source == "loose" || g.meshOrigin.source == "archive";
			const bool tintFile = g.tintOrigin.source == "loose" || g.tintOrigin.source == "archive";
			if (meshFile || tintFile) {
				g.verdict = kFaceGenFound;
				g.evidence = std::format("found {}{}{} (looked in loose files and archives)", meshFile ? "FaceGeom mesh" : "", meshFile && tintFile ? " and " : "",
					tintFile ? "face tint texture" : "");
			} else {
				g.verdict = kFaceGenMissing;
				g.evidence = "no FaceGeom mesh and no face tint texture for this NPC in loose files or archives, so the game builds the face when it loads";
			}
		}

		void FillNpc(PickInfo& a_info, RE::Actor* a_actor, RE::TESNPC* a_npc, std::vector<MeshCandidate>& a_candidates)
		{
			auto& n = a_info.npc;
			n.present = true;
			const bool female = a_npc->IsFemale();
			n.sex = female ? "female" : "male";
			auto* race = a_actor->race ? a_actor->race : a_npc->GetFormRace();
			if (race) {
				n.race = DescribeForm(race);
				n.skeleton = Safe(race->skeletonModel[female ? 1 : 0].GetModel());
			}
			RE::TESObjectARMO* skin = a_npc->formSkin;
			if (skin) {
				n.skin = DescribeForm(skin);
				n.skinItem.present = true;
				n.skinItem.name = NameOf(skin);
				n.skinItem.formId = skin->GetFormID();
				n.skinItem.plugin = PluginOf(skin);
				n.skinItem.source = "the NPC's own skin (WNAM)";
			} else {
				n.skin = "(none: the body comes from the race and the biped)";
			}
			n.skinToneColor = HexColor(static_cast<std::uint8_t>(a_npc->bodyTintColorR), static_cast<std::uint8_t>(a_npc->bodyTintColorG), static_cast<std::uint8_t>(a_npc->bodyTintColorB));
			FillHeadParts(n, a_npc);
			FillWorn(n, a_actor, skin, a_candidates);
			FillFaceGen(n, a_npc);
		}

		// ---- parts ------------------------------------------------------------------------------------------------------

		/** File origin for every texture and the material file of every part: the warnings need to know whether a named file exists. */
		void ResolveOrigins(PickInfo& a_info)
		{
			for (auto& shape : a_info.shapes) {
				for (auto& t : shape.textures) {
					if (t.path.empty())
						continue;
					FileOrigin origin = ResolveOrigin(t.path);
					if (origin.source == "not found" && Lower(t.path).rfind("textures", 0) != 0)
						origin = ResolveOrigin("textures\\" + t.path);
					t.origin = std::move(origin);
				}
				if (!shape.materialPath.empty())
					shape.materialOrigin = ResolveOrigin(shape.materialPath);
			}
		}

		void RunDiagnostics(PickInfo& a_info)
		{
			for (auto& s : a_info.shapes) {
				s.diagnostics = Diagnose(s, DiagGame::kFallout4);
				const std::string name = s.name.empty() ? std::format("part {}", s.index + 1) : s.name;
				for (const auto& d : s.diagnostics)
					a_info.diagnostics.push_back({ d.rule, std::format("{}: {}", name, d.text) });
			}
		}

		/** For worn armor: the clicked part's body area from the worn item whose live 3D holds a part of the same name. */
		void FillBodyAreaFromWorn(PickInfo& a_info)
		{
			if (a_info.clicked < 0 || !a_info.npc.present)
				return;
			auto& shape = a_info.shapes[static_cast<std::size_t>(a_info.clicked)];
			if (!shape.bodyArea.empty())
				return;
			for (const auto& w : a_info.npc.worn) {
				for (const auto& t : w.textures) {
					if (t.shape == shape.name) {
						std::string names;
						for (std::size_t i = 0; i < w.slots.size(); ++i)
							names += (i ? ", " : "") + SlotNameOf(w, w.slots[i]);
						shape.bodyArea = "worn: " + w.name + " (" + names + ")";
						return;
					}
				}
			}
		}
	}

	FormRecord DescribeRecord(RE::TESForm* a_form)
	{
		FormRecord r;
		if (!a_form)
			return r;
		r.formId = a_form->GetFormID();
		r.type = Safe(a_form->GetFormTypeString());
		r.editorId = Safe(a_form->GetFormEditorID());
		r.chain = ChainOf(a_form);
		if (!r.chain.empty()) {
			r.addedBy = r.chain.front();
			r.lastChangedBy = r.chain.back();
		} else if ((r.formId >> 24) == 0xFF) {
			r.addedBy = "(created in game)";
			r.lastChangedBy = r.addedBy;
		}
		return r;
	}

	std::string DescribeForm(RE::TESForm* a_form)
	{
		if (!a_form)
			return "(none)";
		const std::string editor = Safe(a_form->GetFormEditorID());
		const std::string name = NameOf(a_form);
		std::string       out = name.empty() ? std::string("(unnamed)") : name;
		out += " (";
		if (!editor.empty())
			out += editor + ", ";
		out += Hex8(a_form->GetFormID()) + ")";
		return out;
	}

	std::string CellDescription(RE::TESObjectCELL* a_cell)
	{
		if (!a_cell)
			return "(unknown)";
		std::string       name = NameOf(a_cell);
		const std::string editor = Safe(a_cell->GetFormEditorID());
		if (name.empty())
			name = "(unnamed cell)";
		return editor.empty() ? name : name + " (" + editor + ")";
	}

	void FillReport(PickInfo& a_info, RE::TESObjectREFR* a_ref, RE::NiAVObject*)
	{
		a_info.diagnostics.clear();
		try {
			if (!a_ref)
				return;
			auto* base = a_ref->GetObjectReference();
			FillIdentity(a_info, a_ref, base);
			FillFacts(a_info, a_ref, base);
			std::vector<MeshCandidate> candidates;
			if (a_ref->IsActor()) {
				auto* actor = static_cast<RE::Actor*>(a_ref);
				if (auto* npc = actor->GetNPC())
					FillNpc(a_info, actor, npc, candidates);
			} else if (a_ref->extraList) {
				// an item lying in the world (a weapon on the ground) keeps its modifications in the reference's extra data
				const auto omods = OmodsOf(a_ref->extraList->GetByType<RE::BGSObjectInstanceExtra>());
				for (const auto& text : ModTexts(omods))
					AddFact(a_info, "Mod", text);
				AddItemCandidates(candidates, base, a_info.object.model, omods);
			}
			try {
				ResolvePartSources(a_info, candidates);
			} catch (const std::exception& e) {
				spdlog::error("part source failed for {:08X}: {}", a_ref->GetFormID(), e.what());
			} catch (...) {
				spdlog::error("part source failed for {:08X}: unknown exception", a_ref->GetFormID());
			}
			ResolveOrigins(a_info);
			FillBodyAreaFromWorn(a_info);
			RunDiagnostics(a_info);
		} catch (const std::exception& e) {
			spdlog::error("report fill failed for {:08X}: {}", a_ref ? a_ref->GetFormID() : 0u, e.what());
		} catch (...) {
			spdlog::error("report fill failed for {:08X}: unknown exception", a_ref ? a_ref->GetFormID() : 0u);
		}
	}
}
