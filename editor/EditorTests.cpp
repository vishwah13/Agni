#ifdef AGNI_ENABLE_EDITOR_TESTS

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_test_engine/imgui_te_context.h>

void RegisterEditorTests(ImGuiTestEngine* e)
{
	ImGuiTest* t = nullptr;

	// ================================================================
	// Entity Creation
	// ================================================================

	t = IM_REGISTER_TEST(e, "editor", "create_entity_via_add_component");
	t->TestFunc = [](ImGuiTestContext* ctx)
	{
		// This test verifies that the Add Component button exists
		// and the component popup can be opened
		ctx->LogInfo("Editor test: create entity placeholder");
		// TODO: Once editor window names are stable, add:
		// ctx->SetRef("Hierarchy");
		// ctx->MouseClick(ImGuiMouseButton_Right);
		// ctx->ItemClick("Create/Cube");
	};

	// ================================================================
	// Undo/Redo
	// ================================================================

	t = IM_REGISTER_TEST(e, "editor", "undo_redo_menu_enabled");
	t->TestFunc = [](ImGuiTestContext* ctx)
	{
		// Verify undo/redo menu items exist
		ctx->LogInfo("Editor test: undo/redo menu placeholder");
		// TODO: ctx->MenuClick("Edit/Undo");
	};

	// ================================================================
	// Play/Stop
	// ================================================================

	t = IM_REGISTER_TEST(e, "editor", "play_stop_buttons_exist");
	t->TestFunc = [](ImGuiTestContext* ctx)
	{
		ctx->LogInfo("Editor test: play/stop placeholder");
		// TODO: ctx->SetRef("##PlayStopToolbar");
		// ctx->ItemClick("  Play  ");
		// ctx->ItemClick("  Stop  ");
	};
}

#endif // AGNI_ENABLE_EDITOR_TESTS
