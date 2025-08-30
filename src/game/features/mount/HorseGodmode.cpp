#include "core/commands/LoopedCommand.hpp"
#include "game/backend/Self.hpp"
#include "game/backend/ScriptMgr.hpp"
#include "game/rdr/Natives.hpp"
#include <optional>

namespace YimMenu::Features
{
	class HorseGodmode : public LoopedCommand
	{
		using LoopedCommand::LoopedCommand;

		// Store handles instead of Ped objects
		int m_CurrentMountHandle = 0;
		int m_LastMountHandle = 0;

		// Bitset for all proofs (bullet, fire, explosion, etc.)
		// 0xFF is a common value for all proofs enabled
		static constexpr int ALL_PROOFS_BITSET = 0xFF;

		// Helper function to ensure we have control of an entity
		bool EnsureEntityControl(int entityHandle)
		{
			if (entityHandle == 0 || !ENTITY::DOES_ENTITY_EXIST(entityHandle))
				return false;

			if (!NETWORK::NETWORK_HAS_CONTROL_OF_ENTITY(entityHandle))
			{
				NETWORK::NETWORK_REQUEST_CONTROL_OF_ENTITY(entityHandle);
				
				// Wait up to 10 frames for control
				for (int i = 0; i < 10 && !NETWORK::NETWORK_HAS_CONTROL_OF_ENTITY(entityHandle); i++)
				{
					ScriptMgr::Yield();
				}
			}
			
			return NETWORK::NETWORK_HAS_CONTROL_OF_ENTITY(entityHandle);
		}

		virtual void OnTick() override
		{
			// Check for current mount (horse being ridden)
			Ped currentMount = Self::GetMount();
			if (currentMount && currentMount.IsValid())
			{
				int handle = currentMount.GetHandle();
				if (EnsureEntityControl(handle))
				{
					// Apply invincibility
					currentMount.SetInvincible(true);
					// Apply additional proofs for extra protection
					ENTITY::SET_ENTITY_PROOFS(handle, ALL_PROOFS_BITSET, true);
					// Store handle
					m_CurrentMountHandle = handle;
				}
			}

			// Check for player's last mount (main horse)
			if (Self::GetPed())
			{
				Ped lastMount = Self::GetPed().GetLastMount();
				if (lastMount && lastMount.IsValid() && !lastMount.IsDead())
				{
					// Apply invincibility
					lastMount.SetInvincible(true);
					// Apply additional proofs for extra protection
					ENTITY::SET_ENTITY_PROOFS(lastMount.GetHandle(), ALL_PROOFS_BITSET, true);
					// Store handle
					m_LastMountHandle = lastMount.GetHandle();
				}
			}

			// If we have a stored last mount handle and it still exists, make sure it's invincible
			if (m_LastMountHandle != 0 && ENTITY::DOES_ENTITY_EXIST(m_LastMountHandle))
			{
				// Apply invincibility
				ENTITY::SET_ENTITY_INVINCIBLE(m_LastMountHandle, true);
				// Apply additional proofs for extra protection
				ENTITY::SET_ENTITY_PROOFS(m_LastMountHandle, ALL_PROOFS_BITSET, true);
			}
		}

        virtual void OnDisable() override
        {
			// Disable invincibility for current mount handle
			if (m_CurrentMountHandle != 0 && ENTITY::DOES_ENTITY_EXIST(m_CurrentMountHandle))
			{
				ENTITY::SET_ENTITY_INVINCIBLE(m_CurrentMountHandle, false);
				// Reset proofs
				ENTITY::SET_ENTITY_PROOFS(m_CurrentMountHandle, 0, false);
				m_CurrentMountHandle = 0; // Clear handle
			}

			// Disable invincibility for last mount handle
			if (m_LastMountHandle != 0 && ENTITY::DOES_ENTITY_EXIST(m_LastMountHandle))
			{
				ENTITY::SET_ENTITY_INVINCIBLE(m_LastMountHandle, false);
				// Reset proofs
				ENTITY::SET_ENTITY_PROOFS(m_LastMountHandle, 0, false);
				m_LastMountHandle = 0; // Clear handle
			}

			// Also check current mount from Self just to be safe
			if (Self::GetMount())
			{
				Self::GetMount().SetInvincible(false);
				ENTITY::SET_ENTITY_PROOFS(Self::GetMount().GetHandle(), 0, false);
			}

			// Also check last mount from Self just to be safe
			if (Self::GetPed())
			{
				Ped lastMount = Self::GetPed().GetLastMount();
				if (lastMount && lastMount.IsValid())
				{
					lastMount.SetInvincible(false);
					ENTITY::SET_ENTITY_PROOFS(lastMount.GetHandle(), 0, false);
				}
			}
        }
	};

	static HorseGodmode _HorseGodmode{"horsegodmode", "Godmode", "Blocks all incoming damage for your horse"};
}
