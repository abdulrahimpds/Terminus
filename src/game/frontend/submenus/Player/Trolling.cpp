#include "Trolling.hpp"
#include "game/backend/FiberPool.hpp"
#include "game/features/players/toxic/AttachmentUI.hpp"

namespace YimMenu::Submenus
{
	std::shared_ptr<Category> BuildTrollingMenu()
	{
		auto menu = std::make_shared<Category>("Trolling");

		auto attachments = std::make_shared<Group>("Attachments");

		menu->AddItem(std::make_shared<PlayerCommandItem>("cageplayersmall"_J));
		menu->AddItem(std::make_shared<PlayerCommandItem>("cageplayerlarge"_J));
		menu->AddItem(std::make_shared<PlayerCommandItem>("cageplayercircus"_J));

		// add the new attach button
		attachments->AddItem(std::make_shared<PlayerCommandItem>("attach"_J));
		attachments->AddItem(std::make_shared<PlayerCommandItem>("spank"_J));
		attachments->AddItem(std::make_shared<PlayerCommandItem>("rideonshoulders"_J));
		attachments->AddItem(std::make_shared<PlayerCommandItem>("touchplayer"_J));
		attachments->AddItem(std::make_shared<PlayerCommandItem>("slap"_J));
		attachments->AddItem(std::make_shared<CommandItem>("cancelattachment"_J));

		// position and rotation adjustment system
		attachments->AddItem(std::make_shared<ImGuiItem>([] {
			using namespace YimMenu::Features::AttachmentUI;

			ImGui::Spacing();

			// show which attachment is currently active
			ImGui::Text("Active Attachment: %s", GetActiveAttachmentTypeName());
			ImGui::Spacing();

			// only show controls if an attachment is active
			if (IsAttachmentActive())
			{
				ImGui::Text("Position Adjustment:");
				ImGui::Spacing();

				// X axis controls
				if (ImGui::ArrowButton("##x_up", ImGuiDir_Up))
				{
					GetDisplayPosX() += 0.1f;
					FiberPool::Push([] { UpdatePosition(); });
				}
				ImGui::SameLine();
				if (ImGui::ArrowButton("##x_down", ImGuiDir_Down))
				{
					GetDisplayPosX() -= 0.1f;
					FiberPool::Push([] { UpdatePosition(); });
				}
				ImGui::SameLine();
				ImGui::SetNextItemWidth(80);
				if (ImGui::InputFloat("##x_input", &GetDisplayPosX(), 0.0f, 0.0f, "%.3f"))
				{
					FiberPool::Push([] { UpdatePosition(); });
				}
				ImGui::SameLine();
				ImGui::Text("X");

				// Y axis controls
				if (ImGui::ArrowButton("##y_up", ImGuiDir_Up))
				{
					GetDisplayPosY() += 0.1f;
					FiberPool::Push([] { UpdatePosition(); });
				}
				ImGui::SameLine();
				if (ImGui::ArrowButton("##y_down", ImGuiDir_Down))
				{
					GetDisplayPosY() -= 0.1f;
					FiberPool::Push([] { UpdatePosition(); });
				}
				ImGui::SameLine();
				ImGui::SetNextItemWidth(80);
				if (ImGui::InputFloat("##y_input", &GetDisplayPosY(), 0.0f, 0.0f, "%.3f"))
				{
					FiberPool::Push([] { UpdatePosition(); });
				}
				ImGui::SameLine();
				ImGui::Text("Y");

				// Z axis controls
				if (ImGui::ArrowButton("##z_up", ImGuiDir_Up))
				{
					GetDisplayPosZ() += 0.1f;
					FiberPool::Push([] { UpdatePosition(); });
				}
				ImGui::SameLine();
				if (ImGui::ArrowButton("##z_down", ImGuiDir_Down))
				{
					GetDisplayPosZ() -= 0.1f;
					FiberPool::Push([] { UpdatePosition(); });
				}
				ImGui::SameLine();
				ImGui::SetNextItemWidth(80);
				if (ImGui::InputFloat("##z_input", &GetDisplayPosZ(), 0.0f, 0.0f, "%.3f"))
				{
					FiberPool::Push([] { UpdatePosition(); });
				}
				ImGui::SameLine();
				ImGui::Text("Z");

				ImGui::Spacing();
				ImGui::Text("Rotation Adjustment:");
				ImGui::Spacing();

				// vertical rotation controls
				if (ImGui::ArrowButton("##rot_v_up", ImGuiDir_Up))
				{
					GetDisplayRotX() += 5.0f;
					FiberPool::Push([] { UpdatePosition(); });
				}
				ImGui::SameLine();
				if (ImGui::ArrowButton("##rot_v_down", ImGuiDir_Down))
				{
					GetDisplayRotX() -= 5.0f;
					FiberPool::Push([] { UpdatePosition(); });
				}
				ImGui::SameLine();
				ImGui::SetNextItemWidth(80);
				if (ImGui::InputFloat("##rot_v_input", &GetDisplayRotX(), 0.0f, 0.0f, "%.1f"))
				{
					FiberPool::Push([] { UpdatePosition(); });
				}
				ImGui::SameLine();
				ImGui::Text("Vertical");

				// horizontal rotation controls
				if (ImGui::ArrowButton("##rot_h_up", ImGuiDir_Up))
				{
					GetDisplayRotZ() += 5.0f;
					FiberPool::Push([] { UpdatePosition(); });
				}
				ImGui::SameLine();
				if (ImGui::ArrowButton("##rot_h_down", ImGuiDir_Down))
				{
					GetDisplayRotZ() -= 5.0f;
					FiberPool::Push([] { UpdatePosition(); });
				}
				ImGui::SameLine();
				ImGui::SetNextItemWidth(80);
				if (ImGui::InputFloat("##rot_h_input", &GetDisplayRotZ(), 0.0f, 0.0f, "%.1f"))
				{
					FiberPool::Push([] { UpdatePosition(); });
				}
				ImGui::SameLine();
				ImGui::Text("Horizontal");

				ImGui::Spacing();
				if (ImGui::Button("Reset to Default"))
				{
					FiberPool::Push([] { ResetToDefault(); });
				}
			}
			else
			{
				ImGui::Text("Use an attachment button to adjust position/rotation");
			}
		}));

		menu->AddItem(attachments);

		return menu;
	}
}