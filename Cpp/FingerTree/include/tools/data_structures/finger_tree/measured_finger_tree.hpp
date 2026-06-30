#pragma once

#include <tools/data_structures/finger_tree/detail/common.hpp>
#include <tools/data_structures/finger_tree/detail/measured_tree.hpp>
#include <tools/data_structures/finger_tree/measure_predicates.hpp>
#include <tools/data_structures/finger_tree/measures.hpp>

#include <concepts>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tools::data_structures::finger_tree {

template <class Element, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, Element>
class finger_tree;

template <class Element, class MeasurePolicy>
struct finger_tree_split final {
    finger_tree<Element, MeasurePolicy> left;
    finger_tree<Element, MeasurePolicy> right;
};

template <class Element, class MeasurePolicy>
struct finger_tree_item_split final {
    finger_tree<Element, MeasurePolicy> left;
    Element item;
    finger_tree<Element, MeasurePolicy> right;
};

template <class Element, class MeasurePolicy>
struct finger_tree_locate_result final {
    typename MeasurePolicy::measure_type measure_before;
    Element item;
};

template <class Element, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, Element>
class finger_tree final {
public:
    using value_type = Element;
    using measure_policy = MeasurePolicy;
    using measure_type = typename measure_policy::measure_type;
    using size_type = std::size_t;

    finger_tree() = default;

    finger_tree(std::initializer_list<value_type> values)
        : root_(build_tree(values.begin(), values.end()))
    {
    }

    template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
    finger_tree(Iterator first, Sentinel last)
        : root_(build_tree(std::move(first), std::move(last)))
    {
    }

    [[nodiscard]] static finger_tree empty_tree()
    {
        return finger_tree{};
    }

    template <std::ranges::input_range Range>
    [[nodiscard]] static finger_tree from_range(Range&& values)
    {
        return finger_tree{std::ranges::begin(values), std::ranges::end(values)};
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return root_.is_empty();
    }

    [[nodiscard]] measure_type measure() const
    {
        return root_.measure();
    }

    [[nodiscard]] const value_type& front() const
    {
        throw_if_not_empty();
        return root_.first_element().value();
    }

    [[nodiscard]] const value_type& back() const
    {
        throw_if_not_empty();
        return root_.last_element().value();
    }

    [[nodiscard]] finger_tree prepend(value_type value) const
    {
        return finger_tree{root_.cons(detail::measured_element<Element, MeasurePolicy>::leaf(std::move(value)))};
    }

    [[nodiscard]] finger_tree append(value_type value) const
    {
        return finger_tree{root_.snoc(detail::measured_element<Element, MeasurePolicy>::leaf(std::move(value)))};
    }

    [[nodiscard]] finger_tree concat(const finger_tree& other) const
    {
        if (other.empty()) {
            return *this;
        }

        if (empty()) {
            return other;
        }

        return finger_tree{root_.concat(other.root_)};
    }

    [[nodiscard]] std::optional<finger_tree_item_split<Element, MeasurePolicy>> try_view_left() const
    {
        if (auto view = root_.try_view_left()) {
            return finger_tree_item_split<Element, MeasurePolicy>{finger_tree{}, view->value.value(), wrap(view->rest)};
        }

        return std::nullopt;
    }

    [[nodiscard]] std::optional<finger_tree_item_split<Element, MeasurePolicy>> try_view_right() const
    {
        if (auto view = root_.try_view_right()) {
            return finger_tree_item_split<Element, MeasurePolicy>{wrap(view->rest), view->value.value(), finger_tree{}};
        }

        return std::nullopt;
    }

    template <class Predicate>
        requires std::predicate<Predicate&, const measure_type&>
    [[nodiscard]] finger_tree_split<Element, MeasurePolicy> split(Predicate predicate) const
    {
        if (root_.is_empty() || !std::invoke(predicate, root_.measure())) {
            return finger_tree_split<Element, MeasurePolicy>{*this, finger_tree{}};
        }

        auto split_result = root_.split_tree(predicate, MeasurePolicy::empty());
        return finger_tree_split<Element, MeasurePolicy>{
            wrap(split_result.left),
            wrap(split_result.right.cons(split_result.hit))};
    }

    template <class Predicate>
        requires std::predicate<Predicate&, const measure_type&>
    [[nodiscard]] std::optional<finger_tree_item_split<Element, MeasurePolicy>> try_split_find(Predicate predicate) const
    {
        if (root_.is_empty() || !std::invoke(predicate, root_.measure())) {
            return std::nullopt;
        }

        auto split_result = root_.split_tree(predicate, MeasurePolicy::empty());
        return finger_tree_item_split<Element, MeasurePolicy>{
            wrap(split_result.left),
            split_result.hit.value(),
            wrap(split_result.right)};
    }

    template <class Predicate>
        requires std::predicate<Predicate&, const measure_type&>
    [[nodiscard]] std::optional<finger_tree_locate_result<Element, MeasurePolicy>> try_locate(Predicate predicate) const
    {
        if (root_.is_empty() || !std::invoke(predicate, root_.measure())) {
            return std::nullopt;
        }

        auto located = root_.locate_tree(predicate, MeasurePolicy::empty());
        return finger_tree_locate_result<Element, MeasurePolicy>{std::move(located.measure_before), located.hit.value()};
    }

    [[nodiscard]] std::vector<value_type> to_vector() const
    {
        auto result = std::vector<value_type>{};
        root_.flatten(result);
        return result;
    }

    template <std::output_iterator<const value_type&> OutputIterator>
    void copy_to(OutputIterator output) const
    {
        const auto values = to_vector();
        for (const auto& value : values) {
            *output++ = value;
        }
    }

private:
    using root_type = detail::measured_tree<Element, MeasurePolicy>;

    explicit finger_tree(root_type root)
        : root_(std::move(root))
    {
    }

    [[nodiscard]] static finger_tree wrap(root_type root)
    {
        return root.is_empty() ? finger_tree{} : finger_tree{std::move(root)};
    }

    template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
    [[nodiscard]] static root_type build_tree(Iterator first, Sentinel last)
    {
        auto root = root_type::empty();
        for (; first != last; ++first) {
            root = root.snoc(detail::measured_element<Element, MeasurePolicy>::leaf(*first));
        }

        return root;
    }

    void throw_if_not_empty() const
    {
        if (empty()) {
            throw std::logic_error("finger_tree is empty");
        }
    }

    root_type root_;
};

} // namespace tools::data_structures::finger_tree
