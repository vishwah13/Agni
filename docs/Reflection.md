# Agni Reflection System

The reflection system lets each component describe itself once — its fields, types, display names, and modifiers — and have that description automatically used by the inspector, serializer, prefab system, Add Component menu, and duplicate command. No per-system boilerplate.

---

## The Problem It Solves

**Before reflection:** adding a field to a component required editing 3–5 separate places:
- Render logic in `ECSInspector.cpp` (ImGui widget)
- Save logic in `SceneSerializer.cpp` (write JSON)
- Load logic in `SceneSerializer.cpp` (read JSON)
- Copy logic in `EntityCommands.cpp` (DuplicateEntityCommand)
- The "Add Component" popup

**After reflection:** define the field once in `ReflectType`. Everything else is automatic.

---

## Architecture Overview

```
TypeDesc<T>           — compile-time builder (lives in each component's ReflectType)
  └── PropertyInfo[]  — runtime metadata for each field

ComponentRegistry     — singleton, stores all ComponentDescriptors
  └── ComponentDescriptor — TypeDesc metadata + type-erased Flecs lambdas

Consumers:
  ECSInspector        — reads PropertyInfo, dispatches ImGui widgets by PropertyType
  SceneSerializer     — reads PropertyInfo, dispatches JSON read/write by PropertyType
  EntityCommands      — iterates registry for DuplicateEntityCommand
  Add Component popup — iterates registry to list available components
```

---

## Core Types

### `PropertyType` — runtime type tag

Defined in `src/Reflection/TypeDesc.hpp`.

```cpp
enum class PropertyType : uint8_t
{
    Float,     // float
    Int,       // int
    UInt32,    // uint32_t
    Bool,      // bool
    String,    // std::string
    Vec3,      // glm::vec3
    Vec4,      // glm::vec4
    Quat,      // glm::quat
    Mat4,      // glm::mat4
    Enum,      // any enum registered via EnumDesc
    Color3,    // glm::vec3 shown as color picker (set via .SetAsColor())
    Color4,    // glm::vec4 shown as color picker (set via .SetAsColor())
    EntityID,  // uint64_t entity reference
};
```

This tag drives two switch statements at runtime: one in `ECSInspector` to choose the right ImGui widget (DragFloat, Checkbox, InputText, ColorEdit3, etc.) and one in `SceneSerializer` to read/write the correct JSON value type.

### `DeducePropertyType<T>` — compile-time C++ type → PropertyType

```cpp
template<> struct DeducePropertyType<float>       { static constexpr auto value = PropertyType::Float; };
template<> struct DeducePropertyType<glm::vec3>   { static constexpr auto value = PropertyType::Vec3; };
// ... etc
// Any enum type via concept:
template<typename T> requires std::is_enum_v<T>
struct DeducePropertyType<T>                      { static constexpr auto value = PropertyType::Enum; };
```

`AddMember` calls `DeducePropertyType<MemberType>::value` to fill `PropertyInfo::type`. If you add a field of an unsupported type (e.g., `glm::ivec2`), it won't compile — no silent runtime failures.

### `PropertyInfo` — one field's complete metadata

```cpp
struct PropertyInfo
{
    const char*         name;          // Serialization key:   "innerConeAngle"
    const char*         displayName;   // Inspector label:     "Inner Cone Angle"
    const char*         tooltip;       // Inspector tooltip:   nullable
    PropertyType        type;          // Runtime type tag
    size_t              offset;        // Byte offset from start of the component struct
    size_t              size;          // sizeof(MemberType)
    const EnumDescBase* enumDesc;      // Non-null for Enum fields
    uint8_t             defaultValue[64]; // Value from a default-constructed T
    size_t              defaultValueSize;
    float               rangeMin;      // Minimum value (when hasRange = true)
    float               rangeMax;      // Maximum value (when hasRange = true)
    const char*         unit;          // Display unit ("deg", "kg", "m/s", etc.)
    bool                hasRange;      // Clamp to [rangeMin, rangeMax]
    bool                readOnly;      // Show in inspector, disallow editing
    bool                hidden;        // Don't show in inspector at all
    bool                noSerialize;   // Don't save/load this field
    bool                isColor;       // Render Vec3/Vec4 as color picker
};
```

**Reading a field at runtime** using `PropertyInfo`:

```cpp
// Given: const void* componentPtr, const PropertyInfo& prop
const float* val = reinterpret_cast<const float*>(
    static_cast<const char*>(componentPtr) + prop.offset);
```

The inspector and serializer do exactly this — `offset` lets them reach any field without knowing the concrete type.

### `EnumDesc<E>` — enum name ↔ value table

Used for combo boxes in the inspector and string serialization in JSON.

```cpp
static agni::EnumDesc<LightType> lightTypeEnum;
lightTypeEnum.Add(LightType::Point,       "Point")
             .Add(LightType::Directional, "Directional")
             .Add(LightType::Spot,        "Spot");
```

`EnumDescBase` provides two lookups:
- `nameFromValue(int64_t)` → `"Point"` — used by serializer when writing JSON
- `valueFromName(const char*, int64_t&)` → used by serializer when reading JSON

Enums are stored as strings in JSON (`"type": "Point"`), not integers, for human readability and forward compatibility.

---

## `TypeDesc<T>` — the builder

`TypeDesc<T>` is a compile-time description builder. It is only ever used inside `ReflectType`:

```cpp
static void ReflectType(agni::TypeDesc<LightComponent>& desc)
{
    desc.SetName("LightComponent");     // Registry key, serialization type tag
    desc.SetCategory("Rendering");      // "Add Component" menu grouping

    desc.AddMember(&LightComponent::color, "color", "Color").SetAsColor();
    desc.AddMember(&LightComponent::intensity, "intensity", "Intensity");
    desc.AddMember(&LightComponent::radius, "radius", "Radius", "Attenuation radius");
    //                              ^ptr-to-member  ^serial key  ^display name  ^tooltip
}
```

### `AddMember` — offset computation

```cpp
T dummy {};
info.offset = static_cast<size_t>(
    reinterpret_cast<const char*>(&(dummy.*member))
    - reinterpret_cast<const char*>(&dummy));
```

A default-constructed `T` is stack-allocated, then `pointer-to-member` dereference gives the address of that field. Subtracting the base address of `dummy` yields the byte offset. This is defined behavior (no UB, no `offsetof` macro limitations). The same `dummy` also provides the field's **default value**, which the inspector can use for "reset to default" and which the serializer can use for omitting unchanged values.

### `MemberBuilder` — fluent modifiers

`AddMember` returns a `MemberBuilder` proxy that lets you chain modifiers:

| Method | Effect |
|--------|--------|
| `.SetEnum(&desc)` | Override type to `Enum`, attach EnumDesc pointer |
| `.SetReadOnly()` | Inspector shows value but disables editing |
| `.SetHidden()` | Field is invisible in inspector (still serialized unless also `.SetNoSerialize()`) |
| `.SetNoSerialize()` | Excluded from JSON save/load |
| `.SetAsColor()` | Promotes `Vec3`→`Color3` or `Vec4`→`Color4` for color picker widget |
| `.SetRange(min, max)` | Clamp value to [min, max] in inspector drag widgets |
| `.SetUnit("deg")` | Display unit label next to the widget in inspector |

These can be chained:

```cpp
desc.AddMember(&RigidBodyComponent::joltBodyID, "joltBodyID", "Jolt Body ID")
    .SetReadOnly()
    .SetHidden()
    .SetNoSerialize();

desc.AddMember(&LightComponent::innerConeAngle, "innerConeAngle", "Inner Cone Angle")
    .SetRange(0.0f, 90.0f)
    .SetUnit("deg");
```

---

## `ComponentDescriptor` — type-erased runtime descriptor

`TypeDesc<T>` is a template. To store descriptors for all component types in a single container, the registry erases the type via `ComponentDescriptor`:

```cpp
struct ComponentDescriptor
{
    const char*               name;
    const char*               category;
    std::vector<PropertyInfo> properties;
    size_t                    typeSize;   // sizeof(T) — for stack allocation

    std::function<bool(flecs::entity)>              has;
    std::function<void*(flecs::entity)>              getMut;
    std::function<const void*(flecs::entity)>        getConst;
    std::function<void(flecs::entity, const void*)>  set;
    std::function<void(flecs::entity)>               remove;
    std::function<void(void*)>                       construct;  // placement-new T{}
    std::function<void(void*)>                       destruct;   // ~T()
};
```

The 7 lambda slots are the complete Flecs API surface the rest of the engine needs. Because they're `std::function` slots capturing `T` via a lambda, callers never need to know the actual type.

**Example usage in the inspector:**

```cpp
// Check if entity has a component:
if (desc->has(entity)) { ... }

// Get mutable pointer to edit:
void* ptr = desc->getMut(entity);

// Add a component dynamically ("Add Component" button):
alignas(16) uint8_t buf[desc->typeSize];
desc->construct(buf);          // placement-new T{}
desc->set(entity, buf);        // entity.set<T>(*(T*)buf)
desc->destruct(buf);           // ~T() on the local copy
```

---

## `ComponentRegistry` — singleton

Defined in `src/Reflection/ComponentRegistry.hpp`.

```cpp
auto& registry = agni::ComponentRegistry::Instance();
registry.Register<TransformComponent>();
registry.Register<LightComponent>();
// ...

// Lookup:
const ComponentDescriptor* desc = registry.Find("LightComponent");  // nullptr if missing

// Iterate all:
for (const auto* desc : registry.GetAll()) { ... }
```

`Register<T>()` is a template method defined in the header (template instantiation requirement):

1. Stack-allocates `TypeDesc<T>`, calls `T::ReflectType(desc)` to populate it
2. Copies name, category, properties, typeSize into a `ComponentDescriptor`
3. Captures 7 type-erased Flecs lambdas
4. Inserts into `m_descriptors` (an `unordered_map<string, ComponentDescriptor>`)
5. Marks the flat cache dirty

The flat cache (`m_allCache`) is rebuilt lazily on first `GetAll()` call after a registration. After initialization, registration never happens again so the cache is always valid.

**Thread safety:** registration is single-threaded (engine init). After that, the registry is read-only. No mutex needed.

---

## Defining a Reflectable Component

### Minimal example

```cpp
struct HealthComponent
{
    float maxHealth  {100.0f};
    float currHealth {100.0f};
    bool  isDead     {false};

    static void ReflectType(agni::TypeDesc<HealthComponent>& desc)
    {
        desc.SetName("HealthComponent");
        desc.SetCategory("Gameplay");
        desc.AddMember(&HealthComponent::maxHealth,  "maxHealth",  "Max Health");
        desc.AddMember(&HealthComponent::currHealth, "currHealth", "Current Health").SetReadOnly();
        desc.AddMember(&HealthComponent::isDead,     "isDead",     "Is Dead").SetReadOnly();
    }
};
```

### With an enum

```cpp
enum class FactionType : uint8_t { Neutral, Player, Enemy };

struct FactionComponent
{
    FactionType faction {FactionType::Neutral};

    static void ReflectType(agni::TypeDesc<FactionComponent>& desc)
    {
        static agni::EnumDesc<FactionType> factionEnum;
        if (factionEnum.constants.empty())
        {
            factionEnum.name = "FactionType";
            factionEnum.Add(FactionType::Neutral, "Neutral")
                       .Add(FactionType::Player,  "Player")
                       .Add(FactionType::Enemy,   "Enemy");
        }

        desc.SetName("FactionComponent");
        desc.SetCategory("Gameplay");
        desc.AddMember(&FactionComponent::faction, "faction", "Faction")
            .SetEnum(&factionEnum);
    }
};
```

### Registering it

In `World::registerComponents()` (`src/ECS/World.cpp`):

```cpp
registry.Register<HealthComponent>();
registry.Register<FactionComponent>();
```

That's all. The component now appears in:
- **Inspector** — fields rendered with correct widgets
- **Serializer** — fields saved/loaded as JSON
- **Add Component menu** — listed under its category
- **Duplicate command** — fields copied to the new entity
- **Prefab system** — component data saved and restored

---

## What Each Consumer Does

### ECSInspector (`src/Editor/ECSInspector.cpp`)

```
For each desc in registry.GetAll():
  if desc->has(entity):
    void* ptr = desc->getMut(entity)
    For each PropertyInfo prop in desc->properties:
      if prop.hidden: skip
      switch prop.type:
        Float   → ImGui::DragFloat(prop.displayName, ptr+prop.offset)
        Bool    → ImGui::Checkbox(...)
        Vec3    → ImGui::DragFloat3(...)
        Color3  → ImGui::ColorEdit3(...)
        Enum    → ImGui::Combo(...) using prop.enumDesc->nameFromValue(...)
        String  → ImGui::InputText(...)
        ...
```

### SceneSerializer (`src/Scene/SceneSerializer.cpp`)

```
serializeReflectedComponent(entity, desc):
  For each PropertyInfo prop in desc->properties:
    if prop.noSerialize: skip
    void* ptr = getConst(entity) + prop.offset
    switch prop.type:
      Float  → json[prop.name] = *reinterpret_cast<float*>(ptr)
      Vec3   → json[prop.name] = {x, y, z}
      Enum   → json[prop.name] = prop.enumDesc->nameFromValue(...)
      ...

deserializeReflectedComponent(entity, desc, json):
  For each PropertyInfo prop in desc->properties:
    if prop.noSerialize: skip
    void* ptr = getMut(entity) + prop.offset
    switch prop.type:
      Float  → *reinterpret_cast<float*>(ptr) = json[prop.name]
      Enum   → prop.enumDesc->valueFromName(json[prop.name], val); write val
      ...
```

### EntityCommands — DuplicateEntityCommand (`src/Editor/EntityCommands.cpp`)

```cpp
for (const auto* desc : agni::ComponentRegistry::Instance().GetAll())
{
    if (!desc->has(src)) continue;
    const void* data = desc->getConst(src);
    if (data) desc->set(dst, data);  // entity.set<T>(*(const T*)data)
}
```

Copies every registered component from source to destination entity generically. Non-reflected components (MeshComponent, MaterialComponent) are NOT copied — they stay on the source. This is intentional: GPU handles can't be blindly memcopied.

---

## Non-Reflected Components

Some components intentionally have **no** `ReflectType`:

| Component | Why not reflected |
|-----------|------------------|
| `MeshComponent` | Contains `GPUMeshBuffers` (Vulkan VkBuffer handles) — can't be generically serialized or copied |
| `MaterialComponent` | Contains `MaterialInstance*` pointer — lifetime managed externally |
| `SkyboxComponent` | Contains `Texture` struct with GPU handles |
| `PhysicsEnabledTag` | Zero-size tag, no fields to reflect |

The inspector handles these with hardcoded special cases.

---

## Supported Property Types and Their Widgets

| PropertyType | C++ type | Inspector widget | JSON format |
|---|---|---|---|
| `Float` | `float` | `DragFloat` | `1.5` |
| `Int` | `int` | `DragInt` | `-3` |
| `UInt32` | `uint32_t` | `DragScalar` | `42` |
| `Bool` | `bool` | `Checkbox` | `true` |
| `String` | `std::string` | `InputText` | `"hello"` |
| `Vec3` | `glm::vec3` | `DragFloat3` | `[1.0, 2.0, 3.0]` |
| `Vec4` | `glm::vec4` | `DragFloat4` | `[1.0, 0.0, 0.0, 1.0]` |
| `Quat` | `glm::quat` | `DragFloat4` | `[x, y, z, w]` |
| `Mat4` | `glm::mat4` | (hidden by default) | 16-float array |
| `Enum` | any `enum class` | `Combo` | `"EnumValueName"` |
| `Color3` | `glm::vec3` | `ColorEdit3` | `[r, g, b]` |
| `Color4` | `glm::vec4` | `ColorEdit4` | `[r, g, b, a]` |
| `EntityID` | `uint64_t` | Text display | integer |

---

## Known Limitations

1. **64-byte default value cap** — `PropertyInfo::defaultValue[64]` stores up to `glm::mat4` (64 bytes). Fields larger than 64 bytes won't compile (`static_assert` in `AddMember`). Practically, all current component fields fit.

2. **Trivially copyable types only** — the serializer and duplicate command copy field bytes with `memcpy`-style offset reads. `std::string` is handled specially (via `PropertyType::String`). Fields containing non-trivial types not in the `PropertyType` list (e.g., `std::vector`, `std::shared_ptr`) cannot be reflected.

3. **Static EnumDesc initialization** — `static agni::EnumDesc<E>` inside `ReflectType` is initialized lazily with a `constants.empty()` guard. This is safe since `ReflectType` is only called during the single-threaded initialization phase.

4. **Name must be unique** — `ComponentRegistry::Register<T>()` asserts that no two components share a name. The name is also used as the JSON type tag during scene serialization, so renaming a component breaks existing save files.

5. **Manual registration** — every component must be explicitly registered in `World::registerComponents()`. There is no automatic self-registration. This is intentional: it makes the initialization order explicit and avoids static-initialization-order-fiasco issues.
