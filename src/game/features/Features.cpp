#include "Features.hpp"

#include "core/commands/BoolCommand.hpp"
#include "core/commands/Commands.hpp"
#include "core/commands/HotkeySystem.hpp"
#include "core/frontend/Notifications.hpp"
#include "game/backend/FiberPool.hpp"
#include "game/backend/Players.hpp"
#include "game/backend/ScriptMgr.hpp"
#include "game/frontend/ContextMenu.hpp"
#include "game/frontend/GUI.hpp"
#include "game/rdr/Enums.hpp"
#include "game/rdr/Natives.hpp"
#include "game/backend/Self.hpp"
#include "game/backend/CrashSignatures.hpp"

namespace YimMenu
{
	void SpectateTick()
	{
		// comprehensive spectate crash protection for hidden/invisible modders
		if (g_SpectateId != Players::GetSelected().GetId() && g_Spectating)
		{
			// aggressive pre-validation: check for entity flooding attacks (handle 4 spam protection)
			static int consecutiveInvalidChecks = 0;

			// validate selected player before spectating
			if (!Players::GetSelected().IsValid())
			{
				consecutiveInvalidChecks++;
				if (consecutiveInvalidChecks > 5)
				{
					LOG(WARNING) << "SpectateTick: Detected entity flooding attack - disabling spectate";
					Notifications::Show("Protections", "Blocked spectate crash attempt - entity flooding detected", NotificationType::Warning);
					g_Spectating = false;
					consecutiveInvalidChecks = 0;
					return;
				}
				LOG(WARNING) << "SpectateTick: Selected player is invalid - stopping spectate";
				g_Spectating = false;
				return;
			}
			consecutiveInvalidChecks = 0;

			auto selectedPed = Players::GetSelected().GetPed();
			if (!selectedPed.IsValid())
			{
				LOG(WARNING) << "SpectateTick: Selected player ped is invalid - stopping spectate";
				g_Spectating = false;
				return;
			}

			// validate ped pointer against crash signatures and attack patterns
			auto pedPtr = selectedPed.GetPointer<void*>();
			if (CrashSignatures::IsKnownCrashPointerForEntities(pedPtr))
			{
				LOG(WARNING) << "SpectateTick: Blocked crash signature or attack pattern in ped pointer - stopping spectate";
				Notifications::Show("Protections", "Blocked spectate crash attempt from corrupted entity", NotificationType::Warning);
				g_Spectating = false;
				return;
			}

			// get handle with exception protection
			int pedHandle = 0;
			try
			{
				pedHandle = selectedPed.GetHandle();
			}
			catch (...)
			{
				LOG(WARNING) << "SpectateTick: Exception getting ped handle - crash attempt blocked";
				Notifications::Show("Protections", "Blocked spectate crash attempt from corrupted entity", NotificationType::Warning);
				g_Spectating = false;
				return;
			}

			// validate handle against crash signatures and attack patterns
			if (CrashSignatures::IsKnownCrashHandle(pedHandle))
			{
				LOG(WARNING) << "SpectateTick: Blocked crash signature handle - stopping spectate";
				Notifications::Show("Protections", "Blocked spectate crash attempt from invalid handle", NotificationType::Warning);
				g_Spectating = false;
				return;
			}

			g_SpectateId = Players::GetSelected().GetId();

			// aggressive entity existence validation before native calls
			try
			{
				if (!ENTITY::DOES_ENTITY_EXIST(pedHandle))
				{
					LOG(WARNING) << "SpectateTick: Entity does not exist - stopping spectate";
					Notifications::Show("Protections", "Blocked spectate crash attempt - target entity missing", NotificationType::Warning);
					g_Spectating = false;
					return;
				}
			}
			catch (...)
			{
				LOG(WARNING) << "SpectateTick: Exception checking entity existence - crash attempt blocked";
				Notifications::Show("Protections", "Blocked spectate crash attempt during entity check", NotificationType::Warning);
				g_Spectating = false;
				return;
			}

			// protected spectate mode activation
			try
			{
				NETWORK::NETWORK_SET_IN_SPECTATOR_MODE(true, pedHandle);
			}
			catch (...)
			{
				LOG(WARNING) << "SpectateTick: Exception setting spectator mode - crash attempt blocked";
				Notifications::Show("Protections", "Blocked spectate crash attempt during mode activation", NotificationType::Warning);
				g_Spectating = false;
				return;
			}
		}

		if (g_Spectating && g_Running)
		{
			if (!Players::GetSelected().IsValid() || !Players::GetSelected().GetPed())
			{
				STREAMING::CLEAR_FOCUS();
				NETWORK::NETWORK_SET_IN_SPECTATOR_MODE(false, Self::GetPed().GetHandle());
				g_Spectating = false;
				return;
			}

			// re-validate player and ped during spectate loop (protection against dynamic corruption)
			auto selectedPlayer = Players::GetSelected();
			if (!selectedPlayer.IsValid())
			{
				LOG(WARNING) << "SpectateTick: Player became invalid during spectate - stopping";
				STREAMING::CLEAR_FOCUS();
				NETWORK::NETWORK_SET_IN_SPECTATOR_MODE(false, Self::GetPed().GetHandle());
				g_Spectating = false;
				return;
			}

			auto selectedPed = selectedPlayer.GetPed();
			if (!selectedPed.IsValid())
			{
				LOG(WARNING) << "SpectateTick: Player ped became invalid during spectate - stopping";
				STREAMING::CLEAR_FOCUS();
				NETWORK::NETWORK_SET_IN_SPECTATOR_MODE(false, Self::GetPed().GetHandle());
				g_Spectating = false;
				return;
			}

			// get handle with protection
			int playerPed = 0;
			try
			{
				playerPed = selectedPed.GetHandle();
			}
			catch (...)
			{
				LOG(WARNING) << "SpectateTick: Exception getting handle during spectate - stopping";
				STREAMING::CLEAR_FOCUS();
				NETWORK::NETWORK_SET_IN_SPECTATOR_MODE(false, Self::GetPed().GetHandle());
				g_Spectating = false;
				return;
			}

			// aggressive entity validation before streaming operations (major crash point)
			try
			{
				// double-check entity existence before streaming operations
				if (!ENTITY::DOES_ENTITY_EXIST(playerPed))
				{
					LOG(WARNING) << "SpectateTick: Entity disappeared during spectate - stopping";
					Notifications::Show("Protections", "Blocked spectate crash attempt - entity vanished", NotificationType::Warning);
					STREAMING::CLEAR_FOCUS();
					NETWORK::NETWORK_SET_IN_SPECTATOR_MODE(false, Self::GetPed().GetHandle());
					g_Spectating = false;
					return;
				}

				// validate entity is not corrupted before focus operations
				if (CrashSignatures::IsKnownCrashHandle(playerPed))
				{
					LOG(WARNING) << "SpectateTick: Detected crash signature entity during spectate - stopping";
					Notifications::Show("Protections", "Blocked spectate crash attempt - corrupted entity detected", NotificationType::Warning);
					STREAMING::CLEAR_FOCUS();
					NETWORK::NETWORK_SET_IN_SPECTATOR_MODE(false, Self::GetPed().GetHandle());
					g_Spectating = false;
					return;
				}
			}
			catch (...)
			{
				LOG(WARNING) << "SpectateTick: Exception during entity validation - crash attempt blocked";
				Notifications::Show("Protections", "Blocked spectate crash attempt during validation", NotificationType::Warning);
				STREAMING::CLEAR_FOCUS();
				NETWORK::NETWORK_SET_IN_SPECTATOR_MODE(false, Self::GetPed().GetHandle());
				g_Spectating = false;
				return;
			}

			// protected entity focus (major crash point for hidden modders)
			try
			{
				if (!STREAMING::IS_ENTITY_FOCUS(playerPed))
					STREAMING::SET_FOCUS_ENTITY(playerPed);
			}
			catch (...)
			{
				LOG(WARNING) << "SpectateTick: Exception setting entity focus - crash attempt blocked";
				Notifications::Show("Protections", "Blocked spectate crash attempt during entity focus", NotificationType::Warning);
				STREAMING::CLEAR_FOCUS();
				NETWORK::NETWORK_SET_IN_SPECTATOR_MODE(false, Self::GetPed().GetHandle());
				g_Spectating = false;
				return;
			}

			CAM::_FORCE_LETTER_BOX_THIS_UPDATE();
			CAM::_DISABLE_CINEMATIC_MODE_THIS_FRAME();

			// protected spectator mode check and activation
			try
			{
				if (!NETWORK::NETWORK_IS_IN_SPECTATOR_MODE() && ENTITY::DOES_ENTITY_EXIST(playerPed))
				{
					NETWORK::NETWORK_SET_IN_SPECTATOR_MODE(true, playerPed);
				}
			}
			catch (...)
			{
				LOG(WARNING) << "SpectateTick: Exception in spectator mode check - crash attempt blocked";
				Notifications::Show("Protections", "Blocked spectate crash attempt during mode check", NotificationType::Warning);
				STREAMING::CLEAR_FOCUS();
				NETWORK::NETWORK_SET_IN_SPECTATOR_MODE(false, Self::GetPed().GetHandle());
				g_Spectating = false;
				return;
			}
		}
		else
		{
			if (NETWORK::NETWORK_IS_IN_SPECTATOR_MODE())
			{
				STREAMING::CLEAR_FOCUS();
				NETWORK::NETWORK_SET_IN_SPECTATOR_MODE(false, Self::GetPed().GetHandle());
				CAM::_FORCE_LETTER_BOX_THIS_UPDATE();
				CAM::_DISABLE_CINEMATIC_MODE_THIS_FRAME();
			}
		}
	}

	void FeatureLoop()
	{
		Commands::EnableBoolCommands();
		while (true)
		{
			SpectateTick();
			*Pointers.RageSecurityInitialized = false;
			if (g_Running)
			{
				*Pointers.ExplosionBypass = true;
				Commands::RunLoopedCommands();
				if (GetForegroundWindow() == *Pointers.Hwnd && !HUD::IS_PAUSE_MENU_ACTIVE() && !GUI::IsOpen())
					g_HotkeySystem.Update();
				Self::Update();
			}
			ScriptMgr::Yield();
		}
	}

	void BlockControlsForUI()
	{
		while (g_Running)
		{
			if (GUI::IsOpen())
			{
				if (GUI::IsUsingKeyboard())
				{
					PAD::DISABLE_ALL_CONTROL_ACTIONS(0);
				}
				else
				{
					PAD::DISABLE_CONTROL_ACTION(0, (Hash)NativeInputs::INPUT_LOOK_LR, 1);
					PAD::DISABLE_CONTROL_ACTION(0, (Hash)NativeInputs::INPUT_LOOK_UD, 1);
					PAD::DISABLE_CONTROL_ACTION(0, (Hash)NativeInputs::INPUT_AIM, 1);
					PAD::DISABLE_CONTROL_ACTION(0, (Hash)NativeInputs::INPUT_MELEE_ATTACK, 1);
					PAD::DISABLE_CONTROL_ACTION(0, (Hash)NativeInputs::INPUT_VEH_DRIVE_LOOK, 1);
					PAD::DISABLE_CONTROL_ACTION(0, (Hash)NativeInputs::INPUT_VEH_AIM, 1);
					PAD::DISABLE_CONTROL_ACTION(0, (Hash)NativeInputs::INPUT_VEH_ATTACK, 1);
					PAD::DISABLE_CONTROL_ACTION(0, (Hash)NativeInputs::INPUT_VEH_ATTACK2, 1);
					PAD::DISABLE_CONTROL_ACTION(0, (Hash)NativeInputs::INPUT_HORSE_AIM, 1);
					PAD::DISABLE_CONTROL_ACTION(0, (Hash)NativeInputs::INPUT_HORSE_ATTACK, 1);
					PAD::DISABLE_CONTROL_ACTION(0, (Hash)NativeInputs::INPUT_HORSE_ATTACK2, 1);
					PAD::DISABLE_CONTROL_ACTION(0, (Hash)NativeInputs::INPUT_HORSE_GUN_LR, 1);
					PAD::DISABLE_CONTROL_ACTION(0, (Hash)NativeInputs::INPUT_HORSE_GUN_UD, 1);
					PAD::DISABLE_CONTROL_ACTION(0, (Hash)NativeInputs::INPUT_VEH_DRIVE_LOOK2, 1);
					PAD::DISABLE_CONTROL_ACTION(0, (Hash)NativeInputs::INPUT_ATTACK, 1);
					PAD::DISABLE_CONTROL_ACTION(0, (Hash)NativeInputs::INPUT_ATTACK2, 1);
					PAD::DISABLE_CONTROL_ACTION(0, (Hash)NativeInputs::INPUT_NEXT_WEAPON, 1);
					PAD::DISABLE_CONTROL_ACTION(0, (Hash)NativeInputs::INPUT_PREV_WEAPON, 1);
					PAD::DISABLE_CONTROL_ACTION(0, (Hash)NativeInputs::INPUT_VEH_CAR_AIM, 1);
					PAD::DISABLE_CONTROL_ACTION(0, (Hash)NativeInputs::INPUT_VEH_CAR_ATTACK, 1);
					PAD::DISABLE_CONTROL_ACTION(0, (Hash)NativeInputs::INPUT_VEH_CAR_ATTACK2, 1);
					PAD::DISABLE_CONTROL_ACTION(0, (Hash)NativeInputs::INPUT_VEH_CAR_ATTACK2, 1);
					PAD::DISABLE_CONTROL_ACTION(0, (Hash)NativeInputs::INPUT_VEH_BOAT_AIM, 1);
					PAD::DISABLE_CONTROL_ACTION(0, (Hash)NativeInputs::INPUT_VEH_BOAT_ATTACK, 1);
					PAD::DISABLE_CONTROL_ACTION(0, (Hash)NativeInputs::INPUT_VEH_BOAT_ATTACK2, 1);
				}
			}

			ScriptMgr::Yield();
		}
	}

	void ContextMenuTick()
	{
		while (g_Running)
		{
			ContextMenu::GameTick();
			ScriptMgr::Yield();
		}
	}
}
