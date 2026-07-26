/// A run of rope elements stored as one leaf.

#pragma once

#include <durable7/finger_tree/detail/common.hpp>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace durable7::finger_tree::detail {

/// A run of elements stored as one leaf.
template <class T>
class rope_chunk final {
public:
    using value_type = T;
    using storage_type = std::vector<value_type>;
    using storage_pointer = std::shared_ptr<const storage_type>;
    using size_type = std::size_t;

    /// An empty chunk.
    rope_chunk() = default;

    /// Copies the source's contents in.
    [[nodiscard]] static rope_chunk copy_from(std::span<const value_type> values)
    {
        auto storage = std::make_shared<const storage_type>(values.begin(), values.end());
        return rope_chunk{std::move(storage), 0, values.size()};
    }

    /// A chunk holding a vector's elements.
    [[nodiscard]] static rope_chunk from_vector(storage_type values)
    {
        auto size = values.size();
        auto storage = std::make_shared<const storage_type>(std::move(values));
        return rope_chunk{std::move(storage), 0, size};
    }

    /// Wraps an already-built representation, without revalidating it.
    [[nodiscard]] static rope_chunk from_storage(storage_pointer storage)
    {
        if (storage == nullptr) {
            throw std::invalid_argument("rope chunk storage cannot be null");
        }

        auto size = storage->size();
        return rope_chunk{std::move(storage), 0, size};
    }

    /// Wraps an already-built representation, without revalidating it.
    [[nodiscard]] static rope_chunk from_storage(storage_pointer storage, size_type offset, size_type length)
    {
        if (storage == nullptr) {
            throw std::invalid_argument("rope chunk storage cannot be null");
        }

        if (offset > storage->size() || length > storage->size() - offset) {
            throw std::out_of_range("rope chunk slice is outside the backing storage");
        }

        return rope_chunk{std::move(storage), offset, length};
    }

    /// Whether the chunk holds no elements.
    [[nodiscard]] bool empty() const noexcept
    {
        return length_ == 0;
    }

    /// Number of elements.
    [[nodiscard]] size_type length() const noexcept
    {
        return length_;
    }

    /// How large the backing storage is, which may exceed the element count.
    [[nodiscard]] size_type backing_size() const noexcept
    {
        return storage_ == nullptr ? 0 : storage_->size();
    }

    /// The underlying representation.
    [[nodiscard]] const storage_pointer& storage() const noexcept
    {
        return storage_;
    }

    /// A non-owning view over the contents.
    [[nodiscard]] std::span<const value_type> view() const noexcept
    {
        // A default-constructed chunk has no storage; keep noexcept honest.
        if (storage_ == nullptr) {
            return {};
        }

        return std::span<const value_type>{storage_->data() + offset_, length_};
    }

    [[nodiscard]] const value_type& operator[](const size_type offset) const
    {
        return (*storage_)[offset_ + offset];
    }

    /// The elements in the given range.
    [[nodiscard]] rope_chunk slice(const size_type offset, const size_type length) const
    {
        if (offset > length_ || length > length_ - offset) {
            throw std::out_of_range("rope chunk slice is outside the chunk");
        }

        return rope_chunk{storage_, offset_ + offset, length};
    }

    /// A chunk with the element at the position replaced.
    [[nodiscard]] rope_chunk set_at(const size_type offset, value_type value) const
    {
        if (offset >= length_) {
            throw std::out_of_range("rope chunk set offset is outside the chunk");
        }

        auto values = storage_type{};
        copy_to(values);
        values[offset] = std::move(value);
        return from_vector(std::move(values));
    }

    /// A chunk with the element inserted at the position.
    [[nodiscard]] rope_chunk insert_at(const size_type offset, value_type value) const
    {
        if (offset > length_) {
            throw std::out_of_range("rope chunk insert offset is outside the chunk");
        }

        auto values = storage_type{};
        values.reserve(checked_add(length_, size_type{1}));
        auto current = view();
        values.insert(values.end(), current.begin(), current.begin() + static_cast<std::ptrdiff_t>(offset));
        values.push_back(std::move(value));
        values.insert(values.end(), current.begin() + static_cast<std::ptrdiff_t>(offset), current.end());
        return from_vector(std::move(values));
    }

    /// A chunk without the element at the position.
    [[nodiscard]] rope_chunk remove_at(const size_type offset) const
    {
        if (offset >= length_) {
            throw std::out_of_range("rope chunk remove offset is outside the chunk");
        }

        auto values = storage_type{};
        values.reserve(length_ - 1);
        auto current = view();
        values.insert(values.end(), current.begin(), current.begin() + static_cast<std::ptrdiff_t>(offset));
        values.insert(values.end(), current.begin() + static_cast<std::ptrdiff_t>(offset + 1), current.end());
        return from_vector(std::move(values));
    }

    /// The concatenation of two chunks, sharing both operands' unchanged structure.
    [[nodiscard]] static rope_chunk concat(const rope_chunk& left, const rope_chunk& right)
    {
        auto values = storage_type{};
        values.reserve(checked_add(left.length(), right.length()));
        left.copy_to(values);
        right.copy_to(values);
        return from_vector(std::move(values));
    }

    /// Copies the elements into the destination.
    void copy_to(storage_type& sink) const
    {
        auto current = view();
        sink.insert(sink.end(), current.begin(), current.end());
    }

    /// Copies the elements into the destination.
    void copy_to(std::span<value_type> destination) const
    {
        auto current = view();
        std::copy(current.begin(), current.end(), destination.begin());
    }

private:
    rope_chunk(storage_pointer storage, size_type offset, size_type length)
        : storage_(std::move(storage))
        , offset_(offset)
        , length_(length)
    {
    }

    storage_pointer storage_;
    size_type offset_ = 0;
    size_type length_ = 0;
};

/// Measures a rope chunk by how many elements it holds, so the rope stays indexable while storing
/// elements in runs.
template <class T>
struct rope_chunk_length_measure {
    using element_type = rope_chunk<T>;
    using measure_type = std::size_t;

    /// The identity: the measure of an empty tree.
    [[nodiscard]] static constexpr measure_type empty() noexcept
    {
        return 0;
    }

    /// The measure of one element.
    [[nodiscard]] static measure_type measure(const element_type& chunk) noexcept
    {
        return chunk.length();
    }

    /// Combines two measures in order. Must be associative; it need not be commutative.
    [[nodiscard]] static measure_type combine(const measure_type left, const measure_type right)
    {
        return checked_add(left, right);
    }
};

/// The predicate turning a character index into a measured search.
struct rope_index_predicate final {
    std::size_t index;

    [[nodiscard]] constexpr bool operator()(const std::size_t cumulative_count) const noexcept
    {
        return cumulative_count > index;
    }
};

} // namespace durable7::finger_tree::detail
