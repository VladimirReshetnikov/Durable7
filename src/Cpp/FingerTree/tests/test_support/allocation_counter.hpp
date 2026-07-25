#pragma once

#include <atomic>
#include <cstddef>

namespace durable7::finger_tree::tests {

class allocation_counter final {
public:
    static void reset() noexcept;
    static void set_enabled(bool enabled) noexcept;

    [[nodiscard]] static std::size_t allocations() noexcept;
    [[nodiscard]] static std::size_t deallocations() noexcept;
    [[nodiscard]] static std::size_t bytes_allocated() noexcept;
};

class allocation_counting_scope final {
public:
    allocation_counting_scope() noexcept;
    allocation_counting_scope(const allocation_counting_scope&) = delete;
    allocation_counting_scope& operator=(const allocation_counting_scope&) = delete;
    ~allocation_counting_scope();

    [[nodiscard]] std::size_t allocations() const noexcept;
    [[nodiscard]] std::size_t deallocations() const noexcept;
    [[nodiscard]] std::size_t bytes_allocated() const noexcept;
};

} // namespace durable7::finger_tree::tests
