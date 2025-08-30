#include "core/frontend/Notifications.hpp"
#include "core/hooking/DetourHook.hpp"
#include "game/backend/FiberPool.hpp"
#include "game/backend/Players.hpp"
#include "game/backend/ScriptMgr.hpp"
#include "game/backend/Self.hpp"
#include "game/hooks/Hooks.hpp"
#include "game/rdr/Natives.hpp"

#include <chrono>
#include <network/rlScSessionEvent.hpp>


namespace YimMenu::Hooks
{
	// state tracking for automatic model fix
	static uint32_t g_StoredCustomModel = 0;
	static int g_StoredVariation = 0;
	static uint32_t g_BaseModel = "mp_male"_J;
	static bool g_InSession = false;
	static bool g_NeedsAutoFix = false;

	// same-session sync monitoring
	static std::chrono::steady_clock::time_point g_LastSyncCheck = std::chrono::steady_clock::now();
	static bool g_SyncMonitoringActive = false;

	// function to store current model when using Set Model
	void Info::UpdateStoredPlayerModel(uint32_t model, int variation)
	{
		if (model == "mp_male"_J || model == "mp_female"_J)
		{
			g_BaseModel = model;
			g_StoredCustomModel = 0;
			g_StoredVariation = 0;
			g_SyncMonitoringActive = false; // stop monitoring standard models
		}
		else
		{
			g_StoredCustomModel = model;
			g_StoredVariation = variation;
			g_SyncMonitoringActive = true; // start monitoring custom models
			g_LastSyncCheck = std::chrono::steady_clock::now();
		}
	}

	// lightweight sync repair without model switching
	static void PerformSyncRepair()
	{
		auto selfPlayer = Self::GetPlayer();
		auto selfPed = Self::GetPed();

		if (!selfPlayer.IsValid() || !selfPed.IsValid())
			return;

		auto currentModel = selfPed.GetModel();

		// only repair custom models
		if (currentModel == "mp_male"_J || currentModel == "mp_female"_J)
			return;

		LOGF(VERBOSE, "Performing gentle sync refresh for custom model");

		// very gentle repair: only force network sync without visibility changes
		if (selfPed.IsNetworked())
		{
			// gentle network sync - no visibility cycling to avoid invisibility
			selfPed.ForceSync();
			ScriptMgr::Yield(100ms);
		}

		// no model reapplication - too aggressive and causes issues
		// the network sync validation fix should be sufficient
	}

	// monitor for sync issues and auto-repair
	static void MonitorSyncHealth()
	{
		// disabled for now - the network validation fix should be sufficient
		// automatic repairs were causing invisibility issues
		return;

		if (!g_SyncMonitoringActive || !g_InSession)
			return;

		auto now = std::chrono::steady_clock::now();
		auto timeSinceLastCheck = std::chrono::duration_cast<std::chrono::minutes>(now - g_LastSyncCheck);

		// check every 3 minutes for custom models
		if (timeSinceLastCheck.count() >= 3)
		{
			auto selfPed = Self::GetPed();
			if (selfPed.IsValid())
			{
				auto currentModel = selfPed.GetModel();

				// if using custom model, perform preventive sync repair
				if (currentModel != "mp_male"_J && currentModel != "mp_female"_J)
				{
					FiberPool::Push([] {
						PerformSyncRepair();
					});
				}
			}

			g_LastSyncCheck = now;
		}
	}

	// mimics your exact manual process step by step
	static void PerformAutomaticModelFix()
	{
		if (!g_NeedsAutoFix || g_StoredCustomModel == 0)
			return;

		auto selfPlayer = Self::GetPlayer();
		auto selfPed = Self::GetPed();

		if (!selfPlayer.IsValid() || !selfPed.IsValid())
			return;

		// step 1: exactly like your manual process - switch to base model (mp_male/mp_female)
		for (int i = 0; i < 30 && !STREAMING::HAS_MODEL_LOADED(g_BaseModel); i++)
		{
			STREAMING::REQUEST_MODEL(g_BaseModel, false);
			ScriptMgr::Yield(50ms); // shorter yields to avoid menu blocking
		}

		PLAYER::SET_PLAYER_MODEL(selfPlayer.GetId(), g_BaseModel, false);
		Self::Update();

		// apply outfit variation for base model
		PED::_SET_RANDOM_OUTFIT_VARIATION(Self::GetPed().GetHandle(), true);

		// wait like you would manually (time to see it worked) - use shorter yields
		for (int i = 0; i < 40; i++) // 40 * 50ms = 2000ms total
		{
			ScriptMgr::Yield(50ms);
		}

		// step 2: exactly like your manual process - switch back to custom model
		for (int i = 0; i < 30 && !STREAMING::HAS_MODEL_LOADED(g_StoredCustomModel); i++)
		{
			STREAMING::REQUEST_MODEL(g_StoredCustomModel, false);
			ScriptMgr::Yield(50ms); // shorter yields to avoid menu blocking
		}

		PLAYER::SET_PLAYER_MODEL(selfPlayer.GetId(), g_StoredCustomModel, false);
		Self::Update();

		// restore exact variation like your manual process
		if (g_StoredVariation > 0)
			Self::GetPed().SetVariation(g_StoredVariation);
		else
			PED::_SET_RANDOM_OUTFIT_VARIATION(Self::GetPed().GetHandle(), true);

		// cleanup
		STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(g_BaseModel);
		STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(g_StoredCustomModel);

		g_NeedsAutoFix = false;
	}

	void Info::HandleSessionEvent(rage::rlScSessionMultiplayer* mp, CNetworkScServerConnection* cxn, rage::rlScSessionEvent* evt)
	{
		switch (evt->m_EventType)
		{
		case rage::SessionEvent::ADD_PLAYER:
		{
			auto event = reinterpret_cast<rage::rlScAddPlayerEvent*>(evt);
			LOGF(VERBOSE,
			    "Adding player RID={} RID2={} acctId={} IP={}.{}.{}.{}:{} {}",
			    event->m_Identifier.m_Handle.m_RockstarId,
			    event->m_PeerAddress.m_GamerHandle.m_RockstarId,
			    event->m_Identifier.m_AccountId,
			    event->m_PeerAddress.m_ExternalAddress.m_field1,
			    event->m_PeerAddress.m_ExternalAddress.m_field2,
			    event->m_PeerAddress.m_ExternalAddress.m_field3,
			    event->m_PeerAddress.m_ExternalAddress.m_field4,
			    event->m_PeerAddress.m_ExternalPort,
			    event->m_PeerAddress.m_RelayState);

			// block join if the RID carried in the ticket doesn't match the RID in the peer address (spoof detection)
			{
				const uint64_t rid_ticket = event->m_Identifier.m_Handle.m_RockstarId;          // from session identifier (ticket)
				const uint64_t rid_addr   = event->m_PeerAddress.m_GamerHandle.m_RockstarId;     // from peer address (what they present)
				if (rid_ticket != rid_addr)
				{
					std::string msg = std::string("Blocked player join (spoofed RID) addr=")
					                      .append(std::to_string(rid_addr))
					                      .append(" ident=")
					                      .append(std::to_string(rid_ticket));
					Notifications::Show("Protections", msg, NotificationType::Warning);
					LOG(WARNING) << msg;

					// do not forward to the original handler; drop this add-player event locally
					return;
				}
			}

			// detect when WE join a new session (not just another player joining our session)
			// additional check: only trigger if we actually need the fix
			if (!g_InSession && g_NeedsAutoFix)
			{
				g_InSession = true;

				LOGF(VERBOSE, "Triggering automatic model fix for session join");

				// perform automatic fix after session stabilizes (like you do manually)
				// use shorter yields to avoid menu blocking
				FiberPool::Push([] {
					// wait for session to fully stabilize (like you wait before doing manual fix)
					// use shorter yields to avoid menu blocking
					for (int i = 0; i < 100; i++) // 100 * 50ms = 5000ms total
					{
						ScriptMgr::Yield(50ms);
					}

					// perform the exact same steps you do manually
					PerformAutomaticModelFix();
				});
			}
			else if (!g_InSession)
			{
				// we joined a session but don't need fix (using standard model)
				g_InSession = true;
				LOGF(VERBOSE, "Joined session but no model fix needed");
			}

			// monitor sync health for custom models
			MonitorSyncHealth();

			break;
		}
		case rage::SessionEvent::LEAVE_SESSION:
		{
			auto event = reinterpret_cast<rage::rlScLeaveSessionEvent*>(evt);
			LOG(INFO) << "Leave session: reason = " << event->m_Reason << " reason2 = " << event->m_Reason2;

			// when leaving session with custom model, flag for auto fix on next join
			auto selfPed = Self::GetPed();
			if (selfPed.IsValid())
			{
				auto currentModel = selfPed.GetModel();
				if (currentModel != "mp_male"_J && currentModel != "mp_female"_J && g_StoredCustomModel != 0)
				{
					g_NeedsAutoFix = true;
				}
			}

			g_InSession = false;
			break;
		}
		}
		return BaseHook::Get<Info::HandleSessionEvent, DetourHook<decltype(&Info::HandleSessionEvent)>>()->Original()(mp, cxn, evt);
	}
}
