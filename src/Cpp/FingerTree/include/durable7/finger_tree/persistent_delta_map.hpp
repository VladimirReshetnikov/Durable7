/// A persistent sorted map with an explicit checkpoint and a coalesced ordered index of the exact
/// net changes from that checkpoint.
///
/// A version holds three immutable roots: the checkpoint state, the current state, and a change
/// index holding exactly one `(before, after)` record for every key class on which the two states
/// differ. The first effective write to a class captures its checkpoint-relative `before`; later
/// writes replace only `after`; returning a class to its checkpoint state removes its record. When
/// the change index empties, the current root snaps back to the exact checkpoint root, so a wholly
/// cancelled epoch is storage-clean as well as extensionally clean. Every operation returns a new
/// value and leaves its inputs valid, sharing unchanged structure.

#pragma once

#include <durable7/finger_tree/comparisons.hpp>
#include <durable7/finger_tree/sorted_map.hpp>

#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace durable7::finger_tree {

/// An equivalence relation over values, deciding which writes are semantic no-ops and when a
/// recorded change cancels. It must be reflexive, symmetric, and transitive, and stable across
/// calls; a relation that is not reflexive records writes that never cancel.
template <class Equal, class Value>
concept value_equivalence_for = requires(Equal equal, const Value& left, const Value& right) {
    { equal(left, right) } -> std::convertible_to<bool>;
    { equal(right, left) } -> std::convertible_to<bool>;
};

/// Classifies one net key change relative to a `persistent_delta_map` checkpoint.
enum class persistent_map_change_kind {
    /// The key class is absent at the checkpoint and present in the current map.
    added,

    /// The key class is present at the checkpoint and absent from the current map.
    removed,

    /// The key class is present at both endpoints with non-equivalent values.
    updated,
};

/// One exact net change between a version's checkpoint and its current state.
///
/// `key` is the checkpoint representative when the class was present at the checkpoint; otherwise
/// it is the current addition episode's first representative. Endpoint presence is carried by
/// `std::optional`, so an absent endpoint stays distinct from a present value that happens to be an
/// empty `std::optional`, an empty container, or a default.
template <class Key, class Value>
struct persistent_map_change final {
    /// The retained key representative of the changed class.
    Key key;

    /// The checkpoint endpoint, or nothing when the class was absent at the checkpoint.
    std::optional<Value> before;

    /// The current endpoint, or nothing when the class is absent from the current map.
    std::optional<Value> after;

    /// The classification implied by the two endpoints. O(1).
    ///
    /// A record absent at both endpoints is not a net change and is never produced by the map.
    /// Throws `std::logic_error` when both endpoints are absent.
    [[nodiscard]] persistent_map_change_kind kind() const
    {
        if (before.has_value()) {
            return after.has_value() ? persistent_map_change_kind::updated
                                     : persistent_map_change_kind::removed;
        }

        if (after.has_value()) {
            return persistent_map_change_kind::added;
        }

        throw std::logic_error("a persistent map change cannot be absent at both endpoints");
    }

    /// Compares two changes by key and both endpoints.
    [[nodiscard]] friend bool operator==(
        const persistent_map_change&,
        const persistent_map_change&) = default;
};

/// Representation statistics from a `persistent_delta_map` structural audit.
struct persistent_delta_map_statistics final {
    /// Number of current entries.
    std::size_t size = 0;

    /// Number of checkpoint entries.
    std::size_t checkpoint_size = 0;

    /// Number of exact net-changed key classes.
    std::size_t change_count = 0;

    /// Number of records absent at the checkpoint and present now.
    std::size_t added_count = 0;

    /// Number of records present at the checkpoint and absent now.
    std::size_t removed_count = 0;

    /// Number of records present at both endpoints with non-equivalent values.
    std::size_t updated_count = 0;

    /// Whether the current state is the very same root as the checkpoint.
    bool is_clean = false;

    /// Compares two statistics values componentwise.
    [[nodiscard]] friend bool operator==(
        const persistent_delta_map_statistics&,
        const persistent_delta_map_statistics&) = default;
};

/// An immutable, fully persistent sorted map with an explicit checkpoint and a coalesced ordered
/// index of the exact net changes from that checkpoint.
///
/// It answers one narrow question cheaply: which key classes differ from the last checkpoint, and
/// what were their checkpoint and current values? A plain persistent map makes versions cheap, but
/// a root handle alone does not name the changed keys; a write log names operations but leaves
/// repeated writes, cancellations, and ordering to be resolved later. This structure maintains the
/// exact net answer online, while the edits happen.
///
/// # Bounds
///
/// Let `N = max(2, |dom(checkpoint) union dom(current)|)`, counting key-comparison equivalence
/// classes rather than concrete key objects, and let `k` be the number of net-changed classes.
/// Point lookup, point update and removal, rank, and neighbour queries are O(log N), matching the
/// underlying persistent ordered map. An effective point edit costs
/// `O(log N + log(k + 1)) = O(log N)` because it touches the current state and at most one record.
/// `checkpoint()` and `rollback()` are O(1). `try_get_change()` is O(log(k + 1)), searching the
/// change index rather than the state. Fully consuming `for_each_change()` is Theta(k + 1), which
/// is output-optimal and is the whole point of the design: it avoids the Theta(k log(N/k + 1))
/// adversarial divergent-path walk that a reference-pruned comparison between two constant-degree
/// path-copied balanced trees must perform. A live version occupies O(N + k) nodes; retaining every
/// successor adds O(log N) nodes per changed point update, the same asymptotic persistence cost as
/// an ordinary path-copied ordered map.
///
/// Unlike the Rust port, whose ordered substrate has no cached extremes, `min_entry()` and
/// `max_entry()` here are worst-case O(1): the substrate is a finger tree, whose outer digits hold
/// both extremes without a descent.
///
/// The mutation surface is deliberately point-only. An eager bulk clear would produce Theta(N)
/// removal records and cannot simultaneously retain an ordinary map's O(1) clear bound, so callers
/// start a fresh epoch with `create()` when they do not need an enumerable delta, and remove
/// entries explicitly when they do. `set_items()` is not an exception: it is exactly the fold over
/// `set_item()` and claims no better bound than the O(m log N) sequence of point writes it
/// replaces.
///
/// # Policies
///
/// Key identity and output order come from the retained `KeyLess` strict weak ordering; two keys
/// are the same class exactly when neither orders before the other. Semantic no-ops and change
/// cancellation come from the retained `ValueEqual` equivalence. "Exact" therefore means exact
/// extensionally under those two policies, not by object identity of the retained key or value
/// representatives. A baseline key representative is retained across point updates and delete or
/// re-add round trips; a newly added class retains its first representative while its net-added
/// record is active, and a later addition after complete cancellation begins a new representative
/// episode. Policy side effects cannot be undone, but a throwing policy never publishes a partial
/// successor: the receiver and every retained branch remain valid and unchanged. `checkpoint()`,
/// `rollback()`, and unrestricted change enumeration invoke no key-order or value-equality callback
/// at all. The range-restricted forms invoke only key comparisons, and only the O(log(k + 1)) many
/// needed to seek the two window boundaries — never a value comparison.
///
/// Concurrent reads over retained versions are safe when both retained policy objects are safe for
/// overlapping calls; every version is immutable and copies in O(1).
///
/// # Divergences from the C# baseline
///
/// - C# `DeltaMapValue<T>` exists only because `null` is a valid `TValue`. `std::optional<Value>`
///   expresses the same presence-safe endpoint, and a stored `std::optional` inside the endpoint's
///   `std::optional` stays unambiguous, so the wrapper is not ported. This is the already
///   precedented workspace treatment of an absent endpoint.
/// - C# takes an `IComparer<TKey>` and an `IEqualityComparer<TValue>` at run time. Here both are
///   compile-time template policies constrained by concepts, matching the rest of this workspace,
///   so the compiler can inline them. Policy compatibility is therefore a property of the type; two
///   maps with the same policy types but different retained policy state are still the same type,
///   exactly as for the other comparator-bearing sorted wrappers.
/// - C# `GetChanges()` is a lazy `IEnumerable`. Enumeration here is either a streaming
///   `for_each_change()`, which keeps the Theta(k + 1) full-consumption bound and allocates no
///   intermediate container, or a materializing `changes_to_vector()`. There is no lazy iterator,
///   because one would have to own a tree cursor over a value the caller may not retain.
/// - Absent lookups return `std::optional`; an out-of-range rank throws `std::out_of_range`, and an
///   extreme of an empty map throws `std::logic_error`, matching the sorted substrate. The C#
///   `OverflowException` on capacity exhaustion surfaces as `std::overflow_error` from the
///   substrate's checked size arithmetic.
/// - The three roots are held through `std::shared_ptr<const ...>`, so root identity is observable
///   through `shares_roots_with()` and the `*_root_identity()` diagnostics that the no-op and
///   canonicalization contracts need. The C# baseline uses `ReferenceEquals` on the same roots.
template <
    class Key,
    class Value,
    class KeyLess = std::less<Key>,
    class ValueEqual = std::equal_to<Value>>
    requires strict_weak_less_for<KeyLess, Key, Key>
        && value_equivalence_for<ValueEqual, Value>
        && std::copyable<Key>
        && std::copyable<Value>
        && std::copyable<KeyLess>
        && std::copyable<ValueEqual>
class persistent_delta_map final {
private:
    /// One `(before, after)` endpoint pair stored in the change index. Absence is `std::nullopt`,
    /// which stays unambiguous even when `Value` is itself a `std::optional`.
    struct delta_record final {
        std::optional<Value> before;
        std::optional<Value> after;
    };

public:
    using key_type = Key;
    using mapped_type = Value;
    using entry_type = std::pair<Key, Value>;
    using key_comparison_type = KeyLess;
    using value_equality_type = ValueEqual;
    using change_type = persistent_map_change<Key, Value>;
    using state_type = sorted_map<Key, Value, KeyLess>;
    using size_type = std::size_t;
    using const_iterator = typename state_type::const_iterator;

private:
    using change_index_type = sorted_map<Key, delta_record, KeyLess>;
    using state_pointer = std::shared_ptr<const state_type>;
    using change_pointer = std::shared_ptr<const change_index_type>;

    persistent_delta_map(
        state_pointer current,
        state_pointer checkpoint,
        change_pointer changes,
        KeyLess less,
        ValueEqual value_equal)
        : current_(std::move(current))
        , checkpoint_(std::move(checkpoint))
        , changes_(std::move(changes))
        , less_(std::move(less))
        , value_equal_(std::move(value_equal))
    {
    }

public:
    /// An empty map whose current state is also its checkpoint, using default-constructed policies.
    /// O(1).
    persistent_delta_map()
        requires std::default_initializable<KeyLess> && std::default_initializable<ValueEqual>
        : persistent_delta_map(create())
    {
    }

    /// An empty map with explicit key-order and value-equivalence policies, which it retains. O(1).
    ///
    /// This is also the O(1) way to start an unrelated clean epoch. It is not equivalent to
    /// clearing while retaining the old checkpoint, which the point-only mutation surface
    /// deliberately omits.
    [[nodiscard]] static persistent_delta_map create(
        KeyLess less = KeyLess{},
        ValueEqual value_equal = ValueEqual{})
    {
        auto state = std::make_shared<const state_type>(state_type(less));
        auto changes = std::make_shared<const change_index_type>(change_index_type(less));
        return persistent_delta_map{
            state,
            state,
            std::move(changes),
            std::move(less),
            std::move(value_equal)};
    }

    /// A clean checkpoint holding `entries`, with the last comparison-equivalent key winning: the
    /// last entry of a class supplies both the retained key representative and the value.
    /// O(m log m) for m supplied entries.
    template <std::ranges::input_range Range>
    [[nodiscard]] static persistent_delta_map create_range(
        Range&& entries,
        KeyLess less = KeyLess{},
        ValueEqual value_equal = ValueEqual{})
    {
        auto state = std::make_shared<const state_type>(
            state_type::from_range(std::forward<Range>(entries), less));
        auto changes = std::make_shared<const change_index_type>(change_index_type(less));
        return persistent_delta_map{
            state,
            state,
            std::move(changes),
            std::move(less),
            std::move(value_equal)};
    }

    /// The retained ordering defining key equivalence and ascending enumeration order.
    [[nodiscard]] const KeyLess& key_comparison() const noexcept
    {
        return less_;
    }

    /// The retained equivalence defining value no-ops and change cancellation.
    [[nodiscard]] const ValueEqual& value_equality() const noexcept
    {
        return value_equal_;
    }

    /// The current persistent ordered-map snapshot. O(1).
    [[nodiscard]] const state_type& current_snapshot() const noexcept
    {
        return *current_;
    }

    /// The checkpoint persistent ordered-map snapshot. O(1).
    [[nodiscard]] const state_type& checkpoint_snapshot() const noexcept
    {
        return *checkpoint_;
    }

    /// Whether the current map holds no entries. O(1).
    [[nodiscard]] bool empty() const noexcept
    {
        return current_->empty();
    }

    /// Number of current entries. O(1) amortized.
    [[nodiscard]] size_type size() const
    {
        return current_->size();
    }

    /// Number of checkpoint entries. O(1) amortized.
    [[nodiscard]] size_type checkpoint_size() const
    {
        return checkpoint_->size();
    }

    /// Number of exact net-changed key classes. O(1) amortized.
    [[nodiscard]] size_type change_count() const
    {
        return changes_->size();
    }

    /// Whether the current map differs semantically from its checkpoint. O(1).
    [[nodiscard]] bool has_changes() const noexcept
    {
        return !changes_->empty();
    }

    /// Whether this version records no change and reuses the exact checkpoint root. O(1).
    [[nodiscard]] bool is_clean() const noexcept
    {
        return changes_->empty() && current_.get() == checkpoint_.get();
    }

    /// The least current entry by key. Worst-case O(1); the finger-tree substrate keeps both
    /// extremes in its outer digits rather than requiring a descent.
    ///
    /// Throws `std::logic_error` when the current map is empty; prefer `try_min_entry()`.
    [[nodiscard]] const entry_type& min_entry() const
    {
        return current_->min_entry();
    }

    /// The greatest current entry by key. Worst-case O(1), for the same reason as `min_entry()`.
    ///
    /// Throws `std::logic_error` when the current map is empty; prefer `try_max_entry()`.
    [[nodiscard]] const entry_type& max_entry() const
    {
        return current_->max_entry();
    }

    /// The least current entry by key, or nothing when empty. Worst-case O(1).
    [[nodiscard]] std::optional<entry_type> try_min_entry() const
    {
        return empty() ? std::optional<entry_type>{} : std::optional<entry_type>{current_->min_entry()};
    }

    /// The greatest current entry by key, or nothing when empty. Worst-case O(1).
    [[nodiscard]] std::optional<entry_type> try_max_entry() const
    {
        return empty() ? std::optional<entry_type>{} : std::optional<entry_type>{current_->max_entry()};
    }

    /// Whether a current entry exists for the key's class. O(log N).
    [[nodiscard]] bool contains_key(const key_type& key) const
    {
        return current_->contains_key(key);
    }

    /// The current value stored for the key's class, or nothing when absent. O(log N).
    [[nodiscard]] std::optional<mapped_type> try_get(const key_type& key) const
    {
        return current_->try_get(key);
    }

    /// The current value stored for the key's class. O(log N).
    ///
    /// Throws `std::out_of_range` when the key is absent; an absent lookup that is not an error is
    /// `try_get()`.
    ///
    /// The returned reference stays valid while this version, or any snapshot sharing the located
    /// node, remains alive.
    [[nodiscard]] const mapped_type& at(const key_type& key) const
    {
        return current_->at(key);
    }

    /// The retained current representative and value for the key's class, or nothing. O(log N).
    [[nodiscard]] std::optional<entry_type> try_get_entry(const key_type& key) const
    {
        return locate(*current_, key);
    }

    /// The current entry at zero-based key rank. O(log N).
    ///
    /// Throws `std::out_of_range` when the rank is not below `size()`.
    [[nodiscard]] const entry_type& entry_at(const size_type index) const
    {
        return current_->entry_at(index);
    }

    /// The zero-based rank of the key's class in the current map, or nothing when absent.
    /// O(log N).
    [[nodiscard]] std::optional<size_type> index_of_key(const key_type& key) const
    {
        return current_->index_of_key(key);
    }

    /// The greatest current entry whose key does not order after `key`. O(log N).
    [[nodiscard]] std::optional<entry_type> try_floor_entry(const key_type& key) const
    {
        return current_->try_floor_entry(key);
    }

    /// The least current entry whose key does not order before `key`. O(log N).
    [[nodiscard]] std::optional<entry_type> try_ceiling_entry(const key_type& key) const
    {
        return current_->try_ceiling_entry(key);
    }

    /// The greatest current entry whose key orders strictly before `key`. O(log N).
    [[nodiscard]] std::optional<entry_type> try_lower_entry(const key_type& key) const
    {
        return current_->try_lower_entry(key);
    }

    /// The least current entry whose key orders strictly after `key`. O(log N).
    [[nodiscard]] std::optional<entry_type> try_higher_entry(const key_type& key) const
    {
        return current_->try_higher_entry(key);
    }

    /// A plain current-state snapshot for the inclusive key range `[low, high]`. O(log N).
    ///
    /// This is a read: the result is an ordinary ordered map and does not open a delta epoch of its
    /// own. An inverted range, decided by the retained ordering, yields an empty map rather than
    /// failing.
    [[nodiscard]] state_type get_range(const key_type& low, const key_type& high) const
    {
        return current_->get_range(low, high);
    }

    /// The current entries in ascending key order. Theta(n).
    [[nodiscard]] std::vector<entry_type> to_vector() const
    {
        return current_->to_vector();
    }

    /// The current keys in ascending order. Theta(n).
    [[nodiscard]] std::vector<key_type> keys_to_vector() const
    {
        return current_->keys_to_vector();
    }

    /// The current values in ascending key order. Theta(n).
    [[nodiscard]] std::vector<mapped_type> values_to_vector() const
    {
        return current_->values_to_vector();
    }

    /// A retained forward iterator over the current entries in ascending key order.
    [[nodiscard]] const_iterator begin() const { return current_->begin(); }
    [[nodiscard]] const_iterator end() const noexcept { return current_->end(); }
    [[nodiscard]] const_iterator cbegin() const { return begin(); }
    [[nodiscard]] const_iterator cend() const noexcept { return end(); }

    /// Adds or replaces one current entry and coalesces its checkpoint-relative change. O(log N).
    ///
    /// A write whose value is equivalent to the current value under the retained equivalence is a
    /// semantic no-op and returns a version sharing all three of the receiver's roots. A write that
    /// returns a class to its checkpoint state removes that class's record, and a version whose
    /// last record cancels reuses the exact checkpoint root.
    ///
    /// Throws `std::overflow_error` when the current map or the change index is already at its
    /// maximum representable size.
    [[nodiscard]] persistent_delta_map set_item(const key_type& key, const mapped_type& value) const
    {
        const auto current_entry = locate(*current_, key);
        if (current_entry.has_value() && values_equivalent(current_entry->second, value)) {
            return *this;
        }

        const auto change_entry = locate(*changes_, key);
        // A class present at the checkpoint keeps its baseline representative; a still-active
        // addition episode keeps its first representative; only a genuinely new class takes the
        // supplied key.
        const auto& representative = current_entry.has_value() ? current_entry->first
            : change_entry.has_value()                         ? change_entry->first
                                                               : key;
        // The first effective write captures `before`; later writes preserve it.
        auto before = change_entry.has_value()
            ? change_entry->second.before
            : current_entry.has_value() ? std::optional<mapped_type>{current_entry->second}
                                        : std::optional<mapped_type>{};
        auto after = std::optional<mapped_type>{value};
        const auto cancels = endpoints_equivalent(before, after);

        auto next_current = current_->set_item(representative, value);
        auto next_changes = cancels
            ? changes_->remove(representative)
            : changes_->set_item(representative, delta_record{std::move(before), std::move(after)});
        return successor(std::move(next_current), std::move(next_changes));
    }

    /// Applies `entries` in iteration order, exactly as repeated `set_item()` would. O(m log N)
    /// for m entries.
    ///
    /// This is a convenience and an allocation saving over repeated single assignment, not a
    /// different algorithm: it is the left fold of `set_item()` over `entries`, so every
    /// coalescing, cancellation, representative-retention, and checkpoint-snap rule holds verbatim
    /// and no bound stronger than the fold it replaces is claimed. An empty range, or one whose
    /// every entry is a semantic no-op, returns a version sharing all three roots. Because each
    /// intermediate value is itself an immutable published-nothing successor, a policy exception
    /// thrown partway leaves the receiver and every retained branch unchanged.
    template <std::ranges::input_range Range>
    [[nodiscard]] persistent_delta_map set_items(Range&& entries) const
    {
        auto result = *this;
        for (auto&& entry : entries) {
            result = result.set_item(entry.first, entry.second);
        }

        return result;
    }

    /// Removes one current entry and coalesces its checkpoint-relative change. O(log N).
    ///
    /// Removing an absent key is a no-op returning a version sharing all three of the receiver's
    /// roots. Removing a class added since the checkpoint cancels its record instead of recording a
    /// removal, and a version whose last record cancels reuses the exact checkpoint root.
    [[nodiscard]] persistent_delta_map remove(const key_type& key) const
    {
        const auto current_entry = locate(*current_, key);
        if (!current_entry.has_value()) {
            return *this;
        }

        const auto change_entry = locate(*changes_, key);
        const auto& representative =
            change_entry.has_value() ? change_entry->first : current_entry->first;
        auto before = change_entry.has_value()
            ? change_entry->second.before
            : std::optional<mapped_type>{current_entry->second};
        auto after = std::optional<mapped_type>{};
        const auto cancels = endpoints_equivalent(before, after);

        auto next_current = current_->remove(current_entry->first);
        auto next_changes = cancels
            ? changes_->remove(representative)
            : changes_->set_item(representative, delta_record{std::move(before), std::move(after)});
        return successor(std::move(next_current), std::move(next_changes));
    }

    /// Makes the current snapshot the new checkpoint and resets the change index. O(1).
    ///
    /// An already-clean version returns a value sharing all three of its roots, and no key-order or
    /// value-equivalence callback is invoked.
    [[nodiscard]] persistent_delta_map checkpoint() const
    {
        if (is_clean()) {
            return *this;
        }

        return persistent_delta_map{current_, current_, empty_changes(), less_, value_equal_};
    }

    /// Returns to this version's checkpoint and resets the change index. O(1).
    ///
    /// An already-clean version returns a value sharing all three of its roots, and no key-order or
    /// value-equivalence callback is invoked.
    [[nodiscard]] persistent_delta_map rollback() const
    {
        if (is_clean()) {
            return *this;
        }

        return persistent_delta_map{checkpoint_, checkpoint_, empty_changes(), less_, value_equal_};
    }

    /// One exact checkpoint-relative net change, or nothing when the class has none.
    /// O(log(k + 1)).
    [[nodiscard]] std::optional<change_type> try_get_change(const key_type& key) const
    {
        const auto entry = locate(*changes_, key);
        if (!entry.has_value()) {
            return std::nullopt;
        }

        return change_type{entry->first, entry->second.before, entry->second.after};
    }

    /// Applies `callback` to each exact net change once, in ascending key order. Theta(k + 1) and
    /// output-optimal; invokes no key-order or value-equivalence callback.
    template <class Callback>
        requires std::invocable<Callback&, const change_type&>
    void for_each_change(Callback callback) const
    {
        for (const auto& entry : *changes_) {
            std::invoke(callback, change_type{entry.first, entry.second.before, entry.second.after});
        }
    }

    /// The exact net changes once each in ascending key order. Theta(k + 1); invokes no key-order
    /// or value-equivalence callback.
    [[nodiscard]] std::vector<change_type> changes_to_vector() const
    {
        auto changes = std::vector<change_type>{};
        changes.reserve(change_count());
        for_each_change([&changes](const change_type& change) { changes.push_back(change); });
        return changes;
    }

    /// Applies `callback` to each exact net change whose key lies in the inclusive range
    /// `[low, high]`, once each, in the same ascending key order `for_each_change()` uses.
    ///
    /// The change index is itself an ordered map under the collection's ordering, so this is a
    /// boundary seek plus a bounded walk over the isolated in-range sub-map, never a filter over
    /// the whole change set: the cost is O(log(k + 1)) for the seek, which is two measured splits
    /// allocating O(log(k + 1)) spine nodes, plus Theta(1) per yielded change. It does not depend
    /// on how many changes fall outside the range. Only boundary keys are compared, so the ordering
    /// is invoked O(log(k + 1)) times and the value equivalence not at all.
    ///
    /// An inverted range yields no changes rather than failing, matching `get_range()`. "Inverted"
    /// is decided by the retained ordering, so under a descending order the low endpoint is the
    /// numerically greater key.
    template <class Callback>
        requires std::invocable<Callback&, const change_type&>
    void for_each_change_in_range(
        const key_type& low,
        const key_type& high,
        Callback callback) const
    {
        const auto restricted = changes_->get_range(low, high);
        for (const auto& entry : restricted) {
            std::invoke(callback, change_type{entry.first, entry.second.before, entry.second.after});
        }
    }

    /// The exact net changes whose keys lie in the inclusive range `[low, high]`, in ascending key
    /// order. O(log(k + 1)) to seek the boundaries plus Theta(output); see
    /// `for_each_change_in_range()` for why this is not a filter over all k records.
    [[nodiscard]] std::vector<change_type> changes_in_range(
        const key_type& low,
        const key_type& high) const
    {
        auto changes = std::vector<change_type>{};
        for_each_change_in_range(
            low,
            high,
            [&changes](const change_type& change) { changes.push_back(change); });
        return changes;
    }

    /// The current state root's address, for tests distinguishing a shared root from a rebuilt
    /// equal value. O(1).
    [[nodiscard]] const void* current_root_identity() const noexcept
    {
        return current_.get();
    }

    /// The checkpoint state root's address. O(1).
    [[nodiscard]] const void* checkpoint_root_identity() const noexcept
    {
        return checkpoint_.get();
    }

    /// The change index root's address. O(1).
    [[nodiscard]] const void* change_root_identity() const noexcept
    {
        return changes_.get();
    }

    /// Whether both versions name the very same current state root. A representation test, not an
    /// equality test. O(1).
    [[nodiscard]] bool shares_current_root_with(const persistent_delta_map& other) const noexcept
    {
        return current_.get() == other.current_.get();
    }

    /// Whether both versions name the very same checkpoint root. O(1).
    [[nodiscard]] bool shares_checkpoint_root_with(const persistent_delta_map& other) const noexcept
    {
        return checkpoint_.get() == other.checkpoint_.get();
    }

    /// Whether both versions name the very same change index root. O(1).
    [[nodiscard]] bool shares_change_root_with(const persistent_delta_map& other) const noexcept
    {
        return changes_.get() == other.changes_.get();
    }

    /// Whether both versions are backed by all three of the same roots, so neither can observe a
    /// change made through the other. A representation test, not an equality test. O(1).
    [[nodiscard]] bool shares_roots_with(const persistent_delta_map& other) const noexcept
    {
        return shares_current_root_with(other)
            && shares_checkpoint_root_with(other)
            && shares_change_root_with(other);
    }

    /// Walks the whole representation and returns its statistics.
    ///
    /// This is diagnostic validation, not part of any operation's cost: it is O((N + k) log N) and
    /// does invoke the key-order and value-equivalence policies. Throws `std::logic_error` naming
    /// the first invariant it finds violated.
    [[nodiscard]] persistent_delta_map_statistics validate_structure() const
    {
        require_ascending(*current_, "the current state is not strictly ascending by key");
        require_ascending(*checkpoint_, "the checkpoint state is not strictly ascending by key");
        require_ascending(*changes_, "the change index is not strictly ascending by key");

        auto statistics = persistent_delta_map_statistics{};
        statistics.size = current_->size();
        statistics.checkpoint_size = checkpoint_->size();
        statistics.change_count = changes_->size();
        statistics.is_clean = current_.get() == checkpoint_.get();

        if (changes_->empty() && !statistics.is_clean) {
            throw std::logic_error(
                "persistent_delta_map invariant violated: a version without changes does not reuse "
                "its checkpoint root");
        }

        for (const auto& entry : *changes_) {
            const auto& record = entry.second;
            if (endpoints_equivalent(record.before, record.after)) {
                throw std::logic_error(
                    "persistent_delta_map invariant violated: a recorded change has equivalent "
                    "endpoints");
            }

            if (!endpoint_matches(record.before, locate(*checkpoint_, entry.first))) {
                throw std::logic_error(
                    "persistent_delta_map invariant violated: a change's before endpoint differs "
                    "from the checkpoint state");
            }

            if (!endpoint_matches(record.after, locate(*current_, entry.first))) {
                throw std::logic_error(
                    "persistent_delta_map invariant violated: a change's after endpoint differs "
                    "from the current state");
            }

            if (!record.before.has_value() && record.after.has_value()) {
                ++statistics.added_count;
            } else if (record.before.has_value() && !record.after.has_value()) {
                ++statistics.removed_count;
            } else if (record.before.has_value() && record.after.has_value()) {
                ++statistics.updated_count;
            } else {
                throw std::logic_error(
                    "persistent_delta_map invariant violated: a recorded change is absent at both "
                    "endpoints");
            }
        }

        auto expected = size_type{0};
        for (const auto& entry : *checkpoint_) {
            const auto current_entry = locate(*current_, entry.first);
            const auto unchanged = current_entry.has_value()
                && values_equivalent(entry.second, current_entry->second);
            if (unchanged) {
                continue;
            }

            ++expected;
            if (!locate(*changes_, entry.first).has_value()) {
                throw std::logic_error(
                    "persistent_delta_map invariant violated: a checkpoint difference has no "
                    "recorded change");
            }
        }

        for (const auto& entry : *current_) {
            if (locate(*checkpoint_, entry.first).has_value()) {
                continue;
            }

            ++expected;
            if (!locate(*changes_, entry.first).has_value()) {
                throw std::logic_error(
                    "persistent_delta_map invariant violated: a current addition has no recorded "
                    "change");
            }
        }

        if (expected != statistics.change_count) {
            throw std::logic_error(
                "persistent_delta_map invariant violated: the recorded change cardinality is not "
                "exact");
        }

        return statistics;
    }

private:
    [[nodiscard]] change_pointer empty_changes() const
    {
        return changes_->empty()
            ? changes_
            : std::make_shared<const change_index_type>(change_index_type(less_));
    }

    /// A wholly cancelled epoch snaps back to the exact checkpoint root, so clean versions are
    /// canonical and later checkpoint, rollback, and change queries stay O(1).
    [[nodiscard]] persistent_delta_map successor(
        state_type next_current,
        change_index_type next_changes) const
    {
        auto changes = std::make_shared<const change_index_type>(std::move(next_changes));
        auto current = changes->empty()
            ? checkpoint_
            : std::make_shared<const state_type>(std::move(next_current));
        return persistent_delta_map{
            std::move(current),
            checkpoint_,
            std::move(changes),
            less_,
            value_equal_};
    }

    /// The stored entry whose key is comparison-equivalent to `key`, or nothing. One ceiling seek,
    /// so O(log n) in the searched map.
    template <class Mapped>
    [[nodiscard]] std::optional<std::pair<Key, Mapped>> locate(
        const sorted_map<Key, Mapped, KeyLess>& map,
        const key_type& key) const
    {
        auto entry = map.try_ceiling_entry(key);
        if (entry.has_value() && compare_with(less_, entry->first, key) == 0) {
            return entry;
        }

        return std::nullopt;
    }

    [[nodiscard]] bool values_equivalent(const mapped_type& left, const mapped_type& right) const
    {
        return static_cast<bool>(std::invoke(value_equal_, left, right));
    }

    [[nodiscard]] bool endpoints_equivalent(
        const std::optional<mapped_type>& left,
        const std::optional<mapped_type>& right) const
    {
        if (left.has_value() != right.has_value()) {
            return false;
        }

        return !left.has_value() || values_equivalent(*left, *right);
    }

    [[nodiscard]] bool endpoint_matches(
        const std::optional<mapped_type>& endpoint,
        const std::optional<entry_type>& stored) const
    {
        if (endpoint.has_value() != stored.has_value()) {
            return false;
        }

        return !endpoint.has_value() || values_equivalent(*endpoint, stored->second);
    }

    template <class Mapped>
    void require_ascending(const sorted_map<Key, Mapped, KeyLess>& map, const char* message) const
    {
        auto previous = std::optional<Key>{};
        for (const auto& entry : map) {
            if (previous.has_value() && compare_with(less_, *previous, entry.first) >= 0) {
                throw std::logic_error(
                    std::string{"persistent_delta_map invariant violated: "} + message);
            }

            previous = entry.first;
        }
    }

    state_pointer current_;
    state_pointer checkpoint_;
    change_pointer changes_;
    [[no_unique_address]] KeyLess less_{};
    [[no_unique_address]] ValueEqual value_equal_{};
};

} // namespace durable7::finger_tree
