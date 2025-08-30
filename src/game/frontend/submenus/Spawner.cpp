#include "Spawner.hpp"

#include "World/Train.hpp"
#include "World/VehicleSpawner.hpp"
#include "core/commands/Commands.hpp"
#include "core/commands/HotkeySystem.hpp"
#include "core/commands/LoopedCommand.hpp"
#include "game/backend/FiberPool.hpp"
#include "game/backend/NativeHooks.hpp"
#include "game/backend/ScriptMgr.hpp"
#include "game/backend/Self.hpp"
#include "game/frontend/items/Items.hpp"
#include "game/frontend/Menu.hpp"
#include "game/hooks/Hooks.hpp"
#include "game/rdr/Enums.hpp"
#include "game/rdr/Natives.hpp"
#include "game/rdr/Pools.hpp"
#include "game/rdr/data/PedModels.hpp"

#include <algorithm>
#include <game/rdr/Natives.hpp>
#include <rage/fwBasePool.hpp>
#include <rage/pools.hpp>

namespace YimMenu::Submenus
{
	// native hook functions for ped spawning (essential for Set Model)
	// using base code approach - simple hooks that work without multiplayer interference
	void GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH(rage::scrNativeCallContext* ctx)
	{
		if (ctx->GetArg<int>(0) == "mp_intro"_J)
		{
			ctx->SetReturnValue<int>(1);
		}
		else
		{
			ctx->SetReturnValue<int>(SCRIPTS::GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH(ctx->GetArg<int>(0)));
		}
	}

	void _GET_META_PED_TYPE(rage::scrNativeCallContext* ctx)
	{
		ctx->SetReturnValue<int>(4);
	}

	// state management for nested navigation in peds category
	static bool g_InPedDatabase = false;
	static bool g_InHumans = false;
	static bool g_InHorses = false;
	static bool g_InAnimals = false;
	static bool g_InFishes = false;

	// ped details subview (shared window for all peds)
	static bool g_InPedDetailsView = false;
	static int g_SelectedPedIndex = -1;

	// horse gender selection (1 = female, 0 = male)
	static int g_HorseGender = 0;

	// global set model checkbox state for all navigation menus
	static bool g_SetModelMode = false;

	// shared variables for ped spawning
	static std::string g_PedModelBuffer;
	static bool g_Dead, g_Invis, g_Godmode, g_Freeze, g_Companion, g_Sedated, g_Armed, g_BypassRelationship;
	static float g_Scale = 1.0f;
	static int g_Variation = 0;
	static int g_Formation = 0;
	static std::vector<YimMenu::Ped> g_SpawnedPeds;

	// group formations for companion system
	inline std::unordered_map<int, const char*> groupFormations = {{0, "Default"}, {1, "Circle Around Leader"}, {2, "Alternative Circle Around Leader"}, {3, "Line, with Leader at center"}};

	// helper functions from original PedSpawner
	static bool IsPedModelInList(const std::string& model)
	{
		return Data::g_PedModels.contains(Joaat(model));
	}

	static std::string GetDefaultWeaponForPed(const std::string& pedModel)
	{
		auto modelHash = Joaat(pedModel);

		// story character specific weapons
		switch (modelHash)
		{
		case "player_zero"_J: // Arthur
			return "WEAPON_REVOLVER_SCHOFIELD";
		case "player_three"_J: // John
			return "WEAPON_REVOLVER_SCHOFIELD";
		case "cs_micahbell"_J: // Micah
			return "WEAPON_REVOLVER_DOUBLEACTION";
		case "cs_dutch"_J: // Dutch
			return "WEAPON_REVOLVER_SCHOFIELD";
		case "cs_javierescuella"_J: // Javier
			return "WEAPON_REVOLVER_SCHOFIELD";
		case "cs_charlessmith"_J: // Charles
			return "WEAPON_BOW";
		case "cs_mrsadler"_J: // Sadie
			return "WEAPON_REPEATER_WINCHESTER";
		case "CS_miltonandrews"_J: // Milton
			return "WEAPON_PISTOL_MAUSER";
		default:
			// default weapons for generic peds
			return "WEAPON_REVOLVER_CATTLEMAN";
		}
	}

	static int PedSpawnerInputCallback(ImGuiInputTextCallbackData* data)
	{
		if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion)
		{
			std::string newText{};
			std::string inputLower = data->Buf;
			std::transform(inputLower.begin(), inputLower.end(), inputLower.begin(), ::tolower);
			for (const auto& [key, model] : Data::g_PedModels)
			{
				std::string modelLower = model;
				std::transform(modelLower.begin(), modelLower.end(), modelLower.begin(), ::tolower);
				if (modelLower.find(inputLower) != std::string::npos)
				{
					newText = model;
				}
			}

			if (!newText.empty())
			{
				data->DeleteChars(0, data->BufTextLen);
				data->InsertChars(0, newText.c_str());
			}

			return 1;
		}
		return 0;
	}



	static void RenderPedDatabaseView()
	{
		// back button in top-right corner
		ImVec2 windowSize = ImGui::GetContentRegionAvail();
		ImVec2 originalPos = ImGui::GetCursorPos();

		ImGui::SetCursorPos(ImVec2(windowSize.x - 30, 5));
		if (ImGui::Button("X", ImVec2(25, 25)))
		{
			g_InPedDatabase = false;
		}

		// reset cursor to original position and add some top margin
		ImGui::SetCursorPos(ImVec2(originalPos.x, originalPos.y + 35));

		// posse section
		ImGui::PushFont(Menu::Font::g_ChildTitleFont);
		ImGui::Text("Posse");
		ImGui::PopFont();
		ImGui::Separator();
		ImGui::Spacing();
		ImGui::Text("(empty)");
		ImGui::Spacing();

		// database operation section
		ImGui::PushFont(Menu::Font::g_ChildTitleFont);
		ImGui::Text("Database Operation");
		ImGui::PopFont();
		ImGui::Separator();
		ImGui::Spacing();

		// database operation buttons
		if (ImGui::Button("Clear"))
		{
			// completely release ownership and let them behave like regular ambient NPCs
			FiberPool::Push([] {
				for (auto it = g_SpawnedPeds.begin(); it != g_SpawnedPeds.end();)
				{
					if (it->IsValid())
					{
						// remove from player group completely
						PED::REMOVE_PED_FROM_GROUP(it->GetHandle());

						// make them targetable again (like regular NPCs)
						PED::SET_PED_CAN_BE_TARGETTED_BY_PLAYER(it->GetHandle(), YimMenu::Self::GetPlayer().GetId(), true);

						// restore default relationship group based on ped's model
						Hash defaultRel = PED::GET_PED_RELATIONSHIP_GROUP_DEFAULT_HASH(it->GetHandle());
						PED::SET_PED_RELATIONSHIP_GROUP_HASH(it->GetHandle(), defaultRel);

						// clear all tasks so they start fresh AI behavior
						TASK::CLEAR_PED_TASKS_IMMEDIATELY(it->GetHandle(), false, true);

						// mark ped/entity as no longer needed so engine/network can manage it
						{
							auto hnd = it->GetHandle();
							// available in our SDK as PED::DELETE_PED / ENTITY::SET_ENTITY_AS_NO_LONGER_NEEDED; SET_PED_AS_NO_LONGER_NEEDED is not exposed
							ENTITY::SET_ENTITY_AS_NO_LONGER_NEEDED(&hnd);
						}

						// allow normal AI behavior and reactions
						PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(it->GetHandle(), false);

						// reset to default flee attributes (so they can flee like normal NPCs)
						PED::SET_PED_FLEE_ATTRIBUTES(it->GetHandle(), 0, true);

						// remove companion/gang decorators if they exist
						if (DECORATOR::DECOR_EXIST_ON(it->GetHandle(), "SH_CMP_companion"))
							DECORATOR::DECOR_REMOVE(it->GetHandle(), "SH_CMP_companion");
						if (DECORATOR::DECOR_EXIST_ON(it->GetHandle(), "SH_STORY_GANG"))
							DECORATOR::DECOR_REMOVE(it->GetHandle(), "SH_STORY_GANG");
						if (DECORATOR::DECOR_EXIST_ON(it->GetHandle(), "SH_HORSE_MALE"))
							DECORATOR::DECOR_REMOVE(it->GetHandle(), "SH_HORSE_MALE");
					}
					it = g_SpawnedPeds.erase(it);
				}
			});
		}
		ImGui::SameLine();
		if (ImGui::Button("Delete All"))
		{
			// identical behavior to Cleanup Peds: delete every tracked ped and their mounts
			FiberPool::Push([] {
				for (auto it = g_SpawnedPeds.begin(); it != g_SpawnedPeds.end();)
				{
					if (it->IsValid())
					{
						if (it->GetMount())
						{
							it->GetMount().ForceControl();
							if (it->GetMount().HasControl())
								it->GetMount().ForceSync();
							it->GetMount().Delete();
						}
						it->ForceControl();
						if (it->HasControl())
							it->ForceSync();
						it->Delete();
					}
					it = g_SpawnedPeds.erase(it);
				}
			});
		}
		ImGui::SameLine();
		if (ImGui::Button("Delete Dead"))
		{
			// same as Cleanup Peds but only for peds that are dead
			FiberPool::Push([] {
				for (auto it = g_SpawnedPeds.begin(); it != g_SpawnedPeds.end();)
				{
					bool erased = false;
					if (it->IsValid() && it->IsDead())
					{
						if (it->GetMount())
						{
							it->GetMount().ForceControl();
							if (it->GetMount().HasControl())
								it->GetMount().ForceSync();
							it->GetMount().Delete();
						}
						it->ForceControl();
						if (it->HasControl())
							it->ForceSync();
						it->Delete();
						it = g_SpawnedPeds.erase(it);
						erased = true;
					}
					if (!erased)
						++it;
				}
			});
		}

		// list of spawned peds
		ImGui::Text("Spawned Peds:");

		// clean up invalid peds first
		for (auto it = g_SpawnedPeds.begin(); it != g_SpawnedPeds.end();)
		{
			if (!it->IsValid())
			{
				it = g_SpawnedPeds.erase(it);
			}
			else
			{
				++it;
			}
		}

		// display the list of valid spawned peds
		if (g_SpawnedPeds.empty())
		{
			ImGui::Text("No peds spawned");
		}
		else
		{
			for (size_t i = 0; i < g_SpawnedPeds.size(); i++)
			{
				auto& ped = g_SpawnedPeds[i];
				if (ped.IsValid())
				{
					// get model hash and look up name
					auto modelHash = ped.GetModel();
					auto it = Data::g_PedModels.find(modelHash);
					std::string pedName = (it != Data::g_PedModels.end()) ? it->second : "Unknown";
					std::string entry = std::to_string(i + 1) + ". " + pedName;
					if (ImGui::Selectable(entry.c_str()))
					{
						g_SelectedPedIndex = static_cast<int>(i);
						g_InPedDetailsView = true; // go into shared ped window
					}
				}
			}
		}
	}

		static void RenderPedDetailsView()
		{
			// back button in top-right corner
			ImVec2 windowSize = ImGui::GetContentRegionAvail();
			ImVec2 originalPos = ImGui::GetCursorPos();

			ImGui::SetCursorPos(ImVec2(windowSize.x - 30, 5));
			if (ImGui::Button("X", ImVec2(25, 25)))
			{
				// close details view and return to ped database
				g_InPedDetailsView = false;
			}

			// reset cursor to original position and add some top margin
			ImGui::SetCursorPos(ImVec2(originalPos.x, originalPos.y + 35));

			// header showing selected ped (if valid)
			ImGui::PushFont(Menu::Font::g_ChildTitleFont);
			std::string header = "Ped Details";
			if (g_SelectedPedIndex >= 0 && g_SelectedPedIndex < static_cast<int>(g_SpawnedPeds.size()))
			{
				auto& ped = g_SpawnedPeds[g_SelectedPedIndex];
				if (ped.IsValid())
				{
					auto modelHash = ped.GetModel();
					auto it = Data::g_PedModels.find(modelHash);
					std::string pedName = (it != Data::g_PedModels.end()) ? it->second : "Unknown";
					header += " - " + pedName;
				}
			}
			ImGui::Text("%s", header.c_str());
			ImGui::PopFont();
			ImGui::Separator();
			ImGui::Spacing();

			// empty content for now (shared window for all peds)
			ImGui::Text("(empty)");
		}


	// reusable search helper system for all navigation menus
	template<typename T>
	struct SearchHelper
	{
		std::string searchBuffer;

		// core search matching function
		static bool MatchesSearch(const std::string& text, const std::string& searchTerm)
		{
			if (searchTerm.empty())
				return true;

			std::string textLower = text;
			std::string searchLower = searchTerm;
			std::transform(textLower.begin(), textLower.end(), textLower.begin(), ::tolower);
			std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

			return textLower.find(searchLower) != std::string::npos;
		}

		// check if section name matches search
		static bool SectionMatches(const std::string& sectionName, const std::string& searchTerm)
		{
			return MatchesSearch(sectionName, searchTerm);
		}

		// count matching items in a collection
		template<typename Container, typename GetNameFunc>
		static int CountMatches(const Container& items, const std::string& searchTerm, GetNameFunc getName)
		{
			if (searchTerm.empty())
				return static_cast<int>(items.size());

			int count = 0;
			for (const auto& item : items)
			{
				if (MatchesSearch(getName(item), searchTerm))
					count++;
			}
			return count;
		}

		// render search bar with count display and optional gender selection for horses
		void RenderSearchBar(const std::string& placeholder, int totalItems, int visibleItems, bool showGenderSelection = false)
		{
			// get available width for proper alignment
			ImVec2 contentRegion = ImGui::GetContentRegionAvail();
			float setModelCheckboxWidth = 100.0f; // approximate width for "Set Model" checkbox

			// consistent search bar size for all sections (accommodate gender buttons when needed)
			ImGui::SetNextItemWidth(200.0f);
			InputTextWithHint(("##search_" + placeholder).c_str(), placeholder.c_str(), &searchBuffer).Draw();

			// gender radio buttons for horses (on same line as search bar)
			if (showGenderSelection)
			{
				ImGui::SameLine();
				ImGui::Text("Gender:");
				ImGui::SameLine();
				ImGui::RadioButton("Male", &g_HorseGender, 0);
				ImGui::SameLine();
				ImGui::RadioButton("Female", &g_HorseGender, 1);
			}

			// set model checkbox aligned to the very right
			ImGui::SameLine();
			float currentX = ImGui::GetCursorPosX();
			float targetX = contentRegion.x - setModelCheckboxWidth;
			if (targetX > currentX)
			{
				ImGui::SetCursorPosX(targetX);
			}
			ImGui::Checkbox("Set Model", &g_SetModelMode);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("When checked, clicking spawn buttons will change your player model instead of spawning");

			if (searchBuffer.empty())
			{
				ImGui::Text("Total: %d items", totalItems);
			}
			else
			{
				ImGui::Text("Found: %d items", visibleItems);
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
		}

		// check if item should be visible based on search criteria
		template<typename Item, typename GetNameFunc>
		bool ShouldShowItem(const Item& item, bool sectionMatches, GetNameFunc getName) const
		{
			return sectionMatches || MatchesSearch(getName(item), searchBuffer);
		}
	};

	// search instances for each navigation menu
	static SearchHelper<void> g_AnimalSearch;  // handles both legendary and regular animals
	static SearchHelper<void> g_HumanSearch;  // placeholder for future use
	static SearchHelper<void> g_HorseSearch;  // placeholder for future use
	static SearchHelper<void> g_FishSearch;   // placeholder for future use

	// forward declaration
	static void SetHorseGender(Ped horse, int gender);

	// function to set player model (based on existing "Set Model" button logic)
	static void SetPlayerModel(const std::string& model, int variation = 0, bool isHorse = false)
	{
		FiberPool::Push([model, variation, isHorse] {
			auto modelHash = Joaat(model);

			for (int i = 0; i < 30 && !STREAMING::HAS_MODEL_LOADED(modelHash); i++)
			{
				STREAMING::REQUEST_MODEL(modelHash, false);
				ScriptMgr::Yield();
			}

			PLAYER::SET_PLAYER_MODEL(Self::GetPlayer().GetId(), modelHash, false);
			Self::Update();

			if (variation > 0)
				Self::GetPed().SetVariation(variation);
			else
				PED::_SET_RANDOM_OUTFIT_VARIATION(Self::GetPed().GetHandle(), true);

			// track model and variation for automatic session fix
			Hooks::Info::UpdateStoredPlayerModel(modelHash, variation);

			// apply horse gender if this is a horse (after variation is set)
			if (isHorse)
			{
				SetHorseGender(Self::GetPed(), g_HorseGender);
			}

			// give weapon if armed is enabled and ped is not an animal
			if (g_Armed && !Self::GetPed().IsAnimal())
			{
				auto weapon = GetDefaultWeaponForPed(model);
				WEAPON::GIVE_WEAPON_TO_PED(Self::GetPed().GetHandle(), Joaat(weapon), 100, true, false, 0, true, 0.5f, 1.0f, 0x2CD419DC, true, 0.0f, false);
				WEAPON::SET_PED_INFINITE_AMMO(Self::GetPed().GetHandle(), true, Joaat(weapon));
				ScriptMgr::Yield();
				WEAPON::SET_CURRENT_PED_WEAPON(Self::GetPed().GetHandle(), "WEAPON_UNARMED"_J, true, 0, false, false);
			}

			STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(modelHash);
		});
	}

	// function to set horse gender using discovered community natives
	static void SetHorseGender(Ped horse, int gender)
	{
		if (!horse || !horse.IsValid())
			return;

		bool isFemale = (gender == 1);

		// using discovered native from community: 0x5653AB26C82938CF (_SET_PED_FACE_FEATURE)
		// with specific horse gender index 0xA28B from RDR3 face features.txt
		// 0xA28B - horse gender (1.0 = female, 0.0 = male)

		try {
			// mark gender with decorator for tracking
			DECORATOR::DECOR_SET_BOOL(horse.GetHandle(), "SH_HORSE_MALE", !isFemale);

			// use the correct horse gender face feature index
			auto invoker = YimMenu::NativeInvoker{};

			// call _SET_PED_FACE_FEATURE with the correct horse gender index
			invoker.BeginCall();
			invoker.PushArg(horse.GetHandle());
			invoker.PushArg(0xA28B); // horse gender index from RDR3 face features.txt
			invoker.PushArg(isFemale ? 1.0f : 0.0f); // 0.0 = male, 1.0 = female
			Pointers.GetNativeHandler(0x5653AB26C82938CF)(&invoker.m_CallContext);

			// 0xCC8CA3E88256E58F with parameters: ped, false, true, true, true, false
			invoker.BeginCall();
			invoker.PushArg(horse.GetHandle());
			invoker.PushArg(false);
			invoker.PushArg(true);
			invoker.PushArg(true);
			invoker.PushArg(true);
			invoker.PushArg(false);
			Pointers.GetNativeHandler(0xCC8CA3E88256E58F)(&invoker.m_CallContext);

		} catch (...) {
			// if any approach fails, continue without crashing
		}
	}

	// unified ped spawning function - used by all spawn buttons
	static void SpawnPed(const std::string& model, int variation, bool giveWeapon = false, bool isStoryGang = false, bool isHorse = false)
	{
		FiberPool::Push([model, variation, giveWeapon, isStoryGang, isHorse] {
			auto ped = Ped::Create(Joaat(model), Self::GetPed().GetPosition());

			if (!ped)
				return;

			ped.SetFrozen(g_Freeze);

			if (g_Dead)
			{
				ped.Kill();
				if (ped.IsAnimal())
					ped.SetQuality(2);
			}

			ped.SetInvincible(g_Godmode);

			// global relationship bypass: apply before godmode, but do not affect companion or story gang
			if (g_BypassRelationship && !g_Companion && !isStoryGang)
			{
				PED::SET_PED_RELATIONSHIP_GROUP_HASH(ped.GetHandle(), Joaat("NO_RELATIONSHIP"));
			}

			// enhanced godmode logic - creates formidable boss-level threats
			if (g_Godmode)
			{
				// === CORE PROTECTION ===
				// anti ragdoll
				ped.SetRagdoll(false);
				// anti lasso
				PED::SET_PED_LASSO_HOGTIE_FLAG(ped.GetHandle(), (int)LassoFlags::LHF_CAN_BE_LASSOED, false);
				PED::SET_PED_LASSO_HOGTIE_FLAG(ped.GetHandle(), (int)LassoFlags::LHF_CAN_BE_LASSOED_BY_FRIENDLY_AI, false);
				PED::SET_PED_LASSO_HOGTIE_FLAG(ped.GetHandle(), (int)LassoFlags::LHF_CAN_BE_LASSOED_BY_FRIENDLY_PLAYERS, false);
				PED::SET_PED_LASSO_HOGTIE_FLAG(ped.GetHandle(), (int)LassoFlags::LHF_DISABLE_IN_MP, true);
				// anti hogtie
				ENTITY::_SET_ENTITY_CARRYING_FLAG(ped.GetHandle(), (int)CarryingFlags::CARRYING_FLAG_CAN_BE_HOGTIED, false);

				// === COMBAT ATTRIBUTES (ALL from Rampage Trainer) ===
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 0, true);   // CA_USE_COVER - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 1, true);   // CA_USE_VEHICLE - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 2, true);   // CA_DO_DRIVEBYS - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 3, true);   // CA_LEAVE_VEHICLES - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 4, true);   // CA_STRAFE_BASED_ON_TARGET_PROXIMITY - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 5, false);   // CA_ALWAYS_FIGHT - disable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 8, true);   // CA_ALLOW_STRAFE_BREAKUP - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 24, true);  // CA_USE_PROXIMITY_FIRING_RATE - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 31, true);  // CA_MAINTAIN_MIN_DISTANCE_TO_TARGET - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 38, true);  // CA_DISABLE_BULLET_REACTIONS - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 46, true);  // CA_CAN_FIGHT_ARMED_PEDS_WHEN_NOT_ARMED - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 50, true);  // CA_CAN_CHARGE - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 57, true);  // CA_DISABLE_SEEK_DUE_TO_LINE_OF_SIGHT - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 78, true);  // CA_DISABLE_ALL_RANDOMS_FLEE - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 113, true); // CA_USE_INFINITE_CLIPS - enable
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 125, true); // CA_QUIT_WHEN_TARGET_FLEES_INTERACTION_FIGHT - enable

				// DISABLED attributes
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 6, false);   // CA_FLEE_WHILST_IN_VEHICLE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 7, false);   // CA_JUST_FOLLOW_VEHICLE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 9, false);   // CA_WILL_SCAN_FOR_DEAD_PEDS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 10, false);  // CA_0x793BF941
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 11, false);  // CA_JUST_SEEK_COVER
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 12, false);  // CA_BLIND_FIRE_IN_COVER
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 13, false);  // CA_COVER_SEARCH_IN_ARC_AWAY_FROM_TARGET
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 14, false);  // CA_CAN_INVESTIGATE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 15, false);  // CA_CAN_USE_RADIO
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 16, false);  // CA_STRAFE_DUE_TO_BULLET_EVENTS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 17, false);  // CA_ALWAYS_FLEE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 18, false);  // CA_0x934F1825
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 19, false);  // CA_0x70F392F0
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 20, false);  // CA_CAN_TAUNT_IN_VEHICLE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 21, false);  // CA_CAN_CHASE_TARGET_ON_FOOT
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 22, false);  // CA_WILL_DRAG_INJURED_PEDS_TO_SAFETY
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 23, false);  // CA_0x42843828
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 25, false);  // CA_DISABLE_SECONDARY_TARGET
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 26, false);  // CA_DISABLE_ENTRY_REACTIONS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 27, false);  // CA_PERFECT_ACCURACY
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 28, false);  // CA_CAN_USE_FRUSTRATED_ADVANCE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 29, false);  // CA_MOVE_TO_LOCATION_BEFORE_COVER_SEARCH
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 30, false);  // CA_CAN_SHOOT_WITHOUT_LOS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 32, false);  // CA_0xBC6BB720
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 33, false);  // CA_0x8D3F251D
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 34, false);  // CA_ALLOW_PROJECTILE_SWAPS_AFTER_REPEATED_THROWS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 35, false);  // CA_DISABLE_PINNED_DOWN
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 36, false);  // CA_DISABLE_PIN_DOWN_OTHERS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 37, false);  // CA_OPEN_COMBAT_WHEN_DEFENSIVE_AREA_IS_REACHED
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 39, false);  // CA_CAN_BUST
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 40, false);  // CA_IGNORED_BY_OTHER_PEDS_WHEN_WANTED
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 41, false);  // CA_CAN_COMMANDEER_VEHICLES
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 42, false);  // CA_CAN_FLANK
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 43, false);  // CA_SWITCH_TO_ADVANCE_IF_CANT_FIND_COVER
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 44, false);  // CA_SWITCH_TO_DEFENSIVE_IF_IN_COVER
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 45, false);  // CA_CLEAR_PRIMARY_DEFENSIVE_AREA_WHEN_REACHED
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 47, false);  // CA_ENABLE_TACTICAL_POINTS_WHEN_DEFENSIVE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 48, false);  // CA_DISABLE_COVER_ARC_ADJUSTMENTS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 49, false);  // CA_USE_ENEMY_ACCURACY_SCALING
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 51, false);  // CA_REMOVE_AREA_SET_WILL_ADVANCE_WHEN_DEFENSIVE_AREA_REACHED
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 52, false);  // CA_USE_VEHICLE_ATTACK
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 53, false);  // CA_USE_VEHICLE_ATTACK_IF_VEHICLE_HAS_MOUNTED_GUNS
				//PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 54, false);  // CA_ALWAYS_EQUIP_BEST_WEAPON
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 55, false);  // CA_CAN_SEE_UNDERWATER_PEDS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 56, false);  // CA_DISABLE_AIM_AT_AI_TARGETS_IN_HELIS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 58, true);  // CA_DISABLE_FLEE_FROM_COMBAT
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 59, false);  // CA_DISABLE_TARGET_CHANGES_DURING_VEHICLE_PURSUIT
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 60, false);  // CA_CAN_THROW_SMOKE_GRENADE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 61, false);  // CA_UNUSED6
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 62, false);  // CA_CLEAR_AREA_SET_DEFENSIVE_IF_DEFENSIVE_CANNOT_BE_REACHED
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 63, false);  // CA_UNUSED7
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 64, false);  // CA_DISABLE_BLOCK_FROM_PURSUE_DURING_VEHICLE_CHASE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 65, false);  // CA_DISABLE_SPIN_OUT_DURING_VEHICLE_CHASE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 66, false);  // CA_DISABLE_CRUISE_IN_FRONT_DURING_BLOCK_DURING_VEHICLE_CHASE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 67, false);  // CA_CAN_IGNORE_BLOCKED_LOS_WEIGHTING
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 68, false);  // CA_DISABLE_REACT_TO_BUDDY_SHOT
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 69, false);  // CA_PREFER_NAVMESH_DURING_VEHICLE_CHASE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 70, false);  // CA_ALLOWED_TO_AVOID_OFFROAD_DURING_VEHICLE_CHASE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 71, false);  // CA_PERMIT_CHARGE_BEYOND_DEFENSIVE_AREA
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 72, false);  // CA_USE_ROCKETS_AGAINST_VEHICLES_ONLY
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 73, false);  // CA_DISABLE_TACTICAL_POINTS_WITHOUT_CLEAR_LOS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 74, false);  // CA_DISABLE_PULL_ALONGSIDE_DURING_VEHICLE_CHASE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 75, false);  // CA_0xB53C7137
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 76, false);  // CA_UNUSED8
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 77, false);  // CA_DISABLE_RESPONDED_TO_THREAT_BROADCAST
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 79, false);  // CA_WILL_GENERATE_DEAD_PED_SEEN_SCRIPT_EVENTS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 80, false);  // CA_UNUSED9
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 81, false);  // CA_FORCE_STRAFE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 82, false);  // CA_UNUSED10
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 83, false);  // CA_0x2060C16F
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 84, false);  // CA_0x98669E6C
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 85, false);  // CA_0x6E44A6F2
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 86, false);  // CA_0xC6A191DB
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 87, false);  // CA_0x57C8EF37
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 88, false);  // CA_0xA265A9FC
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 89, false);  // CA_0xE3FA8ABB
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 90, false);  // CA_0x9AA00FOF
				//PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 91, false);  // CA_USE_RANGE_BASED_WEAPON_SELECTION
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 92, false);  // CA_0x8AF8D68D
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 93, false);  // CA_PREFER_MELEE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 94, false);  // CA_UNUSED11
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 95, false);  // CA_UNUSED12
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 96, false);  // CA_0x64BBB208
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 97, false);  // CA_0x625F4C52
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 98, false);  // CA_0x945B1F0C
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 99, false);  // CA_UNUSED13
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 100, false); // CA_UNUSED14
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 101, false); // CA_RESTRICT_IN_VEHICLE_AIMING_TO_CURRENT_SIDE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 102, false); // CA_UNUSED15
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 103, false); // CA_UNUSED16
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 104, false); // CA_UNUSED17
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 105, false); // CA_CAN_CRUISE_AND_BLOCK_IN_VEHICLE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 106, false); // CA_PREFER_AIR_COMBAT_WHEN_IN_AIRCRAFT
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 107, false); // CA_ALLOW_DOG_FIGHTING
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 108, false); // CA_PREFER_NON_AIRCRAFT_TARGETS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 109, false); // CA_PREFER_KNOWN_TARGETS_WHEN_COMBAT_CLOSEST_TARGET
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 110, false); // CA_0x875B82F3
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 111, false); // CA_0x1CB77C49
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 112, false); // CA_0x8EB01547
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 114, false); // CA_CAN_EXECUTE_TARGET
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 115, false); // CA_DISABLE_RETREAT_DUE_TO_TARGET_PROXIMITY
				//PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 116, false); // CA_PREFER_DUAL_WIELD
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 117, false); // CA_WILL_CUT_FREE_HOGTIED_PEDS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 118, false); // CA_TRY_TO_FORCE_SURRENDER
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 119, false); // CA_0x0136E7B6
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 120, false); // CA_0x797D7A1A
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 121, false); // CA_0x97B4A6E4
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 122, false); // CA_0x1FAAD7AF
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 123, false); // CA_0x492B880F
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 124, false); // CA_0xBE151581
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 126, false); // CA_0xAC5E5497
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 127, false); // CA_0xE300164C
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 128, false); // CA_0xC82D4787
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 129, false); // CA_0x31E0808F
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 130, false); // CA_0x0A9A7130
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 131, false); // CA_PREVENT_UNSAFE_PROJECTILE_THROWS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 132, false); // CA_0xA55AD510
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 133, false); // CA_DISABLE_BLANK_SHOTS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 134, false); // CA_0xA78BB3BD

				PED::SET_PED_ACCURACY(ped.GetHandle(), 85);
			}

			ped.SetVisible(!g_Invis);

			if (g_Scale != 1.0f)
				ped.SetScale(g_Scale);

			// apply variation
			ped.SetVariation(variation);

			// apply horse gender if this is a horse (AFTER variation is set)
			if (isHorse)
			{
				SetHorseGender(ped, g_HorseGender);
			}

			// give weapon if requested and ped is not an animal
			if (giveWeapon && g_Armed && !ped.IsAnimal())
			{
				auto weapon = GetDefaultWeaponForPed(model);
				WEAPON::GIVE_WEAPON_TO_PED(ped.GetHandle(), Joaat(weapon), 100, true, false, 0, true, 0.5f, 1.0f, 0x2CD419DC, true, 0.0f, false);
				WEAPON::SET_PED_INFINITE_AMMO(ped.GetHandle(), true, Joaat(weapon));
				ScriptMgr::Yield();
				WEAPON::SET_CURRENT_PED_WEAPON(ped.GetHandle(), "WEAPON_UNARMED"_J, true, 0, false, false);
			}

			ped.SetConfigFlag(PedConfigFlag::IsTranquilized, g_Sedated);

			g_SpawnedPeds.push_back(ped);

			if (g_Companion)
			{
				int group = PED::GET_PED_GROUP_INDEX(YimMenu::Self::GetPed().GetHandle());
				if (!PED::DOES_GROUP_EXIST(group))
				{
					group = PED::CREATE_GROUP(0);
					PED::SET_PED_AS_GROUP_LEADER(YimMenu::Self::GetPed().GetHandle(), group, false);
				}

				// === ENHANCED COMBAT ATTRIBUTES ===
				// core combat behavior - enable aggressive combat
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 0, true);   // CA_USE_COVER - enable tactical cover usage
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 1, true);   // CA_USE_VEHICLE - enable vehicle combat
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 2, true);   // CA_DO_DRIVEBYS - enable drive-by attacks
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 3, true);   // CA_LEAVE_VEHICLES - enable vehicle exit for combat
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 4, true);   // CA_STRAFE_BASED_ON_TARGET_PROXIMITY - enable tactical movement
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 5, true);   // CA_ALWAYS_FIGHT - enable aggressive combat stance
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 8, true);   // CA_ALLOW_STRAFE_BREAKUP - enable advanced movement
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 21, true);  // CA_CAN_CHASE_TARGET_ON_FOOT - enable pursuit
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 24, true);  // CA_USE_PROXIMITY_FIRING_RATE - enable smart firing
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 27, true);  // CA_PERFECT_ACCURACY - enable enhanced accuracy
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 31, true);  // CA_MAINTAIN_MIN_DISTANCE_TO_TARGET - enable tactical positioning
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 38, true);  // CA_DISABLE_BULLET_REACTIONS - reduce flinching
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 41, true);  // CA_CAN_COMMANDEER_VEHICLES - enable vehicle hijacking
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 42, true);  // CA_CAN_FLANK - enable flanking maneuvers
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 46, true);  // CA_CAN_FIGHT_ARMED_PEDS_WHEN_NOT_ARMED - enable unarmed combat
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 50, true);  // CA_CAN_CHARGE - enable charging attacks
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 57, true);  // CA_DISABLE_SEEK_DUE_TO_LINE_OF_SIGHT - ignore LOS limitations
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 58, true);  // CA_DISABLE_FLEE_FROM_COMBAT - never flee from combat
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 78, true);  // CA_DISABLE_ALL_RANDOMS_FLEE - disable all flee behaviors
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 93, true);  // CA_PREFER_MELEE - prefer melee combat for animals
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 113, true); // CA_USE_INFINITE_CLIPS - infinite ammo
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 114, true); // CA_CAN_EXECUTE_TARGET - enable execution moves
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 115, true); // CA_DISABLE_RETREAT_DUE_TO_TARGET_PROXIMITY - never retreat
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 125, true); // CA_QUIT_WHEN_TARGET_FLEES_INTERACTION_FIGHT - continue fighting
				//PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 91, true);  // CA_USE_RANGE_BASED_WEAPON_SELECTION

				// === DISABLED COMBAT ATTRIBUTES ===
				// disable flee behaviors - companions never flee
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 6, false);   // CA_FLEE_WHILST_IN_VEHICLE - disable vehicle fleeing
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 7, false);   // CA_JUST_FOLLOW_VEHICLE - disable passive following
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 17, false);  // CA_ALWAYS_FLEE - disable all fleeing

				// disable investigation and passive behaviors
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 9, false);   // CA_WILL_SCAN_FOR_DEAD_PEDS - focus on combat
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 10, false);  // CA_0x793BF941
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 11, false);  // CA_JUST_SEEK_COVER - don't just hide
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 12, false);  // CA_BLIND_FIRE_IN_COVER - use aimed shots
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 13, false);  // CA_COVER_SEARCH_IN_ARC_AWAY_FROM_TARGET
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 14, false);  // CA_CAN_INVESTIGATE - focus on combat
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 15, false);  // CA_CAN_USE_RADIO - no radio calls
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 16, false);  // CA_STRAFE_DUE_TO_BULLET_EVENTS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 18, false);  // CA_0x934F1825
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 19, false);  // CA_0x70F392F0
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 20, false);  // CA_CAN_TAUNT_IN_VEHICLE - focus on combat
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 22, false);  // CA_WILL_DRAG_INJURED_PEDS_TO_SAFETY - focus on combat
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 23, false);  // CA_0x42843828
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 25, false);  // CA_DISABLE_SECONDARY_TARGET
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 26, false);  // CA_DISABLE_ENTRY_REACTIONS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 28, false);  // CA_CAN_USE_FRUSTRATED_ADVANCE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 29, false);  // CA_MOVE_TO_LOCATION_BEFORE_COVER_SEARCH
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 30, false);  // CA_CAN_SHOOT_WITHOUT_LOS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 32, false);  // CA_0xBC6BB720
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 33, false);  // CA_0x8D3F251D
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 34, false);  // CA_ALLOW_PROJECTILE_SWAPS_AFTER_REPEATED_THROWS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 35, false);  // CA_DISABLE_PINNED_DOWN
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 36, false);  // CA_DISABLE_PIN_DOWN_OTHERS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 37, false);  // CA_OPEN_COMBAT_WHEN_DEFENSIVE_AREA_IS_REACHED
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 39, false);  // CA_CAN_BUST - no arrests
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 40, false);  // CA_IGNORED_BY_OTHER_PEDS_WHEN_WANTED
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 43, false);  // CA_SWITCH_TO_ADVANCE_IF_CANT_FIND_COVER
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 44, false);  // CA_SWITCH_TO_DEFENSIVE_IF_IN_COVER
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 45, false);  // CA_CLEAR_PRIMARY_DEFENSIVE_AREA_WHEN_REACHED
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 47, false);  // CA_ENABLE_TACTICAL_POINTS_WHEN_DEFENSIVE
				// disable remaining attributes for focused combat behavior
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 48, false);  // CA_DISABLE_COVER_ARC_ADJUSTMENTS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 49, false);  // CA_USE_ENEMY_ACCURACY_SCALING
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 51, false);  // CA_REMOVE_AREA_SET_WILL_ADVANCE_WHEN_DEFENSIVE_AREA_REACHED
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 52, false);  // CA_USE_VEHICLE_ATTACK
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 53, false);  // CA_USE_VEHICLE_ATTACK_IF_VEHICLE_HAS_MOUNTED_GUNS
				//PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 54, false);  // CA_ALWAYS_EQUIP_BEST_WEAPON
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 55, false);  // CA_CAN_SEE_UNDERWATER_PEDS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 56, false);  // CA_DISABLE_AIM_AT_AI_TARGETS_IN_HELIS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 59, false);  // CA_DISABLE_TARGET_CHANGES_DURING_VEHICLE_PURSUIT
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 60, false);  // CA_CAN_THROW_SMOKE_GRENADE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 61, false);  // CA_UNUSED6
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 62, false);  // CA_CLEAR_AREA_SET_DEFENSIVE_IF_DEFENSIVE_CANNOT_BE_REACHED
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 63, false);  // CA_UNUSED7
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 64, false);  // CA_DISABLE_BLOCK_FROM_PURSUE_DURING_VEHICLE_CHASE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 65, false);  // CA_DISABLE_SPIN_OUT_DURING_VEHICLE_CHASE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 66, false);  // CA_DISABLE_CRUISE_IN_FRONT_DURING_BLOCK_DURING_VEHICLE_CHASE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 67, false);  // CA_CAN_IGNORE_BLOCKED_LOS_WEIGHTING
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 68, false);  // CA_DISABLE_REACT_TO_BUDDY_SHOT
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 69, false);  // CA_PREFER_NAVMESH_DURING_VEHICLE_CHASE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 70, false);  // CA_ALLOWED_TO_AVOID_OFFROAD_DURING_VEHICLE_CHASE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 71, false);  // CA_PERMIT_CHARGE_BEYOND_DEFENSIVE_AREA
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 72, false);  // CA_USE_ROCKETS_AGAINST_VEHICLES_ONLY
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 73, false);  // CA_DISABLE_TACTICAL_POINTS_WITHOUT_CLEAR_LOS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 74, false);  // CA_DISABLE_PULL_ALONGSIDE_DURING_VEHICLE_CHASE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 75, false);  // CA_0xB53C7137
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 76, false);  // CA_UNUSED8
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 77, false);  // CA_DISABLE_RESPONDED_TO_THREAT_BROADCAST
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 79, false);  // CA_WILL_GENERATE_DEAD_PED_SEEN_SCRIPT_EVENTS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 80, false);  // CA_UNUSED9
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 81, false);  // CA_FORCE_STRAFE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 82, false);  // CA_UNUSED10
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 83, false);  // CA_0x2060C16F
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 84, false);  // CA_0x98669E6C
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 85, false);  // CA_0x6E44A6F2
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 86, false);  // CA_0xC6A191DB
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 87, false);  // CA_0x57C8EF37
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 88, false);  // CA_0xA265A9FC
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 89, false);  // CA_0xE3FA8ABB
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 90, false);  // CA_0x9AA00FOF
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 92, false);  // CA_0x8AF8D68D
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 94, false);  // CA_UNUSED11
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 95, false);  // CA_UNUSED12
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 96, false);  // CA_0x64BBB208
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 97, false);  // CA_0x625F4C52
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 98, false);  // CA_0x945B1F0C
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 99, false);  // CA_UNUSED13
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 100, false); // CA_UNUSED14
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 101, false); // CA_RESTRICT_IN_VEHICLE_AIMING_TO_CURRENT_SIDE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 102, false); // CA_UNUSED15
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 103, false); // CA_UNUSED16
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 104, false); // CA_UNUSED17
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 105, false); // CA_CAN_CRUISE_AND_BLOCK_IN_VEHICLE
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 106, false); // CA_PREFER_AIR_COMBAT_WHEN_IN_AIRCRAFT
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 107, false); // CA_ALLOW_DOG_FIGHTING
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 108, false); // CA_PREFER_NON_AIRCRAFT_TARGETS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 109, false); // CA_PREFER_KNOWN_TARGETS_WHEN_COMBAT_CLOSEST_TARGET
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 110, false); // CA_0x875B82F3
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 111, false); // CA_0x1CB77C49
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 112, false); // CA_0x8EB01547
				//PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 116, false); // CA_PREFER_DUAL_WIELD
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 117, false); // CA_WILL_CUT_FREE_HOGTIED_PEDS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 118, false); // CA_TRY_TO_FORCE_SURRENDER
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 119, false); // CA_0x0136E7B6
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 120, false); // CA_0x797D7A1A
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 121, false); // CA_0x97B4A6E4
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 122, false); // CA_0x1FAAD7AF
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 123, false); // CA_0x492B880F
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 124, false); // CA_0xBE151581
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 126, false); // CA_0xAC5E5497
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 127, false); // CA_0xE300164C
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 128, false); // CA_0xC82D4787
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 129, false); // CA_0x31E0808F
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 130, false); // CA_0x0A9A7130
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 131, false); // CA_PREVENT_UNSAFE_PROJECTILE_THROWS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 132, false); // CA_0xA55AD510
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 133, false); // CA_DISABLE_BLANK_SHOTS
				PED::SET_PED_COMBAT_ATTRIBUTES(ped.GetHandle(), 134, false); // CA_0xA78BB3BD

				// enhanced perception for enemy detection
				PED::SET_PED_SEEING_RANGE(ped.GetHandle(), 100.0f);
				PED::SET_PED_HEARING_RANGE(ped.GetHandle(), 100.0f);

				// === ENHANCED GROUP AND RELATIONSHIP SETUP ===
				ENTITY::SET_ENTITY_AS_MISSION_ENTITY(ped.GetHandle(), true, true);
				PED::SET_PED_AS_GROUP_MEMBER(ped.GetHandle(), group);
				PED::SET_PED_CAN_BE_TARGETTED_BY_PLAYER(ped.GetHandle(), YimMenu::Self::GetPlayer().GetId(), false);

				// === ENHANCED RELATIONSHIP SETUP ===
				// set companion to same relationship group as player
				PED::SET_PED_RELATIONSHIP_GROUP_HASH(
				    ped.GetHandle(), PED::GET_PED_RELATIONSHIP_GROUP_HASH(YimMenu::Self::GetPed().GetHandle()));

				// ensure positive relationship with player
				auto playerGroup = PED::GET_PED_RELATIONSHIP_GROUP_HASH(YimMenu::Self::GetPed().GetHandle());
				auto companionGroup = PED::GET_PED_RELATIONSHIP_GROUP_HASH(ped.GetHandle());

				// set mutual respect/like relationship
				PED::SET_RELATIONSHIP_BETWEEN_GROUPS(2, companionGroup, playerGroup); // LIKE
				PED::SET_RELATIONSHIP_BETWEEN_GROUPS(2, playerGroup, companionGroup); // LIKE

				// enhanced group formation and coordination
				PED::SET_GROUP_FORMATION(PED::GET_PED_GROUP_INDEX(ped.GetHandle()), g_Formation);
				// custom spacing: 3m from leader, 2m between companions, 2m formation spread
				PED::SET_GROUP_FORMATION_SPACING(PED::GET_PED_GROUP_INDEX(ped.GetHandle()), 3.0f, 2.0f, 2.0f);

				// mark as companion for tracking
				DECORATOR::DECOR_SET_INT(ped.GetHandle(), "SH_CMP_companion", 2);

				// === ENHANCED ANIMAL-SPECIFIC LOGIC ===
				if (ped.IsAnimal())
				{
					// completely disable all flee behaviors for animals
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 104, 0.0f); // flee_distance_base
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 105, 0.0f); // flee_distance_random
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 10, 0.0f);  // flee_speed_modifier
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 146, 0.0f); // flee_stamina_cost
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 113, 0.0f); // flee_from_gunshot_distance
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 114, 0.0f); // flee_from_explosion_distance
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 115, 0.0f); // flee_from_fire_distance
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 116, 0.0f); // flee_from_predator_distance
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 117, 0.0f); // flee_from_human_distance
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 118, 0.0f); // flee_from_vehicle_distance
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 119, 0.0f); // flee_from_horse_distance
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 111, 0.0f); // flee_threshold
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 107, 0.0f); // general_flee_modifier

					// enhance aggression and combat for animals
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 120, 100.0f); // aggression_level
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 121, 100.0f); // combat_effectiveness
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 122, 0.0f);   // fear_level (no fear)
					FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 123, 100.0f); // territorial_behavior
				}

				// allow dynamic event responses for better AI behavior
				PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped.GetHandle(), false);

				// === ENHANCED FLEE ATTRIBUTES - DISABLE ALL FLEE BEHAVIORS ===
				// disable vehicle-related flee behaviors
				PED::SET_PED_FLEE_ATTRIBUTES(ped.GetHandle(), 65536, false);   // FA_FORCE_EXIT_VEHICLE - don't force exit
				PED::SET_PED_FLEE_ATTRIBUTES(ped.GetHandle(), 4194304, false); // FA_DISABLE_ENTER_VEHICLES - allow vehicle use
				PED::SET_PED_FLEE_ATTRIBUTES(ped.GetHandle(), 1048576, false); // FA_DISABLE_MOUNT_USAGE - allow mount usage

				// set comprehensive flee attributes to disable all flee behaviors
				PED::SET_PED_FLEE_ATTRIBUTES(ped.GetHandle(), 0, false);       // disable all base flee behaviors

				// === ENHANCED CONFIG FLAGS FOR COMPANIONS ===
				// core companion behavior flags
				ped.SetConfigFlag(PedConfigFlag::_0x16A14D9A, false);              // enable normal behavior
				ped.SetConfigFlag(PedConfigFlag::_DisableHorseFleeILO, true);      // disable horse flee
				ped.SetConfigFlag(PedConfigFlag::_0x74F95F2E, false);              // enable combat reactions
				ped.SetConfigFlag(PedConfigFlag::NeverLeavesGroup, true);          // stay with group
				ped.SetConfigFlag(PedConfigFlag::DisableHorseGunshotFleeResponse, true); // disable gunshot flee

				// enhanced combat and reaction flags
				ped.SetConfigFlag(PedConfigFlag::CanAttackFriendly, false);        // don't attack friendlies
				ped.SetConfigFlag(PedConfigFlag::AlwaysRespondToCriesForHelp, true); // respond to help calls
				ped.SetConfigFlag(PedConfigFlag::TreatAsPlayerDuringTargeting, false); // don't treat as player target
				ped.SetConfigFlag(PedConfigFlag::CowerInsteadOfFlee, false);       // fight instead of cowering
				ped.SetConfigFlag(PedConfigFlag::RunFromFiresAndExplosions, false); // don't flee from explosions

				// avoidance and movement flags
				ped.SetConfigFlag(PedConfigFlag::Avoidance_Ignore_All, false);     // enable smart avoidance
				ped.SetConfigFlag(PedConfigFlag::DisableShockingEvents, false);    // allow shock reactions
				ped.SetConfigFlag(PedConfigFlag::DisablePedAvoidance, false);      // enable ped avoidance
				ped.SetConfigFlag(PedConfigFlag::DisableExplosionReactions, false); // allow explosion reactions
				ped.SetConfigFlag(PedConfigFlag::DisableEvasiveStep, false);       // enable evasive movement
				ped.SetConfigFlag(PedConfigFlag::AlwaysSeeApproachingVehicles, true); // enhanced awareness
				ped.SetConfigFlag(PedConfigFlag::CanDiveAwayFromApproachingVehicles, true); // evasive maneuvers

				// group and relationship flags
				ped.SetConfigFlag(PedConfigFlag::KeepRelationshipGroupAfterCleanUp, true); // maintain relationships
				ped.SetConfigFlag(PedConfigFlag::DontEnterVehiclesInPlayersGroup, false); // allow vehicle entry

				// === ENHANCED AI BEHAVIOR SETTINGS ===
				// enhanced following behavior
				PED::SET_PED_KEEP_TASK(ped.GetHandle(), true);

				// enhanced combat coordination
				PED::SET_PED_COMBAT_RANGE(ped.GetHandle(), 2); // medium range combat
				PED::SET_PED_COMBAT_MOVEMENT(ped.GetHandle(), 2); // aggressive movement
				PED::SET_PED_COMBAT_ABILITY(ped.GetHandle(), 2); // professional combat ability

				PED::SET_PED_ACCURACY(ped.GetHandle(), 85); // high accuracy

				// create companion blip for tracking
				auto blip = MAP::BLIP_ADD_FOR_ENTITY("BLIP_STYLE_COMPANION"_J, ped.GetHandle());
				MAP::BLIP_ADD_MODIFIER(blip, "BLIP_MODIFIER_COMPANION_DOG"_J);
			}

			if (isStoryGang)
			{
				PED::SET_PED_ACCURACY(ped.GetHandle(), 95);

				if (!g_Companion)
				{
					// mark as Story Gang member for maintenance loop
					DECORATOR::DECOR_SET_INT(ped.GetHandle(), "SH_STORY_GANG", 1);

					// story gang relationship setup
					Hash storyGangRelationshipGroup = "REL_GANG_DUTCHS"_J; // use Dutch's gang relationship group

					// set up shared gang relationships (companion mode can override this)
					PED::SET_PED_RELATIONSHIP_GROUP_HASH(ped.GetHandle(), storyGangRelationshipGroup);

					// make gang members friendly with each other (critical for preventing infighting)
					PED::SET_RELATIONSHIP_BETWEEN_GROUPS(2, storyGangRelationshipGroup, storyGangRelationshipGroup);
				}

				// === STORY GANG HOSTILE RELATIONSHIPS ===
				// make story gang hostile towards enemy groups (applies to both companion and non-companion)
				Hash storyGangGroup = "REL_GANG_DUTCHS"_J;

				// set hostile relationships with enemy groups
				PED::SET_RELATIONSHIP_BETWEEN_GROUPS(5, storyGangGroup, "REL_RE_ENEMY"_J);
				PED::SET_RELATIONSHIP_BETWEEN_GROUPS(5, storyGangGroup, "REL_PLAYER_ENEMY"_J);
				PED::SET_RELATIONSHIP_BETWEEN_GROUPS(5, storyGangGroup, "REL_GANG_ODRISCOLL"_J);
				PED::SET_RELATIONSHIP_BETWEEN_GROUPS(5, storyGangGroup, "REL_GANG_MURFREE_BROOD"_J);
				PED::SET_RELATIONSHIP_BETWEEN_GROUPS(5, storyGangGroup, "REL_GANG_SKINNER_BROTHERS"_J);
				PED::SET_RELATIONSHIP_BETWEEN_GROUPS(5, storyGangGroup, "REL_GANG_LARAMIE_GANG"_J);
				PED::SET_RELATIONSHIP_BETWEEN_GROUPS(5, storyGangGroup, "REL_GANG_LEMOYNE_RAIDERS"_J);
				PED::SET_RELATIONSHIP_BETWEEN_GROUPS(5, storyGangGroup, "REL_GANG_CREOLE"_J);
				PED::SET_RELATIONSHIP_BETWEEN_GROUPS(5, storyGangGroup, "REL_GANG_SMUGGLERS"_J);
				PED::SET_RELATIONSHIP_BETWEEN_GROUPS(5, storyGangGroup, "REL_PINKERTONS"_J);

				// === STORY GANG MAINTENANCE LOGIC ===
				// initial health/stamina/deadeye setup
				ped.SetHealth(ped.GetMaxHealth());
				ped.SetStamina(ped.GetMaxStamina());

				// set all attribute cores to 99999
				ATTRIBUTE::_SET_ATTRIBUTE_CORE_VALUE(ped.GetHandle(), 0, 99999); // health core
				ATTRIBUTE::_SET_ATTRIBUTE_CORE_VALUE(ped.GetHandle(), 1, 99999); // stamina core
				ATTRIBUTE::_SET_ATTRIBUTE_CORE_VALUE(ped.GetHandle(), 2, 99999); // deadeye core

				// initial cleanliness setup
				PED::_SET_PED_DAMAGE_CLEANLINESS(ped.GetHandle(), 0.0f);
				PED::CLEAR_PED_WETNESS(ped.GetHandle());
				PED::CLEAR_PED_ENV_DIRT(ped.GetHandle());
				PED::CLEAR_PED_BLOOD_DAMAGE(ped.GetHandle());
				PED::CLEAR_PED_DAMAGE_DECAL_BY_ZONE(ped.GetHandle(), 0, "ALL");
			}
		});
	}

	// convenience wrapper for animal spawning (no weapons)
	static void SpawnAnimal(const std::string& model, int variation, bool isHorse = false)
	{
		if (g_SetModelMode)
		{
			SetPlayerModel(model, variation, isHorse);
		}
		else
		{
			SpawnPed(model, variation, false, false, isHorse);
		}
	}

	// human data structures
	struct Human
	{
		std::string model;
	};

	static std::vector<Human> g_AmbientFemale = {
		{"A_F_M_ArmCholeraCorpse_01"},
		{"A_F_M_ArmTownfolk_01"},
		{"A_F_M_ArmTownfolk_02"},
		{"A_F_M_AsbTownfolk_01"},
		{"A_F_M_BivFancyTravellers_01"},
		{"A_F_M_BlwTownfolk_01"},
		{"A_F_M_BlwTownfolk_02"},
		{"A_F_M_BlwUpperClass_01"},
		{"A_F_M_BtcHillBilly_01"},
		{"A_F_M_BtcObeseWomen_01"},
		{"A_F_M_BynFancyTravellers_01"},
		{"A_F_M_FamilyTravelers_Cool_01"},
		{"A_F_M_FamilyTravelers_Warm_01"},
		{"A_F_M_GamHighSociety_01"},
		{"A_F_M_GrifFancyTravellers_01"},
		{"A_F_M_GuaTownfolk_01"},
		{"A_F_M_HtlFancyTravellers_01"},
		{"A_F_M_LagTownfolk_01"},
		{"A_F_M_LowersdTownfolk_01"},
		{"A_F_M_LowersdTownfolk_02"},
		{"A_F_M_LowersdTownfolk_03"},
		{"A_F_M_LowerTrainPassengers_01"},
		{"A_F_M_MiddlesdTownfolk_01"},
		{"A_F_M_MiddlesdTownfolk_02"},
		{"A_F_M_MiddlesdTownfolk_03"},
		{"A_F_M_MiddleTrainPassengers_01"},
		{"A_F_M_NbxSlums_01"},
		{"A_F_M_NbxUpperClass_01"},
		{"A_F_M_NbxWhore_01"},
		{"A_F_M_RhdProstitute_01"},
		{"A_F_M_RhdTownfolk_01"},
		{"A_F_M_RhdTownfolk_02"},
		{"A_F_M_RhdUpperClass_01"},
		{"A_F_M_RkrFancyTravellers_01"},
		{"A_F_M_RoughTravellers_01"},
		{"A_F_M_SclfFancyTravellers_01"},
		{"A_F_M_SdChinatown_01"},
		{"A_F_M_SdFancyWhore_01"},
		{"A_F_M_SdObeseWomen_01"},
		{"A_F_M_SdServersFormal_01"},
		{"A_F_M_SdSlums_02"},
		{"A_F_M_SkppRisonOnline_01"},
		{"A_F_M_StrTownfolk_01"},
		{"A_F_M_TumTownfolk_01"},
		{"A_F_M_TumTownfolk_02"},
		{"A_F_M_UniCorpse_01"},
		{"A_F_M_UpperTrainPassengers_01"},
		{"A_F_M_ValProstitute_01"},
		{"A_F_M_ValTownfolk_01"},
		{"A_F_M_VhtProstitute_01"},
		{"A_F_M_VhtTownfolk_01"},
		{"A_F_M_WapTownfolk_01"}
	};

	static std::vector<Human> g_AmbientFemaleOrdinary = {
		{"A_F_O_BlwUpperClass_01"},
		{"A_F_O_BtcHillBilly_01"},
		{"A_F_O_GuaTownfolk_01"},
		{"A_F_O_LagTownfolk_01"},
		{"A_F_O_SdChinatown_01"},
		{"A_F_O_SdUpperClass_01"},
		{"A_F_O_WapTownfolk_01"}
	};

	static std::vector<Human> g_AmbientMale = {
		{"A_M_M_ArmCholeraCorpse_01"},
		{"A_M_M_ArmDeputyResident_01"},
		{"A_M_M_ArmTownfolk_01"},
		{"A_M_M_ArmTownfolk_02"},
		{"A_M_M_AsbBoatCrew_01"},
		{"A_M_M_AsbDeputyResident_01"},
		{"A_M_M_AsbMiner_01"},
		{"A_M_M_AsbMiner_02"},
		{"A_M_M_AsbMiner_03"},
		{"A_M_M_AsbMiner_04"},
		{"A_M_M_AsbTownfolk_01"},
		{"A_M_M_AsbTownfolk_01_Laborer"},
		{"A_M_M_BivFancyDrivers_01"},
		{"A_M_M_BivFancyTravellers_01"},
		{"A_M_M_BivRoughTravellers_01"},
		{"A_M_M_BivWorker_01"},
		{"A_M_M_BlwForeman_01"},
		{"A_M_M_BlwLaborer_01"},
		{"A_M_M_BlwLaborer_02"},
		{"A_M_M_BlwObeseMen_01"},
		{"A_M_M_BlwTownfolk_01"},
		{"A_M_M_BlwUpperClass_01"},
		{"A_M_M_BtcHillBilly_01"},
		{"A_M_M_BtcObeseMen_01"},
		{"A_M_M_BynFancyDrivers_01"},
		{"A_M_M_BynFancyTravellers_01"},
		{"A_M_M_BynRoughTravellers_01"},
		{"A_M_M_BynSurvivalist_01"},
		{"A_M_M_CardGamePlayers_01"},
		{"A_M_M_Chelonian_01"},
		{"A_M_M_DeliveryTravelers_Cool_01"},
		{"A_M_M_DeliveryTravelers_Warm_01"},
		{"A_M_M_DominoesPlayers_01"},
		{"A_M_M_EmrFarmHand_01"},
		{"A_M_M_FamilyTravelers_Cool_01"},
		{"A_M_M_FamilyTravelers_Warm_01"},
		{"A_M_M_FarmTravelers_Cool_01"},
		{"A_M_M_FarmTravelers_Warm_01"},
		{"A_M_M_FiveFingerFilletPlayers_01"},
		{"A_M_M_Foreman"},
		{"A_M_M_GamHighSociety_01"},
		{"A_M_M_GrifFancyDrivers_01"},
		{"A_M_M_GrifFancyTravellers_01"},
		{"A_M_M_GriRoughTravellers_01"},
		{"A_M_M_GriSurvivalist_01"},
		{"A_M_M_GuaTownfolk_01"},
		{"A_M_M_HtlFancyDrivers_01"},
		{"A_M_M_HtlFancyTravellers_01"},
		{"A_M_M_HtlRoughTravellers_01"},
		{"A_M_M_HtlSurvivalist_01"},
		{"A_M_M_HunterTravelers_Cool_01"},
		{"A_M_M_HunterTravelers_Warm_01"},
		{"A_M_M_JamesonGuard_01"},
		{"A_M_M_LagTownfolk_01"},
		{"A_M_M_LowersdTownfolk_01"},
		{"A_M_M_LowersdTownfolk_02"},
		{"A_M_M_LowerTrainPassengers_01"},
		{"A_M_M_MiddlesdTownfolk_01"},
		{"A_M_M_MiddlesdTownfolk_02"},
		{"A_M_M_MiddlesdTownfolk_03"},
		{"A_M_M_MiddleTrainPassengers_01"},
		{"A_M_M_MoonShiners_01"},
		{"A_M_M_NbxDockWorkers_01"},
		{"A_M_M_NbxLaborers_01"},
		{"A_M_M_NbxSlums_01"},
		{"A_M_M_NbxUpperClass_01"},
		{"A_M_M_NeaRoughTravellers_01"},
		{"A_M_M_Rancher_01"},
		{"A_M_M_RancherTravelers_Cool_01"},
		{"A_M_M_RancherTravelers_Warm_01"},
		{"A_M_M_RhdDeputyResident_01"},
		{"A_M_M_RhdForeman_01"},
		{"A_M_M_RhdObeseMen_01"},
		{"A_M_M_RhdTownfolk_01"},
		{"A_M_M_RhdTownfolk_01_Laborer"},
		{"A_M_M_RhdTownfolk_02"},
		{"A_M_M_RhdUpperClass_01"},
		{"A_M_M_RkrFancyDrivers_01"},
		{"A_M_M_RkrFancyTravellers_01"},
		{"A_M_M_RkrRoughTravellers_01"},
		{"A_M_M_RkrSurvivalist_01"},
		{"A_M_M_SclfFancyDrivers_01"},
		{"A_M_M_SclfFancyTravellers_01"},
		{"A_M_M_SclfRoughTravellers_01"},
		{"A_M_M_SdChinatown_01"},
		{"A_M_M_SdDockForeman_01"},
		{"A_M_M_SdDockWorkers_02"},
		{"A_M_M_SdFancyTravellers_01"},
		{"A_M_M_SdLaborers_02"},
		{"A_M_M_SdObeseMen_01"},
		{"A_M_M_SdRoughTravellers_01"},
		{"A_M_M_SdServersFormal_01"},
		{"A_M_M_SdSlums_02"},
		{"A_M_M_SkppPrisoner_01"},
		{"A_M_M_SkppPrisonLine_01"},
		{"A_M_M_SmhtHug_01"},
		{"A_M_M_StrDeputyResident_01"},
		{"A_M_M_StrFancyTourist_01"},
		{"A_M_M_StrLaborer_01"},
		{"A_M_M_StrTownfolk_01"},
		{"A_M_M_TumTownfolk_01"},
		{"A_M_M_TumTownfolk_02"},
		{"A_M_M_UniBoatCrew_01"},
		{"A_M_M_UniCoachGuards_01"},
		{"A_M_M_UniCorpse_01"},
		{"A_M_M_UniGunslinger_01"},
		{"A_M_M_UpperTrainPassengers_01"},
		{"A_M_M_ValCriminals_01"},
		{"A_M_M_ValDeputyResident_01"},
		{"A_M_M_ValFarmer_01"},
		{"A_M_M_ValLaborer_01"},
		{"A_M_M_ValTownfolk_01"},
		{"A_M_M_ValTownfolk_02"},
		{"A_M_M_VhtBoatCrew_01"},
		{"A_M_M_VhtThug_01"},
		{"A_M_M_VhtTownfolk_01"},
		{"A_M_M_WapWarriors_01"}
	};

	static std::vector<Human> g_AmbientMaleOrdinary = {
		{"A_M_O_BlwUpperClass_01"},
		{"A_M_O_BtcHillBilly_01"},
		{"A_M_O_GuaTownfolk_01"},
		{"A_M_O_LagTownfolk_01"},
		{"A_M_O_SdChinatown_01"},
		{"A_M_O_SdUpperClass_01"},
		{"A_M_O_WapTownfolk_01"}
	};

	static std::vector<Human> g_AmbientMaleSuppressed = {
		{"A_M_Y_AsbMiner_01"},
		{"A_M_Y_AsbMiner_02"},
		{"A_M_Y_AsbMiner_03"},
		{"A_M_Y_AsbMiner_04"},
		{"A_M_Y_NbxStreetKids_01"},
		{"A_M_Y_NbxStreetKids_Slums_01"},
		{"A_M_Y_SdStreetKids_Slums_02"},
		{"A_M_Y_UniCorpse_01"}
	};

	static std::vector<Human> g_Cutscene = {
		{"CS_Abe"},
		{"CS_AberdeenPigFarmer"},
		{"CS_AberdeenSister"},
		{"CS_AbigailRoberts"},
		{"CS_Acrobat"},
		{"CS_AdamGray"},
		{"CS_AgnesDowd"},
		{"CS_AlbertCakeEsquire"},
		{"CS_AlbertMason"},
		{"CS_AndersHelgerson"},
		{"CS_Angel"},
		{"CS_AngryHusband"},
		{"CS_AngusGeddes"},
		{"CS_AnselAtherton"},
		{"CS_AntonyForemen"},
		{"CS_ArcherFordham"},
		{"CS_ArchibaldJameson"},
		{"CS_ArchieDown"},
		{"CS_ArtAppraiser"},
		{"CS_AsbDeputy_01"},
		{"CS_Ashton"},
		{"CS_BalloonOperator"},
		{"CS_BandBassist"},
		{"CS_BandDrummer"},
		{"CS_BandPianist"},
		{"CS_BandSinger"},
		{"CS_Baptiste"},
		{"CS_BartholomewBraithwaite"},
		{"CS_BathingLadies_01"},
		{"CS_BeatenUpCaptain"},
		{"CS_BeauGray"},
		{"CS_BillWilliamson"},
		{"CS_BivCoachDriver"},
		{"CS_BlwPhotographer"},
		{"CS_BlwWitness"},
		{"CS_BraithwaiteButler"},
		{"CS_BraithwaiteMaid"},
		{"CS_BraithwaiteServant"},
		{"CS_BrendaCrawley"},
		{"CS_Bronte"},
		{"CS_BrontesButler"},
		{"CS_BrotherDorkins"},
		{"CS_BrynnTildon"},
		{"CS_Bubba"},
		{"CS_CabaretMC"},
		{"CS_Cajun"},
		{"CS_Cancan_01"},
		{"CS_Cancan_02"},
		{"CS_Cancan_03"},
		{"CS_Cancan_04"},
		{"CS_CancanMan_01"},
		{"CS_CaptainMonroe"},
		{"CS_Cassidy"},
		{"CS_CatherineBraithwaite"},
		{"CS_CattleRustler"},
		{"CS_CaveHermit"},
		{"CS_ChainPrisoner_01"},
		{"CS_ChainPrisoner_02"},
		{"CS_CharlesSmith"},
		{"CS_ChelonianMaster"},
		{"CS_CigcardGuy"},
		{"CS_Clay"},
		{"CS_Cleet"},
		{"CS_Clive"},
		{"CS_Colfavours"},
		{"CS_ColmODriscoll"},
		{"CS_Cooper"},
		{"CS_CornwallTrainConductor"},
		{"CS_CrackpotInventor"},
		{"CS_CrackpotRobot"},
		{"CS_CreepyOldLady"},
		{"CS_CreoleCaptain"},
		{"CS_CreoleDoctor"},
		{"CS_CreoleGuy"},
		{"CS_DaleMaroney"},
		{"CS_DaveyCallender"},
		{"CS_DavidGeddes"},
		{"CS_Desmond"},
		{"CS_Didsbury"},
		{"CS_DinoBonesLady"},
		{"CS_DisguisedDuster_01"},
		{"CS_DisguisedDuster_02"},
		{"CS_DisguisedDuster_03"},
		{"CS_DorotheaWicklow"},
		{"CS_DrHiggins"},
		{"CS_DrMalcolmMacintosh"},
		{"CS_DuncanGeddes"},
		{"CS_DusterInformant_01"},
		{"CS_Dutch"},
		{"CS_EagleFlies"},
		{"CS_EdgarRoss"},
		{"CS_EdithJohn"},
		{"CS_EdithDown"},
		{"CS_EdmundLowry"},
		{"CS_EscapeArtist"},
		{"CS_EscapeArtistAssistant"},
		{"CS_EvelynMiller"},
		{"CS_ExConfedInformant"},
		{"CS_ExConfedSLeader_01"},
		{"CS_ExoticCollector"},
		{"CS_FamousGunslinger_01"},
		{"CS_FamousGunslinger_02"},
		{"CS_FamousGunslinger_03"},
		{"CS_FamousGunslinger_04"},
		{"CS_FamousGunslinger_05"},
		{"CS_FamousGunslinger_06"},
		{"CS_FeatherstonChambers"},
		{"CS_FeatsOfStrength"},
		{"CS_FightRef"},
		{"CS_Fire_Breather"},
		{"CS_FishCollector"},
		{"CS_ForgivenHusband_01"},
		{"CS_ForgivenWife_01"},
		{"CS_ForMyArtBigWoman"},
		{"CS_FrancisSinclair"},
		{"CS_FrenchArtist"},
		{"CS_FrenchMan_01"},
		{"CS_Fussar"},
		{"CS_GarethBraithwaite"},
		{"CS_Gavin"},
		{"CS_GenStoryFemale"},
		{"CS_GenStoryMale"},
		{"CS_GeraldBraithwaite"},
		{"CS_GermanDaughter"},
		{"CS_GermanFather"},
		{"CS_GermanMother"},
		{"CS_GermanSon"},
		{"CS_GilbertKnightly"},
		{"CS_Gloria"},
		{"CS_GrizzledJon"},
		{"CS_GuidoMartelli"},
		{"CS_Hamish"},
		{"CS_HectorFellowes"},
		{"CS_HenriLemiux"},
		{"CS_Herbalist"},
		{"CS_Hercule"},
		{"CS_HestonJameson"},
		{"CS_HobartCrawley"},
		{"CS_HoseaMatthews"},
		{"CS_IanGray"},
		{"CS_JackMarston"},
		{"CS_JackMarston_Teen"},
		{"CS_Jamie"},
		{"CS_Janson"},
		{"CS_JavierEscuella"},
		{"CS_Jeb"},
		{"CS_JimCalloway"},
		{"CS_JockGray"},
		{"CS_Joe"},
		{"CS_JoeButler"},
		{"CS_JohnMarston"},
		{"CS_JohnTheBaptisingMadman"},
		{"CS_JohnWeathers"},
		{"CS_JosiahTrelawny"},
		{"CS_Jules"},
		{"CS_Karen"},
		{"CS_KarensJohn_01"},
		{"CS_Kieran"},
		{"CS_Laramie"},
		{"CS_LeighGray"},
		{"CS_LemiuxAssistant"},
		{"CS_Lenny"},
		{"CS_Leon"},
		{"CS_LeopoldStrauss"},
		{"CS_LeviSimon"},
		{"CS_LeviticusCornwall"},
		{"CS_LillianPowell"},
		{"CS_LillyMillet"},
		{"CS_LondonderrySon"},
		{"CS_LucaNapoli"},
		{"CS_Magnifico"},
		{"CS_MamaWatson"},
		{"CS_MarshallThurwell"},
		{"CS_MaryBeth"},
		{"CS_MaryLinton"},
		{"CS_MeditatingMonk"},
		{"CS_Meredith"},
		{"CS_MeredithsMother"},
		{"CS_MicahBell"},
		{"CS_MicahsNemesis"},
		{"CS_Mickey"},
		{"CS_MiltonAndrews"},
		{"CS_MissMarjorie"},
		{"CS_MixedRaceKid"},
		{"CS_Moira"},
		{"CS_MollyOShea"},
		{"CS_MrAdler"},
		{"CS_MrDevon"},
		{"CS_MrLinton"},
		{"CS_MrPearson"},
		{"CS_MrsCalhoun"},
		{"CS_MrsSinclair"},
		{"CS_MrsAdler"},
		{"CS_MrsFellows"},
		{"CS_MrsGeddes"},
		{"CS_MrsLondonderry"},
		{"CS_MrsWeathers"},
		{"CS_MrWayne"},
		{"CS_Mud2BigGuy"},
		{"CS_MysteriousStranger"},
		{"CS_NbxDrunk"},
		{"CS_NbxExecuted"},
		{"CS_NbxPoliceChiefFormal"},
		{"CS_NbxReceptionist_01"},
		{"CS_NialWhelan"},
		{"CS_NicholasTimmins"},
		{"CS_Nils"},
		{"CS_NorrisForsythe"},
		{"CS_ObediahHinton"},
		{"CS_OddFellowsPinhead"},
		{"CS_OdProstitute"},
		{"CS_OperaSinger"},
		{"CS_Paytah"},
		{"CS_PenelopeBraithwaite"},
		{"CS_PinkertonGoon"},
		{"CS_PoisonWellShaman"},
		{"CS_PoorJoe"},
		{"CS_Priest_Wedding"},
		{"CS_PrincessIsabeau"},
		{"CS_ProfessorBell"},
		{"CS_RainsFall"},
		{"CS_RamonCortez"},
		{"CS_ReverendFotheringham"},
		{"CS_RevSwanson"},
		{"CS_RhoDeputy_01"},
		{"CS_RhodeDeputy_02"},
		{"CS_RhodesAssistant"},
		{"CS_RhodesKidnapVictim"},
		{"CS_RhodesSaloonBouncer"},
		{"CS_RingMaster"},
		{"CS_RockySeven_Widow"},
		{"CS_Samaritan"},
		{"CS_ScottGray"},
		{"CS_SD_StreetKid_01"},
		{"CS_SD_StreetKid_01A"},
		{"CS_SD_StreetKid_01B"},
		{"CS_SD_StreetKid_02"},
		{"CS_SD_Doctor_01"},
		{"CS_SD_Priest"},
		{"CS_SD_SaloonDrunk_01"},
		{"CS_SD_StreetKidThief"},
		{"CS_Sean"},
		{"CS_SheriffFreeman"},
		{"CS_SheriffOwens"},
		{"CS_SisterCalderon"},
		{"CS_SlaveCatcher"},
		{"CS_Soothsayer"},
		{"CS_StrawberryOutlaw_01"},
		{"CS_StrawberryOutlaw_02"},
		{"CS_StrDeputy_01"},
		{"CS_StrDeputy_02"},
		{"CS_StrSheriff_01"},
		{"CS_SunWorshipper"},
		{"CS_SusanGrimshaw"},
		{"CS_SwampFreak"},
		{"CS_SwampWeirdoSonny"},
		{"CS_SwordDancer"},
		{"CS_TavishGray"},
		{"CS_Taxidermist"},
		{"CS_TheodoreLevin"},
		{"CS_ThomasDown"},
		{"CS_TigerHandler"},
		{"CS_Tilly"},
		{"CS_TimothyDonahue"},
		{"CS_TinyHermit"},
		{"CS_TomDickens"},
		{"CS_TownCrier"},
		{"CS_TreasureHunter"},
		{"CS_TwinBrother_01"},
		{"CS_TwinBrother_02"},
		{"CS_TwinGroupie_01"},
		{"CS_TwinGroupie_02"},
		{"CS_Uncle"},
		{"CS_UniDusterJail_01"},
		{"CS_ValAuctionBoss_01"},
		{"CS_ValDeputy_01"},
		{"CS_ValPrayingMan"},
		{"CS_ValProstitute_01"},
		{"CS_ValProstitute_02"},
		{"CS_ValSheriff"},
		{"CS_Vampire"},
		{"CS_Vht_BathGirl"},
		{"CS_WapitiBoy"},
		{"CS_WarVet"},
		{"CS_Watson_01"},
		{"CS_Watson_02"},
		{"CS_Watson_03"},
		{"CS_WelshFighter"},
		{"CS_WintonHolmes"},
		{"CS_Wrobel"}
	};

	static std::vector<Human> g_MultiplayerCutscene = {
		{"CS_MP_Agent_Hixon"},
		{"CS_MP_Alfredo_Montez"},
		{"CS_MP_Allison"},
		{"CS_MP_Amos_Lansing"},
		{"CS_MP_Bessie_Adair"},
		{"CS_MP_Bonnie"},
		{"CS_MP_BountyHunter"},
		{"CS_MP_Camp_Cook"},
		{"CS_MP_Cliff"},
		{"CS_MP_Cripps"},
		{"CS_MP_Cripps_B"},
		{"CS_MP_DannyLee"},
		{"CS_MP_Grace_Lancing"},
		{"CS_MP_Gus_Macmillan"},
		{"CS_MP_Hans"},
		{"CS_MP_Harriet_Davenport"},
		{"CS_MP_Henchman"},
		{"CS_MP_Horley"},
		{"CS_MP_Jeremiah_Shaw"},
		{"CS_MP_Jessica"},
		{"CS_MP_Jorge_Montez"},
		{"CS_MP_Langston"},
		{"CS_MP_Lee"},
		{"CS_MP_Lem"},
		{"CS_MP_Mabel"},
		{"CS_MP_Maggie"},
		{"CS_MP_Marshall_Davies"},
		{"CS_MP_Moonshiner"},
		{"CS_MP_MrAdler"},
		{"CS_MP_OldMan_Jones"},
		{"CS_MP_Revenge_Marshall"},
		{"CS_MP_Samson_Finch"},
		{"CS_MP_Seth"},
		{"CS_MP_Shaky"},
		{"CS_MP_SheriffFreeman"},
		{"CS_MP_TeddyBrown"},
		{"CS_MP_Terrance"},
		{"CS_MP_The_Boy"},
		{"CS_MP_TravellingSaleswoman"},
		{"CS_MP_Went"}
	};

	static std::vector<Human> g_Gang = {
		{"G_F_M_UniDuster_01"},
		{"G_M_M_BountyHunters_01"},
		{"G_M_M_UniAfricanAmericanGang_01"},
		{"G_M_M_UniBanditos_01"},
		{"G_M_M_UniBraithwaites_01"},
		{"G_M_M_UniBronteGoons_01"},
		{"G_M_M_UniCornwallGoons_01"},
		{"G_M_M_UniCriminals_01"},
		{"G_M_M_UniCriminals_02"},
		{"G_M_M_UniDuster_01"},
		{"G_M_M_UniDuster_02"},
		{"G_M_M_UniDuster_03"},
		{"G_M_M_UniDuster_04"},
		{"G_M_M_UniDuster_05"},
		{"G_M_M_UniGrays_01"},
		{"G_M_M_UniGrays_02"},
		{"G_M_M_UniInbred_01"},
		{"G_M_M_UniLangstonBoys_01"},
		{"G_M_M_UniMicahGoons_01"},
		{"G_M_M_UniMountainMen_01"},
		{"G_M_M_UniRanchers_01"},
		{"G_M_M_UniSwamp_01"},
		{"G_M_O_UniExConfeds_01"},
		{"G_M_Y_UniExConfeds_01"},
		{"G_M_Y_UniExConfeds_02"}
	};

	static std::vector<Human> g_StoryFinale = {
		{"MES_Abigail2_Males_01"},
		{"MES_Finale2_Females_01"},
		{"MES_Finale2_Males_01"},
		{"MES_Finale3_Males_01"},
		{"MES_Marston1_Males_01"},
		{"MES_Marston2_Males_01"},
		{"MES_Marston5_2_Males_01"},
		{"MES_Marston6_Females_01"},
		{"MES_Marston6_Males_01"},
		{"MES_Marston6_Teens_01"},
		{"MES_Sadie4_Males_01"},
		{"MES_Sadie5_Males_01"}
	};

	static std::vector<Human> g_MultiplayerBloodMoney = {
		{"CS_MP_PoliceChief_Lambert"},
		{"CS_MP_Senator_Ricard"},
		{"MP_A_F_M_Protect_Endflow_Blackwater_01"},
		{"MP_A_M_M_AsbMiners_01"},
		{"MP_A_M_M_AsbMiners_02"},
		{"MP_A_M_M_CoachGuards_01"},
		{"MP_A_M_M_Fos_CoachGuards_01"},
		{"MP_A_M_M_JamesonGuard_01"},
		{"MP_A_M_M_Lom_AsbMiners_01"},
		{"MP_A_M_M_Protect_Endflow_Blackwater_01"},
		{"MP_Bink_Ember_Of_The_East_Males_01"},
		{"MP_CS_AntonyForemen"},
		{"MP_GuidoMartelli"},
		{"MP_G_M_M_Fos_DebtGangCapitali_01"},
		{"MP_G_M_M_Fos_DebtGang_01"},
		{"MP_G_M_M_Fos_Vigilantes_01"},
		{"MP_G_M_M_MountainMen_01"},
		{"MP_G_M_O_UniExConfeds_Cap_01"},
		{"MP_S_M_M_Fos_HarborGuards_01"},
		{"MP_S_M_M_RevenueAgents_Cap_01"},
		{"MP_U_F_M_Outlaw_SocietyLady_01"},
		{"MP_U_F_M_Protect_Mercer_01"},
		{"MP_U_M_M_AsbDeputy_01"},
		{"MP_U_M_M_DockRecipients_01"},
		{"MP_U_M_M_DropOff_Bronte_01"},
		{"MP_U_M_M_Fos_BagHolders_01"},
		{"MP_U_M_M_Fos_CoachHoldup_Recipient_01"},
		{"MP_U_M_M_Fos_CornwallGuard_01"},
		{"MP_U_M_M_Fos_Cornwall_Bandits_01"},
		{"MP_U_M_M_Fos_DockRecipients_01"},
		{"MP_U_M_M_Fos_DockWorker_01"},
		{"MP_U_M_M_Fos_DropOff_01"},
		{"MP_U_M_M_Fos_HarborMaster_01"},
		{"MP_U_M_M_Fos_Interrogator_01"},
		{"MP_U_M_M_Fos_Interrogator_02"},
		{"MP_U_M_M_Fos_Musician_01"},
		{"MP_U_M_M_Fos_Railway_Baron_01"},
		{"MP_U_M_M_Fos_Railway_Driver_01"},
		{"MP_U_M_M_Fos_Railway_Foreman_01"},
		{"MP_U_M_M_Fos_Railway_Guards_01"},
		{"MP_U_M_M_Fos_Railway_Hunter_01"},
		{"MP_U_M_M_Fos_Railway_Recipient_01"},
		{"MP_U_M_M_Fos_Recovery_Recipient_01"},
		{"MP_U_M_M_Fos_RogueThief_01"},
		{"MP_U_M_M_Fos_Saboteur_01"},
		{"MP_U_M_M_Fos_SdSaloon_Gambler_01"},
		{"MP_U_M_M_Fos_SdSaloon_Owner_01"},
		{"MP_U_M_M_Fos_SdSaloon_Recipient_01"},
		{"MP_U_M_M_Fos_SdSaloon_Recipient_02"},
		{"MP_U_M_M_Fos_Town_Outlaw_01"},
		{"MP_U_M_M_Fos_Town_Vigilante_01"},
		{"MP_U_M_M_HarborMaster_01"},
		{"MP_U_M_M_Hctel_Arm_Hostage_01"},
		{"MP_U_M_M_Hctel_Arm_Hostage_02"},
		{"MP_U_M_M_Hctel_Arm_Hostage_03"},
		{"MP_U_M_M_Hctel_Sd_Gang_01"},
		{"MP_U_M_M_Hctel_Sd_Target_01"},
		{"MP_U_M_M_Hctel_Sd_Target_02"},
		{"MP_U_M_M_Hctel_Sd_Target_03"},
		{"MP_U_M_M_Interrogator_01"},
		{"MP_U_M_M_Lom_AsbMercs_01"},
		{"MP_U_M_M_Lom_DockWorker_01"},
		{"MP_U_M_M_Lom_DropOff_Bronte_01"},
		{"MP_U_M_M_Lom_Head_Security_01"},
		{"MP_U_M_M_Lom_Rhd_Dealers_01"},
		{"MP_U_M_M_Lom_Rhd_Sheriff_01"},
		{"MP_U_M_M_Lom_Rhd_SmithAssistant_01"},
		{"MP_U_M_M_Lom_Saloon_Drunk_01"},
		{"MP_U_M_M_Lom_Sd_DockWorker_01"},
		{"MP_U_M_M_Lom_Train_Barricade_01"},
		{"MP_U_M_M_Lom_Train_Clerk_01"},
		{"MP_U_M_M_Lom_Train_Conductor_01"},
		{"MP_U_M_M_Lom_Train_LawTarget_01"},
		{"MP_U_M_M_Lom_Train_Prisoners_01"},
		{"MP_U_M_M_Lom_Train_WagonDropOff_01"},
		{"MP_U_M_M_Musician_01"},
		{"MP_U_M_M_Outlaw_ArrestedThief_01"},
		{"MP_U_M_M_Outlaw_CoachDriver_01"},
		{"MP_U_M_M_Outlaw_Covington_01"},
		{"MP_U_M_M_Outlaw_MpVictim_01"},
		{"MP_U_M_M_Outlaw_Rhd_Noble_01"},
		{"MP_U_M_M_Protect_Armadillo_01"},
		{"MP_U_M_M_Protect_Blackwater_01"},
		{"MP_U_M_M_Protect_Friendly_Armadillo_01"},
		{"MP_U_M_M_Protect_Halloween_Ned_01"},
		{"MP_U_M_M_Protect_Macfarlanes_Contact_01"},
		{"MP_U_M_M_Protect_Mercer_Contact_01"},
		{"MP_U_M_M_Protect_Strawberry"},
		{"MP_U_M_M_Protect_Strawberry_01"},
		{"MP_U_M_M_Protect_Valentine_01"},
		{"MP_U_M_O_Lom_AsbForeman_01"}
	};

	static std::vector<Human> g_MultiplayerBountyHunters = {
		{"MP_Beau_Bink_Females_01"},
		{"MP_Beau_Bink_Males_01"},
		{"MP_Carmela_Bink_Victim_Males_01"},
		{"MP_CD_RevengeMayor_01"},
		{"MP_FM_BountyTarget_Females_Dlc008_01"},
		{"MP_FM_BountyTarget_Males_Dlc008_01"},
		{"MP_FM_Bounty_Caged_Males_01"},
		{"MP_FM_Bounty_Ct_Corpses_01"},
		{"MP_FM_Bounty_Hideout_Males_01"},
		{"MP_FM_Bounty_Horde_Law_01"},
		{"MP_FM_Bounty_Horde_Males_01"},
		{"MP_FM_Bounty_Infiltration_Males_01"},
		{"MP_FM_KnownBounty_Guards_01"},
		{"MP_FM_KnownBounty_Informants_Females_01"},
		{"MP_FM_KnownBounty_Informants_Males_01"},
		{"MP_FM_MultiTrack_Victims_Males_01"},
		{"MP_FM_Stakeout_Corpses_Males_01"},
		{"MP_FM_Stakeout_Poker_Males_01"},
		{"MP_FM_Stakeout_Target_Males_01"},
		{"MP_FM_Track_Prospector_01"},
		{"MP_FM_Track_Sd_Lawman_01"},
		{"MP_FM_Track_Targets_Males_01"},
		{"MP_G_F_M_CultGuards_01"},
		{"MP_G_F_M_CultMembers_01"},
		{"MP_G_M_M_CultGuards_01"},
		{"MP_G_M_M_CultMembers_01"},
		{"MP_G_M_M_Mercs_01"},
		{"MP_G_M_M_RifleCronies_01"},
		{"MP_Lbm_Carmela_Banditos_01"},
		{"MP_LM_StealHorse_Buyers_01"},
		{"MP_U_F_M_CultPriest_01"},
		{"MP_U_F_M_LegendaryBounty_03"},
		{"MP_U_M_M_BankPrisoner_01"},
		{"MP_U_M_M_BinkMercs_01"},
		{"MP_U_M_M_CultPriest_01"},
		{"MP_U_M_M_DropOff_Josiah_01"},
		{"MP_U_M_M_LegendaryBounty_08"},
		{"MP_U_M_M_LegendaryBounty_09"},
		{"MP_U_M_M_Rhd_BountyTarget_01"},
		{"MP_U_M_M_Rhd_BountyTarget_02"},
		{"MP_U_M_M_Rhd_BountyTarget_03"},
		{"MP_U_M_M_Rhd_BountyTarget_03B"}
	};

	static std::vector<Human> g_MultiplayerNaturalist = {
		{"CS_MP_Agent_Hixon"},
		{"CS_MP_DannyLee"},
		{"CS_MP_Gus_MacMillan"},
		{"CS_MP_Harriet_Davenport"},
		{"CS_MP_Lem"},
		{"CS_MP_Maggie"},
		{"CS_MP_Seth"},
		{"CS_MP_The_Boy"},
		{"MP_A_F_M_SaloonPatrons_01"},
		{"MP_A_F_M_SaloonPatrons_02"},
		{"MP_A_F_M_SaloonPatrons_03"},
		{"MP_A_F_M_SaloonPatrons_04"},
		{"MP_A_F_M_SaloonPatrons_05"},
		{"MP_A_M_M_MoonshineMakers_01"},
		{"MP_A_M_M_SaloonPatrons_01"},
		{"MP_A_M_M_SaloonPatrons_02"},
		{"MP_A_M_M_SaloonPatrons_03"},
		{"MP_A_M_M_SaloonPatrons_04"},
		{"MP_A_M_M_SaloonPatrons_05"},
		{"MP_G_M_M_AnimalPoachers_01"},
		{"MP_G_M_M_UniCriminals_03"},
		{"MP_G_M_M_UniCriminals_04"},
		{"MP_G_M_M_UniCriminals_05"},
		{"MP_G_M_M_UniCriminals_06"},
		{"MP_G_M_M_UniCriminals_07"},
		{"MP_G_M_M_UniCriminals_08"},
		{"MP_G_M_M_UniCriminals_09"},
		{"MP_RE_MoonshineCamp_Males_01"},
		{"MP_S_M_M_RevenueAgents_01"},
		{"MP_U_F_M_Buyer_Improved_01"},
		{"MP_U_F_M_Buyer_Improved_02"},
		{"MP_U_F_M_Buyer_Regular_01"},
		{"MP_U_F_M_Buyer_Regular_02"},
		{"MP_U_F_M_Buyer_Special_01"},
		{"MP_U_F_M_Buyer_Special_02"},
		{"MP_U_M_M_Buyer_Default_01"},
		{"MP_U_M_M_Buyer_Improved_01"},
		{"MP_U_M_M_Buyer_Improved_02"},
		{"MP_U_M_M_Buyer_Improved_03"},
		{"MP_U_M_M_Buyer_Improved_04"},
		{"MP_U_M_M_Buyer_Improved_05"},
		{"MP_U_M_M_Buyer_Improved_06"},
		{"MP_U_M_M_Buyer_Improved_07"},
		{"MP_U_M_M_Buyer_Improved_08"},
		{"MP_U_M_M_Buyer_Regular_01"},
		{"MP_U_M_M_Buyer_Regular_02"},
		{"MP_U_M_M_Buyer_Regular_03"},
		{"MP_U_M_M_Buyer_Regular_04"},
		{"MP_U_M_M_Buyer_Regular_05"},
		{"MP_U_M_M_Buyer_Regular_06"},
		{"MP_U_M_M_Buyer_Regular_07"},
		{"MP_U_M_M_Buyer_Regular_08"},
		{"MP_U_M_M_Buyer_Special_01"},
		{"MP_U_M_M_Buyer_Special_02"},
		{"MP_U_M_M_Buyer_Special_03"},
		{"MP_U_M_M_Buyer_Special_04"},
		{"MP_U_M_M_Buyer_Special_05"},
		{"MP_U_M_M_Buyer_Special_06"},
		{"MP_U_M_M_Buyer_Special_07"},
		{"MP_U_M_M_Buyer_Special_08"},
		{"MP_U_M_M_LawCamp_Prisoner_01"},
		{"MP_U_M_M_SaloonBrawler_01"},
		{"MP_U_M_M_SaloonBrawler_02"},
		{"MP_U_M_M_SaloonBrawler_03"},
		{"MP_U_M_M_SaloonBrawler_04"},
		{"MP_U_M_M_SaloonBrawler_05"},
		{"MP_U_M_M_SaloonBrawler_06"},
		{"MP_U_M_M_SaloonBrawler_07"},
		{"MP_U_M_M_SaloonBrawler_08"},
		{"MP_U_M_M_SaloonBrawler_09"},
		{"MP_U_M_M_SaloonBrawler_10"},
		{"MP_U_M_M_SaloonBrawler_11"},
		{"MP_U_M_M_SaloonBrawler_12"},
		{"MP_U_M_M_SaloonBrawler_13"},
		{"MP_U_M_M_SaloonBrawler_14"}
	};

	static std::vector<Human> g_Multiplayer = {
		{"MP_A_F_M_CardGamePlayers_01"},
		{"MP_A_F_M_SaloonBand_Females_01"},
		{"MP_A_F_M_SaloonPatrons_01"},
		{"MP_A_F_M_SaloonPatrons_02"},
		{"MP_A_F_M_SaloonPatrons_03"},
		{"MP_A_F_M_SaloonPatrons_04"},
		{"MP_A_F_M_SaloonPatrons_05"},
		{"MP_A_F_M_UniCorpse_01"},
		{"MP_A_M_M_LaborUprisers_01"},
		{"MP_A_M_M_MoonshineMakers_01"},
		{"MP_A_M_M_SaloonBand_Males_01"},
		{"MP_A_M_M_SaloonPatrons_01"},
		{"MP_A_M_M_SaloonPatrons_02"},
		{"MP_A_M_M_SaloonPatrons_03"},
		{"MP_A_M_M_SaloonPatrons_04"},
		{"MP_A_M_M_SaloonPatrons_05"},
		{"MP_A_M_M_UniCorpse_01"},
		{"MP_Asn_BenedictPoint_Females_01"},
		{"MP_Asn_BenedictPoint_Males_01"},
		{"MP_Asn_Blackwater_Males_01"},
		{"MP_Asn_BraithwaiteManor_Males_01"},
		{"MP_Asn_BraithwaiteManor_Males_02"},
		{"MP_Asn_BraithwaiteManor_Males_03"},
		{"MP_Asn_CivilWarFort_Males_01"},
		{"MP_Asn_GaptoothBreach_Males_01"},
		{"MP_Asn_PikesBasin_Males_01"},
		{"MP_Asn_SdPoliceStation_Males_01"},
		{"MP_Asn_SdWedding_Females_01"},
		{"MP_Asn_SdWedding_Males_01"},
		{"MP_Asn_ShadyBelle_Females_01"},
		{"MP_Asn_Stillwater_Males_01"},
		{"MP_Asntrk_ElysianPool_Males_01"},
		{"MP_Asntrk_GrizzliesWest_Males_01"},
		{"MP_Asntrk_HagenOrchard_Males_01"},
		{"MP_Asntrk_Isabella_Males_01"},
		{"MP_Asntrk_TallTrees_Males_01"},
		{"MP_Beau_Bink_Females_01"},
		{"MP_Beau_Bink_Males_01"},
		{"MP_CampDef_Bluewater_Females_01"},
		{"MP_CampDef_Bluewater_Males_01"},
		{"MP_CampDef_ChollaSprings_Females_01"},
		{"MP_CampDef_ChollaSprings_Males_01"},
		{"MP_CampDef_EastNewHanover_Females_01"},
		{"MP_CampDef_EastNewHanover_Males_01"},
		{"MP_CampDef_GaptoothBreach_Females_01"},
		{"MP_CampDef_GaptoothBreach_Males_01"},
		{"MP_CampDef_GaptoothRidge_Females_01"},
		{"MP_CampDef_GaptoothRidge_Males_01"},
		{"MP_CampDef_GreatPlains_Males_01"},
		{"MP_CampDef_Grizzlies_Males_01"},
		{"MP_CampDef_Heartlands1_Males_01"},
		{"MP_CampDef_Heartlands2_Females_01"},
		{"MP_CampDef_Heartlands2_Males_01"},
		{"MP_CampDef_Hennigans_Females_01"},
		{"MP_CampDef_Hennigans_Males_01"},
		{"MP_CampDef_LittleCreek_Females_01"},
		{"MP_CampDef_LittleCreek_Males_01"},
		{"MP_CampDef_RadleysPasture_Females_01"},
		{"MP_CampDef_RadleysPasture_Males_01"},
		{"MP_CampDef_RioBravo_Females_01"},
		{"MP_CampDef_RioBravo_Males_01"},
		{"MP_CampDef_Roanoke_Females_01"},
		{"MP_CampDef_Roanoke_Males_01"},
		{"MP_CampDef_TallTrees_Females_01"},
		{"MP_CampDef_TallTrees_Males_01"},
		{"MP_CampDef_TwoRocks_Females_01"},
		{"MP_Carmela_Bink_Victim_Males_01"},
		{"MP_CD_RevengeMayor_01"},
		{"MP_Chu_Kid_Armadillo_Males_01"},
		{"MP_Chu_Kid_DiabloRidge_Males_01"},
		{"MP_Chu_Kid_EmrStation_Males_01"},
		{"MP_Chu_Kid_GreatPlains2_Males_01"},
		{"MP_Chu_Kid_GreatPlains_Males_01"},
		{"MP_Chu_Kid_Heartlands_Males_01"},
		{"MP_Chu_Kid_Lagras_Males_01"},
		{"MP_Chu_Kid_Lemoyne_Females_01"},
		{"MP_Chu_Kid_Lemoyne_Males_01"},
		{"MP_Chu_Kid_Recipient_Males_01"},
		{"MP_Chu_Kid_Rhodes_Males_01"},
		{"MP_Chu_Kid_SaintDenis_Females_01"},
		{"MP_Chu_Kid_SaintDenis_Males_01"},
		{"MP_Chu_Kid_ScarlettMeadows_Males_01"},
		{"MP_Chu_Kid_Tumbleweed_Males_01"},
		{"MP_Chu_Kid_Valentine_Males_01"},
		{"MP_Chu_Rob_Ambarino_Males_01"},
		{"MP_Chu_Rob_Annesburg_Males_01"},
		{"MP_Chu_Rob_BenedictPoint_Females_01"},
		{"MP_Chu_Rob_BenedictPoint_Males_01"},
		{"MP_Chu_Rob_Blackwater_Males_01"},
		{"MP_Chu_Rob_CaligaHall_Males_01"},
		{"MP_Chu_Rob_Coronado_Males_01"},
		{"MP_Chu_Rob_Cumberland_Males_01"},
		{"MP_Chu_Rob_FortMercer_Females_01"},
		{"MP_Chu_Rob_FortMercer_Males_01"},
		{"MP_Chu_Rob_GreenHollow_Males_01"},
		{"MP_Chu_Rob_MacFarlanes_Females_01"},
		{"MP_Chu_Rob_MacFarlanes_Males_01"},
		{"MP_Chu_Rob_MacLeans_Males_01"},
		{"MP_Chu_Rob_Millesani_Males_01"},
		{"MP_Chu_Rob_MontanaRiver_Males_01"},
		{"MP_Chu_Rob_PaintedSky_Males_01"},
		{"MP_Chu_Rob_Rathskeller_Males_01"},
		{"MP_Chu_Rob_Recipient_Males_01"},
		{"MP_Chu_Rob_Rhodes_Males_01"},
		{"MP_Chu_Rob_Strawberry_Males_01"},
		{"MP_Clay"},
		{"MP_Convoy_Recipient_Females_01"},
		{"MP_Convoy_Recipient_Males_01"},
		{"MP_DE_U_F_M_BigValley_01"},
		{"MP_DE_U_F_M_BlueWaterMarsh_01"},
		{"MP_DE_U_F_M_Braithwaite_01"},
		{"MP_DE_U_F_M_DoverHill_01"},
		{"MP_DE_U_F_M_GreatPlains_01"},
		{"MP_DE_U_F_M_HangingRock_01"},
		{"MP_DE_U_F_M_Heartlands_01"},
		{"MP_DE_U_F_M_HennigansStead_01"},
		{"MP_DE_U_F_M_SilentStead_01"},
		{"MP_DE_U_M_M_AuroraBasin_01"},
		{"MP_DE_U_M_M_BarrowLagoon_01"},
		{"MP_DE_U_M_M_BigValleyGraves_01"},
		{"MP_DE_U_M_M_CentralUnionRr_01"},
		{"MP_DE_U_M_M_Pleasance_01"},
		{"MP_DE_U_M_M_RileysCharge_01"},
		{"MP_DE_U_M_M_VanHorn_01"},
		{"MP_DE_U_M_M_WesternHomestead_01"},
		{"MP_DR_U_F_M_BayouGatorFood_01"},
		{"MP_DR_U_F_M_BigValleyCave_01"},
		{"MP_DR_U_F_M_BigValleyCliff_01"},
		{"MP_DR_U_F_M_BluewaterKidnap_01"},
		{"MP_DR_U_F_M_ColterBandits_01"},
		{"MP_DR_U_F_M_ColterBandits_02"},
		{"MP_DR_U_F_M_MissingFisherman_01"},
		{"MP_DR_U_F_M_MissingFisherman_02"},
		{"MP_DR_U_F_M_MistakenBounties_01"},
		{"MP_DR_U_F_M_PlagueTown_01"},
		{"MP_DR_U_F_M_QuakersCove_01"},
		{"MP_DR_U_F_M_QuakersCove_02"},
		{"MP_DR_U_F_M_SdGraveyard_01"},
		{"MP_DR_U_M_M_BigValleyCave_01"},
		{"MP_DR_U_M_M_BigValleyCliff_01"},
		{"MP_DR_U_M_M_BluewaterKidnap_01"},
		{"MP_DR_U_M_M_CanoeEscape_01"},
		{"MP_DR_U_M_M_HwyRobbery_01"},
		{"MP_DR_U_M_M_MistakenBounties_01"},
		{"MP_DR_U_M_M_PikesBasin_01"},
		{"MP_DR_U_M_M_PikesBasin_02"},
		{"MP_DR_U_M_M_PlagueTown_01"},
		{"MP_DR_U_M_M_RoanokeStandoff_01"},
		{"MP_DR_U_M_M_SdGraveyard_01"},
		{"MP_DR_U_M_M_SdMugging_01"},
		{"MP_DR_U_M_M_SdMugging_02"},
		{"MP_Female"},
		{"MP_FM_Bounty_Caged_Males_01"},
		{"MP_FM_Bounty_Ct_Corpses_01"},
		{"MP_FM_Bounty_Hideout_Males_01"},
		{"MP_FM_Bounty_Horde_Law_01"},
		{"MP_FM_Bounty_Horde_Males_01"},
		{"MP_FM_Bounty_Infiltration_Males_01"},
		{"MP_FM_BountyTarget_Females_Dlc008_01"},
		{"MP_FM_BountyTarget_Males_Dlc008_01"},
		{"MP_FM_KnownBounty_Guards_01"},
		{"MP_FM_KnownBounty_Informants_Females_01"},
		{"MP_FM_KnownBounty_Informants_Males_01"},
		{"MP_FM_Multitrack_Victims_Males_01"},
		{"MP_FM_Stakeout_Corpses_Males_01"},
		{"MP_FM_Stakeout_Poker_Males_01"},
		{"MP_FM_Stakeout_Target_Males_01"},
		{"MP_FM_Track_Prospector_01"},
		{"MP_FM_Track_Sd_Lawman_01"},
		{"MP_FM_Track_Targets_Males_01"},
		{"MP_Freeroam_Tut_Females_01"},
		{"MP_Freeroam_Tut_Males_01"},
		{"MP_G_F_M_ArmyOfFear_01"},
		{"MP_G_F_M_CultGuards_01"},
		{"MP_G_F_M_CultMembers_01"},
		{"MP_G_F_M_LaPerleGang_01"},
		{"MP_G_F_M_LaPerleVips_01"},
		{"MP_G_F_M_OwlhootFamily_01"},
		{"MP_G_M_M_AnimalPoachers_01"},
		{"MP_G_M_M_ArmoredJuggernauts_01"},
		{"MP_G_M_M_ArmyOfFear_01"},
		{"MP_G_M_M_BountyHunters_01"},
		{"MP_G_M_M_CultGuards_01"},
		{"MP_G_M_M_CultMembers_01"},
		{"MP_G_M_M_Mercs_01"},
		{"MP_G_M_M_OwlhootFamily_01"},
		{"MP_G_M_M_RedbenGang_01"},
		{"MP_G_M_M_RifleCronies_01"},
		{"MP_G_M_M_UniAfricanAmericanGang_01"},
		{"MP_G_M_M_UniBanditos_01"},
		{"MP_G_M_M_UniBraithwaites_01"},
		{"MP_G_M_M_UniBronteGoons_01"},
		{"MP_G_M_M_UniCornwallGoons_01"},
		{"MP_G_M_M_UniCriminals_01"},
		{"MP_G_M_M_UniCriminals_02"},
		{"MP_G_M_M_UniCriminals_03"},
		{"MP_G_M_M_UniCriminals_04"},
		{"MP_G_M_M_UniCriminals_05"},
		{"MP_G_M_M_UniCriminals_06"},
		{"MP_G_M_M_UniCriminals_07"},
		{"MP_G_M_M_UniCriminals_08"},
		{"MP_G_M_M_UniCriminals_09"},
		{"MP_G_M_M_UniDuster_01"},
		{"MP_G_M_M_UniDuster_02"},
		{"MP_G_M_M_UniDuster_03"},
		{"MP_G_M_M_UniGrays_01"},
		{"MP_G_M_M_UniInbred_01"},
		{"MP_G_M_M_UniLangstonBoys_01"},
		{"MP_G_M_M_UniMountainMen_01"},
		{"MP_G_M_M_UniRanchers_01"},
		{"MP_G_M_M_UniSwamp_01"},
		{"MP_G_M_O_UniExConfeds_01"},
		{"MP_G_M_Y_UniExConfeds_01"},
		{"MP_GunvOutd2_Males_01"},
		{"MP_GunvOutd3_Bht_01"},
		{"MP_GunvOutd3_Males_01"},
		{"MP_Intercept_Recipient_Females_01"},
		{"MP_Intercept_Recipient_Males_01"},
		{"MP_Intro_Females_01"},
		{"MP_Intro_Males_01"},
		{"MP_Jailbreak_Males_01"},
		{"MP_Jailbreak_Recipient_Males_01"},
		{"MP_Lbm_Carmela_Banditos_01"},
		{"MP_Lbt_M3_Males_01"},
		{"MP_Lbt_M6_Females_01"},
		{"MP_Lbt_M6_Males_01"},
		{"MP_Lbt_M7_Males_01"},
		{"MP_LM_StealHorse_Buyers_01"},
		{"MP_Male"},
		{"MP_Oth_Recipient_Males_01"},
		{"MP_Outlaw1_Males_01"},
		{"MP_Outlaw2_Males_01"},
		{"MP_Post_MultiPackage_Females_01"},
		{"MP_Post_MultiPackage_Males_01"},
		{"MP_Post_MultiRelay_Females_01"},
		{"MP_Post_MultiRelay_Males_01"},
		{"MP_Post_Relay_Females_01"},
		{"MP_Post_Relay_Males_01"},
		{"MP_Predator"},
		{"MP_Prsn_Asn_Males_01"},
		{"MP_RE_AnimalAttack_Females_01"},
		{"MP_RE_AnimalAttack_Males_01"},
		{"MP_RE_Duel_Females_01"},
		{"MP_RE_Duel_Males_01"},
		{"MP_RE_GraveRobber_Females_01"},
		{"MP_RE_GraveRobber_Males_01"},
		{"MP_RE_HoboDog_Females_01"},
		{"MP_RE_HoboDog_Males_01"},
		{"MP_RE_Kidnapped_Females_01"},
		{"MP_RE_Kidnapped_Males_01"},
		{"MP_RE_MoonshineCamp_Males_01"},
		{"MP_RE_Photography_Females_01"},
		{"MP_RE_Photography_Females_02"},
		{"MP_RE_Photography_Males_01"},
		{"MP_RE_RivalCollector_Males_01"},
		{"MP_RE_RunawayWagon_Females_01"},
		{"MP_RE_RunawayWagon_Males_01"},
		{"MP_RE_SlumpedHunter_Females_01"},
		{"MP_RE_SlumpedHunter_Males_01"},
		{"MP_RE_SuspendedHunter_Males_01"},
		{"MP_RE_TreasureHunter_Females_01"},
		{"MP_RE_TreasureHunter_Males_01"},
		{"MP_RE_WildMan_Males_01"},
		{"MP_Recover_Recipient_Females_01"},
		{"MP_Recover_Recipient_Males_01"},
		{"MP_Repo_Recipient_Females_01"},
		{"MP_Repo_Recipient_Males_01"},
		{"MP_RepoBoat_Recipient_Females_01"},
		{"MP_RepoBoat_Recipient_Males_01"},
		{"MP_Rescue_BottleTree_Females_01"},
		{"MP_Rescue_BottleTree_Males_01"},
		{"MP_Rescue_Colter_Males_01"},
		{"MP_Rescue_CraterSacrifice_Males_01"},
		{"MP_Rescue_Heartlands_Males_01"},
		{"MP_Rescue_LoftKidnap_Males_01"},
		{"MP_Rescue_LonniesShack_Males_01"},
		{"MP_Rescue_Moonstone_Males_01"},
		{"MP_Rescue_MtnManShack_Males_01"},
		{"MP_Rescue_Recipient_Females_01"},
		{"MP_Rescue_Recipient_Males_01"},
		{"MP_Rescue_RivalShack_Males_01"},
		{"MP_Rescue_ScarlettMeadows_Males_01"},
		{"MP_Rescue_SdDogFight_Females_01"},
		{"MP_Rescue_SdDogFight_Males_01"},
		{"MP_Resupply_Recipient_Females_01"},
		{"MP_Resupply_Recipient_Males_01"},
		{"MP_Revenge1_Males_01"},
		{"MP_S_M_M_CornwallGuard_01"},
		{"MP_S_M_M_PinLaw_01"},
		{"MP_S_M_M_RevenueAgents_01"},
		{"MP_StealBoat_Recipient_Males_01"},
		{"MP_StealHorse_Recipient_Males_01"},
		{"MP_StealWagon_Recipient_Males_01"},
		{"MP_Tattoo_Female"},
		{"MP_Tattoo_Male"},
		{"MP_U_F_M_BountyTarget_001"},
		{"MP_U_F_M_BountyTarget_002"},
		{"MP_U_F_M_BountyTarget_003"},
		{"MP_U_F_M_BountyTarget_004"},
		{"MP_U_F_M_BountyTarget_005"},
		{"MP_U_F_M_BountyTarget_006"},
		{"MP_U_F_M_BountyTarget_007"},
		{"MP_U_F_M_BountyTarget_008"},
		{"MP_U_F_M_BountyTarget_009"},
		{"MP_U_F_M_BountyTarget_010"},
		{"MP_U_F_M_BountyTarget_011"},
		{"MP_U_F_M_BountyTarget_012"},
		{"MP_U_F_M_BountyTarget_013"},
		{"MP_U_F_M_BountyTarget_014"},
		{"MP_U_F_M_Buyer_Improved_01"},
		{"MP_U_F_M_Buyer_Improved_02"},
		{"MP_U_F_M_Buyer_Regular_01"},
		{"MP_U_F_M_Buyer_Regular_02"},
		{"MP_U_F_M_Buyer_Special_01"},
		{"MP_U_F_M_Buyer_Special_02"},
		{"MP_U_F_M_CultPriest_01"},
		{"MP_U_F_M_Gunslinger3_Rifleman_02"},
		{"MP_U_F_M_Gunslinger3_Sharpshooter_01"},
		{"MP_U_F_M_LaperleVipMasked_01"},
		{"MP_U_F_M_LaperleVipMasked_02"},
		{"MP_U_F_M_LaperleVipMasked_03"},
		{"MP_U_F_M_LaperleVipMasked_04"},
		{"MP_U_F_M_LaperleVipUnmasked_01"},
		{"MP_U_F_M_LaperleVipUnmasked_02"},
		{"MP_U_F_M_LaperleVipUnmasked_03"},
		{"MP_U_F_M_LaperleVipUnmasked_04"},
		{"MP_U_F_M_Lbt_OwlHootVictim_01"},
		{"MP_U_F_M_LegendaryBounty_001"},
		{"MP_U_F_M_LegendaryBounty_002"},
		{"MP_U_F_M_LegendaryBounty_03"},
		{"MP_U_F_M_Nat_Traveler_01"},
		{"MP_U_F_M_Nat_Worker_01"},
		{"MP_U_F_M_Nat_Worker_02"},
		{"MP_U_F_M_Outlaw3_Warner_01"},
		{"MP_U_F_M_Outlaw3_Warner_02"},
		{"MP_U_F_M_Revenge2_Passerby_01"},
		{"MP_U_F_M_SaloonPianist_01"},
		{"MP_U_M_M_AnimalPoacher_01"},
		{"MP_U_M_M_AnimalPoacher_02"},
		{"MP_U_M_M_AnimalPoacher_03"},
		{"MP_U_M_M_AnimalPoacher_04"},
		{"MP_U_M_M_AnimalPoacher_05"},
		{"MP_U_M_M_AnimalPoacher_06"},
		{"MP_U_M_M_AnimalPoacher_07"},
		{"MP_U_M_M_AnimalPoacher_08"},
		{"MP_U_M_M_AnimalPoacher_09"},
		{"MP_U_M_M_ArmSheriff_01"},
		{"MP_U_M_M_BankPrisoner_01"},
		{"MP_U_M_M_BinkMercs_01"},
		{"MP_U_M_M_BountyInjuredMan_01"},
		{"MP_U_M_M_BountyTarget_001"},
		{"MP_U_M_M_BountyTarget_002"},
		{"MP_U_M_M_BountyTarget_003"},
		{"MP_U_M_M_BountyTarget_005"},
		{"MP_U_M_M_BountyTarget_008"},
		{"MP_U_M_M_BountyTarget_009"},
		{"MP_U_M_M_BountyTarget_010"},
		{"MP_U_M_M_BountyTarget_011"},
		{"MP_U_M_M_BountyTarget_012"},
		{"MP_U_M_M_BountyTarget_013"},
		{"MP_U_M_M_BountyTarget_014"},
		{"MP_U_M_M_BountyTarget_015"},
		{"MP_U_M_M_BountyTarget_016"},
		{"MP_U_M_M_BountyTarget_017"},
		{"MP_U_M_M_BountyTarget_018"},
		{"MP_U_M_M_BountyTarget_019"},
		{"MP_U_M_M_BountyTarget_020"},
		{"MP_U_M_M_BountyTarget_021"},
		{"MP_U_M_M_BountyTarget_022"},
		{"MP_U_M_M_BountyTarget_023"},
		{"MP_U_M_M_BountyTarget_024"},
		{"MP_U_M_M_BountyTarget_025"},
		{"MP_U_M_M_BountyTarget_026"},
		{"MP_U_M_M_BountyTarget_027"},
		{"MP_U_M_M_BountyTarget_028"},
		{"MP_U_M_M_BountyTarget_029"},
		{"MP_U_M_M_BountyTarget_030"},
		{"MP_U_M_M_BountyTarget_031"},
		{"MP_U_M_M_BountyTarget_032"},
		{"MP_U_M_M_BountyTarget_033"},
		{"MP_U_M_M_BountyTarget_034"},
		{"MP_U_M_M_BountyTarget_035"},
		{"MP_U_M_M_BountyTarget_036"},
		{"MP_U_M_M_BountyTarget_037"},
		{"MP_U_M_M_BountyTarget_038"},
		{"MP_U_M_M_BountyTarget_039"},
		{"MP_U_M_M_BountyTarget_044"},
		{"MP_U_M_M_BountyTarget_045"},
		{"MP_U_M_M_BountyTarget_046"},
		{"MP_U_M_M_BountyTarget_047"},
		{"MP_U_M_M_BountyTarget_048"},
		{"MP_U_M_M_BountyTarget_049"},
		{"MP_U_M_M_BountyTarget_050"},
		{"MP_U_M_M_BountyTarget_051"},
		{"MP_U_M_M_BountyTarget_052"},
		{"MP_U_M_M_BountyTarget_053"},
		{"MP_U_M_M_BountyTarget_054"},
		{"MP_U_M_M_BountyTarget_055"},
		{"MP_U_M_M_Buyer_Default_01"},
		{"MP_U_M_M_Buyer_Improved_01"},
		{"MP_U_M_M_Buyer_Improved_02"},
		{"MP_U_M_M_Buyer_Improved_03"},
		{"MP_U_M_M_Buyer_Improved_04"},
		{"MP_U_M_M_Buyer_Improved_05"},
		{"MP_U_M_M_Buyer_Improved_06"},
		{"MP_U_M_M_Buyer_Improved_07"},
		{"MP_U_M_M_Buyer_Improved_08"},
		{"MP_U_M_M_Buyer_Regular_01"},
		{"MP_U_M_M_Buyer_Regular_02"},
		{"MP_U_M_M_Buyer_Regular_03"},
		{"MP_U_M_M_Buyer_Regular_04"},
		{"MP_U_M_M_Buyer_Regular_05"},
		{"MP_U_M_M_Buyer_Regular_06"},
		{"MP_U_M_M_Buyer_Regular_07"},
		{"MP_U_M_M_Buyer_Regular_08"},
		{"MP_U_M_M_Buyer_Special_01"},
		{"MP_U_M_M_Buyer_Special_02"},
		{"MP_U_M_M_Buyer_Special_03"},
		{"MP_U_M_M_Buyer_Special_04"},
		{"MP_U_M_M_Buyer_Special_05"},
		{"MP_U_M_M_Buyer_Special_06"},
		{"MP_U_M_M_Buyer_Special_07"},
		{"MP_U_M_M_Buyer_Special_08"},
		{"MP_U_M_M_CultPriest_01"},
		{"MP_U_M_M_DropOff_Josiah_01"},
		{"MP_U_M_M_DyingPoacher_01"},
		{"MP_U_M_M_DyingPoacher_02"},
		{"MP_U_M_M_DyingPoacher_03"},
		{"MP_U_M_M_DyingPoacher_04"},
		{"MP_U_M_M_DyingPoacher_05"},
		{"MP_U_M_M_GunForHireClerk_01"},
		{"MP_U_M_M_Gunslinger3_Rifleman_01"},
		{"MP_U_M_M_Gunslinger3_Sharpshooter_02"},
		{"MP_U_M_M_Gunslinger3_Shotgunner_01"},
		{"MP_U_M_M_Gunslinger3_Shotgunner_02"},
		{"MP_U_M_M_Gunslinger4_Warner_01"},
		{"MP_U_M_M_LawCamp_Lawman_01"},
		{"MP_U_M_M_LawCamp_Lawman_02"},
		{"MP_U_M_M_LawCamp_LeadOfficer_01"},
		{"MP_U_M_M_LawCamp_Prisoner_01"},
		{"MP_U_M_M_Lbt_Accomplice_01"},
		{"MP_U_M_M_Lbt_BarbsVictim_01"},
		{"MP_U_M_M_Lbt_BribeInformant_01"},
		{"MP_U_M_M_Lbt_CoachDriver_01"},
		{"MP_U_M_M_Lbt_HostageMarshal_01"},
		{"MP_U_M_M_Lbt_OwlhootVictim_01"},
		{"MP_U_M_M_Lbt_OwlhootVictim_02"},
		{"MP_U_M_M_Lbt_PhilipsVictim_01"},
		{"MP_U_M_M_LegendaryBounty_001"},
		{"MP_U_M_M_LegendaryBounty_002"},
		{"MP_U_M_M_LegendaryBounty_003"},
		{"MP_U_M_M_LegendaryBounty_004"},
		{"MP_U_M_M_LegendaryBounty_005"},
		{"MP_U_M_M_LegendaryBounty_006"},
		{"MP_U_M_M_LegendaryBounty_007"},
		{"MP_U_M_M_LegendaryBounty_08"},
		{"MP_U_M_M_LegendaryBounty_09"},
		{"MP_U_M_M_Nat_Farmer_01"},
		{"MP_U_M_M_Nat_Farmer_02"},
		{"MP_U_M_M_Nat_Farmer_03"},
		{"MP_U_M_M_Nat_Farmer_04"},
		{"MP_U_M_M_Nat_Photographer_01"},
		{"MP_U_M_M_Nat_Photographer_02"},
		{"MP_U_M_M_Nat_Rancher_01"},
		{"MP_U_M_M_Nat_Rancher_02"},
		{"MP_U_M_M_Nat_Townfolk_01"},
		{"MP_U_M_M_Outlaw3_Prisoner_01"},
		{"MP_U_M_M_Outlaw3_Prisoner_02"},
		{"MP_U_M_M_Outlaw3_Warner_01"},
		{"MP_U_M_M_Outlaw3_Warner_02"},
		{"MP_U_M_M_PrisonWagon_01"},
		{"MP_U_M_M_PrisonWagon_02"},
		{"MP_U_M_M_PrisonWagon_03"},
		{"MP_U_M_M_PrisonWagon_04"},
		{"MP_U_M_M_PrisonWagon_05"},
		{"MP_U_M_M_PrisonWagon_06"},
		{"MP_U_M_M_Revenge2_Handshaker_01"},
		{"MP_U_M_M_Revenge2_Passerby_01"},
		{"MP_U_M_M_Rhd_BountyTarget_01"},
		{"MP_U_M_M_Rhd_BountyTarget_02"},
		{"MP_U_M_M_Rhd_BountyTarget_03"},
		{"MP_U_M_M_Rhd_BountyTarget_03B"},
		{"MP_U_M_M_SaloonBrawler_01"},
		{"MP_U_M_M_SaloonBrawler_02"},
		{"MP_U_M_M_SaloonBrawler_03"},
		{"MP_U_M_M_SaloonBrawler_04"},
		{"MP_U_M_M_SaloonBrawler_05"},
		{"MP_U_M_M_SaloonBrawler_06"},
		{"MP_U_M_M_SaloonBrawler_07"},
		{"MP_U_M_M_SaloonBrawler_08"},
		{"MP_U_M_M_SaloonBrawler_09"},
		{"MP_U_M_M_SaloonBrawler_10"},
		{"MP_U_M_M_SaloonBrawler_11"},
		{"MP_U_M_M_SaloonBrawler_12"},
		{"MP_U_M_M_SaloonBrawler_13"},
		{"MP_U_M_M_SaloonBrawler_14"},
		{"MP_U_M_M_StrWelcomeCenter_02"},
		{"MP_U_M_M_Trader_01"},
		{"MP_U_M_M_TraderIntroClerk_01"},
		{"MP_U_M_M_TvlFence_01"},
		{"MP_U_M_O_BlwPoliceChief_01"},
		{"MP_WgnBrkout_Recipient_Males_01"},
		{"MP_WgnThief_Recipient_Males_01"}
	};

	static std::vector<Human> g_Story = {
		{"MSP_BountyHunter1_Females_01"},
		{"MSP_Braithwaites1_Males_01"},
		{"MSP_Feud1_Males_01"},
		{"MSP_Fussar2_Males_01"},
		{"MSP_Gang2_Males_01"},
		{"MSP_Gang3_Males_01"},
		{"MSP_Grays1_Males_01"},
		{"MSP_Grays2_Males_01"},
		{"MSP_Guarma2_Males_01"},
		{"MSP_Industry1_Females_01"},
		{"MSP_Industry1_Males_01"},
		{"MSP_Industry3_Females_01"},
		{"MSP_Industry3_Males_01"},
		{"MSP_Mary1_Females_01"},
		{"MSP_Mary1_Males_01"},
		{"MSP_Mary3_Males_01"},
		{"MSP_Mobo_Males_01"},
		{"MSP_Mob1_Females_01"},
		{"MSP_Mob1_Males_01"},
		{"MSP_Mob1_Teens_01"},
		{"MSP_Mob3_Females_01"},
		{"MSP_Mob3_Males_01"},
		{"MSP_Mudtown3_Males_01"},
		{"MSP_Mudtown3B_Females_01"},
		{"MSP_Mudtown3B_Males_01"},
		{"MSP_Mudtown5_Males_01"},
		{"MSP_Native1_Males_01"},
		{"MSP_Reverend1_Males_01"},
		{"MSP_SaintDenis1_Females_01"},
		{"MSP_SaintDenis1_Males_01"},
		{"MSP_Saloon1_Females_01"},
		{"MSP_Saloon1_Males_01"},
		{"MSP_Smuggler2_Males_01"},
		{"MSP_TrainRobbery2_Males_01"},
		{"MSP_Trelawny1_Males_01"},
		{"MSP_Utopia1_Males_01"},
		{"MSP_Winter4_Males_01"},
		{"RCES_Abigail3_Females_01"},
		{"RCES_Abigail3_Males_01"},
		{"RCES_Beechers1_Males_01"},
		{"RCES_EvelynMiller_Males_01"},
		{"RCSP_BeauAndPenelope1_Females_01"},
		{"RCSP_BeauAndPenelope_Males_01"},
		{"RCSP_Calderon_Males_01"},
		{"RCSP_CalderonStage2_Males_01"},
		{"RCSP_CalderonStage2_Teens_01"},
		{"RCSP_Calloway_Males_01"},
		{"RCSP_CoachRobbery_Males_01"},
		{"RCSP_Crackpot_Females_01"},
		{"RCSP_Crackpot_Males_01"},
		{"RCSP_Creole_Males_01"},
		{"RCSP_Dutch1_Males_01"},
		{"RCSP_Dutch3_Males_01"},
		{"RCSP_EdithDownes2_Males_01"},
		{"RCSP_ForMyArt_Females_01"},
		{"RCSP_ForMyArt_Males_01"},
		{"RCSP_GunslingerDuel4_Males_01"},
		{"RCSP_HereKittyKitty_Males_01"},
		{"RCSP_Hunting1_Males_01"},
		{"RCSP_MrMayor_Males_01"},
		{"RCSP_Native1S2_Males_01"},
		{"RCSP_Native_AmericanFathers_Males_01"},
		{"RCSP_OddFellows_Males_01"},
		{"RCSP_Odriscolls2_Females_01"},
		{"RCSP_PoisonedWell_Females_01"},
		{"RCSP_PoisonedWell_Males_01"},
		{"RCSP_PoisonedWell_Teens_01"},
		{"RCSP_RideTheLightning_Females_01"},
		{"RCSP_RideTheLightning_Males_01"},
		{"RCSP_Sadie1_Males_01"},
		{"RCSP_SlaveCatcher_Males_01"}
	};

	static std::vector<Human> g_RandomEvent = {
		{"RE_AnimalAttack_Females_01"},
		{"RE_AnimalAttack_Males_01"},
		{"RE_AnimalMauling_Males_01"},
		{"RE_Approach_Males_01"},
		{"RE_BearTrap_Males_01"},
		{"RE_BoatAttack_Males_01"},
		{"RE_BurningBodies_Males_01"},
		{"RE_Checkpoint_Males_01"},
		{"RE_CoachRobbery_Females_01"},
		{"RE_CoachRobbery_Males_01"},
		{"RE_Consequence_Males_01"},
		{"RE_CorpseCart_Females_01"},
		{"RE_CorpseCart_Males_01"},
		{"RE_CrashedWagon_Males_01"},
		{"RE_DarkAlleyAmbush_Males_01"},
		{"RE_DarkAlleyBum_Males_01"},
		{"RE_DarkAlleyStabbing_Males_01"},
		{"RE_DeadBodies_Males_01"},
		{"RE_DeadJohn_Females_01"},
		{"RE_DeadJohn_Males_01"},
		{"RE_DisabledBeggar_Males_01"},
		{"RE_DomesticDispute_Females_01"},
		{"RE_DomesticDispute_Males_01"},
		{"RE_DrownMurder_Females_01"},
		{"RE_DrownMurder_Males_01"},
		{"RE_DrunkCamp_Males_01"},
		{"RE_DrunkDueler_Males_01"},
		{"RE_DuelBoaster_Males_01"},
		{"RE_DuelWinner_Females_01"},
		{"RE_DuelWinner_Males_01"},
		{"RE_Escort_Females_01"},
		{"RE_Executions_Males_01"},
		{"RE_FleeingFamily_Females_01"},
		{"RE_FleeingFamily_Males_01"},
		{"RE_FootRobbery_Males_01"},
		{"RE_FriendlyOutdoorsman_Males_01"},
		{"RE_FrozenToDeath_Females_01"},
		{"RE_FrozenToDeath_Males_01"},
		{"RE_Fundraiser_Females_01"},
		{"RE_FussarChase_Males_01"},
		{"RE_GoldPanner_Males_01"},
		{"RE_HorseRace_Females_01"},
		{"RE_HorseRace_Males_01"},
		{"RE_HostageRescue_Females_01"},
		{"RE_HostageRescue_Males_01"},
		{"RE_InbredKidnap_Females_01"},
		{"RE_InbredKidnap_Males_01"},
		{"RE_InjuredRider_Males_01"},
		{"RE_KidnappedVictim_Females_01"},
		{"RE_LaramieGangRustling_Males_01"},
		{"RE_LonePrisoner_Males_01"},
		{"RE_LostDog_Teens_01"},
		{"RE_LostDrunk_Females_01"},
		{"RE_LostDrunk_Males_01"},
		{"RE_LostFriend_Males_01"},
		{"RE_LostMan_Males_01"},
		{"RE_MoonshineCamp_Males_01"},
		{"RE_MurderCamp_Males_01"},
		{"RE_MurderSuicide_Females_01"},
		{"RE_MurderSuicide_Males_01"},
		{"RE_NakedSwimmer_Males_01"},
		{"RE_OnTheRun_Males_01"},
		{"RE_OutlawLooter_Males_01"},
		{"RE_ParlorAmbush_Males_01"},
		{"RE_PeepingTom_Females_01"},
		{"RE_PeepingTom_Males_01"},
		{"RE_PickPocket_Males_01"},
		{"RE_Pisspot_Females_01"},
		{"RE_Pisspot_Males_01"},
		{"RE_PlayerCampStrangers_Females_01"},
		{"RE_PlayerCampStrangers_Males_01"},
		{"RE_Poisoned_Males_01"},
		{"RE_PoliceChase_Males_01"},
		{"RE_PrisonWagon_Females_01"},
		{"RE_PrisonWagon_Males_01"},
		{"RE_PublicHanging_Females_01"},
		{"RE_PublicHanging_Males_01"},
		{"RE_PublicHanging_Teens_01"},
		{"RE_Rally_Males_01"},
		{"RE_RallyDispute_Males_01"},
		{"RE_RallySetup_Males_01"},
		{"RE_RatInfestation_Males_01"},
		{"RE_RowdyDrunks_Males_01"},
		{"RE_SavageAftermath_Females_01"},
		{"RE_SavageAftermath_Males_01"},
		{"RE_SavageFight_Females_01"},
		{"RE_SavageFight_Males_01"},
		{"RE_SavageWagon_Females_01"},
		{"RE_SavageWagon_Males_01"},
		{"RE_SavageWarning_Males_01"},
		{"RE_SharpShooter_Males_01"},
		{"RE_ShowOff_Males_01"},
		{"RE_SkippingStones_Males_01"},
		{"RE_SkippingStones_Teens_01"},
		{"RE_SlumAmbush_Females_01"},
		{"RE_SnakeBite_Males_01"},
		{"RE_StalkingHunter_Males_01"},
		{"RE_StrandedRider_Males_01"},
		{"RE_Street_Fight_Males_01"},
		{"RE_Taunting_01"},
		{"RE_Taunting_Males_01"},
		{"RE_TorturingCaptive_Males_01"},
		{"RE_TownBurial_Males_01"},
		{"RE_TownConfrontation_Females_01"},
		{"RE_TownConfrontation_Males_01"},
		{"RE_TownRobbery_Males_01"},
		{"RE_TownWidow_Females_01"},
		{"RE_TrainHoldup_Females_01"},
		{"RE_TrainHoldup_Males_01"},
		{"RE_TrappedWoman_Females_01"},
		{"RE_TreasureHunter_Males_01"},
		{"RE_Voice_Females_01"},
		{"RE_WagonThreat_Females_01"},
		{"RE_WagonThreat_Males_01"},
		{"RE_WashedAshore_Males_01"},
		{"RE_WealthyCouple_Females_01"},
		{"RE_WealthyCouple_Males_01"},
		{"RE_WildMan_01"}
	};

	static std::vector<Human> g_Scenario = {
		{"S_F_M_BwmWorker_01"},
		{"S_F_M_CghWorker_01"},
		{"S_F_M_MapWorker_01"},
		{"S_M_M_AmbientBlwPolice_01"},
		{"S_M_M_AmbientLawRural_01"},
		{"S_M_M_AmbientSdPolice_01"},
		{"S_M_M_Army_01"},
		{"S_M_M_AsbCowpoke_01"},
		{"S_M_M_AsbDealer_01"},
		{"S_M_M_BankClerk_01"},
		{"S_M_M_Barber_01"},
		{"S_M_M_BlwCowpoke_01"},
		{"S_M_M_BlwDealer_01"},
		{"S_M_M_BwmWorker_01"},
		{"S_M_M_CghWorker_01"},
		{"S_M_M_CktWorker_01"},
		{"S_M_M_CoachTaxiDriver_01"},
		{"S_M_M_CornwallGuard_01"},
		{"S_M_M_DispatchLawRural_01"},
		{"S_M_M_DispatchLeaderPolice_01"},
		{"S_M_M_DispatchLeaderRural_01"},
		{"S_M_M_DispatchPolice_01"},
		{"S_M_M_FussarHenchman_01"},
		{"S_M_M_GenConductor_01"},
		{"S_M_M_HofGuard_01"},
		{"S_M_M_LiveryWorker_01"},
		{"S_M_M_MagicLantern_01"},
		{"S_M_M_MapWorker_01"},
		{"S_M_M_MarketVendor_01"},
		{"S_M_M_MarshallsRural_01"},
		{"S_M_M_MicGuard_01"},
		{"S_M_M_NbxRiverBoatDealers_01"},
		{"S_M_M_NbxRiverBoatGuards_01"},
		{"S_M_M_OrpGuard_01"},
		{"S_M_M_PinLaw_01"},
		{"S_M_M_RacRailGuards_01"},
		{"S_M_M_RacRailWorker_01"},
		{"S_M_M_RhdCowpoke_01"},
		{"S_M_M_RhdDealer_01"},
		{"S_M_M_SdCowpoke_01"},
		{"S_M_M_SdDealer_01"},
		{"S_M_M_SdTicketSeller_01"},
		{"S_M_M_SkpGuard_01"},
		{"S_M_M_StgSailor_01"},
		{"S_M_M_StrCowpoke_01"},
		{"S_M_M_StrDealer_01"},
		{"S_M_M_StrLumberjack_01"},
		{"S_M_M_Tailor_01"},
		{"S_M_M_TrainStationWorker_01"},
		{"S_M_M_TumDeputies_01"},
		{"S_M_M_UniButchers_01"},
		{"S_M_M_UniTrainEngineer_01"},
		{"S_M_M_UniTrainGuards_01"},
		{"S_M_M_ValBankGuards_01"},
		{"S_M_M_ValCowpoke_01"},
		{"S_M_M_ValDealer_01"},
		{"S_M_M_ValDeputy_01"},
		{"S_M_M_VhtDealer_01"},
		{"S_M_O_CktWorker_01"},
		{"S_M_Y_Army_01"},
		{"S_M_Y_NewspaperBoy_01"},
		{"S_M_Y_RacRailWorker_01"}
	};

	static std::vector<Human> g_StoryScenarioFemale = {
		{"U_F_M_Bht_Wife"},
		{"U_F_M_CircusWagon_01"},
		{"U_F_M_EmrDaughter_01"},
		{"U_F_M_Fussar1Lady_01"},
		{"U_F_M_HtlWife_01"},
		{"U_F_M_LagMother_01"},
		{"U_F_M_NbxResident_01"},
		{"U_F_M_RhdNudeWoman_01"},
		{"U_F_M_RksHomesteadTenant_01"},
		{"U_F_M_Story_BlackBelle_01"},
		{"U_F_M_Story_NightFolk_01"},
		{"U_F_M_TljBartender_01"},
		{"U_F_M_TumGeneralStoreOwner_01"},
		{"U_F_M_ValTownfolk_01"},
		{"U_F_M_ValTownfolk_02"},
		{"U_F_M_VhtBartender_01"},
		{"U_F_O_Hermit_Woman_01"},
		{"U_F_O_WtcTownfolk_01"},
		{"U_F_Y_BraithwaitesSecret_01"},
		{"U_F_Y_CzpHomesteadDaughter_01"}
	};

	static std::vector<Human> g_StoryScenarioMale = {
		{"U_M_M_Announcer_01"},
		{"U_M_M_ApfDeadman_01"},
		{"U_M_M_ArmGeneralStoreOwner_01"},
		{"U_M_M_ArmTrainStationWorker_01"},
		{"U_M_M_ArmUndertaker_01"},
		{"U_M_M_Armytrn4_01"},
		{"U_M_M_AsbGunsmith_01"},
		{"U_M_M_AsbPrisoner_01"},
		{"U_M_M_AsbPrisoner_02"},
		{"U_M_M_Bht_BanditoMine"},
		{"U_M_M_Bht_BanditoShack"},
		{"U_M_M_Bht_BenedictAllbright"},
		{"U_M_M_Bht_BlackwaterHunt"},
		{"U_M_M_Bht_ExconfedCampReturn"},
		{"U_M_M_Bht_LaramieSleeping"},
		{"U_M_M_Bht_Lover"},
		{"U_M_M_Bht_MineForeman"},
		{"U_M_M_Bht_NathanKirk"},
		{"U_M_M_Bht_OdriscollDrunk"},
		{"U_M_M_Bht_OdriscollMauled"},
		{"U_M_M_Bht_OdriscollSleeping"},
		{"U_M_M_Bht_OldMan"},
		{"U_M_M_Bht_OutlawMauled"},
		{"U_M_M_Bht_SaintDenisSaloon"},
		{"U_M_M_Bht_ShackEscape"},
		{"U_M_M_Bht_SkinnerBrother"},
		{"U_M_M_Bht_SkinnerSearch"},
		{"U_M_M_Bht_StrawberryDuel"},
		{"U_M_M_BivForeman_01"},
		{"U_M_M_BlwTrainStationWorker_01"},
		{"U_M_M_BulletCatchVolunteer_01"},
		{"U_M_M_BwmStableHand_01"},
		{"U_M_M_CabaretFireHat_01"},
		{"U_M_M_CajHomestead_01"},
		{"U_M_M_ChelonianJumper_01"},
		{"U_M_M_ChelonianJumper_02"},
		{"U_M_M_ChelonianJumper_03"},
		{"U_M_M_ChelonianJumper_04"},
		{"U_M_M_CircusWagon_01"},
		{"U_M_M_CktManager_01"},
		{"U_M_M_CornwallDriver_01"},
		{"U_M_M_CrdHomesteadTenant_01"},
		{"U_M_M_CrdHomesteadTenant_02"},
		{"U_M_M_CrdWitness_01"},
		{"U_M_M_CreoleCaptain_01"},
		{"U_M_M_CzpHomesteadFather_01"},
		{"U_M_M_DorHomesteadHusband_01"},
		{"U_M_M_EmrFarmHand_03"},
		{"U_M_M_EmrFather_01"},
		{"U_M_M_Executioner_01"},
		{"U_M_M_FatDuster_01"},
		{"U_M_M_Finale2_Aa_UpperClass_01"},
		{"U_M_M_GalaStringQuartet_01"},
		{"U_M_M_GalaStringQuartet_02"},
		{"U_M_M_GalaStringQuartet_03"},
		{"U_M_M_GalaStringQuartet_04"},
		{"U_M_M_GamDoorman_01"},
		{"U_M_M_HhrRancher_01"},
		{"U_M_M_HtlForeman_01"},
		{"U_M_M_HtlHusband_01"},
		{"U_M_M_HtlRancherBounty_01"},
		{"U_M_M_IslBum_01"},
		{"U_M_M_LnsOutlaw_01"},
		{"U_M_M_LnsOutlaw_02"},
		{"U_M_M_LnsOutlaw_03"},
		{"U_M_M_LnsOutlaw_04"},
		{"U_M_M_LnsWorker_01"},
		{"U_M_M_LnsWorker_02"},
		{"U_M_M_LnsWorker_03"},
		{"U_M_M_LnsWorker_04"},
		{"U_M_M_LrsHomesteadTenant_01"},
		{"U_M_M_MfrRancher_01"},
		{"U_M_M_Mud3Pimp_01"},
		{"U_M_M_NbxBankerBounty_01"},
		{"U_M_M_NbxBartender_01"},
		{"U_M_M_NbxBartender_02"},
		{"U_M_M_NbxBoatTicketSeller_01"},
		{"U_M_M_NbxBronteAsc_01"},
		{"U_M_M_NbxBronteGoon_01"},
		{"U_M_M_NbxBronteSecForm_01"},
		{"U_M_M_NbxGeneralStoreOwner_01"},
		{"U_M_M_NbxGraveRobber_01"},
		{"U_M_M_NbxGraveRobber_02"},
		{"U_M_M_NbxGraveRobber_03"},
		{"U_M_M_NbxGraveRobber_04"},
		{"U_M_M_NbxGraveRobber_05"},
		{"U_M_M_NbxGunsmith_01"},
		{"U_M_M_NbxLiveryWorker_01"},
		{"U_M_M_NbxMusician_01"},
		{"U_M_M_NbxPriest_01"},
		{"U_M_M_NbxResident_01"},
		{"U_M_M_NbxResident_02"},
		{"U_M_M_NbxResident_03"},
		{"U_M_M_NbxResident_04"},
		{"U_M_M_NbxRiverboatPitBoss_01"},
		{"U_M_M_NbxRiverboatTarget_01"},
		{"U_M_M_NbxShadyDealer_01"},
		{"U_M_M_NbxSkiffDriver_01"},
		{"U_M_M_OddfellowParticipant_01"},
		{"U_M_M_OdriscollBrawler_01"},
		{"U_M_M_OrpGuard_01"},
		{"U_M_M_RacForeman_01"},
		{"U_M_M_RacQuartermaster_01"},
		{"U_M_M_RhdBackupDeputy_01"},
		{"U_M_M_RhdBackupDeputy_02"},
		{"U_M_M_RhdBartender_01"},
		{"U_M_M_RhdDoctor_01"},
		{"U_M_M_RhdFiddlePlayer_01"},
		{"U_M_M_RhdGenStoreOwner_01"},
		{"U_M_M_RhdGenStoreOwner_02"},
		{"U_M_M_RhdGunsmith_01"},
		{"U_M_M_RhdPreacher_01"},
		{"U_M_M_RhdSheriff_01"},
		{"U_M_M_RhdTrainStationWorker_01"},
		{"U_M_M_RhdUndertaker_01"},
		{"U_M_M_RioDonkeyRider_01"},
		{"U_M_M_RkfRancher_01"},
		{"U_M_M_RkrDonkeyRider_01"},
		{"U_M_M_RwfRancher_01"},
		{"U_M_M_SdBankGuard_01"},
		{"U_M_M_SdCustomVendor_01"},
		{"U_M_M_SdExoticsShopkeeper_01"},
		{"U_M_M_SdPhotographer_01"},
		{"U_M_M_SdPoliceChief_01"},
		{"U_M_M_SdStrongWomanAssistant_01"},
		{"U_M_M_SdTrapper_01"},
		{"U_M_M_SdWealthyTraveller_01"},
		{"U_M_M_ShackSerialKiller_01"},
		{"U_M_M_ShackTwin_01"},
		{"U_M_M_ShackTwin_02"},
		{"U_M_M_SkinnyOldGuy_01"},
		{"U_M_M_Story_Armadillo_01"},
		{"U_M_M_Story_Cannibal_01"},
		{"U_M_M_Story_Chelonian_01"},
		{"U_M_M_Story_Copperhead_01"},
		{"U_M_M_Story_Creeper_01"},
		{"U_M_M_Story_EmeraldRanch_01"},
		{"U_M_M_Story_Hunter_01"},
		{"U_M_M_Story_Manzanita_01"},
		{"U_M_M_Story_Murfee_01"},
		{"U_M_M_Story_PigFarm_01"},
		{"U_M_M_Story_Princess_01"},
		{"U_M_M_Story_RedHarlow_01"},
		{"U_M_M_Story_Rhodes_01"},
		{"U_M_M_Story_SdStatue_01"},
		{"U_M_M_Story_Spectre_01"},
		{"U_M_M_Story_Treasure_01"},
		{"U_M_M_Story_Tumbleweed_01"},
		{"U_M_M_Story_Valentine_01"},
		{"U_M_M_StrFreightStationOwner_01"},
		{"U_M_M_StrGenStoreOwner_01"},
		{"U_M_M_StrSherriff_01"},
		{"U_M_M_StrWelcomeCenter_01"},
		{"U_M_M_TumBartender_01"},
		{"U_M_M_TumButcher_01"},
		{"U_M_M_TumGunsmith_01"},
		{"U_M_M_TumTrainStationWorker_01"},
		{"U_M_M_UniBountyHunter_01"},
		{"U_M_M_UniBountyHunter_02"},
		{"U_M_M_UniDusterHenchman_01"},
		{"U_M_M_UniDusterHenchman_02"},
		{"U_M_M_UniDusterHenchman_03"},
		{"U_M_M_UniDusterLeader_01"},
		{"U_M_M_UniExConfedsBounty_01"},
		{"U_M_M_UnionLeader_01"},
		{"U_M_M_UnionLeader_02"},
		{"U_M_M_UniPeepingTom_01"},
		{"U_M_M_ValAuctionForman_01"},
		{"U_M_M_ValAuctionForman_02"},
		{"U_M_M_ValBarber_01"},
		{"U_M_M_ValBartender_01"},
		{"U_M_M_ValBearTrap_01"},
		{"U_M_M_ValButcher_01"},
		{"U_M_M_ValDoctor_01"},
		{"U_M_M_ValGenStoreOwner_01"},
		{"U_M_M_ValGunsmith_01"},
		{"U_M_M_ValHotelOwner_01"},
		{"U_M_M_ValPokerPlayer_01"},
		{"U_M_M_ValPokerPlayer_02"},
		{"U_M_M_ValPoopingMan_01"},
		{"U_M_M_ValSheriff_01"},
		{"U_M_M_ValTheMan_01"},
		{"U_M_M_ValTownFolk_01"},
		{"U_M_M_ValTownFolk_02"},
		{"U_M_M_VhtStationClerk_01"},
		{"U_M_M_WalGeneralStoreOwner_01"},
		{"U_M_M_WapOfficial_01"},
		{"U_M_M_WtcCowboy_04"},
		{"U_M_O_ArmBartender_01"},
		{"U_M_O_AsbSheriff_01"},
		{"U_M_O_Bht_DocWormwood"},
		{"U_M_O_BlwBartender_01"},
		{"U_M_O_BlwGeneralStoreOwner_01"},
		{"U_M_O_BlwPhotographer_01"},
		{"U_M_O_BlwPoliceChief_01"},
		{"U_M_O_CajHomestead_01"},
		{"U_M_O_CmrCivilWarCommando_01"},
		{"U_M_O_MapWiseOldMan_01"},
		{"U_M_O_OldCajun_01"},
		{"U_M_O_PshRancher_01"},
		{"U_M_O_RigTrainStationWorker_01"},
		{"U_M_O_ValBartender_01"},
		{"U_M_O_VhtExoticShopkeeper_01"},
		{"U_M_Y_CajHomestead_01"},
		{"U_M_Y_CzpHomesteadSon_01"},
		{"U_M_Y_CzpHomesteadSon_02"},
		{"U_M_Y_CzpHomesteadSon_03"},
		{"U_M_Y_CzpHomesteadSon_04"},
		{"U_M_Y_CzpHomesteadSon_05"},
		{"U_M_Y_DuellistBounty_01"},
		{"U_M_Y_EmrSon_01"},
		{"U_M_Y_HtlWorker_01"},
		{"U_M_Y_HtlWorker_02"},
		{"U_M_Y_ShackStarvingKid_01"}
	};

	static std::vector<Human> g_Miscellaneous = {
		{"AM_ValentineDoctors_Females_01"},
		{"AMSP_RobsdGunsmith_Males_01"},
		{"CASP_CoachRobbery_Lenny_Males_01"},
		{"CASP_CoachRobbery_Micah_Males_01"},
		{"CASP_Hunting02_Males_01"},
		{"Charro_Saddle_01"},
		{"CR_Strawberry_Males_01"},
		{"Female_Skeleton"},
		{"GC_LemoyneCaptive_Males_01"},
		{"GC_SkinnerTorture_Males_01"},
		{"GE_DelloboParty_Females_01"},
		{"LoanSharking_AsbMiner_Males_01"},
		{"LoanSharking_HorseChase1_Males_01"},
		{"LoanSharking_Undertaker_Females_01"},
		{"LoanSharking_Undertaker_Males_01"},
		{"Male_Skeleton"},
		{"MBH_RhodesRancher_Females_01"},
		{"MBH_RhodesRancher_Teens_01"},
		{"MBH_SkinnerSearch_Males_01"},
		{"Player_Zero"},
		{"Player_Three"},
		{"Shack_MissingHusband_Males_01"},
		{"Shack_OnTheRun_Males_01"}
	};

	static void RenderHumansView()
	{
		// back button in top-right corner
		ImVec2 windowSize = ImGui::GetContentRegionAvail();
		ImVec2 originalPos = ImGui::GetCursorPos();

		ImGui::SetCursorPos(ImVec2(windowSize.x - 30, 5));
		if (ImGui::Button("X", ImVec2(25, 25)))
		{
			g_InHumans = false;
		}

		// reset cursor to original position and add some top margin
		ImGui::SetCursorPos(ImVec2(originalPos.x, originalPos.y + 35));

		// helper function for centered separator with custom line width
		auto RenderCenteredSeparator = [](const char* text) {
			ImGui::PushFont(Menu::Font::g_ChildTitleFont);
			ImVec2 textSize = ImGui::CalcTextSize(text);
			ImVec2 contentRegion = ImGui::GetContentRegionAvail();

			// center text
			ImGui::SetCursorPosX((contentRegion.x - textSize.x) * 0.5f);
			ImGui::Text(text);
			ImGui::PopFont();

			// draw centered line (3x text width) using screen coordinates
			float lineWidth = textSize.x * 3.0f;
			float linePosX = (contentRegion.x - lineWidth) * 0.5f;

			ImDrawList* drawList = ImGui::GetWindowDrawList();
			ImVec2 windowPos = ImGui::GetWindowPos();
			ImVec2 cursorScreenPos = ImGui::GetCursorScreenPos();

			// use screen coordinates for proper positioning with scrolling
			drawList->AddLine(
				ImVec2(windowPos.x + linePosX, cursorScreenPos.y),
				ImVec2(windowPos.x + linePosX + lineWidth, cursorScreenPos.y),
				ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);

			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
			ImGui::Spacing();
		};

		// lambda function for getting human model names
		auto getHumanName = [](const Human& human) { return human.model; };

		// check section matches
		bool ambientFemaleMatches = g_HumanSearch.SectionMatches("Ambient Female", g_HumanSearch.searchBuffer);
		bool ambientFemaleOrdinaryMatches = g_HumanSearch.SectionMatches("Ambient Female Ordinary", g_HumanSearch.searchBuffer);
		bool ambientMaleMatches = g_HumanSearch.SectionMatches("Ambient Male", g_HumanSearch.searchBuffer);
		bool ambientMaleOrdinaryMatches = g_HumanSearch.SectionMatches("Ambient Male Ordinary", g_HumanSearch.searchBuffer);
		bool ambientMaleSuppressedMatches = g_HumanSearch.SectionMatches("Ambient Male Suppressed", g_HumanSearch.searchBuffer);
		bool cutsceneMatches = g_HumanSearch.SectionMatches("Cutscene", g_HumanSearch.searchBuffer);
		bool multiplayerCutsceneMatches = g_HumanSearch.SectionMatches("Multiplayer Cutscene", g_HumanSearch.searchBuffer);
		bool gangMatches = g_HumanSearch.SectionMatches("Gang", g_HumanSearch.searchBuffer);
		bool storyFinaleMatches = g_HumanSearch.SectionMatches("Story Finale", g_HumanSearch.searchBuffer);
		bool multiplayerBloodMoneyMatches = g_HumanSearch.SectionMatches("Multiplayer Blood Money", g_HumanSearch.searchBuffer);
		bool multiplayerBountyHuntersMatches = g_HumanSearch.SectionMatches("Multiplayer Bounty Hunters", g_HumanSearch.searchBuffer);
		bool multiplayerNaturalistMatches = g_HumanSearch.SectionMatches("Multiplayer Naturalist", g_HumanSearch.searchBuffer);
		bool multiplayerMatches = g_HumanSearch.SectionMatches("Multiplayer", g_HumanSearch.searchBuffer);
		bool storyMatches = g_HumanSearch.SectionMatches("Story", g_HumanSearch.searchBuffer);
		bool randomEventMatches = g_HumanSearch.SectionMatches("Random Event", g_HumanSearch.searchBuffer);
		bool scenarioMatches = g_HumanSearch.SectionMatches("Scenario", g_HumanSearch.searchBuffer);
		bool storyScenarioFemaleMatches = g_HumanSearch.SectionMatches("Story Scenario Female", g_HumanSearch.searchBuffer);
		bool storyScenarioMaleMatches = g_HumanSearch.SectionMatches("Story Scenario Male", g_HumanSearch.searchBuffer);
		bool miscellaneousMatches = g_HumanSearch.SectionMatches("Miscellaneous", g_HumanSearch.searchBuffer);

		// count visible humans in each section
		int ambientFemaleVisible = ambientFemaleMatches ? static_cast<int>(g_AmbientFemale.size()) :
		                          g_HumanSearch.CountMatches(g_AmbientFemale, g_HumanSearch.searchBuffer, getHumanName);
		int ambientFemaleOrdinaryVisible = ambientFemaleOrdinaryMatches ? static_cast<int>(g_AmbientFemaleOrdinary.size()) :
		                                  g_HumanSearch.CountMatches(g_AmbientFemaleOrdinary, g_HumanSearch.searchBuffer, getHumanName);
		int ambientMaleVisible = ambientMaleMatches ? static_cast<int>(g_AmbientMale.size()) :
		                        g_HumanSearch.CountMatches(g_AmbientMale, g_HumanSearch.searchBuffer, getHumanName);
		int ambientMaleOrdinaryVisible = ambientMaleOrdinaryMatches ? static_cast<int>(g_AmbientMaleOrdinary.size()) :
		                                g_HumanSearch.CountMatches(g_AmbientMaleOrdinary, g_HumanSearch.searchBuffer, getHumanName);
		int ambientMaleSuppressedVisible = ambientMaleSuppressedMatches ? static_cast<int>(g_AmbientMaleSuppressed.size()) :
		                                  g_HumanSearch.CountMatches(g_AmbientMaleSuppressed, g_HumanSearch.searchBuffer, getHumanName);
		int cutsceneVisible = cutsceneMatches ? static_cast<int>(g_Cutscene.size()) :
		                     g_HumanSearch.CountMatches(g_Cutscene, g_HumanSearch.searchBuffer, getHumanName);
		int multiplayerCutsceneVisible = multiplayerCutsceneMatches ? static_cast<int>(g_MultiplayerCutscene.size()) :
		                                g_HumanSearch.CountMatches(g_MultiplayerCutscene, g_HumanSearch.searchBuffer, getHumanName);
		int gangVisible = gangMatches ? static_cast<int>(g_Gang.size()) :
		                 g_HumanSearch.CountMatches(g_Gang, g_HumanSearch.searchBuffer, getHumanName);
		int storyFinaleVisible = storyFinaleMatches ? static_cast<int>(g_StoryFinale.size()) :
		                        g_HumanSearch.CountMatches(g_StoryFinale, g_HumanSearch.searchBuffer, getHumanName);
		int multiplayerBloodMoneyVisible = multiplayerBloodMoneyMatches ? static_cast<int>(g_MultiplayerBloodMoney.size()) :
		                                  g_HumanSearch.CountMatches(g_MultiplayerBloodMoney, g_HumanSearch.searchBuffer, getHumanName);
		int multiplayerBountyHuntersVisible = multiplayerBountyHuntersMatches ? static_cast<int>(g_MultiplayerBountyHunters.size()) :
		                                     g_HumanSearch.CountMatches(g_MultiplayerBountyHunters, g_HumanSearch.searchBuffer, getHumanName);
		int multiplayerNaturalistVisible = multiplayerNaturalistMatches ? static_cast<int>(g_MultiplayerNaturalist.size()) :
		                                  g_HumanSearch.CountMatches(g_MultiplayerNaturalist, g_HumanSearch.searchBuffer, getHumanName);
		int multiplayerVisible = multiplayerMatches ? static_cast<int>(g_Multiplayer.size()) :
		                        g_HumanSearch.CountMatches(g_Multiplayer, g_HumanSearch.searchBuffer, getHumanName);
		int storyVisible = storyMatches ? static_cast<int>(g_Story.size()) :
		                  g_HumanSearch.CountMatches(g_Story, g_HumanSearch.searchBuffer, getHumanName);
		int randomEventVisible = randomEventMatches ? static_cast<int>(g_RandomEvent.size()) :
		                        g_HumanSearch.CountMatches(g_RandomEvent, g_HumanSearch.searchBuffer, getHumanName);
		int scenarioVisible = scenarioMatches ? static_cast<int>(g_Scenario.size()) :
		                     g_HumanSearch.CountMatches(g_Scenario, g_HumanSearch.searchBuffer, getHumanName);
		int storyScenarioFemaleVisible = storyScenarioFemaleMatches ? static_cast<int>(g_StoryScenarioFemale.size()) :
		                                g_HumanSearch.CountMatches(g_StoryScenarioFemale, g_HumanSearch.searchBuffer, getHumanName);
		int storyScenarioMaleVisible = storyScenarioMaleMatches ? static_cast<int>(g_StoryScenarioMale.size()) :
		                              g_HumanSearch.CountMatches(g_StoryScenarioMale, g_HumanSearch.searchBuffer, getHumanName);
		int miscellaneousVisible = miscellaneousMatches ? static_cast<int>(g_Miscellaneous.size()) :
		                          g_HumanSearch.CountMatches(g_Miscellaneous, g_HumanSearch.searchBuffer, getHumanName);

		// determine section visibility
		bool showAmbientFemale = ambientFemaleMatches || (ambientFemaleVisible > 0);
		bool showAmbientFemaleOrdinary = ambientFemaleOrdinaryMatches || (ambientFemaleOrdinaryVisible > 0);
		bool showAmbientMale = ambientMaleMatches || (ambientMaleVisible > 0);
		bool showAmbientMaleOrdinary = ambientMaleOrdinaryMatches || (ambientMaleOrdinaryVisible > 0);
		bool showAmbientMaleSuppressed = ambientMaleSuppressedMatches || (ambientMaleSuppressedVisible > 0);
		bool showCutscene = cutsceneMatches || (cutsceneVisible > 0);
		bool showMultiplayerCutscene = multiplayerCutsceneMatches || (multiplayerCutsceneVisible > 0);
		bool showGang = gangMatches || (gangVisible > 0);
		bool showStoryFinale = storyFinaleMatches || (storyFinaleVisible > 0);
		bool showMultiplayerBloodMoney = multiplayerBloodMoneyMatches || (multiplayerBloodMoneyVisible > 0);
		bool showMultiplayerBountyHunters = multiplayerBountyHuntersMatches || (multiplayerBountyHuntersVisible > 0);
		bool showMultiplayerNaturalist = multiplayerNaturalistMatches || (multiplayerNaturalistVisible > 0);
		bool showMultiplayer = multiplayerMatches || (multiplayerVisible > 0);
		bool showStory = storyMatches || (storyVisible > 0);
		bool showRandomEvent = randomEventMatches || (randomEventVisible > 0);
		bool showScenario = scenarioMatches || (scenarioVisible > 0);
		bool showStoryScenarioFemale = storyScenarioFemaleMatches || (storyScenarioFemaleVisible > 0);
		bool showStoryScenarioMale = storyScenarioMaleMatches || (storyScenarioMaleVisible > 0);
		bool showMiscellaneous = miscellaneousMatches || (miscellaneousVisible > 0);

		// calculate totals
		int totalHumans = static_cast<int>(g_AmbientFemale.size() + g_AmbientFemaleOrdinary.size() + g_AmbientMale.size() +
		                                  g_AmbientMaleOrdinary.size() + g_AmbientMaleSuppressed.size() + g_Cutscene.size() +
		                                  g_MultiplayerCutscene.size() + g_Gang.size() + g_StoryFinale.size() +
		                                  g_MultiplayerBloodMoney.size() + g_MultiplayerBountyHunters.size() +
		                                  g_MultiplayerNaturalist.size() + g_Multiplayer.size() + g_Story.size() +
		                                  g_RandomEvent.size() + g_Scenario.size() + g_StoryScenarioFemale.size() +
		                                  g_StoryScenarioMale.size() + g_Miscellaneous.size());
		int totalVisible = ambientFemaleVisible + ambientFemaleOrdinaryVisible + ambientMaleVisible +
		                  ambientMaleOrdinaryVisible + ambientMaleSuppressedVisible + cutsceneVisible +
		                  multiplayerCutsceneVisible + gangVisible + storyFinaleVisible + multiplayerBloodMoneyVisible +
		                  multiplayerBountyHuntersVisible + multiplayerNaturalistVisible + multiplayerVisible +
		                  storyVisible + randomEventVisible + scenarioVisible + storyScenarioFemaleVisible +
		                  storyScenarioMaleVisible + miscellaneousVisible;

		// render search bar with count
		g_HumanSearch.RenderSearchBar("Search Humans", totalHumans, totalVisible);

		// variation input field beside search bar (same as main spawner)
		ImGui::SetNextItemWidth(150);
		ImGui::InputInt("Set Variation", &g_Variation);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("outfit variation number (0 = random/default) - affects both spawn and set model actions");

		ImGui::Spacing();

		// ambient female section
		if (showAmbientFemale)
		{
			RenderCenteredSeparator("Ambient Female");

			for (size_t i = 0; i < g_AmbientFemale.size(); ++i)
			{
				const auto& human = g_AmbientFemale[i];
				if (g_HumanSearch.ShouldShowItem(human, ambientFemaleMatches, getHumanName))
				{
					// use unique ID with section prefix to prevent cross-section duplicates
					ImGui::PushID(("AmbientFemale_" + human.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(human.model.c_str(), ImVec2(-1, 25)))
					{
						if (g_SetModelMode)
						{
							SetPlayerModel(human.model, g_Variation);
						}
						else
						{
							SpawnPed(human.model, g_Variation, g_Armed);
						}
					}
					ImGui::PopID();
				}
			}

			ImGui::Spacing();
		}

		// ambient female ordinary section
		if (showAmbientFemaleOrdinary)
		{
			RenderCenteredSeparator("Ambient Female Ordinary");

			for (size_t i = 0; i < g_AmbientFemaleOrdinary.size(); ++i)
			{
				const auto& human = g_AmbientFemaleOrdinary[i];
				if (g_HumanSearch.ShouldShowItem(human, ambientFemaleOrdinaryMatches, getHumanName))
				{
					// use unique ID with section prefix to prevent cross-section duplicates
					ImGui::PushID(("AmbientFemaleOrdinary_" + human.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(human.model.c_str(), ImVec2(-1, 25)))
					{
						if (g_SetModelMode)
						{
							SetPlayerModel(human.model, g_Variation);
						}
						else
						{
							SpawnPed(human.model, g_Variation, g_Armed);
						}
					}
					ImGui::PopID();
				}
			}

			ImGui::Spacing();
		}

		// ambient male section
		if (showAmbientMale)
		{
			RenderCenteredSeparator("Ambient Male");

			for (size_t i = 0; i < g_AmbientMale.size(); ++i)
			{
				const auto& human = g_AmbientMale[i];
				if (g_HumanSearch.ShouldShowItem(human, ambientMaleMatches, getHumanName))
				{
					// use unique ID with section prefix to prevent cross-section duplicates
					ImGui::PushID(("AmbientMale_" + human.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(human.model.c_str(), ImVec2(-1, 25)))
					{
						if (g_SetModelMode)
						{
							SetPlayerModel(human.model, g_Variation);
						}
						else
						{
							SpawnPed(human.model, g_Variation, g_Armed);
						}
					}
					ImGui::PopID();
				}
			}

			ImGui::Spacing();
		}

		// ambient male ordinary section
		if (showAmbientMaleOrdinary)
		{
			RenderCenteredSeparator("Ambient Male Ordinary");

			for (size_t i = 0; i < g_AmbientMaleOrdinary.size(); ++i)
			{
				const auto& human = g_AmbientMaleOrdinary[i];
				if (g_HumanSearch.ShouldShowItem(human, ambientMaleOrdinaryMatches, getHumanName))
				{
					// use unique ID with section prefix to prevent cross-section duplicates
					ImGui::PushID(("AmbientMaleOrdinary_" + human.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(human.model.c_str(), ImVec2(-1, 25)))
					{
						if (g_SetModelMode)
						{
							SetPlayerModel(human.model, g_Variation);
						}
						else
						{
							SpawnPed(human.model, g_Variation, g_Armed);
						}
					}
					ImGui::PopID();
				}
			}

			ImGui::Spacing();
		}

		// ambient male suppressed section
		if (showAmbientMaleSuppressed)
		{
			RenderCenteredSeparator("Ambient Male Suppressed");

			for (size_t i = 0; i < g_AmbientMaleSuppressed.size(); ++i)
			{
				const auto& human = g_AmbientMaleSuppressed[i];
				if (g_HumanSearch.ShouldShowItem(human, ambientMaleSuppressedMatches, getHumanName))
				{
					// use unique ID with section prefix to prevent cross-section duplicates
					ImGui::PushID(("AmbientMaleSuppressed_" + human.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(human.model.c_str(), ImVec2(-1, 25)))
					{
						if (g_SetModelMode)
						{
							SetPlayerModel(human.model, g_Variation);
						}
						else
						{
							SpawnPed(human.model, g_Variation, g_Armed);
						}
					}
					ImGui::PopID();
				}
			}

			ImGui::Spacing();
		}

		// cutscene section
		if (showCutscene)
		{
			RenderCenteredSeparator("Cutscene");

			for (size_t i = 0; i < g_Cutscene.size(); ++i)
			{
				const auto& human = g_Cutscene[i];
				if (g_HumanSearch.ShouldShowItem(human, cutsceneMatches, getHumanName))
				{
					// use unique ID with section prefix to prevent cross-section duplicates
					ImGui::PushID(("Cutscene_" + human.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(human.model.c_str(), ImVec2(-1, 25)))
					{
						if (g_SetModelMode)
						{
							SetPlayerModel(human.model, g_Variation);
						}
						else
						{
							SpawnPed(human.model, g_Variation, g_Armed);
						}
					}
					ImGui::PopID();
				}
			}

			ImGui::Spacing();
		}

		// multiplayer cutscene section
		if (showMultiplayerCutscene)
		{
			RenderCenteredSeparator("Multiplayer Cutscene");

			for (size_t i = 0; i < g_MultiplayerCutscene.size(); ++i)
			{
				const auto& human = g_MultiplayerCutscene[i];
				if (g_HumanSearch.ShouldShowItem(human, multiplayerCutsceneMatches, getHumanName))
				{
					// use unique ID with section prefix to prevent cross-section duplicates
					ImGui::PushID(("MultiplayerCutscene_" + human.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(human.model.c_str(), ImVec2(-1, 25)))
					{
						if (g_SetModelMode)
						{
							SetPlayerModel(human.model, g_Variation);
						}
						else
						{
							SpawnPed(human.model, g_Variation, g_Armed);
						}
					}
					ImGui::PopID();
				}
			}

			ImGui::Spacing();
		}

		// gang section
		if (showGang)
		{
			RenderCenteredSeparator("Gang");

			for (size_t i = 0; i < g_Gang.size(); ++i)
			{
				const auto& human = g_Gang[i];
				if (g_HumanSearch.ShouldShowItem(human, gangMatches, getHumanName))
				{
					// use unique ID with section prefix to prevent cross-section duplicates
					ImGui::PushID(("Gang_" + human.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(human.model.c_str(), ImVec2(-1, 25)))
					{
						if (g_SetModelMode)
						{
							SetPlayerModel(human.model, g_Variation);
						}
						else
						{
							SpawnPed(human.model, g_Variation, g_Armed);
						}
					}
					ImGui::PopID();
				}
			}

			ImGui::Spacing();
		}

		// story finale section
		if (showStoryFinale)
		{
			RenderCenteredSeparator("Story Finale");

			for (size_t i = 0; i < g_StoryFinale.size(); ++i)
			{
				const auto& human = g_StoryFinale[i];
				if (g_HumanSearch.ShouldShowItem(human, storyFinaleMatches, getHumanName))
				{
					// use unique ID with section prefix to prevent cross-section duplicates
					ImGui::PushID(("StoryFinale_" + human.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(human.model.c_str(), ImVec2(-1, 25)))
					{
						if (g_SetModelMode)
						{
							SetPlayerModel(human.model, g_Variation);
						}
						else
						{
							SpawnPed(human.model, g_Variation, g_Armed);
						}
					}
					ImGui::PopID();
				}
			}

			ImGui::Spacing();
		}

		// multiplayer blood money section
		if (showMultiplayerBloodMoney)
		{
			RenderCenteredSeparator("Multiplayer Blood Money");

			for (size_t i = 0; i < g_MultiplayerBloodMoney.size(); ++i)
			{
				const auto& human = g_MultiplayerBloodMoney[i];
				if (g_HumanSearch.ShouldShowItem(human, multiplayerBloodMoneyMatches, getHumanName))
				{
					// use unique ID with section prefix to prevent cross-section duplicates
					ImGui::PushID(("MultiplayerBloodMoney_" + human.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(human.model.c_str(), ImVec2(-1, 25)))
					{
						if (g_SetModelMode)
						{
							SetPlayerModel(human.model, g_Variation);
						}
						else
						{
							SpawnPed(human.model, g_Variation, g_Armed);
						}
					}
					ImGui::PopID();
				}
			}

			ImGui::Spacing();
		}

		// multiplayer bounty hunters section
		if (showMultiplayerBountyHunters)
		{
			RenderCenteredSeparator("Multiplayer Bounty Hunters");

			for (size_t i = 0; i < g_MultiplayerBountyHunters.size(); ++i)
			{
				const auto& human = g_MultiplayerBountyHunters[i];
				if (g_HumanSearch.ShouldShowItem(human, multiplayerBountyHuntersMatches, getHumanName))
				{
					// use unique ID with section prefix to prevent cross-section duplicates
					ImGui::PushID(("MultiplayerBountyHunters_" + human.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(human.model.c_str(), ImVec2(-1, 25)))
					{
						if (g_SetModelMode)
						{
							SetPlayerModel(human.model, g_Variation);
						}
						else
						{
							SpawnPed(human.model, g_Variation, g_Armed);
						}
					}
					ImGui::PopID();
				}
			}

			ImGui::Spacing();
		}

		// multiplayer naturalist section
		if (showMultiplayerNaturalist)
		{
			RenderCenteredSeparator("Multiplayer Naturalist");

			for (size_t i = 0; i < g_MultiplayerNaturalist.size(); ++i)
			{
				const auto& human = g_MultiplayerNaturalist[i];
				if (g_HumanSearch.ShouldShowItem(human, multiplayerNaturalistMatches, getHumanName))
				{
					// use unique ID with section prefix to prevent cross-section duplicates
					ImGui::PushID(("MultiplayerNaturalist_" + human.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(human.model.c_str(), ImVec2(-1, 25)))
					{
						if (g_SetModelMode)
						{
							SetPlayerModel(human.model, g_Variation);
						}
						else
						{
							SpawnPed(human.model, g_Variation, g_Armed);
						}
					}
					ImGui::PopID();
				}
			}

			ImGui::Spacing();
		}

		// multiplayer section
		if (showMultiplayer)
		{
			RenderCenteredSeparator("Multiplayer");

			for (size_t i = 0; i < g_Multiplayer.size(); ++i)
			{
				const auto& human = g_Multiplayer[i];
				if (g_HumanSearch.ShouldShowItem(human, multiplayerMatches, getHumanName))
				{
					// use unique ID with section prefix to prevent cross-section duplicates
					ImGui::PushID(("Multiplayer_" + human.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(human.model.c_str(), ImVec2(-1, 25)))
					{
						if (g_SetModelMode)
						{
							SetPlayerModel(human.model, g_Variation);
						}
						else
						{
							SpawnPed(human.model, g_Variation, g_Armed);
						}
					}
					ImGui::PopID();
				}
			}

			ImGui::Spacing();
		}

		// story section
		if (showStory)
		{
			RenderCenteredSeparator("Story");

			for (size_t i = 0; i < g_Story.size(); ++i)
			{
				const auto& human = g_Story[i];
				if (g_HumanSearch.ShouldShowItem(human, storyMatches, getHumanName))
				{
					// use unique ID with section prefix to prevent cross-section duplicates
					ImGui::PushID(("Story_" + human.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(human.model.c_str(), ImVec2(-1, 25)))
					{
						if (g_SetModelMode)
						{
							SetPlayerModel(human.model, g_Variation);
						}
						else
						{
							SpawnPed(human.model, g_Variation, g_Armed);
						}
					}
					ImGui::PopID();
				}
			}

			ImGui::Spacing();
		}

		// random event section
		if (showRandomEvent)
		{
			RenderCenteredSeparator("Random Event");

			for (size_t i = 0; i < g_RandomEvent.size(); ++i)
			{
				const auto& human = g_RandomEvent[i];
				if (g_HumanSearch.ShouldShowItem(human, randomEventMatches, getHumanName))
				{
					// use unique ID with section prefix to prevent cross-section duplicates
					ImGui::PushID(("RandomEvent_" + human.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(human.model.c_str(), ImVec2(-1, 25)))
					{
						if (g_SetModelMode)
						{
							SetPlayerModel(human.model, g_Variation);
						}
						else
						{
							SpawnPed(human.model, g_Variation, g_Armed);
						}
					}
					ImGui::PopID();
				}
			}

			ImGui::Spacing();
		}

		// scenario section
		if (showScenario)
		{
			RenderCenteredSeparator("Scenario");

			for (size_t i = 0; i < g_Scenario.size(); ++i)
			{
				const auto& human = g_Scenario[i];
				if (g_HumanSearch.ShouldShowItem(human, scenarioMatches, getHumanName))
				{
					// use unique ID with section prefix to prevent cross-section duplicates
					ImGui::PushID(("Scenario_" + human.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(human.model.c_str(), ImVec2(-1, 25)))
					{
						if (g_SetModelMode)
						{
							SetPlayerModel(human.model, g_Variation);
						}
						else
						{
							SpawnPed(human.model, g_Variation, g_Armed);
						}
					}
					ImGui::PopID();
				}
			}

			ImGui::Spacing();
		}

		// story scenario female section
		if (showStoryScenarioFemale)
		{
			RenderCenteredSeparator("Story Scenario Female");

			for (size_t i = 0; i < g_StoryScenarioFemale.size(); ++i)
			{
				const auto& human = g_StoryScenarioFemale[i];
				if (g_HumanSearch.ShouldShowItem(human, storyScenarioFemaleMatches, getHumanName))
				{
					// use unique ID with section prefix to prevent cross-section duplicates
					ImGui::PushID(("StoryScenarioFemale_" + human.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(human.model.c_str(), ImVec2(-1, 25)))
					{
						if (g_SetModelMode)
						{
							SetPlayerModel(human.model, g_Variation);
						}
						else
						{
							SpawnPed(human.model, g_Variation, g_Armed);
						}
					}
					ImGui::PopID();
				}
			}

			ImGui::Spacing();
		}

		// story scenario male section
		if (showStoryScenarioMale)
		{
			RenderCenteredSeparator("Story Scenario Male");

			for (size_t i = 0; i < g_StoryScenarioMale.size(); ++i)
			{
				const auto& human = g_StoryScenarioMale[i];
				if (g_HumanSearch.ShouldShowItem(human, storyScenarioMaleMatches, getHumanName))
				{
					// use unique ID with section prefix to prevent cross-section duplicates
					ImGui::PushID(("StoryScenarioMale_" + human.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(human.model.c_str(), ImVec2(-1, 25)))
					{
						if (g_SetModelMode)
						{
							SetPlayerModel(human.model, g_Variation);
						}
						else
						{
							SpawnPed(human.model, g_Variation, g_Armed);
						}
					}
					ImGui::PopID();
				}
			}

			ImGui::Spacing();
		}

		// miscellaneous section
		if (showMiscellaneous)
		{
			RenderCenteredSeparator("Miscellaneous");

			for (size_t i = 0; i < g_Miscellaneous.size(); ++i)
			{
				const auto& human = g_Miscellaneous[i];
				if (g_HumanSearch.ShouldShowItem(human, miscellaneousMatches, getHumanName))
				{
					// use unique ID with section prefix to prevent cross-section duplicates
					ImGui::PushID(("Miscellaneous_" + human.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(human.model.c_str(), ImVec2(-1, 25)))
					{
						if (g_SetModelMode)
						{
							SetPlayerModel(human.model, g_Variation);
						}
						else
						{
							SpawnPed(human.model, g_Variation, g_Armed);
						}
					}
					ImGui::PopID();
				}
			}

			ImGui::Spacing();
		}

		// show helpful message when no matches found
		if (!showAmbientFemale && !showAmbientFemaleOrdinary && !showAmbientMale && !showAmbientMaleOrdinary &&
		    !showAmbientMaleSuppressed && !showCutscene && !showMultiplayerCutscene && !showGang &&
		    !showStoryFinale && !showMultiplayerBloodMoney && !showMultiplayerBountyHunters &&
		    !showMultiplayerNaturalist && !showMultiplayer && !showStory && !showRandomEvent &&
		    !showScenario && !showStoryScenarioFemale && !showStoryScenarioMale && !showMiscellaneous &&
		    !g_HumanSearch.searchBuffer.empty())
		{
			ImGui::Text("No humans or sections match your search");
			ImGui::Text("Try searching for: 'ambient', 'cutscene', 'gang', 'multiplayer', etc.");
		}
	}

	// horse data structure
	struct Horse
	{
		std::string name;
		std::string model;
		std::string section;
		int variation;
	};

	// horse data (parsed from horses.txt)
	static std::vector<Horse> g_AmericanPaintHorses = {
		{"Grey Overo", "A_C_HORSE_AMERICANPAINT_GREYOVERO", "American Paint", 0},
		{"Overo", "A_C_HORSE_AMERICANPAINT_OVERO", "American Paint", 0},
		{"Splashed White", "A_C_HORSE_AMERICANPAINT_SPLASHEDWHITE", "American Paint", 0},
		{"Tobiano", "A_C_HORSE_AMERICANPAINT_TOBIANO", "American Paint", 0}
	};

	static std::vector<Horse> g_AmericanStandardbredHorses = {
		{"Black", "A_C_HORSE_AMERICANSTANDARDBRED_BLACK", "American Standardbred", 0},
		{"Buckskin", "A_C_HORSE_AMERICANSTANDARDBRED_BUCKSKIN", "American Standardbred", 0},
		{"Light-Buckskin", "A_C_HORSE_AMERICANSTANDARDBRED_LIGHTBUCKSKIN", "American Standardbred", 0},
		{"Palomino Dapple", "A_C_HORSE_AMERICANSTANDARDBRED_PALOMINODAPPLE", "American Standardbred", 0},
		{"Silver Tail Buckskin", "A_C_HORSE_AMERICANSTANDARDBRED_SILVERTAILBUCKSKIN", "American Standardbred", 0}
	};

	static std::vector<Horse> g_AndalusianHorses = {
		{"Dark Bay", "A_C_HORSE_ANDALUSIAN_DARKBAY", "Andalusian", 0},
		{"Perlino", "A_C_HORSE_ANDALUSIAN_PERLINO", "Andalusian", 0},
		{"Rose Gray", "A_C_HORSE_ANDALUSIAN_ROSEGRAY", "Andalusian", 0}
	};

	static std::vector<Horse> g_AppaloosaHorses = {
		{"Black Snowflake", "A_C_HORSE_APPALOOSA_BLACKSNOWFLAKE", "Appaloosa", 0},
		{"Blanket", "A_C_HORSE_APPALOOSA_BLANKET", "Appaloosa", 0},
		{"Brown Leopard", "A_C_HORSE_APPALOOSA_BROWNLEOPARD", "Appaloosa", 0},
		{"Few-Spotted PC", "A_C_HORSE_APPALOOSA_FEWSPOTTED_PC", "Appaloosa", 0},
		{"Leopard", "A_C_HORSE_APPALOOSA_LEOPARD", "Appaloosa", 0},
		{"Leopard Blanket", "A_C_HORSE_APPALOOSA_LEOPARDBLANKET", "Appaloosa", 0}
	};

	static std::vector<Horse> g_ArabianHorses = {
		{"Black", "A_C_HORSE_ARABIAN_BLACK", "Arabian", 0},
		{"Grey", "A_C_HORSE_ARABIAN_GREY", "Arabian", 0},
		{"Red Chestnut", "A_C_HORSE_ARABIAN_REDCHESTNUT", "Arabian", 0},
		{"Red Chestnut PC", "A_C_HORSE_ARABIAN_REDCHESTNUT_PC", "Arabian", 0},
		{"Rose Grey Bay", "A_C_HORSE_ARABIAN_ROSEGREYBAY", "Arabian", 0},
		{"Warped Brindle", "A_C_HORSE_ARABIAN_WARPEDBRINDLE", "Arabian", 0},
		{"White", "A_C_HORSE_ARABIAN_WHITE", "Arabian", 0}
	};

	static std::vector<Horse> g_ArdennesHorses = {
		{"Bay Roan", "A_C_HORSE_ARDENNES_BAYROAN", "Ardennes", 0},
		{"Iron Grey Roan", "A_C_HORSE_ARDENNES_IRONGREYROAN", "Ardennes", 0},
		{"Strawberry Roan", "A_C_HORSE_ARDENNES_STRAWBERRYROAN", "Ardennes", 0}
	};

	static std::vector<Horse> g_BelgianHorses = {
		{"Blonde Chestnut", "A_C_HORSE_BELGIAN_BLONDCHESTNUT", "Belgian", 0},
		{"Mealy Chestnut", "A_C_HORSE_BELGIAN_MEALYCHESTNUT", "Belgian", 0}
	};

	static std::vector<Horse> g_BretonHorses = {
		{"Grullo Dun", "A_C_HORSE_BRETON_GRULLODUN", "Breton", 0},
		{"Mealy Dapple Bay", "A_C_HORSE_BRETON_MEALYDAPPLEBAY", "Breton", 0},
		{"Red Roan", "A_C_HORSE_BRETON_REDROAN", "Breton", 0},
		{"Seal Brown", "A_C_HORSE_BRETON_SEALBROWN", "Breton", 0},
		{"Sorrel", "A_C_HORSE_BRETON_SORREL", "Breton", 0},
		{"Steel Grey", "A_C_HORSE_BRETON_STEELGREY", "Breton", 0}
	};

	static std::vector<Horse> g_CriolloHorses = {
		{"Bay Brindle", "A_C_HORSE_CRIOLLO_BAYBRINDLE", "Criollo", 0},
		{"Bay Frame Overo", "A_C_HORSE_CRIOLLO_BAYFRAMEOVERO", "Criollo", 0},
		{"Blue Roan Overo", "A_C_HORSE_CRIOLLO_BLUEROANOVERO", "Criollo", 0},
		{"Dun", "A_C_HORSE_CRIOLLO_DUN", "Criollo", 0},
		{"Marble Sabino", "A_C_HORSE_CRIOLLO_MARBLESABINO", "Criollo", 0},
		{"Sorrel Overo", "A_C_HORSE_CRIOLLO_SORRELOVERO", "Criollo", 0}
	};

	static std::vector<Horse> g_DutchWarmbloodHorses = {
		{"Chocolate Roan", "A_C_HORSE_DUTCHWARMBLOOD_CHOCOLATEROAN", "Dutch Warmblood", 0},
		{"Seal Brown", "A_C_HORSE_DUTCHWARMBLOOD_SEALBROWN", "Dutch Warmblood", 0},
		{"Sooty Buckskin", "A_C_HORSE_DUTCHWARMBLOOD_SOOTYBUCKSKIN", "Dutch Warmblood", 0}
	};

	static std::vector<Horse> g_GangHorses = {
		{"Bill's Horse", "A_C_HORSE_GANG_BILL", "Gang", 0},
		{"Charles' Horse 1889", "A_C_HORSE_GANG_CHARLES", "Gang", 0},
		{"Charles' Horse 1907", "A_C_HORSE_GANG_CHARLES_ENDLESSSUMMER", "Gang", 0},
		{"Dutch's Horse", "A_C_HORSE_GANG_DUTCH", "Gang", 0},
		{"Hosea's Horse", "A_C_HORSE_GANG_HOSEA", "Gang", 0},
		{"Javier's Horse", "A_C_HORSE_GANG_JAVIER", "Gang", 0},
		{"John's Horse 1889", "A_C_HORSE_GANG_JOHN", "Gang", 0},
		{"John's Horse 1907", "A_C_HORSE_JOHN_ENDLESSSUMMER", "Gang", 0},
		{"Karen's Horse", "A_C_HORSE_GANG_KAREN", "Gang", 0},
		{"Kieran's Horse", "A_C_HORSE_GANG_KIERAN", "Gang", 0},
		{"Lenny's Horse", "A_C_HORSE_GANG_LENNY", "Gang", 0},
		{"Micah's Horse", "A_C_HORSE_GANG_MICAH", "Gang", 0},
		{"Sadie's Horse 1889", "A_C_HORSE_GANG_SADIE", "Gang", 0},
		{"Sadie's Horse 1907", "A_C_HORSE_GANG_SADIE_ENDLESSSUMMER", "Gang", 0},
		{"Sean's Horse", "A_C_HORSE_GANG_SEAN", "Gang", 0},
		{"Trelawney's Horse", "A_C_HORSE_GANG_TRELAWNEY", "Gang", 0},
		{"Uncle's Horse 1889", "A_C_HORSE_GANG_UNCLE", "Gang", 0},
		{"Uncle's Horse 1907", "A_C_HORSE_GANG_UNCLE_ENDLESSSUMMER", "Gang", 0},
		{"EagleFlies' Horse", "A_C_HORSE_EAGLEFLIES", "Gang", 0},
		{"Buell (Special)", "A_C_HORSE_BUELL_WARVETS", "Gang", 0}
	};

	static std::vector<Horse> g_GypsyCobHorses = {
		{"Polomino Blagdon", "A_C_HORSE_GYPSYCOB_PALOMINOBLAGDON", "Gypsy Cob", 0},
		{"Piebald", "A_C_HORSE_GYPSYCOB_PIEBALD", "Gypsy Cob", 0},
		{"Skewbald", "A_C_HORSE_GYPSYCOB_SKEWBALD", "Gypsy Cob", 0},
		{"Splashed Bay", "A_C_HORSE_GYPSYCOB_SPLASHEDBAY", "Gypsy Cob", 0},
		{"Splashed Piedbald", "A_C_HORSE_GYPSYCOB_SPLASHEDPIEBALD", "Gypsy Cob", 0},
		{"White Blagdon", "A_C_HORSE_GYPSYCOB_WHITEBLAGDON", "Gypsy Cob", 0}
	};

	static std::vector<Horse> g_HungarianHalfbredHorses = {
		{"Dapple Dark Gray", "A_C_HORSE_HUNGARIANHALFBRED_DARKDAPPLEGREY", "Hungarian Halfbred", 0},
		{"Flaxen Chestnut", "A_C_HORSE_HUNGARIANHALFBRED_FLAXENCHESTNUT", "Hungarian Halfbred", 0},
		{"Liver Chestnut", "A_C_HORSE_HUNGARIANHALFBRED_LIVERCHESTNUT", "Hungarian Halfbred", 0},
		{"Piebald Tobiano", "A_C_HORSE_HUNGARIANHALFBRED_PIEBALDTOBIANO", "Hungarian Halfbred", 0}
	};

	static std::vector<Horse> g_KentuckySaddlerHorses = {
		{"Black", "A_C_HORSE_KENTUCKYSADDLE_BLACK", "Kentucky Saddler", 0},
		{"Buttermilk Buckskin PC", "A_C_HORSE_KENTUCKYSADDLE_BUTTERMILKBUCKSKIN_PC", "Kentucky Saddler", 0},
		{"Chestnut Pinto", "A_C_HORSE_KENTUCKYSADDLE_CHESTNUTPINTO", "Kentucky Saddler", 0},
		{"Grey", "A_C_HORSE_KENTUCKYSADDLE_GREY", "Kentucky Saddler", 0},
		{"Silver Bay", "A_C_HORSE_KENTUCKYSADDLE_SILVERBAY", "Kentucky Saddler", 0}
	};

	static std::vector<Horse> g_KlardruberHorses = {
		{"Black", "A_C_HORSE_KLADRUBER_BLACK", "Klardruber", 0},
		{"Cremello", "A_C_HORSE_KLADRUBER_CREMELLO", "Klardruber", 0},
		{"Dapple Rose Grey", "A_C_HORSE_KLADRUBER_DAPPLEROSEGREY", "Klardruber", 0},
		{"Grey", "A_C_HORSE_KLADRUBER_GREY", "Klardruber", 0},
		{"Silver", "A_C_HORSE_KLADRUBER_SILVER", "Klardruber", 0},
		{"White", "A_C_HORSE_KLADRUBER_WHITE", "Klardruber", 0}
	};

	static std::vector<Horse> g_MissouriFoxTrotterHorses = {
		{"Amber Champagne", "A_C_HORSE_MISSOURIFOXTROTTER_AMBERCHAMPAGNE", "Missouri Fox Trotter", 0},
		{"Black Tovero", "A_C_HORSE_MISSOURIFOXTROTTER_BLACKTOVERO", "Missouri Fox Trotter", 0},
		{"Blue Roan", "A_C_HORSE_MISSOURIFOXTROTTER_BLUEROAN", "Missouri Fox Trotter", 0},
		{"Buckskin Brindle", "A_C_HORSE_MISSOURIFOXTROTTER_BUCKSKINBRINDLE", "Missouri Fox Trotter", 0},
		{"Dapple Grey", "A_C_HORSE_MISSOURIFOXTROTTER_DAPPLEGREY", "Missouri Fox Trotter", 0},
		{"Sable Champagne", "A_C_HORSE_MISSOURIFOXTROTTER_SABLECHAMPAGNE", "Missouri Fox Trotter", 0},
		{"Silver Dapple Pinto", "A_C_HORSE_MISSOURIFOXTROTTER_SILVERDAPPLEPINTO", "Missouri Fox Trotter", 0}
	};

	static std::vector<Horse> g_MorganHorses = {
		{"Bay", "A_C_HORSE_MORGAN_BAY", "Morgan", 0},
		{"Bay Roan", "A_C_HORSE_MORGAN_BAYROAN", "Morgan", 0},
		{"Flaxen Chestnut", "A_C_HORSE_MORGAN_FLAXENCHESTNUT", "Morgan", 0},
		{"Liver Chestnut PC", "A_C_HORSE_MORGAN_LIVERCHESTNUT_PC", "Morgan", 0},
		{"Palomino", "A_C_HORSE_MORGAN_PALOMINO", "Morgan", 0}
	};

	static std::vector<Horse> g_MustangHorses = {
		{"Black Overo", "A_C_HORSE_MUSTANG_BLACKOVERO", "Mustang", 0},
		{"Buckskin", "A_C_HORSE_MUSTANG_BUCKSKIN", "Mustang", 0},
		{"Chestnut Tovero", "A_C_HORSE_MUSTANG_CHESTNUTTOVERO", "Mustang", 0},
		{"Golden Dun", "A_C_HORSE_MUSTANG_GOLDENDUN", "Mustang", 0},
		{"Grullo Dun", "A_C_HORSE_MUSTANG_GRULLODUN", "Mustang", 0},
		{"Red Dun Overo", "A_C_HORSE_MUSTANG_REDDUNOVERO", "Mustang", 0},
		{"Tigerstriped Bay", "A_C_HORSE_MUSTANG_TIGERSTRIPEDBAY", "Mustang", 0},
		{"Wild Bay", "A_C_HORSE_MUSTANG_WILDBAY", "Mustang", 0}
	};

	static std::vector<Horse> g_NokotaHorses = {
		{"Blue Roan", "A_C_HORSE_NOKOTA_BLUEROAN", "Nokota", 0},
		{"Reverse Dapple Roan", "A_C_HORSE_NOKOTA_REVERSEDAPPLEROAN", "Nokota", 0},
		{"White Roan", "A_C_HORSE_NOKOTA_WHITEROAN", "Nokota", 0}
	};

	static std::vector<Horse> g_NorfolkRoadsterHorses = {
		{"Black", "A_C_HORSE_NORFOLKROADSTER_BLACK", "Norfolk Roadster", 0},
		{"Dappled Buckskin", "A_C_HORSE_NORFOLKROADSTER_DAPPLEDBUCKSKIN", "Norfolk Roadster", 0},
		{"Piebald Roan", "A_C_HORSE_NORFOLKROADSTER_PIEBALDROAN", "Norfolk Roadster", 0},
		{"Rose Gray", "A_C_HORSE_NORFOLKROADSTER_ROSEGREY", "Norfolk Roadster", 0},
		{"Speckled Gray", "A_C_HORSE_NORFOLKROADSTER_SPECKLEDGREY", "Norfolk Roadster", 0},
		{"Spotted Tricolor", "A_C_HORSE_NORFOLKROADSTER_SPOTTEDTRICOLOR", "Norfolk Roadster", 0}
	};

	static std::vector<Horse> g_ShireHorses = {
		{"Dark Bay", "A_C_HORSE_SHIRE_DARKBAY", "Shire", 0},
		{"Light Grey", "A_C_HORSE_SHIRE_LIGHTGREY", "Shire", 0},
		{"Raven Black", "A_C_HORSE_SHIRE_RAVENBLACK", "Shire", 0}
	};

	static std::vector<Horse> g_SuffolkPunchHorses = {
		{"Red Chestnut", "A_C_HORSE_SUFFOLKPUNCH_REDCHESTNUT", "Suffolk Punch", 0},
		{"Sorrel", "A_C_HORSE_SUFFOLKPUNCH_SORREL", "Suffolk Punch", 0}
	};

	static std::vector<Horse> g_TennesseeWalkerHorses = {
		{"Black Rabicano", "A_C_HORSE_TENNESSEEWALKER_BLACKRABICANO", "Tennessee Walker", 0},
		{"Chestnut", "A_C_HORSE_TENNESSEEWALKER_CHESTNUT", "Tennessee Walker", 0},
		{"Dapple Bay", "A_C_HORSE_TENNESSEEWALKER_DAPPLEBAY", "Tennessee Walker", 0},
		{"Flaxen Roan", "A_C_HORSE_TENNESSEEWALKER_FLAXENROAN", "Tennessee Walker", 0},
		{"Gold Palomino PC", "A_C_HORSE_TENNESSEEWALKER_GOLDPALOMINO_PC", "Tennessee Walker", 0},
		{"Mahogany Bay", "A_C_HORSE_TENNESSEEWALKER_MAHOGANYBAY", "Tennessee Walker", 0},
		{"Red Roan", "A_C_HORSE_TENNESSEEWALKER_REDROAN", "Tennessee Walker", 0}
	};

	static std::vector<Horse> g_ThoroughbredHorses = {
		{"Black Chestnut", "A_C_HORSE_THOROUGHBRED_BLACKCHESTNUT", "Thoroughbred", 0},
		{"Blood Bay", "A_C_HORSE_THOROUGHBRED_BLOODBAY", "Thoroughbred", 0},
		{"Brindle", "A_C_HORSE_THOROUGHBRED_BRINDLE", "Thoroughbred", 0},
		{"Dapple Gray", "A_C_HORSE_THOROUGHBRED_DAPPLEGREY", "Thoroughbred", 0},
		{"Reverse Dapple Black", "A_C_HORSE_THOROUGHBRED_REVERSEDAPPLEBLACK", "Thoroughbred", 0}
	};

	static std::vector<Horse> g_TurkomanHorses = {
		{"Black", "A_C_HORSE_TURKOMAN_BLACK", "Turkoman", 0},
		{"Chestnut", "A_C_HORSE_TURKOMAN_CHESTNUT", "Turkoman", 0},
		{"Dark Bay", "A_C_HORSE_TURKOMAN_DARKBAY", "Turkoman", 0},
		{"Gold", "A_C_HORSE_TURKOMAN_GOLD", "Turkoman", 0},
		{"Grey", "A_C_HORSE_TURKOMAN_GREY", "Turkoman", 0},
		{"Perlino", "A_C_HORSE_TURKOMAN_PERLINO", "Turkoman", 0},
		{"Silver", "A_C_HORSE_TURKOMAN_SILVER", "Turkoman", 0}
	};

	static std::vector<Horse> g_MiscellaneousHorses = {
		{"Donkey", "A_C_Donkey_01", "Miscellaneous", 0},
		{"Scrawny Nag", "A_C_HORSE_MP_MANGY_BACKUP", "Miscellaneous", 0},
		{"Mule", "A_C_HORSEMULE_01", "Miscellaneous", 0},
		{"Mule Painted", "A_C_HORSEMULEPAINTED_01", "Miscellaneous", 0},
		{"Murfree Blanket", "A_C_HORSE_MURFREEBROOD_MANGE_01", "Miscellaneous", 0},
		{"Murfree Blue Roan", "A_C_HORSE_MURFREEBROOD_MANGE_02", "Miscellaneous", 0},
		{"Murfree Black Rabicano", "A_C_HORSE_MURFREEBROOD_MANGE_03", "Miscellaneous", 0},
		{"Horse Winter", "A_C_HORSE_WINTER02_01", "Miscellaneous", 0},
		{"Unknown PC Horse", "P_C_HORSE_01", "Miscellaneous", 0},
		{"RDO Special Arabian", "MP_A_C_HORSECORPSE_01", "Miscellaneous", 0},
		{"RDO OwlHoot Victim", "MP_HORSE_OWLHOOTVICTIM_01", "Miscellaneous", 0}
	};

	static void RenderHorsesView()
	{
		// back button in top-right corner
		ImVec2 windowSize = ImGui::GetContentRegionAvail();
		ImVec2 originalPos = ImGui::GetCursorPos();

		ImGui::SetCursorPos(ImVec2(windowSize.x - 30, 5));
		if (ImGui::Button("X", ImVec2(25, 25)))
		{
			g_InHorses = false;
		}

		// reset cursor to original position and add some top margin
		ImGui::SetCursorPos(ImVec2(originalPos.x, originalPos.y + 35));

		// helper function for centered separator with custom line width
		auto RenderCenteredSeparator = [](const char* text) {
			ImGui::PushFont(Menu::Font::g_ChildTitleFont);
			ImVec2 textSize = ImGui::CalcTextSize(text);
			ImVec2 contentRegion = ImGui::GetContentRegionAvail();

			// center text
			ImGui::SetCursorPosX((contentRegion.x - textSize.x) * 0.5f);
			ImGui::Text(text);
			ImGui::PopFont();

			// draw centered line (3x text width) using screen coordinates
			float lineWidth = textSize.x * 3.0f;
			float linePosX = (contentRegion.x - lineWidth) * 0.5f;

			ImDrawList* drawList = ImGui::GetWindowDrawList();
			ImVec2 windowPos = ImGui::GetWindowPos();
			ImVec2 cursorScreenPos = ImGui::GetCursorScreenPos();

			// use screen coordinates for proper positioning with scrolling
			drawList->AddLine(
				ImVec2(windowPos.x + linePosX, cursorScreenPos.y),
				ImVec2(windowPos.x + linePosX + lineWidth, cursorScreenPos.y),
				ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);

			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
			ImGui::Spacing();
		};

		// lambda function for getting horse names
		auto getHorseName = [](const Horse& horse) { return horse.name; };

		// calculate totals for all horse sections
		int totalHorses = static_cast<int>(g_AmericanPaintHorses.size() + g_AmericanStandardbredHorses.size() +
		                                  g_AndalusianHorses.size() + g_AppaloosaHorses.size() + g_ArabianHorses.size() +
		                                  g_ArdennesHorses.size() + g_BelgianHorses.size() + g_BretonHorses.size() +
		                                  g_CriolloHorses.size() + g_DutchWarmbloodHorses.size() + g_GangHorses.size() +
		                                  g_GypsyCobHorses.size() + g_HungarianHalfbredHorses.size() + g_KentuckySaddlerHorses.size() +
		                                  g_KlardruberHorses.size() + g_MissouriFoxTrotterHorses.size() + g_MorganHorses.size() +
		                                  g_MustangHorses.size() + g_NokotaHorses.size() + g_NorfolkRoadsterHorses.size() +
		                                  g_ShireHorses.size() + g_SuffolkPunchHorses.size() + g_TennesseeWalkerHorses.size() +
		                                  g_ThoroughbredHorses.size() + g_TurkomanHorses.size() + g_MiscellaneousHorses.size());

		// check section matches for search
		bool americanPaintMatches = g_HorseSearch.SectionMatches("American Paint", g_HorseSearch.searchBuffer);
		bool americanStandardbredMatches = g_HorseSearch.SectionMatches("American Standardbred", g_HorseSearch.searchBuffer);
		bool andalusianMatches = g_HorseSearch.SectionMatches("Andalusian", g_HorseSearch.searchBuffer);
		bool appaloosaMatches = g_HorseSearch.SectionMatches("Appaloosa", g_HorseSearch.searchBuffer);
		bool arabianMatches = g_HorseSearch.SectionMatches("Arabian", g_HorseSearch.searchBuffer);
		bool ardennesMatches = g_HorseSearch.SectionMatches("Ardennes", g_HorseSearch.searchBuffer);
		bool belgianMatches = g_HorseSearch.SectionMatches("Belgian", g_HorseSearch.searchBuffer);
		bool bretonMatches = g_HorseSearch.SectionMatches("Breton", g_HorseSearch.searchBuffer);
		bool criolloMatches = g_HorseSearch.SectionMatches("Criollo", g_HorseSearch.searchBuffer);
		bool dutchWarmbloodMatches = g_HorseSearch.SectionMatches("Dutch Warmblood", g_HorseSearch.searchBuffer);
		bool gangMatches = g_HorseSearch.SectionMatches("Gang", g_HorseSearch.searchBuffer);
		bool gypsyCobMatches = g_HorseSearch.SectionMatches("Gypsy Cob", g_HorseSearch.searchBuffer);
		bool hungarianHalfbredMatches = g_HorseSearch.SectionMatches("Hungarian Halfbred", g_HorseSearch.searchBuffer);
		bool kentuckySaddlerMatches = g_HorseSearch.SectionMatches("Kentucky Saddler", g_HorseSearch.searchBuffer);
		bool klardruberMatches = g_HorseSearch.SectionMatches("Klardruber", g_HorseSearch.searchBuffer);
		bool missouriFoxTrotterMatches = g_HorseSearch.SectionMatches("Missouri Fox Trotter", g_HorseSearch.searchBuffer);
		bool morganMatches = g_HorseSearch.SectionMatches("Morgan", g_HorseSearch.searchBuffer);
		bool mustangMatches = g_HorseSearch.SectionMatches("Mustang", g_HorseSearch.searchBuffer);
		bool nokotaMatches = g_HorseSearch.SectionMatches("Nokota", g_HorseSearch.searchBuffer);
		bool norfolkRoadsterMatches = g_HorseSearch.SectionMatches("Norfolk Roadster", g_HorseSearch.searchBuffer);
		bool shireMatches = g_HorseSearch.SectionMatches("Shire", g_HorseSearch.searchBuffer);
		bool suffolkPunchMatches = g_HorseSearch.SectionMatches("Suffolk Punch", g_HorseSearch.searchBuffer);
		bool tennesseeWalkerMatches = g_HorseSearch.SectionMatches("Tennessee Walker", g_HorseSearch.searchBuffer);
		bool thoroughbredMatches = g_HorseSearch.SectionMatches("Thoroughbred", g_HorseSearch.searchBuffer);
		bool turkomanMatches = g_HorseSearch.SectionMatches("Turkoman", g_HorseSearch.searchBuffer);
		bool miscellaneousMatches = g_HorseSearch.SectionMatches("Miscellaneous", g_HorseSearch.searchBuffer);

		// count visible horses in each section
		int americanPaintVisible = americanPaintMatches ? static_cast<int>(g_AmericanPaintHorses.size()) :
		                          g_HorseSearch.CountMatches(g_AmericanPaintHorses, g_HorseSearch.searchBuffer, getHorseName);
		int americanStandardbredVisible = americanStandardbredMatches ? static_cast<int>(g_AmericanStandardbredHorses.size()) :
		                                 g_HorseSearch.CountMatches(g_AmericanStandardbredHorses, g_HorseSearch.searchBuffer, getHorseName);
		int andalusianVisible = andalusianMatches ? static_cast<int>(g_AndalusianHorses.size()) :
		                       g_HorseSearch.CountMatches(g_AndalusianHorses, g_HorseSearch.searchBuffer, getHorseName);
		int appaloosaVisible = appaloosaMatches ? static_cast<int>(g_AppaloosaHorses.size()) :
		                      g_HorseSearch.CountMatches(g_AppaloosaHorses, g_HorseSearch.searchBuffer, getHorseName);
		int arabianVisible = arabianMatches ? static_cast<int>(g_ArabianHorses.size()) :
		                    g_HorseSearch.CountMatches(g_ArabianHorses, g_HorseSearch.searchBuffer, getHorseName);
		int ardennesVisible = ardennesMatches ? static_cast<int>(g_ArdennesHorses.size()) :
		                     g_HorseSearch.CountMatches(g_ArdennesHorses, g_HorseSearch.searchBuffer, getHorseName);
		int belgianVisible = belgianMatches ? static_cast<int>(g_BelgianHorses.size()) :
		                    g_HorseSearch.CountMatches(g_BelgianHorses, g_HorseSearch.searchBuffer, getHorseName);
		int bretonVisible = bretonMatches ? static_cast<int>(g_BretonHorses.size()) :
		                   g_HorseSearch.CountMatches(g_BretonHorses, g_HorseSearch.searchBuffer, getHorseName);
		int criolloVisible = criolloMatches ? static_cast<int>(g_CriolloHorses.size()) :
		                    g_HorseSearch.CountMatches(g_CriolloHorses, g_HorseSearch.searchBuffer, getHorseName);
		int dutchWarmbloodVisible = dutchWarmbloodMatches ? static_cast<int>(g_DutchWarmbloodHorses.size()) :
		                           g_HorseSearch.CountMatches(g_DutchWarmbloodHorses, g_HorseSearch.searchBuffer, getHorseName);
		int gangVisible = gangMatches ? static_cast<int>(g_GangHorses.size()) :
		                 g_HorseSearch.CountMatches(g_GangHorses, g_HorseSearch.searchBuffer, getHorseName);
		int gypsyCobVisible = gypsyCobMatches ? static_cast<int>(g_GypsyCobHorses.size()) :
		                     g_HorseSearch.CountMatches(g_GypsyCobHorses, g_HorseSearch.searchBuffer, getHorseName);
		int hungarianHalfbredVisible = hungarianHalfbredMatches ? static_cast<int>(g_HungarianHalfbredHorses.size()) :
		                              g_HorseSearch.CountMatches(g_HungarianHalfbredHorses, g_HorseSearch.searchBuffer, getHorseName);
		int kentuckySaddlerVisible = kentuckySaddlerMatches ? static_cast<int>(g_KentuckySaddlerHorses.size()) :
		                            g_HorseSearch.CountMatches(g_KentuckySaddlerHorses, g_HorseSearch.searchBuffer, getHorseName);
		int klardruberVisible = klardruberMatches ? static_cast<int>(g_KlardruberHorses.size()) :
		                       g_HorseSearch.CountMatches(g_KlardruberHorses, g_HorseSearch.searchBuffer, getHorseName);
		int missouriFoxTrotterVisible = missouriFoxTrotterMatches ? static_cast<int>(g_MissouriFoxTrotterHorses.size()) :
		                               g_HorseSearch.CountMatches(g_MissouriFoxTrotterHorses, g_HorseSearch.searchBuffer, getHorseName);
		int morganVisible = morganMatches ? static_cast<int>(g_MorganHorses.size()) :
		                   g_HorseSearch.CountMatches(g_MorganHorses, g_HorseSearch.searchBuffer, getHorseName);
		int mustangVisible = mustangMatches ? static_cast<int>(g_MustangHorses.size()) :
		                    g_HorseSearch.CountMatches(g_MustangHorses, g_HorseSearch.searchBuffer, getHorseName);
		int nokotaVisible = nokotaMatches ? static_cast<int>(g_NokotaHorses.size()) :
		                   g_HorseSearch.CountMatches(g_NokotaHorses, g_HorseSearch.searchBuffer, getHorseName);
		int norfolkRoadsterVisible = norfolkRoadsterMatches ? static_cast<int>(g_NorfolkRoadsterHorses.size()) :
		                            g_HorseSearch.CountMatches(g_NorfolkRoadsterHorses, g_HorseSearch.searchBuffer, getHorseName);
		int shireVisible = shireMatches ? static_cast<int>(g_ShireHorses.size()) :
		                  g_HorseSearch.CountMatches(g_ShireHorses, g_HorseSearch.searchBuffer, getHorseName);
		int suffolkPunchVisible = suffolkPunchMatches ? static_cast<int>(g_SuffolkPunchHorses.size()) :
		                         g_HorseSearch.CountMatches(g_SuffolkPunchHorses, g_HorseSearch.searchBuffer, getHorseName);
		int tennesseeWalkerVisible = tennesseeWalkerMatches ? static_cast<int>(g_TennesseeWalkerHorses.size()) :
		                            g_HorseSearch.CountMatches(g_TennesseeWalkerHorses, g_HorseSearch.searchBuffer, getHorseName);
		int thoroughbredVisible = thoroughbredMatches ? static_cast<int>(g_ThoroughbredHorses.size()) :
		                         g_HorseSearch.CountMatches(g_ThoroughbredHorses, g_HorseSearch.searchBuffer, getHorseName);
		int turkomanVisible = turkomanMatches ? static_cast<int>(g_TurkomanHorses.size()) :
		                     g_HorseSearch.CountMatches(g_TurkomanHorses, g_HorseSearch.searchBuffer, getHorseName);
		int miscellaneousVisible = miscellaneousMatches ? static_cast<int>(g_MiscellaneousHorses.size()) :
		                          g_HorseSearch.CountMatches(g_MiscellaneousHorses, g_HorseSearch.searchBuffer, getHorseName);

		// determine section visibility
		bool showAmericanPaint = americanPaintMatches || (americanPaintVisible > 0);
		bool showAmericanStandardbred = americanStandardbredMatches || (americanStandardbredVisible > 0);
		bool showAndalusian = andalusianMatches || (andalusianVisible > 0);
		bool showAppaloosa = appaloosaMatches || (appaloosaVisible > 0);
		bool showArabian = arabianMatches || (arabianVisible > 0);
		bool showArdennes = ardennesMatches || (ardennesVisible > 0);
		bool showBelgian = belgianMatches || (belgianVisible > 0);
		bool showBreton = bretonMatches || (bretonVisible > 0);
		bool showCriollo = criolloMatches || (criolloVisible > 0);
		bool showDutchWarmblood = dutchWarmbloodMatches || (dutchWarmbloodVisible > 0);
		bool showGang = gangMatches || (gangVisible > 0);
		bool showGypsyCob = gypsyCobMatches || (gypsyCobVisible > 0);
		bool showHungarianHalfbred = hungarianHalfbredMatches || (hungarianHalfbredVisible > 0);
		bool showKentuckySaddler = kentuckySaddlerMatches || (kentuckySaddlerVisible > 0);
		bool showKlardruber = klardruberMatches || (klardruberVisible > 0);
		bool showMissouriFoxTrotter = missouriFoxTrotterMatches || (missouriFoxTrotterVisible > 0);
		bool showMorgan = morganMatches || (morganVisible > 0);
		bool showMustang = mustangMatches || (mustangVisible > 0);
		bool showNokota = nokotaMatches || (nokotaVisible > 0);
		bool showNorfolkRoadster = norfolkRoadsterMatches || (norfolkRoadsterVisible > 0);
		bool showShire = shireMatches || (shireVisible > 0);
		bool showSuffolkPunch = suffolkPunchMatches || (suffolkPunchVisible > 0);
		bool showTennesseeWalker = tennesseeWalkerMatches || (tennesseeWalkerVisible > 0);
		bool showThoroughbred = thoroughbredMatches || (thoroughbredVisible > 0);
		bool showTurkoman = turkomanMatches || (turkomanVisible > 0);
		bool showMiscellaneous = miscellaneousMatches || (miscellaneousVisible > 0);

		int totalVisible = americanPaintVisible + americanStandardbredVisible + andalusianVisible + appaloosaVisible + arabianVisible +
		                  ardennesVisible + belgianVisible + bretonVisible + criolloVisible + dutchWarmbloodVisible + gangVisible +
		                  gypsyCobVisible + hungarianHalfbredVisible + kentuckySaddlerVisible + klardruberVisible + missouriFoxTrotterVisible +
		                  morganVisible + mustangVisible + nokotaVisible + norfolkRoadsterVisible + shireVisible + suffolkPunchVisible +
		                  tennesseeWalkerVisible + thoroughbredVisible + turkomanVisible + miscellaneousVisible;

		// render search bar with count and gender selection
		g_HorseSearch.RenderSearchBar("Search Horses", totalHorses, totalVisible, true);

		// american paint section
		if (showAmericanPaint)
		{
			RenderCenteredSeparator("American Paint");
			for (size_t i = 0; i < g_AmericanPaintHorses.size(); ++i)
			{
				const auto& horse = g_AmericanPaintHorses[i];
				if (g_HorseSearch.ShouldShowItem(horse, americanPaintMatches, getHorseName))
				{
					// use unique ID with section prefix to prevent cross-section duplicates
					ImGui::PushID(("AmericanPaint_" + horse.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
					ImGui::PopID();
				}
			}
			ImGui::Spacing();
		}

		// american standardbred section
		if (showAmericanStandardbred)
		{
			RenderCenteredSeparator("American Standardbred");
			for (size_t i = 0; i < g_AmericanStandardbredHorses.size(); ++i)
			{
				const auto& horse = g_AmericanStandardbredHorses[i];
				if (g_HorseSearch.ShouldShowItem(horse, americanStandardbredMatches, getHorseName))
				{
					// use unique ID with section prefix to prevent cross-section duplicates
					ImGui::PushID(("AmericanStandardbred_" + horse.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
					ImGui::PopID();
				}
			}
			ImGui::Spacing();
		}

		// andalusian section
		if (showAndalusian)
		{
			RenderCenteredSeparator("Andalusian");
			for (size_t i = 0; i < g_AndalusianHorses.size(); ++i)
			{
				const auto& horse = g_AndalusianHorses[i];
				if (g_HorseSearch.ShouldShowItem(horse, andalusianMatches, getHorseName))
				{
					// use unique ID with section prefix to prevent cross-section duplicates
					ImGui::PushID(("Andalusian_" + horse.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
					ImGui::PopID();
				}
			}
			ImGui::Spacing();
		}

		// appaloosa section
		if (showAppaloosa)
		{
			RenderCenteredSeparator("Appaloosa");
			for (size_t i = 0; i < g_AppaloosaHorses.size(); ++i)
			{
				const auto& horse = g_AppaloosaHorses[i];
				if (g_HorseSearch.ShouldShowItem(horse, appaloosaMatches, getHorseName))
				{
					// use unique ID with section prefix to prevent cross-section duplicates
					ImGui::PushID(("Appaloosa_" + horse.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
					ImGui::PopID();
				}
			}
			ImGui::Spacing();
		}

		// arabian section
		if (showArabian)
		{
			RenderCenteredSeparator("Arabian");
			for (size_t i = 0; i < g_ArabianHorses.size(); ++i)
			{
				const auto& horse = g_ArabianHorses[i];
				if (g_HorseSearch.ShouldShowItem(horse, arabianMatches, getHorseName))
				{
					// use unique ID with section prefix to prevent cross-section duplicates
					ImGui::PushID(("Arabian_" + horse.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
					ImGui::PopID();
				}
			}
			ImGui::Spacing();
		}

		// ardennes section
		if (showArdennes)
		{
			RenderCenteredSeparator("Ardennes");
			for (size_t i = 0; i < g_ArdennesHorses.size(); ++i)
			{
				const auto& horse = g_ArdennesHorses[i];
				if (g_HorseSearch.ShouldShowItem(horse, ardennesMatches, getHorseName))
				{
					// use unique ID with section prefix to prevent cross-section duplicates
					ImGui::PushID(("Ardennes_" + horse.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
					ImGui::PopID();
				}
			}
			ImGui::Spacing();
		}

		// belgian section
		if (showBelgian)
		{
			RenderCenteredSeparator("Belgian");
			for (size_t i = 0; i < g_BelgianHorses.size(); ++i)
			{
				const auto& horse = g_BelgianHorses[i];
				if (g_HorseSearch.ShouldShowItem(horse, belgianMatches, getHorseName))
				{
					// use unique ID with section prefix to prevent cross-section duplicates
					ImGui::PushID(("Belgian_" + horse.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
					ImGui::PopID();
				}
			}
			ImGui::Spacing();
		}

		// breton section
		if (showBreton)
		{
			RenderCenteredSeparator("Breton");
			for (size_t i = 0; i < g_BretonHorses.size(); ++i)
			{
				const auto& horse = g_BretonHorses[i];
				if (g_HorseSearch.ShouldShowItem(horse, bretonMatches, getHorseName))
				{
					// use unique ID based on model name and index to handle duplicate display names
					ImGui::PushID(("Breton_" + horse.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
					ImGui::PopID();
				}
			}
			ImGui::Spacing();
		}

		// criollo section
		if (showCriollo)
		{
			RenderCenteredSeparator("Criollo");
			for (size_t i = 0; i < g_CriolloHorses.size(); ++i)
			{
				const auto& horse = g_CriolloHorses[i];
				if (g_HorseSearch.ShouldShowItem(horse, criolloMatches, getHorseName))
				{
					// use unique ID based on model name and index to handle duplicate display names
					ImGui::PushID(("Criollo_" + horse.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
					ImGui::PopID();
				}
			}
			ImGui::Spacing();
		}

		// dutch warmblood section
		if (showDutchWarmblood)
		{
			RenderCenteredSeparator("Dutch Warmblood");
			for (size_t i = 0; i < g_DutchWarmbloodHorses.size(); ++i)
			{
				const auto& horse = g_DutchWarmbloodHorses[i];
				if (g_HorseSearch.ShouldShowItem(horse, dutchWarmbloodMatches, getHorseName))
				{
					// use unique ID based on model name and index to handle duplicate display names
					ImGui::PushID(("DutchWarmblood_" + horse.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
					ImGui::PopID();
				}
			}
			ImGui::Spacing();
		}

		// gang section
		if (showGang)
		{
			RenderCenteredSeparator("Gang");
			for (size_t i = 0; i < g_GangHorses.size(); ++i)
			{
				const auto& horse = g_GangHorses[i];
				if (g_HorseSearch.ShouldShowItem(horse, gangMatches, getHorseName))
				{
					// use unique ID based on model name and index to handle duplicate display names
					ImGui::PushID(("Gang_" + horse.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
					ImGui::PopID();
				}
			}
			ImGui::Spacing();
		}

		// gypsy cob section
		if (showGypsyCob)
		{
			RenderCenteredSeparator("Gypsy Cob");
			for (size_t i = 0; i < g_GypsyCobHorses.size(); ++i)
			{
				const auto& horse = g_GypsyCobHorses[i];
				if (g_HorseSearch.ShouldShowItem(horse, gypsyCobMatches, getHorseName))
				{
					// use unique ID based on model name and index to handle duplicate display names
					ImGui::PushID(("GypsyCob_" + horse.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
					ImGui::PopID();
				}
			}
			ImGui::Spacing();
		}

		// hungarian halfbred section
		if (showHungarianHalfbred)
		{
			RenderCenteredSeparator("Hungarian Halfbred");
			for (size_t i = 0; i < g_HungarianHalfbredHorses.size(); ++i)
			{
				const auto& horse = g_HungarianHalfbredHorses[i];
				if (g_HorseSearch.ShouldShowItem(horse, hungarianHalfbredMatches, getHorseName))
				{
					// use unique ID based on model name and index to handle duplicate display names
					ImGui::PushID(("HungarianHalfbred_" + horse.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
					ImGui::PopID();
				}
			}
			ImGui::Spacing();
		}

		// kentucky saddler section
		if (showKentuckySaddler)
		{
			RenderCenteredSeparator("Kentucky Saddler");
			for (size_t i = 0; i < g_KentuckySaddlerHorses.size(); ++i)
			{
				const auto& horse = g_KentuckySaddlerHorses[i];
				if (g_HorseSearch.ShouldShowItem(horse, kentuckySaddlerMatches, getHorseName))
				{
					// use unique ID based on model name and index to handle duplicate display names
					ImGui::PushID(("KentuckySaddler_" + horse.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
					ImGui::PopID();
				}
			}
			ImGui::Spacing();
		}

		// klardruber section
		if (showKlardruber)
		{
			RenderCenteredSeparator("Klardruber");
			for (size_t i = 0; i < g_KlardruberHorses.size(); ++i)
			{
				const auto& horse = g_KlardruberHorses[i];
				if (g_HorseSearch.ShouldShowItem(horse, klardruberMatches, getHorseName))
				{
					// use unique ID based on model name and index to handle duplicate display names
					ImGui::PushID(("Klardruber_" + horse.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
					ImGui::PopID();
				}
			}
			ImGui::Spacing();
		}

		// missouri fox trotter section
		if (showMissouriFoxTrotter)
		{
			RenderCenteredSeparator("Missouri Fox Trotter");
			for (size_t i = 0; i < g_MissouriFoxTrotterHorses.size(); ++i)
			{
				const auto& horse = g_MissouriFoxTrotterHorses[i];
				if (g_HorseSearch.ShouldShowItem(horse, missouriFoxTrotterMatches, getHorseName))
				{
					// use unique ID based on model name and index to handle duplicate display names
					ImGui::PushID(("MissouriFoxTrotter_" + horse.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
					ImGui::PopID();
				}
			}
			ImGui::Spacing();
		}

		// morgan section
		if (showMorgan)
		{
			RenderCenteredSeparator("Morgan");
			for (size_t i = 0; i < g_MorganHorses.size(); ++i)
			{
				const auto& horse = g_MorganHorses[i];
				if (g_HorseSearch.ShouldShowItem(horse, morganMatches, getHorseName))
				{
					// use unique ID based on model name and index to handle duplicate display names
					ImGui::PushID(("Morgan_" + horse.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
					ImGui::PopID();
				}
			}
			ImGui::Spacing();
		}

		// mustang section
		if (showMustang)
		{
			RenderCenteredSeparator("Mustang");
			for (size_t i = 0; i < g_MustangHorses.size(); ++i)
			{
				const auto& horse = g_MustangHorses[i];
				if (g_HorseSearch.ShouldShowItem(horse, mustangMatches, getHorseName))
				{
					// use unique ID based on model name and index to handle duplicate display names
					ImGui::PushID(("Mustang_" + horse.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
					ImGui::PopID();
				}
			}
			ImGui::Spacing();
		}

		// nokota section
		if (showNokota)
		{
			RenderCenteredSeparator("Nokota");
			for (size_t i = 0; i < g_NokotaHorses.size(); ++i)
			{
				const auto& horse = g_NokotaHorses[i];
				if (g_HorseSearch.ShouldShowItem(horse, nokotaMatches, getHorseName))
				{
					// use unique ID based on model name and index to handle duplicate display names
					ImGui::PushID(("Nokota_" + horse.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
					ImGui::PopID();
				}
			}
			ImGui::Spacing();
		}

		// norfolk roadster section
		if (showNorfolkRoadster)
		{
			RenderCenteredSeparator("Norfolk Roadster");
			for (size_t i = 0; i < g_NorfolkRoadsterHorses.size(); ++i)
			{
				const auto& horse = g_NorfolkRoadsterHorses[i];
				if (g_HorseSearch.ShouldShowItem(horse, norfolkRoadsterMatches, getHorseName))
				{
					// use unique ID based on model name and index to handle duplicate display names
					ImGui::PushID(("NorfolkRoadster_" + horse.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
					ImGui::PopID();
				}
			}
			ImGui::Spacing();
		}

		// shire section
		if (showShire)
		{
			RenderCenteredSeparator("Shire");
			for (size_t i = 0; i < g_ShireHorses.size(); ++i)
			{
				const auto& horse = g_ShireHorses[i];
				if (g_HorseSearch.ShouldShowItem(horse, shireMatches, getHorseName))
				{
					// use unique ID based on model name and index to handle duplicate display names
					ImGui::PushID(("Shire_" + horse.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
					ImGui::PopID();
				}
			}
			ImGui::Spacing();
		}

		// suffolk punch section
		if (showSuffolkPunch)
		{
			RenderCenteredSeparator("Suffolk Punch");
			for (size_t i = 0; i < g_SuffolkPunchHorses.size(); ++i)
			{
				const auto& horse = g_SuffolkPunchHorses[i];
				if (g_HorseSearch.ShouldShowItem(horse, suffolkPunchMatches, getHorseName))
				{
					// use unique ID based on model name and index to handle duplicate display names
					ImGui::PushID(("SuffolkPunch_" + horse.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
					ImGui::PopID();
				}
			}
			ImGui::Spacing();
		}

		// tennessee walker section
		if (showTennesseeWalker)
		{
			RenderCenteredSeparator("Tennessee Walker");
			for (size_t i = 0; i < g_TennesseeWalkerHorses.size(); ++i)
			{
				const auto& horse = g_TennesseeWalkerHorses[i];
				if (g_HorseSearch.ShouldShowItem(horse, tennesseeWalkerMatches, getHorseName))
				{
					// use unique ID based on model name and index to handle duplicate display names
					ImGui::PushID(("TennesseeWalker_" + horse.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
					ImGui::PopID();
				}
			}
			ImGui::Spacing();
		}

		// thoroughbred section
		if (showThoroughbred)
		{
			RenderCenteredSeparator("Thoroughbred");
			for (size_t i = 0; i < g_ThoroughbredHorses.size(); ++i)
			{
				const auto& horse = g_ThoroughbredHorses[i];
				if (g_HorseSearch.ShouldShowItem(horse, thoroughbredMatches, getHorseName))
				{
					// use unique ID based on model name and index to handle duplicate display names
					ImGui::PushID(("Thoroughbred_" + horse.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
					ImGui::PopID();
				}
			}
			ImGui::Spacing();
		}

		// turkoman section
		if (showTurkoman)
		{
			RenderCenteredSeparator("Turkoman");
			for (size_t i = 0; i < g_TurkomanHorses.size(); ++i)
			{
				const auto& horse = g_TurkomanHorses[i];
				if (g_HorseSearch.ShouldShowItem(horse, turkomanMatches, getHorseName))
				{
					// use unique ID based on model name and index to handle duplicate display names
					ImGui::PushID(("Turkoman_" + horse.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
					ImGui::PopID();
				}
			}
			ImGui::Spacing();
		}

		// miscellaneous section
		if (showMiscellaneous)
		{
			RenderCenteredSeparator("Miscellaneous");
			for (size_t i = 0; i < g_MiscellaneousHorses.size(); ++i)
			{
				const auto& horse = g_MiscellaneousHorses[i];
				if (g_HorseSearch.ShouldShowItem(horse, miscellaneousMatches, getHorseName))
				{
					// use unique ID based on model name and index to handle duplicate display names
					ImGui::PushID(("Miscellaneous_" + horse.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(horse.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(horse.model, horse.variation, true);
					}
					ImGui::PopID();
				}
			}
			ImGui::Spacing();
		}

		// show helpful message when no matches found
		bool anyVisible = showAmericanPaint || showAmericanStandardbred || showAndalusian || showAppaloosa || showArabian ||
		                 showArdennes || showBelgian || showBreton || showCriollo || showDutchWarmblood || showGang ||
		                 showGypsyCob || showHungarianHalfbred || showKentuckySaddler || showKlardruber || showMissouriFoxTrotter ||
		                 showMorgan || showMustang || showNokota || showNorfolkRoadster || showShire || showSuffolkPunch ||
		                 showTennesseeWalker || showThoroughbred || showTurkoman || showMiscellaneous;

		if (!anyVisible && !g_HorseSearch.searchBuffer.empty())
		{
			ImGui::Text("No horses or sections match your search");
			ImGui::Text("Try searching for: 'american', 'arabian', 'black', 'grey', 'gang', 'mustang', etc.");
		}
	}

	// animal data structures
	struct LegendaryAnimal
	{
		std::string name;
		std::string model;
		int variation;
	};

	struct RegularAnimal
	{
		std::string name;
		std::string model;
		int variation;
	};

	struct Dog
	{
		std::string name;
		std::string model;
		int variation;
	};

	// legendary animals data (parsed from animals.txt)
	static std::vector<LegendaryAnimal> g_LegendaryAnimals = {
		{"Bull Gator", "a_c_alligator_02", 0},
		{"Bharati Grizzly Bear", "a_c_bear_01", 1},
		{"Beaver", "a_c_beaver_01", 1},
		{"Big Horn Ram", "a_c_bighornram_01", 12},
		{"Boar", "a_c_boarlegendary_01", 0},
		{"Buck", "a_c_buck_01", 3},
		{"Tatanka Bison", "a_c_buffalo_tatanka_01", 0},
		{"White Bison", "a_c_buffalo_01", 4},
		{"Legendary Cougar", "a_c_cougar_01", 5},
		{"Coyote", "a_c_coyote_01", 1},
		{"Elk", "a_c_elk_01", 1},
		{"Fox", "a_c_fox_01", 3},
		{"Moose", "a_c_moose_01", 6},
		{"Giaguaro Panther", "a_c_panther_01", 1},
		{"Pronghorn", "a_c_pronghorn_01", 1},
		{"Wolf", "a_c_wolf", 3},
		{"Teca Gator", "MP_A_C_Alligator_01", 0},
		{"Sun Gator", "MP_A_C_Alligator_01", 1},
		{"Banded Gator", "MP_A_C_Alligator_01", 2},
		{"Owiza Bear", "MP_A_C_Bear_01", 1},
		{"Ridgeback Spirit Bear", "MP_A_C_Bear_01", 2},
		{"Golden Spirit Bear", "MP_A_C_Bear_01", 3},
		{"Zizi Beaver", "MP_A_C_Beaver_01", 0},
		{"Moon Beaver", "MP_A_C_Beaver_01", 1},
		{"Night Beaver", "MP_A_C_Beaver_01", 2},
		{"Gabbro Horn Ram", "MP_A_C_BigHornRam_01", 0},
		{"Chalk Horn Ram", "MP_A_C_BigHornRam_01", 1},
		{"Rutile Horn Ram", "MP_A_C_BigHornRam_01", 2},
		{"Cogi Boar", "MP_A_C_Boar_01", 0},
		{"Wakpa Boar", "MP_A_C_Boar_01", 1},
		{"Icahi Boar", "MP_A_C_Boar_01", 2},
		{"Mud Runner Buck", "MP_A_C_Buck_01", 2},
		{"Snow Buck", "MP_A_C_Buck_01", 3},
		{"Shadow Buck", "MP_A_C_Buck_01", 4},
		{"Tatanka Bison", "MP_A_C_Buffalo_01", 0},
		{"Winyan Bison", "MP_A_C_Buffalo_01", 1},
		{"Payta Bison", "MP_A_C_Buffalo_01", 2},
		{"Iguga Cougar", "MP_A_C_Cougar_01", 0},
		{"Maza Cougar", "MP_A_C_Cougar_01", 1},
		{"Sapa Cougar", "MP_A_C_Cougar_01", 2},
		{"Red Streak Coyote", "MP_A_C_Coyote_01", 0},
		{"Midnight Paw Coyote", "MP_A_C_Coyote_01", 1},
		{"Milk Coyote", "MP_A_C_Coyote_01", 2},
		{"Katata Elk", "MP_A_C_Elk_01", 1},
		{"Ozula Elk", "MP_A_C_Elk_01", 2},
		{"Inahme Elk", "MP_A_C_Elk_01", 3},
		{"Ota Fox", "MP_A_C_Fox_01", 0},
		{"Marble Fox", "MP_A_C_Fox_01", 1},
		{"Cross Fox", "MP_A_C_Fox_01", 2},
		{"Snowflake Moose", "MP_A_C_Moose_01", 1},
		{"Knight Moose", "MP_A_C_Moose_01", 2},
		{"Ruddy Moose", "MP_A_C_Moose_01", 3},
		{"Nightwalker Panther", "MP_A_C_Panther_01", 0},
		{"Ghost Panther", "MP_A_C_Panther_01", 1},
		{"Iwakta Panther", "MP_A_C_Panther_01", 2},
		{"Emerald Wolf", "MP_A_C_Wolf_01", 0},
		{"Onyx Wolf", "MP_A_C_Wolf_01", 1},
		{"Moonstone Wolf", "MP_A_C_Wolf_01", 2}
	};

	// regular animals data (parsed from animals.txt - lines 1-304)
	static std::vector<RegularAnimal> g_RegularAnimals = {
		{"American Alligator", "A_C_Alligator_01", 0},
		{"American Alligator (small)", "A_C_Alligator_03", 0},
		{"Nine-Banded Armadillo", "A_C_Armadillo_01", 0},
		{"American Badger", "A_C_Badger_01", 0},
		{"Little Brown Bat", "A_C_Bat_01", 0},
		{"American Black Bear", "A_C_BearBlack_01", 0},
		{"Grizzly Bear", "A_C_Bear_01", 0},
		{"North American Beaver", "A_C_Beaver_01", 0},
		{"Blue Jay", "A_C_BlueJay_01", 0},
		{"Wild Boar", "A_C_Boar_01", 0},
		{"Whitetail Buck", "A_C_Buck_01", 0},
		{"Whitetail Deer", "A_C_Deer_01", 0},
		{"American Bison", "A_C_Buffalo_01", 0},
		{"Angus Bull", "A_C_Bull_01", 0},
		{"Devon Bull", "A_C_Bull_01", 3},
		{"Hereford Bull", "A_C_Bull_01", 2},
		{"American Bullfrog", "A_C_FrogBull_01", 0},
		{"Northern Cardinal", "A_C_Cardinal_01", 0},
		{"American Domestic Cat", "A_C_Cat_01", 0},
		{"Cedar Waxwing", "A_C_CedarWaxwing_01", 0},
		{"Dominique Chicken", "A_C_Chicken_01", 0},
		{"Dominique Rooster", "A_C_Rooster_01", 0},
		{"Java Chicken", "A_C_Chicken_01", 2},
		{"Java Rooster", "A_C_Rooster_01", 1},
		{"Leghorn Chicken", "A_C_Chicken_01", 3},
		{"Leghorn Rooster", "A_C_Rooster_01", 2},
		{"Greater Prairie Chicken", "A_C_PrairieChicken_01", 0},
		{"Western Chipmunk", "A_C_Chipmunk_01", 0},
		{"Californian Condor", "A_C_CaliforniaCondor_01", 0},
		{"Cougar", "A_C_Cougar_01", 0},
		{"Double-crested Cormorant", "A_C_Cormorant_01", 0},
		{"Neotropic Cormorant", "A_C_Cormorant_01", 2},
		{"Florida Cracker Cow", "A_C_Cow", 0},
		{"California Valley Coyote", "A_C_Coyote_01", 0},
		{"Cuban Land Crab", "A_C_Crab_01", 0},
		{"Red Swamp Crayfish", "A_C_Crawfish_01", 0},
		{"Whooping Crane", "A_C_CraneWhooping_01", 0},
		{"Sandhill Crane", "A_C_CraneWhooping_01", 1},
		{"American Crow", "A_C_Crow_01", 0},
		{"Standard Donkey", "A_C_Donkey_01", 0},
		{"Mallard Duck", "A_C_Duck_01", 0},
		{"Pekin Duck", "A_C_Duck_01", 2},
		{"Bald Eagle", "A_C_Eagle_01", 0},
		{"Golden Eagle", "A_C_Eagle_01", 1},
		{"Reddish Egret", "A_C_Egret_01", 0},
		{"Little Egret", "A_C_Egret_01", 1},
		{"Snowy Egret", "A_C_Egret_01", 2},
		{"Rocky Mountain Bull Elk", "A_C_Elk_01", 0},
		{"Rocky Mountain Cow Elk", "A_C_Elk_01", 2},
		{"American Red Fox", "A_C_Fox_01", 0},
		{"American Gray Fox", "A_C_Fox_01", 1},
		{"Silver Fox", "A_C_Fox_01", 2},
		{"Banded Gila Monster", "A_C_GilaMonster_01", 0},
		{"Alpine Goat", "A_C_Goat_01", 0},
		{"Canada Goose", "A_C_GooseCanada_01", 0},
		{"Ferruginous Hawk", "A_C_Hawk_01", 0},
		{"Red-tailed Hawk", "A_C_Hawk_01", 2},
		{"Rough-legged Hawk", "A_C_Hawk_01", 1},
		{"Great Blue Heron", "A_C_Heron_01", 0},
		{"Tricolored Heron", "A_C_Heron_01", 2},
		{"Desert Iguana", "A_C_IguanaDesert_01", 0},
		{"Green Iguana", "A_C_Iguana_01", 0},
		{"Collared Peccary", "A_C_Javelina_01", 0},
		{"Lion", "A_C_LionMangy_01", 0},
		{"Common Loon", "A_C_Loon_01", 0},
		{"Pacific Loon", "A_C_Loon_01", 2},
		{"Yellow-billed Loon", "A_C_Loon_01", 1},
		{"Western Bull Moose", "A_C_Moose_01", 3},
		{"Western Moose", "A_C_Moose_01", 0},
		{"Mule", "A_C_HORSEMULE_01", 0},
		{"American Muskrat", "A_C_Muskrat_01", 0},
		{"Baltimore Oriole", "A_C_Oriole_01", 0},
		{"Hooded Oriole", "A_C_Oriole_01", 1},
		{"Californian Horned Owl", "A_C_Owl_01", 1},
		{"Coastal Horned Owl", "A_C_Owl_01", 2},
		{"Great Horned Owl", "A_C_Owl_01", 0},
		{"Angus Ox", "A_C_Ox_01", 0},
		{"Devon Ox", "A_C_Ox_01", 2},
		{"Panther", "A_C_Panther_01", 0},
		{"Florida Panther", "A_C_Panther_01", 4},
		{"Carolina Parakeet", "A_C_CarolinaParakeet_01", 0},
		{"Blue and Yellow Macaw", "A_C_Parrot_01", 0},
		{"Great Green Macaw", "A_C_Parrot_01", 1},
		{"Scarlet Macaw", "A_C_Parrot_01", 2},
		{"American White Pelican", "A_C_Pelican_01", 0},
		{"Brown Pelican", "A_C_Pelican_01", 1},
		{"Ring-necked Pheasant", "A_C_Pheasant_01", 0},
		{"Chinese Ring-necked Pheasant", "A_C_Pheasant_01", 2},
		{"Berkshire Pig", "A_C_Pig_01", 0},
		{"Big China Pig", "A_C_Pig_01", 3},
		{"Old Spot Pig", "A_C_Pig_01", 2},
		{"Band-tailed Pigeon", "A_C_Pigeon", 2},
		{"Rock Pigeon", "A_C_Pigeon", 0},
		{"Virginia Opossum", "A_C_Possum_01", 0},
		{"American Pronghorn Buck", "A_C_Pronghorn_01", 10},
		{"American Pronghorn Doe", "A_C_Pronghorn_01", 0},
		{"Sonoran Pronghorn Buck", "A_C_Pronghorn_01", 13},
		{"Sonoran Pronghorn Doe", "A_C_Pronghorn_01", 4},
		{"Baja California Pronghorn Buck", "A_C_Pronghorn_01", 16},
		{"Baja California Pronghorn Doe", "A_C_Pronghorn_01", 7},
		{"California Quail", "A_C_Quail_01", 0},
		{"Sierra Nevada Bighorn Ram", "A_C_BigHornRam_01", 9},
		{"Sierra Nevada Bighorn Sheep", "A_C_BigHornRam_01", 0},
		{"Desert Bighorn Ram", "A_C_BigHornRam_01", 16},
		{"Desert Bighorn Sheep", "A_C_BigHornRam_01", 6},
		{"Rocky Mountain Bighorn Ram", "A_C_BigHornRam_01", 13},
		{"Rocky Mountain Bighorn Sheep", "A_C_BigHornRam_01", 3},
		{"Black-tailed Jackrabbit", "A_C_Rabbit_01", 0},
		{"North American Raccoon", "A_C_Raccoon_01", 0},
		{"Black Rat", "A_C_Rat_01", 4},
		{"Brown Rat", "A_C_Rat_01", 0},
		{"Western Raven", "A_C_Raven_01", 0},
		{"Red-footed Booby", "A_C_RedFootedBooby_01", 0},
		{"American Robin", "A_C_Robin_01", 0},
		{"Roseate Spoonbill", "A_C_RoseateSpoonbill_01", 0},
		{"Herring Gull", "A_C_Seagull_01", 0},
		{"Laughing Gull", "A_C_Seagull_01", 1},
		{"Ring-billed Gull", "A_C_Seagull_01", 2},
		{"Merino Sheep", "A_C_Sheep_01", 0},
		{"Striped Skunk", "A_C_Skunk_01", 0},
		{"Red Boa Snake", "A_C_SnakeRedBoa_01", 0},
		{"Rainbow Boa Snake", "A_C_SnakeRedBoa_01", 2},
		{"Sunglow Boa Snake", "A_C_SnakeRedBoa_01", 1},
		{"Diamondback Rattlesnake", "A_C_Snake_01", 0},
		{"Fer-de-Lance Snake", "A_C_SnakeFerDeLance_01", 0},
		{"Black-tailed Rattlesnake", "A_C_SnakeBlackTailRattle_01", 0},
		{"Timber Rattlesnake", "A_C_Snake_01", 2},
		{"Northern Copperhead Snake", "A_C_SnakeFerDeLance_01", 2},
		{"Southern Copperhead Snake", "A_C_SnakeFerDeLance_01", 1},
		{"Midland Water Snake", "A_C_SnakeWater_01", 0},
		{"Cottonmouth Snake", "A_C_SnakeWater_01", 1},
		{"Northern Water Snake", "A_C_SnakeWater_01", 2},
		{"Scarlet Tanager Songbird", "A_C_SongBird_01", 1},
		{"Western Tanager Songbird", "A_C_SongBird_01", 0},
		{"Eurasian Tree Sparrow", "A_C_Sparrow_01", 3},
		{"American Tree Sparrow", "A_C_Sparrow_01", 0},
		{"Golden Crowned Sparrow", "A_C_Sparrow_01", 2},
		{"American Red Squirrel", "A_C_Squirrel_01", 1},
		{"Western Gray Squirrel", "A_C_Squirrel_01", 0},
		{"Black Squirrel", "A_C_Squirrel_01", 2},
		{"Western Toad", "A_C_Toad_01", 0},
		{"Sonoran Desert Toad", "A_C_Toad_01", 3},
		{"Eastern Wild Turkey", "A_C_Turkey_01", 0},
		{"Rio Grande Wild Turkey", "A_C_TurkeyWild_01", 0},
		{"Alligator Snapping Turtle", "A_C_TurtleSnapping_01", 0},
		{"Eastern Turkey Vulture", "A_C_Vulture_01", 4},
		{"Western Turkey Vulture", "A_C_Vulture_01", 0},
		{"Gray Wolf", "A_C_Wolf", 0},
		{"Timber Wolf", "A_C_Wolf_Medium", 0},
		{"Gray Wolf (small)", "A_C_Wolf_Small", 0},
		{"Red-bellied Woodpecker", "A_C_Woodpecker_01", 0},
		{"Pileated Woodpecker", "A_C_Woodpecker_02", 0}
	};

	// dogs data (parsed from animals.txt - lines 382-408)
	static std::vector<Dog> g_Dogs = {
		{"American Foxhound", "A_C_DOGAMERICANFOXHOUND_01", 0},
		{"Australian Shepherd", "A_C_DOGAUSTRALIANSHEPERD_01", 0},
		{"Bluetick Coonhound", "A_C_DOGBLUETICKCOONHOUND_01", 0},
		{"Catahoula Cur", "A_C_DOGCATAHOULACUR_01", 0},
		{"Ches Bay Retriever", "A_C_DOGCHESBAYRETRIEVER_01", 0},
		{"Hobo", "A_C_DOGHOBO_01", 0},
		{"Hound", "A_C_DOGHOUND_01", 0},
		{"Husky", "A_C_DOGHUSKY_01", 0},
		{"Labrador", "A_C_DOGLAB_01", 0},
		{"Lion (dog)", "A_C_DOGLION_01", 0},
		{"Poodle", "A_C_DOGPOODLE_01", 0},
		{"Rough Collie", "A_C_DOGCOLLIE_01", 0},
		{"Rufus", "A_C_DOGRUFUS_01", 0},
		{"Street", "A_C_DOGSTREET_01", 0}
	};

	static void RenderAnimalsView()
	{
		// back button in top-right corner
		ImVec2 windowSize = ImGui::GetContentRegionAvail();
		ImVec2 originalPos = ImGui::GetCursorPos();

		ImGui::SetCursorPos(ImVec2(windowSize.x - 30, 5));
		if (ImGui::Button("X", ImVec2(25, 25)))
		{
			g_InAnimals = false;
		}

		// reset cursor to original position and add some top margin
		ImGui::SetCursorPos(ImVec2(originalPos.x, originalPos.y + 35));

		// helper function for centered separator with custom line width
		auto RenderCenteredSeparator = [](const char* text) {
			ImGui::PushFont(Menu::Font::g_ChildTitleFont);
			ImVec2 textSize = ImGui::CalcTextSize(text);
			ImVec2 contentRegion = ImGui::GetContentRegionAvail();

			// center text
			ImGui::SetCursorPosX((contentRegion.x - textSize.x) * 0.5f);
			ImGui::Text(text);
			ImGui::PopFont();

			// draw centered line (3x text width) using screen coordinates
			float lineWidth = textSize.x * 3.0f;
			float linePosX = (contentRegion.x - lineWidth) * 0.5f;

			ImDrawList* drawList = ImGui::GetWindowDrawList();
			ImVec2 windowPos = ImGui::GetWindowPos();
			ImVec2 cursorScreenPos = ImGui::GetCursorScreenPos();

			// use screen coordinates for proper positioning with scrolling
			drawList->AddLine(
				ImVec2(windowPos.x + linePosX, cursorScreenPos.y),
				ImVec2(windowPos.x + linePosX + lineWidth, cursorScreenPos.y),
				ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);

			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
			ImGui::Spacing();
		};

		// lambda functions for getting names
		auto getLegendaryName = [](const LegendaryAnimal& animal) { return animal.name; };
		auto getRegularName = [](const RegularAnimal& animal) { return animal.name; };
		auto getDogName = [](const Dog& dog) { return dog.name; };

		// check section matches
		bool legendaryMatches = g_AnimalSearch.SectionMatches("Legendary Animals", g_AnimalSearch.searchBuffer) ||
		                       g_AnimalSearch.SectionMatches("Legendary", g_AnimalSearch.searchBuffer);
		bool regularMatches = g_AnimalSearch.SectionMatches("Regular Animals", g_AnimalSearch.searchBuffer) ||
		                     g_AnimalSearch.SectionMatches("Animals", g_AnimalSearch.searchBuffer);
		bool dogMatches = g_AnimalSearch.SectionMatches("Dogs", g_AnimalSearch.searchBuffer);

		// count visible animals in each section
		int legendaryVisible = legendaryMatches ? static_cast<int>(g_LegendaryAnimals.size()) :
		                      g_AnimalSearch.CountMatches(g_LegendaryAnimals, g_AnimalSearch.searchBuffer, getLegendaryName);
		int regularVisible = regularMatches ? static_cast<int>(g_RegularAnimals.size()) :
		                    g_AnimalSearch.CountMatches(g_RegularAnimals, g_AnimalSearch.searchBuffer, getRegularName);
		int dogVisible = dogMatches ? static_cast<int>(g_Dogs.size()) :
		                g_AnimalSearch.CountMatches(g_Dogs, g_AnimalSearch.searchBuffer, getDogName);

		// determine section visibility
		bool showLegendarySection = legendaryMatches || (legendaryVisible > 0);
		bool showRegularSection = regularMatches || (regularVisible > 0);
		bool showDogSection = dogMatches || (dogVisible > 0);

		// calculate totals
		int totalAnimals = static_cast<int>(g_LegendaryAnimals.size() + g_RegularAnimals.size() + g_Dogs.size());
		int totalVisible = legendaryVisible + regularVisible + dogVisible;

		// render search bar with count
		g_AnimalSearch.RenderSearchBar("Search Animals", totalAnimals, totalVisible);

		// legendary animals section
		if (showLegendarySection)
		{
			RenderCenteredSeparator("Legendary Animals");

			for (size_t i = 0; i < g_LegendaryAnimals.size(); ++i)
			{
				const auto& animal = g_LegendaryAnimals[i];
				if (g_AnimalSearch.ShouldShowItem(animal, legendaryMatches, getLegendaryName))
				{
					// use unique ID with section prefix to prevent cross-section duplicates
					ImGui::PushID(("LegendaryAnimals_" + animal.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(animal.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(animal.model, animal.variation);
					}
					ImGui::PopID();
				}
			}

			ImGui::Spacing();
		}

		// regular animals section
		if (showRegularSection)
		{
			RenderCenteredSeparator("Regular Animals");

			for (size_t i = 0; i < g_RegularAnimals.size(); ++i)
			{
				const auto& animal = g_RegularAnimals[i];
				if (g_AnimalSearch.ShouldShowItem(animal, regularMatches, getRegularName))
				{
					// use unique ID with section prefix to prevent cross-section duplicates
					ImGui::PushID(("RegularAnimals_" + animal.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(animal.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(animal.model, animal.variation);
					}
					ImGui::PopID();
				}
			}

			ImGui::Spacing();
		}

		// dogs section
		if (showDogSection)
		{
			RenderCenteredSeparator("Dogs");

			for (size_t i = 0; i < g_Dogs.size(); ++i)
			{
				const auto& dog = g_Dogs[i];
				if (g_AnimalSearch.ShouldShowItem(dog, dogMatches, getDogName))
				{
					// use unique ID based on model name and index to handle duplicate display names
					ImGui::PushID((dog.model + "_" + std::to_string(i)).c_str());
					if (ImGui::Button(dog.name.c_str(), ImVec2(-1, 25)))
					{
						SpawnAnimal(dog.model, dog.variation);
					}
					ImGui::PopID();
				}
			}

			ImGui::Spacing();
		}

		// show helpful message when no matches found
		if (!showLegendarySection && !showRegularSection && !showDogSection && !g_AnimalSearch.searchBuffer.empty())
		{
			ImGui::Text("No animals or sections match your search");
			ImGui::Text("Try searching for: 'legendary', 'regular animals', 'dogs', 'bear', 'wolf', etc.");
		}
	}

	// fish data structure
	struct Fish
	{
		std::string name;
		std::string model;
		int variation;
	};

	static std::vector<Fish> g_Fishes = {
		{"Bluegill", "A_C_FishBluegil_01_sm", 0},
		{"Chain Pickerel", "A_C_FishChainPickerel_01_sm", 0},
		{"Redfin Pickerel", "A_C_FishRedfinPickerel_01_sm", 0},
		{"Rock Bass", "A_C_FishRockBass_01_sm", 0},
		{"Smallmouth Bass", "A_C_FishSmallMouthBass_01_ms", 0},
		{"Bullhead Catfish", "A_C_FishBullHeadCat_01_sm", 0},
		{"Perch", "A_C_FishPerch_01_sm", 0},
		{"Lake Sturgeon", "A_C_FishLakeSturgeon_01_lg", 0},
		{"Largemouth Bass", "A_C_FishLargeMouthBass_01_ms", 0},
		{"Steelhead Trout", "A_C_FishRainbowTrout_01_ms", 0},
		{"Channel Catfish", "A_C_FishChannelCatfish_01_lg", 0},
		{"Longnose Gar", "A_C_FishLongNoseGar_01_lg", 0},
		{"Muskie", "A_C_FishMuskie_01_lg", 0},
		{"Northern Pike", "A_C_FishNorthernPike_01_lg", 0},
		{"Sockeye Salmon", "A_C_FishSalmonSockeye_01_ms", 0},
		// legendary Fishes
		{"Legendary Bluegill", "A_C_FishBluegil_01_ms", 1},
		{"Legendary Chain Pickerel", "A_C_FishChainPickerel_01_ms", 1},
		{"Legendary Bullhead Catfish", "A_C_FishBullHeadCat_01_ms", 1},
		{"Legendary Redfin Pickerel", "A_C_FishRedfinPickerel_01_ms", 1},
		{"Legendary Rock Bass", "A_C_FishRockBass_01_ms", 1},
		{"Legendary Smallmouth Bass", "A_C_FishSmallMouthBass_01_lg", 1},
		{"Legendary Perch", "A_C_FishPerch_01_ms", 1},
		{"Legendary Lake Sturgeon", "A_C_FishLakeSturgeon_01_lg", 1},
		{"Legendary Largemouth Bass", "A_C_FishLargeMouthBass_01_lg", 1},
		{"Legendary Steelhead Trout", "A_C_FishRainbowTrout_01_lg", 1},
		{"Legendary Channel Catfish", "A_C_FishChannelCatfish_01_lg", 1},
		{"Legendary Longnose Gar", "A_C_FishLongNoseGar_01_lg", 3},
		{"Legendary Muskie", "A_C_FishMuskie_01_lg", 2},
		{"Legendary Northern Pike", "A_C_FishNorthernPike_01_lg", 3},
		{"Legendary Sockeye Salmon", "A_C_FishSalmonSockeye_01_lg", 1},
	};

	static void RenderFishesView()
	{
		// back button in top-right corner
		ImVec2 windowSize = ImGui::GetContentRegionAvail();
		ImVec2 originalPos = ImGui::GetCursorPos();

		ImGui::SetCursorPos(ImVec2(windowSize.x - 30, 5));
		if (ImGui::Button("X", ImVec2(25, 25)))
		{
			g_InFishes = false;
		}

		// reset cursor to original position and add some top margin
		ImGui::SetCursorPos(ImVec2(originalPos.x, originalPos.y + 35));

		// lambda functions for getting names
		auto getFishName = [](const Fish& fish) { return fish.name; };

		// count visible fishes based on search
		int totalFishes = static_cast<int>(g_Fishes.size());
		int visibleFishes = g_FishSearch.searchBuffer.empty() ? totalFishes :
		                   g_FishSearch.CountMatches(g_Fishes, g_FishSearch.searchBuffer, getFishName);

		// render search bar with count
		g_FishSearch.RenderSearchBar("Search Fishes", totalFishes, visibleFishes);

		// render all fishes in a simple flat list
		for (size_t i = 0; i < g_Fishes.size(); ++i)
		{
			const auto& fish = g_Fishes[i];
			if (g_FishSearch.ShouldShowItem(fish, false, getFishName))
			{
				// use unique ID based on model name and index to handle duplicate display names
				ImGui::PushID((fish.model + "_" + std::to_string(i)).c_str());
				if (ImGui::Button(fish.name.c_str(), ImVec2(-1, 25)))
				{
					SpawnAnimal(fish.model, fish.variation);
				}
				ImGui::PopID();
			}
		}

		// show helpful message when no matches found
		if (visibleFishes == 0 && !g_FishSearch.searchBuffer.empty())
		{
			ImGui::Text("No fishes match your search");
			ImGui::Text("Try searching for: 'bass', 'trout', 'salmon', 'legendary', etc.");
		}
	}

	static void RenderPedsRootView()
	{
		ImGui::PushID("peds"_J);

		// setup native hooks (essential for Set Model functionality)
		// only set up hooks when this specific UI is active to avoid multiplayer interference
		static bool hooks_initialized = false;
		if (!hooks_initialized)
		{
			NativeHooks::AddHook("long_update"_J, NativeIndex::GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH, GET_NUMBER_OF_THREADS_RUNNING_THE_SCRIPT_WITH_THIS_HASH);
			NativeHooks::AddHook("long_update"_J, NativeIndex::_GET_META_PED_TYPE, _GET_META_PED_TYPE);
			hooks_initialized = true;
		}

		// ped database button at the top
		if (ImGui::Button("Ped Database", ImVec2(120, 30)))
		{
			g_InPedDatabase = true;
		}

		ImGui::Spacing();

		// spawner settings section
		ImGui::PushFont(Menu::Font::g_ChildTitleFont);
		ImGui::Text("Spawner Settings");
		ImGui::PopFont();
		ImGui::Separator();
		ImGui::Spacing();

		// spawner settings using global variables
		ImGui::Checkbox("Bypass Relationship", &g_BypassRelationship);
		ImGui::Checkbox("Spawn Dead", &g_Dead);
		ImGui::Checkbox("Sedated", &g_Sedated);
		ImGui::Checkbox("Invisible", &g_Invis);
		ImGui::Checkbox("GodMode", &g_Godmode);
		ImGui::Checkbox("Frozen", &g_Freeze);
		ImGui::Checkbox("Armed", &g_Armed);
		ImGui::Checkbox("Companion", &g_Companion);

		// scale input field instead of slider
		ImGui::SetNextItemWidth(150);
		ImGui::InputFloat("Scale", &g_Scale, 0.1f, 1.0f, "%.3f");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("ped scale multiplier (1.000 = normal size)");

		// variation input field
		ImGui::SetNextItemWidth(150);
		ImGui::InputInt("Variation", &g_Variation);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("outfit variation number (0 = random/default)");

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// category buttons
		if (ImGui::Button("Humans", ImVec2(120, 30)))
		{
			g_InHumans = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Horses", ImVec2(120, 30)))
		{
			g_InHorses = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Animals", ImVec2(120, 30)))
		{
			g_InAnimals = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Fishes", ImVec2(120, 30)))
		{
			g_InFishes = true;
		}

		ImGui::Spacing();

		// unified search input across all navigation sections
		InputTextWithHint("##pedmodel", "Search All Peds", &g_PedModelBuffer, ImGuiInputTextFlags_CallbackCompletion, nullptr, PedSpawnerInputCallback)
		    .Draw();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Search across Animals, Horses, Humans, and Fishes - Press Tab to auto fill");

		// unified search results dropdown
		if (!g_PedModelBuffer.empty() && !IsPedModelInList(g_PedModelBuffer))
		{
			ImGui::BeginListBox("##unifiedsearch", ImVec2(400, 200));

			std::string searchLower = g_PedModelBuffer;
			std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

			// helper function for centered separator in dropdown
			auto RenderCenteredSeparator = [](const char* text) {
				ImGui::PushFont(Menu::Font::g_ChildTitleFont);
				ImVec2 textSize = ImGui::CalcTextSize(text);
				ImVec2 contentRegion = ImGui::GetContentRegionAvail();

				// center text
				ImGui::SetCursorPosX((contentRegion.x - textSize.x) * 0.5f);
				ImGui::Text(text);
				ImGui::PopFont();

				// draw centered line (3x text width) using screen coordinates
				float lineWidth = textSize.x * 3.0f;
				float linePosX = (contentRegion.x - lineWidth) * 0.5f;

				ImDrawList* drawList = ImGui::GetWindowDrawList();
				ImVec2 windowPos = ImGui::GetWindowPos();
				ImVec2 cursorScreenPos = ImGui::GetCursorScreenPos();

				// use screen coordinates for proper positioning with scrolling
				drawList->AddLine(
					ImVec2(windowPos.x + linePosX, cursorScreenPos.y),
					ImVec2(windowPos.x + linePosX + lineWidth, cursorScreenPos.y),
					ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);

				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
				ImGui::Spacing();
			};

			bool hasResults = false;

			// helper function to check if search matches section name
			auto sectionMatches = [&](const char* sectionName) -> bool {
				std::string sectionLower = sectionName;
				std::transform(sectionLower.begin(), sectionLower.end(), sectionLower.begin(), ::tolower);
				return sectionLower.find(searchLower) != std::string::npos;
			};

			// search legendary animals
			bool legendaryHasResults = false;
			bool legendarySection = sectionMatches("Legendary Animals") || sectionMatches("Legendary");

			if (legendarySection)
			{
				RenderCenteredSeparator("Legendary Animals");
				legendaryHasResults = true;
				hasResults = true;
				for (const auto& animal : g_LegendaryAnimals)
				{
					if (ImGui::Selectable(animal.name.c_str()))
					{
						g_PedModelBuffer = animal.model;
					}
				}
			}
			else
			{
				for (const auto& animal : g_LegendaryAnimals)
				{
					std::string nameLower = animal.name;
					std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
					if (nameLower.find(searchLower) != std::string::npos)
					{
						if (!legendaryHasResults)
						{
							RenderCenteredSeparator("Legendary Animals");
							legendaryHasResults = true;
							hasResults = true;
						}
						if (ImGui::Selectable(animal.name.c_str()))
						{
							g_PedModelBuffer = animal.model;
						}
					}
				}
			}

			// search regular animals
			bool regularHasResults = false;
			bool regularSection = sectionMatches("Regular Animals") || sectionMatches("Animals");

			if (regularSection)
			{
				RenderCenteredSeparator("Regular Animals");
				regularHasResults = true;
				hasResults = true;
				for (const auto& animal : g_RegularAnimals)
				{
					if (ImGui::Selectable(animal.name.c_str()))
					{
						g_PedModelBuffer = animal.model;
					}
				}
			}
			else
			{
				for (const auto& animal : g_RegularAnimals)
				{
					std::string nameLower = animal.name;
					std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
					if (nameLower.find(searchLower) != std::string::npos)
					{
						if (!regularHasResults)
						{
							RenderCenteredSeparator("Regular Animals");
							regularHasResults = true;
							hasResults = true;
						}
						if (ImGui::Selectable(animal.name.c_str()))
						{
							g_PedModelBuffer = animal.model;
						}
					}
				}
			}

			// search dogs
			bool dogHasResults = false;
			bool dogSection = sectionMatches("Dogs");

			if (dogSection)
			{
				RenderCenteredSeparator("Dogs");
				dogHasResults = true;
				hasResults = true;
				for (const auto& dog : g_Dogs)
				{
					if (ImGui::Selectable(dog.name.c_str()))
					{
						g_PedModelBuffer = dog.model;
					}
				}
			}
			else
			{
				for (const auto& dog : g_Dogs)
				{
					std::string nameLower = dog.name;
					std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
					if (nameLower.find(searchLower) != std::string::npos)
					{
						if (!dogHasResults)
						{
							RenderCenteredSeparator("Dogs");
							dogHasResults = true;
							hasResults = true;
						}
						if (ImGui::Selectable(dog.name.c_str()))
						{
							g_PedModelBuffer = dog.model;
						}
					}
				}
			}

			// search horses - all breed sections
			auto searchHorseSection = [&](const std::vector<Horse>& horses, const char* sectionName, bool& sectionHasResults) {
				for (const auto& horse : horses)
				{
					std::string nameLower = horse.name;
					std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
					if (nameLower.find(searchLower) != std::string::npos)
					{
						if (!sectionHasResults)
						{
							RenderCenteredSeparator(sectionName);
							sectionHasResults = true;
							hasResults = true;
						}
						if (ImGui::Selectable(horse.name.c_str()))
						{
							g_PedModelBuffer = horse.model;
						}
					}
				}
			};

			bool americanPaintHasResults = false, americanStandardbredHasResults = false, andalusianHasResults = false;
			bool appaloosaHasResults = false, arabianHasResults = false, ardennesHasResults = false;
			bool belgianHasResults = false, bretonHasResults = false, criolloHasResults = false;
			bool dutchWarmbloodHasResults = false, gangHorseHasResults = false, gypsyCobHasResults = false;
			bool hungarianHalfbredHasResults = false, kentuckySaddlerHasResults = false, klardruberHasResults = false;
			bool missouriFoxTrotterHasResults = false, morganHasResults = false, mustangHasResults = false;
			bool nokotaHasResults = false, norfolkRoadsterHasResults = false, shireHasResults = false;
			bool suffolkPunchHasResults = false, tennesseeWalkerHasResults = false, thoroughbredHasResults = false;
			bool turkomanHasResults = false, miscellaneousHorseHasResults = false;

			searchHorseSection(g_AmericanPaintHorses, "American Paint", americanPaintHasResults);
			searchHorseSection(g_AmericanStandardbredHorses, "American Standardbred", americanStandardbredHasResults);
			searchHorseSection(g_AndalusianHorses, "Andalusian", andalusianHasResults);
			searchHorseSection(g_AppaloosaHorses, "Appaloosa", appaloosaHasResults);
			searchHorseSection(g_ArabianHorses, "Arabian", arabianHasResults);
			searchHorseSection(g_ArdennesHorses, "Ardennes", ardennesHasResults);
			searchHorseSection(g_BelgianHorses, "Belgian", belgianHasResults);
			searchHorseSection(g_BretonHorses, "Breton", bretonHasResults);
			searchHorseSection(g_CriolloHorses, "Criollo", criolloHasResults);
			searchHorseSection(g_DutchWarmbloodHorses, "Dutch Warmblood", dutchWarmbloodHasResults);
			searchHorseSection(g_GangHorses, "Gang", gangHorseHasResults);
			searchHorseSection(g_GypsyCobHorses, "Gypsy Cob", gypsyCobHasResults);
			searchHorseSection(g_HungarianHalfbredHorses, "Hungarian Halfbred", hungarianHalfbredHasResults);
			searchHorseSection(g_KentuckySaddlerHorses, "Kentucky Saddler", kentuckySaddlerHasResults);
			searchHorseSection(g_KlardruberHorses, "Klardruber", klardruberHasResults);
			searchHorseSection(g_MissouriFoxTrotterHorses, "Missouri Fox Trotter", missouriFoxTrotterHasResults);
			searchHorseSection(g_MorganHorses, "Morgan", morganHasResults);
			searchHorseSection(g_MustangHorses, "Mustang", mustangHasResults);
			searchHorseSection(g_NokotaHorses, "Nokota", nokotaHasResults);
			searchHorseSection(g_NorfolkRoadsterHorses, "Norfolk Roadster", norfolkRoadsterHasResults);
			searchHorseSection(g_ShireHorses, "Shire", shireHasResults);
			searchHorseSection(g_SuffolkPunchHorses, "Suffolk Punch", suffolkPunchHasResults);
			searchHorseSection(g_TennesseeWalkerHorses, "Tennessee Walker", tennesseeWalkerHasResults);
			searchHorseSection(g_ThoroughbredHorses, "Thoroughbred", thoroughbredHasResults);
			searchHorseSection(g_TurkomanHorses, "Turkoman", turkomanHasResults);
			searchHorseSection(g_MiscellaneousHorses, "Miscellaneous", miscellaneousHorseHasResults);

			// search humans - all category sections
			auto searchHumanSection = [&](const std::vector<Human>& humans, const char* sectionName, bool& sectionHasResults) {
				for (const auto& human : humans)
				{
					std::string modelLower = human.model;
					std::transform(modelLower.begin(), modelLower.end(), modelLower.begin(), ::tolower);
					if (modelLower.find(searchLower) != std::string::npos)
					{
						if (!sectionHasResults)
						{
							RenderCenteredSeparator(sectionName);
							sectionHasResults = true;
							hasResults = true;
						}
						if (ImGui::Selectable(human.model.c_str()))
						{
							g_PedModelBuffer = human.model;
						}
					}
				}
			};

			bool ambientFemaleHasResults = false, ambientFemaleOrdinaryHasResults = false, ambientMaleHasResults = false;
			bool ambientMaleOrdinaryHasResults = false, ambientMaleSuppressedHasResults = false, cutsceneHasResults = false;
			bool multiplayerCutsceneHasResults = false, gangHumanHasResults = false, storyFinaleHasResults = false;
			bool multiplayerBloodMoneyHasResults = false, multiplayerBountyHuntersHasResults = false, multiplayerNaturalistHasResults = false;
			bool multiplayerHasResults = false, storyHasResults = false, randomEventHasResults = false;
			bool scenarioHasResults = false, storyScenarioFemaleHasResults = false, storyScenarioMaleHasResults = false;
			bool miscellaneousHumanHasResults = false;

			searchHumanSection(g_AmbientFemale, "Ambient Female", ambientFemaleHasResults);
			searchHumanSection(g_AmbientFemaleOrdinary, "Ambient Female Ordinary", ambientFemaleOrdinaryHasResults);
			searchHumanSection(g_AmbientMale, "Ambient Male", ambientMaleHasResults);
			searchHumanSection(g_AmbientMaleOrdinary, "Ambient Male Ordinary", ambientMaleOrdinaryHasResults);
			searchHumanSection(g_AmbientMaleSuppressed, "Ambient Male Suppressed", ambientMaleSuppressedHasResults);
			searchHumanSection(g_Cutscene, "Cutscene", cutsceneHasResults);
			searchHumanSection(g_MultiplayerCutscene, "Multiplayer Cutscene", multiplayerCutsceneHasResults);
			searchHumanSection(g_Gang, "Gang", gangHumanHasResults);
			searchHumanSection(g_StoryFinale, "Story Finale", storyFinaleHasResults);
			searchHumanSection(g_MultiplayerBloodMoney, "Multiplayer Blood Money", multiplayerBloodMoneyHasResults);
			searchHumanSection(g_MultiplayerBountyHunters, "Multiplayer Bounty Hunters", multiplayerBountyHuntersHasResults);
			searchHumanSection(g_MultiplayerNaturalist, "Multiplayer Naturalist", multiplayerNaturalistHasResults);
			searchHumanSection(g_Multiplayer, "Multiplayer", multiplayerHasResults);
			searchHumanSection(g_Story, "Story", storyHasResults);
			searchHumanSection(g_RandomEvent, "Random Event", randomEventHasResults);
			searchHumanSection(g_Scenario, "Scenario", scenarioHasResults);
			searchHumanSection(g_StoryScenarioFemale, "Story Scenario Female", storyScenarioFemaleHasResults);
			searchHumanSection(g_StoryScenarioMale, "Story Scenario Male", storyScenarioMaleHasResults);
			searchHumanSection(g_Miscellaneous, "Miscellaneous", miscellaneousHumanHasResults);

			// search fishes
			bool fishHasResults = false;
			bool fishSection = sectionMatches("Fishes") || sectionMatches("Fish");

			if (fishSection)
			{
				RenderCenteredSeparator("Fishes");
				fishHasResults = true;
				hasResults = true;
				for (const auto& fish : g_Fishes)
				{
					if (ImGui::Selectable(fish.name.c_str()))
					{
						g_PedModelBuffer = fish.model;
					}
				}
			}
			else
			{
				for (const auto& fish : g_Fishes)
				{
					std::string nameLower = fish.name;
					std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
					if (nameLower.find(searchLower) != std::string::npos)
					{
						if (!fishHasResults)
						{
							RenderCenteredSeparator("Fishes");
							fishHasResults = true;
							hasResults = true;
						}
						if (ImGui::Selectable(fish.name.c_str()))
						{
							g_PedModelBuffer = fish.model;
						}
					}
				}
			}

			ImGui::EndListBox();
		}

		ImGui::Spacing();

		// action buttons
		if (ImGui::Button("Spawn"))
		{
			// main spawn button always spawns a ped, not affected by set model checkbox
			SpawnPed(g_PedModelBuffer, g_Variation, true); // true = give weapon if armed is enabled
		}
		ImGui::SameLine();
		if (ImGui::Button("Set Model"))
		{
			FiberPool::Push([] {
				auto model = Joaat(g_PedModelBuffer);

				for (int i = 0; i < 30 && !STREAMING::HAS_MODEL_LOADED(model); i++)
				{
					STREAMING::REQUEST_MODEL(model, false);
					ScriptMgr::Yield();
				}

				PLAYER::SET_PLAYER_MODEL(Self::GetPlayer().GetId(), model, false);
				Self::Update();

				if (g_Variation > 0)
					Self::GetPed().SetVariation(g_Variation);
				else
					PED::_SET_RANDOM_OUTFIT_VARIATION(Self::GetPed().GetHandle(), true);

				// track model and variation for automatic session fix
				Hooks::Info::UpdateStoredPlayerModel(model, g_Variation);

				// give weapon if armed is enabled and ped is not an animal
				if (g_Armed && !Self::GetPed().IsAnimal())
				{
					auto weapon = GetDefaultWeaponForPed(g_PedModelBuffer);
					WEAPON::GIVE_WEAPON_TO_PED(Self::GetPed().GetHandle(), Joaat(weapon), 100, true, false, 0, true, 0.5f, 1.0f, 0x2CD419DC, true, 0.0f, false);
					WEAPON::SET_PED_INFINITE_AMMO(Self::GetPed().GetHandle(), true, Joaat(weapon));
					ScriptMgr::Yield();
					WEAPON::SET_CURRENT_PED_WEAPON(Self::GetPed().GetHandle(), "WEAPON_UNARMED"_J, true, 0, false, false);
				}

				STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
			});
		}
		ImGui::SameLine();
		if (ImGui::Button("Story Gang"))
		{
			// story gang members with their specific variations
			struct StoryGangMember
			{
				std::string model;
				int variation;
			};

			std::vector<StoryGangMember> storyGang = {
				{"cs_dutch", 4},                    // Dutch van der Linde
				{"cs_johnmarston", 6},              // John Marston
				{"cs_hoseamatthews", 8},            // Hosea Matthews
				{"cs_billwilliamson", 1},           // Bill Williamson
				{"cs_javierescuella", 20},          // Javier Escuella
				{"cs_micahbell", 1},                // Micah Bell
				{"cs_mrsadler", 17},                // Sadie Adler
				{"cs_charlessmith", 15},            // Charles Smith
				{"cs_mollyoshea", 5},               // Molly O'Shea
				{"cs_susangrimshaw", 7},            // Susan Grimshaw
				{"cs_abigailroberts", 3},           // Abigail Roberts Marston
				{"cs_marybeth", 5},                 // Mary-Beth Gaskill
				{"cs_karen", 9},                    // Karen Jones
				{"cs_uncle", 2},                    // Uncle
				{"cs_sean", 0}                      // Sean
			};

			// spawn each gang member using SpawnPed helper function
			// note: variations are fixed and won't be affected by the global variation setting
			for (const auto& member : storyGang)
			{
				SpawnPed(member.model, member.variation, true, true); // isStoryGang = true to preserve attributes
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Cleanup Peds"))
		{
			FiberPool::Push([] {
				for (auto it = g_SpawnedPeds.begin(); it != g_SpawnedPeds.end();)
				{
					if (it->IsValid())
					{
						if (it->GetMount())
						{
							it->GetMount().ForceControl();
							if (it->GetMount().HasControl())
								it->GetMount().ForceSync();
							it->GetMount().Delete();
						}

						it->ForceControl();
						if (it->HasControl())
							it->ForceSync();
						it->Delete();
					}
					it = g_SpawnedPeds.erase(it);
				}
			});
		}

	ImGui::PopID();
}

	Spawner::Spawner() :
	    Submenu::Submenu("Spawners")
	{
		auto main                = std::make_shared<Category>("Main");
		auto vehicle             = std::make_shared<Category>("Vehicle");
		auto vehicleSpawnerGroup = std::make_shared<Group>("Vehicle Spawner");
		auto trainSpawnerGroup   = std::make_shared<Group>("Train Spawner");

		// main category with nested navigation (formerly peds)
		main->AddItem(std::make_shared<ImGuiItem>([] {
			if (g_InPedDetailsView)
			{
				RenderPedDetailsView();
			}
			else if (g_InPedDatabase)
			{
				RenderPedDatabaseView();
			}
			else if (g_InHumans)
			{
				RenderHumansView();
			}
			else if (g_InHorses)
			{
				RenderHorsesView();
			}
			else if (g_InAnimals)
			{
				RenderAnimalsView();
			}
			else if (g_InFishes)
			{
				RenderFishesView();
			}
			else
			{
				RenderPedsRootView();
			}
		}));

		// vehicle and train spawners in vehicle category
		vehicleSpawnerGroup->AddItem(std::make_shared<ImGuiItem>([] {
			RenderVehicleSpawnerMenu();
		}));

		trainSpawnerGroup->AddItem(std::make_shared<ImGuiItem>([] {
			RenderTrainsMenu();
		}));

		vehicle->AddItem(vehicleSpawnerGroup);
		vehicle->AddItem(trainSpawnerGroup);

		AddCategory(std::move(main));
		AddCategory(std::move(vehicle));
	}
}