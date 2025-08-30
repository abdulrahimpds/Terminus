#include "core/commands/BoolCommand.hpp"
#include "core/frontend/Notifications.hpp"
#include "core/hooking/DetourHook.hpp"
#include "game/backend/Players.hpp"
#include "game/backend/Self.hpp"
#include "game/backend/ScriptMgr.hpp"
#include "game/backend/FiberPool.hpp"
#include "game/frontend/ChatDisplay.hpp"
#include "game/hooks/Hooks.hpp"
#include "game/rdr/Natives.hpp"
#include "game/features/players/toxic/AttachmentTracker.hpp"
#include "network/CNetGamePlayer.hpp"

#include <player/CPlayerInfo.hpp>


namespace YimMenu::Features
{
	BoolCommand _DetectSpoofedNames{"detectspoofednames", "Detect Spoofed Names", "Detects If a Player is Possibly Spoofing Their Name"};
}
namespace YimMenu::Hooks
{
	void Info::PlayerHasJoined(CNetGamePlayer* player)
	{
		BaseHook::Get<Info::PlayerHasJoined, DetourHook<decltype(&Info::PlayerHasJoined)>>()->Original()(player);

		if (g_Running)
		{
			Players::OnPlayerJoin(player);
			uint64_t rid      = player->m_PlayerInfo->m_GamerInfo.m_GamerHandle2.m_RockstarId;
			netAddress ipaddr = player->m_PlayerInfo->m_GamerInfo.m_ExternalAddress;
			std::string ip_str = std::format("{}.{}.{}.{}", ipaddr.m_field1, ipaddr.m_field2, ipaddr.m_field3, ipaddr.m_field4);

			LOG(INFO) << std::format("{} joined the session. Reserved slot #{}. RID: {} | IP: {}",
			    player->GetName(),
			    (int)player->m_PlayerIndex,
			    (int)rid,
			    ip_str);
		}
	}

	void Info::PlayerHasLeft(CNetGamePlayer* player)
	{
		BaseHook::Get<Info::PlayerHasLeft, DetourHook<decltype(&Info::PlayerHasLeft)>>()->Original()(player);

		if (g_Running)
		{
			if (player == Self::GetPlayer().GetHandle())
			{
				ChatDisplay::Clear();
			}

			// check if we're attached to the player who is leaving
			if (Features::g_AttachedToPlayerId == player->m_PlayerIndex && Features::g_AttachedToPlayerHandle != 0)
			{
				FiberPool::Push([] {
					auto selfPed = Self::GetPed();
					if (selfPed.IsValid() && ENTITY::IS_ENTITY_ATTACHED_TO_ANY_PED(selfPed.GetHandle()))
					{
						// detach from the leaving player
						ENTITY::DETACH_ENTITY(selfPed.GetHandle(), true, true);
						PED::SET_PED_SHOULD_PLAY_IMMEDIATE_SCENARIO_EXIT(selfPed.GetHandle());
						TASK::CLEAR_PED_TASKS_IMMEDIATELY(selfPed.GetHandle(), true, true);

						Features::g_AttachedToPlayerHandle = 0;
						Features::g_AttachedToPlayerId = -1;

						// force model refresh to fix invisibility issue
						selfPed.SetVisible(false);
						ScriptMgr::Yield(50ms);
						selfPed.SetVisible(true);

						if (selfPed.IsNetworked())
						{
							selfPed.ForceSync();
						}

						// additional refresh by reapplying current model
						auto selfPlayer = Self::GetPlayer();
						if (selfPlayer.IsValid())
						{
							auto currentModel = selfPed.GetModel();
							if (STREAMING::HAS_MODEL_LOADED(currentModel))
							{
								PLAYER::SET_PLAYER_MODEL(selfPlayer.GetId(), currentModel, false);
								Self::Update();
							}
						}

						Notifications::Show("Attachment", "automatically detached due to player leaving session", NotificationType::Info);
					}
				});
			}

			Players::OnPlayerLeave(player);
			uint64_t rid      = player->m_PlayerInfo->m_GamerInfo.m_GamerHandle2.m_RockstarId;
			netAddress ipaddr = player->m_PlayerInfo->m_GamerInfo.m_ExternalAddress;
			std::string ip_str = std::format("{}.{}.{}.{}", ipaddr.m_field1, ipaddr.m_field2, ipaddr.m_field3, ipaddr.m_field4);

			LOG(INFO)
			    << std::format("{} left the session. Freeing slot #{}. RID: {} | IP: {}", player->GetName(), (int)player->m_PlayerIndex, (int)rid, ip_str);
		}
	}
}
