#include <Reflection/ComponentRegistry.hpp>

namespace agni
{

ComponentRegistry& ComponentRegistry::Instance()
{
	static ComponentRegistry instance;
	return instance;
}

const ComponentDescriptor* ComponentRegistry::Find(const char* name) const
{
	auto it = m_descriptors.find(name);
	return it != m_descriptors.end() ? &it->second : nullptr;
}

const std::vector<const ComponentDescriptor*>& ComponentRegistry::GetAll() const
{
	if (m_cacheDirty)
	{
		m_allCache.clear();
		m_allCache.reserve(m_descriptors.size());
		for (const auto& [name, desc] : m_descriptors)
			m_allCache.push_back(&desc);
		m_cacheDirty = false;
	}
	return m_allCache;
}

} // namespace agni
