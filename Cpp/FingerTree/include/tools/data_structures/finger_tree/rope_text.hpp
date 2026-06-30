#pragma once

#include <tools/data_structures/finger_tree/detail/common.hpp>
#include <tools/data_structures/finger_tree/measured_rope.hpp>
#include <tools/data_structures/finger_tree/rope.hpp>

#include <algorithm>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tools::data_structures::finger_tree {

struct newline_measure final {
    using element_type = char;
    using measure_type = std::size_t;

    [[nodiscard]] static constexpr measure_type empty() noexcept
    {
        return 0;
    }

    [[nodiscard]] static constexpr measure_type measure(const char value) noexcept
    {
        return value == '\n' ? 1 : 0;
    }

    [[nodiscard]] static constexpr measure_type combine(const measure_type left, const measure_type right)
    {
        return checked_add(left, right);
    }
};

using text_rope = measured_rope<char, newline_measure>;

struct line_column final {
    std::size_t line = 0;
    std::size_t column = 0;

    [[nodiscard]] constexpr bool operator==(const line_column&) const = default;
};

[[nodiscard]] inline rope<char> to_char_rope(const std::string_view text)
{
    return rope<char>::create(std::span<const char>{text.data(), text.size()});
}

[[nodiscard]] inline text_rope to_text_rope(const std::string_view text)
{
    return text_rope::create(std::span<const char>{text.data(), text.size()});
}

[[nodiscard]] inline std::string as_string(const rope<char>& rope)
{
    auto values = rope.to_vector();
    return std::string{values.begin(), values.end()};
}

[[nodiscard]] inline std::string as_string(const text_rope& rope)
{
    auto values = rope.to_vector();
    return std::string{values.begin(), values.end()};
}

[[nodiscard]] inline std::size_t line_count(const text_rope& rope)
{
    return checked_add(rope.measure(), std::size_t{1});
}

[[nodiscard]] inline std::size_t line_of_offset(const text_rope& rope, const std::size_t offset)
{
    return rope.prefix_measure(offset);
}

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
    if (!located.has_value()) {
        throw std::logic_error("text rope line index was not found");
    }

    return checked_add(located->index, std::size_t{1});
}

[[nodiscard]] inline line_column line_column_of(const text_rope& rope, const std::size_t offset)
{
    const auto line = line_of_offset(rope, offset);
    return line_column{line, offset - line_start_offset(rope, line)};
}

[[nodiscard]] inline std::size_t offset_of(
    const text_rope& rope,
    const std::size_t line,
    const std::size_t column)
{
    const auto start = line_start_offset(rope, line);
    if (column > rope.size() - start) {
        throw std::out_of_range("column is outside the text rope bounds");
    }

    return start + column;
}

[[nodiscard]] inline std::string get_line(const text_rope& rope, const std::size_t line)
{
    const auto start = line_start_offset(rope, line);
    const auto end = line < rope.measure() ? line_start_offset(rope, line + 1) - 1 : rope.size();
    auto values = rope.get_range(start, end - start);
    return std::string{values.begin(), values.end()};
}

[[nodiscard]] inline std::vector<std::string> lines(const text_rope& rope)
{
    auto result = std::vector<std::string>{};
    result.reserve(line_count(rope));

    auto current = std::string{};
    for (const auto value : rope.to_vector()) {
        if (value == '\n') {
            result.push_back(std::move(current));
            current = std::string{};
        } else {
            current.push_back(value);
        }
    }

    result.push_back(std::move(current));
    return result;
}

class rope_builder final {
public:
    [[nodiscard]] std::size_t length() const noexcept
    {
        return buffer_.size();
    }

    rope_builder& append(const std::string_view text)
    {
        buffer_.append(text);
        return *this;
    }

    rope_builder& append(const char value)
    {
        buffer_.push_back(value);
        return *this;
    }

    rope_builder& append_line(const std::string_view text = {})
    {
        buffer_.append(text);
        buffer_.push_back('\n');
        return *this;
    }

    rope_builder& clear() noexcept
    {
        buffer_.clear();
        return *this;
    }

    [[nodiscard]] rope<char> to_rope() const
    {
        return to_char_rope(buffer_);
    }

    [[nodiscard]] text_rope to_text_rope() const
    {
        return ::tools::data_structures::finger_tree::to_text_rope(buffer_);
    }

    [[nodiscard]] const std::string& str() const noexcept
    {
        return buffer_;
    }

private:
    std::string buffer_;
};

} // namespace tools::data_structures::finger_tree
