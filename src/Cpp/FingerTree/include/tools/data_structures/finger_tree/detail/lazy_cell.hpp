#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tools::data_structures::finger_tree::detail {

template <class Value>
class lazy_cell final {
public:
    using value_type = Value;
    using pointer = std::shared_ptr<const value_type>;

    static lazy_cell computed(value_type value)
    {
        return from_shared(std::make_shared<const value_type>(std::move(value)));
    }

    static lazy_cell from_shared(pointer value)
    {
        if (value == nullptr) {
            throw std::invalid_argument("lazy_cell cannot hold a null computed value");
        }

        return lazy_cell{std::make_shared<control_block>(std::make_shared<computed_state>(std::move(value)))};
    }

    template <class Factory>
    static lazy_cell defer(Factory factory)
    {
        return lazy_cell{
            std::make_shared<control_block>(
                std::make_shared<pending_state<std::decay_t<Factory>>>(std::forward<Factory>(factory)))};
    }

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

    [[nodiscard]] bool is_forced() const
    {
        return control_->state.load()->try_get() != nullptr;
    }

private:
    struct state_base {
        virtual ~state_base() = default;
        [[nodiscard]] virtual pointer try_get() const noexcept = 0;
        [[nodiscard]] virtual pointer compute() const = 0;
    };

    struct computed_state final : state_base {
        explicit computed_state(pointer value)
            : value_(std::move(value))
        {
        }

        [[nodiscard]] pointer try_get() const noexcept override
        {
            return value_;
        }

        [[nodiscard]] pointer compute() const override
        {
            return value_;
        }

    private:
        pointer value_;
    };

    template <class Factory>
    struct pending_state final : state_base {
        explicit pending_state(Factory factory)
            : factory_(std::move(factory))
        {
        }

        [[nodiscard]] pointer try_get() const noexcept override
        {
            return nullptr;
        }

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

} // namespace tools::data_structures::finger_tree::detail
