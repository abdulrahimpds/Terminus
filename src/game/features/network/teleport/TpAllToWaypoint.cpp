#include "core/commands/Command.hpp"
#include "game/backend/Self.hpp"
#include "game/rdr/Natives.hpp"
#include "game/backend/Players.hpp"
#include "game/backend/CrashSignatures.hpp"
#include "util/teleport.hpp"

namespace YimMenu::Features
{
	class TpAllToWaypoint : public Command
	{
		using Command::Command;

		virtual void OnCall() override
		{
			// validate waypoint exists before teleporting
			if (!MAP::IS_WAYPOINT_ACTIVE())
			{
				LOG(WARNING) << "TpAllToWaypoint: No waypoint active - cannot teleport players";
				return;
			}

			// get waypoint coords once and validate
			auto waypointCoords = Teleport::GetWaypointCoords();
			if (waypointCoords.x == 0.0f && waypointCoords.y == 0.0f && waypointCoords.z == 0.0f)
			{
				LOG(WARNING) << "TpAllToWaypoint: Invalid waypoint coordinates - cannot teleport players";
				return;
			}

			// validate each player before teleporting
			for (auto& [idx, player] : Players::GetPlayers())
			{
				// validate player object before use
				if (!player.IsValid())
				{
					LOG(WARNING) << "TpAllToWaypoint: Invalid player object - skipping";
					continue;
				}

				// check player pointer against crash signature database
				auto playerPtr = reinterpret_cast<void*>(&player);
				if (CrashSignatures::IsKnownCrashPointer(playerPtr))
				{
					LOG(WARNING) << "TpAllToWaypoint: Blocked known crash signature player pointer";
					continue;
				}

				// wrap teleport call in exception handling
				try
				{
					Teleport::TeleportPlayerToCoords(player, waypointCoords);
				}
				catch (...)
				{
					LOG(WARNING) << "TpAllToWaypoint: Caught exception during teleport - player may be invalid";
					continue;
				}
			}
		}
	};

	static TpAllToWaypoint _TpAllToWaypoint{"tpalltowaypoint", "Teleport To Waypoint", "Teleport all players to your active waypoint"};
}