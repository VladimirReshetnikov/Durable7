#pragma once

#include <atomic>
#include <compare>
#include <concepts>
#include <cstddef>
#include <functional>
#include <utility>

namespace durable7::finger_tree::tests {

class operation_counter final {
public:
    void reset() noexcept
    {
        comparisons_.store(0);
        invocations_.store(0);
    }

    void record_comparison() noexcept
    {
        comparisons_.fetch_add(1);
    }

    void record_invocation() noexcept
    {
        invocations_.fetch_add(1);
    }

    [[nodiscard]] std::size_t comparisons() const noexcept
    {
        return comparisons_.load();
    }

    [[nodiscard]] std::size_t invocations() const noexcept
    {
        return invocations_.load();
    }

private:
    std::atomic<std::size_t> comparisons_ = 0;
    std::atomic<std::size_t> invocations_ = 0;
};

template <class Compare = std::less<>>
class counting_compare final {
public:
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
