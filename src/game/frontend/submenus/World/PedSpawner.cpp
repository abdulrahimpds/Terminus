#include "PedSpawner.hpp"

#include "core/commands/HotkeySystem.hpp"
#include "core/commands/LoopedCommand.hpp"
#include "game/backend/FiberPool.hpp"
#include "game/backend/NativeHooks.hpp"
#include "game/backend/ScriptMgr.hpp"
#include "game/backend/Self.hpp"
#include "game/frontend/items/Items.hpp"
#include "game/rdr/Enums.hpp"
#include "game/rdr/Natives.hpp"
#include "game/rdr/data/PedModels.hpp"


namespace YimMenu::Submenus
{
	// Native hook functions are now defined in Spawner.cpp to avoid duplicate symbols

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

	void RenderPedSpawnerMenu()
	{
		ImGui::PushID("peds"_J);

		// Native hooks are now registered in Spawner.cpp to avoid duplicate symbols

		static std::string pedModelBuffer;
		static float scale = 1;
		static int variation = 0;
		static bool dead, invis, godmode, freeze, companion, sedated, armed;
		static int formation;
		static std::vector<YimMenu::Ped> spawnedPeds;
		InputTextWithHint("##pedmodel", "Ped Model", &pedModelBuffer, ImGuiInputTextFlags_CallbackCompletion, nullptr, PedSpawnerInputCallback)
		    .Draw();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Press Tab to auto fill");
		if (!pedModelBuffer.empty() && !IsPedModelInList(pedModelBuffer))
		{
			ImGui::BeginListBox("##pedmodels", ImVec2(250, 100));

			std::string bufferLower = pedModelBuffer;
			std::transform(bufferLower.begin(), bufferLower.end(), bufferLower.begin(), ::tolower);
			for (const auto& [hash, model] : Data::g_PedModels)
			{
				std::string pedModelLower = model;
				std::transform(pedModelLower.begin(), pedModelLower.end(), pedModelLower.begin(), ::tolower);
				if (pedModelLower.find(bufferLower) != std::string::npos && ImGui::Selectable(model))
				{
					pedModelBuffer = model;
				}
			}

			ImGui::EndListBox();
		}

		ImGui::Checkbox("Spawn Dead", &dead);
		ImGui::Checkbox("Sedated", &sedated);
		ImGui::Checkbox("Invisible", &invis);
		ImGui::Checkbox("GodMode", &godmode);
		ImGui::Checkbox("Frozen", &freeze);
		ImGui::Checkbox("Armed", &armed);
		ImGui::Checkbox("Companion", &companion);
		if (companion)
		{
			if (ImGui::BeginCombo("Formation", groupFormations[formation]))
			{
				for (const auto& [num, name] : groupFormations)
				{
					bool is_selected = (formation == num);
					if (ImGui::Selectable(name, is_selected))
					{
						formation = num;
					}
					if (is_selected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("What formation should spawned companion use?");
		}
		ImGui::SliderFloat("Scale", &scale, 0.1, 10);

		ImGui::SetNextItemWidth(150);
		ImGui::InputInt("Variation", &variation);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("outfit variation number (0 = random/default)");

		if (ImGui::Button("Spawn"))
		{
			FiberPool::Push([] {
				auto ped = Ped::Create(Joaat(pedModelBuffer), Self::GetPed().GetPosition());

				if (!ped)
					return;

				ped.SetFrozen(freeze);

				if (dead)
				{
					ped.Kill();
					if (ped.IsAnimal())
						ped.SetQuality(2);
				}

				ped.SetInvincible(godmode);

				// Apply anti-lasso protection if godmode is enabled
				if (godmode)
				{
					// anti ragdoll
					ped.SetRagdoll(false);
					// anti lasso
					PED::SET_PED_LASSO_HOGTIE_FLAG(ped.GetHandle(), (int)LassoFlags::LHF_CAN_BE_LASSOED, false);
					PED::SET_PED_LASSO_HOGTIE_FLAG(ped.GetHandle(), (int)LassoFlags::LHF_CAN_BE_LASSOED_BY_FRIENDLY_AI, false);
					PED::SET_PED_LASSO_HOGTIE_FLAG(ped.GetHandle(), (int)LassoFlags::LHF_CAN_BE_LASSOED_BY_FRIENDLY_PLAYERS, false);
					PED::SET_PED_LASSO_HOGTIE_FLAG(ped.GetHandle(), (int)LassoFlags::LHF_DISABLE_IN_MP, true);
					// anti hogtie
					ENTITY::_SET_ENTITY_CARRYING_FLAG(ped.GetHandle(), (int)CarryingFlags::CARRYING_FLAG_CAN_BE_HOGTIED, false);
				}

				ped.SetVisible(!invis);

				if (scale != 1.0f)
					ped.SetScale(scale);

				if (variation > 0)
					ped.SetVariation(variation);

				// give weapon if armed is enabled and ped is not an animal
				if (armed && !ped.IsAnimal())
				{
					auto weapon = GetDefaultWeaponForPed(pedModelBuffer);
					WEAPON::GIVE_WEAPON_TO_PED(ped.GetHandle(), Joaat(weapon), 100, true, false, 0, true, 0.5f, 1.0f, 0x2CD419DC, true, 0.0f, false);
					WEAPON::SET_PED_INFINITE_AMMO(ped.GetHandle(), true, Joaat(weapon));
					ScriptMgr::Yield();
					WEAPON::SET_CURRENT_PED_WEAPON(ped.GetHandle(), "WEAPON_UNARMED"_J, true, 0, false, false);
				}

				ped.SetConfigFlag(PedConfigFlag::IsTranquilized, sedated);

				spawnedPeds.push_back(ped);

				if (companion)
				{
					int group = PED::GET_PED_GROUP_INDEX(YimMenu::Self::GetPed().GetHandle());
					if (!PED::DOES_GROUP_EXIST(group))
					{
						group = PED::CREATE_GROUP(0);
						PED::SET_PED_AS_GROUP_LEADER(YimMenu::Self::GetPed().GetHandle(), group, false);
					}

					ENTITY::SET_ENTITY_AS_MISSION_ENTITY(ped.GetHandle(), true, true);
					PED::SET_PED_AS_GROUP_MEMBER(ped.GetHandle(), group);
					PED::SET_PED_CAN_BE_TARGETTED_BY_PLAYER(ped.GetHandle(), YimMenu::Self::GetPlayer().GetId(), false);
					PED::SET_PED_RELATIONSHIP_GROUP_HASH(
					    ped.GetHandle(), PED::GET_PED_RELATIONSHIP_GROUP_HASH(YimMenu::Self::GetPed().GetHandle()));

					PED::SET_GROUP_FORMATION(PED::GET_PED_GROUP_INDEX(ped.GetHandle()), formation);

					DECORATOR::DECOR_SET_INT(ped.GetHandle(), "SH_CMP_companion", 2);

					if (ped.IsAnimal())
					{
						FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 104, 0.0);
						FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 105, 0.0);
						FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 10, 0.0);
						FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 146, 0.0);
						FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 113, 0.0);
						FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 114, 0.0);
						FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 115, 0.0);
						FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 116, 0.0);
						FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 117, 0.0);
						FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 118, 0.0);
						FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 119, 0.0);
						FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 111, 0.0);
						FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped.GetHandle(), 107, 0.0);
					}
					PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped.GetHandle(), false);

					ped.SetConfigFlag(PedConfigFlag::_0x16A14D9A, false);
					ped.SetConfigFlag(PedConfigFlag::_DisableHorseFleeILO, true);
					ped.SetConfigFlag(PedConfigFlag::_0x74F95F2E, false);
					ped.SetConfigFlag(PedConfigFlag::Avoidance_Ignore_All, false);
					ped.SetConfigFlag(PedConfigFlag::DisableShockingEvents, false);
					ped.SetConfigFlag(PedConfigFlag::DisablePedAvoidance, false);
					ped.SetConfigFlag(PedConfigFlag::DisableExplosionReactions, false);
					ped.SetConfigFlag(PedConfigFlag::DisableEvasiveStep, false);
					ped.SetConfigFlag(PedConfigFlag::DisableHorseGunshotFleeResponse, true);

					auto blip = MAP::BLIP_ADD_FOR_ENTITY("BLIP_STYLE_COMPANION"_J, ped.GetHandle());
					MAP::BLIP_ADD_MODIFIER(blip, "BLIP_MODIFIER_COMPANION_DOG"_J);
				}
			});
		}
		ImGui::SameLine();
		if (ImGui::Button("Set Model"))
		{
			FiberPool::Push([] {
				auto model = Joaat(pedModelBuffer);

				for (int i = 0; i < 30 && !STREAMING::HAS_MODEL_LOADED(model); i++)
				{
					STREAMING::REQUEST_MODEL(model, false);
					ScriptMgr::Yield();
				}

				PLAYER::SET_PLAYER_MODEL(Self::GetPlayer().GetId(), model, false);
				Self::Update();

				if (variation > 0)
					Self::GetPed().SetVariation(variation);
				else
					PED::_SET_RANDOM_OUTFIT_VARIATION(Self::GetPed().GetHandle(), true);

				// give weapon if armed is enabled and ped is not an animal
				if (armed && !Self::GetPed().IsAnimal())
				{
					auto weapon = GetDefaultWeaponForPed(pedModelBuffer);
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
			// placeholder for story gang functionality
			// will be implemented later with specific gang variants
		}
		ImGui::SameLine();
		if (ImGui::Button("Cleanup Peds"))
		{
			FiberPool::Push([] {
				for (auto it = spawnedPeds.begin(); it != spawnedPeds.end();)
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
					it = spawnedPeds.erase(it);
				}
			});
		}

		ImGui::PopID();
	}
}