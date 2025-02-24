// Copyright (C) 2018-2021 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <cstddef>

#include "ngraph/op/util/attr_types.hpp"
#include "openvino/core/attribute_adapter.hpp"
#include "static_dimension.hpp"
#include "openvino/core/rank.hpp"
#include "openvino/core/shape.hpp"
#include "openvino/core/partial_shape.hpp"

namespace ov {
namespace op {
    struct AutoBroadcastSpec;
}

/// \brief Class representing a shape that must be totally static.
class StaticShape : public std::vector<StaticDimension>  {
public:
    StaticShape() = default;
    StaticShape(std::initializer_list<StaticDimension> init);
    StaticShape(const std::vector<StaticDimension::value_type>& dimensions);
    StaticShape(std::vector<StaticDimension> dimensions);

    static bool is_static() { return true; }
    static bool is_dynamic() { return false; }

    Rank rank() const { return Rank(size()); }

    bool compatible(const StaticShape& s) const;
    bool same_scheme(const StaticShape& s) const;
    bool refines(const StaticShape& s) const;
    bool merge_rank(Rank r);

    Shape to_shape() const;
    PartialShape to_partial_shape() const;

    friend std::ostream& operator<<(std::ostream& str, const StaticShape& shape);
    friend StaticShape operator+(const StaticShape& s1, const StaticShape& s2);
    bool operator==(const StaticShape& shape) const;
    bool operator!=(const StaticShape& shape) const;
    /// Get the max bounding shape
    Shape get_max_shape() const;
    /// Get the min bounding shape
    Shape get_min_shape() const;
    /// Get the unique shape
    Shape get_shape() const;
    static bool merge_into(StaticShape& dst, const StaticShape& src);
    static bool broadcast_merge_into(StaticShape& dst,
                                     const StaticShape& src,
                                     const ngraph::op::AutoBroadcastSpec& autob);
};

StaticShape operator+(const StaticShape& s1, const StaticShape& s2);
std::ostream& operator<<(std::ostream& str, const StaticShape& shape);
}  // namespace ov
