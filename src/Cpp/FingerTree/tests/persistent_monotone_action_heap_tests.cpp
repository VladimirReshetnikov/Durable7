/// Tests for the persistent Brodal-Okasaki heap with monotone priority actions.
///
/// Checks the action algebra's laws as well as the heap operations, because an action that does not
/// compose in the advertised direction, or a pending tag that leaks past the moment it was applied,
/// produces wrong priorities only after a later push-down.

#include <durable7/finger_tree/persistent_monotone_action_heap.hpp>

#include "test_support/allocation_counter.hpp"
#include "test_support/command_model.hpp"
#include "test_support/test_runner.hpp"

#include <algorithm>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace ft = durable7::finger_tree;
using namespace durable7::finger_tree::tests;

namespace {

// ---------------------------------------------------------------------------------------------
// Policies and heap spellings under test
// ---------------------------------------------------------------------------------------------

using clamp_policy = ft::order_clamp_policy<int>;
using clamp_action = clamp_policy::action_type;
using int_heap = ft::order_clamp_action_heap<int, int>;
using string_heap = ft::order_clamp_action_heap<std::string, int>;

/// A priority whose comparison ignores half of its state, so that a clamp boundary and an
/// equivalent input remain distinguishable representatives.
struct labeled final {
    int priority = 0;
    int identity = 0;

    friend bool operator==(const labeled&, const labeled&) = default;
};

struct labeled_less final {
    [[nodiscard]] bool operator()(const labeled& left, const labeled& right) const noexcept
    {
        return left.priority < right.priority;
    }
};

using labeled_policy = ft::order_clamp_policy<labeled, labeled_less>;
using labeled_action = labeled_policy::action_type;
using labeled_heap = ft::persistent_monotone_action_heap<std::string, labeled, labeled_policy>;

/// A second, unrelated action family. It exists to show that the action abstraction is a policy
/// point rather than the clamp family in disguise, and that two families never meet in one heap.
struct offset_policy final {
    using action_type = std::int64_t;

    [[nodiscard]] static bool less(const std::int64_t left, const std::int64_t right) noexcept
    {
        return left < right;
    }
    [[nodiscard]] static action_type identity_action() noexcept { return 0; }
    [[nodiscard]] static bool is_identity(const action_type action) noexcept { return action == 0; }
    [[nodiscard]] static action_type compose(const action_type outer, const action_type inner) noexcept
    {
        return outer + inner;
    }
    [[nodiscard]] static std::int64_t apply(const action_type action, const std::int64_t priority) noexcept
    {
        return priority + action;
    }
};

using offset_heap = ft::persistent_monotone_action_heap<int, std::int64_t, offset_policy>;

/// Whether two heap spellings can meet in one meld. The action family is a type, so this is the
/// compile-time form of the managed ports' retained-policy-identity gate.
template <class Left, class Right>
concept meldable = requires(const Left& left, const Right& right) { left.meld(right); };

/// Whether a heap accepts an action drawn from some family.
template <class Heap, class Action>
concept transformable = requires(const Heap& heap, const Action& action) {
    heap.transform_all(action);
};

// ---------------------------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------------------------

template <class Heap>
void require_valid(const Heap& heap)
{
    const auto statistics = heap.validate_structure();
    FT_REQUIRE_EQUAL(statistics.count, heap.size());
    if (heap.empty()) {
        FT_REQUIRE_EQUAL(statistics.root_forest_length, std::size_t{0});
        FT_REQUIRE_EQUAL(statistics.maximum_rank, std::size_t{0});
        FT_REQUIRE_EQUAL(statistics.maximum_depth, std::size_t{0});
        FT_REQUIRE_EQUAL(statistics.tagged_component_count, std::size_t{0});
    } else {
        FT_REQUIRE(statistics.maximum_depth > 0);
        FT_REQUIRE(statistics.root_forest_length <= statistics.count);
    }
}

/// Removes minima until the heap is empty, returning the priorities in removal order.
template <class Heap>
[[nodiscard]] std::vector<typename Heap::priority_type> drain_priorities(Heap heap)
{
    auto result = std::vector<typename Heap::priority_type>{};
    result.reserve(heap.size());
    while (!heap.empty()) {
        const auto view = heap.try_delete_minimum();
        FT_REQUIRE(view.has_value());
        result.push_back(*view->first.priority);
        heap = view->second;
    }
    return result;
}

/// Every priority the version holds, taken through iteration rather than repeated deletion.
template <class Heap>
[[nodiscard]] std::vector<typename Heap::priority_type> iterated_priorities(const Heap& heap)
{
    auto result = std::vector<typename Heap::priority_type>{};
    for (const auto& entry : heap) {
        result.push_back(*entry.priority);
    }
    return result;
}

[[nodiscard]] std::vector<int> sorted_copy(std::vector<int> values)
{
    std::ranges::sort(values);
    return values;
}

[[nodiscard]] int_heap heap_of(const std::vector<int>& priorities)
{
    auto heap = int_heap{};
    for (const auto priority : priorities) {
        heap = heap.insert(priority, priority);
    }
    return heap;
}

[[nodiscard]] std::vector<int> clamp_range(const int first, const int last)
{
    auto result = std::vector<int>{};
    for (auto value = first; value <= last; ++value) {
        result.push_back(value);
    }
    return result;
}

void require_clamp_equivalent(
    const clamp_action& expected,
    const clamp_action& actual,
    const std::vector<int>& samples)
{
    for (const auto sample : samples) {
        FT_REQUIRE_EQUAL(clamp_policy::apply(actual, sample), clamp_policy::apply(expected, sample));
    }
}

[[nodiscard]] std::vector<clamp_action> clamp_action_grid()
{
    return {
        clamp_policy::identity_action(),
        clamp_policy::at_least(-3),
        clamp_policy::at_least(8),
        clamp_policy::at_most(-4),
        clamp_policy::at_most(11),
        clamp_policy::between(-6, 9),
        clamp_policy::between(5, 5),
        clamp_policy::constant(-9),
        clamp_policy::constant(4),
    };
}

} // namespace

void add_persistent_monotone_action_heap_tests(suite& tests)
{
    static_assert(ft::monotone_heap_action_policy<clamp_policy, int>);
    static_assert(ft::monotone_heap_action_policy<labeled_policy, labeled>);
    static_assert(ft::monotone_heap_action_policy<offset_policy, std::int64_t>);
    static_assert(ft::monotone_action_heap_types<int, int, clamp_policy>);
    static_assert(std::input_iterator<int_heap::const_iterator>);
    static_assert(!std::forward_iterator<int_heap::const_iterator>);
    static_assert(std::ranges::input_range<int_heap>);
    static_assert(std::copyable<int_heap>);

    // The action family is a type, so the managed ports' retained-policy-identity gate on melding
    // becomes a compile-time property: two families can never meet in one operation.
    static_assert(!std::same_as<int_heap, offset_heap>);
    static_assert(meldable<int_heap, int_heap>);
    static_assert(!meldable<int_heap, offset_heap>);
    static_assert(!meldable<int_heap, string_heap>);
    static_assert(transformable<int_heap, clamp_action>);
    static_assert(!transformable<int_heap, offset_policy::action_type>);
    static_assert(transformable<offset_heap, offset_policy::action_type>);

    tests.add("Monotone-action clamp algebra pins identity, direction, and collapse cases", [] {
        const auto actions = clamp_action_grid();
        auto samples = clamp_range(-20, 20);
        samples.push_back((std::numeric_limits<int>::min)());
        samples.push_back((std::numeric_limits<int>::max)());
        const auto identity = clamp_policy::identity_action();

        FT_REQUIRE(clamp_policy::is_identity(identity));
        FT_REQUIRE(!clamp_policy::is_identity(clamp_policy::at_least(0)));
        FT_REQUIRE(!clamp_policy::is_identity(clamp_policy::at_most(0)));
        FT_REQUIRE(!clamp_policy::is_identity(clamp_policy::constant(0)));
        FT_REQUIRE(!clamp_policy::is_identity(clamp_policy::between(0, 1)));
        for (const auto& action : actions) {
            require_clamp_equivalent(action, clamp_policy::compose(identity, action), samples);
            require_clamp_equivalent(action, clamp_policy::compose(action, identity), samples);
            // An identity operand short-circuits to the other operand exactly.
            FT_REQUIRE(clamp_policy::compose(identity, action) == action);
            FT_REQUIRE(clamp_policy::compose(action, identity) == action);
        }

        // compose(outer, inner) is outer(inner(p)), so the newer action is the outer one and a cap
        // composed after a floor collapses to the cap's boundary.
        const auto floor_then_cap = clamp_policy::compose(clamp_policy::at_most(5), clamp_policy::at_least(10));
        const auto cap_then_floor = clamp_policy::compose(clamp_policy::at_least(10), clamp_policy::at_most(5));
        FT_REQUIRE(floor_then_cap == clamp_policy::constant(5));
        FT_REQUIRE(cap_then_floor == clamp_policy::constant(10));
        FT_REQUIRE(floor_then_cap.is_constant());
        FT_REQUIRE(cap_then_floor.is_constant());
        FT_REQUIRE_EQUAL(*floor_then_cap.constant_value(), 5);
        FT_REQUIRE_EQUAL(*cap_then_floor.constant_value(), 10);
        for (const auto sample : samples) {
            FT_REQUIRE_EQUAL(clamp_policy::apply(floor_then_cap, sample), 5);
            FT_REQUIRE_EQUAL(clamp_policy::apply(cap_then_floor, sample), 10);
        }

        // An outer constant wins outright; an inner constant gets the outer action applied to it.
        FT_REQUIRE(clamp_policy::compose(clamp_policy::constant(9), clamp_policy::at_most(3))
            == clamp_policy::constant(9));
        FT_REQUIRE(clamp_policy::compose(clamp_policy::at_most(3), clamp_policy::constant(9))
            == clamp_policy::constant(3));
        FT_REQUIRE(clamp_policy::compose(clamp_policy::at_least(20), clamp_policy::constant(9))
            == clamp_policy::constant(20));
        FT_REQUIRE(clamp_policy::compose(clamp_policy::between(0, 4), clamp_policy::constant(9))
            == clamp_policy::constant(4));

        // Overlapping bounds intersect rather than collapsing.
        FT_REQUIRE(clamp_policy::compose(clamp_policy::at_most(7), clamp_policy::at_least(3))
            == clamp_policy::between(3, 7));
        FT_REQUIRE(clamp_policy::compose(clamp_policy::between(0, 10), clamp_policy::between(4, 20))
            == clamp_policy::between(4, 10));

        for (const auto& inner : actions) {
            for (const auto& outer : actions) {
                const auto composed = clamp_policy::compose(outer, inner);
                for (const auto sample : samples) {
                    FT_REQUIRE_EQUAL(
                        clamp_policy::apply(composed, sample),
                        clamp_policy::apply(outer, clamp_policy::apply(inner, sample)));
                }
                // Composition stays inside the closed family: a composed clamp is never crossed.
                if (!composed.is_constant() && composed.has_lower_bound() && composed.has_upper_bound()) {
                    FT_REQUIRE(!clamp_policy::less(*composed.upper_bound(), *composed.lower_bound()));
                }
                for (const auto& oldest : actions) {
                    require_clamp_equivalent(
                        clamp_policy::compose(clamp_policy::compose(outer, inner), oldest),
                        clamp_policy::compose(outer, clamp_policy::compose(inner, oldest)),
                        samples);
                }
            }
            // Every action must be monotone, which is what makes lazy tagging sound.
            for (auto index = std::size_t{1}; index < samples.size(); ++index) {
                const auto low = (std::min)(samples[index - 1], samples[index]);
                const auto high = (std::max)(samples[index - 1], samples[index]);
                FT_REQUIRE(!clamp_policy::less(
                    clamp_policy::apply(inner, high),
                    clamp_policy::apply(inner, low)));
            }
        }

        FT_REQUIRE_THROWS(std::invalid_argument, clamp_policy::between(4, 3));
        FT_REQUIRE(clamp_policy::between(5, 5) == clamp_policy::between(5, 5));
        FT_REQUIRE_EQUAL(clamp_policy::apply(clamp_policy::between(2, 6), 1), 2);
        FT_REQUIRE_EQUAL(clamp_policy::apply(clamp_policy::between(2, 6), 9), 6);
        FT_REQUIRE_EQUAL(clamp_policy::apply(clamp_policy::between(2, 6), 4), 4);
        FT_REQUIRE(!clamp_policy::identity_action().is_constant());
        FT_REQUIRE(!clamp_policy::identity_action().has_lower_bound());
        FT_REQUIRE(!clamp_policy::identity_action().has_upper_bound());
        FT_REQUIRE(!clamp_policy::at_least(1).constant_value().has_value());
        FT_REQUIRE(!clamp_policy::at_least(1).upper_bound().has_value());
        FT_REQUIRE_EQUAL(*clamp_policy::at_least(1).lower_bound(), 1);
        FT_REQUIRE_EQUAL(*clamp_policy::at_most(2).upper_bound(), 2);
    });

    tests.add("Monotone-action clamp collapse keeps the intended boundary representative", [] {
        const auto older_low = labeled{5, 100};
        const auto newer_low = labeled{5, 200};
        const auto older_high = labeled{9, 300};
        const auto newer_high = labeled{9, 400};

        // Equal boundaries intersect keeping the inner (older) representative.
        const auto lowers = labeled_policy::compose(
            labeled_policy::at_least(newer_low),
            labeled_policy::at_least(older_low));
        FT_REQUIRE(lowers.lower_bound().has_value());
        FT_REQUIRE_EQUAL(lowers.lower_bound()->identity, 100);
        const auto uppers = labeled_policy::compose(
            labeled_policy::at_most(newer_high),
            labeled_policy::at_most(older_high));
        FT_REQUIRE(uppers.upper_bound().has_value());
        FT_REQUIRE_EQUAL(uppers.upper_bound()->identity, 300);

        // Disjoint bounds collapse to a constant carrying the outer (newer) boundary.
        const auto low_after_high = labeled_policy::compose(
            labeled_policy::at_least(labeled{7, 11}),
            labeled_policy::at_most(labeled{3, 22}));
        FT_REQUIRE(low_after_high.is_constant());
        FT_REQUIRE((*low_after_high.constant_value() == labeled{7, 11}));
        const auto high_after_low = labeled_policy::compose(
            labeled_policy::at_most(labeled{3, 33}),
            labeled_policy::at_least(labeled{7, 44}));
        FT_REQUIRE(high_after_low.is_constant());
        FT_REQUIRE((*high_after_low.constant_value() == labeled{3, 33}));

        // An inner constant is transformed by the outer action; an outer constant wins outright.
        const auto transformed_constant = labeled_policy::compose(
            labeled_policy::at_most(labeled{3, 55}),
            labeled_policy::constant(labeled{9, 66}));
        FT_REQUIRE((*transformed_constant.constant_value() == labeled{3, 55}));
        const auto outer_constant = labeled_policy::compose(
            labeled_policy::constant(labeled{9, 77}),
            labeled_policy::at_most(labeled{3, 88}));
        FT_REQUIRE((*outer_constant.constant_value() == labeled{9, 77}));

        // An inclusive [k, k] clamp preserves an equivalent input; the exact constant does not.
        const auto inclusive = labeled_policy::between(labeled{5, 1}, labeled{5, 2});
        FT_REQUIRE((labeled_policy::apply(inclusive, labeled{5, 9}) == labeled{5, 9}));
        FT_REQUIRE((labeled_policy::apply(inclusive, labeled{4, 9}) == labeled{5, 1}));
        FT_REQUIRE((labeled_policy::apply(inclusive, labeled{6, 9}) == labeled{5, 2}));
        FT_REQUIRE((labeled_policy::apply(labeled_policy::constant(labeled{5, 1}), labeled{5, 9})
            == labeled{5, 1}));
        FT_REQUIRE_THROWS(
            std::invalid_argument,
            (labeled_policy::between(labeled{6, 0}, labeled{5, 0})));

        // The heap carries the same representative distinction through a whole-heap transform.
        auto heap = labeled_heap{}
                        .insert("low", labeled{1, 10})
                        .insert("high", labeled{20, 20});
        heap = heap.transform_all(labeled_policy::constant(labeled{5, 999}));
        require_valid(heap);
        for (const auto& entry : heap) {
            FT_REQUIRE((*entry.priority == labeled{5, 999}));
        }
    });

    tests.add("Monotone-action composition direction survives every tag push-down site", [] {
        auto model = std::vector<int>{};
        for (auto value = 0; value != 200; ++value) {
            model.push_back((value * 37) % 200);
        }
        const auto base = heap_of(model);
        require_valid(base);

        // A cap composed after a floor must collapse to the cap's boundary at every site that
        // pushes a pending tag down: the root tag itself, the exposed root, the exposed forest
        // suffixes, and the logical heads reached during a deletion.
        const auto collapsed = base.transform_all(clamp_policy::at_least(10))
                                   .transform_all(clamp_policy::at_most(5));
        require_valid(collapsed);
        FT_REQUIRE_EQUAL(collapsed.size(), base.size());
        FT_REQUIRE_EQUAL(*collapsed.minimum().priority, 5);
        for (const auto priority : iterated_priorities(collapsed)) {
            FT_REQUIRE_EQUAL(priority, 5);
        }
        FT_REQUIRE(drain_priorities(collapsed) == std::vector<int>(base.size(), 5));

        const auto reversed = base.transform_all(clamp_policy::at_most(5))
                                  .transform_all(clamp_policy::at_least(10));
        require_valid(reversed);
        FT_REQUIRE(drain_priorities(reversed) == std::vector<int>(base.size(), 10));

        // The same pin, but observed only after deletions have forced the tag through the
        // split-forest decomposition and the skew meld.
        auto deleted = collapsed;
        for (auto step = 0; step != 64; ++step) {
            deleted = deleted.delete_minimum();
            if (step % 13 == 0) {
                require_valid(deleted);
            }
        }
        FT_REQUIRE_EQUAL(deleted.size(), base.size() - 64);
        for (const auto priority : iterated_priorities(deleted)) {
            FT_REQUIRE_EQUAL(priority, 5);
        }

        // An overlapping pair intersects rather than collapsing, and must agree with applying the
        // two actions in the same order to every original priority.
        const auto floor_action = clamp_policy::at_least(40);
        const auto cap_action = clamp_policy::at_most(120);
        const auto windowed = base.transform_all(floor_action).transform_all(cap_action);
        require_valid(windowed);
        auto expected = std::vector<int>{};
        for (const auto priority : model) {
            expected.push_back(clamp_policy::apply(cap_action, clamp_policy::apply(floor_action, priority)));
        }
        FT_REQUIRE(drain_priorities(windowed) == sorted_copy(expected));

        // A deletion in the middle of a chain of transforms keeps the remaining entries correct.
        auto interleaved = base;
        auto interleaved_model = std::multiset<int>(model.begin(), model.end());
        const auto steps = std::vector<clamp_action>{
            clamp_policy::at_least(25),
            clamp_policy::at_most(150),
            clamp_policy::at_least(60),
            clamp_policy::between(70, 130),
            clamp_policy::at_most(90),
        };
        for (const auto& action : steps) {
            interleaved = interleaved.transform_all(action);
            auto next_model = std::multiset<int>{};
            for (const auto priority : interleaved_model) {
                next_model.insert(clamp_policy::apply(action, priority));
            }
            interleaved_model = std::move(next_model);
            for (auto removal = 0; removal != 5; ++removal) {
                interleaved = interleaved.delete_minimum();
                interleaved_model.erase(interleaved_model.begin());
            }
            require_valid(interleaved);
            FT_REQUIRE_EQUAL(*interleaved.minimum().priority, *interleaved_model.begin());
            FT_REQUIRE(sorted_copy(iterated_priorities(interleaved))
                == std::vector<int>(interleaved_model.begin(), interleaved_model.end()));
        }
        FT_REQUIRE(drain_priorities(interleaved)
            == std::vector<int>(interleaved_model.begin(), interleaved_model.end()));

        // Every source stayed intact.
        FT_REQUIRE(drain_priorities(base) == sorted_copy(model));
    });

    tests.add("Monotone-action transforms are temporal and melds keep separate histories", [] {
        const auto base = heap_of({50, 60, 70, 80});
        const auto capped = base.transform_all(clamp_policy::at_most(55));
        FT_REQUIRE(drain_priorities(capped) == std::vector<int>({50, 55, 55, 55}));

        // A later insertion is never retroactively transformed.
        const auto after_insert = capped.insert(1'000, 1'000);
        require_valid(after_insert);
        FT_REQUIRE(drain_priorities(after_insert) == std::vector<int>({50, 55, 55, 55, 1'000}));

        // ... including when the new entry wins the root, which takes the other insert branch.
        const auto after_small_insert = capped.insert(-5, -5);
        require_valid(after_small_insert);
        FT_REQUIRE(drain_priorities(after_small_insert) == std::vector<int>({-5, 50, 55, 55, 55}));

        // A later transform reaches the entry inserted in between, and nothing else changes.
        const auto retransformed = after_insert.transform_all(clamp_policy::at_most(100));
        FT_REQUIRE(drain_priorities(retransformed) == std::vector<int>({50, 55, 55, 55, 100}));

        // Melds keep each operand's own action history, even for a non-invertible clamp.
        const auto other = heap_of({10, 500, 900});
        const auto melded = capped.meld(other);
        require_valid(melded);
        FT_REQUIRE(drain_priorities(melded) == std::vector<int>({10, 50, 55, 55, 55, 500, 900}));
        FT_REQUIRE(drain_priorities(other.meld(capped)) == drain_priorities(melded));

        const auto floored = other.transform_all(clamp_policy::at_least(600));
        const auto mixed = capped.meld(floored);
        require_valid(mixed);
        FT_REQUIRE(drain_priorities(mixed) == std::vector<int>({50, 55, 55, 55, 600, 600, 900}));

        // A transform after the meld reaches everything present at that moment.
        FT_REQUIRE(drain_priorities(mixed.transform_all(clamp_policy::at_least(58)))
            == std::vector<int>({58, 58, 58, 58, 600, 600, 900}));

        // Sources are untouched.
        FT_REQUIRE(drain_priorities(base) == std::vector<int>({50, 60, 70, 80}));
        FT_REQUIRE(drain_priorities(capped) == std::vector<int>({50, 55, 55, 55}));
        FT_REQUIRE(drain_priorities(other) == std::vector<int>({10, 500, 900}));

        // The whole-heap transform composes one tag at the root: identity and empty updates share
        // the root outright, and a real update reuses the entire child forest.
        FT_REQUIRE(base.shares_root_with(base.transform_all(clamp_policy::identity_action())));
        FT_REQUIRE(int_heap{}.shares_root_with(int_heap{}.transform_all(clamp_policy::at_most(1))));
        FT_REQUIRE(!base.shares_root_with(capped));
        FT_REQUIRE(base.shares_children_with(capped));
        FT_REQUIRE(base.is_same_version(base));
        FT_REQUIRE(!base.is_same_version(capped));
        FT_REQUIRE_EQUAL(base.node_identities().size(), std::size_t{4});
        FT_REQUIRE_EQUAL(base.shared_node_count_with(capped), std::size_t{3});

        const auto transformed_statistics = capped.validate_structure();
        FT_REQUIRE(transformed_statistics.tagged_component_count > 0);
        FT_REQUIRE_EQUAL(base.validate_structure().tagged_component_count, std::size_t{0});
        FT_REQUIRE_EQUAL(transformed_statistics.count, std::size_t{4});

#ifndef FINGERTREE_DISABLE_ALLOCATION_TRACKING
        {
            const allocation_counting_scope scope;
            const auto one_root = base.transform_all(clamp_policy::at_least(65));
            (void)one_root;
            FT_REQUIRE(scope.allocations() <= std::size_t{4});
        }
#endif
    });

    tests.add("Monotone-action tie-breaking and empty-heap contracts are exact", [] {
        // An insert-root tie favors the new entry.
        const auto tied = string_heap{}.insert("first", 5).insert("second", 5);
        FT_REQUIRE_EQUAL(*tied.minimum().element, std::string{"second"});
        FT_REQUIRE_EQUAL(*tied.minimum().priority, 5);
        FT_REQUIRE_EQUAL(*tied.insert("third", 5).minimum().element, std::string{"third"});
        FT_REQUIRE_EQUAL(*tied.insert("larger", 6).minimum().element, std::string{"second"});

        // A minimum among tied forest trees favors the earliest spine occurrence. Inserting a
        // losing entry conses it onto the front of the root's child forest, so "c" precedes "b".
        const auto spine = string_heap{}.insert("a", 0).insert("b", 10).insert("c", 10);
        FT_REQUIRE_EQUAL(*spine.minimum().element, std::string{"a"});
        const auto without_a = spine.delete_minimum();
        FT_REQUIRE_EQUAL(*without_a.minimum().element, std::string{"c"});
        FT_REQUIRE_EQUAL(*without_a.delete_minimum().minimum().element, std::string{"b"});

        // A meld tie favors the receiver's root.
        const auto left = string_heap{}.insert("left", 3);
        const auto right = string_heap{}.insert("right", 3);
        FT_REQUIRE_EQUAL(*left.meld(right).minimum().element, std::string{"left"});
        FT_REQUIRE_EQUAL(*right.meld(left).minimum().element, std::string{"right"});

        const auto empty = int_heap{};
        FT_REQUIRE(empty.empty());
        FT_REQUIRE_EQUAL(empty.size(), std::size_t{0});
        FT_REQUIRE(!empty.try_minimum().has_value());
        FT_REQUIRE(!empty.try_delete_minimum().has_value());
        FT_REQUIRE_THROWS(std::logic_error, empty.minimum());
        FT_REQUIRE_THROWS(std::logic_error, empty.delete_minimum());
        FT_REQUIRE(empty.is_same_version(empty.clear()));
        FT_REQUIRE(empty.begin() == empty.end());
        FT_REQUIRE(empty.node_identities().empty());
        require_valid(empty);
        FT_REQUIRE(empty.validate_structure() == ft::persistent_monotone_action_heap_statistics{});

        // A one-entry heap deletes down to the empty representation, not to a stale root.
        const auto single = int_heap{}.insert(7, 7);
        FT_REQUIRE(single.delete_minimum().empty());
        FT_REQUIRE_EQUAL(single.delete_minimum().size(), std::size_t{0});
        require_valid(single.delete_minimum());
        FT_REQUIRE(single.is_same_version(single.meld(empty)));
        FT_REQUIRE(single.is_same_version(empty.meld(single)));
        FT_REQUIRE(!single.is_same_version(single.clear()));

        FT_REQUIRE_THROWS(std::invalid_argument, empty.insert_handles(nullptr, nullptr));
        FT_REQUIRE_THROWS(
            std::invalid_argument,
            empty.insert_handles(std::make_shared<const int>(1), nullptr));
    });

    tests.add("Monotone-action adversarial shapes validate and drain in sorted order", [] {
        constexpr auto count = 4'096;
        auto ascending = std::vector<int>{};
        ascending.reserve(count);
        for (auto value = 0; value != count; ++value) {
            ascending.push_back(value);
        }
        auto descending = ascending;
        std::ranges::reverse(descending);
        const auto equal = std::vector<int>(count, 7);
        auto shuffled = ascending;
        deterministic_rng random{0x6d6f6e6f746f6e65ULL};
        for (auto index = shuffled.size(); index > 1; --index) {
            std::swap(shuffled[index - 1], shuffled[random.next_index(index)]);
        }

        for (const auto& values : {ascending, descending, equal, shuffled}) {
            const auto heap = heap_of(values);
            require_valid(heap);
            FT_REQUIRE_EQUAL(heap.size(), values.size());
            FT_REQUIRE(sorted_copy(iterated_priorities(heap)) == sorted_copy(values));
            FT_REQUIRE(drain_priorities(heap) == sorted_copy(values));
        }

        // Every small size exercises the split-forest decomposition's rank-one and rank-zero
        // ambiguity branches and the zeros re-insertion path.
        for (auto size = std::size_t{0}; size != 200; ++size) {
            auto values = std::vector<int>{};
            for (auto index = std::size_t{0}; index != size; ++index) {
                values.push_back(static_cast<int>((index * 61) % 200));
            }
            auto heap = heap_of(values);
            auto model = std::multiset<int>(values.begin(), values.end());
            while (!heap.empty()) {
                require_valid(heap);
                FT_REQUIRE_EQUAL(heap.size(), model.size());
                FT_REQUIRE_EQUAL(*heap.minimum().priority, *model.begin());
                heap = heap.delete_minimum();
                model.erase(model.begin());
            }
            FT_REQUIRE(model.empty());

            // The same sizes, but with a pending tag in place for the whole decomposition.
            auto tagged = heap_of(values).transform_all(clamp_policy::between(40, 160));
            auto tagged_model = std::multiset<int>{};
            for (const auto value : values) {
                tagged_model.insert(clamp_policy::apply(clamp_policy::between(40, 160), value));
            }
            while (!tagged.empty()) {
                require_valid(tagged);
                FT_REQUIRE_EQUAL(*tagged.minimum().priority, *tagged_model.begin());
                tagged = tagged.delete_minimum();
                tagged_model.erase(tagged_model.begin());
            }
        }
    });

    tests.add("Monotone-action self-meld duplicates every logical occurrence", [] {
        auto heap = int_heap{};
        for (auto value = 0; value != 1'024; ++value) {
            heap = heap.insert(value, value);
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
        FT_REQUIRE(drain_priorities(melded) == expected);

        // Self-melding a transformed version duplicates the transformed occurrences, and the
        // resulting shared DAG still validates.
        const auto capped = heap.transform_all(clamp_policy::at_most(100));
        const auto doubled = capped.meld(capped);
        FT_REQUIRE_EQUAL(doubled.size(), std::size_t{2'048});
        require_valid(doubled);
        auto capped_expected = std::vector<int>{};
        for (auto value = 0; value != 1'024; ++value) {
            const auto clamped = (std::min)(value, 100);
            capped_expected.push_back(clamped);
            capped_expected.push_back(clamped);
        }
        FT_REQUIRE(drain_priorities(doubled) == sorted_copy(capped_expected));
        FT_REQUIRE_EQUAL(heap.size(), std::size_t{1'024});
    });

    tests.add("Monotone-action retained randomized history matches persistent multiset snapshots", [] {
        struct version final {
            int_heap heap;
            std::multiset<int> model;
        };
        deterministic_rng random{0x616374696f6e5f31ULL};
        auto current = version{};
        auto retained = std::vector<version>{current};

        for (auto operation = 0; operation != 12'000; ++operation) {
            if (retained.size() > 1 && random.next_index(6) == 0) {
                current = retained[random.next_index(retained.size())];
            }
            const auto choice = random.next_index(100);
            if (choice < 45 || current.model.empty()) {
                const auto value = static_cast<int>(random.next_index(257)) - 128;
                current.heap = current.heap.insert(value, value);
                current.model.insert(value);
            } else if (choice < 68) {
                current.heap = current.heap.delete_minimum();
                current.model.erase(current.model.begin());
            } else if (choice < 86) {
                const auto action = [&] {
                    switch (random.next_index(4)) {
                    case 0:
                        return clamp_policy::at_least(static_cast<int>(random.next_index(200)) - 100);
                    case 1:
                        return clamp_policy::at_most(static_cast<int>(random.next_index(200)) - 100);
                    case 2: {
                        const auto low = static_cast<int>(random.next_index(200)) - 100;
                        const auto high = low + static_cast<int>(random.next_index(60));
                        return clamp_policy::between(low, high);
                    }
                    default:
                        return clamp_policy::constant(static_cast<int>(random.next_index(200)) - 100);
                    }
                }();
                current.heap = current.heap.transform_all(action);
                auto next_model = std::multiset<int>{};
                for (const auto value : current.model) {
                    next_model.insert(clamp_policy::apply(action, value));
                }
                current.model = std::move(next_model);
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
            FT_REQUIRE_EQUAL(current.heap.empty(), current.model.empty());
            if (!current.model.empty()) {
                FT_REQUIRE_EQUAL(*current.heap.minimum().priority, *current.model.begin());
            }
            if (operation % 149 == 0) {
                require_valid(current.heap);
                FT_REQUIRE(sorted_copy(iterated_priorities(current.heap))
                    == std::vector<int>(current.model.begin(), current.model.end()));
            }
            if (operation % 89 == 0) {
                if (retained.size() < 96) {
                    retained.push_back(current);
                } else {
                    retained[random.next_index(retained.size())] = current;
                }
            }
        }

        for (auto index = std::size_t{0}; index < retained.size(); index += 7) {
            require_valid(retained[index].heap);
            FT_REQUIRE(drain_priorities(retained[index].heap)
                == std::vector<int>(retained[index].model.begin(), retained[index].model.end()));
        }
    });

    tests.add("Monotone-action iteration, handles, and a second action family behave as documented", [] {
        // The heap is generic over the action family, not welded to the clamp.
        auto shifted = offset_heap{};
        for (auto value = 0; value != 64; ++value) {
            shifted = shifted.insert(value, static_cast<std::int64_t>((value * 29) % 64));
        }
        require_valid(shifted);
        const auto moved = shifted.transform_all(1'000).transform_all(-1);
        require_valid(moved);
        FT_REQUIRE_EQUAL(*moved.minimum().priority, std::int64_t{999});
        const auto with_new = moved.insert(999, 5);
        FT_REQUIRE_EQUAL(*with_new.minimum().priority, std::int64_t{5});
        auto expected_offsets = std::vector<std::int64_t>{};
        for (auto value = 0; value != 64; ++value) {
            expected_offsets.push_back(static_cast<std::int64_t>((value * 29) % 64) + 999);
        }
        std::ranges::sort(expected_offsets);
        FT_REQUIRE(drain_priorities(moved) == expected_offsets);
        FT_REQUIRE(shifted.shares_root_with(shifted.transform_all(offset_policy::identity_action())));

        // Iteration reaches every entry with its pending actions already composed, and pairs each
        // payload with the priority that payload actually has.
        auto payloads = string_heap{};
        for (auto value = 0; value != 40; ++value) {
            payloads = payloads.insert("item-" + std::to_string(value), value);
        }
        const auto windowed = payloads.transform_all(clamp_policy::between(10, 20));
        auto observed = std::map<std::string, int>{};
        for (const auto& entry : windowed) {
            observed.emplace(*entry.element, *entry.priority);
        }
        FT_REQUIRE_EQUAL(observed.size(), std::size_t{40});
        for (auto value = 0; value != 40; ++value) {
            const auto key = "item-" + std::to_string(value);
            FT_REQUIRE_EQUAL(observed.at(key), (std::clamp)(value, 10, 20));
        }

        // A cursor owns the version it walks, so it stays valid after the heap value is gone.
        auto escaped = [] {
            const auto local = string_heap{}
                                   .insert("x", 4)
                                   .insert("y", 9)
                                   .transform_all(clamp_policy::at_least(6));
            return local.begin();
        }();
        auto escaped_priorities = std::vector<int>{};
        while (escaped != string_heap{}.end()) {
            escaped_priorities.push_back(*(*escaped).priority);
            ++escaped;
        }
        std::ranges::sort(escaped_priorities);
        FT_REQUIRE(escaped_priorities == std::vector<int>({6, 9}));

        // Copies of a cursor advance independently.
        auto cursor = windowed.begin();
        auto copy = cursor;
        const auto first = *(*cursor).element;
        ++cursor;
        FT_REQUIRE_EQUAL(*(*copy).element, first);
        FT_REQUIRE(*(*cursor).element != first);

        // Shared handles round-trip, and bulk construction agrees with repeated insertion.
        const auto minimum = windowed.minimum();
        const auto rebuilt = string_heap{}.insert_handles(minimum.element, minimum.priority);
        FT_REQUIRE(rebuilt.minimum().element == minimum.element);
        FT_REQUIRE(rebuilt.minimum().priority == minimum.priority);

        auto pairs = std::vector<std::pair<int, int>>{};
        for (auto value = 0; value != 50; ++value) {
            pairs.emplace_back(value, (value * 17) % 50);
        }
        const auto ranged = int_heap::from_range(pairs);
        require_valid(ranged);
        FT_REQUIRE_EQUAL(ranged.size(), std::size_t{50});
        auto ranged_expected = std::vector<int>{};
        for (const auto& pair : pairs) {
            ranged_expected.push_back(pair.second);
        }
        FT_REQUIRE(drain_priorities(ranged) == sorted_copy(ranged_expected));
        FT_REQUIRE_EQUAL(int_heap{}.insert_range(pairs).size(), std::size_t{50});
    });

    tests.add("Monotone-action immutable readers safely share retained snapshots", [] {
        auto values = std::vector<int>{};
        for (auto value = 0; value != 8'192; ++value) {
            values.push_back(value);
        }
        const auto heap = heap_of(values).transform_all(clamp_policy::at_least(1'000));
        const auto expected_minimum = 1'000;
        auto failed = std::atomic<bool>{false};
        {
            auto readers = std::vector<std::jthread>{};
            for (auto worker = 0; worker != 4; ++worker) {
                readers.emplace_back([&] {
                    try {
                        for (auto iteration = 0; iteration != 8; ++iteration) {
                            auto checksum = std::int64_t{0};
                            for (const auto& entry : heap) {
                                checksum += *entry.priority;
                            }
                            const auto statistics = heap.validate_structure();
                            if (*heap.minimum().priority != expected_minimum
                                || statistics.count != heap.size()
                                || checksum <= 0) {
                                throw std::logic_error("concurrent action-heap reader mismatch");
                            }
                        }
                    } catch (...) {
                        failed.store(true, std::memory_order_relaxed);
                    }
                });
            }
        }
        FT_REQUIRE(!failed.load(std::memory_order_relaxed));
        require_valid(heap);
    });
}
