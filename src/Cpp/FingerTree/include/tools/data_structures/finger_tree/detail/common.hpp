#pragma once

#include <cstddef>
#include <cstdint>
#include <concepts>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace tools::data_structures::finger_tree {

inline constexpr std::string_view library_name = "Tools.DataStructures.FingerTree.Cpp";
inline constexpr std::uint32_t version_major = 0;
inline constexpr std::uint32_t version_minor = 1;
inline constexpr std::uint32_t version_patch = 0;

[[nodiscard]] constexpr bool is_valid_index(const std::size_t index, const std::size_t size) noexcept
{
    return index < size;
}

constexpr void throw_if_index_out_of_range(const std::size_t index, const std::size_t size)
{
    if (!is_valid_index(index, size)) {
        throw std::out_of_range("index is outside the collection bounds");
    }
}

constexpr void throw_if_insert_index_out_of_range(const std::size_t index, const std::size_t size)
{
    if (index > size) {
        throw std::out_of_range("insert index is outside the collection bounds");
    }
}

template <class T>
concept equality_comparable_value = requires(const T& left, const T& right) {
    { left == right } -> std::convertible_to<bool>;
};

template <class T>
[[nodiscard]] constexpr T checked_add(const T left, const T right)
    requires(std::is_unsigned_v<T>)
{
    if (left > (std::numeric_limits<T>::max)() - right) {
        throw std::overflow_error("finger-tree size overflow");
    }

    return left + right;
}

} // namespace tools::data_structures::finger_tree
