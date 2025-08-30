#include "Items.hpp"
#include "core/commands/Command.hpp"
#include "core/commands/Commands.hpp"
#include "core/commands/HotkeySystem.hpp"

namespace YimMenu
{
	HotkeySetter::HotkeySetter(joaat_t command_id) :
	    m_Id(command_id)
	{
	}

	void HotkeySetter::Draw()
	{
		auto Command = Commands::GetCommand(m_Id);

		if (!Command)
			ImGui::Text("Unkown Command");
		else
		{
			auto& link = g_HotkeySystem.m_CommandHotkeys[Command->GetHash()];

			ImGui::Button(Command->GetLabel().data());
			link.m_BeingModified = ImGui::IsItemActive();

			if (link.m_BeingModified)
			{
				g_HotkeySystem.CreateHotkey(link.m_Chain);
			}

				ImGui::SameLine(200);
				ImGui::BeginGroup();

				if (link.m_Chain.empty())
				{
					if (link.m_BeingModified)
						ImGui::Text("Press any button...");
					else
						ImGui::Text("No Hotkey Assigned");
				}
				else
				{
					ImGui::PushItemWidth(35);
					for (auto HotkeyModifier : link.m_Chain)
					{
						char KeyLabel[32];
						strcpy(KeyLabel, g_HotkeySystem.GetHotkeyLabel(HotkeyModifier).data());
						ImGui::InputText("##keylabel", KeyLabel, 32, ImGuiInputTextFlags_ReadOnly);
						if (ImGui::IsItemClicked())
						{
							std::erase_if(link.m_Chain, [HotkeyModifier](int i) { return i == HotkeyModifier; });
							g_HotkeySystem.MarkHotkeysDirty();
						}

						ImGui::SameLine();
					}
					ImGui::PopItemWidth();

					ImGui::SameLine();
					if (ImGui::Button("Reassign"))
					{
						link.m_Chain.clear();
						link.m_BeingModified = true; // capture next key(s)
						g_HotkeySystem.MarkHotkeysDirty();
					}
					ImGui::SameLine();
					if (ImGui::Button("Clear"))
					{
						link.m_Chain.clear();
						g_HotkeySystem.MarkHotkeysDirty();
					}
				}

				ImGui::EndGroup();
			}
	}
}