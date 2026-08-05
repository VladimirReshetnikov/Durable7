/*
 * Implementation of the append-only incremental level-ancestor arena.
 *
 * The shipped Myers backend keeps one parent link and one coalesced jump link per immutable node.
 * Adding a leaf copies the parent's links and either starts a fresh one-level jump or coalesces the
 * parent's jump with the jump it lands on, which is constant link work. An ancestor query repeatedly
 * takes the jump when it does not cross above the target depth and otherwise follows the parent,
 * which is logarithmic in the node's depth. Records live in blocks of sizes 1, 3, 5, ..., so block b
 * covers handles b*b through (b + 1)*(b + 1) - 1 and addressing costs one integer square root.
 */

#include <durable7/finger_tree/incremental_ancestor.h>

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_MSC_VER) || defined(__clang__)
#include <stdatomic.h>
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#if defined(_MSC_VER) && !defined(__clang__)
typedef volatile LONG64 ft_incremental_ancestor_ref_count;

static void ft_incremental_ancestor_ref_init(ft_incremental_ancestor_ref_count* value)
{
    *value = 1;
}

static void ft_incremental_ancestor_ref_retain(ft_incremental_ancestor_ref_count* value)
{
    (void)InterlockedIncrement64(value);
}

static bool ft_incremental_ancestor_ref_release(ft_incremental_ancestor_ref_count* value)
{
    return InterlockedDecrement64(value) == 0;
}
#else
typedef atomic_size_t ft_incremental_ancestor_ref_count;

static void ft_incremental_ancestor_ref_init(ft_incremental_ancestor_ref_count* value)
{
    atomic_init(value, 1);
}

static void ft_incremental_ancestor_ref_retain(ft_incremental_ancestor_ref_count* value)
{
    (void)atomic_fetch_add_explicit(value, 1, memory_order_relaxed);
}

static bool ft_incremental_ancestor_ref_release(ft_incremental_ancestor_ref_count* value)
{
    return atomic_fetch_sub_explicit(value, 1, memory_order_acq_rel) == 1;
}
#endif

/* The first directory size; the directory then doubles. Its length is O(sqrt(M)). */
#define FT_INCREMENTAL_ANCESTOR_INITIAL_DIRECTORY_CAPACITY ((size_t)8)

struct ft_incremental_ancestor_policy_rep {
    ft_incremental_ancestor_ref_count refs;
    ft_incremental_ancestor_policy_config config;
};

typedef enum ft_incremental_ancestor_backend_kind {
    FT_INCREMENTAL_ANCESTOR_BACKEND_CUSTOM = 0,
    FT_INCREMENTAL_ANCESTOR_BACKEND_MYERS = 1
} ft_incremental_ancestor_backend_kind;

struct ft_incremental_ancestor_arena_rep {
    ft_incremental_ancestor_ref_count refs;
    ft_incremental_ancestor_policy_rep* policy;
    ft_incremental_ancestor_arena_vtable vtable;
    void* backend;
    ft_incremental_ancestor_backend_kind kind;
};

/* The immutable navigation links of one published node. */
typedef struct ft_incremental_ancestor_links {
    ft_incremental_ancestor_node parent;
    ft_incremental_ancestor_node skip;
    ft_incremental_ancestor_depth depth;
    size_t skip_distance;
} ft_incremental_ancestor_links;

/* One published node: its retained value and its immutable links. The bottom node's value is
 * null. */
typedef struct ft_incremental_ancestor_record {
    ft_incremental_ancestor_links links;
    void* value;
} ft_incremental_ancestor_record;

/* The Myers backend state: an odd-block store of records plus the deterministic counters. */
typedef struct ft_incremental_ancestor_myers {
    ft_incremental_ancestor_policy_rep* policy;
    ft_incremental_ancestor_record** blocks;
    size_t block_count;
    size_t block_capacity;
    size_t count;
    uint64_t allocated_slot_count;
    uint64_t add_leaf_count;
    uint64_t ancestor_query_count;
    uint64_t total_ancestor_hop_count;
    size_t last_ancestor_hop_count;
    size_t maximum_ancestor_hop_count;
} ft_incremental_ancestor_myers;

static void* ft_incremental_ancestor_default_allocate(size_t size, void* context)
{
    (void)context;
    return malloc(size);
}

static void ft_incremental_ancestor_default_deallocate(void* allocation, void* context)
{
    (void)context;
    free(allocation);
}

static void* ft_incremental_ancestor_allocate(
    const ft_incremental_ancestor_policy_rep* policy,
    size_t size)
{
    return policy->config.allocator.allocate(size, policy->config.allocator.context);
}

static void ft_incremental_ancestor_deallocate(
    const ft_incremental_ancestor_policy_rep* policy,
    void* allocation)
{
    if (allocation != NULL) {
        policy->config.allocator.deallocate(allocation, policy->config.allocator.context);
    }
}

static void ft_incremental_ancestor_policy_retain(ft_incremental_ancestor_policy_rep* policy)
{
    if (policy != NULL) {
        ft_incremental_ancestor_ref_retain(&policy->refs);
    }
}

static void ft_incremental_ancestor_policy_release(ft_incremental_ancestor_policy_rep* policy)
{
    if (policy != NULL && ft_incremental_ancestor_ref_release(&policy->refs)) {
        policy->config.allocator.deallocate(policy, policy->config.allocator.context);
    }
}

static bool ft_incremental_ancestor_config_valid(
    const ft_incremental_ancestor_policy_config* config)
{
    return config != NULL && config->value_size != 0 &&
        config->value_type_identity != NULL && config->allocator.allocate != NULL &&
        config->allocator.deallocate != NULL &&
        (config->destroy == NULL || config->copy != NULL);
}

static uint64_t ft_incremental_ancestor_saturating_add(uint64_t value, uint64_t increment)
{
    return value > UINT64_MAX - increment ? UINT64_MAX : value + increment;
}

/* The exact integer square root, computed without floating point so block addressing is
 * deterministic on every target. */
static size_t ft_incremental_ancestor_integer_square_root(size_t value)
{
    size_t remainder = value;
    size_t root = 0;
    size_t bit = (size_t)1 << (sizeof(size_t) * CHAR_BIT - 2);
    while (bit > remainder) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (remainder >= root + bit) {
            remainder -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

static bool ft_incremental_ancestor_multiply_overflows(size_t left, size_t right, size_t* result)
{
    if (left != 0 && right > SIZE_MAX / left) {
        return true;
    }
    *result = left * right;
    return false;
}

/* Copies a value into freshly allocated arena-owned storage. */
static ft_status ft_incremental_ancestor_value_create(
    const ft_incremental_ancestor_policy_rep* policy,
    const void* source,
    void** result)
{
    void* bytes = ft_incremental_ancestor_allocate(policy, policy->config.value_size);
    ft_status status = FT_STATUS_OK;
    if (bytes == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    if (policy->config.copy == NULL) {
        (void)memcpy(bytes, source, policy->config.value_size);
    } else {
        status = policy->config.copy(bytes, source, policy->config.callback_context);
        if (status != FT_STATUS_OK) {
            ft_incremental_ancestor_deallocate(policy, bytes);
            return status;
        }
    }
    *result = bytes;
    return FT_STATUS_OK;
}

static void ft_incremental_ancestor_value_release(
    const ft_incremental_ancestor_policy_rep* policy,
    void* bytes)
{
    if (bytes == NULL) {
        return;
    }
    if (policy->config.destroy != NULL) {
        policy->config.destroy(bytes, policy->config.callback_context);
    }
    ft_incremental_ancestor_deallocate(policy, bytes);
}

static ft_incremental_ancestor_record* ft_incremental_ancestor_slot(
    const ft_incremental_ancestor_myers* arena,
    size_t index)
{
    const size_t block = ft_incremental_ancestor_integer_square_root(index);
    return &arena->blocks[block][index - block * block];
}

/* Makes room for the record at index arena->count, growing the directory and adding one odd block
 * when that index opens a new block. A failure leaves every published record, the record count, and
 * every counter untouched; a directory that grew without its block simply serves the next attempt. */
static ft_status ft_incremental_ancestor_reserve(ft_incremental_ancestor_myers* arena)
{
    const size_t index = arena->count;
    const size_t block = ft_incremental_ancestor_integer_square_root(index);
    size_t length = 0;
    size_t bytes = 0;
    ft_incremental_ancestor_record* slots = NULL;

    if (block != arena->block_count) {
        return FT_STATUS_OK;
    }
    if (arena->block_count == arena->block_capacity) {
        ft_incremental_ancestor_record** directory = NULL;
        size_t capacity = 0;
        if (arena->block_capacity > SIZE_MAX / 2) {
            return FT_STATUS_OVERFLOW;
        }
        capacity = arena->block_capacity == 0
            ? FT_INCREMENTAL_ANCESTOR_INITIAL_DIRECTORY_CAPACITY
            : arena->block_capacity * 2;
        if (ft_incremental_ancestor_multiply_overflows(capacity, sizeof(*directory), &bytes)) {
            return FT_STATUS_OVERFLOW;
        }
        directory = (ft_incremental_ancestor_record**)ft_incremental_ancestor_allocate(
            arena->policy,
            bytes);
        if (directory == NULL) {
            return FT_STATUS_NO_MEMORY;
        }
        if (arena->block_count != 0) {
            (void)memcpy(
                directory,
                arena->blocks,
                arena->block_count * sizeof(*directory));
        }
        ft_incremental_ancestor_deallocate(arena->policy, arena->blocks);
        arena->blocks = directory;
        arena->block_capacity = capacity;
    }

    if (block > (SIZE_MAX - 1) / 2) {
        return FT_STATUS_OVERFLOW;
    }
    length = 2 * block + 1;
    if (ft_incremental_ancestor_multiply_overflows(length, sizeof(*slots), &bytes)) {
        return FT_STATUS_OVERFLOW;
    }
    slots = (ft_incremental_ancestor_record*)ft_incremental_ancestor_allocate(
        arena->policy,
        bytes);
    if (slots == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    arena->blocks[block] = slots;
    arena->block_count = block + 1;
    arena->allocated_slot_count =
        ft_incremental_ancestor_saturating_add(arena->allocated_slot_count, (uint64_t)length);
    return FT_STATUS_OK;
}

static void ft_incremental_ancestor_myers_destroy(void* backend)
{
    ft_incremental_ancestor_myers* arena = (ft_incremental_ancestor_myers*)backend;
    ft_incremental_ancestor_policy_rep* policy = NULL;
    size_t index = 0;
    size_t block = 0;
    if (arena == NULL) {
        return;
    }
    policy = arena->policy;
    for (index = 0; index != arena->count; ++index) {
        ft_incremental_ancestor_value_release(policy, ft_incremental_ancestor_slot(arena, index)->value);
    }
    for (block = 0; block != arena->block_count; ++block) {
        ft_incremental_ancestor_deallocate(policy, arena->blocks[block]);
    }
    ft_incremental_ancestor_deallocate(policy, arena->blocks);
    ft_incremental_ancestor_deallocate(policy, arena);
    ft_incremental_ancestor_policy_release(policy);
}

static ft_status ft_incremental_ancestor_myers_bottom(
    void* backend,
    ft_incremental_ancestor_node* bottom)
{
    (void)backend;
    *bottom = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    return FT_STATUS_OK;
}

static ft_status ft_incremental_ancestor_myers_published_node_count(void* backend, size_t* count)
{
    const ft_incremental_ancestor_myers* arena = (const ft_incremental_ancestor_myers*)backend;
    *count = arena->count - 1;
    return FT_STATUS_OK;
}

static ft_status ft_incremental_ancestor_myers_add_leaf(
    void* backend,
    ft_incremental_ancestor_node parent,
    const void* value,
    ft_incremental_ancestor_node* leaf)
{
    ft_incremental_ancestor_myers* arena = (ft_incremental_ancestor_myers*)backend;
    ft_incremental_ancestor_links parent_links;
    ft_incremental_ancestor_record* record = NULL;
    ft_incremental_ancestor_node skip = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    size_t skip_distance = 0;
    void* bytes = NULL;
    ft_status status = FT_STATUS_OK;

    if (value == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (parent >= arena->count) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    parent_links = ft_incremental_ancestor_slot(arena, parent)->links;
    if (parent_links.depth == PTRDIFF_MAX || arena->count == SIZE_MAX) {
        return FT_STATUS_OVERFLOW;
    }

    if (parent == FT_INCREMENTAL_ANCESTOR_BOTTOM ||
        parent_links.skip == FT_INCREMENTAL_ANCESTOR_BOTTOM) {
        skip = parent;
        skip_distance = 1;
    } else {
        const ft_incremental_ancestor_links skipped =
            ft_incremental_ancestor_slot(arena, parent_links.skip)->links;
        if (skipped.skip_distance <= parent_links.skip_distance) {
            if (parent_links.skip_distance > SIZE_MAX - skipped.skip_distance ||
                parent_links.skip_distance + skipped.skip_distance == SIZE_MAX) {
                return FT_STATUS_OVERFLOW;
            }
            skip = skipped.skip;
            skip_distance = 1 + parent_links.skip_distance + skipped.skip_distance;
        } else {
            skip = parent;
            skip_distance = 1;
        }
    }

    status = ft_incremental_ancestor_value_create(arena->policy, value, &bytes);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_incremental_ancestor_reserve(arena);
    if (status != FT_STATUS_OK) {
        ft_incremental_ancestor_value_release(arena->policy, bytes);
        return status;
    }

    record = ft_incremental_ancestor_slot(arena, arena->count);
    record->links.parent = parent;
    record->links.skip = skip;
    record->links.depth = parent_links.depth + 1;
    record->links.skip_distance = skip_distance;
    record->value = bytes;
    *leaf = arena->count;
    arena->count = arena->count + 1;
    arena->add_leaf_count = ft_incremental_ancestor_saturating_add(arena->add_leaf_count, 1);
    return FT_STATUS_OK;
}

static ft_status ft_incremental_ancestor_myers_depth(
    void* backend,
    ft_incremental_ancestor_node node,
    ft_incremental_ancestor_depth* depth)
{
    const ft_incremental_ancestor_myers* arena = (const ft_incremental_ancestor_myers*)backend;
    if (node >= arena->count) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    *depth = ft_incremental_ancestor_slot(arena, node)->links.depth;
    return FT_STATUS_OK;
}

static ft_status ft_incremental_ancestor_myers_parent(
    void* backend,
    ft_incremental_ancestor_node node,
    ft_incremental_ancestor_node* parent)
{
    const ft_incremental_ancestor_myers* arena = (const ft_incremental_ancestor_myers*)backend;
    if (node == FT_INCREMENTAL_ANCESTOR_BOTTOM || node >= arena->count) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    *parent = ft_incremental_ancestor_slot(arena, node)->links.parent;
    return FT_STATUS_OK;
}

static ft_status ft_incremental_ancestor_myers_ancestor_at_depth(
    void* backend,
    ft_incremental_ancestor_node node,
    ft_incremental_ancestor_depth depth,
    ft_incremental_ancestor_node* ancestor)
{
    ft_incremental_ancestor_myers* arena = (ft_incremental_ancestor_myers*)backend;
    ft_incremental_ancestor_links current;
    size_t hops = 0;

    if (node >= arena->count) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    current = ft_incremental_ancestor_slot(arena, node)->links;
    if (depth < FT_INCREMENTAL_ANCESTOR_BOTTOM_DEPTH || depth > current.depth) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    while (current.depth > depth) {
        /* The unsigned difference is exact because current.depth is strictly greater than depth. */
        const size_t remaining = (size_t)current.depth - (size_t)depth;
        node = (current.skip_distance != 0 && current.skip_distance <= remaining)
            ? current.skip
            : current.parent;
        current = ft_incremental_ancestor_slot(arena, node)->links;
        ++hops;
    }

    arena->ancestor_query_count =
        ft_incremental_ancestor_saturating_add(arena->ancestor_query_count, 1);
    arena->last_ancestor_hop_count = hops;
    arena->total_ancestor_hop_count =
        ft_incremental_ancestor_saturating_add(arena->total_ancestor_hop_count, (uint64_t)hops);
    if (hops > arena->maximum_ancestor_hop_count) {
        arena->maximum_ancestor_hop_count = hops;
    }
    *ancestor = node;
    return FT_STATUS_OK;
}

static ft_status ft_incremental_ancestor_myers_value_ref(
    void* backend,
    ft_incremental_ancestor_node node,
    const void** value_ref)
{
    const ft_incremental_ancestor_myers* arena = (const ft_incremental_ancestor_myers*)backend;
    if (node == FT_INCREMENTAL_ANCESTOR_BOTTOM || node >= arena->count) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    *value_ref = ft_incremental_ancestor_slot(arena, node)->value;
    return FT_STATUS_OK;
}

static const ft_incremental_ancestor_arena_vtable ft_incremental_ancestor_myers_vtable = {
    ft_incremental_ancestor_myers_bottom,
    ft_incremental_ancestor_myers_published_node_count,
    ft_incremental_ancestor_myers_add_leaf,
    ft_incremental_ancestor_myers_depth,
    ft_incremental_ancestor_myers_parent,
    ft_incremental_ancestor_myers_ancestor_at_depth,
    ft_incremental_ancestor_myers_value_ref,
    ft_incremental_ancestor_myers_destroy
};

static bool ft_incremental_ancestor_vtable_valid(
    const ft_incremental_ancestor_arena_vtable* vtable)
{
    return vtable != NULL && vtable->bottom != NULL && vtable->published_node_count != NULL &&
        vtable->add_leaf != NULL && vtable->depth != NULL && vtable->parent != NULL &&
        vtable->ancestor_at_depth != NULL && vtable->value_ref != NULL;
}

static void ft_incremental_ancestor_arena_retain(ft_incremental_ancestor_arena_rep* arena)
{
    if (arena != NULL) {
        ft_incremental_ancestor_ref_retain(&arena->refs);
    }
}

static void ft_incremental_ancestor_arena_release(ft_incremental_ancestor_arena_rep* arena)
{
    if (arena != NULL && ft_incremental_ancestor_ref_release(&arena->refs)) {
        ft_incremental_ancestor_policy_rep* policy = arena->policy;
        if (arena->vtable.destroy != NULL) {
            arena->vtable.destroy(arena->backend);
        }
        ft_incremental_ancestor_deallocate(policy, arena);
        ft_incremental_ancestor_policy_release(policy);
    }
}

/* Publishes one arena handle over an already-constructed backend. On failure the backend is left to
 * the caller, which is the only party that knows how to release a partially built one. */
static ft_status ft_incremental_ancestor_arena_adopt(
    ft_incremental_ancestor_arena* arena,
    ft_incremental_ancestor_policy_rep* policy,
    const ft_incremental_ancestor_arena_config* config,
    ft_incremental_ancestor_backend_kind kind)
{
    ft_incremental_ancestor_arena_rep* rep = (ft_incremental_ancestor_arena_rep*)
        ft_incremental_ancestor_allocate(policy, sizeof(*rep));
    if (rep == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    ft_incremental_ancestor_ref_init(&rep->refs);
    rep->policy = policy;
    rep->vtable = *config->vtable;
    rep->backend = config->backend;
    rep->kind = kind;
    ft_incremental_ancestor_policy_retain(policy);
    arena->rep = rep;
    return FT_STATUS_OK;
}

void ft_incremental_ancestor_policy_config_init(
    ft_incremental_ancestor_policy_config* config,
    size_t value_size,
    const void* value_type_identity,
    void* callback_context)
{
    if (config == NULL) {
        return;
    }
    (void)memset(config, 0, sizeof(*config));
    config->value_size = value_size;
    config->value_type_identity = value_type_identity;
    config->callback_context = callback_context;
    config->allocator.allocate = ft_incremental_ancestor_default_allocate;
    config->allocator.deallocate = ft_incremental_ancestor_default_deallocate;
}

ft_status ft_incremental_ancestor_policy_create(
    ft_incremental_ancestor_policy* policy,
    const ft_incremental_ancestor_policy_config* config)
{
    ft_incremental_ancestor_policy_rep* rep = NULL;
    if (policy == NULL || !ft_incremental_ancestor_config_valid(config)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    rep = (ft_incremental_ancestor_policy_rep*)config->allocator.allocate(
        sizeof(*rep),
        config->allocator.context);
    if (rep == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    ft_incremental_ancestor_ref_init(&rep->refs);
    rep->config = *config;
    policy->rep = rep;
    return FT_STATUS_OK;
}

ft_status ft_incremental_ancestor_policy_copy(
    const ft_incremental_ancestor_policy* source,
    ft_incremental_ancestor_policy* destination)
{
    if (source == NULL || source->rep == NULL || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return FT_STATUS_OK;
    }
    ft_incremental_ancestor_policy_retain(source->rep);
    destination->rep = source->rep;
    return FT_STATUS_OK;
}

void ft_incremental_ancestor_policy_move(
    ft_incremental_ancestor_policy* destination,
    ft_incremental_ancestor_policy* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    destination->rep = source->rep;
    source->rep = NULL;
}

void ft_incremental_ancestor_policy_dispose(ft_incremental_ancestor_policy* policy)
{
    if (policy != NULL) {
        ft_incremental_ancestor_policy_release(policy->rep);
        policy->rep = NULL;
    }
}

bool ft_incremental_ancestor_policy_same(
    const ft_incremental_ancestor_policy* left,
    const ft_incremental_ancestor_policy* right)
{
    return left != NULL && right != NULL && left->rep != NULL && left->rep == right->rep;
}

ft_status ft_incremental_ancestor_policy_get_config(
    const ft_incremental_ancestor_policy* policy,
    ft_incremental_ancestor_policy_config* config)
{
    if (policy == NULL || policy->rep == NULL || config == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    *config = policy->rep->config;
    return FT_STATUS_OK;
}

ft_status ft_incremental_ancestor_arena_create(
    ft_incremental_ancestor_arena* arena,
    const ft_incremental_ancestor_policy* policy,
    const ft_incremental_ancestor_arena_config* config)
{
    if (arena == NULL || policy == NULL || policy->rep == NULL || config == NULL ||
        !ft_incremental_ancestor_vtable_valid(config->vtable)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return ft_incremental_ancestor_arena_adopt(
        arena,
        policy->rep,
        config,
        FT_INCREMENTAL_ANCESTOR_BACKEND_CUSTOM);
}

ft_status ft_incremental_ancestor_myers_arena_create(
    ft_incremental_ancestor_arena* arena,
    const ft_incremental_ancestor_policy* policy)
{
    ft_incremental_ancestor_myers* backend = NULL;
    ft_incremental_ancestor_record* bottom = NULL;
    ft_incremental_ancestor_arena_config config;
    ft_status status = FT_STATUS_OK;

    if (arena == NULL || policy == NULL || policy->rep == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    backend = (ft_incremental_ancestor_myers*)ft_incremental_ancestor_allocate(
        policy->rep,
        sizeof(*backend));
    if (backend == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    (void)memset(backend, 0, sizeof(*backend));
    backend->policy = policy->rep;
    ft_incremental_ancestor_policy_retain(policy->rep);

    status = ft_incremental_ancestor_reserve(backend);
    if (status != FT_STATUS_OK) {
        ft_incremental_ancestor_myers_destroy(backend);
        return status;
    }
    bottom = ft_incremental_ancestor_slot(backend, 0);
    bottom->links.parent = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    bottom->links.skip = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    bottom->links.depth = FT_INCREMENTAL_ANCESTOR_BOTTOM_DEPTH;
    bottom->links.skip_distance = 0;
    bottom->value = NULL;
    backend->count = 1;

    config.vtable = &ft_incremental_ancestor_myers_vtable;
    config.backend = backend;
    status = ft_incremental_ancestor_arena_adopt(
        arena,
        policy->rep,
        &config,
        FT_INCREMENTAL_ANCESTOR_BACKEND_MYERS);
    if (status != FT_STATUS_OK) {
        ft_incremental_ancestor_myers_destroy(backend);
        return status;
    }
    return FT_STATUS_OK;
}

ft_status ft_incremental_ancestor_arena_copy(
    const ft_incremental_ancestor_arena* source,
    ft_incremental_ancestor_arena* destination)
{
    if (source == NULL || source->rep == NULL || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return FT_STATUS_OK;
    }
    ft_incremental_ancestor_arena_retain(source->rep);
    destination->rep = source->rep;
    return FT_STATUS_OK;
}

void ft_incremental_ancestor_arena_move(
    ft_incremental_ancestor_arena* destination,
    ft_incremental_ancestor_arena* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    destination->rep = source->rep;
    source->rep = NULL;
}

void ft_incremental_ancestor_arena_dispose(ft_incremental_ancestor_arena* arena)
{
    if (arena != NULL) {
        ft_incremental_ancestor_arena_release(arena->rep);
        arena->rep = NULL;
    }
}

bool ft_incremental_ancestor_arena_same(
    const ft_incremental_ancestor_arena* left,
    const ft_incremental_ancestor_arena* right)
{
    return left != NULL && right != NULL && left->rep != NULL && left->rep == right->rep;
}

const void* ft_incremental_ancestor_arena_root_identity(
    const ft_incremental_ancestor_arena* arena)
{
    return arena == NULL ? NULL : (const void*)arena->rep;
}

ft_status ft_incremental_ancestor_arena_get_policy(
    const ft_incremental_ancestor_arena* arena,
    ft_incremental_ancestor_policy* policy)
{
    if (arena == NULL || arena->rep == NULL || policy == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_incremental_ancestor_policy_retain(arena->rep->policy);
    policy->rep = arena->rep->policy;
    return FT_STATUS_OK;
}

ft_status ft_incremental_ancestor_arena_bottom(
    const ft_incremental_ancestor_arena* arena,
    ft_incremental_ancestor_node* bottom)
{
    if (arena == NULL || arena->rep == NULL || bottom == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return arena->rep->vtable.bottom(arena->rep->backend, bottom);
}

ft_status ft_incremental_ancestor_arena_published_node_count(
    const ft_incremental_ancestor_arena* arena,
    size_t* count)
{
    if (arena == NULL || arena->rep == NULL || count == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return arena->rep->vtable.published_node_count(arena->rep->backend, count);
}

ft_status ft_incremental_ancestor_arena_add_leaf(
    const ft_incremental_ancestor_arena* arena,
    ft_incremental_ancestor_node parent,
    const void* value,
    ft_incremental_ancestor_node* leaf)
{
    if (arena == NULL || arena->rep == NULL || value == NULL || leaf == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return arena->rep->vtable.add_leaf(arena->rep->backend, parent, value, leaf);
}

ft_status ft_incremental_ancestor_arena_depth(
    const ft_incremental_ancestor_arena* arena,
    ft_incremental_ancestor_node node,
    ft_incremental_ancestor_depth* depth)
{
    if (arena == NULL || arena->rep == NULL || depth == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return arena->rep->vtable.depth(arena->rep->backend, node, depth);
}

ft_status ft_incremental_ancestor_arena_parent(
    const ft_incremental_ancestor_arena* arena,
    ft_incremental_ancestor_node node,
    ft_incremental_ancestor_node* parent)
{
    if (arena == NULL || arena->rep == NULL || parent == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return arena->rep->vtable.parent(arena->rep->backend, node, parent);
}

ft_status ft_incremental_ancestor_arena_ancestor_at_depth(
    const ft_incremental_ancestor_arena* arena,
    ft_incremental_ancestor_node node,
    ft_incremental_ancestor_depth depth,
    ft_incremental_ancestor_node* ancestor)
{
    if (arena == NULL || arena->rep == NULL || ancestor == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return arena->rep->vtable.ancestor_at_depth(arena->rep->backend, node, depth, ancestor);
}

ft_status ft_incremental_ancestor_arena_value_ref(
    const ft_incremental_ancestor_arena* arena,
    ft_incremental_ancestor_node node,
    const void** value_ref)
{
    if (arena == NULL || arena->rep == NULL || value_ref == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return arena->rep->vtable.value_ref(arena->rep->backend, node, value_ref);
}

ft_status ft_incremental_ancestor_arena_value_copy(
    const ft_incremental_ancestor_arena* arena,
    ft_incremental_ancestor_node node,
    void* value)
{
    const ft_incremental_ancestor_policy_config* config = NULL;
    const void* borrowed = NULL;
    ft_status status = FT_STATUS_OK;

    if (arena == NULL || arena->rep == NULL || value == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = arena->rep->vtable.value_ref(arena->rep->backend, node, &borrowed);
    if (status != FT_STATUS_OK) {
        return status;
    }
    config = &arena->rep->policy->config;
    if (config->copy == NULL) {
        (void)memcpy(value, borrowed, config->value_size);
        return FT_STATUS_OK;
    }
    return config->copy(value, borrowed, config->callback_context);
}

ft_status ft_incremental_ancestor_myers_arena_get_statistics(
    const ft_incremental_ancestor_arena* arena,
    ft_incremental_ancestor_myers_statistics* statistics)
{
    const ft_incremental_ancestor_myers* backend = NULL;
    if (arena == NULL || arena->rep == NULL || statistics == NULL ||
        arena->rep->kind != FT_INCREMENTAL_ANCESTOR_BACKEND_MYERS) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    backend = (const ft_incremental_ancestor_myers*)arena->rep->backend;
    statistics->published_node_count = backend->count - 1;
    statistics->block_count = backend->block_count;
    statistics->allocated_slot_count = backend->allocated_slot_count;
    statistics->add_leaf_count = backend->add_leaf_count;
    statistics->ancestor_query_count = backend->ancestor_query_count;
    statistics->last_ancestor_hop_count = backend->last_ancestor_hop_count;
    statistics->maximum_ancestor_hop_count = backend->maximum_ancestor_hop_count;
    statistics->total_ancestor_hop_count = backend->total_ancestor_hop_count;
    return FT_STATUS_OK;
}
