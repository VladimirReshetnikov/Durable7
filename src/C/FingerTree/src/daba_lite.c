#include <tools/data_structures/finger_tree/daba_lite.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    FT_DABA_BLOCK_CAPACITY = 64,
    /* Worst insert path: inserted aggregate + inserted value + one identity
     * + two final aggregates + two pending slot writes. */
    FT_DABA_MAX_OPERATION_TEMPS = 7,
    FT_DABA_SCRATCH_COUNT = 8,
    FT_DABA_STORAGE_COUNT = 2 + FT_DABA_SCRATCH_COUNT
};

typedef struct ft_daba_block ft_daba_block;

#if defined(_MSC_VER) && !defined(__clang__)
/* The UCRT C <stddef.h> does not declare max_align_t even in /std:c11 mode.
 * The Windows ABI gives long double the maximum fundamental alignment that
 * malloc must support. Other C11 implementations use the standard type. */
typedef long double ft_daba_max_align_t;
#else
typedef max_align_t ft_daba_max_align_t;
#endif

_Static_assert(
    FT_DABA_SCRATCH_COUNT >= FT_DABA_MAX_OPERATION_TEMPS,
    "DABA Lite scratch storage must cover the worst staged fixup plan");

typedef struct ft_daba_cursor {
    ft_daba_block* block;
    size_t index;
} ft_daba_cursor;

struct ft_daba_block {
    ft_daba_block* previous;
    ft_daba_block* next;
    uint64_t occupied;
    void* items;
};

struct ft_daba_lite_rep {
    ft_daba_block* first;
    ft_daba_cursor f;
    ft_daba_cursor l;
    ft_daba_cursor r;
    ft_daba_cursor a;
    ft_daba_cursor b;
    ft_daba_cursor e;
    size_t count;
    size_t stride;
    void* storage;
};

typedef struct ft_daba_temp_pool {
    ft_daba_lite* daba;
    size_t next;
    bool initialized[FT_DABA_SCRATCH_COUNT];
} ft_daba_temp_pool;

typedef struct ft_daba_fixup_plan {
    ft_daba_cursor l;
    ft_daba_cursor r;
    ft_daba_cursor a;
    ft_daba_cursor b;
    size_t aggregate_ra;
    size_t aggregate_b;
    bool has_first_write;
    ft_daba_cursor first_position;
    size_t first_value;
    bool has_second_write;
    ft_daba_cursor second_position;
    size_t second_value;
} ft_daba_fixup_plan;

static void* ft_daba_default_allocate(size_t size, void* context)
{
    (void)context;
    return malloc(size == 0 ? 1 : size);
}

static void ft_daba_default_deallocate(void* allocation, void* context)
{
    (void)context;
    free(allocation);
}

static bool ft_daba_add_overflows(size_t left, size_t right, size_t* result)
{
    if (left > SIZE_MAX - right) {
        return true;
    }
    *result = left + right;
    return false;
}

static bool ft_daba_multiply_overflows(size_t left, size_t right, size_t* result)
{
    if (left != 0 && right > SIZE_MAX / left) {
        return true;
    }
    *result = left * right;
    return false;
}

static bool ft_daba_stride(size_t size, size_t* result)
{
    const size_t alignment = _Alignof(ft_daba_max_align_t);
    size_t rounded = 0;
    if (size == 0 || ft_daba_add_overflows(size, alignment - 1, &rounded)) {
        return false;
    }
    *result = rounded / alignment * alignment;
    return true;
}

static bool ft_daba_policy_valid(const ft_daba_policy* policy, size_t* stride)
{
    size_t computed_stride = 0;
    size_t ignored = 0;
    if (policy == NULL || policy->value.size == 0 || policy->monoid.size != policy->value.size ||
        policy->monoid.identity == NULL || policy->monoid.combine == NULL ||
        policy->allocator.allocate == NULL || policy->allocator.deallocate == NULL ||
        !ft_daba_stride(policy->value.size, &computed_stride) ||
        ft_daba_multiply_overflows(computed_stride, FT_DABA_STORAGE_COUNT, &ignored) ||
        ft_daba_multiply_overflows(computed_stride, FT_DABA_BLOCK_CAPACITY, &ignored)) {
        return false;
    }
    if (stride != NULL) {
        *stride = computed_stride;
    }
    return true;
}

static void* ft_daba_allocate(const ft_daba_policy* policy, size_t size)
{
    return policy->allocator.allocate(size, policy->allocator.context);
}

static void ft_daba_deallocate(const ft_daba_policy* policy, void* allocation)
{
    if (allocation != NULL) {
        policy->allocator.deallocate(allocation, policy->allocator.context);
    }
}

static void ft_daba_value_copy(const ft_daba_policy* policy, void* destination, const void* source)
{
    if (policy->value.copy != NULL) {
        policy->value.copy(destination, source, policy->value.context);
    } else {
        (void)memcpy(destination, source, policy->value.size);
    }
}

static void ft_daba_value_destroy(const ft_daba_policy* policy, void* value)
{
    if (value != NULL && policy->value.destroy != NULL) {
        policy->value.destroy(value, policy->value.context);
    }
}

static void ft_daba_identity(const ft_daba_policy* policy, void* destination)
{
    policy->monoid.identity(destination, policy->monoid.context);
}

static void ft_daba_combine(
    const ft_daba_policy* policy,
    void* destination,
    const void* left,
    const void* right)
{
    policy->monoid.combine(destination, left, right, policy->monoid.context);
}

static void* ft_daba_aggregate_ra(const ft_daba_lite* daba)
{
    return daba->rep->storage;
}

static void* ft_daba_aggregate_b(const ft_daba_lite* daba)
{
    return (unsigned char*)daba->rep->storage + daba->rep->stride;
}

static void* ft_daba_scratch(const ft_daba_lite* daba, size_t index)
{
    return (unsigned char*)daba->rep->storage + (2 + index) * daba->rep->stride;
}

static void* ft_daba_block_item(const ft_daba_lite* daba, ft_daba_block* block, size_t index)
{
    return (unsigned char*)block->items + index * daba->rep->stride;
}

static const void* ft_daba_read(const ft_daba_lite* daba, ft_daba_cursor position)
{
    return ft_daba_block_item(daba, position.block, position.index);
}

static bool ft_daba_cursor_equal(ft_daba_cursor left, ft_daba_cursor right)
{
    return left.block == right.block && left.index == right.index;
}

static bool ft_daba_advance(ft_daba_cursor position, ft_daba_cursor* result)
{
    if (position.block == NULL || position.index >= FT_DABA_BLOCK_CAPACITY || result == NULL) {
        return false;
    }
    if (position.index + 1 < FT_DABA_BLOCK_CAPACITY) {
        result->block = position.block;
        result->index = position.index + 1;
        return true;
    }
    if (position.block->next == NULL) {
        return false;
    }
    result->block = position.block->next;
    result->index = 0;
    return true;
}

static bool ft_daba_retreat(ft_daba_cursor position, ft_daba_cursor* result)
{
    if (position.block == NULL || position.index >= FT_DABA_BLOCK_CAPACITY || result == NULL) {
        return false;
    }
    if (position.index != 0) {
        result->block = position.block;
        result->index = position.index - 1;
        return true;
    }
    if (position.block->previous == NULL) {
        return false;
    }
    result->block = position.block->previous;
    result->index = FT_DABA_BLOCK_CAPACITY - 1;
    return true;
}

static ft_status ft_daba_block_create(ft_daba_lite* daba, ft_daba_block** result)
{
    size_t item_bytes = 0;
    ft_daba_block* block = NULL;
    if (daba == NULL || daba->rep == NULL || result == NULL ||
        ft_daba_multiply_overflows(daba->rep->stride, FT_DABA_BLOCK_CAPACITY, &item_bytes)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    block = (ft_daba_block*)ft_daba_allocate(daba->policy, sizeof(ft_daba_block));
    if (block == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    (void)memset(block, 0, sizeof(*block));
    block->items = ft_daba_allocate(daba->policy, item_bytes);
    if (block->items == NULL) {
        ft_daba_deallocate(daba->policy, block);
        return FT_STATUS_NO_MEMORY;
    }
    *result = block;
    return FT_STATUS_OK;
}

static void ft_daba_block_destroy(ft_daba_lite* daba, ft_daba_block* block)
{
    if (block == NULL) {
        return;
    }
    for (size_t index = 0; index < FT_DABA_BLOCK_CAPACITY; ++index) {
        const uint64_t bit = UINT64_C(1) << index;
        if ((block->occupied & bit) != 0) {
            ft_daba_value_destroy(daba->policy, ft_daba_block_item(daba, block, index));
        }
    }
    ft_daba_deallocate(daba->policy, block->items);
    ft_daba_deallocate(daba->policy, block);
}

static void ft_daba_chain_destroy(ft_daba_lite* daba, ft_daba_block* block)
{
    while (block != NULL) {
        ft_daba_block* next = block->next;
        block->next = NULL;
        ft_daba_block_destroy(daba, block);
        block = next;
    }
}

static void ft_daba_pool_init(ft_daba_temp_pool* pool, ft_daba_lite* daba)
{
    (void)memset(pool, 0, sizeof(*pool));
    pool->daba = daba;
}

static size_t ft_daba_pool_reserve(ft_daba_temp_pool* pool)
{
    const size_t index = pool->next;
    if (index >= FT_DABA_SCRATCH_COUNT) {
        return SIZE_MAX;
    }
    ++pool->next;
    return index;
}

static size_t ft_daba_pool_copy(ft_daba_temp_pool* pool, const void* source)
{
    const size_t index = ft_daba_pool_reserve(pool);
    if (index != SIZE_MAX) {
        ft_daba_value_copy(pool->daba->policy, ft_daba_scratch(pool->daba, index), source);
        pool->initialized[index] = true;
    }
    return index;
}

static size_t ft_daba_pool_identity(ft_daba_temp_pool* pool)
{
    const size_t index = ft_daba_pool_reserve(pool);
    if (index != SIZE_MAX) {
        ft_daba_identity(pool->daba->policy, ft_daba_scratch(pool->daba, index));
        pool->initialized[index] = true;
    }
    return index;
}

static size_t ft_daba_pool_combine(ft_daba_temp_pool* pool, const void* left, const void* right)
{
    const size_t index = ft_daba_pool_reserve(pool);
    if (index != SIZE_MAX) {
        ft_daba_combine(pool->daba->policy, ft_daba_scratch(pool->daba, index), left, right);
        pool->initialized[index] = true;
    }
    return index;
}

static void ft_daba_pool_move(ft_daba_temp_pool* pool, size_t index, void* destination, bool destroy_destination)
{
    if (destroy_destination) {
        ft_daba_value_destroy(pool->daba->policy, destination);
    }
    (void)memcpy(destination, ft_daba_scratch(pool->daba, index), pool->daba->policy->value.size);
    pool->initialized[index] = false;
}

static void ft_daba_pool_cleanup(ft_daba_temp_pool* pool)
{
    for (size_t index = 0; index < pool->next; ++index) {
        if (pool->initialized[index]) {
            ft_daba_value_destroy(pool->daba->policy, ft_daba_scratch(pool->daba, index));
            pool->initialized[index] = false;
        }
    }
}

static bool ft_daba_slot_occupied(ft_daba_cursor position)
{
    return position.block != NULL && position.index < FT_DABA_BLOCK_CAPACITY &&
        (position.block->occupied & (UINT64_C(1) << position.index)) != 0;
}

static void ft_daba_slot_take(ft_daba_temp_pool* pool, ft_daba_cursor position, size_t value, bool overwrite)
{
    const uint64_t bit = UINT64_C(1) << position.index;
    const bool occupied = (position.block->occupied & bit) != 0;
    if (overwrite && occupied) {
        ft_daba_value_destroy(
            pool->daba->policy,
            ft_daba_block_item(pool->daba, position.block, position.index));
    }
    ft_daba_pool_move(
        pool,
        value,
        ft_daba_block_item(pool->daba, position.block, position.index),
        false);
    position.block->occupied |= bit;
}

static void ft_daba_slot_clear(ft_daba_lite* daba, ft_daba_cursor position)
{
    const uint64_t bit = UINT64_C(1) << position.index;
    if ((position.block->occupied & bit) != 0) {
        ft_daba_value_destroy(daba->policy, ft_daba_block_item(daba, position.block, position.index));
        position.block->occupied &= ~bit;
    }
}

static void ft_daba_trim_before(ft_daba_lite* daba, ft_daba_cursor front)
{
    while (daba->rep->first != front.block) {
        ft_daba_block* retired = daba->rep->first;
        daba->rep->first = retired->next;
        retired->next = NULL;
        ft_daba_block_destroy(daba, retired);
    }
    daba->rep->first->previous = NULL;
}

static ft_status ft_daba_compute_fixup(
    ft_daba_lite* daba,
    ft_daba_cursor front,
    ft_daba_cursor end,
    const void* aggregate_ra,
    const void* aggregate_b,
    ft_daba_temp_pool* pool,
    ft_daba_fixup_plan* plan)
{
    ft_daba_cursor l = daba->rep->l;
    ft_daba_cursor r = daba->rep->r;
    ft_daba_cursor a = daba->rep->a;
    ft_daba_cursor b = daba->rep->b;
    const void* next_aggregate_ra = aggregate_ra;
    const void* next_aggregate_b = aggregate_b;
    size_t identity = SIZE_MAX;

    (void)memset(plan, 0, sizeof(*plan));
    if (ft_daba_cursor_equal(front, b)) {
        identity = ft_daba_pool_identity(pool);
        if (identity == SIZE_MAX) {
            return FT_STATUS_INVALID_ARGUMENT;
        }
        plan->aggregate_ra = ft_daba_pool_copy(pool, ft_daba_scratch(daba, identity));
        plan->aggregate_b = ft_daba_pool_copy(pool, ft_daba_scratch(daba, identity));
        plan->l = end;
        plan->r = end;
        plan->a = end;
        plan->b = end;
        return plan->aggregate_ra == SIZE_MAX || plan->aggregate_b == SIZE_MAX
            ? FT_STATUS_INVALID_ARGUMENT
            : FT_STATUS_OK;
    }

    if (ft_daba_cursor_equal(l, b)) {
        identity = ft_daba_pool_identity(pool);
        if (identity == SIZE_MAX) {
            return FT_STATUS_INVALID_ARGUMENT;
        }
        l = front;
        a = end;
        b = end;
        next_aggregate_ra = aggregate_b;
        next_aggregate_b = ft_daba_scratch(daba, identity);
    }

    if (ft_daba_cursor_equal(l, r)) {
        ft_daba_cursor next_l;
        ft_daba_cursor next_r;
        ft_daba_cursor next_a;
        if (!ft_daba_advance(l, &next_l) || !ft_daba_advance(r, &next_r) ||
            !ft_daba_advance(a, &next_a)) {
            return FT_STATUS_INVALID_ARGUMENT;
        }
        if (identity == SIZE_MAX) {
            identity = ft_daba_pool_identity(pool);
        }
        if (identity == SIZE_MAX) {
            return FT_STATUS_INVALID_ARGUMENT;
        }
        plan->aggregate_ra = ft_daba_pool_copy(pool, ft_daba_scratch(daba, identity));
        plan->aggregate_b = ft_daba_pool_copy(pool, next_aggregate_b);
        plan->l = next_l;
        plan->r = next_r;
        plan->a = next_a;
        plan->b = b;
        return plan->aggregate_ra == SIZE_MAX || plan->aggregate_b == SIZE_MAX
            ? FT_STATUS_INVALID_ARGUMENT
            : FT_STATUS_OK;
    }

    {
        ft_daba_cursor next_left;
        ft_daba_cursor before_a;
        const void* product_a = NULL;
        if (!ft_daba_advance(l, &next_left) || !ft_daba_retreat(a, &before_a) ||
            !ft_daba_slot_occupied(l) || !ft_daba_slot_occupied(before_a)) {
            return FT_STATUS_INVALID_ARGUMENT;
        }
        plan->aggregate_ra = ft_daba_pool_copy(pool, next_aggregate_ra);
        plan->aggregate_b = ft_daba_pool_copy(pool, next_aggregate_b);
        plan->first_value = ft_daba_pool_combine(pool, ft_daba_read(daba, l), next_aggregate_ra);
        if (ft_daba_cursor_equal(a, b)) {
            if (identity == SIZE_MAX) {
                identity = ft_daba_pool_identity(pool);
            }
            if (identity == SIZE_MAX) {
                return FT_STATUS_INVALID_ARGUMENT;
            }
            product_a = ft_daba_scratch(daba, identity);
        } else {
            if (!ft_daba_slot_occupied(a)) {
                return FT_STATUS_INVALID_ARGUMENT;
            }
            product_a = ft_daba_read(daba, a);
        }
        plan->second_value = ft_daba_pool_combine(pool, ft_daba_read(daba, before_a), product_a);
        if (plan->aggregate_ra == SIZE_MAX || plan->aggregate_b == SIZE_MAX ||
            plan->first_value == SIZE_MAX || plan->second_value == SIZE_MAX) {
            return FT_STATUS_INVALID_ARGUMENT;
        }
        plan->l = next_left;
        plan->r = r;
        plan->a = before_a;
        plan->b = b;
        plan->has_first_write = true;
        plan->first_position = l;
        plan->has_second_write = true;
        plan->second_position = before_a;
    }
    return FT_STATUS_OK;
}

static void ft_daba_apply_fixup(
    ft_daba_lite* daba,
    ft_daba_temp_pool* pool,
    const ft_daba_fixup_plan* plan)
{
    if (plan->has_first_write) {
        ft_daba_slot_take(pool, plan->first_position, plan->first_value, true);
    }
    if (plan->has_second_write) {
        ft_daba_slot_take(pool, plan->second_position, plan->second_value, true);
    }
    ft_daba_pool_move(pool, plan->aggregate_ra, ft_daba_aggregate_ra(daba), true);
    ft_daba_pool_move(pool, plan->aggregate_b, ft_daba_aggregate_b(daba), true);
    daba->rep->l = plan->l;
    daba->rep->r = plan->r;
    daba->rep->a = plan->a;
    daba->rep->b = plan->b;
}

void ft_daba_policy_init(
    ft_daba_policy* policy,
    const ft_value_type* value_type,
    const ft_measure_policy* monoid)
{
    if (policy == NULL) {
        return;
    }
    (void)memset(policy, 0, sizeof(*policy));
    if (value_type != NULL) {
        policy->value = *value_type;
    }
    if (monoid != NULL) {
        policy->monoid = *monoid;
    }
    policy->allocator.allocate = ft_daba_default_allocate;
    policy->allocator.deallocate = ft_daba_default_deallocate;
}

ft_status ft_daba_lite_create(ft_daba_lite* daba, const ft_daba_policy* policy)
{
    ft_daba_lite temporary = { NULL, NULL };
    ft_daba_lite_rep* rep = NULL;
    ft_daba_block* first = NULL;
    size_t stride = 0;
    size_t storage_bytes = 0;
    if (daba == NULL || !ft_daba_policy_valid(policy, &stride) ||
        ft_daba_multiply_overflows(stride, FT_DABA_STORAGE_COUNT, &storage_bytes)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    rep = (ft_daba_lite_rep*)ft_daba_allocate(policy, sizeof(ft_daba_lite_rep));
    if (rep == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    (void)memset(rep, 0, sizeof(*rep));
    rep->stride = stride;
    rep->storage = ft_daba_allocate(policy, storage_bytes);
    if (rep->storage == NULL) {
        ft_daba_deallocate(policy, rep);
        return FT_STATUS_NO_MEMORY;
    }
    temporary.policy = policy;
    temporary.rep = rep;
    {
        const ft_status block_status = ft_daba_block_create(&temporary, &first);
        if (block_status != FT_STATUS_OK) {
            ft_daba_deallocate(policy, rep->storage);
            ft_daba_deallocate(policy, rep);
            return block_status;
        }
    }
    rep->first = first;
    rep->f.block = first;
    rep->f.index = 0;
    rep->l = rep->f;
    rep->r = rep->f;
    rep->a = rep->f;
    rep->b = rep->f;
    rep->e = rep->f;
    ft_daba_identity(policy, ft_daba_aggregate_ra(&temporary));
    ft_daba_identity(policy, ft_daba_aggregate_b(&temporary));
    *daba = temporary;
    return FT_STATUS_OK;
}

void ft_daba_lite_move(ft_daba_lite* destination, ft_daba_lite* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    *destination = *source;
    source->policy = NULL;
    source->rep = NULL;
}

void ft_daba_lite_destroy(ft_daba_lite* daba)
{
    if (daba == NULL || daba->rep == NULL || daba->policy == NULL) {
        return;
    }
    ft_daba_value_destroy(daba->policy, ft_daba_aggregate_ra(daba));
    ft_daba_value_destroy(daba->policy, ft_daba_aggregate_b(daba));
    ft_daba_chain_destroy(daba, daba->rep->first);
    ft_daba_deallocate(daba->policy, daba->rep->storage);
    ft_daba_deallocate(daba->policy, daba->rep);
    daba->policy = NULL;
    daba->rep = NULL;
}

size_t ft_daba_lite_size(const ft_daba_lite* daba)
{
    return daba == NULL || daba->rep == NULL ? 0 : daba->rep->count;
}

bool ft_daba_lite_empty(const ft_daba_lite* daba)
{
    return ft_daba_lite_size(daba) == 0;
}

ft_status ft_daba_lite_aggregate(const ft_daba_lite* daba, void* destination)
{
    if (daba == NULL || daba->policy == NULL || daba->rep == NULL || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (daba->rep->count == 0) {
        ft_daba_identity(daba->policy, destination);
        return FT_STATUS_OK;
    }
    if (!ft_daba_slot_occupied(daba->rep->f)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_daba_combine(
        daba->policy,
        destination,
        ft_daba_read(daba, daba->rep->f),
        ft_daba_aggregate_b(daba));
    return FT_STATUS_OK;
}

ft_status ft_daba_lite_insert(ft_daba_lite* daba, const void* value)
{
    ft_daba_temp_pool pool;
    ft_daba_fixup_plan plan;
    ft_daba_cursor old_end;
    ft_daba_cursor next_end;
    ft_daba_block* successor = NULL;
    size_t next_aggregate_b = SIZE_MAX;
    size_t stored_value = SIZE_MAX;
    ft_status status = FT_STATUS_OK;
    if (daba == NULL || daba->policy == NULL || daba->rep == NULL || value == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (daba->rep->count == SIZE_MAX) {
        return FT_STATUS_OVERFLOW;
    }
    ft_daba_pool_init(&pool, daba);
    next_aggregate_b = ft_daba_pool_combine(&pool, ft_daba_aggregate_b(daba), value);
    stored_value = ft_daba_pool_copy(&pool, value);
    if (next_aggregate_b == SIZE_MAX || stored_value == SIZE_MAX) {
        ft_daba_pool_cleanup(&pool);
        return FT_STATUS_INVALID_ARGUMENT;
    }

    old_end = daba->rep->e;
    if (old_end.index + 1 < FT_DABA_BLOCK_CAPACITY) {
        next_end.block = old_end.block;
        next_end.index = old_end.index + 1;
    } else {
        if (old_end.block->next != NULL) {
            ft_daba_pool_cleanup(&pool);
            return FT_STATUS_INVALID_ARGUMENT;
        }
        status = ft_daba_block_create(daba, &successor);
        if (status != FT_STATUS_OK) {
            ft_daba_pool_cleanup(&pool);
            return status;
        }
        successor->previous = old_end.block;
        old_end.block->next = successor;
        next_end.block = successor;
        next_end.index = 0;
    }

    if (ft_daba_slot_occupied(old_end)) {
        if (successor != NULL) {
            old_end.block->next = NULL;
            ft_daba_block_destroy(daba, successor);
        }
        ft_daba_pool_cleanup(&pool);
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_daba_slot_take(&pool, old_end, stored_value, false);
    status = ft_daba_compute_fixup(
        daba,
        daba->rep->f,
        next_end,
        ft_daba_aggregate_ra(daba),
        ft_daba_scratch(daba, next_aggregate_b),
        &pool,
        &plan);
    if (status != FT_STATUS_OK) {
        ft_daba_slot_clear(daba, old_end);
        if (successor != NULL) {
            old_end.block->next = NULL;
            successor->previous = NULL;
            ft_daba_block_destroy(daba, successor);
        }
        ft_daba_pool_cleanup(&pool);
        return status;
    }
    ft_daba_apply_fixup(daba, &pool, &plan);
    daba->rep->e = next_end;
    ++daba->rep->count;
    ft_daba_pool_cleanup(&pool);
    return FT_STATUS_OK;
}

ft_status ft_daba_lite_try_evict(ft_daba_lite* daba, bool* evicted)
{
    ft_daba_temp_pool pool;
    ft_daba_fixup_plan plan;
    ft_daba_cursor old_front;
    ft_daba_cursor next_front;
    ft_status status = FT_STATUS_OK;
    if (evicted != NULL) {
        *evicted = false;
    }
    if (daba == NULL || daba->policy == NULL || daba->rep == NULL || evicted == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (daba->rep->count == 0) {
        return FT_STATUS_OK;
    }
    old_front = daba->rep->f;
    if (!ft_daba_advance(old_front, &next_front)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_daba_pool_init(&pool, daba);
    status = ft_daba_compute_fixup(
        daba,
        next_front,
        daba->rep->e,
        ft_daba_aggregate_ra(daba),
        ft_daba_aggregate_b(daba),
        &pool,
        &plan);
    if (status != FT_STATUS_OK) {
        ft_daba_pool_cleanup(&pool);
        return status;
    }
    ft_daba_apply_fixup(daba, &pool, &plan);
    daba->rep->f = next_front;
    --daba->rep->count;
    ft_daba_slot_clear(daba, old_front);
    ft_daba_trim_before(daba, next_front);
    ft_daba_pool_cleanup(&pool);
    *evicted = true;
    return FT_STATUS_OK;
}

ft_status ft_daba_lite_evict(ft_daba_lite* daba)
{
    bool evicted = false;
    const ft_status status = ft_daba_lite_try_evict(daba, &evicted);
    if (status != FT_STATUS_OK) {
        return status;
    }
    return evicted ? FT_STATUS_OK : FT_STATUS_EMPTY;
}

ft_status ft_daba_lite_clear(ft_daba_lite* daba)
{
    ft_daba_temp_pool pool;
    ft_daba_block* replacement = NULL;
    ft_daba_block* retired = NULL;
    size_t identity = SIZE_MAX;
    size_t identity_copy = SIZE_MAX;
    ft_status status = FT_STATUS_OK;
    if (daba == NULL || daba->policy == NULL || daba->rep == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (daba->rep->count == 0) {
        return FT_STATUS_OK;
    }
    ft_daba_pool_init(&pool, daba);
    identity = ft_daba_pool_identity(&pool);
    if (identity == SIZE_MAX) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    identity_copy = ft_daba_pool_copy(&pool, ft_daba_scratch(daba, identity));
    if (identity == SIZE_MAX || identity_copy == SIZE_MAX) {
        ft_daba_pool_cleanup(&pool);
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_daba_block_create(daba, &replacement);
    if (status != FT_STATUS_OK) {
        ft_daba_pool_cleanup(&pool);
        return status;
    }

    retired = daba->rep->first;
    daba->rep->first = replacement;
    daba->rep->f.block = replacement;
    daba->rep->f.index = 0;
    daba->rep->l = daba->rep->f;
    daba->rep->r = daba->rep->f;
    daba->rep->a = daba->rep->f;
    daba->rep->b = daba->rep->f;
    daba->rep->e = daba->rep->f;
    daba->rep->count = 0;
    ft_daba_pool_move(&pool, identity, ft_daba_aggregate_ra(daba), true);
    ft_daba_pool_move(&pool, identity_copy, ft_daba_aggregate_b(daba), true);
    ft_daba_chain_destroy(daba, retired);
    ft_daba_pool_cleanup(&pool);
    return FT_STATUS_OK;
}

static bool ft_daba_record_cursor(
    ft_daba_block* block,
    size_t block_base,
    ft_daba_cursor cursor,
    size_t* position,
    bool* found)
{
    if (cursor.block != block) {
        return true;
    }
    if (*found || ft_daba_add_overflows(block_base, cursor.index, position)) {
        return false;
    }
    *found = true;
    return true;
}

bool ft_daba_lite_validate(const ft_daba_lite* daba, ft_daba_lite_statistics* statistics)
{
    ft_daba_block* slow = NULL;
    ft_daba_block* fast = NULL;
    ft_daba_block* previous = NULL;
    ft_daba_block* current = NULL;
    ft_daba_block* last = NULL;
    size_t positions[6] = { 0, 0, 0, 0, 0, 0 };
    bool found[6] = { false, false, false, false, false, false };
    size_t block_base = 0;
    size_t block_count = 0;
    size_t occupied_count = 0;
    size_t expected_block_count = 0;
    size_t allocated_slot_capacity = 0;
    size_t slack_slot_count = 0;
    size_t front_length = 0;
    size_t back_length = 0;
    size_t left_length = 0;
    size_t right_length = 0;
    size_t accumulator_length = 0;
    const ft_daba_cursor cursors[6] = {
        daba == NULL || daba->rep == NULL ? (ft_daba_cursor){ NULL, 0 } : daba->rep->f,
        daba == NULL || daba->rep == NULL ? (ft_daba_cursor){ NULL, 0 } : daba->rep->l,
        daba == NULL || daba->rep == NULL ? (ft_daba_cursor){ NULL, 0 } : daba->rep->r,
        daba == NULL || daba->rep == NULL ? (ft_daba_cursor){ NULL, 0 } : daba->rep->a,
        daba == NULL || daba->rep == NULL ? (ft_daba_cursor){ NULL, 0 } : daba->rep->b,
        daba == NULL || daba->rep == NULL ? (ft_daba_cursor){ NULL, 0 } : daba->rep->e
    };

    if (daba == NULL || daba->policy == NULL || daba->rep == NULL || daba->rep->first == NULL ||
        !ft_daba_policy_valid(daba->policy, NULL)) {
        return false;
    }
    for (size_t index = 0; index < 6; ++index) {
        if (cursors[index].block == NULL || cursors[index].index >= FT_DABA_BLOCK_CAPACITY) {
            return false;
        }
    }
    if (daba->rep->first != daba->rep->f.block || daba->rep->first->previous != NULL) {
        return false;
    }

    slow = daba->rep->first;
    fast = daba->rep->first;
    while (fast != NULL && fast->next != NULL) {
        slow = slow->next;
        fast = fast->next->next;
        if (slow == fast) {
            return false;
        }
    }

    current = daba->rep->first;
    while (current != NULL) {
        if (current->previous != previous || (previous != NULL && previous->next != current) ||
            current->items == NULL) {
            return false;
        }
        for (size_t cursor_index = 0; cursor_index < 6; ++cursor_index) {
            if (!ft_daba_record_cursor(
                    current,
                    block_base,
                    cursors[cursor_index],
                    &positions[cursor_index],
                    &found[cursor_index])) {
                return false;
            }
        }
        for (size_t slot = 0; slot < FT_DABA_BLOCK_CAPACITY; ++slot) {
            if ((current->occupied & (UINT64_C(1) << slot)) != 0) {
                if (occupied_count == SIZE_MAX) {
                    return false;
                }
                ++occupied_count;
            }
        }
        if (block_count == SIZE_MAX) {
            return false;
        }
        ++block_count;
        last = current;
        previous = current;
        current = current->next;
        if (current != NULL && ft_daba_add_overflows(block_base, FT_DABA_BLOCK_CAPACITY, &block_base)) {
            return false;
        }
    }
    for (size_t index = 0; index < 6; ++index) {
        if (!found[index]) {
            return false;
        }
    }
    if (last != daba->rep->e.block || last->next != NULL ||
        !(positions[0] <= positions[1] && positions[1] <= positions[2] &&
          positions[2] <= positions[3] && positions[3] <= positions[4] &&
          positions[4] <= positions[5]) ||
        positions[5] - positions[0] != daba->rep->count || occupied_count != daba->rep->count) {
        return false;
    }

    current = daba->rep->first;
    block_base = 0;
    while (current != NULL) {
        for (size_t slot = 0; slot < FT_DABA_BLOCK_CAPACITY; ++slot) {
            size_t absolute = 0;
            const bool occupied = (current->occupied & (UINT64_C(1) << slot)) != 0;
            if (ft_daba_add_overflows(block_base, slot, &absolute) ||
                occupied != (absolute >= positions[0] && absolute < positions[5])) {
                return false;
            }
        }
        current = current->next;
        if (current != NULL && ft_daba_add_overflows(block_base, FT_DABA_BLOCK_CAPACITY, &block_base)) {
            return false;
        }
    }

    front_length = positions[4] - positions[0];
    back_length = positions[5] - positions[4];
    left_length = positions[2] - positions[1];
    right_length = positions[3] - positions[2];
    accumulator_length = positions[4] - positions[3];
    if (daba->rep->count == 0) {
        if (!(ft_daba_cursor_equal(daba->rep->f, daba->rep->l) &&
              ft_daba_cursor_equal(daba->rep->l, daba->rep->r) &&
              ft_daba_cursor_equal(daba->rep->r, daba->rep->a) &&
              ft_daba_cursor_equal(daba->rep->a, daba->rep->b) &&
              ft_daba_cursor_equal(daba->rep->b, daba->rep->e))) {
            return false;
        }
    } else {
        size_t reversal = 0;
        size_t processed_prefix = 0;
        if (left_length != right_length || front_length < back_length ||
            ft_daba_add_overflows(left_length, right_length, &reversal) ||
            ft_daba_add_overflows(reversal, accumulator_length, &reversal) ||
            ft_daba_add_overflows(reversal, 1, &reversal) ||
            ft_daba_add_overflows(back_length, 1, &processed_prefix) ||
            reversal != front_length - back_length ||
            positions[1] - positions[0] != processed_prefix) {
            return false;
        }
    }

    if (ft_daba_add_overflows(daba->rep->f.index, daba->rep->count, &expected_block_count)) {
        return false;
    }
    expected_block_count = expected_block_count / FT_DABA_BLOCK_CAPACITY + 1;
    if (block_count != expected_block_count ||
        ft_daba_multiply_overflows(block_count, FT_DABA_BLOCK_CAPACITY, &allocated_slot_capacity)) {
        return false;
    }
    slack_slot_count = allocated_slot_capacity - daba->rep->count;
    if (slack_slot_count < 1 || slack_slot_count > 2 * FT_DABA_BLOCK_CAPACITY - 1) {
        return false;
    }
    if (statistics != NULL) {
        statistics->count = daba->rep->count;
        statistics->front_length = front_length;
        statistics->back_length = back_length;
        statistics->left_length = left_length;
        statistics->right_length = right_length;
        statistics->accumulator_length = accumulator_length;
        statistics->block_count = block_count;
        statistics->allocated_slot_capacity = allocated_slot_capacity;
        statistics->slack_slot_count = slack_slot_count;
    }
    return true;
}
