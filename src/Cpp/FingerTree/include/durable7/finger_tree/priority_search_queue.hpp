#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace durable7::finger_tree {

template <class Key, class Priority, class Value, class KeyLess, class PriorityLess>
class priority_search_queue;

template <class Key, class Priority, class Value, class KeyLess, class PriorityLess>
struct priority_search_add_result;

template <class Key, class Priority, class Value, class KeyLess, class PriorityLess>
struct priority_search_remove_result;

template <class Key, class Priority, class Value, class KeyLess, class PriorityLess>
struct priority_search_minimum_view;

template <class T>
concept priority_search_equality_testable = requires(const T& left, const T& right) {
    { left == right } -> std::convertible_to<bool>;
};

/// One ordered key, its priority, and its payload, retained through immutable component handles.
template <class Key, class Priority, class Value>
class priority_search_entry final {
public:
    using key_type = Key;
    using priority_type = Priority;
    using value_type = Value;
    using key_handle_type = std::shared_ptr<const key_type>;
    using priority_handle_type = std::shared_ptr<const priority_type>;
    using value_handle_type = std::shared_ptr<const value_type>;

    template <class KeyArgument, class PriorityArgument, class ValueArgument>
        requires std::constructible_from<Key, KeyArgument&&>
            && std::constructible_from<Priority, PriorityArgument&&>
            && std::constructible_from<Value, ValueArgument&&>
    explicit priority_search_entry(
        KeyArgument&& key,
        PriorityArgument&& priority,
        ValueArgument&& value)
        : key_(std::make_shared<key_type>(std::forward<KeyArgument>(key)))
        , priority_(std::make_shared<priority_type>(std::forward<PriorityArgument>(priority)))
        , value_(std::make_shared<value_type>(std::forward<ValueArgument>(value)))
    {
    }

    [[nodiscard]] const key_type& key() const noexcept { return *key_; }
    [[nodiscard]] const priority_type& priority() const noexcept { return *priority_; }
    [[nodiscard]] const value_type& value() const noexcept { return *value_; }

    [[nodiscard]] key_handle_type key_handle() const noexcept { return key_; }
    [[nodiscard]] priority_handle_type priority_handle() const noexcept { return priority_; }
    [[nodiscard]] value_handle_type value_handle() const noexcept { return value_; }

    [[nodiscard]] bool is_same_entry(const priority_search_entry& other) const noexcept
    {
        return key_ == other.key_ && priority_ == other.priority_ && value_ == other.value_;
    }

    friend bool operator==(const priority_search_entry& left, const priority_search_entry& right)
        requires priority_search_equality_testable<key_type>
            && priority_search_equality_testable<priority_type>
            && priority_search_equality_testable<value_type>
    {
        return left.key() == right.key()
            && left.priority() == right.priority()
            && left.value() == right.value();
    }

private:
    template <class, class, class, class, class>
    friend class priority_search_queue;

    priority_search_entry(
        key_handle_type key,
        priority_handle_type priority,
        value_handle_type value)
        : key_(std::move(key))
        , priority_(std::move(priority))
        , value_(std::move(value))
    {
    }

    [[nodiscard]] priority_search_entry replacing_from(
        const priority_search_entry& replacement) const
    {
        return priority_search_entry{key_, replacement.priority_, replacement.value_};
    }

    key_handle_type key_;
    priority_handle_type priority_;
    value_handle_type value_;
};

struct priority_search_queue_statistics final {
    std::size_t count = 0;
    std::size_t height = 0;
    std::size_t maximum_absolute_balance_factor = 0;

    friend bool operator==(
        const priority_search_queue_statistics&,
        const priority_search_queue_statistics&) = default;
};

/// An immutable ordered map whose AVL nodes cache the priority-then-key winner of each subtree.
///
/// One entry is retained per key-comparer equivalence class. The first concrete key representative
/// is stable, while priority and payload are last-wins. Priority ties break by retained key order.
template <
    class Key,
    class Priority,
    class Value,
    class KeyLess = std::less<Key>,
    class PriorityLess = std::less<Priority>>
class priority_search_queue final {
private:
    struct node;
    using node_pointer = std::shared_ptr<const node>;

    static_assert(std::copy_constructible<KeyLess>);
    static_assert(std::copy_constructible<PriorityLess>);
    static_assert(std::predicate<const KeyLess&, const Key&, const Key&>);
    static_assert(std::predicate<const PriorityLess&, const Priority&, const Priority&>);

public:
    using key_type = Key;
    using priority_type = Priority;
    using value_type = Value;
    using key_comparer_type = KeyLess;
    using priority_comparer_type = PriorityLess;
    using entry_type = priority_search_entry<key_type, priority_type, value_type>;
    using size_type = std::size_t;
    using key_comparer_handle = std::shared_ptr<const key_comparer_type>;
    using priority_comparer_handle = std::shared_ptr<const priority_comparer_type>;
    using add_result = priority_search_add_result<
        key_type,
        priority_type,
        value_type,
        key_comparer_type,
        priority_comparer_type>;
    using remove_result = priority_search_remove_result<
        key_type,
        priority_type,
        value_type,
        key_comparer_type,
        priority_comparer_type>;
    using minimum_view = priority_search_minimum_view<
        key_type,
        priority_type,
        value_type,
        key_comparer_type,
        priority_comparer_type>;

    class const_iterator final {
    public:
        using iterator_category = std::forward_iterator_tag;
        using iterator_concept = std::forward_iterator_tag;
        using value_type = entry_type;
        using difference_type = std::ptrdiff_t;
        using pointer = const entry_type*;
        using reference = const entry_type&;

        const_iterator() = default;

        [[nodiscard]] reference operator*() const { return pending_.back()->entry; }
        [[nodiscard]] pointer operator->() const { return &pending_.back()->entry; }

        const_iterator& operator++()
        {
            const auto* current = pending_.back();
            pending_.pop_back();
            push_left(current->right.get());
            return *this;
        }

        const_iterator operator++(int)
        {
            auto previous = *this;
            ++*this;
            return previous;
        }

        friend bool operator==(const const_iterator& left, const const_iterator& right) noexcept
        {
            if (left.pending_.empty() || right.pending_.empty()) {
                return left.pending_.empty() && right.pending_.empty();
            }
            return left.owner_ == right.owner_ && left.pending_ == right.pending_;
        }

    private:
        friend class priority_search_queue;

        explicit const_iterator(node_pointer root)
            : owner_(std::move(root))
        {
            push_left(owner_.get());
        }

        void push_left(const node* current)
        {
            while (current != nullptr) {
                pending_.push_back(current);
                current = current->left.get();
            }
        }

        node_pointer owner_;
        std::vector<const node*> pending_;
    };

    priority_search_queue()
        requires std::default_initializable<key_comparer_type>
            && std::default_initializable<priority_comparer_type>
        : key_comparer_(default_key_comparer())
        , priority_comparer_(default_priority_comparer())
    {
    }

    priority_search_queue(
        key_comparer_type key_comparer,
        priority_comparer_type priority_comparer)
        : key_comparer_(std::make_shared<key_comparer_type>(std::move(key_comparer)))
        , priority_comparer_(
            std::make_shared<priority_comparer_type>(std::move(priority_comparer)))
    {
    }

    priority_search_queue(
        key_comparer_handle key_comparer,
        priority_comparer_handle priority_comparer)
        : key_comparer_(std::move(key_comparer))
        , priority_comparer_(std::move(priority_comparer))
    {
        if (key_comparer_ == nullptr || priority_comparer_ == nullptr) {
            throw std::invalid_argument("priority-search comparer handles cannot be null");
        }
    }

    template <std::ranges::input_range Range>
        requires std::same_as<std::remove_cvref_t<std::ranges::range_value_t<Range>>, entry_type>
            && priority_search_equality_testable<priority_type>
            && priority_search_equality_testable<value_type>
            && std::default_initializable<key_comparer_type>
            && std::default_initializable<priority_comparer_type>
    [[nodiscard]] static priority_search_queue from_range(Range&& entries)
    {
        return priority_search_queue{}.insert_range(std::forward<Range>(entries));
    }

    template <std::ranges::input_range Range>
        requires std::same_as<std::remove_cvref_t<std::ranges::range_value_t<Range>>, entry_type>
            && priority_search_equality_testable<priority_type>
            && priority_search_equality_testable<value_type>
    [[nodiscard]] static priority_search_queue from_range(
        Range&& entries,
        key_comparer_type key_comparer,
        priority_comparer_type priority_comparer)
    {
        return priority_search_queue{
            std::move(key_comparer),
            std::move(priority_comparer)}.insert_range(std::forward<Range>(entries));
    }

    template <std::ranges::input_range Range>
        requires std::same_as<std::remove_cvref_t<std::ranges::range_value_t<Range>>, entry_type>
            && priority_search_equality_testable<priority_type>
            && priority_search_equality_testable<value_type>
    [[nodiscard]] priority_search_queue insert_range(Range&& entries) const
    {
        auto result = *this;
        for (auto&& entry : entries) {
            result = result.set_entry(entry);
        }
        return result;
    }

    [[nodiscard]] bool empty() const noexcept { return root_ == nullptr; }
    [[nodiscard]] size_type size() const noexcept { return count_of(root_); }
    [[nodiscard]] size_type height() const noexcept { return height_of(root_); }
    [[nodiscard]] const key_comparer_type& key_comparer() const noexcept { return *key_comparer_; }
    [[nodiscard]] const priority_comparer_type& priority_comparer() const noexcept
    {
        return *priority_comparer_;
    }
    [[nodiscard]] const key_comparer_handle& key_comparer_policy() const noexcept
    {
        return key_comparer_;
    }
    [[nodiscard]] const priority_comparer_handle& priority_comparer_policy() const noexcept
    {
        return priority_comparer_;
    }
    [[nodiscard]] const void* root_identity() const noexcept { return root_.get(); }

    [[nodiscard]] bool is_same_version(const priority_search_queue& other) const noexcept
    {
        return root_ == other.root_
            && key_comparer_ == other.key_comparer_
            && priority_comparer_ == other.priority_comparer_;
    }

    [[nodiscard]] bool contains_key(const key_type& key) const
    {
        return find_node(key) != nullptr;
    }

    /// Returns the number of entries whose keys order strictly before `key`, which is also the
    /// rank of its lower-bound gap. One O(h) descent guided by the cached subtree counts.
    [[nodiscard]] size_type count_keys_less_than(const key_type& key) const
    {
        return count_keys_before(key, false);
    }

    /// Returns the number of entries whose keys do not order after `key`, which is also the
    /// rank of its upper-bound gap. One O(h) descent guided by the cached subtree counts.
    [[nodiscard]] size_type count_keys_at_most(const key_type& key) const
    {
        return count_keys_before(key, true);
    }

    /// Returns the entry at a key-order rank, or nullptr when the rank is at or past the end.
    /// One O(h) descent guided by the cached subtree counts; no comparisons are made, so the
    /// key comparer is never invoked. The reference follows this snapshot's lifetime.
    [[nodiscard]] const entry_type* try_entry_at_rank(size_type rank) const noexcept
    {
        const auto* current = root_.get();
        while (current != nullptr) {
            const auto left_count = count_of(current->left);
            if (rank < left_count) {
                current = current->left.get();
                continue;
            }
            if (rank == left_count) {
                return &current->entry;
            }
            rank -= left_count + 1;
            current = current->right.get();
        }
        return nullptr;
    }

    [[nodiscard]] const entry_type* try_get_entry(const key_type& key) const
    {
        const auto* found = find_node(key);
        return found == nullptr ? nullptr : &found->entry;
    }

    [[nodiscard]] std::optional<entry_type> try_get_entry_handle(const key_type& key) const
    {
        const auto* found = find_node(key);
        return found == nullptr
            ? std::nullopt
            : std::optional<entry_type>{found->entry};
    }

    template <class KeyArgument, class PriorityArgument, class ValueArgument>
        requires std::constructible_from<key_type, KeyArgument&&>
            && std::constructible_from<priority_type, PriorityArgument&&>
            && std::constructible_from<value_type, ValueArgument&&>
            && priority_search_equality_testable<priority_type>
            && priority_search_equality_testable<value_type>
    [[nodiscard]] priority_search_queue set_item(
        KeyArgument&& key,
        PriorityArgument&& priority,
        ValueArgument&& value) const
    {
        return set_entry(entry_type{
            std::forward<KeyArgument>(key),
            std::forward<PriorityArgument>(priority),
            std::forward<ValueArgument>(value)});
    }

    [[nodiscard]] priority_search_queue set_entry(entry_type entry) const
        requires priority_search_equality_testable<priority_type>
            && priority_search_equality_testable<value_type>
    {
        const auto result = set_overwrite(root_, std::move(entry));
        return result.changed
            ? priority_search_queue{result.node, key_comparer_, priority_comparer_}
            : *this;
    }

    template <class KeyArgument, class PriorityArgument, class ValueArgument>
        requires std::constructible_from<Key, KeyArgument&&>
            && std::constructible_from<Priority, PriorityArgument&&>
            && std::constructible_from<Value, ValueArgument&&>
    [[nodiscard]] add_result try_add(
        KeyArgument&& key,
        PriorityArgument&& priority,
        ValueArgument&& value) const;

    [[nodiscard]] priority_search_queue remove(const key_type& key) const
    {
        const auto result = remove_node(root_, key);
        return result.entry.has_value()
            ? priority_search_queue{result.node, key_comparer_, priority_comparer_}
            : *this;
    }

    [[nodiscard]] remove_result try_remove(const key_type& key) const;

    [[nodiscard]] const entry_type& minimum() const
    {
        if (root_ == nullptr) {
            throw std::logic_error("priority search queue is empty");
        }
        return root_->winner;
    }

    [[nodiscard]] std::optional<entry_type> try_minimum() const
    {
        return root_ == nullptr
            ? std::nullopt
            : std::optional<entry_type>{root_->winner};
    }

    [[nodiscard]] minimum_view delete_minimum() const;

    [[nodiscard]] std::vector<entry_type> enumerate_at_most(
        const key_type& minimum_key,
        const key_type& maximum_key,
        const priority_type& maximum_priority) const
    {
        if (std::invoke(*key_comparer_, maximum_key, minimum_key)) {
            throw std::invalid_argument("priority-search key range is inverted");
        }

        struct query_frame final {
            const node* current;
            bool emit;
        };
        auto result = std::vector<entry_type>{};
        auto pending = std::vector<query_frame>{};
        if (root_ != nullptr) {
            pending.push_back(query_frame{root_.get(), false});
        }
        while (!pending.empty()) {
            const auto frame = pending.back();
            pending.pop_back();
            const auto& current = *frame.current;
            if (frame.emit) {
                result.push_back(current.entry);
                continue;
            }
            if (std::invoke(
                    *priority_comparer_,
                    maximum_priority,
                    current.winner.priority())) {
                continue;
            }

            const auto below_minimum = std::invoke(
                *key_comparer_, current.entry.key(), minimum_key);
            if (below_minimum) {
                if (current.right != nullptr) {
                    pending.push_back(query_frame{current.right.get(), false});
                }
                continue;
            }
            const auto above_maximum = std::invoke(
                *key_comparer_, maximum_key, current.entry.key());
            if (above_maximum) {
                if (current.left != nullptr) {
                    pending.push_back(query_frame{current.left.get(), false});
                }
                continue;
            }

            if (current.right != nullptr
                && std::invoke(*key_comparer_, current.entry.key(), maximum_key)) {
                pending.push_back(query_frame{current.right.get(), false});
            }
            if (!std::invoke(
                    *priority_comparer_,
                    maximum_priority,
                    current.entry.priority())) {
                pending.push_back(query_frame{&current, true});
            }
            if (current.left != nullptr
                && std::invoke(*key_comparer_, minimum_key, current.entry.key())) {
                pending.push_back(query_frame{current.left.get(), false});
            }
        }
        return result;
    }

    [[nodiscard]] priority_search_queue clear() const
    {
        return root_ == nullptr
            ? *this
            : priority_search_queue{nullptr, key_comparer_, priority_comparer_};
    }

    [[nodiscard]] std::vector<const void*> node_identities() const
    {
        auto result = std::vector<const void*>{};
        auto pending = std::vector<const node*>{};
        if (root_ != nullptr) {
            pending.push_back(root_.get());
        }
        while (!pending.empty()) {
            const auto* current = pending.back();
            pending.pop_back();
            result.push_back(current);
            if (current->left != nullptr) {
                pending.push_back(current->left.get());
            }
            if (current->right != nullptr) {
                pending.push_back(current->right.get());
            }
        }
        return result;
    }

    [[nodiscard]] size_type shared_node_count_with(const priority_search_queue& other) const
    {
        const auto identities = node_identities();
        const auto known = std::unordered_set<const void*>{identities.begin(), identities.end()};
        auto shared = size_type{0};
        for (const auto identity : other.node_identities()) {
            shared += known.contains(identity) ? 1 : 0;
        }
        return shared;
    }

    [[nodiscard]] const void* node_identity_for_key(const key_type& key) const
    {
        return find_node(key);
    }

    [[nodiscard]] bool shares_node_for_key(
        const priority_search_queue& other,
        const key_type& key) const
    {
        return find_node(key) == other.find_node(key);
    }

    [[nodiscard]] priority_search_queue_statistics validate_structure() const
    {
        if (root_ == nullptr) {
            return {};
        }
        struct validation_frame final {
            const node* current;
            const key_type* lower;
            const key_type* upper;
        };
        auto pending = std::vector<validation_frame>{{root_.get(), nullptr, nullptr}};
        auto validated_count = size_type{0};
        auto maximum_balance = size_type{0};
        while (!pending.empty()) {
            const auto frame = pending.back();
            pending.pop_back();
            const auto& current = *frame.current;
            if (frame.lower != nullptr && compare_key(current.entry.key(), *frame.lower) <= 0) {
                throw std::logic_error("priority-search node crosses its lower key bound");
            }
            if (frame.upper != nullptr && compare_key(current.entry.key(), *frame.upper) >= 0) {
                throw std::logic_error("priority-search node crosses its upper key bound");
            }

            const auto expected_count = checked_add(
                checked_add(size_type{1}, count_of(current.left)),
                count_of(current.right));
            const auto expected_height = checked_add(
                size_type{1},
                (std::max)(height_of(current.left), height_of(current.right)));
            if (current.count != expected_count || current.height != expected_height) {
                throw std::logic_error("priority-search node has invalid cached metadata");
            }
            const auto left_height = height_of(current.left);
            const auto right_height = height_of(current.right);
            const auto balance = left_height >= right_height
                ? left_height - right_height
                : right_height - left_height;
            if (balance > 1) {
                throw std::logic_error("priority-search node violates AVL balance");
            }

            const auto expected_winner = select_winner(
                current.entry,
                current.left,
                current.right);
            if (!expected_winner.is_same_entry(current.winner)) {
                throw std::logic_error("priority-search node has an invalid cached winner");
            }

            validated_count = checked_add(validated_count, 1);
            maximum_balance = (std::max)(maximum_balance, balance);
            if (current.right != nullptr) {
                pending.push_back(validation_frame{
                    current.right.get(),
                    &current.entry.key(),
                    frame.upper});
            }
            if (current.left != nullptr) {
                pending.push_back(validation_frame{
                    current.left.get(),
                    frame.lower,
                    &current.entry.key()});
            }
        }
        if (validated_count != size()) {
            throw std::logic_error("priority-search root count disagrees with validated nodes");
        }
        return priority_search_queue_statistics{
            validated_count,
            height(),
            maximum_balance};
    }

    [[nodiscard]] const_iterator begin() const { return const_iterator{root_}; }
    [[nodiscard]] const_iterator end() const noexcept { return {}; }
    [[nodiscard]] const_iterator cbegin() const { return begin(); }
    [[nodiscard]] const_iterator cend() const noexcept { return end(); }

private:
    struct node final {
        node(
            entry_type node_entry,
            node_pointer node_left,
            node_pointer node_right,
            entry_type node_winner,
            const size_type node_height,
            const size_type node_count)
            : entry(std::move(node_entry))
            , left(std::move(node_left))
            , right(std::move(node_right))
            , winner(std::move(node_winner))
            , height(node_height)
            , count(node_count)
        {
        }

        entry_type entry;
        node_pointer left;
        node_pointer right;
        entry_type winner;
        size_type height;
        size_type count;
    };

    struct set_result final {
        node_pointer node;
        bool added;
        bool changed;
    };

    struct node_removal final {
        node_pointer node;
        std::optional<entry_type> entry;
    };

    priority_search_queue(
        node_pointer root,
        key_comparer_handle key_comparer,
        priority_comparer_handle priority_comparer)
        : root_(std::move(root))
        , key_comparer_(std::move(key_comparer))
        , priority_comparer_(std::move(priority_comparer))
    {
    }

    [[nodiscard]] static const key_comparer_handle& default_key_comparer()
        requires std::default_initializable<key_comparer_type>
    {
        static const key_comparer_handle value = std::make_shared<key_comparer_type>();
        return value;
    }

    [[nodiscard]] static const priority_comparer_handle& default_priority_comparer()
        requires std::default_initializable<priority_comparer_type>
    {
        static const priority_comparer_handle value =
            std::make_shared<priority_comparer_type>();
        return value;
    }

    [[nodiscard]] static size_type checked_add(const size_type left, const size_type right)
    {
        if (right > (std::numeric_limits<size_type>::max)() - left) {
            throw std::length_error("priority search queue size exceeds size_t");
        }
        return left + right;
    }

    [[nodiscard]] static size_type height_of(const node_pointer& value) noexcept
    {
        return value == nullptr ? 0 : value->height;
    }

    [[nodiscard]] static size_type count_of(const node_pointer& value) noexcept
    {
        return value == nullptr ? 0 : value->count;
    }

    [[nodiscard]] int compare_key(const key_type& left, const key_type& right) const
    {
        if (std::invoke(*key_comparer_, left, right)) {
            return -1;
        }
        if (std::invoke(*key_comparer_, right, left)) {
            return 1;
        }
        return 0;
    }

    [[nodiscard]] bool priorities_equivalent(
        const priority_type& left,
        const priority_type& right) const
    {
        return !std::invoke(*priority_comparer_, left, right)
            && !std::invoke(*priority_comparer_, right, left);
    }

    [[nodiscard]] bool is_before(const entry_type& left, const entry_type& right) const
    {
        if (std::invoke(*priority_comparer_, left.priority(), right.priority())) {
            return true;
        }
        if (std::invoke(*priority_comparer_, right.priority(), left.priority())) {
            return false;
        }
        return std::invoke(*key_comparer_, left.key(), right.key());
    }

    [[nodiscard]] bool same_replacement(
        const entry_type& replacement,
        const entry_type& stored) const
        requires priority_search_equality_testable<priority_type>
            && priority_search_equality_testable<value_type>
    {
        return priorities_equivalent(replacement.priority(), stored.priority())
            && replacement.priority() == stored.priority()
            && replacement.value() == stored.value();
    }

    [[nodiscard]] entry_type select_winner(
        const entry_type& entry,
        const node_pointer& left,
        const node_pointer& right) const
    {
        auto winner = entry;
        if (left != nullptr && is_before(left->winner, winner)) {
            winner = left->winner;
        }
        if (right != nullptr && is_before(right->winner, winner)) {
            winner = right->winner;
        }
        return winner;
    }

    [[nodiscard]] node_pointer make_node(
        entry_type entry,
        node_pointer left,
        node_pointer right) const
    {
        auto winner = select_winner(entry, left, right);
        const auto node_height = checked_add(
            size_type{1},
            (std::max)(height_of(left), height_of(right)));
        const auto node_count = checked_add(
            checked_add(size_type{1}, count_of(left)),
            count_of(right));
        return std::make_shared<node>(
            std::move(entry),
            std::move(left),
            std::move(right),
            std::move(winner),
            node_height,
            node_count);
    }

    [[nodiscard]] node_pointer rotate_left(const node_pointer& value) const
    {
        const auto& pivot = value->right;
        return make_node(
            pivot->entry,
            make_node(value->entry, value->left, pivot->left),
            pivot->right);
    }

    [[nodiscard]] node_pointer rotate_right(const node_pointer& value) const
    {
        const auto& pivot = value->left;
        return make_node(
            pivot->entry,
            pivot->left,
            make_node(value->entry, pivot->right, value->right));
    }

    [[nodiscard]] node_pointer balance(node_pointer value) const
    {
        const auto left_height = height_of(value->left);
        const auto right_height = height_of(value->right);
        if (left_height > checked_add(right_height, 1)) {
            if (height_of(value->left->left) < height_of(value->left->right)) {
                return rotate_right(make_node(
                    value->entry,
                    rotate_left(value->left),
                    value->right));
            }
            return rotate_right(value);
        }
        if (right_height > checked_add(left_height, 1)) {
            if (height_of(value->right->right) < height_of(value->right->left)) {
                return rotate_left(make_node(
                    value->entry,
                    value->left,
                    rotate_right(value->right)));
            }
            return rotate_left(value);
        }
        return value;
    }

    [[nodiscard]] set_result set_overwrite(node_pointer current, entry_type entry) const
        requires priority_search_equality_testable<priority_type>
            && priority_search_equality_testable<value_type>
    {
        if (current == nullptr) {
            return set_result{make_node(std::move(entry), nullptr, nullptr), true, true};
        }
        const auto comparison = compare_key(entry.key(), current->entry.key());
        if (comparison == 0) {
            if (same_replacement(entry, current->entry)) {
                return set_result{current, false, false};
            }
            return set_result{
                make_node(
                    current->entry.replacing_from(entry),
                    current->left,
                    current->right),
                false,
                true};
        }
        if (comparison < 0) {
            const auto result = set_overwrite(current->left, std::move(entry));
            if (!result.changed) {
                return set_result{current, result.added, false};
            }
            return set_result{
                balance(make_node(current->entry, result.node, current->right)),
                result.added,
                true};
        }
        const auto result = set_overwrite(current->right, std::move(entry));
        if (!result.changed) {
            return set_result{current, result.added, false};
        }
        return set_result{
            balance(make_node(current->entry, current->left, result.node)),
            result.added,
            true};
    }

    [[nodiscard]] set_result insert_new(node_pointer current, entry_type entry) const
    {
        if (current == nullptr) {
            return set_result{make_node(std::move(entry), nullptr, nullptr), true, true};
        }
        const auto comparison = compare_key(entry.key(), current->entry.key());
        if (comparison == 0) {
            return set_result{current, false, false};
        }
        if (comparison < 0) {
            const auto result = insert_new(current->left, std::move(entry));
            if (!result.added) {
                return set_result{current, false, false};
            }
            return set_result{
                balance(make_node(current->entry, result.node, current->right)),
                true,
                true};
        }
        const auto result = insert_new(current->right, std::move(entry));
        if (!result.added) {
            return set_result{current, false, false};
        }
        return set_result{
            balance(make_node(current->entry, current->left, result.node)),
            true,
            true};
    }

    [[nodiscard]] node_removal remove_node(
        const node_pointer& current,
        const key_type& key) const
    {
        if (current == nullptr) {
            return node_removal{nullptr, std::nullopt};
        }
        const auto comparison = compare_key(key, current->entry.key());
        if (comparison == 0) {
            if (current->left == nullptr) {
                return node_removal{current->right, current->entry};
            }
            if (current->right == nullptr) {
                return node_removal{current->left, current->entry};
            }
            const auto* successor = minimum_key_node(current->right.get());
            const auto removed_successor = remove_node(current->right, successor->entry.key());
            return node_removal{
                balance(make_node(successor->entry, current->left, removed_successor.node)),
                current->entry};
        }
        if (comparison < 0) {
            const auto result = remove_node(current->left, key);
            if (!result.entry.has_value()) {
                return node_removal{current, std::nullopt};
            }
            return node_removal{
                balance(make_node(current->entry, result.node, current->right)),
                result.entry};
        }
        const auto result = remove_node(current->right, key);
        if (!result.entry.has_value()) {
            return node_removal{current, std::nullopt};
        }
        return node_removal{
            balance(make_node(current->entry, current->left, result.node)),
            result.entry};
    }

    /// Counts the entries whose keys order before `key`, optionally including an equal key.
    /// Each descent step folds in the whole left subtree through its cached count, so the key
    /// comparer is invoked once per level rather than once per stored entry.
    [[nodiscard]] size_type count_keys_before(
        const key_type& key,
        const bool include_equal) const
    {
        auto rank = size_type{0};
        const auto* current = root_.get();
        while (current != nullptr) {
            const auto comparison = compare_key(key, current->entry.key());
            if (comparison < 0) {
                current = current->left.get();
                continue;
            }
            const auto left_count = count_of(current->left);
            if (comparison == 0) {
                return include_equal ? rank + left_count + 1 : rank + left_count;
            }
            rank += left_count + 1;
            current = current->right.get();
        }
        return rank;
    }

    [[nodiscard]] const node* find_node(const key_type& key) const
    {
        auto current = root_.get();
        while (current != nullptr) {
            const auto comparison = compare_key(key, current->entry.key());
            if (comparison == 0) {
                return current;
            }
            current = comparison < 0 ? current->left.get() : current->right.get();
        }
        return nullptr;
    }

    [[nodiscard]] static const node* minimum_key_node(const node* current) noexcept
    {
        while (current->left != nullptr) {
            current = current->left.get();
        }
        return current;
    }

    node_pointer root_;
    key_comparer_handle key_comparer_;
    priority_comparer_handle priority_comparer_;
};

/// Result of a non-overwriting keyed insertion.
template <class Key, class Priority, class Value, class KeyLess, class PriorityLess>
struct priority_search_add_result final {
    bool added;
    priority_search_queue<Key, Priority, Value, KeyLess, PriorityLess> queue;
};

/// Result of a keyed removal. The outer optional is distinct from optional-valued components.
template <class Key, class Priority, class Value, class KeyLess, class PriorityLess>
struct priority_search_remove_result final {
    std::optional<priority_search_entry<Key, Priority, Value>> entry;
    priority_search_queue<Key, Priority, Value, KeyLess, PriorityLess> queue;

    [[nodiscard]] bool removed() const noexcept { return entry.has_value(); }
};

/// The exact priority-then-key winner handle and the persistent queue remaining after its removal.
template <class Key, class Priority, class Value, class KeyLess, class PriorityLess>
struct priority_search_minimum_view final {
    priority_search_entry<Key, Priority, Value> entry;
    priority_search_queue<Key, Priority, Value, KeyLess, PriorityLess> remainder;
};

template <class Key, class Priority, class Value, class KeyLess, class PriorityLess>
template <class KeyArgument, class PriorityArgument, class ValueArgument>
    requires std::constructible_from<Key, KeyArgument&&>
        && std::constructible_from<Priority, PriorityArgument&&>
        && std::constructible_from<Value, ValueArgument&&>
auto priority_search_queue<Key, Priority, Value, KeyLess, PriorityLess>::try_add(
    KeyArgument&& key,
    PriorityArgument&& priority,
    ValueArgument&& value) const -> add_result
{
    const auto result = insert_new(
        root_,
        entry_type{
            std::forward<KeyArgument>(key),
            std::forward<PriorityArgument>(priority),
            std::forward<ValueArgument>(value)});
    return add_result{
        result.added,
        result.added
            ? priority_search_queue{result.node, key_comparer_, priority_comparer_}
            : *this};
}

template <class Key, class Priority, class Value, class KeyLess, class PriorityLess>
auto priority_search_queue<Key, Priority, Value, KeyLess, PriorityLess>::try_remove(
    const key_type& key) const -> remove_result
{
    const auto result = remove_node(root_, key);
    return remove_result{
        result.entry,
        result.entry.has_value()
            ? priority_search_queue{result.node, key_comparer_, priority_comparer_}
            : *this};
}

template <class Key, class Priority, class Value, class KeyLess, class PriorityLess>
auto priority_search_queue<Key, Priority, Value, KeyLess, PriorityLess>::delete_minimum() const
    -> minimum_view
{
    if (root_ == nullptr) {
        throw std::logic_error("priority search queue is empty");
    }
    const auto winner = root_->winner;
    const auto removed = remove_node(root_, winner.key());
    return minimum_view{
        winner,
        priority_search_queue{removed.node, key_comparer_, priority_comparer_}};
}

} // namespace durable7::finger_tree
