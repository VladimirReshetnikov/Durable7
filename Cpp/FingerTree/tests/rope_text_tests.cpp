#include <tools/data_structures/finger_tree/finger_tree.hpp>

#include "test_support/test_runner.hpp"

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace ft = tools::data_structures::finger_tree;
using namespace tools::data_structures::finger_tree::tests;

namespace {

[[nodiscard]] std::vector<std::string> split_lines(const std::string_view text)
{
    auto result = std::vector<std::string>{};
    auto current = std::string{};
    for (const auto value : text) {
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

[[nodiscard]] std::vector<std::size_t> line_starts(const std::vector<std::string>& lines)
{
    auto starts = std::vector<std::size_t>{};
    starts.reserve(lines.size());
    auto running = std::size_t{0};
    for (const auto& line : lines) {
        starts.push_back(running);
        running += line.size() + 1;
    }

    return starts;
}

void require_text_model(const std::string& text)
{
    const auto char_rope = ft::to_char_rope(text);
    const auto rope = ft::to_text_rope(text);
    const auto expected_lines = split_lines(text);
    const auto starts = line_starts(expected_lines);
    const auto newline_count = static_cast<std::size_t>(std::ranges::count(text, '\n'));

    FT_REQUIRE(ft::as_string(char_rope) == text);
    FT_REQUIRE(ft::as_string(rope) == text);
    FT_REQUIRE_EQUAL(rope.measure(), newline_count);
    FT_REQUIRE_EQUAL(ft::line_count(rope), expected_lines.size());
    FT_REQUIRE(ft::lines(rope) == expected_lines);

    for (auto line = std::size_t{0}; line < expected_lines.size(); ++line) {
        FT_REQUIRE_EQUAL(ft::line_start_offset(rope, line), starts[line]);
        FT_REQUIRE(ft::get_line(rope, line) == expected_lines[line]);
    }

    for (auto offset = std::size_t{0}; offset <= text.size(); ++offset) {
        const auto expected_line = static_cast<std::size_t>(
            std::ranges::count(std::string_view{text.data(), offset}, '\n'));
        FT_REQUIRE_EQUAL(ft::line_of_offset(rope, offset), expected_line);

        const auto position = ft::line_column_of(rope, offset);
        FT_REQUIRE_EQUAL(position.line, expected_line);
        FT_REQUIRE_EQUAL(position.column, offset - starts[expected_line]);
        FT_REQUIRE_EQUAL(ft::offset_of(rope, position.line, position.column), offset);
    }
}

void add_rope_text_tests_impl(suite& tests)
{
    tests.add("rope text string interop and line navigation match string model", [] {
        const auto samples = std::vector<std::string>{
            "",
            "no newlines here",
            "a\nb\nc",
            "a\nbb\nccc",
            "trailing\n",
            "\nleading",
            "\n\n\n",
            "line0\nline1\n\nline3\n"};

        for (const auto& text : samples) {
            require_text_model(text);
        }
    });

    tests.add("rope text editing round-trips through measured rope", [] {
        const auto rope = ft::to_text_rope("hello world");
        const auto inserted = rope.insert_range(5, std::string{",\nthere"});

        FT_REQUIRE(ft::as_string(inserted) == "hello,\nthere world");
        FT_REQUIRE_EQUAL(ft::line_count(inserted), static_cast<std::size_t>(2));
        FT_REQUIRE(ft::get_line(inserted, 0) == "hello,");
        FT_REQUIRE(ft::get_line(inserted, 1) == "there world");

        const auto removed = inserted.remove_range(5, 7);
        FT_REQUIRE(ft::as_string(removed) == "hello world");
        FT_REQUIRE_EQUAL(ft::line_count(removed), static_cast<std::size_t>(1));
    });

    tests.add("rope builder materializes char and text ropes", [] {
        auto builder = ft::rope_builder{};
        builder.append("hello").append(' ').append("world").append_line().append_line("tail");

        const auto expected = std::string{"hello world\ntail\n"};
        FT_REQUIRE_EQUAL(builder.length(), expected.size());
        FT_REQUIRE(builder.str() == expected);
        FT_REQUIRE(ft::as_string(builder.to_rope()) == expected);
        const auto text = builder.to_text_rope();
        FT_REQUIRE(ft::as_string(text) == expected);
        FT_REQUIRE_EQUAL(ft::line_count(text), static_cast<std::size_t>(3));
        FT_REQUIRE(ft::get_line(text, 1) == "tail");
        FT_REQUIRE(ft::get_line(text, 2).empty());

        builder.clear().append("reset");
        FT_REQUIRE_EQUAL(builder.length(), static_cast<std::size_t>(5));
        FT_REQUIRE(ft::as_string(builder.to_text_rope()) == "reset");
    });

    tests.add("rope text large document line lookups", [] {
        auto text = std::string{};
        for (auto index = 0; index != 10000; ++index) {
            text += "line ";
            text += std::to_string(index);
            text += '\n';
        }

        const auto rope = ft::to_text_rope(text);
        FT_REQUIRE_EQUAL(ft::line_count(rope), static_cast<std::size_t>(10001));
        FT_REQUIRE(ft::get_line(rope, 0) == "line 0");
        FT_REQUIRE(ft::get_line(rope, 3141) == "line 3141");
        FT_REQUIRE(ft::get_line(rope, 9999) == "line 9999");
        FT_REQUIRE(ft::get_line(rope, 10000).empty());

        const auto offset = ft::offset_of(rope, 3141, 2);
        FT_REQUIRE_EQUAL(rope[offset], 'n');
        const auto position = ft::line_column_of(rope, offset);
        FT_REQUIRE_EQUAL(position.line, static_cast<std::size_t>(3141));
        FT_REQUIRE_EQUAL(position.column, static_cast<std::size_t>(2));
    });

    tests.add("rope text argument validation", [] {
        const auto rope = ft::to_text_rope("a\nb");
        FT_REQUIRE_THROWS(std::out_of_range, ft::line_start_offset(rope, 3));
        FT_REQUIRE_THROWS(std::out_of_range, ft::line_of_offset(rope, 4));
        FT_REQUIRE_THROWS(std::out_of_range, ft::offset_of(rope, 1, 100));
        FT_REQUIRE_THROWS(std::out_of_range, ft::get_line(rope, 3));
    });
}

} // namespace

void add_rope_text_tests(suite& tests)
{
    add_rope_text_tests_impl(tests);
}
