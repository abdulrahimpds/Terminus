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
		// per-player logging filter (when enabled, only log this player's traffic)
		bool m_Logging{};

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
		// rate limiter for projectile creation (valid weapon spam / invisible shooters)
		RateLimiter m_ProjectileCreateRateLimit{1s, 10};
		// rate limiter for projectile attachment spam (reduce false positives vs. arrows/dynamite by allowing normal rates)
		RateLimiter m_ProjectileAttachRateLimit{1s, 8};
		// rate limiter for null-object sync logging (detect flood of null object nodes)
		RateLimiter m_NullObjectLogRateLimit{2s, 60};

		RateLimiter m_TaskTreeRateLimit{std::chrono::seconds(2), 80};

		// rate limiter for null-object pointer spam seen before crashes
		RateLimiter m_NullObjectFloodLimit{std::chrono::milliseconds(200), 8};
		// rate limiters for vehicle creations (generic and ambient)
		RateLimiter m_VehicleCreationRateLimit{2s, 3};
		RateLimiter m_AmbientVehicleCreationRateLimit{5s, 1};

		RateLimiter m_GhostEventRateLimit{3s, 5};
		// per-event rate limiters to suppress valid-but-spammed events
		RateLimiter m_ClearTasksRateLimit{2s, 4};
		// give-control flood limiter (host/control spam) — allow high burst to avoid false positives
		RateLimiter m_GiveControlRateLimit{1s, 30};

		// draft-vehicle control spam limiter
		RateLimiter m_DraftVehControlRateLimit{2s, 8};

		// recent null-object flood metadata for escalation logic
		std::chrono::steady_clock::time_point m_LastNullObjectFloodAt{};
		int m_NullObjectFloodStrikes{};

		std::optional<std::uint64_t> m_PeerIdToSpoofTo{};
	};
}
