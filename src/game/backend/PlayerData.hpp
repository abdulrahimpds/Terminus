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
		RateLimiter m_PedFloodLimit{10s, 25};
		RateLimiter m_ObjectFloodLimit{10s, 30};
		RateLimiter m_PropSetFloodLimit{10s, 15};
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

		// suspicion tracking for task-tree anomalies
		int m_TaskTreeSuspicions{};
		std::chrono::steady_clock::time_point m_LastTaskTreeSuspicion{};

		RateLimiter m_LargeVehicleFloodLimit{15s, 5};
		RateLimiter m_TickerMessageRateLimit{5s, 3};

		// rate limiters for suspicious events
		RateLimiter m_WeaponDamageRateLimit{2s, 6};
		RateLimiter m_TrainEventRateLimit{10s, 3};
		RateLimiter m_AttachRateLimit{2s, 4};
		// rate limiter for task-tree updates (valid-triple flood protection)
		RateLimiter m_TaskTreeRateLimit{2s, 80};

		RateLimiter m_GhostEventRateLimit{3s, 5};



		std::optional<std::uint64_t> m_PeerIdToSpoofTo{};
	};
}
