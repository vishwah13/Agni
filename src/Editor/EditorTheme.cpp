#include <Editor/EditorTheme.hpp>
#include <fmt/core.h>

namespace agni
{
namespace editor
{

void ApplyDarkModernTheme(const ThemeConfig& config)
{
    ImGuiStyle& style = ImGui::GetStyle();

    // ========================================================================
    // Sizing and Spacing
    // ========================================================================

    style.WindowPadding     = config.windowPadding;
    style.FramePadding      = config.framePadding;
    style.ItemSpacing       = config.itemSpacing;
    style.ItemInnerSpacing  = config.itemInnerSpacing;
    style.CellPadding       = config.cellPadding;
    style.IndentSpacing     = config.indentSpacing;
    style.ScrollbarSize     = config.scrollbarSize;
    style.GrabMinSize       = config.grabMinSize;

    // ========================================================================
    // Rounding
    // ========================================================================

    style.WindowRounding    = config.windowRounding;
    style.ChildRounding     = config.childRounding;
    style.FrameRounding     = config.frameRounding;
    style.PopupRounding     = config.popupRounding;
    style.ScrollbarRounding = config.scrollbarRounding;
    style.GrabRounding      = config.grabRounding;
    style.TabRounding       = config.tabRounding;

    // ========================================================================
    // Borders
    // ========================================================================

    style.WindowBorderSize  = config.windowBorderSize;
    style.ChildBorderSize   = config.childBorderSize;
    style.FrameBorderSize   = config.frameBorderSize;
    style.PopupBorderSize   = config.popupBorderSize;
    style.TabBorderSize     = config.tabBorderSize;

    // ========================================================================
    // Alignment
    // ========================================================================

    style.WindowTitleAlign        = ImVec2(0.0f, 0.5f);
    style.WindowMenuButtonPosition= ImGuiDir_Left;
    style.ColorButtonPosition     = ImGuiDir_Right;
    style.ButtonTextAlign         = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign     = ImVec2(0.0f, 0.5f);

    // ========================================================================
    // Anti-aliasing
    // ========================================================================

    style.AntiAliasedLines       = true;
    style.AntiAliasedLinesUseTex = true;
    style.AntiAliasedFill        = true;

    // ========================================================================
    // Colors
    // ========================================================================

    ImVec4* c = style.Colors;

    // Text
    c[ImGuiCol_Text]                  = colors::Text;
    c[ImGuiCol_TextDisabled]          = colors::TextDisabled;

    // Backgrounds
    c[ImGuiCol_WindowBg]              = colors::Background;
    c[ImGuiCol_ChildBg]               = colors::BackgroundDark;
    c[ImGuiCol_PopupBg]               = colors::BackgroundLight;
    c[ImGuiCol_MenuBarBg]             = colors::BackgroundDark;

    // Borders
    c[ImGuiCol_Border]                = colors::Border;
    c[ImGuiCol_BorderShadow]          = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    // Frames (input fields, checkboxes, etc.)
    c[ImGuiCol_FrameBg]               = colors::Surface;
    c[ImGuiCol_FrameBgHovered]        = colors::SurfaceHover;
    c[ImGuiCol_FrameBgActive]         = colors::SurfaceActive;

    // Title bar
    c[ImGuiCol_TitleBg]               = colors::BackgroundDark;
    c[ImGuiCol_TitleBgActive]         = colors::BackgroundDark;
    c[ImGuiCol_TitleBgCollapsed]      = colors::BackgroundDark;

    // Scrollbars
    c[ImGuiCol_ScrollbarBg]           = colors::ScrollbarBg;
    c[ImGuiCol_ScrollbarGrab]         = colors::ScrollbarGrab;
    c[ImGuiCol_ScrollbarGrabHovered]  = colors::ScrollbarGrabHover;
    c[ImGuiCol_ScrollbarGrabActive]   = colors::ScrollbarGrabActive;

    // Interactive elements
    c[ImGuiCol_CheckMark]             = colors::Accent;
    c[ImGuiCol_SliderGrab]            = colors::Accent;
    c[ImGuiCol_SliderGrabActive]      = colors::AccentActive;

    // Buttons
    c[ImGuiCol_Button]                = colors::Button;
    c[ImGuiCol_ButtonHovered]         = colors::ButtonHover;
    c[ImGuiCol_ButtonActive]          = colors::ButtonActive;

    // Headers (collapsing headers, tree nodes, selectables)
    c[ImGuiCol_Header]                = colors::Header;
    c[ImGuiCol_HeaderHovered]         = colors::HeaderHover;
    c[ImGuiCol_HeaderActive]          = colors::HeaderActive;

    // Separators
    c[ImGuiCol_Separator]             = colors::Border;
    c[ImGuiCol_SeparatorHovered]      = colors::Accent;
    c[ImGuiCol_SeparatorActive]       = colors::AccentActive;

    // Resize grips
    c[ImGuiCol_ResizeGrip]            = colors::Surface;
    c[ImGuiCol_ResizeGripHovered]     = colors::Accent;
    c[ImGuiCol_ResizeGripActive]      = colors::AccentActive;

    // Tabs
    c[ImGuiCol_Tab]                   = colors::Tab;
    c[ImGuiCol_TabHovered]            = colors::TabHovered;
    c[ImGuiCol_TabSelected]           = colors::TabSelected;
    c[ImGuiCol_TabSelectedOverline]   = colors::TabSelectedOverline;
    c[ImGuiCol_TabDimmed]             = colors::Tab;
    c[ImGuiCol_TabDimmedSelected]     = colors::TabSelected;
    c[ImGuiCol_TabDimmedSelectedOverline] = colors::Border;

    // Docking
    c[ImGuiCol_DockingPreview]        = colors::DockingPreview;
    c[ImGuiCol_DockingEmptyBg]        = colors::BackgroundDark;

    // Plot
    c[ImGuiCol_PlotLines]             = colors::Accent;
    c[ImGuiCol_PlotLinesHovered]      = colors::AccentHover;
    c[ImGuiCol_PlotHistogram]         = colors::Accent;
    c[ImGuiCol_PlotHistogramHovered]  = colors::AccentHover;

    // Tables
    c[ImGuiCol_TableHeaderBg]         = colors::Header;
    c[ImGuiCol_TableBorderStrong]     = colors::Border;
    c[ImGuiCol_TableBorderLight]      = colors::Border;
    c[ImGuiCol_TableRowBg]            = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_TableRowBgAlt]         = ImVec4(1.0f, 1.0f, 1.0f, 0.02f);

    // Drag and drop
    c[ImGuiCol_DragDropTarget]        = colors::Accent;

    // Navigation
    c[ImGuiCol_NavCursor]             = colors::Accent;
    c[ImGuiCol_NavWindowingHighlight] = colors::Accent;
    c[ImGuiCol_NavWindowingDimBg]     = colors::ModalDimBg;

    // Modal
    c[ImGuiCol_ModalWindowDimBg]      = colors::ModalDimBg;

    // Text selection
    c[ImGuiCol_TextSelectedBg]        = colors::Selection;

    fmt::print("[EditorTheme] Dark modern theme applied\n");
}

void ConfigureFonts(ImGuiIO& io, const ThemeConfig& config)
{
    // Try to load Roboto-Medium from ImGui's bundled fonts
    const char* fontPath = config.fontPath;
    if (fontPath == nullptr)
    {
        fontPath = "../../third_party/imgui/misc/fonts/Roboto-Medium.ttf";
    }

    // Attempt to load the font
    ImFont* font = io.Fonts->AddFontFromFileTTF(fontPath, config.fontSize);
    if (font == nullptr)
    {
        fmt::print("[EditorTheme] Could not load font '{}', using default\n", fontPath);
        io.Fonts->AddFontDefault();
    }
    else
    {
        fmt::print("[EditorTheme] Loaded font: {} at {}px\n", fontPath, config.fontSize);
    }

    // Note: Don't call io.Fonts->Build() - the backend handles this automatically
}

} // namespace editor
} // namespace agni
