#include "Players.hpp"

#include "game/features/Features.hpp"
#include "game/pointers/Pointers.hpp"
#include "PlayerDatabase.hpp"
#include "core/commands/Commands.hpp"
#include "core/commands/BoolCommand.hpp"


#include <network/CNetGamePlayer.hpp>
#include <network/CNetworkPlayerMgr.hpp>
#include <network/rlGamerInfo.hpp>


namespace YimMenu
{
	Players::Players()
	{
		const auto& playerMgr = Pointers.NetworkPlayerMgr;
		if (!playerMgr)
			return;

		for (uint8_t idx = 0; idx < 32u; idx++)
		{
			if (const auto& netPlayer = playerMgr->m_PlayerList[idx];
			    netPlayer && (Pointers.GetNetPlayerFromPid(idx) == netPlayer) && netPlayer->IsValid())
			{
				OnPlayerJoin(netPlayer);
			}
		}
	}

	void Players::OnPlayerJoinImpl(CNetGamePlayer* player)
	{
		auto new_player = Player(player);

		m_Players[player->m_PlayerIndex]     = new_player;
		m_PlayerDatas[player->m_PlayerIndex] = PlayerData();

		if (auto p = g_PlayerDatabase->GetPlayer(new_player.GetRID()))
		{
			m_PlayerDatas[player->m_PlayerIndex].m_Detections = *reinterpret_cast<std::unordered_set<Detection>*>(&p->infractions);
			if (p->name != player->GetName())
			{
				p->name = player->GetName();
				g_PlayerDatabase->Save();
			}
		}

		if (new_player.GetGamerInfo()->m_GamerHandle2 != new_player.GetGamerInfo()->m_GamerHandle)
		{
			new_player.AddDetection(Detection::SPOOFING_ROCKSTAR_ID);
		}

		if (new_player.GetGamerInfo()->m_ExternalAddress.m_packed != new_player.GetExternalAddress().m_packed)
		{
			new_player.AddDetection(Detection::SPOOFING_IP);
		}

		// TODO: needs more work
		if (std::string(new_player.GetName()).contains('~'))
		{
			new_player.AddDetection(Detection::SPOOFING_NAME);
		}
	}

	void Players::OnPlayerLeaveImpl(CNetGamePlayer* player)
	{
		// remove from player maps
		m_Players.erase(player->m_PlayerIndex);
		// erase player data
		if (auto it = m_PlayerDatas.find(player->m_PlayerIndex); it != m_PlayerDatas.end())
		{
			m_PlayerDatas.erase(it);
		}
		else
		{
			m_PlayerDatas.erase(player->m_PlayerIndex);
		}

		// simplified: if no players have Logging checked, turn off all "Log ..." debuggers
		{
			bool any = false;
			for (auto& [idx, data] : m_PlayerDatas)
			{
				if (data.m_Logging) { any = true; break; }
			}
			if (!any && Players::IsLoggingAutoEnabled())
			{
				auto startsWithLog = [](const std::string& s) {
					return s.size() >= 3 && (s[0] == 'l' || s[0] == 'L') && (s[1] == 'o' || s[1] == 'O') && (s[2] == 'g' || s[2] == 'G');
				};
				for (auto* bc : Commands::GetBoolCommands())
				{
					const auto& n = bc->GetName();
					const auto& l = bc->GetLabel();
					if ((startsWithLog(n) || startsWithLog(l)) && bc->GetState())
						bc->SetState(false);
				}
				Players::SetLoggingAutoEnabled(false);
			}
		}
	}

	Player Players::GetByRIDImpl(uint64_t rid)
	{
		for (auto& [idx, player] : Players::GetPlayers())
		{
			if (player.GetGamerInfo()->m_GamerHandle2.m_RockstarId == rid)
			{
				return player;
			}
		}

		return nullptr;
	}

	Player Players::GetByHostTokenImpl(uint64_t token)
	{
		for (auto& [idx, player] : Players::GetPlayers())
		{
			if (player.GetGamerInfo()->m_HostToken == token)
			{
				return player;
			}
		}

		return nullptr;
	}

	Player Players::GetByMessageIdImpl(int id)
	{
		for (auto& [idx, player] : Players::GetPlayers())
		{
			if (player.GetHandle()->m_MessageId == id)
			{
				return player;
			}
		}

		return nullptr;
	}
}