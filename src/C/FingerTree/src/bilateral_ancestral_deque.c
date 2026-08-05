/*
 * The bilateral ancestral deque: two oppositely oriented intervals of one append-only ancestry
 * arena.
 *
 * Every operation here is interval arithmetic plus a bounded number of arena primitives. The
 * invariants the whole file rests on are: an interval (anchor, tail] holds exactly count nodes, so
 * depth(tail) - depth(anchor) == count; base is the unique child of anchor on the tail's ancestry
 * path, so it is depth(anchor) + 1; and the logical value of a handle is the reverse of the left
 * interval followed by the right interval. See the header for the bounds and the query ceilings the
 * operations below are written to respect.
 *
 * Failure handling. The arena is the only mutable state, and only ..._add_leaf touches it; the arena
 * itself guarantees that a failed addition leaves it exactly as it was. Every producer therefore
 * computes a complete successor into a local handle and publishes it only on success, which is also
 * what makes exact result/operand aliasing safe.
 */

#include <durable7/finger_tree/bilateral_ancestral_deque.h>

#include <stdint.h>
#include <string.h>

/* Whether the handle is initialized and still usable. */
static bool ft_bilateral_deque_usable(const ft_bilateral_deque* deque)
{
    return deque != NULL && deque->arena.rep != NULL;
}

/* Whether the product overflows, reporting it rather than wrapping. */
static bool ft_bilateral_deque_multiply_overflows(size_t left, size_t right, size_t* product)
{
    if (left != 0 && right > SIZE_MAX / left) {
        return true;
    }
    *product = left * right;
    return false;
}

/* Converts a visible count into an absolute depth offset. */
static ft_status ft_bilateral_deque_depth_offset(
    size_t count,
    ft_incremental_ancestor_depth* offset)
{
    if (count > (size_t)PTRDIFF_MAX) {
        return FT_STATUS_OVERFLOW;
    }
    *offset = (ft_incremental_ancestor_depth)count;
    return FT_STATUS_OK;
}

/* Adds a visible offset to an absolute depth. */
static ft_status ft_bilateral_deque_add_depth(
    ft_incremental_ancestor_depth depth,
    size_t offset,
    ft_incremental_ancestor_depth* result)
{
    ft_incremental_ancestor_depth delta = 0;
    const ft_status status = ft_bilateral_deque_depth_offset(offset, &delta);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (depth > PTRDIFF_MAX - delta) {
        return FT_STATUS_OVERFLOW;
    }
    *result = depth + delta;
    return FT_STATUS_OK;
}

/* Subtracts a visible offset from an absolute depth. */
static ft_status ft_bilateral_deque_subtract_depth(
    ft_incremental_ancestor_depth depth,
    size_t offset,
    ft_incremental_ancestor_depth* result)
{
    ft_incremental_ancestor_depth delta = 0;
    const ft_status status = ft_bilateral_deque_depth_offset(offset, &delta);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (depth < PTRDIFF_MIN + delta) {
        return FT_STATUS_OVERFLOW;
    }
    *result = depth - delta;
    return FT_STATUS_OK;
}

/* Reads a node's immutable absolute depth. O(1), no level-ancestor query. */
static ft_status ft_bilateral_deque_node_depth(
    const ft_bilateral_deque* deque,
    ft_incremental_ancestor_node node,
    ft_incremental_ancestor_depth* depth)
{
    return ft_incremental_ancestor_arena_depth(&deque->arena, node, depth);
}

/* Reads a node's immutable parent. O(1), no level-ancestor query. */
static ft_status ft_bilateral_deque_node_parent(
    const ft_bilateral_deque* deque,
    ft_incremental_ancestor_node node,
    ft_incremental_ancestor_node* parent)
{
    return ft_incremental_ancestor_arena_parent(&deque->arena, node, parent);
}

/* Spends one level-ancestor query. Every caller is written so that the ceilings the header states
 * bound how often this is reached. */
static ft_status ft_bilateral_deque_node_ancestor(
    const ft_bilateral_deque* deque,
    ft_incremental_ancestor_node node,
    ft_incremental_ancestor_depth depth,
    ft_incremental_ancestor_node* ancestor)
{
    return ft_incremental_ancestor_arena_ancestor_at_depth(&deque->arena, node, depth, ancestor);
}

/* Reads the arena's distinguished bottom node, which anchors every empty interval. */
static ft_status ft_bilateral_deque_bottom(
    const ft_bilateral_deque* deque,
    ft_incremental_ancestor_node* bottom)
{
    return ft_incremental_ancestor_arena_bottom(&deque->arena, bottom);
}

/* Reads the arena policy's configuration, so a traversal can size and destroy owned values and
 * borrow the allocator its temporary buffer needs. */
static ft_status ft_bilateral_deque_get_config(
    const ft_bilateral_deque* deque,
    ft_incremental_ancestor_policy_config* config)
{
    ft_incremental_ancestor_policy policy;
    ft_status status = ft_incremental_ancestor_arena_get_policy(&deque->arena, &policy);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_incremental_ancestor_policy_get_config(&policy, config);
    ft_incremental_ancestor_policy_dispose(&policy);
    return status;
}

/* The empty interval anchored at one node. */
static ft_bilateral_deque_interval ft_bilateral_deque_interval_empty_at(
    ft_incremental_ancestor_node node)
{
    ft_bilateral_deque_interval interval;
    interval.anchor = node;
    interval.base = node;
    interval.tail = node;
    interval.count = 0;
    return interval;
}

/* Builds one interval from its endpoints. */
static ft_bilateral_deque_interval ft_bilateral_deque_interval_make(
    ft_incremental_ancestor_node anchor,
    ft_incremental_ancestor_node base,
    ft_incremental_ancestor_node tail,
    size_t count)
{
    ft_bilateral_deque_interval interval;
    interval.anchor = anchor;
    interval.base = base;
    interval.tail = tail;
    interval.count = count;
    return interval;
}

/* Assembles a successor handle, retaining the source's arena for it. The retained reference is what
 * makes publishing over an aliased operand safe. */
static ft_status ft_bilateral_deque_adopt(
    const ft_bilateral_deque* source,
    ft_bilateral_deque_interval left,
    ft_bilateral_deque_interval right,
    size_t count,
    ft_bilateral_deque* produced)
{
    ft_incremental_ancestor_arena arena;
    const ft_status status = ft_incremental_ancestor_arena_copy(&source->arena, &arena);
    if (status != FT_STATUS_OK) {
        return status;
    }
    produced->arena = arena;
    produced->left = left;
    produced->right = right;
    produced->count = count;
    return FT_STATUS_OK;
}

/* Assembles the empty successor anchored at the arena's bottom node. */
static ft_status ft_bilateral_deque_adopt_empty(
    const ft_bilateral_deque* source,
    ft_bilateral_deque* produced)
{
    ft_incremental_ancestor_node bottom = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    ft_bilateral_deque_interval empty;
    const ft_status status = ft_bilateral_deque_bottom(source, &bottom);
    if (status != FT_STATUS_OK) {
        return status;
    }
    empty = ft_bilateral_deque_interval_empty_at(bottom);
    return ft_bilateral_deque_adopt(source, empty, empty, 0, produced);
}

/* Publishes a fully computed successor, releasing the operand's own reference when the result
 * aliases it exactly. */
static void ft_bilateral_deque_publish(
    const ft_bilateral_deque* source,
    ft_bilateral_deque* result,
    ft_bilateral_deque produced)
{
    if (source == result) {
        ft_bilateral_deque old = *result;
        *result = produced;
        ft_bilateral_deque_dispose(&old);
    } else {
        *result = produced;
    }
}

/* Publishes one interval extended by a leaf below its tail. This is the only arena mutation in the
 * file, and it makes no level-ancestor query. */
static ft_status ft_bilateral_deque_add_to_tail(
    const ft_bilateral_deque* deque,
    ft_bilateral_deque_interval interval,
    const void* value,
    ft_bilateral_deque_interval* result)
{
    ft_incremental_ancestor_node leaf = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    const ft_status status =
        ft_incremental_ancestor_arena_add_leaf(&deque->arena, interval.tail, value, &leaf);
    if (status != FT_STATUS_OK) {
        return status;
    }
    *result = ft_bilateral_deque_interval_make(
        interval.anchor,
        interval.count == 0 ? leaf : interval.base,
        leaf,
        interval.count + 1);
    return FT_STATUS_OK;
}

/* Drops an interval's newest physical label by moving its tail to the parent. No queries. */
static ft_status ft_bilateral_deque_remove_tail(
    const ft_bilateral_deque* deque,
    ft_bilateral_deque_interval interval,
    ft_bilateral_deque_interval* result)
{
    ft_incremental_ancestor_node tail = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    const ft_status status = ft_bilateral_deque_node_parent(deque, interval.tail, &tail);
    if (status != FT_STATUS_OK) {
        return status;
    }
    *result = interval.count == 1
        ? ft_bilateral_deque_interval_empty_at(tail)
        : ft_bilateral_deque_interval_make(
              interval.anchor, interval.base, tail, interval.count - 1);
    return FT_STATUS_OK;
}

/* Drops an interval's oldest physical label by advancing its anchor to the cached base. Costs at
 * most one level-ancestor query, which selects the next cached base. */
static ft_status ft_bilateral_deque_remove_base(
    const ft_bilateral_deque* deque,
    ft_bilateral_deque_interval interval,
    ft_bilateral_deque_interval* result)
{
    ft_incremental_ancestor_node base = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    ft_incremental_ancestor_depth tail_depth = 0;
    ft_incremental_ancestor_depth base_depth = 0;
    ft_status status = FT_STATUS_OK;

    if (interval.count == 1) {
        *result = ft_bilateral_deque_interval_empty_at(interval.tail);
        return FT_STATUS_OK;
    }
    if (interval.count == 2) {
        base = interval.tail;
    } else {
        status = ft_bilateral_deque_node_depth(deque, interval.tail, &tail_depth);
        if (status == FT_STATUS_OK) {
            status = ft_bilateral_deque_subtract_depth(tail_depth, interval.count, &base_depth);
        }
        if (status == FT_STATUS_OK) {
            status = ft_bilateral_deque_add_depth(base_depth, 2, &base_depth);
        }
        if (status == FT_STATUS_OK) {
            status = ft_bilateral_deque_node_ancestor(deque, interval.tail, base_depth, &base);
        }
        if (status != FT_STATUS_OK) {
            return status;
        }
    }
    *result = ft_bilateral_deque_interval_make(
        interval.base, base, interval.tail, interval.count - 1);
    return FT_STATUS_OK;
}

/* Selects the sub-interval [start, start + count) of a reversed left arm, where start counts down
 * from the tail. At most two queries, and fewer whenever a cached endpoint already answers. */
static ft_status ft_bilateral_deque_slice_left(
    const ft_bilateral_deque* deque,
    ft_bilateral_deque_interval interval,
    size_t start,
    size_t count,
    ft_bilateral_deque_interval* result)
{
    ft_incremental_ancestor_depth tail_depth = 0;
    ft_incremental_ancestor_depth base_depth = 0;
    ft_incremental_ancestor_depth sliced_tail_depth = 0;
    ft_incremental_ancestor_depth sliced_base_depth = 0;
    ft_incremental_ancestor_node tail = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    ft_incremental_ancestor_node base = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    ft_incremental_ancestor_node anchor = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    ft_status status = FT_STATUS_OK;

    if (count == 0) {
        ft_incremental_ancestor_node bottom = FT_INCREMENTAL_ANCESTOR_BOTTOM;
        status = ft_bilateral_deque_bottom(deque, &bottom);
        if (status != FT_STATUS_OK) {
            return status;
        }
        *result = ft_bilateral_deque_interval_empty_at(bottom);
        return FT_STATUS_OK;
    }
    if (start == 0 && count == interval.count) {
        *result = interval;
        return FT_STATUS_OK;
    }

    status = ft_bilateral_deque_node_depth(deque, interval.tail, &tail_depth);
    if (status == FT_STATUS_OK) {
        status = ft_bilateral_deque_subtract_depth(tail_depth, interval.count, &base_depth);
    }
    if (status == FT_STATUS_OK) {
        status = ft_bilateral_deque_add_depth(base_depth, 1, &base_depth);
    }
    if (status == FT_STATUS_OK) {
        status = ft_bilateral_deque_subtract_depth(tail_depth, start, &sliced_tail_depth);
    }
    if (status == FT_STATUS_OK) {
        status = ft_bilateral_deque_subtract_depth(sliced_tail_depth, count, &sliced_base_depth);
    }
    if (status == FT_STATUS_OK) {
        status = ft_bilateral_deque_add_depth(sliced_base_depth, 1, &sliced_base_depth);
    }
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (sliced_tail_depth == tail_depth) {
        tail = interval.tail;
    } else {
        status = ft_bilateral_deque_node_ancestor(deque, interval.tail, sliced_tail_depth, &tail);
        if (status != FT_STATUS_OK) {
            return status;
        }
    }
    if (sliced_base_depth == sliced_tail_depth) {
        base = tail;
    } else if (sliced_base_depth == base_depth) {
        base = interval.base;
    } else {
        status = ft_bilateral_deque_node_ancestor(deque, interval.tail, sliced_base_depth, &base);
        if (status != FT_STATUS_OK) {
            return status;
        }
    }
    if (base == interval.base) {
        anchor = interval.anchor;
    } else {
        status = ft_bilateral_deque_node_parent(deque, base, &anchor);
        if (status != FT_STATUS_OK) {
            return status;
        }
    }
    *result = ft_bilateral_deque_interval_make(anchor, base, tail, count);
    return FT_STATUS_OK;
}

/* Selects the sub-interval [start, start + count) of a forward right arm. At most two queries, and
 * fewer whenever a cached endpoint already answers. */
static ft_status ft_bilateral_deque_slice_right(
    const ft_bilateral_deque* deque,
    ft_bilateral_deque_interval interval,
    size_t start,
    size_t count,
    ft_bilateral_deque_interval* result)
{
    ft_incremental_ancestor_depth anchor_depth = 0;
    ft_incremental_ancestor_depth sliced_base_depth = 0;
    ft_incremental_ancestor_depth sliced_tail_depth = 0;
    ft_incremental_ancestor_node tail = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    ft_incremental_ancestor_node base = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    ft_incremental_ancestor_node anchor = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    ft_status status = FT_STATUS_OK;

    if (count == 0) {
        ft_incremental_ancestor_node bottom = FT_INCREMENTAL_ANCESTOR_BOTTOM;
        status = ft_bilateral_deque_bottom(deque, &bottom);
        if (status != FT_STATUS_OK) {
            return status;
        }
        *result = ft_bilateral_deque_interval_empty_at(bottom);
        return FT_STATUS_OK;
    }
    if (start == 0 && count == interval.count) {
        *result = interval;
        return FT_STATUS_OK;
    }

    status = ft_bilateral_deque_node_depth(deque, interval.anchor, &anchor_depth);
    if (status == FT_STATUS_OK) {
        status = ft_bilateral_deque_add_depth(anchor_depth, start, &sliced_base_depth);
    }
    if (status == FT_STATUS_OK) {
        status = ft_bilateral_deque_add_depth(sliced_base_depth, 1, &sliced_base_depth);
    }
    if (status == FT_STATUS_OK) {
        status = ft_bilateral_deque_add_depth(sliced_base_depth, count, &sliced_tail_depth);
    }
    if (status == FT_STATUS_OK) {
        status = ft_bilateral_deque_subtract_depth(sliced_tail_depth, 1, &sliced_tail_depth);
    }
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (start == 0) {
        base = interval.base;
    } else {
        status = ft_bilateral_deque_node_ancestor(deque, interval.tail, sliced_base_depth, &base);
        if (status != FT_STATUS_OK) {
            return status;
        }
    }
    if (sliced_tail_depth == sliced_base_depth) {
        tail = base;
    } else if (start + count == interval.count) {
        tail = interval.tail;
    } else {
        status = ft_bilateral_deque_node_ancestor(deque, interval.tail, sliced_tail_depth, &tail);
        if (status != FT_STATUS_OK) {
            return status;
        }
    }
    if (start == 0) {
        anchor = interval.anchor;
    } else {
        status = ft_bilateral_deque_node_parent(deque, base, &anchor);
        if (status != FT_STATUS_OK) {
            return status;
        }
    }
    *result = ft_bilateral_deque_interval_make(anchor, base, tail, count);
    return FT_STATUS_OK;
}

/* The node holding the first visible value of a nonempty deque. No queries. */
static ft_incremental_ancestor_node ft_bilateral_deque_first_node(const ft_bilateral_deque* deque)
{
    return deque->left.count != 0 ? deque->left.tail : deque->right.base;
}

/* The node holding the last visible value of a nonempty deque. No queries. */
static ft_incremental_ancestor_node ft_bilateral_deque_last_node(const ft_bilateral_deque* deque)
{
    return deque->right.count != 0 ? deque->right.tail : deque->left.base;
}

/* The node holding the value at a validated visible index. At most one query. */
static ft_status ft_bilateral_deque_node_at(
    const ft_bilateral_deque* deque,
    size_t index,
    ft_incremental_ancestor_node* node)
{
    ft_incremental_ancestor_depth depth = 0;
    size_t right_index = 0;
    ft_status status = FT_STATUS_OK;

    if (index < deque->left.count) {
        if (index == 0) {
            *node = deque->left.tail;
            return FT_STATUS_OK;
        }
        if (index == deque->left.count - 1) {
            *node = deque->left.base;
            return FT_STATUS_OK;
        }
        status = ft_bilateral_deque_node_depth(deque, deque->left.tail, &depth);
        if (status == FT_STATUS_OK) {
            status = ft_bilateral_deque_subtract_depth(depth, index, &depth);
        }
        if (status != FT_STATUS_OK) {
            return status;
        }
        return ft_bilateral_deque_node_ancestor(deque, deque->left.tail, depth, node);
    }

    right_index = index - deque->left.count;
    if (right_index == 0) {
        *node = deque->right.base;
        return FT_STATUS_OK;
    }
    if (right_index == deque->right.count - 1) {
        *node = deque->right.tail;
        return FT_STATUS_OK;
    }
    status = ft_bilateral_deque_node_depth(deque, deque->right.anchor, &depth);
    if (status == FT_STATUS_OK) {
        status = ft_bilateral_deque_add_depth(depth, right_index, &depth);
    }
    if (status == FT_STATUS_OK) {
        status = ft_bilateral_deque_add_depth(depth, 1, &depth);
    }
    if (status != FT_STATUS_OK) {
        return status;
    }
    return ft_bilateral_deque_node_ancestor(deque, deque->right.tail, depth, node);
}

ft_status ft_bilateral_deque_init(
    ft_bilateral_deque* deque,
    const ft_incremental_ancestor_arena* arena)
{
    ft_incremental_ancestor_arena retained;
    ft_incremental_ancestor_node bottom = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    ft_incremental_ancestor_depth depth = 0;
    ft_bilateral_deque_interval empty;
    ft_status status = FT_STATUS_OK;

    if (deque == NULL || arena == NULL || arena->rep == NULL) {
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
    /* The interval arithmetic assumes the bottom node sits at depth -1; an arena that reports any
     * other bottom depth is rejected before a handle is published. */
    if (depth != FT_INCREMENTAL_ANCESTOR_BOTTOM_DEPTH) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_incremental_ancestor_arena_copy(arena, &retained);
    if (status != FT_STATUS_OK) {
        return status;
    }
    empty = ft_bilateral_deque_interval_empty_at(bottom);
    deque->arena = retained;
    deque->left = empty;
    deque->right = empty;
    deque->count = 0;
    return FT_STATUS_OK;
}

ft_status ft_bilateral_deque_init_myers(
    ft_bilateral_deque* deque,
    const ft_incremental_ancestor_policy* policy)
{
    ft_incremental_ancestor_arena arena;
    ft_status status = FT_STATUS_OK;

    if (deque == NULL || policy == NULL || policy->rep == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_incremental_ancestor_myers_arena_create(&arena, policy);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_bilateral_deque_init(deque, &arena);
    ft_incremental_ancestor_arena_dispose(&arena);
    return status;
}

ft_status ft_bilateral_deque_from_array(
    ft_bilateral_deque* deque,
    const ft_incremental_ancestor_policy* policy,
    const void* values,
    size_t count)
{
    ft_incremental_ancestor_policy_config config;
    ft_bilateral_deque working;
    size_t bytes = 0;
    size_t index = 0;
    ft_status status = FT_STATUS_OK;

    if (deque == NULL || policy == NULL || policy->rep == NULL || (count != 0 && values == NULL)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_incremental_ancestor_policy_get_config(policy, &config);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (ft_bilateral_deque_multiply_overflows(count, config.value_size, &bytes)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    (void)bytes;
    status = ft_bilateral_deque_init_myers(&working, policy);
    if (status != FT_STATUS_OK) {
        return status;
    }
    for (index = 0; status == FT_STATUS_OK && index != count; ++index) {
        const unsigned char* value = (const unsigned char*)values + index * config.value_size;
        status = ft_bilateral_deque_push_back(&working, value, &working);
    }
    if (status == FT_STATUS_OK) {
        *deque = working;
    } else {
        ft_bilateral_deque_dispose(&working);
    }
    return status;
}

ft_status ft_bilateral_deque_copy(
    const ft_bilateral_deque* source,
    ft_bilateral_deque* destination)
{
    ft_incremental_ancestor_arena arena;
    ft_status status = FT_STATUS_OK;

    if (!ft_bilateral_deque_usable(source) || destination == NULL) {
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
    destination->left = source->left;
    destination->right = source->right;
    destination->count = source->count;
    return FT_STATUS_OK;
}

void ft_bilateral_deque_move(ft_bilateral_deque* destination, ft_bilateral_deque* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    *destination = *source;
    (void)memset(source, 0, sizeof(*source));
}

void ft_bilateral_deque_dispose(ft_bilateral_deque* deque)
{
    if (deque == NULL) {
        return;
    }
    ft_incremental_ancestor_arena_dispose(&deque->arena);
    (void)memset(deque, 0, sizeof(*deque));
}

ft_status ft_bilateral_deque_get_arena(
    const ft_bilateral_deque* deque,
    ft_incremental_ancestor_arena* arena)
{
    if (!ft_bilateral_deque_usable(deque) || arena == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return ft_incremental_ancestor_arena_copy(&deque->arena, arena);
}

bool ft_bilateral_deque_empty(const ft_bilateral_deque* deque)
{
    return ft_bilateral_deque_usable(deque) && deque->count == 0;
}

size_t ft_bilateral_deque_size(const ft_bilateral_deque* deque)
{
    return ft_bilateral_deque_usable(deque) ? deque->count : 0;
}

ft_status ft_bilateral_deque_first_ref(const ft_bilateral_deque* deque, const void** value_ref)
{
    if (!ft_bilateral_deque_usable(deque) || value_ref == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (deque->count == 0) {
        return FT_STATUS_EMPTY;
    }
    return ft_incremental_ancestor_arena_value_ref(
        &deque->arena, ft_bilateral_deque_first_node(deque), value_ref);
}

ft_status ft_bilateral_deque_first_copy(const ft_bilateral_deque* deque, void* value)
{
    if (!ft_bilateral_deque_usable(deque) || value == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (deque->count == 0) {
        return FT_STATUS_EMPTY;
    }
    return ft_incremental_ancestor_arena_value_copy(
        &deque->arena, ft_bilateral_deque_first_node(deque), value);
}

ft_status ft_bilateral_deque_last_ref(const ft_bilateral_deque* deque, const void** value_ref)
{
    if (!ft_bilateral_deque_usable(deque) || value_ref == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (deque->count == 0) {
        return FT_STATUS_EMPTY;
    }
    return ft_incremental_ancestor_arena_value_ref(
        &deque->arena, ft_bilateral_deque_last_node(deque), value_ref);
}

ft_status ft_bilateral_deque_last_copy(const ft_bilateral_deque* deque, void* value)
{
    if (!ft_bilateral_deque_usable(deque) || value == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (deque->count == 0) {
        return FT_STATUS_EMPTY;
    }
    return ft_incremental_ancestor_arena_value_copy(
        &deque->arena, ft_bilateral_deque_last_node(deque), value);
}

ft_status ft_bilateral_deque_at_ref(
    const ft_bilateral_deque* deque,
    size_t index,
    const void** value_ref)
{
    ft_incremental_ancestor_node node = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    ft_status status = FT_STATUS_OK;

    if (!ft_bilateral_deque_usable(deque) || value_ref == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (index >= deque->count) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    status = ft_bilateral_deque_node_at(deque, index, &node);
    if (status != FT_STATUS_OK) {
        return status;
    }
    return ft_incremental_ancestor_arena_value_ref(&deque->arena, node, value_ref);
}

ft_status ft_bilateral_deque_at_copy(const ft_bilateral_deque* deque, size_t index, void* value)
{
    ft_incremental_ancestor_node node = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    ft_status status = FT_STATUS_OK;

    if (!ft_bilateral_deque_usable(deque) || value == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (index >= deque->count) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    status = ft_bilateral_deque_node_at(deque, index, &node);
    if (status != FT_STATUS_OK) {
        return status;
    }
    return ft_incremental_ancestor_arena_value_copy(&deque->arena, node, value);
}

ft_status ft_bilateral_deque_push_front(
    const ft_bilateral_deque* deque,
    const void* value,
    ft_bilateral_deque* result)
{
    ft_bilateral_deque produced = {0};
    ft_bilateral_deque_interval left;
    ft_status status = FT_STATUS_OK;

    if (!ft_bilateral_deque_usable(deque) || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (deque->count == SIZE_MAX) {
        return FT_STATUS_OVERFLOW;
    }
    /* The successor's arena reference is taken before the only mutation, so a failed leaf addition
     * leaves nothing provisionally retained. */
    status = ft_incremental_ancestor_arena_copy(&deque->arena, &produced.arena);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_bilateral_deque_add_to_tail(deque, deque->left, value, &left);
    if (status != FT_STATUS_OK) {
        ft_incremental_ancestor_arena_dispose(&produced.arena);
        return status;
    }
    produced.left = left;
    produced.right = deque->right;
    produced.count = deque->count + 1;
    ft_bilateral_deque_publish(deque, result, produced);
    return FT_STATUS_OK;
}

ft_status ft_bilateral_deque_push_back(
    const ft_bilateral_deque* deque,
    const void* value,
    ft_bilateral_deque* result)
{
    ft_bilateral_deque produced = {0};
    ft_bilateral_deque_interval right;
    ft_status status = FT_STATUS_OK;

    if (!ft_bilateral_deque_usable(deque) || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (deque->count == SIZE_MAX) {
        return FT_STATUS_OVERFLOW;
    }
    status = ft_incremental_ancestor_arena_copy(&deque->arena, &produced.arena);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_bilateral_deque_add_to_tail(deque, deque->right, value, &right);
    if (status != FT_STATUS_OK) {
        ft_incremental_ancestor_arena_dispose(&produced.arena);
        return status;
    }
    produced.left = deque->left;
    produced.right = right;
    produced.count = deque->count + 1;
    ft_bilateral_deque_publish(deque, result, produced);
    return FT_STATUS_OK;
}

/* Computes the successor of a nonempty deque without its first value. */
static ft_status ft_bilateral_deque_produce_without_first(
    const ft_bilateral_deque* deque,
    ft_bilateral_deque* produced)
{
    ft_bilateral_deque_interval interval;
    ft_status status = FT_STATUS_OK;

    if (deque->count == 1) {
        return ft_bilateral_deque_adopt_empty(deque, produced);
    }
    if (deque->left.count != 0) {
        status = ft_bilateral_deque_remove_tail(deque, deque->left, &interval);
        if (status != FT_STATUS_OK) {
            return status;
        }
        return ft_bilateral_deque_adopt(
            deque, interval, deque->right, deque->count - 1, produced);
    }
    status = ft_bilateral_deque_remove_base(deque, deque->right, &interval);
    if (status != FT_STATUS_OK) {
        return status;
    }
    return ft_bilateral_deque_adopt(deque, deque->left, interval, deque->count - 1, produced);
}

/* Computes the successor of a nonempty deque without its last value. */
static ft_status ft_bilateral_deque_produce_without_last(
    const ft_bilateral_deque* deque,
    ft_bilateral_deque* produced)
{
    ft_bilateral_deque_interval interval;
    ft_status status = FT_STATUS_OK;

    if (deque->count == 1) {
        return ft_bilateral_deque_adopt_empty(deque, produced);
    }
    if (deque->right.count != 0) {
        status = ft_bilateral_deque_remove_tail(deque, deque->right, &interval);
        if (status != FT_STATUS_OK) {
            return status;
        }
        return ft_bilateral_deque_adopt(deque, deque->left, interval, deque->count - 1, produced);
    }
    status = ft_bilateral_deque_remove_base(deque, deque->left, &interval);
    if (status != FT_STATUS_OK) {
        return status;
    }
    return ft_bilateral_deque_adopt(deque, interval, deque->right, deque->count - 1, produced);
}

ft_status ft_bilateral_deque_remove_first(
    const ft_bilateral_deque* deque,
    ft_bilateral_deque* result)
{
    ft_bilateral_deque produced = {0};
    ft_status status = FT_STATUS_OK;

    if (!ft_bilateral_deque_usable(deque) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (deque->count == 0) {
        return FT_STATUS_EMPTY;
    }
    status = ft_bilateral_deque_produce_without_first(deque, &produced);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_bilateral_deque_publish(deque, result, produced);
    return FT_STATUS_OK;
}

ft_status ft_bilateral_deque_remove_last(
    const ft_bilateral_deque* deque,
    ft_bilateral_deque* result)
{
    ft_bilateral_deque produced = {0};
    ft_status status = FT_STATUS_OK;

    if (!ft_bilateral_deque_usable(deque) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (deque->count == 0) {
        return FT_STATUS_EMPTY;
    }
    status = ft_bilateral_deque_produce_without_last(deque, &produced);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_bilateral_deque_publish(deque, result, produced);
    return FT_STATUS_OK;
}

ft_status ft_bilateral_deque_try_pop_front(
    const ft_bilateral_deque* deque,
    bool* removed,
    void* value,
    ft_bilateral_deque* result)
{
    ft_bilateral_deque produced = {0};
    ft_status status = FT_STATUS_OK;

    if (!ft_bilateral_deque_usable(deque) || removed == NULL || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (deque->count == 0) {
        status = ft_bilateral_deque_copy(deque, &produced);
        if (status != FT_STATUS_OK) {
            return status;
        }
        ft_bilateral_deque_publish(deque, result, produced);
        *removed = false;
        return FT_STATUS_OK;
    }
    /* The remainder is computed first because it cannot leave an owned copy behind on failure. */
    status = ft_bilateral_deque_produce_without_first(deque, &produced);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_incremental_ancestor_arena_value_copy(
        &deque->arena, ft_bilateral_deque_first_node(deque), value);
    if (status != FT_STATUS_OK) {
        ft_bilateral_deque_dispose(&produced);
        return status;
    }
    ft_bilateral_deque_publish(deque, result, produced);
    *removed = true;
    return FT_STATUS_OK;
}

ft_status ft_bilateral_deque_try_pop_back(
    const ft_bilateral_deque* deque,
    bool* removed,
    void* value,
    ft_bilateral_deque* result)
{
    ft_bilateral_deque produced = {0};
    ft_status status = FT_STATUS_OK;

    if (!ft_bilateral_deque_usable(deque) || removed == NULL || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (deque->count == 0) {
        status = ft_bilateral_deque_copy(deque, &produced);
        if (status != FT_STATUS_OK) {
            return status;
        }
        ft_bilateral_deque_publish(deque, result, produced);
        *removed = false;
        return FT_STATUS_OK;
    }
    status = ft_bilateral_deque_produce_without_last(deque, &produced);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_incremental_ancestor_arena_value_copy(
        &deque->arena, ft_bilateral_deque_last_node(deque), value);
    if (status != FT_STATUS_OK) {
        ft_bilateral_deque_dispose(&produced);
        return status;
    }
    ft_bilateral_deque_publish(deque, result, produced);
    *removed = true;
    return FT_STATUS_OK;
}

/* Computes one contiguous slice of a validated index/count pair. */
static ft_status ft_bilateral_deque_produce_slice(
    const ft_bilateral_deque* deque,
    size_t index,
    size_t count,
    ft_bilateral_deque* produced)
{
    const size_t end = index + count;
    const size_t left_count = deque->left.count;
    const size_t left_start = index < left_count ? index : left_count;
    const size_t left_end = end < left_count ? end : left_count;
    const size_t right_start = index > left_count ? index - left_count : 0;
    const size_t right_end = end > left_count ? end - left_count : 0;
    ft_bilateral_deque_interval left;
    ft_bilateral_deque_interval right;
    ft_status status = FT_STATUS_OK;

    if (index == 0 && count == deque->count) {
        return ft_bilateral_deque_copy(deque, produced);
    }
    if (count == 0) {
        return ft_bilateral_deque_adopt_empty(deque, produced);
    }
    status = ft_bilateral_deque_slice_left(
        deque, deque->left, left_start, left_end - left_start, &left);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_bilateral_deque_slice_right(
        deque, deque->right, right_start, right_end - right_start, &right);
    if (status != FT_STATUS_OK) {
        return status;
    }
    return ft_bilateral_deque_adopt(deque, left, right, count, produced);
}

ft_status ft_bilateral_deque_slice(
    const ft_bilateral_deque* deque,
    size_t index,
    size_t count,
    ft_bilateral_deque* result)
{
    ft_bilateral_deque produced = {0};
    ft_status status = FT_STATUS_OK;

    if (!ft_bilateral_deque_usable(deque) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (index > deque->count || count > deque->count - index) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    status = ft_bilateral_deque_produce_slice(deque, index, count, &produced);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_bilateral_deque_publish(deque, result, produced);
    return FT_STATUS_OK;
}

/* Computes the prefix of a validated boundary. */
static ft_status ft_bilateral_deque_produce_take(
    const ft_bilateral_deque* deque,
    size_t count,
    ft_bilateral_deque* produced)
{
    if (count == deque->count) {
        return ft_bilateral_deque_copy(deque, produced);
    }
    return ft_bilateral_deque_produce_slice(deque, 0, count, produced);
}

/* Computes the suffix of a validated boundary. */
static ft_status ft_bilateral_deque_produce_drop(
    const ft_bilateral_deque* deque,
    size_t count,
    ft_bilateral_deque* produced)
{
    if (count == 0) {
        return ft_bilateral_deque_copy(deque, produced);
    }
    return ft_bilateral_deque_produce_slice(deque, count, deque->count - count, produced);
}

ft_status ft_bilateral_deque_take(
    const ft_bilateral_deque* deque,
    size_t count,
    ft_bilateral_deque* result)
{
    ft_bilateral_deque produced = {0};
    ft_status status = FT_STATUS_OK;

    if (!ft_bilateral_deque_usable(deque) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (count > deque->count) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    status = ft_bilateral_deque_produce_take(deque, count, &produced);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_bilateral_deque_publish(deque, result, produced);
    return FT_STATUS_OK;
}

ft_status ft_bilateral_deque_drop(
    const ft_bilateral_deque* deque,
    size_t count,
    ft_bilateral_deque* result)
{
    ft_bilateral_deque produced = {0};
    ft_status status = FT_STATUS_OK;

    if (!ft_bilateral_deque_usable(deque) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (count > deque->count) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    status = ft_bilateral_deque_produce_drop(deque, count, &produced);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_bilateral_deque_publish(deque, result, produced);
    return FT_STATUS_OK;
}

ft_status ft_bilateral_deque_split_at(
    const ft_bilateral_deque* deque,
    size_t index,
    ft_bilateral_deque_split_result* result)
{
    ft_bilateral_deque prefix = {0};
    ft_bilateral_deque suffix = {0};
    ft_status status = FT_STATUS_OK;

    if (!ft_bilateral_deque_usable(deque) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (index > deque->count) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    status = ft_bilateral_deque_produce_take(deque, index, &prefix);
    if (status != FT_STATUS_OK) {
        return status;
    }
    /* Both halves are computed before either is published, so a result member that aliases the
     * operand cannot invalidate the operand mid-way. */
    status = ft_bilateral_deque_produce_drop(deque, index, &suffix);
    if (status != FT_STATUS_OK) {
        ft_bilateral_deque_dispose(&prefix);
        return status;
    }
    ft_bilateral_deque_publish(deque, &result->left, prefix);
    ft_bilateral_deque_publish(deque, &result->right, suffix);
    return FT_STATUS_OK;
}

ft_status ft_bilateral_deque_reverse(
    const ft_bilateral_deque* deque,
    ft_bilateral_deque* result)
{
    ft_bilateral_deque produced = {0};
    ft_status status = FT_STATUS_OK;

    if (!ft_bilateral_deque_usable(deque) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (deque->count <= 1) {
        status = ft_bilateral_deque_copy(deque, &produced);
    } else {
        status = ft_bilateral_deque_adopt(
            deque, deque->right, deque->left, deque->count, &produced);
    }
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_bilateral_deque_publish(deque, result, produced);
    return FT_STATUS_OK;
}

ft_status ft_bilateral_deque_clear(
    const ft_bilateral_deque* deque,
    ft_bilateral_deque* result)
{
    ft_bilateral_deque produced = {0};
    ft_status status = FT_STATUS_OK;

    if (!ft_bilateral_deque_usable(deque) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = deque->count == 0
        ? ft_bilateral_deque_copy(deque, &produced)
        : ft_bilateral_deque_adopt_empty(deque, &produced);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_bilateral_deque_publish(deque, result, produced);
    return FT_STATUS_OK;
}

/* Writes the right arm's nodes into the buffer in logical order. Theta(count) parent reads and no
 * level-ancestor query; the buffer exists precisely so that the forward-oriented arm can be read
 * back to front without one. */
static ft_status ft_bilateral_deque_fill_right_nodes(
    const ft_bilateral_deque* deque,
    ft_incremental_ancestor_node* nodes)
{
    ft_incremental_ancestor_node node = deque->right.tail;
    size_t index = deque->right.count;
    ft_status status = FT_STATUS_OK;

    while (index != 0) {
        --index;
        nodes[index] = node;
        if (index != 0) {
            status = ft_bilateral_deque_node_parent(deque, node, &node);
            if (status != FT_STATUS_OK) {
                return status;
            }
        }
    }
    return FT_STATUS_OK;
}

/* Borrows the right arm's node buffer from the policy's allocator. */
static ft_status ft_bilateral_deque_allocate_right_nodes(
    const ft_bilateral_deque* deque,
    const ft_incremental_ancestor_policy_config* config,
    ft_incremental_ancestor_node** nodes)
{
    size_t bytes = 0;
    if (ft_bilateral_deque_multiply_overflows(
            deque->right.count, sizeof(**nodes), &bytes)) {
        return FT_STATUS_OVERFLOW;
    }
    *nodes = (ft_incremental_ancestor_node*)config->allocator.allocate(
        bytes, config->allocator.context);
    return *nodes == NULL ? FT_STATUS_NO_MEMORY : FT_STATUS_OK;
}

ft_status ft_bilateral_deque_visit(
    const ft_bilateral_deque* deque,
    ft_bilateral_deque_visit_fn visitor,
    void* context)
{
    ft_incremental_ancestor_policy_config config;
    ft_incremental_ancestor_node* nodes = NULL;
    ft_incremental_ancestor_node node = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    const void* value = NULL;
    size_t index = 0;
    ft_status status = FT_STATUS_OK;

    if (!ft_bilateral_deque_usable(deque) || visitor == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    node = deque->left.tail;
    for (index = 0; index != deque->left.count; ++index) {
        status = ft_incremental_ancestor_arena_value_ref(&deque->arena, node, &value);
        if (status != FT_STATUS_OK) {
            return status;
        }
        status = visitor(value, context);
        if (status != FT_STATUS_OK) {
            return status;
        }
        if (index + 1 != deque->left.count) {
            status = ft_bilateral_deque_node_parent(deque, node, &node);
            if (status != FT_STATUS_OK) {
                return status;
            }
        }
    }
    if (deque->right.count == 0) {
        return FT_STATUS_OK;
    }
    status = ft_bilateral_deque_get_config(deque, &config);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_bilateral_deque_allocate_right_nodes(deque, &config, &nodes);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_bilateral_deque_fill_right_nodes(deque, nodes);
    for (index = 0; status == FT_STATUS_OK && index != deque->right.count; ++index) {
        status = ft_incremental_ancestor_arena_value_ref(&deque->arena, nodes[index], &value);
        if (status == FT_STATUS_OK) {
            status = visitor(value, context);
        }
    }
    config.allocator.deallocate(nodes, config.allocator.context);
    return status;
}

/* Destroys the owned copies already written into the caller's array, leaving it ownership-free. */
static void ft_bilateral_deque_destroy_prefix(
    const ft_incremental_ancestor_policy_config* config,
    void* values,
    size_t written)
{
    size_t index = 0;
    if (config->destroy == NULL) {
        return;
    }
    for (index = 0; index != written; ++index) {
        config->destroy(
            (unsigned char*)values + index * config->value_size,
            config->callback_context);
    }
}

ft_status ft_bilateral_deque_copy_to_array(
    const ft_bilateral_deque* deque,
    void* values,
    size_t capacity)
{
    ft_incremental_ancestor_policy_config config;
    ft_incremental_ancestor_node* nodes = NULL;
    ft_incremental_ancestor_node node = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    size_t written = 0;
    size_t index = 0;
    ft_status status = FT_STATUS_OK;

    if (!ft_bilateral_deque_usable(deque)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (capacity < deque->count) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    if (deque->count == 0) {
        return FT_STATUS_OK;
    }
    if (values == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_bilateral_deque_get_config(deque, &config);
    if (status != FT_STATUS_OK) {
        return status;
    }

    node = deque->left.tail;
    for (index = 0; index != deque->left.count; ++index) {
        status = ft_incremental_ancestor_arena_value_copy(
            &deque->arena, node, (unsigned char*)values + written * config.value_size);
        if (status != FT_STATUS_OK) {
            ft_bilateral_deque_destroy_prefix(&config, values, written);
            return status;
        }
        ++written;
        if (index + 1 != deque->left.count) {
            status = ft_bilateral_deque_node_parent(deque, node, &node);
            if (status != FT_STATUS_OK) {
                ft_bilateral_deque_destroy_prefix(&config, values, written);
                return status;
            }
        }
    }
    if (deque->right.count == 0) {
        return FT_STATUS_OK;
    }

    status = ft_bilateral_deque_allocate_right_nodes(deque, &config, &nodes);
    if (status != FT_STATUS_OK) {
        ft_bilateral_deque_destroy_prefix(&config, values, written);
        return status;
    }
    status = ft_bilateral_deque_fill_right_nodes(deque, nodes);
    for (index = 0; status == FT_STATUS_OK && index != deque->right.count; ++index) {
        status = ft_incremental_ancestor_arena_value_copy(
            &deque->arena, nodes[index], (unsigned char*)values + written * config.value_size);
        if (status == FT_STATUS_OK) {
            ++written;
        }
    }
    config.allocator.deallocate(nodes, config.allocator.context);
    if (status != FT_STATUS_OK) {
        ft_bilateral_deque_destroy_prefix(&config, values, written);
    }
    return status;
}

const void* ft_bilateral_deque_root_identity(const ft_bilateral_deque* deque)
{
    if (!ft_bilateral_deque_usable(deque)) {
        return NULL;
    }
    return ft_incremental_ancestor_arena_root_identity(&deque->arena);
}

/* Reports an arena read that a corrupted handle drove out of range as structural invalidity, and
 * leaves every other status distinguishable. */
static ft_status ft_bilateral_deque_soften(ft_status status, bool* valid)
{
    if (status == FT_STATUS_OUT_OF_RANGE) {
        *valid = false;
        return FT_STATUS_OK;
    }
    return status;
}

/* Checks one interval's endpoint identities, cached base, and count/depth agreement. An empty
 * interval is settled from cached fields alone; a nonempty one spends exactly two level-ancestor
 * queries to place the anchor and the cached base on the tail's ancestry path. */
static ft_status ft_bilateral_deque_validate_interval(
    const ft_bilateral_deque* deque,
    ft_bilateral_deque_interval interval,
    bool* valid)
{
    ft_incremental_ancestor_depth anchor_depth = 0;
    ft_incremental_ancestor_depth tail_depth = 0;
    ft_incremental_ancestor_depth expected_depth = 0;
    ft_incremental_ancestor_node node = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    ft_status status = FT_STATUS_OK;

    if (interval.count == 0) {
        *valid = interval.anchor == interval.base && interval.base == interval.tail;
        return FT_STATUS_OK;
    }

    status = ft_bilateral_deque_node_depth(deque, interval.anchor, &anchor_depth);
    if (status != FT_STATUS_OK) {
        return ft_bilateral_deque_soften(status, valid);
    }
    status = ft_bilateral_deque_node_depth(deque, interval.tail, &tail_depth);
    if (status != FT_STATUS_OK) {
        return ft_bilateral_deque_soften(status, valid);
    }
    /* The interval count must be exactly the depth span between its endpoints. */
    status = ft_bilateral_deque_add_depth(anchor_depth, interval.count, &expected_depth);
    if (status != FT_STATUS_OK || expected_depth != tail_depth) {
        *valid = false;
        return FT_STATUS_OK;
    }

    status = ft_bilateral_deque_node_parent(deque, interval.base, &node);
    if (status != FT_STATUS_OK) {
        return ft_bilateral_deque_soften(status, valid);
    }
    if (node != interval.anchor) {
        *valid = false;
        return FT_STATUS_OK;
    }
    status = ft_bilateral_deque_node_ancestor(deque, interval.tail, anchor_depth, &node);
    if (status != FT_STATUS_OK) {
        return ft_bilateral_deque_soften(status, valid);
    }
    if (node != interval.anchor) {
        *valid = false;
        return FT_STATUS_OK;
    }
    status = ft_bilateral_deque_add_depth(anchor_depth, 1, &expected_depth);
    if (status != FT_STATUS_OK) {
        *valid = false;
        return FT_STATUS_OK;
    }
    status = ft_bilateral_deque_node_ancestor(deque, interval.tail, expected_depth, &node);
    if (status != FT_STATUS_OK) {
        return ft_bilateral_deque_soften(status, valid);
    }
    *valid = node == interval.base;
    return FT_STATUS_OK;
}

ft_status ft_bilateral_deque_validate(
    const ft_bilateral_deque* deque,
    bool* valid,
    ft_bilateral_deque_statistics* statistics)
{
    bool result = true;
    size_t total = 0;
    ft_status status = FT_STATUS_OK;

    if (!ft_bilateral_deque_usable(deque) || valid == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (deque->left.count > SIZE_MAX - deque->right.count) {
        result = false;
    } else {
        total = deque->left.count + deque->right.count;
        result = total == deque->count;
    }
    if (result) {
        status = ft_bilateral_deque_validate_interval(deque, deque->left, &result);
        if (status != FT_STATUS_OK) {
            return status;
        }
    }
    if (result) {
        status = ft_bilateral_deque_validate_interval(deque, deque->right, &result);
        if (status != FT_STATUS_OK) {
            return status;
        }
    }
    if (statistics != NULL) {
        statistics->count = deque->count;
        statistics->left_count = deque->left.count;
        statistics->right_count = deque->right.count;
    }
    *valid = result;
    return FT_STATUS_OK;
}
