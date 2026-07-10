#include <tools/data_structures/finger_tree/finger_tree.hpp>

#include "test_support/command_model.hpp"
#include "test_support/test_runner.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <iterator>
#include <queue>
#include <ranges>
#include <source_location>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ft = tools::data_structures::finger_tree;
using namespace tools::data_structures::finger_tree::tests;

namespace {

using string_queue = ft::priority_queue<std::string, int>;
static_assert(std::forward_iterator<string_queue::const_iterator>);
static_assert(std::ranges::forward_range<const string_queue>);

struct non_equality_element final {
    int value;
};

template <class T>
concept has_equality = requires(const T& left, const T& right) {
    { left == right } -> std::convertible_to<bool>;
};

static_assert(!has_equality<ft::priority_queue_dequeue<non_equality_element, int>>);

template <class T>
void require_vector_equal(
    const std::vector<T>& actual,
    const std::vector<T>& expected,
    const std::source_location location = std::source_location::current())
{
    if (actual == expected) {
        return;
    }

    std::ostringstream message;
    message << location.file_name() << ':' << location.line() << ": vector mismatch";
    throw test_failure(message.str());
}

void add_priority_queue_tests_impl(suite& tests)
{
    tests.add("priority queue empty has no entries", [] {
        const auto queue = ft::priority_queue<std::string, int>{};

        FT_REQUIRE(queue.empty());
        FT_REQUIRE_EQUAL(queue.size(), static_cast<std::size_t>(0));
        FT_REQUIRE(!queue.try_peek_priority().has_value());
        FT_REQUIRE(!queue.try_peek().has_value());
        FT_REQUIRE(!queue.try_dequeue().has_value());
        FT_REQUIRE(queue.to_vector().empty());
    });

    tests.add("priority queue peek returns front without removing", [] {
        const auto queue = ft::priority_queue<std::string, int>{}
            .enqueue("a", 5)
            .enqueue("b", 2)
            .enqueue("c", 8);

        const auto priority = queue.try_peek_priority();
        FT_REQUIRE(priority.has_value());
        FT_REQUIRE_EQUAL(*priority, 2);

        const auto entry = queue.try_peek();
        FT_REQUIRE(entry.has_value());
        FT_REQUIRE_EQUAL(entry->element, std::string{"b"});
        FT_REQUIRE_EQUAL(entry->priority, 2);
        FT_REQUIRE_EQUAL(queue.size(), static_cast<std::size_t>(3));
    });

    tests.add("priority queue drains in nondecreasing priority order", [] {
        auto rng = deterministic_rng{17};
        auto queue = ft::priority_queue<std::string, int>{};
        auto pairs = std::vector<ft::priority_entry<std::string, int>>{};

        for (auto index = 0; index != 300; ++index) {
            const auto priority = rng.next_int(0, 49);
            auto element = std::string{"e"} + std::to_string(index);
            pairs.push_back({element, priority});
            queue = queue.enqueue(std::move(element), priority);
        }

        FT_REQUIRE_EQUAL(queue.size(), pairs.size());

        auto drained = std::vector<ft::priority_entry<std::string, int>>{};
        while (auto next = queue.try_dequeue()) {
            drained.push_back({next->element, next->priority});
            queue = next->rest;
        }

        FT_REQUIRE_EQUAL(drained.size(), pairs.size());
        for (std::size_t index = 1; index != drained.size(); ++index) {
            FT_REQUIRE(drained[index - 1].priority <= drained[index].priority);
        }

        std::ranges::sort(pairs, {}, [](const auto& entry) {
            return std::pair{entry.priority, entry.element};
        });
        std::ranges::sort(drained, {}, [](const auto& entry) {
            return std::pair{entry.priority, entry.element};
        });
        require_vector_equal(drained, pairs);
    });

    tests.add("priority queue keeps FIFO order among equal priorities", [] {
        auto queue = ft::priority_queue<std::string, int>{}
            .enqueue("first", 1)
            .enqueue("second", 1)
            .enqueue("third", 1)
            .enqueue("early", 0);

        auto next = queue.try_dequeue();
        FT_REQUIRE(next.has_value());
        FT_REQUIRE_EQUAL(next->element, std::string{"early"});
        queue = next->rest;

        next = queue.try_dequeue();
        FT_REQUIRE(next.has_value());
        FT_REQUIRE_EQUAL(next->element, std::string{"first"});
        queue = next->rest;

        next = queue.try_dequeue();
        FT_REQUIRE(next.has_value());
        FT_REQUIRE_EQUAL(next->element, std::string{"second"});
        queue = next->rest;

        next = queue.try_dequeue();
        FT_REQUIRE(next.has_value());
        FT_REQUIRE_EQUAL(next->element, std::string{"third"});
        queue = next->rest;

        FT_REQUIRE(!queue.try_dequeue().has_value());
    });

    tests.add("priority queue meld combines both queues", [] {
        const auto left = ft::priority_queue<std::string, int>{}
            .enqueue("a1", 4)
            .enqueue("a2", 1)
            .enqueue("a3", 7);
        const auto right = ft::priority_queue<std::string, int>{}
            .enqueue("b1", 2)
            .enqueue("b2", 6)
            .enqueue("b3", 0);

        auto melded = left.meld(right);
        FT_REQUIRE_EQUAL(melded.size(), static_cast<std::size_t>(6));
        FT_REQUIRE_EQUAL(left.size(), static_cast<std::size_t>(3));
        FT_REQUIRE_EQUAL(right.size(), static_cast<std::size_t>(3));

        auto priorities = std::vector<int>{};
        while (auto next = melded.try_dequeue()) {
            priorities.push_back(next->priority);
            melded = next->rest;
        }

        require_vector_equal(priorities, {0, 1, 2, 4, 6, 7});
    });

    tests.add("priority queue dequeue preserves original", [] {
        const auto original = ft::priority_queue<std::string, int>{}
            .enqueue("a", 3)
            .enqueue("b", 1)
            .enqueue("c", 2);

        const auto next = original.try_dequeue();
        FT_REQUIRE(next.has_value());
        FT_REQUIRE_EQUAL(next->element, std::string{"b"});
        FT_REQUIRE_EQUAL(original.size(), static_cast<std::size_t>(3));
        FT_REQUIRE_EQUAL(next->rest.size(), static_cast<std::size_t>(2));
    });

    tests.add("priority queue streams insertion order and dequeue results have value equality", [] {
        const auto first = string_queue{}
            .enqueue("a", 3)
            .enqueue("b", 1)
            .enqueue("c", 2);
        const auto second = string_queue::from_range(first.to_vector());

        auto iterated = std::vector<string_queue::entry_type>{};
        for (const auto& entry : first) {
            iterated.push_back(entry);
        }
        FT_REQUIRE(iterated == first.to_vector());

        auto copied = std::vector<string_queue::entry_type>{};
        first.copy_to(std::back_inserter(copied));
        FT_REQUIRE(copied == iterated);

        const auto first_dequeue = first.try_dequeue();
        const auto second_dequeue = second.try_dequeue();
        FT_REQUIRE(first_dequeue.has_value());
        FT_REQUIRE(second_dequeue.has_value());
        FT_REQUIRE(*first_dequeue == *second_dequeue);

        const auto different = second.enqueue("extra", 9).try_dequeue();
        FT_REQUIRE(different.has_value());
        FT_REQUIRE(*first_dequeue != *different);
    });

    tests.add("priority queue reversed comparison acts as max queue", [] {
        using max_queue = ft::priority_queue<std::string, int, ft::reverse_comparison<int>>;
        auto queue = max_queue{}
            .enqueue("low", 1)
            .enqueue("high", 9)
            .enqueue("mid", 5);

        const auto next = queue.try_dequeue();
        FT_REQUIRE(next.has_value());
        FT_REQUIRE_EQUAL(next->element, std::string{"high"});
        FT_REQUIRE_EQUAL(next->priority, 9);
    });
}

} // namespace

void add_priority_queue_tests(suite& tests)
{
    add_priority_queue_tests_impl(tests);
}
