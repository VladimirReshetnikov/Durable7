/// The Ordered workspace's umbrella header: the insertion-ordered collections.

#pragma once

#include <durable7/ordered/persistent_ordered_map.hpp>
#include <durable7/ordered/persistent_ordered_multimap.hpp>
#include <durable7/ordered/persistent_ordered_set.hpp>
#include <durable7/ordered/persistent_ordered_cursors.hpp>

#include <string_view>

namespace durable7::ordered {

inline constexpr std::string_view library_name = "Durable7.Ordered.Cpp";
inline constexpr unsigned version_major = 0;
inline constexpr unsigned version_minor = 1;
inline constexpr unsigned version_patch = 0;

} // namespace durable7::ordered
