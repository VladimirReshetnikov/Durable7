#pragma once

#include <tools/data_structures/finger_tree/detail/common.hpp>
#include <tools/data_structures/finger_tree/detail/deque_tree.hpp>

#include <compare>
#include <concepts>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace tools::data_structures::finger_tree {

template <class T>
class persistent_deque;

template <class T>
struct deque_split final {
    persistent_deque<T> left;
    persistent_deque<T> right;
};

template <class T>
struct deque_item_split final {
    persistent_deque<T> left;
    T item;
    persistent_deque<T> right;
};

template <class T>
struct deque_range_split final {
    persistent_deque<T> before;
    persistent_deque<T> range;
    persistent_deque<T> after;
};

template <class T>
struct deque_pop final {
    T value;
    persistent_deque<T> rest;
};

template <class T>
class persistent_deque final {
public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = const value_type&;
    using const_reference = const value_type&;

    class const_iterator;

    persistent_deque() = default;

    persistent_deque(std::initializer_list<T> items)
        : root_(build_tree(items.begin(), items.end(), (std::numeric_limits<size_type>::max)()))
    {
    }

    template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
    persistent_deque(Iterator first, Sentinel last)
        : root_(build_tree(std::move(first), std::move(last), (std::numeric_limits<size_type>::max)()))
    {
    }

    [[nodiscard]] static persistent_deque empty_deque()
    {
        return persistent_deque{};
    }

    template <std::ranges::input_range Range>
    [[nodiscard]] static persistent_deque from_range(Range&& items)
    {
        return persistent_deque{std::ranges::begin(items), std::ranges::end(items)};
    }

    [[nodiscard]] bool empty_state() const noexcept
    {
        return root_.size() == 0;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return empty_state();
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

    [[nodiscard]] const_reference operator[](const size_type index) const
    {
        return root_.get_leaf(index);
    }

    [[nodiscard]] const_reference at(const size_type index) const
    {
        throw_if_index_out_of_range(index, size());
        return root_.get_leaf(index);
    }

    [[nodiscard]] const value_type* try_get(const size_type index) const
    {
        return index < size() ? &root_.get_leaf(index) : nullptr;
    }

    [[nodiscard]] persistent_deque push_front(value_type item) const
    {
        return persistent_deque{root_.cons(detail::deque_element<T>::leaf(std::move(item)))};
    }

    [[nodiscard]] persistent_deque push_back(value_type item) const
    {
        return persistent_deque{root_.snoc(detail::deque_element<T>::leaf(std::move(item)))};
    }

    [[nodiscard]] persistent_deque add_first(value_type item) const
    {
        return push_front(std::move(item));
    }

    [[nodiscard]] persistent_deque add_last(value_type item) const
    {
        return push_back(std::move(item));
    }

    [[nodiscard]] persistent_deque remove_first() const
    {
        throw_if_not_empty();
        return wrap(root_.remove_first().rest);
    }

    [[nodiscard]] persistent_deque remove_last() const
    {
        throw_if_not_empty();
        return wrap(root_.remove_last().rest);
    }

    [[nodiscard]] deque_pop<T> pop_first() const
    {
        throw_if_not_empty();
        auto result = root_.remove_first();
        return deque_pop<T>{result.removed.leaf_value(), wrap(result.rest)};
    }

    [[nodiscard]] deque_pop<T> pop_last() const
    {
        throw_if_not_empty();
        auto result = root_.remove_last();
        return deque_pop<T>{result.removed.leaf_value(), wrap(result.rest)};
    }

    [[nodiscard]] std::optional<deque_pop<T>> try_pop_first() const
    {
        if (empty()) {
            return std::nullopt;
        }

        return pop_first();
    }

    [[nodiscard]] std::optional<deque_pop<T>> try_pop_last() const
    {
        if (empty()) {
            return std::nullopt;
        }

        return pop_last();
    }

    [[nodiscard]] persistent_deque set_item(const size_type index, const value_type& value) const
    {
        throw_if_index_out_of_range(index, size());
        return persistent_deque{root_.set_leaf(index, value)};
    }

    [[nodiscard]] persistent_deque set_at(const size_type index, const value_type& value) const
    {
        return set_item(index, value);
    }

    template <class Updater>
        requires std::invocable<Updater&, const value_type&>
    [[nodiscard]] persistent_deque update_at(const size_type index, Updater updater) const
    {
        throw_if_index_out_of_range(index, size());
        auto updated = std::invoke(updater, root_.get_leaf(index));
        return persistent_deque{root_.set_leaf(index, updated)};
    }

    [[nodiscard]] persistent_deque insert_at(const size_type index, value_type item) const
    {
        throw_if_insert_index_out_of_range(index, size());
        if (index == 0) {
            return push_front(std::move(item));
        }

        if (index == size()) {
            return push_back(std::move(item));
        }

        auto split = root_.split_child(index);
        auto inserted = split.left.snoc(detail::deque_element<T>::leaf(std::move(item)));
        return persistent_deque{detail::deque_concat(std::move(inserted), split.right.cons(split.hit))};
    }

    template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
    [[nodiscard]] persistent_deque insert_range(const size_type index, Iterator first, Sentinel last) const
    {
        throw_if_insert_index_out_of_range(index, size());
        auto inserted = build_tree(std::move(first), std::move(last), (std::numeric_limits<size_type>::max)() - size());
        if (inserted.size() == 0) {
            return *this;
        }

        if (index == 0) {
            return persistent_deque{detail::deque_concat(std::move(inserted), root_)};
        }

        if (index == size()) {
            return persistent_deque{detail::deque_concat(root_, std::move(inserted))};
        }

        auto split = root_.split_child(index);
        return persistent_deque{
            detail::deque_concat(detail::deque_concat(std::move(split.left), std::move(inserted)), split.right.cons(split.hit))};
    }

    template <std::ranges::input_range Range>
    [[nodiscard]] persistent_deque insert_range(const size_type index, Range&& items) const
    {
        return insert_range(index, std::ranges::begin(items), std::ranges::end(items));
    }

    [[nodiscard]] persistent_deque remove_at(const size_type index) const
    {
        throw_if_index_out_of_range(index, size());
        auto split = root_.split_child(index);
        return wrap(detail::deque_concat(std::move(split.left), std::move(split.right)));
    }

    [[nodiscard]] persistent_deque remove_range(const size_type index, const size_type count) const
    {
        check_range(index, count);
        if (count == 0) {
            return *this;
        }

        if (count == size()) {
            return persistent_deque{};
        }

        auto first_split = detail::deque_split_tree(root_, index);
        auto second_split = detail::deque_split_tree(first_split.second, count);
        return wrap(detail::deque_concat(std::move(first_split.first), std::move(second_split.second)));
    }

    [[nodiscard]] persistent_deque get_range(const size_type index, const size_type count) const
    {
        check_range(index, count);
        if (count == 0) {
            return persistent_deque{};
        }

        if (count == size()) {
            return *this;
        }

        auto first_split = detail::deque_split_tree(root_, index);
        auto second_split = detail::deque_split_tree(first_split.second, count);
        return wrap(second_split.first);
    }

    [[nodiscard]] deque_split<T> split_at(const size_type index) const
    {
        throw_if_insert_index_out_of_range(index, size());
        if (index == 0) {
            return deque_split<T>{persistent_deque{}, *this};
        }

        if (index == size()) {
            return deque_split<T>{*this, persistent_deque{}};
        }

        auto split = detail::deque_split_tree(root_, index);
        return deque_split<T>{wrap(split.first), wrap(split.second)};
    }

    [[nodiscard]] deque_item_split<T> split_item_at(const size_type index) const
    {
        throw_if_index_out_of_range(index, size());
        auto split = root_.split_child(index);
        return deque_item_split<T>{wrap(split.left), split.hit.leaf_value(), wrap(split.right)};
    }

    [[nodiscard]] deque_range_split<T> split_range(const size_type index, const size_type count) const
    {
        check_range(index, count);
        auto first_split = detail::deque_split_tree(root_, index);
        auto second_split = detail::deque_split_tree(first_split.second, count);
        return deque_range_split<T>{wrap(first_split.first), wrap(second_split.first), wrap(second_split.second)};
    }

    [[nodiscard]] persistent_deque concat(const persistent_deque& other) const
    {
        if (other.empty()) {
            return *this;
        }

        if (empty()) {
            return other;
        }

        return persistent_deque{detail::deque_concat(root_, other.root_)};
    }

    [[nodiscard]] persistent_deque add_range(const persistent_deque& other) const
    {
        return concat(other);
    }

    template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
    [[nodiscard]] persistent_deque add_range(Iterator first, Sentinel last) const
    {
        auto appended = build_tree(std::move(first), std::move(last), (std::numeric_limits<size_type>::max)() - size());
        if (appended.size() == 0) {
            return *this;
        }

        return persistent_deque{detail::deque_concat(root_, std::move(appended))};
    }

    template <std::ranges::input_range Range>
    [[nodiscard]] persistent_deque add_range(Range&& items) const
    {
        return add_range(std::ranges::begin(items), std::ranges::end(items));
    }

    template <class Compare = std::less<>>
    [[nodiscard]] size_type sorted_lower_bound(const value_type& item, Compare compare = {}) const
    {
        auto predicate = [&](const value_type& value) {
            return !std::invoke(compare, value, item);
        };

        return root_.bound_index(predicate);
    }

    template <class Compare = std::less<>>
    [[nodiscard]] size_type sorted_upper_bound(const value_type& item, Compare compare = {}) const
    {
        auto predicate = [&](const value_type& value) {
            return std::invoke(compare, item, value);
        };

        return root_.bound_index(predicate);
    }

    template <class Compare = std::less<>>
    [[nodiscard]] difference_type sorted_binary_search(const value_type& item, Compare compare = {}) const
    {
        const auto lower = sorted_lower_bound(item, compare);
        if (lower < size() && equivalent(root_.get_leaf(lower), item, compare)) {
            return checked_difference(lower);
        }

        return ~checked_difference(lower);
    }

    template <class Compare = std::less<>>
    [[nodiscard]] bool sorted_contains(const value_type& item, Compare compare = {}) const
    {
        return sorted_binary_search(item, compare) >= 0;
    }

    template <class Compare = std::less<>>
    [[nodiscard]] deque_split<T> split_at_sorted_lower_bound(const value_type& item, Compare compare = {}) const
    {
        return split_at(sorted_lower_bound(item, compare));
    }

    template <class Compare = std::less<>>
    [[nodiscard]] deque_split<T> split_at_sorted_upper_bound(const value_type& item, Compare compare = {}) const
    {
        return split_at(sorted_upper_bound(item, compare));
    }

    template <class Compare = std::less<>>
    [[nodiscard]] deque_range_split<T> split_at_sorted_equal_range(const value_type& item, Compare compare = {}) const
    {
        const auto lower = sorted_lower_bound(item, compare);
        const auto upper = sorted_upper_bound(item, compare);
        auto first_split = detail::deque_split_tree(root_, lower);
        auto second_split = detail::deque_split_tree(first_split.second, upper - lower);
        return deque_range_split<T>{wrap(first_split.first), wrap(second_split.first), wrap(second_split.second)};
    }

    template <class Compare = std::less<>>
    [[nodiscard]] persistent_deque insert_sorted(value_type item, Compare compare = {}) const
    {
        return insert_at(sorted_upper_bound(item, compare), std::move(item));
    }

    template <class Compare = std::less<>>
    [[nodiscard]] persistent_deque remove_all_sorted(const value_type& item, Compare compare = {}) const
    {
        const auto lower = sorted_lower_bound(item, compare);
        const auto upper = sorted_upper_bound(item, compare);
        return lower == upper ? *this : remove_range(lower, upper - lower);
    }

    [[nodiscard]] std::vector<value_type> to_vector() const
    {
        auto result = std::vector<value_type>{};
        result.reserve(size());
        root_.copy_leaves_to(result);
        return result;
    }

    template <std::output_iterator<const value_type&> OutputIterator>
    void copy_to(OutputIterator output) const
    {
        for (const auto& item : *this) {
            *output++ = item;
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

    void validate_invariants() const
    {
        const auto computed = root_.validate_and_count();
        if (computed != size()) {
            throw std::logic_error("persistent_deque root size disagrees with recomputed total");
        }
    }

    [[nodiscard]] size_type tree_depth() const
    {
        return root_.depth();
    }

    class const_iterator final {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        const_iterator() = default;

        [[nodiscard]] reference operator*() const
        {
            return *current_;
        }

        [[nodiscard]] pointer operator->() const
        {
            return current_;
        }

        const_iterator& operator++()
        {
            advance();
            return *this;
        }

        const_iterator operator++(int)
        {
            auto copy = *this;
            advance();
            return copy;
        }

        friend bool operator==(const const_iterator& left, const const_iterator& right) noexcept
        {
            if (left.current_ == nullptr || right.current_ == nullptr) {
                return left.current_ == right.current_;
            }

            return left.current_ == right.current_;
        }

        friend bool operator!=(const const_iterator& left, const const_iterator& right) noexcept
        {
            return !(left == right);
        }

    private:
        friend class persistent_deque;

        struct frame final {
            enum class frame_kind {
                tree,
                node,
            };

            explicit frame(detail::deque_tree<T> value)
                : kind(frame_kind::tree)
                , tree(std::move(value))
            {
            }

            explicit frame(typename detail::deque_element<T>::node_pointer value)
                : kind(frame_kind::node)
                , node(std::move(value))
            {
            }

            frame_kind kind;
            detail::deque_tree<T> tree;
            typename detail::deque_element<T>::node_pointer node;
            size_type next_child = 0;

            [[nodiscard]] size_type child_count() const
            {
                return kind == frame_kind::tree ? tree.enumeration_child_count() : node->child_count();
            }

            [[nodiscard]] detail::deque_enumeration_child<T> child(const size_type index) const
            {
                if (kind == frame_kind::tree) {
                    return tree.enumeration_child(index);
                }

                const auto& element = node->child_at(index);
                if (element.is_leaf()) {
                    return detail::deque_enumeration_child<T>{
                        detail::deque_enumeration_child<T>::child_kind::leaf,
                        &element.leaf_value(),
                        detail::deque_tree<T>{},
                        {}};
                }

                return detail::deque_enumeration_child<T>{
                    detail::deque_enumeration_child<T>::child_kind::node,
                    nullptr,
                    detail::deque_tree<T>{},
                    element.node_ptr()};
            }
        };

        explicit const_iterator(detail::deque_tree<T> root)
        {
            if (root.size() != 0) {
                stack_.push_back(frame{std::move(root)});
                advance();
            }
        }

        void advance()
        {
            current_ = nullptr;
            while (!stack_.empty()) {
                auto& top = stack_.back();
                if (top.next_child == top.child_count()) {
                    stack_.pop_back();
                    continue;
                }

                auto child = top.child(top.next_child++);
                switch (child.kind) {
                case detail::deque_enumeration_child<T>::child_kind::leaf:
                    current_ = child.leaf;
                    return;
                case detail::deque_enumeration_child<T>::child_kind::tree:
                    if (child.tree.size() != 0) {
                        stack_.push_back(frame{std::move(child.tree)});
                    }
                    break;
                case detail::deque_enumeration_child<T>::child_kind::node:
                    stack_.push_back(frame{std::move(child.node)});
                    break;
                }
            }
        }

        std::vector<frame> stack_;
        const T* current_ = nullptr;
    };

private:
    explicit persistent_deque(detail::deque_tree<T> root)
        : root_(std::move(root))
    {
    }

    [[nodiscard]] static persistent_deque wrap(detail::deque_tree<T> root)
    {
        return root.size() == 0 ? persistent_deque{} : persistent_deque{std::move(root)};
    }

    template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
    [[nodiscard]] static detail::deque_tree<T> build_tree(Iterator first, Sentinel last, size_type capacity)
    {
        auto root = detail::deque_tree<T>::empty();
        for (; first != last; ++first) {
            if (root.size() == capacity) {
                throw std::overflow_error("operation would create a persistent_deque larger than its capacity");
            }

            root = root.snoc(detail::deque_element<T>::leaf(*first));
        }

        return root;
    }

    void throw_if_not_empty() const
    {
        if (empty()) {
            throw std::logic_error("persistent_deque is empty");
        }
    }

    void check_range(const size_type index, const size_type count) const
    {
        if (index > size() || count > size() - index) {
            throw std::out_of_range("range extends past the end of the persistent_deque");
        }
    }

    template <class Compare>
    [[nodiscard]] static bool equivalent(const value_type& left, const value_type& right, Compare& compare)
    {
        return !std::invoke(compare, left, right) && !std::invoke(compare, right, left);
    }

    [[nodiscard]] static difference_type checked_difference(const size_type value)
    {
        if (value > static_cast<size_type>((std::numeric_limits<difference_type>::max)())) {
            throw std::overflow_error("persistent_deque index does not fit in difference_type");
        }

        return static_cast<difference_type>(value);
    }

    detail::deque_tree<T> root_;
};

} // namespace tools::data_structures::finger_tree
