#pragma once

#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace tools::data_structures::finger_tree::tests {

class deterministic_rng final {
public:
    explicit constexpr deterministic_rng(const std::uint64_t seed) noexcept
        : state_(seed == 0 ? 0x9e3779b97f4a7c15ULL : seed)
    {
    }

    [[nodiscard]] constexpr std::uint64_t seed_state() const noexcept
    {
        return state_;
    }

    [[nodiscard]] constexpr std::uint64_t next() noexcept
    {
        auto x = state_;
        x ^= x << 7;
        x ^= x >> 9;
        state_ = x;
        return x;
    }

    [[nodiscard]] constexpr std::size_t next_index(const std::size_t exclusive_upper_bound) noexcept
    {
        if (exclusive_upper_bound == 0) {
            return 0;
        }

        return static_cast<std::size_t>(next() % exclusive_upper_bound);
    }

    [[nodiscard]] constexpr int next_int(const int inclusive_min, const int inclusive_max) noexcept
    {
        const auto width = static_cast<std::uint64_t>(inclusive_max - inclusive_min + 1);
        return inclusive_min + static_cast<int>(next() % width);
    }

private:
    std::uint64_t state_;
};

template <class Operation>
class command_sequence final {
public:
    void push(Operation operation)
    {
        operations_.push_back(std::move(operation));
    }

    [[nodiscard]] const std::vector<Operation>& operations() const noexcept
    {
        return operations_;
    }

    [[nodiscard]] std::string describe() const
    {
        std::ostringstream output;
        for (std::size_t index = 0; index < operations_.size(); ++index) {
            output << index << ": " << operations_[index] << '\n';
        }

        return output.str();
    }

private:
    std::vector<Operation> operations_;
};

} // namespace tools::data_structures::finger_tree::tests
