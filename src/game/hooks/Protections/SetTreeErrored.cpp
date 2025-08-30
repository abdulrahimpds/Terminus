#pragma once
#include "game/hooks/Hooks.hpp"
#include "core/hooking/DetourHook.hpp"
#include "game/backend/Protections.hpp"
#include "game/backend/PlayerData.hpp"

#include "core/frontend/Notifications.hpp"
#include <unordered_map>
#include <mutex>
#include <chrono>

namespace YimMenu::Hooks
{
	// rate-limit spam and still forward to the original to keep engine state consistent
	static std::unordered_map<std::string, std::chrono::steady_clock::time_point> g_last_log_time;
	static std::unordered_map<std::string, int> g_error_counts;
	static std::mutex g_log_mutex;

	void Protections::SetTreeErrored(rage::netSyncTree* tree, bool errored)
	{
		if (errored)
		{
			LOGF(SYNC, WARNING, "rage::netSyncTree::SetErrored called");
			// when the engine hits an error while processing sync, quarantine the last syncing player
			auto p = ::YimMenu::Protections::GetSyncingPlayer();
			if (p)
			{
				p.GetData().QuarantineFor(std::chrono::seconds(10));
			}
		}
		// always forward to original so the engine marks/clears the error flag
		BaseHook::Get<Protections::SetTreeErrored, DetourHook<decltype(&Protections::SetTreeErrored)>>()->Original()(tree, errored);
	}
}
