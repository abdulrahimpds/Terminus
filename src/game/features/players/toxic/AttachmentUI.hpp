#pragma once

namespace YimMenu::Features::AttachmentUI
{
	// get current attachment type name for display
	const char* GetActiveAttachmentTypeName();

	// check if any attachment is active
	bool IsAttachmentActive();

	// get/set display position values
	float& GetDisplayPosX();
	float& GetDisplayPosY();
	float& GetDisplayPosZ();
	float& GetDisplayRotX();
	float& GetDisplayRotZ();

	// update attachment position (call after changing display values)
	void UpdatePosition();

	// reset current attachment to default values
	void ResetToDefault();
}
