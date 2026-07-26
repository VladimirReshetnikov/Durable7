/// Allocation accounting for the benchmarks, so they can report allocations as well as time.

#pragma once

#include <cstddef>

namespace durable7::finger_tree::benchmarks {

/// Installs the allocation counter for the duration of a scope and restores the previous state on
/// exit.
class allocation_counting_scope final {
public:
    /// Takes a second handle on the same collection version; the nodes are shared, not copied.
    explicit allocation_counting_scope(bool enabled) noexcept;
    /// Takes a second handle on the same collection version; the nodes are shared, not copied.
    allocation_counting_scope(const allocation_counting_scope&) = delete;
    allocation_counting_scope& operator=(const allocation_counting_scope&) = delete;
    /// Constructs the allocation counting scope from the given parts.
    ~allocation_counting_scope();

    /// How many allocations have been counted.
    [[nodiscard]] std::size_t allocations() const noexcept;
    /// How many bytes have been allocated.
    [[nodiscard]] std::size_t bytes_allocated() const noexcept;

private:
    bool enabled_;
};

} // namespace durable7::finger_tree::benchmarks
