/*
 * Implementation of the ancestral slice queue.
 *
 * Every operation is boundary arithmetic on the pair (tail, low_depth) under the invariant
 * depth(tail) == low_depth + count - 1, which holds for the anchored empty case too, where it reads
 * depth(tail) == low_depth - 1. Appending publishes one arena leaf below the tail; removing an
 * endpoint or slicing moves a boundary and, only when the window's last element is not already the
 * retained tail, asks the arena for the ancestor at the new tail depth. Nothing here copies a node
 * or a value except through the arena's own policy, so a failure mid-way releases the one handle
 * reference it provisionally took and leaves both the inputs and the arena untouched.
 */

#include <durable7/finger_tree/ancestral_slice_queue.h>

#include <stdint.h>
#include <string.h>

static bool ft_ancestral_slice_queue_valid(const ft_ancestral_slice_queue* queue)
{
    return queue != NULL && queue->arena.rep != NULL;
}

static bool ft_ancestral_slice_queue_multiply_overflows(
    size_t left,
    size_t right,
    size_t* result)
{
    if (left != 0 && right > SIZE_MAX / left) {
        return true;
    }
    *result = left * right;
    return false;
}

/* Returns base + offset as an absolute ancestry depth. Every low boundary this module produces is
 * zero or more, so only the upper end can overflow. */
static ft_status ft_ancestral_slice_queue_offset_depth(
    ft_incremental_ancestor_depth base,
    size_t offset,
    ft_incremental_ancestor_depth* result)
{
    if (offset > (size_t)PTRDIFF_MAX) {
        return FT_STATUS_OVERFLOW;
    }
    if (base > PTRDIFF_MAX - (ft_incremental_ancestor_depth)offset) {
        return FT_STATUS_OVERFLOW;
    }
    *result = base + (ft_incremental_ancestor_depth)offset;
    return FT_STATUS_OK;
}

/* Builds a successor handle over the source's arena, taking the one reference the successor owns.
 * The caller publishes it only after every fallible step has succeeded. */
static ft_status ft_ancestral_slice_queue_make(
    const ft_ancestral_slice_queue* source,
    ft_incremental_ancestor_node tail,
    ft_incremental_ancestor_depth low_depth,
    size_t count,
    ft_ancestral_slice_queue* produced)
{
    const ft_status status =
        ft_incremental_ancestor_arena_copy(&source->arena, &produced->arena);
    if (status != FT_STATUS_OK) {
        return status;
    }
    produced->tail = tail;
    produced->low_depth = low_depth;
    produced->count = count;
    return FT_STATUS_OK;
}

/* Publishes a fully built successor, supporting a result that aliases the source: the source's
 * reference is released only once the successor occupies the slot. */
static void ft_ancestral_slice_queue_publish(
    const ft_ancestral_slice_queue* source,
    ft_ancestral_slice_queue* result,
    ft_ancestral_slice_queue produced)
{
    if (source == result) {
        ft_ancestral_slice_queue old = *result;
        *result = produced;
        ft_ancestral_slice_queue_dispose(&old);
    } else {
        *result = produced;
    }
}

/* Reads the arena policy's configuration, which carries the allocator the traversal buffer uses. */
static ft_status ft_ancestral_slice_queue_get_config(
    const ft_ancestral_slice_queue* queue,
    ft_incremental_ancestor_policy_config* config)
{
    ft_incremental_ancestor_policy policy;
    ft_status status = ft_incremental_ancestor_arena_get_policy(&queue->arena, &policy);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_incremental_ancestor_policy_get_config(&policy, config);
    ft_incremental_ancestor_policy_dispose(&policy);
    return status;
}

/* Names the node holding the value at a zero-based visible index. The last index is the retained
 * tail, so it costs no ancestor query. */
static ft_status ft_ancestral_slice_queue_node_at(
    const ft_ancestral_slice_queue* queue,
    size_t index,
    ft_incremental_ancestor_node* node)
{
    ft_incremental_ancestor_depth depth = 0;
    ft_status status = FT_STATUS_OK;
    if (index == queue->count - 1) {
        *node = queue->tail;
        return FT_STATUS_OK;
    }
    status = ft_ancestral_slice_queue_offset_depth(queue->low_depth, index, &depth);
    if (status != FT_STATUS_OK) {
        return status;
    }
    return ft_incremental_ancestor_arena_ancestor_at_depth(
        &queue->arena,
        queue->tail,
        depth,
        node);
}

/* Produces a second handle on exactly this version, used by the operations whose boundary case is
 * "return the source unchanged". */
static ft_status ft_ancestral_slice_queue_republish(
    const ft_ancestral_slice_queue* queue,
    ft_ancestral_slice_queue* result)
{
    ft_ancestral_slice_queue produced;
    const ft_status status = ft_ancestral_slice_queue_make(
        queue,
        queue->tail,
        queue->low_depth,
        queue->count,
        &produced);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_ancestral_slice_queue_publish(queue, result, produced);
    return FT_STATUS_OK;
}

ft_status ft_ancestral_slice_queue_init(
    ft_ancestral_slice_queue* queue,
    const ft_incremental_ancestor_arena* arena)
{
    ft_incremental_ancestor_node bottom = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    ft_incremental_ancestor_depth depth = 0;
    ft_incremental_ancestor_arena retained;
    ft_status status = FT_STATUS_OK;

    if (queue == NULL || arena == NULL || arena->rep == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_incremental_ancestor_arena_bottom(arena, &bottom);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_incremental_ancestor_arena_depth(arena, bottom, &depth);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (depth != FT_INCREMENTAL_ANCESTOR_BOTTOM_DEPTH) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_incremental_ancestor_arena_copy(arena, &retained);
    if (status != FT_STATUS_OK) {
        return status;
    }
    queue->arena = retained;
    queue->tail = bottom;
    queue->low_depth = 0;
    queue->count = 0;
    return FT_STATUS_OK;
}

ft_status ft_ancestral_slice_queue_init_myers(
    ft_ancestral_slice_queue* queue,
    const ft_incremental_ancestor_policy* policy)
{
    ft_incremental_ancestor_arena arena;
    ft_status status = FT_STATUS_OK;

    if (queue == NULL || policy == NULL || policy->rep == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_incremental_ancestor_myers_arena_create(&arena, policy);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_ancestral_slice_queue_init(queue, &arena);
    ft_incremental_ancestor_arena_dispose(&arena);
    return status;
}

ft_status ft_ancestral_slice_queue_from_array(
    ft_ancestral_slice_queue* queue,
    const ft_incremental_ancestor_policy* policy,
    const void* values,
    size_t count)
{
    ft_incremental_ancestor_policy_config config;
    ft_ancestral_slice_queue working;
    size_t bytes = 0;
    size_t index = 0;
    ft_status status = FT_STATUS_OK;

    if (queue == NULL || policy == NULL || policy->rep == NULL ||
        (count != 0 && values == NULL)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_incremental_ancestor_policy_get_config(policy, &config);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (ft_ancestral_slice_queue_multiply_overflows(count, config.value_size, &bytes)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    (void)bytes;
    status = ft_ancestral_slice_queue_init_myers(&working, policy);
    if (status != FT_STATUS_OK) {
        return status;
    }
    for (index = 0; status == FT_STATUS_OK && index != count; ++index) {
        const unsigned char* value = (const unsigned char*)values + index * config.value_size;
        status = ft_ancestral_slice_queue_add_last(&working, value, &working);
    }
    if (status != FT_STATUS_OK) {
        ft_ancestral_slice_queue_dispose(&working);
        return status;
    }
    *queue = working;
    return FT_STATUS_OK;
}

ft_status ft_ancestral_slice_queue_copy(
    const ft_ancestral_slice_queue* source,
    ft_ancestral_slice_queue* destination)
{
    ft_incremental_ancestor_arena arena;
    ft_status status = FT_STATUS_OK;

    if (!ft_ancestral_slice_queue_valid(source) || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return FT_STATUS_OK;
    }
    status = ft_incremental_ancestor_arena_copy(&source->arena, &arena);
    if (status != FT_STATUS_OK) {
        return status;
    }
    destination->arena = arena;
    destination->tail = source->tail;
    destination->low_depth = source->low_depth;
    destination->count = source->count;
    return FT_STATUS_OK;
}

void ft_ancestral_slice_queue_move(
    ft_ancestral_slice_queue* destination,
    ft_ancestral_slice_queue* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    *destination = *source;
    (void)memset(source, 0, sizeof(*source));
}

void ft_ancestral_slice_queue_dispose(ft_ancestral_slice_queue* queue)
{
    if (queue == NULL) {
        return;
    }
    ft_incremental_ancestor_arena_dispose(&queue->arena);
    (void)memset(queue, 0, sizeof(*queue));
}

bool ft_ancestral_slice_queue_empty(const ft_ancestral_slice_queue* queue)
{
    return ft_ancestral_slice_queue_valid(queue) && queue->count == 0;
}

size_t ft_ancestral_slice_queue_size(const ft_ancestral_slice_queue* queue)
{
    return ft_ancestral_slice_queue_valid(queue) ? queue->count : 0;
}

ft_status ft_ancestral_slice_queue_first_ref(
    const ft_ancestral_slice_queue* queue,
    const void** value_ref)
{
    ft_incremental_ancestor_node node = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    ft_status status = FT_STATUS_OK;

    if (!ft_ancestral_slice_queue_valid(queue) || value_ref == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (queue->count == 0) {
        return FT_STATUS_EMPTY;
    }
    status = ft_ancestral_slice_queue_node_at(queue, 0, &node);
    if (status != FT_STATUS_OK) {
        return status;
    }
    return ft_incremental_ancestor_arena_value_ref(&queue->arena, node, value_ref);
}

ft_status ft_ancestral_slice_queue_first_copy(
    const ft_ancestral_slice_queue* queue,
    void* value)
{
    ft_incremental_ancestor_node node = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    ft_status status = FT_STATUS_OK;

    if (!ft_ancestral_slice_queue_valid(queue) || value == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (queue->count == 0) {
        return FT_STATUS_EMPTY;
    }
    status = ft_ancestral_slice_queue_node_at(queue, 0, &node);
    if (status != FT_STATUS_OK) {
        return status;
    }
    return ft_incremental_ancestor_arena_value_copy(&queue->arena, node, value);
}

ft_status ft_ancestral_slice_queue_last_ref(
    const ft_ancestral_slice_queue* queue,
    const void** value_ref)
{
    if (!ft_ancestral_slice_queue_valid(queue) || value_ref == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (queue->count == 0) {
        return FT_STATUS_EMPTY;
    }
    return ft_incremental_ancestor_arena_value_ref(&queue->arena, queue->tail, value_ref);
}

ft_status ft_ancestral_slice_queue_last_copy(
    const ft_ancestral_slice_queue* queue,
    void* value)
{
    if (!ft_ancestral_slice_queue_valid(queue) || value == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (queue->count == 0) {
        return FT_STATUS_EMPTY;
    }
    return ft_incremental_ancestor_arena_value_copy(&queue->arena, queue->tail, value);
}

ft_status ft_ancestral_slice_queue_at_ref(
    const ft_ancestral_slice_queue* queue,
    size_t index,
    const void** value_ref)
{
    ft_incremental_ancestor_node node = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    ft_status status = FT_STATUS_OK;

    if (!ft_ancestral_slice_queue_valid(queue) || value_ref == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (index >= queue->count) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    status = ft_ancestral_slice_queue_node_at(queue, index, &node);
    if (status != FT_STATUS_OK) {
        return status;
    }
    return ft_incremental_ancestor_arena_value_ref(&queue->arena, node, value_ref);
}

ft_status ft_ancestral_slice_queue_at_copy(
    const ft_ancestral_slice_queue* queue,
    size_t index,
    void* value)
{
    ft_incremental_ancestor_node node = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    ft_status status = FT_STATUS_OK;

    if (!ft_ancestral_slice_queue_valid(queue) || value == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (index >= queue->count) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    status = ft_ancestral_slice_queue_node_at(queue, index, &node);
    if (status != FT_STATUS_OK) {
        return status;
    }
    return ft_incremental_ancestor_arena_value_copy(&queue->arena, node, value);
}

ft_status ft_ancestral_slice_queue_add_last(
    const ft_ancestral_slice_queue* queue,
    const void* value,
    ft_ancestral_slice_queue* result)
{
    ft_ancestral_slice_queue produced;
    ft_incremental_ancestor_node leaf = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    ft_status status = FT_STATUS_OK;

    if (!ft_ancestral_slice_queue_valid(queue) || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (queue->count == SIZE_MAX) {
        return FT_STATUS_OVERFLOW;
    }
    /* Take the successor's arena reference first: the addition below is the only fallible step, and
     * an addition that already published a node could not be undone. */
    status = ft_incremental_ancestor_arena_copy(&queue->arena, &produced.arena);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_incremental_ancestor_arena_add_leaf(&queue->arena, queue->tail, value, &leaf);
    if (status != FT_STATUS_OK) {
        ft_incremental_ancestor_arena_dispose(&produced.arena);
        return status;
    }
    produced.tail = leaf;
    produced.low_depth = queue->low_depth;
    produced.count = queue->count + 1;
    ft_ancestral_slice_queue_publish(queue, result, produced);
    return FT_STATUS_OK;
}

ft_status ft_ancestral_slice_queue_remove_first(
    const ft_ancestral_slice_queue* queue,
    ft_ancestral_slice_queue* result)
{
    ft_ancestral_slice_queue produced;
    ft_incremental_ancestor_depth low_depth = 0;
    ft_status status = FT_STATUS_OK;

    if (!ft_ancestral_slice_queue_valid(queue) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (queue->count == 0) {
        return FT_STATUS_EMPTY;
    }
    status = ft_ancestral_slice_queue_offset_depth(queue->low_depth, 1, &low_depth);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_ancestral_slice_queue_make(
        queue,
        queue->tail,
        low_depth,
        queue->count - 1,
        &produced);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_ancestral_slice_queue_publish(queue, result, produced);
    return FT_STATUS_OK;
}

ft_status ft_ancestral_slice_queue_remove_last(
    const ft_ancestral_slice_queue* queue,
    ft_ancestral_slice_queue* result)
{
    ft_ancestral_slice_queue produced;
    ft_incremental_ancestor_node parent = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    ft_status status = FT_STATUS_OK;

    if (!ft_ancestral_slice_queue_valid(queue) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (queue->count == 0) {
        return FT_STATUS_EMPTY;
    }
    status = ft_incremental_ancestor_arena_parent(&queue->arena, queue->tail, &parent);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_ancestral_slice_queue_make(
        queue,
        parent,
        queue->low_depth,
        queue->count - 1,
        &produced);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_ancestral_slice_queue_publish(queue, result, produced);
    return FT_STATUS_OK;
}

ft_status ft_ancestral_slice_queue_try_remove_first(
    const ft_ancestral_slice_queue* queue,
    bool* removed,
    void* value,
    ft_ancestral_slice_queue* result)
{
    ft_ancestral_slice_queue produced;
    ft_incremental_ancestor_node node = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    ft_incremental_ancestor_depth low_depth = 0;
    ft_status status = FT_STATUS_OK;

    if (!ft_ancestral_slice_queue_valid(queue) || removed == NULL || value == NULL ||
        result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (queue->count == 0) {
        status = ft_ancestral_slice_queue_republish(queue, result);
        if (status != FT_STATUS_OK) {
            return status;
        }
        *removed = false;
        return FT_STATUS_OK;
    }
    status = ft_ancestral_slice_queue_offset_depth(queue->low_depth, 1, &low_depth);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_ancestral_slice_queue_node_at(queue, 0, &node);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_ancestral_slice_queue_make(
        queue,
        queue->tail,
        low_depth,
        queue->count - 1,
        &produced);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_incremental_ancestor_arena_value_copy(&queue->arena, node, value);
    if (status != FT_STATUS_OK) {
        ft_ancestral_slice_queue_dispose(&produced);
        return status;
    }
    ft_ancestral_slice_queue_publish(queue, result, produced);
    *removed = true;
    return FT_STATUS_OK;
}

ft_status ft_ancestral_slice_queue_try_remove_last(
    const ft_ancestral_slice_queue* queue,
    bool* removed,
    void* value,
    ft_ancestral_slice_queue* result)
{
    ft_ancestral_slice_queue produced;
    ft_incremental_ancestor_node parent = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    ft_status status = FT_STATUS_OK;

    if (!ft_ancestral_slice_queue_valid(queue) || removed == NULL || value == NULL ||
        result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (queue->count == 0) {
        status = ft_ancestral_slice_queue_republish(queue, result);
        if (status != FT_STATUS_OK) {
            return status;
        }
        *removed = false;
        return FT_STATUS_OK;
    }
    status = ft_incremental_ancestor_arena_parent(&queue->arena, queue->tail, &parent);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_ancestral_slice_queue_make(
        queue,
        parent,
        queue->low_depth,
        queue->count - 1,
        &produced);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_incremental_ancestor_arena_value_copy(&queue->arena, queue->tail, value);
    if (status != FT_STATUS_OK) {
        ft_ancestral_slice_queue_dispose(&produced);
        return status;
    }
    ft_ancestral_slice_queue_publish(queue, result, produced);
    *removed = true;
    return FT_STATUS_OK;
}

ft_status ft_ancestral_slice_queue_take(
    const ft_ancestral_slice_queue* queue,
    size_t count,
    ft_ancestral_slice_queue* result)
{
    if (!ft_ancestral_slice_queue_valid(queue) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (count > queue->count) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    if (count == queue->count) {
        return ft_ancestral_slice_queue_republish(queue, result);
    }
    return ft_ancestral_slice_queue_slice(queue, 0, count, result);
}

ft_status ft_ancestral_slice_queue_drop(
    const ft_ancestral_slice_queue* queue,
    size_t count,
    ft_ancestral_slice_queue* result)
{
    ft_ancestral_slice_queue produced;
    ft_incremental_ancestor_depth low_depth = 0;
    ft_status status = FT_STATUS_OK;

    if (!ft_ancestral_slice_queue_valid(queue) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (count > queue->count) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    if (count == 0) {
        return ft_ancestral_slice_queue_republish(queue, result);
    }
    status = ft_ancestral_slice_queue_offset_depth(queue->low_depth, count, &low_depth);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_ancestral_slice_queue_make(
        queue,
        queue->tail,
        low_depth,
        queue->count - count,
        &produced);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_ancestral_slice_queue_publish(queue, result, produced);
    return FT_STATUS_OK;
}

ft_status ft_ancestral_slice_queue_slice(
    const ft_ancestral_slice_queue* queue,
    size_t index,
    size_t count,
    ft_ancestral_slice_queue* result)
{
    ft_ancestral_slice_queue produced;
    ft_incremental_ancestor_node tail = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    ft_incremental_ancestor_depth low_depth = 0;
    ft_incremental_ancestor_depth target_depth = 0;
    ft_incremental_ancestor_depth current_tail_depth = 0;
    ft_status status = FT_STATUS_OK;

    if (!ft_ancestral_slice_queue_valid(queue) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (index > queue->count || count > queue->count - index) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    if (index == 0 && count == queue->count) {
        return ft_ancestral_slice_queue_republish(queue, result);
    }
    status = ft_ancestral_slice_queue_offset_depth(queue->low_depth, index, &low_depth);
    if (status != FT_STATUS_OK) {
        return status;
    }
    /* The window's last element sits one below its exclusive end, which for an empty window is
     * exactly the anchor the anchored-empty rule requires. */
    status = ft_ancestral_slice_queue_offset_depth(low_depth, count, &target_depth);
    if (status != FT_STATUS_OK) {
        return status;
    }
    --target_depth;
    status = ft_ancestral_slice_queue_offset_depth(
        queue->low_depth,
        queue->count,
        &current_tail_depth);
    if (status != FT_STATUS_OK) {
        return status;
    }
    --current_tail_depth;
    tail = queue->tail;
    if (target_depth != current_tail_depth) {
        status = ft_incremental_ancestor_arena_ancestor_at_depth(
            &queue->arena,
            queue->tail,
            target_depth,
            &tail);
        if (status != FT_STATUS_OK) {
            return status;
        }
    }
    status = ft_ancestral_slice_queue_make(queue, tail, low_depth, count, &produced);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_ancestral_slice_queue_publish(queue, result, produced);
    return FT_STATUS_OK;
}

ft_status ft_ancestral_slice_queue_split_at(
    const ft_ancestral_slice_queue* queue,
    size_t index,
    ft_ancestral_slice_queue_split_result* result)
{
    ft_ancestral_slice_queue left;
    ft_ancestral_slice_queue right;
    ft_status status = FT_STATUS_OK;

    if (!ft_ancestral_slice_queue_valid(queue) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (index > queue->count) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    status = ft_ancestral_slice_queue_take(queue, index, &left);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_ancestral_slice_queue_drop(queue, index, &right);
    if (status != FT_STATUS_OK) {
        ft_ancestral_slice_queue_dispose(&left);
        return status;
    }
    /* Both halves are published together, and a source that occupies one of the result slots keeps
     * its reference until the successors are in place. */
    if (queue == &result->left || queue == &result->right) {
        ft_ancestral_slice_queue old = *queue;
        result->left = left;
        result->right = right;
        ft_ancestral_slice_queue_dispose(&old);
    } else {
        result->left = left;
        result->right = right;
    }
    return FT_STATUS_OK;
}

ft_status ft_ancestral_slice_queue_visit(
    const ft_ancestral_slice_queue* queue,
    ft_ancestral_slice_queue_visit_fn visitor,
    void* context)
{
    ft_incremental_ancestor_policy_config config;
    ft_incremental_ancestor_node* path = NULL;
    ft_incremental_ancestor_node node = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    const void* value = NULL;
    size_t bytes = 0;
    size_t index = 0;
    ft_status status = FT_STATUS_OK;

    if (!ft_ancestral_slice_queue_valid(queue) || visitor == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (queue->count == 0) {
        return FT_STATUS_OK;
    }
    status = ft_ancestral_slice_queue_get_config(queue, &config);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (ft_ancestral_slice_queue_multiply_overflows(queue->count, sizeof(*path), &bytes)) {
        return FT_STATUS_OVERFLOW;
    }
    path = (ft_incremental_ancestor_node*)config.allocator.allocate(
        bytes,
        config.allocator.context);
    if (path == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    /* Capture the whole path before the first callback, so a branch published during the traversal
     * cannot enter it. */
    node = queue->tail;
    for (index = queue->count; index != 0; --index) {
        path[index - 1] = node;
        if (index != 1) {
            status = ft_incremental_ancestor_arena_parent(&queue->arena, node, &node);
            if (status != FT_STATUS_OK) {
                break;
            }
        }
    }
    for (index = 0; status == FT_STATUS_OK && index != queue->count; ++index) {
        status = ft_incremental_ancestor_arena_value_ref(&queue->arena, path[index], &value);
        if (status != FT_STATUS_OK) {
            break;
        }
        status = visitor(value, context);
    }
    config.allocator.deallocate(path, config.allocator.context);
    return status;
}

const void* ft_ancestral_slice_queue_root_identity(const ft_ancestral_slice_queue* queue)
{
    return queue == NULL ? NULL : ft_incremental_ancestor_arena_root_identity(&queue->arena);
}

ft_status ft_ancestral_slice_queue_get_arena(
    const ft_ancestral_slice_queue* queue,
    ft_incremental_ancestor_arena* arena)
{
    if (!ft_ancestral_slice_queue_valid(queue) || arena == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return ft_incremental_ancestor_arena_copy(&queue->arena, arena);
}
