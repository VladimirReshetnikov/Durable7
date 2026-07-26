/// Counts allocations and bytes, so a test can assert on allocation behavior.

#pragma once

#include <atomic>
#include <cstddef>

namespace durable7::finger_tree::tests {

/// Counts allocations and bytes, so a benchmark can report allocation behavior rather than only
/// time.
class allocation_counter final {
public:
    /// Clears the counts.
    static void reset() noexcept;
    /// Turns the facility on or off.
    static void set_enabled(bool enabled) noexcept;

    /// How many allocations have been counted.
    [[nodiscard]] static std::size_t allocations() noexcept;
    /// How many deallocations have been counted.
    [[nodiscard]] static std::size_t deallocations() noexcept;
    /// How many bytes have been allocated.
    [[nodiscard]] static std::size_t bytes_allocated() noexcept;
};

/// Installs the allocation counter for the duration of a scope and restores the previous state on
/// exit.
class allocation_counting_scope final {
public:
    /// Takes a second handle on the same collection version; the nodes are shared, not copied.
    allocation_counting_scope() noexcept;
    /// Takes a second handle on the same collection version; the nodes are shared, not copied.
    allocation_counting_scope(const allocation_counting_scope&) = delete;
    allocation_counting_scope& operator=(const allocation_counting_scope&) = delete;
    /// Constructs the allocation counting scope from the given parts.
    ~allocation_counting_scope();

    /// How many allocations have been counted.
    [[nodiscard]] std::size_t allocations() const noexcept;
    /// How many deallocations have been counted.
    [[nodiscard]] std::size_t deallocations() const noexcept;
    /// How many bytes have been allocated.
    [[nodiscard]] std::size_t bytes_allocated() const noexcept;
};

} // namespace durable7::finger_tree::tests
