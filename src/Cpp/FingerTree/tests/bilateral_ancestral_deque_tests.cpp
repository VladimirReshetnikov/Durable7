/// Tests for the bilateral ancestral deque.
///
/// The properties under test are that two oppositely oriented ancestry intervals reproduce a
/// sequence oracle exactly, that every produced handle stays growable at both ends, and that the
/// advertised level-ancestor query profiles are exact rather than merely bounded. The compile-time
/// arena seam is exercised by running the same handle over three different backends.

#include <durable7/finger_tree/bilateral_ancestral_deque.hpp>

#include "test_support/replay_seed.hpp"
#include "test_support/test_runner.hpp"

#include <algorithm>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using namespace durable7::finger_tree;
using namespace durable7::finger_tree::tests;

namespace {

using int_arena = myers_incremental_ancestor_arena<int>;
using int_deque = bilateral_ancestral_deque<int>;

/// A faithful Myers arena that can be told to misreport depths, parents, or level ancestors.
///
/// It exists to reach the audit's failure branches, which a correct arena never reaches, and it
/// doubles as proof that the backend really is a compile-time parameter rather than a fixed type.
template <class T>
class biased_arena final {
public:
    [[nodiscard]] std::size_t bottom() const noexcept
    {
        return inner_.bottom();
    }

    [[nodiscard]] std::size_t published_node_count() const
    {
        return inner_.published_node_count();
    }

    [[nodiscard]] std::size_t add_leaf(const std::size_t parent, T value)
    {
        return inner_.add_leaf(parent, std::move(value));
    }

    [[nodiscard]] std::ptrdiff_t depth_of(const std::size_t node) const
    {
        const auto depth = inner_.depth_of(node);
        return node == inner_.bottom() ? depth : depth + depth_bias;
    }

    [[nodiscard]] std::size_t parent_of(const std::size_t node) const
    {
        return misreport_parent ? node : inner_.parent_of(node);
    }

    [[nodiscard]] std::size_t ancestor_at_depth(
        const std::size_t node,
        const std::ptrdiff_t depth) const
    {
        if (misreported_ancestor_depth.has_value() && depth == *misreported_ancestor_depth) {
            return node;
        }

        return inner_.ancestor_at_depth(node, depth);
    }

    [[nodiscard]] const T& value_at(const std::size_t node) const
    {
        return inner_.value_at(node);
    }

    [[nodiscard]] myers_incremental_ancestor_statistics statistics() const
    {
        return inner_.statistics();
    }

    std::ptrdiff_t depth_bias = 0;
    bool misreport_parent = false;
    std::optional<std::ptrdiff_t> misreported_ancestor_depth;

private:
    myers_incremental_ancestor_arena<T> inner_;
};

/// An arena whose bottom node reports the wrong depth, used to prove eager rejection.
class misplaced_bottom_arena final {
public:
    [[nodiscard]] static constexpr std::size_t bottom() noexcept
    {
        return 0;
    }

    [[nodiscard]] std::size_t published_node_count() const noexcept
    {
        return 0;
    }

    [[nodiscard]] std::size_t add_leaf(std::size_t, int)
    {
        ++touched;
        throw std::logic_error("a rejected arena is never asked to publish");
    }

    [[nodiscard]] std::ptrdiff_t depth_of(std::size_t) const noexcept
    {
        return 0;
    }

    [[nodiscard]] std::size_t parent_of(std::size_t) const
    {
        throw std::logic_error("a rejected arena is never navigated");
    }

    [[nodiscard]] std::size_t ancestor_at_depth(std::size_t, std::ptrdiff_t) const
    {
        throw std::logic_error("a rejected arena is never queried");
    }

    [[nodiscard]] const int& value_at(std::size_t) const
    {
        throw std::logic_error("a rejected arena holds no values");
    }

    std::size_t touched = 0;
};

using biased_deque = bilateral_ancestral_deque<int, biased_arena<int>>;
using rejected_deque = bilateral_ancestral_deque<int, misplaced_bottom_arena>;

/// A deterministic xorshift generator; the tests must not depend on the standard library's engines
/// staying byte-identical across implementations.
class xorshift final {
public:
    explicit xorshift(const std::uint64_t seed) noexcept
        : state_(seed | 1)
    {
    }

    [[nodiscard]] std::size_t below(const std::size_t bound) noexcept
    {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 7;
        state_ ^= state_ << 17;
        return static_cast<std::size_t>(state_ % static_cast<std::uint64_t>(bound));
    }

private:
    std::uint64_t state_;
};

[[nodiscard]] std::vector<int> ints(const std::initializer_list<int> values)
{
    return std::vector<int>(values);
}

/// The audit result a handle with the stated interval counts must report.
[[nodiscard]] bilateral_ancestral_deque_statistics counts(
    const std::size_t count,
    const std::size_t left,
    const std::size_t right)
{
    return bilateral_ancestral_deque_statistics{count, left, right};
}

[[nodiscard]] std::vector<int> inclusive_ints(const int first, const int last)
{
    auto values = std::vector<int>{};
    for (auto value = first; value <= last; ++value) {
        values.push_back(value);
    }
    return values;
}

/// Asserts the full observable contract of one handle against a sequence oracle.
template <class T, class Arena>
void assert_deque(
    const std::vector<T>& expected,
    const bilateral_ancestral_deque<T, Arena>& actual)
{
    FT_REQUIRE_EQUAL(actual.size(), expected.size());
    FT_REQUIRE_EQUAL(actual.empty(), expected.empty());
    FT_REQUIRE(actual.to_vector() == expected);
    FT_REQUIRE(std::vector<T>(actual.begin(), actual.end()) == expected);
    FT_REQUIRE_EQUAL(
        static_cast<std::size_t>(std::distance(actual.begin(), actual.end())),
        expected.size());

    for (auto index = std::size_t{0}; index != expected.size(); ++index) {
        FT_REQUIRE(actual.at(index) == expected[index]);
        FT_REQUIRE(actual[index] == expected[index]);
    }
    FT_REQUIRE_THROWS(std::out_of_range, actual.at(expected.size()));

    if (expected.empty()) {
        FT_REQUIRE(actual.try_front() == nullptr);
        FT_REQUIRE(actual.try_back() == nullptr);
        FT_REQUIRE_THROWS(std::logic_error, actual.front());
        FT_REQUIRE_THROWS(std::logic_error, actual.back());
    } else {
        FT_REQUIRE(actual.front() == expected.front());
        FT_REQUIRE(actual.back() == expected.back());
        FT_REQUIRE(actual.try_front() != nullptr && *actual.try_front() == expected.front());
        FT_REQUIRE(actual.try_back() != nullptr && *actual.try_back() == expected.back());
    }

    const auto statistics = actual.validate_structure();
    FT_REQUIRE_EQUAL(statistics.count, expected.size());
    FT_REQUIRE_EQUAL(statistics.left_count + statistics.right_count, expected.size());
}

/// Runs an operation and asserts it made exactly the stated number of level-ancestor queries.
template <class Arena, class Operation>
auto with_queries(
    const Arena& arena,
    const std::uint64_t expected,
    const std::string_view label,
    Operation&& operation,
    const std::source_location location = std::source_location::current())
{
    const auto before = arena.statistics().ancestor_query_count;
    if constexpr (std::is_void_v<std::invoke_result_t<Operation&>>) {
        operation();
        const auto after = arena.statistics().ancestor_query_count;
        require_equal(
            after - before,
            expected,
            label,
            "the expected level-ancestor query count",
            location);
    } else {
        auto result = operation();
        const auto after = arena.statistics().ancestor_query_count;
        require_equal(
            after - before,
            expected,
            label,
            "the expected level-ancestor query count",
            location);
        return result;
    }
}

/// Builds the canonical eight-value bilateral deque `[1..8]` with four values on each arm.
[[nodiscard]] int_deque eight_value_deque(const std::shared_ptr<int_arena>& arena)
{
    return int_deque::create(arena)
        .push_front(4)
        .push_front(3)
        .push_front(2)
        .push_front(1)
        .push_back(5)
        .push_back(6)
        .push_back(7)
        .push_back(8);
}

/// The message of the `std::logic_error` an action raises, or an empty string when it raises none.
template <class Action>
[[nodiscard]] std::string invariant_message(Action&& action)
{
    try {
        std::forward<Action>(action)();
    } catch (const std::logic_error& error) {
        return std::string{error.what()};
    }

    return std::string{};
}

} // namespace

void add_bilateral_ancestral_deque_tests(suite& tests)
{
    static_assert(incremental_ancestor_arena<int_arena, int>);
    static_assert(incremental_ancestor_arena<biased_arena<int>, int>);
    static_assert(incremental_ancestor_arena<misplaced_bottom_arena, int>);
    static_assert(std::same_as<int_deque::arena_type, int_arena>);
    static_assert(!std::same_as<int_deque, biased_deque>);
    static_assert(std::forward_iterator<int_deque::const_iterator>);
    static_assert(std::ranges::forward_range<int_deque>);
    static_assert(std::copyable<int_deque>);

    tests.add("Bilateral ancestral deque empty handles have stable reads, failures, and identity", [] {
        const auto empty = bilateral_ancestral_deque<std::string>{};
        assert_deque(std::vector<std::string>{}, empty);
        FT_REQUIRE_THROWS(std::logic_error, empty.remove_first());
        FT_REQUIRE_THROWS(std::logic_error, empty.remove_last());
        FT_REQUIRE(!empty.try_pop_front().has_value());
        FT_REQUIRE(!empty.try_pop_back().has_value());

        for (const auto& produced : {
                 empty.clear(),
                 empty.take(0),
                 empty.drop(0),
                 empty.get_range(0, 0),
                 empty.reverse(),
             }) {
            assert_deque(std::vector<std::string>{}, produced);
            FT_REQUIRE(produced.shares_storage_with(empty));
        }

        const auto split = empty.split_at(0);
        FT_REQUIRE(split.left.shares_storage_with(empty));
        FT_REQUIRE(split.right.shares_storage_with(empty));

        // Every identity result above is a copy of the receiver, so a separately created empty
        // handle over the same arena is the discriminating case for the sharing diagnostic: a
        // distinct value that nonetheless denotes the same two intervals.
        const auto separate = bilateral_ancestral_deque<std::string>::create(empty.arena());
        FT_REQUIRE(separate.shares_storage_with(empty));
        FT_REQUIRE(!separate.shares_storage_with(bilateral_ancestral_deque<std::string>{}));

        FT_REQUIRE_THROWS(std::out_of_range, empty.take(1));
        FT_REQUIRE_THROWS(std::out_of_range, empty.drop(1));
        FT_REQUIRE_THROWS(std::out_of_range, empty.split_at(1));
        FT_REQUIRE_THROWS(std::out_of_range, empty.get_range(0, 1));
        FT_REQUIRE_THROWS(std::out_of_range, empty.get_range(1, 0));

        // A stored empty optional stays a visible value; only the outer optional means absence.
        using nullable_deque = bilateral_ancestral_deque<std::optional<std::string>>;
        const auto nullable = nullable_deque{}.push_front(std::nullopt).push_back("tail");
        assert_deque(
            std::vector<std::optional<std::string>>{std::nullopt, std::string{"tail"}},
            nullable);
        const auto popped = nullable.try_pop_front();
        FT_REQUIRE(popped.has_value());
        FT_REQUIRE(!popped->value->has_value());
        assert_deque(std::vector<std::optional<std::string>>{std::string{"tail"}}, popped->rest);

        auto rejected = std::make_shared<misplaced_bottom_arena>();
        FT_REQUIRE_THROWS(std::invalid_argument, rejected_deque::create(rejected));
        FT_REQUIRE_EQUAL(rejected->touched, std::size_t{0});
        FT_REQUIRE_THROWS(std::invalid_argument, int_deque::create(nullptr));

        assert_deque(ints({1, 2, 3}), int_deque::from_range(ints({1, 2, 3})));
        assert_deque(ints({1, 2, 3}), int_deque::from_range({1, 2, 3}));
        assert_deque(std::vector<int>{}, int_deque::from_range(std::vector<int>{}));
        FT_REQUIRE(!int_deque::from_range({1, 2, 3}).shares_storage_with(
            int_deque::from_range({1, 2, 3})));
    });

    tests.add("Bilateral ancestral deque endpoint operations retain every prior branch", [] {
        const auto root = int_deque{};
        const auto one = root.push_back(10);
        const auto two = one.push_back(20);
        const auto three = two.push_front(5);
        const auto four = three.push_front(1);
        const auto five = four.push_back(30);

        assert_deque(std::vector<int>{}, root);
        assert_deque(ints({10}), one);
        assert_deque(ints({10, 20}), two);
        assert_deque(ints({5, 10, 20}), three);
        assert_deque(ints({1, 5, 10, 20}), four);
        assert_deque(ints({1, 5, 10, 20, 30}), five);

        assert_deque(ints({5, 10, 20, 30}), five.remove_first());
        assert_deque(ints({1, 5, 10, 20}), five.remove_last());
        assert_deque(ints({1, 5, 10, 20, 30}), five);

        const auto right_only = root.push_back(1).push_back(2).push_back(3);
        const auto after_one = right_only.remove_first();
        const auto after_two = after_one.remove_first();
        assert_deque(ints({2, 3}), after_one);
        assert_deque(ints({3}), after_two);
        assert_deque(std::vector<int>{}, after_two.remove_first());
        assert_deque(ints({1, 2, 3}), right_only);

        const auto left_only = root.push_front(3).push_front(2).push_front(1);
        const auto without_last = left_only.remove_last();
        const auto without_two = without_last.remove_last();
        assert_deque(ints({1, 2}), without_last);
        assert_deque(ints({1}), without_two);
        assert_deque(std::vector<int>{}, without_two.remove_last());

        const auto popped_front = five.try_pop_front();
        FT_REQUIRE(popped_front.has_value());
        FT_REQUIRE_EQUAL(*popped_front->value, 1);
        assert_deque(ints({5, 10, 20, 30}), popped_front->rest);
        const auto popped_back = five.try_pop_back();
        FT_REQUIRE(popped_back.has_value());
        FT_REQUIRE_EQUAL(*popped_back->value, 30);
        assert_deque(ints({1, 5, 10, 20}), popped_back->rest);

        // Two independent branches grow from one retained version.
        const auto left_branch = two.push_front(-1);
        const auto right_branch = two.push_back(99);
        assert_deque(ints({-1, 10, 20}), left_branch);
        assert_deque(ints({10, 20, 99}), right_branch);
        assert_deque(ints({10, 20}), two);

        const auto cleared = five.clear();
        assert_deque(std::vector<int>{}, cleared);
        assert_deque(ints({-7, 8}), cleared.push_front(-7).push_back(8));
        assert_deque(ints({1, 5, 10, 20, 30}), five);
        FT_REQUIRE(!cleared.shares_storage_with(five));
        FT_REQUIRE(cleared.clear().shares_storage_with(cleared));
    });

    tests.add("Bilateral ancestral deque reversal exchanges the two intervals in place", [] {
        const auto deque = int_deque{}
                               .push_front(3)
                               .push_front(2)
                               .push_front(1)
                               .push_back(4)
                               .push_back(5)
                               .push_back(6)
                               .push_back(7);
        const auto reversed = deque.reverse();
        const auto restored = reversed.reverse();

        assert_deque(inclusive_ints(1, 7), deque);
        assert_deque(ints({7, 6, 5, 4, 3, 2, 1}), reversed);
        assert_deque(inclusive_ints(1, 7), restored);
        FT_REQUIRE(restored.shares_storage_with(deque));
        FT_REQUIRE(!reversed.shares_storage_with(deque));

        assert_deque(
            ints({0, 7, 6, 5, 4, 3, 2, 1, 8}),
            reversed.push_front(0).push_back(8));
        assert_deque(ints({6, 5, 4, 3, 2}), reversed.remove_first().remove_last());
        assert_deque(inclusive_ints(1, 7), deque);

        // A handle of at most one value keeps its representation, because exchanging its arms would
        // change the representation without changing the sequence.
        const auto singleton = int_deque::from_range({42});
        FT_REQUIRE(singleton.reverse().shares_storage_with(singleton));
        const auto empty = int_deque{};
        FT_REQUIRE(empty.reverse().shares_storage_with(empty));
    });

    tests.add("Bilateral ancestral deque slices and splits stay growable at both ends", [] {
        const auto original = int_deque{}
                                  .push_front(3)
                                  .push_front(2)
                                  .push_front(1)
                                  .push_back(4)
                                  .push_back(5)
                                  .push_back(6)
                                  .push_back(7);
        const auto expected = inclusive_ints(1, 7);

        for (auto start = std::size_t{0}; start <= expected.size(); ++start) {
            for (auto count = std::size_t{0}; count <= expected.size() - start; ++count) {
                const auto slice = original.get_range(start, count);
                const auto model = std::vector<int>(
                    expected.begin() + static_cast<std::ptrdiff_t>(start),
                    expected.begin() + static_cast<std::ptrdiff_t>(start + count));
                assert_deque(model, slice);

                auto extended_model = std::vector<int>{-100 - static_cast<int>(start)};
                extended_model.insert(extended_model.end(), model.begin(), model.end());
                extended_model.push_back(100 + static_cast<int>(count));
                assert_deque(
                    extended_model,
                    slice.push_front(-100 - static_cast<int>(start))
                        .push_back(100 + static_cast<int>(count)));
                assert_deque(model, slice);

                auto reversed_model = std::vector<int>{-1};
                reversed_model.insert(reversed_model.end(), model.rbegin(), model.rend());
                reversed_model.push_back(-2);
                assert_deque(reversed_model, slice.reverse().push_front(-1).push_back(-2));
            }
        }

        for (auto boundary = std::size_t{0}; boundary <= expected.size(); ++boundary) {
            const auto split = original.split_at(boundary);
            const auto prefix = std::vector<int>(
                expected.begin(),
                expected.begin() + static_cast<std::ptrdiff_t>(boundary));
            const auto suffix = std::vector<int>(
                expected.begin() + static_cast<std::ptrdiff_t>(boundary),
                expected.end());
            assert_deque(prefix, split.left);
            assert_deque(suffix, split.right);
            assert_deque(prefix, original.take(boundary));
            assert_deque(suffix, original.drop(boundary));

            auto grown_prefix = prefix;
            grown_prefix.push_back(88);
            assert_deque(grown_prefix, split.left.push_back(88));

            auto grown_suffix = std::vector<int>{77};
            grown_suffix.insert(grown_suffix.end(), suffix.begin(), suffix.end());
            assert_deque(grown_suffix, split.right.push_front(77));
        }

        assert_deque(expected, original);
        FT_REQUIRE(original.get_range(0, original.size()).shares_storage_with(original));
        FT_REQUIRE(original.take(original.size()).shares_storage_with(original));
        FT_REQUIRE(original.drop(0).shares_storage_with(original));
        FT_REQUIRE(original.split_at(0).right.shares_storage_with(original));
        FT_REQUIRE(original.split_at(original.size()).left.shares_storage_with(original));
    });

    tests.add("Bilateral ancestral deque construction words match the sequence oracle", [] {
        for (auto length = std::size_t{0}; length <= 8; ++length) {
            for (auto word = std::uint32_t{0}; word != (std::uint32_t{1} << length); ++word) {
                auto deque = int_deque{};
                auto model = std::vector<int>{};
                for (auto position = std::size_t{0}; position != length; ++position) {
                    const auto value = static_cast<int>(position);
                    if ((word & (std::uint32_t{1} << position)) == 0) {
                        deque = deque.push_front(value);
                        model.insert(model.begin(), value);
                    } else {
                        deque = deque.push_back(value);
                        model.push_back(value);
                    }
                }

                assert_deque(model, deque);
                for (auto start = std::size_t{0}; start <= length; ++start) {
                    for (auto count = std::size_t{0}; count <= length - start; ++count) {
                        const auto expected = std::vector<int>(
                            model.begin() + static_cast<std::ptrdiff_t>(start),
                            model.begin() + static_cast<std::ptrdiff_t>(start + count));
                        const auto slice = deque.get_range(start, count);
                        assert_deque(expected, slice);

                        auto grown = std::vector<int>{-1};
                        grown.insert(grown.end(), expected.begin(), expected.end());
                        grown.push_back(-2);
                        assert_deque(grown, slice.push_front(-1).push_back(-2));
                    }
                }

                for (auto boundary = std::size_t{0}; boundary <= length; ++boundary) {
                    const auto split = deque.split_at(boundary);
                    const auto prefix = std::vector<int>(
                        model.begin(),
                        model.begin() + static_cast<std::ptrdiff_t>(boundary));
                    const auto suffix = std::vector<int>(
                        model.begin() + static_cast<std::ptrdiff_t>(boundary),
                        model.end());
                    assert_deque(prefix, split.left);
                    assert_deque(suffix, split.right);
                    assert_deque(prefix, deque.take(boundary));
                    assert_deque(suffix, deque.drop(boundary));
                }
            }
        }
    });

    tests.add("Bilateral ancestral deque randomized branches match an immutable sequence model", [] {
        for (const auto seed : select_replay_seeds({1, 17, 91, 8'675'309, 0x9e37'79b9})) {
            capture_replay_seed(seed);
            auto random = xorshift{seed};
            const auto root = int_deque{};
            auto versions = std::vector<std::pair<int_deque, std::vector<int>>>{};
            versions.emplace_back(root, std::vector<int>{});

            for (auto step = std::size_t{0}; step != 400; ++step) {
                const auto source_index = random.below(versions.size());
                const auto source = versions[source_index].first;
                const auto source_model = versions[source_index].second;
                assert_deque(source_model, source);

                const auto value = static_cast<int>(step) * 31 + 7;
                auto result = source;
                auto model = source_model;
                switch (random.below(11)) {
                case 0:
                    result = source.push_front(value);
                    model.insert(model.begin(), value);
                    break;
                case 1:
                    result = source.push_back(value);
                    model.push_back(value);
                    break;
                case 2:
                    if (!source_model.empty()) {
                        result = source.remove_first();
                        model.erase(model.begin());
                    }
                    break;
                case 3:
                    if (!source_model.empty()) {
                        result = source.remove_last();
                        model.pop_back();
                    }
                    break;
                case 4:
                    result = source.reverse();
                    std::reverse(model.begin(), model.end());
                    break;
                case 5: {
                    const auto start = random.below(source_model.size() + 1);
                    const auto count = random.below(source_model.size() - start + 1);
                    result = source.get_range(start, count);
                    model = std::vector<int>(
                        source_model.begin() + static_cast<std::ptrdiff_t>(start),
                        source_model.begin() + static_cast<std::ptrdiff_t>(start + count));
                    break;
                }
                case 6: {
                    const auto boundary = random.below(source_model.size() + 1);
                    const auto split = source.split_at(boundary);
                    if (random.below(2) == 0) {
                        result = split.left;
                        model.resize(boundary);
                    } else {
                        result = split.right;
                        model.erase(
                            model.begin(),
                            model.begin() + static_cast<std::ptrdiff_t>(boundary));
                    }
                    break;
                }
                case 7: {
                    const auto count = random.below(source_model.size() + 1);
                    result = source.take(count);
                    model.resize(count);
                    break;
                }
                case 8: {
                    const auto count = random.below(source_model.size() + 1);
                    result = source.drop(count);
                    model.erase(model.begin(), model.begin() + static_cast<std::ptrdiff_t>(count));
                    break;
                }
                case 9:
                    result = source.clear();
                    model.clear();
                    break;
                default:
                    result = source.push_front(value).reverse().push_back(~value);
                    model.insert(model.begin(), value);
                    std::reverse(model.begin(), model.end());
                    model.push_back(~value);
                    break;
                }

                assert_deque(model, result);
                assert_deque(source_model, source);
                versions.emplace_back(result, model);

                if (step % 29 == 0) {
                    for (auto probe = std::size_t{0}; probe != (std::min)(versions.size(), std::size_t{8});
                         ++probe) {
                        const auto index = random.below(versions.size());
                        assert_deque(versions[index].second, versions[index].first);
                    }
                }
            }

            assert_deque(std::vector<int>{}, root);
        }
    });

    tests.add("Bilateral ancestral deque indexing spends one query away from cached endpoints", [] {
        auto arena = std::make_shared<int_arena>();
        const auto root = int_deque::create(arena);
        const auto deque = eight_value_deque(arena);

        // Visible index zero, the end of the left interval, the first right value, and the last
        // visible index are the four cached endpoints; every other index costs exactly one query.
        const auto profile = std::vector<std::uint64_t>{0, 1, 1, 0, 0, 1, 1, 0};
        for (auto index = std::size_t{0}; index != deque.size(); ++index) {
            const auto value = with_queries(
                *arena,
                profile[index],
                "the indexing query count",
                [&] { return deque.at(index); });
            FT_REQUIRE_EQUAL(value, static_cast<int>(index) + 1);
        }

        const auto single_arm_profile = std::vector<std::uint64_t>{0, 1, 1, 0};
        const auto right_only = root.push_back(1).push_back(2).push_back(3).push_back(4);
        const auto left_only = root.push_front(4).push_front(3).push_front(2).push_front(1);
        for (auto index = std::size_t{0}; index != 4; ++index) {
            FT_REQUIRE_EQUAL(
                with_queries(
                    *arena,
                    single_arm_profile[index],
                    "the right-only indexing query count",
                    [&] { return right_only.at(index); }),
                static_cast<int>(index) + 1);
            FT_REQUIRE_EQUAL(
                with_queries(
                    *arena,
                    single_arm_profile[index],
                    "the left-only indexing query count",
                    [&] { return left_only.at(index); }),
                static_cast<int>(index) + 1);
        }

        // Endpoint reads, enumeration, insertion, reversal, and clearing never query at all.
        with_queries(*arena, 0, "the front read query count", [&] { return deque.front(); });
        with_queries(*arena, 0, "the back read query count", [&] { return deque.back(); });
        with_queries(*arena, 0, "the right-only front query count", [&] { return right_only.front(); });
        with_queries(*arena, 0, "the right-only back query count", [&] { return right_only.back(); });
        with_queries(*arena, 0, "the left-only front query count", [&] { return left_only.front(); });
        with_queries(*arena, 0, "the left-only back query count", [&] { return left_only.back(); });
        with_queries(*arena, 0, "the reversal query count", [&] { return deque.reverse(); });
        with_queries(*arena, 0, "the clear query count", [&] { return deque.clear(); });
        with_queries(*arena, 0, "the push_front query count", [&] { return deque.push_front(0); });
        with_queries(*arena, 0, "the push_back query count", [&] { return deque.push_back(9); });
        with_queries(*arena, 0, "the to_vector query count", [&] { return deque.to_vector(); });
        with_queries(*arena, 0, "the enumeration query count", [&] {
            return std::vector<int>(deque.begin(), deque.end());
        });

        const auto before = arena->statistics().ancestor_query_count;
        FT_REQUIRE_THROWS(std::out_of_range, deque.at(deque.size()));
        FT_REQUIRE_THROWS(
            std::out_of_range,
            deque.at((std::numeric_limits<std::size_t>::max)()));
        FT_REQUIRE_EQUAL(arena->statistics().ancestor_query_count, before);
    });

    tests.add("Bilateral ancestral deque slicing and splitting match their exact query profiles", [] {
        auto arena = std::make_shared<int_arena>();
        const auto deque = eight_value_deque(arena);
        const auto expected = inclusive_ints(1, 8);

        // Row `start` holds the exact query count for each `count` in `0..8 - start`. The maximum is
        // two, including every cross-arm entry, where each arm spends at most one.
        const auto slice_profile = std::vector<std::vector<std::uint64_t>>{
            {0, 0, 1, 1, 0, 0, 1, 1, 0},
            {0, 1, 2, 1, 1, 2, 2, 1},
            {0, 1, 1, 1, 2, 2, 1},
            {0, 1, 1, 2, 2, 1},
            {0, 0, 1, 1, 0},
            {0, 1, 2, 1},
            {0, 1, 1},
            {0, 1},
            {0},
        };

        for (auto start = std::size_t{0}; start <= deque.size(); ++start) {
            FT_REQUIRE_EQUAL(slice_profile[start].size(), deque.size() - start + 1);
            for (auto count = std::size_t{0}; count <= deque.size() - start; ++count) {
                const auto slice = with_queries(
                    *arena,
                    slice_profile[start][count],
                    "the slicing query count",
                    [&] { return deque.get_range(start, count); });
                FT_REQUIRE(
                    slice.to_vector()
                    == std::vector<int>(
                        expected.begin() + static_cast<std::ptrdiff_t>(start),
                        expected.begin() + static_cast<std::ptrdiff_t>(start + count)));
            }
        }

        const auto split_profile = std::vector<std::uint64_t>{0, 1, 2, 2, 0, 1, 2, 2, 0};
        for (auto boundary = std::size_t{0}; boundary <= deque.size(); ++boundary) {
            const auto split = with_queries(
                *arena,
                split_profile[boundary],
                "the splitting query count",
                [&] { return deque.split_at(boundary); });
            FT_REQUIRE(
                split.left.to_vector()
                == std::vector<int>(
                    expected.begin(),
                    expected.begin() + static_cast<std::ptrdiff_t>(boundary)));
            FT_REQUIRE(
                split.right.to_vector()
                == std::vector<int>(
                    expected.begin() + static_cast<std::ptrdiff_t>(boundary),
                    expected.end()));
        }

        for (auto count = std::size_t{0}; count <= deque.size(); ++count) {
            with_queries(*arena, slice_profile[0][count], "the take query count", [&] {
                return deque.take(count);
            });
            with_queries(
                *arena,
                slice_profile[count][deque.size() - count],
                "the drop query count",
                [&] { return deque.drop(count); });
        }

        // Rejected boundaries validate before touching the arena.
        const auto before = arena->statistics();
        const auto saturated = (std::numeric_limits<std::size_t>::max)();
        FT_REQUIRE_THROWS(std::out_of_range, deque.get_range(saturated, 1));
        FT_REQUIRE_THROWS(std::out_of_range, deque.get_range(1, saturated));
        FT_REQUIRE_THROWS(std::out_of_range, deque.get_range(9, 0));
        FT_REQUIRE_THROWS(std::out_of_range, deque.get_range(7, 2));
        FT_REQUIRE_THROWS(std::out_of_range, deque.take(9));
        FT_REQUIRE_THROWS(std::out_of_range, deque.drop(9));
        FT_REQUIRE_THROWS(std::out_of_range, deque.split_at(9));
        const auto after = arena->statistics();
        FT_REQUIRE_EQUAL(after.ancestor_query_count, before.ancestor_query_count);
        FT_REQUIRE_EQUAL(after.published_node_count, before.published_node_count);
        assert_deque(expected, deque);
    });

    tests.add("Bilateral ancestral deque removals match their exact query profiles", [] {
        auto arena = std::make_shared<int_arena>();
        const auto root = int_deque::create(arena);
        const auto deque = eight_value_deque(arena);

        // A removal on its own nonempty arm moves one cached tail to its parent and queries nothing.
        const auto left_own = with_queries(*arena, 0, "the own-arm remove_first query count", [&] {
            return deque.remove_first();
        });
        const auto right_own = with_queries(*arena, 0, "the own-arm remove_last query count", [&] {
            return deque.remove_last();
        });
        assert_deque(inclusive_ints(2, 8), left_own);
        assert_deque(inclusive_ints(1, 7), right_own);

        // Crossing the centre selects the next cached base with one query, except for the last two
        // values, where the count == 2 shortcut and the singleton shortcut both answer from cache.
        const auto crossing_profile = std::vector<std::uint64_t>{1, 1, 0, 0};
        auto right_only = root.push_back(10).push_back(11).push_back(12).push_back(13);
        for (auto step = std::size_t{0}; step != crossing_profile.size(); ++step) {
            right_only = with_queries(
                *arena,
                crossing_profile[step],
                "the crossing remove_first query count",
                [&] { return right_only.remove_first(); });
        }
        FT_REQUIRE(right_only.empty());

        auto left_only = root.push_front(13).push_front(12).push_front(11).push_front(10);
        for (auto step = std::size_t{0}; step != crossing_profile.size(); ++step) {
            left_only = with_queries(
                *arena,
                crossing_profile[step],
                "the crossing remove_last query count",
                [&] { return left_only.remove_last(); });
        }
        FT_REQUIRE(left_only.empty());

        // The value-returning removals cost exactly what the plain removals do.
        const auto two_value = root.push_back(1).push_back(2);
        with_queries(*arena, 0, "the two-value pop query count", [&] {
            return two_value.try_pop_front();
        });
        const auto three_value = root.push_back(1).push_back(2).push_back(3);
        const auto popped = with_queries(*arena, 1, "the three-value pop query count", [&] {
            return three_value.try_pop_front();
        });
        FT_REQUIRE(popped.has_value());
        FT_REQUIRE_EQUAL(*popped->value, 1);
        assert_deque(ints({2, 3}), popped->rest);

        const auto before = arena->statistics().ancestor_query_count;
        FT_REQUIRE(!root.try_pop_front().has_value());
        FT_REQUIRE(!root.try_pop_back().has_value());
        FT_REQUIRE_THROWS(std::logic_error, root.remove_first());
        FT_REQUIRE_THROWS(std::logic_error, root.remove_last());
        FT_REQUIRE_EQUAL(arena->statistics().ancestor_query_count, before);
    });

    tests.add("Bilateral ancestral deque validation reports counts and rejects a misreporting arena", [] {
        auto arena = std::make_shared<int_arena>();
        const auto root = int_deque::create(arena);

        // An empty handle has no ancestry path to confirm; each nonempty interval places its anchor
        // and its cached base, which is exactly two queries.
        with_queries(*arena, 0, "the empty audit query count", [&] {
            return root.validate_structure();
        });
        const auto right_only = root.push_back(1).push_back(2).push_back(3);
        const auto left_only = root.push_front(3).push_front(2).push_front(1);
        const auto bilateral = root.push_front(2).push_front(1).push_back(3).push_back(4);
        with_queries(*arena, 2, "the right-only audit query count", [&] {
            return right_only.validate_structure();
        });
        with_queries(*arena, 2, "the left-only audit query count", [&] {
            return left_only.validate_structure();
        });
        const auto statistics = with_queries(*arena, 4, "the bilateral audit query count", [&] {
            return bilateral.validate_structure();
        });
        FT_REQUIRE(statistics == counts(4, 2, 2));
        FT_REQUIRE(right_only.validate_structure() == counts(3, 0, 3));
        FT_REQUIRE(left_only.validate_structure() == counts(3, 3, 0));

        // The audit's failure branches are unreachable through a faithful backend, so a deliberately
        // misreporting arena is what exercises them. This also proves the arena really is a
        // compile-time parameter: the same handle runs unchanged over a second backend type.
        auto biased = std::make_shared<biased_arena<int>>();
        const auto over_biased = biased_deque::create(biased).push_back(1).push_back(2).push_back(3);
        assert_deque(ints({1, 2, 3}), over_biased);

        biased->depth_bias = 1;
        FT_REQUIRE_EQUAL(
            invariant_message([&] { (void)over_biased.validate_structure(); }),
            std::string{"an interval count disagrees with its endpoint depths"});
        biased->depth_bias = 0;

        biased->misreport_parent = true;
        FT_REQUIRE_EQUAL(
            invariant_message([&] { (void)over_biased.validate_structure(); }),
            std::string{"the cached base is not the first node after the anchor"});
        biased->misreport_parent = false;

        biased->misreported_ancestor_depth = -1;
        FT_REQUIRE_EQUAL(
            invariant_message([&] { (void)over_biased.validate_structure(); }),
            std::string{"the interval anchor is not an ancestor of its tail"});

        biased->misreported_ancestor_depth = 0;
        FT_REQUIRE_EQUAL(
            invariant_message([&] { (void)over_biased.validate_structure(); }),
            std::string{"the cached base is not on the tail's ancestry path"});

        biased->misreported_ancestor_depth.reset();
        FT_REQUIRE(over_biased.validate_structure() == counts(3, 0, 3));
    });

    tests.add("Bilateral ancestral deque reads publish nothing and one arena serves many families", [] {
        auto arena = std::make_shared<int_arena>();
        const auto root = int_deque::create(arena);
        const auto deque = root.push_front(2).push_front(1).push_back(3).push_back(4);

        const auto before = arena->statistics();
        (void)deque.to_vector();
        (void)std::vector<int>(deque.begin(), deque.end());
        (void)deque.front();
        (void)deque.back();
        (void)deque.at(2);
        (void)deque.reverse();
        (void)deque.clear();
        (void)deque.get_range(1, 2);
        (void)deque.validate_structure();
        const auto after = arena->statistics();
        FT_REQUIRE_EQUAL(after.published_node_count, before.published_node_count);
        FT_REQUIRE_EQUAL(after.add_leaf_count, before.add_leaf_count);

        const auto first = root.push_back(1).push_back(2);
        const auto second = root.push_front(9).push_front(8);
        assert_deque(ints({1, 2}), first);
        assert_deque(ints({8, 9}), second);
        FT_REQUIRE(!first.shares_storage_with(second));
        FT_REQUIRE(first.arena() == second.arena());
        FT_REQUIRE_EQUAL(arena->published_node_count(), std::size_t{8});
        FT_REQUIRE_EQUAL(arena->statistics().published_node_count, std::size_t{8});

        // Two equal values over two arenas are never reported as sharing storage.
        const auto elsewhere = int_deque::from_range({1, 2});
        FT_REQUIRE(elsewhere.to_vector() == first.to_vector());
        FT_REQUIRE(!elsewhere.shares_storage_with(first));
    });

    tests.add("Bilateral ancestral deque handles survive concurrent branching over one arena", [] {
        const auto worker_count = std::size_t{4};
        const auto steps = std::size_t{160};

        auto arena = std::make_shared<int_arena>();
        auto seed = int_deque::create(arena);
        for (auto value = 0; value != 48; ++value) {
            seed = seed.push_back(value);
        }
        const auto root = seed;
        const auto root_model = inclusive_ints(0, 47);

        auto failed = std::atomic<bool>{false};
        auto additions = std::atomic<std::size_t>{0};
        {
            auto workers = std::vector<std::jthread>{};
            for (auto worker = std::size_t{0}; worker != worker_count; ++worker) {
                workers.emplace_back([&, worker] {
                    try {
                        auto deque = root;
                        auto model = root_model;
                        auto published = std::size_t{0};
                        for (auto step = std::size_t{0}; step != steps; ++step) {
                            switch (step % 6) {
                            case 0: {
                                const auto value = -1 - static_cast<int>(worker * steps + step);
                                deque = deque.push_front(value);
                                model.insert(model.begin(), value);
                                ++published;
                                break;
                            }
                            case 1: {
                                const auto value = 10'000 + static_cast<int>(worker * steps + step);
                                deque = deque.push_back(value);
                                model.push_back(value);
                                ++published;
                                break;
                            }
                            case 2:
                                deque = deque.reverse();
                                std::reverse(model.begin(), model.end());
                                break;
                            case 3:
                                if (!model.empty()) {
                                    deque = deque.remove_first();
                                    model.erase(model.begin());
                                }
                                break;
                            case 4: {
                                const auto boundary = (worker * 17 + step) % (model.size() + 1);
                                const auto split = deque.split_at(boundary);
                                if ((worker + step) % 2 == 0) {
                                    deque = split.left;
                                    model.resize(boundary);
                                } else {
                                    deque = split.right;
                                    model.erase(
                                        model.begin(),
                                        model.begin() + static_cast<std::ptrdiff_t>(boundary));
                                }
                                break;
                            }
                            default:
                                if (root.to_vector() != root_model) {
                                    throw std::logic_error("a retained root changed under readers");
                                }
                                break;
                            }

                            if (deque.to_vector() != model) {
                                throw std::logic_error("a concurrent branch left the oracle");
                            }
                            (void)deque.validate_structure();
                        }

                        additions.fetch_add(published, std::memory_order_relaxed);
                    } catch (...) {
                        failed.store(true, std::memory_order_relaxed);
                    }
                });
            }
        }

        FT_REQUIRE(!failed.load(std::memory_order_relaxed));
        assert_deque(root_model, root);
        FT_REQUIRE_EQUAL(
            arena->published_node_count(),
            root_model.size() + additions.load(std::memory_order_relaxed));
    });
}
