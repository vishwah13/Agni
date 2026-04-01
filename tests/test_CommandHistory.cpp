#include <gtest/gtest.h>
#include <Editor/CommandHistory.hpp>

using namespace agni::editor;

// Mock command that tracks execute/undo calls
class MockCommand : public ICommand
{
public:
	MockCommand(std::string desc, int& counter)
	    : m_desc(std::move(desc)), m_counter(counter) {}

	void execute() override { m_counter++; }
	void undo() override { m_counter--; }
	std::string getDescription() const override { return m_desc; }

private:
	std::string m_desc;
	int& m_counter;
};

class CommandHistoryTest : public ::testing::Test
{
protected:
	CommandHistory history;
	int counter = 0;

	std::unique_ptr<ICommand> makeCmd(std::string desc = "test")
	{
		return std::make_unique<MockCommand>(std::move(desc), counter);
	}
};

// --- Initial state ---

TEST_F(CommandHistoryTest, InitialStateIsEmpty)
{
	EXPECT_FALSE(history.canUndo());
	EXPECT_FALSE(history.canRedo());
	EXPECT_EQ(history.getUndoCount(), 0u);
	EXPECT_EQ(history.getRedoCount(), 0u);
	EXPECT_EQ(history.getUndoDescription(), "");
	EXPECT_EQ(history.getRedoDescription(), "");
}

// --- Execute ---

TEST_F(CommandHistoryTest, ExecuteRunsCommand)
{
	history.execute(makeCmd());
	EXPECT_EQ(counter, 1);
}

TEST_F(CommandHistoryTest, ExecuteAddsToUndoStack)
{
	history.execute(makeCmd());
	EXPECT_TRUE(history.canUndo());
	EXPECT_EQ(history.getUndoCount(), 1u);
}

TEST_F(CommandHistoryTest, ExecuteNullIsNoOp)
{
	history.execute(nullptr);
	EXPECT_EQ(counter, 0);
	EXPECT_FALSE(history.canUndo());
}

TEST_F(CommandHistoryTest, ExecuteClearsRedoStack)
{
	history.execute(makeCmd());
	history.undo();
	EXPECT_TRUE(history.canRedo());

	history.execute(makeCmd());
	EXPECT_FALSE(history.canRedo());
}

// --- Undo ---

TEST_F(CommandHistoryTest, UndoReversesCommand)
{
	history.execute(makeCmd());
	EXPECT_EQ(counter, 1);

	history.undo();
	EXPECT_EQ(counter, 0);
}

TEST_F(CommandHistoryTest, UndoMovesToRedoStack)
{
	history.execute(makeCmd());
	history.undo();
	EXPECT_FALSE(history.canUndo());
	EXPECT_TRUE(history.canRedo());
}

TEST_F(CommandHistoryTest, UndoOnEmptyIsNoOp)
{
	history.undo();
	EXPECT_EQ(counter, 0);
}

// --- Redo ---

TEST_F(CommandHistoryTest, RedoReExecutesCommand)
{
	history.execute(makeCmd());
	history.undo();
	EXPECT_EQ(counter, 0);

	history.redo();
	EXPECT_EQ(counter, 1);
}

TEST_F(CommandHistoryTest, RedoMovesBackToUndoStack)
{
	history.execute(makeCmd());
	history.undo();
	history.redo();
	EXPECT_TRUE(history.canUndo());
	EXPECT_FALSE(history.canRedo());
}

TEST_F(CommandHistoryTest, RedoOnEmptyIsNoOp)
{
	history.redo();
	EXPECT_EQ(counter, 0);
}

// --- Multiple undo/redo ---

TEST_F(CommandHistoryTest, MultipleUndoRedo)
{
	history.execute(makeCmd("A"));
	history.execute(makeCmd("B"));
	history.execute(makeCmd("C"));
	EXPECT_EQ(counter, 3);

	history.undo(); // undo C
	EXPECT_EQ(counter, 2);
	history.undo(); // undo B
	EXPECT_EQ(counter, 1);

	history.redo(); // redo B
	EXPECT_EQ(counter, 2);
	history.redo(); // redo C
	EXPECT_EQ(counter, 3);
}

// --- Descriptions ---

TEST_F(CommandHistoryTest, DescriptionsReflectTopOfStack)
{
	history.execute(makeCmd("Create Cube"));
	history.execute(makeCmd("Move Entity"));
	EXPECT_EQ(history.getUndoDescription(), "Move Entity");

	history.undo();
	EXPECT_EQ(history.getUndoDescription(), "Create Cube");
	EXPECT_EQ(history.getRedoDescription(), "Move Entity");
}

// --- Max history ---

TEST_F(CommandHistoryTest, MaxHistoryEnforced)
{
	history.setMaxHistory(3);

	history.execute(makeCmd("A"));
	history.execute(makeCmd("B"));
	history.execute(makeCmd("C"));
	EXPECT_EQ(history.getUndoCount(), 3u);

	history.execute(makeCmd("D"));
	EXPECT_EQ(history.getUndoCount(), 3u); // A was evicted
	EXPECT_EQ(counter, 4); // all 4 executed
}

// --- Clear ---

TEST_F(CommandHistoryTest, ClearRemovesAll)
{
	history.execute(makeCmd());
	history.execute(makeCmd());
	history.undo();

	history.clear();
	EXPECT_FALSE(history.canUndo());
	EXPECT_FALSE(history.canRedo());
	EXPECT_EQ(history.getUndoCount(), 0u);
	EXPECT_EQ(history.getRedoCount(), 0u);
}
