/// Tests for the ancestry-interval persistent queue.
///
/// Two things need pinning that a plain sequence model cannot express. The anchored-empty rule makes
/// "empty" a value with provenance, so a queue drained from the front and one drained from the back
/// must stay distinguishable; and the whole point of the representation is which operations avoid a
/// level-ancestor query, so the backend's query counter is asserted exactly rather than bounded.

#include <durable7/finger_tree/ancestral_slice_queue.hpp>

#include "test_support/command_model.hpp"
#include "test_support/test_runner.hpp"

#include <algorithm>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace durable7::finger_tree;
using namespace durable7::finger_tree::tests;

namespace {

using int_arena = myers_incremental_ancestor_arena<int>;
using int_queue = ancestral_slice_queue<int>;

/// A backend that satisfies the seam but anchors its bottom node at depth zero, which the queue must
/// refuse before it ever navigates the arena.
template <class T>
class flat_bottom_arena final {
public:
    [[nodiscard]] std::size_t bottom() const noexcept { return 0; }

    [[nodiscard]] std::size_t published_node_count() const { return 0; }

    [[nodiscard]] std::size_t add_leaf(std::size_t, T)
    {
        throw std::logic_error("the queue must reject this arena before using it");
    }

    [[nodiscard]] std::ptrdiff_t depth_of(std::size_t) const { return 0; }

    [[nodiscard]] std::size_t parent_of(std::size_t) const
    {
        throw std::logic_error("the queue must reject this arena before using it");
    }

    [[nodiscard]] std::size_t ancestor_at_depth(std::size_t, std::ptrdiff_t) const
    {
        throw std::logic_error("the queue must reject this arena before using it");
    }

    [[nodiscard]] const T& value_at(std::size_t) const
    {
        throw std::logic_error("the queue must reject this arena before using it");
    }
};

[[nodiscard]] std::vector<int> ascending(const std::size_t count)
{
    auto values = std::vector<int>{};
    values.reserve(count);
    for (auto value = std::size_t{0}; value != count; ++value) {
        values.push_back(static_cast<int>(value));
    }
    return values;
}

[[nodiscard]] std::vector<int> slice_of(
    const std::vector<int>& values,
    const std::size_t index,
    const std::size_t count)
{
    return std::vector<int>(
        values.begin() + static_cast<std::ptrdiff_t>(index),
        values.begin() + static_cast<std::ptrdiff_t>(index + count));
}

[[nodiscard]] int_queue queue_of(const std::vector<int>& values)
{
    return int_queue::from_range(values);
}

/// Checks every read of one queue value against a model, including the reads that must fail.
///
/// The audit performs indexed reads, so it must never be used inside a query-count window.
template <class T>
void require_state(const ancestral_slice_queue<T>& queue, const std::vector<T>& expected)
{
    FT_REQUIRE_EQUAL(queue.size(), expected.size());
    FT_REQUIRE_EQUAL(queue.empty(), expected.empty());
    FT_REQUIRE(queue.to_vector() == expected);

    auto seen = std::vector<T>{};
    for (const auto& value : queue) {
        seen.push_back(value);
    }
    FT_REQUIRE(seen == expected);

    for (auto index = std::size_t{0}; index != expected.size(); ++index) {
        FT_REQUIRE(queue.at(index) == expected[index]);
        FT_REQUIRE(queue[index] == expected[index]);
        const auto* const probe = queue.try_get(index);
        FT_REQUIRE(probe != nullptr);
        FT_REQUIRE(*probe == expected[index]);
    }

    FT_REQUIRE(queue.try_get(expected.size()) == nullptr);
    FT_REQUIRE_THROWS(std::out_of_range, queue.at(expected.size()));
    FT_REQUIRE_THROWS(std::out_of_range, queue[expected.size()]);
    FT_REQUIRE_THROWS(std::out_of_range, queue.take(expected.size() + 1));
    FT_REQUIRE_THROWS(std::out_of_range, queue.drop(expected.size() + 1));
    FT_REQUIRE_THROWS(std::out_of_range, queue.split_at(expected.size() + 1));
    FT_REQUIRE_THROWS(std::out_of_range, queue.get_range(expected.size() + 1, 0));

    if (expected.empty()) {
        FT_REQUIRE(queue.try_front() == nullptr);
        FT_REQUIRE(queue.try_back() == nullptr);
        FT_REQUIRE_THROWS(std::logic_error, queue.front());
        FT_REQUIRE_THROWS(std::logic_error, queue.back());
        FT_REQUIRE_THROWS(std::logic_error, queue.remove_first());
        FT_REQUIRE_THROWS(std::logic_error, queue.remove_last());
        FT_REQUIRE(!queue.try_pop_first().has_value());
        FT_REQUIRE(!queue.try_pop_last().has_value());
    } else {
        FT_REQUIRE(queue.front() == expected.front());
        FT_REQUIRE(queue.back() == expected.back());
        FT_REQUIRE(queue.try_front() != nullptr);
        FT_REQUIRE(*queue.try_front() == expected.front());
        FT_REQUIRE(queue.try_back() != nullptr);
        FT_REQUIRE(*queue.try_back() == expected.back());
    }

    const auto statistics = queue.validate_structure();
    FT_REQUIRE_EQUAL(statistics.count, expected.size());
    FT_REQUIRE_EQUAL(
        statistics.tail_depth,
        statistics.low_depth + static_cast<std::ptrdiff_t>(expected.size()) - 1);
    FT_REQUIRE_EQUAL(statistics.anchor_depth, statistics.low_depth - 1);
    FT_REQUIRE_EQUAL(statistics.anchored_at_bottom, statistics.anchor_depth == -1);
    FT_REQUIRE(statistics.arena_published_node_count >= expected.size());
}

} // namespace

void add_ancestral_slice_queue_tests(suite& tests)
{
    static_assert(incremental_ancestor_arena<int_arena, int>);
    static_assert(incremental_ancestor_arena<flat_bottom_arena<int>, int>);
    static_assert(std::same_as<int_queue::arena_type, int_arena>);
    static_assert(std::forward_iterator<int_queue::const_iterator>);
    static_assert(std::ranges::forward_range<int_queue>);
    static_assert(std::copyable<int_queue>);

    tests.add("Ancestral slice queue endpoint and index reads follow the queue contract", [] {
        using optional_queue = ancestral_slice_queue<std::optional<std::string>>;

        const auto empty = optional_queue{};
        require_state(empty, {});
        FT_REQUIRE(empty.begin() == empty.end());
        FT_REQUIRE(empty.cbegin() == empty.cend());

        // The empty-endpoint channel is logic_error, not the out_of_range used for positions.
        auto caught_plain_logic_error = false;
        try {
            (void)empty.front();
        } catch (const std::out_of_range&) {
            // An index-shaped failure would be the wrong channel for an empty endpoint.
        } catch (const std::logic_error&) {
            caught_plain_logic_error = true;
        }
        FT_REQUIRE(caught_plain_logic_error);

        // A stored empty optional is a present value; absence is reported only by the null pointer.
        const auto singleton = empty.push_back(std::optional<std::string>{});
        require_state(singleton, {std::optional<std::string>{}});
        FT_REQUIRE(singleton.try_get(0) != nullptr);
        FT_REQUIRE(!singleton.try_get(0)->has_value());

        const auto pair = singleton.push_back(std::optional<std::string>{"tail"});
        require_state(pair, {std::optional<std::string>{}, std::optional<std::string>{"tail"}});
        require_state(singleton, {std::optional<std::string>{}});

        const auto popped_front = pair.pop_first();
        FT_REQUIRE(!popped_front.value.has_value());
        require_state(popped_front.rest, {std::optional<std::string>{"tail"}});

        const auto popped_back = pair.pop_last();
        FT_REQUIRE(popped_back.value == std::optional<std::string>{"tail"});
        require_state(popped_back.rest, {std::optional<std::string>{}});
        require_state(pair, {std::optional<std::string>{}, std::optional<std::string>{"tail"}});

        const auto tried_front = pair.try_pop_first();
        FT_REQUIRE(tried_front.has_value());
        FT_REQUIRE(!tried_front->value.has_value());
        FT_REQUIRE_EQUAL(tried_front->rest.size(), std::size_t{1});

        const auto tried_back = pair.try_pop_last();
        FT_REQUIRE(tried_back.has_value());
        FT_REQUIRE(tried_back->value == std::optional<std::string>{"tail"});
        FT_REQUIRE_EQUAL(tried_back->rest.size(), std::size_t{1});

        // add_last is the reference contract's spelling of push_back.
        const auto values = ascending(4);
        const auto source = queue_of(values);
        require_state(source.add_last(9), {0, 1, 2, 3, 9});
        require_state(source.push_back(9), {0, 1, 2, 3, 9});
        require_state(source, values);

        // A count that would overflow an unchecked index + count is rejected as a range error.
        FT_REQUIRE_THROWS(
            std::out_of_range,
            source.get_range(2, (std::numeric_limits<std::size_t>::max)()));
        FT_REQUIRE_THROWS(std::out_of_range, source.get_range(3, 2));
        FT_REQUIRE_THROWS(std::out_of_range, source.get_range(5, 0));
        require_state(source, values);
    });

    tests.add("Ancestral slice queue ranges match the model and empty slices stay appendable", [] {
        constexpr auto length = std::size_t{65};
        const auto values = ascending(length);
        const auto source = queue_of(values);

        for (auto start = std::size_t{0}; start <= length; ++start) {
            for (auto count = std::size_t{0}; count <= length - start; ++count) {
                const auto slice = source.get_range(start, count);
                require_state(slice, slice_of(values, start, count));

                if (count != 0) {
                    continue;
                }

                // The anchored-empty rule: appending to any empty slice yields exactly the new
                // values and never resurrects the discarded prefix.
                const auto marker = static_cast<int>(start);
                const auto continued = slice.push_back(-marker).push_back(10'000 + marker);
                require_state(continued, {-marker, 10'000 + marker});
                require_state(slice, {});
            }
        }

        // The three whole-window spellings agree and reuse the retained tail rather than naming a
        // new node for it.
        const auto whole = source.get_range(0, source.size());
        const auto prefix = source.take(source.size());
        const auto suffix = source.drop(0);
        require_state(whole, values);
        require_state(prefix, values);
        require_state(suffix, values);
        FT_REQUIRE(whole.shares_tail_with(source));
        FT_REQUIRE(prefix.shares_tail_with(source));
        FT_REQUIRE(suffix.shares_tail_with(source));
        FT_REQUIRE(whole.shares_arena_with(source));
        FT_REQUIRE_EQUAL(whole.low_depth(), source.low_depth());
        require_state(source, values);
    });

    tests.add("Ancestral slice queue take, drop, and split partition at every boundary", [] {
        constexpr auto length = std::size_t{129};
        const auto values = ascending(length);
        const auto source = queue_of(values);

        for (auto position = std::size_t{0}; position <= length; ++position) {
            const auto prefix = source.take(position);
            const auto suffix = source.drop(position);
            require_state(prefix, slice_of(values, 0, position));
            require_state(suffix, slice_of(values, position, length - position));

            const auto split = source.split_at(position);
            require_state(split.left, slice_of(values, 0, position));
            require_state(split.right, slice_of(values, position, length - position));

            // Both results remain real ancestry slices and start independent branches.
            const auto marker = -static_cast<int>(position) - 1;
            auto expected_prefix = slice_of(values, 0, position);
            expected_prefix.push_back(marker);
            require_state(prefix.push_back(marker), expected_prefix);

            auto expected_suffix = slice_of(values, position, length - position);
            expected_suffix.push_back(marker);
            require_state(suffix.push_back(marker), expected_suffix);
        }

        require_state(source, values);
    });

    tests.add("Ancestral slice queue square block boundaries support index, range, and branching", [] {
        constexpr auto maximum_root = std::size_t{32};
        const auto length = maximum_root * maximum_root + 1;
        const auto values = ascending(length);
        const auto source = queue_of(values);

        for (auto root = std::size_t{0}; root <= maximum_root; ++root) {
            const auto square = root * root;
            FT_REQUIRE_EQUAL(source.at(square), static_cast<int>(square));

            const auto start = square == 0 ? std::size_t{0} : square - 1;
            const auto count = std::min(std::size_t{3}, source.size() - start);
            require_state(source.get_range(start, count), slice_of(values, start, count));

            const auto marker = -static_cast<int>(square) - 1;
            require_state(source.get_range(square, 0).push_back(marker), {marker});

            if (square != 0) {
                // The queue length equals the tail handle here, so the versions of length
                // square - 1 and square end immediately before and at the odd-block seam.
                auto before_seam = slice_of(values, 0, square - 1);
                before_seam.push_back(marker - 1);
                require_state(source.take(square - 1).push_back(marker - 1), before_seam);

                auto at_seam = slice_of(values, 0, square);
                at_seam.push_back(marker - 2);
                require_state(source.take(square).push_back(marker - 2), at_seam);
            }

            if (root == maximum_root) {
                continue;
            }

            const auto next_square = (root + 1) * (root + 1);
            FT_REQUIRE_EQUAL(next_square - square, 2 * root + 1);
            require_state(
                source.get_range(square, next_square - square),
                slice_of(values, square, next_square - square));
        }

        require_state(source, values);
    });

    tests.add("Ancestral slice queue operations respect the parameterized backend query boundary", [] {
        const auto arena = std::make_shared<int_arena>();
        auto queue = int_queue::create(arena);
        for (auto value = 0; value != 16; ++value) {
            queue = queue.push_back(value);
        }

        const auto require_delta = [&arena](
                                       const std::uint64_t expected_adds,
                                       const std::uint64_t expected_queries,
                                       auto&& action) {
            const auto before = arena->statistics();
            action();
            const auto after = arena->statistics();
            FT_REQUIRE_EQUAL(after.add_leaf_count - before.add_leaf_count, expected_adds);
            FT_REQUIRE_EQUAL(
                after.ancestor_query_count - before.ancestor_query_count,
                expected_queries);
            FT_REQUIRE_EQUAL(
                after.published_node_count - before.published_node_count,
                static_cast<std::size_t>(expected_adds));
        };

        // Cached and tail-only reads consult no ancestry at all.
        require_delta(0, 0, [&] {
            FT_REQUIRE_EQUAL(queue.size(), std::size_t{16});
            FT_REQUIRE(!queue.empty());
            FT_REQUIRE_EQUAL(queue.back(), 15);
            FT_REQUIRE(queue.try_back() != nullptr);
            FT_REQUIRE_EQUAL(queue.at(15), 15);
            FT_REQUIRE_EQUAL(*queue.try_get(15), 15);
            FT_REQUIRE_EQUAL(queue.validate_structure().count, std::size_t{16});
            FT_REQUIRE_EQUAL(queue.to_vector().size(), std::size_t{16});
        });
        require_delta(0, 1, [&] { FT_REQUIRE_EQUAL(queue.front(), 0); });
        require_delta(0, 1, [&] { FT_REQUIRE_EQUAL(queue.at(7), 7); });
        require_delta(0, 0, [&] { FT_REQUIRE_EQUAL(queue.remove_first().size(), std::size_t{15}); });
        require_delta(0, 0, [&] { FT_REQUIRE_EQUAL(queue.remove_last().size(), std::size_t{15}); });
        require_delta(0, 0, [&] { FT_REQUIRE_EQUAL(queue.drop(7).size(), std::size_t{9}); });
        require_delta(0, 1, [&] { FT_REQUIRE_EQUAL(queue.take(7).size(), std::size_t{7}); });
        require_delta(0, 1, [&] { FT_REQUIRE_EQUAL(queue.get_range(3, 5).size(), std::size_t{5}); });

        // Whole-window and suffix spellings derive their tail without a query.
        require_delta(0, 0, [&] { FT_REQUIRE(queue.take(queue.size()).shares_tail_with(queue)); });
        require_delta(0, 0, [&] { FT_REQUIRE(queue.drop(0).shares_tail_with(queue)); });
        require_delta(0, 0, [&] {
            FT_REQUIRE(queue.get_range(0, queue.size()).shares_tail_with(queue));
        });
        require_delta(0, 0, [&] { FT_REQUIRE(queue.get_range(3, 13).shares_tail_with(queue)); });
        require_delta(0, 0, [&] { FT_REQUIRE(queue.get_range(16, 0).shares_tail_with(queue)); });

        // An empty prefix must name the node before the window, which needs a query.
        require_delta(0, 1, [&] { FT_REQUIRE(queue.get_range(0, 0).empty()); });
        require_delta(0, 1, [&] {
            const auto split = queue.split_at(7);
            FT_REQUIRE_EQUAL(split.left.size(), std::size_t{7});
            FT_REQUIRE_EQUAL(split.right.size(), std::size_t{9});
        });
        require_delta(0, 1, [&] {
            const auto split = queue.split_at(0);
            FT_REQUIRE_EQUAL(split.left.size(), std::size_t{0});
            FT_REQUIRE_EQUAL(split.right.size(), std::size_t{16});
            FT_REQUIRE(split.right.shares_tail_with(queue));
        });
        require_delta(0, 0, [&] {
            const auto split = queue.split_at(queue.size());
            FT_REQUIRE(split.left.shares_tail_with(queue));
            FT_REQUIRE(split.right.shares_tail_with(queue));
            FT_REQUIRE(split.right.empty());
        });

        require_delta(0, 1, [&] {
            const auto popped = queue.pop_first();
            FT_REQUIRE_EQUAL(popped.value, 0);
            FT_REQUIRE_EQUAL(popped.rest.size(), std::size_t{15});
        });
        require_delta(0, 0, [&] {
            const auto popped = queue.pop_last();
            FT_REQUIRE_EQUAL(popped.value, 15);
            FT_REQUIRE_EQUAL(popped.rest.size(), std::size_t{15});
        });
        require_delta(1, 0, [&] {
            const auto appended = queue.push_back(16);
            FT_REQUIRE_EQUAL(appended.size(), std::size_t{17});
            FT_REQUIRE_EQUAL(appended.back(), 16);
        });

        // Iteration walks parent links rather than issuing one query per position.
        require_delta(0, 0, [&] {
            auto total = 0;
            for (const auto value : queue) {
                total += value;
            }
            FT_REQUIRE_EQUAL(total, 120);
        });

        // A singleton reuses its retained tail for the front.
        const auto singleton = queue.get_range(15, 1);
        require_delta(0, 0, [&] { FT_REQUIRE_EQUAL(singleton.front(), 15); });

        // On an empty queue both split boundaries coincide with the whole range, so every boundary
        // spelling is query-free there.
        const auto drained = queue.drop(queue.size());
        require_delta(0, 0, [&] {
            const auto split = drained.split_at(0);
            FT_REQUIRE(split.left.shares_tail_with(drained));
            FT_REQUIRE(split.right.shares_tail_with(drained));
            FT_REQUIRE(drained.take(0).shares_tail_with(drained));
            FT_REQUIRE(drained.drop(0).shares_tail_with(drained));
            FT_REQUIRE(drained.get_range(0, 0).shares_tail_with(drained));
        });

        require_state(queue, ascending(16));
    });

    tests.add("Ancestral slice queue factories reject an unanchored arena and stay independent", [] {
        const auto arena = std::make_shared<int_arena>();
        const auto explicitly_seeded = int_queue::create(arena).push_back(10).push_back(20);
        require_state(explicitly_seeded, {10, 20});
        FT_REQUIRE_EQUAL(arena->published_node_count(), std::size_t{2});
        FT_REQUIRE(explicitly_seeded.arena() == arena);
        FT_REQUIRE(explicitly_seeded.arena_identity() == arena.get());

        require_state(int_queue::from_range(std::vector{1, 2, 3}), {1, 2, 3});
        require_state(int_queue{1, 2, 3}, {1, 2, 3});
        require_state(int_queue{1, 2, 3}.push_back(4), {1, 2, 3, 4});

        // Each convenience factory owns a private arena, so unrelated queues share no history.
        const auto first = int_queue{}.push_back(1);
        const auto second = int_queue{}.push_back(2);
        require_state(first, {1});
        require_state(second, {2});
        FT_REQUIRE(!first.shares_arena_with(second));
        FT_REQUIRE(first.arena_identity() != second.arena_identity());
        FT_REQUIRE(!first.shares_tail_with(second));

        FT_REQUIRE_THROWS(std::invalid_argument, int_queue::create(nullptr));
        using flat_queue = ancestral_slice_queue<int, flat_bottom_arena<int>>;
        FT_REQUIRE_THROWS(
            std::invalid_argument,
            flat_queue::create(std::make_shared<flat_bottom_arena<int>>()));
    });

    tests.add("Ancestral slice queue anchored-empty rule keeps front and back drains distinct", [] {
        constexpr auto length = std::size_t{256};
        const auto values = ascending(length);
        const auto source = queue_of(values);

        auto front_versions = std::vector<int_queue>{source};
        auto back_versions = std::vector<int_queue>{source};
        for (auto removed = std::size_t{0}; removed != length; ++removed) {
            front_versions.push_back(front_versions.back().remove_first());
            back_versions.push_back(back_versions.back().remove_last());
        }

        for (auto removed = std::size_t{0}; removed <= length; ++removed) {
            require_state(front_versions[removed], slice_of(values, removed, length - removed));
            require_state(back_versions[removed], slice_of(values, 0, length - removed));
        }

        // Both fully drained versions denote the empty sequence, yet keep different anchors.
        const auto& front_drained = front_versions[length];
        const auto& back_drained = back_versions[length];
        FT_REQUIRE(front_drained.empty());
        FT_REQUIRE(back_drained.empty());
        FT_REQUIRE(front_drained.shares_arena_with(back_drained));
        FT_REQUIRE(!front_drained.shares_tail_with(back_drained));

        const auto front_statistics = front_drained.validate_structure();
        const auto back_statistics = back_drained.validate_structure();
        FT_REQUIRE_EQUAL(front_statistics.anchor_depth, static_cast<std::ptrdiff_t>(length) - 1);
        FT_REQUIRE(!front_statistics.anchored_at_bottom);
        FT_REQUIRE_EQUAL(back_statistics.anchor_depth, std::ptrdiff_t{-1});
        FT_REQUIRE(back_statistics.anchored_at_bottom);

        require_state(front_drained.push_back(-1), {-1});
        require_state(back_drained.push_back(-2), {-2});

        // A singleton emptied from either end also appends to exactly one visible value.
        const auto singleton = int_queue{}.push_back(7);
        require_state(singleton.remove_first().push_back(8), {8});
        require_state(singleton.remove_last().push_back(9), {9});
        require_state(singleton, {7});
        require_state(source, values);
    });

    tests.add("Ancestral slice queue retained branches from sliced versions stay independent", [] {
        const auto values = ascending(200);
        const auto source = queue_of(values);
        const auto middle = source.get_range(50, 100);
        const auto captured = middle.begin();

        const auto branches = std::vector<int_queue>{
            middle.push_back(-1),
            middle.push_back(-2).push_back(-3),
            middle.remove_first().push_back(-4),
            middle.remove_last().push_back(-5),
            middle.take(40).push_back(-6),
            middle.drop(60).push_back(-7),
            middle.get_range(30, 0).push_back(-8),
        };

        const auto expect = [&values](
                                const std::size_t index,
                                const std::size_t count,
                                const std::vector<int>& appended) {
            auto expected = slice_of(values, index, count);
            expected.insert(expected.end(), appended.begin(), appended.end());
            return expected;
        };

        require_state(branches[0], expect(50, 100, {-1}));
        require_state(branches[1], expect(50, 100, {-2, -3}));
        require_state(branches[2], expect(51, 99, {-4}));
        require_state(branches[3], expect(50, 99, {-5}));
        require_state(branches[4], expect(50, 40, {-6}));
        require_state(branches[5], expect(110, 40, {-7}));
        require_state(branches[6], expect(0, 0, {-8}));
        require_state(middle, slice_of(values, 50, 100));
        require_state(source, values);

        // The iterator captured before any branch existed still yields the original path.
        auto captured_values = std::vector<int>{};
        for (auto position = captured; position != middle.end(); ++position) {
            captured_values.push_back(*position);
        }
        FT_REQUIRE(captured_values == slice_of(values, 50, 100));
    });

    tests.add("Ancestral slice queue values copy, move, and outlive their producer", [] {
        const auto source = queue_of(ascending(8));
        const auto copied = source;
        FT_REQUIRE(copied.shares_tail_with(source));
        FT_REQUIRE(copied.shares_arena_with(source));
        require_state(copied, ascending(8));

        auto donor = source;
        const auto moved = std::move(donor);
        require_state(moved, ascending(8));

        // Moving copies the shared arena handle, so the source of a move stays fully usable.
        require_state(donor, ascending(8));
        FT_REQUIRE(donor.shares_tail_with(moved));

        auto assigned = int_queue{};
        assigned = source;
        require_state(assigned, ascending(8));
        assigned = int_queue{99};
        require_state(assigned, {99});
        FT_REQUIRE(!assigned.shares_arena_with(source));

        // An iterator retains the arena and the captured path, so it outlives its producer.
        auto escaped = [] { return queue_of(ascending(5)).begin(); }();
        auto escaped_values = std::vector<int>{};
        while (escaped != int_queue{}.end()) {
            escaped_values.push_back(*escaped);
            ++escaped;
        }
        FT_REQUIRE(escaped_values == ascending(5));

        // The captured path is multipass: a copy keeps its own position.
        const auto queue = queue_of(ascending(4));
        auto position = queue.begin();
        const auto snapshot = position;
        ++position;
        FT_REQUIRE_EQUAL(*position, 1);
        FT_REQUIRE_EQUAL(*snapshot, 0);
        FT_REQUIRE_EQUAL(*position++, 1);
        FT_REQUIRE_EQUAL(*position, 2);
    });

    tests.add("Ancestral slice queue iterators compare by position within one queue version", [] {
        const auto values = ascending(6);
        const auto source = queue_of(values);

        // Equality must name the logical position within one queue version. Which begin() call
        // minted the captured path buffer is an implementation detail, so two iterators obtained
        // from separate begin() calls on one queue value are the same iterator.
        FT_REQUIRE(source.begin() == source.begin());
        FT_REQUIRE(source.cbegin() == source.begin());
        FT_REQUIRE(source.begin() != source.end());
        FT_REQUIRE(source.begin() != source.cend());

        auto first = source.begin();
        auto second = source.begin();
        for (auto step = std::size_t{0}; step != values.size(); ++step) {
            FT_REQUIRE(first == second);
            FT_REQUIRE_EQUAL(*first, values[step]);
            FT_REQUIRE(std::addressof(*first) == std::addressof(*second));
            ++first;
            ++second;
        }
        FT_REQUIRE(first == second);
        FT_REQUIRE(first == source.end());

        // Advancing one of two equal iterators separates them again.
        auto lagging = source.begin();
        auto leading = source.begin();
        ++leading;
        FT_REQUIRE(lagging != leading);
        ++lagging;
        FT_REQUIRE(lagging == leading);

        // Multipass: a copy taken before the first walk repeats that walk exactly, and the walk
        // leaves the queue's own begin() unchanged.
        auto walker = source.begin();
        const auto snapshot = walker;
        auto first_pass = std::vector<int>{};
        while (walker != source.end()) {
            first_pass.push_back(*walker);
            ++walker;
        }
        FT_REQUIRE(first_pass == values);

        auto replay = snapshot;
        auto second_pass = std::vector<int>{};
        while (replay != source.end()) {
            second_pass.push_back(*replay);
            ++replay;
        }
        FT_REQUIRE(second_pass == values);
        FT_REQUIRE(snapshot == source.begin());
        FT_REQUIRE(walker == replay);

        // Queues denoting different intervals never share a position, whether they differ in tail,
        // in low boundary, or in both while holding equally many values.
        const auto shorter_prefix = source.take(4);
        const auto later_suffix = source.drop(2);
        const auto shifted_window = source.get_range(1, 4);
        FT_REQUIRE(source.begin() != shorter_prefix.begin());
        FT_REQUIRE(source.begin() != later_suffix.begin());
        FT_REQUIRE(shorter_prefix.begin() != shifted_window.begin());
        FT_REQUIRE(shorter_prefix.begin() == source.take(4).begin());

        // An unrelated history is never mistaken for this one, even holding equal values.
        const auto unrelated = queue_of(values);
        FT_REQUIRE(!unrelated.shares_arena_with(source));
        FT_REQUIRE(source.begin() != unrelated.begin());

        // Every exhausted iterator is the one end value, whatever produced it.
        FT_REQUIRE(source.end() == unrelated.end());
        FT_REQUIRE(int_queue{}.begin() == source.end());

        require_state(source, values);
    });

    tests.add("Ancestral slice queue randomized branching history matches vector models", [] {
        struct history final {
            int_queue queue;
            std::vector<int> model;
        };

        deterministic_rng random{0x414E4345535452ULL};
        auto histories = std::vector<history>{history{int_queue{}, {}}};

        for (auto step = 0; step != 800; ++step) {
            const auto selected = random.next_index(histories.size());
            const auto source = histories[selected].queue;
            const auto model = histories[selected].model;
            const auto length = model.size();
            const auto step_value = step;

            auto result = source;
            auto expected = model;
            switch (random.next_index(9)) {
            case 1:
                if (length != 0) {
                    result = source.remove_first();
                    expected = slice_of(model, 1, length - 1);
                    break;
                }
                [[fallthrough]];
            case 2:
                if (length != 0) {
                    result = source.remove_last();
                    expected = slice_of(model, 0, length - 1);
                    break;
                }
                [[fallthrough]];
            case 3: {
                const auto count = random.next_index(length + 1);
                result = source.take(count);
                expected = slice_of(model, 0, count);
                break;
            }
            case 4: {
                const auto count = random.next_index(length + 1);
                result = source.drop(count);
                expected = slice_of(model, count, length - count);
                break;
            }
            case 5: {
                const auto start = random.next_index(length + 1);
                const auto count = random.next_index(length - start + 1);
                result = source.get_range(start, count);
                expected = slice_of(model, start, count);
                break;
            }
            case 6: {
                const auto position = random.next_index(length + 1);
                const auto split = source.split_at(position);
                if (random.next_index(2) == 0) {
                    result = split.left;
                    expected = slice_of(model, 0, position);
                } else {
                    result = split.right;
                    expected = slice_of(model, position, length - position);
                }
                break;
            }
            case 7:
                if (length != 0) {
                    const auto popped = source.pop_first();
                    FT_REQUIRE_EQUAL(popped.value, model.front());
                    result = popped.rest;
                    expected = slice_of(model, 1, length - 1);
                    break;
                }
                [[fallthrough]];
            case 8:
                if (length != 0) {
                    const auto popped = source.pop_last();
                    FT_REQUIRE_EQUAL(popped.value, model.back());
                    result = popped.rest;
                    expected = slice_of(model, 0, length - 1);
                    break;
                }
                [[fallthrough]];
            default:
                if (length < 96) {
                    result = source.push_back(step_value);
                    expected.push_back(step_value);
                }
                break;
            }

            require_state(source, model);
            require_state(result, expected);
            histories.push_back(history{result, expected});

            if (step % 53 == 0) {
                const auto& sample = histories[random.next_index(histories.size())];
                require_state(sample.queue, sample.model);
            }
        }

        for (auto index = std::size_t{0}; index < histories.size(); index += 37) {
            require_state(histories[index].queue, histories[index].model);
        }
    });

    tests.add("Ancestral slice queue concurrent branches publish complete independent versions", [] {
        constexpr auto thread_count = std::size_t{4};
        constexpr auto branches_per_thread = std::size_t{8};

        const auto values = ascending(256);
        const auto source = queue_of(values);
        auto published = std::vector<std::vector<std::pair<int, int_queue>>>(thread_count);
        auto failed = std::atomic<bool>{false};

        {
            auto workers = std::vector<std::jthread>{};
            for (auto worker = std::size_t{0}; worker != thread_count; ++worker) {
                workers.emplace_back([&, worker] {
                    try {
                        for (auto offset = std::size_t{0}; offset != branches_per_thread; ++offset) {
                            const auto marker =
                                -static_cast<int>(worker * branches_per_thread + offset) - 1;
                            const auto branch = source.push_back(marker);
                            if (branch.size() != values.size() + 1 || branch.back() != marker) {
                                throw std::logic_error("concurrent branch is incomplete");
                            }

                            // Read level ancestors while sibling nodes are published elsewhere.
                            for (auto index = offset % 31; index < values.size(); index += 61) {
                                if (source.at(index) != values[index]) {
                                    throw std::logic_error("concurrent ancestor read mismatch");
                                }
                            }
                            if (source.get_range(100, 40).to_vector()
                                != slice_of(values, 100, 40)) {
                                throw std::logic_error("concurrent range read mismatch");
                            }

                            published[worker].emplace_back(marker, branch);
                        }
                    } catch (...) {
                        failed.store(true, std::memory_order_relaxed);
                    }
                });
            }
        }

        FT_REQUIRE(!failed.load(std::memory_order_relaxed));
        auto branch_count = std::size_t{0};
        for (const auto& worker_results : published) {
            for (const auto& [marker, branch] : worker_results) {
                auto expected = values;
                expected.push_back(marker);
                require_state(branch, expected);
                ++branch_count;
            }
        }
        FT_REQUIRE_EQUAL(branch_count, thread_count * branches_per_thread);
        require_state(source, values);
    });
}
