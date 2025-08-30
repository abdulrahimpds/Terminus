#include "core/commands/LoopedCommand.hpp"
#include "game/rdr/Natives.hpp"
#include "game/rdr/Enums.hpp"
#include "game/backend/Self.hpp"
#include "game/backend/CrashSignatures.hpp"

namespace YimMenu::Features
{
	class AntiLasso : public LoopedCommand
	{
		using LoopedCommand::LoopedCommand;

		virtual void OnTick() override
		{
			// validate self ped before use (exact crash location from .map analysis)
			auto selfPed = Self::GetPed();
			if (!selfPed.IsValid())
			{
				LOG(WARNING) << "AntiLasso: Self ped is invalid - skipping lasso protection";
				return;
			}

			// validate ped pointer against crash signatures and attack patterns
			auto pedPtr = selfPed.GetPointer<void*>();
			if (CrashSignatures::IsKnownCrashPointerForEntities(pedPtr))
			{
				LOG(WARNING) << "AntiLasso: Blocked crash signature or attack pattern in ped pointer";
				return;
			}

			// get handle once and validate
			int pedHandle = 0;
			try
			{
				pedHandle = selfPed.GetHandle();
			}
			catch (...)
			{
				LOG(WARNING) << "AntiLasso: Exception getting ped handle - crash attempt blocked";
				return;
			}

			// validate handle before use
			if (pedHandle == 0 || !ENTITY::DOES_ENTITY_EXIST(pedHandle))
			{
				LOG(WARNING) << "AntiLasso: Invalid ped handle " << pedHandle << " - skipping lasso protection";
				return;
			}

			// wrap native calls in exception handling
			try
			{
				PED::SET_PED_LASSO_HOGTIE_FLAG(pedHandle, (int)LassoFlags::LHF_CAN_BE_LASSOED, false);
				PED::SET_PED_LASSO_HOGTIE_FLAG(pedHandle, (int)LassoFlags::LHF_CAN_BE_LASSOED_BY_FRIENDLY_AI, false);
				PED::SET_PED_LASSO_HOGTIE_FLAG(pedHandle, (int)LassoFlags::LHF_CAN_BE_LASSOED_BY_FRIENDLY_PLAYERS, false);
				PED::SET_PED_LASSO_HOGTIE_FLAG(pedHandle, (int)LassoFlags::LHF_DISABLE_IN_MP, true);
			}
			catch (...)
			{
				LOG(WARNING) << "AntiLasso: Exception in lasso flag operations - crash attempt blocked";
			}
		}

		virtual void OnDisable() override
		{
			// same protection for OnDisable
			auto selfPed = Self::GetPed();
			if (!selfPed.IsValid())
			{
				LOG(WARNING) << "AntiLasso: Self ped is invalid - skipping lasso restore";
				return;
			}

			// get handle safely
			int pedHandle = 0;
			try
			{
				pedHandle = selfPed.GetHandle();
			}
			catch (...)
			{
				LOG(WARNING) << "AntiLasso: Exception getting ped handle in OnDisable - crash attempt blocked";
				return;
			}

			if (pedHandle == 0 || !ENTITY::DOES_ENTITY_EXIST(pedHandle))
			{
				LOG(WARNING) << "AntiLasso: Invalid ped handle in OnDisable - skipping lasso restore";
				return;
			}

			// restore lasso flags safely
			try
			{
				PED::SET_PED_LASSO_HOGTIE_FLAG(pedHandle, (int)LassoFlags::LHF_CAN_BE_LASSOED, true);
				PED::SET_PED_LASSO_HOGTIE_FLAG(pedHandle, (int)LassoFlags::LHF_CAN_BE_LASSOED_BY_FRIENDLY_AI, true);
				PED::SET_PED_LASSO_HOGTIE_FLAG(pedHandle, (int)LassoFlags::LHF_CAN_BE_LASSOED_BY_FRIENDLY_PLAYERS, true);
				PED::SET_PED_LASSO_HOGTIE_FLAG(pedHandle, (int)LassoFlags::LHF_DISABLE_IN_MP, false);
			}
			catch (...)
			{
				LOG(WARNING) << "AntiLasso: Exception in lasso flag restore - crash attempt blocked";
			}
		}
	};

	static AntiLasso _AntiLasso{"antilasso", "Anti Lasso", "Disable getting lasso'd by other players"};
}