#include "core/hooking/DetourHook.hpp"
#include "game/hooks/Hooks.hpp"
#include "game/pointers/Pointers.hpp"
#include "game/rdr/Enums.hpp"
#include "game/backend/Protections.hpp"
#include "game/rdr/Player.hpp"
#include "game/backend/PlayerData.hpp"

#include <network/CNetGamePlayer.hpp>

namespace YimMenu::Hooks
{
	int Protections::HandleCloneCreate(void* mgr, CNetGamePlayer* sender, uint16_t objectType, uint16_t objectId, int flags, void* guid, rage::datBitBuffer* buffer, int a8, int a9, bool isQueued)
	{
		// quarantine gate: drop clone create while sender is quarantined or during join-grace
		if (sender)
		{
			auto sp = Player(sender);
			if (sp.GetData().IsSyncsBlocked() || sp.GetData().IsInJoinGrace())
			{
				return 0;
			}
		}

		YimMenu::Protections::SetSyncingPlayer(sender);
		auto ret = BaseHook::Get<Protections::HandleCloneCreate, DetourHook<decltype(&Protections::HandleCloneCreate)>>()->Original()(mgr, sender, objectType, objectId, flags, guid, buffer, a8, a9, isQueued);
		YimMenu::Protections::SetSyncingPlayer(nullptr);

		if (ret == 6)
			ret = 0;

		return ret;
	}
}
