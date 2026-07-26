/// A sample showing that older versions stay valid and readable after later edits.

#include "sample_runs.hpp"

#include <durable7/finger_tree/finger_tree.hpp>

#include <atomic>
#include <cstddef>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace durable7::finger_tree::samples::persistent_snapshots {
namespace {

[[nodiscard]] std::string quoted(const std::string_view text)
{
    auto result = std::string{"\""};
    for (const auto value : text) {
        result += value == '\n' ? "\\n" : std::string(1, value);
    }
    result += '"';
    return result;
}

void undo_redo_act(std::ostream& output)
{
    output << "Act 1 - Undo/redo over retained structural snapshots\n";
    auto history = std::vector<text_rope>{to_text_rope("")};
    auto cursor = std::size_t{0};

    const auto apply = [&history, &cursor](const std::size_t offset, const std::string_view text) {
        auto next = history[cursor].insert_range(offset, text);
        history.erase(history.begin() + static_cast<std::ptrdiff_t>(cursor + 1), history.end());
        history.push_back(std::move(next));
        ++cursor;
    };

    apply(history[cursor].size(), "hello");
    apply(history[cursor].size(), " world");
    apply(5, ",\nbrave");
    output << "  after 3 edits: " << quoted(as_string(history[cursor])) << '\n';
    --cursor;
    output << "  undo:          " << quoted(as_string(history[cursor])) << '\n';
    --cursor;
    output << "  undo:          " << quoted(as_string(history[cursor])) << '\n';
    ++cursor;
    output << "  redo:          " << quoted(as_string(history[cursor])) << '\n';
    output << "  retained versions: " << history.size() << " (snapshot values share immutable tree nodes)\n\n";
}

void line_navigation_act(std::ostream& output)
{
    output << "Act 2 - O(log n) line/column navigation by cached newline measure\n";
    const auto document = to_text_rope("first line\nsecond line\nthird line");
    const auto position = line_column_of(document, 14);
    output << "  document:       " << quoted(as_string(document)) << '\n';
    output << "  line count:     " << line_count(document) << '\n';
    output << "  offset 14:      line " << position.line << ", column " << position.column
           << " (character '" << document[14] << "')\n";
    output << "  line 1:         " << quoted(get_line(document, 1)) << '\n';
    output << "  line 2 start:   offset " << offset_of(document, 2, 0) << "\n\n";
}

void concurrent_publication_act(std::ostream& output)
{
    output << "Act 3 - Atomic single-writer/concurrent-reader snapshot publication\n";
    constexpr auto publication_count = std::size_t{5};
    std::atomic<std::shared_ptr<const text_rope>> published{std::make_shared<const text_rope>(to_text_rope(""))};
    std::atomic<std::size_t> epoch{0};
    std::atomic<std::size_t> observed_epoch{0};
    std::atomic<bool> inconsistent{false};
    auto observed_characters = std::size_t{0};

    std::thread reader{[&] {
        for (auto expected = std::size_t{1}; expected <= publication_count; ++expected) {
            while (epoch.load(std::memory_order_acquire) < expected) {
                std::this_thread::yield();
            }

            const auto snapshot = published.load(std::memory_order_acquire);
            const auto expected_characters = expected * std::string_view{"log line 0\n"}.size();
            if (snapshot->size() != expected_characters || snapshot->measure() != expected) {
                inconsistent.store(true, std::memory_order_relaxed);
            }
            observed_characters += snapshot->size();
            observed_epoch.store(expected, std::memory_order_release);
        }
    }};

    auto current = to_text_rope("");
    for (auto index = std::size_t{0}; index < publication_count; ++index) {
        const auto line = std::string{"log line "} + static_cast<char>('0' + index) + '\n';
        current = current.insert_range(current.size(), line);
        published.store(std::make_shared<const text_rope>(current), std::memory_order_release);
        epoch.store(index + 1, std::memory_order_release);
        while (observed_epoch.load(std::memory_order_acquire) < index + 1) {
            std::this_thread::yield();
        }
    }
    reader.join();

    if (inconsistent.load(std::memory_order_relaxed)) {
        throw std::runtime_error("reader observed an inconsistent persistent snapshot");
    }

    const auto final = published.load(std::memory_order_acquire);
    output << "  writer published " << final->measure() << " complete log lines / " << final->size() << " characters\n";
    output << "  reader observed " << publication_count << " whole immutable versions (checksum "
           << observed_characters << ") without an application lock\n\n";
    output << "  atomic<shared_ptr> may serialize internally; this is data-race-safe, not a lock-free guarantee\n\n";
}

} // namespace

void run(std::ostream& output)
{
    output << "FingerTree tour: a persistent measured-rope text buffer\n"
              "=======================================================\n\n";
    undo_redo_act(output);
    line_navigation_act(output);
    concurrent_publication_act(output);
    output << "Done. Every retained or published version is immutable and structurally shared.\n";
}

} // namespace durable7::finger_tree::samples::persistent_snapshots
