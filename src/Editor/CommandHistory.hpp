#pragma once

#include <memory>
#include <string>
#include <vector>

namespace agni::editor
{

// ============================================================================
// ICommand - Base interface for undoable commands
// ============================================================================

class ICommand
{
public:
	virtual ~ICommand() = default;

	// Execute the command (called on first execution and redo)
	virtual void execute() = 0;

	// Undo the command
	virtual void undo() = 0;

	// Get a description of the command (for UI display)
	virtual std::string getDescription() const = 0;
};

// ============================================================================
// CommandHistory - Manages undo/redo stack
// ============================================================================

class CommandHistory
{
public:
	CommandHistory() = default;

	// Execute a command and add it to the history
	void execute(std::unique_ptr<ICommand> command);

	// Undo the last command
	void undo();

	// Redo the last undone command
	void redo();

	// Check if undo/redo is available
	bool canUndo() const { return !m_undoStack.empty(); }
	bool canRedo() const { return !m_redoStack.empty(); }

	// Get descriptions for UI
	std::string getUndoDescription() const;
	std::string getRedoDescription() const;

	// Clear all history
	void clear();

	// Get history sizes (for debugging/UI)
	size_t getUndoCount() const { return m_undoStack.size(); }
	size_t getRedoCount() const { return m_redoStack.size(); }

	// Set maximum history size (0 = unlimited)
	void setMaxHistory(size_t maxHistory) { m_maxHistory = maxHistory; }

private:
	std::vector<std::unique_ptr<ICommand>> m_undoStack;
	std::vector<std::unique_ptr<ICommand>> m_redoStack;
	size_t m_maxHistory = 100;
};

} // namespace agni::editor
