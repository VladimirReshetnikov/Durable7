#pragma once

#include <tools/data_structures/finger_tree/detail/atomic_box.hpp>
#include <tools/data_structures/finger_tree/detail/common.hpp>
#include <tools/data_structures/finger_tree/detail/measured_lazy_cell.hpp>
#include <tools/data_structures/finger_tree/measures.hpp>

#include <concepts>
#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace tools::data_structures::finger_tree::detail {

template <class Element, class MeasurePolicy>
class measured_node;

template <class Element, class MeasurePolicy>
class measured_element;

template <class Element, class MeasurePolicy>
class measured_tree;

template <class Element, class MeasurePolicy>
struct measured_tree_rep;

template <class Element, class MeasurePolicy>
struct measured_split_result;

template <class Element, class MeasurePolicy>
struct measured_view_result;

template <class Element, class MeasurePolicy>
struct measured_locate_result;

template <class Element, class MeasurePolicy>
using measured_buffer = std::vector<measured_element<Element, MeasurePolicy>>;

template <class Element, class MeasurePolicy>
[[nodiscard]] measured_tree<Element, MeasurePolicy> measured_from_buffer(
    const measured_buffer<Element, MeasurePolicy>& values);

template <class Element, class MeasurePolicy>
[[nodiscard]] measured_tree<Element, MeasurePolicy> measured_deep_left(
    measured_buffer<Element, MeasurePolicy> prefix,
    measured_tree<Element, MeasurePolicy> middle,
    const measured_buffer<Element, MeasurePolicy>& suffix);

template <class Element, class MeasurePolicy>
[[nodiscard]] measured_tree<Element, MeasurePolicy> measured_deep_right(
    const measured_buffer<Element, MeasurePolicy>& prefix,
    measured_tree<Element, MeasurePolicy> middle,
    measured_buffer<Element, MeasurePolicy> suffix);

template <class Element, class MeasurePolicy>
[[nodiscard]] measured_tree<Element, MeasurePolicy> measured_concat(
    measured_tree<Element, MeasurePolicy> left,
    measured_tree<Element, MeasurePolicy> right);

template <class Element, class MeasurePolicy>
[[nodiscard]] measured_tree<Element, MeasurePolicy> measured_concat_with_mid(
    measured_tree<Element, MeasurePolicy> left,
    const measured_buffer<Element, MeasurePolicy>& middle,
    measured_tree<Element, MeasurePolicy> right);

template <class Element, class MeasurePolicy>
class measured_element final {
public:
    using element_type = Element;
    using measure_policy = MeasurePolicy;
    using measure_type = typename measure_policy::measure_type;
    using node_pointer = std::shared_ptr<const measured_node<element_type, measure_policy>>;

    [[nodiscard]] static measured_element leaf(element_type value)
        requires ::tools::data_structures::finger_tree::measure_policy<MeasurePolicy, Element>
    {
        auto measure = MeasurePolicy::measure(value);
        return measured_element{leaf_storage{std::move(value), std::move(measure)}};
    }

    [[nodiscard]] static measured_element node(node_pointer value)
    {
        if (value == nullptr) {
            throw std::invalid_argument("measured node element cannot hold a null node");
        }

        return measured_element{std::move(value)};
    }

    [[nodiscard]] static measured_element node2(measured_element first, measured_element second);

    [[nodiscard]] static measured_element node3(
        measured_element first,
        measured_element second,
        measured_element third);

    [[nodiscard]] bool is_leaf() const noexcept
    {
        return std::holds_alternative<leaf_storage>(storage_);
    }

    [[nodiscard]] bool is_node() const noexcept
    {
        return std::holds_alternative<node_pointer>(storage_);
    }

    [[nodiscard]] const measure_type& measure() const;

    [[nodiscard]] const element_type& value() const
    {
        return std::get<leaf_storage>(storage_).value;
    }

    [[nodiscard]] const measured_node<element_type, measure_policy>& node_value() const
    {
        return *std::get<node_pointer>(storage_);
    }

    [[nodiscard]] node_pointer node_ptr() const
    {
        return std::get<node_pointer>(storage_);
    }

    void flatten(std::vector<element_type>& sink) const;

    template <class Function>
    void for_each(Function& function) const;

private:
    struct leaf_storage final {
        element_type value;
        measure_type measure;
    };

    using storage_type = std::variant<leaf_storage, node_pointer>;

    explicit measured_element(storage_type storage)
        : storage_(std::move(storage))
    {
    }

    storage_type storage_;
};

template <class Element, class MeasurePolicy>
class measured_node final {
public:
    using element_type = Element;
    using measure_policy = MeasurePolicy;
    using measure_type = typename measure_policy::measure_type;
    using child_type = measured_element<element_type, measure_policy>;
    using buffer_type = measured_buffer<element_type, measure_policy>;

    [[nodiscard]] static std::shared_ptr<const measured_node> make(buffer_type children)
    {
        if (children.size() < 2 || children.size() > 3) {
            throw std::logic_error("measured node arity must be two or three");
        }

        return std::make_shared<const measured_node>(std::move(children));
    }

    explicit measured_node(buffer_type children)
        : children_(std::move(children))
        , measure_(combine_all(children_))
    {
        if (children_.size() < 2 || children_.size() > 3) {
            throw std::logic_error("measured node arity must be two or three");
        }
    }

    [[nodiscard]] const buffer_type& children() const noexcept
    {
        return children_;
    }

    [[nodiscard]] const measure_type& measure() const noexcept
    {
        return measure_;
    }

    void flatten(std::vector<element_type>& sink) const
    {
        for (const auto& child : children_) {
            child.flatten(sink);
        }
    }

    template <class Function>
    void for_each(Function& function) const
    {
        for (const auto& child : children_) {
            child.for_each(function);
        }
    }

private:
    [[nodiscard]] static measure_type combine_all(const buffer_type& children)
    {
        auto result = children.front().measure();
        for (std::size_t index = 1; index != children.size(); ++index) {
            result = MeasurePolicy::combine(result, children[index].measure());
        }

        return result;
    }

    buffer_type children_;
    measure_type measure_;
};

template <class Element, class MeasurePolicy>
measured_element<Element, MeasurePolicy> measured_element<Element, MeasurePolicy>::node2(
    measured_element first,
    measured_element second)
{
    auto children = measured_buffer<Element, MeasurePolicy>{};
    children.reserve(2);
    children.push_back(std::move(first));
    children.push_back(std::move(second));
    return node(measured_node<Element, MeasurePolicy>::make(std::move(children)));
}

template <class Element, class MeasurePolicy>
measured_element<Element, MeasurePolicy> measured_element<Element, MeasurePolicy>::node3(
    measured_element first,
    measured_element second,
    measured_element third)
{
    auto children = measured_buffer<Element, MeasurePolicy>{};
    children.reserve(3);
    children.push_back(std::move(first));
    children.push_back(std::move(second));
    children.push_back(std::move(third));
    return node(measured_node<Element, MeasurePolicy>::make(std::move(children)));
}

template <class Element, class MeasurePolicy>
const typename measured_element<Element, MeasurePolicy>::measure_type&
measured_element<Element, MeasurePolicy>::measure() const
{
    return is_leaf() ? std::get<leaf_storage>(storage_).measure : node_value().measure();
}

template <class Element, class MeasurePolicy>
void measured_element<Element, MeasurePolicy>::flatten(std::vector<Element>& sink) const
{
    if (is_leaf()) {
        sink.push_back(value());
        return;
    }

    node_value().flatten(sink);
}

template <class Element, class MeasurePolicy>
template <class Function>
void measured_element<Element, MeasurePolicy>::for_each(Function& function) const
{
    if (is_leaf()) {
        std::invoke(function, value());
        return;
    }

    node_value().for_each(function);
}

template <class Element, class MeasurePolicy>
[[nodiscard]] typename MeasurePolicy::measure_type measured_buffer_measure(
    const measured_buffer<Element, MeasurePolicy>& values)
{
    if (values.empty()) {
        return MeasurePolicy::empty();
    }

    auto result = values.front().measure();
    for (std::size_t index = 1; index != values.size(); ++index) {
        result = MeasurePolicy::combine(result, values[index].measure());
    }

    return result;
}

template <class Buffer>
[[nodiscard]] Buffer measured_slice(const Buffer& values, const std::size_t first, const std::size_t last)
{
    if (first > last || last > values.size()) {
        throw std::out_of_range("measured buffer slice range is invalid");
    }

    return Buffer{
        values.begin() + static_cast<typename Buffer::difference_type>(first),
        values.begin() + static_cast<typename Buffer::difference_type>(last)};
}

template <class Element, class MeasurePolicy>
struct measured_buffer_split final {
    measured_buffer<Element, MeasurePolicy> before;
    measured_element<Element, MeasurePolicy> hit;
    measured_buffer<Element, MeasurePolicy> after;
};

enum class measured_tree_kind {
    empty,
    single,
    deep,
};

template <class Element, class MeasurePolicy>
struct measured_split_result final {
    measured_tree<Element, MeasurePolicy> left;
    measured_element<Element, MeasurePolicy> hit;
    measured_tree<Element, MeasurePolicy> right;
};

template <class Element, class MeasurePolicy>
struct measured_view_result final {
    measured_element<Element, MeasurePolicy> value;
    measured_tree<Element, MeasurePolicy> rest;
};

template <class Element, class MeasurePolicy>
struct measured_locate_result final {
    typename MeasurePolicy::measure_type measure_before;
    measured_element<Element, MeasurePolicy> hit;
};

template <class Element, class MeasurePolicy>
struct measured_locate_reference_result final {
    typename MeasurePolicy::measure_type measure_before;
    const measured_element<Element, MeasurePolicy>* hit;
};

template <class Element, class MeasurePolicy, class Predicate>
[[nodiscard]] measured_buffer_split<Element, MeasurePolicy> measured_split_buffer(
    Predicate& predicate,
    typename MeasurePolicy::measure_type accumulator,
    const measured_buffer<Element, MeasurePolicy>& values)
{
    if (values.empty()) {
        throw std::logic_error("measured_split_buffer requires a non-empty buffer");
    }

    for (std::size_t index = 0; index + 1 < values.size(); ++index) {
        accumulator = MeasurePolicy::combine(accumulator, values[index].measure());
        if (std::invoke(predicate, accumulator)) {
            return measured_buffer_split<Element, MeasurePolicy>{
                measured_slice(values, 0, index),
                values[index],
                measured_slice(values, index + 1, values.size())};
        }
    }

    return measured_buffer_split<Element, MeasurePolicy>{
        measured_slice(values, 0, values.size() - 1),
        values.back(),
        measured_buffer<Element, MeasurePolicy>{}};
}

template <class Element, class MeasurePolicy, class Predicate>
[[nodiscard]] measured_locate_result<Element, MeasurePolicy> measured_locate_buffer(
    Predicate& predicate,
    typename MeasurePolicy::measure_type accumulator,
    const measured_buffer<Element, MeasurePolicy>& values)
{
    if (values.empty()) {
        throw std::logic_error("measured_locate_buffer requires a non-empty buffer");
    }

    for (std::size_t index = 0; index + 1 < values.size(); ++index) {
        auto next = MeasurePolicy::combine(accumulator, values[index].measure());
        if (std::invoke(predicate, next)) {
            return measured_locate_result<Element, MeasurePolicy>{std::move(accumulator), values[index]};
        }

        accumulator = std::move(next);
    }

    return measured_locate_result<Element, MeasurePolicy>{std::move(accumulator), values.back()};
}

template <class Element, class MeasurePolicy, class Predicate>
[[nodiscard]] measured_locate_reference_result<Element, MeasurePolicy> measured_locate_buffer_reference(
    Predicate& predicate,
    typename MeasurePolicy::measure_type accumulator,
    const measured_buffer<Element, MeasurePolicy>& values)
{
    if (values.empty()) {
        throw std::logic_error("measured_locate_buffer_reference requires a non-empty buffer");
    }

    for (std::size_t index = 0; index + 1 < values.size(); ++index) {
        auto next = MeasurePolicy::combine(accumulator, values[index].measure());
        if (std::invoke(predicate, next)) {
            return measured_locate_reference_result<Element, MeasurePolicy>{
                std::move(accumulator),
                &values[index]};
        }

        accumulator = std::move(next);
    }

    return measured_locate_reference_result<Element, MeasurePolicy>{std::move(accumulator), &values.back()};
}

template <class Element, class MeasurePolicy>
class measured_tree final {
public:
    using element_type = Element;
    using measure_policy = MeasurePolicy;
    using measure_type = typename measure_policy::measure_type;
    using child_type = measured_element<element_type, measure_policy>;
    using buffer_type = measured_buffer<element_type, measure_policy>;

    measured_tree();
    ~measured_tree();

    measured_tree(const measured_tree&) noexcept = default;
    measured_tree(measured_tree&&) noexcept = default;
    measured_tree& operator=(const measured_tree&) noexcept = default;
    measured_tree& operator=(measured_tree&&) noexcept = default;

    [[nodiscard]] static measured_tree empty();

    [[nodiscard]] static measured_tree single(child_type element);

    [[nodiscard]] static measured_tree deep(
        buffer_type prefix,
        measured_lazy_cell<measured_tree> middle,
        buffer_type suffix);

    [[nodiscard]] static measured_tree deep_computed(
        buffer_type prefix,
        measured_tree middle,
        buffer_type suffix)
    {
        return deep(
            std::move(prefix),
            measured_lazy_cell<measured_tree>::computed(std::make_shared<const measured_tree>(std::move(middle))),
            std::move(suffix));
    }

    [[nodiscard]] measured_tree_kind kind() const noexcept;

    [[nodiscard]] bool is_empty() const noexcept;

    [[nodiscard]] measure_type measure() const;

    [[nodiscard]] const child_type& first_element() const;

    [[nodiscard]] const child_type& last_element() const;

    [[nodiscard]] measured_tree cons(child_type value) const;

    [[nodiscard]] measured_tree snoc(child_type value) const;

    [[nodiscard]] std::optional<measured_view_result<Element, MeasurePolicy>> try_view_left() const;

    [[nodiscard]] std::optional<measured_view_result<Element, MeasurePolicy>> try_view_right() const;

    void flatten(std::vector<Element>& sink) const;

    template <class Function>
    void for_each(Function& function) const;

    template <class Predicate>
    [[nodiscard]] measured_split_result<Element, MeasurePolicy> split_tree(
        Predicate& predicate,
        measure_type accumulator) const;

    template <class Predicate>
    [[nodiscard]] measured_locate_result<Element, MeasurePolicy> locate_tree(
        Predicate& predicate,
        measure_type accumulator) const;

    template <class Predicate>
    [[nodiscard]] measured_locate_reference_result<Element, MeasurePolicy> locate_tree_reference(
        Predicate& predicate,
        measure_type accumulator) const;

    [[nodiscard]] measured_tree concat(measured_tree right) const
    {
        return measured_concat(*this, std::move(right));
    }

    [[nodiscard]] const child_type& single_element() const;

    [[nodiscard]] const buffer_type& deep_prefix() const;

    [[nodiscard]] const buffer_type& deep_suffix() const;

    [[nodiscard]] measured_tree force_middle() const;

private:
    explicit measured_tree(std::shared_ptr<const measured_tree_rep<Element, MeasurePolicy>> rep)
        : rep_(std::move(rep))
    {
    }

    std::shared_ptr<const measured_tree_rep<Element, MeasurePolicy>> rep_;
};

template <class Element, class MeasurePolicy>
struct measured_tree_rep {
    using tree_type = measured_tree<Element, MeasurePolicy>;
    using child_type = measured_element<Element, MeasurePolicy>;
    using measure_type = typename MeasurePolicy::measure_type;
    using buffer_type = measured_buffer<Element, MeasurePolicy>;

    virtual ~measured_tree_rep() = default;

    [[nodiscard]] virtual measured_tree_kind kind() const noexcept = 0;
    [[nodiscard]] virtual bool is_empty() const noexcept = 0;
    [[nodiscard]] virtual measure_type measure() const = 0;
    [[nodiscard]] virtual const child_type& first_element() const = 0;
    [[nodiscard]] virtual const child_type& last_element() const = 0;
    [[nodiscard]] virtual tree_type cons(child_type value) const = 0;
    [[nodiscard]] virtual tree_type snoc(child_type value) const = 0;
    [[nodiscard]] virtual std::optional<measured_view_result<Element, MeasurePolicy>> try_view_left() const = 0;
    [[nodiscard]] virtual std::optional<measured_view_result<Element, MeasurePolicy>> try_view_right() const = 0;
    virtual void flatten(std::vector<Element>& sink) const = 0;
};

template <class Element, class MeasurePolicy>
struct empty_measured_tree_rep final : measured_tree_rep<Element, MeasurePolicy> {
    using tree_type = measured_tree<Element, MeasurePolicy>;
    using child_type = measured_element<Element, MeasurePolicy>;
    using measure_type = typename MeasurePolicy::measure_type;

    [[nodiscard]] measured_tree_kind kind() const noexcept override
    {
        return measured_tree_kind::empty;
    }

    [[nodiscard]] bool is_empty() const noexcept override
    {
        return true;
    }

    [[nodiscard]] measure_type measure() const override
    {
        return MeasurePolicy::empty();
    }

    [[nodiscard]] const child_type& first_element() const override
    {
        throw std::logic_error("element access on an empty measured tree");
    }

    [[nodiscard]] const child_type& last_element() const override
    {
        throw std::logic_error("element access on an empty measured tree");
    }

    [[nodiscard]] tree_type cons(child_type value) const override
    {
        return tree_type::single(std::move(value));
    }

    [[nodiscard]] tree_type snoc(child_type value) const override
    {
        return tree_type::single(std::move(value));
    }

    [[nodiscard]] std::optional<measured_view_result<Element, MeasurePolicy>> try_view_left() const override
    {
        return std::nullopt;
    }

    [[nodiscard]] std::optional<measured_view_result<Element, MeasurePolicy>> try_view_right() const override
    {
        return std::nullopt;
    }

    void flatten(std::vector<Element>&) const override
    {
    }
};

template <class Element, class MeasurePolicy>
struct single_measured_tree_rep final : measured_tree_rep<Element, MeasurePolicy> {
    using tree_type = measured_tree<Element, MeasurePolicy>;
    using child_type = measured_element<Element, MeasurePolicy>;
    using measure_type = typename MeasurePolicy::measure_type;

    explicit single_measured_tree_rep(child_type element)
        : element(std::move(element))
    {
    }

    child_type element;

    [[nodiscard]] measured_tree_kind kind() const noexcept override
    {
        return measured_tree_kind::single;
    }

    [[nodiscard]] bool is_empty() const noexcept override
    {
        return false;
    }

    [[nodiscard]] measure_type measure() const override
    {
        return element.measure();
    }

    [[nodiscard]] const child_type& first_element() const override
    {
        return element;
    }

    [[nodiscard]] const child_type& last_element() const override
    {
        return element;
    }

    [[nodiscard]] tree_type cons(child_type value) const override
    {
        auto prefix = measured_buffer<Element, MeasurePolicy>{};
        prefix.push_back(std::move(value));
        auto suffix = measured_buffer<Element, MeasurePolicy>{};
        suffix.push_back(element);
        return tree_type::deep_computed(std::move(prefix), tree_type::empty(), std::move(suffix));
    }

    [[nodiscard]] tree_type snoc(child_type value) const override
    {
        auto prefix = measured_buffer<Element, MeasurePolicy>{};
        prefix.push_back(element);
        auto suffix = measured_buffer<Element, MeasurePolicy>{};
        suffix.push_back(std::move(value));
        return tree_type::deep_computed(std::move(prefix), tree_type::empty(), std::move(suffix));
    }

    [[nodiscard]] std::optional<measured_view_result<Element, MeasurePolicy>> try_view_left() const override
    {
        return measured_view_result<Element, MeasurePolicy>{element, tree_type::empty()};
    }

    [[nodiscard]] std::optional<measured_view_result<Element, MeasurePolicy>> try_view_right() const override
    {
        return measured_view_result<Element, MeasurePolicy>{element, tree_type::empty()};
    }

    void flatten(std::vector<Element>& sink) const override
    {
        element.flatten(sink);
    }
};

template <class Element, class MeasurePolicy>
struct deep_measured_tree_rep final : measured_tree_rep<Element, MeasurePolicy> {
    using tree_type = measured_tree<Element, MeasurePolicy>;
    using child_type = measured_element<Element, MeasurePolicy>;
    using measure_type = typename MeasurePolicy::measure_type;
    using buffer_type = measured_buffer<Element, MeasurePolicy>;

    deep_measured_tree_rep(
        buffer_type prefix,
        measured_lazy_cell<tree_type> middle,
        buffer_type suffix)
        : prefix(std::move(prefix))
        , middle(std::move(middle))
        , suffix(std::move(suffix))
    {
        if (this->prefix.empty() || this->prefix.size() > 4) {
            throw std::logic_error("deep measured prefix length must be in 1..4");
        }

        if (this->suffix.empty() || this->suffix.size() > 4) {
            throw std::logic_error("deep measured suffix length must be in 1..4");
        }
    }

    buffer_type prefix;
    measured_lazy_cell<tree_type> middle;
    buffer_type suffix;
    atomic_box<measure_type> measure_box;

    [[nodiscard]] measured_tree_kind kind() const noexcept override
    {
        return measured_tree_kind::deep;
    }

    [[nodiscard]] bool is_empty() const noexcept override
    {
        return false;
    }

    [[nodiscard]] measure_type measure() const override
    {
        return *measure_box.get_or_compute([this] {
            return MeasurePolicy::combine(
                MeasurePolicy::combine(measured_buffer_measure<Element, MeasurePolicy>(prefix), middle.measure()),
                measured_buffer_measure<Element, MeasurePolicy>(suffix));
        });
    }

    [[nodiscard]] const child_type& first_element() const override
    {
        return prefix.front();
    }

    [[nodiscard]] const child_type& last_element() const override
    {
        return suffix.back();
    }

    [[nodiscard]] tree_type force_middle() const
    {
        return *middle.force();
    }

    [[nodiscard]] tree_type cons(child_type value) const override
    {
        if (prefix.size() < 4) {
            auto next_prefix = prefix;
            next_prefix.insert(next_prefix.begin(), std::move(value));
            return tree_type::deep(std::move(next_prefix), middle, suffix);
        }

        auto pushed = child_type::node3(prefix[1], prefix[2], prefix[3]);
        auto forced_middle = force_middle();
        auto next_prefix = buffer_type{};
        next_prefix.reserve(2);
        next_prefix.push_back(std::move(value));
        next_prefix.push_back(prefix[0]);

        // The middle's measure is read inside the probe, not eagerly here:
        // constructing a deep node must not run the user combine chain (or
        // surface its exceptions) unless a measure is actually queried,
        // matching the C# PendingMeasuredPushFront contract.
        auto suspended = measured_lazy_cell<tree_type>::defer(
            [forced_middle, pushed] {
                return forced_middle.cons(pushed);
            },
            [forced_middle, pushed_measure = pushed.measure()] {
                return std::optional<measure_type>{
                    MeasurePolicy::combine(pushed_measure, forced_middle.measure())};
            });

        return tree_type::deep(std::move(next_prefix), std::move(suspended), suffix);
    }

    [[nodiscard]] tree_type snoc(child_type value) const override
    {
        if (suffix.size() < 4) {
            auto next_suffix = suffix;
            next_suffix.push_back(std::move(value));
            return tree_type::deep(prefix, middle, std::move(next_suffix));
        }

        auto pushed = child_type::node3(suffix[0], suffix[1], suffix[2]);
        auto forced_middle = force_middle();
        auto next_suffix = buffer_type{};
        next_suffix.reserve(2);
        next_suffix.push_back(suffix[3]);
        next_suffix.push_back(std::move(value));

        // See cons: the middle's measure is deferred into the probe.
        auto suspended = measured_lazy_cell<tree_type>::defer(
            [forced_middle, pushed] {
                return forced_middle.snoc(pushed);
            },
            [forced_middle, pushed_measure = pushed.measure()] {
                return std::optional<measure_type>{
                    MeasurePolicy::combine(forced_middle.measure(), pushed_measure)};
            });

        return tree_type::deep(prefix, std::move(suspended), std::move(next_suffix));
    }

    [[nodiscard]] std::optional<measured_view_result<Element, MeasurePolicy>> try_view_left() const override
    {
        auto head = prefix.front();
        if (prefix.size() > 1) {
            return measured_view_result<Element, MeasurePolicy>{
                head,
                tree_type::deep(measured_slice(prefix, 1, prefix.size()), middle, suffix)};
        }

        auto forced_middle = force_middle();
        if (forced_middle.is_empty()) {
            return measured_view_result<Element, MeasurePolicy>{head, measured_from_buffer(suffix)};
        }

        auto node = forced_middle.first_element();
        auto suspended = measured_lazy_cell<tree_type>::defer_force_only(
            [forced_middle = std::move(forced_middle)] {
                return forced_middle.try_view_left()->rest;
            });

        return measured_view_result<Element, MeasurePolicy>{
            head,
            tree_type::deep(node.node_value().children(), std::move(suspended), suffix)};
    }

    [[nodiscard]] std::optional<measured_view_result<Element, MeasurePolicy>> try_view_right() const override
    {
        auto tail = suffix.back();
        if (suffix.size() > 1) {
            return measured_view_result<Element, MeasurePolicy>{
                tail,
                tree_type::deep(prefix, middle, measured_slice(suffix, 0, suffix.size() - 1))};
        }

        auto forced_middle = force_middle();
        if (forced_middle.is_empty()) {
            return measured_view_result<Element, MeasurePolicy>{tail, measured_from_buffer(prefix)};
        }

        auto node = forced_middle.last_element();
        auto suspended = measured_lazy_cell<tree_type>::defer_force_only(
            [forced_middle = std::move(forced_middle)] {
                return forced_middle.try_view_right()->rest;
            });

        return measured_view_result<Element, MeasurePolicy>{
            tail,
            tree_type::deep(prefix, std::move(suspended), node.node_value().children())};
    }

    void flatten(std::vector<Element>& sink) const override
    {
        for (const auto& child : prefix) {
            child.flatten(sink);
        }

        force_middle().flatten(sink);

        for (const auto& child : suffix) {
            child.flatten(sink);
        }
    }

    template <class Predicate>
    [[nodiscard]] measured_split_result<Element, MeasurePolicy> split_tree(
        Predicate& predicate,
        measure_type accumulator) const
    {
        auto before_middle = MeasurePolicy::combine(accumulator, measured_buffer_measure<Element, MeasurePolicy>(prefix));
        if (std::invoke(predicate, before_middle)) {
            auto split = measured_split_buffer<Element, MeasurePolicy>(predicate, std::move(accumulator), prefix);
            return measured_split_result<Element, MeasurePolicy>{
                measured_from_buffer(split.before),
                split.hit,
                measured_deep_left(std::move(split.after), force_middle(), suffix)};
        }

        auto forced_middle = force_middle();
        auto after_middle = MeasurePolicy::combine(before_middle, forced_middle.measure());
        if (std::invoke(predicate, after_middle)) {
            auto middle_split = forced_middle.split_tree(predicate, before_middle);
            auto before_node = MeasurePolicy::combine(before_middle, middle_split.left.measure());
            auto node_split = measured_split_buffer<Element, MeasurePolicy>(
                predicate,
                std::move(before_node),
                middle_split.hit.node_value().children());

            return measured_split_result<Element, MeasurePolicy>{
                measured_deep_right(prefix, middle_split.left, std::move(node_split.before)),
                node_split.hit,
                measured_deep_left(std::move(node_split.after), middle_split.right, suffix)};
        }

        auto suffix_split = measured_split_buffer<Element, MeasurePolicy>(
            predicate,
            std::move(after_middle),
            suffix);
        return measured_split_result<Element, MeasurePolicy>{
            measured_deep_right(prefix, forced_middle, std::move(suffix_split.before)),
            suffix_split.hit,
            measured_from_buffer(suffix_split.after)};
    }

    template <class Predicate>
    [[nodiscard]] measured_locate_result<Element, MeasurePolicy> locate_tree(
        Predicate& predicate,
        measure_type accumulator) const
    {
        auto before_middle = MeasurePolicy::combine(accumulator, measured_buffer_measure<Element, MeasurePolicy>(prefix));
        if (std::invoke(predicate, before_middle)) {
            return measured_locate_buffer<Element, MeasurePolicy>(predicate, std::move(accumulator), prefix);
        }

        auto forced_middle = force_middle();
        auto after_middle = MeasurePolicy::combine(before_middle, forced_middle.measure());
        if (std::invoke(predicate, after_middle)) {
            auto located_node = forced_middle.locate_tree(predicate, before_middle);
            return measured_locate_buffer<Element, MeasurePolicy>(
                predicate,
                std::move(located_node.measure_before),
                located_node.hit.node_value().children());
        }

        return measured_locate_buffer<Element, MeasurePolicy>(predicate, std::move(after_middle), suffix);
    }

    template <class Predicate>
    [[nodiscard]] measured_locate_reference_result<Element, MeasurePolicy> locate_tree_reference(
        Predicate& predicate,
        measure_type accumulator) const
    {
        auto before_middle = MeasurePolicy::combine(accumulator, measured_buffer_measure<Element, MeasurePolicy>(prefix));
        if (std::invoke(predicate, before_middle)) {
            return measured_locate_buffer_reference<Element, MeasurePolicy>(
                predicate,
                std::move(accumulator),
                prefix);
        }

        auto forced_middle = force_middle();
        auto after_middle = MeasurePolicy::combine(before_middle, forced_middle.measure());
        if (std::invoke(predicate, after_middle)) {
            auto located_node = forced_middle.locate_tree_reference(predicate, before_middle);
            return measured_locate_buffer_reference<Element, MeasurePolicy>(
                predicate,
                std::move(located_node.measure_before),
                located_node.hit->node_value().children());
        }

        return measured_locate_buffer_reference<Element, MeasurePolicy>(
            predicate,
            std::move(after_middle),
            suffix);
    }
};

template <class Element, class MeasurePolicy>
[[nodiscard]] std::shared_ptr<const measured_tree_rep<Element, MeasurePolicy>> empty_measured_tree_rep_instance()
{
    static const auto instance = std::make_shared<const empty_measured_tree_rep<Element, MeasurePolicy>>();
    return instance;
}

template <class Element, class MeasurePolicy>
measured_tree<Element, MeasurePolicy>::measured_tree()
    : rep_(empty_measured_tree_rep_instance<Element, MeasurePolicy>())
{
}

template <class Element, class MeasurePolicy>
measured_tree<Element, MeasurePolicy>::~measured_tree() = default;

template <class Element, class MeasurePolicy>
measured_tree<Element, MeasurePolicy> measured_tree<Element, MeasurePolicy>::empty()
{
    return measured_tree{empty_measured_tree_rep_instance<Element, MeasurePolicy>()};
}

template <class Element, class MeasurePolicy>
measured_tree<Element, MeasurePolicy> measured_tree<Element, MeasurePolicy>::single(child_type element)
{
    return measured_tree{std::make_shared<const single_measured_tree_rep<Element, MeasurePolicy>>(std::move(element))};
}

template <class Element, class MeasurePolicy>
measured_tree<Element, MeasurePolicy> measured_tree<Element, MeasurePolicy>::deep(
    buffer_type prefix,
    measured_lazy_cell<measured_tree> middle,
    buffer_type suffix)
{
    return measured_tree{
        std::make_shared<const deep_measured_tree_rep<Element, MeasurePolicy>>(
            std::move(prefix),
            std::move(middle),
            std::move(suffix))};
}

template <class Element, class MeasurePolicy>
measured_tree_kind measured_tree<Element, MeasurePolicy>::kind() const noexcept
{
    return rep_->kind();
}

template <class Element, class MeasurePolicy>
bool measured_tree<Element, MeasurePolicy>::is_empty() const noexcept
{
    return rep_->is_empty();
}

template <class Element, class MeasurePolicy>
typename measured_tree<Element, MeasurePolicy>::measure_type measured_tree<Element, MeasurePolicy>::measure() const
{
    return rep_->measure();
}

template <class Element, class MeasurePolicy>
const typename measured_tree<Element, MeasurePolicy>::child_type&
measured_tree<Element, MeasurePolicy>::first_element() const
{
    return rep_->first_element();
}

template <class Element, class MeasurePolicy>
const typename measured_tree<Element, MeasurePolicy>::child_type&
measured_tree<Element, MeasurePolicy>::last_element() const
{
    return rep_->last_element();
}

template <class Element, class MeasurePolicy>
measured_tree<Element, MeasurePolicy> measured_tree<Element, MeasurePolicy>::cons(child_type value) const
{
    return rep_->cons(std::move(value));
}

template <class Element, class MeasurePolicy>
measured_tree<Element, MeasurePolicy> measured_tree<Element, MeasurePolicy>::snoc(child_type value) const
{
    return rep_->snoc(std::move(value));
}

template <class Element, class MeasurePolicy>
std::optional<measured_view_result<Element, MeasurePolicy>>
measured_tree<Element, MeasurePolicy>::try_view_left() const
{
    return rep_->try_view_left();
}

template <class Element, class MeasurePolicy>
std::optional<measured_view_result<Element, MeasurePolicy>>
measured_tree<Element, MeasurePolicy>::try_view_right() const
{
    return rep_->try_view_right();
}

template <class Element, class MeasurePolicy>
void measured_tree<Element, MeasurePolicy>::flatten(std::vector<Element>& sink) const
{
    rep_->flatten(sink);
}

template <class Element, class MeasurePolicy>
template <class Function>
void measured_tree<Element, MeasurePolicy>::for_each(Function& function) const
{
    switch (kind()) {
    case measured_tree_kind::empty:
        return;
    case measured_tree_kind::single:
        first_element().for_each(function);
        return;
    case measured_tree_kind::deep:
        for (const auto& child : deep_prefix()) {
            child.for_each(function);
        }

        force_middle().for_each(function);

        for (const auto& child : deep_suffix()) {
            child.for_each(function);
        }
        return;
    }

    throw std::logic_error("unknown measured tree kind");
}

template <class Element, class MeasurePolicy>
template <class Predicate>
measured_split_result<Element, MeasurePolicy> measured_tree<Element, MeasurePolicy>::split_tree(
    Predicate& predicate,
    measure_type accumulator) const
{
    switch (kind()) {
    case measured_tree_kind::empty:
        throw std::logic_error("split_tree on an empty measured tree");
    case measured_tree_kind::single:
        return measured_split_result<Element, MeasurePolicy>{empty(), first_element(), empty()};
    case measured_tree_kind::deep:
        return static_cast<const deep_measured_tree_rep<Element, MeasurePolicy>&>(*rep_)
            .split_tree(predicate, std::move(accumulator));
    }

    throw std::logic_error("unknown measured tree kind");
}

template <class Element, class MeasurePolicy>
template <class Predicate>
measured_locate_result<Element, MeasurePolicy> measured_tree<Element, MeasurePolicy>::locate_tree(
    Predicate& predicate,
    measure_type accumulator) const
{
    switch (kind()) {
    case measured_tree_kind::empty:
        throw std::logic_error("locate_tree on an empty measured tree");
    case measured_tree_kind::single:
        return measured_locate_result<Element, MeasurePolicy>{std::move(accumulator), first_element()};
    case measured_tree_kind::deep:
        return static_cast<const deep_measured_tree_rep<Element, MeasurePolicy>&>(*rep_)
            .locate_tree(predicate, std::move(accumulator));
    }

    throw std::logic_error("unknown measured tree kind");
}

template <class Element, class MeasurePolicy>
template <class Predicate>
measured_locate_reference_result<Element, MeasurePolicy>
measured_tree<Element, MeasurePolicy>::locate_tree_reference(
    Predicate& predicate,
    measure_type accumulator) const
{
    switch (kind()) {
    case measured_tree_kind::empty:
        throw std::logic_error("locate_tree_reference on an empty measured tree");
    case measured_tree_kind::single:
        return measured_locate_reference_result<Element, MeasurePolicy>{
            std::move(accumulator),
            &first_element()};
    case measured_tree_kind::deep:
        return static_cast<const deep_measured_tree_rep<Element, MeasurePolicy>&>(*rep_)
            .locate_tree_reference(predicate, std::move(accumulator));
    }

    throw std::logic_error("unknown measured tree kind");
}

template <class Element, class MeasurePolicy>
const typename measured_tree<Element, MeasurePolicy>::child_type&
measured_tree<Element, MeasurePolicy>::single_element() const
{
    if (kind() != measured_tree_kind::single) {
        throw std::logic_error("measured tree is not single");
    }

    return static_cast<const single_measured_tree_rep<Element, MeasurePolicy>&>(*rep_).element;
}

template <class Element, class MeasurePolicy>
const typename measured_tree<Element, MeasurePolicy>::buffer_type&
measured_tree<Element, MeasurePolicy>::deep_prefix() const
{
    if (kind() != measured_tree_kind::deep) {
        throw std::logic_error("measured tree is not deep");
    }

    return static_cast<const deep_measured_tree_rep<Element, MeasurePolicy>&>(*rep_).prefix;
}

template <class Element, class MeasurePolicy>
const typename measured_tree<Element, MeasurePolicy>::buffer_type&
measured_tree<Element, MeasurePolicy>::deep_suffix() const
{
    if (kind() != measured_tree_kind::deep) {
        throw std::logic_error("measured tree is not deep");
    }

    return static_cast<const deep_measured_tree_rep<Element, MeasurePolicy>&>(*rep_).suffix;
}

template <class Element, class MeasurePolicy>
measured_tree<Element, MeasurePolicy> measured_tree<Element, MeasurePolicy>::force_middle() const
{
    if (kind() != measured_tree_kind::deep) {
        throw std::logic_error("measured tree is not deep");
    }

    return static_cast<const deep_measured_tree_rep<Element, MeasurePolicy>&>(*rep_).force_middle();
}

template <class Element, class MeasurePolicy>
[[nodiscard]] measured_tree<Element, MeasurePolicy> measured_from_buffer(
    const measured_buffer<Element, MeasurePolicy>& values)
{
    switch (values.size()) {
    case 0:
        return measured_tree<Element, MeasurePolicy>::empty();
    case 1:
        return measured_tree<Element, MeasurePolicy>::single(values.front());
    default: {
        auto prefix = measured_buffer<Element, MeasurePolicy>{};
        prefix.push_back(values.front());
        auto suffix = measured_slice(values, 1, values.size());
        return measured_tree<Element, MeasurePolicy>::deep_computed(
            std::move(prefix),
            measured_tree<Element, MeasurePolicy>::empty(),
            std::move(suffix));
    }
    }
}

template <class Element, class MeasurePolicy>
[[nodiscard]] measured_tree<Element, MeasurePolicy> measured_deep_left(
    measured_buffer<Element, MeasurePolicy> prefix,
    measured_tree<Element, MeasurePolicy> middle,
    const measured_buffer<Element, MeasurePolicy>& suffix)
{
    if (!prefix.empty()) {
        return measured_tree<Element, MeasurePolicy>::deep_computed(std::move(prefix), std::move(middle), suffix);
    }

    if (auto view = middle.try_view_left()) {
        return measured_tree<Element, MeasurePolicy>::deep_computed(
            view->value.node_value().children(),
            std::move(view->rest),
            suffix);
    }

    return measured_from_buffer(suffix);
}

template <class Element, class MeasurePolicy>
[[nodiscard]] measured_tree<Element, MeasurePolicy> measured_deep_right(
    const measured_buffer<Element, MeasurePolicy>& prefix,
    measured_tree<Element, MeasurePolicy> middle,
    measured_buffer<Element, MeasurePolicy> suffix)
{
    if (!suffix.empty()) {
        return measured_tree<Element, MeasurePolicy>::deep_computed(prefix, std::move(middle), std::move(suffix));
    }

    if (auto view = middle.try_view_right()) {
        return measured_tree<Element, MeasurePolicy>::deep_computed(
            prefix,
            std::move(view->rest),
            view->value.node_value().children());
    }

    return measured_from_buffer(prefix);
}

template <class Element, class MeasurePolicy>
[[nodiscard]] measured_buffer<Element, MeasurePolicy> measured_nodes(
    const measured_buffer<Element, MeasurePolicy>& values)
{
    if (values.size() < 2) {
        throw std::logic_error("measured node grouping requires at least two elements");
    }

    auto nodes = measured_buffer<Element, MeasurePolicy>{};
    auto index = std::size_t{0};
    while (values.size() - index > 4) {
        nodes.push_back(measured_element<Element, MeasurePolicy>::node3(
            values[index],
            values[index + 1],
            values[index + 2]));
        index += 3;
    }

    switch (values.size() - index) {
    case 2:
        nodes.push_back(measured_element<Element, MeasurePolicy>::node2(values[index], values[index + 1]));
        break;
    case 3:
        nodes.push_back(measured_element<Element, MeasurePolicy>::node3(
            values[index],
            values[index + 1],
            values[index + 2]));
        break;
    case 4:
        nodes.push_back(measured_element<Element, MeasurePolicy>::node2(values[index], values[index + 1]));
        nodes.push_back(measured_element<Element, MeasurePolicy>::node2(values[index + 2], values[index + 3]));
        break;
    default:
        throw std::logic_error("unexpected measured node grouping remainder");
    }

    return nodes;
}

template <class Element, class MeasurePolicy>
measured_tree<Element, MeasurePolicy> measured_prepend_all(
    const measured_buffer<Element, MeasurePolicy>& values,
    measured_tree<Element, MeasurePolicy> tree)
{
    for (auto index = values.size(); index != 0; --index) {
        tree = tree.cons(values[index - 1]);
    }

    return tree;
}

template <class Element, class MeasurePolicy>
measured_tree<Element, MeasurePolicy> measured_append_all(
    measured_tree<Element, MeasurePolicy> tree,
    const measured_buffer<Element, MeasurePolicy>& values)
{
    for (const auto& value : values) {
        tree = tree.snoc(value);
    }

    return tree;
}

template <class Element, class MeasurePolicy>
measured_tree<Element, MeasurePolicy> measured_concat(
    measured_tree<Element, MeasurePolicy> left,
    measured_tree<Element, MeasurePolicy> right)
{
    return measured_concat_with_mid(std::move(left), {}, std::move(right));
}

template <class Element, class MeasurePolicy>
measured_tree<Element, MeasurePolicy> measured_concat_with_mid(
    measured_tree<Element, MeasurePolicy> left,
    const measured_buffer<Element, MeasurePolicy>& middle,
    measured_tree<Element, MeasurePolicy> right)
{
    if (left.kind() == measured_tree_kind::empty) {
        return measured_prepend_all(middle, std::move(right));
    }

    if (right.kind() == measured_tree_kind::empty) {
        return measured_append_all(std::move(left), middle);
    }

    if (left.kind() == measured_tree_kind::single) {
        return measured_prepend_all(middle, std::move(right)).cons(left.single_element());
    }

    if (right.kind() == measured_tree_kind::single) {
        return measured_append_all(std::move(left), middle).snoc(right.single_element());
    }

    auto combined = measured_buffer<Element, MeasurePolicy>{};
    const auto& left_suffix = left.deep_suffix();
    combined.insert(combined.end(), left_suffix.begin(), left_suffix.end());
    combined.insert(combined.end(), middle.begin(), middle.end());
    const auto& right_prefix = right.deep_prefix();
    combined.insert(combined.end(), right_prefix.begin(), right_prefix.end());

    return measured_tree<Element, MeasurePolicy>::deep_computed(
        left.deep_prefix(),
        measured_concat_with_mid(left.force_middle(), measured_nodes(combined), right.force_middle()),
        right.deep_suffix());
}

} // namespace tools::data_structures::finger_tree::detail
