#pragma once

// Component registry — stores type-erased descriptors for all registered components.
// The inspector, serializer, and "Add Component" menu read from this registry.
//
// Thread safety: populated during initialization (single-threaded), read-only after.
// No mutex needed.
//
// Reference: CryEngine's Schematyc environment component registration.

#include <Reflection/TypeDesc.hpp>

#include <flecs.h>

#include <cassert>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace agni
{

// ============================================================================
// ComponentDescriptor — type-erased descriptor stored in the registry
// ============================================================================

struct ComponentDescriptor
{
	// Metadata (copied from TypeDesc at registration time)
	const char*               name     = nullptr;
	const char*               category = nullptr;
	std::vector<PropertyInfo> properties;
	size_t                    typeSize = 0;

	// Type-erased Flecs operations (lambdas captured during Register<T>)
	std::function<bool(flecs::entity)>               has;        // entity.has<T>()
	std::function<void*(flecs::entity)>               getMut;     // &entity.ensure<T>()
	std::function<const void*(flecs::entity)>          getConst;   // entity.try_get<T>()
	std::function<void(flecs::entity, const void*)>    set;        // entity.set<T>(*(const T*)data)
	std::function<void(flecs::entity)>                 remove;     // entity.remove<T>()
	std::function<void(void*)>                         construct;  // placement-new default T
	std::function<void(void*)>                         destruct;   // call ~T()
};

// ============================================================================
// ComponentRegistry — singleton, populated at init, read-only after
// ============================================================================

class ComponentRegistry
{
public:
	static ComponentRegistry& Instance();

	// Register a component type. Calls T::ReflectType to get metadata.
	// Must be called during initialization before any frame renders.
	template<typename T>
	void Register();

	// Lookup by name (returns nullptr if not found)
	const ComponentDescriptor* Find(const char* name) const;

	// All registered descriptors (unordered)
	const std::vector<const ComponentDescriptor*>& GetAll() const;

	// Total count
	size_t Count() const { return m_descriptors.size(); }

private:
	ComponentRegistry() = default;

	std::unordered_map<std::string, ComponentDescriptor> m_descriptors;

	// Cached flat list for iteration (rebuilt on Register)
	mutable std::vector<const ComponentDescriptor*> m_allCache;
	mutable bool m_cacheDirty = true;
};

// ============================================================================
// Register<T> implementation — must be in header for template instantiation
// ============================================================================

template<typename T>
void ComponentRegistry::Register()
{
	TypeDesc<T> desc;
	T::ReflectType(desc);

	assert(m_descriptors.find(desc.getName()) == m_descriptors.end()
	       && "Component registered twice with the same name");

	ComponentDescriptor cd;
	cd.name       = desc.getName();
	cd.category   = desc.getCategory();
	cd.properties = desc.getProperties();
	cd.typeSize   = sizeof(T);

	// Capture type-erased Flecs operations via lambdas
	cd.has       = [](flecs::entity e) -> bool          { return e.has<T>(); };
	cd.getMut    = [](flecs::entity e) -> void*          { return &e.ensure<T>(); };
	cd.getConst  = [](flecs::entity e) -> const void*    { return e.try_get<T>(); };
	cd.set       = [](flecs::entity e, const void* data) { e.set<T>(*static_cast<const T*>(data)); };
	cd.remove    = [](flecs::entity e)                   { e.remove<T>(); };
	cd.construct = [](void* ptr)                         { new (ptr) T{}; };
	cd.destruct  = [](void* ptr)                         { static_cast<T*>(ptr)->~T(); };

	m_descriptors.emplace(desc.getName(), std::move(cd));
	m_cacheDirty = true;
}

} // namespace agni
