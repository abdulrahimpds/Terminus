#pragma once
#include "core/settings/IStateSerializer.hpp"
#include <map>
#include <string>
#include <vector>
#include <optional>


namespace YimMenu
{
	struct CommandLink
	{
	public:
		std::optional<std::string> m_Label; // optional cached label for display when command is unavailable

		std::vector<int> m_Chain{};
		bool m_BeingModified = false;

		CommandLink(){};
	};

	class HotkeySystem :
		private IStateSerializer
	{
		std::chrono::system_clock::time_point m_LastHotkeyTriggerTime;

	public:
		HotkeySystem();

		std::map<uint32_t, CommandLink> m_CommandHotkeys;
		void RegisterCommands();
		bool ListenAndApply(int& Hotkey, std::vector<int> blacklist = {0});
		std::string GetHotkeyLabel(int hotkey_modifiers);
		void CreateHotkey(std::vector<int>& Hotkey);

		void Update();
		// public way to mark hotkeys state dirty for persistence
		void MarkHotkeysDirty();

		virtual void SaveStateImpl(nlohmann::json& state) override;
		virtual void LoadStateImpl(nlohmann::json& state) override;
	};

	inline HotkeySystem g_HotkeySystem;
}