#pragma once

#include <cstddef>

namespace durable7::finger_tree::benchmarks {

class allocation_counting_scope final {
public:
    explicit allocation_counting_scope(bool enabled) noexcept;
    allocation_counting_scope(const allocation_counting_scope&) = delete;
    allocation_counting_scope& operator=(const allocation_counting_scope&) = delete;
    ~allocation_counting_scope();

    [[nodiscard]] std::size_t allocations() const noexcept;
    [[nodiscard]] std::size_t bytes_allocated() const noexcept;

private:
    bool enabled_;
};

} // namespace durable7::finger_tree::benchmarks
