/// A lazy cell that can also report its measure without forcing.
///
/// A push cell caches the measure its subtree would have, so a measure read usually leaves the
/// subtree unbuilt. Only an operation that needs the elements themselves forces it.

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace durable7::finger_tree::detail {

/// A lazy cell that can also report its measure without forcing.
template <class Tree>
class measured_lazy_cell final {
public:
    using tree_type = Tree;
    using pointer = std::shared_ptr<const tree_type>;
    using measure_type = typename tree_type::measure_type;

    /// The cached value, if the cell has been forced.
    static measured_lazy_cell computed(pointer tree)
    {
        if (tree == nullptr) {
            throw std::invalid_argument("measured_lazy_cell cannot hold a null computed tree");
        }

        return measured_lazy_cell{std::make_shared<control_block>(std::move(tree), nullptr)};
    }

    /// Records the work without doing it, so a later read pays for it only if it happens.
    template <class Factory, class MeasureProbe>
    static measured_lazy_cell defer(Factory factory, MeasureProbe measure_probe)
    {
        return measured_lazy_cell{
            std::make_shared<control_block>(
                nullptr,
                std::make_shared<pending_state<std::decay_t<Factory>, std::decay_t<MeasureProbe>>>(
                    std::forward<Factory>(factory),
                    std::forward<MeasureProbe>(measure_probe)))};
    }

    /// Records work that only a force needs, leaving the measure readable without it.
    template <class Factory>
    static measured_lazy_cell defer_force_only(Factory factory)
    {
        return defer(std::forward<Factory>(factory), []() -> std::optional<measure_type> {
            return std::nullopt;
        });
    }

    /// Forces the cell, computing its value once and caching it.
    [[nodiscard]] pointer force() const
    {
        for (;;) {
            if (auto tree = control_->tree.load()) {
                return tree;
            }

            auto state = control_->state.load();
            if (state == nullptr) {
                continue;
            }

            auto computed = state->compute_tree();
            if (computed == nullptr) {
                throw std::logic_error("measured_lazy_cell factory returned a null tree");
            }

            auto expected = pointer{};
            if (control_->tree.compare_exchange_strong(expected, computed)) {
                control_->state.store(nullptr);
                return computed;
            }

            return expected;
        }
    }

    /// The combined measure of every element, read from the cached root measure.
    [[nodiscard]] measure_type measure() const
    {
        if (auto tree = control_->tree.load()) {
            return tree->measure();
        }

        auto state = control_->state.load();
        if (state != nullptr) {
            if (auto measure = state->try_measure_without_forcing()) {
                return *measure;
            }
        }

        return force()->measure();
    }

    /// Whether the cell has already been computed.
    [[nodiscard]] bool is_forced() const
    {
        return control_->tree.load() != nullptr;
    }

private:
    struct state_base {
        /// Constructs the state base.
        virtual ~state_base() = default;
        /// Computes the subtree the cell stands for.
        [[nodiscard]] virtual pointer compute_tree() const = 0;
        /// Reads the cached measure if it is available without forcing. A push cell caches its
        /// resulting measure, so this usually succeeds and the subtree stays unbuilt.
        [[nodiscard]] virtual std::optional<measure_type> try_measure_without_forcing() const = 0;
    };

    template <class Factory, class MeasureProbe>
    struct pending_state final : state_base {
        /// Constructs the pending state from the given parts.
        pending_state(Factory factory, MeasureProbe measure_probe)
            : factory_(std::move(factory))
            , measure_probe_(std::move(measure_probe))
        {
        }

        /// Computes the subtree the cell stands for.
        [[nodiscard]] pointer compute_tree() const override
        {
            using result_type = std::invoke_result_t<const Factory&>;

            if constexpr (std::is_same_v<std::decay_t<result_type>, pointer>) {
                return std::invoke(factory_);
            } else {
                return std::make_shared<const tree_type>(std::invoke(factory_));
            }
        }

        /// Reads the cached measure if it is available without forcing. A push cell caches its
        /// resulting measure, so this usually succeeds and the subtree stays unbuilt.
        [[nodiscard]] std::optional<measure_type> try_measure_without_forcing() const override
        {
            return std::invoke(measure_probe_);
        }

    private:
        Factory factory_;
        MeasureProbe measure_probe_;
    };

    struct control_block final {
        /// Constructs the control block from the given parts.
        control_block(pointer initial_tree, std::shared_ptr<const state_base> initial_state)
            : tree(std::move(initial_tree))
            , state(std::move(initial_state))
        {
        }

        mutable std::atomic<pointer> tree;
        mutable std::atomic<std::shared_ptr<const state_base>> state;
    };

    explicit measured_lazy_cell(std::shared_ptr<control_block> control)
        : control_(std::move(control))
    {
    }

    std::shared_ptr<control_block> control_;
};

} // namespace durable7::finger_tree::detail
