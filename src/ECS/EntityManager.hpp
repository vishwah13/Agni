#pragma once

#include <string>
#include <unordered_map>

namespace agni::ecs
{

class World;

// ============================================================================
// EntityManager - Unique entity naming service
// ============================================================================

class EntityManager
{
public:
	explicit EntityManager(World& world);

	// === Unique Naming ===
	// Returns "baseName_N" where N is an incrementing counter
	// Example: getUniqueName("Cube") returns "Cube_1", "Cube_2", etc.
	std::string getUniqueName(const std::string& baseName);

	// Reset all counters (call on scene clear/new)
	void resetCounters();

	// Reset specific counter (rarely needed)
	void resetCounter(const std::string& baseName);

private:
	World& m_world;

	// Name counters: "Cube" -> 3 means next is "Cube_4"
	std::unordered_map<std::string, uint32_t> m_nameCounters;
};

} // namespace agni::ecs
