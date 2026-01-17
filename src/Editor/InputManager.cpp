#include <Editor/InputManager.hpp>
#include <imgui.h>

namespace agni
{
namespace editor
{

bool InputManager::processEvent(const SDL_Event& e)
{
	// Track modifier keys
	if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP)
	{
		SDL_Keymod mods = SDL_GetModState();
		m_ctrlPressed  = (mods & SDL_KMOD_CTRL) != 0;
		m_shiftPressed = (mods & SDL_KMOD_SHIFT) != 0;
		m_altPressed   = (mods & SDL_KMOD_ALT) != 0;
	}

	// Only process key down events for shortcuts
	if (e.type == SDL_EVENT_KEY_DOWN)
	{
		// Don't consume input if ImGui wants it (typing in text field, etc.)
		if (ImGui::GetIO().WantCaptureKeyboard)
			return false;

		// Check if this key combo matches any registered shortcut
		KeyShortcut pressed;
		pressed.key   = e.key.key;
		pressed.ctrl  = m_ctrlPressed;
		pressed.shift = m_shiftPressed;
		pressed.alt   = m_altPressed;

		auto it = m_shortcuts.find(pressed);
		if (it != m_shortcuts.end())
		{
			// Execute the callback
			it->second();
			return true; // Consumed
		}
	}

	return false; // Not consumed
}

void InputManager::registerShortcut(const KeyShortcut& shortcut, std::function<void()> callback)
{
	m_shortcuts[shortcut] = callback;
}

void InputManager::clearShortcuts()
{
	m_shortcuts.clear();
}

void InputManager::update()
{
	// Clear per-frame state if needed
	m_keysPressed.clear();
}

} // namespace editor
} // namespace agni
