#include "core/commands/BoolCommand.hpp"
#include "core/hooking/DetourHook.hpp"
#include "game/backend/PlayerData.hpp"
#include "game/backend/Protections.hpp"
#include "game/backend/Self.hpp"
#include "game/hooks/Hooks.hpp"
#include "game/pointers/Pointers.hpp"
#include "game/rdr/Enums.hpp"

#include <network/CNetGamePlayer.hpp>
#include <network/netObject.hpp>
#include <network/rlGamerInfo.hpp>
#include <rage/datBitBuffer.hpp>
#include <excpt.h>

#include <unordered_set>


namespace
{
	bool IsVehicleType(NetObjType type)
	{
		switch (type)
		{
		case NetObjType::Automobile:
		case NetObjType::Bike:
		case NetObjType::Heli:
		case NetObjType::DraftVeh:
		case NetObjType::Boat: return true;
		}

		return false;
	}
}

namespace YimMenu::Features
{
	BoolCommand _LogEvents("logevents", "Log Network Events", "Log network events");
	BoolCommand _BlockExplosions("blockexplosions", "Block Explosions", "Blocks all explosion events", false);
	BoolCommand _BlockPtfx("blockptfx", "Block PTFX", "Blocks all particle effect events", true);

		// seh wrapper like ReceiveNetMessage: no c++ objects in guarded region
		using HandleEventFn = void (*)(rage::netEventMgr*, CNetGamePlayer*, CNetGamePlayer*, NetEventType, int, int, std::int16_t, rage::datBitBuffer*);
		static void __declspec(noinline) CallEvent_SEH(HandleEventFn fn,
		    rage::netEventMgr* eventMgr,
		    CNetGamePlayer* sourcePlayer,
		    CNetGamePlayer* targetPlayer,
		    NetEventType type,
		    int index,
		    int handledBits,
		    std::int16_t unk,
		    rage::datBitBuffer* buffer)
		{
			__try
			{
				fn(eventMgr, sourcePlayer, targetPlayer, type, index, handledBits, unk, buffer);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				if (sourcePlayer)
				{
					Player(sourcePlayer).GetData().QuarantineFor(std::chrono::seconds(10));
				}
			}
		}

	BoolCommand _BlockClearTasks("blockclearpedtasks", "Block Clear Tasks", "Blocks all clear ped tasks events", true);
	BoolCommand _BlockScriptCommand("blockscriptcommand", "Block Remote Native Calls", "Blocks all remote native call events", true);
}

namespace YimMenu::Hooks
{
	static std::unordered_set<uint64_t> g_PtfxWarned;
	static void LogScriptCommandEvent(Player sender, rage::datBitBuffer& buffer)
	{
		struct ScriptCommandEventData
		{
			bool Active;
			int NumParams;
			std::uint64_t Hash;
			std::uint32_t ScriptHash;
		} datas[7]{};

		std::uint32_t parameters[255]{};

		auto net_id      = buffer.Read<uint16_t>(13);
		auto count       = buffer.Read<int>(3);
		int total_params = 0;

		for (int i = 0; i < count; i++)
		{
			datas[i].Hash       = buffer.Read<uint64_t>(64);
			datas[i].NumParams  = buffer.Read<uint32_t>(5);
			datas[i].ScriptHash = buffer.Read<uint32_t>(32);
			total_params += datas[i].NumParams;
		}

		for (int i = 0; i < total_params; i++)
		{
			int size = 1;
			bool one = buffer.Read<bool>(1);
			if (!one)
				size = buffer.Read<int>(7);
			parameters[i] = buffer.Read<uint64_t>(size);
		}

		int global_param_access = 0;
		for (int i = 0; i < count; i++)
		{
			std::ostringstream data;
			data << HEX(datas[i].Hash) << "(";
			for (int j = 0; j < datas[i].NumParams; j++)
			{
				data << std::to_string(parameters[global_param_access]);
				global_param_access++;
				if (j != datas[i].NumParams - 1)
					data << ", ";
			}
			data << ");";
			LOG(WARNING) << sender.GetName() << " tried to execute " << data.str();
		}
	}

	void Protections::HandleNetGameEvent(rage::netEventMgr* eventMgr, CNetGamePlayer* sourcePlayer, CNetGamePlayer* targetPlayer, NetEventType type, int index, int handledBits, std::int16_t unk, rage::datBitBuffer* buffer)
	{
		rage::datBitBuffer new_buffer = *buffer;

		// quarantine gate: drop all net events from quarantined senders
		if (sourcePlayer)
		{
			auto p = Player(sourcePlayer);
			if (p.GetData().IsSyncsBlocked())
			{
				Pointers.SendEventAck(eventMgr, nullptr, sourcePlayer, targetPlayer, index, handledBits);
				return;
			}
		}

		if (Features::_LogEvents.GetState() && (int)type < g_NetEventsToString.size())
		{
			LOG(INFO) << "NETWORK_EVENT: " << g_NetEventsToString[(int)type] << " from " << sourcePlayer->GetName();
		}

		if (type == NetEventType::NETWORK_DESTROY_VEHICLE_LOCK_EVENT)
		{
			auto net_id = new_buffer.Read<uint16_t>(13);
			if (auto object = Pointers.GetNetObjectById(net_id))
			{
				if (!IsVehicleType((NetObjType)object->m_ObjectType))
				{
					LOG(WARNING) << "Blocked mismatched destroy vehicle lock event entity from " << sourcePlayer->GetName();
					Player(sourcePlayer).AddDetection(Detection::TRIED_CRASH_PLAYER);
					Pointers.SendEventAck(eventMgr, nullptr, sourcePlayer, targetPlayer, index, handledBits);
					return;
				}
			}
		}

			// weapon damage crash/spam protection
			if (type == NetEventType::WEAPON_DAMAGE_EVENT && sourcePlayer)
			{
				auto p = Player(sourcePlayer);
				// rate-limit damage events per attacker; drop and quarantine on spam
				if (p.GetData().m_WeaponDamageRateLimit.Process())
				{
					if (p.GetData().m_WeaponDamageRateLimit.ExceededLastProcess())
					{
						LOGF(NET_EVENT, WARNING, "Blocked weapon damage spam from {}", sourcePlayer->GetName());
						p.GetData().QuarantineFor(std::chrono::seconds(10));
						Pointers.SendEventAck(eventMgr, nullptr, sourcePlayer, targetPlayer, index, handledBits);
						return;
					}
				}
			}

			// sunrise variant: sometimes routes damage through different net events; apply same rate-limiting
			if ((type == NetEventType::FIRE_EVENT || type == NetEventType::FIRE_TRAIL_UPDATE_EVENT || type == NetEventType::PED_PLAY_PAIN_EVENT) && sourcePlayer)
			{
				auto p = Player(sourcePlayer);
				if (p.GetData().m_WeaponDamageRateLimit.Process())
				{
					if (p.GetData().m_WeaponDamageRateLimit.ExceededLastProcess())
					{
						LOGF(NET_EVENT, WARNING, "Blocked damage-adjacent spam (type {}) from {}", (int)type, sourcePlayer->GetName());
						p.GetData().QuarantineFor(std::chrono::seconds(10));
						Pointers.SendEventAck(eventMgr, nullptr, sourcePlayer, targetPlayer, index, handledBits);
						return;
					}
				}
			}

			// train crash protection: spam + invalid
			if (type == NetEventType::NETWORK_TRAIN_REQUEST_EVENT && sourcePlayer)
			{
				auto p = Player(sourcePlayer);
				if (p.GetData().m_TrainEventRateLimit.Process())
				{
					if (p.GetData().m_TrainEventRateLimit.ExceededLastProcess())
					{
						LOGF(NET_EVENT, WARNING, "Blocked train request spam from {}", sourcePlayer->GetName());
						p.GetData().QuarantineFor(std::chrono::seconds(10));
						Pointers.SendEventAck(eventMgr, nullptr, sourcePlayer, targetPlayer, index, handledBits);
						return;
					}
				}
			}
			// ghost-with-player spam protection: quarantine abusive senders
			if (type == NetEventType::NETWORK_SET_ENTITY_GHOST_WITH_PLAYER_EVENT && sourcePlayer)
			{
				auto p = Player(sourcePlayer);
				if (p.GetData().m_GhostEventRateLimit.Process() && p.GetData().m_GhostEventRateLimit.ExceededLastProcess())
				{
					LOGF(NET_EVENT, WARNING, "Blocked ghost-with-player spam from {}", sourcePlayer->GetName());
					p.GetData().QuarantineFor(std::chrono::seconds(10));
					Pointers.SendEventAck(eventMgr, nullptr, sourcePlayer, targetPlayer, index, handledBits);
					return;
				}
			}



		if (type == NetEventType::EXPLOSION_EVENT && sourcePlayer)
		{
			if (Features::_BlockExplosions.GetState()
			    || (Player(sourcePlayer).IsValid() && Player(sourcePlayer).GetData().m_BlockExplosions))
			{
				LOG(WARNING) << "Blocked explosion from " << sourcePlayer->GetName();
				Player(sourcePlayer).GetData().QuarantineFor(std::chrono::seconds(10));
				Pointers.SendEventAck(eventMgr, nullptr, sourcePlayer, targetPlayer, index, handledBits);
				return;
			}
		}


		if (type == NetEventType::NETWORK_PTFX_EVENT && sourcePlayer)
		{
			if (Features::_BlockPtfx.GetState() || (Player(sourcePlayer).IsValid() && Player(sourcePlayer).GetData().m_BlockParticles))
			{
				auto rid = Player(sourcePlayer).GetRID();
				if (g_PtfxWarned.insert(rid).second)
				{
					LOG(WARNING) << "Blocked particle effects from " << sourcePlayer->GetName();
				}
				Player(sourcePlayer).GetData().QuarantineFor(std::chrono::seconds(10));
				Pointers.SendEventAck(eventMgr, nullptr, sourcePlayer, targetPlayer, index, handledBits);
				return;
			}
		}

		if (type == NetEventType::SCRIPT_COMMAND_EVENT && sourcePlayer && Features::_BlockScriptCommand.GetState())
		{
			LogScriptCommandEvent(sourcePlayer, new_buffer);
			LOG(WARNING) << "Blocked remote native call from " << sourcePlayer->GetName();
			Pointers.SendEventAck(eventMgr, nullptr, sourcePlayer, targetPlayer, index, handledBits);
			Player(sourcePlayer).AddDetection(Detection::MODDER_EVENTS);
			return;
		}

		if (type == NetEventType::GIVE_CONTROL_EVENT && sourcePlayer)
		{
			// rate-limit give-control storms; quarantine abusive senders to prevent session faults
			auto p = Player(sourcePlayer);
			if (p.GetData().m_GiveControlRateLimit.Process() && p.GetData().m_GiveControlRateLimit.ExceededLastProcess())
			{
				LOGF(NET_EVENT, WARNING, "Blocked give-control spam from {}", sourcePlayer->GetName());
				p.GetData().QuarantineFor(std::chrono::seconds(30));
				Pointers.SendEventAck(eventMgr, nullptr, sourcePlayer, targetPlayer, index, handledBits);
				return;
			}
			YimMenu::Protections::SetSyncingPlayer(sourcePlayer);
		}

		// flood controls for common valid-but-abused events
		if (sourcePlayer)
		{
			auto p = Player(sourcePlayer);
			switch (type)
			{
			case NetEventType::NETWORK_CLEAR_PED_TASKS_EVENT:
				if (p.GetData().m_ClearTasksRateLimit.Process() && p.GetData().m_ClearTasksRateLimit.ExceededLastProcess())
				{
					LOGF(NET_EVENT, WARNING, "Blocked clear tasks spam from {}", sourcePlayer->GetName());
					p.GetData().QuarantineFor(std::chrono::seconds(10));
					Pointers.SendEventAck(eventMgr, nullptr, sourcePlayer, targetPlayer, index, handledBits);
					return;
				}
				break;
			default:
				break;
			}
		}

		// wrap original call in SEH to avoid crash on malformed payloads
		YimMenu::Features::CallEvent_SEH(BaseHook::Get<Protections::HandleNetGameEvent, DetourHook<decltype(&Protections::HandleNetGameEvent)>>()->Original(), eventMgr, sourcePlayer, targetPlayer, type, index, handledBits, unk, buffer);
		return;
	}
}
