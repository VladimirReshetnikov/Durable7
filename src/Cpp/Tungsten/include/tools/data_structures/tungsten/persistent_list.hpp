#pragma once

#include <tools/data_structures/finger_tree/persistent_deque.hpp>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace tools::data_structures::tungsten {

template <class T>
class persistent_list;

template <class T>
struct persistent_list_split final {
    persistent_list<T> left;
    persistent_list<T> right;
};

template <class T>
class persistent_list final {
public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using const_reference = const value_type&;
    using const_iterator = typename finger_tree::persistent_deque<T>::const_iterator;

    persistent_list() = default;

    persistent_list(std::initializer_list<T> items)
        : items_(items)
    {
    }

    template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
    persistent_list(Iterator first, Sentinel last)
        : items_(first, last)
    {
    }

    [[nodiscard]] static persistent_list empty_list()
    {
        return {};
    }

    [[nodiscard]] static persistent_list create(std::initializer_list<T> items)
    {
        return persistent_list{items};
    }

    template <std::ranges::input_range Range>
    [[nodiscard]] static persistent_list from_range(Range&& items)
    {
        if constexpr (std::same_as<std::remove_cvref_t<Range>, persistent_list>) {
            return items;
        } else {
            return persistent_list{std::ranges::begin(items), std::ranges::end(items)};
        }
    }

    [[nodiscard]] bool is_empty() const noexcept
    {
        return items_.empty();
    }

    [[nodiscard]] size_type size() const noexcept
    {
        return items_.size();
    }

    [[nodiscard]] const_reference front() const
    {
        return items_.front();
    }

    [[nodiscard]] const_reference back() const
    {
        return items_.back();
    }

    [[nodiscard]] const value_type* try_front() const
    {
        return items_.try_front();
    }

    [[nodiscard]] const value_type* try_back() const
    {
        return items_.try_back();
    }

    [[nodiscard]] const_reference operator[](const size_type index) const
    {
        return items_[index];
    }

    [[nodiscard]] const_reference at(const size_type index) const
    {
        return items_.at(index);
    }

    [[nodiscard]] const value_type* try_get(const size_type index) const
    {
        return items_.try_get(index);
    }

    [[nodiscard]] persistent_list append(value_type item) const
    {
        return wrap(items_.push_back(std::move(item)));
    }

    [[nodiscard]] persistent_list prepend(value_type item) const
    {
        return wrap(items_.push_front(std::move(item)));
    }

    [[nodiscard]] persistent_list join(const persistent_list& other) const
    {
        if (other.is_empty()) {
            return *this;
        }

        if (is_empty()) {
            return other;
        }

        return wrap(items_.concat(other.items_));
    }

    [[nodiscard]] persistent_list add_range(const persistent_list& other) const
    {
        return join(other);
    }

    template <std::ranges::input_range Range>
    [[nodiscard]] persistent_list add_range(Range&& items) const
    {
        // An rvalue persistent_list binds to this template in preference to
        // the const& overload; keep the O(log min) join fast path for it.
        if constexpr (std::same_as<std::remove_cvref_t<Range>, persistent_list>) {
            return join(items);
        } else {
            return wrap(items_.add_range(std::ranges::begin(items), std::ranges::end(items)));
        }
    }

    [[nodiscard]] persistent_list insert(const size_type index, value_type item) const
    {
        return wrap(items_.insert_at(index, std::move(item)));
    }

    template <std::ranges::input_range Range>
    [[nodiscard]] persistent_list insert_range(const size_type index, Range&& items) const
    {
        return wrap(items_.insert_range(index, std::ranges::begin(items), std::ranges::end(items)));
    }

    [[nodiscard]] persistent_list remove_at(const size_type index) const
    {
        return wrap(items_.remove_at(index));
    }

    [[nodiscard]] persistent_list remove_range(const size_type index, const size_type count) const
    {
        return wrap(items_.remove_range(index, count));
    }

    [[nodiscard]] persistent_list remove_first() const
    {
        return wrap(items_.remove_first());
    }

    [[nodiscard]] persistent_list remove_last() const
    {
        return wrap(items_.remove_last());
    }

    [[nodiscard]] persistent_list set_item(const size_type index, const value_type& value) const
    {
        return wrap(items_.set_item(index, value));
    }

    template <class Updater>
        requires std::invocable<Updater&, const value_type&>
    [[nodiscard]] persistent_list update_at(const size_type index, Updater updater) const
    {
        return wrap(items_.update_at(index, std::move(updater)));
    }

    [[nodiscard]] persistent_list get_range(const size_type index, const size_type count) const
    {
        return wrap(items_.get_range(index, count));
    }

    [[nodiscard]] persistent_list take(const size_type count) const
    {
        return get_range(0, count);
    }

    [[nodiscard]] persistent_list take_last(const size_type count) const
    {
        throw_if_count_out_of_range(count);
        return get_range(size() - count, count);
    }

    [[nodiscard]] persistent_list drop(const size_type count) const
    {
        throw_if_count_out_of_range(count);
        return get_range(count, size() - count);
    }

    [[nodiscard]] persistent_list drop_last(const size_type count) const
    {
        throw_if_count_out_of_range(count);
        return get_range(0, size() - count);
    }

    [[nodiscard]] persistent_list_split<T> split_at(const size_type index) const
    {
        auto split = items_.split_at(index);
        return persistent_list_split<T>{wrap(std::move(split.left)), wrap(std::move(split.right))};
    }

    [[nodiscard]] persistent_list reverse() const
    {
        if (size() <= 1) {
            return *this;
        }

        auto values = items_.to_vector();
        std::ranges::reverse(values);
        return persistent_list{values.begin(), values.end()};
    }

    template <class Selector>
        requires std::invocable<Selector&, const value_type&>
    [[nodiscard]] auto map(Selector selector) const
        -> persistent_list<std::invoke_result_t<Selector&, const value_type&>>
    {
        using result_type = std::invoke_result_t<Selector&, const value_type&>;

        auto values = std::vector<result_type>{};
        values.reserve(size());
        for (const auto& item : items_) {
            values.push_back(std::invoke(selector, item));
        }

        return persistent_list<result_type>{values.begin(), values.end()};
    }

    template <class Equal = std::equal_to<>>
    [[nodiscard]] difference_type index_of(const value_type& item, Equal equal = {}) const
    {
        auto index = size_type{0};
        for (const auto& candidate : items_) {
            if (std::invoke(equal, candidate, item)) {
                return checked_difference(index);
            }

            ++index;
        }

        return -1;
    }

    template <class Equal = std::equal_to<>>
    [[nodiscard]] bool contains(const value_type& item, Equal equal = {}) const
    {
        return index_of(item, std::move(equal)) >= 0;
    }

    [[nodiscard]] std::vector<value_type> to_vector() const
    {
        return items_.to_vector();
    }

    template <class OutputIterator>
    void copy_to(OutputIterator output) const
    {
        items_.copy_to(std::move(output));
    }

    [[nodiscard]] const_iterator begin() const
    {
        return items_.begin();
    }

    [[nodiscard]] const_iterator end() const noexcept
    {
        return items_.end();
    }

private:
    explicit persistent_list(finger_tree::persistent_deque<T> items)
        : items_(std::move(items))
    {
    }

    [[nodiscard]] static persistent_list wrap(finger_tree::persistent_deque<T> items)
    {
        return persistent_list{std::move(items)};
    }

    void throw_if_count_out_of_range(const size_type count) const
    {
        if (count > size()) {
            throw std::out_of_range("persistent_list count extends past the end of the list");
        }
    }

    [[nodiscard]] static difference_type checked_difference(const size_type value)
    {
        if (value > static_cast<size_type>((std::numeric_limits<difference_type>::max)())) {
            throw std::overflow_error("persistent_list index does not fit in difference_type");
        }

        return static_cast<difference_type>(value);
    }

    finger_tree::persistent_deque<T> items_;
};

} // namespace tools::data_structures::tungsten
