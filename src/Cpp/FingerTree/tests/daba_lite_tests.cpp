/// Tests for the DABA Lite sliding-window aggregate.

#include <durable7/finger_tree/finger_tree.hpp>

#include "test_support/test_runner.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <numeric>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ft = durable7::finger_tree;
using namespace durable7::finger_tree::tests;

namespace {

constexpr auto offset_identity = 11;

struct matrix final {
    static constexpr std::int64_t modulus = 1'000'003;

    std::int64_t m00 = 0;
    std::int64_t m01 = 0;
    std::int64_t m10 = 0;
    std::int64_t m11 = 0;

    [[nodiscard]] constexpr bool operator==(const matrix&) const = default;

    [[nodiscard]] static constexpr matrix identity() noexcept { return matrix{1, 0, 0, 1}; }

    [[nodiscard]] static constexpr matrix create(const std::int64_t seed) noexcept
    {
        const auto positive = seed < 0 ? -seed : seed;
        return matrix{(positive * 17 + 3) % modulus, (positive * 29 + 5) % modulus, (positive * 43 + 7) % modulus,
                      (positive * 61 + 11) % modulus};
    }

    [[nodiscard]] static constexpr matrix multiply(const matrix& left, const matrix& right) noexcept
    {
        return matrix{(left.m00 * right.m00 + left.m01 * right.m10) % modulus,
                      (left.m00 * right.m01 + left.m01 * right.m11) % modulus,
                      (left.m10 * right.m00 + left.m11 * right.m10) % modulus,
                      (left.m10 * right.m01 + left.m11 * right.m11) % modulus};
    }
};

struct matrix_monoid final {
    using measure_type = matrix;

    [[nodiscard]] static constexpr measure_type empty() noexcept { return matrix::identity(); }

    [[nodiscard]] static constexpr measure_type combine(const measure_type& left, const measure_type& right) noexcept
    {
        return matrix::multiply(left, right);
    }
};

struct sum_monoid final {
    using measure_type = std::int64_t;

    [[nodiscard]] static constexpr measure_type empty() noexcept { return 0; }

    [[nodiscard]] static constexpr measure_type combine(const measure_type left, const measure_type right) noexcept
    {
        return left + right;
    }
};

struct offset_monoid final {
    using measure_type = int;

    [[nodiscard]] static constexpr measure_type empty() noexcept { return offset_identity; }

    [[nodiscard]] static constexpr measure_type combine(const measure_type left, const measure_type right) noexcept
    {
        return left + right - offset_identity;
    }
};

struct counting_offset_monoid final {
    using measure_type = int;

    inline static int combine_count = 0;
    inline static int empty_count = 0;

    [[nodiscard]] static measure_type empty() noexcept
    {
        ++empty_count;
        return offset_identity;
    }

    [[nodiscard]] static measure_type combine(const measure_type left, const measure_type right) noexcept
    {
        ++combine_count;
        return left + right - offset_identity;
    }

    static void reset() noexcept
    {
        combine_count = 0;
        empty_count = 0;
    }
};

class callback_exception final : public std::runtime_error {
public:
    callback_exception() : std::runtime_error("injected monoid callback failure") {}
};

struct throwing_combine_monoid final {
    using measure_type = int;

    inline static int combine_count = 0;
    inline static int throw_on = 0;

    [[nodiscard]] static constexpr measure_type empty() noexcept { return offset_identity; }

    [[nodiscard]] static measure_type combine(const measure_type left, const measure_type right)
    {
        ++combine_count;
        if (combine_count == throw_on) {
            throw callback_exception{};
        }
        return left + right - offset_identity;
    }

    static void reset() noexcept
    {
        combine_count = 0;
        throw_on = 0;
    }

    static void fail_on(const int ordinal) noexcept
    {
        combine_count = 0;
        throw_on = ordinal;
    }
};

class value_copy_exception final : public std::runtime_error {
public:
    value_copy_exception()
        : std::runtime_error("injected value-copy failure")
    {
    }
};

struct throwing_move_value final {
    int value = 0;

    throwing_move_value() = default;
    throwing_move_value(const throwing_move_value&) = default;
    throwing_move_value& operator=(const throwing_move_value&) = default;
    throwing_move_value(throwing_move_value&& other) noexcept(false)
        : value(other.value)
    {
    }
    throwing_move_value& operator=(throwing_move_value&& other) noexcept(false)
    {
        value = other.value;
        return *this;
    }
};

struct throwing_copy_value final {
    inline static int copy_count = 0;
    inline static int throw_on = 0;

    int value = 0;

    throwing_copy_value() = default;
    explicit throwing_copy_value(const int value_arg) noexcept
        : value(value_arg)
    {
    }
    throwing_copy_value(const throwing_copy_value& other)
        : value(copy(other.value))
    {
    }
    throwing_copy_value& operator=(const throwing_copy_value& other)
    {
        value = copy(other.value);
        return *this;
    }
    throwing_copy_value(throwing_copy_value&& other) noexcept
        : value(other.value)
    {
    }
    throwing_copy_value& operator=(throwing_copy_value&& other) noexcept
    {
        value = other.value;
        return *this;
    }

    [[nodiscard]] bool operator==(const throwing_copy_value&) const = default;

    static void reset() noexcept
    {
        copy_count = 0;
        throw_on = 0;
    }

    static void fail_on(const int ordinal) noexcept
    {
        copy_count = 0;
        throw_on = ordinal;
    }

private:
    [[nodiscard]] static int copy(const int source)
    {
        ++copy_count;
        if (copy_count == throw_on) {
            throw value_copy_exception{};
        }
        return source;
    }
};

struct throwing_copy_monoid final {
    using measure_type = throwing_copy_value;

    [[nodiscard]] static measure_type empty() noexcept
    {
        return measure_type{offset_identity};
    }

    [[nodiscard]] static measure_type combine(const measure_type& left, const measure_type& right) noexcept
    {
        return measure_type{left.value + right.value - offset_identity};
    }
};

struct throwing_empty_monoid final {
    using measure_type = int;

    inline static int empty_count = 0;
    inline static int throw_on = 0;

    [[nodiscard]] static measure_type empty()
    {
        ++empty_count;
        if (empty_count == throw_on) {
            throw callback_exception{};
        }
        return offset_identity;
    }

    [[nodiscard]] static constexpr measure_type combine(const measure_type left, const measure_type right) noexcept
    {
        return left + right - offset_identity;
    }

    static void reset() noexcept
    {
        empty_count = 0;
        throw_on = 0;
    }

    static void fail_on(const int ordinal) noexcept
    {
        empty_count = 0;
        throw_on = ordinal;
    }
};

struct reference_aggregate final {
    std::shared_ptr<int> token;
    bool is_identity = false;
};

struct first_reference_monoid final {
    using measure_type = reference_aggregate;

    [[nodiscard]] static measure_type empty() { return reference_aggregate{nullptr, true}; }

    [[nodiscard]] static measure_type combine(const measure_type& left, const measure_type& right)
    {
        return left.is_identity ? right : left;
    }
};

static_assert(ft::daba_lite_monoid_policy<matrix_monoid, matrix>);
static_assert(std::copyable<throwing_move_value>);
static_assert(!ft::daba_lite_value<throwing_move_value>);
static_assert(ft::daba_lite_value<throwing_copy_value>);
static_assert(!std::copy_constructible<ft::daba_lite<int, offset_monoid>>);
static_assert(!std::move_constructible<ft::daba_lite<int, offset_monoid>>);

[[nodiscard]] matrix fold_matrices(const std::deque<matrix>& values)
{
    auto result = matrix::identity();
    for (const auto& value : values) {
        result = matrix::multiply(result, value);
    }
    return result;
}

[[nodiscard]] int fold_offset(const std::deque<int>& values)
{
    auto result = offset_identity;
    for (const auto value : values) {
        result = result + value - offset_identity;
    }
    return result;
}

void require_matrix_state(const ft::daba_lite<matrix, matrix_monoid>& daba, const std::deque<matrix>& model)
{
    const auto statistics = daba.validate_structure();
    FT_REQUIRE_EQUAL(statistics.count, model.size());
    FT_REQUIRE(daba.aggregate() == fold_matrices(model));
}

enum class fixup_phase : std::size_t {
    singleton,
    flip_and_shrink,
    shift,
    shrink,
    count,
};

[[nodiscard]] fixup_phase classify_next_fixup(const ft::daba_lite_statistics& statistics, const bool evicting) noexcept
{
    if ((!evicting && statistics.count == 0) || (evicting && statistics.front_length == 1)) {
        return fixup_phase::singleton;
    }
    if (statistics.left_length + statistics.right_length + statistics.accumulator_length == 0) {
        return fixup_phase::flip_and_shrink;
    }
    return statistics.left_length == 0 ? fixup_phase::shift : fixup_phase::shrink;
}

template <class Monoid>
[[nodiscard]] bool try_replay(const std::vector<bool>& history, std::unique_ptr<ft::daba_lite<int, Monoid>>& daba,
                              std::deque<int>& model)
{
    daba = std::make_unique<ft::daba_lite<int, Monoid>>();
    model.clear();
    auto next = 20;
    for (const auto inserting : history) {
        if (inserting) {
            daba->insert(next);
            model.push_back(next++);
        } else {
            if (model.empty()) {
                return false;
            }
            daba->evict();
            model.pop_front();
        }
    }
    return true;
}

[[nodiscard]] std::vector<bool> decode_history(const std::size_t mask, const std::size_t length)
{
    auto result = std::vector<bool>{};
    result.reserve(length);
    for (auto index = std::size_t{0}; index < length; ++index) {
        result.push_back((mask & (std::size_t{1} << index)) != 0);
    }
    return result;
}

template <class Monoid, class Reset, class CallbackCount>
[[nodiscard]] std::vector<bool> find_history(const bool inserting, const int minimum_callbacks, Reset&& reset,
                                             CallbackCount&& callback_count)
{
    for (auto length = std::size_t{0}; length <= 10; ++length) {
        const auto history_count = std::size_t{1} << length;
        for (auto mask = std::size_t{0}; mask < history_count; ++mask) {
            const auto history = decode_history(mask, length);
            std::forward<Reset>(reset)();
            auto daba = std::unique_ptr<ft::daba_lite<int, Monoid>>{};
            auto model = std::deque<int>{};
            if (!try_replay<Monoid>(history, daba, model) || (!inserting && model.empty())) {
                continue;
            }
            std::forward<Reset>(reset)();
            if (inserting) {
                daba->insert(101);
            } else {
                daba->evict();
            }
            if (std::forward<CallbackCount>(callback_count)() >= minimum_callbacks) {
                std::forward<Reset>(reset)();
                return history;
            }
        }
    }
    throw std::logic_error("no short DABA Lite history reaches the requested callback ordinal");
}

[[nodiscard]] std::vector<bool> find_combine_history(const bool inserting, const int ordinal)
{
    return find_history<throwing_combine_monoid>(
        inserting, ordinal, [] { throwing_combine_monoid::reset(); },
        [] { return throwing_combine_monoid::combine_count; });
}

[[nodiscard]] std::vector<bool> find_empty_history(const bool inserting, const int ordinal)
{
    return find_history<throwing_empty_monoid>(
        inserting, ordinal, [] { throwing_empty_monoid::reset(); }, [] { return throwing_empty_monoid::empty_count; });
}

[[nodiscard]] int find_maximum_empty_callbacks(const bool inserting)
{
    auto maximum = 0;
    for (auto ordinal = 1; ordinal <= 3; ++ordinal) {
        try {
            (void)find_empty_history(inserting, ordinal);
            maximum = ordinal;
        } catch (const std::logic_error&) {
            break;
        }
    }
    return maximum;
}

template <class Monoid> void apply_candidate(ft::daba_lite<int, Monoid>& daba, const bool inserting)
{
    if (inserting) {
        daba.insert(101);
    } else {
        daba.evict();
    }
}

template <class Monoid>
void retry_candidate_and_continue(ft::daba_lite<int, Monoid>& daba, std::deque<int>& model, const bool inserting)
{
    apply_candidate(daba, inserting);
    if (inserting) {
        model.push_back(101);
    } else {
        model.pop_front();
    }

    for (auto index = 0; index < 24; ++index) {
        daba.insert(200 + index);
        model.push_back(200 + index);
        if ((index & 1) == 0) {
            daba.evict();
            model.pop_front();
        }
        FT_REQUIRE_EQUAL(daba.validate_structure().count, model.size());
        FT_REQUIRE_EQUAL(daba.aggregate(), fold_offset(model));
    }
}

[[nodiscard]] bool try_replay_copy_history(
    const std::vector<bool>& history,
    std::unique_ptr<ft::daba_lite<throwing_copy_value, throwing_copy_monoid>>& daba,
    std::deque<int>& model)
{
    daba = std::make_unique<ft::daba_lite<throwing_copy_value, throwing_copy_monoid>>();
    model.clear();
    auto next = 20;
    for (const auto inserting : history) {
        if (inserting) {
            daba->insert(throwing_copy_value{next});
            model.push_back(next++);
        } else {
            if (model.empty()) {
                return false;
            }
            daba->evict();
            model.pop_front();
        }
    }
    return true;
}

void apply_copy_candidate(
    ft::daba_lite<throwing_copy_value, throwing_copy_monoid>& daba,
    const bool inserting)
{
    if (inserting) {
        daba.insert(throwing_copy_value{101});
    } else {
        daba.evict();
    }
}

[[nodiscard]] std::vector<bool> find_copy_history(const bool inserting, const int minimum_copies)
{
    for (auto length = std::size_t{0}; length <= 10; ++length) {
        const auto history_count = std::size_t{1} << length;
        for (auto mask = std::size_t{0}; mask < history_count; ++mask) {
            const auto history = decode_history(mask, length);
            throwing_copy_value::reset();
            auto daba = std::unique_ptr<ft::daba_lite<throwing_copy_value, throwing_copy_monoid>>{};
            auto model = std::deque<int>{};
            if (!try_replay_copy_history(history, daba, model) || (!inserting && model.empty())) {
                continue;
            }
            throwing_copy_value::reset();
            apply_copy_candidate(*daba, inserting);
            if (throwing_copy_value::copy_count >= minimum_copies) {
                throwing_copy_value::reset();
                return history;
            }
        }
    }
    throw std::logic_error("no short DABA Lite history reaches the requested value-copy ordinal");
}

[[nodiscard]] int find_maximum_copy_count(const bool inserting)
{
    auto maximum = 0;
    for (auto ordinal = 1; ordinal <= 16; ++ordinal) {
        try {
            (void)find_copy_history(inserting, ordinal);
            maximum = ordinal;
        } catch (const std::logic_error&) {
            break;
        }
    }
    return maximum;
}

void retry_copy_candidate_and_continue(
    ft::daba_lite<throwing_copy_value, throwing_copy_monoid>& daba,
    std::deque<int>& model,
    const bool inserting)
{
    apply_copy_candidate(daba, inserting);
    if (inserting) {
        model.push_back(101);
    } else {
        model.pop_front();
    }

    for (auto index = 0; index < 24; ++index) {
        daba.insert(throwing_copy_value{200 + index});
        model.push_back(200 + index);
        if ((index & 1) == 0) {
            daba.evict();
            model.pop_front();
        }
        FT_REQUIRE_EQUAL(daba.validate_structure().count, model.size());
        FT_REQUIRE_EQUAL(daba.aggregate().value, fold_offset(model));
    }
}

} // namespace

void add_daba_lite_tests(suite& tests)
{
    tests.add("DABA Lite exhaustive noncommutative histories preserve FIFO order", [] {
        const auto left = matrix::create(2);
        const auto right = matrix::create(7);
        FT_REQUIRE(!(matrix::multiply(left, right) == matrix::multiply(right, left)));

        constexpr auto history_length = std::size_t{10};
        for (auto mask = std::size_t{0}; mask < (std::size_t{1} << history_length); ++mask) {
            auto daba = ft::daba_lite<matrix, matrix_monoid>{};
            auto model = std::deque<matrix>{};
            for (auto step = std::size_t{0}; step < history_length; ++step) {
                if ((mask & (std::size_t{1} << step)) != 0) {
                    const auto value = matrix::create(static_cast<std::int64_t>(mask * 17 + step + 1));
                    daba.insert(value);
                    model.push_back(value);
                } else if (model.empty()) {
                    FT_REQUIRE(!daba.try_evict());
                } else {
                    FT_REQUIRE(daba.try_evict());
                    model.pop_front();
                }
                require_matrix_state(daba, model);
            }
        }
    });

    tests.add("DABA Lite randomized variable window matches naive reaggregation", [] {
        auto random = std::mt19937_64{20260715};
        auto daba = ft::daba_lite<std::int64_t, sum_monoid>{};
        auto model = std::deque<std::int64_t>{};
        for (auto iteration = 0; iteration < 100'000; ++iteration) {
            if (model.empty() || (random() & 1U) == 0) {
                const auto value = static_cast<std::int64_t>(random() % 20'001U) - 10'000;
                daba.insert(value);
                model.push_back(value);
            } else {
                FT_REQUIRE(daba.try_evict());
                model.pop_front();
            }
            const auto expected = std::accumulate(model.begin(), model.end(), std::int64_t{0});
            FT_REQUIRE_EQUAL(daba.aggregate(), expected);
            FT_REQUIRE_EQUAL(daba.size(), model.size());
        }
    });

    tests.add("DABA Lite chunk boundaries and churn retain bounded storage", [] {
        for (const auto size : {63U, 64U, 65U, 127U, 128U, 129U}) {
            auto daba = ft::daba_lite<matrix, matrix_monoid>{};
            auto model = std::deque<matrix>{};
            for (auto index = std::size_t{0}; index < size; ++index) {
                const auto value = matrix::create(static_cast<std::int64_t>(index + 1));
                daba.insert(value);
                model.push_back(value);
            }

            const auto initial = daba.validate_structure();
            FT_REQUIRE_EQUAL(initial.block_count, static_cast<std::size_t>(size / 64U + 1U));
            FT_REQUIRE(initial.slack_slot_count >= 1 && initial.slack_slot_count <= 127);
            require_matrix_state(daba, model);

            for (auto index = std::size_t{0}; index < 512; ++index) {
                daba.evict();
                model.pop_front();
                const auto value = matrix::create(static_cast<std::int64_t>(10'000 + size * 1'000U + index));
                daba.insert(value);
                model.push_back(value);
                const auto statistics = daba.validate_structure();
                FT_REQUIRE(statistics.slack_slot_count >= 1 && statistics.slack_slot_count <= 127);
                FT_REQUIRE(statistics.block_count >= 1 && statistics.block_count <= size / 64U + 2U);
                if ((index & 15U) == 0) {
                    FT_REQUIRE(daba.aggregate() == fold_matrices(model));
                }
            }

            while (!model.empty()) {
                daba.evict();
                model.pop_front();
                require_matrix_state(daba, model);
            }
            const auto empty = daba.validate_structure();
            FT_REQUIRE_EQUAL(empty.block_count, std::size_t{1});
            FT_REQUIRE_EQUAL(empty.allocated_slot_capacity, std::size_t{64});
        }
    });

    tests.add("DABA Lite covers every fixup phase and callback ceiling", [] {
        counting_offset_monoid::reset();
        auto daba = ft::daba_lite<int, counting_offset_monoid>{};
        auto model = std::deque<int>{};
        auto phases = std::array<bool, static_cast<std::size_t>(fixup_phase::count)>{};
        auto maximum_insert = 0;
        auto maximum_evict = 0;
        auto maximum_query = 0;

        for (auto cycle = 0; cycle < 4; ++cycle) {
            for (auto index = 0; index < 130; ++index) {
                const auto phase = classify_next_fixup(daba.validate_structure(), false);
                phases[static_cast<std::size_t>(phase)] = true;
                counting_offset_monoid::reset();
                const auto value = cycle * 1'000 + index + 20;
                daba.insert(value);
                model.push_back(value);
                FT_REQUIRE(counting_offset_monoid::combine_count >= 1);
                FT_REQUIRE(counting_offset_monoid::combine_count <= 3);
                maximum_insert = (std::max)(maximum_insert, counting_offset_monoid::combine_count);

                counting_offset_monoid::reset();
                FT_REQUIRE_EQUAL(daba.aggregate(), fold_offset(model));
                FT_REQUIRE(counting_offset_monoid::combine_count <= 1);
                maximum_query = (std::max)(maximum_query, counting_offset_monoid::combine_count);
            }

            while (!model.empty()) {
                const auto phase = classify_next_fixup(daba.validate_structure(), true);
                phases[static_cast<std::size_t>(phase)] = true;
                counting_offset_monoid::reset();
                daba.evict();
                model.pop_front();
                FT_REQUIRE(counting_offset_monoid::combine_count <= 2);
                maximum_evict = (std::max)(maximum_evict, counting_offset_monoid::combine_count);

                counting_offset_monoid::reset();
                FT_REQUIRE_EQUAL(daba.aggregate(), fold_offset(model));
                FT_REQUIRE(counting_offset_monoid::combine_count <= 1);
                maximum_query = (std::max)(maximum_query, counting_offset_monoid::combine_count);
            }
        }

        FT_REQUIRE(std::ranges::all_of(phases, [](const bool covered) { return covered; }));
        FT_REQUIRE_EQUAL(maximum_insert, 3);
        FT_REQUIRE_EQUAL(maximum_evict, 2);
        FT_REQUIRE_EQUAL(maximum_query, 1);
    });

    tests.add("DABA Lite combine failures preserve exact state and usability", [] {
        for (const auto inserting : {true, false}) {
            const auto maximum = inserting ? 3 : 2;
            for (auto ordinal = 1; ordinal <= maximum; ++ordinal) {
                const auto history = find_combine_history(inserting, ordinal);
                throwing_combine_monoid::reset();
                auto daba = std::unique_ptr<ft::daba_lite<int, throwing_combine_monoid>>{};
                auto model = std::deque<int>{};
                FT_REQUIRE(try_replay<throwing_combine_monoid>(history, daba, model));
                const auto before_statistics = daba->validate_structure();
                const auto before_aggregate = fold_offset(model);

                throwing_combine_monoid::fail_on(ordinal);
                FT_REQUIRE_THROWS(callback_exception, apply_candidate(*daba, inserting));
                FT_REQUIRE_EQUAL(throwing_combine_monoid::combine_count, ordinal);

                throwing_combine_monoid::reset();
                FT_REQUIRE(daba->validate_structure() == before_statistics);
                FT_REQUIRE_EQUAL(daba->aggregate(), before_aggregate);
                retry_candidate_and_continue(*daba, model, inserting);
            }
        }
    });

    tests.add("DABA Lite boundary failure unlinks the provisional successor", [] {
        throwing_combine_monoid::reset();
        auto daba = ft::daba_lite<int, throwing_combine_monoid>{};
        auto model = std::deque<int>{};
        for (auto index = 0; index < 63; ++index) {
            daba.insert(20 + index);
            model.push_back(20 + index);
        }

        const auto before_statistics = daba.validate_structure();
        const auto before_aggregate = fold_offset(model);
        FT_REQUIRE_EQUAL(before_statistics.block_count, std::size_t{1});
        FT_REQUIRE_EQUAL(before_statistics.slack_slot_count, std::size_t{1});

        throwing_combine_monoid::fail_on(2);
        FT_REQUIRE_THROWS(callback_exception, daba.insert(101));
        FT_REQUIRE_EQUAL(throwing_combine_monoid::combine_count, 2);

        throwing_combine_monoid::reset();
        FT_REQUIRE(daba.validate_structure() == before_statistics);
        FT_REQUIRE_EQUAL(daba.aggregate(), before_aggregate);
        daba.insert(101);
        model.push_back(101);
        const auto after_retry = daba.validate_structure();
        FT_REQUIRE_EQUAL(after_retry.count, std::size_t{64});
        FT_REQUIRE_EQUAL(after_retry.block_count, std::size_t{2});
        FT_REQUIRE_EQUAL(after_retry.allocated_slot_capacity, std::size_t{128});
        FT_REQUIRE_EQUAL(daba.aggregate(), fold_offset(model));
    });

    tests.add("DABA Lite identity failures preserve exact state and usability", [] {
        for (const auto inserting : {true, false}) {
            const auto maximum = find_maximum_empty_callbacks(inserting);
            FT_REQUIRE(maximum >= 1 && maximum <= 2);
            for (auto ordinal = 1; ordinal <= maximum; ++ordinal) {
                const auto history = find_empty_history(inserting, ordinal);
                throwing_empty_monoid::reset();
                auto daba = std::unique_ptr<ft::daba_lite<int, throwing_empty_monoid>>{};
                auto model = std::deque<int>{};
                FT_REQUIRE(try_replay<throwing_empty_monoid>(history, daba, model));
                const auto before_statistics = daba->validate_structure();
                const auto before_aggregate = fold_offset(model);

                throwing_empty_monoid::fail_on(ordinal);
                FT_REQUIRE_THROWS(callback_exception, apply_candidate(*daba, inserting));
                FT_REQUIRE_EQUAL(throwing_empty_monoid::empty_count, ordinal);

                throwing_empty_monoid::reset();
                FT_REQUIRE(daba->validate_structure() == before_statistics);
                FT_REQUIRE_EQUAL(daba->aggregate(), before_aggregate);
                retry_candidate_and_continue(*daba, model, inserting);
            }
        }

        throwing_empty_monoid::reset();
        auto clear_target = ft::daba_lite<int, throwing_empty_monoid>{};
        clear_target.insert(23);
        const auto clear_statistics = clear_target.validate_structure();
        const auto clear_aggregate = clear_target.aggregate();
        throwing_empty_monoid::fail_on(1);
        FT_REQUIRE_THROWS(callback_exception, clear_target.clear());
        throwing_empty_monoid::reset();
        FT_REQUIRE(clear_target.validate_structure() == clear_statistics);
        FT_REQUIRE_EQUAL(clear_target.aggregate(), clear_aggregate);
        clear_target.clear();
        FT_REQUIRE(clear_target.empty());
    });

    tests.add("DABA Lite stages throwing copies before a nonthrowing commit", [] {
        for (const auto inserting : {true, false}) {
            const auto maximum = find_maximum_copy_count(inserting);
            FT_REQUIRE(maximum >= 2);
            for (auto ordinal = 1; ordinal <= maximum; ++ordinal) {
                const auto history = find_copy_history(inserting, ordinal);
                throwing_copy_value::reset();
                auto daba = std::unique_ptr<ft::daba_lite<throwing_copy_value, throwing_copy_monoid>>{};
                auto model = std::deque<int>{};
                FT_REQUIRE(try_replay_copy_history(history, daba, model));
                const auto before_statistics = daba->validate_structure();
                const auto before_aggregate = fold_offset(model);

                throwing_copy_value::fail_on(ordinal);
                FT_REQUIRE_THROWS(value_copy_exception, apply_copy_candidate(*daba, inserting));
                FT_REQUIRE_EQUAL(throwing_copy_value::copy_count, ordinal);

                throwing_copy_value::reset();
                FT_REQUIRE(daba->validate_structure() == before_statistics);
                FT_REQUIRE_EQUAL(daba->aggregate().value, before_aggregate);
                retry_copy_candidate_and_continue(*daba, model, inserting);
            }
        }

        throwing_copy_value::reset();
        auto boundary = ft::daba_lite<throwing_copy_value, throwing_copy_monoid>{};
        auto boundary_model = std::deque<int>{};
        for (auto index = 0; index < 63; ++index) {
            boundary.insert(throwing_copy_value{20 + index});
            boundary_model.push_back(20 + index);
        }
        const auto boundary_statistics = boundary.validate_structure();
        throwing_copy_value::fail_on(1);
        FT_REQUIRE_THROWS(value_copy_exception, boundary.insert(throwing_copy_value{101}));
        throwing_copy_value::reset();
        FT_REQUIRE(boundary.validate_structure() == boundary_statistics);
        FT_REQUIRE_EQUAL(boundary.aggregate().value, fold_offset(boundary_model));
        boundary.insert(throwing_copy_value{101});
        FT_REQUIRE_EQUAL(boundary.validate_structure().block_count, std::size_t{2});

        for (auto ordinal = 1; ordinal <= 2; ++ordinal) {
            throwing_copy_value::reset();
            auto clear_target = ft::daba_lite<throwing_copy_value, throwing_copy_monoid>{};
            clear_target.insert(throwing_copy_value{23});
            clear_target.insert(throwing_copy_value{29});
            const auto clear_statistics = clear_target.validate_structure();
            const auto clear_aggregate = clear_target.aggregate().value;

            throwing_copy_value::fail_on(ordinal);
            FT_REQUIRE_THROWS(value_copy_exception, clear_target.clear());
            FT_REQUIRE_EQUAL(throwing_copy_value::copy_count, ordinal);
            throwing_copy_value::reset();
            FT_REQUIRE(clear_target.validate_structure() == clear_statistics);
            FT_REQUIRE_EQUAL(clear_target.aggregate().value, clear_aggregate);
        }

        throwing_copy_value::reset();
        auto successful_clear = ft::daba_lite<throwing_copy_value, throwing_copy_monoid>{};
        successful_clear.insert(throwing_copy_value{37});
        throwing_copy_value::reset();
        successful_clear.clear();
        FT_REQUIRE_EQUAL(throwing_copy_value::copy_count, 2);
        FT_REQUIRE(successful_clear.empty());
    });

    tests.add("DABA Lite eviction and clear promptly release owned references", [] {
        auto daba = ft::daba_lite<reference_aggregate, first_reference_monoid>{};
        auto victim = std::make_shared<int>(17);
        const auto weak_victim = std::weak_ptr<int>{victim};
        daba.insert(reference_aggregate{victim, false});
        victim.reset();
        daba.insert(reference_aggregate{std::make_shared<int>(23), false});
        FT_REQUIRE(!weak_victim.expired());
        daba.evict();
        FT_REQUIRE(weak_victim.expired());

        auto first_block_victim = std::make_shared<int>(29);
        const auto weak_first_block_victim = std::weak_ptr<int>{first_block_victim};
        daba.clear();
        daba.insert(reference_aggregate{first_block_victim, false});
        first_block_victim.reset();
        for (auto index = 0; index < 128; ++index) {
            daba.insert(reference_aggregate{std::make_shared<int>(100 + index), false});
        }
        FT_REQUIRE(!weak_first_block_victim.expired());
        for (auto index = 0; index < 64; ++index) {
            daba.evict();
        }
        FT_REQUIRE_EQUAL(daba.validate_structure().block_count, std::size_t{2});
        FT_REQUIRE(weak_first_block_victim.expired());

        auto clear_daba = ft::daba_lite<reference_aggregate, first_reference_monoid>{};
        auto clear_victim = std::make_shared<int>(31);
        const auto weak_clear_victim = std::weak_ptr<int>{clear_victim};
        clear_daba.insert(reference_aggregate{clear_victim, false});
        clear_victim.reset();
        FT_REQUIRE(!weak_clear_victim.expired());
        clear_daba.clear();
        FT_REQUIRE(weak_clear_victim.expired());
        const auto statistics = clear_daba.validate_structure();
        FT_REQUIRE_EQUAL(statistics.count, std::size_t{0});
        FT_REQUIRE_EQUAL(statistics.block_count, std::size_t{1});
    });

    tests.add("DABA Lite clear is callback-bounded and reusable", [] {
        counting_offset_monoid::reset();
        auto daba = ft::daba_lite<int, counting_offset_monoid>{};
        for (auto index = 0; index < 257; ++index) {
            daba.insert(index + 20);
        }

        counting_offset_monoid::reset();
        FT_REQUIRE_EQUAL(daba.validate_structure().count, std::size_t{257});
        FT_REQUIRE_EQUAL(counting_offset_monoid::combine_count, 0);
        FT_REQUIRE_EQUAL(counting_offset_monoid::empty_count, 0);

        daba.clear();
        FT_REQUIRE_EQUAL(counting_offset_monoid::combine_count, 0);
        FT_REQUIRE_EQUAL(counting_offset_monoid::empty_count, 1);
        const auto statistics = daba.validate_structure();
        FT_REQUIRE_EQUAL(statistics.count, std::size_t{0});
        FT_REQUIRE_EQUAL(statistics.block_count, std::size_t{1});
        FT_REQUIRE_EQUAL(statistics.allocated_slot_capacity, std::size_t{64});
        FT_REQUIRE_EQUAL(statistics.slack_slot_count, std::size_t{64});

        counting_offset_monoid::reset();
        daba.clear();
        FT_REQUIRE_EQUAL(counting_offset_monoid::combine_count, 0);
        FT_REQUIRE_EQUAL(counting_offset_monoid::empty_count, 0);

        daba.insert(20);
        daba.insert(30);
        FT_REQUIRE_EQUAL(daba.aggregate(), 39);
        FT_REQUIRE_EQUAL(daba.validate_structure().count, std::size_t{2});
    });
}
