// Copyright (C) 2018-2021 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "ngraph/compatibility.hpp"
#include "openvino/core/core_visibility.hpp"

namespace ov {

/// Supports three functions, ov::is_type<Type>, ov::as_type<Type>, and ov::as_type_ptr<Type> for type-safe
/// dynamic conversions via static_cast/static_ptr_cast without using C++ RTTI.
/// Type must have a static type_info member and a virtual get_type_info() member that
/// returns a reference to its type_info member.

/// Type information for a type system without inheritance; instances have exactly one type not
/// related to any other type.
struct OPENVINO_API DiscreteTypeInfo {
    const char* name;
    uint64_t version;
    const char* version_id;
    // A pointer to a parent type info; used for casting and inheritance traversal, not for
    // exact type identification
    const DiscreteTypeInfo* parent;

    DiscreteTypeInfo() = default;

    constexpr DiscreteTypeInfo(const char* _name, uint64_t _version, const DiscreteTypeInfo* _parent = nullptr)
        : name(_name),
          version(_version),
          version_id(nullptr),
          parent(_parent) {}

    constexpr DiscreteTypeInfo(const char* _name,
                               uint64_t _version,
                               const char* _version_id,
                               const DiscreteTypeInfo* _parent = nullptr)
        : name(_name),
          version(_version),
          version_id(_version_id),
          parent(_parent) {}

    bool is_castable(const DiscreteTypeInfo& target_type) const {
        return *this == target_type || (parent && parent->is_castable(target_type));
    }

    std::string get_version() const {
        if (version_id) {
            return std::string(version_id);
        }
        return std::to_string(version);
    }

    operator std::string() const {
        return std::string(name) + "_" + get_version();
    }

    // For use as a key
    bool operator<(const DiscreteTypeInfo& b) const;
    bool operator<=(const DiscreteTypeInfo& b) const;
    bool operator>(const DiscreteTypeInfo& b) const;
    bool operator>=(const DiscreteTypeInfo& b) const;
    bool operator==(const DiscreteTypeInfo& b) const;
    bool operator!=(const DiscreteTypeInfo& b) const;
};

OPENVINO_API
std::ostream& operator<<(std::ostream& s, const DiscreteTypeInfo& info);

/// \brief Tests if value is a pointer/shared_ptr that can be statically cast to a
/// Type*/shared_ptr<Type>
OPENVINO_SUPPRESS_DEPRECATED_START
template <typename Type, typename Value>
typename std::enable_if<
    ngraph::HasTypeInfoMember<Type>::value &&
        std::is_convertible<decltype(std::declval<Value>()->get_type_info().is_castable(Type::type_info)), bool>::value,
    bool>::type
is_type(Value value) {
    return value->get_type_info().is_castable(Type::type_info);
}
OPENVINO_SUPPRESS_DEPRECATED_END

template <typename Type, typename Value>
typename std::enable_if<
    !ngraph::HasTypeInfoMember<Type>::value &&
        std::is_convertible<decltype(std::declval<Value>()->get_type_info().is_castable(Type::get_type_info_static())),
                            bool>::value,
    bool>::type
is_type(Value value) {
    return value->get_type_info().is_castable(Type::get_type_info_static());
}

/// Casts a Value* to a Type* if it is of type Type, nullptr otherwise
template <typename Type, typename Value>
typename std::enable_if<std::is_convertible<decltype(static_cast<Type*>(std::declval<Value>())), Type*>::value,
                        Type*>::type
as_type(Value value) {
    return ov::is_type<Type>(value) ? static_cast<Type*>(value) : nullptr;
}

/// Casts a std::shared_ptr<Value> to a std::shared_ptr<Type> if it is of type
/// Type, nullptr otherwise
template <typename Type, typename Value>
typename std::enable_if<
    std::is_convertible<decltype(std::static_pointer_cast<Type>(std::declval<Value>())), std::shared_ptr<Type>>::value,
    std::shared_ptr<Type>>::type
as_type_ptr(Value value) {
    return ov::is_type<Type>(value) ? std::static_pointer_cast<Type>(value) : std::shared_ptr<Type>();
}
}  // namespace ov

namespace std {
template <>
struct OPENVINO_API hash<ov::DiscreteTypeInfo> {
    size_t operator()(const ov::DiscreteTypeInfo& k) const;
};
}  // namespace std
