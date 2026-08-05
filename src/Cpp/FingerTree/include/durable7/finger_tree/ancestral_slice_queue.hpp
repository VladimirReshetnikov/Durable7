/// A persistent queue whose every version is one contiguous interval of an append ancestry path.
///
/// A value does not own nodes. It is the constant-sized triple `(tail, low_depth, count)` naming an
/// interval of the unique bottom-to-`tail` path inside a shared incremental level-ancestor arena,
/// where `count` is a redundant cache of `depth(tail) - low_depth + 1`. The visible values are the
/// labels of the ancestors of `tail` at absolute depths `low_depth ..= depth(tail)`. Appending adds
/// one child below `tail`; removing and slicing move only the two interval boundaries.
///
/// Because the arena is append-only and its published nodes are immutable, every retained handle
/// keeps denoting exactly the same sequence forever, and adding below an *old* handle simply starts
/// a sibling branch. That is what makes the structure fully persistent for its restricted algebra:
/// any historical version can be extended, not only the newest one. It is not confluently
/// persistent, because two unrelated versions cannot be merged.
///
/// # The anchored-empty rule
///
/// An empty value is not "nothing". It retains the node immediately *before* its window as an
/// anchor, so `low_depth == depth(tail) + 1` holds exactly when the value is empty. Appending to any
/// empty slice therefore yields exactly the new value and never resurrects a discarded prefix. A
/// queue drained from the front and a queue drained from the back are both empty yet keep different
/// anchors, so empty values reached through different histories have distinct provenance. There is
/// no canonical empty value and no process-global empty arena.
///
/// # Bounds
///
/// The bounds are parameterized by the backend. Let `M` be the number of labelled nodes the arena
/// has ever published, `U(M)` the cost of adding a leaf, and `Q(M)` the cost of one level-ancestor
/// query. Then `push_back` costs `U(M)`; `front`, `at`, `operator[]`, `take`, `get_range`, and a
/// nontrivial `split_at` cost `Q(M)`; and `size`, `empty`, `back`, `remove_first`, `remove_last`,
/// `pop_last`, and `drop` are O(1). `pop_first` costs `Q(M)` because it also returns the front
/// value. Every operation allocates O(1) new words besides the single arena node `push_back`
/// publishes.
///
/// Instantiating the backend with Alstrup--Holm incremental level ancestry would give
/// `U(M) = Q(M) = O(1)` worst case in linear space. **That backend is not implemented here**; the
/// statement is a reduction to a published result, not a property of this workspace. The shipped
/// `myers_incremental_ancestor_arena` has O(1)-amortized leaf addition and O(log M) worst-case
/// ancestor queries, so the shipped bounds are `push_back` O(1) amortized, the `Q(M)` group O(log M)
/// worst case, and the O(1) group O(1) worst case. No amortized bound may be read as a worst-case
/// bound.
///
/// Boundary-specialized calls make no ancestor query at all: `take(size())`, `drop(0)`, the whole
/// range, every suffix range `get_range(index, size() - index)`, a split at `size()`, and an indexed
/// read at `size() - 1` all reuse a retained or derived tail. A split at `0` of a *non-empty* queue
/// is deliberately not specialized: the anchored-empty rule requires the empty prefix to retain the
/// node at depth `low_depth - 1`, and only an ancestor query can name that node from the tail. On an
/// empty queue the two split boundaries coincide with the whole range, so a split at `0` is
/// query-free there.
///
/// # Deliberate limits
///
/// The algebra intentionally excludes prepending, point replacement, arbitrary middle edits, and
/// concatenation of unrelated histories; omitting them is precisely why the indexed and slicing
/// bounds are possible. Arena space is charged to *all* successful historical appends, not to the
/// visible length of one handle: dropping a prefix or releasing a handle reclaims nothing, and every
/// published value stays reachable until the arena is destroyed. Two handles may enumerate equal
/// values while having different tails, anchors, or arenas, so this type promises no O(1) sequence
/// equality, hashing, or canonical empty identity, and deliberately defines none of them.

#pragma once

#include <durable7/finger_tree/detail/common.hpp>
#include <durable7/finger_tree/incremental_ancestor.hpp>

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

/// Window, anchor, and arena measurements from a structural audit of one queue value.
///
/// The audit uses only the arena's O(1) primitive reads, so it performs no level-ancestor query and
/// cannot perturb a query-count measurement taken around it.
struct ancestral_slice_queue_statistics final {
    /// The number of visible values.
    std::size_t count = 0;
    /// The absolute ancestry depth of the first visible value; zero for a queue anchored at bottom.
    std::ptrdiff_t low_depth = 0;
    /// The absolute ancestry depth of the retained tail; `low_depth - 1` when the queue is empty.
    std::ptrdiff_t tail_depth = -1;
    /// The absolute ancestry depth of the node retained immediately before the window.
    std::ptrdiff_t anchor_depth = -1;
    /// Whether that anchor is the arena's unlabelled bottom node.
    bool anchored_at_bottom = true;
    /// The labelled nodes the shared arena has published, counting every historical append.
    std::size_t arena_published_node_count = 0;

    friend bool operator==(
        const ancestral_slice_queue_statistics&,
        const ancestral_slice_queue_statistics&) = default;
};

template <class T, class Arena = myers_incremental_ancestor_arena<T>>
    requires incremental_ancestor_arena<Arena, T>
class ancestral_slice_queue;

/// One removed endpoint value and the persistent queue that remains without it.
template <class T, class Arena = myers_incremental_ancestor_arena<T>>
    requires incremental_ancestor_arena<Arena, T>
struct ancestral_slice_queue_pop final {
    /// The removed endpoint value.
    T value;
    /// The persistent queue remaining after that endpoint is removed.
    ancestral_slice_queue<T, Arena> rest;
};

/// The two appendable ancestry intervals a split produced.
template <class T, class Arena = myers_incremental_ancestor_arena<T>>
    requires incremental_ancestor_arena<Arena, T>
struct ancestral_slice_queue_split final {
    /// The prefix holding the first `index` visible values.
    ancestral_slice_queue<T, Arena> left;
    /// The suffix holding the remaining visible values.
    ancestral_slice_queue<T, Arena> right;
};

/// An immutable, fully persistent queue slice denoting one interval of an append ancestry path.
///
/// A value retains its arena, a tail node, the absolute depth of its first visible value, and a
/// redundant count cache. Empty values retain the node immediately before their window as an anchor,
/// so appending to any empty slice yields exactly the new value without exposing a discarded prefix.
/// All handles are immutable; adding below an old handle creates a sibling branch in the shared
/// monotone arena.
///
/// The backend is a compile-time policy parameter rather than a runtime interface, matching how this
/// workspace expresses every other policy. A different backend is a different type, so the
/// parameterized bounds above are resolved at instantiation and no virtual dispatch stands between
/// the queue and its arena. The observable contract is the one the managed ports document.
///
/// Copying a value is O(1): the arena is shared through `std::shared_ptr`, never duplicated. Moving
/// deliberately copies that shared handle rather than emptying the source, so both values stay
/// usable; an arena-less queue could not represent even the empty sequence. Independently retained
/// values may be read concurrently whenever the arena is safe for concurrent use, which the shipped
/// arena is.
///
/// See the module comment above for the complexity table, the anchored-empty rule, and the
/// operations this algebra deliberately omits.
template <class T, class Arena>
    requires incremental_ancestor_arena<Arena, T>
class ancestral_slice_queue final {
public:
    /// The element type stored in each labelled arena node.
    using value_type = T;
    /// The compile-time incremental level-ancestor backend.
    using arena_type = Arena;
    /// The retained arena handle every value of one history shares.
    using arena_pointer = std::shared_ptr<Arena>;
    /// The unsigned type used for counts and indices.
    using size_type = std::size_t;
    /// The signed type used for absolute ancestry depths.
    using difference_type = std::ptrdiff_t;
    /// Stored values are never handed out for modification.
    using reference = const T&;
    /// Stored values are never handed out for modification.
    using const_reference = const T&;

    /// A stable front-to-back iterator over one immutable ancestry interval.
    ///
    /// The iterator captures the whole visible path of node handles when it is created, so it
    /// observes exactly the value it was obtained from and never sees a later append. Capture costs
    /// Theta(count) parent reads and Theta(count) transient handle slots rather than `count`
    /// ancestor queries, and copies element values not at all. The iterator retains both the arena
    /// and the captured path, so it stays traversable after the queue that produced it is destroyed.
    class const_iterator final {
    public:
        using iterator_concept = std::forward_iterator_tag;
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        /// An exhausted iterator, which compares equal to any other exhausted iterator.
        const_iterator() = default;

        /// The value at the current position. This read is O(1).
        [[nodiscard]] reference operator*() const
        {
            return arena_->value_at((*path_)[position_]);
        }

        /// A pointer to the value at the current position. This read is O(1).
        [[nodiscard]] pointer operator->() const
        {
            return std::addressof(**this);
        }

        /// Advances to the next visible value. This step is O(1).
        const_iterator& operator++()
        {
            ++position_;
            if (position_ == path_->size()) {
                *this = const_iterator{};
            }
            return *this;
        }

        /// Advances to the next visible value, returning the previous position. This step is O(1).
        const_iterator operator++(int)
        {
            auto previous = *this;
            ++*this;
            return previous;
        }

        /// Whether both iterators name the same position of the same queue version, where all
        /// exhausted iterators compare equal so an end iterator needs no source value.
        ///
        /// Equality is a property of the logical position, never of which `begin()` call minted the
        /// buffer, so two iterators taken from separate `begin()` calls on one queue value are the
        /// same iterator and the forward-iterator multipass requirement holds. A version is fixed
        /// by its arena together with its retained tail and visible count, which name one unique
        /// interval because the low boundary is `depth(tail) - count + 1`; iterators over versions
        /// denoting different intervals therefore never compare equal, even when their windows
        /// overlap or their lengths agree. This read is O(1).
        [[nodiscard]] friend bool operator==(
            const const_iterator& left,
            const const_iterator& right) noexcept
        {
            const auto left_exhausted = left.path_ == nullptr;
            const auto right_exhausted = right.path_ == nullptr;
            if (left_exhausted || right_exhausted) {
                return left_exhausted && right_exhausted;
            }

            return left.position_ == right.position_ && left.arena_ == right.arena_
                && left.path_->size() == right.path_->size()
                && left.path_->back() == right.path_->back();
        }

    private:
        friend class ancestral_slice_queue;

        using path_pointer = std::shared_ptr<const std::vector<std::size_t>>;

        const_iterator(std::shared_ptr<const Arena> arena, path_pointer path) noexcept
            : arena_(std::move(arena))
            , path_(std::move(path))
        {
        }

        std::shared_ptr<const Arena> arena_;
        path_pointer path_;
        std::size_t position_ = 0;
    };

    /// The iteration type; traversal never exposes a modifiable value.
    using iterator = const_iterator;

    /// An empty queue owned by a fresh, independently released arena of the default backend.
    ///
    /// There is deliberately no shared global empty value: each call owns its own arena, so
    /// unrelated workloads share neither retained history nor write contention.
    ///
    /// @throws std::invalid_argument when the backend does not anchor its bottom node at depth -1.
    ancestral_slice_queue()
        requires std::default_initializable<Arena>
        : ancestral_slice_queue(create(std::make_shared<Arena>()))
    {
    }

    /// A queue holding the listed values, owned by a fresh arena of the default backend.
    ///
    /// Publishes one arena node per value, for Theta(k) total leaf-add work.
    ancestral_slice_queue(std::initializer_list<T> values)
        requires std::default_initializable<Arena> && std::copy_constructible<T>
        : ancestral_slice_queue(from_range(values))
    {
    }

    /// Copies the constant-sized handle in O(1); the arena is shared, never duplicated.
    ancestral_slice_queue(const ancestral_slice_queue&) = default;

    /// Copies the constant-sized handle in O(1); the arena is shared, never duplicated.
    ancestral_slice_queue& operator=(const ancestral_slice_queue&) = default;

    /// Copies the shared arena handle rather than emptying the source, so a moved-from queue stays
    /// exactly as usable as the value it was moved from.
    ancestral_slice_queue(ancestral_slice_queue&& other) noexcept
        : arena_(other.arena_)
        , tail_(other.tail_)
        , low_depth_(other.low_depth_)
        , count_(other.count_)
    {
    }

    /// Copies the shared arena handle rather than emptying the source, so a moved-from queue stays
    /// exactly as usable as the value it was moved from.
    ancestral_slice_queue& operator=(ancestral_slice_queue&& other) noexcept
    {
        arena_ = other.arena_;
        tail_ = other.tail_;
        low_depth_ = other.low_depth_;
        count_ = other.count_;
        return *this;
    }

    ~ancestral_slice_queue() = default;

    /// An empty queue owned by an explicit incremental-ancestor arena.
    ///
    /// This is the extension seam the managed ports express as a constructor taking a backend
    /// interface. Supplying an arena with different `U(M)`/`Q(M)` costs changes every parameterized
    /// bound documented above. The arena retains and navigates every appended node.
    ///
    /// @throws std::invalid_argument when the handle is null, or when the arena does not report
    ///     depth -1 for its bottom node.
    [[nodiscard]] static ancestral_slice_queue create(arena_pointer arena)
    {
        if (arena == nullptr) {
            throw std::invalid_argument("an ancestral slice queue requires an arena");
        }

        const auto bottom = arena->bottom();
        if (arena->depth_of(bottom) != -1) {
            throw std::invalid_argument(
                "an ancestral slice queue arena must report depth -1 for its bottom node");
        }

        return ancestral_slice_queue(std::move(arena), bottom, 0, 0);
    }

    /// A queue holding the range's values in enumeration order, owned by a fresh default arena.
    ///
    /// Publishes one arena node per value, for Theta(k) total leaf-add work. The range is enumerated
    /// exactly once.
    template <std::ranges::input_range Range>
        requires std::default_initializable<Arena>
        && std::constructible_from<T, std::ranges::range_reference_t<Range>>
    [[nodiscard]] static ancestral_slice_queue from_range(Range&& values)
    {
        auto result = ancestral_slice_queue{};
        for (auto&& value : values) {
            result = result.push_back(T(std::forward<decltype(value)>(value)));
        }
        return result;
    }

    /// The number of visible values, read from the cache in O(1).
    [[nodiscard]] size_type size() const noexcept
    {
        return count_;
    }

    /// Whether this handle denotes an empty ancestry interval. This read is O(1).
    [[nodiscard]] bool empty() const noexcept
    {
        return count_ == 0;
    }

    /// The first visible value.
    ///
    /// Costs one ancestor query `Q(M)`: the front is selected jointly by the tail and the low
    /// boundary, so it is not an O(1) endpoint read under a logarithmic backend. A singleton reuses
    /// its retained tail and makes no query. The reference stays valid while the arena is retained.
    ///
    /// @throws std::logic_error when the queue is empty.
    [[nodiscard]] const_reference front() const
    {
        throw_if_empty();
        return arena_->value_at(front_node());
    }

    /// The last visible value. This read is O(1), because the handle retains the tail directly.
    ///
    /// The reference stays valid while the arena is retained.
    ///
    /// @throws std::logic_error when the queue is empty.
    [[nodiscard]] const_reference back() const
    {
        throw_if_empty();
        return arena_->value_at(tail_);
    }

    /// A pointer to the first visible value, or null when the queue is empty.
    ///
    /// A stored empty `std::optional` is a present value; absence is reported only by the null
    /// pointer. Costs the same `Q(M)` as `front`.
    [[nodiscard]] const value_type* try_front() const
    {
        return count_ == 0 ? nullptr : std::addressof(arena_->value_at(front_node()));
    }

    /// A pointer to the last visible value, or null when the queue is empty. This read is O(1).
    [[nodiscard]] const value_type* try_back() const
    {
        return count_ == 0 ? nullptr : std::addressof(arena_->value_at(tail_));
    }

    /// The visible value at a zero-based index.
    ///
    /// Costs one ancestor query `Q(M)`; the last visible index reuses the retained tail and makes no
    /// query. The reference stays valid while the arena is retained.
    ///
    /// @throws std::out_of_range when the index is outside the queue.
    [[nodiscard]] const_reference at(const size_type index) const
    {
        throw_if_index_out_of_range(index, count_);
        return arena_->value_at(node_at(index));
    }

    /// The visible value at a zero-based index, validated exactly as `at`.
    ///
    /// @throws std::out_of_range when the index is outside the queue.
    [[nodiscard]] const_reference operator[](const size_type index) const
    {
        return at(index);
    }

    /// A pointer to the visible value at a zero-based index, or null when the index is outside the
    /// queue. Costs the same `Q(M)` as `at`.
    [[nodiscard]] const value_type* try_get(const size_type index) const
    {
        return index < count_ ? std::addressof(arena_->value_at(node_at(index))) : nullptr;
    }

    /// A queue with one value appended below this handle's tail or empty anchor.
    ///
    /// Costs `U(M)` and publishes exactly one arena node. This handle and every other retained
    /// version are unaffected; appending below an old handle starts a sibling branch.
    ///
    /// @throws std::overflow_error when the visible count cannot grow further, or when the arena
    ///     rejects the addition because its depth or node count cannot grow further. A rejected
    ///     addition publishes nothing and leaves this handle valid.
    [[nodiscard]] ancestral_slice_queue push_back(T value) const
    {
        if (count_ == (std::numeric_limits<size_type>::max)()) {
            throw std::overflow_error("the ancestral slice queue count cannot grow further");
        }

        const auto tail = arena_->add_leaf(tail_, std::move(value));
        return ancestral_slice_queue(arena_, tail, low_depth_, count_ + 1);
    }

    /// A queue with one value appended; the spelling the reference contract calls `AddLast`.
    [[nodiscard]] ancestral_slice_queue add_last(T value) const
    {
        return push_back(std::move(value));
    }

    /// The queue without its first visible value. This update is O(1).
    ///
    /// Only the low boundary moves, so no ancestor query is made. Emptying a singleton this way
    /// leaves the old tail as the result's anchor.
    ///
    /// @throws std::logic_error when the queue is empty.
    [[nodiscard]] ancestral_slice_queue remove_first() const
    {
        throw_if_empty();
        return ancestral_slice_queue(arena_, tail_, offset_depth(low_depth_, 1), count_ - 1);
    }

    /// The queue without its last visible value. This update is O(1).
    ///
    /// The new tail is the old tail's parent, which the arena reads in constant time. Emptying a
    /// singleton this way anchors the result immediately before the window.
    ///
    /// @throws std::logic_error when the queue is empty.
    [[nodiscard]] ancestral_slice_queue remove_last() const
    {
        throw_if_empty();
        return ancestral_slice_queue(arena_, arena_->parent_of(tail_), low_depth_, count_ - 1);
    }

    /// The first visible value together with the queue remaining without it.
    ///
    /// Costs `Q(M)`, because it also returns the front value.
    ///
    /// @throws std::logic_error when the queue is empty.
    [[nodiscard]] ancestral_slice_queue_pop<T, Arena> pop_first() const
        requires std::copy_constructible<T>
    {
        throw_if_empty();
        return ancestral_slice_queue_pop<T, Arena>{arena_->value_at(front_node()), remove_first()};
    }

    /// The last visible value together with the queue remaining without it. This update is O(1).
    ///
    /// @throws std::logic_error when the queue is empty.
    [[nodiscard]] ancestral_slice_queue_pop<T, Arena> pop_last() const
        requires std::copy_constructible<T>
    {
        throw_if_empty();
        return ancestral_slice_queue_pop<T, Arena>{arena_->value_at(tail_), remove_last()};
    }

    /// The first visible value together with the remainder, or nothing when the queue is empty.
    ///
    /// Nothing is published on the empty path, and this handle stays valid either way.
    [[nodiscard]] std::optional<ancestral_slice_queue_pop<T, Arena>> try_pop_first() const
        requires std::copy_constructible<T>
    {
        if (count_ == 0) {
            return std::nullopt;
        }

        return pop_first();
    }

    /// The last visible value together with the remainder, or nothing when the queue is empty.
    [[nodiscard]] std::optional<ancestral_slice_queue_pop<T, Arena>> try_pop_last() const
        requires std::copy_constructible<T>
    {
        if (count_ == 0) {
            return std::nullopt;
        }

        return pop_last();
    }

    /// The first `count` visible values, as an appendable ancestry prefix.
    ///
    /// Costs `Q(M)`; `take(size())` returns this handle unchanged and makes no query. The result is
    /// a real ancestry slice and remains appendable, and `take(0)` on a non-empty queue is anchored
    /// immediately before the window.
    ///
    /// @throws std::out_of_range when `count` exceeds the visible length.
    [[nodiscard]] ancestral_slice_queue take(const size_type count) const
    {
        throw_if_count_out_of_range(count, count_);
        return count == count_ ? *this : get_range(0, count);
    }

    /// The queue after omitting its first `count` visible values. This update is O(1).
    ///
    /// Only the low boundary moves, so no ancestor query is ever made. `drop(0)` returns this handle
    /// unchanged, and `drop(size())` keeps the old tail as the empty result's anchor.
    ///
    /// @throws std::out_of_range when `count` exceeds the visible length.
    [[nodiscard]] ancestral_slice_queue drop(const size_type count) const
    {
        throw_if_count_out_of_range(count, count_);
        if (count == 0) {
            return *this;
        }

        return ancestral_slice_queue(
            arena_,
            tail_,
            offset_depth(low_depth_, count),
            count_ - count);
    }

    /// One contiguous, appendable ancestry slice.
    ///
    /// Costs `Q(M)` in general. A range whose result ends at the current tail — the whole range and
    /// every suffix, including the empty suffix `get_range(size(), 0)` — reuses that tail and makes
    /// no query. A zero-length range elsewhere selects the ancestor immediately before its start as
    /// an anchor, which is the arena's bottom node when the start is the very front of a queue grown
    /// from an empty one.
    ///
    /// The range is validated as `count > size() - index`, so an index plus count that would
    /// overflow is rejected rather than wrapped.
    ///
    /// @throws std::out_of_range when the index or the count runs past the end of the queue.
    [[nodiscard]] ancestral_slice_queue get_range(const size_type index, const size_type count) const
    {
        if (index > count_) {
            throw std::out_of_range("index is outside the collection bounds");
        }
        throw_if_count_out_of_range(count, count_ - index);
        if (index == 0 && count == count_) {
            return *this;
        }

        const auto low_depth = offset_depth(low_depth_, index);
        const auto target_depth = window_end_depth(low_depth, count);
        const auto current_tail_depth = window_end_depth(low_depth_, count_);
        const auto tail = target_depth == current_tail_depth
            ? tail_
            : arena_->ancestor_at_depth(tail_, target_depth);
        return ancestral_slice_queue(arena_, tail, low_depth, count);
    }

    /// The queue split at a zero-based boundary into an appendable prefix and suffix.
    ///
    /// Costs `Q(M)`: the prefix performs the one ancestor-seeking query and the suffix only moves
    /// the low boundary. Both results are real ancestry slices and can independently start new
    /// branches.
    ///
    /// A split at `size()` makes no query, because the prefix is this handle and the suffix only
    /// moves the low boundary past the tail. A split at `0` of a non-empty queue still costs `Q(M)`
    /// and cannot be specialized away: the anchored-empty rule requires the empty prefix to retain
    /// the node at depth `low_depth - 1`, and only an ancestor query can name that node from the
    /// retained tail. On an empty queue the two boundaries coincide, so the split is query-free.
    ///
    /// @throws std::out_of_range when the boundary exceeds the visible length.
    [[nodiscard]] ancestral_slice_queue_split<T, Arena> split_at(const size_type index) const
    {
        throw_if_split_index_out_of_range(index, count_);
        return ancestral_slice_queue_split<T, Arena>{take(index), drop(index)};
    }

    /// The visible values in front-to-back order.
    ///
    /// Follows parent links from the tail rather than performing `size()` ancestor queries, taking
    /// Theta(size()) time and Theta(size()) transient handle slots. A concurrent append cannot enter
    /// the captured path.
    [[nodiscard]] std::vector<T> to_vector() const
        requires std::copy_constructible<T>
    {
        const auto path = capture_path();
        auto values = std::vector<T>{};
        values.reserve(path.size());
        for (const auto node : path) {
            values.push_back(arena_->value_at(node));
        }
        return values;
    }

    /// A stable front-to-back iterator over this immutable ancestry interval.
    ///
    /// Creating the iterator captures the whole visible path, so it costs Theta(size()) parent reads
    /// and no ancestor query. See `const_iterator` for the retention contract.
    [[nodiscard]] const_iterator begin() const
    {
        if (count_ == 0) {
            return const_iterator{};
        }

        return const_iterator{
            arena_,
            std::make_shared<std::vector<std::size_t>>(capture_path())};
    }

    /// The exhausted iterator. All exhausted iterators compare equal. This read is O(1).
    [[nodiscard]] const_iterator end() const noexcept
    {
        return const_iterator{};
    }

    /// A stable front-to-back iterator over this immutable ancestry interval.
    [[nodiscard]] const_iterator cbegin() const
    {
        return begin();
    }

    /// The exhausted iterator. This read is O(1).
    [[nodiscard]] const_iterator cend() const noexcept
    {
        return end();
    }

    /// The shared arena this value navigates.
    ///
    /// Exposed so a caller can seed further versions on one history and read backend-specific
    /// diagnostics such as the shipped arena's `statistics()`. The handle is the retained arena
    /// itself, not a copy: arenas are neither copyable nor movable.
    [[nodiscard]] const arena_pointer& arena() const noexcept
    {
        return arena_;
    }

    /// The shared arena's address, for tests that a value navigates one specific history.
    [[nodiscard]] const void* arena_identity() const noexcept
    {
        return arena_.get();
    }

    /// The retained tail or empty anchor handle. Two values with the same arena and the same tail
    /// end their windows at the same node.
    [[nodiscard]] size_type tail_handle() const noexcept
    {
        return tail_;
    }

    /// The absolute ancestry depth of the first visible value, which for an empty value is one past
    /// the depth of its anchor.
    [[nodiscard]] difference_type low_depth() const noexcept
    {
        return low_depth_;
    }

    /// Whether both values navigate the very same arena. This read is O(1).
    [[nodiscard]] bool shares_arena_with(const ancestral_slice_queue& other) const noexcept
    {
        return arena_ == other.arena_;
    }

    /// Whether both values end at the very same arena node, which is how an operation that returned
    /// the receiver or derived its tail without publishing anything is distinguished from one that
    /// named a different node. Two empty values sharing a tail also share an anchor.
    [[nodiscard]] bool shares_tail_with(const ancestral_slice_queue& other) const noexcept
    {
        return arena_ == other.arena_ && tail_ == other.tail_;
    }

    /// Audits the window against the arena and reports what it measured.
    ///
    /// Checks that the low boundary is a real depth, that the cached count agrees with the tail and
    /// low depths, that every visible node carries a value, that each parent hop from the tail drops
    /// the depth by exactly one, and that the anchor sits at `low_depth - 1`. Runs in Theta(size())
    /// time using only the arena's O(1) primitive reads, so it makes no level-ancestor query.
    ///
    /// @throws std::logic_error when any of those invariants fails.
    [[nodiscard]] ancestral_slice_queue_statistics validate_structure() const
    {
        if (low_depth_ < 0) {
            throw std::logic_error("the ancestral slice queue low boundary is not a real depth");
        }

        const auto tail_depth = arena_->depth_of(tail_);
        if (tail_depth != window_end_depth(low_depth_, count_)) {
            throw std::logic_error(
                "the ancestral slice queue count disagrees with its tail and low depths");
        }
        if ((tail_ == arena_->bottom()) != (tail_depth == -1)) {
            throw std::logic_error("only the ancestral slice queue arena bottom may sit at depth -1");
        }

        auto node = tail_;
        auto depth = tail_depth;
        for (auto remaining = count_; remaining != 0; --remaining) {
            (void)arena_->value_at(node);
            const auto parent = arena_->parent_of(node);
            if (arena_->depth_of(parent) != depth - 1) {
                throw std::logic_error("an ancestral slice queue parent hop skipped a depth");
            }
            node = parent;
            --depth;
        }

        if (depth != low_depth_ - 1) {
            throw std::logic_error("the ancestral slice queue anchor is not below its window");
        }

        const auto anchored_at_bottom = node == arena_->bottom();
        if (anchored_at_bottom != (depth == -1)) {
            throw std::logic_error("the ancestral slice queue anchor depth contradicts its identity");
        }

        return ancestral_slice_queue_statistics{
            count_,
            low_depth_,
            tail_depth,
            depth,
            anchored_at_bottom,
            arena_->published_node_count()};
    }

private:
    ancestral_slice_queue(
        arena_pointer arena,
        const std::size_t tail,
        const std::ptrdiff_t low_depth,
        const std::size_t count) noexcept
        : arena_(std::move(arena))
        , tail_(tail)
        , low_depth_(low_depth)
        , count_(count)
    {
    }

    /// `base + offset` as an absolute ancestry depth.
    ///
    /// @throws std::overflow_error when the depth cannot grow further.
    [[nodiscard]] static std::ptrdiff_t offset_depth(
        const std::ptrdiff_t base,
        const std::size_t offset)
    {
        constexpr auto maximum = (std::numeric_limits<std::ptrdiff_t>::max)();
        if (offset > static_cast<std::size_t>(maximum)) {
            throw std::overflow_error("the ancestral slice queue ancestry depth cannot grow further");
        }

        const auto widened = static_cast<std::ptrdiff_t>(offset);
        if (base > maximum - widened) {
            throw std::overflow_error("the ancestral slice queue ancestry depth cannot grow further");
        }

        return base + widened;
    }

    /// The absolute depth of the last value of a `count`-long window starting at `low_depth`.
    ///
    /// An empty window yields `low_depth - 1`, which is exactly the anchor depth the anchored-empty
    /// rule requires.
    ///
    /// @throws std::overflow_error when the depth cannot grow further.
    [[nodiscard]] static std::ptrdiff_t window_end_depth(
        const std::ptrdiff_t low_depth,
        const std::size_t count)
    {
        return offset_depth(low_depth, count) - 1;
    }

    [[nodiscard]] std::size_t front_node() const
    {
        return count_ == 1 ? tail_ : arena_->ancestor_at_depth(tail_, low_depth_);
    }

    [[nodiscard]] std::size_t node_at(const std::size_t index) const
    {
        return index == count_ - 1
            ? tail_
            : arena_->ancestor_at_depth(tail_, offset_depth(low_depth_, index));
    }

    [[nodiscard]] std::vector<std::size_t> capture_path() const
    {
        auto path = std::vector<std::size_t>(count_);
        auto node = tail_;
        for (auto index = count_; index != 0; --index) {
            path[index - 1] = node;
            if (index != 1) {
                node = arena_->parent_of(node);
            }
        }
        return path;
    }

    void throw_if_empty() const
    {
        if (count_ == 0) {
            throw std::logic_error("ancestral_slice_queue is empty");
        }
    }

    arena_pointer arena_;
    std::size_t tail_ = 0;
    std::ptrdiff_t low_depth_ = 0;
    std::size_t count_ = 0;
};

} // namespace durable7::finger_tree

