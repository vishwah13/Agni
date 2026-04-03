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

	// Track right mouse button for fly mode
	if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT)
		m_rightMousePressed = true;
	if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_RIGHT)
		m_rightMousePressed = false;

	// Only process key down events for shortcuts
	if (e.type == SDL_EVENT_KEY_DOWN)
	{
		// Don't consume input if ImGui wants it (typing in text field, etc.)
		if (ImGui::GetIO().WantCaptureKeyboard)
			return false;

		// Don't fire plain-key shortcuts during fly mode (right mouse held)
		if (m_rightMousePressed && !m_ctrlPressed && !m_shiftPressed && !m_altPressed)
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
