#pragma once

#include <tools/data_structures/finger_tree/detail/common.hpp>
#include <tools/data_structures/finger_tree/detail/measured_tree.hpp>
#include <tools/data_structures/finger_tree/measures.hpp>

#include <concepts>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace tools::data_structures::finger_tree {

template <class Element, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, Element>
class finger_tree;

template <class Element, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, Element>
class finger_tree_cursor;

template <class Element, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, Element>
struct finger_tree_cursor_search_result;

template <class Element, class MeasurePolicy>
struct finger_tree_split final {
    /// Elements before the first prefix for which the split predicate holds.
    finger_tree<Element, MeasurePolicy> left;
    /// The boundary element, when present, and every following element.
    finger_tree<Element, MeasurePolicy> right;
};

template <class Element, class MeasurePolicy>
struct finger_tree_item_split final {
    /// Elements strictly before the located boundary element.
    finger_tree<Element, MeasurePolicy> left;
    /// A value copy of the located boundary element.
    Element item;
    /// Elements strictly after the located boundary element.
    finger_tree<Element, MeasurePolicy> right;
};

template <class Element, class MeasurePolicy>
struct finger_tree_extract_result final {
    /// A value copy of the extracted element.
    Element item;
    /// The persistent remainder; it may share immutable storage with the source.
    finger_tree<Element, MeasurePolicy> rest;
};

template <class Element, class MeasurePolicy>
struct finger_tree_locate_result final {
    /// The measure of elements strictly before `item`, or the whole-tree
    /// measure when no item is found.
    typename MeasurePolicy::measure_type measure_before;
    /// A value copy of the boundary element, or empty when no prefix matches.
    std::optional<Element> item;

    [[nodiscard]] bool has_value() const noexcept
    {
        return item.has_value();
    }

    [[nodiscard]] bool operator==(const finger_tree_locate_result&) const = default;
};

template <class Element, class MeasurePolicy>
struct finger_tree_locate_reference_result final {
    /// The measure of elements strictly before `item`, or the whole-tree
    /// measure when no item is found.
    typename MeasurePolicy::measure_type measure_before;
    /// The canonical stored boundary element, or null when no prefix matches.
    /// It remains valid while the source tree or a sharing persistent snapshot
    /// remains alive.
    const Element* item;

    [[nodiscard]] bool has_value() const noexcept
    {
        return item != nullptr;
    }

    // Intentionally no equality: pointer equality would expose storage-sharing
    // identity, while pointee equality would misstate this reference-lifetime API.
};

template <class Element, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, Element>
/// An immutable, catenable sequence annotated by an application-defined monoid.
///
/// Every update returns a new value and path-copies only changed structure;
/// existing snapshots remain unchanged and may share all untouched nodes. The
/// lazy middle spine and cached measures use atomic immutable publication, so
/// concurrent reads of the same snapshot are safe provided the element,
/// measure-policy, predicate, and comparator operations invoked by those reads
/// are themselves safe for concurrent use.
///
/// `front()` and `back()` are structural worst-case O(1) reads. `measure()`,
/// `prepend()`, and `append()` have amortized O(1) structural cost, not a hard
/// worst-case O(1) guarantee: they may force and publish a pending middle-spine
/// operation. Measure/predicate/copy costs are additional. Prefix search is
/// logarithmic in sequence length when the predicate is monotone over prefix
/// measures and policy operations are constant-time.
///
/// Operations provide the strong persistence guarantee: if element copying,
/// allocation, measure-policy code, or a predicate throws, the source snapshot
/// is unchanged. A failed lazy computation is not published and may be retried.
/// Streaming iterators are multipass forward iterators. They retain the
/// immutable root they traverse, so stored-element references do not dangle
/// when the facade object used to create the iterator is destroyed.
class finger_tree final {
public:
    using value_type = Element;
    using measure_policy = MeasurePolicy;
    using measure_type = typename measure_policy::measure_type;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = const value_type&;
    using const_reference = const value_type&;

    class const_iterator;

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

    /// Returns the monoidal annotation of the whole sequence.
    /// Structural cost is amortized O(1); this call may force a pending spine.
    [[nodiscard]] measure_type measure() const
    {
        return root_.measure();
    }

    /// Creates a measure-aware cursor before the first element.
    [[nodiscard]] finger_tree_cursor<value_type, measure_policy> get_cursor_at_start() const;

    /// Creates a measure-aware cursor after the final element.
    [[nodiscard]] finger_tree_cursor<value_type, measure_policy> get_cursor_at_end() const;

    /// Locates the gap before the first element whose inclusive prefix satisfies a monotone
    /// predicate. A miss retains a usable end cursor.
    template <class Predicate>
        requires std::predicate<Predicate&, const measure_type&>
    [[nodiscard]] finger_tree_cursor_search_result<value_type, measure_policy>
    get_cursor_by_measure(Predicate predicate) const;

    /// Returns the first stored element in worst-case O(1) structural time.
    /// The reference follows the lifetime of this snapshot or sharing storage.
    [[nodiscard]] const value_type& front() const
    {
        throw_if_empty();
        return root_.first_element().value();
    }

    /// Returns the last stored element in worst-case O(1) structural time.
    /// The reference follows the lifetime of this snapshot or sharing storage.
    [[nodiscard]] const value_type& back() const
    {
        throw_if_empty();
        return root_.last_element().value();
    }

    /// Returns a snapshot with `value` at the front. Structural cost is
    /// amortized O(1); no existing snapshot is mutated.
    [[nodiscard]] finger_tree prepend(value_type value) const
    {
        return finger_tree{root_.cons(detail::measured_element<Element, MeasurePolicy>::leaf(std::move(value)))};
    }

    /// Returns a snapshot with `value` at the back. Structural cost is
    /// amortized O(1); no existing snapshot is mutated.
    [[nodiscard]] finger_tree append(value_type value) const
    {
        return finger_tree{root_.snoc(detail::measured_element<Element, MeasurePolicy>::leaf(std::move(value)))};
    }

    /// Catenates two snapshots while retaining untouched storage from both.
    /// Work follows the involved middle spines; this does not enumerate either
    /// input or materialize an intermediate sequence.
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
    /// Splits before the first element whose inclusive prefix measure makes the
    /// monotone predicate true. If it never becomes true, `left` is this tree.
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
    /// Locates the first element whose inclusive prefix measure satisfies the
    /// monotone predicate without rebuilding either side of the boundary.
    [[nodiscard]] finger_tree_locate_result<Element, MeasurePolicy> try_locate(Predicate predicate) const
    {
        if (root_.is_empty()) {
            return finger_tree_locate_result<Element, MeasurePolicy>{MeasurePolicy::empty(), std::nullopt};
        }

        auto total = root_.measure();
        if (!std::invoke(predicate, total)) {
            return finger_tree_locate_result<Element, MeasurePolicy>{std::move(total), std::nullopt};
        }

        auto located = root_.locate_tree(predicate, MeasurePolicy::empty());
        return finger_tree_locate_result<Element, MeasurePolicy>{std::move(located.measure_before), located.hit.value()};
    }

    // The returned pointer remains valid while this immutable tree (or a snapshot
    // sharing the located node) remains alive.
    template <class Predicate>
        requires std::predicate<Predicate&, const measure_type&>
    [[nodiscard]] finger_tree_locate_reference_result<Element, MeasurePolicy> try_locate_reference(
        Predicate predicate) const
    {
        if (root_.is_empty()) {
            return finger_tree_locate_reference_result<Element, MeasurePolicy>{MeasurePolicy::empty(), nullptr};
        }

        auto total = root_.measure();
        if (!std::invoke(predicate, total)) {
            return finger_tree_locate_reference_result<Element, MeasurePolicy>{std::move(total), nullptr};
        }

        auto located = root_.locate_tree_reference(predicate, MeasurePolicy::empty());
        return finger_tree_locate_reference_result<Element, MeasurePolicy>{
            std::move(located.measure_before),
            &located.hit->value()};
    }

    /// Materializes all values in logical order. Prefer iteration, `for_each`,
    /// or `copy_to` when an owning contiguous result is not required.
    [[nodiscard]] std::vector<value_type> to_vector() const
    {
        auto result = std::vector<value_type>{};
        root_.flatten(result);
        return result;
    }

    template <class Function>
        requires std::invocable<Function&, const value_type&>
    void for_each(Function function) const
    {
        root_.for_each(function);
    }

    template <std::output_iterator<const value_type&> OutputIterator>
    void copy_to(OutputIterator output) const
    {
        auto write = [&output](const value_type& value) {
            *output++ = value;
        };
        root_.for_each(write);
    }

    /// Creates a forward traversal cursor. Construction retains the immutable
    /// root and prepares O(log n) traversal state; prefix increment performs no
    /// iterator-bookkeeping allocations.
    [[nodiscard]] const_iterator begin() const
    {
        return const_iterator{root_};
    }

    [[nodiscard]] const_iterator end() const noexcept
    {
        return const_iterator{};
    }

    [[nodiscard]] const_iterator cbegin() const
    {
        return begin();
    }

    [[nodiscard]] const_iterator cend() const noexcept
    {
        return end();
    }

    class const_iterator final {
    public:
        using iterator_concept = std::forward_iterator_tag;
        using iterator_category = std::forward_iterator_tag;
        using value_type = Element;
        using difference_type = std::ptrdiff_t;
        using pointer = const Element*;
        using reference = const Element&;

        const_iterator() = default;

        const_iterator(const const_iterator& other)
            : root_owner_(other.root_owner_)
            , current_(other.current_)
            , owner_(other.owner_)
            , position_(other.position_)
        {
            stack_.reserve(other.stack_.capacity());
            stack_.insert(stack_.end(), other.stack_.begin(), other.stack_.end());
        }

        const_iterator& operator=(const const_iterator& other)
        {
            if (this != &other) {
                auto copy = other;
                *this = std::move(copy);
            }
            return *this;
        }

        const_iterator(const_iterator&&) noexcept = default;
        const_iterator& operator=(const_iterator&&) noexcept = default;

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

            return left.owner_ == right.owner_ && left.position_ == right.position_;
        }

    private:
        friend class finger_tree;

        using tree_type = detail::measured_tree<Element, MeasurePolicy>;
        using element_type = detail::measured_element<Element, MeasurePolicy>;
        using node_pointer = typename element_type::node_pointer;
        using child_type = detail::measured_enumeration_child<Element, MeasurePolicy>;
        using child_kind = typename child_type::child_kind;

        struct frame final {
            enum class frame_kind {
                tree,
                node,
            };

            explicit frame(tree_type value)
                : kind(frame_kind::tree)
                , tree(std::move(value))
            {
            }

            explicit frame(node_pointer value)
                : kind(frame_kind::node)
                , node(std::move(value))
            {
            }

            frame_kind kind;
            tree_type tree;
            node_pointer node;
            size_type next_child = 0;

            [[nodiscard]] size_type child_count() const
            {
                return kind == frame_kind::tree ? tree.enumeration_child_count() : node->children().size();
            }

            [[nodiscard]] child_type child(const size_type index) const
            {
                if (kind == frame_kind::tree) {
                    return tree.enumeration_child(index);
                }

                const auto& element = node->children()[index];
                if (element.is_leaf()) {
                    return child_type{
                        child_kind::leaf,
                        &element.value(),
                        tree_type{},
                        {}};
                }

                return child_type{
                    child_kind::node,
                    nullptr,
                    tree_type{},
                    element.node_ptr()};
            }
        };

        explicit const_iterator(tree_type root)
            : root_owner_(root)
            , owner_(root.identity())
        {
            if (!root.is_empty()) {
                const auto depth = root.enumeration_depth();
                // At most one tree frame per middle-spine level and one node
                // frame per descent level can be live at once, plus the root
                // and terminal frames. Reserving this proven bound makes
                // prefix increment allocation-free after begin().
                stack_.reserve(checked_add(checked_add(depth, depth), size_type{2}));
                stack_.push_back(frame{std::move(root)});
                advance();
            }
        }

        void advance()
        {
            if (current_ != nullptr) {
                ++position_;
            }

            current_ = nullptr;
            while (!stack_.empty()) {
                auto& top = stack_.back();
                if (top.next_child == top.child_count()) {
                    stack_.pop_back();
                    continue;
                }

                auto child = top.child(top.next_child++);
                switch (child.kind) {
                case child_kind::leaf:
                    current_ = child.leaf;
                    return;
                case child_kind::tree:
                    if (!child.tree.is_empty()) {
                        stack_.push_back(frame{std::move(child.tree)});
                    }
                    break;
                case child_kind::node:
                    stack_.push_back(frame{std::move(child.node)});
                    break;
                }
            }
        }

        tree_type root_owner_;
        std::vector<frame> stack_;
        const Element* current_ = nullptr;
        const void* owner_ = nullptr;
        size_type position_ = 0;
    };

private:
    friend class finger_tree_cursor<Element, MeasurePolicy>;

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

    void throw_if_empty() const
    {
        if (empty()) {
            throw std::logic_error("finger_tree is empty");
        }
    }

    root_type root_;
};

/// An immutable measure-aware gap cursor over one exact general finger-tree snapshot.
///
/// Arbitrary monoids expose ordered measures and neighbors rather than a fabricated public count
/// or element position. The retained left and right trees preserve cached leaf measures while the
/// gap moves, so navigation does not remeasure existing payloads.
template <class Element, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, Element>
class finger_tree_cursor final {
public:
    using value_type = Element;
    using measure_policy = MeasurePolicy;
    using measure_type = typename measure_policy::measure_type;

    finger_tree_cursor() = delete;
    finger_tree_cursor(const finger_tree_cursor&) = default;
    finger_tree_cursor& operator=(const finger_tree_cursor&) = default;

    finger_tree_cursor(finger_tree_cursor&& other) noexcept(
        std::is_nothrow_copy_constructible_v<finger_tree<value_type, measure_policy>>)
        : snapshot_(other.snapshot_)
        , left_(other.left_)
        , right_(other.right_)
    {
    }

    finger_tree_cursor& operator=(finger_tree_cursor&& other) noexcept(
        std::is_nothrow_copy_assignable_v<finger_tree<value_type, measure_policy>>)
    {
        if (this != &other) {
            snapshot_ = other.snapshot_;
            left_ = other.left_;
            right_ = other.right_;
        }
        return *this;
    }

    [[nodiscard]] bool is_at_start() const noexcept { return left_.empty(); }
    [[nodiscard]] bool is_at_end() const noexcept { return right_.empty(); }
    [[nodiscard]] measure_type measure_before() const { return left_.measure(); }
    [[nodiscard]] measure_type measure_after() const { return right_.measure(); }

    [[nodiscard]] const value_type* try_peek_previous() const &
    {
        return left_.empty() ? nullptr : &left_.back();
    }

    const value_type* try_peek_previous() const && = delete;

    [[nodiscard]] const value_type* try_peek_next() const &
    {
        return right_.empty() ? nullptr : &right_.front();
    }

    const value_type* try_peek_next() const && = delete;

    [[nodiscard]] finger_tree_cursor move_previous() const
    {
        const auto view = left_.root_.try_view_right();
        if (!view.has_value()) {
            throw std::logic_error("finger-tree cursor is already at the start");
        }
        return finger_tree_cursor{
            snapshot_,
            finger_tree<value_type, measure_policy>::wrap(view->rest),
            finger_tree<value_type, measure_policy>::wrap(right_.root_.cons(view->value))};
    }

    [[nodiscard]] finger_tree_cursor move_next() const
    {
        const auto view = right_.root_.try_view_left();
        if (!view.has_value()) {
            throw std::logic_error("finger-tree cursor is already at the end");
        }
        return finger_tree_cursor{
            snapshot_,
            finger_tree<value_type, measure_policy>::wrap(left_.root_.snoc(view->value)),
            finger_tree<value_type, measure_policy>::wrap(view->rest)};
    }

    template <class Predicate>
        requires std::predicate<Predicate&, const measure_type&>
    [[nodiscard]] finger_tree_cursor_search_result<value_type, measure_policy>
    seek_by_measure(Predicate predicate) const;

    [[nodiscard]] finger_tree_cursor insert(value_type value) const
    {
        auto left = left_.append(std::move(value));
        auto snapshot = left.concat(right_);
        return finger_tree_cursor{snapshot, std::move(left), right_};
    }

    [[nodiscard]] finger_tree_cursor delete_previous() const
    {
        const auto view = left_.root_.try_view_right();
        if (!view.has_value()) {
            throw std::logic_error("finger-tree cursor has no previous element");
        }
        auto left = finger_tree<value_type, measure_policy>::wrap(view->rest);
        auto snapshot = left.concat(right_);
        return finger_tree_cursor{snapshot, std::move(left), right_};
    }

    [[nodiscard]] finger_tree_cursor delete_next() const
    {
        const auto view = right_.root_.try_view_left();
        if (!view.has_value()) {
            throw std::logic_error("finger-tree cursor has no next element");
        }
        auto right = finger_tree<value_type, measure_policy>::wrap(view->rest);
        auto snapshot = left_.concat(right);
        return finger_tree_cursor{snapshot, left_, std::move(right)};
    }

    [[nodiscard]] finger_tree_cursor replace_next(value_type value) const
    {
        const auto view = right_.root_.try_view_left();
        if (!view.has_value()) {
            throw std::logic_error("finger-tree cursor has no next element");
        }
        auto replacement = detail::measured_element<value_type, measure_policy>::leaf(std::move(value));
        auto right = finger_tree<value_type, measure_policy>::wrap(view->rest.cons(std::move(replacement)));
        auto snapshot = left_.concat(right);
        return finger_tree_cursor{snapshot, left_, std::move(right)};
    }

    [[nodiscard]] finger_tree<value_type, measure_policy> snapshot() const { return snapshot_; }

private:
    friend class finger_tree<Element, MeasurePolicy>;

    finger_tree_cursor(
        finger_tree<value_type, measure_policy> snapshot,
        finger_tree<value_type, measure_policy> left,
        finger_tree<value_type, measure_policy> right)
        : snapshot_(std::move(snapshot))
        , left_(std::move(left))
        , right_(std::move(right))
    {
    }

    finger_tree<value_type, measure_policy> snapshot_;
    finger_tree<value_type, measure_policy> left_;
    finger_tree<value_type, measure_policy> right_;
};

/// Result of inclusive-prefix measure cursor search. A miss retains a usable end cursor.
template <class Element, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, Element>
struct finger_tree_cursor_search_result final {
    finger_tree_cursor<Element, MeasurePolicy> cursor;
    bool found = false;
};

template <class Element, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, Element>
template <class Predicate>
    requires std::predicate<Predicate&, const typename MeasurePolicy::measure_type&>
[[nodiscard]] finger_tree_cursor_search_result<Element, MeasurePolicy>
finger_tree_cursor<Element, MeasurePolicy>::seek_by_measure(Predicate predicate) const
{
    return snapshot_.get_cursor_by_measure(std::move(predicate));
}

template <class Element, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, Element>
[[nodiscard]] finger_tree_cursor<Element, MeasurePolicy>
finger_tree<Element, MeasurePolicy>::get_cursor_at_start() const
{
    return finger_tree_cursor<value_type, measure_policy>{*this, finger_tree{}, *this};
}

template <class Element, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, Element>
[[nodiscard]] finger_tree_cursor<Element, MeasurePolicy>
finger_tree<Element, MeasurePolicy>::get_cursor_at_end() const
{
    return finger_tree_cursor<value_type, measure_policy>{*this, *this, finger_tree{}};
}

template <class Element, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, Element>
template <class Predicate>
    requires std::predicate<Predicate&, const typename MeasurePolicy::measure_type&>
[[nodiscard]] finger_tree_cursor_search_result<Element, MeasurePolicy>
finger_tree<Element, MeasurePolicy>::get_cursor_by_measure(Predicate predicate) const
{
    auto split_result = split(std::move(predicate));
    const auto found = !split_result.right.empty();
    return finger_tree_cursor_search_result<value_type, measure_policy>{
        finger_tree_cursor<value_type, measure_policy>{
            *this,
            std::move(split_result.left),
            std::move(split_result.right)},
        found};
}

template <class Element, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, Element> && equality_comparable_value<Element>
[[nodiscard]] bool operator==(
    const finger_tree_split<Element, MeasurePolicy>& left,
    const finger_tree_split<Element, MeasurePolicy>& right)
{
    return detail::sequence_equal(left.left, right.left)
        && detail::sequence_equal(left.right, right.right);
}

template <class Element, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, Element> && equality_comparable_value<Element>
[[nodiscard]] bool operator==(
    const finger_tree_item_split<Element, MeasurePolicy>& left,
    const finger_tree_item_split<Element, MeasurePolicy>& right)
{
    return detail::sequence_equal(left.left, right.left)
        && left.item == right.item
        && detail::sequence_equal(left.right, right.right);
}

template <class Element, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, Element> && equality_comparable_value<Element>
[[nodiscard]] bool operator==(
    const finger_tree_extract_result<Element, MeasurePolicy>& left,
    const finger_tree_extract_result<Element, MeasurePolicy>& right)
{
    return left.item == right.item && detail::sequence_equal(left.rest, right.rest);
}

} // namespace tools::data_structures::finger_tree
