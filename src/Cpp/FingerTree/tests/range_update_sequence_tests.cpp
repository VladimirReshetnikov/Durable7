#include <durable7/finger_tree/range_update_sequence.hpp>

#include "test_support/allocation_counter.hpp"
#include "test_support/command_model.hpp"
#include "test_support/test_runner.hpp"

#include <algorithm>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

using namespace durable7::finger_tree;
using namespace durable7::finger_tree::tests;

namespace {

struct affine_tag final {
    bool alternate_identity = false;
    std::optional<std::int64_t> assignment;
    std::int64_t addition = 0;

    friend bool operator==(const affine_tag&, const affine_tag&) = default;
};

[[nodiscard]] affine_tag add_tag(const std::int64_t amount)
{
    return affine_tag{false, std::nullopt, amount};
}

[[nodiscard]] affine_tag assign_tag(const std::int64_t value)
{
    return affine_tag{false, value, 0};
}

struct affine_sum_algebra {
    using measure_type = std::int64_t;
    using tag_type = affine_tag;

    [[nodiscard]] static constexpr measure_type empty() noexcept { return 0; }
    [[nodiscard]] static constexpr measure_type measure(const std::int64_t element) noexcept { return element; }
    [[nodiscard]] static constexpr measure_type combine(
        const measure_type left,
        const measure_type right) noexcept
    {
        return left + right;
    }
    [[nodiscard]] static constexpr tag_type identity_tag() noexcept { return {}; }
    [[nodiscard]] static constexpr bool is_identity(const tag_type& tag) noexcept
    {
        return !tag.assignment.has_value() && tag.addition == 0;
    }
    [[nodiscard]] static constexpr tag_type compose(const tag_type& newer, const tag_type& older) noexcept
    {
        if (is_identity(newer)) {
            return older;
        }
        if (is_identity(older)) {
            return newer;
        }
        if (newer.assignment.has_value()) {
            return newer;
        }
        if (older.assignment.has_value()) {
            return affine_tag{false, *older.assignment + older.addition + newer.addition, 0};
        }
        return add_tag(older.addition + newer.addition);
    }
    [[nodiscard]] static constexpr std::int64_t apply_element(
        const tag_type& tag,
        const std::int64_t element) noexcept
    {
        return tag.assignment.value_or(element) + tag.addition;
    }
    [[nodiscard]] static constexpr measure_type apply_measure(
        const tag_type& tag,
        const measure_type value,
        const std::size_t count) noexcept
    {
        if (count == 0) {
            return empty();
        }
        const auto signed_count = static_cast<std::int64_t>(count);
        return tag.assignment.has_value()
            ? (*tag.assignment + tag.addition) * signed_count
            : value + tag.addition * signed_count;
    }
};

using affine_sequence = range_update_sequence<std::int64_t, affine_sum_algebra>;

[[nodiscard]] std::vector<std::int64_t> inclusive_values(
    const std::int64_t first,
    const std::int64_t last)
{
    auto result = std::vector<std::int64_t>{};
    for (auto value = first; value <= last; ++value) {
        result.push_back(value);
    }
    return result;
}

[[nodiscard]] std::size_t avl_height_ceiling(const std::size_t count)
{
    if (count == 0) {
        return 0;
    }
    auto remaining = count + 1;
    auto logarithm = std::size_t{0};
    while (remaining > 1) {
        remaining /= 2;
        ++logarithm;
    }
    return 2 * logarithm + 1;
}

template <class Algebra>
void require_sequence(
    const std::vector<std::int64_t>& expected,
    const range_update_sequence<std::int64_t, Algebra>& actual)
{
    FT_REQUIRE(actual.to_vector() == expected);
    FT_REQUIRE_EQUAL(actual.size(), expected.size());
    const auto expected_measure = std::accumulate(
        expected.begin(),
        expected.end(),
        std::int64_t{0});
    FT_REQUIRE_EQUAL(actual.measure(), expected_measure);
    const auto statistics = actual.validate_structure();
    FT_REQUIRE(statistics.has_value());
    FT_REQUIRE_EQUAL(statistics->count, expected.size());
    FT_REQUIRE(statistics->height <= avl_height_ceiling(expected.size()));
    FT_REQUIRE(statistics->maximum_absolute_balance_factor <= 1);
}

struct trace_algebra {
    using measure_type = std::vector<std::int64_t>;
    using tag_type = std::int64_t;

    [[nodiscard]] static measure_type empty() { return {}; }
    [[nodiscard]] static measure_type measure(const std::int64_t element) { return {element}; }
    [[nodiscard]] static measure_type combine(const measure_type& left, const measure_type& right)
    {
        auto result = left;
        result.insert(result.end(), right.begin(), right.end());
        return result;
    }
    [[nodiscard]] static constexpr tag_type identity_tag() noexcept { return 0; }
    [[nodiscard]] static constexpr bool is_identity(const tag_type tag) noexcept { return tag == 0; }
    [[nodiscard]] static constexpr tag_type compose(const tag_type newer, const tag_type older) noexcept
    {
        return older + newer;
    }
    [[nodiscard]] static constexpr std::int64_t apply_element(
        const tag_type tag,
        const std::int64_t element) noexcept
    {
        return element + tag;
    }
    [[nodiscard]] static measure_type apply_measure(
        const tag_type tag,
        const measure_type& measure_value,
        const std::size_t count)
    {
        FT_REQUIRE_EQUAL(measure_value.size(), count);
        auto result = measure_value;
        for (auto& value : result) {
            value += tag;
        }
        return result;
    }
};

struct nullable_tag final {
    std::optional<std::optional<int>> replacement;
};

[[nodiscard]] nullable_tag replace_with(std::optional<int> value)
{
    auto tag = nullable_tag{};
    tag.replacement.emplace(std::move(value));
    return tag;
}

struct nullable_algebra {
    using measure_type = std::vector<std::optional<int>>;
    using tag_type = nullable_tag;

    [[nodiscard]] static measure_type empty() { return {}; }
    [[nodiscard]] static measure_type measure(const std::optional<int>& element) { return {element}; }
    [[nodiscard]] static measure_type combine(const measure_type& left, const measure_type& right)
    {
        auto result = left;
        result.insert(result.end(), right.begin(), right.end());
        return result;
    }
    [[nodiscard]] static tag_type identity_tag() { return {}; }
    [[nodiscard]] static bool is_identity(const tag_type& tag) noexcept
    {
        return !tag.replacement.has_value();
    }
    [[nodiscard]] static tag_type compose(const tag_type& newer, const tag_type& older)
    {
        return newer.replacement.has_value() ? newer : older;
    }
    [[nodiscard]] static std::optional<int> apply_element(
        const tag_type& tag,
        const std::optional<int>& element)
    {
        return tag.replacement.has_value() ? *tag.replacement : element;
    }
    [[nodiscard]] static measure_type apply_measure(
        const tag_type& tag,
        const measure_type& measure_value,
        const std::size_t count)
    {
        return tag.replacement.has_value()
            ? measure_type(count, *tag.replacement)
            : measure_value;
    }
};

inline bool enumeration_complete = false;
inline std::size_t ordered_measure_calls = 0;

struct one_shot_range final {
    struct sentinel final {};

    struct iterator final {
        using value_type = std::int64_t;
        using difference_type = std::ptrdiff_t;
        using iterator_concept = std::input_iterator_tag;

        std::size_t position = 0;

        [[nodiscard]] value_type operator*() const noexcept
        {
            return static_cast<value_type>(position);
        }

        iterator& operator++() noexcept
        {
            ++position;
            return *this;
        }

        void operator++(int) noexcept
        {
            ++*this;
        }

        friend bool operator==(const iterator& value, sentinel) noexcept
        {
            if (value.position >= 8) {
                enumeration_complete = true;
                return true;
            }
            return false;
        }

        // C++20 equality rewriting supplies sentinel == iterator from the
        // iterator == sentinel overload above. Keeping both overloads causes
        // current Clang to reject the redundant internal friend under -Werror.
    };

    [[nodiscard]] iterator begin() const noexcept { return {}; }
    [[nodiscard]] sentinel end() const noexcept { return {}; }
};

struct ordered_measure_algebra {
    using measure_type = std::int64_t;
    using tag_type = bool;

    [[nodiscard]] static constexpr measure_type empty() noexcept { return 0; }
    [[nodiscard]] static measure_type measure(const std::int64_t element)
    {
        if (!enumeration_complete) {
            throw std::logic_error("range enumeration did not complete before measurement");
        }
        ++ordered_measure_calls;
        return element;
    }
    [[nodiscard]] static constexpr measure_type combine(
        const measure_type left,
        const measure_type right) noexcept
    {
        return left + right;
    }
    [[nodiscard]] static constexpr tag_type identity_tag() noexcept { return false; }
    [[nodiscard]] static constexpr bool is_identity(const tag_type tag) noexcept { return !tag; }
    [[nodiscard]] static constexpr tag_type compose(const tag_type newer, const tag_type older) noexcept
    {
        return newer || older;
    }
    [[nodiscard]] static constexpr std::int64_t apply_element(
        const tag_type tag,
        const std::int64_t element) noexcept
    {
        return tag ? 0 : element;
    }
    [[nodiscard]] static constexpr measure_type apply_measure(
        const tag_type tag,
        const measure_type measure_value,
        const std::size_t) noexcept
    {
        return tag ? 0 : measure_value;
    }
};

enum class policy_callback {
    none,
    measure,
    combine,
    is_identity,
    compose,
    apply_element,
    apply_measure,
};

class callback_failure final : public std::runtime_error {
public:
    callback_failure(const policy_callback callback, const std::size_t ordinal)
        : std::runtime_error("injected range-update policy failure")
        , callback_(callback)
        , ordinal_(ordinal)
    {
    }

    [[nodiscard]] policy_callback callback() const noexcept { return callback_; }
    [[nodiscard]] std::size_t ordinal() const noexcept { return ordinal_; }

private:
    policy_callback callback_;
    std::size_t ordinal_;
};

struct throwing_affine_algebra {
    using measure_type = affine_sum_algebra::measure_type;
    using tag_type = affine_sum_algebra::tag_type;

    static inline policy_callback armed_callback = policy_callback::none;
    static inline std::size_t armed_ordinal = 0;
    static inline std::size_t hits = 0;

    static void arm(const policy_callback callback, const std::size_t ordinal) noexcept
    {
        armed_callback = callback;
        armed_ordinal = ordinal;
        hits = 0;
    }

    static void disarm() noexcept
    {
        arm(policy_callback::none, 0);
    }

    static void hit(const policy_callback callback)
    {
        if (armed_callback != callback) {
            return;
        }
        ++hits;
        if (hits == armed_ordinal) {
            throw callback_failure{callback, hits};
        }
    }

    [[nodiscard]] static constexpr measure_type empty() noexcept { return affine_sum_algebra::empty(); }
    [[nodiscard]] static measure_type measure(const std::int64_t element)
    {
        hit(policy_callback::measure);
        return affine_sum_algebra::measure(element);
    }
    [[nodiscard]] static measure_type combine(const measure_type left, const measure_type right)
    {
        hit(policy_callback::combine);
        return affine_sum_algebra::combine(left, right);
    }
    [[nodiscard]] static constexpr tag_type identity_tag() noexcept
    {
        return affine_sum_algebra::identity_tag();
    }
    [[nodiscard]] static bool is_identity(const tag_type& tag)
    {
        hit(policy_callback::is_identity);
        return affine_sum_algebra::is_identity(tag);
    }
    [[nodiscard]] static tag_type compose(const tag_type& newer, const tag_type& older)
    {
        hit(policy_callback::compose);
        return affine_sum_algebra::compose(newer, older);
    }
    [[nodiscard]] static std::int64_t apply_element(
        const tag_type& tag,
        const std::int64_t element)
    {
        hit(policy_callback::apply_element);
        return affine_sum_algebra::apply_element(tag, element);
    }
    [[nodiscard]] static measure_type apply_measure(
        const tag_type& tag,
        const measure_type measure_value,
        const std::size_t count)
    {
        hit(policy_callback::apply_measure);
        return affine_sum_algebra::apply_measure(tag, measure_value, count);
    }
};

using throwing_sequence = range_update_sequence<std::int64_t, throwing_affine_algebra>;

void exhaust_callback_failures(
    const policy_callback callback,
    const throwing_sequence& source,
    const std::vector<std::int64_t>& expected)
{
    auto reached_success = false;
    for (auto ordinal = std::size_t{1}; ordinal != 1'024; ++ordinal) {
        throwing_affine_algebra::arm(callback, ordinal);
        try {
            const auto result = callback == policy_callback::compose
                ? source.apply_range(0, source.size(), add_tag(1))
                : source.apply_range(7, 41, assign_tag(9));
            (void)result;
            throwing_affine_algebra::disarm();
            FT_REQUIRE(ordinal > 1);
            reached_success = true;
            break;
        } catch (const callback_failure& failure) {
            throwing_affine_algebra::disarm();
            FT_REQUIRE(failure.callback() == callback);
            FT_REQUIRE_EQUAL(failure.ordinal(), ordinal);
            require_sequence(expected, source);
        } catch (...) {
            throwing_affine_algebra::disarm();
            throw;
        }
    }
    FT_REQUIRE(reached_success);
}

void require_iterator_callback_retry(
    const throwing_sequence& source,
    const policy_callback callback)
{
    const auto expected = source.to_vector();
    auto throwing_position = std::optional<std::size_t>{};

    for (auto position = std::size_t{0}; position + 1 < source.size(); ++position) {
        auto probe = source.begin();
        for (auto skipped = std::size_t{0}; skipped != position; ++skipped) {
            ++probe;
        }

        throwing_affine_algebra::arm(callback, (std::numeric_limits<std::size_t>::max)());
        ++probe;
        const auto hits = throwing_affine_algebra::hits;
        throwing_affine_algebra::disarm();
        if (hits != 0) {
            throwing_position = position;
            break;
        }
    }

    FT_REQUIRE(throwing_position.has_value());
    auto iterator = source.begin();
    for (auto skipped = std::size_t{0}; skipped != *throwing_position; ++skipped) {
        ++iterator;
    }
    FT_REQUIRE_EQUAL(*iterator, expected[*throwing_position]);

    auto threw = false;
    throwing_affine_algebra::arm(callback, 1);
    try {
        ++iterator;
    } catch (const callback_failure& failure) {
        threw = true;
        FT_REQUIRE(failure.callback() == callback);
    } catch (...) {
        throwing_affine_algebra::disarm();
        throw;
    }
    throwing_affine_algebra::disarm();
    FT_REQUIRE(threw);
    FT_REQUIRE_EQUAL(*iterator, expected[*throwing_position]);

    ++iterator;
    FT_REQUIRE_EQUAL(*iterator, expected[*throwing_position + 1]);
}

struct count_tag final {};

struct count_algebra {
    using measure_type = std::size_t;
    using tag_type = count_tag;

    [[nodiscard]] static constexpr measure_type empty() noexcept { return 0; }
    [[nodiscard]] static constexpr measure_type measure(const int&) noexcept { return 1; }
    [[nodiscard]] static constexpr measure_type combine(
        const measure_type left,
        const measure_type right)
    {
        return checked_add(left, right);
    }
    [[nodiscard]] static constexpr tag_type identity_tag() noexcept { return {}; }
    [[nodiscard]] static constexpr bool is_identity(const tag_type&) noexcept { return true; }
    [[nodiscard]] static constexpr tag_type compose(const tag_type&, const tag_type&) noexcept { return {}; }
    [[nodiscard]] static constexpr int apply_element(const tag_type&, const int element) noexcept
    {
        return element;
    }
    [[nodiscard]] static constexpr measure_type apply_measure(
        const tag_type&,
        const measure_type value,
        const std::size_t) noexcept
    {
        return value;
    }
};

} // namespace

void add_range_update_sequence_tests(suite& tests)
{
    static_assert(range_update_algebra<affine_sum_algebra, std::int64_t>);
    static_assert(std::input_iterator<affine_sequence::const_iterator>);
    static_assert(!std::forward_iterator<affine_sequence::const_iterator>);
    static_assert(std::ranges::input_range<affine_sequence>);

    tests.add("Range-update algebra laws include distinct identities and directional composition", [] {
        const auto identity = affine_sum_algebra::identity_tag();
        const auto alternate_identity = affine_tag{true, std::nullopt, 0};
        const auto tags = std::vector{
            identity,
            alternate_identity,
            add_tag(5),
            add_tag(-3),
            assign_tag(7),
            affine_tag{false, 4, 9},
        };
        const auto values = std::vector<std::int64_t>{-5, 0, 11};
        const auto require_tags_equivalent = [](const affine_tag& expected, const affine_tag& actual) {
            for (auto element = std::int64_t{-7}; element <= 7; ++element) {
                FT_REQUIRE_EQUAL(
                    affine_sum_algebra::apply_element(expected, element),
                    affine_sum_algebra::apply_element(actual, element));
            }
            FT_REQUIRE_EQUAL(
                affine_sum_algebra::apply_measure(expected, 31, 4),
                affine_sum_algebra::apply_measure(actual, 31, 4));
        };

        FT_REQUIRE(identity != alternate_identity);
        FT_REQUIRE(affine_sum_algebra::is_identity(alternate_identity));
        const auto measures = std::vector<std::int64_t>{-7, 0, 12};
        for (const auto measure : measures) {
            FT_REQUIRE_EQUAL(
                affine_sum_algebra::combine(affine_sum_algebra::empty(), measure),
                measure);
            FT_REQUIRE_EQUAL(
                affine_sum_algebra::combine(measure, affine_sum_algebra::empty()),
                measure);
        }
        for (const auto left : measures) {
            for (const auto middle : measures) {
                for (const auto right : measures) {
                    FT_REQUIRE_EQUAL(
                        affine_sum_algebra::combine(
                            left,
                            affine_sum_algebra::combine(middle, right)),
                        affine_sum_algebra::combine(
                            affine_sum_algebra::combine(left, middle),
                            right));
                }
            }
        }

        for (const auto& older : tags) {
            require_tags_equivalent(older, affine_sum_algebra::compose(identity, older));
            require_tags_equivalent(older, affine_sum_algebra::compose(older, identity));
            for (const auto value : values) {
                FT_REQUIRE_EQUAL(
                    affine_sum_algebra::apply_element(
                        affine_sum_algebra::compose(identity, older),
                        value),
                    affine_sum_algebra::apply_element(older, value));
                FT_REQUIRE_EQUAL(
                    affine_sum_algebra::apply_element(
                        affine_sum_algebra::compose(older, alternate_identity),
                        value),
                    affine_sum_algebra::apply_element(older, value));
                FT_REQUIRE_EQUAL(
                    affine_sum_algebra::apply_measure(
                        older,
                        affine_sum_algebra::measure(value),
                        1),
                    affine_sum_algebra::measure(
                        affine_sum_algebra::apply_element(older, value)));
            }
            FT_REQUIRE_EQUAL(
                affine_sum_algebra::apply_measure(older, affine_sum_algebra::empty(), 0),
                affine_sum_algebra::empty());
            FT_REQUIRE_EQUAL(
                affine_sum_algebra::apply_measure(identity, 17, 3),
                17);
            FT_REQUIRE_EQUAL(
                affine_sum_algebra::apply_measure(alternate_identity, 17, 3),
                17);

            const auto left_measure = std::int64_t{3};
            const auto right_measure = std::int64_t{9};
            FT_REQUIRE_EQUAL(
                affine_sum_algebra::apply_measure(
                    older,
                    affine_sum_algebra::combine(left_measure, right_measure),
                    5),
                affine_sum_algebra::combine(
                    affine_sum_algebra::apply_measure(older, left_measure, 2),
                    affine_sum_algebra::apply_measure(older, right_measure, 3)));
            for (const auto& newer : tags) {
                for (const auto value : values) {
                    FT_REQUIRE_EQUAL(
                        affine_sum_algebra::apply_element(
                            affine_sum_algebra::compose(newer, older),
                            value),
                        affine_sum_algebra::apply_element(
                            newer,
                            affine_sum_algebra::apply_element(older, value)));
                }
                FT_REQUIRE_EQUAL(
                    affine_sum_algebra::apply_measure(
                        affine_sum_algebra::compose(newer, older),
                        31,
                        4),
                    affine_sum_algebra::apply_measure(
                        newer,
                        affine_sum_algebra::apply_measure(older, 31, 4),
                        4));
            }
        }

        const auto trace_left = trace_algebra::measure_type{1, 2};
        const auto trace_right = trace_algebra::measure_type{3, 4, 5};
        FT_REQUIRE(
            trace_algebra::apply_measure(
                10,
                trace_algebra::combine(trace_left, trace_right),
                5)
            == trace_algebra::combine(
                trace_algebra::apply_measure(10, trace_left, 2),
                trace_algebra::apply_measure(10, trace_right, 3)));

        for (const auto& oldest : tags) {
            for (const auto& middle : tags) {
                for (const auto& newest : tags) {
                    require_tags_equivalent(
                        affine_sum_algebra::compose(
                            newest,
                            affine_sum_algebra::compose(middle, oldest)),
                        affine_sum_algebra::compose(
                            affine_sum_algebra::compose(newest, middle),
                            oldest));
                }
            }
        }

        FT_REQUIRE_EQUAL(
            affine_sum_algebra::apply_element(
                affine_sum_algebra::compose(assign_tag(7), add_tag(10)),
                3),
            7);
        FT_REQUIRE_EQUAL(
            affine_sum_algebra::apply_element(
                affine_sum_algebra::compose(add_tag(10), assign_tag(7)),
                3),
            17);
    });

    tests.add("Range-update surface preserves snapshots and treats inserted values as current", [] {
        const auto empty = affine_sequence{};
        FT_REQUIRE(empty.empty());
        FT_REQUIRE_EQUAL(empty.size(), std::size_t{0});
        FT_REQUIRE_EQUAL(empty.measure(), 0);
        FT_REQUIRE_EQUAL(empty.measure_range(0, 0), 0);
        FT_REQUIRE(empty.shares_root_with(empty.apply_range(0, 0, add_tag(99))));
        const auto empty_split = empty.split_at(0);
        FT_REQUIRE(empty_split.left.empty());
        FT_REQUIRE(empty_split.right.empty());
        FT_REQUIRE(empty.get_range(0, 0).empty());
        FT_REQUIRE_THROWS(std::out_of_range, empty.at(0));
        FT_REQUIRE_THROWS(std::out_of_range, empty.remove_at(0));

        const auto original_values = inclusive_values(1, 8);
        const auto original = affine_sequence::from_range(original_values);
        require_sequence(original_values, original);
        FT_REQUIRE(affine_sequence::from_range(original).shares_root_with(original));
        FT_REQUIRE_EQUAL(original[3], 4);

        const auto added = original.apply_range(2, 4, add_tag(10));
        require_sequence({1, 2, 13, 14, 15, 16, 7, 8}, added);
        const auto assigned = added.apply_range(3, 3, assign_tag(7));
        require_sequence({1, 2, 13, 7, 7, 7, 7, 8}, assigned);
        FT_REQUIRE_EQUAL(assigned.measure_range(2, 4), 34);
        FT_REQUIRE_EQUAL(assigned.measure_range(8, 0), 0);
        require_sequence(original_values, original);

        require_sequence({2, 3, 3, 4, 5, 6, 7, 8}, original.apply_range(0, 2, add_tag(1)));
        require_sequence({1, 2, 3, 4, 5, 6, 8, 9}, original.apply_range(6, 2, add_tag(1)));
        require_sequence({1, 2, 3, 4, 15, 6, 7, 8}, original.apply_range(4, 1, add_tag(10)));
        FT_REQUIRE_EQUAL(original.measure_range(0, 2), 3);
        FT_REQUIRE_EQUAL(original.measure_range(6, 2), 15);

        for (auto boundary = std::size_t{0}; boundary <= original.size(); ++boundary) {
            const auto boundary_split = original.split_at(boundary);
            require_sequence(original_values, boundary_split.left.concat(boundary_split.right));
            FT_REQUIRE(original.get_range(boundary, 0).empty());
        }

        const auto fully_tagged = original.apply_range(0, original.size(), add_tag(5));
        require_sequence({6, 7, 8, 9, 10, 11, 12, 13}, fully_tagged);
        require_sequence(
            {6, 7, 8, 100, 10, 11, 12, 13},
            fully_tagged.set_item(3, 100));
        require_sequence(
            {6, 7, 50, 8, 9, 10, 11, 12, 13},
            fully_tagged.insert_at(2, 50));
        require_sequence(
            {6, 8, 9, 10, 11, 12, 13},
            fully_tagged.remove_at(1));
        require_sequence({0, 1, 2, 3, 4, 5, 6, 7, 8}, original.prepend(0));
        require_sequence({1, 2, 3, 4, 5, 6, 7, 8, 9}, original.append(9));
        FT_REQUIRE(affine_sequence{7}.remove_at(0).empty());

        const auto split = assigned.split_at(3);
        require_sequence({1, 2, 13}, split.left);
        require_sequence({7, 7, 7, 7, 8}, split.right);
        require_sequence(assigned.to_vector(), split.left.concat(split.right));
        require_sequence({13, 7, 7, 7}, assigned.get_range(2, 4));

        FT_REQUIRE_THROWS(std::out_of_range, original.at(8));
        FT_REQUIRE_THROWS(std::out_of_range, original.insert_at(9, 0));
        FT_REQUIRE_THROWS(std::out_of_range, original.set_item(8, 0));
        FT_REQUIRE_THROWS(std::out_of_range, original.remove_at(8));
        FT_REQUIRE_THROWS(std::out_of_range, original.split_at(9));
        FT_REQUIRE_THROWS(std::out_of_range, original.get_range(7, 2));
        FT_REQUIRE_THROWS(
            std::out_of_range,
            original.apply_range(1, (std::numeric_limits<std::size_t>::max)(), add_tag(1)));
    });

    tests.add("Range-update measures preserve order and nullable elements remain unambiguous", [] {
        using trace_sequence = range_update_sequence<std::int64_t, trace_algebra>;
        const auto traced = trace_sequence::from_range(std::vector<std::int64_t>{1, 2, 3, 4, 5})
                                .apply_range(1, 3, 10);
        FT_REQUIRE(traced.measure() == std::vector<std::int64_t>({1, 12, 13, 14, 5}));
        FT_REQUIRE(traced.measure_range(1, 3) == std::vector<std::int64_t>({12, 13, 14}));
        FT_REQUIRE(traced.validate_structure().has_value());

        using nullable_sequence = range_update_sequence<std::optional<int>, nullable_algebra>;
        const auto nullable = nullable_sequence::from_range(
            std::vector<std::optional<int>>{1, std::nullopt, 3, 4});
        const auto cleared = nullable.apply_range(0, 3, replace_with(std::nullopt));
        FT_REQUIRE(cleared.to_vector()
            == std::vector<std::optional<int>>({std::nullopt, std::nullopt, std::nullopt, 4}));
        const auto replaced = cleared.apply_range(1, 2, replace_with(9));
        FT_REQUIRE(replaced.to_vector()
            == std::vector<std::optional<int>>({std::nullopt, 9, 9, 4}));
        FT_REQUIRE(replaced.validate_structure().has_value());
    });

    tests.add("Range-update retained thousand-step history matches a vector model", [] {
        deterministic_rng random{0x52414e4745555044ULL};
        auto model = inclusive_values(0, 15);
        const auto original = affine_sequence::from_range(model);
        auto sequence = original;

        struct snapshot final {
            affine_sequence sequence;
            std::vector<std::int64_t> model;
        };
        auto snapshots = std::vector<snapshot>{{sequence, model}};

        for (auto step = 0; step != 1'000; ++step) {
            const auto boundary = random.next_index(model.size() + 1);
            const auto position = model.empty() ? std::size_t{0} : random.next_index(model.size());
            const auto start = random.next_index(model.size() + 1);
            const auto count = random.next_index(model.size() - start + 1);

            switch (static_cast<std::size_t>(step) % 8) {
            case 0:
                if (model.size() < 64) {
                    const auto value = static_cast<std::int64_t>(random.next_int(-1'000, 1'000));
                    sequence = sequence.insert_at(boundary, value);
                    model.insert(model.begin() + static_cast<std::ptrdiff_t>(boundary), value);
                }
                break;
            case 1:
                if (!model.empty()) {
                    sequence = sequence.remove_at(position);
                    model.erase(model.begin() + static_cast<std::ptrdiff_t>(position));
                }
                break;
            case 2:
                if (!model.empty()) {
                    const auto value = std::int64_t{10'000} + step;
                    sequence = sequence.set_item(position, value);
                    model[position] = value;
                }
                break;
            case 3: {
                const auto tag = add_tag(random.next_int(-10, 10));
                sequence = sequence.apply_range(start, count, tag);
                for (auto index = start; index != start + count; ++index) {
                    model[index] = affine_sum_algebra::apply_element(tag, model[index]);
                }
                break;
            }
            case 4: {
                const auto tag = assign_tag(random.next_int(-15, 15));
                sequence = sequence.apply_range(start, count, tag);
                for (auto index = start; index != start + count; ++index) {
                    model[index] = affine_sum_algebra::apply_element(tag, model[index]);
                }
                break;
            }
            case 5: {
                const auto expected = std::accumulate(
                    model.begin() + static_cast<std::ptrdiff_t>(start),
                    model.begin() + static_cast<std::ptrdiff_t>(start + count),
                    std::int64_t{0});
                FT_REQUIRE_EQUAL(sequence.measure_range(start, count), expected);
                break;
            }
            case 6: {
                const auto split = sequence.split_at(boundary);
                sequence = split.left.concat(split.right);
                break;
            }
            default:
                FT_REQUIRE(sequence.get_range(start, count).to_vector()
                    == std::vector<std::int64_t>(
                        model.begin() + static_cast<std::ptrdiff_t>(start),
                        model.begin() + static_cast<std::ptrdiff_t>(start + count)));
                break;
            }

            require_sequence(model, sequence);
            if (step % 73 == 0) {
                snapshots.push_back(snapshot{sequence, model});
            }
            if (step % 97 == 0) {
                const auto& retained = snapshots[random.next_index(snapshots.size())];
                auto branch_model = retained.model;
                const auto branch_start = random.next_index(branch_model.size() + 1);
                const auto branch_count = random.next_index(branch_model.size() - branch_start + 1);
                const auto tag = add_tag(3);
                const auto branch = retained.sequence.apply_range(branch_start, branch_count, tag);
                for (auto index = branch_start; index != branch_start + branch_count; ++index) {
                    branch_model[index] += 3;
                }
                require_sequence(branch_model, branch);
                require_sequence(retained.model, retained.sequence);
            }
        }

        require_sequence(inclusive_values(0, 15), original);
        for (const auto& snapshot : snapshots) {
            require_sequence(snapshot.model, snapshot.sequence);
        }
    });

    tests.add("Range-update identity, sharing, allocation, and value-iterator lifetime contracts hold", [] {
        const auto values = inclusive_values(1, 31);
        const auto sequence = affine_sequence::from_range(values);
        const auto identity = sequence.apply_range(3, 10, affine_tag{true, std::nullopt, 0});
        const auto empty_update = sequence.apply_range(7, 0, add_tag(99));
        const auto split_right = sequence.split_at(0).right;
        FT_REQUIRE(sequence.shares_root_with(identity));
        FT_REQUIRE(sequence.shares_root_with(empty_update));
        FT_REQUIRE(sequence.shares_root_with(split_right));
        FT_REQUIRE(sequence.shares_root_with(sequence.concat(affine_sequence{})));
        FT_REQUIRE(sequence.shares_root_with(affine_sequence{}.concat(sequence)));

        const auto full = sequence.apply_range(0, sequence.size(), add_tag(1));
        FT_REQUIRE(!sequence.shares_root_with(full));
        FT_REQUIRE_EQUAL(sequence.physical_node_count(), 31U);
        FT_REQUIRE_EQUAL(sequence.shared_node_count_with(full), 30U);
        FT_REQUIRE(full.shares_structure_with(sequence));
        const auto statistics = full.validate_structure();
        FT_REQUIRE(statistics.has_value());
        FT_REQUIRE(statistics->pending_tag_count > 0);

#ifndef FINGERTREE_DISABLE_ALLOCATION_TRACKING
        {
            allocation_counting_scope scope;
            const auto one_root = sequence.apply_range(0, sequence.size(), add_tag(2));
            (void)one_root;
            FT_REQUIRE_EQUAL(scope.allocations(), std::size_t{1});
        }
#endif

        auto first = full.begin();
        auto copied = first;
        FT_REQUIRE_EQUAL(*first, 2);
        FT_REQUIRE_EQUAL(*copied, 2);
        ++first;
        FT_REQUIRE_EQUAL(*first, 3);
        FT_REQUIRE_EQUAL(*copied, 2);

        auto escaped = [] {
            const auto local = affine_sequence::from_range(inclusive_values(1, 5))
                                   .apply_range(0, 5, add_tag(10));
            return local.begin();
        }();
        auto escaped_values = std::vector<std::int64_t>{};
        while (escaped != affine_sequence{}.end()) {
            escaped_values.push_back(*escaped);
            ++escaped;
        }
        FT_REQUIRE(escaped_values == std::vector<std::int64_t>({11, 12, 13, 14, 15}));
    });

    tests.add("Range-update construction is one-shot and validation precedes callbacks", [] {
        enumeration_complete = false;
        ordered_measure_calls = 0;
        using ordered_sequence = range_update_sequence<std::int64_t, ordered_measure_algebra>;
        const auto ordered = ordered_sequence::from_range(one_shot_range{});
        FT_REQUIRE(enumeration_complete);
        FT_REQUIRE_EQUAL(ordered_measure_calls, std::size_t{8});
        FT_REQUIRE(ordered.to_vector() == inclusive_values(0, 7));

        throwing_affine_algebra::disarm();
        const auto source = throwing_sequence::from_range(inclusive_values(1, 3));
        throwing_affine_algebra::arm(policy_callback::is_identity, 1);
        const auto unchanged = source.apply_range(1, 0, add_tag(9));
        FT_REQUIRE(source.shares_root_with(unchanged));
        FT_REQUIRE_EQUAL(throwing_affine_algebra::hits, std::size_t{0});
        FT_REQUIRE_THROWS(
            std::out_of_range,
            source.apply_range(4, 0, affine_sum_algebra::identity_tag()));
        FT_REQUIRE_EQUAL(throwing_affine_algebra::hits, std::size_t{0});
        throwing_affine_algebra::disarm();
    });

    tests.add("Range-update exhaustively injected callback failures preserve every source", [] {
        throwing_affine_algebra::disarm();
        const auto base_values = inclusive_values(1, 64);
        const auto base = throwing_sequence::from_range(base_values);
        const auto source = base.apply_range(0, base.size(), add_tag(5));
        auto expected = base_values;
        for (auto& value : expected) {
            value += 5;
        }

        const auto callbacks = std::vector{
            policy_callback::measure,
            policy_callback::combine,
            policy_callback::is_identity,
            policy_callback::compose,
            policy_callback::apply_element,
            policy_callback::apply_measure,
        };
        for (const auto callback : callbacks) {
            exhaust_callback_failures(callback, source, expected);
        }

        // A proper right-half tag nested under a later whole-tree tag gives iterator descent both
        // an inherited tag and a node-local pending tag. Increment stages every callback before
        // publishing the next cursor, so compose/identity/element failures can be retried.
        const auto nested = base.apply_range(32, 32, add_tag(5))
                                .apply_range(0, base.size(), add_tag(7));
        require_iterator_callback_retry(nested, policy_callback::compose);
        require_iterator_callback_retry(nested, policy_callback::is_identity);
        require_iterator_callback_retry(nested, policy_callback::apply_element);

        throwing_affine_algebra::disarm();
        require_sequence(base_values, base);
        require_sequence(expected, source);
    });

    tests.add("Range-update checked maximum count uses a compact shared DAG", [] {
        using count_sequence = range_update_sequence<int, count_algebra>;
        const auto seed = count_sequence::from_range(std::vector{7});
        auto power = seed;
        const auto maximum = (std::numeric_limits<std::size_t>::max)();
        while (power.size() <= maximum / 2) {
            power = power.concat(power);
        }

        const auto almost_power = power.remove_at(0);
        const auto full = power.concat(almost_power);
        FT_REQUIRE_EQUAL(full.size(), maximum);
        FT_REQUIRE_EQUAL(full.measure(), maximum);
        FT_REQUIRE_EQUAL(full.at(0), 7);
        FT_REQUIRE_EQUAL(full.at(maximum - 1), 7);
        FT_REQUIRE(full.physical_node_count() < 16'384);
        const auto statistics = full.validate_structure();
        FT_REQUIRE(statistics.has_value());
        FT_REQUIRE_EQUAL(statistics->count, maximum);
        FT_REQUIRE_EQUAL(statistics->node_count, maximum);
        FT_REQUIRE(statistics->maximum_absolute_balance_factor <= 1);
        FT_REQUIRE_THROWS(std::overflow_error, full.append(8));
        FT_REQUIRE_THROWS(std::overflow_error, full.concat(seed));
        FT_REQUIRE_EQUAL(full.size(), maximum);
    });

    tests.add("Range-update independent readers observe retained snapshots concurrently", [] {
        auto values = std::vector<std::int64_t>{};
        values.reserve(256);
        for (auto value = std::int64_t{0}; value != 256; ++value) {
            values.push_back(value);
        }
        const auto original = affine_sequence::from_range(values);
        const auto changed = original.apply_range(64, 128, add_tag(1'000));
        const auto expected_original = std::accumulate(
            values.begin(),
            values.end(),
            std::int64_t{0});
        const auto expected_middle = std::accumulate(
            values.begin() + 64,
            values.begin() + 192,
            std::int64_t{0}) + 128'000;
        auto failed = std::atomic<bool>{false};
        auto readers = std::vector<std::jthread>{};
        for (auto worker = 0; worker != 4; ++worker) {
            readers.emplace_back([&] {
                try {
                    for (auto iteration = 0; iteration != 64; ++iteration) {
                        if (original.measure() != expected_original
                            || original[0] != 0
                            || changed[128] != 1'128
                            || changed.measure_range(64, 128) != expected_middle
                            || changed.to_vector().size() != 256) {
                            throw std::logic_error("concurrent range-update reader mismatch");
                        }
                    }
                } catch (...) {
                    failed.store(true, std::memory_order_relaxed);
                }
            });
        }
        readers.clear();
        FT_REQUIRE(!failed.load(std::memory_order_relaxed));
        require_sequence(values, original);
    });
}
