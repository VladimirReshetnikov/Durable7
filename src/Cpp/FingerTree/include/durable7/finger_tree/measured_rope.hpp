/// A chunked persistent sequence that also caches a monoidal measure.
///
/// Combines the rope's chunking with the measured tree's search, so a measured query descends
/// without visiting the elements it skips. Every operation returns a new version and leaves its
/// inputs valid, sharing unchanged structure, so an edit copies a path rather than the whole
/// collection.

#pragma once

#include <durable7/finger_tree/detail/common.hpp>
#include <durable7/finger_tree/detail/measured_rope_chunk.hpp>
#include <durable7/finger_tree/measure_predicates.hpp>
#include <durable7/finger_tree/measured_finger_tree.hpp>

#include <concepts>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace durable7::finger_tree {

template <class T, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, T>
class measured_rope;

template <class T, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, T>
class measured_rope_cursor;

template <class T, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, T>
struct measured_rope_cursor_search_result;

/// The two ropes a split produced, each with its own recomputed measure.
template <class T, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, T>
struct measured_rope_split final {
    measured_rope<T, MeasurePolicy> left;
    measured_rope<T, MeasurePolicy> right;
};

/// Where a measured search landed within a rope.
template <class T, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, T>
struct measured_rope_locate_result final {
    std::size_t index = 0;
    typename MeasurePolicy::measure_type measure_before = MeasurePolicy::empty();
    std::optional<T> value;

    /// Whether a value is present.
    [[nodiscard]] bool has_value() const noexcept
    {
        return value.has_value();
    }

    [[nodiscard]] bool operator==(const measured_rope_locate_result&) const = default;
};

/// A chunked persistent sequence that also caches a monoidal measure, so a measured search can
/// descend without visiting the elements it skips.
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

    class const_iterator;

    static constexpr size_type min_chunk_size = 256;
    static constexpr size_type max_chunk_size = 2048;

    /// An empty rope.
    measured_rope() = default;

    /// A rope holding the listed elements.
    measured_rope(std::initializer_list<value_type> values)
        : tree_(build_tree(values.begin(), values.end()))
    {
    }

    /// A rope holding the elements an iterator pair yields.
    template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
    measured_rope(Iterator first, Sentinel last)
        : tree_(build_tree(std::move(first), std::move(last)))
    {
    }

    /// The shared empty rope.
    [[nodiscard]] static measured_rope empty_rope()
    {
        return measured_rope{};
    }

    /// An empty rope using the supplied policies, which it retains.
    [[nodiscard]] static measured_rope create(std::initializer_list<value_type> values)
    {
        return measured_rope{values};
    }

    /// An empty rope using the supplied policies, which it retains.
    [[nodiscard]] static measured_rope create(std::span<const value_type> values)
    {
        return values.empty() ? measured_rope{} : measured_rope{build_tree(values.begin(), values.end())};
    }

    /// A rope holding a range's elements, built in bulk rather than by repeated insertion.
    template <std::ranges::input_range Range>
        requires std::convertible_to<std::ranges::range_reference_t<Range>, value_type>
    [[nodiscard]] static measured_rope from_range(Range&& values)
    {
        return measured_rope{std::ranges::begin(values), std::ranges::end(values)};
    }

    /// A cursor at the given gap of the rope.
    [[nodiscard]] measured_rope_cursor<value_type, measure_policy> get_cursor(size_type position = 0) const;

    /// A cursor at the first gap where the measure satisfies the predicate.
    template <class Predicate>
    [[nodiscard]] auto get_cursor_by_measure(Predicate predicate) const
        -> measured_rope_cursor_search_result<value_type, measure_policy>
        requires measure_predicate<Predicate, user_measure_type>;

    [[nodiscard]] bool empty() const noexcept
    {
        return tree_.empty();
    }

    /// Number of elements in the rope.
    [[nodiscard]] size_type size() const
    {
        return tree_.measure().first;
    }

    /// The combined measure of every element, read from the cached root measure.
    [[nodiscard]] user_measure_type measure() const
    {
        return tree_.measure().second;
    }

    /// The first element.
    [[nodiscard]] const_reference front() const
    {
        if (auto view = tree_.try_view_left()) {
            return view->item[0];
        }

        throw empty_error();
    }

    /// The last element.
    [[nodiscard]] const_reference back() const
    {
        if (auto view = tree_.try_view_right()) {
            return view->item[view->item.length() - 1];
        }

        throw empty_error();
    }

    /// The element at the given position.
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

    /// Reads the value stored for the key, or nothing when absent.
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

    /// A rope with the key bound to the value, adding or replacing as needed.
    [[nodiscard]] measured_rope set_item(const size_type index, value_type value) const
    {
        throw_if_index_out_of_range(index, size());
        auto split = tree_.try_split_find(count_predicate(index));
        const auto offset = index - split->left.measure().first;
        return wrap(split->left.append(split->item.set_at(offset, std::move(value))).concat(split->right));
    }

    /// A rope with the element at the position replaced.
    [[nodiscard]] measured_rope set_at(const size_type index, value_type value) const
    {
        return set_item(index, std::move(value));
    }

    /// A rope with the element added at the front.
    [[nodiscard]] measured_rope push_front(value_type value) const
    {
        return insert_at(0, std::move(value));
    }

    /// A rope with the element added at the back.
    [[nodiscard]] measured_rope push_back(value_type value) const
    {
        return insert_at(size(), std::move(value));
    }

    /// A rope with the element placed first.
    [[nodiscard]] measured_rope add_first(value_type value) const
    {
        return push_front(std::move(value));
    }

    /// A rope with the element placed last.
    [[nodiscard]] measured_rope add_last(value_type value) const
    {
        return push_back(std::move(value));
    }

    /// A rope without its first element.
    [[nodiscard]] measured_rope remove_first() const
    {
        if (empty()) {
            throw empty_error();
        }

        return remove_at(0);
    }

    /// A rope without its last element.
    [[nodiscard]] measured_rope remove_last() const
    {
        if (empty()) {
            throw empty_error();
        }

        return remove_at(size() - 1);
    }

    /// A rope with the element inserted at the position.
    [[nodiscard]] measured_rope insert_at(size_type index, value_type value) const
    {
        throw_if_insert_index_out_of_range(index, size());
        (void)checked_add(size(), size_type{1});

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
        requires(!std::same_as<std::remove_cvref_t<Range>, measured_rope>)
            && std::convertible_to<std::ranges::range_reference_t<Range>, value_type>
    /// A rope with a range's elements inserted at the position.
    [[nodiscard]] measured_rope insert_range(const size_type index, Range&& values) const
    {
        throw_if_insert_index_out_of_range(index, size());
        auto middle = measured_rope::from_range(std::forward<Range>(values));
        return middle.empty() ? *this : insert_range(index, middle);
    }

    /// A rope with a range's elements inserted at the position.
    [[nodiscard]] measured_rope insert_range(const size_type index, const measured_rope& values) const
    {
        throw_if_insert_index_out_of_range(index, size());
        (void)checked_add(size(), values.size());
        if (values.empty()) {
            return *this;
        }

        auto split = split_at(index);
        return split.left.concat(values).concat(split.right);
    }

    /// A rope without the element at the position.
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

    /// A rope without the elements in the range.
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

    /// The elements in the given range.
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

    /// The elements in the given range.
    [[nodiscard]] measured_rope slice(const size_type index) const
    {
        if (index > size()) {
            throw std::out_of_range("range is outside the measured rope bounds");
        }

        return slice(index, size() - index);
    }

    /// Splits into the elements before the position and those from it onward.
    [[nodiscard]] measured_rope_split<value_type, measure_policy> split_at(const size_type index) const
    {
        throw_if_split_index_out_of_range(index, size());
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

    /// The concatenation of two ropes, sharing both operands' unchanged structure.
    [[nodiscard]] measured_rope concat(const measured_rope& other) const
    {
        (void)checked_add(size(), other.size());
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

    /// The combined measure of the elements before the position.
    [[nodiscard]] user_measure_type prefix_measure(const size_type count) const
    {
        throw_if_count_out_of_range(count, size());
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

    /// Splits at the first point where the accumulated measure satisfies the predicate, descending
    /// by cached measures rather than scanning.
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

    /// Finds the first position where the accumulated measure satisfies the predicate, or nothing
    /// when none does.
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

    /// Copies the elements out into a vector, in the rope's own order.
    [[nodiscard]] std::vector<value_type> to_vector() const
    {
        auto result = std::vector<value_type>{};
        result.reserve(size());
        tree_.for_each([&result](const chunk_type& chunk) {
            chunk.copy_to(result);
        });

        return result;
    }

    /// Calls the function once per element, in the rope's own order.
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

    /// The elements in the range.
    [[nodiscard]] std::vector<value_type> get_range(const size_type index, const size_type count) const
    {
        return slice(index, count).to_vector();
    }

    /// Copies the elements into the destination.
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

    /// An iterator over the elements, in the rope's own order.
    [[nodiscard]] const_iterator begin() const
    {
        return const_iterator{tree_};
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

    /// A forward const iterator over one rope version's elements. It keeps that version alive, so
    /// it stays valid across later edits.
    class const_iterator final {
    public:
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
            return (*chunk_)[offset_];
        }

        [[nodiscard]] pointer operator->() const
        {
            return &(**this);
        }

        const_iterator& operator++()
        {
            ++offset_;
            if (offset_ == chunk_->length()) {
                ++chunk_;
                offset_ = 0;
                skip_empty_chunks();
            }
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
            const auto left_end = left.chunk_ == left.chunk_end_;
            const auto right_end = right.chunk_ == right.chunk_end_;
            if (left_end || right_end) {
                return left_end == right_end;
            }

            return left.chunk_ == right.chunk_ && left.offset_ == right.offset_;
        }

    private:
        friend class measured_rope;

        using chunk_type = detail::measured_rope_chunk<T, MeasurePolicy>;
        using tree_measure_policy = detail::measured_rope_chunk_measure<T, MeasurePolicy>;
        using chunk_tree_type = finger_tree<chunk_type, tree_measure_policy>;
        using chunk_iterator = typename chunk_tree_type::const_iterator;

        explicit const_iterator(const chunk_tree_type& tree)
            : chunk_(tree.begin())
            , chunk_end_(tree.end())
        {
            skip_empty_chunks();
        }

        void skip_empty_chunks()
        {
            while (chunk_ != chunk_end_ && chunk_->empty()) {
                ++chunk_;
            }
        }

        chunk_iterator chunk_;
        chunk_iterator chunk_end_;
        size_type offset_ = 0;
    };

    /// Rebuilds the structure into its compact form, dropping slack accumulated by earlier edits.
    [[nodiscard]] measured_rope compact() const
    {
        return empty() ? measured_rope{} : measured_rope::from_range(to_vector());
    }

    /// Checks the rope's structural invariants. For tests and diagnostics.
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

    /// How many chunks the structure holds.
    [[nodiscard]] size_type chunk_count() const
    {
        auto count = size_type{0};
        tree_.for_each([&count](const chunk_type&) {
            ++count;
        });
        return count;
    }

private:
    friend class measured_rope_cursor<T, MeasurePolicy>;

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

    template <class Predicate>
        requires measure_predicate<Predicate, user_measure_type>
    [[nodiscard]] std::pair<size_type, bool> cursor_position_by_measure(Predicate predicate) const
    {
        auto located = tree_.try_locate(
            second_component_predicate<Predicate, size_type, user_measure_type>{predicate});
        if (!located.item.has_value()) {
            return {size(), false};
        }

        const auto offset = chunk_split_offset(
            located.item.value(),
            located.measure_before.second,
            predicate);
        return {checked_add(located.measure_before.first, offset), true};
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

/// An immutable gap cursor over one exact measured-rope snapshot.
///
/// This semantic checkpoint retains the persistent root plus a position. It deliberately does not
/// claim the focused cursor representation or amortized local-edit bounds of the C# implementation.
template <class T, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, T>
class measured_rope_cursor final {
public:
    using value_type = T;
    using measure_policy = MeasurePolicy;
    using user_measure_type = typename measure_policy::measure_type;
    using size_type = std::size_t;

    /// An unpositioned cursor, holding no version.
    measured_rope_cursor() = delete;
    /// An unpositioned cursor, holding no version.
    measured_rope_cursor(const measured_rope_cursor&) = default;
    measured_rope_cursor& operator=(const measured_rope_cursor&) = default;

    // Cursor values are immutable version handles. A move copies the shared root so the source
    // remains a valid cursor, matching rope_cursor's version-handle convention.
    measured_rope_cursor(measured_rope_cursor&& other) noexcept(
        std::is_nothrow_copy_constructible_v<measured_rope<value_type, measure_policy>>)
        : snapshot_(other.snapshot_)
        , position_(other.position_)
    {
    }

    measured_rope_cursor& operator=(measured_rope_cursor&& other) noexcept(
        std::is_nothrow_copy_assignable_v<measured_rope<value_type, measure_policy>>)
    {
        if (this != &other) {
            snapshot_ = other.snapshot_;
            position_ = other.position_;
        }

        return *this;
    }

    /// Number of elements in the rope version the cursor is positioned in.
    [[nodiscard]] size_type size() const
    {
        return snapshot_.size();
    }

    /// Whether the rope version the cursor is positioned in holds no elements.
    [[nodiscard]] bool empty() const
    {
        return snapshot_.empty();
    }

    /// The cursor's gap position.
    [[nodiscard]] size_type position() const noexcept
    {
        return position_;
    }

    /// Whether the gap precedes the first element.
    [[nodiscard]] bool is_at_start() const noexcept
    {
        return position_ == 0;
    }

    /// Whether the gap follows the last element.
    [[nodiscard]] bool is_at_end() const
    {
        return position_ == snapshot_.size();
    }

    /// The combined measure of everything before the gap.
    [[nodiscard]] user_measure_type measure_before() const
    {
        return snapshot_.prefix_measure(position_);
    }

    /// The combined measure of everything after the gap.
    [[nodiscard]] user_measure_type measure_after() const
    {
        return snapshot_.split_at(position_).right.measure();
    }

    /// Reads the element immediately before the gap, or nothing at the start.
    [[nodiscard]] const value_type* try_peek_previous() const &
    {
        return position_ == 0 ? nullptr : snapshot_.try_get(position_ - 1);
    }

    /// Reads the element immediately before the gap, or nothing at the start.
    const value_type* try_peek_previous() const && = delete;

    /// Reads the element immediately after the gap, or nothing at the end.
    [[nodiscard]] const value_type* try_peek_next() const &
    {
        return snapshot_.try_get(position_);
    }

    /// Reads the element immediately after the gap, or nothing at the end.
    const value_type* try_peek_next() const && = delete;

    /// A cursor one position earlier. The receiver is unchanged; movement produces a new cursor
    /// over the same version.
    [[nodiscard]] measured_rope_cursor move_previous() const
    {
        if (is_at_start()) {
            throw std::logic_error("measured rope cursor is already at the start");
        }

        return measured_rope_cursor{snapshot_, position_ - 1};
    }

    /// A cursor one position later. The receiver is unchanged.
    [[nodiscard]] measured_rope_cursor move_next() const
    {
        if (is_at_end()) {
            throw std::logic_error("measured rope cursor is already at the end");
        }

        return measured_rope_cursor{snapshot_, position_ + 1};
    }

    /// A cursor at the given position within the same rope version.
    [[nodiscard]] measured_rope_cursor seek(const size_type position) const
    {
        if (position > snapshot_.size()) {
            throw std::out_of_range("cursor position is outside the measured rope bounds");
        }

        return position == position_ ? *this : measured_rope_cursor{snapshot_, position};
    }

    /// A cursor at the first gap where the measure satisfies the predicate.
    template <class Predicate>
    [[nodiscard]] auto seek_by_measure(Predicate predicate) const
        -> measured_rope_cursor_search_result<value_type, measure_policy>
        requires measure_predicate<Predicate, user_measure_type>;

    [[nodiscard]] measured_rope_cursor insert(value_type value) const
    {
        const auto next_position = checked_add(position_, size_type{1});
        return measured_rope_cursor{snapshot_.insert_at(position_, std::move(value)), next_position};
    }

    template <std::ranges::input_range Range>
        requires(!std::same_as<std::remove_cvref_t<Range>, measured_rope<value_type, measure_policy>>)
            && std::convertible_to<std::ranges::range_reference_t<Range>, value_type>
    /// A rope with a range's elements inserted at the position.
    [[nodiscard]] measured_rope_cursor insert_range(Range&& values) const
    {
        auto middle = measured_rope<value_type, measure_policy>::from_range(std::forward<Range>(values));
        return middle.empty() ? *this : insert_range(middle);
    }

    /// A rope with a range's elements inserted at the position.
    [[nodiscard]] measured_rope_cursor insert_range(
        const measured_rope<value_type, measure_policy>& values) const
    {
        if (values.empty()) {
            return *this;
        }

        const auto next_position = checked_add(position_, values.size());
        return measured_rope_cursor{snapshot_.insert_range(position_, values), next_position};
    }

    /// Removes the element before the gap, producing a new version the returned cursor is
    /// positioned in.
    [[nodiscard]] measured_rope_cursor delete_previous() const
    {
        if (is_at_start()) {
            throw std::logic_error("measured rope cursor has no previous element");
        }

        return measured_rope_cursor{snapshot_.remove_at(position_ - 1), position_ - 1};
    }

    /// Removes the element after the gap, producing a new version the returned cursor is positioned
    /// in.
    [[nodiscard]] measured_rope_cursor delete_next() const
    {
        if (is_at_end()) {
            throw std::logic_error("measured rope cursor has no next element");
        }

        return measured_rope_cursor{snapshot_.remove_at(position_), position_};
    }

    /// Replaces the element after the gap, producing a new version the returned cursor is
    /// positioned in.
    [[nodiscard]] measured_rope_cursor replace_next(value_type value) const
    {
        if (is_at_end()) {
            throw std::logic_error("measured rope cursor has no next element");
        }

        return measured_rope_cursor{snapshot_.set_item(position_, std::move(value)), position_};
    }

    /// The rope version this cursor is positioned in.
    [[nodiscard]] measured_rope<value_type, measure_policy> snapshot() const
    {
        return snapshot_;
    }

private:
    friend class measured_rope<T, MeasurePolicy>;

    measured_rope_cursor(
        measured_rope<value_type, measure_policy> snapshot,
        const size_type position)
        : snapshot_(std::move(snapshot))
        , position_(position)
    {
    }

    measured_rope<value_type, measure_policy> snapshot_;
    size_type position_;
};

/// Result of absolute prefix-measure cursor search. A miss retains a usable end cursor.
template <class T, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, T>
struct measured_rope_cursor_search_result final {
    measured_rope_cursor<T, MeasurePolicy> cursor;
    bool found = false;
};

template <class T, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, T>
template <class Predicate>
[[nodiscard]] auto measured_rope_cursor<T, MeasurePolicy>::seek_by_measure(Predicate predicate) const
    -> measured_rope_cursor_search_result<value_type, measure_policy>
    requires measure_predicate<Predicate, user_measure_type>
{
    const auto [position, found] = snapshot_.cursor_position_by_measure(predicate);
    return measured_rope_cursor_search_result<value_type, measure_policy>{
        measured_rope_cursor{snapshot_, position},
        found};
}

template <class T, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, T>
[[nodiscard]] measured_rope_cursor<T, MeasurePolicy>
measured_rope<T, MeasurePolicy>::get_cursor(const size_type position) const
{
    if (position > size()) {
        throw std::out_of_range("cursor position is outside the measured rope bounds");
    }

    return measured_rope_cursor<value_type, measure_policy>{*this, position};
}

template <class T, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, T>
template <class Predicate>
[[nodiscard]] auto measured_rope<T, MeasurePolicy>::get_cursor_by_measure(Predicate predicate) const
    -> measured_rope_cursor_search_result<value_type, measure_policy>
    requires measure_predicate<Predicate, user_measure_type>
{
    return get_cursor().seek_by_measure(std::move(predicate));
}

template <class T, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, T> && equality_comparable_value<T>
[[nodiscard]] bool operator==(
    const measured_rope_split<T, MeasurePolicy>& left,
    const measured_rope_split<T, MeasurePolicy>& right)
{
    return detail::sequence_equal(left.left, right.left)
        && detail::sequence_equal(left.right, right.right);
}

} // namespace durable7::finger_tree
