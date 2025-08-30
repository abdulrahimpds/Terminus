#pragma once
#include "Detections.hpp"
#include "core/misc/RateLimiter.hpp"
#include "game/rdr/Player.hpp"
#include <unordered_set>
#include <chrono>
#include <optional>


namespace YimMenu
{
	class PlayerData
	{
	public:
		std::unordered_set<Detection> m_Detections{};
		Player m_SpectatingPlayer{nullptr};
		bool m_UseSessionSplitKick{};
		bool m_BlockExplosions{};
		bool m_BlockParticles{};
		bool m_GhostMode{};

		RateLimiter m_VehicleFloodLimit{10s, 10};
		// temporary quarantine end time to block all net traffic from this player
		std::optional<std::chrono::steady_clock::time_point> m_BlockSyncsUntil{};

		// returns true while the player is in quarantine (all sync/messages should be blocked)
		inline bool IsSyncsBlocked() const
		{
			return m_BlockSyncsUntil.has_value() && std::chrono::steady_clock::now() < *m_BlockSyncsUntil;
		}

		// start or extend quarantine duration
		inline void QuarantineFor(std::chrono::seconds duration)
		{
			m_BlockSyncsUntil = std::chrono::steady_clock::now() + duration;
		}

		RateLimiter m_LargeVehicleFloodLimit{15s, 5};
		RateLimiter m_TickerMessageRateLimit{5s, 3};

		std::optional<std::uint64_t> m_PeerIdToSpoofTo{};
	};
}