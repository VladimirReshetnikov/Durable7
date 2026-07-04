#pragma once

#include <tools/data_structures/finger_tree/detail/common.hpp>
#include <tools/data_structures/finger_tree/detail/measured_rope_chunk.hpp>
#include <tools/data_structures/finger_tree/measure_predicates.hpp>
#include <tools/data_structures/finger_tree/measured_finger_tree.hpp>

#include <concepts>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tools::data_structures::finger_tree {

template <class T, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, T>
class measured_rope;

template <class T, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, T>
struct measured_rope_split final {
    measured_rope<T, MeasurePolicy> left;
    measured_rope<T, MeasurePolicy> right;
};

template <class T, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, T>
struct measured_rope_locate_result final {
    std::size_t index = 0;
    typename MeasurePolicy::measure_type measure_before = MeasurePolicy::empty();
    std::optional<T> value;

    [[nodiscard]] bool has_value() const noexcept
    {
        return value.has_value();
    }

    [[nodiscard]] bool operator==(const measured_rope_locate_result&) const = default;
};

template <class T, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, T>
class measured_rope final {
public:
    using value_type = T;
    using measure_policy = MeasurePolicy;
    using user_measure_type = typename measure_policy::measure_type;
    using size_type = std::size_t;
    using reference = const value_type&;
    using const_reference = const value_type&;

    static constexpr size_type min_chunk_size = 256;
    static constexpr size_type max_chunk_size = 2048;

    measured_rope() = default;

    measured_rope(std::initializer_list<value_type> values)
        : tree_(build_tree(values.begin(), values.end()))
    {
    }

    template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
    measured_rope(Iterator first, Sentinel last)
        : tree_(build_tree(std::move(first), std::move(last)))
    {
    }

    [[nodiscard]] static measured_rope empty_rope()
    {
        return measured_rope{};
    }

    [[nodiscard]] static measured_rope create(std::initializer_list<value_type> values)
    {
        return measured_rope{values};
    }

    [[nodiscard]] static measured_rope create(std::span<const value_type> values)
    {
        return values.empty() ? measured_rope{} : measured_rope{build_tree(values.begin(), values.end())};
    }

    template <std::ranges::input_range Range>
        requires std::convertible_to<std::ranges::range_reference_t<Range>, value_type>
    [[nodiscard]] static measured_rope from_range(Range&& values)
    {
        return measured_rope{std::ranges::begin(values), std::ranges::end(values)};
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return tree_.empty();
    }

    [[nodiscard]] size_type size() const
    {
        return tree_.measure().first;
    }

    [[nodiscard]] user_measure_type measure() const
    {
        return tree_.measure().second;
    }

    [[nodiscard]] const_reference front() const
    {
        if (auto view = tree_.try_view_left()) {
            return view->item[0];
        }

        throw empty_error();
    }

    [[nodiscard]] const_reference back() const
    {
        if (auto view = tree_.try_view_right()) {
            return view->item[view->item.length() - 1];
        }

        throw empty_error();
    }

    [[nodiscard]] const_reference at(const size_type index) const
    {
        throw_if_index_out_of_range(index, size());
        auto located = tree_.try_locate(count_predicate(index));
        if (!located.item.has_value()) {
            throw std::logic_error("measured_rope index locate failed");
        }

        return located.item.value()[index - located.measure_before.first];
    }

    [[nodiscard]] const_reference operator[](const size_type index) const
    {
        return at(index);
    }

    [[nodiscard]] const value_type* try_get(const size_type index) const
    {
        if (index >= size()) {
            return nullptr;
        }

        auto located = tree_.try_locate(count_predicate(index));
        if (!located.item.has_value()) {
            throw std::logic_error("measured_rope index locate failed");
        }

        return &located.item.value()[index - located.measure_before.first];
    }

    [[nodiscard]] measured_rope set_item(const size_type index, value_type value) const
    {
        throw_if_index_out_of_range(index, size());
        auto split = tree_.try_split_find(count_predicate(index));
        const auto offset = index - split->left.measure().first;
        return wrap(split->left.append(split->item.set_at(offset, std::move(value))).concat(split->right));
    }

    [[nodiscard]] measured_rope set_at(const size_type index, value_type value) const
    {
        return set_item(index, std::move(value));
    }

    [[nodiscard]] measured_rope push_front(value_type value) const
    {
        return insert_at(0, std::move(value));
    }

    [[nodiscard]] measured_rope push_back(value_type value) const
    {
        return insert_at(size(), std::move(value));
    }

    [[nodiscard]] measured_rope add_first(value_type value) const
    {
        return push_front(std::move(value));
    }

    [[nodiscard]] measured_rope add_last(value_type value) const
    {
        return push_back(std::move(value));
    }

    [[nodiscard]] measured_rope remove_first() const
    {
        if (empty()) {
            throw empty_error();
        }

        return remove_at(0);
    }

    [[nodiscard]] measured_rope remove_last() const
    {
        if (empty()) {
            throw empty_error();
        }

        return remove_at(size() - 1);
    }

    [[nodiscard]] measured_rope insert_at(size_type index, value_type value) const
    {
        throw_if_insert_index_out_of_range(index, size());

        if (empty()) {
            return wrap(tree_.append(single_chunk(std::move(value))));
        }

        if (index == size()) {
            auto view = tree_.try_view_right();
            if (view->item.length() < max_chunk_size) {
                return wrap(view->left.append(view->item.insert_at(view->item.length(), std::move(value))));
            }

            return wrap(tree_.append(single_chunk(std::move(value))));
        }

        auto split = tree_.try_split_find(count_predicate(index));
        const auto offset = index - split->left.measure().first;
        return wrap(join_grown(split->left, split->item.insert_at(offset, std::move(value)), split->right));
    }

    template <std::ranges::input_range Range>
        requires std::convertible_to<std::ranges::range_reference_t<Range>, value_type>
    [[nodiscard]] measured_rope insert_range(const size_type index, Range&& values) const
    {
        throw_if_insert_index_out_of_range(index, size());
        auto middle = measured_rope::from_range(std::forward<Range>(values));
        return middle.empty() ? *this : insert_range(index, middle);
    }

    [[nodiscard]] measured_rope insert_range(const size_type index, const measured_rope& values) const
    {
        throw_if_insert_index_out_of_range(index, size());
        if (values.empty()) {
            return *this;
        }

        auto split = split_at(index);
        return split.left.concat(values).concat(split.right);
    }

    [[nodiscard]] measured_rope remove_at(size_type index) const
    {
        throw_if_index_out_of_range(index, size());
        auto split = tree_.try_split_find(count_predicate(index));
        if (split->item.length() == 1) {
            return wrap(split->left.concat(split->right));
        }

        const auto offset = index - split->left.measure().first;
        return wrap(join_shrunk(split->left, split->item.remove_at(offset), split->right));
    }

    [[nodiscard]] measured_rope remove_range(const size_type index, const size_type count) const
    {
        throw_if_range_out_of_bounds(index, count);
        if (count == 0) {
            return *this;
        }

        auto left_rest = split_at(index);
        auto middle_right = left_rest.right.split_at(count);
        return left_rest.left.concat(middle_right.right);
    }

    [[nodiscard]] measured_rope slice(const size_type index, const size_type count) const
    {
        throw_if_range_out_of_bounds(index, count);
        if (count == 0) {
            return measured_rope{};
        }

        if (index == 0 && count == size()) {
            return *this;
        }

        auto left_rest = split_at(index);
        return left_rest.right.split_at(count).left;
    }

    [[nodiscard]] measured_rope slice(const size_type index) const
    {
        if (index > size()) {
            throw std::out_of_range("range is outside the measured rope bounds");
        }

        return slice(index, size() - index);
    }

    [[nodiscard]] measured_rope_split<value_type, measure_policy> split_at(const size_type index) const
    {
        throw_if_insert_index_out_of_range(index, size());
        if (index == 0) {
            return measured_rope_split<value_type, measure_policy>{measured_rope{}, *this};
        }

        if (index == size()) {
            return measured_rope_split<value_type, measure_policy>{*this, measured_rope{}};
        }

        auto split = tree_.try_split_find(count_predicate(index));
        const auto offset = index - split->left.measure().first;
        auto left = offset == 0 ? split->left : split->left.append(split->item.slice(0, offset));
        auto right = split->right.prepend(split->item.slice(offset, split->item.length() - offset));
        return measured_rope_split<value_type, measure_policy>{wrap(std::move(left)), wrap(std::move(right))};
    }

    [[nodiscard]] measured_rope concat(const measured_rope& other) const
    {
        if (other.empty()) {
            return *this;
        }

        if (empty()) {
            return other;
        }

        auto left_view = tree_.try_view_right();
        auto right_view = other.tree_.try_view_left();
        if (checked_add(left_view->item.length(), right_view->item.length()) <= max_chunk_size) {
            return wrap(left_view->left
                            .append(chunk_type::concat(left_view->item, right_view->item))
                            .concat(right_view->right));
        }

        return wrap(tree_.concat(other.tree_));
    }

    [[nodiscard]] user_measure_type prefix_measure(const size_type count) const
    {
        throw_if_insert_index_out_of_range(count, size());
        if (count == 0) {
            return measure_policy::empty();
        }

        if (count == size()) {
            return measure();
        }

        auto located = tree_.try_locate(count_predicate(count));
        if (!located.item.has_value()) {
            throw std::logic_error("measured_rope prefix locate failed");
        }

        return measure_policy::combine(
            located.measure_before.second,
            located.item.value().measure_prefix(count - located.measure_before.first));
    }

    template <class Predicate>
        requires measure_predicate<Predicate, user_measure_type>
    [[nodiscard]] measured_rope_split<value_type, measure_policy> split_by_measure(Predicate predicate) const
    {
        auto split = tree_.try_split_find(second_component_predicate<Predicate, size_type, user_measure_type>{predicate});
        if (!split.has_value()) {
            return measured_rope_split<value_type, measure_policy>{*this, measured_rope{}};
        }

        const auto offset = chunk_split_offset(split->item, split->left.measure().second, predicate);
        auto left = offset == 0 ? split->left : split->left.append(split->item.slice(0, offset));
        auto right = split->right.prepend(split->item.slice(offset, split->item.length() - offset));
        return measured_rope_split<value_type, measure_policy>{wrap(std::move(left)), wrap(std::move(right))};
    }

    template <class Predicate>
        requires measure_predicate<Predicate, user_measure_type>
    [[nodiscard]] measured_rope_locate_result<value_type, measure_policy> try_locate_by_measure(Predicate predicate) const
    {
        auto located = tree_.try_locate(second_component_predicate<Predicate, size_type, user_measure_type>{predicate});
        if (!located.item.has_value()) {
            return measured_rope_locate_result<value_type, measure_policy>{size(), located.measure_before.second, std::nullopt};
        }

        const auto& chunk = located.item.value();
        const auto offset = chunk_split_offset(chunk, located.measure_before.second, predicate);
        return measured_rope_locate_result<value_type, measure_policy>{
            located.measure_before.first + offset,
            measure_policy::combine(located.measure_before.second, chunk.measure_prefix(offset)),
            chunk[offset]};
    }

    [[nodiscard]] std::vector<value_type> to_vector() const
    {
        auto result = std::vector<value_type>{};
        result.reserve(size());
        tree_.for_each([&result](const chunk_type& chunk) {
            chunk.copy_to(result);
        });

        return result;
    }

    template <class Function>
        requires std::invocable<Function&, const value_type&>
    void for_each(Function function) const
    {
        tree_.for_each([&function](const chunk_type& chunk) {
            for (const auto& value : chunk.view()) {
                std::invoke(function, value);
            }
        });
    }

    [[nodiscard]] std::vector<value_type> get_range(const size_type index, const size_type count) const
    {
        return slice(index, count).to_vector();
    }

    void copy_to(const size_type index, std::span<value_type> destination) const
    {
        throw_if_range_out_of_bounds(index, destination.size());
        if (destination.empty()) {
            return;
        }

        auto source = slice(index, destination.size());
        auto offset = size_type{0};
        source.tree_.for_each([&destination, &offset](const chunk_type& chunk) {
            chunk.copy_to(destination.subspan(offset, chunk.length()));
            offset += chunk.length();
        });
    }

    [[nodiscard]] measured_rope compact() const
    {
        return empty() ? measured_rope{} : measured_rope::from_range(to_vector());
    }

    void validate_invariants() const
        requires equality_comparable_value<user_measure_type>
    {
        auto total = size_type{0};
        auto total_measure = measure_policy::empty();
        tree_.for_each([&total, &total_measure](const chunk_type& chunk) {
            if (chunk.length() == 0) {
                throw std::logic_error("measured_rope invariant violated: empty chunk");
            }

            if (chunk.length() > max_chunk_size) {
                throw std::logic_error("measured_rope invariant violated: oversized chunk");
            }

            if (!(chunk.recompute_measure() == chunk.measure())) {
                throw std::logic_error("measured_rope invariant violated: stale chunk measure");
            }

            total = checked_add(total, chunk.length());
            total_measure = measure_policy::combine(total_measure, chunk.measure());
        });

        if (total != size()) {
            throw std::logic_error("measured_rope invariant violated: cached size mismatch");
        }

        if (!(total_measure == measure())) {
            throw std::logic_error("measured_rope invariant violated: cached measure mismatch");
        }
    }

    [[nodiscard]] size_type chunk_count() const
    {
        auto count = size_type{0};
        tree_.for_each([&count](const chunk_type&) {
            ++count;
        });
        return count;
    }

private:
    using chunk_type = detail::measured_rope_chunk<value_type, measure_policy>;
    using tree_measure_type = measure_pair<size_type, user_measure_type>;
    using tree_type = finger_tree<chunk_type, detail::measured_rope_chunk_measure<value_type, measure_policy>>;

    explicit measured_rope(tree_type tree)
        : tree_(std::move(tree))
    {
    }

    [[nodiscard]] static measured_rope wrap(tree_type tree)
    {
        return tree.empty() ? measured_rope{} : measured_rope{std::move(tree)};
    }

    [[nodiscard]] static first_component_predicate<size_above_predicate, size_type, user_measure_type> count_predicate(
        const size_type index)
    {
        return first_component_predicate<size_above_predicate, size_type, user_measure_type>{
            size_above_predicate{index}};
    }

    template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
    [[nodiscard]] static tree_type build_tree(Iterator first, Sentinel last)
    {
        auto tree = tree_type{};
        auto buffer = std::vector<value_type>{};
        buffer.reserve(max_chunk_size);
        for (; first != last; ++first) {
            buffer.push_back(*first);
            if (buffer.size() == max_chunk_size) {
                tree = tree.append(chunk_type::from_vector(std::move(buffer)));
                buffer = std::vector<value_type>{};
                buffer.reserve(max_chunk_size);
            }
        }

        if (!buffer.empty()) {
            tree = tree.append(chunk_type::from_vector(std::move(buffer)));
        }

        return tree;
    }

    [[nodiscard]] static chunk_type single_chunk(value_type value)
    {
        auto values = std::vector<value_type>{};
        values.reserve(1);
        values.push_back(std::move(value));
        return chunk_type::from_vector(std::move(values));
    }

    template <class Predicate>
        requires measure_predicate<Predicate, user_measure_type>
    [[nodiscard]] static size_type chunk_split_offset(
        const chunk_type& chunk,
        user_measure_type measure_before,
        const Predicate& predicate)
    {
        auto current = chunk.view();
        for (auto offset = size_type{0}; offset < current.size(); ++offset) {
            measure_before = measure_policy::combine(measure_before, measure_policy::measure(current[offset]));
            if (predicate(measure_before)) {
                return offset;
            }
        }

        throw std::logic_error("measured_rope predicate did not flip inside the located chunk");
    }

    [[nodiscard]] static tree_type join_grown(tree_type left, chunk_type grown, tree_type right)
    {
        if (grown.length() <= max_chunk_size) {
            return left.append(std::move(grown)).concat(right);
        }

        const auto half = grown.length() / 2;
        return left.append(grown.slice(0, half))
            .append(grown.slice(half, grown.length() - half))
            .concat(right);
    }

    [[nodiscard]] static tree_type join_shrunk(tree_type left, chunk_type shrunk, tree_type right)
    {
        if (shrunk.length() >= min_chunk_size) {
            return left.append(std::move(shrunk)).concat(right);
        }

        if (auto left_view = left.try_view_right()) {
            if (checked_add(left_view->item.length(), shrunk.length()) <= max_chunk_size) {
                return left_view->left.append(chunk_type::concat(left_view->item, shrunk)).concat(right);
            }
        }

        if (auto right_view = right.try_view_left()) {
            if (checked_add(shrunk.length(), right_view->item.length()) <= max_chunk_size) {
                return left.append(chunk_type::concat(shrunk, right_view->item)).concat(right_view->right);
            }
        }

        return left.append(std::move(shrunk)).concat(right);
    }

    void throw_if_range_out_of_bounds(const size_type index, const size_type count) const
    {
        if (index > size() || count > size() - index) {
            throw std::out_of_range("range is outside the measured rope bounds");
        }
    }

    [[nodiscard]] static std::logic_error empty_error()
    {
        return std::logic_error("measured_rope is empty");
    }

    tree_type tree_;
};

} // namespace tools::data_structures::finger_tree
