#pragma once

namespace agni
{
namespace editor
{

// ============================================================================
// Icons - Simple text-based icons for UI elements
// Using basic ASCII for maximum compatibility
// ============================================================================

namespace icons
{
    // Component Icons
    constexpr const char* Transform     = "[T]";
    constexpr const char* Mesh          = "[M]";
    constexpr const char* Light         = "[L]";
    constexpr const char* Camera        = "[C]";
    constexpr const char* Physics       = "[P]";
    constexpr const char* Collider      = "[O]";
    constexpr const char* SceneNode     = "[N]";

    // Gizmo Mode Icons
    constexpr const char* Translate     = "T";
    constexpr const char* Rotate        = "R";
    constexpr const char* Scale         = "S";
    constexpr const char* Local         = "L";
    constexpr const char* World         = "W";

    // Actions
    constexpr const char* Add           = "+";
    constexpr const char* Remove        = "-";
    constexpr const char* Delete        = "X";
    constexpr const char* Edit          = "*";
    constexpr const char* Settings      = "#";
    constexpr const char* Refresh       = "@";

    // Status
    constexpr const char* Check         = "v";
    constexpr const char* Cross         = "x";
    constexpr const char* Warning       = "!";
    constexpr const char* Info          = "i";

    // Visibility
    constexpr const char* Visible       = "o";
    constexpr const char* Hidden        = "-";

    // Hierarchy
    constexpr const char* Expanded      = "v";
    constexpr const char* Collapsed     = ">";
    constexpr const char* Entity        = "*";

    // Rendering
    constexpr const char* Quality       = "[Q]";
    constexpr const char* Shadows       = "[S]";
}

} // namespace editor
} // namespace agni
