/// A persistent sequence that ranks and selects events whose occurrence depends on finite left
/// context.
///
/// An ordinary sum-measured sequence ranks elements that carry a fixed local weight. It cannot rank
/// a comma that is a field separator in one prefix context and quoted data in another, because no
/// single weight is correct for both. A streaming automaton keeps that context but has to replay a
/// prefix after every edit.
///
/// This sequence lifts a finite deterministic machine into the measure of the measured finger tree.
/// Every subtree caches, *for every possible incoming state*, the outgoing state and the number of
/// events emitted while consuming that subtree. Ordered composition feeds the left subtree's
/// outgoing state into the right subtree's summary, which turns a context-dependent scan into an
/// associative measure:
///
///     (A * B).next(q)   = B.next(A.next(q))
///     (A * B).events(q) = A.events(q) + B.events(A.next(q))
///     (A * B).count     = A.count + B.count
///
/// The identity maps every state to itself, emits zero events, and holds zero elements. Composition
/// is associative but deliberately **not** commutative: swapping the operands feeds a different
/// state into the other summary. Because event counts are nonnegative, the select predicate
/// `prefix.events(q0) > r` is monotone, so one measure-directed descent finds the element that
/// emitted event `r` — no binary search and no prefix replay.

#pragma once

#include <durable7/finger_tree/detail/common.hpp>
#include <durable7/finger_tree/measured_finger_tree.hpp>
#include <durable7/finger_tree/measures.hpp>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace durable7::finger_tree {

/// One transition of a deterministic additive event machine.
///
/// A single transition may emit more than one event. Rank counts every emitted event, while select
/// maps each event back to its containing input element and its zero-based ordinal within that
/// element.
struct contextual_event_transition final {
    /// The state after consuming the element. Must be below the machine's state count.
    std::size_t next_state = 0;
    /// The nonnegative number of logical events the transition emits.
    std::int64_t event_count = 0;

    friend bool operator==(
        const contextual_event_transition&,
        const contextual_event_transition&) = default;
};

/// A finite deterministic machine whose transitions emit nonnegative event counts.
///
/// Implementations are compile-time policy *types*: no instance is ever created, which is how this
/// port spells the C# static-abstract interface. `state_count` must be a positive constant
/// expression convertible to `std::size_t`, and it is fixed for the lifetime of the program. For
/// every state in `0 .. state_count - 1`, `transition` must return a state in the same range
/// together with a nonnegative event count, and it must be deterministic and free of side effects.
///
/// A machine that declares zero states is rejected here, at constraint checking, rather than at
/// first use.
template <class Machine, class Element>
concept contextual_event_machine = requires(const Element& element, const std::size_t state) {
    typename std::integral_constant<std::size_t, Machine::state_count>;
    requires Machine::state_count > 0;
    { Machine::transition(state, element) } -> std::same_as<contextual_event_transition>;
};

/// The result of running a contextual event machine over a prefix.
struct contextual_prefix_summary final {
    /// The machine state after the prefix.
    std::size_t final_state = 0;
    /// The number of events the prefix emitted.
    std::int64_t event_count = 0;

    friend bool operator==(
        const contextual_prefix_summary&,
        const contextual_prefix_summary&) = default;
};

/// Identifies one contextual event and the input element that emitted it.
struct contextual_event_location final {
    /// The zero-based index of the emitting element.
    std::size_t element_index = 0;
    /// The event's zero-based ordinal among the events that one transition emitted.
    std::int64_t event_index_in_element = 0;
    /// The machine state before the element.
    std::size_t state_before = 0;
    /// The machine state after the element.
    std::size_t state_after = 0;
    /// The total number of events that transition emitted.
    std::int64_t element_event_count = 0;

    friend bool operator==(
        const contextual_event_location&,
        const contextual_event_location&) = default;
};

/// Shape and machine measurements from a structural audit.
struct contextual_rank_validation_statistics final {
    /// The number of elements, recounted by enumeration.
    std::size_t count = 0;
    /// The machine's state count.
    std::size_t state_count = 0;
    /// The largest total event count over all initial states, recomputed by direct scan.
    std::int64_t maximum_total_event_count = 0;

    friend bool operator==(
        const contextual_rank_validation_statistics&,
        const contextual_rank_validation_statistics&) = default;
};

namespace detail {

/// The effect one subtree has on one particular incoming machine state.
struct contextual_state_effect final {
    std::size_t next_state = 0;
    std::int64_t event_count = 0;

    friend bool operator==(const contextual_state_effect&, const contextual_state_effect&) = default;
};

/// Adds two event counts, trapping on overflow rather than wrapping. Both operands are nonnegative:
/// leaf summaries reject a negative machine event count before any table is published, and every
/// composed count is a sum of nonnegative counts.
[[nodiscard]] inline std::int64_t checked_event_add(const std::int64_t left, const std::int64_t right)
{
    if (right < 0 || left < 0) {
        throw std::logic_error("contextual event counts must be nonnegative");
    }

    if (left > (std::numeric_limits<std::int64_t>::max)() - right) {
        throw std::overflow_error("contextual event count overflow");
    }

    return left + right;
}

/// A subtree summary: one effect per machine state, plus the element count.
///
/// The effect table is filled eagerly whenever a summary is created; nothing about it is deferred
/// behind a thunk. It is held through a shared pointer purely so that copying a cached measure out
/// of the tree stays O(1) even though the table itself is O(s) wide.
template <std::size_t StateCount>
class contextual_summary final {
public:
    using effect_table = std::array<contextual_state_effect, StateCount>;
    using table_pointer = std::shared_ptr<const effect_table>;

    /// The monoid identity: every state maps to itself, no events, no elements.
    contextual_summary()
        : effects_(identity_table())
    {
    }

    /// Constructs the summary from an element count and an eagerly filled effect table.
    contextual_summary(const std::size_t count, table_pointer effects)
        : count_(count)
        , effects_(std::move(effects))
    {
        if (effects_ == nullptr) {
            throw std::invalid_argument("a contextual summary requires an effect table");
        }
    }

    /// The number of elements the summarized subtree holds.
    [[nodiscard]] std::size_t count() const noexcept
    {
        return count_;
    }

    /// The state the subtree leaves the machine in, given an incoming state.
    [[nodiscard]] std::size_t final_state(const std::size_t initial_state) const
    {
        return effects_->at(initial_state).next_state;
    }

    /// The number of events the subtree emits, given an incoming state.
    [[nodiscard]] std::int64_t event_count(const std::size_t initial_state) const
    {
        return effects_->at(initial_state).event_count;
    }

    /// The outgoing state and emitted-event count together.
    [[nodiscard]] contextual_prefix_summary evaluate(const std::size_t initial_state) const
    {
        const auto effect = effects_->at(initial_state);
        return contextual_prefix_summary{effect.next_state, effect.event_count};
    }

    friend bool operator==(const contextual_summary& left, const contextual_summary& right)
    {
        return left.count_ == right.count_
            && (left.effects_ == right.effects_ || *left.effects_ == *right.effects_);
    }

private:
    [[nodiscard]] static const table_pointer& identity_table()
    {
        static const table_pointer value = [] {
            auto table = std::make_shared<effect_table>();
            for (auto state = std::size_t{0}; state != StateCount; ++state) {
                (*table)[state] = contextual_state_effect{state, 0};
            }

            return table_pointer{std::move(table)};
        }();

        return value;
    }

    std::size_t count_ = 0;
    table_pointer effects_;
};

/// The measure policy that lifts a contextual event machine into the measured finger tree.
template <class Element, class Machine>
    requires contextual_event_machine<Machine, Element>
struct contextual_summary_measure final {
    /// The machine's finite state count.
    static constexpr std::size_t machine_state_count = static_cast<std::size_t>(Machine::state_count);

    using measure_type = contextual_summary<machine_state_count>;
    using effect_table = typename measure_type::effect_table;
    using table_pointer = typename measure_type::table_pointer;

    /// Runs the machine once and rejects a result that violates the machine contract.
    [[nodiscard]] static contextual_event_transition checked_transition(
        const std::size_t state,
        const Element& element)
    {
        const auto transition = Machine::transition(state, element);
        if (transition.next_state >= machine_state_count) {
            throw std::logic_error("the contextual event machine returned an invalid next state");
        }

        if (transition.event_count < 0) {
            throw std::logic_error("the contextual event machine returned a negative event count");
        }

        return transition;
    }

    /// The identity: the measure of an empty tree.
    [[nodiscard]] static measure_type empty()
    {
        return measure_type{};
    }

    /// The measure of one element, obtained by running the machine once from every state.
    [[nodiscard]] static measure_type measure(const Element& element)
    {
        auto table = std::make_shared<effect_table>();
        for (auto state = std::size_t{0}; state != machine_state_count; ++state) {
            const auto transition = checked_transition(state, element);
            (*table)[state] = contextual_state_effect{transition.next_state, transition.event_count};
        }

        return measure_type{1, table_pointer{std::move(table)}};
    }

    /// Ordered composition, `left` preceding `right`. Associative but not commutative.
    [[nodiscard]] static measure_type combine(const measure_type& left, const measure_type& right)
    {
        if (left.count() == 0) {
            return right;
        }

        if (right.count() == 0) {
            return left;
        }

        // Structural growth is rejected before any table is allocated or published.
        const auto count = checked_add(left.count(), right.count());
        auto table = std::make_shared<effect_table>();
        for (auto state = std::size_t{0}; state != machine_state_count; ++state) {
            const auto middle = left.final_state(state);
            (*table)[state] = contextual_state_effect{
                right.final_state(middle),
                checked_event_add(left.event_count(state), right.event_count(middle))};
        }

        return measure_type{count, table_pointer{std::move(table)}};
    }
};

} // namespace detail

template <typename T, typename Machine>
    requires contextual_event_machine<Machine, T>
class contextual_rank_sequence;

/// The two halves a split produced.
///
/// Both halves are complete sequences that share structure with the original, which itself stays
/// valid.
template <typename T, typename Machine>
    requires contextual_event_machine<Machine, T>
struct contextual_rank_split final {
    /// The elements before the boundary.
    contextual_rank_sequence<T, Machine> left;
    /// The elements at and after the boundary.
    contextual_rank_sequence<T, Machine> right;
};

/// An immutable sequence indexed by events whose occurrence depends on finite left context.
///
/// `Machine` is the deterministic additive event-machine policy. Because it is a type parameter
/// rather than a stored value, machine compatibility is a compile-time property: only sequences
/// built under exactly the same machine name the same type, so C#'s type-identity gate on
/// concatenation is preserved without a runtime check.
///
/// ## Bounds
///
/// Let `n` be the element count and `s` the machine's fixed state count. The substrate is this
/// package's `finger_tree`, a genuine Hinze-Paterson tree: 1..4-element digits, a lazy middle spine
/// whose measure probe answers without forcing, and a memoized root measure. The reference bounds
/// therefore hold here as stated, rather than degrading the way they must on a height-balanced join
/// core.
///
/// * whole-sequence `evaluate` is O(1): it reads the memoized root summary and copies one shared
///   effect-table handle. Every operation forces that summary while its result is still
///   unpublished, so the substrate's amortized measure cost is charged to the constructing
///   operation and never to a later read;
/// * `size` and `empty` are O(1) for the same reason;
/// * `at`, `evaluate_prefix`, `event_rank`, `try_select_event`, `insert_at`, `set_item`,
///   `remove_at`, `split_at`, and `get_range` are O(s log n) amortized: one measure-directed
///   descent over O(log n) levels, each level composing a constant number of O(s) effect tables.
///   `evaluate_prefix` is O(1) at either endpoint, where the answer is the identity or the cached
///   root summary;
/// * `prepend` and `append` are O(s) amortized: a digit push is amortized O(1) node work, and each
///   rebuilt node composes one O(s) effect table. Every node's summary is memoized on first read,
///   so a run of m pushes creates O(m) nodes and pays O(s) per node in total;
/// * `concat` is O(s log(min(n, m))) amortized: the seam descends both middle spines in lockstep
///   and stops at the shallower operand, rebuilding only that many nodes;
/// * iteration and `to_vector` are O(n), building from a source is O(s n), and `validate_structure`
///   is O(s n).
///
/// Each retained changed spine adds O(s log n) summary storage while untouched structure stays
/// shared. When the machine is fixed, `s` is a constant and the contextual queries are O(log n)
/// instead of the Theta(n) replay a state-free persistent sequence needs.
///
/// ## Deliberate limits
///
/// Context must be finite and must flow left to right. Event weights must be nonnegative so select
/// stays monotone. One input element occupies one measured leaf, so this is not a chunked text rope
/// and makes no cache-density claim. There is no way to change the machine of an existing root;
/// rebuilding under another machine costs O(s n).
///
/// ## Failure contract
///
/// Index, boundary, range, and initial-state violations throw `std::out_of_range`, except that
/// `try_select_event` reports an out-of-range initial state or an out-of-range event rank as
/// `std::nullopt`. A machine that returns a state outside its declared range or a negative event
/// count throws `std::logic_error`; a composed event total that exceeds `std::int64_t` throws
/// `std::overflow_error`, and an element count that exceeds `std::size_t` throws the shared
/// checked-addition `std::overflow_error`. Every version is an immutable persistent value, so a
/// throwing operation publishes no successor and leaves every retained version readable and
/// unchanged.
template <typename T, typename Machine>
    requires contextual_event_machine<Machine, T>
class contextual_rank_sequence final {
private:
    using measure_policy_type = detail::contextual_summary_measure<T, Machine>;
    using tree_type = finger_tree<T, measure_policy_type>;
    using summary_type = typename measure_policy_type::measure_type;

    /// Matches once the accumulated element count passes a threshold, which is how a positional
    /// split is expressed as a measured one.
    struct position_predicate final {
        std::size_t index = 0;

        [[nodiscard]] bool operator()(const summary_type& measure) const noexcept
        {
            return measure.count() > index;
        }
    };

    /// Matches once the accumulated event count from one incoming state passes a rank. Monotone
    /// because every emitted event count is nonnegative.
    struct event_predicate final {
        std::size_t initial_state = 0;
        std::int64_t event_index = 0;

        [[nodiscard]] bool operator()(const summary_type& measure) const
        {
            return measure.event_count(initial_state) > event_index;
        }
    };

public:
    using value_type = T;
    using machine_type = Machine;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using const_reference = const value_type&;
    using const_iterator = typename tree_type::const_iterator;
    using split_result = contextual_rank_split<T, Machine>;

    /// The machine's finite state count.
    static constexpr size_type state_count = measure_policy_type::machine_state_count;

    /// The empty sequence. Its summary maps every state to itself with zero events.
    contextual_rank_sequence() = default;

    /// A sequence holding the listed elements, in input order.
    ///
    /// Delegates to the forcing constructor so this path charges the root-summary walk, and any
    /// machine-contract or overflow failure it uncovers, to construction rather than to a later
    /// read of an already-published value.
    contextual_rank_sequence(std::initializer_list<value_type> values)
        : contextual_rank_sequence(tree_type{values.begin(), values.end()})
    {
    }

    /// The canonical empty sequence.
    [[nodiscard]] static contextual_rank_sequence empty_sequence()
    {
        return {};
    }

    /// A sequence holding a span's elements in input order. O(s n).
    [[nodiscard]] static contextual_rank_sequence create(const std::span<const value_type> items)
    {
        return contextual_rank_sequence{tree_type::from_range(items)};
    }

    /// A sequence built by enumerating a source once, in input order. O(s n). Passing this exact
    /// sequence type returns it while retaining its root.
    template <std::ranges::input_range Range>
        requires std::constructible_from<value_type, std::ranges::range_reference_t<Range>>
    [[nodiscard]] static contextual_rank_sequence from_range(Range&& values)
    {
        if constexpr (std::same_as<std::remove_cvref_t<Range>, contextual_rank_sequence>) {
            return std::forward<Range>(values);
        } else {
            return contextual_rank_sequence{tree_type::from_range(std::forward<Range>(values))};
        }
    }

    /// Whether the sequence holds no elements. O(1).
    [[nodiscard]] bool empty() const noexcept
    {
        return tree_.empty();
    }

    /// Number of stored elements, read from the memoized root summary. O(1).
    [[nodiscard]] size_type size() const
    {
        return tree_.measure().count();
    }

    /// The element at a zero-based index. O(s log n).
    ///
    /// The reference names the canonical stored element and follows the lifetime of this snapshot,
    /// or of any later snapshot sharing the located node.
    [[nodiscard]] const_reference at(const size_type index) const
    {
        throw_if_index_out_of_range(index, size());
        const auto located = tree_.try_locate_reference(position_predicate{index});
        if (!located.has_value()) {
            throw std::logic_error("the contextual event summary is inconsistent");
        }

        return *located.item;
    }

    /// The element at a zero-based index. O(s log n).
    [[nodiscard]] const_reference operator[](const size_type index) const
    {
        return at(index);
    }

    /// Runs the machine over the whole sequence from an initial state. O(1): the answer is the
    /// memoized root summary, which every constructing operation already forced.
    [[nodiscard]] contextual_prefix_summary evaluate(const size_type initial_state) const
    {
        throw_if_state_out_of_range(initial_state);
        return tree_.measure().evaluate(initial_state);
    }

    /// Runs the machine over the first `element_count` elements from an initial state.
    /// O(s log n), and O(1) at either endpoint.
    [[nodiscard]] contextual_prefix_summary evaluate_prefix(
        const size_type element_count,
        const size_type initial_state) const
    {
        const auto count = size();
        throw_if_split_index_out_of_range(element_count, count);
        throw_if_state_out_of_range(initial_state);
        if (element_count == 0) {
            return contextual_prefix_summary{initial_state, 0};
        }

        if (element_count == count) {
            return tree_.measure().evaluate(initial_state);
        }

        const auto located = tree_.try_locate_reference(position_predicate{element_count});
        if (!located.has_value()) {
            throw std::logic_error("the contextual event summary is inconsistent");
        }

        return located.measure_before.evaluate(initial_state);
    }

    /// The number of contextual events strictly before an element boundary. O(s log n).
    [[nodiscard]] std::int64_t event_rank(
        const size_type element_count,
        const size_type initial_state) const
    {
        return evaluate_prefix(element_count, initial_state).event_count;
    }

    /// Locates the zero-based contextual event `event_index`, counting from `initial_state`.
    ///
    /// Reports nothing when the initial state is outside the machine's range or the rank is
    /// negative, or at or past the sequence's total event count. O(s log n): exactly one
    /// measure-directed descent, which recovers both the boundary element and the prefix summary at
    /// it, followed by exactly one machine transition. There is no binary search and no replay.
    [[nodiscard]] std::optional<contextual_event_location> try_select_event(
        const std::int64_t event_index,
        const size_type initial_state) const
    {
        if (initial_state >= state_count || event_index < 0) {
            return std::nullopt;
        }

        if (event_index >= tree_.measure().event_count(initial_state)) {
            return std::nullopt;
        }

        const auto located = tree_.try_locate_reference(
            event_predicate{initial_state, event_index});
        if (!located.has_value()) {
            throw std::logic_error("the contextual event summary is inconsistent");
        }

        const auto& before = located.measure_before;
        const auto state_before = before.final_state(initial_state);
        const auto transition = measure_policy_type::checked_transition(state_before, *located.item);
        return contextual_event_location{
            before.count(),
            event_index - before.event_count(initial_state),
            state_before,
            transition.next_state,
            transition.event_count};
    }

    /// A sequence with the element prepended. O(s) amortized.
    [[nodiscard]] contextual_rank_sequence prepend(value_type value) const
    {
        (void)checked_add(size(), size_type{1});
        return contextual_rank_sequence{tree_.prepend(std::move(value))};
    }

    /// A sequence with the element appended. O(s) amortized.
    [[nodiscard]] contextual_rank_sequence append(value_type value) const
    {
        (void)checked_add(size(), size_type{1});
        return contextual_rank_sequence{tree_.append(std::move(value))};
    }

    /// A sequence with the element added at the front. O(s) amortized.
    [[nodiscard]] contextual_rank_sequence push_front(value_type value) const
    {
        return prepend(std::move(value));
    }

    /// A sequence with the element added at the back. O(s) amortized.
    [[nodiscard]] contextual_rank_sequence push_back(value_type value) const
    {
        return append(std::move(value));
    }

    /// The concatenation of two sequences built under the same machine, sharing both operands'
    /// unchanged structure. An empty operand is not copied: the result retains the other operand's
    /// root. O(s log(min(n, m))) amortized.
    [[nodiscard]] contextual_rank_sequence concat(const contextual_rank_sequence& other) const
    {
        (void)checked_add(size(), other.size());
        return contextual_rank_sequence{tree_.concat(other.tree_)};
    }

    /// A sequence with the element inserted so that `index` old elements precede it. O(s log n).
    [[nodiscard]] contextual_rank_sequence insert_at(const size_type index, value_type value) const
    {
        const auto count = size();
        throw_if_insert_index_out_of_range(index, count);
        (void)checked_add(count, size_type{1});
        if (index == 0) {
            return contextual_rank_sequence{tree_.prepend(std::move(value))};
        }

        if (index == count) {
            return contextual_rank_sequence{tree_.append(std::move(value))};
        }

        auto parts = tree_.split(position_predicate{index});
        return contextual_rank_sequence{
            parts.left.append(std::move(value)).concat(parts.right)};
    }

    /// A sequence with the element at the index replaced. O(s log n).
    [[nodiscard]] contextual_rank_sequence set_item(const size_type index, value_type value) const
    {
        throw_if_index_out_of_range(index, size());
        auto parts = tree_.try_split_find(position_predicate{index});
        if (!parts.has_value()) {
            throw std::logic_error("the contextual event summary is inconsistent");
        }

        return contextual_rank_sequence{
            parts->left.append(std::move(value)).concat(parts->right)};
    }

    /// A sequence with the element at the index replaced. O(s log n).
    [[nodiscard]] contextual_rank_sequence set_at(const size_type index, value_type value) const
    {
        return set_item(index, std::move(value));
    }

    /// A sequence without the element at the index. O(s log n).
    [[nodiscard]] contextual_rank_sequence remove_at(const size_type index) const
    {
        throw_if_index_out_of_range(index, size());
        auto parts = tree_.try_split_find(position_predicate{index});
        if (!parts.has_value()) {
            throw std::logic_error("the contextual event summary is inconsistent");
        }

        return contextual_rank_sequence{parts->left.concat(parts->right)};
    }

    /// Splits at a zero-based element boundary. Splitting at either endpoint retains the receiver's
    /// root in the nonempty half. O(s log n).
    [[nodiscard]] split_result split_at(const size_type index) const
    {
        const auto count = size();
        throw_if_split_index_out_of_range(index, count);
        if (index == 0) {
            return split_result{contextual_rank_sequence{}, *this};
        }

        if (index == count) {
            return split_result{*this, contextual_rank_sequence{}};
        }

        auto parts = tree_.split(position_predicate{index});
        return split_result{
            contextual_rank_sequence{std::move(parts.left)},
            contextual_rank_sequence{std::move(parts.right)}};
    }

    /// The `count` elements starting at `index`. A range covering the whole sequence retains the
    /// receiver's root. O(s log n).
    [[nodiscard]] contextual_rank_sequence get_range(const size_type index, const size_type count) const
    {
        const auto available = size();
        if (index > available || count > available - index) {
            throw std::out_of_range("range extends past the end of the contextual rank sequence");
        }

        if (count == 0) {
            return {};
        }

        if (count == available) {
            return *this;
        }

        return split_at(index).right.split_at(count).left;
    }

    /// Copies the elements out into a vector, in the sequence's own order. O(n).
    [[nodiscard]] std::vector<value_type> to_vector() const
    {
        return tree_.to_vector();
    }

    /// An iterator over the elements, in the sequence's own order. It retains the immutable root,
    /// so it stays valid across later edits and outlives the facade that produced it.
    [[nodiscard]] const_iterator begin() const
    {
        return tree_.begin();
    }

    /// The iterator one past the last element.
    [[nodiscard]] const_iterator end() const noexcept
    {
        return tree_.end();
    }

    /// A const iterator over the elements.
    [[nodiscard]] const_iterator cbegin() const
    {
        return begin();
    }

    /// The const iterator one past the last element.
    [[nodiscard]] const_iterator cend() const noexcept
    {
        return end();
    }

    /// Whether both handles name the same root. A representation check used to confirm that a
    /// no-op shared rather than rebuilt, not an equality test. O(1).
    [[nodiscard]] bool shares_root_with(const contextual_rank_sequence& other) const noexcept
    {
        return tree_.shares_root_with(other.tree_);
    }

    /// Recomputes the cached root summary by direct machine scan and compares it with the tree.
    ///
    /// A defensive audit; ordinary operations maintain these invariants. O(s n). Reports nothing
    /// when the cached element count, outgoing state, or event count disagrees with the scan. A
    /// machine-contract violation or an event-count overflow found during the scan propagates as
    /// the corresponding exception rather than as an absent result.
    [[nodiscard]] std::optional<contextual_rank_validation_statistics> validate_structure() const
    {
        const auto summary = tree_.measure();
        auto count = size_type{0};
        for (auto iterator = tree_.begin(); iterator != tree_.end(); ++iterator) {
            count = checked_add(count, size_type{1});
        }

        if (count != summary.count()) {
            return std::nullopt;
        }

        auto maximum_total_event_count = std::int64_t{0};
        for (auto initial_state = size_type{0}; initial_state != state_count; ++initial_state) {
            auto state = initial_state;
            auto events = std::int64_t{0};
            for (const auto& element : tree_) {
                const auto transition = measure_policy_type::checked_transition(state, element);
                events = detail::checked_event_add(events, transition.event_count);
                state = transition.next_state;
            }

            if (state != summary.final_state(initial_state)
                || events != summary.event_count(initial_state)) {
                return std::nullopt;
            }

            maximum_total_event_count = (std::max)(maximum_total_event_count, events);
        }

        return contextual_rank_validation_statistics{count, state_count, maximum_total_event_count};
    }

private:
    /// Adopts a tree, forcing its root summary before the successor becomes reachable.
    ///
    /// The force is what makes the failure contract hold: the substrate builds a deep node without
    /// touching the measure, so an invalid transition or an event-count overflow introduced by this
    /// operation would otherwise surface at some later read of an already-published value. Running
    /// it here matches the C# reference, and it also memoizes the summary the very next `size()`
    /// reads.
    explicit contextual_rank_sequence(tree_type tree)
        : tree_(std::move(tree))
    {
        (void)tree_.measure();
    }

    static void throw_if_state_out_of_range(const size_type state)
    {
        if (state >= state_count) {
            throw std::out_of_range("state is outside the contextual machine state range");
        }
    }

    tree_type tree_;
};

} // namespace durable7::finger_tree
