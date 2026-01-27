#include "EntityManager.hpp"
#include "World.hpp"

namespace agni::ecs
{

EntityManager::EntityManager(World& world)
    : m_world(world)
{
}

std::string EntityManager::getUniqueName(const std::string& baseName)
{
	uint32_t& counter = m_nameCounters[baseName];
	++counter;
	return baseName + "_" + std::to_string(counter);
}

void EntityManager::resetCounters()
{
	m_nameCounters.clear();
}

void EntityManager::resetCounter(const std::string& baseName)
{
	m_nameCounters.erase(baseName);
}

} // namespace agni::ecs
