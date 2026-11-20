// SPDX-License-Identifier: GPL-3.0-or-later
// AI-Generated (Made with AI)
#include "PCH.h"

#include "ReportFill.h"

#include "Diagnostics.h"
#include "Inspector.h"
#include "TextureInfo.h"

namespace ti
{
	namespace
	{
		constexpr float kRadiansToDegrees = 57.29577951308232f;

		std::string Safe(const char* a_text) { return a_text ? std::string(a_text) : std::string(); }

		std::string Hex8(std::uint32_t a_id) { return std::format("{:08X}", a_id); }

		// ---- forms ------------------------------------------------------------------------------------------------------

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

		FormRecord MakeRecord(RE::TESForm* a_form, std::string& a_editorSource)
		{
			FormRecord r;
			if (!a_form)
				return r;
			r.formId = a_form->GetFormID();
			r.type = std::string(RE::FormTypeToString(a_form->GetFormType()));
			std::string source;
			r.editorId = EditorIdOf(r.formId, a_form->GetFormEditorID(), source);
			if (!r.editorId.empty() && a_editorSource.empty())
				a_editorSource = source;
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

		/** "Name (EDID, 0001234)" for a form named in a fact. */
		std::string Describe(RE::TESForm* a_form)
		{
			if (!a_form)
				return "(none)";
			std::string       source;
			const std::string editor = EditorIdOf(a_form->GetFormID(), a_form->GetFormEditorID(), source);
			const std::string name = Safe(a_form->GetName());
			std::string       out = name.empty() ? std::string("(unnamed)") : name;
			out += " (";
			if (!editor.empty())
				out += editor + ", ";
			out += Hex8(a_form->GetFormID()) + ")";
			return out;
		}

		void AddFact(PickInfo& a_info, std::string a_label, std::string a_value)
		{
			a_info.facts.push_back({ std::move(a_label), std::move(a_value) });
		}

		std::string LockText(RE::TESObjectREFR* a_ref)
		{
			auto* lock = a_ref->GetLock();
			if (!lock)
				return "no lock";
			if (!lock->IsLocked())
				return "unlocked";
			std::string level;
			switch (a_ref->GetLockLevel()) {
			case RE::LOCK_LEVEL::kVeryEasy: level = "Novice"; break;
			case RE::LOCK_LEVEL::kEasy: level = "Apprentice"; break;
			case RE::LOCK_LEVEL::kAverage: level = "Adept"; break;
			case RE::LOCK_LEVEL::kHard: level = "Expert"; break;
			case RE::LOCK_LEVEL::kVeryHard: level = "Master"; break;
			case RE::LOCK_LEVEL::kRequiresKey: level = "requires key"; break;
			default: level = "unknown level"; break;
			}
			return "locked (" + level + ")";
		}

		std::string CellText(RE::TESObjectCELL* a_cell)
		{
			if (!a_cell)
				return "(unknown)";
			std::string       source;
			const std::string editor = EditorIdOf(a_cell->GetFormID(), a_cell->GetFormEditorID(), source);
			std::string       name = Safe(a_cell->GetName());
			if (name.empty())
				name = "(unnamed cell)";
			return editor.empty() ? name : name + " (" + editor + ")";
		}

		// ---- identity ---------------------------------------------------------------------------------------------------

		std::string KindOf(RE::FormType a_base)
		{
			using T = RE::FormType;
			switch (a_base) {
			case T::NPC: return "npc";
			case T::Door: return "door";
			case T::Container: return "container";
			case T::Light: return "light";
			case T::Tree: return "tree";
			case T::Weapon:
			case T::Armor:
			case T::Misc:
			case T::Ingredient:
			case T::AlchemyItem:
			case T::Book:
			case T::Ammo:
			case T::Scroll:
			case T::SoulGem:
			case T::KeyMaster:
				return "item";
			default: return "object";
			}
		}

		void FillIdentity(PickInfo& a_info, RE::TESObjectREFR* a_ref, RE::TESBoundObject* a_base)
		{
			auto& id = a_info.identity;
			id.displayName = a_info.object.name;
			id.ref = MakeRecord(a_ref, id.editorIdSource);
			id.base = MakeRecord(a_base, id.editorIdSource);
			if (a_base)
				id.baseName = Safe(a_base->GetName());
			id.kind = a_base ? KindOf(a_base->GetFormType()) : "object";
			// editor IDs the engine dropped but another source (po3 Tweaks) still has: show them in the basics too
			if (a_info.object.baseEditorId.empty())
				a_info.object.baseEditorId = id.base.editorId;
			if (a_info.object.refEditorId.empty())
				a_info.object.refEditorId = id.ref.editorId;

			const auto position = a_ref->GetPosition();
			auto*      world = a_ref->GetWorldspace();
			if (auto* cell = a_ref->GetParentCell()) {
				// A reference in a worldspace's persistent cell would report that cell: "(Wilderness) [0,0]". Say where it really
				// stands instead: the exterior cell at its position (the worldspace's own lookup by grid), and that it is persistent.
				bool stillPersistentCell = false;
				if (!world)
					world = cell->GetRuntimeData().worldSpace;
				bool persistentCell = world && world->persistentCell == cell;
				if (!persistentCell && !cell->IsInteriorCell()) {
					// the worldspace's pointer was not it: an exterior cell whose grid is not the grid the position falls in is the persistent one
					if (auto* coordinates = cell->GetCoordinates()) {
						persistentCell = coordinates->cellX != static_cast<int>(std::floor(position.x / 4096.0f)) ||
						                 coordinates->cellY != static_cast<int>(std::floor(position.y / 4096.0f));
					}
				}
				if (persistentCell) {
					id.persistent = true;
					const int          gridX = static_cast<int>(std::floor(position.x / 4096.0f));
					const int          gridY = static_cast<int>(std::floor(position.y / 4096.0f));
					RE::TESObjectCELL* real = nullptr;
					if (world) {
						if (const auto found = world->cellMap.find(RE::CellID(static_cast<std::int16_t>(gridY), static_cast<std::int16_t>(gridX))); found != world->cellMap.end())
							real = found->second;
					}
					if (!real) {
						if (auto* tes = RE::TES::GetSingleton())
							real = tes->GetCell(position);
					}
					if (real && real != cell)
						cell = real;
					else
						stillPersistentCell = true;  // the real cell is not in memory: keep the persistent cell, without made-up coordinates
					spdlog::info("card: persistent-cell reference {:08X} at {:.0f}, {:.0f}: grid {}, {}: the real cell is {}", a_ref->GetFormID(), position.x, position.y, gridX, gridY, stillPersistentCell ? "not in memory" : "found");
				}
				id.cellFormId = cell->GetFormID();
				id.cellName = Safe(cell->GetName());
				std::string source;
				id.cellEditorId = EditorIdOf(cell->GetFormID(), cell->GetFormEditorID(), source);
				id.interior = cell->IsInteriorCell();
				if (!id.interior && !stillPersistentCell) {
					if (auto* coordinates = cell->GetCoordinates()) {
						id.hasCoords = true;
						id.cellX = coordinates->cellX;
						id.cellY = coordinates->cellY;
					}
				}
			}
			if (world) {
				id.worldspaceName = Safe(world->GetName());
				std::string source;
				id.worldspaceEditorId = EditorIdOf(world->GetFormID(), world->GetFormEditorID(), source);
			}
			id.position = { position.x, position.y, position.z };
			const auto angle = a_ref->GetAngle();
			id.rotationDegrees = { angle.x * kRadiansToDegrees, angle.y * kRadiansToDegrees, angle.z * kRadiansToDegrees };
			id.scale = a_ref->GetScale();
			if (!a_info.object.model.empty())
				id.modelOrigin = ResolveOrigin(a_info.object.model.rfind("meshes", 0) == 0 || a_info.object.model.rfind("Meshes", 0) == 0 ?
				                                   a_info.object.model :
				                                   "meshes\\" + a_info.object.model);
		}

		// ---- facts ------------------------------------------------------------------------------------------------------

		std::string ColourText(int a_r, int a_g, int a_b) { return std::format("{} (R{} G{} B{})", HexColor(a_r, a_g, a_b), a_r, a_g, a_b); }

		void FactsForDoor(PickInfo& a_info, RE::TESObjectREFR* a_ref)
		{
			AddFact(a_info, "Lock", LockText(a_ref));
			if (auto* lock = a_ref->GetLock(); lock && lock->key)
				AddFact(a_info, "Key", Describe(lock->key));
			if (auto* teleport = a_ref->extraList.GetByType<RE::ExtraTeleport>(); teleport && teleport->teleportData) {
				auto linked = teleport->teleportData->linkedDoor.get();
				if (linked) {
					RE::TESObjectCELL* cell = linked->GetParentCell();
					if (!cell)
						cell = linked->GetSaveParentCell();  // the cell the engine saved the door in, when it is not loaded
					std::string where;
					if (cell) {
						where = CellText(cell);
					} else {
						const auto pos = linked->GetPosition();
						where = std::format("a cell that is not loaded, door at {:.0f}, {:.0f}, {:.0f}", pos.x, pos.y, pos.z);
					}
					AddFact(a_info, "Leads to", std::format("{} via door {}", where, Hex8(linked->GetFormID())));
				} else {
					AddFact(a_info, "Leads to", "(destination door not loaded)");
				}
			} else {
				AddFact(a_info, "Leads to", "(not a teleport door)");
			}
		}

		void FactsForContainer(PickInfo& a_info, RE::TESObjectREFR* a_ref, RE::TESBoundObject* a_base)
		{
			if (auto* container = a_base ? a_base->As<RE::TESObjectCONT>() : nullptr)
				AddFact(a_info, "Respawns", container->data.flags.all(RE::CONT_DATA::Flag::kRespawn) ? "yes" : "no");
			AddFact(a_info, "Lock", LockText(a_ref));
			if (auto* lock = a_ref->GetLock(); lock && lock->key)
				AddFact(a_info, "Key", Describe(lock->key));
		}

		void FactsForItem(PickInfo& a_info, RE::TESObjectREFR* a_ref, RE::TESBoundObject* a_base)
		{
			if (!a_base)
				return;
			AddFact(a_info, "Value", std::format("{} gold", a_base->GetGoldValue()));
			AddFact(a_info, "Weight", std::format("{:.2f}", a_base->GetWeight()));
			RE::EnchantmentItem* enchantment = a_ref->GetEnchantment();
			if (!enchantment) {
				if (auto* enchantable = a_base->As<RE::TESEnchantableForm>())
					enchantment = enchantable->formEnchanting;
			}
			if (enchantment)
				AddFact(a_info, "Enchantment", Describe(enchantment));
		}

		void FactsForLight(PickInfo& a_info, RE::TESBoundObject* a_base)
		{
			auto* light = a_base ? a_base->As<RE::TESObjectLIGH>() : nullptr;
			if (!light)
				return;
			using Flag = RE::TES_LIGHT_FLAGS;
			const auto& d = light->data;
			AddFact(a_info, "Color", ColourText(d.color.red, d.color.green, d.color.blue));
			AddFact(a_info, "Radius", std::format("{}", d.radius));
			AddFact(a_info, "Fade", std::format("{:.2f}", light->fade));
			std::string flicker = "none";
			if (d.flags.any(Flag::kFlicker))
				flicker = "flicker";
			else if (d.flags.any(Flag::kFlickerSlow))
				flicker = "flicker (slow)";
			else if (d.flags.any(Flag::kPulse))
				flicker = "pulse";
			else if (d.flags.any(Flag::kPulseSlow))
				flicker = "pulse (slow)";
			AddFact(a_info, "Flicker", flicker);
			std::string shadow = "none";
			if (d.flags.any(Flag::kSpotShadow))
				shadow = "spot";
			else if (d.flags.any(Flag::kHemiShadow))
				shadow = "hemisphere";
			else if (d.flags.any(Flag::kOmniShadow))
				shadow = "omni";
			AddFact(a_info, "Shadows", shadow);
			AddFact(a_info, "Field of view", std::format("{:.0f} degrees{}", d.fov, d.flags.any(Flag::kSpotlight, Flag::kSpotShadow) ? " (spotlight)" : ""));
			if (d.flags.any(Flag::kOffByDefault))
				AddFact(a_info, "Initially off", "yes");
		}

		void FactsForNpc(PickInfo& a_info, RE::Actor* a_actor)
		{
			auto* npc = a_actor->GetActorBase();
			if (!npc)
				return;
			if (auto* race = a_actor->GetRace())
				AddFact(a_info, "Race", Describe(race));
			AddFact(a_info, "Level", std::format("{}", npc->GetLevel()));
			if (npc->npcClass)
				AddFact(a_info, "Class", Describe(npc->npcClass));
			AddFact(a_info, "Sex", npc->GetSex() == RE::SEXES::kFemale ? "female" : "male");
			std::string flags;
			if (npc->IsEssential())
				flags = "essential";
			if (npc->IsProtected())
				flags += std::string(flags.empty() ? "" : ", ") + "protected";
			AddFact(a_info, "Essential / protected", flags.empty() ? "neither" : flags);
		}

		void FactsForAnyRef(PickInfo& a_info, RE::TESObjectREFR* a_ref)
		{
			if (auto* owner = a_ref->GetOwner()) {
				std::string text = Describe(owner);
				if (owner->GetFormType() == RE::FormType::Faction)
					if (auto* rank = a_ref->extraList.GetByType<RE::ExtraRank>())
						text += std::format(", rank {} required", rank->rank);
				AddFact(a_info, "Owner", text);
			}
			if (auto* parent = a_ref->extraList.GetByType<RE::ExtraEnableStateParent>()) {
				if (auto linked = parent->parent.get())
					AddFact(a_info, "Enable parent", Describe(linked.get()) + (parent->flags & 1 ? " (inverted)" : ""));
				else
					AddFact(a_info, "Enable parent", "(not loaded)");
			}
			const float scale = a_ref->GetScale();
			if (std::abs(scale - 1.0f) > 0.001f)
				AddFact(a_info, "Scale", std::format("{:.3f}", scale));
		}

		void FillFacts(PickInfo& a_info, RE::TESObjectREFR* a_ref, RE::TESBoundObject* a_base)
		{
			const auto& kind = a_info.identity.kind;
			if (kind == "door")
				FactsForDoor(a_info, a_ref);
			else if (kind == "container")
				FactsForContainer(a_info, a_ref, a_base);
			else if (kind == "item")
				FactsForItem(a_info, a_ref, a_base);
			else if (kind == "light")
				FactsForLight(a_info, a_base);
			else if (kind == "tree")
				AddFact(a_info, "Tree record", Describe(a_base));
			else if (kind == "npc")
				if (auto* actor = a_ref->As<RE::Actor>())
					FactsForNpc(a_info, actor);
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

		/**
		 * One worn item (or the skin) as the biped holds it. The armor addon is the one the engine put on this actor; when the biped
		 * slot has none recorded (the head slot of a skin, for one), it is worked out the way the engine does: the armor's addon for
		 * the actor's race and this slot, else for the race alone.
		 */
		WornItem MakeWornItem(const RE::BIPOBJECT& a_object, int a_slot, RE::TESRace* a_race, RE::SEX a_sex, bool a_skinSlot)
		{
			WornItem w;
			w.slots.push_back(a_slot);
			w.itemFormId = a_object.item->GetFormID();
			w.name = Safe(a_object.item->GetName());
			if (w.name.empty()) {
				std::string       source;
				const std::string editor = EditorIdOf(w.itemFormId, a_object.item->GetFormEditorID(), source);
				w.name = editor.empty() ? std::format("(unnamed item {})", Hex8(w.itemFormId)) : editor;
			}
			if (auto* file = a_object.item->GetFile(0))
				w.itemPlugin = std::string(file->GetFilename());
			RE::TESObjectARMA* addon = a_object.addon;
			if (!addon)
				addon = a_object.item->As<RE::TESObjectARMA>();  // some biped slots hold the addon itself as their item
			if (!addon && a_race) {
				if (auto* armor = a_object.item->As<RE::TESObjectARMO>()) {
					addon = armor->GetArmorAddonByMask(a_race, static_cast<RE::BIPED_MODEL::BipedObjectSlot>(1u << (a_slot - 30)));
					if (!addon && !a_skinSlot)
						addon = armor->GetArmorAddon(a_race);  // a skin's head slot has no addon: the head is built from the head parts
				}
			}
			if (addon) {
				w.addonFormId = addon->GetFormID();
				if (auto* file = addon->GetFile(0))
					w.addonPlugin = std::string(file->GetFilename());
				w.model = Safe(addon->bipedModels[a_sex == RE::SEXES::kFemale ? 1 : 0].GetModel());
			}
			if (w.model.empty() && a_object.part)
				w.model = Safe(a_object.part->GetModel());
			if (a_object.partClone) {
				w.live3d = true;
				for (const auto& shape : DescribeParts(a_object.partClone.get()))
					for (const auto& slotTexture : shape.textures)
						w.textures.push_back(ToNpcTexture(shape, slotTexture));
			}
			return w;
		}

		/** Adds the item's slot to the entry already holding the same item and addon, else a new entry. */
		void AddOrMerge(std::vector<WornItem>& a_list, WornItem a_item)
		{
			auto it = std::find_if(a_list.begin(), a_list.end(), [&](const WornItem& w) { return w.itemFormId == a_item.itemFormId && w.addonFormId == a_item.addonFormId; });
			if (it != a_list.end()) {
				it->slots.push_back(a_item.slots.front());
				return;
			}
			a_list.push_back(std::move(a_item));
		}

		/** The skin the actor wears: its own (WNAM), else its race's. A skin is no worn item, so it goes in skinItem, never in worn. */
		RE::TESObjectARMO* SkinOf(RE::TESNPC* a_npc, RE::TESRace* a_race, std::string& a_source)
		{
			if (a_npc->skin) {
				a_source = "the NPC's own skin (WNAM)";
				return a_npc->skin;
			}
			if (a_race && a_race->skin) {
				a_source = "the race's skin";
				return a_race->skin;
			}
			return nullptr;
		}

		void FillWorn(NpcSection& a_npc, RE::Actor* a_actor, RE::SEX a_sex, RE::TESRace* a_race, const RE::TESObjectARMO* a_skin)
		{
			const auto& biped = a_actor->GetBiped();
			if (!biped)
				return;
			for (std::uint32_t i = 0; i < RE::BIPED_OBJECTS::kTotal; ++i) {
				const auto& object = biped->objects[i];
				if (!object.item)
					continue;
				const int  slot = 30 + static_cast<int>(i);
				const bool isSkin = a_skin && object.item == a_skin;
				WornItem   w = MakeWornItem(object, slot, a_race, a_sex, isSkin);
				if (isSkin && !w.addonFormId)
					continue;  // a skin slot with no addon of its own (the head): nothing to show
				AddOrMerge(isSkin ? a_npc.skinItem.parts : a_npc.worn, std::move(w));
			}
			// a slot whose item is an addon (the armor's other slots): fold it into the armor that wears that addon
			auto& worn = a_npc.worn;
			for (std::size_t i = 0; i < worn.size();) {
				const bool addonOnly = worn[i].addonFormId && worn[i].itemFormId == worn[i].addonFormId;
				auto       owner = std::find_if(worn.begin(), worn.end(), [&](const WornItem& o) { return &o != &worn[i] && o.addonFormId == worn[i].addonFormId && o.itemFormId != o.addonFormId; });
				if (addonOnly && owner != worn.end()) {
					owner->slots.insert(owner->slots.end(), worn[i].slots.begin(), worn[i].slots.end());
					std::sort(owner->slots.begin(), owner->slots.end());
					if (!owner->live3d && worn[i].live3d) {
						owner->live3d = true;
						owner->textures = worn[i].textures;
					}
					worn.erase(worn.begin() + static_cast<std::ptrdiff_t>(i));
					continue;
				}
				++i;
			}
		}

		/** The skin's armor addons for this race when the biped holds none of them (nothing of the skin is built yet). No live textures. */
		void FillSkinFromAddons(NpcSection& a_npc, RE::TESObjectARMO* a_skin, RE::TESRace* a_race, RE::SEX a_sex)
		{
			for (auto* addon : a_skin->armorAddons) {
				if (!addon || (a_race && !addon->IsValidRace(a_race)))
					continue;
				WornItem w;
				w.itemFormId = a_skin->GetFormID();
				w.name = a_npc.skinItem.name;
				w.itemPlugin = a_npc.skinItem.plugin;
				w.addonFormId = addon->GetFormID();
				if (auto* file = addon->GetFile(0))
					w.addonPlugin = std::string(file->GetFilename());
				w.model = Safe(addon->bipedModels[a_sex == RE::SEXES::kFemale ? 1 : 0].GetModel());
				for (int bit = 0; bit < 32; ++bit)
					if (addon->bipedModelData.bipedObjectSlots.all(static_cast<RE::BIPED_MODEL::BipedObjectSlot>(1u << bit)))
						w.slots.push_back(30 + bit);
				a_npc.skinItem.parts.push_back(std::move(w));
			}
		}

		void FillHeadParts(NpcSection& a_npc, RE::TESNPC* a_npcBase)
		{
			RE::TESNPC* source = a_npcBase->GetRootFaceNPC();
			if (!source || !source->headParts || source->numHeadParts <= 0)
				source = a_npcBase;
			if (!source->headParts)
				return;
			for (int i = 0; i < source->numHeadParts; ++i) {
				auto* part = source->headParts[i];
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
				default: h.type = "Unknown"; break;
				}
				h.formId = part->GetFormID();
				h.editorId = Safe(part->formEditorID.c_str());  // head parts keep their real editor IDs
				h.name = Safe(part->GetName());
				h.model = Safe(part->GetModel());
				if (auto* set = part->textureSet) {
					h.textureSetFormId = set->GetFormID();
					for (const auto& texture : set->textures) {
						const std::string path = Safe(texture.textureName.c_str());
						if (!path.empty())
							h.textureSetPaths.push_back(path);
					}
				}
				a_npc.headParts.push_back(std::move(h));
			}
		}

		std::string FaceGenName(const RE::TESNPC* a_face, const std::string& a_plugin, bool a_tint)
		{
			const std::uint32_t local = a_face->GetLocalFormID() & 0x00FFFFFFu;
			return std::format("{}\\{}\\{:08x}.{}", a_tint ? "textures\\actors\\character\\facegendata\\facetint" : "meshes\\actors\\character\\facegendata\\facegeom",
				a_plugin, local, a_tint ? "dds" : "nif");
		}

		/**
		 * Is this NPC's face a baked FaceGen file (made in the Creation Kit) or generated when the game ran? The engine looks for
		 * "FaceGeom\<plugin>\<form id>.nif" and "FaceTint\<plugin>\<form id>.dds", in loose files and archives. Absent both, it
		 * builds the face from the head parts and morphs. We probe the same two paths over the plugins that carry the face NPC,
		 * and add the head shape's own FaceGen material values as evidence.
		 */
		void FillFaceGen(NpcSection& a_npc, RE::TESNPC* a_npcBase, const std::vector<ShapeInfo>& a_shapes)
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
			} else {
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
						tintFile ? "FaceTint texture" : "");
				} else {
					g.verdict = kFaceGenMissing;
					g.evidence = "no FaceGeom mesh and no FaceTint texture for this NPC in loose files or archives, so the game builds the face when it loads";
				}
			}
			for (const auto& s : a_shapes) {
				if (s.featureId == 4 || s.featureId == 5) {
					g.liveTintTexture = s.tintTexture;
					g.renderTargetIndex = s.renderTargetIndex;
					break;
				}
			}
		}

		void FillNpc(PickInfo& a_info, RE::Actor* a_actor, RE::NiAVObject* a_root)
		{
			auto* npc = a_actor->GetActorBase();
			if (!npc)
				return;
			auto& n = a_info.npc;
			n.present = true;
			const RE::SEX sex = npc->GetSex();
			n.sex = sex == RE::SEXES::kFemale ? "female" : "male";
			auto* race = a_actor->GetRace();
			if (race) {
				n.race = Describe(race);
				n.skeleton = Safe(race->skeletonModels[sex == RE::SEXES::kFemale ? 1 : 0].GetModel());
			}
			std::string skinSource;
			auto*       skin = SkinOf(npc, race, skinSource);
			if (skin) {
				n.skin = Describe(skin) + (npc->skin ? std::string() : " (the race's skin)");
				n.skinItem.present = true;
				n.skinItem.name = Safe(skin->GetName());
				if (n.skinItem.name.empty()) {
					std::string source;
					n.skinItem.name = EditorIdOf(skin->GetFormID(), skin->GetFormEditorID(), source);
				}
				n.skinItem.formId = skin->GetFormID();
				if (auto* file = skin->GetFile(0))
					n.skinItem.plugin = std::string(file->GetFilename());
				n.skinItem.source = skinSource;
			} else {
				n.skin = "(none)";
			}
			n.skinToneColor = HexColor(npc->bodyTintColor.red, npc->bodyTintColor.green, npc->bodyTintColor.blue);
			FillHeadParts(n, npc);
			FillWorn(n, a_actor, sex, race, skin);
			if (skin && n.skinItem.parts.empty())
				FillSkinFromAddons(n, skin, race, sex);
			FillFaceGen(n, npc, a_root ? DescribeParts(a_root) : std::vector<ShapeInfo>{});
		}

		// ---- parts ------------------------------------------------------------------------------------------------------

		/** File origin for every texture of the clicked part (kept off the other parts to keep a pick quick). */
		void ResolveClickedOrigins(PickInfo& a_info)
		{
			if (a_info.clicked < 0)
				return;
			for (auto& t : a_info.shapes[static_cast<std::size_t>(a_info.clicked)].textures) {
				if (t.path.empty())
					continue;
				std::string path = t.path;
				FileOrigin  origin = ResolveOrigin(path);
				std::string lower = path;
				for (auto& c : lower)
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				if (origin.source == "not found" && lower.rfind("textures", 0) != 0)
					origin = ResolveOrigin("textures\\" + path);
				t.origin = std::move(origin);
			}
		}

		void RunDiagnostics(PickInfo& a_info)
		{
			for (auto& s : a_info.shapes) {
				s.diagnostics = Diagnose(s);
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
							names += (i ? ", " : "") + BodySlotName(w.slots[i]);
						shape.bodyArea = "worn: " + w.name + " (" + names + ")";
						return;
					}
				}
			}
		}
	}

	void FillReport(PickInfo& a_info, RE::TESObjectREFR* a_ref, RE::NiAVObject* a_root)
	{
		a_info.diagnostics.clear();
		try {
			if (!a_ref)
				return;
			auto* base = a_ref->GetBaseObject();
			FillIdentity(a_info, a_ref, base);
			FillFacts(a_info, a_ref, base);
			if (auto* actor = a_ref->As<RE::Actor>())
				FillNpc(a_info, actor, a_root);
			ResolveClickedOrigins(a_info);
			FillBodyAreaFromWorn(a_info);
			RunDiagnostics(a_info);
		} catch (const std::exception& e) {
			spdlog::error("report fill failed for {:08X}: {}", a_ref ? a_ref->GetFormID() : 0u, e.what());
		} catch (...) {
			spdlog::error("report fill failed for {:08X}: unknown exception", a_ref ? a_ref->GetFormID() : 0u);
		}
	}

	FormRecord DescribeRecord(RE::TESForm* a_form)
	{
		std::string source;
		return MakeRecord(a_form, source);
	}

	std::string DescribeForm(RE::TESForm* a_form) { return Describe(a_form); }

	std::string CellDescription(RE::TESObjectCELL* a_cell) { return CellText(a_cell); }
}
