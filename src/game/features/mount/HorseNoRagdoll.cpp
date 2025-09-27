#include "core/commands/LoopedCommand.hpp"
#include "game/backend/Self.hpp"
#include "game/rdr/Enums.hpp"
#include "game/rdr/Natives.hpp"

namespace YimMenu::Features
{
	class HorseNoRagdoll : public LoopedCommand
	{
		using LoopedCommand::LoopedCommand;

		virtual void OnTick() override
		{
			auto mount = Self::GetMount();
			if (mount && mount.HasControl())
				mount.SetRagdoll(false);
		}

		virtual void OnDisable() override
		{
			auto mount = Self::GetMount();
			if (mount && mount.HasControl())
				mount.SetRagdoll(true);
		}
	};

	static HorseNoRagdoll _HorseNoRagdoll{"horsenoragdoll", "No Ragdoll", "Your horse will never ragdoll"};
}