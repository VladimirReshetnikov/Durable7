/// A persistent Brodal-Okasaki heap carrying lazily composable monotone priority actions.
///
/// This is the action-tagged sibling of `brodal_okasaki_heap`. It keeps the same bootstrapped
/// skew-binomial representation - a global minimum root above a skew-binomial forest - and adds one
/// immutable pending action to every tree and every forest spine cell. A tree tag applies to that
/// tree and all of its descendants; a forest tag applies uniformly to every tree in that suffix.
///
/// The point of the tags is `transform_all`, which applies one monotone action to every priority
/// *currently* in a version by composing a single tag at the root. Its semantics are deliberately
/// temporal: entries inserted later, and entries arriving through a later meld, are not
/// retroactively transformed. Two independently transformed heaps still meld in worst-case constant
/// time, including when the action has no inverse.
///
/// Every operation returns a new version and leaves its inputs valid, sharing unchanged structure,
/// so an edit copies a path rather than the whole collection.

#pragma once

#include <durable7/finger_tree/detail/common.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace durable7::finger_tree {

/// A constant-time-composable monotone action family together with the order it is monotone for.
///
/// `compose(outer, inner)` denotes `outer(inner(priority))`, so the *newer* action is the outer
/// one. Every action must be monotone for `less`: if `p <= q` then `apply(a, p) <= apply(a, q)`.
/// Identity recognition, composition, application, and comparison are part of the heap's advertised
/// bounds and must each take O(1) time over fixed-size actions. Results must remain deterministic
/// and semantically stable for the lifetime of every heap version; a policy whose composition is
/// not associative, or whose actions grow without bound, invalidates both the semantic and the
/// complexity guarantees.
///
/// Because the policy is a type rather than a retained object, two heaps can only meet in one
/// operation when they were built from the same policy - the managed ports' policy-identity gate on
/// melding becomes a compile-time property here.
template <class Policy, class Priority>
concept monotone_heap_action_policy = requires(
    const typename Policy::action_type& action,
    const typename Policy::action_type& outer,
    const typename Policy::action_type& inner,
    const Priority& left,
    const Priority& right) {
       typename Policy::action_type;
       { Policy::identity_action() } -> std::same_as<typename Policy::action_type>;
       { Policy::is_identity(action) } -> std::convertible_to<bool>;
       { Policy::compose(outer, inner) } -> std::same_as<typename Policy::action_type>;
       { Policy::apply(action, left) } -> std::same_as<Priority>;
       { Policy::less(left, right) } -> std::convertible_to<bool>;
   };

/// The storage requirements the action heap adds on top of the action policy itself.
template <class Element, class Priority, class Policy>
concept monotone_action_heap_types =
    monotone_heap_action_policy<Policy, Priority>
    && std::copyable<typename Policy::action_type>
    && std::move_constructible<Priority>;

template <class T, class Less = std::less<T>>
struct order_clamp_policy;

/// A lower bound, an upper bound, both bounds, an exact constant, or the identity.
///
/// Values are created by `order_clamp_policy` so that two-bound actions are validated with the same
/// comparison used for application and composition. A missing bound denotes negative or positive
/// infinity. The explicit constant case preserves exact results even when the comparison treats
/// distinct priority values as equal: an inclusive `[k, k]` clamp preserves an input equivalent to
/// `k`, whereas the constant function must return the exact stored `k` representative.
template <class T>
class order_clamp final {
public:
    using value_type = T;

    /// The identity action, which leaves every priority alone.
    order_clamp() = default;

    /// Whether this action is an exact constant function.
    [[nodiscard]] bool is_constant() const noexcept { return constant_.has_value(); }

    /// The constant result, or nothing when the action is not constant.
    [[nodiscard]] const std::optional<T>& constant_value() const noexcept { return constant_; }

    /// Whether this action has a lower bound.
    [[nodiscard]] bool has_lower_bound() const noexcept { return lower_.has_value(); }

    /// The lower bound, or nothing when the action has none.
    [[nodiscard]] const std::optional<T>& lower_bound() const noexcept { return lower_; }

    /// Whether this action has an upper bound.
    [[nodiscard]] bool has_upper_bound() const noexcept { return upper_.has_value(); }

    /// The upper bound, or nothing when the action has none.
    [[nodiscard]] const std::optional<T>& upper_bound() const noexcept { return upper_; }

    friend bool operator==(const order_clamp&, const order_clamp&) = default;

private:
    template <class, class>
    friend struct order_clamp_policy;

    order_clamp(std::optional<T> constant_part, std::optional<T> lower_part, std::optional<T> upper_part)
        : constant_(std::move(constant_part))
        , lower_(std::move(lower_part))
        , upper_(std::move(upper_part))
    {
    }

    std::optional<T> constant_;
    std::optional<T> lower_;
    std::optional<T> upper_;
};

/// The closed monotone action family `x -> clamp(x, lower, upper)`, plus an exact constant.
///
/// Composition always yields either one constant-sized clamp or an explicit constant function, so
/// `compose` and `apply` are O(1). Disjoint bounds collapse to a constant carrying the *outer*
/// (newer) boundary; overlapping bounds intersect, keeping the *inner* (older) representative when
/// two boundaries compare equal. Application order is constant, then lower bound, then upper bound.
///
/// `Less` is a compile-time comparison policy rather than a retained comparer object, which is what
/// ties an action family and its order together into a single type.
template <class T, class Less>
struct order_clamp_policy final {
    static_assert(
        std::default_initializable<Less>,
        "an order-clamp comparison policy must be default constructible");
    static_assert(
        std::predicate<const Less&, const T&, const T&>,
        "an order-clamp comparison policy must be a strict ordering over the priority type");

    using priority_type = T;
    using action_type = order_clamp<T>;
    using comparer_type = Less;

    /// Whether the left priority orders strictly before the right one.
    [[nodiscard]] static bool less(const T& left, const T& right) { return Less{}(left, right); }

    /// The semantic identity action.
    [[nodiscard]] static action_type identity_action() { return action_type{}; }

    /// Whether an action is the semantic identity.
    [[nodiscard]] static bool is_identity(const action_type& action) noexcept
    {
        return !action.is_constant() && !action.has_lower_bound() && !action.has_upper_bound();
    }

    /// A floor action.
    [[nodiscard]] static action_type at_least(T lower_bound)
    {
        return action_type{std::nullopt, std::move(lower_bound), std::nullopt};
    }

    /// A cap action.
    [[nodiscard]] static action_type at_most(T upper_bound)
    {
        return action_type{std::nullopt, std::nullopt, std::move(upper_bound)};
    }

    /// An exact constant action.
    [[nodiscard]] static action_type constant(T value)
    {
        return action_type{std::move(value), std::nullopt, std::nullopt};
    }

    /// A two-sided clamp action, rejecting a lower bound that compares greater than the upper one.
    [[nodiscard]] static action_type between(T lower_bound, T upper_bound)
    {
        if (less(upper_bound, lower_bound)) {
            throw std::invalid_argument(
                "an order-clamp lower bound must not compare greater than its upper bound");
        }
        return action_type{std::nullopt, std::move(lower_bound), std::move(upper_bound)};
    }

    /// The action `outer(inner(priority))`, in O(1) time and space.
    [[nodiscard]] static action_type compose(const action_type& outer, const action_type& inner)
    {
        if (is_identity(outer)) {
            return inner;
        }
        if (is_identity(inner)) {
            return outer;
        }
        if (outer.is_constant()) {
            return outer;
        }
        if (inner.is_constant()) {
            return constant(apply(outer, *inner.constant_value()));
        }

        // Disjoint bounds collapse to an explicit constant carrying the newer boundary.
        if (inner.has_upper_bound() && outer.has_lower_bound()
            && less(*inner.upper_bound(), *outer.lower_bound())) {
            return constant(*outer.lower_bound());
        }
        if (inner.has_lower_bound() && outer.has_upper_bound()
            && less(*outer.upper_bound(), *inner.lower_bound())) {
            return constant(*outer.upper_bound());
        }

        // Overlapping bounds intersect; equal boundaries keep the older (inner) representative.
        auto lower = std::optional<T>{};
        if (!inner.has_lower_bound()) {
            lower = outer.lower_bound();
        } else if (!outer.has_lower_bound() || !less(*inner.lower_bound(), *outer.lower_bound())) {
            lower = inner.lower_bound();
        } else {
            lower = outer.lower_bound();
        }
        auto upper = std::optional<T>{};
        if (!inner.has_upper_bound()) {
            upper = outer.upper_bound();
        } else if (!outer.has_upper_bound() || !less(*outer.upper_bound(), *inner.upper_bound())) {
            upper = inner.upper_bound();
        } else {
            upper = outer.upper_bound();
        }
        return action_type{std::nullopt, std::move(lower), std::move(upper)};
    }

    /// The transformed priority, in O(1) time.
    [[nodiscard]] static T apply(const action_type& action, const T& priority)
    {
        if (action.is_constant()) {
            return *action.constant_value();
        }
        if (action.has_lower_bound() && less(priority, *action.lower_bound())) {
            return *action.lower_bound();
        }
        if (action.has_upper_bound() && less(*action.upper_bound(), priority)) {
            return *action.upper_bound();
        }
        return priority;
    }
};

/// A payload paired with its current logical priority.
///
/// Both halves are owned shared handles, so an entry stays usable independently of the heap version
/// it came from and neither the payload nor the priority has to be copyable.
template <class Element, class Priority>
struct monotone_heap_entry final {
    std::shared_ptr<const Element> element;
    std::shared_ptr<const Priority> priority;
};

/// Shape and pending-tag measurements from a structural audit.
struct persistent_monotone_action_heap_statistics final {
    std::size_t count = 0;
    std::size_t root_forest_length = 0;
    std::size_t maximum_rank = 0;
    std::size_t maximum_depth = 0;
    /// Pending tree/forest-tag observations made during the audit. This is not a distinct-object
    /// count: exposing one uniform forest tag produces several tagged logical views of the same
    /// retained cells.
    std::size_t tagged_component_count = 0;

    friend bool operator==(
        const persistent_monotone_action_heap_statistics&,
        const persistent_monotone_action_heap_statistics&) = default;
};

/// An immutable bootstrapped skew-binomial min-heap with pending monotone priority actions.
///
/// Insert, minimum, and meld are worst-case O(1); delete-minimum is worst-case O(log n);
/// `transform_all` is worst-case O(1) time and allocates O(1) new structure; iteration and
/// `validate_structure` are Theta(n); forking by retaining a version is O(1). These bounds include
/// the O(1)-time policy calls and priority comparisons they perform.
///
/// Payloads and priorities are retained through `std::shared_ptr`, so no operation requires either
/// to be copyable. The heap itself is an ordinary copyable value whose copy is cheap because it
/// shares immutable nodes.
template <class Element, class Priority, class Policy>
    requires monotone_action_heap_types<Element, Priority, Policy>
class persistent_monotone_action_heap final {
private:
    struct reclaimable {
        reclaimable* deferred_next = nullptr;
        /// Destroys the node through the deferred reclamation queue.
        virtual ~reclaimable() = default;
    };

    struct tree;
    struct forest;
    using tree_pointer = std::shared_ptr<const tree>;
    using forest_pointer = std::shared_ptr<const forest>;

public:
    using element_type = Element;
    using priority_type = Priority;
    using policy_type = Policy;
    using action_type = typename Policy::action_type;
    using size_type = std::size_t;
    using element_handle = std::shared_ptr<const Element>;
    using priority_handle = std::shared_ptr<const Priority>;
    using entry_type = monotone_heap_entry<Element, Priority>;
    using value_type = entry_type;
    using statistics_type = persistent_monotone_action_heap_statistics;
    using delete_minimum_result = std::pair<entry_type, persistent_monotone_action_heap>;

    /// A copyable input cursor over one heap version's entries, in unspecified structural order.
    ///
    /// Dereference returns a value rather than a reference: a logical priority may only exist once
    /// a pending action has been applied, so a reference into cursor-owned storage would not have a
    /// forward iterator's lifetime. The cursor owns the version it walks, so it stays valid after
    /// the heap value that produced it is destroyed.
    class const_iterator final {
    public:
        using iterator_category = std::input_iterator_tag;
        using iterator_concept = std::input_iterator_tag;
        using value_type = entry_type;
        using difference_type = std::ptrdiff_t;
        using pointer = const entry_type*;
        using reference = entry_type;

        /// An exhausted cursor, holding no version.
        const_iterator() = default;

        [[nodiscard]] entry_type operator*() const { return current_; }

        const_iterator& operator++()
        {
            advance();
            return *this;
        }

        void operator++(int) { advance(); }

        friend bool operator==(const const_iterator& left, const const_iterator& right) noexcept
        {
            if (left.exhausted() || right.exhausted()) {
                return left.exhausted() && right.exhausted();
            }
            return left.owner_ == right.owner_ && left.position_ == right.position_;
        }

    private:
        friend class persistent_monotone_action_heap;

        explicit const_iterator(tree_pointer root)
            : owner_(root)
        {
            if (root != nullptr) {
                pending_.push_back(std::move(root));
                advance();
            }
        }

        [[nodiscard]] bool exhausted() const noexcept { return current_.element == nullptr; }

        void advance()
        {
            if (pending_.empty()) {
                current_ = entry_type{};
                return;
            }
            auto tagged = pending_.back();
            pending_.pop_back();
            const auto node = expose(tagged);
            for (auto link = node->children; link != nullptr; link = forest_tail(link)) {
                pending_.push_back(forest_head(link));
            }
            current_ = entry_type{node->element, node->priority};
            ++position_;
        }

        tree_pointer owner_;
        std::vector<tree_pointer> pending_;
        entry_type current_;
        size_type position_ = 0;
    };

    /// An empty heap over the compile-time action policy.
    persistent_monotone_action_heap() = default;

    template <std::ranges::input_range Range>
        requires requires(std::ranges::range_reference_t<Range> entry) {
            { std::get<0>(entry) } -> std::convertible_to<const Element&>;
            { std::get<1>(entry) } -> std::convertible_to<const Priority&>;
        }
    /// A heap holding a range's (element, priority) pairs, built by repeated worst-case-O(1)
    /// insertion in Theta(n) time.
    [[nodiscard]] static persistent_monotone_action_heap from_range(Range&& entries)
    {
        return persistent_monotone_action_heap{}.insert_range(std::forward<Range>(entries));
    }

    template <std::ranges::input_range Range>
        requires requires(std::ranges::range_reference_t<Range> entry) {
            { std::get<0>(entry) } -> std::convertible_to<const Element&>;
            { std::get<1>(entry) } -> std::convertible_to<const Priority&>;
        }
    /// A heap with a range's (element, priority) pairs inserted, in Theta(n) time.
    [[nodiscard]] persistent_monotone_action_heap insert_range(Range&& entries) const
    {
        auto result = *this;
        for (auto&& entry : entries) {
            result = result.insert(std::get<0>(entry), std::get<1>(entry));
        }
        return result;
    }

    /// Number of entries in the heap.
    [[nodiscard]] size_type size() const noexcept { return count_; }

    /// Whether the heap holds no entries.
    [[nodiscard]] bool empty() const noexcept { return root_ == nullptr; }

    /// The root node's address, for tests that a no-op update shared rather than rebuilt.
    [[nodiscard]] const void* root_identity() const noexcept { return root_.get(); }

    /// Whether both handles retain exactly the same root node.
    [[nodiscard]] bool shares_root_with(const persistent_monotone_action_heap& other) const noexcept
    {
        return root_ == other.root_;
    }

    /// Whether both roots retain exactly the same child forest. A whole-heap transform composes one
    /// tag at the root, so the successor must reuse the receiver's entire child forest; this is the
    /// structural half of the O(1)-allocation claim.
    [[nodiscard]] bool shares_children_with(const persistent_monotone_action_heap& other) const noexcept
    {
        if (root_ == nullptr || other.root_ == nullptr) {
            return root_ == other.root_;
        }
        return root_->children == other.root_->children;
    }

    /// Whether both handles denote the same version.
    [[nodiscard]] bool is_same_version(const persistent_monotone_action_heap& other) const noexcept
    {
        return root_ == other.root_ && count_ == other.count_;
    }

    /// A minimum entry, in worst-case O(1) time.
    [[nodiscard]] entry_type minimum() const
    {
        if (root_ == nullptr) {
            throw std::logic_error("monotone-action heap is empty");
        }
        return entry_of(*root_);
    }

    /// A minimum entry, or nothing when the heap is empty, in worst-case O(1) time.
    [[nodiscard]] std::optional<entry_type> try_minimum() const
    {
        if (root_ == nullptr) {
            return std::nullopt;
        }
        return entry_of(*root_);
    }

    template <class ElementArgument, class PriorityArgument>
        requires std::constructible_from<Element, ElementArgument&&>
            && std::constructible_from<Priority, PriorityArgument&&>
    /// A heap with the entry inserted, in worst-case O(1) time.
    ///
    /// The new entry joins untransformed: earlier `transform_all` actions never reach it.
    [[nodiscard]] persistent_monotone_action_heap insert(
        ElementArgument&& element,
        PriorityArgument&& priority) const
    {
        return insert_handles(
            std::make_shared<const Element>(std::forward<ElementArgument>(element)),
            std::make_shared<const Priority>(std::forward<PriorityArgument>(priority)));
    }

    /// A heap with the already-shared entry inserted, in worst-case O(1) time.
    [[nodiscard]] persistent_monotone_action_heap insert_handles(
        element_handle element,
        priority_handle priority) const
    {
        if (element == nullptr || priority == nullptr) {
            throw std::invalid_argument("monotone-action heap entry handles cannot be null");
        }
        auto singleton = make_tree(
            0,
            std::move(element),
            std::move(priority),
            nullptr,
            Policy::identity_action());
        if (root_ == nullptr) {
            return persistent_monotone_action_heap{std::move(singleton), 1};
        }

        const auto next_count = checked_add(count_, size_type{1});
        // A tie favors the new entry, so it becomes the root and the old root descends untouched
        // through an identity-tagged spine cell.
        if (less_or_equal(*singleton, *root_)) {
            return persistent_monotone_action_heap{
                make_tree(
                    0,
                    singleton->element,
                    singleton->priority,
                    cons(root_, nullptr),
                    Policy::identity_action()),
                next_count};
        }
        // Exposing the old root first is what stops its pending action from leaking onto a child
        // that did not exist when the action was applied.
        const auto exposed = expose(root_);
        return persistent_monotone_action_heap{
            make_tree(
                0,
                exposed->element,
                exposed->priority,
                skew_insert(std::move(singleton), exposed->children),
                Policy::identity_action()),
            next_count};
    }

    /// A heap in which the action has been applied to every priority present in this version, in
    /// worst-case O(1) time and O(1) new structure.
    ///
    /// Later insertions and later melds are not retroactively transformed. An empty heap or a
    /// recognized identity action returns a version sharing this one's root.
    [[nodiscard]] persistent_monotone_action_heap transform_all(const action_type& action) const
    {
        if (root_ == nullptr || Policy::is_identity(action)) {
            return *this;
        }
        return persistent_monotone_action_heap{tag_tree(root_, action), count_};
    }

    /// The heap holding both operands' entries, in worst-case O(1) time.
    ///
    /// Each operand keeps its own action history: neither heap's pending actions are applied to the
    /// other's entries, even for non-invertible actions. Melding a heap with itself duplicates
    /// every logical occurrence over a shared DAG.
    [[nodiscard]] persistent_monotone_action_heap meld(const persistent_monotone_action_heap& other) const
    {
        if (root_ == nullptr) {
            return other;
        }
        if (other.root_ == nullptr) {
            return *this;
        }

        const auto total = checked_add(count_, other.count_);
        const auto left_wins = less_or_equal(*root_, *other.root_);
        const auto& winner = left_wins ? root_ : other.root_;
        const auto& loser = left_wins ? other.root_ : root_;
        const auto exposed = expose(winner);
        return persistent_monotone_action_heap{
            make_tree(
                0,
                exposed->element,
                exposed->priority,
                skew_insert(loser, exposed->children),
                Policy::identity_action()),
            total};
    }

    /// A heap with one minimum entry removed, in worst-case O(log n) time.
    [[nodiscard]] persistent_monotone_action_heap delete_minimum() const
    {
        if (root_ == nullptr) {
            throw std::logic_error("monotone-action heap is empty");
        }
        const auto root = expose(root_);
        if (root->children == nullptr) {
            return persistent_monotone_action_heap{};
        }

        const auto minimum_split = get_minimum(root->children);
        const auto minimum = expose(minimum_split.first);
        auto decomposition = split_forest(minimum->rank, minimum->children);
        auto merged = skew_meld(
            skew_meld(decomposition.trees, minimum_split.second),
            decomposition.embedded);
        const auto zeros = forest_trees(decomposition.zeros);
        for (auto iterator = zeros.rbegin(); iterator != zeros.rend(); ++iterator) {
            merged = skew_insert(*iterator, std::move(merged));
        }
        return persistent_monotone_action_heap{
            make_tree(
                0,
                minimum->element,
                minimum->priority,
                std::move(merged),
                Policy::identity_action()),
            count_ - 1};
    }

    /// The removed minimum entry together with the persistent remainder, or nothing when empty.
    [[nodiscard]] std::optional<delete_minimum_result> try_delete_minimum() const
    {
        if (root_ == nullptr) {
            return std::nullopt;
        }
        return delete_minimum_result{entry_of(*root_), delete_minimum()};
    }

    /// An empty heap; returns the receiver when it is already empty.
    [[nodiscard]] persistent_monotone_action_heap clear() const
    {
        return root_ == nullptr ? *this : persistent_monotone_action_heap{};
    }

    /// The addresses of the distinct physical tree nodes this version retains.
    ///
    /// These are physical identities: one retained node observed under two different pending tags
    /// is one entry here even though it yields two distinct logical trees.
    [[nodiscard]] std::vector<const void*> node_identities() const
    {
        auto result = std::vector<const void*>{};
        if (root_ == nullptr) {
            return result;
        }
        auto seen = std::unordered_set<const void*>{};
        auto pending = std::vector<const tree*>{root_.get()};
        while (!pending.empty()) {
            const auto* current = pending.back();
            pending.pop_back();
            if (!seen.insert(current).second) {
                continue;
            }
            result.push_back(current);
            for (const auto* link = current->children.get(); link != nullptr; link = link->raw_tail.get()) {
                pending.push_back(link->raw_head.get());
            }
        }
        return result;
    }

    /// How many physical tree nodes both versions retain.
    [[nodiscard]] size_type shared_node_count_with(const persistent_monotone_action_heap& other) const
    {
        const auto identities = node_identities();
        const auto known = std::unordered_set<const void*>{identities.begin(), identities.end()};
        auto shared = size_type{0};
        for (const auto identity : other.node_identities()) {
            shared += known.contains(identity) ? 1 : 0;
        }
        return shared;
    }

    /// Audits rank encodings, the fused-children walk, skew-forest rank rules, logical heap order
    /// through every composed tag, and the count, in Theta(n) time.
    [[nodiscard]] statistics_type validate_structure() const
    {
        if (root_ == nullptr) {
            if (count_ != 0) {
                throw std::logic_error("empty monotone-action heap has a nonzero count");
            }
            return {};
        }
        if (root_->rank != 0) {
            throw std::logic_error("monotone-action heap global root must have rank zero");
        }

        auto tagged_component_count = size_type{0};
        const auto normalized_root = expose(root_);
        const auto root_forest_length = validate_skew_forest(normalized_root->children);

        struct frame final {
            tree_pointer current;
            size_type depth;
        };
        auto pending = std::vector<frame>{};
        pending.push_back(frame{root_, 1});
        auto logical_count = size_type{0};
        auto maximum_rank = size_type{0};
        auto maximum_depth = size_type{0};
        while (!pending.empty()) {
            const auto current_frame = pending.back();
            pending.pop_back();
            if (!Policy::is_identity(current_frame.current->action)) {
                ++tagged_component_count;
            }
            const auto node = expose(current_frame.current);
            logical_count = checked_add(logical_count, size_type{1});
            maximum_rank = (std::max)(maximum_rank, node->rank);
            maximum_depth = (std::max)(maximum_depth, current_frame.depth);
            (void)validate_skew_forest(validate_fused_children(*node));

            for (auto link = node->children; link != nullptr; link = forest_tail(link)) {
                if (!Policy::is_identity(link->action)) {
                    ++tagged_component_count;
                }
                auto child = forest_head(link);
                if (!less_or_equal(*node, *child)) {
                    throw std::logic_error("monotone-action heap child outranks its parent");
                }
                pending.push_back(frame{std::move(child), checked_add(current_frame.depth, size_type{1})});
            }
        }
        if (logical_count != count_) {
            throw std::logic_error("monotone-action heap count disagrees with its tree graph");
        }
        return statistics_type{
            count_,
            root_forest_length,
            maximum_rank,
            maximum_depth,
            tagged_component_count};
    }

    [[nodiscard]] const_iterator begin() const { return const_iterator{root_}; }
    [[nodiscard]] const_iterator end() const noexcept { return {}; }
    [[nodiscard]] const_iterator cbegin() const { return begin(); }
    [[nodiscard]] const_iterator cend() const noexcept { return end(); }

private:
    struct tree final : reclaimable {
        /// One skew-binomial tree carrying a pending action for itself and all of its descendants.
        tree(
            const size_type tree_rank,
            element_handle tree_element,
            priority_handle tree_priority,
            forest_pointer tree_children,
            action_type tree_action)
            : rank(tree_rank)
            , element(std::move(tree_element))
            , priority(std::move(tree_priority))
            , children(std::move(tree_children))
            , action(std::move(tree_action))
        {
        }

        size_type rank;
        element_handle element;
        /// Raw priority; the logical priority is `apply(action, priority)`.
        priority_handle priority;
        /// Raw children; the logical children are `tag_forest(children, action)`.
        forest_pointer children;
        action_type action;
    };

    struct forest final : reclaimable {
        /// One forest spine cell carrying a pending action for its whole suffix.
        forest(tree_pointer head, forest_pointer tail, action_type forest_action)
            : raw_head(std::move(head))
            , raw_tail(std::move(tail))
            , action(std::move(forest_action))
        {
        }

        /// Raw head; the logical head is `tag_tree(raw_head, action)`.
        tree_pointer raw_head;
        /// Raw tail; the logical tail is `tag_forest(raw_tail, action)`.
        forest_pointer raw_tail;
        action_type action;
    };

    struct deferred_deleter final {
        template <class Node>
        void operator()(const Node* value) const noexcept
        {
            reclaim(const_cast<Node*>(value));
        }
    };

    struct forest_decomposition final {
        forest_pointer zeros;
        forest_pointer trees;
        forest_pointer embedded;
    };

    persistent_monotone_action_heap(tree_pointer root, const size_type count)
        : root_(std::move(root))
        , count_(count)
    {
    }

    static void reclaim(reclaimable* value) noexcept
    {
        struct reclamation_state final {
            reclaimable* pending = nullptr;
            bool draining = false;
        };
        static thread_local auto state = reclamation_state{};

        value->deferred_next = state.pending;
        state.pending = value;
        if (state.draining) {
            return;
        }

        state.draining = true;
        while (state.pending != nullptr) {
            auto* current = state.pending;
            state.pending = current->deferred_next;
            delete current;
        }
        state.draining = false;
    }

    [[nodiscard]] static tree_pointer make_tree(
        const size_type rank,
        element_handle element,
        priority_handle priority,
        forest_pointer children,
        action_type action)
    {
        return tree_pointer{
            new tree{
                rank,
                std::move(element),
                std::move(priority),
                std::move(children),
                std::move(action)},
            deferred_deleter{}};
    }

    [[nodiscard]] static forest_pointer make_forest(
        tree_pointer head,
        forest_pointer tail,
        action_type action)
    {
        return forest_pointer{
            new forest{std::move(head), std::move(tail), std::move(action)},
            deferred_deleter{}};
    }

    // -----------------------------------------------------------------------------------------
    // Tag algebra. `compose(outer, inner)` is `outer(inner(priority))`, so at every push-down site
    // the newer action is the outer operand.
    // -----------------------------------------------------------------------------------------

    /// Pushes a newer action onto one tree, composing it with the tree's pending action.
    [[nodiscard]] static tree_pointer tag_tree(const tree_pointer& node, const action_type& outer)
    {
        if (Policy::is_identity(outer)) {
            return node;
        }
        return make_tree(
            node->rank,
            node->element,
            node->priority,
            node->children,
            Policy::compose(outer, node->action));
    }

    /// Pushes a newer action onto one forest suffix, composing it with the suffix's action.
    [[nodiscard]] static forest_pointer tag_forest(const forest_pointer& link, const action_type& outer)
    {
        if (Policy::is_identity(outer)) {
            return link;
        }
        return make_forest(link->raw_head, link->raw_tail, Policy::compose(outer, link->action));
    }

    /// Materializes one tree's pending action into its own priority and its child forest.
    [[nodiscard]] static tree_pointer expose(const tree_pointer& node)
    {
        if (Policy::is_identity(node->action)) {
            return node;
        }
        return make_tree(
            node->rank,
            node->element,
            logical_priority(*node),
            node->children == nullptr ? forest_pointer{} : tag_forest(node->children, node->action),
            Policy::identity_action());
    }

    /// Builds an identity-tagged spine cell, so an attached tree keeps only its own history.
    [[nodiscard]] static forest_pointer cons(tree_pointer head, forest_pointer tail)
    {
        return make_forest(std::move(head), std::move(tail), Policy::identity_action());
    }

    /// The logical head of a spine cell.
    [[nodiscard]] static tree_pointer forest_head(const forest_pointer& link)
    {
        return tag_tree(link->raw_head, link->action);
    }

    /// The logical tail of a spine cell, retaining its uniform action.
    [[nodiscard]] static forest_pointer forest_tail(const forest_pointer& link)
    {
        return link->raw_tail == nullptr ? forest_pointer{} : tag_forest(link->raw_tail, link->action);
    }

    /// The logical priority of one tree, reusing the retained handle when nothing is pending.
    [[nodiscard]] static priority_handle logical_priority(const tree& node)
    {
        if (Policy::is_identity(node.action)) {
            return node.priority;
        }
        return std::make_shared<const Priority>(Policy::apply(node.action, *node.priority));
    }

    /// The logical priority of one tree without publishing a handle for it.
    [[nodiscard]] static const Priority& observe_priority(
        const tree& node,
        std::optional<Priority>& storage)
    {
        if (Policy::is_identity(node.action)) {
            return *node.priority;
        }
        storage.emplace(Policy::apply(node.action, *node.priority));
        return *storage;
    }

    [[nodiscard]] static entry_type entry_of(const tree& node)
    {
        return entry_type{node.element, logical_priority(node)};
    }

    [[nodiscard]] static bool less_or_equal(const tree& left, const tree& right)
    {
        auto left_storage = std::optional<Priority>{};
        auto right_storage = std::optional<Priority>{};
        const auto& left_priority = observe_priority(left, left_storage);
        const auto& right_priority = observe_priority(right, right_storage);
        return !Policy::less(right_priority, left_priority);
    }

    // -----------------------------------------------------------------------------------------
    // Skew-binomial kernel over logical (tag-composed) views.
    // -----------------------------------------------------------------------------------------

    [[nodiscard]] static forest_pointer skew_insert(tree_pointer node, forest_pointer values)
    {
        // Ranks are tag invariant, so the raw tail's rank decides the link before any tagged tail
        // cell is materialized. Only the linking branch pays for the tagged tail.
        if (values != nullptr && values->raw_tail != nullptr
            && values->raw_head->rank == values->raw_tail->raw_head->rank) {
            const auto tail = tag_forest(values->raw_tail, values->action);
            return cons(
                skew_link(std::move(node), forest_head(values), forest_head(tail)),
                forest_tail(tail));
        }
        return cons(std::move(node), std::move(values));
    }

    [[nodiscard]] static tree_pointer skew_link(
        tree_pointer zero,
        tree_pointer first,
        tree_pointer second)
    {
        if (first->rank != second->rank) {
            throw std::logic_error("monotone-action heap skew-link ranks differ");
        }
        // Only the logical winner is exposed; the losing trees attach through identity-tagged
        // spine cells so they keep their own action histories.
        const auto rank = checked_add(first->rank, size_type{1});
        if (less_or_equal(*first, *zero) && less_or_equal(*first, *second)) {
            const auto winner = expose(first);
            auto children = cons(std::move(zero), cons(std::move(second), winner->children));
            return make_tree(
                rank,
                winner->element,
                winner->priority,
                std::move(children),
                Policy::identity_action());
        }
        if (less_or_equal(*second, *zero) && less_or_equal(*second, *first)) {
            const auto winner = expose(second);
            auto children = cons(std::move(zero), cons(std::move(first), winner->children));
            return make_tree(
                rank,
                winner->element,
                winner->priority,
                std::move(children),
                Policy::identity_action());
        }
        const auto winner = expose(zero);
        auto children = cons(std::move(first), cons(std::move(second), winner->children));
        return make_tree(
            rank,
            winner->element,
            winner->priority,
            std::move(children),
            Policy::identity_action());
    }

    [[nodiscard]] static tree_pointer link(tree_pointer first, tree_pointer second)
    {
        if (first->rank != second->rank) {
            throw std::logic_error("monotone-action heap link ranks differ");
        }
        const auto first_wins = less_or_equal(*first, *second);
        const auto winner = expose(first_wins ? first : second);
        auto loser = first_wins ? std::move(second) : std::move(first);
        const auto rank = checked_add(winner->rank, size_type{1});
        auto children = cons(std::move(loser), winner->children);
        return make_tree(
            rank,
            winner->element,
            winner->priority,
            std::move(children),
            Policy::identity_action());
    }

    /// The minimum logical tree of a forest paired with the forest that remains. Ties favor the
    /// earliest spine occurrence.
    [[nodiscard]] static std::pair<tree_pointer, forest_pointer> get_minimum(
        const forest_pointer& values)
    {
        auto links = std::vector<forest_pointer>{};
        for (auto current = values; current != nullptr; current = forest_tail(current)) {
            links.push_back(current);
        }
        auto heads = std::vector<tree_pointer>{};
        heads.reserve(links.size());
        for (const auto& link : links) {
            heads.push_back(forest_head(link));
        }

        auto minimum_index = size_type{0};
        for (auto index = size_type{1}; index < heads.size(); ++index) {
            if (!less_or_equal(*heads[minimum_index], *heads[index])) {
                minimum_index = index;
            }
        }
        auto remainder = forest_tail(links[minimum_index]);
        for (auto index = minimum_index; index != 0; --index) {
            remainder = cons(heads[index - 1], std::move(remainder));
        }
        return {heads[minimum_index], std::move(remainder)};
    }

    /// Decomposes a rank-`rank` tree's fused children into rank-zero singletons, the ranked trees,
    /// and the embedded skew-binomial forest.
    [[nodiscard]] static forest_decomposition split_forest(size_type rank, forest_pointer values)
    {
        auto zeros = forest_pointer{};
        auto trees = forest_pointer{};
        while (rank != 0) {
            if (values == nullptr) {
                throw std::logic_error("invalid monotone-action skew-binomial forest");
            }
            auto first = forest_head(values);
            auto tail = forest_tail(values);
            if (rank == 1 && tail == nullptr) {
                trees = cons(std::move(first), std::move(trees));
                values = nullptr;
                break;
            }
            if (tail == nullptr) {
                throw std::logic_error("incomplete monotone-action skew-binomial forest");
            }
            auto second = forest_head(tail);
            auto rest = forest_tail(tail);
            if (rank == 1) {
                // The rank-zero ambiguity is resolved in favor of the structural prefix.
                if (second->rank == 0) {
                    zeros = cons(std::move(first), std::move(zeros));
                    trees = cons(std::move(second), std::move(trees));
                    values = std::move(rest);
                } else {
                    trees = cons(std::move(first), std::move(trees));
                    values = cons(std::move(second), std::move(rest));
                }
                break;
            }
            if (first->rank == second->rank) {
                auto tail_trees = cons(std::move(second), std::move(trees));
                trees = cons(std::move(first), std::move(tail_trees));
                values = std::move(rest);
                break;
            }
            if (first->rank == 0) {
                zeros = cons(std::move(first), std::move(zeros));
                trees = cons(std::move(second), std::move(trees));
                values = std::move(rest);
            } else {
                trees = cons(std::move(first), std::move(trees));
                values = cons(std::move(second), std::move(rest));
            }
            --rank;
        }
        return forest_decomposition{std::move(zeros), std::move(trees), std::move(values)};
    }

    [[nodiscard]] static forest_pointer skew_meld(
        const forest_pointer& left,
        const forest_pointer& right)
    {
        return union_unique(uniquify(left), uniquify(right));
    }

    [[nodiscard]] static forest_pointer uniquify(const forest_pointer& values)
    {
        if (values == nullptr) {
            return nullptr;
        }
        auto head = forest_head(values);
        auto tail = uniquify(forest_tail(values));
        return insert_ranked(std::move(head), std::move(tail));
    }

    [[nodiscard]] static forest_pointer insert_ranked(tree_pointer node, forest_pointer values)
    {
        while (true) {
            if (values == nullptr) {
                return cons(std::move(node), nullptr);
            }
            auto head = forest_head(values);
            if (node->rank < head->rank) {
                return cons(std::move(node), std::move(values));
            }
            auto tail = forest_tail(values);
            node = link(std::move(node), std::move(head));
            values = std::move(tail);
        }
    }

    [[nodiscard]] static forest_pointer union_unique(forest_pointer left, forest_pointer right)
    {
        if (left == nullptr) {
            return right;
        }
        if (right == nullptr) {
            return left;
        }
        auto left_head = forest_head(left);
        auto right_head = forest_head(right);
        if (left_head->rank < right_head->rank) {
            auto merged = union_unique(forest_tail(left), std::move(right));
            return cons(std::move(left_head), std::move(merged));
        }
        if (left_head->rank > right_head->rank) {
            auto merged = union_unique(std::move(left), forest_tail(right));
            return cons(std::move(right_head), std::move(merged));
        }
        auto merged = union_unique(forest_tail(left), forest_tail(right));
        return insert_ranked(link(std::move(left_head), std::move(right_head)), std::move(merged));
    }

    /// The logical trees of a forest, in spine order.
    [[nodiscard]] static std::vector<tree_pointer> forest_trees(const forest_pointer& values)
    {
        auto result = std::vector<tree_pointer>{};
        for (auto current = values; current != nullptr; current = forest_tail(current)) {
            result.push_back(forest_head(current));
        }
        return result;
    }

    // -----------------------------------------------------------------------------------------
    // Validation. Rank encodings are tag invariant, so the two rank-only walks read raw spines;
    // every value-observing check goes through the tag-composing accessors.
    // -----------------------------------------------------------------------------------------

    /// Walks one exposed tree's fused primitive-child encoding and returns its embedded forest.
    [[nodiscard]] static forest_pointer validate_fused_children(const tree& node)
    {
        auto rank = node.rank;
        auto values = node.children;
        while (rank != 0) {
            if (values == nullptr) {
                throw std::logic_error("ranked monotone-action tree is missing structural children");
            }
            const auto first_rank = values->raw_head->rank;
            if (rank == 1) {
                if (first_rank != 0) {
                    throw std::logic_error("rank-one monotone-action tree must begin with a rank-zero child");
                }
                if (values->raw_tail == nullptr) {
                    return nullptr;
                }
                return values->raw_tail->raw_head->rank == 0
                    ? values->raw_tail->raw_tail
                    : values->raw_tail;
            }
            if (values->raw_tail == nullptr) {
                throw std::logic_error("ranked monotone-action tree has incomplete child encoding");
            }
            const auto second_rank = values->raw_tail->raw_head->rank;
            if (first_rank == second_rank) {
                if (first_rank != rank - 1) {
                    throw std::logic_error("skew-linked monotone-action tree has invalid child ranks");
                }
                return values->raw_tail->raw_tail;
            }
            if (first_rank == 0) {
                if (second_rank != rank - 1) {
                    throw std::logic_error("skew-linked monotone-action tree has an invalid ranked child");
                }
                values = values->raw_tail->raw_tail;
            } else {
                if (first_rank != rank - 1) {
                    throw std::logic_error("linked monotone-action tree has an invalid ranked child");
                }
                values = values->raw_tail;
            }
            --rank;
        }
        return values;
    }

    /// Checks nondecreasing forest ranks where only the first two may be equal.
    [[nodiscard]] static size_type validate_skew_forest(forest_pointer values)
    {
        auto length = size_type{0};
        auto previous_rank = std::optional<size_type>{};
        auto duplicate = false;
        for (; values != nullptr; values = values->raw_tail) {
            const auto rank = values->raw_head->rank;
            if (previous_rank.has_value() && rank < *previous_rank) {
                throw std::logic_error("monotone-action forest ranks are not nondecreasing");
            }
            if (previous_rank.has_value() && rank == *previous_rank) {
                if (length != 1 || duplicate) {
                    throw std::logic_error("only the first two monotone-action forest ranks may be equal");
                }
                duplicate = true;
            }
            previous_rank = rank;
            ++length;
        }
        return length;
    }

    tree_pointer root_;
    size_type count_ = 0;
};

/// The shipped clamp-family action heap: `x -> clamp(x, lower, upper)` over a compile-time order.
template <class Element, class Priority, class Less = std::less<Priority>>
using order_clamp_action_heap =
    persistent_monotone_action_heap<Element, Priority, order_clamp_policy<Priority, Less>>;

} // namespace durable7::finger_tree
