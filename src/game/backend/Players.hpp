#pragma once
#include "game/rdr/Player.hpp"
#include "PlayerData.hpp"

namespace YimMenu
{
	class Players
	{
		std::unordered_map<uint8_t, Player> m_Players{};
		std::unordered_map<uint8_t, PlayerData> m_PlayerDatas{};
		Player m_SelectedPlayer = Player((uint8_t)0);
		// tracks whether logging debuggers were auto-enabled via the first per-player Logging checkbox
		bool m_LoggingAutoEnabled = false;

	public:
		static void OnPlayerJoin(CNetGamePlayer* player)
		{
			GetInstance().OnPlayerJoinImpl(player);
		}

		static void OnPlayerLeave(CNetGamePlayer* player)
		{
			GetInstance().OnPlayerLeaveImpl(player);
		}

		static Player GetSelected()
		{
			return GetInstance().m_SelectedPlayer;
		}

		static void SetSelected(Player player)
		{
			GetInstance().m_SelectedPlayer = player;
		}

		static std::unordered_map<uint8_t, Player>& GetPlayers()
		{
			return GetInstance().m_Players;
		}

		static Player GetByRID(uint64_t rid)
		{
			return GetInstance().GetByRIDImpl(rid);
		}

		static Player GetByHostToken(uint64_t token)
		{
			return GetInstance().GetByHostTokenImpl(token);
		}

		static Player GetByMessageId(int id)
		{
			return GetInstance().GetByMessageIdImpl(id);
		}

		static PlayerData& GetPlayerData(uint8_t idx)
		{
			return GetInstance().m_PlayerDatas[idx];
		}

		// flag management for auto-enabled debug logging
		static void SetLoggingAutoEnabled(bool v)
		{
			GetInstance().m_LoggingAutoEnabled = v;
		}
		static bool IsLoggingAutoEnabled()
		{
			return GetInstance().m_LoggingAutoEnabled;
		}

			// logging filter helper: if any players have logging enabled, only log for those; otherwise log all
			static bool ShouldLogFor(Player player)
			{
				auto& inst = GetInstance();
				bool any = false;
				for (auto& [i, data] : inst.m_PlayerDatas)
				{
					if (data.m_Logging) { any = true; break; }
				}
				if (!any) return true; // no selection -> log everyone
				return player && player.GetData().m_Logging;
			}


		static Player GetRandom()
		{
			auto& players = GetPlayers();

			if (players.empty())
			{
				return Player((uint8_t)0);
			}

			uint8_t random = static_cast<uint8_t>(rand() % players.size());
			auto it = std::next(players.begin(), random);
			return it->second;
		}

	private:
		Players();

		static Players& GetInstance()
		{
			static Players Instance;
			return Instance;
		}

		void OnPlayerJoinImpl(CNetGamePlayer* player);
		void OnPlayerLeaveImpl(CNetGamePlayer* player);
		Player GetByRIDImpl(uint64_t rid);
		Player GetByHostTokenImpl(uint64_t token);
		Player GetByMessageIdImpl(int id);
	};
}