/// The simple reference model generated command sequences are replayed against.
///
/// Deliberately naive: it is right by inspection, which is what makes a disagreement with the real
/// structure evidence against the structure.

#pragma once

#include "replay_seed.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace durable7::finger_tree::tests {

/// A reproducible pseudorandom generator. Deterministic so a failing generated case can be replayed
/// from its seed.
class deterministic_rng final {
public:
    /// Constructs the deterministic rng from the given parts.
    explicit deterministic_rng(const std::uint64_t seed)
        : state_(effective_replay_seed(seed))
    {
        capture_replay_seed(state_);
    }

    /// The policy's seed state.
    [[nodiscard]] constexpr std::uint64_t seed_state() const noexcept
    {
        return state_;
    }

    /// The next value, or nothing when exhausted.
    [[nodiscard]] constexpr std::uint64_t next() noexcept
    {
        auto x = state_;
        x ^= x << 7;
        x ^= x >> 9;
        state_ = x;
        return x;
    }

    /// The next index.
    [[nodiscard]] constexpr std::size_t next_index(const std::size_t exclusive_upper_bound) noexcept
    {
        if (exclusive_upper_bound == 0) {
            return 0;
        }

        return static_cast<std::size_t>(next() % exclusive_upper_bound);
    }

    /// The next pseudorandom integer in the sequence.
    [[nodiscard]] constexpr int next_int(const int inclusive_min, const int inclusive_max) noexcept
    {
        const auto width = static_cast<std::uint64_t>(inclusive_max - inclusive_min + 1);
        return inclusive_min + static_cast<int>(next() % width);
    }

private:
    std::uint64_t state_;
};

/// A generated sequence of operations, replayed against both the structure and a simple model so
/// any divergence is a bug.
template <class Operation>
class command_sequence final {
public:
    /// An empty sequence.
    command_sequence() = default;

    /// An empty sequence using the supplied policy, which it retains.
    explicit command_sequence(std::vector<Operation> operations)
        : operations_(std::move(operations))
    {
    }

    /// A sequence with the element added.
    void push(Operation operation)
    {
        operations_.push_back(std::move(operation));
    }

    /// Whether the sequence holds no elements.
    [[nodiscard]] bool empty() const noexcept
    {
        return operations_.empty();
    }

    /// Number of elements in the sequence.
    [[nodiscard]] std::size_t size() const noexcept
    {
        return operations_.size();
    }

    [[nodiscard]] const Operation& operator[](const std::size_t index) const
    {
        return operations_[index];
    }

    /// How many operations have been counted.
    [[nodiscard]] const std::vector<Operation>& operations() const noexcept
    {
        return operations_;
    }

    /// A readable rendering, for diagnostics.
    [[nodiscard]] std::string describe() const
    {
        std::ostringstream output;
        for (std::size_t index = 0; index < operations_.size(); ++index) {
            output << index << ": " << operations_[index] << '\n';
        }

        return output.str();
    }

    /// A sequence without the elements in the range.
    [[nodiscard]] command_sequence without_range(const std::size_t index, const std::size_t count) const
    {
        if (index > operations_.size() || count > operations_.size() - index) {
            throw std::out_of_range("command-sequence removal range is outside the program");
        }

        auto result = std::vector<Operation>{};
        result.reserve(operations_.size() - count);
        result.insert(result.end(), operations_.begin(), operations_.begin() + static_cast<std::ptrdiff_t>(index));
        result.insert(
            result.end(),
            operations_.begin() + static_cast<std::ptrdiff_t>(index + count),
            operations_.end());
        return command_sequence{std::move(result)};
    }

private:
    std::vector<Operation> operations_;
};

// Delta-debug a known failing stateful program. The returned program is
// deletion-minimal: removing any one remaining command makes the supplied
// failure predicate false. Commands should therefore be total under replay,
// typically by interpreting stored index operands relative to the current
// model state.
template <class Operation, class Fails>
[[nodiscard]] command_sequence<Operation> shrink_failing_sequence(
    command_sequence<Operation> failing,
    Fails fails)
{
    if (!fails(failing)) {
        throw std::invalid_argument("cannot shrink a command sequence that does not reproduce the failure");
    }

    auto granularity = std::size_t{2};
    while (!failing.empty()) {
        const auto size = failing.size();
        const auto chunk_size = (size + granularity - 1) / granularity;
        auto reduced = false;

        for (auto index = std::size_t{0}; index < size; index += chunk_size) {
            const auto count = (std::min)(chunk_size, size - index);
            auto candidate = failing.without_range(index, count);
            if (fails(candidate)) {
                failing = std::move(candidate);
                granularity = (std::max)(std::size_t{2}, granularity - 1);
                reduced = true;
                break;
            }
        }

        if (reduced) {
            continue;
        }

        if (granularity >= size) {
            break;
        }

        granularity = (std::min)(size, granularity * 2);
    }

    return failing;
}

} // namespace durable7::finger_tree::tests
