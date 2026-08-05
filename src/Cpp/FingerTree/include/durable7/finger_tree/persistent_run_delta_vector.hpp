/// A fixed-length persistent vector with a checkpoint and an exact maximal-run change index.
///
/// The vector keeps two logical views of one fixed-length sequence -- the current values and a
/// checkpoint -- plus a persistent ordered map holding one `start -> end_exclusive` record for every
/// *maximal* run of differing positions. Dirtiness is relative to a compile-time value-equality
/// policy rather than to write history: writing a policy-equal value is a semantic no-op, and
/// returning a position to its checkpoint equality class cancels that position's delta and restores
/// the exact checkpoint representative.
///
/// Let `n` be the fixed length, `k` the number of dirty positions, and `r` the number of maximal
/// dirty runs, so `0 <= r <= k <= n`. Against an unindexed pair of current/checkpoint roots the one
/// strict gain is that exact dirty-run descriptor discovery is output-optimal `Theta(r)` rather than
/// worst-case `Theta(n)`; the gap is unbounded, since one contiguous changed block gives `k = n` and
/// `r = 1`. Emitting the changed payload *values* is still `Omega(k)` and is not claimed to be
/// faster. The run index is an application of the Discrete Interval Encoding Tree idea over the
/// package's persistent sorted map, not a new interval algorithm, and the splice bound below is
/// inherited from the RRB vector rather than introduced here.
///
/// | Operation | Bound |
/// | --- | --- |
/// | Build `n` values | `Theta(n)` |
/// | Read a current or checkpoint value | `O(log n)` |
/// | Persistent point edit | `O(log n)` amortized time and fresh nodes |
/// | Fork by copying a handle | `O(1)` |
/// | Whole checkpoint or rollback | `O(1)` |
/// | Enumerate current values | `Theta(n)` |
/// | Dirty membership | `O(log(r + 2))` amortized |
/// | Exact dirty-run descriptors | `Theta(r)` |
/// | Select a dirty run by rank | `O(log(r + 2))` amortized |
/// | Accept or revert one indexed run | `O(log n)` amortized, independent of that run's length |
/// | Live delta metadata | `Theta(r)` |
/// | Total live structure | `O(n)` |
///
/// The rows marked *amortized* are amortized because they consult the start-keyed run index, whose
/// measured finger-tree substrate may force a pending lazy spine on a read. No amortized bound
/// above may be read as a worst-case bound.
///
/// Accepting or reverting a run splits and concatenates the RRB roots structurally and never walks
/// the run's positions, which is what makes it independent of the run's length and free of any
/// value-policy call. Length is fixed at construction: there is deliberately no insertion, deletion,
/// concatenation, split, range assignment, rebase, or merge of unrelated branches, because
/// checkpoint correspondence would need a position-transformation policy this abstract data type
/// does not define. There is one checkpoint per version.
///
/// Every version is immutable and every producing operation leaves its receiver usable, so any
/// retained version can be branched again. All policy calls in an edit happen before any successor
/// is constructed, so a throwing policy publishes no partial version. Concurrent reads and
/// independent branch derivation are safe when the policy is safe for concurrent calls; the policy
/// must stay one stable equivalence relation, and values retained by a version must not mutate state
/// the policy observes, or the maintained index goes stale.

#pragma once

#include <durable7/finger_tree/detail/common.hpp>
#include <durable7/finger_tree/rrb_vector.hpp>
#include <durable7/finger_tree/sorted_map.hpp>

#include <cmath>
#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace durable7::finger_tree {

/// A value-equality policy carrying no state, so two instances are interchangeable and the policy is
/// a compile-time property of the type rather than a retained callback.
///
/// The relation `equals` denotes must be a true equivalence relation -- reflexive, symmetric, and
/// transitive -- and must stay stable for every retained version's lifetime. A non-reflexive
/// relation silently corrupts run accounting: a position rewritten with its own value would be
/// recorded as a change and leave a phantom run that no invariant check can detect, because the
/// index would agree with itself. Values retained by a version must not mutate state the policy
/// observes; replacing such a value through `set_item` is the supported way to change
/// equality-observable state.
template <class Policy, class T>
concept static_equality_policy = requires(const T& left, const T& right) {
    { Policy::equals(left, right) } -> std::convertible_to<bool>;
};

/// Value equality over the platform's own `operator==`.
///
/// Deliberately refuses raw floating-point payloads. IEEE equality is not reflexive on NaN, so
/// `natural_value_equality<double>` would not be an equivalence relation and rewriting NaN over NaN
/// would be recorded as a change. Floating payloads use `reflexive_ieee_equality` instead, which is
/// what `default_value_equality` already selects for them.
template <class T>
struct natural_value_equality {
    static_assert(
        !std::is_floating_point_v<T>,
        "natural_value_equality is not an equivalence relation on a raw floating-point payload: "
        "IEEE equality is irreflexive on NaN, so rewriting NaN over NaN would be recorded as a "
        "change and leave a phantom run. Use reflexive_ieee_equality<T> instead.");

    /// Whether two values are equal. O(1) for scalars; otherwise the type's own cost.
    [[nodiscard]] static constexpr bool equals(const T& left, const T& right)
        requires equality_comparable_value<T>
    {
        return left == right;
    }
};

/// Value equality that repairs IEEE reflexivity: every NaN is equal to every NaN, and the two signed
/// zeros stay one class.
///
/// This reproduces the .NET default comparer's floating-point behaviour, which the C# original
/// depends on, and is the canonical policy for raw `float`, `double`, and `long double` payloads.
/// For any other type it is exactly `operator==`; it does *not* reach inside an aggregate, so a
/// struct or `std::optional` holding a floating field still needs a hand-written policy.
template <class T>
struct reflexive_ieee_equality {
    /// Whether two values are equal, treating NaN as equal to itself. O(1) for scalars.
    [[nodiscard]] static bool equals(const T& left, const T& right)
        requires equality_comparable_value<T>
    {
        if constexpr (std::is_floating_point_v<T>) {
            return left == right || (std::isnan(left) && std::isnan(right));
        } else {
            return left == right;
        }
    }
};

namespace detail {

/// Selects the default value-equality policy: reflexive IEEE equality for raw floating payloads,
/// plain `operator==` for everything else.
template <class T>
struct default_value_equality_selector {
    using type = natural_value_equality<T>;
};

template <class T>
    requires std::is_floating_point_v<T>
struct default_value_equality_selector<T> {
    using type = reflexive_ieee_equality<T>;
};

/// One element slot, identified by pointer rather than by value.
///
/// The cleanliness invariant is "a clean position reuses its exact checkpoint representative", which
/// needs reference identity: the retained policy may be coarser or finer than natural equality, so
/// it cannot decide representative identity. Holding a `std::shared_ptr` and comparing the stored
/// pointers supplies that identity, and turns `rrb_vector::set_item`'s equal-replacement short
/// circuit into an identity test, which is exactly the test wanted there.
template <class T>
struct run_delta_cell final {
    std::shared_ptr<const T> value;

    /// Whether two slots are the same slot. Pointer identity, never value equality. O(1).
    [[nodiscard]] friend bool operator==(const run_delta_cell& left, const run_delta_cell& right) noexcept
    {
        return left.value == right.value;
    }
};

} // namespace detail

/// The default value-equality policy for a payload type.
template <class T>
using default_value_equality = typename detail::default_value_equality_selector<T>::type;

/// One maximal half-open run of checkpoint-relative changes.
///
/// A run covers the positions `start .. start + length` and is never empty. The runs one version
/// publishes are ordered, non-overlapping, non-adjacent, and maximal.
struct persistent_dirty_run final {
    /// The zero-based first changed position.
    std::size_t start = 0;
    /// The number of consecutive changed positions.
    std::size_t length = 0;

    /// The exclusive end position. O(1); raises `std::overflow_error` on `size_t` overflow.
    [[nodiscard]] constexpr std::size_t end_exclusive() const
    {
        return checked_add(start, length);
    }

    /// Whether the position lies inside this run. O(1).
    [[nodiscard]] constexpr bool contains(const std::size_t index) const noexcept
    {
        return index >= start && index - start < length;
    }

    friend constexpr bool operator==(const persistent_dirty_run&, const persistent_dirty_run&) = default;
    friend constexpr auto operator<=>(const persistent_dirty_run&, const persistent_dirty_run&) = default;
};

/// Measurements from a run-delta-vector structural audit.
struct persistent_run_delta_vector_statistics final {
    /// The fixed number of positions.
    std::size_t count = 0;
    /// The number of positions differing from the checkpoint, recounted from the run index.
    std::size_t dirty_count = 0;
    /// The number of maximal dirty runs.
    std::size_t dirty_run_count = 0;
    /// The number of clean positions confirmed to reuse their exact checkpoint representative.
    std::size_t shared_representative_count = 0;

    friend bool operator==(
        const persistent_run_delta_vector_statistics&,
        const persistent_run_delta_vector_statistics&) = default;
};

/// A fixed-length persistent vector with a checkpoint and an exact maximal-run change index.
///
/// See the file header for the representation, the shipped bounds, and the deliberate limits of this
/// abstract data type. `EqualityPolicy` is a static, stateless policy type rather than a retained
/// comparer object, so policy compatibility is a compile-time property: only two vectors of the same
/// closed type can be compared or derived from one another.
template <class T, class EqualityPolicy = default_value_equality<T>>
    requires std::copy_constructible<T> && static_equality_policy<EqualityPolicy, T>
class persistent_run_delta_vector final {
private:
    using cell_type = detail::run_delta_cell<T>;
    using vector_type = rrb_vector<cell_type>;
    using run_index_type = sorted_map<std::size_t, std::size_t>;

public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = const T&;
    using const_reference = const T&;
    using equality_policy_type = EqualityPolicy;

    /// A borrowed forward cursor over one version's current values.
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
            return *inner_->value;
        }

        [[nodiscard]] pointer operator->() const
        {
            return inner_->value.get();
        }

        const_iterator& operator++()
        {
            ++inner_;
            return *this;
        }

        const_iterator operator++(int)
        {
            auto copy = *this;
            ++inner_;
            return copy;
        }

        friend bool operator==(const const_iterator& left, const const_iterator& right) noexcept
        {
            return left.inner_ == right.inner_;
        }

    private:
        friend class persistent_run_delta_vector;

        using inner_iterator = typename vector_type::const_iterator;

        explicit const_iterator(inner_iterator inner)
            : inner_(std::move(inner))
        {
        }

        inner_iterator inner_{};
    };

    /// The clean empty vector.
    persistent_run_delta_vector() = default;

    /// A clean fixed-length vector holding the listed values in positional order. `Theta(n)`.
    persistent_run_delta_vector(std::initializer_list<T> items)
        : persistent_run_delta_vector(items.begin(), items.end())
    {
    }

    /// A clean fixed-length vector built from one pass over an iterator pair. `Theta(n)`.
    template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
        requires std::constructible_from<T, std::iter_reference_t<Iterator>>
    persistent_run_delta_vector(Iterator first, Sentinel last)
    {
        auto cells = std::vector<cell_type>{};
        for (; first != last; ++first) {
            cells.push_back(make_cell(T(*first)));
        }

        auto root = vector_type::from_range(cells);
        current_ = root;
        checkpoint_ = std::move(root);
    }

    /// The clean empty vector. O(1).
    [[nodiscard]] static persistent_run_delta_vector empty_vector()
    {
        return {};
    }

    /// A clean fixed-length vector built from one pass over a range. `Theta(n)`.
    ///
    /// Both roots start as one root, so the vector begins with no dirty runs.
    template <std::ranges::input_range Range>
        requires std::constructible_from<T, std::ranges::range_reference_t<Range>>
    [[nodiscard]] static persistent_run_delta_vector from_range(Range&& items)
    {
        return persistent_run_delta_vector{std::ranges::begin(items), std::ranges::end(items)};
    }

    /// Whether the vector holds no positions. O(1).
    [[nodiscard]] bool empty() const noexcept
    {
        return current_.empty();
    }

    /// The fixed number of positions. O(1).
    [[nodiscard]] size_type size() const noexcept
    {
        return current_.size();
    }

    /// The number of positions that differ from the checkpoint. O(1).
    [[nodiscard]] size_type dirty_count() const noexcept
    {
        return dirty_count_;
    }

    /// The number of maximal dirty runs. O(1) amortized.
    ///
    /// Amortized rather than worst case, unlike the sibling `dirty_count()`: this reads the run
    /// index's cached measure, which may force a pending lazy spine. The managed and Rust ports
    /// state it unqualified because their run indexes are not lazily measured; the claim is
    /// narrowed here to what this substrate actually delivers.
    [[nodiscard]] size_type dirty_run_count() const
    {
        return dirty_runs_.size();
    }

    /// Whether at least one current value differs from its checkpoint value. O(1).
    [[nodiscard]] bool has_changes() const noexcept
    {
        return dirty_count_ != 0;
    }

    /// The current value at a zero-based position. `O(log n)`.
    ///
    /// The reference stays valid while this version, or any version sharing the located slot,
    /// remains alive. Raises `std::out_of_range` when the position is outside the vector.
    [[nodiscard]] const_reference at(const size_type index) const
    {
        throw_if_index_out_of_range(index, size());
        return *current_.at(index).value;
    }

    /// The current value at a zero-based position. `O(log n)`.
    [[nodiscard]] const_reference operator[](const size_type index) const
    {
        return at(index);
    }

    /// The current value at a zero-based position, or `nullptr` when the position is outside the
    /// vector. `O(log n)`.
    [[nodiscard]] const T* try_get(const size_type index) const
    {
        const auto* const cell = current_.try_get(index);
        return cell != nullptr ? cell->value.get() : nullptr;
    }

    /// The checkpoint value at a zero-based position. `O(log n)`.
    ///
    /// Raises `std::out_of_range` when the position is outside the vector.
    [[nodiscard]] const_reference checkpoint_at(const size_type index) const
    {
        throw_if_index_out_of_range(index, size());
        return *checkpoint_.at(index).value;
    }

    /// The checkpoint value at a zero-based position, or `nullptr` when the position is outside the
    /// vector. `O(log n)`.
    [[nodiscard]] const T* try_get_checkpoint(const size_type index) const
    {
        const auto* const cell = checkpoint_.try_get(index);
        return cell != nullptr ? cell->value.get() : nullptr;
    }

    /// Whether a position differs from its checkpoint value. `O(log(r + 2))` amortized.
    ///
    /// Raises `std::out_of_range` when the position is outside the vector.
    [[nodiscard]] bool is_dirty(const size_type index) const
    {
        throw_if_index_out_of_range(index, size());
        return find_dirty_run(index).has_value();
    }

    /// The maximal dirty run containing a position, or nothing when that position is clean.
    /// `O(log(r + 2))` amortized.
    ///
    /// Raises `std::out_of_range` when the position is outside the vector, so an absent result means
    /// "clean" and never "out of range".
    [[nodiscard]] std::optional<persistent_dirty_run> dirty_run_containing(const size_type index) const
    {
        throw_if_index_out_of_range(index, size());
        return find_dirty_run(index);
    }

    /// The maximal dirty run at a zero-based ascending-start rank. `O(log(r + 2))` amortized.
    ///
    /// Raises `std::out_of_range` when the rank is outside the run index.
    [[nodiscard]] persistent_dirty_run dirty_run_at(const size_type rank) const
    {
        return to_run(dirty_runs_.entry_at(rank));
    }

    /// The maximal dirty run at a zero-based ascending-start rank, or nothing when the rank is
    /// outside the run index. `O(log(r + 2))` amortized.
    [[nodiscard]] std::optional<persistent_dirty_run> try_dirty_run_at(const size_type rank) const
    {
        if (rank >= dirty_runs_.size()) {
            return std::nullopt;
        }

        return dirty_run_at(rank);
    }

    /// The maximal dirty runs in ascending position order.
    ///
    /// Output-optimal `Theta(r)`: the cost tracks the number of runs, not the number of positions
    /// they cover, and no value-policy call is made.
    [[nodiscard]] std::vector<persistent_dirty_run> dirty_runs() const
    {
        const auto entries = dirty_runs_.to_vector();
        auto result = std::vector<persistent_dirty_run>{};
        result.reserve(entries.size());
        for (const auto& entry : entries) {
            result.push_back(to_run(entry));
        }

        return result;
    }

    /// A version with one current value replaced. `O(log n)` amortized.
    ///
    /// A policy-equal replacement is a semantic no-op that returns the receiver's contents sharing
    /// every root and retaining the existing current representative. A replacement landing back in
    /// the checkpoint's equality class cancels that position's delta and restores the *exact*
    /// checkpoint representative; when the last dirty position clears, the current root snaps back
    /// onto the checkpoint root. Otherwise the position becomes or stays dirty and at most two
    /// neighbouring run records change. Every policy call happens before any successor is built, so
    /// a throwing policy publishes no partial version.
    ///
    /// Raises `std::out_of_range` when the position is outside the vector.
    [[nodiscard]] persistent_run_delta_vector set_item(const size_type index, T value) const
    {
        throw_if_index_out_of_range(index, size());
        const auto& current_cell = current_.at(index);
        if (EqualityPolicy::equals(*current_cell.value, value)) {
            return *this;
        }

        const auto& checkpoint_cell = checkpoint_.at(index);
        const auto remains_dirty = !EqualityPolicy::equals(*checkpoint_cell.value, value);
        auto replacement = remains_dirty ? make_cell(std::move(value)) : checkpoint_cell;
        return replace_cell(index, std::move(replacement), remains_dirty);
    }

    /// A version whose current value at one position is reset to its checkpoint value.
    /// `O(log n)` amortized.
    ///
    /// An already clean position returns the receiver's contents sharing every root.
    /// Raises `std::out_of_range` when the position is outside the vector.
    [[nodiscard]] persistent_run_delta_vector reset_item(const size_type index) const
    {
        throw_if_index_out_of_range(index, size());
        const auto& current_cell = current_.at(index);
        const auto& checkpoint_cell = checkpoint_.at(index);
        if (EqualityPolicy::equals(*current_cell.value, *checkpoint_cell.value)) {
            return *this;
        }

        return replace_cell(index, checkpoint_cell, false);
    }

    /// A version accepting every current value as a new checkpoint. O(1) root swap.
    ///
    /// An already clean version returns its own contents sharing every root.
    [[nodiscard]] persistent_run_delta_vector checkpoint() const
    {
        if (!has_changes()) {
            return *this;
        }

        return clean(current_);
    }

    /// A version reverting every current value to the checkpoint. O(1) root swap.
    ///
    /// An already clean version returns its own contents sharing every root.
    [[nodiscard]] persistent_run_delta_vector rollback() const
    {
        if (!has_changes()) {
            return *this;
        }

        return clean(checkpoint_);
    }

    /// A version accepting the maximal dirty run at a rank into the checkpoint. `O(log n)`
    /// amortized.
    ///
    /// Only the selected run becomes clean; every other run is untouched. The splice is structural
    /// RRB splitting and concatenation, so the cost is independent of that run's length and no
    /// value-policy call is made at all.
    ///
    /// Raises `std::out_of_range` when the rank is outside the run index.
    [[nodiscard]] persistent_run_delta_vector accept_dirty_run_at(const size_type rank) const
    {
        return accept_run(dirty_run_at(rank));
    }

    /// A version reverting the maximal dirty run at a rank from the checkpoint. `O(log n)`
    /// amortized.
    ///
    /// Only the selected run becomes clean; every other run is untouched. The splice is structural
    /// RRB splitting and concatenation, so the cost is independent of that run's length and no
    /// value-policy call is made at all.
    ///
    /// Raises `std::out_of_range` when the rank is outside the run index.
    [[nodiscard]] persistent_run_delta_vector revert_dirty_run_at(const size_type rank) const
    {
        return revert_run(dirty_run_at(rank));
    }

    /// A version accepting the maximal dirty run *containing a position* into the checkpoint.
    /// `O(log n)` amortized, independent of that run's length.
    ///
    /// The argument is a zero-based position, not a run rank, so a descriptor obtained from
    /// `dirty_run_containing` can be acted on without rediscovering its rank. A clean position is
    /// vacuous rather than an error: the receiver's contents come back sharing every root, and no
    /// value-policy call is made. Locating the containing run is `O(log(r + 2))` amortized, never a
    /// scan.
    ///
    /// Raises `std::out_of_range` when the position is outside the vector.
    [[nodiscard]] persistent_run_delta_vector accept_dirty_run_containing(const size_type index) const
    {
        throw_if_index_out_of_range(index, size());
        const auto run = find_dirty_run(index);
        return run.has_value() ? accept_run(*run) : *this;
    }

    /// A version reverting the maximal dirty run *containing a position* from the checkpoint.
    /// `O(log n)` amortized, independent of that run's length.
    ///
    /// The argument is a zero-based position, not a run rank. A clean position is vacuous rather
    /// than an error: the receiver's contents come back sharing every root, and no value-policy call
    /// is made. Locating the containing run is `O(log(r + 2))` amortized, never a scan.
    ///
    /// Raises `std::out_of_range` when the position is outside the vector.
    [[nodiscard]] persistent_run_delta_vector revert_dirty_run_containing(const size_type index) const
    {
        throw_if_index_out_of_range(index, size());
        const auto run = find_dirty_run(index);
        return run.has_value() ? revert_run(*run) : *this;
    }

    [[nodiscard]] const_iterator begin() const { return const_iterator{current_.begin()}; }
    [[nodiscard]] const_iterator end() const { return const_iterator{current_.end()}; }
    [[nodiscard]] const_iterator cbegin() const { return begin(); }
    [[nodiscard]] const_iterator cend() const { return end(); }

    /// The current values in positional order. `Theta(n)`.
    [[nodiscard]] std::vector<T> to_vector() const
    {
        auto result = std::vector<T>{};
        result.reserve(size());
        for (const auto& value : *this) {
            result.push_back(value);
        }

        return result;
    }

    /// The checkpoint values in positional order. `Theta(n)`.
    [[nodiscard]] std::vector<T> checkpoint_to_vector() const
    {
        auto result = std::vector<T>{};
        result.reserve(size());
        for (const auto& cell : checkpoint_) {
            result.push_back(*cell.value);
        }

        return result;
    }

    /// Whether two versions are backed by the same current root. A representation test, not an
    /// equality test. O(1).
    [[nodiscard]] bool shares_current_root_with(const persistent_run_delta_vector& other) const noexcept
    {
        return current_.shares_root_with(other.current_);
    }

    /// Whether two versions are backed by the same checkpoint root. A representation test, not an
    /// equality test. O(1).
    [[nodiscard]] bool shares_checkpoint_root_with(const persistent_run_delta_vector& other) const noexcept
    {
        return checkpoint_.shares_root_with(other.checkpoint_);
    }

    /// The identity of the current representative at a position. `O(log n)`.
    ///
    /// A diagnostic, not a value: two positions hold the same representative exactly when this
    /// returns the same pointer. Raises `std::out_of_range` when the position is outside the vector.
    [[nodiscard]] const void* current_representative_at(const size_type index) const
    {
        throw_if_index_out_of_range(index, size());
        return current_.at(index).value.get();
    }

    /// The identity of the checkpoint representative at a position. `O(log n)`.
    ///
    /// Raises `std::out_of_range` when the position is outside the vector.
    [[nodiscard]] const void* checkpoint_representative_at(const size_type index) const
    {
        throw_if_index_out_of_range(index, size());
        return checkpoint_.at(index).value.get();
    }

    /// Whether a position's current and checkpoint slots are the *same* representative rather than
    /// merely equal values. `O(log n)`.
    ///
    /// This is the observable form of the cleanliness invariant: it holds exactly at clean
    /// positions. Raises `std::out_of_range` when the position is outside the vector.
    [[nodiscard]] bool shares_representative_at(const size_type index) const
    {
        return current_representative_at(index) == checkpoint_representative_at(index);
    }

    /// Audits both RRB roots, run ordering, non-adjacency, maximality, the dirty-count sum,
    /// policy-relative truth at every dirty position, and exact representative sharing at every
    /// clean position.
    ///
    /// `O(n log n)`: an auditor, not a hot-path operation. Raises `std::logic_error` naming the
    /// first invariant that failed.
    [[nodiscard]] persistent_run_delta_vector_statistics validate_structure() const
    {
        if (current_.size() != checkpoint_.size()) {
            throw std::logic_error("the current and checkpoint roots have different lengths");
        }
        if (dirty_count_ > size()) {
            throw std::logic_error("the dirty cardinality exceeds the position count");
        }

        (void)current_.structure_statistics();
        (void)checkpoint_.structure_statistics();

        if (dirty_runs_.empty() && (dirty_count_ != 0 || !current_.shares_root_with(checkpoint_))) {
            throw std::logic_error("a clean version is not canonicalized to its checkpoint root");
        }

        auto previous_end = std::optional<size_type>{};
        auto counted_dirty = size_type{0};
        for (const auto& entry : dirty_runs_.to_vector()) {
            const auto start = entry.first;
            const auto end = entry.second;
            if (start >= end || end > size()
                || (previous_end.has_value() && start <= *previous_end)) {
                throw std::logic_error(
                    "dirty runs are empty, out of range, overlapping, adjacent, or unordered");
            }

            counted_dirty = checked_add(counted_dirty, end - start);
            previous_end = end;
        }
        if (counted_dirty != dirty_count_) {
            throw std::logic_error("dirty run lengths do not sum to the dirty cardinality");
        }

        auto shared_representative_count = size_type{0};
        for (auto index = size_type{0}; index != size(); ++index) {
            const auto dirty = find_dirty_run(index).has_value();
            const auto& current_cell = current_.at(index);
            const auto& checkpoint_cell = checkpoint_.at(index);
            if (!dirty) {
                if (!(current_cell == checkpoint_cell)) {
                    throw std::logic_error(
                        "a clean position does not reuse its exact checkpoint representative");
                }

                ++shared_representative_count;
                continue;
            }

            if (EqualityPolicy::equals(*current_cell.value, *checkpoint_cell.value)) {
                throw std::logic_error("a dirty position is policy-equal to its checkpoint value");
            }
        }

        return persistent_run_delta_vector_statistics{
            size(),
            dirty_count_,
            dirty_runs_.size(),
            shared_representative_count};
    }

private:
    persistent_run_delta_vector(
        vector_type current,
        vector_type checkpoint,
        run_index_type dirty_runs,
        const size_type dirty_count)
        : current_(std::move(current))
        , checkpoint_(std::move(checkpoint))
        , dirty_runs_(std::move(dirty_runs))
        , dirty_count_(dirty_count)
    {
    }

    [[nodiscard]] static cell_type make_cell(T value)
    {
        return cell_type{std::make_shared<const T>(std::move(value))};
    }

    [[nodiscard]] static persistent_run_delta_vector clean(vector_type root)
    {
        auto result = persistent_run_delta_vector{};
        result.current_ = root;
        result.checkpoint_ = std::move(root);
        return result;
    }

    [[nodiscard]] static persistent_dirty_run to_run(const std::pair<size_type, size_type>& entry)
    {
        return persistent_dirty_run{entry.first, entry.second - entry.first};
    }

    [[nodiscard]] std::optional<persistent_dirty_run> find_dirty_run(const size_type index) const
    {
        const auto entry = dirty_runs_.try_floor_entry(index);
        if (!entry.has_value() || entry->second <= index) {
            return std::nullopt;
        }

        return to_run(*entry);
    }

    [[nodiscard]] persistent_run_delta_vector replace_cell(
        const size_type index,
        cell_type replacement,
        const bool remains_dirty) const
    {
        const auto was_dirty = find_dirty_run(index).has_value();
        auto next_current = current_.set_item(index, std::move(replacement));
        auto next_runs = dirty_runs_;
        auto next_dirty_count = dirty_count_;

        if (!was_dirty && remains_dirty) {
            next_runs = add_dirty_position(next_runs, index);
            next_dirty_count = checked_add(next_dirty_count, size_type{1});
        } else if (was_dirty && !remains_dirty) {
            next_runs = remove_dirty_position(next_runs, index);
            --next_dirty_count;
        }

        if (next_runs.empty()) {
            next_current = checkpoint_;
        }

        return persistent_run_delta_vector(
            std::move(next_current),
            checkpoint_,
            std::move(next_runs),
            next_dirty_count);
    }

    /// Splices one indexed run's current values into the checkpoint. `O(log n)` amortized and
    /// comparison-free; the run must be a record of this version's index.
    [[nodiscard]] persistent_run_delta_vector accept_run(const persistent_dirty_run& run) const
    {
        if (dirty_runs_.size() == 1) {
            return checkpoint();
        }

        return persistent_run_delta_vector(
            current_,
            replace_range(checkpoint_, current_, run),
            dirty_runs_.remove(run.start),
            dirty_count_ - run.length);
    }

    /// Splices one indexed run's checkpoint values back over the current root. `O(log n)` amortized
    /// and comparison-free; the run must be a record of this version's index.
    [[nodiscard]] persistent_run_delta_vector revert_run(const persistent_dirty_run& run) const
    {
        if (dirty_runs_.size() == 1) {
            return rollback();
        }

        return persistent_run_delta_vector(
            replace_range(current_, checkpoint_, run),
            checkpoint_,
            dirty_runs_.remove(run.start),
            dirty_count_ - run.length);
    }

    /// Records a position as dirty, merging the runs on both sides, extending one side, or inserting
    /// a fresh singleton run. The result stays ordered, non-overlapping, non-adjacent, and maximal,
    /// and at most two records change. `O(log(r + 2))` amortized.
    [[nodiscard]] static run_index_type add_dirty_position(
        const run_index_type& runs,
        const size_type index)
    {
        const auto next = checked_add(index, size_type{1});
        auto left = runs.try_floor_entry(index);
        if (left.has_value() && left->second != index) {
            left.reset();
        }

        auto right = runs.try_ceiling_entry(index);
        if (right.has_value() && right->first != next) {
            right.reset();
        }

        if (left.has_value() && right.has_value()) {
            // The left record is overwritten in place rather than removed and reinserted: set_item
            // replaces a present key, so dropping the redundant remove saves one persistent rebuild
            // without changing the resulting map.
            return runs.remove(right->first).set_item(left->first, right->second);
        }
        if (left.has_value()) {
            return runs.set_item(left->first, next);
        }
        if (right.has_value()) {
            return runs.remove(right->first).set_item(index, right->second);
        }

        return runs.set_item(index, next);
    }

    /// Clears a position, deleting, shrinking at either edge, or splitting its unique containing
    /// run. `O(log(r + 2))` amortized. Raises `std::logic_error` when the position is absent from
    /// the index, which would mean the maintained invariant had already broken.
    [[nodiscard]] static run_index_type remove_dirty_position(
        const run_index_type& runs,
        const size_type index)
    {
        const auto containing = runs.try_floor_entry(index);
        if (!containing.has_value() || containing->second <= index) {
            throw std::logic_error("the cleared dirty position is absent from the run index");
        }

        const auto start = containing->first;
        const auto end = containing->second;
        const auto next = checked_add(index, size_type{1});
        if (start == index && end == next) {
            return runs.remove(start);
        }
        if (start == index) {
            return runs.remove(start).set_item(next, end);
        }
        if (end == next) {
            return runs.set_item(start, index);
        }

        return runs.set_item(start, index).set_item(next, end);
    }

    /// Splices the source's run slice over the destination's, sharing every untouched subtree.
    ///
    /// Four splits and two boundary-spine concatenations, so `O(log n)` regardless of the run's
    /// length, and no element is inspected or compared.
    [[nodiscard]] static vector_type replace_range(
        const vector_type& destination,
        const vector_type& source,
        const persistent_dirty_run& run)
    {
        if (run.start == 0 && run.length == destination.size()) {
            return source;
        }

        auto destination_split = destination.split_at(run.start);
        auto right = destination_split.right.split_at(run.length).right;
        auto source_tail = source.split_at(run.start).right;
        auto middle = source_tail.split_at(run.length).left;
        return destination_split.left.concat(middle).concat(right);
    }

    vector_type current_;
    vector_type checkpoint_;
    run_index_type dirty_runs_;
    size_type dirty_count_ = 0;
};

} // namespace durable7::finger_tree
