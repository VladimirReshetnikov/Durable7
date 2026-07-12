#include <tools/data_structures/finger_tree/finger_tree.hpp>

#include "test_support/allocation_counter.hpp"
#include "test_support/command_model.hpp"
#include "test_support/test_runner.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace ft = tools::data_structures::finger_tree;
using namespace tools::data_structures::finger_tree::tests;

namespace {

using int_queue = ft::priority_search_queue<int, int, int>;
using int_entry = ft::priority_search_entry<int, int, int>;

static_assert(std::forward_iterator<int_queue::const_iterator>);
static_assert(std::ranges::forward_range<const int_queue>);

template <class Queue>
void require_valid(const Queue& queue)
{
    const auto statistics = queue.validate_structure();
    FT_REQUIRE_EQUAL(statistics.count, queue.size());
    FT_REQUIRE_EQUAL(statistics.height, queue.height());
    FT_REQUIRE(statistics.maximum_absolute_balance_factor <= 1);
}

[[nodiscard]] std::vector<std::tuple<int, int, int>> values_of(const int_queue& queue)
{
    auto result = std::vector<std::tuple<int, int, int>>{};
    result.reserve(queue.size());
    for (const auto& entry : queue) {
        result.emplace_back(entry.key(), entry.priority(), entry.value());
    }
    return result;
}

[[nodiscard]] std::vector<std::tuple<int, int, int>> model_values(
    const std::map<int, std::pair<int, int>>& model)
{
    auto result = std::vector<std::tuple<int, int, int>>{};
    result.reserve(model.size());
    for (const auto& [key, priority_and_value] : model) {
        result.emplace_back(key, priority_and_value.first, priority_and_value.second);
    }
    return result;
}

[[nodiscard]] auto model_minimum(const std::map<int, std::pair<int, int>>& model)
{
    return std::ranges::min_element(model, {}, [](const auto& pair) {
        return std::pair{pair.second.first, pair.first};
    });
}

struct insensitive_less final {
    [[nodiscard]] bool operator()(const std::string& left, const std::string& right) const
    {
        return std::lexicographical_compare(
            left.begin(),
            left.end(),
            right.begin(),
            right.end(),
            [](const char first, const char second) {
                const auto lower_first = static_cast<unsigned char>(first >= 'A' && first <= 'Z'
                    ? first - 'A' + 'a'
                    : first);
                const auto lower_second = static_cast<unsigned char>(second >= 'A' && second <= 'Z'
                    ? second - 'A' + 'a'
                    : second);
                return lower_first < lower_second;
            });
    }
};

struct priority_probe final {
    int rank;
    int equality_class;

    friend bool operator==(const priority_probe& left, const priority_probe& right)
    {
        return left.equality_class == right.equality_class;
    }
};

struct priority_probe_less final {
    [[nodiscard]] bool operator()(const priority_probe& left, const priority_probe& right) const
    {
        return left.rank < right.rank;
    }
};

struct priority_bucket_less final {
    [[nodiscard]] bool operator()(const priority_probe& left, const priority_probe& right) const
    {
        return left.rank / 10 < right.rank / 10;
    }
};

struct counting_less final {
    std::shared_ptr<std::atomic<std::size_t>> calls;

    [[nodiscard]] bool operator()(const int left, const int right) const
    {
        calls->fetch_add(1, std::memory_order_relaxed);
        return left < right;
    }
};

struct throwing_less final {
    std::shared_ptr<std::atomic<bool>> should_throw;

    [[nodiscard]] bool operator()(const int left, const int right) const
    {
        if (should_throw->load(std::memory_order_relaxed)) {
            throw std::runtime_error("injected priority-search comparison failure");
        }
        return left < right;
    }
};

struct equality_probe final {
    int value;
    std::shared_ptr<std::atomic<bool>> should_throw;

    friend bool operator==(const equality_probe& left, const equality_probe& right)
    {
        if (left.should_throw->load(std::memory_order_relaxed)
            || right.should_throw->load(std::memory_order_relaxed)) {
            throw std::runtime_error("injected priority-search equality failure");
        }
        return left.value == right.value;
    }
};

struct equality_probe_less final {
    [[nodiscard]] bool operator()(const equality_probe& left, const equality_probe& right) const
    {
        return left.value < right.value;
    }
};

struct throwing_payload final {
    int value;
    std::shared_ptr<std::atomic<bool>> should_throw;

    throwing_payload(int payload_value, std::shared_ptr<std::atomic<bool>> flag)
        : value(payload_value)
        , should_throw(std::move(flag))
    {
    }

    throwing_payload(const throwing_payload& other)
        : value(other.value)
        , should_throw(other.should_throw)
    {
        if (should_throw->load(std::memory_order_relaxed)) {
            throw std::runtime_error("injected priority-search payload copy failure");
        }
    }

    throwing_payload(throwing_payload&& other)
        : value(other.value)
        , should_throw(std::move(other.should_throw))
    {
        if (should_throw->load(std::memory_order_relaxed)) {
            throw std::runtime_error("injected priority-search payload move failure");
        }
    }

    throwing_payload& operator=(const throwing_payload&) = delete;
    throwing_payload& operator=(throwing_payload&&) = delete;

    friend bool operator==(const throwing_payload& left, const throwing_payload& right)
    {
        return left.value == right.value;
    }
};

struct move_only_component final {
    int value;

    explicit move_only_component(const int component_value)
        : value(component_value)
    {
    }

    move_only_component(const move_only_component&) = delete;
    move_only_component& operator=(const move_only_component&) = delete;
    move_only_component(move_only_component&&) noexcept = default;
    move_only_component& operator=(move_only_component&&) noexcept = default;

    friend bool operator==(const move_only_component&, const move_only_component&) = default;
};

struct move_only_less final {
    [[nodiscard]] bool operator()(
        const move_only_component& left,
        const move_only_component& right) const
    {
        return left.value < right.value;
    }
};

[[nodiscard]] std::uint32_t reverse_bits(std::uint32_t value)
{
    value = ((value & 0x5555'5555U) << 1U) | ((value >> 1U) & 0x5555'5555U);
    value = ((value & 0x3333'3333U) << 2U) | ((value >> 2U) & 0x3333'3333U);
    value = ((value & 0x0F0F'0F0FU) << 4U) | ((value >> 4U) & 0x0F0F'0F0FU);
    value = ((value & 0x00FF'00FFU) << 8U) | ((value >> 8U) & 0x00FF'00FFU);
    return (value << 16U) | (value >> 16U);
}

void add_priority_search_queue_tests_impl(suite& tests)
{
    tests.add("Priority-search keyed updates minimum views and source snapshots are exact", [] {
        const auto source = int_queue{}
            .set_item(3, 3, 30)
            .set_item(1, 1, 10)
            .set_item(2, 1, 20)
            .set_item(4, 4, 40);
        FT_REQUIRE((values_of(source) == std::vector<std::tuple<int, int, int>>({
            {1, 1, 10}, {2, 1, 20}, {3, 3, 30}, {4, 4, 40}})));
        FT_REQUIRE_EQUAL(source.minimum().key(), 1);
        FT_REQUIRE(source.try_minimum()->is_same_entry(source.minimum()));
        FT_REQUIRE_EQUAL(source.try_get_entry(3)->value(), 30);
        FT_REQUIRE(source.try_get_entry(99) == nullptr);

        const auto reprioritized = source.set_item(3, 0, 300);
        FT_REQUIRE_EQUAL(reprioritized.minimum().key(), 3);
        FT_REQUIRE_EQUAL(source.minimum().key(), 1);
        const auto deleted = reprioritized.delete_minimum();
        FT_REQUIRE(deleted.entry.is_same_entry(reprioritized.minimum()));
        FT_REQUIRE_EQUAL(deleted.entry.key(), 3);
        FT_REQUIRE_EQUAL(deleted.remainder.minimum().key(), 1);
        FT_REQUIRE_EQUAL(deleted.remainder.size(), std::size_t{3});

        FT_REQUIRE(source.is_same_version(source.set_item(3, 3, 30)));
        FT_REQUIRE(source.is_same_version(source.remove(99)));
        FT_REQUIRE_THROWS(std::logic_error, int_queue{}.minimum());
        FT_REQUIRE_THROWS(std::logic_error, int_queue{}.delete_minimum());
        require_valid(source);
        require_valid(reprioritized);
        require_valid(deleted.remainder);
    });

    tests.add("Priority-search first key representative survives replacement add lookup and removal", [] {
        using queue_type = ft::priority_search_queue<std::string, int, std::string, insensitive_less>;
        auto queue = queue_type{insensitive_less{}, std::less<int>{}}
            .set_item(std::string{"Alpha"}, 9, std::string{"first"});
        const auto original = queue.try_get_entry_handle("ALPHA");
        FT_REQUIRE(original.has_value());
        const auto original_key = original->key_handle();

        const auto updated = queue.set_item(std::string{"alpha"}, 2, std::string{"second"});
        const auto stored = updated.try_get_entry_handle("aLpHa");
        FT_REQUIRE(stored.has_value());
        FT_REQUIRE(stored->key_handle() == original_key);
        FT_REQUIRE_EQUAL(stored->priority(), 2);
        FT_REQUIRE_EQUAL(stored->value(), std::string{"second"});

        const auto duplicate = updated.try_add(
            std::string{"ALPHA"},
            0,
            std::string{"third"});
        FT_REQUIRE(!duplicate.added);
        FT_REQUIRE(duplicate.queue.is_same_version(updated));
        const auto removed = updated.try_remove("alpha");
        FT_REQUIRE(removed.removed());
        FT_REQUIRE(removed.entry->key_handle() == original_key);
        FT_REQUIRE(removed.queue.empty());
        FT_REQUIRE(updated.key_comparer_policy() == queue.key_comparer_policy());

        auto entries = std::vector<queue_type::entry_type>{};
        entries.emplace_back(std::string{"Bravo"}, 8, std::string{"old"});
        entries.emplace_back(std::string{"BRAVO"}, 1, std::string{"new"});
        const auto first_bulk_key = entries.front().key_handle();
        const auto bulk = queue_type::from_range(
            entries,
            insensitive_less{},
            std::less<int>{});
        FT_REQUIRE(bulk.try_get_entry("bravo")->key_handle() == first_bulk_key);
        FT_REQUIRE_EQUAL(bulk.try_get_entry("bravo")->priority(), 1);
        FT_REQUIRE_EQUAL(bulk.try_get_entry("bravo")->value(), std::string{"new"});
        require_valid(queue);
        require_valid(updated);
        require_valid(bulk);
    });

    tests.add("Priority-search replacement no-op requires priority order and ordinary equalities", [] {
        using queue_type = ft::priority_search_queue<
            int,
            priority_probe,
            std::string,
            std::less<int>,
            priority_probe_less>;
        const auto original_priority = priority_probe{10, 1};
        const auto queue = queue_type{std::less<int>{}, priority_probe_less{}}
            .set_item(7, original_priority, std::string{"payload"});

        const auto comparer_equal_but_unequal = priority_probe{10, 2};
        const auto represented = queue.set_item(
            7,
            comparer_equal_but_unequal,
            std::string{"payload"});
        FT_REQUIRE(!represented.is_same_version(queue));
        FT_REQUIRE_EQUAL(represented.try_get_entry(7)->priority().equality_class, 2);
        FT_REQUIRE_EQUAL(queue.minimum().priority().equality_class, 1);

        const auto ordinary_equal_but_reordered = priority_probe{1, 2};
        const auto reprioritized = represented.set_item(
            7,
            ordinary_equal_but_reordered,
            std::string{"payload"});
        FT_REQUIRE(!reprioritized.is_same_version(represented));
        FT_REQUIRE(reprioritized.is_same_version(reprioritized.set_item(
            7,
            ordinary_equal_but_reordered,
            std::string{"payload"})));
        require_valid(queue);
        require_valid(represented);
        require_valid(reprioritized);
    });

    tests.add("Priority-search AVL rotations ascending growth and deletion rebalancing stay valid", [] {
        for (const auto order : {
                 std::array{3, 2, 1},
                 std::array{1, 2, 3},
                 std::array{3, 1, 2},
                 std::array{1, 3, 2}}) {
            auto queue = int_queue{};
            for (const auto key : order) {
                queue = queue.set_item(key, key, -key);
            }
            FT_REQUIRE_EQUAL(queue.height(), std::size_t{2});
            require_valid(queue);
        }

        constexpr auto count = 50'000;
        auto ascending = int_queue{};
        for (auto key = 0; key != count; ++key) {
            ascending = ascending.set_item(key, key % 257, -key);
        }
        require_valid(ascending);
        FT_REQUIRE_EQUAL(ascending.size(), static_cast<std::size_t>(count));
        FT_REQUIRE(ascending.height() <= 2 * static_cast<std::size_t>(
            std::bit_width(static_cast<std::size_t>(count))));
        FT_REQUIRE_EQUAL(ascending.begin()->key(), 0);

        auto insertion_order = std::vector<int>{};
        insertion_order.reserve(2'048);
        for (auto key = 0; key != 2'048; ++key) {
            insertion_order.push_back(key);
        }
        std::ranges::sort(insertion_order, std::ranges::less{}, [](const int key) {
            return reverse_bits(static_cast<std::uint32_t>(key));
        });
        auto reduced = int_queue{};
        for (const auto key : insertion_order) {
            reduced = reduced.set_item(key, (key * 37) % 101, -key);
        }
        for (auto key = 0; key != 2'048; ++key) {
            if (key % 3 != 1) {
                reduced = reduced.remove(key);
            }
            if ((key & 63) == 0) {
                require_valid(reduced);
            }
        }
        auto expected_key = 1;
        for (const auto& entry : reduced) {
            FT_REQUIRE_EQUAL(entry.key(), expected_key);
            expected_key += 3;
        }
        require_valid(reduced);
    });

    tests.add("Priority-search retained twenty-thousand-step history matches the ordered model", [] {
        deterministic_rng random{0x7073715f72657431ULL};
        auto queue = int_queue{};
        auto model = std::map<int, std::pair<int, int>>{};
        struct snapshot final {
            int_queue queue;
            std::vector<std::tuple<int, int, int>> values;
        };
        auto retained = std::vector<snapshot>{};

        for (auto step = 0; step != 20'000; ++step) {
            const auto key = static_cast<int>(random.next_index(1'537)) - 768;
            switch (random.next_index(8)) {
            case 0:
            case 1:
            case 2:
            case 3: {
                const auto priority = static_cast<int>(random.next_index(64));
                queue = queue.set_item(key, priority, step);
                model[key] = {priority, step};
                break;
            }
            case 4:
                queue = queue.remove(key);
                model.erase(key);
                break;
            case 5: {
                const auto priority = static_cast<int>(random.next_index(64));
                const auto expected_added = !model.contains(key);
                const auto result = queue.try_add(key, priority, step);
                FT_REQUIRE_EQUAL(result.added, expected_added);
                queue = result.queue;
                if (expected_added) {
                    model.emplace(key, std::pair{priority, step});
                }
                break;
            }
            default:
                if (model.empty()) {
                    queue = queue.set_item(key, 0, step);
                    model[key] = {0, step};
                } else {
                    const auto expected = model_minimum(model);
                    const auto deleted = queue.delete_minimum();
                    FT_REQUIRE_EQUAL(deleted.entry.key(), expected->first);
                    FT_REQUIRE_EQUAL(deleted.entry.priority(), expected->second.first);
                    FT_REQUIRE_EQUAL(deleted.entry.value(), expected->second.second);
                    queue = deleted.remainder;
                    model.erase(expected);
                }
                break;
            }

            if (step % 127 == 0) {
                FT_REQUIRE(values_of(queue) == model_values(model));
                require_valid(queue);
            }
            if (step % 239 == 0) {
                retained.push_back(snapshot{queue, model_values(model)});
            }
        }

        FT_REQUIRE(values_of(queue) == model_values(model));
        require_valid(queue);
        for (const auto& snapshot : retained) {
            FT_REQUIRE(values_of(snapshot.queue) == snapshot.values);
            require_valid(snapshot.queue);
        }
    });

    tests.add("Priority-search range pruning has exact full and impossible comparison equations", [] {
        auto key_calls = std::make_shared<std::atomic<std::size_t>>(0);
        auto priority_calls = std::make_shared<std::atomic<std::size_t>>(0);
        using queue_type = ft::priority_search_queue<
            int,
            int,
            int,
            counting_less,
            counting_less>;
        auto queue = queue_type{
            counting_less{key_calls},
            counting_less{priority_calls}};
        constexpr auto count = 4'095;
        for (auto key = 0; key != count; ++key) {
            queue = queue.set_item(key, key % 17, -key);
        }
        require_valid(queue);

        key_calls->store(0, std::memory_order_relaxed);
        priority_calls->store(0, std::memory_order_relaxed);
        FT_REQUIRE(queue.enumerate_at_most(0, count - 1, -1).empty());
        FT_REQUIRE_EQUAL(key_calls->load(std::memory_order_relaxed), std::size_t{1});
        FT_REQUIRE_EQUAL(priority_calls->load(std::memory_order_relaxed), std::size_t{1});

        key_calls->store(0, std::memory_order_relaxed);
        priority_calls->store(0, std::memory_order_relaxed);
        const auto all = queue.enumerate_at_most(0, count - 1, 16);
        FT_REQUIRE_EQUAL(all.size(), static_cast<std::size_t>(count));
        FT_REQUIRE_EQUAL(
            key_calls->load(std::memory_order_relaxed),
            std::size_t{1} + 2 * static_cast<std::size_t>(count));
        FT_REQUIRE_EQUAL(
            priority_calls->load(std::memory_order_relaxed),
            2 * static_cast<std::size_t>(count));
        for (auto index = std::size_t{0}; index != all.size(); ++index) {
            FT_REQUIRE_EQUAL(all[index].key(), static_cast<int>(index));
        }
        FT_REQUIRE_THROWS(std::invalid_argument, queue.enumerate_at_most(2, 1, 0));
    });

    tests.add("Priority-search optional components never collide with outer result absence", [] {
        using optional_type = std::optional<int>;
        using queue_type = ft::priority_search_queue<optional_type, optional_type, optional_type>;
        const auto queue = queue_type{}
            .set_item(optional_type{}, optional_type{}, optional_type{})
            .set_item(optional_type{1}, optional_type{2}, optional_type{});
        const auto found = queue.try_get_entry_handle(optional_type{});
        FT_REQUIRE(found.has_value());
        FT_REQUIRE(!found->key().has_value());
        FT_REQUIRE(!found->priority().has_value());
        FT_REQUIRE(!found->value().has_value());
        FT_REQUIRE(queue.try_minimum().has_value());
        FT_REQUIRE(!queue.try_minimum()->key().has_value());
        const auto removed = queue.try_remove(optional_type{});
        FT_REQUIRE(removed.entry.has_value());
        FT_REQUIRE(!removed.entry->key().has_value());
        FT_REQUIRE(!removed.entry->priority().has_value());
        FT_REQUIRE(!removed.entry->value().has_value());
        FT_REQUIRE(!removed.queue.try_get_entry_handle(optional_type{}).has_value());
        require_valid(queue);
        require_valid(removed.queue);
    });

    tests.add("Priority-search move-only key priority and payload retain exact shared components", [] {
        using queue_type = ft::priority_search_queue<
            move_only_component,
            move_only_component,
            move_only_component,
            move_only_less,
            move_only_less>;
        auto queue = queue_type{move_only_less{}, move_only_less{}}
            .set_item(
                move_only_component{2},
                move_only_component{20},
                move_only_component{200})
            .set_item(
                move_only_component{1},
                move_only_component{10},
                move_only_component{100});
        const auto key = move_only_component{2};
        const auto original = queue.try_get_entry_handle(key);
        FT_REQUIRE(original.has_value());
        const auto original_key = original->key_handle();

        queue = queue.set_item(
            move_only_component{2},
            move_only_component{5},
            move_only_component{250});
        const auto updated = queue.try_get_entry_handle(key);
        FT_REQUIRE(updated->key_handle() == original_key);
        FT_REQUIRE_EQUAL(updated->priority().value, 5);
        FT_REQUIRE_EQUAL(updated->value().value, 250);
        const auto duplicate = queue.try_add(
            move_only_component{2},
            move_only_component{1},
            move_only_component{1});
        FT_REQUIRE(!duplicate.added);
        FT_REQUIRE(duplicate.queue.is_same_version(queue));

        const auto lower = move_only_component{1};
        const auto upper = move_only_component{2};
        const auto threshold = move_only_component{20};
        const auto range = queue.enumerate_at_most(lower, upper, threshold);
        FT_REQUIRE_EQUAL(range.size(), std::size_t{2});

        const auto minimum = queue.delete_minimum();
        FT_REQUIRE(minimum.entry.key_handle() == original_key);
        const auto retained_priority = minimum.entry.priority_handle();
        const auto retained_value = minimum.entry.value_handle();
        queue = minimum.remainder.clear();
        FT_REQUIRE_EQUAL(retained_priority->value, 5);
        FT_REQUIRE_EQUAL(retained_value->value, 250);
        require_valid(queue);
    });

    tests.add("Priority-search priority ties drain by retained key order", [] {
        using queue_type = ft::priority_search_queue<
            int,
            priority_probe,
            std::string,
            std::greater<int>,
            priority_bucket_less>;
        auto queue = queue_type{std::greater<int>{}, priority_bucket_less{}};
        for (const auto key : {2, 8, 0, 6, 9, 1, 7, 3, 5, 4}) {
            queue = queue.set_item(
                key,
                priority_probe{10 + key, 100 + key},
                std::string{"v"} + std::to_string(key));
        }
        for (auto key = 9; key >= 0; --key) {
            FT_REQUIRE_EQUAL(queue.minimum().key(), key);
            const auto deletion = queue.delete_minimum();
            FT_REQUIRE_EQUAL(deletion.entry.key(), key);
            FT_REQUIRE_EQUAL(deletion.entry.value(), std::string{"v"} + std::to_string(key));
            queue = deletion.remainder;
            require_valid(queue);
        }
        FT_REQUIRE(queue.empty());
    });

    tests.add("Priority-search path copying and allocation growth remain logarithmic", [] {
        constexpr auto count = 16'384;
        auto queue = int_queue{};
        for (auto key = 0; key != count; ++key) {
            queue = queue.set_item(key, key + 10, -key);
        }
        require_valid(queue);

        allocation_counting_scope update_allocations;
        const auto updated = queue.set_item(0, -1, 42);
        const auto update_count = update_allocations.allocations();
        const auto logarithm = static_cast<std::size_t>(
            std::bit_width(static_cast<std::size_t>(count)));
#ifndef FINGERTREE_DISABLE_ALLOCATION_TRACKING
        FT_REQUIRE(update_count <= 16 * logarithm + 16);
#endif
        FT_REQUIRE(queue.shared_node_count_with(updated) > queue.size() - 4 * logarithm);
        FT_REQUIRE(!queue.is_same_version(updated));
        FT_REQUIRE_EQUAL(queue.try_get_entry(0)->priority(), 10);
        FT_REQUIRE_EQUAL(updated.try_get_entry(0)->priority(), -1);

        allocation_counting_scope absent_allocations;
        const auto unchanged = queue.remove(-1);
#ifndef FINGERTREE_DISABLE_ALLOCATION_TRACKING
        FT_REQUIRE_EQUAL(absent_allocations.allocations(), std::size_t{0});
#endif
        FT_REQUIRE(unchanged.is_same_version(queue));
        require_valid(updated);
    });

    tests.add("Priority-search comparator equality and payload exceptions preserve snapshots", [] {
        auto comparison_flag = std::make_shared<std::atomic<bool>>(false);
        using comparison_queue = ft::priority_search_queue<
            int,
            int,
            int,
            throwing_less,
            throwing_less>;
        const auto compared = comparison_queue{
            throwing_less{comparison_flag},
            throwing_less{comparison_flag}}
            .set_item(1, 10, 100)
            .set_item(2, 20, 200);
        const auto compared_root = compared.root_identity();
        comparison_flag->store(true, std::memory_order_relaxed);
        FT_REQUIRE_THROWS(std::runtime_error, compared.set_item(3, 5, 300));
        FT_REQUIRE_THROWS(std::runtime_error, compared.remove(1));
        FT_REQUIRE_THROWS(std::runtime_error, compared.enumerate_at_most(0, 3, 30));
        comparison_flag->store(false, std::memory_order_relaxed);
        FT_REQUIRE_EQUAL(compared.root_identity(), compared_root);
        require_valid(compared);

        auto priority_flag = std::make_shared<std::atomic<bool>>(false);
        using priority_comparison_queue = ft::priority_search_queue<
            int,
            int,
            int,
            std::less<int>,
            throwing_less>;
        const auto priority_compared = priority_comparison_queue{
            std::less<int>{},
            throwing_less{priority_flag}}
            .set_item(1, 10, 100)
            .set_item(2, 20, 200);
        const auto priority_root = priority_compared.root_identity();
        priority_flag->store(true, std::memory_order_relaxed);
        FT_REQUIRE_THROWS(std::runtime_error, priority_compared.set_item(3, 5, 300));
        priority_flag->store(false, std::memory_order_relaxed);
        FT_REQUIRE_EQUAL(priority_compared.root_identity(), priority_root);
        require_valid(priority_compared);

        auto equality_flag = std::make_shared<std::atomic<bool>>(false);
        using equality_queue = ft::priority_search_queue<
            int,
            equality_probe,
            equality_probe,
            std::less<int>,
            equality_probe_less>;
        const auto equalities = equality_queue{std::less<int>{}, equality_probe_less{}}
            .set_item(
                1,
                equality_probe{10, equality_flag},
                equality_probe{20, equality_flag});
        const auto equality_root = equalities.root_identity();
        equality_flag->store(true, std::memory_order_relaxed);
        FT_REQUIRE_THROWS(
            std::runtime_error,
            equalities.set_item(
                1,
                equality_probe{10, equality_flag},
                equality_probe{20, equality_flag}));
        equality_flag->store(false, std::memory_order_relaxed);
        FT_REQUIRE_EQUAL(equalities.root_identity(), equality_root);
        require_valid(equalities);

        using payload_equality_queue = ft::priority_search_queue<int, int, equality_probe>;
        const auto payload_equalities = payload_equality_queue{}.set_item(
            1,
            10,
            equality_probe{20, equality_flag});
        const auto payload_equality_root = payload_equalities.root_identity();
        equality_flag->store(true, std::memory_order_relaxed);
        FT_REQUIRE_THROWS(
            std::runtime_error,
            payload_equalities.set_item(1, 10, equality_probe{20, equality_flag}));
        equality_flag->store(false, std::memory_order_relaxed);
        FT_REQUIRE_EQUAL(payload_equalities.root_identity(), payload_equality_root);
        require_valid(payload_equalities);

        auto payload_flag = std::make_shared<std::atomic<bool>>(false);
        using payload_queue = ft::priority_search_queue<int, int, throwing_payload>;
        const auto payloads = payload_queue{}.set_item(
            1,
            10,
            throwing_payload{20, payload_flag});
        const auto payload_root = payloads.root_identity();
        payload_flag->store(true, std::memory_order_relaxed);
        FT_REQUIRE_THROWS(
            std::runtime_error,
            payloads.set_item(2, 5, throwing_payload{30, payload_flag}));
        payload_flag->store(false, std::memory_order_relaxed);
        FT_REQUIRE_EQUAL(payloads.root_identity(), payload_root);
        require_valid(payloads);
    });

    tests.add("Priority-search immutable snapshots support concurrent readers", [] {
        constexpr auto count = 20'000;
        auto queue = int_queue{};
        for (auto key = 0; key != count; ++key) {
            queue = queue.set_item(key, key % 101, key * 2);
        }
        constexpr auto thread_count = std::size_t{8};
        auto results = std::array<bool, thread_count>{};
        auto threads = std::vector<std::thread>{};
        for (auto index = std::size_t{0}; index != thread_count; ++index) {
            threads.emplace_back([&, index] {
                auto checksum = std::int64_t{0};
                for (const auto& entry : queue) {
                    checksum += entry.key();
                    checksum += entry.value();
                }
                const auto selected = queue.enumerate_at_most(1'000, 2'000, 3);
                const auto statistics = queue.validate_structure();
                results[index] = checksum == 599'970'000LL
                    && queue.minimum().key() == 0
                    && statistics.count == static_cast<std::size_t>(count)
                    && std::ranges::all_of(selected, [](const auto& entry) {
                           return entry.key() >= 1'000
                               && entry.key() <= 2'000
                               && entry.priority() <= 3;
                       });
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
        FT_REQUIRE(std::ranges::all_of(results, [](const bool result) { return result; }));
    });
}

} // namespace

void add_priority_search_queue_tests(suite& tests)
{
    add_priority_search_queue_tests_impl(tests);
}
