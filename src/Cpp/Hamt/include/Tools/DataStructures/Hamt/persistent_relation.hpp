#pragma once

#include "persistent_hash_multimap.hpp"

#include <concepts>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tools::data_structures::hamt {

/// Immutable many-to-many relation backed by mutually inverse hash multimaps.
///
/// Addition normalizes each domain to one global first representative before
/// touching either index. inverse() swaps existing root-sharing values in O(1).
template <
    class Left,
    class Right,
    class LeftHash = std::hash<Left>,
    class LeftEqual = std::equal_to<Left>,
    class RightHash = std::hash<Right>,
    class RightEqual = std::equal_to<Right>>
    requires std::copyable<Left>
        && std::copyable<Right>
        && std::copyable<LeftHash>
        && std::copyable<LeftEqual>
        && std::copyable<RightHash>
        && std::copyable<RightEqual>
class persistent_relation final {
private:
    using forward_type = persistent_hash_multimap<
        Left, Right, LeftHash, LeftEqual, RightHash, RightEqual>;
    using reverse_type = persistent_hash_multimap<
        Right, Left, RightHash, RightEqual, LeftHash, LeftEqual>;

    // Only the swapped specialization needs the private root-adopting constructor.
    // Friending the unconstrained primary template is not a matching redeclaration
    // of this constrained template (and Clang correctly rejects it).
    friend class persistent_relation<
        Right, Left, RightHash, RightEqual, LeftHash, LeftEqual>;

public:
    using value_type = std::pair<Left, Right>;
    using size_type = std::size_t;
    using inverse_type = persistent_relation<
        Right, Left, RightHash, RightEqual, LeftHash, LeftEqual>;

    persistent_relation() = default;

    [[nodiscard]] static persistent_relation empty()
    {
        return {};
    }

    [[nodiscard]] static persistent_relation create(
        LeftHash left_hash = {},
        LeftEqual left_equal = {},
        RightHash right_hash = {},
        RightEqual right_equal = {})
    {
        return persistent_relation{
            forward_type::create(left_hash, left_equal, right_hash, right_equal),
            reverse_type::create(right_hash, right_equal, left_hash, left_equal)};
    }

    template <class Range>
    [[nodiscard]] static persistent_relation create_range(
        const Range& pairs,
        LeftHash left_hash = {},
        LeftEqual left_equal = {},
        RightHash right_hash = {},
        RightEqual right_equal = {})
    {
        auto result = create(
            std::move(left_hash),
            std::move(left_equal),
            std::move(right_hash),
            std::move(right_equal));
        for (const auto& [left, right] : pairs) {
            result = result.add(left, right);
        }
        return result;
    }

    [[nodiscard]] std::int64_t pair_count() const noexcept
    {
        return forward_.pair_count();
    }

    [[nodiscard]] size_type left_count() const noexcept
    {
        return forward_.key_count();
    }

    [[nodiscard]] size_type right_count() const noexcept
    {
        return reverse_.key_count();
    }

    [[nodiscard]] bool is_empty() const noexcept
    {
        return forward_.is_empty();
    }

    [[nodiscard]] bool contains(const Left& left, const Right& right) const
    {
        return forward_.contains(left, right);
    }

    [[nodiscard]] bool contains_left(const Left& left) const
    {
        return forward_.contains_key(left);
    }

    [[nodiscard]] bool contains_right(const Right& right) const
    {
        return reverse_.contains_key(right);
    }

    [[nodiscard]] typename forward_type::value_set rights_or_empty(const Left& left) const
    {
        return forward_.values_or_empty(left);
    }

    [[nodiscard]] typename reverse_type::value_set lefts_or_empty(const Right& right) const
    {
        return reverse_.values_or_empty(right);
    }

    [[nodiscard]] persistent_relation add(const Left& left, const Right& right) const
    {
        const auto* known_left = forward_.try_get_key(left);
        const auto* known_right = reverse_.try_get_key(right);
        const auto& stored_left = known_left == nullptr ? left : *known_left;
        const auto& stored_right = known_right == nullptr ? right : *known_right;
        if (forward_.contains(stored_left, stored_right)) {
            return *this;
        }
        return persistent_relation{
            forward_.add(stored_left, stored_right),
            reverse_.add(stored_right, stored_left)};
    }

    [[nodiscard]] persistent_relation remove(const Left& left, const Right& right) const
    {
        const auto* stored_left = forward_.try_get_key(left);
        const auto* stored_right = reverse_.try_get_key(right);
        if (stored_left == nullptr || stored_right == nullptr
            || !forward_.contains(*stored_left, *stored_right)) {
            return *this;
        }
        return persistent_relation{
            forward_.remove(*stored_left, *stored_right),
            reverse_.remove(*stored_right, *stored_left)};
    }

    [[nodiscard]] persistent_relation remove_left(const Left& left) const
    {
        const auto* stored_left = forward_.try_get_key(left);
        const auto* rights = forward_.try_get_values(left);
        if (stored_left == nullptr || rights == nullptr) {
            return *this;
        }
        auto reverse = reverse_;
        for (const auto& right : *rights) {
            reverse = reverse.remove(right, *stored_left);
        }
        return persistent_relation{forward_.remove_key(*stored_left), std::move(reverse)};
    }

    [[nodiscard]] persistent_relation remove_right(const Right& right) const
    {
        const auto* stored_right = reverse_.try_get_key(right);
        const auto* lefts = reverse_.try_get_values(right);
        if (stored_right == nullptr || lefts == nullptr) {
            return *this;
        }
        auto forward = forward_;
        for (const auto& left : *lefts) {
            forward = forward.remove(left, *stored_right);
        }
        return persistent_relation{std::move(forward), reverse_.remove_key(*stored_right)};
    }

    [[nodiscard]] persistent_relation clear() const
    {
        return is_empty()
            ? *this
            : persistent_relation{forward_.clear(), reverse_.clear()};
    }

    /// Returns an O(1) inverse facade over the already-built roots.
    [[nodiscard]] inverse_type inverse() const
    {
        return inverse_type{reverse_, forward_};
    }

    template <class Function>
        requires std::invocable<Function&, const Left&, const Right&>
    void for_each_pair(Function&& function) const
    {
        forward_.for_each_pair(std::forward<Function>(function));
    }

    [[nodiscard]] std::vector<value_type> to_vector() const
    {
        return forward_.to_vector();
    }

    [[nodiscard]] bool shares_roots_with(const persistent_relation& other) const noexcept
    {
        return forward_.shares_root_with(other.forward_)
            && reverse_.shares_root_with(other.reverse_);
    }

    void validate_invariants() const
    {
        forward_.validate_invariants();
        reverse_.validate_invariants();
        if (forward_.pair_count() != reverse_.pair_count()) {
            throw std::logic_error("persistent_relation directional counts disagree");
        }
        forward_.for_each_pair([this](const Left& left, const Right& right) {
            const auto* reverse_right = reverse_.try_get_key(right);
            const auto* reverse_lefts = reverse_.try_get_values(right);
            const auto* reverse_left = reverse_lefts == nullptr
                ? nullptr
                : reverse_lefts->try_get_value(left);
            if (reverse_right == nullptr || reverse_left == nullptr
                || !std::invoke(reverse_.key_eq(), *reverse_right, right)
                || !std::invoke(reverse_.value_eq(), *reverse_left, left)
                || !same_representative(*reverse_right, right)
                || !same_representative(*reverse_left, left)) {
                throw std::logic_error("persistent_relation representative indexes disagree");
            }
        });
        reverse_.for_each_pair([this](const Right& right, const Left& left) {
            if (!forward_.contains(left, right)) {
                throw std::logic_error("persistent_relation indexes are not inverse");
            }
        });
    }

    [[nodiscard]] bool debug_validate() const noexcept
    {
        try {
            validate_invariants();
            return true;
        } catch (...) {
            return false;
        }
    }

private:
    persistent_relation(forward_type forward, reverse_type reverse)
        : forward_(std::move(forward)), reverse_(std::move(reverse))
    {
    }

    template <class T>
    [[nodiscard]] static bool same_representative(const T& left, const T& right)
    {
        if constexpr (std::equality_comparable<T>) {
            return left == right;
        } else {
            // Copied value-semantic objects have no general identity predicate.
            // Their configured equivalence is checked by the caller above.
            return true;
        }
    }

    forward_type forward_;
    reverse_type reverse_;
};

} // namespace tools::data_structures::hamt
