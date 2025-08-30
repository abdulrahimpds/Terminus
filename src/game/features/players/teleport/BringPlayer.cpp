#include "core/frontend/Notifications.hpp"
#include "game/backend/Self.hpp"
#include "game/backend/Players.hpp"
#include "game/commands/PlayerCommand.hpp"
#include "game/rdr/Natives.hpp"
#include "util/teleport.hpp"

namespace YimMenu::Features
{
	class BringPlayer : public PlayerCommand
	{
		using PlayerCommand::PlayerCommand;

		virtual void OnCall(Player player) override
		{
			// validate player before teleporting
			if (!player.IsValid())
			{
				Notifications::Show("Teleport", "Invalid player selected", NotificationType::Error);
				return;
			}

			// don't teleport self
			if (player.GetId() == Self::GetPlayer().GetId())
			{
				Notifications::Show("Teleport", "Cannot bring yourself", NotificationType::Warning);
				return;
			}

			auto selfPos = Self::GetPed().GetPosition();
			if (Teleport::TeleportPlayerToCoords(player, selfPos))
			{
				Notifications::Show("Teleport",
					std::format("Brought {}", player.GetName()),
					NotificationType::Success);
			}
			else
			{
				Notifications::Show("Teleport",
					std::format("Failed to bring {}", player.GetName()),
					NotificationType::Error);
			}
		}
	};

	static BringPlayer _BringPlayer{"bring", "Bring Player", "Teleport the player to you"};
}