/// Shared helpers and range-validation used across the finger tree implementation.

#pragma once

#include <cstddef>
#include <cstdint>
#include <concepts>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace durable7::finger_tree {

inline constexpr std::string_view library_name = "Durable7.FingerTree.Cpp";
inline constexpr std::uint32_t version_major = 0;
inline constexpr std::uint32_t version_minor = 1;
inline constexpr std::uint32_t version_patch = 0;

/// Whether the index lies inside a sequence of the given size.
[[nodiscard]] constexpr bool is_valid_index(const std::size_t index, const std::size_t size) noexcept
{
    return index < size;
}

/// Raises when the index falls outside the sequence.
constexpr void throw_if_index_out_of_range(const std::size_t index, const std::size_t size)
{
    if (!is_valid_index(index, size)) {
        throw std::out_of_range("index is outside the collection bounds");
    }
}

/// Raises when the insertion position falls outside the sequence, where the position one past the
/// end is allowed.
constexpr void throw_if_insert_index_out_of_range(const std::size_t index, const std::size_t size)
{
    if (index > size) {
        throw std::out_of_range("insert index is outside the collection bounds");
    }
}

/// Raises when the split position falls outside the sequence.
constexpr void throw_if_split_index_out_of_range(const std::size_t index, const std::size_t size)
{
    if (index > size) {
        throw std::out_of_range("split index is outside the collection bounds");
    }
}

/// Raises when the offset falls outside the text.
constexpr void throw_if_offset_out_of_range(const std::size_t offset, const std::size_t size)
{
    if (offset > size) {
        throw std::out_of_range("offset is outside the collection bounds");
    }
}

/// Raises when the count runs past the end of the sequence.
constexpr void throw_if_count_out_of_range(const std::size_t count, const std::size_t size)
{
    if (count > size) {
        throw std::out_of_range("count is outside the collection bounds");
    }
}

/// A value type comparable for equality.
template <class T>
concept equality_comparable_value = requires(const T& left, const T& right) {
    { left == right } -> std::convertible_to<bool>;
};

namespace detail {

template <std::ranges::input_range LeftRange, std::ranges::input_range RightRange>
    requires requires(
        std::ranges::range_reference_t<LeftRange> left,
        std::ranges::range_reference_t<RightRange> right) {
        { left == right } -> std::convertible_to<bool>;
    }
/// Whether two ranges hold equal elements in the same order.
[[nodiscard]] constexpr bool sequence_equal(LeftRange&& left, RightRange&& right)
{
    auto left_iterator = std::ranges::begin(left);
    const auto left_end = std::ranges::end(left);
    auto right_iterator = std::ranges::begin(right);
    const auto right_end = std::ranges::end(right);

    while (left_iterator != left_end && right_iterator != right_end) {
        if (!(*left_iterator == *right_iterator)) {
            return false;
        }
        ++left_iterator;
        ++right_iterator;
    }

    return left_iterator == left_end && right_iterator == right_end;
}

} // namespace detail

/// Adds, trapping on overflow rather than wrapping.
template <class T>
[[nodiscard]] constexpr T checked_add(const T left, const T right)
    requires(std::is_unsigned_v<T>)
{
    if (left > (std::numeric_limits<T>::max)() - right) {
        throw std::overflow_error("finger-tree size overflow");
    }

    return left + right;
}

} // namespace durable7::finger_tree
