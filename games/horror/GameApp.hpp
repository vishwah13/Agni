#pragma once

class AgniEngine;

// Game application setup for the survival horror game.
// Registers game-specific components and ECS systems.
// Used by both the editor (Play mode) and runtime.
class GameApp
{
public:
	void init(AgniEngine& engine);
	void update(AgniEngine& engine, float dt);
	void cleanup();
};
