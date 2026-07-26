/// Counts operations and comparisons, so a test can assert on how much work was done.

#pragma once

#include <atomic>
#include <compare>
#include <concepts>
#include <cstddef>
#include <functional>
#include <utility>

namespace durable7::finger_tree::tests {

/// Counts operations, for tests asserting on how much work an operation did.
class operation_counter final {
public:
    /// Clears the counts.
    void reset() noexcept
    {
        comparisons_.store(0);
        invocations_.store(0);
    }

    /// Counts one comparison.
    void record_comparison() noexcept
    {
        comparisons_.fetch_add(1);
    }

    /// Counts one invocation.
    void record_invocation() noexcept
    {
        invocations_.fetch_add(1);
    }

    /// How many comparisons have been counted.
    [[nodiscard]] std::size_t comparisons() const noexcept
    {
        return comparisons_.load();
    }

    /// How many invocations have been counted.
    [[nodiscard]] std::size_t invocations() const noexcept
    {
        return invocations_.load();
    }

private:
    std::atomic<std::size_t> comparisons_ = 0;
    std::atomic<std::size_t> invocations_ = 0;
};

/// A comparison that counts its invocations, so a test can assert an operation compared only as
/// often as its bound allows.
template <class Compare = std::less<>>
class counting_compare final {
public:
    /// Constructs the counting compare from the given parts.
    explicit counting_compare(operation_counter& counter, Compare compare = Compare{})
        : counter_(&counter)
        , compare_(std::move(compare))
    {
    }

    template <class T, class U>
    [[nodiscard]] bool operator()(T&& left, U&& right) const
        requires(std::predicate<Compare&, T, U>)
    {
        counter_->record_comparison();
        return compare_(std::forward<T>(left), std::forward<U>(right));
    }

private:
    operation_counter* counter_;
    Compare compare_;
};

} // namespace durable7::finger_tree::tests
