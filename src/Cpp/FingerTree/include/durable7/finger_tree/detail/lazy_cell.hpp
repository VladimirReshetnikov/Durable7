/// A cell computing its value at most once, on first force.
///
/// The deque's amortized bounds depend on deferred work actually staying deferred, so a forced cell
/// caches its result and a never-read cell is never computed.

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace durable7::finger_tree::detail {

/// A cell computing its value at most once, on first force.
template <class Value>
class lazy_cell final {
public:
    using value_type = Value;
    using pointer = std::shared_ptr<const value_type>;

    /// The cached value, if the cell has been forced.
    static lazy_cell computed(value_type value)
    {
        return from_shared(std::make_shared<const value_type>(std::move(value)));
    }

    /// Wraps an already-shared representation.
    static lazy_cell from_shared(pointer value)
    {
        if (value == nullptr) {
            throw std::invalid_argument("lazy_cell cannot hold a null computed value");
        }

        return lazy_cell{std::make_shared<control_block>(std::make_shared<computed_state>(std::move(value)))};
    }

    /// Records the work without doing it, so a later read pays for it only if it happens.
    template <class Factory>
    static lazy_cell defer(Factory factory)
    {
        return lazy_cell{
            std::make_shared<control_block>(
                std::make_shared<pending_state<std::decay_t<Factory>>>(std::forward<Factory>(factory)))};
    }

    /// Reads the value stored for the key.
    [[nodiscard]] pointer get() const
    {
        for (;;) {
            auto state = control_->state.load();
            if (auto value = state->try_get()) {
                return value;
            }

            auto computed = state->compute();
            if (computed == nullptr) {
                throw std::logic_error("lazy_cell factory returned a null computed value");
            }

            auto replacement = std::make_shared<computed_state>(computed);
            if (control_->state.compare_exchange_strong(state, replacement)) {
                return computed;
            }

            if (auto value = state->try_get()) {
                return value;
            }
        }
    }

    /// Whether the cell has already been computed.
    [[nodiscard]] bool is_forced() const
    {
        return control_->state.load()->try_get() != nullptr;
    }

private:
    struct state_base {
        /// Constructs the state base.
        virtual ~state_base() = default;
        /// Reads the value stored for the key, or nothing when absent.
        [[nodiscard]] virtual pointer try_get() const noexcept = 0;
        /// Computes the value the cell stands for.
        [[nodiscard]] virtual pointer compute() const = 0;
    };

    struct computed_state final : state_base {
        /// Constructs the computed state from the given parts.
        explicit computed_state(pointer value)
            : value_(std::move(value))
        {
        }

        /// Reads the value stored for the key, or nothing when absent.
        [[nodiscard]] pointer try_get() const noexcept override
        {
            return value_;
        }

        /// Computes the value the cell stands for.
        [[nodiscard]] pointer compute() const override
        {
            return value_;
        }

    private:
        pointer value_;
    };

    template <class Factory>
    struct pending_state final : state_base {
        /// Constructs the pending state from the given parts.
        explicit pending_state(Factory factory)
            : factory_(std::move(factory))
        {
        }

        /// Reads the value stored for the key, or nothing when absent.
        [[nodiscard]] pointer try_get() const noexcept override
        {
            return nullptr;
        }

        /// Computes the value the cell stands for.
        [[nodiscard]] pointer compute() const override
        {
            using result_type = std::invoke_result_t<const Factory&>;

            if constexpr (std::is_same_v<std::decay_t<result_type>, pointer>) {
                return std::invoke(factory_);
            } else {
                return std::make_shared<const value_type>(std::invoke(factory_));
            }
        }

    private:
        Factory factory_;
    };

    struct control_block final {
        /// Constructs the control block from the given parts.
        explicit control_block(std::shared_ptr<const state_base> initial)
            : state(std::move(initial))
        {
        }

        mutable std::atomic<std::shared_ptr<const state_base>> state;
    };

    explicit lazy_cell(std::shared_ptr<control_block> control)
        : control_(std::move(control))
    {
    }

    std::shared_ptr<control_block> control_;
};

} // namespace durable7::finger_tree::detail
