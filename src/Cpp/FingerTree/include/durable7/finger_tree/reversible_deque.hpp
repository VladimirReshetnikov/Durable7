/// A persistent deque whose order can be reversed in constant time.
///
/// Reversal flips an orientation flag and shares the underlying tree rather than rebuilding it, so
/// it costs the same whatever the deque's size. Every operation returns a new version and leaves
/// its inputs valid, sharing unchanged structure, so an edit copies a path rather than the whole
/// collection.

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

/// A persistent deque whose order can be reversed in constant time, by flipping an orientation flag
/// and sharing the underlying tree rather than rebuilding it.
template <class T>
class reversible_deque final {
public:
    using value_type = T;
    using size_type = std::size_t;
    using reference = const value_type&;
    using const_reference = const value_type&;

    class const_iterator;

    /// An empty deque.
    reversible_deque() = default;

    /// A deque holding the listed elements.
    reversible_deque(std::initializer_list<value_type> items)
        : root_(build_tree(items.begin(), items.end()))
    {
    }

    /// A deque holding the elements an iterator pair yields.
    template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
    reversible_deque(Iterator first, Sentinel last)
        : root_(build_tree(std::move(first), std::move(last)))
    {
    }

    /// The shared empty deque.
    [[nodiscard]] static reversible_deque empty_deque()
    {
        return reversible_deque{};
    }

    /// A deque holding a range's elements, built in bulk rather than by repeated insertion.
    template <std::ranges::input_range Range>
    [[nodiscard]] static reversible_deque from_range(Range&& items)
    {
        return reversible_deque{std::ranges::begin(items), std::ranges::end(items)};
    }

    /// Whether the deque holds no elements.
    [[nodiscard]] bool empty() const noexcept
    {
        return root_.empty_state();
    }

    /// Number of elements in the deque.
    [[nodiscard]] size_type size() const noexcept
    {
        return root_.size();
    }

    /// The root node's address, for tests that a no-op shared rather than copied.
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

    /// The last element.
    [[nodiscard]] const_reference back() const
    {
        throw_if_empty();
        return root_.last_leaf();
    }

    /// The first element, or nothing when empty.
    [[nodiscard]] const value_type* try_front() const
    {
        return empty() ? nullptr : &root_.first_leaf();
    }

    /// The last element, or nothing when empty.
    [[nodiscard]] const value_type* try_back() const
    {
        return empty() ? nullptr : &root_.last_leaf();
    }

    /// The element at the given position.
    [[nodiscard]] value_type at(const size_type index) const
    {
        throw_if_index_out_of_range(index, size());
        return root_.get_leaf(index);
    }

    [[nodiscard]] value_type operator[](const size_type index) const
    {
        return at(index);
    }

    /// A deque with the element added at the front.
    [[nodiscard]] reversible_deque push_front(value_type item) const
    {
        throw_if_full();
        return reversible_deque{root_.cons(detail::rev_element<value_type>::leaf(std::move(item)))};
    }

    /// A deque with the element added at the back.
    [[nodiscard]] reversible_deque push_back(value_type item) const
    {
        throw_if_full();
        return reversible_deque{root_.snoc(detail::rev_element<value_type>::leaf(std::move(item)))};
    }

    /// A deque without its first element.
    [[nodiscard]] reversible_deque remove_first() const;

    /// A deque without its last element.
    [[nodiscard]] reversible_deque remove_last() const;

    /// Removes the front element, or nothing when empty.
    [[nodiscard]] std::optional<reversible_deque_pop<value_type>> try_pop_front() const;

    /// Removes the back element, or nothing when empty.
    [[nodiscard]] std::optional<reversible_deque_pop<value_type>> try_pop_back() const;

    /// The concatenation of two deques, sharing both operands' unchanged structure.
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

    /// A deque with the key bound to the value, adding or replacing as needed.
    [[nodiscard]] reversible_deque set_item(const size_type index, value_type value) const
    {
        throw_if_index_out_of_range(index, size());
        return reversible_deque{root_.set_leaf(index, std::move(value))};
    }

    /// A deque with the element at the position replaced.
    [[nodiscard]] reversible_deque set_at(const size_type index, value_type value) const
    {
        return set_item(index, std::move(value));
    }

    /// A deque with the element inserted at the position.
    [[nodiscard]] reversible_deque insert_at(size_type index, value_type item) const;

    /// A deque without the element at the position.
    [[nodiscard]] reversible_deque remove_at(size_type index) const;

    /// Splits into the elements before the position and those from it onward.
    [[nodiscard]] reversible_deque_split<value_type> split_at(size_type index) const;

    /// The deque in the opposite order.
    [[nodiscard]] reversible_deque reverse() const
    {
        return reversible_deque{root_.mirror()};
    }

    /// Copies the elements out into a vector, in the deque's own order.
    [[nodiscard]] std::vector<value_type> to_vector() const
    {
        auto result = std::vector<value_type>{};
        result.reserve(size());
        root_.copy_leaves(result);
        return result;
    }

    /// Copies the elements into the destination.
    template <std::output_iterator<const value_type&> OutputIterator>
    void copy_to(OutputIterator output) const
    {
        for (const auto& value : *this) {
            *output++ = value;
        }
    }

    /// An iterator over the elements, in the deque's own order.
    [[nodiscard]] const_iterator begin() const
    {
        return const_iterator{root_};
    }

    /// The iterator one past the last element.
    [[nodiscard]] const_iterator end() const noexcept
    {
        return const_iterator{};
    }

    /// The const iterator one past the last element.
    /// A const iterator over the elements.
    [[nodiscard]] const_iterator cbegin() const { return begin(); }
    [[nodiscard]] const_iterator cend() const noexcept { return end(); }

    /// A forward const iterator over one deque version's elements. It keeps that version alive, so
    /// it stays valid across later edits.
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

        /// An unpositioned cursor, holding no version.
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

    /// Checks the deque's structural invariants. For tests and diagnostics.
    void validate_invariants() const
    {
        const auto computed = root_.validate_and_count();
        if (computed != size()) {
            throw std::logic_error("reversible_deque cached size mismatch");
        }
    }

    /// The tree's depth.
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

/// The two deques a split produced.
template <class T>
struct reversible_deque_split final {
    reversible_deque<T> left;
    reversible_deque<T> right;
};

/// An endpoint element together with the deque remaining.
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

    /// An unpositioned cursor, holding no version.
    reversible_deque_cursor() = delete;
    /// An unpositioned cursor, holding no version.
    reversible_deque_cursor(const reversible_deque_cursor&) = default;
    reversible_deque_cursor& operator=(const reversible_deque_cursor&) = default;

    /// Takes over the source's handle, leaving it empty.
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

    /// Whether the gap follows the last element.
    /// Whether the gap precedes the first element.
    /// The cursor's gap position.
    /// Whether the deque version the cursor is positioned in holds no elements.
    /// Number of elements in the deque version the cursor is positioned in.
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

    /// Reads the element immediately after the gap, or nothing at the end.
    [[nodiscard]] std::optional<value_type> try_peek_next() const
    {
        return is_at_end()
            ? std::nullopt
            : std::optional<value_type>{std::in_place, snapshot_.at(position_)};
    }

    /// A cursor one position earlier. The receiver is unchanged; movement produces a new cursor
    /// over the same version.
    [[nodiscard]] reversible_deque_cursor move_previous() const
    {
        if (is_at_start()) {
            throw std::logic_error("reversible-deque cursor is already at the start");
        }
        return reversible_deque_cursor{snapshot_, position_ - 1};
    }

    /// A cursor one position later. The receiver is unchanged.
    [[nodiscard]] reversible_deque_cursor move_next() const
    {
        if (is_at_end()) {
            throw std::logic_error("reversible-deque cursor is already at the end");
        }
        return reversible_deque_cursor{snapshot_, position_ + 1};
    }

    /// A cursor at the given position within the same deque version.
    [[nodiscard]] reversible_deque_cursor seek(const size_type position) const
    {
        if (position > snapshot_.size()) {
            throw std::out_of_range("cursor position is outside the reversible-deque bounds");
        }
        return position == position_ ? *this : reversible_deque_cursor{snapshot_, position};
    }

    /// A deque with the element inserted.
    [[nodiscard]] reversible_deque_cursor insert(value_type value) const
    {
        return reversible_deque_cursor{
            snapshot_.insert_at(position_, std::move(value)),
            checked_add(position_, size_type{1})};
    }

    /// A deque with a range's elements inserted at the position.
    template <std::ranges::input_range Range>
        requires(!std::same_as<std::remove_cvref_t<Range>, reversible_deque<value_type>>)
    [[nodiscard]] reversible_deque_cursor insert_range(Range&& values) const
    {
        auto middle = reversible_deque<value_type>::from_range(std::forward<Range>(values));
        return middle.empty() ? *this : insert_range(middle);
    }

    /// A deque with a range's elements inserted at the position.
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

    /// Removes the element before the gap, producing a new version the returned cursor is
    /// positioned in.
    [[nodiscard]] reversible_deque_cursor delete_previous() const
    {
        if (is_at_start()) {
            throw std::logic_error("reversible-deque cursor has no previous element");
        }
        return reversible_deque_cursor{snapshot_.remove_at(position_ - 1), position_ - 1};
    }

    /// Removes the element after the gap, producing a new version the returned cursor is positioned
    /// in.
    [[nodiscard]] reversible_deque_cursor delete_next() const
    {
        if (is_at_end()) {
            throw std::logic_error("reversible-deque cursor has no next element");
        }
        return reversible_deque_cursor{snapshot_.remove_at(position_), position_};
    }

    /// Replaces the element after the gap, producing a new version the returned cursor is
    /// positioned in.
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

    /// The deque version this cursor is positioned in.
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
