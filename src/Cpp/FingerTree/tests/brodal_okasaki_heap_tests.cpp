/// Tests for the persistent Brodal-Okasaki heap.

#include <durable7/finger_tree/finger_tree.hpp>

#include "test_support/allocation_counter.hpp"
#include "test_support/command_model.hpp"
#include "test_support/test_runner.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ranges>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace ft = durable7::finger_tree;
using namespace durable7::finger_tree::tests;

namespace {

static_assert(std::forward_iterator<ft::brodal_okasaki_heap<int>::const_iterator>);
static_assert(std::ranges::forward_range<const ft::brodal_okasaki_heap<int>>);

template <class Less>
[[nodiscard]] std::vector<int> drain(ft::brodal_okasaki_heap<int, Less> heap)
{
    auto result = std::vector<int>{};
    result.reserve(heap.size());
    while (!heap.empty()) {
        result.push_back(heap.minimum());
        heap = heap.delete_minimum();
    }
    return result;
}

template <class Less>
void require_valid(const ft::brodal_okasaki_heap<int, Less>& heap)
{
    const auto statistics = heap.validate_structure();
    FT_REQUIRE_EQUAL(statistics.count, heap.size());
    if (heap.empty()) {
        FT_REQUIRE_EQUAL(statistics.root_forest_length, std::size_t{0});
        FT_REQUIRE_EQUAL(statistics.maximum_rank, std::size_t{0});
        FT_REQUIRE_EQUAL(statistics.maximum_depth, std::size_t{0});
    } else {
        FT_REQUIRE(statistics.maximum_depth > 0);
    }
}

struct counting_less final {
    std::shared_ptr<std::atomic<std::size_t>> comparisons;

    [[nodiscard]] bool operator()(const int left, const int right) const noexcept
    {
        comparisons->fetch_add(1, std::memory_order_relaxed);
        return left < right;
    }
};

struct tagged final {
    int priority = 0;
    int identity = 0;
};

struct tagged_less final {
    [[nodiscard]] bool operator()(const tagged& left, const tagged& right) const noexcept
    {
        return left.priority < right.priority;
    }
};

struct move_only_item final {
    int priority = 0;
    int identity = 0;

    move_only_item(int priority_value, int identity_value) noexcept
        : priority(priority_value)
        , identity(identity_value)
    {
    }

    move_only_item(const move_only_item&) = delete;
    move_only_item& operator=(const move_only_item&) = delete;
    move_only_item(move_only_item&&) noexcept = default;
    move_only_item& operator=(move_only_item&&) noexcept = default;
};

struct move_only_less final {
    [[nodiscard]] bool operator()(const move_only_item& left, const move_only_item& right) const noexcept
    {
        return left.priority < right.priority;
    }
};

struct throwing_less final {
    std::shared_ptr<std::atomic<bool>> should_throw;

    [[nodiscard]] bool operator()(const int left, const int right) const
    {
        if (should_throw->load(std::memory_order_relaxed)) {
            throw std::runtime_error("injected Brodal comparator failure");
        }
        return left < right;
    }
};

void add_brodal_okasaki_heap_tests_impl(suite& tests)
{
    tests.add("Brodal adversarial shapes validate and drain in sorted order", [] {
        constexpr auto count = 8'192;
        auto ascending = std::vector<int>{};
        ascending.reserve(count);
        for (auto value = 0; value != count; ++value) {
            ascending.push_back(value);
        }
        auto descending = ascending;
        std::ranges::reverse(descending);
        auto equal = std::vector<int>(count, 7);
        auto shuffled = ascending;
        deterministic_rng random{0x62726f64616c5f31ULL};
        for (auto index = shuffled.size(); index > 1; --index) {
            std::swap(shuffled[index - 1], shuffled[random.next_index(index)]);
        }

        const auto ascending_heap = ft::brodal_okasaki_heap<int>::from_range(ascending);
        const auto descending_heap = ft::brodal_okasaki_heap<int>::from_range(descending);
        const auto equal_heap = ft::brodal_okasaki_heap<int>::from_range(equal);
        const auto shuffled_heap = ft::brodal_okasaki_heap<int>::from_range(shuffled);
        require_valid(ascending_heap);
        require_valid(descending_heap);
        require_valid(equal_heap);
        require_valid(shuffled_heap);
        FT_REQUIRE(drain(ascending_heap) == ascending);
        FT_REQUIRE(drain(descending_heap) == ascending);
        FT_REQUIRE(drain(equal_heap) == equal);
        FT_REQUIRE(drain(shuffled_heap) == ascending);
    });

    tests.add("Brodal randomized meld forest matches one multiset model", [] {
        deterministic_rng random{0x62726f64616c5f32ULL};
        auto base = ft::brodal_okasaki_heap<int>{};
        auto heaps = std::vector<ft::brodal_okasaki_heap<int>>{};
        auto expected = std::multiset<int>{};
        for (auto group = 0; group != 256; ++group) {
            auto heap = base;
            const auto length = random.next_index(64);
            for (auto index = std::size_t{0}; index != length; ++index) {
                const auto value = static_cast<int>(random.next_index(2'001)) - 1'000;
                heap = heap.insert(value);
                expected.insert(value);
            }
            heaps.push_back(std::move(heap));
        }
        while (heaps.size() > 1) {
            const auto index = random.next_index(heaps.size() - 1);
            heaps[index] = heaps[index].meld(heaps[index + 1]);
            heaps.erase(heaps.begin() + static_cast<std::ptrdiff_t>(index + 1));
        }
        require_valid(heaps.front());
        FT_REQUIRE(drain(heaps.front()) == std::vector<int>(expected.begin(), expected.end()));
    });

    tests.add("Brodal insert and meld retain the five-comparison worst-case ceiling", [] {
        auto counter = std::make_shared<std::atomic<std::size_t>>(0);
        auto base = ft::brodal_okasaki_heap<int, counting_less>{counting_less{counter}};
        auto left = base;
        auto right = base;
        for (auto value = 0; value != 50'000; ++value) {
            counter->store(0, std::memory_order_relaxed);
            left = left.insert(value * 2);
            FT_REQUIRE(counter->load(std::memory_order_relaxed) <= 5);
            counter->store(0, std::memory_order_relaxed);
            right = right.insert(value * 2 + 1);
            FT_REQUIRE(counter->load(std::memory_order_relaxed) <= 5);
        }

        counter->store(0, std::memory_order_relaxed);
        const auto melded = left.meld(right);
        FT_REQUIRE(counter->load(std::memory_order_relaxed) >= 1);
        FT_REQUIRE(counter->load(std::memory_order_relaxed) <= 5);
        FT_REQUIRE_EQUAL(melded.size(), std::size_t{100'000});

        counter->store(0, std::memory_order_relaxed);
        (void)left.minimum();
        FT_REQUIRE_EQUAL(counter->load(std::memory_order_relaxed), std::size_t{0});
        for (const auto& value : left) {
            (void)value;
        }
        FT_REQUIRE_EQUAL(counter->load(std::memory_order_relaxed), std::size_t{0});
    });

    tests.add("Brodal policy identity empty behavior and no-op melding are exact", [] {
        auto base = ft::brodal_okasaki_heap<int>{std::less<int>{}};
        auto heap = base.insert(10).insert(5);
        FT_REQUIRE(base.try_minimum() == nullptr);
        FT_REQUIRE(!base.try_delete_minimum().has_value());
        FT_REQUIRE_THROWS(std::logic_error, base.minimum());
        FT_REQUIRE_THROWS(std::logic_error, base.delete_minimum());
        FT_REQUIRE(heap.is_same_version(heap.meld(base)));
        FT_REQUIRE(heap.is_same_version(base.meld(heap)));
        FT_REQUIRE(base.is_same_version(base.clear()));

        const auto incompatible = ft::brodal_okasaki_heap<int>{std::less<int>{}};
        FT_REQUIRE_THROWS(std::invalid_argument, heap.meld(incompatible));
        FT_REQUIRE_THROWS(std::invalid_argument, base.meld(incompatible));
        FT_REQUIRE_EQUAL(heap.minimum(), 5);
        FT_REQUIRE_EQUAL(heap.delete_minimum().minimum(), 10);
    });

    tests.add("Brodal retained randomized history matches persistent multiset snapshots", [] {
        struct version final {
            ft::brodal_okasaki_heap<int> heap;
            std::multiset<int> model;
        };
        deterministic_rng random{0x62726f64616c5f33ULL};
        auto current = version{};
        auto retained = std::vector<version>{current};
        for (auto operation = 0; operation != 15'000; ++operation) {
            const auto& source = retained.size() > 1 && random.next_index(5) == 0
                ? retained[random.next_index(retained.size())]
                : current;
            current = source;
            const auto choice = random.next_index(100);
            if (choice < 55 || current.model.empty()) {
                const auto value = static_cast<int>(random.next_index(257)) - 128;
                current.heap = current.heap.insert(value);
                current.model.insert(value);
            } else if (choice < 80) {
                current.heap = current.heap.delete_minimum();
                current.model.erase(current.model.begin());
            } else {
                const auto& partner = retained[random.next_index(retained.size())];
                if (current.model.size() + partner.model.size() <= 2'048) {
                    current.heap = current.heap.meld(partner.heap);
                    current.model.insert(partner.model.begin(), partner.model.end());
                } else {
                    current.heap = current.heap.delete_minimum();
                    current.model.erase(current.model.begin());
                }
            }
            FT_REQUIRE_EQUAL(current.heap.size(), current.model.size());
            if (!current.model.empty()) {
                FT_REQUIRE_EQUAL(current.heap.minimum(), *current.model.begin());
            }
            if (operation % 127 == 0) {
                require_valid(current.heap);
            }
            if (operation % 97 == 0) {
                if (retained.size() < 128) {
                    retained.push_back(current);
                } else {
                    retained[random.next_index(retained.size())] = current;
                }
            }
        }
        for (auto index = std::size_t{0}; index < retained.size(); index += 11) {
            require_valid(retained[index].heap);
            FT_REQUIRE(drain(retained[index].heap)
                == std::vector<int>(retained[index].model.begin(), retained[index].model.end()));
        }
    });

    tests.add("Brodal updates share exact off-path tree objects", [] {
        const auto base = ft::brodal_okasaki_heap<int>{};
        const auto before = base.insert(0).insert(10);
        const auto after = before.insert(20);
        FT_REQUIRE_EQUAL(before.node_identities().size(), std::size_t{2});
        FT_REQUIRE_EQUAL(after.node_identities().size(), std::size_t{3});
        FT_REQUIRE_EQUAL(before.shared_node_count_with(after), std::size_t{1});

        auto values = std::vector<int>{};
        for (auto value = 0; value != 8'192; ++value) {
            values.push_back(value);
        }
        deterministic_rng random{0x62726f64616c5f34ULL};
        for (auto index = values.size(); index > 1; --index) {
            std::swap(values[index - 1], values[random.next_index(index)]);
        }
        const auto large = ft::brodal_okasaki_heap<int>::from_range(values);
        const auto reduced = large.delete_minimum();
        const auto logarithm = std::bit_width(large.size());
        FT_REQUIRE(large.shared_node_count_with(reduced) > large.size() - 32 * logarithm);
        require_valid(large);
        require_valid(reduced);
    });

    tests.add("Brodal comparer ties retain every distinct representative", [] {
        auto base = ft::brodal_okasaki_heap<tagged, tagged_less>{};
        auto heap = base;
        for (auto identity = 0; identity != 2'048; ++identity) {
            heap = heap.insert(tagged{identity % 7, identity});
        }
        const auto statistics = heap.validate_structure();
        FT_REQUIRE_EQUAL(statistics.count, std::size_t{2'048});
        auto identities = std::vector<int>{};
        auto previous_priority = -1;
        while (!heap.empty()) {
            const auto& minimum = heap.minimum();
            FT_REQUIRE(minimum.priority >= previous_priority);
            previous_priority = minimum.priority;
            identities.push_back(minimum.identity);
            heap = heap.delete_minimum();
        }
        std::ranges::sort(identities);
        for (auto index = 0; index != 2'048; ++index) {
            FT_REQUIRE_EQUAL(identities[static_cast<std::size_t>(index)], index);
        }
    });

    tests.add("Brodal move-only values support insertion moved bulk melding and minimum handles", [] {
        auto base = ft::brodal_okasaki_heap<move_only_item, move_only_less>{};
        auto left = base.insert(move_only_item{3, 30}).insert(move_only_item{1, 10});
        auto values = std::vector<move_only_item>{};
        values.emplace_back(4, 40);
        values.emplace_back(2, 20);
        auto right = base.insert_range(std::move(values));
        auto heap = left.meld(right);
        const auto retained_minimum = heap.minimum_handle();
        FT_REQUIRE_EQUAL(retained_minimum->identity, 10);

        const auto deletion = heap.try_delete_minimum();
        FT_REQUIRE(deletion.has_value());
        FT_REQUIRE(deletion->first == retained_minimum);
        FT_REQUIRE_EQUAL(deletion->first->identity, 10);
        FT_REQUIRE_EQUAL(deletion->second.size(), std::size_t{3});
        FT_REQUIRE_EQUAL(deletion->second.minimum().identity, 20);

        auto priorities = std::vector<int>{};
        while (!heap.empty()) {
            priorities.push_back(heap.minimum().priority);
            heap = heap.delete_minimum();
        }
        FT_REQUIRE(priorities == std::vector<int>({1, 2, 3, 4}));
        FT_REQUIRE_EQUAL(retained_minimum->identity, 10);
    });

    tests.add("Brodal delete-min comparison and allocation growth remain logarithmic", [] {
        for (const auto count : {1'024, 4'096, 16'384}) {
            auto counter = std::make_shared<std::atomic<std::size_t>>(0);
            auto base = ft::brodal_okasaki_heap<int, counting_less>{counting_less{counter}};
            auto heap = base;
            for (auto value = 0; value != count; ++value) {
                heap = heap.insert(value);
            }
            counter->store(0, std::memory_order_relaxed);
            allocation_counting_scope allocations;
            const auto reduced = heap.delete_minimum();
            const auto logarithm = static_cast<std::size_t>(
                std::bit_width(static_cast<std::size_t>(count)));
            FT_REQUIRE(counter->load(std::memory_order_relaxed) <= 32 * logarithm + 8);
#ifndef FINGERTREE_DISABLE_ALLOCATION_TRACKING
            FT_REQUIRE(allocations.allocations() <= 64 * logarithm + 32);
#endif
            FT_REQUIRE_EQUAL(reduced.size(), static_cast<std::size_t>(count - 1));
        }
    });

    tests.add("Brodal throwing comparisons preserve every published snapshot", [] {
        auto should_throw = std::make_shared<std::atomic<bool>>(false);
        auto base = ft::brodal_okasaki_heap<int, throwing_less>{throwing_less{should_throw}};
        const auto left = base.insert(10).insert(20).insert(5);
        const auto right = base.insert(7).insert(30);
        const auto identity = left.root_identity();
        should_throw->store(true, std::memory_order_relaxed);
        FT_REQUIRE_THROWS(std::runtime_error, left.insert(1));
        FT_REQUIRE_THROWS(std::runtime_error, left.meld(right));
        should_throw->store(false, std::memory_order_relaxed);
        FT_REQUIRE_EQUAL(left.root_identity(), identity);
        FT_REQUIRE(drain(left) == std::vector<int>({5, 10, 20}));
        require_valid(right);
    });

    tests.add("Brodal immutable readers safely share retained snapshots", [] {
        auto values = std::vector<int>{};
        for (auto value = 0; value != 20'000; ++value) {
            values.push_back(value);
        }
        const auto heap = ft::brodal_okasaki_heap<int>::from_range(values);
        constexpr auto thread_count = std::size_t{8};
        auto results = std::array<bool, thread_count>{};
        auto threads = std::vector<std::thread>{};
        for (auto index = std::size_t{0}; index != thread_count; ++index) {
            threads.emplace_back([&, index] {
                auto checksum = std::int64_t{0};
                for (const auto value : heap) {
                    checksum += value;
                }
                const auto statistics = heap.validate_structure();
                results[index] = heap.minimum() == 0
                    && statistics.count == heap.size()
                    && checksum == 199'990'000LL;
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
        FT_REQUIRE(std::ranges::all_of(results, [](const bool value) { return value; }));
    });

    tests.add("Brodal self-meld validates a shared DAG as doubled logical occurrences", [] {
        auto heap = ft::brodal_okasaki_heap<int>{};
        for (auto value = 0; value != 1'024; ++value) {
            heap = heap.insert(value);
        }
        const auto melded = heap.meld(heap);
        FT_REQUIRE_EQUAL(melded.size(), std::size_t{2'048});
        FT_REQUIRE(melded.node_identities().size() < melded.size());
        require_valid(melded);
        auto expected = std::vector<int>{};
        expected.reserve(2'048);
        for (auto value = 0; value != 1'024; ++value) {
            expected.push_back(value);
            expected.push_back(value);
        }
        FT_REQUIRE(drain(melded) == expected);
    });
}

} // namespace

void add_brodal_okasaki_heap_tests(suite& tests)
{
    add_brodal_okasaki_heap_tests_impl(tests);
}
