#include "benchmark_allocation.hpp"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>

#ifdef _WIN32
#include <malloc.h>
#endif

namespace tools::data_structures::finger_tree::benchmarks {
namespace {

std::atomic<bool> enabled = false;
std::atomic<std::size_t> allocation_count = 0;
std::atomic<std::size_t> allocated_bytes = 0;

} // namespace

void record_allocation(const std::size_t size) noexcept
{
    if (enabled.load(std::memory_order_relaxed)) {
        allocation_count.fetch_add(1, std::memory_order_relaxed);
        allocated_bytes.fetch_add(size, std::memory_order_relaxed);
    }
}

allocation_counting_scope::allocation_counting_scope(const bool should_enable) noexcept
    : enabled_(should_enable)
{
    if (enabled_) {
        allocation_count.store(0, std::memory_order_relaxed);
        allocated_bytes.store(0, std::memory_order_relaxed);
        enabled.store(true, std::memory_order_relaxed);
    }
}

allocation_counting_scope::~allocation_counting_scope()
{
    if (enabled_) {
        enabled.store(false, std::memory_order_relaxed);
    }
}

std::size_t allocation_counting_scope::allocations() const noexcept
{
    return enabled_ ? allocation_count.load(std::memory_order_relaxed) : 0;
}

std::size_t allocation_counting_scope::bytes_allocated() const noexcept
{
    return enabled_ ? allocated_bytes.load(std::memory_order_relaxed) : 0;
}

} // namespace tools::data_structures::finger_tree::benchmarks

#ifndef FINGERTREE_DISABLE_ALLOCATION_TRACKING
namespace {

[[nodiscard]] std::size_t actual_size(const std::size_t size) noexcept
{
    return size == 0 ? std::size_t{1} : size;
}

[[nodiscard]] void* allocate_unaligned(const std::size_t size) noexcept
{
    const auto requested = actual_size(size);
    if (void* const memory = std::malloc(requested)) {
        tools::data_structures::finger_tree::benchmarks::record_allocation(requested);
        return memory;
    }
    return nullptr;
}

[[nodiscard]] void* allocate_aligned(const std::size_t size, const std::align_val_t alignment) noexcept
{
    const auto requested = actual_size(size);
    auto requested_alignment = static_cast<std::size_t>(alignment);
    if (requested_alignment < alignof(void*)) {
        requested_alignment = alignof(void*);
    }

#ifdef _WIN32
    void* const memory = _aligned_malloc(requested, requested_alignment);
#else
    void* memory = nullptr;
    if (posix_memalign(&memory, requested_alignment, requested) != 0) {
        memory = nullptr;
    }
#endif
    if (memory != nullptr) {
        tools::data_structures::finger_tree::benchmarks::record_allocation(requested);
    }
    return memory;
}

} // namespace

void* operator new(const std::size_t size)
{
    if (void* const memory = allocate_unaligned(size)) {
        return memory;
    }
    throw std::bad_alloc();
}

void* operator new[](const std::size_t size)
{
    return ::operator new(size);
}

void* operator new(const std::size_t size, const std::nothrow_t&) noexcept
{
    return allocate_unaligned(size);
}

void* operator new[](const std::size_t size, const std::nothrow_t&) noexcept
{
    return ::operator new(size, std::nothrow);
}

void* operator new(const std::size_t size, const std::align_val_t alignment)
{
    if (void* const memory = allocate_aligned(size, alignment)) {
        return memory;
    }
    throw std::bad_alloc();
}

void* operator new[](const std::size_t size, const std::align_val_t alignment)
{
    return ::operator new(size, alignment);
}

void* operator new(const std::size_t size, const std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return allocate_aligned(size, alignment);
}

void* operator new[](const std::size_t size, const std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return ::operator new(size, alignment, std::nothrow);
}

void operator delete(void* const memory) noexcept
{
    std::free(memory);
}

void operator delete[](void* const memory) noexcept
{
    ::operator delete(memory);
}

void operator delete(void* const memory, std::size_t) noexcept
{
    ::operator delete(memory);
}

void operator delete[](void* const memory, std::size_t) noexcept
{
    ::operator delete(memory);
}

void operator delete(void* const memory, const std::nothrow_t&) noexcept
{
    ::operator delete(memory);
}

void operator delete[](void* const memory, const std::nothrow_t&) noexcept
{
    ::operator delete(memory);
}

void operator delete(void* const memory, const std::align_val_t) noexcept
{
#ifdef _WIN32
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}

void operator delete[](void* const memory, const std::align_val_t alignment) noexcept
{
    ::operator delete(memory, alignment);
}

void operator delete(void* const memory, std::size_t, const std::align_val_t alignment) noexcept
{
    ::operator delete(memory, alignment);
}

void operator delete[](void* const memory, std::size_t, const std::align_val_t alignment) noexcept
{
    ::operator delete(memory, alignment);
}

void operator delete(void* const memory, const std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    ::operator delete(memory, alignment);
}

void operator delete[](void* const memory, const std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    ::operator delete(memory, alignment);
}
#endif
