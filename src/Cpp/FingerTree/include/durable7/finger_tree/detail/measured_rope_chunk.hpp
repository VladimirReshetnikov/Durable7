/// A run of rope elements stored as one leaf, together with its combined measure.

#pragma once

#include <durable7/finger_tree/detail/common.hpp>
#include <durable7/finger_tree/detail/rope_chunk.hpp>
#include <durable7/finger_tree/measures.hpp>
#include <durable7/finger_tree/product_measure.hpp>

#include <cstddef>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace durable7::finger_tree::detail {

/// A run of elements stored as one leaf, together with its combined measure.
template <class T, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, T>
class measured_rope_chunk final {
public:
    using value_type = T;
    using measure_policy = MeasurePolicy;
    using measure_type = typename measure_policy::measure_type;
    using chunk_type = rope_chunk<value_type>;
    using storage_type = typename chunk_type::storage_type;
    using storage_pointer = typename chunk_type::storage_pointer;
    using size_type = std::size_t;

    /// An empty chunk.
    measured_rope_chunk() = default;

    /// Constructs the measured rope chunk from the given parts.
    explicit measured_rope_chunk(chunk_type chunk)
        : chunk_(std::move(chunk))
        , measure_(compute_measure(chunk_))
    {
    }

    /// Constructs the measured rope chunk from the given parts.
    measured_rope_chunk(chunk_type chunk, measure_type measure)
        : chunk_(std::move(chunk))
        , measure_(std::move(measure))
    {
    }

    /// Copies the source's contents in.
    [[nodiscard]] static measured_rope_chunk copy_from(std::span<const value_type> values)
    {
        return measured_rope_chunk{chunk_type::copy_from(values)};
    }

    /// A chunk holding a vector's elements.
    [[nodiscard]] static measured_rope_chunk from_vector(storage_type values)
    {
        return measured_rope_chunk{chunk_type::from_vector(std::move(values))};
    }

    /// Wraps an already-built representation, without revalidating it.
    [[nodiscard]] static measured_rope_chunk from_storage(storage_pointer storage)
    {
        return measured_rope_chunk{chunk_type::from_storage(std::move(storage))};
    }

    /// Wraps an already-built representation, without revalidating it.
    [[nodiscard]] static measured_rope_chunk from_storage(
        storage_pointer storage,
        const size_type offset,
        const size_type length)
    {
        return measured_rope_chunk{chunk_type::from_storage(std::move(storage), offset, length)};
    }

    /// Whether the chunk holds no elements.
    [[nodiscard]] bool empty() const noexcept
    {
        return chunk_.empty();
    }

    /// Number of elements.
    [[nodiscard]] size_type length() const noexcept
    {
        return chunk_.length();
    }

    /// The combined measure of every element, read from the cached root measure.
    [[nodiscard]] const measure_type& measure() const noexcept
    {
        return measure_;
    }

    /// The chunk this value holds.
    [[nodiscard]] const chunk_type& chunk() const noexcept
    {
        return chunk_;
    }

    /// A non-owning view over the contents.
    [[nodiscard]] std::span<const value_type> view() const noexcept
    {
        return chunk_.view();
    }

    [[nodiscard]] const value_type& operator[](const size_type offset) const
    {
        return chunk_[offset];
    }

    /// The elements in the given range.
    [[nodiscard]] measured_rope_chunk slice(const size_type offset, const size_type length) const
    {
        return measured_rope_chunk{chunk_.slice(offset, length)};
    }

    /// A chunk with the element at the position replaced.
    [[nodiscard]] measured_rope_chunk set_at(const size_type offset, value_type value) const
    {
        return measured_rope_chunk{chunk_.set_at(offset, std::move(value))};
    }

    /// A chunk with the element inserted at the position.
    [[nodiscard]] measured_rope_chunk insert_at(const size_type offset, value_type value) const
    {
        return measured_rope_chunk{chunk_.insert_at(offset, std::move(value))};
    }

    /// A chunk without the element at the position.
    [[nodiscard]] measured_rope_chunk remove_at(const size_type offset) const
    {
        return measured_rope_chunk{chunk_.remove_at(offset)};
    }

    /// The concatenation of two chunks, sharing both operands' unchanged structure.
    [[nodiscard]] static measured_rope_chunk concat(
        const measured_rope_chunk& left,
        const measured_rope_chunk& right)
    {
        return measured_rope_chunk{
            chunk_type::concat(left.chunk_, right.chunk_),
            measure_policy::combine(left.measure_, right.measure_)};
    }

    /// The combined measure of the elements before the position.
    [[nodiscard]] measure_type measure_prefix(const size_type length) const
    {
        if (length > this->length()) {
            throw std::out_of_range("measured rope chunk prefix is outside the chunk");
        }

        auto measure = measure_policy::empty();
        auto current = view();
        for (auto index = size_type{0}; index < length; ++index) {
            measure = measure_policy::combine(measure, measure_policy::measure(current[index]));
        }

        return measure;
    }

    /// Copies the elements into the destination.
    void copy_to(std::vector<value_type>& sink) const
    {
        chunk_.copy_to(sink);
    }

    /// Copies the elements into the destination.
    void copy_to(std::span<value_type> destination) const
    {
        chunk_.copy_to(destination);
    }

    /// Recomputes the measure from the children.
    [[nodiscard]] measure_type recompute_measure() const
    {
        return compute_measure(chunk_);
    }

private:
    [[nodiscard]] static measure_type compute_measure(const chunk_type& chunk)
    {
        auto measure = measure_policy::empty();
        for (const auto& value : chunk.view()) {
            measure = measure_policy::combine(measure, measure_policy::measure(value));
        }

        return measure;
    }

    chunk_type chunk_;
    measure_type measure_ = measure_policy::empty();
};

/// Measures a rope chunk by both its length and the measure of its elements.
template <class T, class MeasurePolicy>
    requires measure_policy<MeasurePolicy, T>
struct measured_rope_chunk_measure {
    using element_type = measured_rope_chunk<T, MeasurePolicy>;
    using measure_type = measure_pair<std::size_t, typename MeasurePolicy::measure_type>;

    /// The identity: the measure of an empty tree.
    [[nodiscard]] static measure_type empty()
    {
        return measure_type{0, MeasurePolicy::empty()};
    }

    /// The measure of one element.
    [[nodiscard]] static measure_type measure(const element_type& chunk)
    {
        return measure_type{chunk.length(), chunk.measure()};
    }

    /// Combines two measures in order. Must be associative; it need not be commutative.
    [[nodiscard]] static measure_type combine(const measure_type& left, const measure_type& right)
    {
        return measure_type{
            checked_add(left.first, right.first),
            MeasurePolicy::combine(left.second, right.second)};
    }
};

} // namespace durable7::finger_tree::detail
