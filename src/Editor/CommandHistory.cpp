#include "CommandHistory.hpp"

namespace agni::editor
{

void CommandHistory::execute(std::unique_ptr<ICommand> command)
{
	if (!command)
		return;

	// Execute the command
	command->execute();

	// Add to undo stack
	m_undoStack.push_back(std::move(command));

	// Clear redo stack (new action invalidates redo history)
	m_redoStack.clear();

	// Enforce max history limit
	if (m_maxHistory > 0 && m_undoStack.size() > m_maxHistory)
	{
		m_undoStack.erase(m_undoStack.begin());
	}
}

void CommandHistory::undo()
{
	if (m_undoStack.empty())
		return;

	// Get the last command
	auto command = std::move(m_undoStack.back());
	m_undoStack.pop_back();

	// Undo it
	command->undo();

	// Move to redo stack
	m_redoStack.push_back(std::move(command));
}

void CommandHistory::redo()
{
	if (m_redoStack.empty())
		return;

	// Get the last undone command
	auto command = std::move(m_redoStack.back());
	m_redoStack.pop_back();

	// Re-execute it
	command->execute();

	// Move back to undo stack
	m_undoStack.push_back(std::move(command));
}

std::string CommandHistory::getUndoDescription() const
{
	if (m_undoStack.empty())
		return "";
	return m_undoStack.back()->getDescription();
}

std::string CommandHistory::getRedoDescription() const
{
	if (m_redoStack.empty())
		return "";
	return m_redoStack.back()->getDescription();
}

void CommandHistory::clear()
{
	m_undoStack.clear();
	m_redoStack.clear();
}

} // namespace agni::editor
