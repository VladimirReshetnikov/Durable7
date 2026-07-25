#ifndef DURABLE7_FINGER_TREE_C_DABA_LITE_H
#define DURABLE7_FINGER_TREE_C_DABA_LITE_H

#include <durable7/finger_tree/fingertree.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* (*ft_daba_allocate_fn)(size_t size, void* context);
typedef void (*ft_daba_deallocate_fn)(void* allocation, void* context);

typedef struct ft_daba_allocator {
    ft_daba_allocate_fn allocate;
    ft_daba_deallocate_fn deallocate;
    void* context;
} ft_daba_allocator;

/* DABA Lite stores monoid values directly. The value and measure sizes must
 * therefore be equal. The measure callback is retained for policy vocabulary
 * parity with the measured-tree family but is not invoked. */
typedef struct ft_daba_policy {
    ft_value_type value;
    ft_measure_policy monoid;
    ft_daba_allocator allocator;
} ft_daba_policy;

/* Callback-produced values obey ft_value_type ownership: copy, identity, and
 * combine construct independent owned values. The implementation transfers
 * those values between private type-erased buffers by byte move and destroys
 * each exactly once. Such values must be relocatable as ordinary C objects;
 * handles and internal values must never be duplicated by shallow copying. */

typedef struct ft_daba_lite_rep ft_daba_lite_rep;

/* Mutable, non-thread-safe handle. A policy and its callback contexts must
 * outlive the handle. Callbacks must return normally and must not reenter the
 * same handle; an operation may have private scratch or provisional storage
 * live before its logical state is published. DABA Lite deliberately exposes
 * no iteration surface: incremental reversal replaces some input values with
 * partial aggregates. */
typedef struct ft_daba_lite {
    const ft_daba_policy* policy;
    ft_daba_lite_rep* rep;
} ft_daba_lite;

typedef struct ft_daba_lite_statistics {
    size_t count;
    size_t front_length;
    size_t back_length;
    size_t left_length;
    size_t right_length;
    size_t accumulator_length;
    size_t block_count;
    size_t allocated_slot_capacity;
    size_t slack_slot_count;
} ft_daba_lite_statistics;

void ft_daba_policy_init(
    ft_daba_policy* policy,
    const ft_value_type* value_type,
    const ft_measure_policy* monoid);

ft_status ft_daba_lite_create(ft_daba_lite* daba, const ft_daba_policy* policy);
/* Transfers a handle into an uninitialized or disposed destination. */
void ft_daba_lite_move(ft_daba_lite* destination, ft_daba_lite* source);
void ft_daba_lite_destroy(ft_daba_lite* daba);

size_t ft_daba_lite_size(const ft_daba_lite* daba);
bool ft_daba_lite_empty(const ft_daba_lite* daba);

/* Constructs an owned aggregate value in an uninitialized destination. The
 * caller releases it through policy.value.destroy when that callback is set,
 * matching the workspace's other type-erased output conventions. Empty
 * queries invoke identity and no combine; nonempty queries invoke combine
 * exactly once. */
ft_status ft_daba_lite_aggregate(const ft_daba_lite* daba, void* destination);

/* Insert invokes combine at most three times. All callback-derived values and
 * any successor block are prepared before the logical window is published. */
ft_status ft_daba_lite_insert(ft_daba_lite* daba, const void* value);

/* Evict invokes combine at most twice. */
ft_status ft_daba_lite_evict(ft_daba_lite* daba);
ft_status ft_daba_lite_try_evict(ft_daba_lite* daba, bool* evicted);

/* Deterministically destroys n live positions and c active blocks. This is
 * O(n + c), unlike the O(1) tracing-GC reset used by the C# implementation. */
ft_status ft_daba_lite_clear(ft_daba_lite* daba);

/* Callback-free structural validation. Returns false for malformed handles,
 * links, occupancy, cursor order, size equations, or arithmetic overflow. */
bool ft_daba_lite_validate(
    const ft_daba_lite* daba,
    ft_daba_lite_statistics* statistics);

#ifdef __cplusplus
}
#endif

#endif
