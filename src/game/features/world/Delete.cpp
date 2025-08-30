#include "core/commands/Command.hpp"
#include "game/rdr/Pools.hpp"
#include "game/rdr/Entity.hpp"
#include "game/rdr/Network.hpp"
#include <network/netObject.hpp>
#include "game/backend/ScriptMgr.hpp"
#include "game/backend/FiberPool.hpp"

namespace YimMenu::Features
{
	class DeleteAllObjects : public Command
	{
		using Command::Command;

		virtual void OnCall() override
		{
			for (auto obj : Pools::GetObjects())
			{
				if (obj)
				{
					if (obj.HasControl())
						obj.ForceSync();
					obj.Delete();
				}
			}
		}
	};

	class DeleteAllPeds : public Command
	{
		using Command::Command;

		virtual void OnCall() override
		{
			// pass 1: fast path (delete what we own or can immediately own)
			for (auto ped : Pools::GetPeds())
			{
				if (!ped.IsPlayer())
				{
					ped.ForceControl();
					if (ped.HasControl())
					{
						ped.ForceSync();
						ped.Delete();
					}
				}
			}

			// pass 2: yield a moment to let ownership changes propagate and stragglers sync
			ScriptMgr::Yield(50ms);

			// pass 3: handle stubborn networked peds by sending explicit remove to all tokens
			for (auto ped : Pools::GetPeds())
			{
				if (!ped.IsPlayer() && ped.IsNetworked())
				{
					if (ped.HasControl())
					{
						ped.Delete();
					}
					else
					{
						// ask engine to broadcast one more time
						ped.ForceControl();
						if (ped.HasControl())
						{
							ped.Delete();
						}
						else
						{
							// fallback: explicit network remove with unknown token
							auto net = ped.GetNetworkObject();
							if (net)
								Network::ForceRemoveNetworkEntity(net->m_ObjectId, -1);
						}
					}
				}
			}

			// final small wait to flush removals
			ScriptMgr::Yield(20ms);
		}
	};

	class DeleteAllVehs : public Command
	{
		using Command::Command;

		virtual void OnCall() override
		{
			for (auto veh : Pools::GetVehicles())
			{
				if (veh)
				{
					veh.ForceControl();
					if (veh.HasControl())
					{
						veh.ForceSync();
						veh.Delete();
					}
					else
					{
						auto net = veh.GetNetworkObject();
						if (net)
							Network::ForceRemoveNetworkEntity(net->m_ObjectId, -1);
					}
				}
			}
			ScriptMgr::Yield(20ms);
		}
	};

	static DeleteAllObjects _DeleteAllObjects{"delobjs", "Delete All Objects", "Deletes all objects in the game world, including mission critical objects"};
	static DeleteAllPeds _DeleteAllPeds{"delpeds", "Delete All Peds", "Deletes all peds in the game world, including mission critical peds"};
	static DeleteAllVehs _DeleteAllVehs{"delvehs", "Delete All Vehicles", "Deletes all vehicles in the game world, including mission critical vehicles"};
}