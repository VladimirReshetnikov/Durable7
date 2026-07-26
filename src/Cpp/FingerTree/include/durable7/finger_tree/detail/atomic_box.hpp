/// A box holding a value that may be replaced atomically.
///
/// Independently held handles may be read and updated concurrently, which is what lets the lazy
/// cells force themselves without external locking.

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace durable7::finger_tree::detail {

/// A box holding a value that may be replaced atomically, so independently held handles can be read
/// and updated concurrently.
template <class Value>
class atomic_box final {
public:
    using value_type = Value;
    using pointer = std::shared_ptr<const value_type>;

    /// Constructs the atomic box.
    atomic_box() noexcept = default;
    /// Takes a second handle on the same collection version; the nodes are shared, not copied.
    atomic_box(const atomic_box&) = delete;
    atomic_box& operator=(const atomic_box&) = delete;

    /// Reads the value stored for the key.
    [[nodiscard]] pointer get() const noexcept
    {
        return value_.load();
    }

    /// Whether a value is present.
    [[nodiscard]] bool has_value() const noexcept
    {
        return get() != nullptr;
    }

    /// The value stored for the key, computing and storing one when absent.
    template <class Factory>
    [[nodiscard]] pointer get_or_compute(Factory&& factory) const
    {
        if (auto existing = value_.load()) {
            return existing;
        }

        auto computed = make_pointer(std::forward<Factory>(factory));
        if (computed == nullptr) {
            throw std::logic_error("atomic_box factory returned a null value");
        }

        pointer expected;
        if (value_.compare_exchange_strong(expected, computed)) {
            return computed;
        }

        return expected;
    }

private:
    template <class Factory>
    [[nodiscard]] static pointer make_pointer(Factory&& factory)
    {
        using result_type = std::invoke_result_t<Factory&&>;

        if constexpr (std::is_same_v<std::decay_t<result_type>, pointer>) {
            return std::invoke(std::forward<Factory>(factory));
        } else {
            return std::make_shared<const value_type>(std::invoke(std::forward<Factory>(factory)));
        }
    }

    mutable std::atomic<pointer> value_;
};

} // namespace durable7::finger_tree::detail
