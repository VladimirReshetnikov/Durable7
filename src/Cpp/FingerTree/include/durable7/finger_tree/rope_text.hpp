/// A text rope over characters, with newline counts cached in its measure.
///
/// That cache is what makes converting between a character offset and a line/column position
/// logarithmic rather than a scan of the text.

#pragma once

#include <durable7/finger_tree/detail/common.hpp>
#include <durable7/finger_tree/measured_rope.hpp>
#include <durable7/finger_tree/rope.hpp>

#include <algorithm>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace durable7::finger_tree {

/// Counts line feeds, which is what gives a text rope logarithmic offset-to-line conversion.
struct newline_measure final {
    using element_type = char;
    using measure_type = std::size_t;

    /// The identity: the measure of an empty tree.
    [[nodiscard]] static constexpr measure_type empty() noexcept
    {
        return 0;
    }

    /// The measure of one element.
    [[nodiscard]] static constexpr measure_type measure(const char value) noexcept
    {
        return value == '\n' ? 1 : 0;
    }

    /// Combines two measures in order. Must be associative; it need not be commutative.
    [[nodiscard]] static constexpr measure_type combine(const measure_type left, const measure_type right)
    {
        return checked_add(left, right);
    }
};

/// A rope over characters with newline counts cached in its measure, so converting between an
/// offset and a line/column is logarithmic rather than a scan.
using text_rope = measured_rope<char, newline_measure>;
/// A gap cursor over one text rope version.
using text_rope_cursor = measured_rope_cursor<char, newline_measure>;
/// Where a text rope cursor search landed.
using text_rope_cursor_search_result = measured_rope_cursor_search_result<char, newline_measure>;

/// A zero-based line and column position within a text rope.
struct line_column final {
    std::size_t line = 0;
    std::size_t column = 0;

    [[nodiscard]] constexpr bool operator==(const line_column&) const = default;
};

/// The text as a rope over characters.
[[nodiscard]] inline rope<char> to_char_rope(const std::string_view text)
{
    return rope<char>::create(std::span<const char>{text.data(), text.size()});
}

/// The text as a text rope.
[[nodiscard]] inline text_rope to_text_rope(const std::string_view text)
{
    return text_rope::create(std::span<const char>{text.data(), text.size()});
}

/// The value as a string.
[[nodiscard]] inline std::string as_string(const rope<char>& rope)
{
    auto result = std::string{};
    result.reserve(rope.size());
    rope.for_each([&result](const char value) {
        result.push_back(value);
    });
    return result;
}

/// The value as a string.
[[nodiscard]] inline std::string as_string(const text_rope& rope)
{
    auto result = std::string{};
    result.reserve(rope.size());
    rope.for_each([&result](const char value) {
        result.push_back(value);
    });
    return result;
}

/// How many lines the text holds. Newline counts are cached in the measure, so this is a cached
/// read.
[[nodiscard]] inline std::size_t line_count(const text_rope& rope)
{
    return checked_add(rope.measure(), std::size_t{1});
}

/// Which line the character offset falls on.
[[nodiscard]] inline std::size_t line_of_offset(const text_rope& rope, const std::size_t offset)
{
    throw_if_offset_out_of_range(offset, rope.size());
    return rope.prefix_measure(offset);
}

/// The character offset where the given line begins.
[[nodiscard]] inline std::size_t line_start_offset(const text_rope& rope, const std::size_t line)
{
    if (line > rope.measure()) {
        throw std::out_of_range("line is outside the text rope bounds");
    }

    if (line == 0) {
        return 0;
    }

    auto located = rope.try_locate_by_measure([line](const std::size_t newlines) {
        return newlines >= line;
    });
    if (!located.value.has_value()) {
        throw std::logic_error("text rope line index was not found");
    }

    return checked_add(located.index, std::size_t{1});
}

/// The line and column of a character offset.
[[nodiscard]] inline line_column line_column_of(const text_rope& rope, const std::size_t offset)
{
    const auto line = line_of_offset(rope, offset);
    return line_column{line, offset - line_start_offset(rope, line)};
}

/// The line and column of a character offset.
[[nodiscard]] inline line_column line_column_of(const text_rope_cursor& cursor)
{
    return line_column_of(cursor.snapshot(), cursor.position());
}

/// The character offset of a line and column.
[[nodiscard]] inline std::size_t offset_of(
    const text_rope& rope,
    const std::size_t line,
    const std::size_t column)
{
    const auto start = line_start_offset(rope, line);
    // Validate against the end of the requested line, not the end of the
    // rope; a column equal to the line length addresses the terminating
    // newline (or the end of the rope for the last line).
    const auto end = line < rope.measure() ? line_start_offset(rope, line + 1) - 1 : rope.size();
    if (column > end - start) {
        throw std::out_of_range("column is outside the line bounds");
    }

    return start + column;
}

/// The text of the given line.
[[nodiscard]] inline std::string get_line(const text_rope& rope, const std::size_t line)
{
    const auto start = line_start_offset(rope, line);
    const auto end = line < rope.measure() ? line_start_offset(rope, line + 1) - 1 : rope.size();
    auto result = std::string(end - start, '\0');
    rope.copy_to(start, std::span<char>{result.data(), result.size()});
    return result;
}

/// The text's lines.
[[nodiscard]] inline std::vector<std::string> lines(const text_rope& rope)
{
    auto result = std::vector<std::string>{};
    result.reserve(line_count(rope));

    auto current = std::string{};
    rope.for_each([&result, &current](const char value) {
        if (value == '\n') {
            result.push_back(std::move(current));
            current = std::string{};
        } else {
            current.push_back(value);
        }
    });

    result.push_back(std::move(current));
    return result;
}

/// A mutable accumulator for building a rope in bulk. Deliberately not a snapshot: it fills a
/// buffer and produces a rope only on demand.
class rope_builder final {
public:
    /// Number of elements.
    [[nodiscard]] std::size_t length() const noexcept
    {
        return buffer_.size();
    }

    /// Appends the given input.
    rope_builder& append(const std::string_view text)
    {
        buffer_.append(text);
        return *this;
    }

    /// Appends the given input.
    rope_builder& append(const char value)
    {
        buffer_.push_back(value);
        return *this;
    }

    /// Appends text followed by a line feed.
    rope_builder& append_line(const std::string_view text = {})
    {
        buffer_.append(text);
        buffer_.push_back('\n');
        return *this;
    }

    /// An empty rope retaining the same policies; returns the receiver when already empty.
    rope_builder& clear() noexcept
    {
        buffer_.clear();
        return *this;
    }

    /// The contents as a rope.
    [[nodiscard]] rope<char> to_rope() const
    {
        return to_char_rope(buffer_);
    }

    /// The contents as a text rope.
    [[nodiscard]] text_rope to_text_rope() const
    {
        return ::durable7::finger_tree::to_text_rope(buffer_);
    }

    /// The value as a string.
    [[nodiscard]] const std::string& str() const noexcept
    {
        return buffer_;
    }

private:
    std::string buffer_;
};

} // namespace durable7::finger_tree
