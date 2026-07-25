#pragma once

#include <durable7/finger_tree/detail/common.hpp>
#include <durable7/finger_tree/detail/reversible_tree.hpp>

#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace durable7::finger_tree {

template <class T>
class reversible_deque;

template <class T>
class reversible_deque_cursor;

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

    class const_iterator;

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

    [[nodiscard]] const void* root_identity() const noexcept
    {
        return root_.identity();
    }

    /// Reports whether both snapshots name the very same root, which is how a clean operation
    /// that returned the receiver unchanged is distinguished from one that rebuilt an equal
    /// value. Two snapshots holding opposite orientations of the same elements do not share a
    /// root, because reversal installs a new root carrying the mirrored orientation.
    [[nodiscard]] bool shares_root_with(const reversible_deque& other) const noexcept
    {
        return root_.identity() == other.root_.identity();
    }

    /// Creates an immutable cursor at a logical gap in `0..size()`.
    [[nodiscard]] reversible_deque_cursor<value_type> get_cursor(size_type position = 0) const;

    [[nodiscard]] const_reference front() const
    {
        throw_if_empty();
        return root_.first_leaf();
    }

    [[nodiscard]] const_reference back() const
    {
        throw_if_empty();
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

    template <std::output_iterator<const value_type&> OutputIterator>
    void copy_to(OutputIterator output) const
    {
        for (const auto& value : *this) {
            *output++ = value;
        }
    }

    [[nodiscard]] const_iterator begin() const
    {
        return const_iterator{root_};
    }

    [[nodiscard]] const_iterator end() const noexcept
    {
        return const_iterator{};
    }

    [[nodiscard]] const_iterator cbegin() const { return begin(); }
    [[nodiscard]] const_iterator cend() const noexcept { return end(); }

    class const_iterator final {
    public:
        // The orientation-bit cursor reaches physical leaves without
        // materializing mirrored nodes, so retained references are stable and
        // copied cursors remain independent multipass traversals.
        using iterator_concept = std::forward_iterator_tag;
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        const_iterator() = default;

        [[nodiscard]] reference operator*() const
        {
            return cursor_.current();
        }

        [[nodiscard]] pointer operator->() const
        {
            return &cursor_.current();
        }

        const_iterator& operator++()
        {
            cursor_.advance();
            ++position_;
            return *this;
        }

        const_iterator operator++(int)
        {
            auto copy = *this;
            ++*this;
            return copy;
        }

        friend bool operator==(const const_iterator& left, const const_iterator& right) noexcept
        {
            if (left.cursor_.at_end() || right.cursor_.at_end()) {
                return left.cursor_.at_end() == right.cursor_.at_end();
            }

            return left.owner_ == right.owner_ && left.position_ == right.position_;
        }

    private:
        friend class reversible_deque;

        explicit const_iterator(detail::rev_tree<T> root)
            : cursor_(root)
            , owner_(root.identity())
        {
        }

        detail::rev_tree_cursor<T> cursor_;
        const void* owner_ = nullptr;
        size_type position_ = 0;
    };

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

    void throw_if_empty() const
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

/// An immutable logical-order gap cursor over a persistent reversible deque.
template <class T>
class reversible_deque_cursor final {
public:
    using value_type = T;
    using size_type = std::size_t;

    reversible_deque_cursor() = delete;
    reversible_deque_cursor(const reversible_deque_cursor&) = default;
    reversible_deque_cursor& operator=(const reversible_deque_cursor&) = default;

    reversible_deque_cursor(reversible_deque_cursor&& other) noexcept(
        std::is_nothrow_copy_constructible_v<reversible_deque<value_type>>)
        : snapshot_(other.snapshot_)
        , position_(other.position_)
    {
    }

    reversible_deque_cursor& operator=(reversible_deque_cursor&& other) noexcept(
        std::is_nothrow_copy_assignable_v<reversible_deque<value_type>>)
    {
        if (this != &other) {
            snapshot_ = other.snapshot_;
            position_ = other.position_;
        }
        return *this;
    }

    [[nodiscard]] size_type size() const noexcept { return snapshot_.size(); }
    [[nodiscard]] bool empty() const noexcept { return snapshot_.empty(); }
    [[nodiscard]] size_type position() const noexcept { return position_; }
    [[nodiscard]] bool is_at_start() const noexcept { return position_ == 0; }
    [[nodiscard]] bool is_at_end() const noexcept { return position_ == snapshot_.size(); }

    /// The outer optional reports presence, so a stored optional value remains distinguishable.
    [[nodiscard]] std::optional<value_type> try_peek_previous() const
    {
        return position_ == 0
            ? std::nullopt
            : std::optional<value_type>{std::in_place, snapshot_.at(position_ - 1)};
    }

    [[nodiscard]] std::optional<value_type> try_peek_next() const
    {
        return is_at_end()
            ? std::nullopt
            : std::optional<value_type>{std::in_place, snapshot_.at(position_)};
    }

    [[nodiscard]] reversible_deque_cursor move_previous() const
    {
        if (is_at_start()) {
            throw std::logic_error("reversible-deque cursor is already at the start");
        }
        return reversible_deque_cursor{snapshot_, position_ - 1};
    }

    [[nodiscard]] reversible_deque_cursor move_next() const
    {
        if (is_at_end()) {
            throw std::logic_error("reversible-deque cursor is already at the end");
        }
        return reversible_deque_cursor{snapshot_, position_ + 1};
    }

    [[nodiscard]] reversible_deque_cursor seek(const size_type position) const
    {
        if (position > snapshot_.size()) {
            throw std::out_of_range("cursor position is outside the reversible-deque bounds");
        }
        return position == position_ ? *this : reversible_deque_cursor{snapshot_, position};
    }

    [[nodiscard]] reversible_deque_cursor insert(value_type value) const
    {
        return reversible_deque_cursor{
            snapshot_.insert_at(position_, std::move(value)),
            checked_add(position_, size_type{1})};
    }

    template <std::ranges::input_range Range>
        requires(!std::same_as<std::remove_cvref_t<Range>, reversible_deque<value_type>>)
    [[nodiscard]] reversible_deque_cursor insert_range(Range&& values) const
    {
        auto middle = reversible_deque<value_type>::from_range(std::forward<Range>(values));
        return middle.empty() ? *this : insert_range(middle);
    }

    [[nodiscard]] reversible_deque_cursor insert_range(
        const reversible_deque<value_type>& values) const
    {
        if (values.empty()) {
            return *this;
        }
        const auto split = snapshot_.split_at(position_);
        return reversible_deque_cursor{
            split.left.concat(values).concat(split.right),
            checked_add(position_, values.size())};
    }

    [[nodiscard]] reversible_deque_cursor delete_previous() const
    {
        if (is_at_start()) {
            throw std::logic_error("reversible-deque cursor has no previous element");
        }
        return reversible_deque_cursor{snapshot_.remove_at(position_ - 1), position_ - 1};
    }

    [[nodiscard]] reversible_deque_cursor delete_next() const
    {
        if (is_at_end()) {
            throw std::logic_error("reversible-deque cursor has no next element");
        }
        return reversible_deque_cursor{snapshot_.remove_at(position_), position_};
    }

    [[nodiscard]] reversible_deque_cursor replace_next(value_type value) const
    {
        if (is_at_end()) {
            throw std::logic_error("reversible-deque cursor has no next element");
        }
        return reversible_deque_cursor{snapshot_.set_item(position_, std::move(value)), position_};
    }

    /// Reverses the logical version and maps this gap to `size() - position()`.
    [[nodiscard]] reversible_deque_cursor reverse() const
    {
        return reversible_deque_cursor{snapshot_.reverse(), snapshot_.size() - position_};
    }

    [[nodiscard]] reversible_deque<value_type> snapshot() const { return snapshot_; }

private:
    friend class reversible_deque<T>;

    reversible_deque_cursor(reversible_deque<value_type> snapshot, const size_type position)
        : snapshot_(std::move(snapshot))
        , position_(position)
    {
    }

    reversible_deque<value_type> snapshot_;
    size_type position_;
};

template <class T>
[[nodiscard]] reversible_deque_cursor<T>
reversible_deque<T>::get_cursor(const size_type position) const
{
    if (position > size()) {
        throw std::out_of_range("cursor position is outside the reversible-deque bounds");
    }
    return reversible_deque_cursor<value_type>{*this, position};
}

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

template <class T>
    requires equality_comparable_value<T>
[[nodiscard]] bool operator==(
    const reversible_deque_split<T>& left,
    const reversible_deque_split<T>& right)
{
    return detail::sequence_equal(left.left, right.left)
        && detail::sequence_equal(left.right, right.right);
}

template <class T>
    requires equality_comparable_value<T>
[[nodiscard]] bool operator==(
    const reversible_deque_pop<T>& left,
    const reversible_deque_pop<T>& right)
{
    return left.value == right.value && detail::sequence_equal(left.rest, right.rest);
}

} // namespace durable7::finger_tree
