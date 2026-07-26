/// A persistent multiset: hashed elements with multiplicities.
///
/// Multiplicity is stored per distinct element, so a large count costs no more than a small one.
/// Every operation returns a new version and leaves its inputs valid, sharing unchanged structure,
/// so an edit copies a path rather than the whole collection.

#pragma once

#include "persistent_hash_map.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace durable7::hamt {

// An immutable unordered multiset. Each equivalence class retains one stored
// representative and an Int32 multiplicity; total_count is an Int64 expanded
// count. Algebra is interpreted under the receiver's hash/equality policy.
template <
    class T,
    class Hash = std::hash<T>,
    class KeyEqual = std::equal_to<T>>
/// A persistent multiset: hashed elements with multiplicities stored per distinct element, so a
/// large count costs no more than a small one.
class persistent_hash_bag {
public:
    using multiplicity_type = std::int32_t;

private:
    using map_type = persistent_hash_map<T, multiplicity_type, Hash, KeyEqual>;

    enum class operation {
        union_values,
        intersect,
        except,
        sum,
    };

public:
    using key_type = T;
    using value_type = T;
    using entry_type = std::pair<T, multiplicity_type>;
    using size_type = std::size_t;
    using total_size_type = std::int64_t;
    using hasher = Hash;
    using key_equal = KeyEqual;

    /// An empty bag.
    persistent_hash_bag() = default;

    /// An empty bag.
    persistent_hash_bag(const persistent_hash_bag&) = default;
    persistent_hash_bag& operator=(const persistent_hash_bag&) = default;

    /// Takes over the source's handle, leaving it empty.
    persistent_hash_bag(persistent_hash_bag&& other) noexcept(
        std::is_nothrow_move_constructible_v<map_type>)
        : counts_(std::move(other.counts_)),
          total_count_(std::exchange(other.total_count_, 0)) {
    }

    persistent_hash_bag& operator=(persistent_hash_bag&& other) noexcept(
        std::is_nothrow_move_assignable_v<map_type>) {
        if (this != &other) {
            counts_ = std::move(other.counts_);
            total_count_ = std::exchange(other.total_count_, 0);
        }
        return *this;
    }

    /// An empty bag.
    ~persistent_hash_bag() = default;

    /// Whether the bag holds no occurrences.
    static persistent_hash_bag empty() {
        return {};
    }

    /// An empty bag using the supplied policies, which it retains.
    static persistent_hash_bag create(Hash hash = {}, KeyEqual equal = {}) {
        return persistent_hash_bag(
            map_type::create(std::move(hash), std::move(equal)), 0);
    }

    /// A bag holding a range's occurrences, built in bulk rather than by repeated insertion.
    static persistent_hash_bag create_range(
        std::initializer_list<T> items,
        Hash hash = {},
        KeyEqual equal = {}) {
        return create_range_impl(items, std::move(hash), std::move(equal));
    }

    /// A bag holding a range's occurrences, built in bulk rather than by repeated insertion.
    template <class Range>
    static persistent_hash_bag create_range(
        const Range& items,
        Hash hash = {},
        KeyEqual equal = {}) {
        return create_range_impl(items, std::move(hash), std::move(equal));
    }

    /// How many distinct elements are present, ignoring multiplicity.
    [[nodiscard]] size_type distinct_count() const noexcept {
        return counts_.count();
    }

    /// The total multiplicity across all elements.
    [[nodiscard]] total_size_type total_count() const noexcept {
        return total_count_;
    }

    /// Whether the bag holds no occurrences.
    [[nodiscard]] bool is_empty() const noexcept {
        return counts_.is_empty();
    }

    /// The retained hashing policy. Keys the policy treats as equivalent must hash identically.
    [[nodiscard]] const Hash& hash_function() const noexcept {
        return counts_.hash_function();
    }

    /// The retained key equivalence policy.
    [[nodiscard]] const KeyEqual& key_eq() const noexcept {
        return counts_.key_eq();
    }

    /// Whether the occurrence is present.
    [[nodiscard]] bool contains(const T& item) const {
        return counts_.contains_key(item);
    }

    /// How many times the occurrence occurs.
    [[nodiscard]] multiplicity_type count_of(const T& item) const {
        const auto* count = counts_.try_get(item);
        return count == nullptr ? 0 : *count;
    }

    // Returns the retained representative for an equivalent item, or null
    // when the equivalence class is absent. The pointer follows map lookup's
    // shared-node lifetime contract.
    [[nodiscard]] const T* try_get_value(const T& equal_value) const {
        return counts_.try_get_key(equal_value);
    }

    /// A bag containing the given occurrence; returns the receiver when already present.
    [[nodiscard]] persistent_hash_bag add(const T& item) const {
        return add_copies(item, 1);
    }

    /// A bag with the element's multiplicity raised by the given count.
    [[nodiscard]] persistent_hash_bag add_copies(
        const T& item,
        total_size_type copies) const {
        const auto increment = validate_copy_count(copies);
        if (increment == 0) {
            return *this;
        }

        const auto new_total = checked_total_add(total_count_, increment);
        auto [counts, selected] = counts_.add_or_update(
            item,
            [increment](const T&) { return increment; },
            [increment](const T&, multiplicity_type stored) {
                return checked_multiplicity_add(stored, increment);
            });
        (void)selected;
        return with_counts(std::move(counts), new_total);
    }

    /// A bag without that occurrence; returns the receiver when absent.
    [[nodiscard]] persistent_hash_bag remove(const T& item) const {
        return remove_copies(item, 1);
    }

    /// A bag with the element's multiplicity lowered by the given count, stopping at zero.
    [[nodiscard]] persistent_hash_bag remove_copies(
        const T& item,
        total_size_type copies) const {
        const auto decrement = validate_copy_count(copies);
        if (decrement == 0) {
            return *this;
        }

        const auto* stored = counts_.try_get(item);
        if (stored == nullptr) {
            return *this;
        }
        if (decrement >= *stored) {
            auto [counts, removed, value] = counts_.try_remove(item);
            if (!removed || !value) {
                throw std::logic_error("A located hash-bag entry could not be removed.");
            }
            return with_counts(
                std::move(counts), total_count_ - static_cast<total_size_type>(*value));
        }

        const auto selected = static_cast<multiplicity_type>(*stored - decrement);
        return with_counts(
            counts_.set_item(item, selected),
            total_count_ - static_cast<total_size_type>(decrement));
    }

    /// A bag without any occurrence of the occurrence.
    [[nodiscard]] persistent_hash_bag remove_all(const T& item) const {
        auto [counts, removed, value] = counts_.try_remove(item);
        if (!removed) {
            return *this;
        }
        if (!value) {
            throw std::logic_error("A removed hash-bag entry had no multiplicity.");
        }
        return with_counts(
            std::move(counts), total_count_ - static_cast<total_size_type>(*value));
    }

    /// An empty bag retaining the same policies; returns the receiver when already empty.
    [[nodiscard]] persistent_hash_bag clear() const {
        return with_counts(counts_.clear(), 0);
    }

    // Multiset union: the larger multiplicity wins in every receiver-policy
    // equivalence class.
    [[nodiscard]] persistent_hash_bag union_with(
        const persistent_hash_bag& other) const {
        return combine(other, operation::union_values);
    }

    // Multiset intersection: the smaller multiplicity wins.
    [[nodiscard]] persistent_hash_bag intersect_with(
        const persistent_hash_bag& other) const {
        return combine(other, operation::intersect);
    }

    // Saturating receiver-minus-argument subtraction.
    [[nodiscard]] persistent_hash_bag except_with(
        const persistent_hash_bag& other) const {
        return combine(other, operation::except);
    }

    // Additive multiset sum with checked Int32 per-class and Int64 total
    // multiplicities.
    [[nodiscard]] persistent_hash_bag sum_with(
        const persistent_hash_bag& other) const {
        return combine(other, operation::sum);
    }

    /// A forward const iterator over one bag version's occurrences. It keeps that version alive, so
    /// it stays valid across later edits.
    class const_iterator {
    public:
        using iterator_concept = std::input_iterator_tag;
        using iterator_category = std::input_iterator_tag;
        using value_type = persistent_hash_bag::value_type;
        using difference_type = std::ptrdiff_t;
        using reference = const value_type&;
        using pointer = const value_type*;

        /// An unpositioned cursor, holding no version.
        const_iterator() = default;

        reference operator*() const {
            return inner_->first;
        }

        pointer operator->() const {
            return std::addressof(inner_->first);
        }

        const_iterator& operator++() {
            if (--remaining_ == 0) {
                ++inner_;
                if (inner_ != std::default_sentinel) {
                    remaining_ = inner_->second;
                }
            }
            return *this;
        }

        const_iterator operator++(int) {
            auto copy = *this;
            ++(*this);
            return copy;
        }

        friend bool operator==(
            const const_iterator& iterator,
            std::default_sentinel_t sentinel) noexcept {
            return iterator.inner_ == sentinel;
        }

        friend bool operator==(
            std::default_sentinel_t sentinel,
            const const_iterator& iterator) noexcept {
            return iterator == sentinel;
        }

        friend bool operator!=(
            const const_iterator& iterator,
            std::default_sentinel_t sentinel) noexcept {
            return !(iterator == sentinel);
        }

        friend bool operator!=(
            std::default_sentinel_t sentinel,
            const const_iterator& iterator) noexcept {
            return !(iterator == sentinel);
        }

    private:
        friend class persistent_hash_bag;

        explicit const_iterator(typename map_type::const_iterator inner)
            : inner_(std::move(inner)),
              remaining_(inner_ == std::default_sentinel ? 0 : inner_->second) {
        }

        typename map_type::const_iterator inner_;
        multiplicity_type remaining_ = 0;
    };

    /// A view over the distinct elements, ignoring multiplicity.
    class distinct_items_view {
    public:
        /// A forward const iterator over one collection version's elements. It keeps that version
        /// alive, so it stays valid across later edits.
        class const_iterator {
        public:
            using iterator_concept = std::input_iterator_tag;
            using iterator_category = std::input_iterator_tag;
            using value_type = T;
            using difference_type = std::ptrdiff_t;
            using reference = const T&;
            using pointer = const T*;

            /// An unpositioned cursor, holding no version.
            const_iterator() = default;

            reference operator*() const { return inner_->first; }
            pointer operator->() const { return std::addressof(inner_->first); }
            const_iterator& operator++() { ++inner_; return *this; }
            const_iterator operator++(int) { auto copy = *this; ++(*this); return copy; }

            friend bool operator==(
                const const_iterator& iterator,
                std::default_sentinel_t sentinel) noexcept {
                return iterator.inner_ == sentinel;
            }

            friend bool operator==(
                std::default_sentinel_t sentinel,
                const const_iterator& iterator) noexcept {
                return iterator == sentinel;
            }

            friend bool operator!=(
                const const_iterator& iterator,
                std::default_sentinel_t sentinel) noexcept {
                return !(iterator == sentinel);
            }

            friend bool operator!=(
                std::default_sentinel_t sentinel,
                const const_iterator& iterator) noexcept {
                return !(iterator == sentinel);
            }

        private:
            friend class distinct_items_view;

            explicit const_iterator(typename map_type::const_iterator inner)
                : inner_(std::move(inner)) {
            }

            typename map_type::const_iterator inner_;
        };

        /// An iterator over the elements, in the collection's own order.
        [[nodiscard]] const_iterator begin() const {
            return const_iterator(map_.begin());
        }

        /// The iterator one past the last element.
        [[nodiscard]] std::default_sentinel_t end() const noexcept {
            return {};
        }

    private:
        friend class persistent_hash_bag;

        explicit distinct_items_view(map_type map)
            : map_(std::move(map)) {
        }

        map_type map_;
    };

    /// A view over the entries.
    class entries_view {
    public:
        using const_iterator = typename map_type::const_iterator;

        /// An iterator over the elements, in the collection's own order.
        [[nodiscard]] const_iterator begin() const {
            return map_.begin();
        }

        /// The iterator one past the last element.
        [[nodiscard]] std::default_sentinel_t end() const noexcept {
            return {};
        }

    private:
        friend class persistent_hash_bag;

        explicit entries_view(map_type map)
            : map_(std::move(map)) {
        }

        map_type map_;
    };

    /// An iterator over the occurrences, in the bag's own order.
    [[nodiscard]] const_iterator begin() const {
        return const_iterator(counts_.begin());
    }

    /// The iterator one past the last element.
    [[nodiscard]] std::default_sentinel_t end() const noexcept {
        return {};
    }

    /// A const iterator over the occurrences.
    [[nodiscard]] const_iterator cbegin() const {
        return begin();
    }

    /// The const iterator one past the last element.
    [[nodiscard]] std::default_sentinel_t cend() const noexcept {
        return {};
    }

    /// The distinct elements, ignoring multiplicity.
    [[nodiscard]] distinct_items_view distinct_items() const {
        return distinct_items_view(counts_);
    }

    /// The entries.
    [[nodiscard]] entries_view entries() const {
        return entries_view(counts_);
    }

    /// Copies the occurrences out into a vector, in the bag's own order.
    [[nodiscard]] std::vector<T> to_vector() const {
        auto result = std::vector<T>{};
        if (std::cmp_greater(total_count_, result.max_size())) {
            throw std::length_error(
                "The expanded hash-bag count exceeds vector::max_size().");
        }

        result.reserve(static_cast<size_type>(total_count_));
        for (const auto& item : *this) {
            result.push_back(item);
        }
        return result;
    }

    /// Whether both handles reference the same root node. A representation check used to confirm a
    /// no-op avoided copying, not an equality test.
    [[nodiscard]] bool shares_root_with(const persistent_hash_bag& other) const noexcept {
        return counts_.shares_root_with(other.counts_);
    }

    /// Whether both handles retain the same policy. Policy identity governs whether two collections
    /// may be combined at all.
    [[nodiscard]] bool shares_policy_with(const persistent_hash_bag& other) const noexcept {
        return counts_.shares_policy_with(other.counts_);
    }

    /// The root node's address, for tests that a no-op shared rather than copied.
    [[nodiscard]] const void* debug_root_identity() const noexcept {
        return counts_.debug_root_identity();
    }

    /// Checks that the structure is in its canonical shape, which must depend only on contents and
    /// not on edit history.
    [[nodiscard]] bool debug_validate_canonical() const noexcept {
        if (!counts_.debug_validate_canonical() || total_count_ < 0) {
            return false;
        }

        auto checked_total = total_size_type{0};
        for (const auto& entry : counts_) {
            const auto multiplicity = entry.second;
            if (multiplicity <= 0
                || multiplicity
                    > std::numeric_limits<total_size_type>::max() - checked_total) {
                return false;
            }
            checked_total += multiplicity;
        }
        return checked_total == total_count_;
    }

private:
    persistent_hash_bag(map_type counts, total_size_type total_count)
        : counts_(std::move(counts)),
          total_count_(total_count) {
    }

    template <class Range>
    static persistent_hash_bag create_range_impl(
        const Range& items,
        Hash hash,
        KeyEqual equal) {
        auto builder = map_type::create_bulk_builder(
            std::move(hash), std::move(equal));
        auto total = total_size_type{0};
        for (const auto& item : items) {
            builder.add_or_update(
                item,
                multiplicity_type{1},
                [](multiplicity_type stored, multiplicity_type increment) {
                    return checked_multiplicity_add(stored, increment);
                });
            total = checked_total_add(total, multiplicity_type{1});
        }
        return persistent_hash_bag(builder.to_immutable(), total);
    }

    static multiplicity_type validate_copy_count(total_size_type copies) {
        if (copies < 0
            || copies > std::numeric_limits<multiplicity_type>::max()) {
            throw std::out_of_range(
                "copies must be between zero and INT32_MAX.");
        }
        return static_cast<multiplicity_type>(copies);
    }

    static multiplicity_type checked_multiplicity_add(
        multiplicity_type left,
        multiplicity_type right) {
        if (right > std::numeric_limits<multiplicity_type>::max() - left) {
            throw std::overflow_error(
                "The per-class hash-bag multiplicity exceeds INT32_MAX.");
        }
        return static_cast<multiplicity_type>(left + right);
    }

    static total_size_type checked_total_add(
        total_size_type left,
        multiplicity_type right) {
        if (right > std::numeric_limits<total_size_type>::max() - left) {
            throw std::overflow_error(
                "The expanded hash-bag count exceeds INT64_MAX.");
        }
        return left + static_cast<total_size_type>(right);
    }

    [[nodiscard]] persistent_hash_bag with_counts(
        map_type counts,
        total_size_type total_count) const {
        if (counts.shares_root_with(counts_)) {
            if (total_count != total_count_) {
                throw std::logic_error(
                    "An unchanged hash-bag map cannot have a different total count.");
            }
            return *this;
        }
        return persistent_hash_bag(std::move(counts), total_count);
    }

    [[nodiscard]] persistent_hash_bag combine(
        const persistent_hash_bag& other,
        operation selected_operation) const {
        auto normalized = shares_policy_with(other)
            ? other
            : normalize_argument(other);

        if (shares_root_with(normalized)) {
            switch (selected_operation) {
            case operation::union_values:
            case operation::intersect:
                return *this;
            case operation::except:
                return clear();
            case operation::sum:
                break;
            }
        }

        switch (selected_operation) {
        case operation::union_values:
            return union_normalized(normalized);
        case operation::intersect:
            return intersect_normalized(normalized);
        case operation::except:
            return except_normalized(normalized);
        case operation::sum:
            return sum_normalized(normalized);
        }
        throw std::logic_error("Unknown hash-bag operation.");
    }

    [[nodiscard]] persistent_hash_bag normalize_argument(
        const persistent_hash_bag& other) const {
        auto builder = map_type::create_bulk_builder(hash_function(), key_eq());
        auto total = total_size_type{0};
        for (const auto& [item, multiplicity] : other.counts_) {
            builder.add_or_update(
                item,
                multiplicity,
                [](multiplicity_type stored, multiplicity_type incoming) {
                    return checked_multiplicity_add(stored, incoming);
                });
            total = checked_total_add(total, multiplicity);
        }
        return persistent_hash_bag(builder.to_immutable(), total);
    }

    [[nodiscard]] persistent_hash_bag union_normalized(
        const persistent_hash_bag& other) const {
        if (other.is_empty()) {
            return *this;
        }

        auto counts = counts_;
        auto total = total_count_;
        for (const auto& [item, argument_count] : other.counts_) {
            auto increase = argument_count;
            auto [next, selected] = counts.add_or_update(
                item,
                [argument_count](const T&) { return argument_count; },
                [argument_count, &increase](const T&, multiplicity_type receiver_count) {
                    if (receiver_count >= argument_count) {
                        increase = 0;
                        return receiver_count;
                    }
                    increase = static_cast<multiplicity_type>(argument_count - receiver_count);
                    return argument_count;
                });
            (void)selected;
            counts = std::move(next);
            total = checked_total_add(total, increase);
        }
        return with_counts(std::move(counts), total);
    }

    [[nodiscard]] persistent_hash_bag intersect_normalized(
        const persistent_hash_bag& other) const {
        if (is_empty()) {
            return *this;
        }
        if (other.is_empty()) {
            return clear();
        }

        auto counts = counts_;
        auto total = total_count_;
        for (const auto& [item, receiver_count] : counts_) {
            const auto* argument_count = other.counts_.try_get(item);
            if (argument_count == nullptr) {
                auto [next, removed, value] = counts.try_remove(item);
                if (!removed || !value) {
                    throw std::logic_error(
                        "A located hash-bag entry could not be removed.");
                }
                counts = std::move(next);
                total -= static_cast<total_size_type>(*value);
            } else if (*argument_count < receiver_count) {
                counts = counts.set_item(item, *argument_count);
                total -= static_cast<total_size_type>(receiver_count - *argument_count);
            }
        }
        return with_counts(std::move(counts), total);
    }

    [[nodiscard]] persistent_hash_bag except_normalized(
        const persistent_hash_bag& other) const {
        if (is_empty() || other.is_empty()) {
            return *this;
        }

        auto counts = counts_;
        auto total = total_count_;
        for (const auto& [item, receiver_count] : counts_) {
            const auto* argument_count = other.counts_.try_get(item);
            if (argument_count == nullptr) {
                continue;
            }
            if (*argument_count >= receiver_count) {
                auto [next, removed, value] = counts.try_remove(item);
                if (!removed || !value) {
                    throw std::logic_error(
                        "A located hash-bag entry could not be removed.");
                }
                counts = std::move(next);
                total -= static_cast<total_size_type>(*value);
            } else {
                counts = counts.set_item(
                    item,
                    static_cast<multiplicity_type>(receiver_count - *argument_count));
                total -= static_cast<total_size_type>(*argument_count);
            }
        }
        return with_counts(std::move(counts), total);
    }

    [[nodiscard]] persistent_hash_bag sum_normalized(
        const persistent_hash_bag& other) const {
        if (other.is_empty()) {
            return *this;
        }

        auto counts = counts_;
        auto total = total_count_;
        for (const auto& [item, argument_count] : other.counts_) {
            auto [next, selected] = counts.add_or_update(
                item,
                [argument_count](const T&) { return argument_count; },
                [argument_count](const T&, multiplicity_type receiver_count) {
                    return checked_multiplicity_add(receiver_count, argument_count);
                });
            (void)selected;
            counts = std::move(next);
            total = checked_total_add(total, argument_count);
        }
        return with_counts(std::move(counts), total);
    }

    map_type counts_;
    total_size_type total_count_ = 0;
};

} // namespace durable7::hamt
