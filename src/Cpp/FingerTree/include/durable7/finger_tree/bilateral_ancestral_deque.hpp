/// A persistent deque held as two oppositely oriented intervals of an append-only ancestry arena.
///
/// This is not a balanced sequence tree. A version is a constant-sized handle over one
/// `incremental_ancestor_arena`: it stores a left interval `(l_anchor, l_tail]` and a right interval
/// `(r_anchor, r_tail]`, and its logical value is `reverse(left)` followed by `right`. A front
/// insertion appends a leaf below the left tail; a back insertion appends one below the right tail.
/// Reversal only exchanges the two intervals, so it costs O(1) and needs no lazy flag or rebuild.
///
/// The load-bearing property is *slice closure*: every contiguous slice intersects each interval at
/// most once, so a slice is again a bilateral handle rather than a rebuilt tree. Slicing and
/// splitting each cost at most **two** level-ancestor queries, including every cross-arm case;
/// indexing costs at most one; end removal costs none on its own nonempty arm and at most one when
/// it crosses the centre; and the structural audit costs at most four. Endpoint reads, `size`,
/// `clear`, and `reverse` are O(1) and query-free.
///
/// Two layers of complexity claim must not be conflated.
///
/// 1. **The reduction.** Let `U(M)` be arena leaf-add time and `Q(M)` be arena level-ancestor query
///    time after `M` published nodes. End insertion costs `U(M)`; end removal, indexing, slicing,
///    splitting, and `validate_structure` cost `O(1 + Q(M))` with the query ceilings above.
///    Enumeration is `Theta(n)`.
/// 2. **The optimal backend.** An Alstrup-Holm incremental level-ancestor backend has
///    `U(M) = Q(M) = O(1)` worst case in linear space, which makes every scalar operation of this
///    restricted algebra O(1) worst case. No such backend is included here.
///
/// The shipped `myers_incremental_ancestor_arena` does **not** realize that theorem. Backed by it,
/// end insertion is O(1) *amortized*; endpoint reads, `size`, `reverse`, and `clear` are O(1) worst
/// case; and removal, indexing, slicing, splitting, and the audit are O(log M) worst case after `M`
/// historical insertions. Nothing here upgrades an amortized or logarithmic bound to a worst-case
/// constant one.
///
/// The algebra is deliberately restricted: add or remove at either end, read either end or an
/// indexed value, reverse, take/drop/slice/split, and enumerate front to back. Arbitrary
/// concatenation of unrelated versions, point replacement, and middle editing are intentionally
/// absent, and no comparison here treats an omitted operation as fast. Arena space is charged to all
/// successful historical end insertions, not merely to the values visible from one handle: a handle
/// retains its arena, and the arena retains every node it ever published.
///
/// Where the managed ports inject the backend through a runtime interface, this workspace expresses
/// the seam the way it expresses every other policy: `Arena` is a template parameter constrained by
/// `incremental_ancestor_arena` and defaulted to `myers_incremental_ancestor_arena<T>`. A different
/// backend is a different type rather than a virtual dispatch, and the observable contract is
/// unchanged. There is no process-global empty value: an empty deque belongs to an arena, and
/// retaining it retains that arena.

#pragma once

#include <durable7/finger_tree/incremental_ancestor.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

namespace durable7::finger_tree {

/// The cached representation counts reported by a bilateral handle's structural audit.
struct bilateral_ancestral_deque_statistics final {
    /// The number of visible values, which equals `left_count + right_count`.
    std::size_t count = 0;
    /// The number of values held by the reversed left interval.
    std::size_t left_count = 0;
    /// The number of values held by the forward right interval.
    std::size_t right_count = 0;

    friend bool operator==(
        const bilateral_ancestral_deque_statistics&,
        const bilateral_ancestral_deque_statistics&) = default;
};

template <typename T, typename Arena = myers_incremental_ancestor_arena<T>>
    requires incremental_ancestor_arena<Arena, T>
class bilateral_ancestral_deque;

template <typename T, typename Arena>
    requires incremental_ancestor_arena<Arena, T>
struct bilateral_ancestral_deque_split;

template <typename T, typename Arena>
    requires incremental_ancestor_arena<Arena, T>
struct bilateral_ancestral_deque_pop;

/// An immutable deque held as two oppositely oriented intervals of an append-only ancestry arena.
///
/// A handle is an ordinary copyable value whose copy is cheap because it shares immutable state: it
/// carries a retained arena pointer, the two intervals, and the visible count. Every operation
/// returns a new value and leaves its inputs usable, so retained versions stay valid forever.
///
/// Concurrency is exactly the arena's. The shipped Myers arena serializes its own operations with a
/// private mutex, so independent handles over one shared arena may be used from several threads;
/// one handle value, like any other C++ value, still must not be written while it is read.
template <typename T, typename Arena>
    requires incremental_ancestor_arena<Arena, T>
class bilateral_ancestral_deque final {
private:
    /// One oriented ancestry interval `(anchor, tail]`.
    ///
    /// `base` is redundant but load-bearing: it caches the first physical node after `anchor`, which
    /// keeps both endpoint reads query-free even when the opposite arm is empty. For the left arm
    /// the logical first value sits at `tail` and the logical last at `base`; for the right arm
    /// those roles are exchanged. An empty interval has `anchor == base == tail` and count zero.
    struct segment final {
        std::size_t anchor = 0;
        std::size_t base = 0;
        std::size_t tail = 0;
        std::size_t count = 0;

        /// The empty interval anchored at one node.
        [[nodiscard]] static constexpr segment empty_at(const std::size_t node) noexcept
        {
            return segment{node, node, node, 0};
        }

        friend constexpr bool operator==(const segment&, const segment&) noexcept = default;
    };

public:
    /// The element type retained by the arena.
    using value_type = T;
    /// The level-ancestor backend this handle publishes into.
    using arena_type = Arena;
    /// The type used for counts and visible indexes.
    using size_type = std::size_t;
    /// The type used for index distances.
    using difference_type = std::ptrdiff_t;
    /// A borrowed reference to one stored value.
    using const_reference = const T&;
    /// A borrowed pointer to one stored value.
    using const_pointer = const T*;

    /// The two persistent versions produced by a split.
    using split_result = bilateral_ancestral_deque_split<T, Arena>;
    /// One endpoint value and the version remaining after removing it.
    using pop_result = bilateral_ancestral_deque_pop<T, Arena>;

    class const_iterator;
    /// This container exposes only immutable traversal.
    using iterator = const_iterator;

    /// Creates an empty deque over a fresh, independently released arena of the default backend.
    ///
    /// Every version derived from the result shares that arena, which is released once the last of
    /// those versions is. This construction is O(1).
    ///
    /// @throws std::invalid_argument when the fresh arena's bottom node is not at depth `-1`.
    bilateral_ancestral_deque()
        requires std::default_initializable<Arena>
        : bilateral_ancestral_deque(create(std::make_shared<Arena>()))
    {
    }

    /// Creates an empty deque owned by an explicit level-ancestor arena, which it retains.
    ///
    /// The arena retains and navigates every inserted node and therefore determines the bounds this
    /// deque inherits. Sharing one arena between independent deque families is allowed; the arena is
    /// append-only, so their nodes simply interleave. This creation is O(1) and publishes nothing.
    ///
    /// @throws std::invalid_argument when `arena` is null, or its bottom node is not at depth `-1`.
    [[nodiscard]] static bilateral_ancestral_deque create(std::shared_ptr<Arena> arena)
    {
        if (arena == nullptr) {
            throw std::invalid_argument("the incremental-ancestor arena must not be null");
        }

        const auto bottom = arena->bottom();
        if (arena->depth_of(bottom) != -1) {
            throw std::invalid_argument("the arena bottom node must have depth -1");
        }

        const auto empty_interval = segment::empty_at(bottom);
        return bilateral_ancestral_deque{std::move(arena), empty_interval, empty_interval, 0};
    }

    /// Creates a deque holding a range's values in enumeration order, appending each at the back.
    ///
    /// The result owns a fresh arena of the default backend. This costs one arena leaf addition per
    /// value and makes no level-ancestor query.
    ///
    /// @throws std::overflow_error when the visible count or the ancestry depth cannot grow further.
    template <std::ranges::input_range R>
        requires std::default_initializable<Arena>
              && std::convertible_to<std::ranges::range_reference_t<R>, T>
    [[nodiscard]] static bilateral_ancestral_deque from_range(R&& values)
    {
        auto result = bilateral_ancestral_deque{};
        for (auto&& value : values) {
            result = result.push_back(std::forward<decltype(value)>(value));
        }
        return result;
    }

    /// Creates a deque holding an initializer list's values in order, appending each at the back.
    ///
    /// @throws std::overflow_error when the visible count or the ancestry depth cannot grow further.
    [[nodiscard]] static bilateral_ancestral_deque from_range(std::initializer_list<T> values)
        requires std::default_initializable<Arena>
    {
        auto result = bilateral_ancestral_deque{};
        for (const auto& value : values) {
            result = result.push_back(value);
        }
        return result;
    }

    /// The number of visible values. This read is O(1).
    [[nodiscard]] size_type size() const noexcept
    {
        return count_;
    }

    /// Whether this handle denotes an empty deque. This read is O(1).
    [[nodiscard]] bool empty() const noexcept
    {
        return count_ == 0;
    }

    /// The arena this deque and every version derived from it publishes into. This read is O(1).
    [[nodiscard]] const std::shared_ptr<Arena>& arena() const noexcept
    {
        return arena_;
    }

    /// Whether both handles denote the very same retained storage: one arena and two equal
    /// intervals.
    ///
    /// Because a handle is a value rather than a reference, this is how an operation that returned
    /// its receiver unchanged is distinguished from one that rebuilt an equal value. Two handles
    /// holding opposite orientations of the same values do not share storage. This read is O(1).
    [[nodiscard]] bool shares_storage_with(const bilateral_ancestral_deque& other) const noexcept
    {
        return arena_ == other.arena_ && left_ == other.left_ && right_ == other.right_
            && count_ == other.count_;
    }

    /// The first visible value, read from one cached handle with no level-ancestor query. O(1).
    ///
    /// The reference stays valid while this handle, or any other version retaining the same arena,
    /// is alive.
    ///
    /// @throws std::logic_error when the deque is empty.
    [[nodiscard]] const_reference front() const
    {
        throw_if_empty();
        return arena_->value_at(left_.count != 0 ? left_.tail : right_.base);
    }

    /// The last visible value, read from one cached handle with no level-ancestor query. O(1).
    ///
    /// @throws std::logic_error when the deque is empty.
    [[nodiscard]] const_reference back() const
    {
        throw_if_empty();
        return arena_->value_at(right_.count != 0 ? right_.tail : left_.base);
    }

    /// The first visible value, or null when the deque is empty. O(1) and query-free.
    [[nodiscard]] const_pointer try_front() const
    {
        return empty() ? nullptr : &front();
    }

    /// The last visible value, or null when the deque is empty. O(1) and query-free.
    [[nodiscard]] const_pointer try_back() const
    {
        return empty() ? nullptr : &back();
    }

    /// The value at a zero-based visible index, costing at most one level-ancestor query.
    ///
    /// The four cached endpoints — visible index zero, the end of the left interval, the first right
    /// value, and the last visible index — are answered with no query at all.
    ///
    /// @throws std::out_of_range when `index` is not below `size()`.
    [[nodiscard]] const_reference at(const size_type index) const
    {
        if (index >= count_) {
            throw std::out_of_range("the index is outside the bilateral ancestral deque");
        }

        return arena_->value_at(node_at(index));
    }

    /// The value at a zero-based visible index; see `at`.
    ///
    /// @throws std::out_of_range when `index` is not below `size()`.
    [[nodiscard]] const_reference operator[](const size_type index) const
    {
        return at(index);
    }

    /// A deque with one value added at the front. The receiver is unchanged.
    ///
    /// Publishes exactly one arena leaf and makes no level-ancestor query, so it costs one arena
    /// leaf addition: O(1) amortized with the shipped Myers arena.
    ///
    /// @throws std::overflow_error when the visible count or the ancestry depth cannot grow further.
    [[nodiscard]] bilateral_ancestral_deque push_front(T value) const
    {
        const auto next = next_count();
        const auto left = add_to_tail(left_, std::move(value));
        return bilateral_ancestral_deque{arena_, left, right_, next};
    }

    /// A deque with one value added at the back. The receiver is unchanged.
    ///
    /// Publishes exactly one arena leaf and makes no level-ancestor query, so it costs one arena
    /// leaf addition: O(1) amortized with the shipped Myers arena.
    ///
    /// @throws std::overflow_error when the visible count or the ancestry depth cannot grow further.
    [[nodiscard]] bilateral_ancestral_deque push_back(T value) const
    {
        const auto next = next_count();
        const auto right = add_to_tail(right_, std::move(value));
        return bilateral_ancestral_deque{arena_, left_, right, next};
    }

    /// The deque without its first value.
    ///
    /// Makes no level-ancestor query when the left arm is nonempty, and at most one when the removal
    /// crosses the centre into the right arm. The final two values of a crossing run are answered
    /// from cache alone.
    ///
    /// @throws std::logic_error when the deque is empty.
    [[nodiscard]] bilateral_ancestral_deque remove_first() const
    {
        throw_if_empty();
        if (count_ == 1) {
            return empty_handle();
        }
        if (left_.count != 0) {
            return bilateral_ancestral_deque{arena_, remove_tail(left_), right_, count_ - 1};
        }

        return bilateral_ancestral_deque{arena_, left_, remove_base(right_), count_ - 1};
    }

    /// The deque without its last value.
    ///
    /// Makes no level-ancestor query when the right arm is nonempty, and at most one when the
    /// removal crosses the centre into the left arm.
    ///
    /// @throws std::logic_error when the deque is empty.
    [[nodiscard]] bilateral_ancestral_deque remove_last() const
    {
        throw_if_empty();
        if (count_ == 1) {
            return empty_handle();
        }
        if (right_.count != 0) {
            return bilateral_ancestral_deque{arena_, left_, remove_tail(right_), count_ - 1};
        }

        return bilateral_ancestral_deque{arena_, remove_base(left_), right_, count_ - 1};
    }

    /// The first value together with the deque remaining, or nothing when the deque is empty.
    ///
    /// The borrowed value pointer stays valid while the returned result, which retains the arena
    /// through its remainder, is alive. Costs exactly what `remove_first` costs.
    [[nodiscard]] std::optional<pop_result> try_pop_front() const;

    /// The last value together with the deque remaining, or nothing when the deque is empty.
    ///
    /// The borrowed value pointer stays valid while the returned result, which retains the arena
    /// through its remainder, is alive. Costs exactly what `remove_last` costs.
    [[nodiscard]] std::optional<pop_result> try_pop_back() const;

    /// The first `count` values. A prefix is a slice, so it costs at most two level-ancestor
    /// queries.
    ///
    /// A whole-length prefix returns a value sharing this handle's storage.
    ///
    /// @throws std::out_of_range when `count` exceeds `size()`.
    [[nodiscard]] bilateral_ancestral_deque take(const size_type count) const
    {
        if (count > count_) {
            throw std::out_of_range("the prefix length is outside the bilateral ancestral deque");
        }

        return count == count_ ? *this : get_range(0, count);
    }

    /// The deque after omitting its first `count` values. A suffix is a slice, so it costs at most
    /// two level-ancestor queries.
    ///
    /// Omitting nothing returns a value sharing this handle's storage.
    ///
    /// @throws std::out_of_range when `count` exceeds `size()`.
    [[nodiscard]] bilateral_ancestral_deque drop(const size_type count) const
    {
        if (count > count_) {
            throw std::out_of_range("the suffix length is outside the bilateral ancestral deque");
        }

        return count == 0 ? *this : get_range(count, count_ - count);
    }

    /// One contiguous slice, using at most **two** level-ancestor queries.
    ///
    /// Every contiguous slice intersects each interval at most once, so the result is another
    /// bilateral handle and the cost is independent of how many values it selects. Each arm spends
    /// at most one query, including in the cross-arm cases. An empty slice and a whole-range slice
    /// each spend none, and a whole-range slice returns a value sharing this handle's storage.
    ///
    /// @throws std::out_of_range when the index/count pair is outside this deque.
    [[nodiscard]] bilateral_ancestral_deque get_range(
        const size_type index,
        const size_type count) const
    {
        if (index > count_) {
            throw std::out_of_range("the slice index is outside the bilateral ancestral deque");
        }
        if (count > count_ - index) {
            throw std::out_of_range("the slice count is outside the bilateral ancestral deque");
        }
        if (index == 0 && count == count_) {
            return *this;
        }
        if (count == 0) {
            return empty_handle();
        }

        const auto end = index + count;
        const auto left_start = (std::min)(index, left_.count);
        const auto left_end = (std::min)(end, left_.count);
        const auto right_start = index > left_.count ? index - left_.count : size_type{0};
        const auto right_end = end > left_.count ? end - left_.count : size_type{0};

        const auto left = slice_left(left_, left_start, left_end - left_start);
        const auto right = slice_right(right_, right_start, right_end - right_start);
        return bilateral_ancestral_deque{arena_, left, right, count};
    }

    /// The prefix and suffix at a zero-based boundary; together they enumerate the original value.
    ///
    /// Although the split is expressed as a prefix plus a suffix, the pair costs at most two
    /// level-ancestor queries in total. Both endpoint boundaries are query-free and return values
    /// sharing this handle's storage.
    ///
    /// @throws std::out_of_range when `index` exceeds `size()`.
    [[nodiscard]] split_result split_at(size_type index) const;

    /// The logical reversal, obtained by exchanging the two oriented intervals. O(1), no queries.
    ///
    /// A deque of at most one value returns a value sharing this handle's storage, because
    /// exchanging the arms of such a handle would change its representation without changing its
    /// sequence.
    [[nodiscard]] bilateral_ancestral_deque reverse() const
    {
        if (count_ <= 1) {
            return *this;
        }

        return bilateral_ancestral_deque{arena_, right_, left_, count_};
    }

    /// An empty, independently appendable handle in the same arena. O(1), no queries.
    ///
    /// Clearing an already empty deque returns a value sharing this handle's storage.
    [[nodiscard]] bilateral_ancestral_deque clear() const
    {
        return empty() ? *this : empty_handle();
    }

    /// The retained values copied into a vector, in logical order.
    ///
    /// `Theta(size())` time with no level-ancestor query: both arms stream through parent links.
    [[nodiscard]] std::vector<T> to_vector() const
    {
        auto values = std::vector<T>{};
        values.reserve(count_);

        auto node = left_.tail;
        for (auto index = size_type{0}; index != left_.count; ++index) {
            values.push_back(arena_->value_at(node));
            if (index + 1 != left_.count) {
                node = arena_->parent_of(node);
            }
        }

        const auto start = values.size();
        node = right_.tail;
        for (auto remaining = right_.count; remaining != 0; --remaining) {
            values.push_back(arena_->value_at(node));
            if (remaining != 1) {
                node = arena_->parent_of(node);
            }
        }

        std::reverse(values.begin() + static_cast<difference_type>(start), values.end());
        return values;
    }

    /// A front-to-back iterator over this immutable handle.
    ///
    /// Enumeration takes `Theta(size())` time and makes no level-ancestor query. The reversed left
    /// interval streams through parent links with constant iterator state, while the
    /// forward-oriented right interval is buffered when the iterator is created, so an iterator can
    /// retain `O(size())` node handles in the worst case.
    [[nodiscard]] const_iterator begin() const
    {
        auto buffer = std::shared_ptr<const std::vector<std::size_t>>{};
        if (right_.count != 0) {
            auto nodes = std::vector<std::size_t>(right_.count, std::size_t{0});
            auto node = right_.tail;
            for (auto remaining = right_.count; remaining != 0; --remaining) {
                nodes[remaining - 1] = node;
                if (remaining != 1) {
                    node = arena_->parent_of(node);
                }
            }
            buffer = std::make_shared<const std::vector<std::size_t>>(std::move(nodes));
        }

        return const_iterator{arena_, left_.tail, left_.count, std::move(buffer), count_};
    }

    /// The end of the front-to-back traversal. O(1).
    [[nodiscard]] const_iterator end() const noexcept
    {
        return const_iterator{};
    }

    /// A front-to-back iterator over this immutable handle; see `begin`.
    [[nodiscard]] const_iterator cbegin() const
    {
        return begin();
    }

    /// The end of the front-to-back traversal; see `end`.
    [[nodiscard]] const_iterator cend() const noexcept
    {
        return end();
    }

    /// Checks the cached interval endpoints and counts of this handle and reports its counts.
    ///
    /// The audit inspects a constant number of cached fields: it compares the cached counts against
    /// the interval endpoint depths and confirms each cached base and anchor lies on its tail's
    /// ancestry path. Confirming the ancestry path costs **two** level-ancestor queries per nonempty
    /// interval, so the audit costs `O(1 + Q(M))` with a ceiling of four queries — not O(1) work.
    /// An empty handle spends no query. It ports the C# internal `ValidateInvariants` audit.
    ///
    /// @throws std::logic_error on the first invariant violation observed.
    [[nodiscard]] bilateral_ancestral_deque_statistics validate_structure() const
    {
        if (left_.count > (std::numeric_limits<size_type>::max)() - right_.count) {
            throw std::logic_error("the interval counts overflow the visible count");
        }
        if (count_ != left_.count + right_.count) {
            throw std::logic_error("the total count disagrees with the interval counts");
        }

        validate_segment(left_);
        validate_segment(right_);
        return bilateral_ancestral_deque_statistics{count_, left_.count, right_.count};
    }

    /// A front-to-back forward iterator over one immutable bilateral handle.
    ///
    /// Two iterators are comparable only when they traverse the same handle; equality compares the
    /// number of values still to be produced, so the end iterator is the default-constructed one.
    class const_iterator final {
    public:
        using iterator_category = std::forward_iterator_tag;
        using iterator_concept = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        /// The past-the-end iterator, which belongs to no handle.
        const_iterator() = default;

        /// The value the iterator denotes. O(1), no level-ancestor query.
        [[nodiscard]] reference operator*() const
        {
            return arena_->value_at(
                left_remaining_ != 0 ? left_node_ : (*right_nodes_)[right_index_]);
        }

        /// A borrowed pointer to the value the iterator denotes. O(1).
        [[nodiscard]] pointer operator->() const
        {
            return std::addressof(**this);
        }

        /// Advances to the next value. O(1), no level-ancestor query.
        const_iterator& operator++()
        {
            if (left_remaining_ != 0) {
                --left_remaining_;
                if (left_remaining_ != 0) {
                    left_node_ = arena_->parent_of(left_node_);
                }
            } else {
                ++right_index_;
            }

            --remaining_;
            return *this;
        }

        /// Advances to the next value and returns the previous position. O(1).
        const_iterator operator++(int)
        {
            auto previous = *this;
            ++*this;
            return previous;
        }

        /// Whether both iterators stand at the same position of one traversal. O(1).
        [[nodiscard]] friend bool operator==(
            const const_iterator& left,
            const const_iterator& right) noexcept
        {
            return left.remaining_ == right.remaining_;
        }

    private:
        friend class bilateral_ancestral_deque;

        const_iterator(
            std::shared_ptr<Arena> arena,
            const std::size_t left_node,
            const std::size_t left_remaining,
            std::shared_ptr<const std::vector<std::size_t>> right_nodes,
            const std::size_t remaining)
            : arena_(std::move(arena))
            , right_nodes_(std::move(right_nodes))
            , left_node_(left_node)
            , left_remaining_(left_remaining)
            , remaining_(remaining)
        {
        }

        std::shared_ptr<Arena> arena_;
        std::shared_ptr<const std::vector<std::size_t>> right_nodes_;
        std::size_t left_node_ = 0;
        std::size_t left_remaining_ = 0;
        std::size_t right_index_ = 0;
        std::size_t remaining_ = 0;
    };

private:
    bilateral_ancestral_deque(
        std::shared_ptr<Arena> arena,
        const segment& left,
        const segment& right,
        const size_type count)
        : arena_(std::move(arena))
        , left_(left)
        , right_(right)
        , count_(count)
    {
    }

    /// Converts a visible count into a depth offset.
    [[nodiscard]] static difference_type depth_offset(const size_type count)
    {
        if (count > static_cast<size_type>((std::numeric_limits<difference_type>::max)())) {
            throw std::overflow_error(
                "the bilateral ancestral deque interval exceeds the ancestry-depth domain");
        }

        return static_cast<difference_type>(count);
    }

    /// Adds a visible offset to an absolute depth.
    [[nodiscard]] static difference_type add_depth(
        const difference_type depth,
        const size_type offset)
    {
        const auto widened = depth_offset(offset);
        if (depth > (std::numeric_limits<difference_type>::max)() - widened) {
            throw std::overflow_error("the bilateral ancestral deque ancestry depth overflowed");
        }

        return depth + widened;
    }

    /// Subtracts a visible offset from an absolute depth.
    [[nodiscard]] static difference_type sub_depth(
        const difference_type depth,
        const size_type offset)
    {
        const auto widened = depth_offset(offset);
        if (depth < (std::numeric_limits<difference_type>::min)() + widened) {
            throw std::overflow_error("the bilateral ancestral deque ancestry depth overflowed");
        }

        return depth - widened;
    }

    /// The visible count this deque would have after one insertion.
    [[nodiscard]] size_type next_count() const
    {
        if (count_ == (std::numeric_limits<size_type>::max)()) {
            throw std::overflow_error("the bilateral ancestral deque count cannot grow further");
        }

        return count_ + 1;
    }

    /// An empty handle anchored at the arena bottom node.
    [[nodiscard]] bilateral_ancestral_deque empty_handle() const
    {
        const auto empty_interval = segment::empty_at(arena_->bottom());
        return bilateral_ancestral_deque{arena_, empty_interval, empty_interval, 0};
    }

    void throw_if_empty() const
    {
        if (empty()) {
            throw std::logic_error("the bilateral ancestral deque is empty");
        }
    }

    /// The node holding one visible value, spending at most one level-ancestor query.
    [[nodiscard]] std::size_t node_at(const size_type index) const
    {
        if (index < left_.count) {
            if (index == 0) {
                return left_.tail;
            }
            if (index == left_.count - 1) {
                return left_.base;
            }

            return arena_->ancestor_at_depth(
                left_.tail,
                sub_depth(arena_->depth_of(left_.tail), index));
        }

        const auto right_index = index - left_.count;
        if (right_index == 0) {
            return right_.base;
        }
        if (right_index == right_.count - 1) {
            return right_.tail;
        }

        return arena_->ancestor_at_depth(
            right_.tail,
            add_depth(add_depth(arena_->depth_of(right_.anchor), right_index), 1));
    }

    /// Publishes one leaf below an interval's tail and extends the interval by one.
    [[nodiscard]] segment add_to_tail(const segment& source, T value) const
    {
        const auto tail = arena_->add_leaf(source.tail, std::move(value));
        const auto base = source.count == 0 ? tail : source.base;
        return segment{source.anchor, base, tail, source.count + 1};
    }

    /// Drops an interval's newest physical label by moving its tail to the parent. No queries.
    [[nodiscard]] segment remove_tail(const segment& source) const
    {
        const auto tail = arena_->parent_of(source.tail);
        if (source.count == 1) {
            return segment::empty_at(tail);
        }

        return segment{source.anchor, source.base, tail, source.count - 1};
    }

    /// Drops an interval's oldest physical label by advancing its anchor to the cached base.
    ///
    /// Costs at most one level-ancestor query, which selects the next cached base. A one-value
    /// interval and the two-value shortcut both answer from cached handles alone.
    [[nodiscard]] segment remove_base(const segment& source) const
    {
        if (source.count == 1) {
            return segment::empty_at(source.tail);
        }

        const auto anchor = source.base;
        const auto base = source.count == 2
            ? source.tail
            : arena_->ancestor_at_depth(
                  source.tail,
                  add_depth(sub_depth(arena_->depth_of(source.tail), source.count), 2));
        return segment{anchor, base, source.tail, source.count - 1};
    }

    /// Selects the sub-interval `[start, start + count)` of a reversed left arm.
    ///
    /// The reversed reading order means `start` counts down from the tail. At most two queries, and
    /// fewer whenever a cached endpoint already answers.
    [[nodiscard]] segment slice_left(
        const segment& source,
        const size_type start,
        const size_type count) const
    {
        if (count == 0) {
            return segment::empty_at(arena_->bottom());
        }
        if (start == 0 && count == source.count) {
            return source;
        }

        const auto tail_depth = arena_->depth_of(source.tail);
        const auto base_depth = add_depth(sub_depth(tail_depth, source.count), 1);
        const auto sliced_tail_depth = sub_depth(tail_depth, start);
        const auto sliced_base_depth = add_depth(sub_depth(sliced_tail_depth, count), 1);

        const auto tail = sliced_tail_depth == tail_depth
            ? source.tail
            : arena_->ancestor_at_depth(source.tail, sliced_tail_depth);
        std::size_t base = 0;
        if (sliced_base_depth == sliced_tail_depth) {
            base = tail;
        } else if (sliced_base_depth == base_depth) {
            base = source.base;
        } else {
            base = arena_->ancestor_at_depth(source.tail, sliced_base_depth);
        }

        const auto anchor = base == source.base ? source.anchor : arena_->parent_of(base);
        return segment{anchor, base, tail, count};
    }

    /// Selects the sub-interval `[start, start + count)` of a forward right arm.
    ///
    /// At most two queries, and fewer whenever a cached endpoint already answers.
    [[nodiscard]] segment slice_right(
        const segment& source,
        const size_type start,
        const size_type count) const
    {
        if (count == 0) {
            return segment::empty_at(arena_->bottom());
        }
        if (start == 0 && count == source.count) {
            return source;
        }

        const auto anchor_depth = arena_->depth_of(source.anchor);
        const auto sliced_base_depth = add_depth(add_depth(anchor_depth, start), 1);
        const auto sliced_tail_depth = sub_depth(add_depth(sliced_base_depth, count), 1);
        const auto end = start + count;

        const auto base = start == 0
            ? source.base
            : arena_->ancestor_at_depth(source.tail, sliced_base_depth);
        std::size_t tail = 0;
        if (sliced_tail_depth == sliced_base_depth) {
            tail = base;
        } else if (end == source.count) {
            tail = source.tail;
        } else {
            tail = arena_->ancestor_at_depth(source.tail, sliced_tail_depth);
        }

        const auto anchor = start == 0 ? source.anchor : arena_->parent_of(base);
        return segment{anchor, base, tail, count};
    }

    /// Checks one interval's endpoint identities, cached base, and count/depth agreement.
    ///
    /// An empty interval is settled from cached fields alone. A nonempty one spends exactly two
    /// level-ancestor queries to place the anchor and the cached base on the tail's ancestry path.
    void validate_segment(const segment& source) const
    {
        if (source.count == 0) {
            if (source.anchor != source.base || source.base != source.tail) {
                throw std::logic_error("an empty interval does not have one shared endpoint");
            }
            return;
        }

        const auto anchor_depth = arena_->depth_of(source.anchor);
        const auto tail_depth = arena_->depth_of(source.tail);
        const auto span = depth_offset(source.count);
        if (anchor_depth > (std::numeric_limits<difference_type>::max)() - span
            || tail_depth != anchor_depth + span) {
            throw std::logic_error("an interval count disagrees with its endpoint depths");
        }
        if (arena_->parent_of(source.base) != source.anchor) {
            throw std::logic_error("the cached base is not the first node after the anchor");
        }
        if (arena_->ancestor_at_depth(source.tail, anchor_depth) != source.anchor) {
            throw std::logic_error("the interval anchor is not an ancestor of its tail");
        }
        if (arena_->ancestor_at_depth(source.tail, anchor_depth + 1) != source.base) {
            throw std::logic_error("the cached base is not on the tail's ancestry path");
        }
    }

    std::shared_ptr<Arena> arena_;
    segment left_;
    segment right_;
    size_type count_ = 0;
};

/// The two persistent versions produced by `bilateral_ancestral_deque::split_at`.
///
/// Together they enumerate the original value. Both retain the source's arena.
template <typename T, typename Arena>
    requires incremental_ancestor_arena<Arena, T>
struct bilateral_ancestral_deque_split final {
    /// The prefix holding the values before the boundary.
    bilateral_ancestral_deque<T, Arena> left;
    /// The suffix holding the values from the boundary onward.
    bilateral_ancestral_deque<T, Arena> right;
};

/// An endpoint value and the deque that remains after removing it.
///
/// The value is a borrowed pointer rather than a copy: arena nodes are immutable and shared by every
/// version that can still see them, and `rest` retains the arena, so the pointer stays valid for at
/// least as long as this result.
template <typename T, typename Arena>
    requires incremental_ancestor_arena<Arena, T>
struct bilateral_ancestral_deque_pop final {
    /// The removed endpoint value, borrowed from the arena.
    const T* value = nullptr;
    /// The deque without that value.
    bilateral_ancestral_deque<T, Arena> rest;
};

template <typename T, typename Arena>
    requires incremental_ancestor_arena<Arena, T>
bilateral_ancestral_deque_split<T, Arena> bilateral_ancestral_deque<T, Arena>::split_at(
    const size_type index) const
{
    if (index > count_) {
        throw std::out_of_range("the boundary is outside the bilateral ancestral deque");
    }

    return split_result{take(index), drop(index)};
}

template <typename T, typename Arena>
    requires incremental_ancestor_arena<Arena, T>
std::optional<bilateral_ancestral_deque_pop<T, Arena>>
bilateral_ancestral_deque<T, Arena>::try_pop_front() const
{
    if (empty()) {
        return std::nullopt;
    }

    return pop_result{std::addressof(front()), remove_first()};
}

template <typename T, typename Arena>
    requires incremental_ancestor_arena<Arena, T>
std::optional<bilateral_ancestral_deque_pop<T, Arena>>
bilateral_ancestral_deque<T, Arena>::try_pop_back() const
{
    if (empty()) {
        return std::nullopt;
    }

    return pop_result{std::addressof(back()), remove_last()};
}

} // namespace durable7::finger_tree

