#include <durable7/finger_tree/finger_tree.hpp>

#include "test_support/test_runner.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace ft = durable7::finger_tree;
using namespace durable7::finger_tree::tests;

namespace {

static_assert(std::same_as<ft::text_rope_cursor, ft::measured_rope_cursor<char, ft::newline_measure>>);
static_assert(std::same_as<
    ft::text_rope_cursor_search_result,
    ft::measured_rope_cursor_search_result<char, ft::newline_measure>>);

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

    tests.add("rope text long edit script preserves snapshots and navigation", [] {
        auto text = std::string{};
        for (auto line = 0; line != 240; ++line) {
            text += "line-";
            text += std::to_string(line);
            text += ':';
            text.push_back(static_cast<char>('a' + (line % 26)));
            text.push_back(static_cast<char>('A' + ((line * 7) % 26)));
            text.push_back(static_cast<char>('0' + (line % 10)));
            text.push_back('\n');
        }

        auto rope = ft::to_text_rope(text);
        const auto snapshot = rope;
        const auto snapshot_text = text;
        require_text_model(text);

        for (auto step = 0; step != 180; ++step) {
            if (step % 5 == 1 && !text.empty()) {
                const auto index = (static_cast<std::size_t>(step) * 53U + 17U) % text.size();
                const auto count = std::min<std::size_t>((step % 4) + 1, text.size() - index);
                rope = rope.remove_range(index, count);
                text.erase(index, count);
            } else {
                const auto index = (static_cast<std::size_t>(step) * 97U + 11U) % (text.size() + 1);
                auto inserted = std::string{};
                if (step % 5 == 0) {
                    inserted = "\nsection-" + std::to_string(step) + "\n";
                } else {
                    inserted.push_back(static_cast<char>('!' + (step % 57)));
                    inserted.push_back(static_cast<char>('0' + (step % 10)));
                }

                rope = rope.insert_range(index, inserted);
                text.insert(index, inserted);
            }

            if (step % 17 == 0) {
                FT_REQUIRE(ft::as_string(rope) == text);
                require_text_model(text);
                FT_REQUIRE(ft::as_string(snapshot) == snapshot_text);
                require_text_model(snapshot_text);
            }
        }

        FT_REQUIRE(ft::as_string(rope) == text);
        require_text_model(text);
        FT_REQUIRE(ft::as_string(snapshot) == snapshot_text);
        require_text_model(snapshot_text);
    });

    tests.add("rope text cursor preserves byte offsets line measures and exact text snapshots", [] {
        const auto text = ft::to_text_rope("a\r\nb");
        const auto start = text.get_cursor();
        FT_REQUIRE_EQUAL(start.size(), static_cast<std::size_t>(4));
        FT_REQUIRE((ft::line_column_of(start) == ft::line_column{0, 0}));
        FT_REQUIRE(start.snapshot().begin() == text.begin());

        const auto newline = text.get_cursor_by_measure([](const std::size_t count) {
            return count >= 1;
        });
        FT_REQUIRE(newline.found);
        FT_REQUIRE_EQUAL(newline.cursor.position(), static_cast<std::size_t>(2));
        FT_REQUIRE_EQUAL(*newline.cursor.try_peek_next(), '\n');
        FT_REQUIRE_EQUAL(newline.cursor.measure_before(), static_cast<std::size_t>(0));
        FT_REQUIRE_EQUAL(newline.cursor.measure_after(), static_cast<std::size_t>(1));
        FT_REQUIRE((ft::line_column_of(newline.cursor.move_next()) == ft::line_column{1, 0}));

        const auto edited = text.get_cursor(1)
            .insert_range(std::string{"x\n"})
            .replace_next('Z');
        FT_REQUIRE_EQUAL(edited.position(), static_cast<std::size_t>(3));
        FT_REQUIRE(ft::as_string(edited.snapshot()) == "ax\nZ\nb");
        FT_REQUIRE((ft::line_column_of(edited) == ft::line_column{1, 0}));
        FT_REQUIRE(ft::as_string(text) == "a\r\nb");

        const auto second_newline = edited.seek_by_measure([](const std::size_t count) {
            return count >= 2;
        });
        FT_REQUIRE(second_newline.found);
        FT_REQUIRE_EQUAL(second_newline.cursor.position(), static_cast<std::size_t>(4));

        const auto miss = edited.seek_by_measure([](const std::size_t count) {
            return count > 2;
        });
        FT_REQUIRE(!miss.found);
        FT_REQUIRE(miss.cursor.is_at_end());
        FT_REQUIRE(ft::as_string(miss.cursor.snapshot()) == "ax\nZ\nb");

        FT_REQUIRE_THROWS(std::runtime_error, edited.seek_by_measure([](const std::size_t) -> bool {
            throw std::runtime_error("text cursor predicate failure");
        }));
        FT_REQUIRE_EQUAL(edited.position(), static_cast<std::size_t>(3));
        FT_REQUIRE(ft::as_string(edited.snapshot()) == "ax\nZ\nb");
    });

    tests.add("rope text argument validation", [] {
        const auto rope = ft::to_text_rope("a\nb");
        FT_REQUIRE_THROWS(std::out_of_range, ft::line_start_offset(rope, 3));
        FT_REQUIRE_THROWS(std::out_of_range, ft::line_of_offset(rope, 4));
        FT_REQUIRE_THROWS(std::out_of_range, ft::offset_of(rope, 1, 100));
        FT_REQUIRE_THROWS(std::out_of_range, ft::get_line(rope, 3));
    });

    tests.add("rope text offset_of validates the column against the line end", [] {
        const auto rope = ft::to_text_rope("ab\ncd");

        FT_REQUIRE(ft::offset_of(rope, 0, 0) == 0);
        FT_REQUIRE(ft::offset_of(rope, 0, 2) == 2);   // the newline terminating line 0
        FT_REQUIRE(ft::offset_of(rope, 1, 2) == 5);   // end of the rope on the last line

        // Columns past the line end must throw, not silently address line 1.
        FT_REQUIRE_THROWS(std::out_of_range, ft::offset_of(rope, 0, 3));
        FT_REQUIRE_THROWS(std::out_of_range, ft::offset_of(rope, 0, 4));
        FT_REQUIRE_THROWS(std::out_of_range, ft::offset_of(rope, 1, 3));
    });
}

} // namespace

void add_rope_text_tests(suite& tests)
{
    add_rope_text_tests_impl(tests);
}
