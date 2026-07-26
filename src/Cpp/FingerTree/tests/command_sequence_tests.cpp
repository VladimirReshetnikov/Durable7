/// Tests that replay generated command sequences against both the structure and a simple model.
///
/// Any divergence between the two is a bug in the structure. Sequences are generated from a
/// recorded seed, so a failure can be replayed exactly.

#include <durable7/finger_tree/finger_tree.hpp>

#include "test_support/command_model.hpp"
#include "test_support/test_runner.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <numeric>
#include <optional>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ft = durable7::finger_tree;
using namespace durable7::finger_tree::tests;

namespace {

struct signed_sum_measure final {
    using measure_type = std::int64_t;

    [[nodiscard]] static constexpr measure_type empty() noexcept
    {
        return 0;
    }

    [[nodiscard]] static constexpr measure_type measure(const int value) noexcept
    {
        return value;
    }

    [[nodiscard]] static constexpr measure_type combine(const measure_type left, const measure_type right) noexcept
    {
        return left + right;
    }
};

using measured_tree = ft::finger_tree<int, ft::size_measure<int>>;
using measured_sequence = ft::measured_rope<int, signed_sum_measure>;

enum class sequence_command_kind {
    push_front,
    push_back,
    pop_front,
    pop_back,
    insert,
    erase,
    replace,
    split_rejoin,
    reverse,
    retain,
    restore,
};

struct sequence_command final {
    sequence_command_kind kind = sequence_command_kind::push_back;
    std::size_t index_operand = 0;
    int value = 0;
};

std::ostream& operator<<(std::ostream& output, const sequence_command& command)
{
    auto name = std::string_view{};
    switch (command.kind) {
    case sequence_command_kind::push_front:
        name = "push_front";
        break;
    case sequence_command_kind::push_back:
        name = "push_back";
        break;
    case sequence_command_kind::pop_front:
        name = "pop_front";
        break;
    case sequence_command_kind::pop_back:
        name = "pop_back";
        break;
    case sequence_command_kind::insert:
        name = "insert";
        break;
    case sequence_command_kind::erase:
        name = "erase";
        break;
    case sequence_command_kind::replace:
        name = "replace";
        break;
    case sequence_command_kind::split_rejoin:
        name = "split_rejoin";
        break;
    case sequence_command_kind::reverse:
        name = "reverse";
        break;
    case sequence_command_kind::retain:
        name = "retain";
        break;
    case sequence_command_kind::restore:
        name = "restore";
        break;
    }

    return output << name << "(index=" << command.index_operand << ", value=" << command.value << ')';
}

struct sequence_snapshot {
    std::vector<int> model;
    ft::persistent_deque<int> deque;
    measured_tree tree;
    ft::reversible_deque<int> reversible;
    ft::rope<int> rope;
    measured_sequence measured_rope;
};

struct sequence_state final : sequence_snapshot {
    std::vector<sequence_snapshot> retained;
};

[[nodiscard]] std::size_t insert_index(const std::size_t operand, const std::size_t size) noexcept
{
    return operand % (size + 1);
}

[[nodiscard]] std::size_t existing_index(const std::size_t operand, const std::size_t size) noexcept
{
    return size == 0 ? 0 : operand % size;
}

[[nodiscard]] measured_tree tree_insert(const measured_tree& tree, const std::size_t index, const int value)
{
    const auto split = tree.split([index](const std::size_t count) {
        return count > index;
    });
    return split.left.append(value).concat(split.right);
}

[[nodiscard]] measured_tree tree_remove(const measured_tree& tree, const std::size_t index)
{
    const auto split = tree.try_split_find([index](const std::size_t count) {
        return count > index;
    });
    if (!split.has_value()) {
        throw std::logic_error("generated tree removal did not locate its in-range index");
    }

    return split->left.concat(split->right);
}

[[nodiscard]] measured_tree tree_replace(const measured_tree& tree, const std::size_t index, const int value)
{
    const auto split = tree.try_split_find([index](const std::size_t count) {
        return count > index;
    });
    if (!split.has_value()) {
        throw std::logic_error("generated tree replacement did not locate its in-range index");
    }

    return split->left.append(value).concat(split->right);
}

void replace_with_snapshot(sequence_state& state, const sequence_snapshot& snapshot)
{
    state.model = snapshot.model;
    state.deque = snapshot.deque;
    state.tree = snapshot.tree;
    state.reversible = snapshot.reversible;
    state.rope = snapshot.rope;
    state.measured_rope = snapshot.measured_rope;
}

[[nodiscard]] sequence_snapshot snapshot_of(const sequence_state& state)
{
    return sequence_snapshot{
        state.model,
        state.deque,
        state.tree,
        state.reversible,
        state.rope,
        state.measured_rope};
}

void apply(sequence_state& state, const sequence_command& command)
{
    constexpr auto maximum_size = std::size_t{128};
    switch (command.kind) {
    case sequence_command_kind::push_front:
        if (state.model.size() == maximum_size) {
            return;
        }
        state.model.insert(state.model.begin(), command.value);
        state.deque = state.deque.push_front(command.value);
        state.tree = state.tree.prepend(command.value);
        state.reversible = state.reversible.push_front(command.value);
        state.rope = state.rope.push_front(command.value);
        state.measured_rope = state.measured_rope.push_front(command.value);
        break;

    case sequence_command_kind::push_back:
        if (state.model.size() == maximum_size) {
            return;
        }
        state.model.push_back(command.value);
        state.deque = state.deque.push_back(command.value);
        state.tree = state.tree.append(command.value);
        state.reversible = state.reversible.push_back(command.value);
        state.rope = state.rope.push_back(command.value);
        state.measured_rope = state.measured_rope.push_back(command.value);
        break;

    case sequence_command_kind::pop_front:
        if (state.model.empty()) {
            return;
        }
        state.model.erase(state.model.begin());
        state.deque = state.deque.remove_first();
        state.tree = tree_remove(state.tree, 0);
        state.reversible = state.reversible.remove_first();
        state.rope = state.rope.remove_first();
        state.measured_rope = state.measured_rope.remove_first();
        break;

    case sequence_command_kind::pop_back:
        if (state.model.empty()) {
            return;
        }
        state.model.pop_back();
        state.deque = state.deque.remove_last();
        state.tree = tree_remove(state.tree, state.tree.measure() - 1);
        state.reversible = state.reversible.remove_last();
        state.rope = state.rope.remove_last();
        state.measured_rope = state.measured_rope.remove_last();
        break;

    case sequence_command_kind::insert: {
        if (state.model.size() == maximum_size) {
            return;
        }
        const auto index = insert_index(command.index_operand, state.model.size());
        state.model.insert(state.model.begin() + static_cast<std::ptrdiff_t>(index), command.value);
        state.deque = state.deque.insert_at(index, command.value);
        state.tree = tree_insert(state.tree, index, command.value);
        state.reversible = state.reversible.insert_at(index, command.value);
        state.rope = state.rope.insert_at(index, command.value);
        state.measured_rope = state.measured_rope.insert_at(index, command.value);
        break;
    }

    case sequence_command_kind::erase: {
        if (state.model.empty()) {
            return;
        }
        const auto index = existing_index(command.index_operand, state.model.size());
        state.model.erase(state.model.begin() + static_cast<std::ptrdiff_t>(index));
        state.deque = state.deque.remove_at(index);
        state.tree = tree_remove(state.tree, index);
        state.reversible = state.reversible.remove_at(index);
        state.rope = state.rope.remove_at(index);
        state.measured_rope = state.measured_rope.remove_at(index);
        break;
    }

    case sequence_command_kind::replace: {
        if (state.model.empty()) {
            return;
        }
        const auto index = existing_index(command.index_operand, state.model.size());
        state.model[index] = command.value;
        state.deque = state.deque.set_item(index, command.value);
        state.tree = tree_replace(state.tree, index, command.value);
        state.reversible = state.reversible.set_item(index, command.value);
        state.rope = state.rope.set_item(index, command.value);
        state.measured_rope = state.measured_rope.set_item(index, command.value);
        break;
    }

    case sequence_command_kind::split_rejoin: {
        const auto index = insert_index(command.index_operand, state.model.size());
        const auto deque_split = state.deque.split_at(index);
        state.deque = deque_split.left.concat(deque_split.right);
        const auto tree_split = state.tree.split([index](const std::size_t count) {
            return count > index;
        });
        state.tree = tree_split.left.concat(tree_split.right);
        const auto reversible_split = state.reversible.split_at(index);
        state.reversible = reversible_split.left.concat(reversible_split.right);
        const auto rope_split = state.rope.split_at(index);
        state.rope = rope_split.left.concat(rope_split.right);
        const auto measured_split = state.measured_rope.split_at(index);
        state.measured_rope = measured_split.left.concat(measured_split.right);
        break;
    }

    case sequence_command_kind::reverse:
        std::ranges::reverse(state.model);
        state.deque = ft::persistent_deque<int>::from_range(state.model);
        state.tree = measured_tree::from_range(state.model);
        state.reversible = state.reversible.reverse();
        state.rope = ft::rope<int>::from_range(state.model);
        state.measured_rope = measured_sequence::from_range(state.model);
        break;

    case sequence_command_kind::retain:
        state.retained.push_back(snapshot_of(state));
        if (state.retained.size() > 12) {
            state.retained.erase(state.retained.begin());
        }
        break;

    case sequence_command_kind::restore:
        if (!state.retained.empty()) {
            replace_with_snapshot(
                state,
                state.retained[command.index_operand % state.retained.size()]);
        }
        break;
    }
}

[[nodiscard]] std::optional<std::string> validate_snapshot(
    const sequence_snapshot& state,
    const std::string_view label)
{
    try {
        state.deque.validate_invariants();
        state.reversible.validate_invariants();
        state.rope.validate_invariants();
        state.measured_rope.validate_invariants();
    } catch (const std::exception& error) {
        return std::string{label} + " invariant validation threw: " + error.what();
    }

    const auto expected_sum = std::accumulate(
        state.model.begin(),
        state.model.end(),
        std::int64_t{0});
    if (state.deque.to_vector() != state.model) {
        return std::string{label} + " persistent_deque diverged from the vector model";
    }
    if (state.tree.to_vector() != state.model || state.tree.measure() != state.model.size()) {
        return std::string{label} + " measured finger tree diverged from the vector model";
    }
    if (state.reversible.to_vector() != state.model) {
        return std::string{label} + " reversible_deque diverged from the vector model";
    }
    if (state.rope.to_vector() != state.model) {
        return std::string{label} + " rope diverged from the vector model";
    }
    if (state.measured_rope.to_vector() != state.model || state.measured_rope.measure() != expected_sum) {
        return std::string{label} + " measured_rope diverged from the vector/sum model";
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> replay_sequence_model(
    const command_sequence<sequence_command>& program)
{
    auto state = sequence_state{};
    for (auto index = std::size_t{0}; index < program.size(); ++index) {
        try {
            apply(state, program[index]);
        } catch (const std::exception& error) {
            auto message = std::ostringstream{};
            message << "command " << index << " (" << program[index] << ") threw: " << error.what();
            return message.str();
        }

        if (auto failure = validate_snapshot(state, "current state"); failure.has_value()) {
            auto message = std::ostringstream{};
            message << "after command " << index << " (" << program[index] << "): " << *failure;
            return message.str();
        }

        for (auto retained_index = std::size_t{0}; retained_index < state.retained.size(); ++retained_index) {
            if (auto failure = validate_snapshot(state.retained[retained_index], "retained state");
                failure.has_value()) {
                auto message = std::ostringstream{};
                message << "after command " << index << ", retained snapshot " << retained_index << ": "
                        << *failure;
                return message.str();
            }
        }
    }

    return std::nullopt;
}

[[nodiscard]] command_sequence<sequence_command> generate_sequence_program(
    const std::uint64_t seed,
    const std::size_t command_count)
{
    auto rng = deterministic_rng{seed};
    auto result = command_sequence<sequence_command>{};
    for (auto index = std::size_t{0}; index < command_count; ++index) {
        result.push(sequence_command{
            static_cast<sequence_command_kind>(rng.next_index(11)),
            static_cast<std::size_t>(rng.next()),
            rng.next_int(-10'000, 10'000)});
    }
    return result;
}

enum class set_command_kind {
    add,
    remove,
    retain,
    restore,
};

struct set_command final {
    set_command_kind kind = set_command_kind::add;
    int value = 0;
    std::size_t snapshot_operand = 0;
};

std::ostream& operator<<(std::ostream& output, const set_command& command)
{
    auto name = std::string_view{};
    switch (command.kind) {
    case set_command_kind::add:
        name = "add";
        break;
    case set_command_kind::remove:
        name = "remove";
        break;
    case set_command_kind::retain:
        name = "retain";
        break;
    case set_command_kind::restore:
        name = "restore";
        break;
    }
    return output << name << "(value=" << command.value << ", snapshot=" << command.snapshot_operand << ')';
}

struct set_snapshot final {
    ft::sorted_set<int> actual;
    std::set<int> expected;
};

[[nodiscard]] std::optional<std::string> validate_set_snapshot(const set_snapshot& state)
{
    const auto expected = std::vector<int>{state.expected.begin(), state.expected.end()};
    if (state.actual.to_vector() != expected || state.actual.size() != expected.size()) {
        return "sorted_set diverged from std::set";
    }

    for (auto value = -35; value <= 35; ++value) {
        if (state.actual.contains(value) != state.expected.contains(value)) {
            return "sorted_set membership diverged from std::set";
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> replay_set_model(const command_sequence<set_command>& program)
{
    auto state = set_snapshot{};
    auto retained = std::vector<set_snapshot>{};
    for (auto index = std::size_t{0}; index < program.size(); ++index) {
        const auto& command = program[index];
        switch (command.kind) {
        case set_command_kind::add:
            state.actual = state.actual.add(command.value);
            state.expected.insert(command.value);
            break;
        case set_command_kind::remove:
            state.actual = state.actual.remove(command.value);
            state.expected.erase(command.value);
            break;
        case set_command_kind::retain:
            retained.push_back(state);
            if (retained.size() > 12) {
                retained.erase(retained.begin());
            }
            break;
        case set_command_kind::restore:
            if (!retained.empty()) {
                state = retained[command.snapshot_operand % retained.size()];
            }
            break;
        }

        if (auto failure = validate_set_snapshot(state); failure.has_value()) {
            auto message = std::ostringstream{};
            message << "after command " << index << " (" << command << "): " << *failure;
            return message.str();
        }
        for (const auto& snapshot : retained) {
            if (auto failure = validate_set_snapshot(snapshot); failure.has_value()) {
                return "retained sorted_set snapshot failed: " + *failure;
            }
        }
    }

    return std::nullopt;
}

[[nodiscard]] command_sequence<set_command> generate_set_program(
    const std::uint64_t seed,
    const std::size_t command_count)
{
    auto rng = deterministic_rng{seed};
    auto result = command_sequence<set_command>{};
    for (auto index = std::size_t{0}; index < command_count; ++index) {
        result.push(set_command{
            static_cast<set_command_kind>(rng.next_index(4)),
            rng.next_int(-32, 32),
            static_cast<std::size_t>(rng.next())});
    }
    return result;
}

template <class Operation, class Replay>
void require_program_passes(
    const command_sequence<Operation>& program,
    const std::uint64_t seed,
    Replay replay)
{
    const auto original_failure = replay(program);
    if (!original_failure.has_value()) {
        return;
    }

    const auto minimal = shrink_failing_sequence(program, [&](const command_sequence<Operation>& candidate) {
        return replay(candidate).has_value();
    });
    const auto minimal_failure = replay(minimal);

    auto message = std::ostringstream{};
    message << "stateful command replay failed with seed " << format_replay_seed(seed) << ": "
            << *original_failure << "\noriginal command count: " << program.size()
            << "; deletion-minimal count: " << minimal.size() << "\nminimal replay:\n"
            << minimal.describe();
    if (minimal_failure.has_value()) {
        message << "minimal failure: " << *minimal_failure;
    }
    throw test_failure(message.str());
}

struct arithmetic_command final {
    bool multiply = false;
    int operand = 0;
};

std::ostream& operator<<(std::ostream& output, const arithmetic_command& command)
{
    return output << (command.multiply ? "multiply" : "add") << '(' << command.operand << ')';
}

[[nodiscard]] bool reaches_thirteen(const command_sequence<arithmetic_command>& program)
{
    auto state = 0;
    for (const auto& command : program.operations()) {
        state = command.multiply ? state * command.operand : state + command.operand;
    }
    return state == 13;
}

[[nodiscard]] std::vector<int> iota_values(const std::size_t size)
{
    auto values = std::vector<int>(size);
    std::iota(values.begin(), values.end(), 0);
    return values;
}

void add_command_sequence_tests_impl(suite& tests)
{
    tests.add("command sequence shrinker produces a deletion-minimal replay", [] {
        auto program = command_sequence<arithmetic_command>{};
        program.push({false, 0});
        program.push({false, 2});
        program.push({false, 3});
        program.push({true, 2});
        program.push({false, 3});
        program.push({false, 0});

        const auto minimal = shrink_failing_sequence(program, reaches_thirteen);
        FT_REQUIRE(reaches_thirteen(minimal));
        FT_REQUIRE_EQUAL(minimal.size(), static_cast<std::size_t>(4));
        FT_REQUIRE_EQUAL(minimal.describe(), std::string{"0: add(2)\n1: add(3)\n2: multiply(2)\n3: add(3)\n"});
        for (auto index = std::size_t{0}; index < minimal.size(); ++index) {
            FT_REQUIRE(!reaches_thirteen(minimal.without_range(index, 1)));
        }

        auto observed_integrated_diagnostic = false;
        try {
            require_program_passes(
                program,
                0x5eed,
                [](const command_sequence<arithmetic_command>& candidate) -> std::optional<std::string> {
                    return reaches_thirteen(candidate)
                        ? std::optional<std::string>{"injected arithmetic-model failure"}
                        : std::nullopt;
                });
        } catch (const test_failure& failure) {
            const auto diagnostic = std::string_view{failure.what()};
            FT_REQUIRE(diagnostic.find("deletion-minimal count: 4") != std::string_view::npos);
            FT_REQUIRE(diagnostic.find("minimal replay:\n0: add(2)") != std::string_view::npos);
            observed_integrated_diagnostic = true;
        }
        FT_REQUIRE(observed_integrated_diagnostic);
    });

    tests.add("test runner reports captured replay seeds on randomized failure", [] {
        auto nested = suite{};
        nested.set_group("seed-diagnostic");
        nested.add("injected randomized failure", [] {
            auto rng = deterministic_rng{0x5eedc0de};
            (void)rng.next();
            throw test_failure("injected failure used only to verify runner diagnostics");
        });

        auto errors = std::ostringstream{};
        auto* const previous_errors = std::cerr.rdbuf(errors.rdbuf());
        const char* arguments[] = {"nested-runner"};
        const auto exit_code = nested.run(1, arguments);
        std::cerr.rdbuf(previous_errors);

        FT_REQUIRE_EQUAL(exit_code, 1);
        FT_REQUIRE(errors.str().find("[replay] seed:") != std::string::npos);
        FT_REQUIRE(errors.str().find("rerun with --seed <seed>") != std::string::npos);
    });

    tests.add("stateful sequence facades replay five seeds with shrinking diagnostics", [] {
        const auto seeds = select_replay_seeds(
            {0x0123456789abcdefULL,
             0x243f6a8885a308d3ULL,
             0x9e3779b97f4a7c15ULL,
             0xb7e151628aed2a6bULL,
             0xd1b54a32d192ed03ULL});
        for (const auto seed : seeds) {
            const auto program = generate_sequence_program(seed, 320);
            require_program_passes(program, seed, replay_sequence_model);
        }
    });

    tests.add("stateful sorted set replay uses retained versions and five seeds", [] {
        const auto seeds = select_replay_seeds(
            {0x13198a2e03707344ULL,
             0x3c6ef372fe94f82bULL,
             0x94d049bb133111ebULL,
             0xa4093822299f31d0ULL,
             0xdeadbeefcafef00dULL});
        for (const auto seed : seeds) {
            const auto program = generate_set_program(seed, 320);
            require_program_passes(program, seed, replay_set_model);
        }
    });

    tests.add("persistent sequence families exhaustively validate sizes zero through twenty four", [] {
        for (auto size = std::size_t{0}; size <= 24; ++size) {
            const auto values = iota_values(size);
            const auto deque = ft::persistent_deque<int>::from_range(values);
            const auto tree = measured_tree::from_range(values);
            const auto reversible = ft::reversible_deque<int>::from_range(values);
            const auto reversed_reversible = reversible.reverse();
            auto reversed_values = values;
            std::ranges::reverse(reversed_values);
            const auto rope = ft::rope<int>::from_range(values);
            const auto measured_rope = measured_sequence::from_range(values);

            const auto snapshot = sequence_snapshot{values, deque, tree, reversible, rope, measured_rope};
            FT_REQUIRE(!validate_snapshot(snapshot, "small-N state").has_value());

            for (auto index = std::size_t{0}; index <= size; ++index) {
                const auto deque_split = deque.split_at(index);
                FT_REQUIRE(deque_split.left.concat(deque_split.right).to_vector() == values);
                deque_split.left.validate_invariants();
                deque_split.right.validate_invariants();

                const auto tree_split = tree.split([index](const std::size_t count) {
                    return count > index;
                });
                FT_REQUIRE(tree_split.left.concat(tree_split.right).to_vector() == values);

                const auto reversible_split = reversible.split_at(index);
                FT_REQUIRE(reversible_split.left.concat(reversible_split.right).to_vector() == values);
                reversible_split.left.validate_invariants();
                reversible_split.right.validate_invariants();

                const auto reversed_split = reversed_reversible.split_at(index);
                FT_REQUIRE(
                    reversed_split.left.concat(reversed_split.right).to_vector() == reversed_values);
                reversed_split.left.validate_invariants();
                reversed_split.right.validate_invariants();

                const auto rope_split = rope.split_at(index);
                FT_REQUIRE(rope_split.left.concat(rope_split.right).to_vector() == values);
                rope_split.left.validate_invariants();
                rope_split.right.validate_invariants();

                const auto measured_split = measured_rope.split_at(index);
                FT_REQUIRE(measured_split.left.concat(measured_split.right).to_vector() == values);
                measured_split.left.validate_invariants();
                measured_split.right.validate_invariants();
            }
        }
    });

    tests.add("empty persistent deque sorted search and split behavior is total", [] {
        const auto empty = ft::persistent_deque<int>{};
        FT_REQUIRE_EQUAL(empty.sorted_lower_bound(7), static_cast<std::size_t>(0));
        FT_REQUIRE_EQUAL(empty.sorted_upper_bound(7), static_cast<std::size_t>(0));
        FT_REQUIRE_EQUAL(empty.sorted_binary_search(7), std::ptrdiff_t{-1});
        FT_REQUIRE(!empty.sorted_contains(7));

        const auto lower = empty.split_at_sorted_lower_bound(7);
        FT_REQUIRE(lower.left.empty());
        FT_REQUIRE(lower.right.empty());
        const auto upper = empty.split_at_sorted_upper_bound(7);
        FT_REQUIRE(upper.left.empty());
        FT_REQUIRE(upper.right.empty());
        const auto equal = empty.split_at_sorted_equal_range(7);
        FT_REQUIRE(equal.before.empty());
        FT_REQUIRE(equal.range.empty());
        FT_REQUIRE(equal.after.empty());
        FT_REQUIRE(empty.remove_all_sorted(7).empty());
        FT_REQUIRE(empty.insert_sorted(7).to_vector() == std::vector<int>{7});
        empty.validate_invariants();
    });

    tests.add("non-group key measure locate matches split-find at every threshold", [] {
        using tree_type = ft::finger_tree<int, ft::order_statistic_measure<int>>;
        for (auto size = std::size_t{0}; size <= 48; ++size) {
            auto values = iota_values(size);
            for (auto& value : values) {
                value *= 2;
            }
            const auto tree = tree_type::from_range(values);

            for (auto target = -2; target <= static_cast<int>(size * 2 + 2); ++target) {
                const auto predicate = [target](const ft::ranked_key<int>& measure) {
                    return measure.key.has_value() && measure.key.value() >= target;
                };
                const auto located = tree.try_locate(predicate);
                const auto split = tree.try_split_find(predicate);
                FT_REQUIRE_EQUAL(located.item.has_value(), split.has_value());
                if (split.has_value()) {
                    FT_REQUIRE_EQUAL(*located.item, split->item);
                    FT_REQUIRE(located.measure_before == split->left.measure());
                } else {
                    FT_REQUIRE(located.measure_before == tree.measure());
                }
            }
        }
    });
}

} // namespace

void add_command_sequence_tests(suite& tests)
{
    add_command_sequence_tests_impl(tests);
}
