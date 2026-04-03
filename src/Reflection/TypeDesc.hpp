#pragma once

// CryEngine-style component reflection system for Agni.
// Components describe themselves via a static ReflectType method.
// The inspector, serializer, and prefab system read this metadata
// instead of hardcoding per-component behavior.
//
// Reference: CryEngine's Schematyc/Reflection/TypeDesc.h

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/quaternion.hpp>

namespace agni
{

// ============================================================================
// PropertyType — runtime type tag for inspector widget selection and serialization
// ============================================================================

enum class PropertyType : uint8_t
{
	Float,
	Int,
	UInt32,
	Bool,
	String,    // std::string
	Vec3,      // glm::vec3
	Vec4,      // glm::vec4
	Quat,      // glm::quat
	Mat4,      // glm::mat4
	Enum,      // Any enum registered via EnumDesc
	Color3,    // glm::vec3 displayed as color picker
	Color4,    // glm::vec4 displayed as color picker
	EntityID,  // uint64_t entity reference
};

// ============================================================================
// DeducePropertyType — compile-time type → PropertyType mapping
// Unspecialized = compile error (forces explicit support for new types)
// ============================================================================

template<typename T> struct DeducePropertyType;
template<> struct DeducePropertyType<float>        { static constexpr auto value = PropertyType::Float; };
template<> struct DeducePropertyType<int>          { static constexpr auto value = PropertyType::Int; };
template<> struct DeducePropertyType<uint32_t>     { static constexpr auto value = PropertyType::UInt32; };
template<> struct DeducePropertyType<bool>         { static constexpr auto value = PropertyType::Bool; };
template<> struct DeducePropertyType<std::string>  { static constexpr auto value = PropertyType::String; };
template<> struct DeducePropertyType<glm::vec3>    { static constexpr auto value = PropertyType::Vec3; };
template<> struct DeducePropertyType<glm::vec4>    { static constexpr auto value = PropertyType::Vec4; };
template<> struct DeducePropertyType<glm::quat>    { static constexpr auto value = PropertyType::Quat; };
template<> struct DeducePropertyType<glm::mat4>    { static constexpr auto value = PropertyType::Mat4; };
template<> struct DeducePropertyType<uint64_t>     { static constexpr auto value = PropertyType::EntityID; };

// Enums: any enum type defaults to PropertyType::Enum
template<typename T>
    requires std::is_enum_v<T>
struct DeducePropertyType<T> { static constexpr auto value = PropertyType::Enum; };

// ============================================================================
// EnumDesc — describes enum constants for combo boxes and serialization
// ============================================================================

struct EnumConstant
{
	const char* name  = nullptr;
	int64_t     value = 0;
};

struct EnumDescBase
{
	const char*                name = nullptr;
	std::vector<EnumConstant>  constants;

	const char* nameFromValue(int64_t val) const
	{
		for (const auto& c : constants)
			if (c.value == val) return c.name;
		return nullptr;
	}

	bool valueFromName(const char* n, int64_t& outVal) const
	{
		for (const auto& c : constants)
		{
			if (std::strcmp(c.name, n) == 0)
			{
				outVal = c.value;
				return true;
			}
		}
		return false;
	}
};

template<typename E>
struct EnumDesc : EnumDescBase
{
	EnumDesc& Add(E value, const char* constantName)
	{
		constants.push_back({constantName, static_cast<int64_t>(value)});
		return *this;
	}
};

// ============================================================================
// PropertyInfo — runtime descriptor for one member field
// ============================================================================

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to alignment specifier
#endif

struct PropertyInfo
{
	const char*          name         = nullptr;  // Serialization key ("detectionRadius")
	const char*          displayName  = nullptr;  // Inspector label ("Detection Radius")
	const char*          tooltip      = nullptr;  // Inspector tooltip (nullable)
	PropertyType         type         = PropertyType::Float;
	size_t               offset       = 0;        // Byte offset from start of component struct
	size_t               size         = 0;        // sizeof(MemberType)
	const EnumDescBase*  enumDesc     = nullptr;  // Non-null for enum fields
	alignas(16) uint8_t  defaultValue[64] = {};   // Default value (up to mat4 = 64 bytes)
	size_t               defaultValueSize = 0;
	float                rangeMin     = 0.0f;    // Minimum value (when hasRange = true)
	float                rangeMax     = 0.0f;    // Maximum value (when hasRange = true)
	const char*          unit         = nullptr;  // Display unit ("degrees", "m/s", etc.)
	bool                 hasRange     = false;    // Clamp to [rangeMin, rangeMax]
	bool                 readOnly     = false;    // Inspector shows but can't edit
	bool                 hidden       = false;    // Don't show in inspector
	bool                 noSerialize  = false;    // Don't save/load
	bool                 isColor      = false;    // Vec3/Vec4 rendered as color picker
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

// ============================================================================
// MemberBuilder — fluent builder returned by AddMember for chaining
// ============================================================================

class MemberBuilder
{
public:
	explicit MemberBuilder(PropertyInfo& info) : m_info(info) {}

	MemberBuilder& SetEnum(const EnumDescBase* desc)
	{
		m_info.enumDesc = desc;
		m_info.type     = PropertyType::Enum;
		return *this;
	}

	MemberBuilder& SetReadOnly()
	{
		m_info.readOnly = true;
		return *this;
	}

	MemberBuilder& SetHidden()
	{
		m_info.hidden = true;
		return *this;
	}

	MemberBuilder& SetNoSerialize()
	{
		m_info.noSerialize = true;
		return *this;
	}

	MemberBuilder& SetAsColor()
	{
		m_info.isColor = true;
		if (m_info.type == PropertyType::Vec3) m_info.type = PropertyType::Color3;
		if (m_info.type == PropertyType::Vec4) m_info.type = PropertyType::Color4;
		return *this;
	}

	MemberBuilder& SetRange(float min, float max)
	{
		m_info.hasRange = true;
		m_info.rangeMin = min;
		m_info.rangeMax = max;
		return *this;
	}

	MemberBuilder& SetUnit(const char* unit)
	{
		m_info.unit = unit;
		return *this;
	}

private:
	PropertyInfo& m_info;
};

// ============================================================================
// TypeDesc<T> — builder used inside ReflectType to describe a component
// ============================================================================

template<typename T>
class TypeDesc
{
public:
	TypeDesc& SetName(const char* n) { m_name = n; return *this; }
	TypeDesc& SetCategory(const char* c) { m_category = c; return *this; }

	template<typename MemberType>
	MemberBuilder AddMember(MemberType T::*member,
	                        const char* name,
	                        const char* displayName,
	                        const char* tooltip = nullptr)
	{
		PropertyInfo info {};
		info.name        = name;
		info.displayName = displayName;
		info.tooltip     = tooltip;
		info.type        = DeducePropertyType<MemberType>::value;
		info.size        = sizeof(MemberType);

		// Offset computation using defined behavior (default-constructed instance)
		// Reference: CryEngine's Cry::Memory::GetMemberOffset in AddressHelpers.h
		T dummy {};
		info.offset = static_cast<size_t>(
		    reinterpret_cast<const char*>(&(dummy.*member))
		    - reinterpret_cast<const char*>(&dummy));

		// Store default value from a default-constructed instance
		static_assert(sizeof(MemberType) <= sizeof(info.defaultValue),
		              "MemberType exceeds max default value storage (64 bytes)");
		const MemberType& defaultVal = dummy.*member;
		std::memcpy(info.defaultValue, &defaultVal, sizeof(MemberType));
		info.defaultValueSize = sizeof(MemberType);

		m_properties.push_back(info);
		return MemberBuilder{m_properties.back()};
	}

	// Accessors
	const char*                     getName()       const { return m_name; }
	const char*                     getCategory()   const { return m_category; }
	const std::vector<PropertyInfo>& getProperties() const { return m_properties; }

private:
	const char*              m_name     = "Unknown";
	const char*              m_category = "General";
	std::vector<PropertyInfo> m_properties;
};

} // namespace agni
