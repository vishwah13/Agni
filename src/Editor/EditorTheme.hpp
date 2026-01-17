#pragma once

#include <imgui.h>

namespace agni
{
namespace editor
{

// ============================================================================
// Color Palette - Dark Modern Theme (Unreal/Unity inspired)
// ============================================================================

namespace colors
{
    // Base colors (dark charcoal backgrounds)
    constexpr ImVec4 Background         = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);  // #1A1A1C
    constexpr ImVec4 BackgroundDark     = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);  // #141415
    constexpr ImVec4 BackgroundLight    = ImVec4(0.14f, 0.14f, 0.15f, 1.00f);  // #242426

    // Surface colors (panels, frames)
    constexpr ImVec4 Surface            = ImVec4(0.18f, 0.18f, 0.19f, 1.00f);  // #2E2E31
    constexpr ImVec4 SurfaceHover       = ImVec4(0.22f, 0.22f, 0.24f, 1.00f);  // #38383D
    constexpr ImVec4 SurfaceActive      = ImVec4(0.26f, 0.26f, 0.28f, 1.00f);  // #424247

    // Border colors
    constexpr ImVec4 Border             = ImVec4(0.24f, 0.24f, 0.26f, 1.00f);  // #3D3D42
    constexpr ImVec4 BorderLight        = ImVec4(0.30f, 0.30f, 0.32f, 1.00f);  // #4D4D52

    // Text colors
    constexpr ImVec4 Text               = ImVec4(0.92f, 0.92f, 0.93f, 1.00f);  // #EBEBED
    constexpr ImVec4 TextDim            = ImVec4(0.60f, 0.60f, 0.62f, 1.00f);  // #99999E
    constexpr ImVec4 TextDisabled       = ImVec4(0.40f, 0.40f, 0.42f, 1.00f);  // #66666B

    // Accent colors (blue)
    constexpr ImVec4 Accent             = ImVec4(0.10f, 0.46f, 0.82f, 1.00f);  // #1A75D1
    constexpr ImVec4 AccentHover        = ImVec4(0.15f, 0.52f, 0.90f, 1.00f);  // #2685E6
    constexpr ImVec4 AccentActive       = ImVec4(0.08f, 0.40f, 0.72f, 1.00f);  // #1466B8
    constexpr ImVec4 AccentDim          = ImVec4(0.10f, 0.46f, 0.82f, 0.50f);  // #1A75D1 @ 50%

    // Header colors (collapsing headers, tree nodes)
    constexpr ImVec4 Header             = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);  // #333338
    constexpr ImVec4 HeaderHover        = ImVec4(0.26f, 0.26f, 0.28f, 1.00f);  // #424247
    constexpr ImVec4 HeaderActive       = ImVec4(0.10f, 0.46f, 0.82f, 0.80f);  // Accent @ 80%

    // Tab colors
    constexpr ImVec4 Tab                = ImVec4(0.12f, 0.12f, 0.13f, 1.00f);  // #1F1F21
    constexpr ImVec4 TabHovered         = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);  // #333338
    constexpr ImVec4 TabSelected        = ImVec4(0.18f, 0.18f, 0.19f, 1.00f);  // #2E2E31
    constexpr ImVec4 TabSelectedOverline= ImVec4(0.10f, 0.46f, 0.82f, 1.00f);  // Accent

    // Scrollbar colors
    constexpr ImVec4 ScrollbarBg        = ImVec4(0.10f, 0.10f, 0.11f, 0.50f);
    constexpr ImVec4 ScrollbarGrab      = ImVec4(0.30f, 0.30f, 0.32f, 1.00f);
    constexpr ImVec4 ScrollbarGrabHover = ImVec4(0.40f, 0.40f, 0.42f, 1.00f);
    constexpr ImVec4 ScrollbarGrabActive= ImVec4(0.50f, 0.50f, 0.52f, 1.00f);

    // Button colors
    constexpr ImVec4 Button             = ImVec4(0.22f, 0.22f, 0.24f, 1.00f);
    constexpr ImVec4 ButtonHover        = ImVec4(0.28f, 0.28f, 0.30f, 1.00f);
    constexpr ImVec4 ButtonActive       = ImVec4(0.10f, 0.46f, 0.82f, 1.00f);

    // Semantic colors
    constexpr ImVec4 Success            = ImVec4(0.26f, 0.72f, 0.36f, 1.00f);  // Green
    constexpr ImVec4 Warning            = ImVec4(0.92f, 0.68f, 0.20f, 1.00f);  // Orange
    constexpr ImVec4 Error              = ImVec4(0.86f, 0.26f, 0.30f, 1.00f);  // Red
    constexpr ImVec4 Info               = ImVec4(0.30f, 0.60f, 0.90f, 1.00f);  // Light Blue

    // Special purpose
    constexpr ImVec4 Selection          = ImVec4(0.10f, 0.46f, 0.82f, 0.35f);
    constexpr ImVec4 ModalDimBg         = ImVec4(0.00f, 0.00f, 0.00f, 0.60f);
    constexpr ImVec4 DockingPreview     = ImVec4(0.10f, 0.46f, 0.82f, 0.70f);

    // Axis colors (for transform gizmos)
    constexpr ImVec4 AxisX              = ImVec4(0.86f, 0.26f, 0.30f, 1.00f);  // Red
    constexpr ImVec4 AxisY              = ImVec4(0.26f, 0.72f, 0.36f, 1.00f);  // Green
    constexpr ImVec4 AxisZ              = ImVec4(0.30f, 0.60f, 0.90f, 1.00f);  // Blue
    constexpr ImVec4 AxisXHover         = ImVec4(0.96f, 0.36f, 0.40f, 1.00f);
    constexpr ImVec4 AxisYHover         = ImVec4(0.36f, 0.82f, 0.46f, 1.00f);
    constexpr ImVec4 AxisZHover         = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);
}

// ============================================================================
// Theme Configuration
// ============================================================================

struct ThemeConfig
{
    // Rounding
    float windowRounding    = 4.0f;
    float frameRounding     = 3.0f;
    float popupRounding     = 4.0f;
    float scrollbarRounding = 6.0f;
    float grabRounding      = 3.0f;
    float tabRounding       = 4.0f;
    float childRounding     = 4.0f;

    // Borders
    float windowBorderSize  = 1.0f;
    float frameBorderSize   = 0.0f;
    float popupBorderSize   = 1.0f;
    float tabBorderSize     = 0.0f;
    float childBorderSize   = 1.0f;

    // Padding & Spacing
    ImVec2 windowPadding    = ImVec2(10.0f, 10.0f);
    ImVec2 framePadding     = ImVec2(8.0f, 4.0f);
    ImVec2 itemSpacing      = ImVec2(8.0f, 6.0f);
    ImVec2 itemInnerSpacing = ImVec2(6.0f, 4.0f);
    ImVec2 cellPadding      = ImVec2(6.0f, 4.0f);

    // Sizes
    float scrollbarSize     = 14.0f;
    float grabMinSize       = 10.0f;
    float indentSpacing     = 20.0f;

    // Font
    float fontSize          = 15.0f;
    const char* fontPath    = nullptr;  // nullptr = use default
};

// ============================================================================
// Theme Functions
// ============================================================================

// Apply the dark modern theme to ImGui
void ApplyDarkModernTheme(const ThemeConfig& config = ThemeConfig{});

// Configure fonts (call before backend init, after CreateContext)
void ConfigureFonts(ImGuiIO& io, const ThemeConfig& config = ThemeConfig{});

} // namespace editor
} // namespace agni
