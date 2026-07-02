#include "test_support/allocation_counter.hpp"

#include <cstdlib>
#include <new>

namespace tools::data_structures::finger_tree::tests {
namespace {

std::atomic<bool> g_enabled = false;
std::atomic<std::size_t> g_allocations = 0;
std::atomic<std::size_t> g_deallocations = 0;
std::atomic<std::size_t> g_bytes_allocated = 0;

} // namespace

void record_test_allocation(const std::size_t size) noexcept
{
    if (g_enabled.load()) {
        g_allocations.fetch_add(1);
        g_bytes_allocated.fetch_add(size);
    }
}

void record_test_deallocation() noexcept
{
    if (g_enabled.load()) {
        g_deallocations.fetch_add(1);
    }
}

void allocation_counter::reset() noexcept
{
    g_allocations.store(0);
    g_deallocations.store(0);
    g_bytes_allocated.store(0);
}

void allocation_counter::set_enabled(const bool enabled) noexcept
{
    g_enabled.store(enabled);
}

std::size_t allocation_counter::allocations() noexcept
{
    return g_allocations.load();
}

std::size_t allocation_counter::deallocations() noexcept
{
    return g_deallocations.load();
}

std::size_t allocation_counter::bytes_allocated() noexcept
{
    return g_bytes_allocated.load();
}

allocation_counting_scope::allocation_counting_scope() noexcept
{
    allocation_counter::reset();
    allocation_counter::set_enabled(true);
}

allocation_counting_scope::~allocation_counting_scope()
{
    allocation_counter::set_enabled(false);
}

std::size_t allocation_counting_scope::allocations() const noexcept
{
    return allocation_counter::allocations();
}

std::size_t allocation_counting_scope::deallocations() const noexcept
{
    return allocation_counter::deallocations();
}

std::size_t allocation_counting_scope::bytes_allocated() const noexcept
{
    return allocation_counter::bytes_allocated();
}

} // namespace tools::data_structures::finger_tree::tests

void* operator new(const std::size_t size)
{
    const auto actual_size = size == 0 ? std::size_t{1} : size;
    if (void* const memory = std::malloc(actual_size)) {
        tools::data_structures::finger_tree::tests::record_test_allocation(actual_size);
        return memory;
    }

    throw std::bad_alloc();
}

void* operator new[](const std::size_t size)
{
    return ::operator new(size);
}

void operator delete(void* const memory) noexcept
{
    if (memory != nullptr) {
        tools::data_structures::finger_tree::tests::record_test_deallocation();
    }
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
