#include "game/backend/ScriptMgr.hpp"
#include "game/backend/Self.hpp"
#include "game/backend/Players.hpp"
#include "game/commands/PlayerCommand.hpp"
#include "game/rdr/Natives.hpp"
#include "core/commands/LoopedCommand.hpp"
#include "core/frontend/Notifications.hpp"
#include "AttachmentTracker.hpp"

namespace YimMenu::Features
{
	// global variables to track attachment state
	int g_AttachedToPlayerHandle = 0;
	int g_AttachedToPlayerId = -1;

	// attachment types enum for position/rotation system
	enum class AttachmentType
	{
		None,
		Attach,
		Spank,
		RideOnShoulders,
		TouchPlayer,
		Slap
	};

	// structure to hold position and rotation settings for each attachment type
	struct AttachmentSettings
	{
		float posX, posY, posZ;
		float rotX, rotZ; // vertical and horizontal rotation
	};

	// default settings for each attachment type
	static std::map<AttachmentType, AttachmentSettings> g_AttachmentDefaults = {
		{AttachmentType::Attach,         {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}},
		{AttachmentType::Spank,          {0.0f, -0.8f, -0.5f, 15.0f, -30.0f}},
		{AttachmentType::RideOnShoulders, {0.0f, 0.0f, 0.7f, 0.0f, 10.0f}},
		{AttachmentType::TouchPlayer,    {0.0f, 0.8f, 0.0f, -8.0f, 150.0f}},
		{AttachmentType::Slap,           {0.0f, 0.6f, 0.0f, 0.0f, 180.0f}}
	};

	// current settings (can be modified by user)
	static std::map<AttachmentType, AttachmentSettings> g_AttachmentSettings = g_AttachmentDefaults;

	// currently active attachment type
	static AttachmentType g_ActiveAttachmentType = AttachmentType::None;

	// current UI display values (what user sees in the controls)
	static float g_DisplayPosX = 0.0f;
	static float g_DisplayPosY = 0.0f;
	static float g_DisplayPosZ = 0.0f;
	static float g_DisplayRotX = 0.0f;
	static float g_DisplayRotZ = 0.0f;
	static void ClearPedTasks(int ped)
	{
		PED::SET_PED_SHOULD_PLAY_IMMEDIATE_SCENARIO_EXIT(ped);
		TASK::CLEAR_PED_TASKS_IMMEDIATELY(ped, true, true);
		ScriptMgr::Yield(50ms);
	}

	static void PlayAnim(std::string anim_dict, std::string anim_name)
	{
		while (!STREAMING::HAS_ANIM_DICT_LOADED(anim_dict.c_str()))
		{
			STREAMING::REQUEST_ANIM_DICT(anim_dict.c_str());
			ScriptMgr::Yield();
		}

		TASK::TASK_PLAY_ANIM(Self::GetPed().GetHandle(), anim_dict.c_str(), anim_name.c_str(), 1.0f, 1.0f, -1, 1, 0.0f, false, 0, false, "", false);
	}

	// helper function to load settings into display variables
	static void LoadSettingsForType(AttachmentType type)
	{
		if (g_AttachmentSettings.find(type) != g_AttachmentSettings.end())
		{
			auto& settings = g_AttachmentSettings[type];
			g_DisplayPosX = settings.posX;
			g_DisplayPosY = settings.posY;
			g_DisplayPosZ = settings.posZ;
			g_DisplayRotX = settings.rotX;
			g_DisplayRotZ = settings.rotZ;
		}
	}

	// helper function to save display variables to settings
	static void SaveSettingsForType(AttachmentType type)
	{
		if (g_AttachmentSettings.find(type) != g_AttachmentSettings.end())
		{
			auto& settings = g_AttachmentSettings[type];
			settings.posX = g_DisplayPosX;
			settings.posY = g_DisplayPosY;
			settings.posZ = g_DisplayPosZ;
			settings.rotX = g_DisplayRotX;
			settings.rotZ = g_DisplayRotZ;
		}
	}

	static void AttachToEntity(int target, float PosX, float PosY, float PosZ, float RotX, float RotY, float RotZ)
	{
		ENTITY::ATTACH_ENTITY_TO_ENTITY(Self::GetPed().GetHandle(), target, 0, PosX, PosY, PosZ, RotX, RotY, RotZ, false, false, false, true, 0, true, false, false);

		// track attachment state for monitoring system
		g_AttachedToPlayerHandle = target;
		g_AttachedToPlayerId = -1;

		// find player id from handle for tracking
		for (auto& [id, player] : Players::GetPlayers())
		{
			if (player.GetPed().IsValid() && player.GetPed().GetHandle() == target)
			{
				g_AttachedToPlayerId = id;
				break;
			}
		}
	}

	// helper function to attach with current settings and switch attachment type
	static void AttachToPlayerWithCurrentSettings(Player player, AttachmentType type)
	{
		if (player.GetPed().IsValid())
		{
			// save current display settings before switching
			if (g_ActiveAttachmentType != AttachmentType::None)
			{
				SaveSettingsForType(g_ActiveAttachmentType);
			}

			// switch to new attachment type and load its settings
			g_ActiveAttachmentType = type;
			LoadSettingsForType(type);

			// don't automatically clear animations - let user control via Reset to Default
			// attach with the loaded settings using existing AttachToEntity function
			AttachToEntity(player.GetPed().GetHandle(), g_DisplayPosX, g_DisplayPosY, g_DisplayPosZ, g_DisplayRotX, 0.0f, g_DisplayRotZ);
		}
	}

	// helper function to update attachment position with current display values
	static void UpdateCurrentAttachmentPosition()
	{
		if (g_ActiveAttachmentType != AttachmentType::None && ENTITY::IS_ENTITY_ATTACHED_TO_ANY_PED(Self::GetPed().GetHandle()))
		{
			// save current settings
			SaveSettingsForType(g_ActiveAttachmentType);

			// get the entity we're attached to
			int attachedTo = ENTITY::GET_ENTITY_ATTACHED_TO(Self::GetPed().GetHandle());
			if (attachedTo != 0)
			{
				// detach and reattach with new position
				ENTITY::DETACH_ENTITY(Self::GetPed().GetHandle(), false, false);
				AttachToEntity(attachedTo, g_DisplayPosX, g_DisplayPosY, g_DisplayPosZ, g_DisplayRotX, 0.0f, g_DisplayRotZ);
			}
		}
	}

	// public functions for UI access
	namespace AttachmentUI
	{
		// get current attachment type name for display
		const char* GetActiveAttachmentTypeName()
		{
			switch (g_ActiveAttachmentType)
			{
				case AttachmentType::Attach: return "Attach";
				case AttachmentType::Spank: return "Spank";
				case AttachmentType::RideOnShoulders: return "Ride on Shoulders";
				case AttachmentType::TouchPlayer: return "Touch Player";
				case AttachmentType::Slap: return "Slap";
				default: return "None";
			}
		}

		// check if any attachment is active
		bool IsAttachmentActive()
		{
			return g_ActiveAttachmentType != AttachmentType::None;
		}

		// get/set display position values
		float& GetDisplayPosX() { return g_DisplayPosX; }
		float& GetDisplayPosY() { return g_DisplayPosY; }
		float& GetDisplayPosZ() { return g_DisplayPosZ; }
		float& GetDisplayRotX() { return g_DisplayRotX; }
		float& GetDisplayRotZ() { return g_DisplayRotZ; }

		// update attachment position (call after changing display values)
		void UpdatePosition()
		{
			UpdateCurrentAttachmentPosition();
		}

		// reset current attachment to default values
		void ResetToDefault()
		{
			if (g_ActiveAttachmentType != AttachmentType::None && g_AttachmentDefaults.find(g_ActiveAttachmentType) != g_AttachmentDefaults.end())
			{
				auto& defaults = g_AttachmentDefaults[g_ActiveAttachmentType];
				g_DisplayPosX = defaults.posX;
				g_DisplayPosY = defaults.posY;
				g_DisplayPosZ = defaults.posZ;
				g_DisplayRotX = defaults.rotX;
				g_DisplayRotZ = defaults.rotZ;

				// if resetting to Attach type, clear any existing animations
				if (g_ActiveAttachmentType == AttachmentType::Attach)
				{
					ClearPedTasks(Self::GetPed().GetHandle());
				}

				UpdateCurrentAttachmentPosition();
			}
		}
	}

	// new attach command with adjustable position/rotation
	class Attach : public PlayerCommand
	{
		using PlayerCommand::PlayerCommand;

		virtual void OnCall(Player player) override
		{
			if (player.GetPed().IsValid())
			{
				ClearPedTasks(player.GetPed().GetHandle());
				// don't clear self animations - allow custom animations from Self menu
				AttachToPlayerWithCurrentSettings(player, AttachmentType::Attach);
			}
		}
	};

	class Spank : public PlayerCommand
	{
		using PlayerCommand::PlayerCommand;

		virtual void OnCall(Player player) override
		{
			if (player.GetPed().IsValid())
			{
				ClearPedTasks(player.GetPed().GetHandle());
				AttachToPlayerWithCurrentSettings(player, AttachmentType::Spank);
				PlayAnim("script_re@peep_tom@spanking_cowboy", "spanking_idle_female");
			}
		}
	};

	class RideOnShoulders : public PlayerCommand
	{
		using PlayerCommand::PlayerCommand;

		virtual void OnCall(Player player) override
		{
			if (player.GetPed().IsValid())
			{
				ClearPedTasks(player.GetPed().GetHandle());
				AttachToPlayerWithCurrentSettings(player, AttachmentType::RideOnShoulders);
				PlayAnim("script_story@gry1@ig@ig_15_archibald_sit_reading", "held_1hand_archibald");
			}
		}
	};

	class TouchPlayer : public PlayerCommand
	{
		using PlayerCommand::PlayerCommand;

		virtual void OnCall(Player player) override
		{
			if (player.GetPed().IsValid())
			{
				ClearPedTasks(player.GetPed().GetHandle());
				AttachToPlayerWithCurrentSettings(player, AttachmentType::TouchPlayer);
				PlayAnim("script_re@unpaid_debt@paid", "slap_loop_attacker");
			}
		}
	};

	class Slap : public PlayerCommand
	{
		using PlayerCommand::PlayerCommand;

		virtual void OnCall(Player player) override
		{
			if (player.GetPed().IsValid())
			{
				ClearPedTasks(player.GetPed().GetHandle());
				AttachToPlayerWithCurrentSettings(player, AttachmentType::Slap);
				PlayAnim("mech_melee@unarmed@posse@ambient@healthy@noncombat", "slap_back_drunk_vs_drunk_light_v1_att");
			}
		}
	};

	class CancelAttachment : public Command
	{
		using Command::Command;

		virtual void OnCall() override
		{
			if (ENTITY::IS_ENTITY_ATTACHED_TO_ANY_PED(Self::GetPed().GetHandle()))
				ENTITY::DETACH_ENTITY(Self::GetPed().GetHandle(), true, true);
			ClearPedTasks(Self::GetPed().GetHandle());

			// clear attachment tracking state
			g_AttachedToPlayerHandle = 0;
			g_AttachedToPlayerId = -1;
			g_ActiveAttachmentType = AttachmentType::None;
		}
	};

	// attachment monitoring system to prevent invisibility issues
	class AttachmentMonitor : public LoopedCommand
	{
		using LoopedCommand::LoopedCommand;

		virtual void OnTick() override
		{
			// only monitor if we're attached to someone
			if (g_AttachedToPlayerId == -1 || g_AttachedToPlayerHandle == 0)
				return;

			auto selfPed = Self::GetPed();
			if (!selfPed.IsValid())
				return;

			// check if we're still attached
			if (!ENTITY::IS_ENTITY_ATTACHED_TO_ANY_PED(selfPed.GetHandle()))
			{
				g_AttachedToPlayerHandle = 0;
				g_AttachedToPlayerId = -1;
				return;
			}

			// check if the player we're attached to still exists and is valid
			bool playerStillValid = false;
			for (auto& [id, player] : Players::GetPlayers())
			{
				if (id == g_AttachedToPlayerId && player.IsValid() && player.GetPed().IsValid())
				{
					if (player.GetPed().GetHandle() == g_AttachedToPlayerHandle)
					{
						playerStillValid = true;
						break;
					}
				}
			}

			// if player is no longer valid or left the session, detach and refresh
			if (!playerStillValid)
			{
				ENTITY::DETACH_ENTITY(selfPed.GetHandle(), true, true);
				ClearPedTasks(selfPed.GetHandle());

				g_AttachedToPlayerHandle = 0;
				g_AttachedToPlayerId = -1;

				RefreshPlayerModel();

				Notifications::Show("Attachment", "automatically detached due to player leaving session", NotificationType::Info);
			}
		}

	private:
		void RefreshPlayerModel()
		{
			auto selfPlayer = Self::GetPlayer();
			auto selfPed = Self::GetPed();

			if (!selfPlayer.IsValid() || !selfPed.IsValid())
				return;

			auto currentModel = selfPed.GetModel();

			// force visibility refresh to fix local rendering
			selfPed.SetVisible(false);
			ScriptMgr::Yield(50ms);
			selfPed.SetVisible(true);

			// force network sync refresh
			if (selfPed.IsNetworked())
			{
				selfPed.ForceSync();
			}

			// additional refresh by reapplying current model
			if (STREAMING::HAS_MODEL_LOADED(currentModel))
			{
				PLAYER::SET_PLAYER_MODEL(selfPlayer.GetId(), currentModel, false);
				Self::Update();
			}
		}
	};

	static Attach _Attach{"attach", "Attach", "Attaches to target player with adjustable position/rotation"};
	static Spank _Spank{"spank", "Spank", "Spanks the target player"};
	static RideOnShoulders _RideOnShoulders{"rideonshoulders", "Ride on Shoulders", "Ride on a players shoulders"};
	static TouchPlayer _TouchPlayer{"touchplayer", "Touch Player", "Touches the other player..."};
	static Slap _Slap{"slap", "Slap", "Slaps the player"};
	static CancelAttachment _CancelAttachment{"cancelattachment", "Cancel Attachment", "Cancels current attachment"};
	static AttachmentMonitor _AttachmentMonitor{"attachmentmonitor", "Attachment Monitor", "automatically handles attachment cleanup to prevent invisibility issues"};
}