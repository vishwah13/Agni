#pragma once

#include <imgui.h>

namespace agni
{
namespace editor
{
namespace widgets
{

// ============================================================================
// Section Headers
// ============================================================================

// Creates a styled section header with optional icon
void SectionHeader(const char* label, const char* icon = nullptr);

// Creates a collapsible section with styling
bool CollapsibleSection(const char* label, const char* icon = nullptr,
                        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen);

// ============================================================================
// Property Widgets (label on left, control on right)
// ============================================================================

// Vec3 editor with per-axis coloring (X=red, Y=green, Z=blue)
bool PropertyVec3(const char* label, float* values, float resetValue = 0.0f,
                  float dragSpeed = 0.1f, float labelWidth = 100.0f);

// Float property with label
bool PropertyFloat(const char* label, float* value, float min = 0.0f,
                   float max = 0.0f, const char* format = "%.3f",
                   float labelWidth = 100.0f);

// Int property with label
bool PropertyInt(const char* label, int* value, int min = 0, int max = 0,
                 float labelWidth = 100.0f);

// Checkbox property with label
bool PropertyCheckbox(const char* label, bool* value, float labelWidth = 100.0f);

// Color property with label
bool PropertyColor3(const char* label, float* color, float labelWidth = 100.0f);

// Dropdown/Combo property with label
bool PropertyCombo(const char* label, int* currentItem, const char* const* items,
                   int itemCount, float labelWidth = 100.0f);

// ============================================================================
// Styled Buttons
// ============================================================================

// Primary action button (accent colored)
bool ButtonPrimary(const char* label, const ImVec2& size = ImVec2(0, 0));

// Secondary action button (neutral)
bool ButtonSecondary(const char* label, const ImVec2& size = ImVec2(0, 0));

// Danger button (red, for destructive actions)
bool ButtonDanger(const char* label, const ImVec2& size = ImVec2(0, 0));

// Toggle button (highlighted when active)
bool ButtonToggle(const char* label, bool active, const ImVec2& size = ImVec2(0, 0));

// ============================================================================
// Info Displays
// ============================================================================

// Styled info row (label: value)
void InfoRow(const char* label, const char* format, ...);

// Styled stat display
void StatDisplay(const char* label, const char* value, const char* icon = nullptr);

// ============================================================================
// Separators and Spacing
// ============================================================================

// Horizontal separator with label
void SeparatorText(const char* label);

// Vertical spacing
void Spacing(float height = 8.0f);

// ============================================================================
// Tooltips
// ============================================================================

// Show tooltip for previous item if hovered
void TooltipOnHover(const char* description);

// Help marker (?) with tooltip on hover
void HelpMarker(const char* description);

} // namespace widgets
} // namespace editor
} // namespace agni
