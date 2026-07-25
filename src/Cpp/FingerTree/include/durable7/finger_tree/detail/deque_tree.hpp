#pragma once

#include <durable7/finger_tree/detail/common.hpp>
#include <durable7/finger_tree/detail/lazy_cell.hpp>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace durable7::finger_tree::detail {

template <class T>
class deque_node;

template <class T>
class deque_element;

template <class T>
class deque_digit;

template <class T>
class deque_tree;

template <class T>
struct deque_tree_rep;

template <class T>
struct deque_split_child_result;

template <class T>
struct deque_remove_result;

template <class T>
struct deque_enumeration_child;

template <class T>
[[nodiscard]] deque_tree<T> deque_from_digit(const deque_digit<T>& digit);

template <class T>
[[nodiscard]] deque_tree<T> deque_from_partial_digit(const deque_digit<T>& digit);

template <class T>
[[nodiscard]] deque_tree<T> deque_deep_left(
    const deque_digit<T>& prefix,
    deque_tree<T> middle,
    const deque_digit<T>& suffix);

template <class T>
[[nodiscard]] deque_tree<T> deque_deep_right(
    const deque_digit<T>& prefix,
    deque_tree<T> middle,
    const deque_digit<T>& suffix);

template <class T>
[[nodiscard]] deque_tree<T> deque_pull_left(deque_tree<T> middle, const deque_digit<T>& suffix);

template <class T>
[[nodiscard]] deque_tree<T> deque_pull_right(const deque_digit<T>& prefix, deque_tree<T> middle);

template <class T>
[[nodiscard]] deque_tree<T> deque_concat(deque_tree<T> left, deque_tree<T> right);

template <class T>
[[nodiscard]] deque_tree<T> deque_concat_with_carry(
    deque_tree<T> left,
    const std::vector<deque_element<T>>& carry,
    deque_tree<T> right);

template <class T>
class deque_element final {
public:
    using node_pointer = std::shared_ptr<const deque_node<T>>;

    [[nodiscard]] static deque_element leaf(T value)
    {
        return deque_element{leaf_storage{std::move(value)}};
    }

    [[nodiscard]] static deque_element node(node_pointer value)
    {
        if (value == nullptr) {
            throw std::invalid_argument("deque node element cannot hold a null node");
        }

        return deque_element{std::move(value)};
    }

    [[nodiscard]] static deque_element node2(deque_element first, deque_element second);

    [[nodiscard]] static deque_element node3(deque_element first, deque_element second, deque_element third);

    [[nodiscard]] bool is_leaf() const noexcept
    {
        return std::holds_alternative<leaf_storage>(storage_);
    }

    [[nodiscard]] bool is_node() const noexcept
    {
        return std::holds_alternative<node_pointer>(storage_);
    }

    [[nodiscard]] std::size_t size() const;

    [[nodiscard]] const T& first_leaf() const;

    [[nodiscard]] const T& last_leaf() const;

    [[nodiscard]] const T& leaf_value() const
    {
        return std::get<leaf_storage>(storage_).value;
    }

    [[nodiscard]] const deque_node<T>& node_value() const
    {
        return *std::get<node_pointer>(storage_);
    }

    [[nodiscard]] node_pointer node_ptr() const
    {
        return std::get<node_pointer>(storage_);
    }

    [[nodiscard]] const T& get_leaf(std::size_t index) const;

    [[nodiscard]] deque_element set_leaf(std::size_t index, const T& value) const;

    template <class Predicate>
    [[nodiscard]] std::size_t bound_index(Predicate& predicate) const;

    void copy_leaves_to(std::vector<T>& destination) const;

    [[nodiscard]] std::size_t validate_and_count() const;

private:
    struct leaf_storage final {
        T value;
    };

    using storage_type = std::variant<leaf_storage, node_pointer>;

    explicit deque_element(storage_type storage)
        : storage_(std::move(storage))
    {
    }

    storage_type storage_;
};

template <class T>
class deque_digit final {
public:
    deque_digit() = default;

    explicit deque_digit(deque_element<T> first)
        : length_(1)
        , first_(std::move(first))
    {
    }

    deque_digit(deque_element<T> first, deque_element<T> second)
        : length_(2)
        , first_(std::move(first))
        , second_(std::move(second))
    {
    }

    deque_digit(deque_element<T> first, deque_element<T> second, deque_element<T> third)
        : length_(3)
        , first_(std::move(first))
        , second_(std::move(second))
        , third_(std::move(third))
    {
    }

    [[nodiscard]] static deque_digit empty()
    {
        return deque_digit{};
    }

    [[nodiscard]] std::size_t length() const noexcept
    {
        return length_;
    }

    [[nodiscard]] bool empty_digit() const noexcept
    {
        return length_ == 0;
    }

    [[nodiscard]] std::size_t size() const
    {
        switch (length_) {
        case 0:
            return 0;
        case 1:
            return a().size();
        case 2:
            return checked_add(a().size(), b().size());
        default:
            return checked_add(checked_add(a().size(), b().size()), c().size());
        }
    }

    [[nodiscard]] const deque_element<T>& a() const
    {
        return *first_;
    }

    [[nodiscard]] const deque_element<T>& b() const
    {
        return *second_;
    }

    [[nodiscard]] const deque_element<T>& c() const
    {
        return *third_;
    }

    [[nodiscard]] const deque_element<T>& last() const
    {
        switch (length_) {
        case 1:
            return a();
        case 2:
            return b();
        case 3:
            return c();
        default:
            throw std::logic_error("empty deque digit has no last child");
        }
    }

    [[nodiscard]] const deque_element<T>& child_at(std::size_t index) const
    {
        switch (index) {
        case 0:
            return a();
        case 1:
            return b();
        case 2:
            return c();
        default:
            throw std::out_of_range("deque digit child index is outside 0..2");
        }
    }

    [[nodiscard]] deque_digit cons(deque_element<T> child) const
    {
        if (length_ == 1) {
            return deque_digit{std::move(child), a()};
        }

        if (length_ == 2) {
            return deque_digit{std::move(child), a(), b()};
        }

        throw std::logic_error("deque digit cons requires room for one child");
    }

    [[nodiscard]] deque_digit snoc(deque_element<T> child) const
    {
        if (length_ == 1) {
            return deque_digit{a(), std::move(child)};
        }

        if (length_ == 2) {
            return deque_digit{a(), b(), std::move(child)};
        }

        throw std::logic_error("deque digit snoc requires room for one child");
    }

    [[nodiscard]] deque_digit remove_first() const
    {
        switch (length_) {
        case 1:
            return deque_digit{};
        case 2:
            return deque_digit{b()};
        case 3:
            return deque_digit{b(), c()};
        default:
            throw std::logic_error("deque digit remove_first requires a non-empty digit");
        }
    }

    [[nodiscard]] deque_digit remove_last() const
    {
        switch (length_) {
        case 1:
            return deque_digit{};
        case 2:
            return deque_digit{a()};
        case 3:
            return deque_digit{a(), b()};
        default:
            throw std::logic_error("deque digit remove_last requires a non-empty digit");
        }
    }

    [[nodiscard]] deque_digit replace_first(deque_element<T> child) const
    {
        switch (length_) {
        case 1:
            return deque_digit{std::move(child)};
        case 2:
            return deque_digit{std::move(child), b()};
        case 3:
            return deque_digit{std::move(child), b(), c()};
        default:
            throw std::logic_error("deque digit replace_first requires a non-empty digit");
        }
    }

    [[nodiscard]] deque_digit replace_last(deque_element<T> child) const
    {
        switch (length_) {
        case 1:
            return deque_digit{std::move(child)};
        case 2:
            return deque_digit{a(), std::move(child)};
        case 3:
            return deque_digit{a(), b(), std::move(child)};
        default:
            throw std::logic_error("deque digit replace_last requires a non-empty digit");
        }
    }

    [[nodiscard]] deque_digit with_child_at(std::size_t index, deque_element<T> child) const
    {
        switch (index) {
        case 0:
            return replace_first(std::move(child));
        case 1:
            if (length_ == 2) {
                return deque_digit{a(), std::move(child)};
            }

            if (length_ == 3) {
                return deque_digit{a(), std::move(child), c()};
            }

            break;
        case 2:
            if (length_ == 3) {
                return deque_digit{a(), b(), std::move(child)};
            }

            break;
        default:
            break;
        }

        throw std::out_of_range("deque digit replacement index is outside the digit length");
    }

    [[nodiscard]] deque_digit take_before(std::size_t index) const
    {
        switch (index) {
        case 0:
            return deque_digit{};
        case 1:
            return deque_digit{a()};
        case 2:
            return deque_digit{a(), b()};
        default:
            throw std::out_of_range("deque digit take_before index is outside 0..2");
        }
    }

    [[nodiscard]] deque_digit take_after(std::size_t index) const
    {
        if (index >= length_) {
            throw std::out_of_range("deque digit take_after index is outside the digit length");
        }

        const auto remaining = length_ - index - 1;
        switch (remaining) {
        case 0:
            return deque_digit{};
        case 1:
            return deque_digit{last()};
        default:
            return deque_digit{b(), c()};
        }
    }

    struct split_result final {
        deque_digit before;
        deque_element<T> hit;
        std::size_t index_in_hit;
        deque_digit after;
    };

    [[nodiscard]] split_result split(std::size_t leaf_index) const
    {
        if (leaf_index >= size()) {
            throw std::out_of_range("deque digit split index is outside the digit leaf range");
        }

        auto index = leaf_index;
        for (std::size_t position = 0; position != length_; ++position) {
            const auto& child = child_at(position);
            if (index < child.size()) {
                return split_result{take_before(position), child, index, take_after(position)};
            }

            index -= child.size();
        }

        throw std::logic_error("deque digit split failed to locate a child");
    }

private:
    std::size_t length_ = 0;
    std::optional<deque_element<T>> first_;
    std::optional<deque_element<T>> second_;
    std::optional<deque_element<T>> third_;
};

template <class T>
class deque_node final {
public:
    [[nodiscard]] static std::shared_ptr<const deque_node> make2(deque_element<T> first, deque_element<T> second)
    {
        return std::make_shared<const deque_node>(std::move(first), std::move(second));
    }

    [[nodiscard]] static std::shared_ptr<const deque_node> make3(
        deque_element<T> first,
        deque_element<T> second,
        deque_element<T> third)
    {
        return std::make_shared<const deque_node>(std::move(first), std::move(second), std::move(third));
    }

    deque_node(deque_element<T> first, deque_element<T> second)
        : child_count_(2)
        , first_(std::move(first))
        , second_(std::move(second))
        , size_(checked_add(first_.size(), second_.size()))
        , last_leaf_(std::addressof(second_.last_leaf()))
    {
    }

    deque_node(deque_element<T> first, deque_element<T> second, deque_element<T> third)
        : child_count_(3)
        , first_(std::move(first))
        , second_(std::move(second))
        , third_(std::move(third))
        , size_(checked_add(checked_add(first_.size(), second_.size()), third_->size()))
        , last_leaf_(std::addressof(third_->last_leaf()))
    {
    }

    deque_node(const deque_node&) = delete;
    deque_node& operator=(const deque_node&) = delete;
    deque_node(deque_node&&) = delete;
    deque_node& operator=(deque_node&&) = delete;

    [[nodiscard]] std::size_t child_count() const noexcept
    {
        return child_count_;
    }

    [[nodiscard]] const deque_element<T>& first_child() const
    {
        return first_;
    }

    [[nodiscard]] const deque_element<T>& second_child() const
    {
        return second_;
    }

    [[nodiscard]] const deque_element<T>& third_child() const
    {
        return *third_;
    }

    [[nodiscard]] const deque_element<T>& child_at(std::size_t index) const
    {
        switch (index) {
        case 0:
            return first_child();
        case 1:
            return second_child();
        case 2:
            if (child_count_ == 3) {
                return third_child();
            }

            break;
        default:
            break;
        }

        throw std::out_of_range("deque node child index is outside the node arity");
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return size_;
    }

    [[nodiscard]] const T& first_leaf() const
    {
        return first_.first_leaf();
    }

    [[nodiscard]] const T& last_leaf() const noexcept
    {
        return *last_leaf_;
    }

    [[nodiscard]] const T& get_leaf(std::size_t index) const
    {
        if (index >= size_) {
            throw std::out_of_range("deque node leaf index is outside the node");
        }

        if (index < first_.size()) {
            return first_.get_leaf(index);
        }

        index -= first_.size();
        if (index < second_.size()) {
            return second_.get_leaf(index);
        }

        return third_child().get_leaf(index - second_.size());
    }

    [[nodiscard]] std::shared_ptr<const deque_node> set_leaf(std::size_t index, const T& value) const
    {
        if (index >= size_) {
            throw std::out_of_range("deque node leaf index is outside the node");
        }

        if (index < first_.size()) {
            return child_count_ == 2
                ? make2(first_.set_leaf(index, value), second_)
                : make3(first_.set_leaf(index, value), second_, third_child());
        }

        index -= first_.size();
        if (index < second_.size()) {
            return child_count_ == 2
                ? make2(first_, second_.set_leaf(index, value))
                : make3(first_, second_.set_leaf(index, value), third_child());
        }

        return make3(first_, second_, third_child().set_leaf(index - second_.size(), value));
    }

    template <class Predicate>
    [[nodiscard]] std::size_t bound_index(Predicate& predicate) const
    {
        if (predicate(first_.last_leaf())) {
            return first_.bound_index(predicate);
        }

        if (predicate(second_.last_leaf())) {
            return checked_add(first_.size(), second_.bound_index(predicate));
        }

        if (child_count_ == 3 && predicate(third_child().last_leaf())) {
            return checked_add(checked_add(first_.size(), second_.size()), third_child().bound_index(predicate));
        }

        return size_;
    }

    void copy_leaves_to(std::vector<T>& destination) const
    {
        first_.copy_leaves_to(destination);
        second_.copy_leaves_to(destination);
        if (child_count_ == 3) {
            third_child().copy_leaves_to(destination);
        }
    }

    [[nodiscard]] std::size_t validate_and_count() const
    {
        auto computed = checked_add(first_.validate_and_count(), second_.validate_and_count());
        if (child_count_ == 3) {
            computed = checked_add(computed, third_child().validate_and_count());
        }

        if (computed != size_) {
            throw std::logic_error("deque node cached size disagrees with recomputed child total");
        }

        if (last_leaf_ != std::addressof(child_at(child_count_ - 1).last_leaf())) {
            throw std::logic_error("deque node cached rightmost leaf disagrees with its last child");
        }

        return computed;
    }

    [[nodiscard]] deque_digit<T> to_digit() const
    {
        return child_count_ == 2 ? deque_digit<T>{first_, second_} : deque_digit<T>{first_, second_, third_child()};
    }

    struct split_result final {
        deque_digit<T> before;
        deque_element<T> hit;
        std::size_t index_in_hit;
        deque_digit<T> after;
    };

    [[nodiscard]] split_result split(std::size_t leaf_index) const
    {
        if (leaf_index >= size_) {
            throw std::out_of_range("deque node split index is outside the node leaf range");
        }

        if (leaf_index < first_.size()) {
            return split_result{deque_digit<T>::empty(), first_, leaf_index,
                child_count_ == 2 ? deque_digit<T>{second_} : deque_digit<T>{second_, third_child()}};
        }

        leaf_index -= first_.size();
        if (leaf_index < second_.size()) {
            return split_result{deque_digit<T>{first_}, second_, leaf_index,
                child_count_ == 2 ? deque_digit<T>::empty() : deque_digit<T>{third_child()}};
        }

        return split_result{deque_digit<T>{first_, second_}, third_child(), leaf_index - second_.size(),
            deque_digit<T>::empty()};
    }

private:
    std::size_t child_count_;
    deque_element<T> first_;
    deque_element<T> second_;
    std::optional<deque_element<T>> third_;
    std::size_t size_;
    const T* last_leaf_;
};

template <class T>
deque_element<T> deque_element<T>::node2(deque_element first, deque_element second)
{
    return node(deque_node<T>::make2(std::move(first), std::move(second)));
}

template <class T>
deque_element<T> deque_element<T>::node3(deque_element first, deque_element second, deque_element third)
{
    return node(deque_node<T>::make3(std::move(first), std::move(second), std::move(third)));
}

template <class T>
std::size_t deque_element<T>::size() const
{
    return is_leaf() ? std::size_t{1} : node_value().size();
}

template <class T>
const T& deque_element<T>::first_leaf() const
{
    return is_leaf() ? leaf_value() : node_value().first_leaf();
}

template <class T>
const T& deque_element<T>::last_leaf() const
{
    return is_leaf() ? leaf_value() : node_value().last_leaf();
}

template <class T>
const T& deque_element<T>::get_leaf(const std::size_t index) const
{
    if (is_leaf()) {
        if (index != 0) {
            throw std::out_of_range("leaf element has only index zero");
        }

        return leaf_value();
    }

    return node_value().get_leaf(index);
}

template <class T>
deque_element<T> deque_element<T>::set_leaf(const std::size_t index, const T& value) const
{
    if (is_leaf()) {
        if (index != 0) {
            throw std::out_of_range("leaf element has only index zero");
        }

        return leaf(value);
    }

    return node(node_value().set_leaf(index, value));
}

template <class T>
template <class Predicate>
std::size_t deque_element<T>::bound_index(Predicate& predicate) const
{
    if (is_leaf()) {
        return predicate(leaf_value()) ? 0 : 1;
    }

    return node_value().bound_index(predicate);
}

template <class T>
void deque_element<T>::copy_leaves_to(std::vector<T>& destination) const
{
    if (is_leaf()) {
        destination.push_back(leaf_value());
        return;
    }

    node_value().copy_leaves_to(destination);
}

template <class T>
std::size_t deque_element<T>::validate_and_count() const
{
    return is_leaf() ? std::size_t{1} : node_value().validate_and_count();
}

enum class deque_tree_kind {
    empty,
    single,
    deep,
};

template <class T>
struct deque_split_child_result final {
    deque_tree<T> left;
    deque_element<T> hit;
    std::size_t index_in_hit;
    deque_tree<T> right;
};

template <class T>
struct deque_remove_result final {
    deque_element<T> removed;
    deque_tree<T> rest;
};

template <class T>
class deque_tree final {
public:
    deque_tree();
    ~deque_tree();

    deque_tree(const deque_tree&) noexcept = default;
    deque_tree(deque_tree&&) noexcept = default;
    deque_tree& operator=(const deque_tree&) noexcept = default;
    deque_tree& operator=(deque_tree&&) noexcept = default;

    [[nodiscard]] static deque_tree empty();

    [[nodiscard]] static deque_tree single(deque_element<T> element);

    [[nodiscard]] static deque_tree deep(
        deque_digit<T> prefix,
        lazy_cell<deque_tree> middle,
        deque_digit<T> suffix,
        std::size_t size);

    [[nodiscard]] static deque_tree deep_computed(
        deque_digit<T> prefix,
        deque_tree middle,
        deque_digit<T> suffix,
        std::size_t size)
    {
        return deep(std::move(prefix), lazy_cell<deque_tree>::computed(std::move(middle)), std::move(suffix), size);
    }

    [[nodiscard]] deque_tree_kind kind() const noexcept;

    [[nodiscard]] bool empty_tree() const noexcept
    {
        return size() == 0;
    }

    [[nodiscard]] std::size_t size() const noexcept;

    [[nodiscard]] const T& first_leaf() const;

    [[nodiscard]] const T& last_leaf() const;

    [[nodiscard]] const deque_element<T>& first_element() const;

    [[nodiscard]] const deque_element<T>& last_element() const;

    [[nodiscard]] deque_tree cons(deque_element<T> child) const;

    [[nodiscard]] deque_tree snoc(deque_element<T> child) const;

    [[nodiscard]] deque_remove_result<T> remove_first() const;

    [[nodiscard]] deque_remove_result<T> remove_last() const;

    [[nodiscard]] deque_tree replace_first(deque_element<T> child) const;

    [[nodiscard]] deque_tree replace_last(deque_element<T> child) const;

    [[nodiscard]] const T& get_leaf(std::size_t index) const;

    [[nodiscard]] deque_tree set_leaf(std::size_t index, const T& value) const;

    [[nodiscard]] deque_split_child_result<T> split_child(std::size_t index) const;

    template <class Predicate>
    [[nodiscard]] std::size_t bound_index(Predicate& predicate) const;

    void copy_leaves_to(std::vector<T>& destination) const;

    [[nodiscard]] std::size_t validate_and_count() const;

    [[nodiscard]] std::size_t depth() const;

    [[nodiscard]] std::size_t enumeration_child_count() const;

    [[nodiscard]] deque_enumeration_child<T> enumeration_child(std::size_t index) const;

    [[nodiscard]] const void* identity() const noexcept
    {
        return rep_.get();
    }

    [[nodiscard]] const deque_element<T>& single_element() const;

    [[nodiscard]] const deque_digit<T>& deep_prefix() const;

    [[nodiscard]] const deque_digit<T>& deep_suffix() const;

    [[nodiscard]] deque_tree deep_force_middle() const;

private:
    explicit deque_tree(std::shared_ptr<const deque_tree_rep<T>> rep)
        : rep_(std::move(rep))
    {
    }

    std::shared_ptr<const deque_tree_rep<T>> rep_;
};

template <class T>
struct deque_enumeration_child final {
    enum class child_kind {
        leaf,
        tree,
        node,
    };

    child_kind kind;
    const T* leaf = nullptr;
    deque_tree<T> tree;
    typename deque_element<T>::node_pointer node;
};

template <class T>
struct deque_tree_rep {
    virtual ~deque_tree_rep() = default;

    [[nodiscard]] virtual deque_tree_kind kind() const noexcept = 0;
    [[nodiscard]] virtual std::size_t size() const noexcept = 0;
    [[nodiscard]] virtual const T& first_leaf() const = 0;
    [[nodiscard]] virtual const T& last_leaf() const = 0;
    [[nodiscard]] virtual const deque_element<T>& first_element() const = 0;
    [[nodiscard]] virtual const deque_element<T>& last_element() const = 0;
    [[nodiscard]] virtual deque_tree<T> cons(deque_element<T> child) const = 0;
    [[nodiscard]] virtual deque_tree<T> snoc(deque_element<T> child) const = 0;
    [[nodiscard]] virtual deque_remove_result<T> remove_first() const = 0;
    [[nodiscard]] virtual deque_remove_result<T> remove_last() const = 0;
    [[nodiscard]] virtual deque_tree<T> replace_first(deque_element<T> child) const = 0;
    [[nodiscard]] virtual deque_tree<T> replace_last(deque_element<T> child) const = 0;
    [[nodiscard]] virtual const T& get_leaf(std::size_t index) const = 0;
    [[nodiscard]] virtual deque_tree<T> set_leaf(std::size_t index, const T& value) const = 0;
    [[nodiscard]] virtual deque_split_child_result<T> split_child(std::size_t index) const = 0;
    virtual void copy_leaves_to(std::vector<T>& destination) const = 0;
    [[nodiscard]] virtual std::size_t validate_and_count() const = 0;
    [[nodiscard]] virtual std::size_t depth() const = 0;
    [[nodiscard]] virtual std::size_t enumeration_child_count() const = 0;
    [[nodiscard]] virtual deque_enumeration_child<T> enumeration_child(std::size_t index) const = 0;
};

template <class T>
struct empty_deque_tree_rep final : deque_tree_rep<T> {
    [[nodiscard]] deque_tree_kind kind() const noexcept override
    {
        return deque_tree_kind::empty;
    }

    [[nodiscard]] std::size_t size() const noexcept override
    {
        return 0;
    }

    [[nodiscard]] const T& first_leaf() const override
    {
        throw std::logic_error("element access on an empty deque tree");
    }

    [[nodiscard]] const T& last_leaf() const override
    {
        throw std::logic_error("element access on an empty deque tree");
    }

    [[nodiscard]] const deque_element<T>& first_element() const override
    {
        throw std::logic_error("element access on an empty deque tree");
    }

    [[nodiscard]] const deque_element<T>& last_element() const override
    {
        throw std::logic_error("element access on an empty deque tree");
    }

    [[nodiscard]] deque_tree<T> cons(deque_element<T> child) const override
    {
        return deque_tree<T>::single(std::move(child));
    }

    [[nodiscard]] deque_tree<T> snoc(deque_element<T> child) const override
    {
        return deque_tree<T>::single(std::move(child));
    }

    [[nodiscard]] deque_remove_result<T> remove_first() const override
    {
        throw std::logic_error("remove_first on an empty deque tree");
    }

    [[nodiscard]] deque_remove_result<T> remove_last() const override
    {
        throw std::logic_error("remove_last on an empty deque tree");
    }

    [[nodiscard]] deque_tree<T> replace_first(deque_element<T>) const override
    {
        throw std::logic_error("replace_first on an empty deque tree");
    }

    [[nodiscard]] deque_tree<T> replace_last(deque_element<T>) const override
    {
        throw std::logic_error("replace_last on an empty deque tree");
    }

    [[nodiscard]] const T& get_leaf(std::size_t) const override
    {
        throw std::logic_error("get_leaf on an empty deque tree");
    }

    [[nodiscard]] deque_tree<T> set_leaf(std::size_t, const T&) const override
    {
        throw std::logic_error("set_leaf on an empty deque tree");
    }

    [[nodiscard]] deque_split_child_result<T> split_child(std::size_t) const override
    {
        throw std::logic_error("split_child on an empty deque tree");
    }

    void copy_leaves_to(std::vector<T>&) const override
    {
    }

    [[nodiscard]] std::size_t validate_and_count() const override
    {
        return 0;
    }

    [[nodiscard]] std::size_t depth() const override
    {
        return 0;
    }

    [[nodiscard]] std::size_t enumeration_child_count() const override
    {
        return 0;
    }

    [[nodiscard]] deque_enumeration_child<T> enumeration_child(std::size_t) const override
    {
        throw std::logic_error("enumeration child access on an empty deque tree");
    }
};

template <class T>
struct single_deque_tree_rep final : deque_tree_rep<T> {
    explicit single_deque_tree_rep(deque_element<T> element)
        : element(std::move(element))
    {
    }

    deque_element<T> element;

    [[nodiscard]] deque_tree_kind kind() const noexcept override
    {
        return deque_tree_kind::single;
    }

    [[nodiscard]] std::size_t size() const noexcept override
    {
        return element.size();
    }

    [[nodiscard]] const T& first_leaf() const override
    {
        return element.first_leaf();
    }

    [[nodiscard]] const T& last_leaf() const override
    {
        return element.last_leaf();
    }

    [[nodiscard]] const deque_element<T>& first_element() const override
    {
        return element;
    }

    [[nodiscard]] const deque_element<T>& last_element() const override
    {
        return element;
    }

    [[nodiscard]] deque_tree<T> cons(deque_element<T> child) const override
    {
        const auto child_size = child.size();
        return deque_tree<T>::deep_computed(
            deque_digit<T>{std::move(child)},
            deque_tree<T>::empty(),
            deque_digit<T>{element},
            checked_add(child_size, element.size()));
    }

    [[nodiscard]] deque_tree<T> snoc(deque_element<T> child) const override
    {
        const auto child_size = child.size();
        return deque_tree<T>::deep_computed(
            deque_digit<T>{element},
            deque_tree<T>::empty(),
            deque_digit<T>{std::move(child)},
            checked_add(element.size(), child_size));
    }

    [[nodiscard]] deque_remove_result<T> remove_first() const override
    {
        return deque_remove_result<T>{element, deque_tree<T>::empty()};
    }

    [[nodiscard]] deque_remove_result<T> remove_last() const override
    {
        return deque_remove_result<T>{element, deque_tree<T>::empty()};
    }

    [[nodiscard]] deque_tree<T> replace_first(deque_element<T> child) const override
    {
        return deque_tree<T>::single(std::move(child));
    }

    [[nodiscard]] deque_tree<T> replace_last(deque_element<T> child) const override
    {
        return deque_tree<T>::single(std::move(child));
    }

    [[nodiscard]] const T& get_leaf(std::size_t index) const override
    {
        return element.get_leaf(index);
    }

    [[nodiscard]] deque_tree<T> set_leaf(std::size_t index, const T& value) const override
    {
        return deque_tree<T>::single(element.set_leaf(index, value));
    }

    [[nodiscard]] deque_split_child_result<T> split_child(std::size_t index) const override
    {
        return deque_split_child_result<T>{deque_tree<T>::empty(), element, index, deque_tree<T>::empty()};
    }

    void copy_leaves_to(std::vector<T>& destination) const override
    {
        element.copy_leaves_to(destination);
    }

    [[nodiscard]] std::size_t validate_and_count() const override
    {
        return element.validate_and_count();
    }

    [[nodiscard]] std::size_t depth() const override
    {
        return 0;
    }

    [[nodiscard]] std::size_t enumeration_child_count() const override
    {
        return 1;
    }

    [[nodiscard]] deque_enumeration_child<T> enumeration_child(std::size_t index) const override
    {
        if (index != 0) {
            throw std::out_of_range("single deque tree has only one enumeration child");
        }

        if (element.is_leaf()) {
            return deque_enumeration_child<T>{
                deque_enumeration_child<T>::child_kind::leaf,
                &element.leaf_value(),
                deque_tree<T>{},
                {}};
        }

        return deque_enumeration_child<T>{
            deque_enumeration_child<T>::child_kind::node,
            nullptr,
            deque_tree<T>{},
            element.node_ptr()};
    }
};

template <class T>
struct deep_deque_tree_rep final : deque_tree_rep<T> {
    deep_deque_tree_rep(
        deque_digit<T> prefix,
        lazy_cell<deque_tree<T>> middle,
        deque_digit<T> suffix,
        std::size_t size)
        : prefix(std::move(prefix))
        , middle(std::move(middle))
        , suffix(std::move(suffix))
        , cached_size(size)
    {
        if (this->prefix.length() == 0 || this->prefix.length() > 3) {
            throw std::logic_error("deep deque prefix digit length must be in 1..3");
        }

        if (this->suffix.length() == 0 || this->suffix.length() > 3) {
            throw std::logic_error("deep deque suffix digit length must be in 1..3");
        }
    }

    deque_digit<T> prefix;
    lazy_cell<deque_tree<T>> middle;
    deque_digit<T> suffix;
    std::size_t cached_size;

    [[nodiscard]] deque_tree_kind kind() const noexcept override
    {
        return deque_tree_kind::deep;
    }

    [[nodiscard]] std::size_t size() const noexcept override
    {
        return cached_size;
    }

    [[nodiscard]] std::size_t middle_size() const
    {
        return cached_size - prefix.size() - suffix.size();
    }

    [[nodiscard]] deque_tree<T> force_middle() const
    {
        return *middle.get();
    }

    [[nodiscard]] const T& first_leaf() const override
    {
        return prefix.a().first_leaf();
    }

    [[nodiscard]] const T& last_leaf() const override
    {
        return suffix.last().last_leaf();
    }

    [[nodiscard]] const deque_element<T>& first_element() const override
    {
        return prefix.a();
    }

    [[nodiscard]] const deque_element<T>& last_element() const override
    {
        return suffix.last();
    }

    [[nodiscard]] deque_tree<T> cons(deque_element<T> child) const override
    {
        const auto child_size = child.size();
        if (prefix.length() < 3) {
            return deque_tree<T>::deep(prefix.cons(std::move(child)), middle, suffix, checked_add(cached_size, child_size));
        }

        auto forced_middle = force_middle();
        auto pushed_node = deque_element<T>::node2(prefix.b(), prefix.c());

        return deque_tree<T>::deep(
            deque_digit<T>{std::move(child), prefix.a()},
            lazy_cell<deque_tree<T>>::defer(
                [forced_middle = std::move(forced_middle), pushed_node = std::move(pushed_node)] {
                    return forced_middle.cons(pushed_node);
                }),
            suffix,
            checked_add(cached_size, child_size));
    }

    [[nodiscard]] deque_tree<T> snoc(deque_element<T> child) const override
    {
        const auto child_size = child.size();
        if (suffix.length() < 3) {
            return deque_tree<T>::deep(prefix, middle, suffix.snoc(std::move(child)), checked_add(cached_size, child_size));
        }

        auto forced_middle = force_middle();
        auto pushed_node = deque_element<T>::node2(suffix.a(), suffix.b());

        return deque_tree<T>::deep(
            prefix,
            lazy_cell<deque_tree<T>>::defer(
                [forced_middle = std::move(forced_middle), pushed_node = std::move(pushed_node)] {
                    return forced_middle.snoc(pushed_node);
                }),
            deque_digit<T>{suffix.c(), std::move(child)},
            checked_add(cached_size, child_size));
    }

    [[nodiscard]] deque_remove_result<T> remove_first() const override
    {
        auto removed = prefix.a();
        if (prefix.length() > 1) {
            return deque_remove_result<T>{
                removed,
                deque_tree<T>::deep(prefix.remove_first(), middle, suffix, cached_size - removed.size())};
        }

        if (middle_size() == 0) {
            return deque_remove_result<T>{removed, deque_from_digit(suffix)};
        }

        return deque_remove_result<T>{removed, deque_pull_left(force_middle(), suffix)};
    }

    [[nodiscard]] deque_remove_result<T> remove_last() const override
    {
        auto removed = suffix.last();
        if (suffix.length() > 1) {
            return deque_remove_result<T>{
                removed,
                deque_tree<T>::deep(prefix, middle, suffix.remove_last(), cached_size - removed.size())};
        }

        if (middle_size() == 0) {
            return deque_remove_result<T>{removed, deque_from_digit(prefix)};
        }

        return deque_remove_result<T>{removed, deque_pull_right(prefix, force_middle())};
    }

    [[nodiscard]] deque_tree<T> replace_first(deque_element<T> child) const override
    {
        const auto child_size = child.size();
        return deque_tree<T>::deep(
            prefix.replace_first(std::move(child)),
            middle,
            suffix,
            checked_add(cached_size - prefix.a().size(), child_size));
    }

    [[nodiscard]] deque_tree<T> replace_last(deque_element<T> child) const override
    {
        const auto child_size = child.size();
        return deque_tree<T>::deep(
            prefix,
            middle,
            suffix.replace_last(std::move(child)),
            checked_add(cached_size - suffix.last().size(), child_size));
    }

    [[nodiscard]] const T& get_leaf(std::size_t index) const override
    {
        if (index >= cached_size) {
            throw std::out_of_range("deque tree leaf index is outside the tree");
        }

        auto prefix_size = prefix.size();
        if (index < prefix_size) {
            return get_leaf_from_digit(prefix, index);
        }

        index -= prefix_size;
        auto middle_leaf_size = middle_size();
        if (index < middle_leaf_size) {
            return force_middle().get_leaf(index);
        }

        return get_leaf_from_digit(suffix, index - middle_leaf_size);
    }

    [[nodiscard]] deque_tree<T> set_leaf(std::size_t index, const T& value) const override
    {
        if (index >= cached_size) {
            throw std::out_of_range("deque tree leaf index is outside the tree");
        }

        auto prefix_size = prefix.size();
        if (index < prefix_size) {
            return set_leaf_in_digit(prefix, index, value, true);
        }

        index -= prefix_size;
        auto middle_leaf_size = middle_size();
        if (index < middle_leaf_size) {
            return deque_tree<T>::deep_computed(prefix, force_middle().set_leaf(index, value), suffix, cached_size);
        }

        return set_leaf_in_digit(suffix, index - middle_leaf_size, value, false);
    }

    [[nodiscard]] deque_split_child_result<T> split_child(std::size_t index) const override
    {
        if (index >= cached_size) {
            throw std::out_of_range("deque tree split index is outside the tree");
        }

        auto prefix_size = prefix.size();
        if (index < prefix_size) {
            auto split = prefix.split(index);
            return deque_split_child_result<T>{
                deque_from_partial_digit(split.before),
                split.hit,
                split.index_in_hit,
                deque_deep_left(split.after, force_middle(), suffix)};
        }

        index -= prefix_size;
        auto middle_leaf_size = middle_size();
        if (index < middle_leaf_size) {
            auto middle_split = force_middle().split_child(index);
            auto node_split = middle_split.hit.node_value().split(middle_split.index_in_hit);
            return deque_split_child_result<T>{
                deque_deep_right(prefix, middle_split.left, node_split.before),
                node_split.hit,
                node_split.index_in_hit,
                deque_deep_left(node_split.after, middle_split.right, suffix)};
        }

        auto split = suffix.split(index - middle_leaf_size);
        return deque_split_child_result<T>{
            deque_deep_right(prefix, force_middle(), split.before),
            split.hit,
            split.index_in_hit,
            deque_from_partial_digit(split.after)};
    }

    template <class Predicate>
    [[nodiscard]] std::size_t bound_index(Predicate& predicate) const
    {
        auto accumulated = std::size_t{0};
        for (std::size_t position = 0; position != prefix.length(); ++position) {
            const auto& child = prefix.child_at(position);
            if (predicate(child.last_leaf())) {
                return checked_add(accumulated, child.bound_index(predicate));
            }

            accumulated = checked_add(accumulated, child.size());
        }

        if (middle_size() > 0) {
            auto middle_tree = force_middle();
            if (predicate(middle_tree.last_leaf())) {
                return checked_add(accumulated, middle_tree.bound_index(predicate));
            }

            accumulated = checked_add(accumulated, middle_tree.size());
        }

        for (std::size_t position = 0; position != suffix.length(); ++position) {
            const auto& child = suffix.child_at(position);
            if (predicate(child.last_leaf())) {
                return checked_add(accumulated, child.bound_index(predicate));
            }

            accumulated = checked_add(accumulated, child.size());
        }

        return accumulated;
    }

    void copy_leaves_to(std::vector<T>& destination) const override
    {
        for (std::size_t position = 0; position != prefix.length(); ++position) {
            prefix.child_at(position).copy_leaves_to(destination);
        }

        force_middle().copy_leaves_to(destination);

        for (std::size_t position = 0; position != suffix.length(); ++position) {
            suffix.child_at(position).copy_leaves_to(destination);
        }
    }

    [[nodiscard]] std::size_t validate_and_count() const override
    {
        if (prefix.length() == 0 || prefix.length() > 3) {
            throw std::logic_error("deep deque prefix digit length is outside 1..3");
        }

        if (suffix.length() == 0 || suffix.length() > 3) {
            throw std::logic_error("deep deque suffix digit length is outside 1..3");
        }

        auto computed = std::size_t{0};
        for (std::size_t position = 0; position != prefix.length(); ++position) {
            computed = checked_add(computed, prefix.child_at(position).validate_and_count());
        }

        computed = checked_add(computed, force_middle().validate_and_count());

        for (std::size_t position = 0; position != suffix.length(); ++position) {
            computed = checked_add(computed, suffix.child_at(position).validate_and_count());
        }

        if (computed != cached_size) {
            throw std::logic_error("deep deque cached size disagrees with recomputed leaf total");
        }

        return computed;
    }

    [[nodiscard]] std::size_t depth() const override
    {
        return 1 + force_middle().depth();
    }

    [[nodiscard]] std::size_t enumeration_child_count() const override
    {
        return checked_add(checked_add(prefix.length(), std::size_t{1}), suffix.length());
    }

    [[nodiscard]] deque_enumeration_child<T> enumeration_child(std::size_t index) const override
    {
        if (index < prefix.length()) {
            return classify(prefix.child_at(index));
        }

        if (index == prefix.length()) {
            return deque_enumeration_child<T>{
                deque_enumeration_child<T>::child_kind::tree,
                nullptr,
                force_middle(),
                {}};
        }

        return classify(suffix.child_at(index - prefix.length() - 1));
    }

private:
    [[nodiscard]] static const T& get_leaf_from_digit(const deque_digit<T>& digit, std::size_t index)
    {
        for (std::size_t position = 0; position != digit.length(); ++position) {
            const auto& child = digit.child_at(position);
            if (index < child.size()) {
                return child.get_leaf(index);
            }

            index -= child.size();
        }

        throw std::logic_error("digit leaf lookup failed to locate a child");
    }

    [[nodiscard]] deque_tree<T> set_leaf_in_digit(
        const deque_digit<T>& digit,
        std::size_t index,
        const T& value,
        const bool in_prefix) const
    {
        for (std::size_t position = 0; position != digit.length(); ++position) {
            const auto& child = digit.child_at(position);
            if (index < child.size()) {
                auto updated = digit.with_child_at(position, child.set_leaf(index, value));
                return in_prefix ? deque_tree<T>::deep(updated, middle, suffix, cached_size)
                                 : deque_tree<T>::deep(prefix, middle, updated, cached_size);
            }

            index -= child.size();
        }

        throw std::logic_error("digit leaf update failed to locate a child");
    }

    [[nodiscard]] static deque_enumeration_child<T> classify(const deque_element<T>& element)
    {
        if (element.is_leaf()) {
            return deque_enumeration_child<T>{
                deque_enumeration_child<T>::child_kind::leaf,
                &element.leaf_value(),
                deque_tree<T>{},
                {}};
        }

        return deque_enumeration_child<T>{
            deque_enumeration_child<T>::child_kind::node,
            nullptr,
            deque_tree<T>{},
            element.node_ptr()};
    }
};

template <class T>
[[nodiscard]] std::shared_ptr<const deque_tree_rep<T>> empty_deque_tree_rep_instance()
{
    static const auto instance = std::make_shared<const empty_deque_tree_rep<T>>();
    return instance;
}

template <class T>
deque_tree<T>::deque_tree()
    : rep_(empty_deque_tree_rep_instance<T>())
{
}

template <class T>
deque_tree<T>::~deque_tree() = default;

template <class T>
deque_tree<T> deque_tree<T>::empty()
{
    return deque_tree{empty_deque_tree_rep_instance<T>()};
}

template <class T>
deque_tree<T> deque_tree<T>::single(deque_element<T> element)
{
    return deque_tree{std::make_shared<const single_deque_tree_rep<T>>(std::move(element))};
}

template <class T>
deque_tree<T> deque_tree<T>::deep(
    deque_digit<T> prefix,
    lazy_cell<deque_tree> middle,
    deque_digit<T> suffix,
    const std::size_t size)
{
    return deque_tree{
        std::make_shared<const deep_deque_tree_rep<T>>(std::move(prefix), std::move(middle), std::move(suffix), size)};
}

template <class T>
deque_tree_kind deque_tree<T>::kind() const noexcept
{
    return rep_->kind();
}

template <class T>
std::size_t deque_tree<T>::size() const noexcept
{
    return rep_->size();
}

template <class T>
const T& deque_tree<T>::first_leaf() const
{
    return rep_->first_leaf();
}

template <class T>
const T& deque_tree<T>::last_leaf() const
{
    return rep_->last_leaf();
}

template <class T>
const deque_element<T>& deque_tree<T>::first_element() const
{
    return rep_->first_element();
}

template <class T>
const deque_element<T>& deque_tree<T>::last_element() const
{
    return rep_->last_element();
}

template <class T>
deque_tree<T> deque_tree<T>::cons(deque_element<T> child) const
{
    return rep_->cons(std::move(child));
}

template <class T>
deque_tree<T> deque_tree<T>::snoc(deque_element<T> child) const
{
    return rep_->snoc(std::move(child));
}

template <class T>
deque_remove_result<T> deque_tree<T>::remove_first() const
{
    return rep_->remove_first();
}

template <class T>
deque_remove_result<T> deque_tree<T>::remove_last() const
{
    return rep_->remove_last();
}

template <class T>
deque_tree<T> deque_tree<T>::replace_first(deque_element<T> child) const
{
    return rep_->replace_first(std::move(child));
}

template <class T>
deque_tree<T> deque_tree<T>::replace_last(deque_element<T> child) const
{
    return rep_->replace_last(std::move(child));
}

template <class T>
const T& deque_tree<T>::get_leaf(const std::size_t index) const
{
    return rep_->get_leaf(index);
}

template <class T>
deque_tree<T> deque_tree<T>::set_leaf(const std::size_t index, const T& value) const
{
    return rep_->set_leaf(index, value);
}

template <class T>
deque_split_child_result<T> deque_tree<T>::split_child(const std::size_t index) const
{
    return rep_->split_child(index);
}

template <class T>
template <class Predicate>
std::size_t deque_tree<T>::bound_index(Predicate& predicate) const
{
    switch (kind()) {
    case deque_tree_kind::empty:
        return 0;
    case deque_tree_kind::single:
        return predicate(first_element().last_leaf()) ? first_element().bound_index(predicate) : size();
    case deque_tree_kind::deep:
        return static_cast<const deep_deque_tree_rep<T>&>(*rep_).bound_index(predicate);
    }

    throw std::logic_error("unknown deque tree kind");
}

template <class T>
void deque_tree<T>::copy_leaves_to(std::vector<T>& destination) const
{
    rep_->copy_leaves_to(destination);
}

template <class T>
std::size_t deque_tree<T>::validate_and_count() const
{
    return rep_->validate_and_count();
}

template <class T>
std::size_t deque_tree<T>::depth() const
{
    return rep_->depth();
}

template <class T>
std::size_t deque_tree<T>::enumeration_child_count() const
{
    return rep_->enumeration_child_count();
}

template <class T>
deque_enumeration_child<T> deque_tree<T>::enumeration_child(const std::size_t index) const
{
    return rep_->enumeration_child(index);
}

template <class T>
const deque_element<T>& deque_tree<T>::single_element() const
{
    if (kind() != deque_tree_kind::single) {
        throw std::logic_error("deque tree is not single");
    }

    return static_cast<const single_deque_tree_rep<T>&>(*rep_).element;
}

template <class T>
const deque_digit<T>& deque_tree<T>::deep_prefix() const
{
    if (kind() != deque_tree_kind::deep) {
        throw std::logic_error("deque tree is not deep");
    }

    return static_cast<const deep_deque_tree_rep<T>&>(*rep_).prefix;
}

template <class T>
const deque_digit<T>& deque_tree<T>::deep_suffix() const
{
    if (kind() != deque_tree_kind::deep) {
        throw std::logic_error("deque tree is not deep");
    }

    return static_cast<const deep_deque_tree_rep<T>&>(*rep_).suffix;
}

template <class T>
deque_tree<T> deque_tree<T>::deep_force_middle() const
{
    if (kind() != deque_tree_kind::deep) {
        throw std::logic_error("deque tree is not deep");
    }

    return static_cast<const deep_deque_tree_rep<T>&>(*rep_).force_middle();
}

template <class T>
deque_tree<T> deque_from_digit(const deque_digit<T>& digit)
{
    switch (digit.length()) {
    case 1:
        return deque_tree<T>::single(digit.a());
    case 2:
        return deque_tree<T>::deep_computed(
            deque_digit<T>{digit.a()}, deque_tree<T>::empty(), deque_digit<T>{digit.b()}, digit.size());
    case 3:
        return deque_tree<T>::deep_computed(
            deque_digit<T>{digit.a()}, deque_tree<T>::empty(), deque_digit<T>{digit.b(), digit.c()}, digit.size());
    default:
        throw std::logic_error("deque_from_digit requires one through three children");
    }
}

template <class T>
deque_tree<T> deque_from_partial_digit(const deque_digit<T>& digit)
{
    return digit.length() == 0 ? deque_tree<T>::empty() : deque_from_digit(digit);
}

template <class T>
deque_tree<T> deque_deep_left(const deque_digit<T>& prefix, deque_tree<T> middle, const deque_digit<T>& suffix)
{
    if (prefix.length() > 0) {
        const auto total_size = checked_add(checked_add(prefix.size(), middle.size()), suffix.size());
        return deque_tree<T>::deep_computed(
            prefix,
            std::move(middle),
            suffix,
            total_size);
    }

    return middle.size() == 0 ? deque_from_digit(suffix) : deque_pull_left(std::move(middle), suffix);
}

template <class T>
deque_tree<T> deque_deep_right(const deque_digit<T>& prefix, deque_tree<T> middle, const deque_digit<T>& suffix)
{
    if (suffix.length() > 0) {
        const auto total_size = checked_add(checked_add(prefix.size(), middle.size()), suffix.size());
        return deque_tree<T>::deep_computed(
            prefix,
            std::move(middle),
            suffix,
            total_size);
    }

    return middle.size() == 0 ? deque_from_digit(prefix) : deque_pull_right(prefix, std::move(middle));
}

template <class T>
deque_tree<T> deque_pull_left(deque_tree<T> middle, const deque_digit<T>& suffix)
{
    if (middle.size() == 0 || !middle.first_element().is_node()) {
        throw std::logic_error("deque_pull_left requires a non-empty middle tree of nodes");
    }

    const auto& head = middle.first_element().node_value();
    if (head.child_count() == 3) {
        return deque_tree<T>::deep_computed(
            deque_digit<T>{head.first_child()},
            middle.replace_first(deque_element<T>::node2(head.second_child(), head.third_child())),
            suffix,
            checked_add(middle.size(), suffix.size()));
    }

    const auto total_size = checked_add(middle.size(), suffix.size());
    return deque_tree<T>::deep(
        head.to_digit(),
        lazy_cell<deque_tree<T>>::defer([middle = std::move(middle)] { return middle.remove_first().rest; }),
        suffix,
        total_size);
}

template <class T>
deque_tree<T> deque_pull_right(const deque_digit<T>& prefix, deque_tree<T> middle)
{
    if (middle.size() == 0 || !middle.last_element().is_node()) {
        throw std::logic_error("deque_pull_right requires a non-empty middle tree of nodes");
    }

    const auto& tail = middle.last_element().node_value();
    if (tail.child_count() == 3) {
        return deque_tree<T>::deep_computed(
            prefix,
            middle.replace_last(deque_element<T>::node2(tail.first_child(), tail.second_child())),
            deque_digit<T>{tail.third_child()},
            checked_add(prefix.size(), middle.size()));
    }

    const auto total_size = checked_add(prefix.size(), middle.size());
    return deque_tree<T>::deep(
        prefix,
        lazy_cell<deque_tree<T>>::defer([middle = std::move(middle)] { return middle.remove_last().rest; }),
        tail.to_digit(),
        total_size);
}

template <class T>
[[nodiscard]] std::vector<deque_element<T>> deque_to_nodes(const std::vector<deque_element<T>>& source)
{
    if (source.size() < 2 || source.size() > 9) {
        throw std::logic_error("deque node chunking is defined for two through nine elements");
    }

    auto destination = std::vector<deque_element<T>>{};
    destination.reserve(3);
    auto index = std::size_t{0};
    while (index < source.size()) {
        const auto remaining = source.size() - index;
        if (remaining == 2 || remaining == 4) {
            destination.push_back(deque_element<T>::node2(source[index], source[index + 1]));
            index += 2;
        } else {
            destination.push_back(deque_element<T>::node3(source[index], source[index + 1], source[index + 2]));
            index += 3;
        }
    }

    return destination;
}

template <class T>
deque_tree<T> deque_concat(deque_tree<T> left, deque_tree<T> right)
{
    return deque_concat_with_carry(std::move(left), {}, std::move(right));
}

template <class T>
deque_tree<T> deque_concat_with_carry(
    deque_tree<T> left,
    const std::vector<deque_element<T>>& carry,
    deque_tree<T> right)
{
    if (carry.size() > 3) {
        throw std::logic_error("deque concat carries at most three elements between levels");
    }

    if (left.kind() == deque_tree_kind::empty) {
        for (auto index = carry.size(); index != 0; --index) {
            right = right.cons(carry[index - 1]);
        }

        return right;
    }

    if (left.kind() == deque_tree_kind::single) {
        for (auto index = carry.size(); index != 0; --index) {
            right = right.cons(carry[index - 1]);
        }

        return right.cons(left.single_element());
    }

    if (right.kind() == deque_tree_kind::empty) {
        for (const auto& element : carry) {
            left = left.snoc(element);
        }

        return left;
    }

    if (right.kind() == deque_tree_kind::single) {
        for (const auto& element : carry) {
            left = left.snoc(element);
        }

        return left.snoc(right.single_element());
    }

    auto combined = std::vector<deque_element<T>>{};
    combined.reserve(9);
    auto carry_size = std::size_t{0};

    const auto& left_suffix = left.deep_suffix();
    for (std::size_t position = 0; position != left_suffix.length(); ++position) {
        combined.push_back(left_suffix.child_at(position));
    }

    for (const auto& element : carry) {
        carry_size = checked_add(carry_size, element.size());
        combined.push_back(element);
    }

    const auto& right_prefix = right.deep_prefix();
    for (std::size_t position = 0; position != right_prefix.length(); ++position) {
        combined.push_back(right_prefix.child_at(position));
    }

    auto nodes = deque_to_nodes(combined);

    return deque_tree<T>::deep_computed(
        left.deep_prefix(),
        deque_concat_with_carry(left.deep_force_middle(), nodes, right.deep_force_middle()),
        right.deep_suffix(),
        checked_add(checked_add(left.size(), carry_size), right.size()));
}

template <class T>
[[nodiscard]] std::pair<deque_tree<T>, deque_tree<T>> deque_split_tree(const deque_tree<T>& tree, const std::size_t index)
{
    if (index > tree.size()) {
        throw std::out_of_range("deque split index is outside the tree bounds");
    }

    if (index == 0) {
        return {deque_tree<T>::empty(), tree};
    }

    if (index == tree.size()) {
        return {tree, deque_tree<T>::empty()};
    }

    auto split = tree.split_child(index);
    return {split.left, split.right.cons(split.hit)};
}

} // namespace durable7::finger_tree::detail
