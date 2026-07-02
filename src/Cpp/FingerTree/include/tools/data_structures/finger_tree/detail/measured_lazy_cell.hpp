#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tools::data_structures::finger_tree::detail {

template <class Tree>
class measured_lazy_cell final {
public:
    using tree_type = Tree;
    using pointer = std::shared_ptr<const tree_type>;
    using measure_type = typename tree_type::measure_type;

    static measured_lazy_cell computed(pointer tree)
    {
        if (tree == nullptr) {
            throw std::invalid_argument("measured_lazy_cell cannot hold a null computed tree");
        }

        return measured_lazy_cell{
            std::make_shared<control_block>(
                std::make_shared<computed_state>(std::move(tree)))};
    }

    template <class Factory, class MeasureProbe>
    static measured_lazy_cell defer(Factory factory, MeasureProbe measure_probe)
    {
        return measured_lazy_cell{
            std::make_shared<control_block>(
                std::make_shared<pending_state<std::decay_t<Factory>, std::decay_t<MeasureProbe>>>(
                    std::forward<Factory>(factory),
                    std::forward<MeasureProbe>(measure_probe)))};
    }

    template <class Factory>
    static measured_lazy_cell defer_force_only(Factory factory)
    {
        return defer(std::forward<Factory>(factory), []() -> std::optional<measure_type> {
            return std::nullopt;
        });
    }

    [[nodiscard]] pointer force() const
    {
        for (;;) {
            auto state = control_->state.load();
            if (auto tree = state->try_get_tree()) {
                return tree;
            }

            auto computed = state->compute_tree();
            if (computed == nullptr) {
                throw std::logic_error("measured_lazy_cell factory returned a null tree");
            }

            auto replacement = std::make_shared<computed_state>(computed);
            if (control_->state.compare_exchange_strong(state, replacement)) {
                return computed;
            }

            if (auto tree = state->try_get_tree()) {
                return tree;
            }
        }
    }

    [[nodiscard]] measure_type measure() const
    {
        auto state = control_->state.load();
        if (auto tree = state->try_get_tree()) {
            return tree->measure();
        }

        if (auto measure = state->try_measure_without_forcing()) {
            return *measure;
        }

        return force()->measure();
    }

    [[nodiscard]] bool is_forced() const
    {
        return control_->state.load()->try_get_tree() != nullptr;
    }

private:
    struct state_base {
        virtual ~state_base() = default;
        [[nodiscard]] virtual pointer try_get_tree() const noexcept = 0;
        [[nodiscard]] virtual pointer compute_tree() const = 0;
        [[nodiscard]] virtual std::optional<measure_type> try_measure_without_forcing() const = 0;
    };

    struct computed_state final : state_base {
        explicit computed_state(pointer tree)
            : tree_(std::move(tree))
        {
        }

        [[nodiscard]] pointer try_get_tree() const noexcept override
        {
            return tree_;
        }

        [[nodiscard]] pointer compute_tree() const override
        {
            return tree_;
        }

        [[nodiscard]] std::optional<measure_type> try_measure_without_forcing() const override
        {
            return tree_->measure();
        }

    private:
        pointer tree_;
    };

    template <class Factory, class MeasureProbe>
    struct pending_state final : state_base {
        pending_state(Factory factory, MeasureProbe measure_probe)
            : factory_(std::move(factory))
            , measure_probe_(std::move(measure_probe))
        {
        }

        [[nodiscard]] pointer try_get_tree() const noexcept override
        {
            return nullptr;
        }

        [[nodiscard]] pointer compute_tree() const override
        {
            using result_type = std::invoke_result_t<const Factory&>;

            if constexpr (std::is_same_v<std::decay_t<result_type>, pointer>) {
                return std::invoke(factory_);
            } else {
                return std::make_shared<const tree_type>(std::invoke(factory_));
            }
        }

        [[nodiscard]] std::optional<measure_type> try_measure_without_forcing() const override
        {
            return std::invoke(measure_probe_);
        }

    private:
        Factory factory_;
        MeasureProbe measure_probe_;
    };

    struct control_block final {
        explicit control_block(std::shared_ptr<const state_base> initial)
            : state(std::move(initial))
        {
        }

        mutable std::atomic<std::shared_ptr<const state_base>> state;
    };

    explicit measured_lazy_cell(std::shared_ptr<control_block> control)
        : control_(std::move(control))
    {
    }

    std::shared_ptr<control_block> control_;
};

} // namespace tools::data_structures::finger_tree::detail
