#include "core/commands/Command.hpp"
#include "core/frontend/Notifications.hpp"
#include "game/backend/Players.hpp"
#include "game/backend/ScriptMgr.hpp"
#include "game/backend/Self.hpp"
#include "game/rdr/Natives.hpp"
#include "util/teleport.hpp"
#include <vector>

namespace YimMenu::Features
{
	class BringAllPlayers : public Command
	{
		using Command::Command;

		virtual void OnCall() override
		{
			auto selfPos = Self::GetPed().GetPosition();
			auto selfId = Self::GetPlayer().GetId();

			// create a snapshot of current players to avoid crashes when new players join during teleportation
			std::vector<Player> playersToTeleport;
			for (auto& [idx, player] : Players::GetPlayers())
			{
				if (player.IsValid() && player.GetId() != selfId)
				{
					playersToTeleport.push_back(player);
				}
			}

			int successCount = 0;
			for (auto& player : playersToTeleport)
			{
				// revalidate player before teleporting (they might have left)
				if (!player.IsValid())
				{
					continue;
				}

				// add small delay between teleports
				if (successCount > 0)
				{
					ScriptMgr::Yield(100ms);
				}

				if (Teleport::TeleportPlayerToCoords(player, selfPos))
				{
					successCount++;
				}
			}

			// provide feedback to user
			if (!playersToTeleport.empty())
			{
				Notifications::Show("Teleport",
					std::format("Brought {}/{} players", successCount, (int)playersToTeleport.size()),
					successCount > 0 ? NotificationType::Success : NotificationType::Warning);
			}
			else
			{
				Notifications::Show("Teleport", "No other players to bring", NotificationType::Info);
			}
		}
	};

	static BringAllPlayers _BringAllPlayers{"bringall", "Bring", "Teleport all players to you"};
}