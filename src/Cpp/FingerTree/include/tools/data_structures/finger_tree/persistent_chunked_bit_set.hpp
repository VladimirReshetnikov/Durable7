#pragma once

#include <tools/data_structures/finger_tree/measured_finger_tree.hpp>

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tools::data_structures::finger_tree {

struct chunked_bit_set_chunk final {
    std::int32_t word_index = 0;
    std::uint64_t bits = 0;

    [[nodiscard]] bool operator==(const chunked_bit_set_chunk&) const = default;
};

struct chunked_bit_set_annotation final {
    std::size_t chunk_count = 0;
    std::uint64_t pop_count = 0;
    std::optional<std::int32_t> last_word;
};

struct chunked_bit_set_measure final {
    using element_type = chunked_bit_set_chunk;
    using measure_type = chunked_bit_set_annotation;

    [[nodiscard]] static measure_type empty() { return {}; }

    [[nodiscard]] static measure_type measure(const element_type& element)
    {
        return {1, static_cast<std::uint64_t>(std::popcount(element.bits)), element.word_index};
    }

    [[nodiscard]] static measure_type combine(
        const measure_type& left,
        const measure_type& right)
    {
        return {
            checked_add(left.chunk_count, right.chunk_count),
            left.pop_count + right.pop_count,
            right.last_word.has_value() ? right.last_word : left.last_word};
    }
};

/// Immutable sparse nonnegative 32-bit integer set represented by nonzero
/// 64-bit chunks in one measured finger tree. Cached population measures make
/// point lookup, inclusive rank, and zero-based select logarithmic in chunks.
class persistent_chunked_bit_set final {
private:
    using tree_type = finger_tree<chunked_bit_set_chunk, chunked_bit_set_measure>;
    enum class set_operation { union_, intersect, except, symmetric_except };

public:
    using value_type = std::int32_t;
    using size_type = std::uint64_t;

    persistent_chunked_bit_set() = default;

    [[nodiscard]] static persistent_chunked_bit_set empty() { return {}; }

    template <std::ranges::input_range Range>
    [[nodiscard]] static persistent_chunked_bit_set create_range(Range&& bit_indexes)
    {
        auto words = std::map<std::int32_t, std::uint64_t>{};
        for (auto&& source : bit_indexes) {
            const auto bit_index = static_cast<std::int64_t>(source);
            validate_bit_index(bit_index);
            const auto word = static_cast<std::int32_t>(bit_index >> 6);
            words[word] |= std::uint64_t{1} << (bit_index & 63);
        }
        auto chunks = std::vector<chunked_bit_set_chunk>{};
        chunks.reserve(words.size());
        for (const auto& [word, bits] : words) {
            chunks.push_back({word, bits});
        }
        return chunks.empty()
            ? empty()
            : persistent_chunked_bit_set{tree_type::from_range(chunks)};
    }

    [[nodiscard]] size_type count() const { return chunks_.measure().pop_count; }
    [[nodiscard]] std::size_t chunk_count() const { return chunks_.measure().chunk_count; }
    [[nodiscard]] bool is_empty() const noexcept { return chunks_.empty(); }

    [[nodiscard]] bool contains(const value_type bit_index) const
    {
        if (bit_index < 0) {
            return false;
        }
        const auto word = bit_index >> 6;
        const auto located = locate_word(word);
        return located.item != nullptr
            && located.item->word_index == word
            && (located.item->bits
                & (std::uint64_t{1} << (static_cast<unsigned>(bit_index) & 63u))) != 0;
    }

    [[nodiscard]] persistent_chunked_bit_set add(const value_type bit_index) const
    {
        validate_bit_index(bit_index);
        const auto word = bit_index >> 6;
        const auto bit = std::uint64_t{1}
            << (static_cast<unsigned>(bit_index) & 63u);
        auto split = split_at_word(word);
        if (auto view = split.right.try_view_left();
            view.has_value() && view->item.word_index == word) {
            const auto updated = view->item.bits | bit;
            if (updated == view->item.bits) {
                return *this;
            }
            return persistent_chunked_bit_set{
                split.left.append({word, updated}).concat(view->right)};
        }
        return persistent_chunked_bit_set{
            split.left.append({word, bit}).concat(split.right)};
    }

    [[nodiscard]] std::pair<persistent_chunked_bit_set, bool> try_add(
        const value_type bit_index) const
    {
        const auto was_present = contains(bit_index);
        return {add(bit_index), !was_present};
    }

    [[nodiscard]] persistent_chunked_bit_set remove(const value_type bit_index) const
    {
        if (bit_index < 0) {
            return *this;
        }
        const auto word = bit_index >> 6;
        const auto bit = std::uint64_t{1}
            << (static_cast<unsigned>(bit_index) & 63u);
        auto split = split_at_word(word);
        auto view = split.right.try_view_left();
        if (!view.has_value() || view->item.word_index != word
            || (view->item.bits & bit) == 0) {
            return *this;
        }
        const auto updated = view->item.bits & ~bit;
        return persistent_chunked_bit_set{
            updated == 0
                ? split.left.concat(view->right)
                : split.left.append({word, updated}).concat(view->right)};
    }

    [[nodiscard]] std::pair<persistent_chunked_bit_set, bool> try_remove(
        const value_type bit_index) const
    {
        const auto was_present = contains(bit_index);
        return {remove(bit_index), was_present};
    }

    /// Counts set bits whose indexes are less than or equal to bit_index.
    [[nodiscard]] size_type rank(const value_type bit_index) const
    {
        if (bit_index < 0) {
            return 0;
        }
        const auto word = bit_index >> 6;
        const auto located = locate_word(word);
        if (located.item == nullptr || located.item->word_index != word) {
            return located.measure_before.pop_count;
        }
        const auto offset = static_cast<unsigned>(bit_index) & 63u;
        const auto mask = offset == 63u
            ? (std::numeric_limits<std::uint64_t>::max)()
            : (std::uint64_t{1} << (offset + 1u)) - 1u;
        return located.measure_before.pop_count
            + static_cast<std::uint64_t>(std::popcount(located.item->bits & mask));
    }

    [[nodiscard]] std::optional<value_type> try_select(const size_type rank) const
    {
        if (rank >= count()) {
            return std::nullopt;
        }
        const auto located = chunks_.try_locate_reference([rank](const auto& annotation) {
            return annotation.pop_count > rank;
        });
        if (located.item == nullptr) {
            return std::nullopt;
        }
        auto bits = located.item->bits;
        const auto local_rank = rank - located.measure_before.pop_count;
        for (auto index = size_type{0}; index != local_rank; ++index) {
            bits &= bits - 1;
        }
        const auto offset = std::countr_zero(bits);
        return static_cast<value_type>(
            (static_cast<std::int64_t>(located.item->word_index) << 6) + offset);
    }

    [[nodiscard]] value_type select(const size_type rank) const
    {
        const auto result = try_select(rank);
        if (!result.has_value()) {
            throw std::out_of_range("persistent_chunked_bit_set rank is outside the population");
        }
        return *result;
    }

    [[nodiscard]] persistent_chunked_bit_set union_with(
        const persistent_chunked_bit_set& other) const
    {
        return combine(other, set_operation::union_);
    }
    [[nodiscard]] persistent_chunked_bit_set intersect_with(
        const persistent_chunked_bit_set& other) const
    {
        return combine(other, set_operation::intersect);
    }
    [[nodiscard]] persistent_chunked_bit_set except_with(
        const persistent_chunked_bit_set& other) const
    {
        return this == &other ? empty() : combine(other, set_operation::except);
    }
    [[nodiscard]] persistent_chunked_bit_set symmetric_except_with(
        const persistent_chunked_bit_set& other) const
    {
        return this == &other ? empty() : combine(other, set_operation::symmetric_except);
    }

    [[nodiscard]] persistent_chunked_bit_set clear() const
    {
        return is_empty() ? *this : empty();
    }

    [[nodiscard]] std::vector<value_type> to_vector() const
    {
        auto result = std::vector<value_type>{};
        result.reserve(static_cast<std::size_t>(count()));
        for (const auto& chunk : chunks_) {
            auto bits = chunk.bits;
            while (bits != 0) {
                const auto offset = std::countr_zero(bits);
                result.push_back(static_cast<value_type>(
                    (static_cast<std::int64_t>(chunk.word_index) << 6) + offset));
                bits &= bits - 1;
            }
        }
        return result;
    }

    void validate_invariants() const
    {
        auto previous = std::int32_t{-1};
        auto chunks = std::size_t{0};
        auto population = size_type{0};
        for (const auto& chunk : chunks_) {
            if (chunk.word_index <= previous || chunk.bits == 0) {
                throw std::logic_error("persistent_chunked_bit_set chunks are not canonical");
            }
            previous = chunk.word_index;
            ++chunks;
            population += static_cast<std::uint64_t>(std::popcount(chunk.bits));
        }
        const auto annotation = chunks_.measure();
        if (annotation.chunk_count != chunks || annotation.pop_count != population
            || (chunks != 0
                && (!annotation.last_word.has_value() || *annotation.last_word != previous))) {
            throw std::logic_error("persistent_chunked_bit_set annotation disagrees");
        }
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
    explicit persistent_chunked_bit_set(tree_type chunks)
        : chunks_(std::move(chunks))
    {
    }

    static void validate_bit_index(const std::int64_t bit_index)
    {
        if (bit_index < 0 || bit_index > (std::numeric_limits<value_type>::max)()) {
            throw std::out_of_range("persistent_chunked_bit_set index must be nonnegative int32");
        }
    }

    [[nodiscard]] finger_tree_split<chunked_bit_set_chunk, chunked_bit_set_measure>
    split_at_word(const std::int32_t word) const
    {
        return chunks_.split([word](const auto& annotation) {
            return annotation.last_word.has_value() && *annotation.last_word >= word;
        });
    }

    [[nodiscard]] finger_tree_locate_reference_result<
        chunked_bit_set_chunk, chunked_bit_set_measure>
    locate_word(const std::int32_t word) const
    {
        return chunks_.try_locate_reference([word](const auto& annotation) {
            return annotation.last_word.has_value() && *annotation.last_word >= word;
        });
    }

    [[nodiscard]] persistent_chunked_bit_set combine(
        const persistent_chunked_bit_set& other,
        const set_operation operation) const
    {
        auto left = chunks_.begin();
        const auto left_end = chunks_.end();
        auto right = other.chunks_.begin();
        const auto right_end = other.chunks_.end();
        auto chunks = std::vector<chunked_bit_set_chunk>{};
        while (left != left_end || right != right_end) {
            if (right == right_end
                || (left != left_end && left->word_index < right->word_index)) {
                if (operation == set_operation::union_
                    || operation == set_operation::except
                    || operation == set_operation::symmetric_except) {
                    chunks.push_back(*left);
                }
                ++left;
                continue;
            }
            if (left == left_end || right->word_index < left->word_index) {
                if (operation == set_operation::union_
                    || operation == set_operation::symmetric_except) {
                    chunks.push_back(*right);
                }
                ++right;
                continue;
            }
            const auto bits = operation == set_operation::union_
                ? left->bits | right->bits
                : operation == set_operation::intersect
                    ? left->bits & right->bits
                    : operation == set_operation::except
                        ? left->bits & ~right->bits
                        : left->bits ^ right->bits;
            if (bits != 0) {
                chunks.push_back({left->word_index, bits});
            }
            ++left;
            ++right;
        }
        if (chunks.empty()) {
            return empty();
        }
        if (chunks.size() == chunk_count()
            && std::equal(chunks.begin(), chunks.end(), chunks_.begin(), chunks_.end())) {
            return *this;
        }
        return persistent_chunked_bit_set{tree_type::from_range(chunks)};
    }

    tree_type chunks_;
};

} // namespace tools::data_structures::finger_tree
