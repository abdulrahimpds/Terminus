#include "core/hooking/DetourHook.hpp"
#include "game/hooks/Hooks.hpp"
#include "game/backend/Self.hpp"
#include "core/frontend/Notifications.hpp"
#include "game/backend/FiberPool.hpp"
#include "game/backend/Players.hpp"

#include <network/CNetGamePlayer.hpp>
#include <player/CPlayerInfo.hpp>

namespace YimMenu::Hooks
{
	int Protections::HandleCloneRemove(void* mgr, CNetGamePlayer* sender, CNetGamePlayer* target, uint16_t objectId, int ownershipToken, bool unk)
	{
		if (Self::GetPed() && Self::GetPed().GetNetworkObjectId() == objectId)
		{
			// immediately resync our ped to counter attempted removal
			FiberPool::Push([] {
				if (Self::GetPed())
					Self::GetPed().ForceSync();
			});

			Notifications::Show("Protections", std::format("Blocked player ped removal crash from {}", sender ? sender->GetName() : "unknown"), NotificationType::Warning);
			// quarantine the sender briefly to stop repeated attempts
			if (sender)
				Player(sender).GetData().QuarantineFor(std::chrono::seconds(10));
			return 0;
		}

		if (Self::GetMount() && Self::GetMount().GetNetworkObjectId() == objectId)
		{
			FiberPool::Push([] {
				if (Self::GetMount())
					Self::GetMount().ForceSync();
			});

			Notifications::Show("Protections", std::format("Blocked delete mount from {}", sender->GetName()), NotificationType::Warning);
			if (sender)
				Player(sender).GetData().QuarantineFor(std::chrono::seconds(10));
			return 0;
		}

		if (Self::GetVehicle() && Self::GetVehicle().GetNetworkObjectId() == objectId)
		{
			FiberPool::Push([] {
				if (Self::GetVehicle())
					Self::GetVehicle().ForceSync();
			});

			Notifications::Show("Protections", std::format("Blocked delete vehicle from {}", sender->GetName()), NotificationType::Warning);
			if (sender)
				Player(sender).GetData().QuarantineFor(std::chrono::seconds(10));
			return 0;
		}

		return BaseHook::Get<Protections::HandleCloneRemove, DetourHook<decltype(&Protections::HandleCloneRemove)>>()->Original()(mgr, sender, target, objectId, ownershipToken, unk);
	}
}