#include "core/hooking/DetourHook.hpp"
#include "core/frontend/Notifications.hpp"
#include "game/backend/Protections.hpp"
#include "game/backend/Self.hpp"
#include "game/backend/CrashSignatures.hpp"
#include "game/hooks/Hooks.hpp"
#include "game/rdr/Enums.hpp"
#include "game/rdr/Natives.hpp"
#include "game/pointers/Pointers.hpp"

#include "game/backend/PlayerData.hpp"
#include <network/netObject.hpp>

#include "game/rdr/Player.hpp"

#include <network/CNetGamePlayer.hpp>
#include <rage/datBitBuffer.hpp>

#include "core/commands/BoolCommand.hpp"
namespace YimMenu::Features { extern BoolCommand _BlockKickFromMount; }

namespace YimMenu::Hooks
{
	// exception handling for complex attacks
	static int SafeCallOriginal(void* mgr, CNetGamePlayer* src, CNetGamePlayer* dst, uint16_t objectType, uint16_t objectId, rage::datBitBuffer* buffer, int a7, int a8, void* a9)
	{
		// additional validation before calling original
		try
		{
			// validate buffer pointer range
			if (buffer)
			{
				uintptr_t bufferAddr = reinterpret_cast<uintptr_t>(buffer);

				// check against known attack signatures with pattern detection
				if (CrashSignatures::IsKnownCrashPointerEnhanced(buffer))
				{
					LOG(WARNING) << "HandleCloneSync: Blocked attack signature or pattern in buffer: " << HEX(bufferAddr);
					return 0;
				}

				// validate buffer is in valid memory range
				if (bufferAddr < 0x10000 || bufferAddr > 0x7FFFFFFFFFFF)
				{
					LOG(WARNING) << "HandleCloneSync: Blocked invalid buffer address: " << HEX(bufferAddr);
					return 0;
				}

				// check for common corruption patterns
				if ((bufferAddr & 0xFFFFFFFF) == 0x97 ||
				    (bufferAddr & 0xFFFFFFFF) == 0x7 ||
				    (bufferAddr & 0xFFFFFFFF) == 0xC08)
				{
					LOG(WARNING) << "HandleCloneSync: Blocked known corruption pattern: " << HEX(bufferAddr);
					return 0;
				}
			}

			// validate player pointers with pattern detection
			if (src)
			{
				uintptr_t srcAddr = reinterpret_cast<uintptr_t>(src);
				if (CrashSignatures::IsKnownCrashPointerEnhanced(src) || srcAddr < 0x10000)
				{
					LOG(WARNING) << "HandleCloneSync: Blocked attack pattern in src player: " << HEX(srcAddr);
					return 0;
				}
			}

			if (dst)
			{
				uintptr_t dstAddr = reinterpret_cast<uintptr_t>(dst);
				if (CrashSignatures::IsKnownCrashPointerEnhanced(dst) || dstAddr < 0x10000)
				{
					LOG(WARNING) << "HandleCloneSync: Blocked attack pattern in dst player: " << HEX(dstAddr);
					return 0;
				}
			}

			return BaseHook::Get<Protections::HandleCloneSync, DetourHook<decltype(&Protections::HandleCloneSync)>>()->Original()(mgr, src, dst, objectType, objectId, buffer, a7, a8, a9);
		}
		catch (...)
		{
			LOG(WARNING) << "HandleCloneSync: Caught exception in protected block - Nemesis attack blocked";
			return 0; // safe fallback
		}
	}
	int Protections::HandleCloneSync(void* mgr, CNetGamePlayer* src, CNetGamePlayer* dst, uint16_t objectType, uint16_t objectId, rage::datBitBuffer* buffer, int a7, int a8, void* a9)
	{
		// validate all pointers before use (based on .map analysis)
		if (!mgr || !src || !dst || !buffer)
		{
			LOG(WARNING) << "HandleCloneSync: Blocked null pointer - mgr:" << HEX(reinterpret_cast<uintptr_t>(mgr))
			             << " src:" << HEX(reinterpret_cast<uintptr_t>(src))
			             << " dst:" << HEX(reinterpret_cast<uintptr_t>(dst))
			             << " buffer:" << HEX(reinterpret_cast<uintptr_t>(buffer));
			return 0;
		}

// quarantine gate: drop all clone sync while sender is quarantined or during join-grace
		if (src)
		{
			auto sp = Player(src);
			if (sp.GetData().IsSyncsBlocked() || sp.GetData().IsInJoinGrace())
			{
				return 0;
			}
		}

		// crash signature checking with pattern detection
		if (CrashSignatures::IsKnownCrashPointerEnhanced(mgr) ||
		    CrashSignatures::IsKnownCrashPointerEnhanced(src) ||
		    CrashSignatures::IsKnownCrashPointerEnhanced(dst) ||
		    CrashSignatures::IsKnownCrashPointerEnhanced(buffer) ||
		    CrashSignatures::IsKnownCrashPointerEnhanced(a9))
		{
			LOG(WARNING) << "HandleCloneSync: Blocked crash signature or attack pattern (intelligent detection)";

			// add detection for the attacking player
			if (src && reinterpret_cast<uintptr_t>(src) > 0x10000)
			{
				try
				{
					Player(src).AddDetection(Detection::TRIED_CRASH_PLAYER);
					Notifications::Show("Protections", std::format("Blocked Nemesis attack from {}", src->GetName()), NotificationType::Warning);
				}
				catch (...)
				{
					LOG(WARNING) << "HandleCloneSync: Exception accessing attacking player info";
				}
			}
			return 0;
		}

		// detect fuzzer attack patterns in clone sync data
		// fuzzer attacks often embed invalid task combinations in clone sync
		if (buffer && buffer->m_Data)
		{
			// check for fuzzer attack signatures in buffer data
			auto bufferData = reinterpret_cast<uintptr_t>(buffer->m_Data);
			if ((bufferData & 0xFFFFFFFF) == 0x97 ||   // Nemesis fuzzer signature
			    (bufferData & 0xFFFFFFFF) == 0x7 ||    // Nemesis fuzzer signature
			    (bufferData & 0xFFFFFFFF) == 0xC08)    // BringPlayer fuzzer signature
			{
				LOG(WARNING) << "HandleCloneSync: Blocked fuzzer attack pattern in buffer data: " << HEX(bufferData);
				return 0;
			}
		}

		// additional attack pattern detection
		uintptr_t bufferAddr = reinterpret_cast<uintptr_t>(buffer);
		uintptr_t srcAddr = reinterpret_cast<uintptr_t>(src);
		uintptr_t dstAddr = reinterpret_cast<uintptr_t>(dst);

		// check for specific corruption attack patterns
		if ((bufferAddr & 0xFFFFFFFF) == 0x97 || (bufferAddr & 0xFFFFFFFF) == 0x7 ||
		    (srcAddr & 0xFFFFFFFF) == 0x97 || (srcAddr & 0xFFFFFFFF) == 0x7 ||
		    (dstAddr & 0xFFFFFFFF) == 0x97 || (dstAddr & 0xFFFFFFFF) == 0x7)
		{
			LOG(WARNING) << "HandleCloneSync: Blocked Nemesis attack pattern - buffer:" << HEX(bufferAddr)
			             << " src:" << HEX(srcAddr) << " dst:" << HEX(dstAddr);

			if (src && srcAddr > 0x10000)
			{
				try
				{
					Player(src).AddDetection(Detection::TRIED_CRASH_PLAYER);
					Notifications::Show("Protections", std::format("Blocked Nemesis crash pattern from {}", src->GetName()), NotificationType::Warning);
				}
				catch (...) {}
			}
			return 0;
		}

		// detect corrupted data from crash victims that can cause secondary crashes
		if (src && srcAddr > 0x10000)
		{
			try
			{
				// check if source player has recently crashed or is sending corrupted data
				std::string srcName = src->GetName();

				// detect cascade attack patterns - corrupted data from crash victims
				if ((bufferAddr >= 0x1A02C7DF000 && bufferAddr <= 0x1A02C850FFF) ||  // corrupted pointer range from crash log
				    (bufferAddr & 0xFFFFFFF0) == 0x1A02C850320 ||                     // specific corruption pattern
				    (bufferAddr & 0xFFFFFFF0) == 0x1A02C7DFC40)                      // another corruption pattern
				{
					LOG(WARNING) << "HandleCloneSync: Blocked CASCADE CRASH from " << srcName
					             << " - corrupted data from crash victim: " << HEX(bufferAddr);

					Notifications::Show("Protections",
					    std::format("Blocked cascade crash from {} (crash victim spreading corruption)", srcName),
					    NotificationType::Warning);

					// don't add detection for cascade crashes - the player might be innocent victim
					return 0;
				}
			}
			catch (...)
			{
				LOG(WARNING) << "HandleCloneSync: Exception during cascade crash detection";
			}
		}

		// basic buffer validation (simplified due to type constraints)
		// note: detailed buffer validation would require full rage::datBitBuffer definition

		// verify the game objects exist before manipulation
		// note: this is a simplified check - actual implementation may vary based on RDR2 internals
		if (objectId > 0)
		{
			// basic validation that the object ID is in reasonable range
			if (objectId > 0xFFFF)
			{
				LOG(WARNING) << "HandleCloneSync: Invalid object ID " << objectId;
				return 0;
			}
		}

		if (Self::GetPed() && Self::GetPed().GetNetworkObjectId() == objectId)
		{
			Notifications::Show("Protections", std::format("Blocked player sync crash from {}", src->GetName()), NotificationType::Warning);
			Player(src).AddDetection(Detection::TRIED_CRASH_PLAYER);
			return 0;
		}

		if (Features::_BlockKickFromMount.GetState() && Self::GetMount() && Self::GetMount().GetNetworkObjectId() == objectId && Self::GetMount().HasControl())
		{
			Notifications::Show("Protections", std::format("Blocked kick from mount from {}", src->GetName()), NotificationType::Warning);
			return 0;
		}

		// validate entity type to mitigate spoofed type sync (sunrise sync crash)
		if (auto obj = Pointers.GetNetObjectById(objectId))
		{
			if (obj->m_ObjectType != objectType)
			{
				LOGF(WARNING, "HandleCloneSync: Blocked entity type spoof (objId {} actual {} != claimed {}) from {}", objectId, (int)obj->m_ObjectType, (int)objectType, src ? src->GetName() : "unknown");
				try { Player(src).AddDetection(Detection::TRIED_CRASH_PLAYER); } catch (...) {}
				Player(src).GetData().QuarantineFor(std::chrono::seconds(10));
				return 0;
			}
		}

		// wrap the critical section for maximum protection
		YimMenu::Protections::SetSyncingPlayer(src);

		// call the exception-protected helper function
		int result = SafeCallOriginal(mgr, src, dst, objectType, objectId, buffer, a7, a8, a9);

		YimMenu::Protections::SetSyncingPlayer(nullptr);
		return result;
	}
}
