#pragma once

#include <SDL3/SDL_events.h>
#include <functional>
#include <vector>

namespace agni
{
namespace editor
{

// Keyboard shortcut definition
struct KeyShortcut
{
	SDL_Keycode key = 0;
	bool ctrl  = false;
	bool shift = false;
	bool alt   = false;

	bool operator==(const KeyShortcut& other) const
	{
		return key == other.key && ctrl == other.ctrl &&
		       shift == other.shift && alt == other.alt;
	}
};

// Hash function for KeyShortcut (for unordered_map)
struct KeyShortcutHash
{
	std::size_t operator()(const KeyShortcut& shortcut) const
	{
		std::size_t h1 = std::hash<int>{}(shortcut.key);
		std::size_t h2 = std::hash<bool>{}(shortcut.ctrl);
		std::size_t h3 = std::hash<bool>{}(shortcut.shift);
		std::size_t h4 = std::hash<bool>{}(shortcut.alt);
		return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3);
	}
};

// Input manager for editor shortcuts and keyboard handling
class InputManager
{
public:
	InputManager() = default;

	// Process SDL event and return true if consumed
	bool processEvent(const SDL_Event& e);

	// Register a keyboard shortcut callback
	void registerShortcut(const KeyShortcut& shortcut, std::function<void()> callback);

	// Clear all shortcuts
	void clearShortcuts();

	// Frame update (call after all events processed)
	void update();

private:
	std::unordered_map<KeyShortcut, std::function<void()>, KeyShortcutHash> m_shortcuts;

	// Track pressed keys this frame
	std::vector<SDL_Keycode> m_keysPressed;
	bool m_ctrlPressed  = false;
	bool m_shiftPressed = false;
	bool m_altPressed   = false;

	// Helper to check modifier state
	bool isModifierPressed(SDL_Keymod mod, SDL_Keymod check) const;
};

} // namespace editor
} // namespace agni
