#include <Editor/EditorWidgets.hpp>
#include <Editor/EditorTheme.hpp>
#include <cstdarg>
#include <cstdio>

namespace agni
{
namespace editor
{
namespace widgets
{

// ============================================================================
// Section Headers
// ============================================================================

void SectionHeader(const char* label, const char* icon)
{
    ImGui::PushStyleColor(ImGuiCol_Text, colors::Text);

    if (icon)
    {
        ImGui::Text("%s  %s", icon, label);
    }
    else
    {
        ImGui::Text("%s", label);
    }

    ImGui::PopStyleColor();
    ImGui::Separator();
}

bool CollapsibleSection(const char* label, const char* icon, ImGuiTreeNodeFlags flags)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 6));

    char buffer[256];
    if (icon)
    {
        snprintf(buffer, sizeof(buffer), "%s  %s", icon, label);
    }
    else
    {
        snprintf(buffer, sizeof(buffer), "%s", label);
    }

    bool open = ImGui::CollapsingHeader(buffer, flags);

    ImGui::PopStyleVar();
    return open;
}

// ============================================================================
// Property Widgets
// ============================================================================

bool PropertyVec3(const char* label, float* values, float resetValue,
                  float dragSpeed, float labelWidth)
{
    ImGui::PushID(label);

    bool changed = false;

    // Label
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, colors::TextDim);
    ImGui::Text("%s", label);
    ImGui::PopStyleColor();

    ImGui::SameLine(labelWidth);

    // Calculate width for each component
    float lineWidth = ImGui::GetContentRegionAvail().x;
    float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    float buttonWidth = ImGui::GetFrameHeight();
    float inputWidth = (lineWidth - buttonWidth * 3 - spacing * 6) / 3.0f;

    if (inputWidth < 30.0f) inputWidth = 30.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, 0));

    // X Component (Red)
    ImGui::PushStyleColor(ImGuiCol_Button, colors::AxisX);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors::AxisXHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, colors::AxisX);
    if (ImGui::Button("X##btn_x", ImVec2(buttonWidth, 0)))
    {
        values[0] = resetValue;
        changed = true;
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(inputWidth);
    if (ImGui::DragFloat("##val_x", &values[0], dragSpeed, 0.0f, 0.0f, "%.2f"))
    {
        changed = true;
    }

    // Y Component (Green)
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, colors::AxisY);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors::AxisYHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, colors::AxisY);
    if (ImGui::Button("Y##btn_y", ImVec2(buttonWidth, 0)))
    {
        values[1] = resetValue;
        changed = true;
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(inputWidth);
    if (ImGui::DragFloat("##val_y", &values[1], dragSpeed, 0.0f, 0.0f, "%.2f"))
    {
        changed = true;
    }

    // Z Component (Blue)
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, colors::AxisZ);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors::AxisZHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, colors::AxisZ);
    if (ImGui::Button("Z##btn_z", ImVec2(buttonWidth, 0)))
    {
        values[2] = resetValue;
        changed = true;
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(inputWidth);
    if (ImGui::DragFloat("##val_z", &values[2], dragSpeed, 0.0f, 0.0f, "%.2f"))
    {
        changed = true;
    }

    ImGui::PopStyleVar();
    ImGui::PopID();

    return changed;
}

bool PropertyFloat(const char* label, float* value, float min, float max,
                   const char* format, float labelWidth)
{
    ImGui::PushID(label);

    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, colors::TextDim);
    ImGui::Text("%s", label);
    ImGui::PopStyleColor();

    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);

    bool changed = false;
    // Always use DragFloat — supports Ctrl+Click to type precise values
    changed = ImGui::DragFloat("##value", value, 0.1f, min, max, format,
                               (min != 0.0f || max != 0.0f) ? ImGuiSliderFlags_AlwaysClamp : 0);

    ImGui::PopID();
    return changed;
}

bool PropertyInt(const char* label, int* value, int min, int max, float labelWidth)
{
    ImGui::PushID(label);

    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, colors::TextDim);
    ImGui::Text("%s", label);
    ImGui::PopStyleColor();

    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);

    bool changed = false;
    // Always use DragInt — supports Ctrl+Click to type precise values
    changed = ImGui::DragInt("##value", value, 1.0f, min, max, "%d",
                             (min != 0 || max != 0) ? ImGuiSliderFlags_AlwaysClamp : 0);

    ImGui::PopID();
    return changed;
}

bool PropertyCheckbox(const char* label, bool* value, float labelWidth)
{
    ImGui::PushID(label);

    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, colors::TextDim);
    ImGui::Text("%s", label);
    ImGui::PopStyleColor();

    ImGui::SameLine(labelWidth);

    bool changed = ImGui::Checkbox("##value", value);

    ImGui::PopID();
    return changed;
}

bool PropertyColor3(const char* label, float* color, float labelWidth)
{
    ImGui::PushID(label);

    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, colors::TextDim);
    ImGui::Text("%s", label);
    ImGui::PopStyleColor();

    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);

    bool changed = ImGui::ColorEdit3("##color", color);

    ImGui::PopID();
    return changed;
}

bool PropertyCombo(const char* label, int* currentItem, const char* const* items,
                   int itemCount, float labelWidth)
{
    ImGui::PushID(label);

    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, colors::TextDim);
    ImGui::Text("%s", label);
    ImGui::PopStyleColor();

    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);

    bool changed = ImGui::Combo("##combo", currentItem, items, itemCount);

    ImGui::PopID();
    return changed;
}

// ============================================================================
// Styled Buttons
// ============================================================================

bool ButtonPrimary(const char* label, const ImVec2& size)
{
    ImGui::PushStyleColor(ImGuiCol_Button, colors::Accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors::AccentHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, colors::AccentActive);

    bool clicked = ImGui::Button(label, size);

    ImGui::PopStyleColor(3);
    return clicked;
}

bool ButtonSecondary(const char* label, const ImVec2& size)
{
    ImGui::PushStyleColor(ImGuiCol_Button, colors::Surface);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors::SurfaceHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, colors::SurfaceActive);

    bool clicked = ImGui::Button(label, size);

    ImGui::PopStyleColor(3);
    return clicked;
}

bool ButtonDanger(const char* label, const ImVec2& size)
{
    ImGui::PushStyleColor(ImGuiCol_Button, colors::Error);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.96f, 0.36f, 0.40f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.76f, 0.16f, 0.20f, 1.0f));

    bool clicked = ImGui::Button(label, size);

    ImGui::PopStyleColor(3);
    return clicked;
}

bool ButtonToggle(const char* label, bool active, const ImVec2& size)
{
    if (active)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, colors::Accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors::AccentHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, colors::AccentActive);
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Button, colors::Surface);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors::SurfaceHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, colors::SurfaceActive);
    }

    bool clicked = ImGui::Button(label, size);

    ImGui::PopStyleColor(3);
    return clicked;
}

// ============================================================================
// Info Displays
// ============================================================================

void InfoRow(const char* label, const char* format, ...)
{
    ImGui::PushStyleColor(ImGuiCol_Text, colors::TextDim);
    ImGui::Text("%s:", label);
    ImGui::PopStyleColor();

    ImGui::SameLine();

    va_list args;
    va_start(args, format);
    ImGui::TextV(format, args);
    va_end(args);
}

void StatDisplay(const char* label, const char* value, const char* icon)
{
    if (icon)
    {
        ImGui::Text("%s", icon);
        ImGui::SameLine();
    }

    ImGui::PushStyleColor(ImGuiCol_Text, colors::TextDim);
    ImGui::Text("%s:", label);
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::Text("%s", value);
}

// ============================================================================
// Separators and Spacing
// ============================================================================

void SeparatorText(const char* label)
{
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, colors::TextDim);
    ImGui::SeparatorText(label);
    ImGui::PopStyleColor();
}

void Spacing(float height)
{
    ImGui::Dummy(ImVec2(0, height));
}

// ============================================================================
// Tooltips
// ============================================================================

void TooltipOnHover(const char* description)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    {
        ImGui::SetTooltip("%s", description);
    }
}

void HelpMarker(const char* description)
{
    ImGui::PushStyleColor(ImGuiCol_Text, colors::TextDim);
    ImGui::Text("(?)");
    ImGui::PopStyleColor();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(description);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

} // namespace widgets
} // namespace editor
} // namespace agni
