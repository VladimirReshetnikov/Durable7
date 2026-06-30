#pragma once

#include <tools/data_structures/finger_tree/detail/common.hpp>
#include <tools/data_structures/finger_tree/detail/reversible_tree.hpp>

#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tools::data_structures::finger_tree {

template <class T>
class reversible_deque;

template <class T>
struct reversible_deque_split;

template <class T>
struct reversible_deque_pop;

template <class T>
class reversible_deque final {
public:
    using value_type = T;
    using size_type = std::size_t;
    using reference = const value_type&;
    using const_reference = const value_type&;

    reversible_deque() = default;

    reversible_deque(std::initializer_list<value_type> items)
        : root_(build_tree(items.begin(), items.end()))
    {
    }

    template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
    reversible_deque(Iterator first, Sentinel last)
        : root_(build_tree(std::move(first), std::move(last)))
    {
    }

    [[nodiscard]] static reversible_deque empty_deque()
    {
        return reversible_deque{};
    }

    template <std::ranges::input_range Range>
    [[nodiscard]] static reversible_deque from_range(Range&& items)
    {
        return reversible_deque{std::ranges::begin(items), std::ranges::end(items)};
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return root_.empty_state();
    }

    [[nodiscard]] size_type size() const noexcept
    {
        return root_.size();
    }

    [[nodiscard]] const_reference front() const
    {
        throw_if_not_empty();
        return root_.first_leaf();
    }

    [[nodiscard]] const_reference back() const
    {
        throw_if_not_empty();
        return root_.last_leaf();
    }

    [[nodiscard]] const value_type* try_front() const
    {
        return empty() ? nullptr : &root_.first_leaf();
    }

    [[nodiscard]] const value_type* try_back() const
    {
        return empty() ? nullptr : &root_.last_leaf();
    }

    [[nodiscard]] value_type at(const size_type index) const
    {
        throw_if_index_out_of_range(index, size());
        return root_.get_leaf(index);
    }

    [[nodiscard]] value_type operator[](const size_type index) const
    {
        return at(index);
    }

    [[nodiscard]] reversible_deque push_front(value_type item) const
    {
        throw_if_full();
        return reversible_deque{root_.cons(detail::rev_element<value_type>::leaf(std::move(item)))};
    }

    [[nodiscard]] reversible_deque push_back(value_type item) const
    {
        throw_if_full();
        return reversible_deque{root_.snoc(detail::rev_element<value_type>::leaf(std::move(item)))};
    }

    [[nodiscard]] reversible_deque remove_first() const;

    [[nodiscard]] reversible_deque remove_last() const;

    [[nodiscard]] std::optional<reversible_deque_pop<value_type>> try_pop_front() const;

    [[nodiscard]] std::optional<reversible_deque_pop<value_type>> try_pop_back() const;

    [[nodiscard]] reversible_deque concat(const reversible_deque& other) const
    {
        if (other.empty()) {
            return *this;
        }

        if (empty()) {
            return other;
        }

        (void)checked_add(size(), other.size());
        return reversible_deque{detail::rev_concat(root_, other.root_)};
    }

    [[nodiscard]] reversible_deque set_item(const size_type index, value_type value) const
    {
        throw_if_index_out_of_range(index, size());
        return reversible_deque{root_.set_leaf(index, std::move(value))};
    }

    [[nodiscard]] reversible_deque set_at(const size_type index, value_type value) const
    {
        return set_item(index, std::move(value));
    }

    [[nodiscard]] reversible_deque insert_at(size_type index, value_type item) const;

    [[nodiscard]] reversible_deque remove_at(size_type index) const;

    [[nodiscard]] reversible_deque_split<value_type> split_at(size_type index) const;

    [[nodiscard]] reversible_deque reverse() const
    {
        return reversible_deque{root_.mirror()};
    }

    [[nodiscard]] std::vector<value_type> to_vector() const
    {
        auto result = std::vector<value_type>{};
        result.reserve(size());
        root_.copy_leaves(result);
        return result;
    }

    void validate_invariants() const
    {
        const auto computed = root_.validate_and_count();
        if (computed != size()) {
            throw std::logic_error("reversible_deque cached size mismatch");
        }
    }

    [[nodiscard]] size_type tree_depth() const noexcept
    {
        return root_.depth();
    }

private:
    using root_type = detail::rev_tree<value_type>;

    explicit reversible_deque(root_type root)
        : root_(std::move(root))
    {
    }

    template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
    [[nodiscard]] static root_type build_tree(Iterator first, Sentinel last)
    {
        auto root = root_type{};
        for (; first != last; ++first) {
            root = root.snoc(detail::rev_element<value_type>::leaf(*first));
        }

        return root;
    }

    void throw_if_not_empty() const
    {
        if (empty()) {
            throw std::logic_error("reversible_deque is empty");
        }
    }

    void throw_if_full() const
    {
        if (size() == (std::numeric_limits<size_type>::max)()) {
            throw std::overflow_error("reversible_deque size overflow");
        }
    }

    root_type root_;
};

template <class T>
struct reversible_deque_split final {
    reversible_deque<T> left;
    reversible_deque<T> right;
};

template <class T>
struct reversible_deque_pop final {
    T value;
    reversible_deque<T> rest;
};

template <class T>
reversible_deque<T> reversible_deque<T>::remove_first() const
{
    auto popped = try_pop_front();
    if (!popped.has_value()) {
        throw std::logic_error("reversible_deque is empty");
    }

    return popped->rest;
}

template <class T>
reversible_deque<T> reversible_deque<T>::remove_last() const
{
    auto popped = try_pop_back();
    if (!popped.has_value()) {
        throw std::logic_error("reversible_deque is empty");
    }

    return popped->rest;
}

template <class T>
std::optional<reversible_deque_pop<T>> reversible_deque<T>::try_pop_front() const
{
    if (auto view = root_.try_view_left()) {
        return reversible_deque_pop<T>{view->value.first_leaf(), reversible_deque{view->rest}};
    }

    return std::nullopt;
}

template <class T>
std::optional<reversible_deque_pop<T>> reversible_deque<T>::try_pop_back() const
{
    if (auto view = root_.try_view_right()) {
        return reversible_deque_pop<T>{view->value.last_leaf(), reversible_deque{view->rest}};
    }

    return std::nullopt;
}

template <class T>
reversible_deque<T> reversible_deque<T>::insert_at(const size_type index, value_type item) const
{
    throw_if_insert_index_out_of_range(index, size());
    if (index == 0) {
        return push_front(std::move(item));
    }

    if (index == size()) {
        return push_back(std::move(item));
    }

    throw_if_full();
    auto split = root_.split_tree(index);
    auto left = split.left.snoc(detail::rev_element<value_type>::leaf(std::move(item)));
    auto right = split.right.cons(split.hit);
    return reversible_deque{detail::rev_concat(std::move(left), std::move(right))};
}

template <class T>
reversible_deque<T> reversible_deque<T>::remove_at(const size_type index) const
{
    throw_if_index_out_of_range(index, size());
    auto split = root_.split_tree(index);
    return reversible_deque{detail::rev_concat(std::move(split.left), std::move(split.right))};
}

template <class T>
reversible_deque_split<T> reversible_deque<T>::split_at(const size_type index) const
{
    throw_if_insert_index_out_of_range(index, size());
    if (index == 0) {
        return reversible_deque_split<T>{reversible_deque{}, *this};
    }

    if (index == size()) {
        return reversible_deque_split<T>{*this, reversible_deque{}};
    }

    auto split = root_.split_tree(index);
    return reversible_deque_split<T>{
        reversible_deque{std::move(split.left)},
        reversible_deque{split.right.cons(std::move(split.hit))}};
}

} // namespace tools::data_structures::finger_tree
