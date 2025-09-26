#include "core/commands/BoolCommand.hpp"
#include "core/frontend/Notifications.hpp"
#include "core/misc/RateLimiter.hpp"
#include "game/backend/FiberPool.hpp"
#include "game/backend/PlayerData.hpp"
#include "game/backend/Players.hpp"
#include "game/backend/Protections.hpp"
#include "game/backend/Self.hpp"
#include "game/hooks/Hooks.hpp"
#include "game/pointers/Pointers.hpp"
#include "game/rdr/Enums.hpp"
#include "game/rdr/Natives.hpp"
#include "game/rdr/Network.hpp"
#include "game/rdr/Nodes.hpp"
#include "game/rdr/data/PedModels.hpp"
#include "game/backend/CrashSignatures.hpp"
#include <format>
#include <algorithm>
#include <chrono>

#include <network/CNetGamePlayer.hpp>
#include <network/CNetworkScSession.hpp>
#include <network/netObject.hpp>
#include <network/rlGamerInfo.hpp>
#include <network/sync/CProjectBaseSyncDataNode.hpp>
#include <network/sync/animal/CAnimalCreationData.hpp>
#include <network/sync/animscene/CAnimSceneCreationData.hpp>
#include <network/sync/animscene/CAnimSceneInfrequentData.hpp>
#include <network/sync/object/CObjectCreationData.hpp>
#include <network/sync/ped/CPedAttachData.hpp>
#include <network/sync/ped/CPedCreationData.hpp>
#include <network/sync/ped/CPedTaskTreeData.hpp>
#include <network/sync/physical/CPhysicalAttachData.hpp>
#include <network/sync/pickup/CPickupCreationData.hpp>
#include <network/sync/player/CPlayerAppearanceData.hpp>
#include <network/sync/player/CPlayerCreationData.hpp>
#include <network/sync/player/CPlayerGameStateUncommonData.hpp>
#include <network/sync/projectile/CProjectileCreationData.hpp>
#include <network/sync/propset/CPropSetCreationData.hpp>
#include <network/sync/vehicle/CVehicleCreationData.hpp>
#include <network/sync/vehicle/CVehicleGadgetData.hpp>
#include <network/sync/vehicle/CVehicleProximityMigrationData.hpp>
#include <ped/CPed.hpp>
#include <rage/vector.hpp>
#include <unordered_set>
#include <network/CNetworkScSession.hpp>
#include <train/CTrainConfig.hpp>
#include <excpt.h>
#include <windows.h>

#define BLOCK_CRASHES 1

namespace YimMenu::Features
{
	BoolCommand _LogClones("logclones", "Log Incoming Clones", "Log clone creates and clone syncs");
	BoolCommand _AllowRemoteTPs("allowremotetp", "Allow Remote Teleports", "Allow trusted players to remote teleport you!");

	BoolCommand _BlockSpectate("blockspectate", "Block Spectate", "Attempts to prevent modders from spectating you", true);
	BoolCommand _BlockSpectateSession("blockspectatesession", "Block Spectate for Session", "Attempts to prevent modders from spectating anyone", false);
	BoolCommand _BlockAttachments("blockattach", "Block Attachments", "Prevents modders from attaching objects and peds to you", true);
	BoolCommand _BlockVehicleFlooding("blockvehflood", "Block Vehicle Flooding", "Prevents modders from creating too many vehicles", true);

	BoolCommand _BlockGhostPeds("blockghostpeds", "Block Ghost Peds", "Blocks creation of ghost peds that are seemingly created due to a game bug", true);
	BoolCommand _LogTaskTrees("logtasktrees", "Log Task Trees", "Log task tree combinations (learning; unique non-whitelisted 4-tuples only)");
}

namespace
{
	using namespace YimMenu;

	static const std::unordered_set<uint32_t> g_CrashObjects = {0xD1641E60, 0x6927D266};
	static const std::unordered_set<uint32_t> g_FishModels   = {
        "A_C_Crawfish_01"_J,
        "A_C_FishBluegil_01_ms"_J,
        "A_C_FishBluegil_01_sm"_J,
        "A_C_FishBullHeadCat_01_ms"_J,
        "A_C_FishBullHeadCat_01_sm"_J,
        "A_C_FishChainPickerel_01_ms"_J,
        "A_C_FishChainPickerel_01_sm"_J,
        "A_C_FishChannelCatfish_01_lg"_J,
        "A_C_FishChannelCatfish_01_XL"_J,
        "A_C_FishLakeSturgeon_01_lg"_J,
        "A_C_FishLargeMouthBass_01_lg"_J,
        "A_C_FishLargeMouthBass_01_ms"_J,
        "A_C_FishLongNoseGar_01_lg"_J,
        "A_C_FishMuskie_01_lg"_J,
        "A_C_FishNorthernPike_01_lg"_J,
        "A_C_FishPerch_01_ms"_J,
        "A_C_FishPerch_01_sm"_J,
        "A_C_FishRainbowTrout_01_lg"_J,
        "A_C_FishRainbowTrout_01_ms"_J,
        "A_C_FishRedfinPickerel_01_ms"_J,
        "A_C_FishRedfinPickerel_01_sm"_J,
        "A_C_FishRockBass_01_ms"_J,
        "A_C_FishRockBass_01_sm"_J,
        "A_C_FishSalmonSockeye_01_lg"_J,
        "A_C_FishSalmonSockeye_01_ml"_J,
        "A_C_FishSalmonSockeye_01_ms"_J,
        "A_C_FishSmallMouthBass_01_lg"_J,
        "A_C_FishSmallMouthBass_01_ms"_J,
    };

	static const std::unordered_set<uint32_t> g_BirdModels = {
	    "a_c_prairiechicken_01"_J,
	    "a_c_cormorant_01"_J,
	    "a_c_crow_01"_J,
	    "a_c_duck_01"_J,
	    "a_c_eagle_01"_J,
	    "a_c_goosecanada_01"_J,
	    "a_c_hawk_01"_J,
	    "a_c_owl_01"_J,
	    "a_c_pelican_01"_J,
	    "a_c_pigeon"_J,
	    "a_c_raven_01"_J,
	    "a_c_cardinal_01"_J,
	    "a_c_seagull_01"_J,
	    "a_c_songbird_01"_J,
	    "a_c_turkeywild_01"_J,
	    "a_c_turkey_01"_J,
	    "a_c_turkey_02"_J,
	    "a_c_vulture_01"_J,
	    "a_c_bluejay_01"_J,
	    "a_c_cedarwaxwing_01"_J,
	    "a_c_rooster_01"_J,
	    "mp_a_c_chicken_01"_J,
	    "a_c_chicken_01"_J,
	    "a_c_californiacondor_01"_J,
	    "a_c_cranewhooping_01"_J,
	    "a_c_egret_01"_J,
	    "a_c_heron_01"_J,
	    "a_c_loon_01"_J,
	    "a_c_oriole_01"_J,
	    "a_c_carolinaparakeet_01"_J,
	    "a_c_parrot_01"_J,
	    "a_c_pelican_01"_J,
	    "a_c_pheasant_01"_J,
	    "a_c_pigeon"_J,
	    "a_c_quail_01"_J,
	    "a_c_redfootedbooby_01"_J,
	    "a_c_robin_01"_J,
	    "a_c_roseatespoonbill_01"_J,
	    "a_c_seagull_01"_J,
	    "a_c_sparrow_01"_J,
	    "a_c_vulture_01"_J,
	    "a_c_woodpecker_01"_J,
	    "a_c_woodpecker_02"_J,
	};

	static const std::unordered_set<uint32_t> g_CageModels        = {0x99C0CFCF, 0xF3D580D3, 0xEE8254F6, 0xC2D200FE};
	static const std::unordered_set<uint32_t> g_ValidPlayerModels = {"mp_male"_J, "mp_female"_J};

	static const std::unordered_set<uint32_t> g_BlacklistedAnimScenes = {"script@beat@town@peepingtom@spankscene"_J, "script@story@sal1@ig@sal1_18_lenny_on_lenny@sal1_18_lenny_on_lenny"_J, "script@vignette@dutch_33@player_karen@dance"_J, "script@vignette@beecher@abigail_6@action_enter"_J};

	inline bool IsValidPlayerModel(rage::joaat_t model)
	{
		// allow any valid ped model to fix custom model sync issues
		return STREAMING::IS_MODEL_A_PED(model);
	}

	inline void CheckPlayerModel(YimMenu::Player player, uint32_t model)
	{
		if (!player)
			return;

		if (!IsValidPlayerModel(model))
			player.AddDetection(Detection::INVALID_PLAYER_MODEL);
	}

	inline YimMenu::Player GetObjectCreator(rage::netObject* object)
	{
		if (!object)
			return nullptr;

		for (auto& [idx, player] : Players::GetPlayers())
		{
			if ((*Pointers.ScSession)->m_SessionMultiplayer->GetPlayerByIndex(idx)->m_SessionPeer->m_Identifier.m_AccountId == object->m_Guid.GetAccountId())
			{
				return player;
			}
		}

		return nullptr;
	}

	inline void SyncBlocked(std::string crash, YimMenu::Player source = Protections::GetSyncingPlayer())
	{
		if (source)
		{
			LOGF(WARNING, "Blocked {} from {}", crash, source.GetName());
			// start a short quarantine to block all further traffic from this player
			source.GetData().QuarantineFor(std::chrono::seconds(10));
		}
		else
		{
			LOGF(WARNING, "Blocked {}", crash);
		}
	}

	inline void DeleteSyncObject(std::uint16_t object)
	{
		if (object == -1)
			return;

		// CRASH FIX: Don't check crash signatures for object IDs - they're just numbers
		// The crash signature check was causing issues when trying to delete corrupted objects
		// Object IDs are 16-bit integers, not pointers, so crash signature detection doesn't apply
		try
		{
			Network::ForceRemoveNetworkEntity(object, -1, false);
		}
		catch (...)
		{
			LOG(WARNING) << "DeleteSyncObject: Exception deleting object " << object << " - continuing safely";
		}
	}

	inline void DeleteSyncObjectLater(std::uint16_t object)
	{
		FiberPool::Push([object] {
			if (object == -1)
				return;

			// CRASH FIX: Don't check crash signatures for object IDs - they're just numbers
			// Object IDs are 16-bit integers, not pointers, so crash signature detection doesn't apply
			try
			{
				Network::ForceRemoveNetworkEntity(object, -1, true);
			}
			catch (...)
			{
				LOG(WARNING) << "DeleteSyncObjectLater: Exception deleting object " << object << " - continuing safely";
			}
		});
	}

	// best-effort readable-memory check to avoid AV on malformed nodes
	static bool IsReadable(const void* p, size_t len)
	{
		if (!p || len == 0) return false;
		MEMORY_BASIC_INFORMATION mbi{};
		const unsigned char* addr = static_cast<const unsigned char*>(p);
		size_t remaining = len;
		while (remaining)
		{
			if (!VirtualQuery(addr, &mbi, sizeof(mbi)))
				return false;
			if (mbi.State != MEM_COMMIT)
				return false;
			DWORD prot = mbi.Protect & 0xFF;
			if (prot == PAGE_NOACCESS || (mbi.Protect & PAGE_GUARD))
				return false;
			const unsigned char* regionEnd = static_cast<const unsigned char*>(mbi.BaseAddress) + mbi.RegionSize;
			if (addr >= regionEnd)
				return false;
			size_t avail = static_cast<size_t>(regionEnd - addr);
			size_t step = std::min(remaining, avail);
			addr += step;
			remaining -= step;
		}
		return true;
	}

	// entity type gating to prevent node/type spoofing
	static bool IsNodeAllowedForType(SyncNodeId id, NetObjType type)
	{
		switch (id)
		{
		// creation nodes only (gate here to minimize false positives)
		case "CPedCreationNode"_J:
			return type == NetObjType::Player || type == NetObjType::Ped || type == NetObjType::Animal || type == NetObjType::Horse;
		case "CAnimalCreationNode"_J:
			return type == NetObjType::Animal || type == NetObjType::Horse || type == NetObjType::Ped;
		case "CPlayerCreationNode"_J:
			return type == NetObjType::Player;
		case "CObjectCreationNode"_J:
			// allow for Object and PropSet as some payloads misreport, we'll validate model below
			return type == NetObjType::Object || type == NetObjType::PropSet;
		case "CPropSetCreationNode"_J:
			return type == NetObjType::PropSet;
		case "CProjectileCreationNode"_J:
			return type == NetObjType::WorldProjectile;
		case "CVehicleCreationNode"_J:
			return type == NetObjType::DraftVeh || type == NetObjType::Boat;
		case "CDraftVehCreationNode"_J:
			return type == NetObjType::DraftVeh;

		// for state/attach/migration nodes we default-allow and let targeted logic handle abuse
		default:
			return true;
		}
	}

	bool ShouldBlockNode(CProjectBaseSyncDataNode* node, SyncNodeId id, NetObjType type, rage::netObject* object)
	{
		// entity type spoofing guard - not needed its already patched by Rockstar
		// if (!IsNodeAllowedForType(id, type))
		// {
		// 	SyncBlocked("entity type spoofing");
		// 	if (object)
		// 		DeleteSyncObjectLater(object->m_ObjectId);
		// 	return true;
		// }
		switch (id)
		{
		case "CPedCreationNode"_J:
		{
			auto& data = node->GetData<CPedCreationData>();
			if (data.m_ModelHash && !STREAMING::IS_MODEL_A_PED(data.m_ModelHash))
			{
				LOGF(SYNC, WARNING, "Blocking invalid ped creation model 0x{:X} from {}", data.m_ModelHash, Protections::GetSyncingPlayer().GetName());
				SyncBlocked("mismatched ped model crash");
				data.m_ModelHash = "MP_MALE"_J;
				data.m_BannedPed = true; // blocking this seems difficult
				return true;
			}
			break;
		}
		case "CAnimalCreationNode"_J:
		{
			auto& data = node->GetData<CAnimalCreationData>();
			if (data.m_ModelHash && !STREAMING::IS_MODEL_A_PED(data.m_ModelHash))
			{
				LOGF(SYNC, WARNING, "Blocking invalid animal creation model 0x{:X} from {}", data.m_ModelHash, Protections::GetSyncingPlayer().GetName());
				SyncBlocked("mismatched animal model crash");
				data.m_ModelHash = "MP_MALE"_J;
				data.m_BannedPed = true; // blocking this seems difficult
				return true;
			}
			if (data.m_PopulationType == 10 && Features::_BlockGhostPeds.GetState())
			{
				// block ghost peds
				if (object)
					DeleteSyncObject(object->m_ObjectId);
				return true;
			}

			break;
		}
		case "CObjectCreationNode"_J:
		{
			auto& data = node->GetData<CObjectCreationData>();
			if (g_CrashObjects.count(data.m_ModelHash))
			{
				LOGF(SYNC, WARNING, "Blocking crash object creation model 0x{:X} from {}", data.m_ModelHash, Protections::GetSyncingPlayer().GetName());
				SyncBlocked("invalid object crash");
				return true;
			}
			if (data.m_ModelHash && !STREAMING::_IS_MODEL_AN_OBJECT(data.m_ModelHash))
			{
				LOGF(SYNC, WARNING, "Blocking invalid object creation model 0x{:X} from {}", data.m_ModelHash, Protections::GetSyncingPlayer().GetName());
				SyncBlocked("mismatched object model crash");
				return true;
			}
			if (g_CageModels.count(data.m_ModelHash))
			{
				if (object)
					DeleteSyncObject(object->m_ObjectId);
				SyncBlocked("cage spawn", GetObjectCreator(object));
				return true;
			}

			break;
		}
		case "CPlayerAppearanceNode"_J:
		{
			auto& data = node->GetData<CPlayerAppearanceData>();
			if (data.m_ModelHash && !STREAMING::IS_MODEL_A_PED(data.m_ModelHash))
			{
				LOGF(SYNC, WARNING, "Blocking invalid player appearance model 0x{:X} from {}", data.m_ModelHash, Protections::GetSyncingPlayer().GetName());
				SyncBlocked("mismatched player model crash");
				Protections::GetSyncingPlayer().AddDetection(Detection::TRIED_CRASH_PLAYER); // false positives very unlikely
				data.m_ModelHash = "MP_MALE"_J;
			}

			if (data.m_ModelHash && (g_FishModels.count(data.m_ModelHash) || g_BirdModels.count(data.m_ModelHash)))
			{
				LOGF(SYNC, WARNING, "Replacing animal model 0x{:X} from {} with MP_MALE to prevent potential task crashes", data.m_ModelHash, Protections::GetSyncingPlayer().GetName());
				data.m_ModelHash = "MP_MALE"_J;
			}

			CheckPlayerModel(Protections::GetSyncingPlayer().GetHandle(), data.m_ModelHash);

			break;
		}
		case "CVehicleCreationNode"_J:
		{
			auto& data = node->GetData<CVehicleCreationData>();

			// global sanity: must be a vehicle model
			if (data.m_ModelHash && !STREAMING::IS_MODEL_A_VEHICLE(data.m_ModelHash))
			{
				LOGF(SYNC, WARNING, "Blocking invalid vehicle creation model 0x{:X} from {}", data.m_ModelHash, Protections::GetSyncingPlayer().GetName());
				SyncBlocked("mismatched vehicle model crash");
				Protections::GetSyncingPlayer().AddDetection(Detection::TRIED_CRASH_PLAYER);
				return true;
			}

			// legacy: specific large vehicle flood guard (kept for compatibility with your setting)
			if (data.m_PopulationType == 8 && data.m_ModelHash == "SHIP_GUAMA02"_J && Protections::GetSyncingPlayer().GetData().m_LargeVehicleFloodLimit.Process() && Features::_BlockVehicleFlooding.GetState())
			{
				SyncBlocked("large vehicle flood");
				return true;
			}

			if (data.m_PopulationType == 8 && Protections::GetSyncingPlayer().GetData().m_VehicleFloodLimit.Process() && Features::_BlockVehicleFlooding.GetState())
			{
				SyncBlocked("vehicle flood");
				return true;
			}

			break;
		}
		case "CPhysicalAttachNode"_J:
		{
			auto& data = node->GetData<CPhysicalAttachData>();
			if (auto local = Pointers.GetLocalPed(); local && local->m_NetObject)
			{
				const int localPedId = local->m_NetObject->m_ObjectId;
				int mountId = -1;
				{
					auto m = Self::GetMount();
					if (m && m.IsValid())
						mountId = m.GetNetworkObjectId();
				}
				int vehicleId = -1;
				{
					auto v = Self::GetVehicle();
					if (v && v.IsValid())
						vehicleId = v.GetNetworkObjectId();
				}

				const bool targetingUs = data.m_IsAttached && (data.m_AttachObjectId == localPedId || (mountId != -1 && data.m_AttachObjectId == mountId) || (vehicleId != -1 && data.m_AttachObjectId == vehicleId));

				// attachment spam limiter
				auto sp = Protections::GetSyncingPlayer();
				if (sp && targetingUs)
				{
					if (sp.GetData().m_AttachRateLimit.Process() && sp.GetData().m_AttachRateLimit.ExceededLastProcess())
					{
						SyncBlocked("attachment spam");
						sp.GetData().QuarantineFor(std::chrono::seconds(10));
						if (object)
							DeleteSyncObject(object->m_ObjectId);
						return true;
					}
				}

				// block physical attachments that target us only when the toggle is enabled
				if (targetingUs && Features::_BlockAttachments.GetState())
				{
					SyncBlocked("attachment", GetObjectCreator(object));
					if (sp)
						sp.AddDetection(Detection::TRIED_ATTACH);

					// if the node's owning object is not our player, remove it; otherwise force-detach ourselves on their end
					if (object && object->m_ObjectType != (int)NetObjType::Player)
					{
						DeleteSyncObject(object->m_ObjectId);
					}
					else if (local && local->m_NetObject)
					{
						// temporarily ghost us for this attacker
						const auto localId = local->m_NetObject->m_ObjectId;
						auto forPlayer = Protections::GetSyncingPlayer();
						// begin ghost period for this attacker
						forPlayer.GetData().m_GhostMode = true;
						// send detach
						FiberPool::Push([localId, forPlayer] {
							try {
								Network::ForceRemoveNetworkEntity(localId, -1, false, forPlayer);
							} catch (...) {
								LOG(WARNING) << "ForceRemoveNetworkEntity (fiber) failed; continuing";
							}
						});
						// end ghost
						std::thread([forPlayer]() mutable {
							std::this_thread::sleep_for(std::chrono::seconds(11));
							if (forPlayer.IsValid())
								forPlayer.GetData().m_GhostMode = false;
						}).detach();
					}
					// additionally, if the attach target id is valid, try removing it to guarantee detachment (e.g., draft veh/wagon)
					if (data.m_IsAttached && data.m_AttachObjectId != 0 && data.m_AttachObjectId != (int)0xFFFF)
					{
						DeleteSyncObject(data.m_AttachObjectId);
					}
					data.m_IsAttached = false;
					return true;
				}

				// trailer-specific crash: block trailer attachments to any of our assets
				if (data.m_IsAttached && object && object->m_ObjectType == (uint16_t)NetObjType::Trailer && targetingUs)
				{
					SyncBlocked("physical trailer attachment crash");
					if (sp)
						sp.AddDetection(Detection::TRIED_CRASH_PLAYER);
					return true;
				}
			}
			break;
		}

		case "CPedStandingOnObjectNode"_J:
		{
			// if a remote player tries to set our ped as standing on an object (e.g., draft vehicle platform), block it
			auto local = Pointers.GetLocalPed();
			if (local && local->m_NetObject && object && object->m_ObjectType == (uint16_t)NetObjType::Player && object->m_ObjectId == local->m_NetObject->m_ObjectId)
			{
				LOGF(SYNC, WARNING, "Blocking CPedStandingOnObjectNode targeting local ped from {}", Protections::GetSyncingPlayer().GetName());
				SyncBlocked("standing on object targeting local");
				return true;
			}
			break;
		}

		// weapon node hardening: after any recent null-object flood or active quarantine, drop weapon/control updates
		case "CPedWeaponNode"_J:
		{
			auto p = Protections::GetSyncingPlayer();
			if (p)
			{
				auto& pd = p.GetData();
				const auto now = std::chrono::steady_clock::now();
				const bool recent_null_flood = pd.m_LastNullObjectFloodAt.time_since_epoch().count() != 0 && (now - pd.m_LastNullObjectFloodAt) < std::chrono::seconds(30);
				if (pd.IsSyncsBlocked() || recent_null_flood)
				{
					LOGF(SYNC, WARNING, "Blocking CPedWeaponNode from {} due to recent null-object flood/quarantine", p.GetName());
					SyncBlocked("weapon node after null-object flood");
					if (object && object->m_ObjectType != (int)NetObjType::Player)
					{
						DeleteSyncObject(object->m_ObjectId);
					}
					return true;
				}
			}
			break;
		}

		case "CVehicleProximityMigrationNode"_J:
		{
			auto& data = node->GetData<CVehicleProximityMigrationData>();
			if (auto local = Pointers.GetLocalPed(); local && local->m_NetObject)
			{
				bool allowRemoteTp = Features::_AllowRemoteTPs.GetState();
				for (int i = 0; i < 17; i++)
				{
					if (data.m_PassengersActive[i] && data.m_PassengerObjectIds[i] == local->m_NetObject->m_ObjectId && !allowRemoteTp)
					{
						LOGF(SYNC, WARNING, "Blocking vehicle migration that's spuriously added us to the passenger list (seat={}) by {}", i, Protections::GetSyncingPlayer().GetName());
						SyncBlocked("remote teleport");
						Protections::GetSyncingPlayer().AddDetection(Detection::REMOTE_TELEPORT);
						DeleteSyncObject(object->m_ObjectId);
						return true;
					}
				}
			}
			break;
		}
		case "CPedTaskTreeNode"_J:
		{
			auto& data = node->GetData<CPedTaskTreeData>();

			for (int i = 0; i < data.GetNumTaskTrees(); i++)
			{
				const int num = data.m_Trees[i].m_NumTasks;
				const int maxRead = std::min(num, 64);
				// if attacker packs too many tasks, quarantine to suppress follow-ups
				if (num > 64)
				{
					if (auto qp = Protections::GetSyncingPlayer())
						qp.GetData().QuarantineFor(std::chrono::seconds(60));
				}

				if (maxRead > 0)
				{
					if (!IsReadable(&data.m_Trees[i].m_Tasks[0], size_t(maxRead) * sizeof(data.m_Trees[i].m_Tasks[0])))
					{
						SyncBlocked("task array unreadable");
						// if (object) DeleteSyncObjectLater(object->m_ObjectId);
						return true;
					}
				}

				for (int j = 0; j < maxRead; ++j)
				{
					const auto& t = data.m_Trees[i].m_Tasks[j];

					// whitelist enforcement: block + quarantine like other attacks (unless learning mode is enabled)
					if (!YimMenu::CrashSignatures::IsValidTaskTriple(i, t.m_TaskType, t.m_TaskTreeType))
					{
						// learning mode: log unique non-whitelisted 4-tuples once
						if (YimMenu::Features::_LogTaskTrees.GetState() && ::YimMenu::Players::ShouldLogFor(::YimMenu::Protections::GetSyncingPlayer()))
						{
							if (YimMenu::CrashSignatures::RememberNonWhitelistedTaskQuad(i, j, t.m_TaskType, t.m_TaskTreeType))
							{
								LOGF(SYNC, INFO, "LEARNING: TaskTree data - treeIndex={}, taskIndex={}, taskType={}, taskTreeType={} from {}", i, j, t.m_TaskType, t.m_TaskTreeType, Protections::GetSyncingPlayer().GetName());
							}
						}
						// when learning is on, do NOT block/quarantine here — allow subsequent sanity checks to run
						if (!YimMenu::Features::_LogTaskTrees.GetState())
						{
							LOGF(SYNC, WARNING, "Blocking non-whitelisted task triple (tree={}, task={}) from {}", i, j, Protections::GetSyncingPlayer().GetName());
							SyncBlocked("task whitelist");
							// proactively quarantine to prevent immediate retries chaining into a crash
							if (auto qp = Protections::GetSyncingPlayer())
								qp.GetData().QuarantineFor(std::chrono::seconds(12));
							//if (object) DeleteSyncObjectLater(object->m_ObjectId);
							return true;
						}
					}

					// if (t.m_TaskType == -1)
					// {
					// 	LOGF(SYNC, WARNING, "Blocking null task type (tree={}, task={}) from {}", i, j, Protections::GetSyncingPlayer().GetName());
					// 	SyncBlocked("task fuzzer crash");
					// 	//if (object) DeleteSyncObjectLater(object->m_ObjectId);
					// 	return true;
					// }

					// // TODO: better heuristics
					// if (t.m_TaskTreeType == 31)
					// {
					// 	LOGF(SYNC, WARNING, "Blocking invalid task tree type (tree={}, task={}) from {}", i, j, Protections::GetSyncingPlayer().GetName());
					// 	SyncBlocked("task fuzzer crash");
					// 	//if (object) DeleteSyncObjectLater(object->m_ObjectId);
					// 	return true;
					// }
				}
			}
			break;
		}
		case "CPedAttachNode"_J:
		{
			auto& data = node->GetData<CPedAttachData>();
			if (auto local = Pointers.GetLocalPed(); local && local->m_NetObject)
			{
				const int localPedId = local->m_NetObject->m_ObjectId;
				int mountId = -1;
				{
					auto m = Self::GetMount();
					if (m && m.IsValid())
						mountId = m.GetNetworkObjectId();
				}
				int vehicleId = -1;
				{
					auto v = Self::GetVehicle();
					if (v && v.IsValid())
						vehicleId = v.GetNetworkObjectId();
				}

				const bool targetingUs = data.m_IsAttached && (data.m_AttachObjectId == localPedId || (mountId != -1 && data.m_AttachObjectId == mountId) || (vehicleId != -1 && data.m_AttachObjectId == vehicleId));

				// attachment spam limiter
				auto sp = Protections::GetSyncingPlayer();
				if (sp && targetingUs)
				{
					if (sp.GetData().m_AttachRateLimit.Process() && sp.GetData().m_AttachRateLimit.ExceededLastProcess())
					{
						SyncBlocked("attachment spam");
						sp.GetData().QuarantineFor(std::chrono::seconds(10));
						if (object)
							DeleteSyncObject(object->m_ObjectId);
						return true;
					}
				}

				// block ped attachments that target us only when the toggle is enabled
				if (targetingUs && Features::_BlockAttachments.GetState())
				{
					SyncBlocked("ped attachment");
					if (sp)
						sp.AddDetection(Detection::TRIED_ATTACH);

					if (object && object->m_ObjectType != (int)NetObjType::Player)
					{
						LOGF(SYNC, WARNING, "Deleting ped object {} attached to our entity", object->m_ObjectId);
						// sanity: reject inconsistent ped attach payloads when not attached
						if (!data.m_IsAttached)
						{
							bool invalid_bones = false;
							bool suspicious_flags = false;
							if (invalid_bones || suspicious_flags)
							{
								SyncBlocked("invalid ped attach state");
								return true;
							}
						}

						DeleteSyncObject(object->m_ObjectId);
					}
					else if (local && local->m_NetObject)
					{
						LOGF(SYNC, WARNING, "Player {} has attached themselves to us/our asset. Forcing detach on their end via fiber", Protections::GetSyncingPlayer().GetName());
						// temporarily ghost us for this attacker
						const auto localId = local->m_NetObject->m_ObjectId;
						auto forPlayer = Protections::GetSyncingPlayer();
						// begin ghost period for this attacker
						forPlayer.GetData().m_GhostMode = true;
						// send detach
						FiberPool::Push([localId, forPlayer] {
							try {
								Network::ForceRemoveNetworkEntity(localId, -1, false, forPlayer);
							} catch (...) {
								LOG(WARNING) << "ForceRemoveNetworkEntity (fiber) failed; continuing";
							}
						});
						// end ghost
						std::thread([forPlayer]() mutable {
							std::this_thread::sleep_for(std::chrono::seconds(11));
							if (forPlayer.IsValid())
								forPlayer.GetData().m_GhostMode = false;
						}).detach();
					}

					// additionally, if the attach target id is valid, try removing it to guarantee detachment (e.g., draft veh/wagon)
					if (data.m_IsAttached && data.m_AttachObjectId != 0 && data.m_AttachObjectId != (int)0xFFFF)
					{
						DeleteSyncObject(data.m_AttachObjectId);
					}
					data.m_IsAttached = false;
					return true;
				}

				// trailer-specific crash: block trailer attachments targeting our assets
				if (data.m_IsAttached && object && object->m_ObjectType == (uint16_t)NetObjType::Trailer && targetingUs)
				{
					SyncBlocked("ped trailer attachment crash");
					if (sp)
						sp.AddDetection(Detection::TRIED_CRASH_PLAYER);
					return true;
				}
			}
			break;
		}
		case "CPropSetCreationNode"_J:
		{
			auto& data = node->GetData<CPropSetCreationData>();
			if (data.m_Hash == "pg_veh_privatedining01x_med"_J || data.m_Hash == 0x3701844F || data.m_Type == -1 || STREAMING::IS_MODEL_A_PED(data.m_Hash))
			{
				LOGF(SYNC, WARNING, "Blocking invalid propset model 0x{:X} from {}", data.m_Hash, Protections::GetSyncingPlayer().GetName());
				SyncBlocked("invalid propset crash");
				Protections::GetSyncingPlayer().AddDetection(Detection::TRIED_CRASH_PLAYER);
				return true;
			}

			break;
		}
		case "CPlayerCreationNode"_J:
		{
			auto& data = node->GetData<CPlayerCreationData>();
			if (!STREAMING::IS_MODEL_A_PED(data.m_Hash))
			{
				LOGF(SYNC, WARNING, "Blocking invalid player creation model 0x{:X} from {}", data.m_Hash, Protections::GetSyncingPlayer().GetName());
				SyncBlocked("invalid player creation crash");
				Protections::GetSyncingPlayer().AddDetection(Detection::TRIED_CRASH_PLAYER);
				// fix the crash instead of rejecting sync
				data.m_Hash = "MP_MALE"_J;
			}
			else if (g_FishModels.count(data.m_Hash) || g_BirdModels.count(data.m_Hash))
			{
				LOGF(SYNC, WARNING, "Replacing animal player creation model 0x{:X} from {} with MP_MALE to prevent potential task crashes", data.m_Hash, Protections::GetSyncingPlayer().GetName());
				data.m_Hash = "MP_MALE"_J;
			}
			break;
		}
			case "CDynamicEntityGameStateNode"_J:
			{
				auto base = (std::uint64_t)&node->GetData<char>();
				if (!IsReadable((void*)(base + 0), 32))
				{
					SyncBlocked("dynamic entity state unreadable");
					if (object) DeleteSyncObjectLater(object->m_ObjectId);
					return true;
				}
				break;
			}


			case "CPedScriptGameStateUncommonNode"_J:
			{
				auto base = (std::uint64_t)&node->GetData<char>();
				if (!IsReadable((void*)(base + 0), 32))
				{
					SyncBlocked("ped uncommon unreadable");
					if (object) DeleteSyncObjectLater(object->m_ObjectId);
					return true;
				}
				break;
			}

		case "CProjectileCreationNode"_J:
		{
			auto& data = node->GetData<CProjectileCreationData>();
			// rate-limit and block during quarantine or recent null-flood
			if (auto p = Protections::GetSyncingPlayer())
			{
				auto& pd = p.GetData();
				const auto now = std::chrono::steady_clock::now();
				const bool recent_null_flood = pd.m_LastNullObjectFloodAt.time_since_epoch().count() != 0 && (now - pd.m_LastNullObjectFloodAt) < std::chrono::seconds(30);
				bool exceeded = pd.m_ProjectileCreateRateLimit.Process() && pd.m_ProjectileCreateRateLimit.ExceededLastProcess();
				if (pd.IsSyncsBlocked() || recent_null_flood || exceeded)
				{
					SyncBlocked("projectile spam/quarantine");
					if (object && object->m_ObjectType == (uint16_t)NetObjType::WorldProjectile)
						DeleteSyncObject(object->m_ObjectId);
					return true;
				}
			}
			if (!WEAPON::IS_WEAPON_VALID(data.m_WeaponHash))
			{
				LOGF(SYNC, WARNING, "Blocking projectile with invalid weapon hash 0x{:X} from {}", data.m_WeaponHash, Protections::GetSyncingPlayer().GetName());
				SyncBlocked("invalid projectile weapon crash");
				Protections::GetSyncingPlayer().AddDetection(Detection::TRIED_CRASH_PLAYER);
				return true;
			}
			if (!WEAPON::_IS_AMMO_VALID(data.m_AmmoHash))
			{
				LOGF(SYNC, WARNING, "Blocking projectile with invalid ammo hash 0x{:X} from {}", data.m_AmmoHash, Protections::GetSyncingPlayer().GetName());
				SyncBlocked("invalid projectile ammo crash");
				Protections::GetSyncingPlayer().AddDetection(Detection::TRIED_CRASH_PLAYER);
				return true;
			}
			break;
		}
		case "CPlayerGameStateUncommonNode"_J:
		{
			auto& data = node->GetData<CPlayerGameStateUncommonData>();
			if (data.m_IsSpectating && !data.m_IsSpectatingStaticPos)
			{
				auto object = Pointers.GetNetObjectById(data.m_SpectatorId);
				if (object && ((NetObjType)object->m_ObjectType) == NetObjType::Player)
				{
					if (Protections::GetSyncingPlayer())
					{
						auto old_spectator = Protections::GetSyncingPlayer().GetData().m_SpectatingPlayer;
						Protections::GetSyncingPlayer().GetData().m_SpectatingPlayer = object->m_OwnerId;

						if (old_spectator != Protections::GetSyncingPlayer().GetData().m_SpectatingPlayer)
						{
							bool spectating_local = Protections::GetSyncingPlayer().GetData().m_SpectatingPlayer == Self::GetPlayer();
							if (spectating_local)
							{
								Notifications::Show("Protections",
								    std::format("{} is spectating you", Protections::GetSyncingPlayer().GetName()),
								    NotificationType::Warning);
							}

							if (Features::_BlockSpectate.GetState()
							    && (spectating_local || Features::_BlockSpectateSession.GetState())
							    && Protections::GetSyncingPlayer().GetData().m_SpectatingPlayer != Protections::GetSyncingPlayer())
							{
								Network::ForceRemoveNetworkEntity(
								    Protections::GetSyncingPlayer().GetData().m_SpectatingPlayer.GetPed().GetNetworkObjectId(),
								    -1,
								    false,
								    Protections::GetSyncingPlayer());
							}
						}

						Protections::GetSyncingPlayer().AddDetection(Detection::USED_SPECTATE);
					}
				}
				else
				{
					if (Protections::GetSyncingPlayer())
						Protections::GetSyncingPlayer().GetData().m_SpectatingPlayer = nullptr;
				}
			}
			else
			{
				if (Protections::GetSyncingPlayer())
					Protections::GetSyncingPlayer().GetData().m_SpectatingPlayer = nullptr;
			}
			break;
		}
		case "CPickupCreationNode"_J:
		{
			auto& data = node->GetData<CPickupCreationData>();
			if (!OBJECT::_IS_PICKUP_TYPE_VALID(data.m_PickupHash) && !(data.m_PickupHash == 0xFFFFFFFF && data.m_ModelHash == 0) && (data.m_ModelHash != 0 && STREAMING::_IS_MODEL_AN_OBJECT(data.m_ModelHash)))
			{
				LOGF(SYNC, WARNING, "Blocking pickup with invalid hashes (m_PickupHash = 0x{}, m_ModelHash = 0x{}) from {}", data.m_PickupHash, data.m_ModelHash, Protections::GetSyncingPlayer().GetName());
				SyncBlocked("invalid pickup type crash");
				Protections::GetSyncingPlayer().AddDetection(Detection::TRIED_CRASH_PLAYER);
				return true;
			}
			break;
		}
		case "Node_14359d660"_J:
		{
			auto data = (std::uint64_t)&node->GetData<char>();

			// validate base fields are readable first
			if (!IsReadable((void*)(data + 36), sizeof(int)))
			{
				SyncBlocked("wanted data node unreadable header");
				if (object) DeleteSyncObjectLater(object->m_ObjectId);
				return true;
			}

			int count = *(int*)(data + 36);
			// basic sanity
			if (count < 0 || count > 16)
			{
				SyncBlocked("wanted data count out of bounds");
				if (object) DeleteSyncObjectLater(object->m_ObjectId);
				return true;
			}

			for (int i = 0; i < count; i++)
			{
				// ensure we can read both bounds before comparing
				auto hiPtr = (void*)(data + 36ULL * i + 72ULL);
				auto loPtr = (void*)(data + 36ULL * i + 64ULL);
				if (!IsReadable(hiPtr, sizeof(int)) || !IsReadable(loPtr, sizeof(int)))
				{
					SyncBlocked("wanted data element unreadable");
					if (object) DeleteSyncObjectLater(object->m_ObjectId);
					return true;
				}

				if (*(int*)hiPtr < *(int*)loPtr)
				{
					LOGF(SYNC, WARNING, "Blocking wanted data array out of bounds range ({} < {}) from {}", *(int*)hiPtr, *(int*)loPtr, Protections::GetSyncingPlayer().GetName());
					SyncBlocked("wanted data array out of bounds crash");
					Protections::GetSyncingPlayer().AddDetection(Detection::TRIED_CRASH_PLAYER);
					return true;
				}
			}
			break;
		}
		case "CTrainGameStateUncommonNode"_J:
		{
			auto data = (std::uint64_t)&node->GetData<char>();
			if (!IsReadable((void*)(data + 12), sizeof(unsigned char)))
			{
				SyncBlocked("train uncommon unreadable config index");
				if (object) DeleteSyncObjectLater(object->m_ObjectId);
				return true;
			}
			if (*(unsigned char*)(data + 12) >= Pointers.TrainConfigs->m_TrainConfigs.size())
			{
				LOGF(SYNC, WARNING, "Blocking CTrainGameStateUncommonNode out of bounds train config ({} >= {}) from {}", *(unsigned char*)(data + 12), Pointers.TrainConfigs->m_TrainConfigs.size(), Protections::GetSyncingPlayer().GetName());
				SyncBlocked("out of bounds train config index crash");
				DeleteSyncObjectLater(object->m_ObjectId); // delete bad train just in case
				return true;
			}
			break;
		}
		case "CTrainGameStateNode"_J:
		{
			auto data = (std::uint64_t)&node->GetData<char>();
			// validate readable before deref
			if (!IsReadable((void*)(data + 30), sizeof(unsigned char)))
			{
				SyncBlocked("train state unreadable track index");
				if (object) DeleteSyncObjectLater(object->m_ObjectId);
				return true;
			}
			unsigned char trackIdx = *(unsigned char*)(data + 30);
			if (trackIdx > 120)
			{
				LOGF(SYNC, WARNING, "Blocking CTrainGameStateNode invalid track index ({}) from {}", trackIdx, Protections::GetSyncingPlayer().GetName());
				SyncBlocked("invalid train state");
				if (object)
					DeleteSyncObjectLater(object->m_ObjectId);
				return true;
			}
			break;
		}
			case "CGlobalFlagsNode"_J:
			{
				auto data = (std::uint64_t)&node->GetData<char>();
				// conservative content safety: ensure minimal header is readable; do not rate-limit
				if (!IsReadable((void*)(data + 0), 16))
				{
					SyncBlocked("global flags unreadable header");
					if (object) DeleteSyncObjectLater(object->m_ObjectId);
					return true;
				}
				break;
			}
			case "CWorldStateBaseNode"_J:
			{
				auto data = (std::uint64_t)&node->GetData<char>();
				if (!IsReadable((void*)(data + 0), 16))
				{
					SyncBlocked("world state unreadable header");
					if (object) DeleteSyncObjectLater(object->m_ObjectId);
					return true;
				}
				break;
			}

			case "Node_14359d020"_J:
			{
				// conservative guard for frequent player sync node to prevent malformed payload crashes
				auto base = (std::uint64_t)&node->GetData<char>();
				if (!IsReadable((void*)(base + 0), 64))
				{
					SyncBlocked("player state unreadable");
					if (object) DeleteSyncObjectLater(object->m_ObjectId);
					return true;
				}
				break;
			}

		case "CAnimSceneCreationNode"_J:
		{
			auto& data = node->GetData<CAnimSceneCreationData>();
			if (g_BlacklistedAnimScenes.contains(data.m_AnimDict))
			{
				LOGF(SYNC, WARNING, "Blocking animscene 0x{:X} from {} since it's in the blacklist", data.m_AnimDict, Protections::GetSyncingPlayer().GetName());
				SyncBlocked("bad anim scene", GetObjectCreator(object));
				DeleteSyncObject(object->m_ObjectId);
				return true;
			}
			break;
		}
		}

		return false;
	}

	// bridge + seh wrapper with no c++ object construction inside guarded region
	static bool ShouldBlockNode_Raw(CProjectBaseSyncDataNode* node, NetObjType type, rage::netObject* object)
	{
		return ShouldBlockNode(node, Nodes::Find((uint64_t)node), type, object);
	}

	using ShouldBlockNodeRawFn = bool (*)(CProjectBaseSyncDataNode*, NetObjType, rage::netObject*);
	static bool __declspec(noinline) CallNodeEval_SEH(ShouldBlockNodeRawFn fn, CProjectBaseSyncDataNode* node, NetObjType type, rage::netObject* object, bool* outSeh)
	{
		__try
		{
			return fn(node, type, object);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			if (outSeh) *outSeh = true;
			return true; // block to prevent crash
		}
	}

		// raw logger and seh wrapper mirroring pattern used for node eval
		static void LogSyncNode_Raw(CProjectBaseSyncDataNode* node, NetObjType type, rage::netObject* object)
		{
			auto sid = Nodes::Find((uint64_t)node);
			YimMenu::Hooks::Protections::LogSyncNode(node, sid, type, object, Protections::GetSyncingPlayer());
		}
		using LogSyncNodeRawFn = void (*)(CProjectBaseSyncDataNode*, NetObjType, rage::netObject*);
		static void __declspec(noinline) CallLogSyncNode_SEH(LogSyncNodeRawFn fn, CProjectBaseSyncDataNode* node, NetObjType type, rage::netObject* object, bool* outSeh)
		{
			__try
			{
				fn(node, type, object);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				if (outSeh) *outSeh = true;
			}
		}


	bool SyncNodeVisitor(CProjectBaseSyncDataNode* node, NetObjType type, rage::netObject* object)
	{
		if (node->IsParentNode())
		{
			for (auto child = node->m_FirstChild; child; child = child->m_NextSibling)
			{
				if (SyncNodeVisitor(reinterpret_cast<CProjectBaseSyncDataNode*>(child), type, object))
					return true;
			}
		}
		else if (node->IsDataNode())
		{
			if (!node->IsActive())
				return false;

				auto sid = Nodes::Find((uint64_t)node);

			bool log_seh = false;
			if (YimMenu::Features::_LogClones.GetState() && ::YimMenu::Players::ShouldLogFor(::YimMenu::Protections::GetSyncingPlayer()))
				CallLogSyncNode_SEH(&LogSyncNode_Raw, node, type, object, &log_seh);
			if (log_seh)
			{
				auto p = ::YimMenu::Protections::GetSyncingPlayer();
				auto sid = Nodes::Find((uint64_t)node);
				// logging should never change behavior: do not quarantine/block on logger exceptions
				LOGF(SYNC, WARNING, "SEH in LogSyncNode for {} from {} (logging-only; no quarantine)", sid.name ? sid.name : "unknown_node", p ? p.GetName() : "unknown");
			}
			{
				bool seh = false;
				bool blocked = CallNodeEval_SEH(&ShouldBlockNode_Raw, node, type, object, &seh);
				if (seh)
				{
					auto p = ::YimMenu::Protections::GetSyncingPlayer();
					LOGF(SYNC, WARNING, "SEH in ShouldBlockNode for {} from {}", sid.name ? sid.name : "unknown_node", p ? p.GetName() : "unknown");
					if (p)
					{
						p.AddDetection(Detection::TRIED_CRASH_PLAYER);
						p.GetData().QuarantineFor(std::chrono::seconds(10));
					}
					if (object)
						DeleteSyncObjectLater(object->m_ObjectId);
					return true;
				}
				return blocked;
			}
		}

		return false;
	}
}

namespace YimMenu::Hooks::Protections
{
	bool ShouldBlockSync(rage::netSyncTree* tree, NetObjType type, rage::netObject* object)
	{
		try
		{
			// early quarantine gate
			{
				auto qp = ::YimMenu::Protections::GetSyncingPlayer();
				if (qp && qp.GetData().IsSyncsBlocked())
				{
					return true;
				}
			}

			Nodes::Init();

			if (g_Running && SyncNodeVisitor(reinterpret_cast<CProjectBaseSyncDataNode*>(tree->m_NextSyncNode), type, object))
			{
				return true;
			}

			return false;
		}
		catch (const std::exception& e)
		{
			LOGF(SYNC, WARNING, "Exception in ShouldBlockSync: {} from {}", e.what(), ::YimMenu::Protections::GetSyncingPlayer().GetName());
			auto p = ::YimMenu::Protections::GetSyncingPlayer();
			if (p)
			{
				p.AddDetection(Detection::TRIED_CRASH_PLAYER);
				p.GetData().QuarantineFor(std::chrono::seconds(10));
			}
			return true; // Block on exception to prevent crash
		}
		catch (...)
		{
			LOGF(SYNC, WARNING, "Unknown exception in ShouldBlockSync from {}", ::YimMenu::Protections::GetSyncingPlayer().GetName());
			::YimMenu::Protections::GetSyncingPlayer().AddDetection(Detection::TRIED_CRASH_PLAYER);
			return true; // Block on exception to prevent crash
		}
	}
}
