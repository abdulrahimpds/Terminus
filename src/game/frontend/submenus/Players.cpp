#include "Players.hpp"

#include "core/frontend/widgets/imgui_colors.h"
#include "game/backend/PlayerData.hpp"
#include "game/backend/Players.hpp"
#include "game/backend/PlayerDatabase.hpp"
#include "game/features/Features.hpp"
#include "game/frontend/items/Items.hpp"
#include "game/backend/FiberPool.hpp"
#include "core/commands/Commands.hpp"
#include "game/commands/PlayerCommand.hpp"

#include "Player/Helpful.hpp"
#include "Player/Info.hpp"
#include "Player/Kick.hpp"
#include "Player/Toxic.hpp"
#include "Player/Trolling.hpp"

namespace YimMenu::Submenus
{

	// Teleport action functions using existing commands
	static void TeleportToPlayer(Player player)
	{
		FiberPool::Push([player] {
			if (auto cmd = Commands::GetCommand<PlayerCommand>("tptoplayer"_J))
			{
				cmd->Call(player);
			}
		});
	}

	static void BringPlayer(Player player)
	{
		FiberPool::Push([player] {
			if (auto cmd = Commands::GetCommand<PlayerCommand>("bring"_J))
			{
				cmd->Call(player);
			}
		});
	}

	static void TeleportPlayerToWaypoint(Player player)
	{
		FiberPool::Push([player] {
			if (auto cmd = Commands::GetCommand<PlayerCommand>("tpplayertowaypoint"_J))
			{
				cmd->Call(player);
			}
		});
	}

	static void TeleportPlayerToJail(Player player)
	{
		FiberPool::Push([player] {
			if (auto cmd = Commands::GetCommand<PlayerCommand>("tpplayertojail"_J))
			{
				cmd->Call(player);
			}
		});
	}

	struct Tag
	{
		std::string Name;
		std::string FullName;
		ImVec4 Color;
	};

	static std::vector<Tag> GetPlayerTags(YimMenu::Player player)
	{
		std::vector<Tag> tags;

		if (player.IsHost())
			tags.push_back({"H", "Host", ImGui::Colors::DeepSkyBlue});

		if (player.IsModder())
			tags.push_back({"M", "Modder", ImGui::Colors::DeepPink});

		if (player.GetPed() && player.GetPed().IsInvincible())
			tags.push_back({"G", "Godmode", ImGui::Colors::Crimson});

		if (player.GetPed() && !player.GetPed().IsVisible())
			tags.push_back({"I", "Invisible", ImGui::Colors::MediumPurple});

		return tags;
	}

	static void DrawPlayerList(bool external = true, float offset = 15.0f)
	{
		struct ComparePlayerNames
		{
			bool operator()(YimMenu::Player a, YimMenu::Player b) const
			{
				std::string nameA = a.GetName();
				std::string nameB = b.GetName();
				return nameA < nameB;
			}
		};

		std::multimap<uint8_t, Player, ComparePlayerNames> sortedPlayers(YimMenu::Players::GetPlayers().begin(),
		    YimMenu::Players::GetPlayers().end());

		if (external)
		{
			ImGui::SetNextWindowPos(
			    ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x + offset, ImGui::GetWindowPos().y));
			ImGui::SetNextWindowSize(ImVec2(320, ImGui::GetWindowSize().y));
			ImGui::Begin("Player List", nullptr, ImGuiWindowFlags_NoDecoration);

			ImGui::Checkbox("Spectate", &YimMenu::g_Spectating);
			for (auto& [id, player] : sortedPlayers)
			{
				std::string display_name = player.GetName();

				ImGui::PushID(id);

				// Calculate layout: player name + tags on left, buttons aligned to far right
				float windowWidth = ImGui::GetContentRegionAvail().x;
				float buttonWidth = 20.0f; // Width for each small button
				float buttonsAreaWidth = buttonWidth * 4 + ImGui::GetStyle().ItemSpacing.x * 3; // 4 buttons + spacing

				// Start the row
				float cursorStartX = ImGui::GetCursorPosX();

				// Player name (clickable for selection) - limit width to prevent button overlap
				float nameAreaWidth = windowWidth - buttonsAreaWidth - 20.0f; // Reserve space for buttons
				if (ImGui::Selectable(display_name.c_str(), (YimMenu::Players::GetSelected() == player), 0, ImVec2(nameAreaWidth * 0.6f, 0)))
				{
					YimMenu::Players::SetSelected(id);
				}

				// Get the size of the selectable item to position tags right after it
				ImVec2 selectableSize = ImGui::GetItemRectSize();
				float nameEndX = cursorStartX + selectableSize.x;

				if (player.IsModder() && ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					for (auto detection : player.GetData().m_Detections)
						ImGui::BulletText("%s", g_PlayerDatabase->ConvertDetectionToDescription(detection).c_str());
					ImGui::EndTooltip();
				}

				// Add player tags immediately after the name
				auto tags = GetPlayerTags(player);
				if (!tags.empty())
				{
					ImGui::SameLine();
					auto old_item_spacing = ImGui::GetStyle().ItemSpacing.x;
					ImGui::GetStyle().ItemSpacing.x = 1;

					for (auto& tag : tags)
					{
						ImGui::SameLine();
						ImGui::PushStyleColor(ImGuiCol_Text, tag.Color);
						ImGui::Text(("[" + tag.Name + "]").c_str());
						if (ImGui::IsItemHovered())
							ImGui::SetTooltip(tag.FullName.c_str());
						ImGui::PopStyleColor();
					}

					ImGui::GetStyle().ItemSpacing.x = old_item_spacing;
				}

				// Position buttons at the far right
				float buttonsStartX = windowWidth - buttonsAreaWidth;
				ImGui::SameLine();
				ImGui::SetCursorPosX(buttonsStartX);

				// Add teleport action icons
				ImGui::PushID(("tp_to_" + std::to_string(id)).c_str());
				if (ImGui::SmallButton("T"))
				{
					TeleportToPlayer(player);
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Teleport To Player");
				ImGui::PopID();

				ImGui::SameLine();
				ImGui::PushID(("bring_" + std::to_string(id)).c_str());
				if (ImGui::SmallButton("B"))
				{
					BringPlayer(player);
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Bring Player");
				ImGui::PopID();

				ImGui::SameLine();
				ImGui::PushID(("tp_waypoint_" + std::to_string(id)).c_str());
				if (ImGui::SmallButton("W"))
				{
					TeleportPlayerToWaypoint(player);
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Teleport To Waypoint");
				ImGui::PopID();

				ImGui::SameLine();
				ImGui::PushID(("tp_jail_" + std::to_string(id)).c_str());
				if (ImGui::SmallButton("J"))
				{
					TeleportPlayerToJail(player);
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Teleport To Jail");
				ImGui::PopID();

				ImGui::PopID();
			}
			ImGui::End();
		}
		else
		{
			if (ImGui::BeginCombo("Players", YimMenu::Players::GetSelected().GetName()))
			{
				for (auto& [id, player] : sortedPlayers)
				{
					if (ImGui::Selectable(player.GetName(), (YimMenu::Players::GetSelected() == player)))
					{
						YimMenu::Players::SetSelected(id);
					}
				}
				ImGui::EndCombo();
			}
		}
	}

	Players::Players() :
	    Submenu::Submenu("Players")
	{
		AddCategory(std::move(BuildInfoMenu()));
		AddCategory(std::move(BuildHelpfulMenu()));
		AddCategory(std::move(BuildTrollingMenu()));
		AddCategory(std::move(BuildToxicMenu()));
		AddCategory(std::move(BuildKickMenu()));

		for (auto& category : m_Categories)
			category->PrependItem(std::make_shared<ImGuiItem>([] {
				DrawPlayerList();
			}));
	}
}
