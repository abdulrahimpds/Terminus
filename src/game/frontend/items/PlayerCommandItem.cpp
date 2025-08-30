#include "Items.hpp"
#include "game/commands/PlayerCommand.hpp"
#include "core/commands/Commands.hpp"
#include "game/backend/FiberPool.hpp"
#include "game/backend/Players.hpp"

namespace YimMenu
{
	PlayerCommandItem::PlayerCommandItem(joaat_t id, std::optional<std::string> label_override) :
	    m_Command(Commands::GetCommand<PlayerCommand>(id)),
		m_LabelOverride(label_override)
	{
	}

	void PlayerCommandItem::Draw()
	{
		if (!m_Command)
		{
			ImGui::Text("Unknown!");
			return;
		}

		if (ImGui::Button(m_LabelOverride.has_value() ? m_LabelOverride.value().data() : m_Command->GetLabel().data()))
		{
			auto cmd = m_Command; // capture by value to avoid dangling 'this'
			FiberPool::Push([cmd] {
				if (cmd && Players::GetSelected().IsValid())
					cmd->Call(Players::GetSelected());
			});
		}

		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip(m_Command->GetDescription().data());
			if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
			{
				auto windowLabel = std::format("{} Hotkey", m_Command->GetLabel());
				ImGui::OpenPopup(windowLabel.c_str());
			}
		}

		auto windowLabel = std::format("{} Hotkey", m_Command->GetLabel());
		ImGui::SetNextWindowSize(ImVec2(500, 120));
		if (ImGui::BeginPopupModal(windowLabel.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar))
		{
			ImGui::BulletText("Hold the command name clicked to change its hotkey");
			ImGui::BulletText("Press any registered key to remove");
			ImGui::Separator();
			HotkeySetter(m_Command->GetHash()).Draw();
			ImGui::Spacing();
			if (ImGui::Button("Close") || ((!ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered()) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}
}