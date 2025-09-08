#include "core/commands/BoolCommand.hpp"
#include "core/frontend/Notifications.hpp"
#include "core/misc/RateLimiter.hpp"
#include "game/backend/FiberPool.hpp"
#include "game/backend/PlayerData.hpp"
#include "game/backend/Protections.hpp"
#include "game/backend/Self.hpp"
#include "game/hooks/Hooks.hpp"
#include "game/pointers/Pointers.hpp"
#include "game/rdr/Enums.hpp"
#include "game/backend/NodeHooks.hpp"

#include "game/rdr/Natives.hpp"
#include "game/rdr/Network.hpp"
#include "game/rdr/Nodes.hpp"
#include "game/rdr/data/PedModels.hpp"

#include <network/CNetGamePlayer.hpp>
#include <network/netObject.hpp>
#include <network/rlGamerInfo.hpp>
#include <network/sync/CProjectBaseSyncDataNode.hpp>
#include <network/sync/animal/CAnimalCreationData.hpp>
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
#include <Windows.h>

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
		if (!(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)))
			return false;
		size_t chunk = std::min(remaining, static_cast<size_t>(reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize - reinterpret_cast<uintptr_t>(addr)));
		addr += chunk;
		remaining -= chunk;
	}
	return true;
}


#define LOG_FIELD_H(type, field) LOG(INFO) << "\t" << #field << ": " << HEX((node->GetData<type>().field));
#define LOG_FIELD(type, field) LOG(INFO) << "\t" << #field << ": " << ((node->GetData<type>().field));
#define LOG_FIELD_C(type, field) LOG(INFO) << "\t" << #field << ": " << (int)((node->GetData<type>().field));
#define LOG_FIELD_B(type, field) LOG(INFO) << "\t" << #field << ": " << ((node->GetData<type>().field) ? "YES" : "NO");
#define LOG_FIELD_V3(type, field)                                               \
	LOG(INFO) << "\t" << #field << ": X: " << ((node->GetData<type>().field)).x \
	          << " Y: " << ((node->GetData<type>().field)).y << " Z: " << ((node->GetData<type>().field)).z;
#define LOG_FIELD_V4(type, field)                                                                           \
	LOG(INFO) << "\t" << #field << ": X: " << ((node->GetData<type>().field)).x                             \
	          << " Y: " << ((node->GetData<type>().field)).y << " Z: " << ((node->GetData<type>().field)).z \
	          << " W: " << ((node->GetData<type>().field)).w;
#define LOG_FIELD_APPLY(type, field, func) LOG(INFO) << "\t" << #field << ": " << func((node->GetData<type>().field));
#define LOG_FIELD_UNDOCUM(num, type)                                                                                   \
	do {                                                                                                                  \
		auto basePtr = (&node->GetData<char>());                                                                           \
		auto ptr = basePtr + (num);                                                                                        \
		if (IsReadable(ptr, sizeof(type)))                                                                                \
			LOG(INFO) << "\tFIELD_" << #num << ": " << *(type*)ptr;                                                       \
		else                                                                                                              \
			LOG(INFO) << "\tFIELD_" << #num << ": <unreadable>";                                                         \
	} while (0)
#define LOG_FIELD_UNDOCUM_C(num, type)                                                                                \
	do {                                                                                                                  \
		auto basePtr = (&node->GetData<char>());                                                                           \
		auto ptr = basePtr + (num);                                                                                        \
		if (IsReadable(ptr, sizeof(type)))                                                                                \
			LOG(INFO) << "\tFIELD_" << #num << ": " << (int)*(type*)ptr;                                                  \
		else                                                                                                              \
			LOG(INFO) << "\tFIELD_" << #num << ": <unreadable>";                                                         \
	} while (0)

namespace YimMenu::Hooks
{
	void Protections::LogSyncNode(CProjectBaseSyncDataNode* node, SyncNodeId& id, NetObjType type, rage::netObject* object, Player& player)
	{

		// targeted crash protection based on .map analysis - prevent null pointer dereference
		if (!node)
		{
			LOG(WARNING) << "LogSyncNode: Blocked null node pointer";
			return;
		}

		if (!object)
		{
			// note: null object can be legitimate during creation queue; just log and attribute to player if known
			if (player)
			{
				LOGF(SYNC, WARNING, "LogSyncNode: Blocked null object pointer (from {})", player.GetName());
				// detect flood of null-object nodes from a single player and quarantine briefly
				if (player.GetData().m_NullObjectLogRateLimit.Process() && player.GetData().m_NullObjectLogRateLimit.ExceededLastProcess())
				{
					player.GetData().QuarantineFor(std::chrono::seconds(10));
					LOGF(SYNC, WARNING, "Quarantined {} for null-object node flood", player.GetName());
				}
			}
			else
			{
				LOG(WARNING) << "LogSyncNode: Blocked null object pointer (from unknown)";
			}
			return;
		}
			// validate object memory readability to avoid AV on crafted pointers
			if (!IsReadable(object, sizeof(*object)) || !IsReadable(&object->m_ObjectId, sizeof(object->m_ObjectId)))
			{
				LOG(WARNING) << "LogSyncNode: Unreadable object memory; skipping";
				return;
			}


		// validate player before accessing
		if (!player.IsValid())
		{
			LOG(INFO) << "UNKNOWN: " << id.name << " " << object->m_ObjectId;
			// continue processing but don't access player data
		}
		else
		{
			LOG(INFO) << player.GetName() << ": " << id.name << ", " << object->m_ObjectId;
		}

		int object_id = object->m_ObjectId;

		switch (id)
		{
		case "CPedCreationNode"_J:
			LOG_FIELD(CPedCreationData, m_PopulationType);
			LOG_FIELD_H(CPedCreationData, m_ModelHash);
			LOG_FIELD_B(CPedCreationData, m_BannedPed);
			break;
		case "CAnimalCreationNode"_J:
			LOG_FIELD(CAnimalCreationData, m_PopulationType);
			LOG_FIELD_H(CAnimalCreationData, m_ModelHash);
			LOG_FIELD_B(CAnimalCreationData, m_BannedPed);
			break;
		case "CObjectCreationNode"_J:
			LOG_FIELD(CObjectCreationData, m_ObjectType);
			LOG_FIELD_H(CObjectCreationData, m_ModelHash);
			break;
		case "CPlayerAppearanceNode"_J:
			LOG_FIELD_H(CPlayerAppearanceData, m_ModelHash);
			LOG_FIELD_B(CPlayerAppearanceData, m_BannedPlayerModel);
			break;
		case "CVehicleCreationNode"_J:
			LOG_FIELD(CVehicleCreationData, m_PopulationType);
			LOG_FIELD_H(CVehicleCreationData, m_ModelHash);
			break;
		case "CPickupCreationNode"_J:
			LOG_FIELD_H(CPickupCreationData, m_PickupHash);
			LOG_FIELD_H(CPickupCreationData, m_ModelHash);
			break;
		case "CPhysicalAttachNode"_J:
			LOG_FIELD_B(CPhysicalAttachData, m_IsAttached);
			LOG_FIELD(CPhysicalAttachData, m_AttachObjectId);
			LOG_FIELD_V3(CPhysicalAttachData, m_Offset);
			LOG_FIELD_V4(CPhysicalAttachData, m_Orientation);
			LOG_FIELD_V3(CPhysicalAttachData, m_ParentOffset);
			LOG_FIELD(CPhysicalAttachData, m_OtherAttachBone);
			LOG_FIELD(CPhysicalAttachData, m_AttachBone);
			LOG_FIELD(CPhysicalAttachData, m_AttachFlags);
			break;
		case "CVehicleProximityMigrationNode"_J:
			LOG_FIELD(CVehicleProximityMigrationData, m_NumPassengers);
			LOG_FIELD_B(CVehicleProximityMigrationData, m_OverridePopulationType);
			LOG_FIELD(CVehicleProximityMigrationData, m_PopulationType);
			LOG_FIELD(CVehicleProximityMigrationData, m_Flags);
			LOG_FIELD(CVehicleProximityMigrationData, m_Timestamp);
			LOG_FIELD_B(CVehicleProximityMigrationData, m_HasPositionData);
			LOG_FIELD_V3(CVehicleProximityMigrationData, m_Position);
			LOG_FIELD(CVehicleProximityMigrationData, m_UnkAmount);
			break;
		case "CPedTaskTreeNode"_J:
		{
			auto& d = node->GetData<CPedTaskTreeData>();
			// first tree type
			LOG_FIELD(CPedTaskTreeData, m_Trees[0].m_TreeType);
			int trees = std::min(d.GetNumTaskTrees(), 8);
			for (int i = 0; i < trees; ++i)
			{
				LOG_FIELD(CPedTaskTreeData, m_Trees[i].m_NumTasks);
				LOG_FIELD_B(CPedTaskTreeData, m_Trees[i].m_SequenceTree);
				int num = d.m_Trees[i].m_NumTasks;
				int maxRead = std::min(num, 16);
				if (maxRead > 0)
				{
					if (!IsReadable(&d.m_Trees[i].m_Tasks[0], size_t(maxRead) * sizeof(d.m_Trees[i].m_Tasks[0])))
					{
						LOG(WARNING) << "LogSyncNode: task array unreadable; skipping tasks";
						maxRead = 0;
					}
				}
				for (int j = 0; j < maxRead; ++j)
				{
					LOG_FIELD(CPedTaskTreeData, m_Trees[i].m_Tasks[j].m_TaskType);
					LOG_FIELD(CPedTaskTreeData, m_Trees[i].m_Tasks[j].m_TaskUnk1);
					LOG_FIELD(CPedTaskTreeData, m_Trees[i].m_Tasks[j].m_TaskTreeType);
					LOG_FIELD(CPedTaskTreeData, m_Trees[i].m_Tasks[j].m_TaskSequenceId);
					LOG_FIELD(CPedTaskTreeData, m_Trees[i].m_Tasks[j].m_TaskTreeDepth);
				}
			}
			if (IsReadable(&d.m_ScriptCommand, sizeof(d.m_ScriptCommand)))
			{
				LOG_FIELD_H(CPedTaskTreeData, m_ScriptCommand);
			}
			else
			{
				LOG(INFO) << "\tm_ScriptCommand: <unreadable>";
			}
			if (IsReadable(&d.m_ScriptTaskStage, sizeof(d.m_ScriptTaskStage)))
			{
				LOG_FIELD(CPedTaskTreeData, m_ScriptTaskStage);
			}
			else
			{
				LOG(INFO) << "\tm_ScriptTaskStage: <unreadable>";
			}
		}
			break;
		case "CPedAttachNode"_J:
			LOG_FIELD_B(CPedAttachData, m_IsAttached);
			LOG_FIELD(CPedAttachData, m_AttachObjectId);
			break;
		case "CVehicleGadgetNode"_J:
			LOG_FIELD_B(CVehicleGadgetData, m_HasPosition);
			LOG_FIELD(CVehicleGadgetData, m_Position[0]);
			LOG_FIELD(CVehicleGadgetData, m_Position[1]);
			LOG_FIELD(CVehicleGadgetData, m_Position[2]);
			LOG_FIELD(CVehicleGadgetData, m_Position[3]);
			LOG_FIELD(CVehicleGadgetData, m_NumGadgets);
			if (node->GetData<CVehicleGadgetData>().m_NumGadgets <= 2)
			{
				for (int i = 0; i < node->GetData<CVehicleGadgetData>().m_NumGadgets; i++)
				{
					LOG_FIELD(CVehicleGadgetData, m_Gadgets[i].m_Type);
				}
			}
			break;
		case "CPropSetCreationNode"_J:
			LOG_FIELD_H(CPropSetCreationData, m_Hash);
			LOG_FIELD_H(CPropSetCreationData, m_Type);
			break;
		case "CProjectileCreationNode"_J:
			LOG_FIELD_UNDOCUM(4, int);
			LOG_FIELD_UNDOCUM(8, int);
			LOG_FIELD_UNDOCUM(0x2C, int);
			break;
		case "CTrainGameStateUncommonNode"_J:
			LOG_FIELD_UNDOCUM(0, int);
			LOG_FIELD_UNDOCUM(4, int);
			LOG_FIELD_UNDOCUM(8, int);
			LOG_FIELD_UNDOCUM_C(12, char); // config index
			LOG_FIELD_UNDOCUM_C(13, char);
			LOG_FIELD_UNDOCUM_C(14, char);
			LOG_FIELD_UNDOCUM_C(15, char);
			LOG_FIELD_UNDOCUM_C(16, char);
			LOG_FIELD_UNDOCUM_C(17, char);
			LOG_FIELD_UNDOCUM_C(18, char);
			LOG_FIELD_UNDOCUM_C(19, char);
			LOG_FIELD_UNDOCUM_C(20, char);
			LOG_FIELD_UNDOCUM_C(21, char);
			LOG_FIELD_UNDOCUM_C(22, char);
			LOG_FIELD_UNDOCUM_C(23, char);
			LOG_FIELD_UNDOCUM_C(24, char);
			break;
		case "CTrainGameStateNode"_J:
			LOG_FIELD_UNDOCUM(0, int);
			LOG_FIELD_UNDOCUM(4, int);
			LOG_FIELD_UNDOCUM(8, int);
			LOG_FIELD_UNDOCUM(12, int);
			LOG_FIELD_UNDOCUM(16, int);
			LOG_FIELD_UNDOCUM(20, int);
			LOG_FIELD_UNDOCUM(24, int); // whistle sequence
			LOG_FIELD_UNDOCUM_C(28, char);
			LOG_FIELD_UNDOCUM_C(29, char);
			LOG_FIELD_UNDOCUM_C(30, char); // track index?
			LOG_FIELD_UNDOCUM_C(31, char);
			LOG_FIELD_UNDOCUM_C(32, char);
			LOG_FIELD_UNDOCUM_C(33, char);
			LOG_FIELD_UNDOCUM_C(34, char);
			LOG_FIELD_UNDOCUM_C(35, char);
			LOG_FIELD_UNDOCUM_C(36, char);
			LOG_FIELD_UNDOCUM_C(37, char);
			LOG_FIELD_UNDOCUM_C(38, char);
			LOG_FIELD_UNDOCUM_C(39, char);
			LOG_FIELD_UNDOCUM_C(40, char);
			break;
			case "CGlobalFlagsNode"_J:
			{
				if (player && player.GetData().IsSyncsBlocked())
				{
					auto base = (&node->GetData<char>());
					if (IsReadable(base, 16))
					{
						// log minimal header words only when player is already quarantined
						LOG(INFO) << "\tHDR_0: " << *(int32_t*)(base + 0);
						LOG(INFO) << "\tHDR_4: " << *(int32_t*)(base + 4);
						LOG(INFO) << "\tHDR_8: " << *(int32_t*)(base + 8);
						LOG(INFO) << "\tHDR_12: " << *(int32_t*)(base + 12);
					}
					else
					{
						LOG(INFO) << "\tHDR: <unreadable>";
					}
				}
				break;
			}
			case "CWorldStateBaseNode"_J:
			{
				if (player && player.GetData().IsSyncsBlocked())
				{
					auto base = (&node->GetData<char>());
					if (IsReadable(base, 16))
					{
						LOG(INFO) << "\tHDR_0: " << *(int32_t*)(base + 0);
						LOG(INFO) << "\tHDR_4: " << *(int32_t*)(base + 4);
						LOG(INFO) << "\tHDR_8: " << *(int32_t*)(base + 8);
						LOG(INFO) << "\tHDR_12: " << *(int32_t*)(base + 12);
					}
					else
					{
						LOG(INFO) << "\tHDR: <unreadable>";
					}
				}
				break;
			}

		case "CTrainControlNode"_J:
			LOG_FIELD_UNDOCUM(160, float);
			LOG_FIELD_UNDOCUM_C(164, char);
			break;
		case "CAnimSceneCreationNode"_J:
			LOG_FIELD_UNDOCUM(0, int);
			LOG_FIELD_UNDOCUM(4, int);
			LOG_FIELD_UNDOCUM(8, int);
			LOG_FIELD_UNDOCUM(12, int);
			LOG_FIELD_UNDOCUM_C(16, char);
			//LOG_FIELD_UNDOCUM(32, __int128);
			//LOG_FIELD_UNDOCUM(48, __int128);
			//LOG_FIELD_UNDOCUM(64, __int128);
			//LOG_FIELD_UNDOCUM(80, __int128);
			//LOG_FIELD_UNDOCUM(96, __int128);
			LOG_FIELD_UNDOCUM(112, int16_t);
			LOG_FIELD_UNDOCUM(114, int16_t);
			break;
		case "CAnimSceneFrequentNode"_J:
			LOG_FIELD_UNDOCUM(0, int);
			LOG_FIELD_UNDOCUM_C(4, char);
			LOG_FIELD_UNDOCUM(8, int);
			LOG_FIELD_UNDOCUM(12, int);
			break;
		case "CAnimSceneInfrequentNode"_J:
			LOG_FIELD_UNDOCUM(16, int);
			LOG_FIELD_UNDOCUM_C(20, char);
			LOG_FIELD_UNDOCUM(24, int16_t);
			LOG_FIELD_UNDOCUM_C(26, char);
			LOG_FIELD_UNDOCUM(32, int64_t);
			LOG_FIELD_UNDOCUM(40, int64_t);
			break;
		case "CPedGameStateCommonNode"_J:
			LOG_FIELD_UNDOCUM(0, int);
			LOG_FIELD_UNDOCUM(4, int);
			LOG_FIELD_UNDOCUM(8, int);
			LOG_FIELD_UNDOCUM(12, int);
			LOG_FIELD_UNDOCUM(16, int);
			LOG_FIELD_UNDOCUM(20, float);
			LOG_FIELD_UNDOCUM(24, float);
			LOG_FIELD_UNDOCUM(28, float);
			LOG_FIELD_UNDOCUM(32, float);
			LOG_FIELD_UNDOCUM(36, float);
			LOG_FIELD_UNDOCUM_C(40, char);
			LOG_FIELD_UNDOCUM(44, int);
			LOG_FIELD_UNDOCUM_C(48, char);
			LOG_FIELD_UNDOCUM_C(50, char);
			LOG_FIELD_UNDOCUM_C(51, char);
			LOG_FIELD_UNDOCUM_C(52, char);
			LOG_FIELD_UNDOCUM_C(53, char);
			LOG_FIELD_UNDOCUM_C(54, char);
			LOG_FIELD_UNDOCUM_C(55, char);
			break;
		case "CPedSectorPosMapNode"_J:
			LOG_FIELD_UNDOCUM(0, float);
			LOG_FIELD_UNDOCUM(4, float);
			LOG_FIELD_UNDOCUM(8, float);
			LOG_FIELD_UNDOCUM_C(12, bool); // using ragdoll?
			LOG_FIELD_UNDOCUM_C(13, char);
			break;
		case "CPedSectorPosNavMeshNode"_J:
			LOG_FIELD_UNDOCUM(0, float);
			LOG_FIELD_UNDOCUM(4, float);
			LOG_FIELD_UNDOCUM(8, float);
			break;
		case "CPedMovementNode"_J:
			LOG_FIELD_UNDOCUM(32, int);
			LOG_FIELD_UNDOCUM(44, int);
			LOG_FIELD_UNDOCUM(64, float);
			LOG_FIELD_UNDOCUM(68, int);
			LOG_FIELD_UNDOCUM(72, int);
			LOG_FIELD_UNDOCUM(76, int);
			LOG_FIELD_UNDOCUM(80, int);
			LOG_FIELD_UNDOCUM(84, int);
			LOG_FIELD_UNDOCUM(88, int);
			LOG_FIELD_UNDOCUM_C(92, char);
			LOG_FIELD_UNDOCUM_C(97, char);
			break;
		case "CDraftVehControlNode"_J:
			LOG_FIELD_UNDOCUM(32, int);
			LOG_FIELD_UNDOCUM(36, int);
			LOG_FIELD_UNDOCUM(40, int);
			LOG_FIELD_UNDOCUM(44, int);
			LOG_FIELD_UNDOCUM(48, int);
			LOG_FIELD_UNDOCUM_C(52, char);
			LOG_FIELD_UNDOCUM_C(53, char);
			LOG_FIELD_UNDOCUM_C(54, char);
			LOG_FIELD_UNDOCUM(56, int);
			LOG_FIELD_UNDOCUM(60, int);
			break;
		case "Node_14359d020"_J:
			//LOG_FIELD_UNDOCUM(16, OWORD);
			//LOG_FIELD_UNDOCUM(32, OWORD);
			//LOG_FIELD_UNDOCUM(48, OWORD);
			LOG_FIELD_UNDOCUM(64, int);
			LOG_FIELD_UNDOCUM(100, int);
			LOG_FIELD_UNDOCUM(140, int);
			//LOG_FIELD_UNDOCUM(160, __m128i);
			LOG_FIELD_UNDOCUM(176, int16_t);
			LOG_FIELD_UNDOCUM(178, int16_t);
			LOG_FIELD_UNDOCUM(180, int16_t);
			LOG_FIELD_UNDOCUM(184, int);
			LOG_FIELD_UNDOCUM(188, int);
			LOG_FIELD_UNDOCUM(196, int);
			LOG_FIELD_UNDOCUM(200, int);
			LOG_FIELD_UNDOCUM(204, int);
			LOG_FIELD_UNDOCUM(208, int);
			LOG_FIELD_UNDOCUM(212, int);
			LOG_FIELD_UNDOCUM(216, int);
			LOG_FIELD_UNDOCUM_C(220, char);
			LOG_FIELD_UNDOCUM_C(221, char);
			LOG_FIELD_UNDOCUM_C(222, char);
			LOG_FIELD_UNDOCUM_C(223, char);
			LOG_FIELD_UNDOCUM_C(224, char);
			LOG_FIELD_UNDOCUM_C(225, char);
			LOG_FIELD_UNDOCUM_C(226, char);
			LOG_FIELD_UNDOCUM(356, int);
			LOG_FIELD_UNDOCUM(360, int);
			LOG_FIELD_UNDOCUM(364, int);
			LOG_FIELD_UNDOCUM(368, int);
			LOG_FIELD_UNDOCUM(216, int);
			LOG_FIELD_UNDOCUM(372, int);
			LOG_FIELD_UNDOCUM(376, int);
			break;
		case "CVehicleControlNode"_J:
			LOG_FIELD_UNDOCUM(4, int);
			LOG_FIELD_UNDOCUM(8, int);
			LOG_FIELD_UNDOCUM(12, int);
			LOG_FIELD_UNDOCUM(16, int);
			LOG_FIELD_UNDOCUM(20, int);
			LOG_FIELD_UNDOCUM_C(24, char);
			LOG_FIELD_UNDOCUM_C(25, char);
			LOG_FIELD_UNDOCUM_C(26, char);
			LOG_FIELD_UNDOCUM_C(27, char);
			break;
		case "CVehicleAngVelocityNode"_J:
			LOG_FIELD_UNDOCUM(0, int);
			LOG_FIELD_UNDOCUM(4, int);
			LOG_FIELD_UNDOCUM(8, int);
			LOG_FIELD_UNDOCUM_C(12, char);
			break;
		case "CDraftVehHorseHealthNode"_J:
			LOG_FIELD_UNDOCUM_C(158, char);
			LOG_FIELD_UNDOCUM_C(656, char);
			break;
		case "CVehicleGameStateNode"_J:
			LOG_FIELD_UNDOCUM_C(1, char);
			LOG_FIELD_UNDOCUM_C(2, char);
			LOG_FIELD_UNDOCUM_C(3, char);
			LOG_FIELD_UNDOCUM_C(4, char);
			LOG_FIELD_UNDOCUM_C(5, char);
			LOG_FIELD_UNDOCUM(8, int);
			LOG_FIELD_UNDOCUM(12, int);
			LOG_FIELD_UNDOCUM(16, int);
			LOG_FIELD_UNDOCUM(20, int);
			LOG_FIELD_UNDOCUM(24, int);
			LOG_FIELD_UNDOCUM(28, int);
			LOG_FIELD_UNDOCUM(32, int);
			LOG_FIELD_UNDOCUM(36, int);
			LOG_FIELD_UNDOCUM(40, int);
			LOG_FIELD_UNDOCUM(44, int16_t);
			LOG_FIELD_UNDOCUM(46, int16_t);
			LOG_FIELD_UNDOCUM(50, int16_t);
			LOG_FIELD_UNDOCUM(52, int16_t);
			LOG_FIELD_UNDOCUM_C(54, char);
			LOG_FIELD_UNDOCUM(55, int);
			LOG_FIELD_UNDOCUM(59, int16_t);
			LOG_FIELD_UNDOCUM_C(61, char);
			LOG_FIELD_UNDOCUM_C(62, char);
			LOG_FIELD_UNDOCUM_C(63, char);
			LOG_FIELD_UNDOCUM_C(104, char);
			LOG_FIELD_UNDOCUM_C(105, char);
			LOG_FIELD_UNDOCUM_C(106, char);
			LOG_FIELD_UNDOCUM_C(107, char);
			LOG_FIELD_UNDOCUM_C(108, char);
			LOG_FIELD_UNDOCUM_C(109, char);
			LOG_FIELD_UNDOCUM_C(110, char);
			LOG_FIELD_UNDOCUM_C(111, char);
			LOG_FIELD_UNDOCUM_C(112, char);
			LOG_FIELD_UNDOCUM_C(114, char);
			LOG_FIELD_UNDOCUM_C(115, char);
			LOG_FIELD_UNDOCUM_C(116, char);
			LOG_FIELD_UNDOCUM_C(117, char);
			LOG_FIELD_UNDOCUM_C(118, char);
			LOG_FIELD_UNDOCUM_C(119, char);
			LOG_FIELD_UNDOCUM_C(120, char);
			LOG_FIELD_UNDOCUM_C(121, char);
			LOG_FIELD_UNDOCUM_C(123, char);
			LOG_FIELD_UNDOCUM_C(124, char);
			LOG_FIELD_UNDOCUM_C(125, char);
			LOG_FIELD_UNDOCUM_C(126, char);
			LOG_FIELD_UNDOCUM_C(127, char);
			LOG_FIELD_UNDOCUM_C(128, char);
			LOG_FIELD_UNDOCUM_C(129, char);
			LOG_FIELD_UNDOCUM_C(130, char);
			LOG_FIELD_UNDOCUM_C(131, char); // has seat manager
			LOG_FIELD_UNDOCUM_C(132, char); // depends on the value of tunable 0xF03033E4
			break;
		case "CPedStandingOnObjectNode"_J:
			LOG_FIELD_UNDOCUM_C(0, int16_t);
			LOG_FIELD_UNDOCUM(16, float); // also __m128i, not sure
			LOG_FIELD_UNDOCUM(20, float);
			LOG_FIELD_UNDOCUM(24, float);
			LOG_FIELD_UNDOCUM(32, int);
			LOG_FIELD_UNDOCUM(36, int);
			LOG_FIELD_UNDOCUM(40, int);
			LOG_FIELD_UNDOCUM_C(44, char);
			LOG_FIELD_UNDOCUM_C(45, char);
			break;
		case "Node_143594ab8"_J:
			LOG_FIELD_UNDOCUM_C(0, char);
			LOG_FIELD_UNDOCUM_C(1, char);
			LOG_FIELD_UNDOCUM_C(2, char);
			LOG_FIELD_UNDOCUM_C(3, char);
			LOG_FIELD_UNDOCUM_C(4, char);
			LOG_FIELD_UNDOCUM_C(5, char);
			LOG_FIELD_UNDOCUM_C(6, char);
			LOG_FIELD_UNDOCUM_C(7, char);
			LOG_FIELD_UNDOCUM_C(8, char);
			LOG_FIELD_UNDOCUM_C(9, char);
			LOG_FIELD_UNDOCUM(12, int);
			LOG_FIELD_UNDOCUM(16, int);
			LOG_FIELD_UNDOCUM(20, int);
			break;
		case "CVehicleScriptGameStateNode"_J:
			LOG_FIELD_UNDOCUM_C(96, char);
			LOG_FIELD_UNDOCUM_C(97, char);
			LOG_FIELD_UNDOCUM_C(98, char);
			LOG_FIELD_UNDOCUM_C(99, char);
			LOG_FIELD_UNDOCUM_C(100, char);
			LOG_FIELD_UNDOCUM_C(101, char);
			LOG_FIELD_UNDOCUM_C(102, char);
			LOG_FIELD_UNDOCUM_C(103, char);
			LOG_FIELD_UNDOCUM_C(104, char);
			LOG_FIELD_UNDOCUM_C(105, char);
			LOG_FIELD_UNDOCUM_C(106, char);
			LOG_FIELD_UNDOCUM_C(107, char);
			LOG_FIELD_UNDOCUM_C(108, char);
			LOG_FIELD_UNDOCUM_C(109, char);
			LOG_FIELD_UNDOCUM_C(110, char);
			LOG_FIELD_UNDOCUM_C(111, char);
			LOG_FIELD_UNDOCUM_C(112, char);
			LOG_FIELD_UNDOCUM_C(113, char);
			LOG_FIELD_UNDOCUM_C(114, char);
			LOG_FIELD_UNDOCUM_C(115, char);
			LOG_FIELD_UNDOCUM_C(116, char);
			LOG_FIELD_UNDOCUM_C(117, char);
			LOG_FIELD_UNDOCUM_C(118, char);
			LOG_FIELD_UNDOCUM_C(119, char);
			LOG_FIELD_UNDOCUM_C(120, char);
			LOG_FIELD_UNDOCUM_C(121, char);
			LOG_FIELD_UNDOCUM_C(122, char);
			LOG_FIELD_UNDOCUM(124, int32_t);
			LOG_FIELD_UNDOCUM(128, int16_t);
			LOG_FIELD_UNDOCUM(132, int32_t);
			LOG_FIELD_UNDOCUM(136, int32_t);
			LOG_FIELD_UNDOCUM_C(140, char);
			LOG_FIELD_UNDOCUM_C(141, char);
			LOG_FIELD_UNDOCUM_C(142, char);
			break;
		case "CPhysicalGameState"_J:
			LOG_FIELD_UNDOCUM_C(0, char);
			LOG_FIELD_UNDOCUM_C(1, char);
			LOG_FIELD_UNDOCUM_C(2, char);
			LOG_FIELD_UNDOCUM_C(3, char);
			LOG_FIELD_UNDOCUM_C(4, char);
			LOG_FIELD_UNDOCUM_C(5, char);
			LOG_FIELD_UNDOCUM_C(6, char);
			LOG_FIELD_UNDOCUM_C(7, char);
			LOG_FIELD_UNDOCUM_C(8, char);
			LOG_FIELD_UNDOCUM_C(9, char);
			LOG_FIELD_UNDOCUM(12, int32_t);
			LOG_FIELD_UNDOCUM(16, int32_t);
			LOG_FIELD_UNDOCUM(20, int32_t);
			break;
		case "CEntityScriptGameStateNode"_J:
			LOG_FIELD_UNDOCUM_C(0, char);
			LOG_FIELD_UNDOCUM_C(1, char);
			LOG_FIELD_UNDOCUM_C(2, char);
			break;
		case "CPedScriptGameStateUncommonNode"_J:
			LOG_FIELD_UNDOCUM(16, int32_t);
			LOG_FIELD_UNDOCUM_C(27, char);
			LOG_FIELD_UNDOCUM(32, int32_t);
			LOG_FIELD_UNDOCUM(36, int16_t);
			LOG_FIELD_UNDOCUM(38, int32_t);
			LOG_FIELD_UNDOCUM_C(40, char);
			LOG_FIELD_UNDOCUM_C(41, char);
			LOG_FIELD_UNDOCUM_C(42, char);
			LOG_FIELD_UNDOCUM_C(43, char);
			break;
		case "CPedEmotionalLocoNode"_J:
			LOG_FIELD_UNDOCUM_C(0, char);
			LOG_FIELD_UNDOCUM(4, int32_t);
			LOG_FIELD_UNDOCUM(12, int32_t); // some pointer
			break;
		case "CPropSetGameStateNode"_J:
			LOG_FIELD_UNDOCUM_C(1, char);
			LOG_FIELD_UNDOCUM_C(91, char);
			LOG_FIELD_UNDOCUM_C(181, char);
			LOG_FIELD_UNDOCUM(272, int32_t);
			LOG_FIELD_UNDOCUM_C(276, char);
			LOG_FIELD_UNDOCUM_C(277, char);
			break;
		case "CPlayerGameStateUncommonNode"_J:
			LOG_FIELD_UNDOCUM_C(0, char);
			LOG_FIELD_UNDOCUM_C(1, char);
			LOG_FIELD_UNDOCUM_C(2, char);
			LOG_FIELD_UNDOCUM_C(3, char);
			LOG_FIELD_UNDOCUM_C(4, char);
			LOG_FIELD_UNDOCUM_C(5, char);
			LOG_FIELD_UNDOCUM_C(6, char);
			LOG_FIELD_UNDOCUM_C(7, char);
			LOG_FIELD_UNDOCUM_C(8, char);
			LOG_FIELD_UNDOCUM(76, int64_t);
			LOG_FIELD_UNDOCUM(84, int32_t);
			LOG_FIELD_UNDOCUM(88, int16_t);
			LOG_FIELD_UNDOCUM(92, int32_t);
			LOG_FIELD_UNDOCUM_C(96, char);
			LOG_FIELD_UNDOCUM_C(97, char);
			LOG_FIELD_UNDOCUM(98, int16_t);
			LOG_FIELD_UNDOCUM_C(100, char);
			LOG_FIELD_UNDOCUM_C(101, char);
			LOG_FIELD_UNDOCUM(104, int32_t);
			LOG_FIELD_UNDOCUM(108, int32_t);
			LOG_FIELD_UNDOCUM(112, int32_t);
			LOG_FIELD_UNDOCUM(116, float);
			LOG_FIELD_UNDOCUM(120, int16_t);
			LOG_FIELD_UNDOCUM(144, int16_t);
			LOG_FIELD_UNDOCUM(146, int16_t);
			LOG_FIELD_UNDOCUM_C(148, char);
			LOG_FIELD_UNDOCUM_C(149, char);
			LOG_FIELD_UNDOCUM_C(150, char);
			LOG_FIELD_UNDOCUM_C(151, char);
			LOG_FIELD_UNDOCUM_C(152, char);
			LOG_FIELD_UNDOCUM_C(153, char);
			LOG_FIELD_UNDOCUM_C(154, char);
			LOG_FIELD_UNDOCUM_C(155, char);
			break;
		case "CPhysicalGameStateNode"_J:
			LOG_FIELD_UNDOCUM_C(0, char);
			LOG_FIELD_UNDOCUM_C(1, char);
			LOG_FIELD_UNDOCUM_C(2, char);
			LOG_FIELD_UNDOCUM_C(3, char);
			LOG_FIELD_UNDOCUM_C(4, char);
			LOG_FIELD_UNDOCUM_C(5, char);
			LOG_FIELD_UNDOCUM_C(6, char);
			LOG_FIELD_UNDOCUM_C(7, char);
			LOG_FIELD_UNDOCUM_C(8, char);
			LOG_FIELD_UNDOCUM_C(9, char);
			LOG_FIELD_UNDOCUM(12, int32_t);
			LOG_FIELD_UNDOCUM(16, int32_t);
			LOG_FIELD_UNDOCUM(20, int32_t);
			break;
		case "CDynamicEntityGameStateNode"_J:
			LOG_FIELD_UNDOCUM(0, int32_t);
			LOG_FIELD_UNDOCUM_C(4, char);
			LOG_FIELD_UNDOCUM(8, int32_t);
			LOG_FIELD_UNDOCUM(12, int64_t);
			break;
		case "CPedInteractionNode"_J:
			LOG_FIELD_UNDOCUM(0, int16_t);
			LOG_FIELD_UNDOCUM(2, int16_t);
			LOG_FIELD_UNDOCUM(4, int32_t);
			LOG_FIELD_UNDOCUM(8, int32_t);
			LOG_FIELD_UNDOCUM(12, int32_t);
			LOG_FIELD_UNDOCUM(16, int32_t);
			LOG_FIELD_UNDOCUM(28, int32_t);
			LOG_FIELD_UNDOCUM(32, int32_t);
			LOG_FIELD_UNDOCUM(36, int32_t);
			LOG_FIELD_UNDOCUM(40, int32_t);
			LOG_FIELD_UNDOCUM(44, int16_t);
			LOG_FIELD_UNDOCUM(48, int16_t);
			LOG_FIELD_UNDOCUM_C(50, char);
			LOG_FIELD_UNDOCUM_C(51, char);
			break;
		case "CPhysicalScriptGameStateNode"_J:
			LOG_FIELD_UNDOCUM_C(0, char);
			LOG_FIELD_UNDOCUM_C(1, char);
			LOG_FIELD_UNDOCUM_C(2, char);
			LOG_FIELD_UNDOCUM_C(3, char);
			LOG_FIELD_UNDOCUM_C(4, char);
			LOG_FIELD_UNDOCUM_C(5, char);
			LOG_FIELD_UNDOCUM_C(6, char);
			LOG_FIELD_UNDOCUM_C(7, char);
			LOG_FIELD_UNDOCUM_C(8, char);
			LOG_FIELD_UNDOCUM_C(9, char);
			LOG_FIELD_UNDOCUM_C(10, char);
			LOG_FIELD_UNDOCUM_C(11, char);
			LOG_FIELD_UNDOCUM_C(12, char);
			LOG_FIELD_UNDOCUM_C(13, char);
			LOG_FIELD_UNDOCUM_C(14, char);
			LOG_FIELD_UNDOCUM_C(15, char);
			LOG_FIELD_UNDOCUM_C(16, char);
			LOG_FIELD_UNDOCUM(20, int32_t);
			LOG_FIELD_UNDOCUM(24, int32_t);
			LOG_FIELD_UNDOCUM(28, float);
			LOG_FIELD_UNDOCUM(32, int32_t);
			break;
		case "CEntityScriptInfoNode"_J:
			LOG_FIELD_UNDOCUM_C(0, char);
			//LOG_FIELD_UNDOCUM_C(8, void*);
			LOG_FIELD_UNDOCUM_C(64, char);
			LOG_FIELD_UNDOCUM(68, int32_t);
			//LOG_FIELD_UNDOCUM(72, __m128i);
			LOG_FIELD_UNDOCUM(312, int32_t);
			break;
		case "CDoorDamageNode"_J:
			LOG_FIELD_UNDOCUM(0, int32_t);
			LOG_FIELD_UNDOCUM(4, int64_t);  // oword
			LOG_FIELD_UNDOCUM(20, int64_t); // oword
			break;
		case "CPedOrientationNode"_J:
			LOG_FIELD_UNDOCUM(0, float);
			LOG_FIELD_UNDOCUM(4, float);
			LOG_FIELD_UNDOCUM_C(8, char);
			LOG_FIELD_UNDOCUM_C(9, char);
			break;
		case "CPedWeaponNode"_J:
			LOG_FIELD_UNDOCUM(4, int16_t);
			LOG_FIELD_UNDOCUM_C(6, char);
			LOG_FIELD_UNDOCUM_C(7, char);
			LOG_FIELD_UNDOCUM(8, int);
			LOG_FIELD_UNDOCUM(476, int32_t);
			LOG_FIELD_UNDOCUM(480, int32_t);
			LOG_FIELD_UNDOCUM(484, int32_t);
			LOG_FIELD_UNDOCUM(488, int32_t);
			LOG_FIELD_UNDOCUM_C(492, char);
			LOG_FIELD_UNDOCUM_C(493, char);
			LOG_FIELD_UNDOCUM_C(494, char);
			LOG_FIELD_UNDOCUM_C(495, char);
			LOG_FIELD_UNDOCUM_C(496, char);
			LOG_FIELD_UNDOCUM_C(497, char);
			LOG_FIELD_UNDOCUM_C(498, char);
			LOG_FIELD_UNDOCUM_C(799, char);
			// untested
			LOG_FIELD_UNDOCUM(16, int64_t);
			LOG_FIELD_UNDOCUM(24, int32_t);
			LOG_FIELD_UNDOCUM(32, int16_t);
			LOG_FIELD_UNDOCUM(40, int32_t);
			break;
		case "CPedInventoryNode"_J:
			LOG_FIELD_UNDOCUM(4000, int);
			LOG_FIELD_UNDOCUM(4080, int);
			LOG_FIELD_UNDOCUM(4084, int);
			LOG_FIELD_UNDOCUM(4184, int);
			LOG_FIELD_UNDOCUM(4284, int);
			LOG_FIELD_UNDOCUM_C(4288, char);
			LOG_FIELD_UNDOCUM_C(4308, char);
			LOG_FIELD_UNDOCUM_C(4328, char);
			LOG_FIELD_UNDOCUM_C(5328, char);
			LOG_FIELD_UNDOCUM_C(5353, char);
			break;
		case "Node_14359d660"_J:
			LOG_FIELD_UNDOCUM(36, uint32_t);
			for (int i = 0; i < 16; i++)
			{
				LOG_FIELD_UNDOCUM(36 * i + 56, uint32_t);
				LOG_FIELD_UNDOCUM(36 * i + 60, uint32_t);
				LOG_FIELD_UNDOCUM(36 * i + 64, uint32_t);
				LOG_FIELD_UNDOCUM(36 * i + 68, uint32_t);
				LOG_FIELD_UNDOCUM(36 * i + 72, uint32_t);
			}
			break;
		case "CDraftVehGameStateNode"_J:
			LOG_FIELD_UNDOCUM_C(0, char);
			LOG_FIELD_UNDOCUM_C(1, char);
			LOG_FIELD_UNDOCUM_C(2, char);
			LOG_FIELD_UNDOCUM_C(3, char);
			LOG_FIELD_UNDOCUM_C(4, char);
			break;
		case "CPhysicalHealthNode"_J:
			LOG_FIELD_UNDOCUM(0, int);
			LOG_FIELD_UNDOCUM(4, int);
			LOG_FIELD_UNDOCUM(8, int16_t);
			LOG_FIELD_UNDOCUM(10, int);
			LOG_FIELD_UNDOCUM(16, int);
			LOG_FIELD_UNDOCUM_C(20, char);
			LOG_FIELD_UNDOCUM_C(21, char);
			break;
		case "Node_143599c30"_J:
			LOG_FIELD_UNDOCUM(80, int);
			LOG_FIELD_UNDOCUM_C(84, char);
			LOG_FIELD_UNDOCUM_C(85, char);
			break;
		case "CAutomobileCreationNode"_J:
			LOG_FIELD_UNDOCUM_C(0, char);
			for (int i = 0; i < 10; ++i)
			{
				LOG_FIELD_UNDOCUM(14 + i, int);
			}
			LOG_FIELD_UNDOCUM(24, int);
			break;
		case "CDraftVehCreationNodeThing"_J:
			LOG_FIELD_UNDOCUM(0, int);
			LOG_FIELD_UNDOCUM_C(48, char);
			break;
		}

	}
}