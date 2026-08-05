/*
 * Implementation of the persistent monotone-action heap.
 *
 * The structural kernel is the fused bootstrapped skew-binomial representation of
 * brodal_okasaki_heap.c, with one immutable pending action added to every tree and every forest
 * spine cell. Every consumer reaches a child only through the tag-composing accessors
 * (ft_ma_forest_head, ft_ma_forest_tail, ft_ma_expose), so every comparison sees a fully composed
 * logical priority; a single raw child read would corrupt heap order for transformed heaps only.
 * Losing trees are always attached through identity-tagged spine cells and only logical winners
 * are exposed, which is what keeps two operands' action histories separate across a meld.
 *
 * One refinement over the managed ports: the global root is kept in exposed (identity-tagged)
 * form. Exposing a freshly tagged root is O(1) - it applies one action and pushes one tag onto the
 * child forest - so transform-all stays O(1) worst-case with O(1) fresh structure, while the
 * borrowed minimum accessor can hand back a stored representative rather than a temporary.
 *
 * Internal statics use the abbreviated ft_ma_ prefix; every exported symbol uses the full
 * ft_monotone_action_ prefix.
 */

#include <durable7/finger_tree/persistent_monotone_action_heap.h>

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
typedef volatile LONG64 ft_ma_ref_count;

static void ft_ma_ref_init(ft_ma_ref_count* value)
{
    *value = 1;
}

static void ft_ma_ref_retain(ft_ma_ref_count* value)
{
    (void)InterlockedIncrement64(value);
}

static bool ft_ma_ref_release(ft_ma_ref_count* value)
{
    return InterlockedDecrement64(value) == 0;
}
#else
typedef atomic_size_t ft_ma_ref_count;

static void ft_ma_ref_init(ft_ma_ref_count* value)
{
    atomic_init(value, 1);
}

static void ft_ma_ref_retain(ft_ma_ref_count* value)
{
    (void)atomic_fetch_add_explicit(value, 1, memory_order_relaxed);
}

static bool ft_ma_ref_release(ft_ma_ref_count* value)
{
    return atomic_fetch_sub_explicit(value, 1, memory_order_acq_rel) == 1;
}
#endif

typedef enum ft_ma_release_kind {
    FT_MA_RELEASE_TREE,
    FT_MA_RELEASE_FOREST
} ft_ma_release_kind;

typedef struct ft_ma_release_header {
    ft_ma_ref_count refs;
    ft_ma_release_kind kind;
    struct ft_ma_release_header* release_next;
} ft_ma_release_header;

/* One reference-counted erased value: a payload, a priority, or an action. */
typedef struct ft_ma_value {
    ft_ma_ref_count refs;
    void* bytes;
} ft_ma_value;

typedef struct ft_ma_forest ft_ma_forest;

struct ft_monotone_action_tree {
    ft_ma_release_header header;
    unsigned rank;
    ft_ma_value* element;
    /* Raw priority; the logical priority is apply(action, priority). */
    ft_ma_value* priority;
    /* Raw children; the logical children are tag_forest(children, action). */
    ft_ma_forest* children;
    ft_ma_value* action;
};

struct ft_ma_forest {
    ft_ma_release_header header;
    /* Raw head; the logical head is tag_tree(head, action). */
    ft_monotone_action_tree* head;
    /* Raw tail; the logical tail is tag_forest(tail, action). */
    ft_ma_forest* tail;
    ft_ma_value* action;
};

struct ft_monotone_action_policy_rep {
    ft_ma_ref_count refs;
    ft_monotone_action_policy_config config;
    ft_ma_value* identity_action;
};

/* An owning vector of logical trees, used by the O(log n) deletion path. */
typedef struct ft_ma_tree_list {
    ft_monotone_action_tree** items;
    size_t count;
    size_t capacity;
} ft_ma_tree_list;

typedef struct ft_ma_frame {
    ft_monotone_action_tree* tree;
    size_t depth;
} ft_ma_frame;

/* -------------------------------------------------------------------------------------------
 * Allocation and reference counting
 * ------------------------------------------------------------------------------------------- */

static void* ft_ma_default_allocate(size_t size, void* context)
{
    (void)context;
    return malloc(size);
}

static void ft_ma_default_deallocate(void* allocation, void* context)
{
    (void)context;
    free(allocation);
}

static void* ft_ma_allocate_with(
    const ft_monotone_action_allocator* allocator,
    size_t size)
{
    return allocator->allocate(size, allocator->context);
}

static void ft_ma_deallocate_with(
    const ft_monotone_action_allocator* allocator,
    void* allocation)
{
    if (allocation != NULL) {
        allocator->deallocate(allocation, allocator->context);
    }
}

static void* ft_ma_allocate(
    const ft_monotone_action_policy_rep* policy,
    size_t size)
{
    return ft_ma_allocate_with(&policy->config.allocator, size);
}

static void ft_ma_deallocate(
    const ft_monotone_action_policy_rep* policy,
    void* allocation)
{
    ft_ma_deallocate_with(&policy->config.allocator, allocation);
}

static bool ft_ma_multiply_overflows(size_t left, size_t right, size_t* result)
{
    if (left != 0 && right > SIZE_MAX / left) {
        return true;
    }
    *result = left * right;
    return false;
}

static bool ft_ma_add_overflows(size_t left, size_t right, size_t* result)
{
    if (right > SIZE_MAX - left) {
        return true;
    }
    *result = left + right;
    return false;
}

static bool ft_ma_type_valid(const ft_monotone_action_type_policy* type)
{
    return type->size != 0 && type->type_identity != NULL &&
        (type->destroy == NULL || type->copy != NULL);
}

static bool ft_ma_config_valid(const ft_monotone_action_policy_config* config)
{
    return config != NULL && ft_ma_type_valid(&config->element) &&
        ft_ma_type_valid(&config->priority) && ft_ma_type_valid(&config->action) &&
        config->identity_action != NULL && config->priority_compare != NULL &&
        config->is_identity != NULL && config->compose != NULL && config->apply != NULL &&
        config->allocator.allocate != NULL && config->allocator.deallocate != NULL;
}

/* -------------------------------------------------------------------------------------------
 * Erased values
 * ------------------------------------------------------------------------------------------- */

static void ft_ma_value_retain(ft_ma_value* value)
{
    if (value != NULL) {
        ft_ma_ref_retain(&value->refs);
    }
}

static void ft_ma_value_release_with(
    const ft_monotone_action_allocator* allocator,
    const ft_monotone_action_type_policy* type,
    ft_ma_value* value)
{
    if (value == NULL || !ft_ma_ref_release(&value->refs)) {
        return;
    }
    if (type->destroy != NULL) {
        type->destroy(value->bytes, type->context);
    }
    ft_ma_deallocate_with(allocator, value->bytes);
    ft_ma_deallocate_with(allocator, value);
}

static void ft_ma_value_release(
    const ft_monotone_action_policy_rep* policy,
    const ft_monotone_action_type_policy* type,
    ft_ma_value* value)
{
    ft_ma_value_release_with(&policy->config.allocator, type, value);
}

static ft_status ft_ma_value_create_with(
    const ft_monotone_action_allocator* allocator,
    const ft_monotone_action_type_policy* type,
    const void* source,
    ft_ma_value** result)
{
    ft_ma_value* value = NULL;
    ft_status status = FT_STATUS_OK;
    if (source == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    value = (ft_ma_value*)ft_ma_allocate_with(allocator, sizeof(*value));
    if (value == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    value->bytes = ft_ma_allocate_with(allocator, type->size);
    if (value->bytes == NULL) {
        ft_ma_deallocate_with(allocator, value);
        return FT_STATUS_NO_MEMORY;
    }
    if (type->copy == NULL) {
        (void)memcpy(value->bytes, source, type->size);
    } else {
        status = type->copy(value->bytes, source, type->context);
        if (status != FT_STATUS_OK) {
            ft_ma_deallocate_with(allocator, value->bytes);
            ft_ma_deallocate_with(allocator, value);
            return status;
        }
    }
    ft_ma_ref_init(&value->refs);
    *result = value;
    return FT_STATUS_OK;
}

static ft_status ft_ma_value_create(
    const ft_monotone_action_policy_rep* policy,
    const ft_monotone_action_type_policy* type,
    const void* source,
    ft_ma_value** result)
{
    return ft_ma_value_create_with(&policy->config.allocator, type, source, result);
}

/* Allocates an uninitialized value whose bytes a callback is about to construct. */
static ft_status ft_ma_value_reserve(
    const ft_monotone_action_policy_rep* policy,
    const ft_monotone_action_type_policy* type,
    ft_ma_value** result)
{
    ft_ma_value* value = (ft_ma_value*)ft_ma_allocate(policy, sizeof(*value));
    if (value == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    value->bytes = ft_ma_allocate(policy, type->size);
    if (value->bytes == NULL) {
        ft_ma_deallocate(policy, value);
        return FT_STATUS_NO_MEMORY;
    }
    ft_ma_ref_init(&value->refs);
    *result = value;
    return FT_STATUS_OK;
}

/* Releases a reserved value whose bytes were never constructed. */
static void ft_ma_value_abandon(
    const ft_monotone_action_policy_rep* policy,
    ft_ma_value* value)
{
    ft_ma_deallocate(policy, value->bytes);
    ft_ma_deallocate(policy, value);
}

/* -------------------------------------------------------------------------------------------
 * Nodes
 * ------------------------------------------------------------------------------------------- */

static void ft_ma_tree_retain(ft_monotone_action_tree* tree)
{
    if (tree != NULL) {
        ft_ma_ref_retain(&tree->header.refs);
    }
}

static void ft_ma_forest_retain(ft_ma_forest* forest)
{
    if (forest != NULL) {
        ft_ma_ref_retain(&forest->header.refs);
    }
}

static void ft_ma_release_graph(
    const ft_monotone_action_policy_rep* policy,
    ft_ma_release_header* work)
{
    while (work != NULL) {
        ft_ma_release_header* current = work;
        work = current->release_next;
        if (current->kind == FT_MA_RELEASE_TREE) {
            ft_monotone_action_tree* tree = (ft_monotone_action_tree*)current;
            ft_ma_forest* children = tree->children;
            ft_ma_value_release(policy, &policy->config.element, tree->element);
            ft_ma_value_release(policy, &policy->config.priority, tree->priority);
            ft_ma_value_release(policy, &policy->config.action, tree->action);
            ft_ma_deallocate(policy, tree);
            if (children != NULL && ft_ma_ref_release(&children->header.refs)) {
                children->header.release_next = work;
                work = &children->header;
            }
        } else {
            ft_ma_forest* forest = (ft_ma_forest*)current;
            ft_monotone_action_tree* head = forest->head;
            ft_ma_forest* tail = forest->tail;
            ft_ma_value_release(policy, &policy->config.action, forest->action);
            ft_ma_deallocate(policy, forest);
            if (head != NULL && ft_ma_ref_release(&head->header.refs)) {
                head->header.release_next = work;
                work = &head->header;
            }
            if (tail != NULL && ft_ma_ref_release(&tail->header.refs)) {
                tail->header.release_next = work;
                work = &tail->header;
            }
        }
    }
}

static void ft_ma_tree_release(
    const ft_monotone_action_policy_rep* policy,
    ft_monotone_action_tree* tree)
{
    if (tree != NULL && ft_ma_ref_release(&tree->header.refs)) {
        tree->header.release_next = NULL;
        ft_ma_release_graph(policy, &tree->header);
    }
}

static void ft_ma_forest_release(
    const ft_monotone_action_policy_rep* policy,
    ft_ma_forest* forest)
{
    if (forest != NULL && ft_ma_ref_release(&forest->header.refs)) {
        forest->header.release_next = NULL;
        ft_ma_release_graph(policy, &forest->header);
    }
}

static ft_status ft_ma_tree_create(
    const ft_monotone_action_policy_rep* policy,
    unsigned rank,
    ft_ma_value* element,
    ft_ma_value* priority,
    ft_ma_forest* children,
    ft_ma_value* action,
    ft_monotone_action_tree** result)
{
    ft_monotone_action_tree* tree = NULL;
    if (element == NULL || priority == NULL || action == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    tree = (ft_monotone_action_tree*)ft_ma_allocate(policy, sizeof(*tree));
    if (tree == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    ft_ma_ref_init(&tree->header.refs);
    tree->header.kind = FT_MA_RELEASE_TREE;
    tree->header.release_next = NULL;
    tree->rank = rank;
    tree->element = element;
    tree->priority = priority;
    tree->children = children;
    tree->action = action;
    ft_ma_value_retain(element);
    ft_ma_value_retain(priority);
    ft_ma_value_retain(action);
    ft_ma_forest_retain(children);
    *result = tree;
    return FT_STATUS_OK;
}

static ft_status ft_ma_forest_create(
    const ft_monotone_action_policy_rep* policy,
    ft_monotone_action_tree* head,
    ft_ma_forest* tail,
    ft_ma_value* action,
    ft_ma_forest** result)
{
    ft_ma_forest* forest = NULL;
    if (head == NULL || action == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    forest = (ft_ma_forest*)ft_ma_allocate(policy, sizeof(*forest));
    if (forest == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    ft_ma_ref_init(&forest->header.refs);
    forest->header.kind = FT_MA_RELEASE_FOREST;
    forest->header.release_next = NULL;
    forest->head = head;
    forest->tail = tail;
    forest->action = action;
    ft_ma_tree_retain(head);
    ft_ma_forest_retain(tail);
    ft_ma_value_retain(action);
    *result = forest;
    return FT_STATUS_OK;
}

/* -------------------------------------------------------------------------------------------
 * Action algebra and comparison
 * ------------------------------------------------------------------------------------------- */

static ft_status ft_ma_is_identity_raw(
    const ft_monotone_action_policy_rep* policy,
    const void* action,
    bool* result)
{
    return policy->config.is_identity(action, result, policy->config.algebra_context);
}

static ft_status ft_ma_is_identity(
    const ft_monotone_action_policy_rep* policy,
    const ft_ma_value* action,
    bool* result)
{
    return ft_ma_is_identity_raw(policy, action->bytes, result);
}

static ft_status ft_ma_compose(
    const ft_monotone_action_policy_rep* policy,
    const ft_ma_value* outer,
    const ft_ma_value* inner,
    ft_ma_value** result)
{
    ft_ma_value* value = NULL;
    ft_status status = ft_ma_value_reserve(policy, &policy->config.action, &value);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = policy->config.compose(
        value->bytes, outer->bytes, inner->bytes, policy->config.algebra_context);
    if (status != FT_STATUS_OK) {
        ft_ma_value_abandon(policy, value);
        return status;
    }
    *result = value;
    return FT_STATUS_OK;
}

static ft_status ft_ma_apply(
    const ft_monotone_action_policy_rep* policy,
    const ft_ma_value* action,
    const ft_ma_value* priority,
    ft_ma_value** result)
{
    ft_ma_value* value = NULL;
    ft_status status = ft_ma_value_reserve(policy, &policy->config.priority, &value);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = policy->config.apply(
        value->bytes, action->bytes, priority->bytes, policy->config.algebra_context);
    if (status != FT_STATUS_OK) {
        ft_ma_value_abandon(policy, value);
        return status;
    }
    *result = value;
    return FT_STATUS_OK;
}

/* Materializes one tree's logical priority, sharing the stored representative when nothing is
 * pending. */
static ft_status ft_ma_logical_priority(
    const ft_monotone_action_policy_rep* policy,
    const ft_monotone_action_tree* tree,
    ft_ma_value** result)
{
    bool identity = false;
    ft_status status = ft_ma_is_identity(policy, tree->action, &identity);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (identity) {
        ft_ma_value_retain(tree->priority);
        *result = tree->priority;
        return FT_STATUS_OK;
    }
    return ft_ma_apply(policy, tree->action, tree->priority, result);
}

static ft_status ft_ma_compare_trees(
    const ft_monotone_action_policy_rep* policy,
    const ft_monotone_action_tree* left,
    const ft_monotone_action_tree* right,
    int* comparison)
{
    ft_ma_value* left_priority = NULL;
    ft_ma_value* right_priority = NULL;
    ft_status status = ft_ma_logical_priority(policy, left, &left_priority);
    if (status == FT_STATUS_OK) {
        status = ft_ma_logical_priority(policy, right, &right_priority);
    }
    if (status == FT_STATUS_OK) {
        status = policy->config.priority_compare(
            left_priority->bytes,
            right_priority->bytes,
            comparison,
            policy->config.priority.context);
    }
    ft_ma_value_release(policy, &policy->config.priority, right_priority);
    ft_ma_value_release(policy, &policy->config.priority, left_priority);
    return status;
}

static ft_status ft_ma_less_equal(
    const ft_monotone_action_policy_rep* policy,
    const ft_monotone_action_tree* left,
    const ft_monotone_action_tree* right,
    bool* result)
{
    int comparison = 0;
    ft_status status = ft_ma_compare_trees(policy, left, right, &comparison);
    if (status == FT_STATUS_OK) {
        *result = comparison <= 0;
    }
    return status;
}

/* -------------------------------------------------------------------------------------------
 * Logical views
 * ------------------------------------------------------------------------------------------- */

/* Pushes a newer action onto one tree, composing it with the tree's pending action. */
static ft_status ft_ma_tag_tree(
    const ft_monotone_action_policy_rep* policy,
    ft_monotone_action_tree* node,
    ft_ma_value* outer,
    ft_monotone_action_tree** result)
{
    ft_ma_value* composed = NULL;
    bool identity = false;
    ft_status status = ft_ma_is_identity(policy, outer, &identity);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (identity) {
        ft_ma_tree_retain(node);
        *result = node;
        return FT_STATUS_OK;
    }
    status = ft_ma_compose(policy, outer, node->action, &composed);
    if (status == FT_STATUS_OK) {
        status = ft_ma_tree_create(
            policy, node->rank, node->element, node->priority, node->children, composed, result);
    }
    ft_ma_value_release(policy, &policy->config.action, composed);
    return status;
}

/* Pushes a newer action onto one forest suffix, composing it with the suffix's action. */
static ft_status ft_ma_tag_forest(
    const ft_monotone_action_policy_rep* policy,
    ft_ma_forest* link,
    ft_ma_value* outer,
    ft_ma_forest** result)
{
    ft_ma_value* composed = NULL;
    bool identity = false;
    ft_status status = ft_ma_is_identity(policy, outer, &identity);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (identity) {
        ft_ma_forest_retain(link);
        *result = link;
        return FT_STATUS_OK;
    }
    status = ft_ma_compose(policy, outer, link->action, &composed);
    if (status == FT_STATUS_OK) {
        status = ft_ma_forest_create(policy, link->head, link->tail, composed, result);
    }
    ft_ma_value_release(policy, &policy->config.action, composed);
    return status;
}

/* Materializes one tree's pending action into its own priority and its child forest. */
static ft_status ft_ma_expose(
    const ft_monotone_action_policy_rep* policy,
    ft_monotone_action_tree* node,
    ft_monotone_action_tree** result)
{
    ft_ma_value* applied = NULL;
    ft_ma_forest* children = NULL;
    bool identity = false;
    ft_status status = ft_ma_is_identity(policy, node->action, &identity);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (identity) {
        ft_ma_tree_retain(node);
        *result = node;
        return FT_STATUS_OK;
    }
    status = ft_ma_apply(policy, node->action, node->priority, &applied);
    if (status == FT_STATUS_OK && node->children != NULL) {
        status = ft_ma_tag_forest(policy, node->children, node->action, &children);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ma_tree_create(
            policy, node->rank, node->element, applied, children, policy->identity_action, result);
    }
    ft_ma_forest_release(policy, children);
    ft_ma_value_release(policy, &policy->config.priority, applied);
    return status;
}

/* Builds an identity-tagged spine cell, so an attached tree keeps only its own history. */
static ft_status ft_ma_cons(
    const ft_monotone_action_policy_rep* policy,
    ft_monotone_action_tree* head,
    ft_ma_forest* tail,
    ft_ma_forest** result)
{
    return ft_ma_forest_create(policy, head, tail, policy->identity_action, result);
}

static ft_status ft_ma_forest_head(
    const ft_monotone_action_policy_rep* policy,
    ft_ma_forest* link,
    ft_monotone_action_tree** result)
{
    return ft_ma_tag_tree(policy, link->head, link->action, result);
}

static ft_status ft_ma_forest_tail(
    const ft_monotone_action_policy_rep* policy,
    ft_ma_forest* link,
    ft_ma_forest** result)
{
    if (link->tail == NULL) {
        *result = NULL;
        return FT_STATUS_OK;
    }
    return ft_ma_tag_forest(policy, link->tail, link->action, result);
}

/* -------------------------------------------------------------------------------------------
 * Owning tree vectors
 * ------------------------------------------------------------------------------------------- */

static void ft_ma_list_init(ft_ma_tree_list* list)
{
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void ft_ma_list_dispose(
    const ft_monotone_action_policy_rep* policy,
    ft_ma_tree_list* list)
{
    for (size_t index = 0; index != list->count; ++index) {
        ft_ma_tree_release(policy, list->items[index]);
    }
    ft_ma_deallocate(policy, list->items);
    ft_ma_list_init(list);
}

static ft_status ft_ma_list_push(
    const ft_monotone_action_policy_rep* policy,
    ft_ma_tree_list* list,
    ft_monotone_action_tree* tree)
{
    if (list->count == list->capacity) {
        ft_monotone_action_tree** items = NULL;
        size_t capacity = list->capacity == 0 ? 4 : list->capacity;
        size_t bytes = 0;
        if (list->capacity != 0 &&
            ft_ma_add_overflows(list->capacity, list->capacity, &capacity)) {
            return FT_STATUS_OVERFLOW;
        }
        if (ft_ma_multiply_overflows(capacity, sizeof(*items), &bytes)) {
            return FT_STATUS_OVERFLOW;
        }
        items = (ft_monotone_action_tree**)ft_ma_allocate(policy, bytes);
        if (items == NULL) {
            return FT_STATUS_NO_MEMORY;
        }
        if (list->count != 0) {
            (void)memcpy(items, list->items, list->count * sizeof(*items));
        }
        ft_ma_deallocate(policy, list->items);
        list->items = items;
        list->capacity = capacity;
    }
    ft_ma_tree_retain(tree);
    list->items[list->count++] = tree;
    return FT_STATUS_OK;
}

/* Collects the logical trees of a forest in spine order. */
static ft_status ft_ma_forest_to_list(
    const ft_monotone_action_policy_rep* policy,
    ft_ma_forest* forest,
    size_t bound,
    ft_ma_tree_list* list)
{
    ft_ma_forest* cursor = forest;
    ft_status status = FT_STATUS_OK;
    ft_ma_forest_retain(cursor);
    while (cursor != NULL) {
        ft_monotone_action_tree* head = NULL;
        ft_ma_forest* next = NULL;
        if (list->count == bound) {
            status = FT_STATUS_INCONSISTENT_POLICY;
            break;
        }
        status = ft_ma_forest_head(policy, cursor, &head);
        if (status != FT_STATUS_OK) {
            break;
        }
        status = ft_ma_list_push(policy, list, head);
        ft_ma_tree_release(policy, head);
        if (status != FT_STATUS_OK) {
            break;
        }
        status = ft_ma_forest_tail(policy, cursor, &next);
        if (status != FT_STATUS_OK) {
            break;
        }
        ft_ma_forest_release(policy, cursor);
        cursor = next;
    }
    ft_ma_forest_release(policy, cursor);
    return status;
}

static ft_status ft_ma_list_to_forest(
    const ft_monotone_action_policy_rep* policy,
    const ft_ma_tree_list* list,
    ft_ma_forest** result)
{
    ft_ma_forest* current = NULL;
    ft_status status = FT_STATUS_OK;
    for (size_t index = list->count; index != 0; --index) {
        ft_ma_forest* next = NULL;
        status = ft_ma_cons(policy, list->items[index - 1], current, &next);
        if (status != FT_STATUS_OK) {
            ft_ma_forest_release(policy, current);
            return status;
        }
        ft_ma_forest_release(policy, current);
        current = next;
    }
    *result = current;
    return FT_STATUS_OK;
}

/* -------------------------------------------------------------------------------------------
 * Skew-binomial kernel over logical views
 * ------------------------------------------------------------------------------------------- */

static ft_status ft_ma_skew_link(
    const ft_monotone_action_policy_rep* policy,
    ft_monotone_action_tree* zero,
    ft_monotone_action_tree* first,
    ft_monotone_action_tree* second,
    ft_monotone_action_tree** result)
{
    ft_monotone_action_tree* winner = NULL;
    ft_monotone_action_tree* exposed = NULL;
    ft_monotone_action_tree* child_first = NULL;
    ft_monotone_action_tree* child_second = NULL;
    ft_ma_forest* inner = NULL;
    ft_ma_forest* children = NULL;
    bool first_le_zero = false;
    bool first_le_second = false;
    bool second_le_zero = false;
    bool second_le_first = false;
    ft_status status = FT_STATUS_OK;
    if (first->rank != second->rank || first->rank == UINT_MAX) {
        return FT_STATUS_INCONSISTENT_POLICY;
    }
    status = ft_ma_less_equal(policy, first, zero, &first_le_zero);
    if (status == FT_STATUS_OK && first_le_zero) {
        status = ft_ma_less_equal(policy, first, second, &first_le_second);
    }
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (first_le_zero && first_le_second) {
        winner = first;
        child_first = zero;
        child_second = second;
    } else {
        status = ft_ma_less_equal(policy, second, zero, &second_le_zero);
        if (status == FT_STATUS_OK && second_le_zero) {
            status = ft_ma_less_equal(policy, second, first, &second_le_first);
        }
        if (status != FT_STATUS_OK) {
            return status;
        }
        if (second_le_zero && second_le_first) {
            winner = second;
            child_first = zero;
            child_second = first;
        } else {
            winner = zero;
            child_first = first;
            child_second = second;
        }
    }
    status = ft_ma_expose(policy, winner, &exposed);
    if (status == FT_STATUS_OK) {
        status = ft_ma_cons(policy, child_second, exposed->children, &inner);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ma_cons(policy, child_first, inner, &children);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ma_tree_create(
            policy,
            first->rank + 1,
            exposed->element,
            exposed->priority,
            children,
            policy->identity_action,
            result);
    }
    ft_ma_forest_release(policy, children);
    ft_ma_forest_release(policy, inner);
    ft_ma_tree_release(policy, exposed);
    return status;
}

static ft_status ft_ma_link(
    const ft_monotone_action_policy_rep* policy,
    ft_monotone_action_tree* first,
    ft_monotone_action_tree* second,
    ft_monotone_action_tree** result)
{
    ft_monotone_action_tree* exposed = NULL;
    ft_monotone_action_tree* loser = NULL;
    ft_ma_forest* children = NULL;
    bool first_wins = false;
    ft_status status = FT_STATUS_OK;
    if (first->rank != second->rank || first->rank == UINT_MAX) {
        return FT_STATUS_INCONSISTENT_POLICY;
    }
    status = ft_ma_less_equal(policy, first, second, &first_wins);
    if (status != FT_STATUS_OK) {
        return status;
    }
    loser = first_wins ? second : first;
    status = ft_ma_expose(policy, first_wins ? first : second, &exposed);
    if (status == FT_STATUS_OK) {
        status = ft_ma_cons(policy, loser, exposed->children, &children);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ma_tree_create(
            policy,
            first->rank + 1,
            exposed->element,
            exposed->priority,
            children,
            policy->identity_action,
            result);
    }
    ft_ma_forest_release(policy, children);
    ft_ma_tree_release(policy, exposed);
    return status;
}

/* Ranks are tag-invariant, so the raw tail's rank decides the link before any tagged tail cell is
 * materialized. Only the linking branch pays for the tagged tail. */
static ft_status ft_ma_skew_insert(
    const ft_monotone_action_policy_rep* policy,
    ft_monotone_action_tree* tree,
    ft_ma_forest* forest,
    ft_ma_forest** result)
{
    ft_ma_forest* tail = NULL;
    ft_ma_forest* rest = NULL;
    ft_monotone_action_tree* head = NULL;
    ft_monotone_action_tree* second = NULL;
    ft_monotone_action_tree* linked = NULL;
    ft_status status = FT_STATUS_OK;
    if (forest == NULL || forest->tail == NULL ||
        forest->head->rank != forest->tail->head->rank) {
        return ft_ma_cons(policy, tree, forest, result);
    }
    status = ft_ma_tag_forest(policy, forest->tail, forest->action, &tail);
    if (status == FT_STATUS_OK) {
        status = ft_ma_forest_head(policy, forest, &head);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ma_forest_head(policy, tail, &second);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ma_forest_tail(policy, tail, &rest);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ma_skew_link(policy, tree, head, second, &linked);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ma_cons(policy, linked, rest, result);
    }
    ft_ma_tree_release(policy, linked);
    ft_ma_tree_release(policy, second);
    ft_ma_tree_release(policy, head);
    ft_ma_forest_release(policy, rest);
    ft_ma_forest_release(policy, tail);
    return status;
}

static ft_status ft_ma_bucket_add(
    const ft_monotone_action_policy_rep* policy,
    ft_monotone_action_tree** buckets,
    size_t capacity,
    ft_monotone_action_tree* source)
{
    ft_monotone_action_tree* current = source;
    size_t rank = source->rank;
    ft_ma_tree_retain(current);
    while (rank < capacity && buckets[rank] != NULL) {
        ft_monotone_action_tree* linked = NULL;
        ft_status status = ft_ma_link(policy, buckets[rank], current, &linked);
        ft_ma_tree_release(policy, buckets[rank]);
        buckets[rank] = NULL;
        ft_ma_tree_release(policy, current);
        if (status != FT_STATUS_OK) {
            return status;
        }
        current = linked;
        rank = current->rank;
    }
    if (rank >= capacity) {
        ft_ma_tree_release(policy, current);
        return FT_STATUS_OVERFLOW;
    }
    buckets[rank] = current;
    return FT_STATUS_OK;
}

/* Melds two rank-ordered forests. Ranked buckets replace the reference ports' recursive
 * uniquify/union pair; the produced multiset of logical entries and the strictly increasing rank
 * encoding are the same. */
static ft_status ft_ma_skew_meld(
    const ft_monotone_action_policy_rep* policy,
    const ft_ma_tree_list* left,
    const ft_ma_tree_list* right,
    ft_ma_tree_list* result)
{
    ft_monotone_action_tree** buckets = NULL;
    size_t maximum_rank = 0;
    size_t capacity = 0;
    size_t bytes = 0;
    ft_status status = FT_STATUS_OK;
    for (size_t index = 0; index != left->count; ++index) {
        if (left->items[index]->rank > maximum_rank) {
            maximum_rank = left->items[index]->rank;
        }
    }
    for (size_t index = 0; index != right->count; ++index) {
        if (right->items[index]->rank > maximum_rank) {
            maximum_rank = right->items[index]->rank;
        }
    }
    if (ft_ma_add_overflows(left->count, right->count, &capacity) ||
        ft_ma_add_overflows(capacity, maximum_rank, &capacity) ||
        ft_ma_add_overflows(capacity, 2, &capacity) ||
        ft_ma_multiply_overflows(capacity, sizeof(*buckets), &bytes)) {
        return FT_STATUS_OVERFLOW;
    }
    if (left->count == 0 && right->count == 0) {
        return FT_STATUS_OK;
    }
    buckets = (ft_monotone_action_tree**)ft_ma_allocate(policy, bytes);
    if (buckets == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    (void)memset(buckets, 0, bytes);
    for (size_t index = 0; status == FT_STATUS_OK && index != left->count; ++index) {
        status = ft_ma_bucket_add(policy, buckets, capacity, left->items[index]);
    }
    for (size_t index = 0; status == FT_STATUS_OK && index != right->count; ++index) {
        status = ft_ma_bucket_add(policy, buckets, capacity, right->items[index]);
    }
    for (size_t index = 0; status == FT_STATUS_OK && index != capacity; ++index) {
        if (buckets[index] != NULL) {
            status = ft_ma_list_push(policy, result, buckets[index]);
        }
    }
    for (size_t index = 0; index != capacity; ++index) {
        ft_ma_tree_release(policy, buckets[index]);
    }
    ft_ma_deallocate(policy, buckets);
    return status;
}

/* Decomposes the fused primitive-child encoding of a rank-r tree. The rank-zero ambiguity is
 * resolved in favour of the structural prefix. */
static ft_status ft_ma_split_forest(
    const ft_monotone_action_policy_rep* policy,
    unsigned rank,
    const ft_ma_tree_list* children,
    ft_ma_tree_list* zeros,
    ft_ma_tree_list* trees,
    size_t* embedded_start)
{
    size_t index = 0;
    ft_status status = FT_STATUS_OK;
    while (rank != 0) {
        ft_monotone_action_tree* first = NULL;
        ft_monotone_action_tree* second = NULL;
        if (index == children->count) {
            return FT_STATUS_INCONSISTENT_POLICY;
        }
        first = children->items[index];
        if (rank == 1 && index + 1 == children->count) {
            status = ft_ma_list_push(policy, trees, first);
            index = children->count;
            break;
        }
        if (index + 1 == children->count) {
            return FT_STATUS_INCONSISTENT_POLICY;
        }
        second = children->items[index + 1];
        if (rank == 1) {
            if (second->rank == 0) {
                status = ft_ma_list_push(policy, zeros, first);
                if (status == FT_STATUS_OK) {
                    status = ft_ma_list_push(policy, trees, second);
                }
                index += 2;
            } else {
                status = ft_ma_list_push(policy, trees, first);
                index += 1;
            }
            break;
        }
        if (first->rank == second->rank) {
            status = ft_ma_list_push(policy, trees, second);
            if (status == FT_STATUS_OK) {
                status = ft_ma_list_push(policy, trees, first);
            }
            index += 2;
            break;
        }
        if (first->rank == 0) {
            status = ft_ma_list_push(policy, zeros, first);
            if (status == FT_STATUS_OK) {
                status = ft_ma_list_push(policy, trees, second);
            }
            index += 2;
        } else {
            status = ft_ma_list_push(policy, trees, first);
            index += 1;
        }
        if (status != FT_STATUS_OK) {
            return status;
        }
        --rank;
    }
    if (status != FT_STATUS_OK) {
        return status;
    }
    *embedded_start = index;
    return FT_STATUS_OK;
}

/* -------------------------------------------------------------------------------------------
 * Heap plumbing
 * ------------------------------------------------------------------------------------------- */

static bool ft_ma_heap_valid(const ft_monotone_action_heap* heap)
{
    return heap != NULL && heap->policy != NULL &&
        ((heap->root == NULL && heap->count == 0) ||
            (heap->root != NULL && heap->count != 0));
}

static void ft_ma_policy_retain(ft_monotone_action_policy_rep* policy)
{
    if (policy != NULL) {
        ft_ma_ref_retain(&policy->refs);
    }
}

static void ft_ma_policy_release(ft_monotone_action_policy_rep* policy)
{
    if (policy != NULL && ft_ma_ref_release(&policy->refs)) {
        ft_monotone_action_allocator allocator = policy->config.allocator;
        ft_ma_value_release_with(&allocator, &policy->config.action, policy->identity_action);
        ft_ma_deallocate_with(&allocator, policy);
    }
}

static ft_status ft_ma_heap_adopt(
    ft_monotone_action_policy_rep* policy,
    ft_monotone_action_tree* root,
    size_t count,
    ft_monotone_action_heap* result)
{
    if (policy == NULL || result == NULL || ((root == NULL) != (count == 0))) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_ma_policy_retain(policy);
    result->policy = policy;
    result->root = root;
    result->count = count;
    return FT_STATUS_OK;
}

static void ft_ma_publish_unary(
    const ft_monotone_action_heap* source,
    ft_monotone_action_heap* result,
    ft_monotone_action_heap produced)
{
    if (source == result) {
        ft_monotone_action_heap old = *result;
        *result = produced;
        ft_monotone_action_heap_dispose(&old);
    } else {
        *result = produced;
    }
}

static void ft_ma_publish_binary(
    const ft_monotone_action_heap* left,
    const ft_monotone_action_heap* right,
    ft_monotone_action_heap* result,
    ft_monotone_action_heap produced)
{
    if (result == left || result == right) {
        ft_monotone_action_heap old = *result;
        *result = produced;
        ft_monotone_action_heap_dispose(&old);
    } else {
        *result = produced;
    }
}

static ft_status ft_ma_copy_out(
    const ft_monotone_action_type_policy* type,
    void* destination,
    const void* source)
{
    if (type->copy == NULL) {
        (void)memcpy(destination, source, type->size);
        return FT_STATUS_OK;
    }
    return type->copy(destination, source, type->context);
}

/* -------------------------------------------------------------------------------------------
 * Policy surface
 * ------------------------------------------------------------------------------------------- */

void ft_monotone_action_type_policy_init(
    ft_monotone_action_type_policy* type,
    size_t size,
    const void* type_identity,
    void* context)
{
    if (type == NULL) {
        return;
    }
    (void)memset(type, 0, sizeof(*type));
    type->size = size;
    type->type_identity = type_identity;
    type->context = context;
}

void ft_monotone_action_policy_config_init(
    ft_monotone_action_policy_config* config,
    const ft_monotone_action_type_policy* element,
    const ft_monotone_action_type_policy* priority,
    const ft_monotone_action_type_policy* action,
    const void* identity_action,
    ft_monotone_action_compare_fn priority_compare,
    ft_monotone_action_is_identity_fn is_identity,
    ft_monotone_action_compose_fn compose,
    ft_monotone_action_apply_fn apply,
    void* algebra_context)
{
    if (config == NULL) {
        return;
    }
    (void)memset(config, 0, sizeof(*config));
    if (element != NULL) {
        config->element = *element;
    }
    if (priority != NULL) {
        config->priority = *priority;
    }
    if (action != NULL) {
        config->action = *action;
    }
    config->identity_action = identity_action;
    config->priority_compare = priority_compare;
    config->is_identity = is_identity;
    config->compose = compose;
    config->apply = apply;
    config->algebra_context = algebra_context;
    config->allocator.allocate = ft_ma_default_allocate;
    config->allocator.deallocate = ft_ma_default_deallocate;
}

ft_status ft_monotone_action_policy_create(
    ft_monotone_action_policy* policy,
    const ft_monotone_action_policy_config* config)
{
    ft_monotone_action_policy_rep* rep = NULL;
    ft_ma_value* identity = NULL;
    ft_status status = FT_STATUS_OK;
    if (policy == NULL || !ft_ma_config_valid(config)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_ma_value_create_with(
        &config->allocator, &config->action, config->identity_action, &identity);
    if (status != FT_STATUS_OK) {
        return status;
    }
    rep = (ft_monotone_action_policy_rep*)ft_ma_allocate_with(
        &config->allocator, sizeof(*rep));
    if (rep == NULL) {
        ft_ma_value_release_with(&config->allocator, &config->action, identity);
        return FT_STATUS_NO_MEMORY;
    }
    ft_ma_ref_init(&rep->refs);
    rep->config = *config;
    rep->identity_action = identity;
    policy->rep = rep;
    return FT_STATUS_OK;
}

ft_status ft_monotone_action_policy_copy(
    const ft_monotone_action_policy* source,
    ft_monotone_action_policy* destination)
{
    if (source == NULL || source->rep == NULL || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return FT_STATUS_OK;
    }
    ft_ma_policy_retain(source->rep);
    destination->rep = source->rep;
    return FT_STATUS_OK;
}

void ft_monotone_action_policy_move(
    ft_monotone_action_policy* destination,
    ft_monotone_action_policy* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    destination->rep = source->rep;
    source->rep = NULL;
}

void ft_monotone_action_policy_dispose(ft_monotone_action_policy* policy)
{
    if (policy != NULL) {
        ft_ma_policy_release(policy->rep);
        policy->rep = NULL;
    }
}

bool ft_monotone_action_policy_same(
    const ft_monotone_action_policy* left,
    const ft_monotone_action_policy* right)
{
    return left != NULL && right != NULL && left->rep != NULL && left->rep == right->rep;
}

/* -------------------------------------------------------------------------------------------
 * Heap surface
 * ------------------------------------------------------------------------------------------- */

ft_status ft_monotone_action_heap_init(
    ft_monotone_action_heap* heap,
    const ft_monotone_action_policy* policy)
{
    if (heap == NULL || policy == NULL || policy->rep == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return ft_ma_heap_adopt(policy->rep, NULL, 0, heap);
}

ft_status ft_monotone_action_heap_copy(
    const ft_monotone_action_heap* source,
    ft_monotone_action_heap* destination)
{
    if (!ft_ma_heap_valid(source) || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return FT_STATUS_OK;
    }
    ft_ma_policy_retain(source->policy);
    ft_ma_tree_retain(source->root);
    destination->policy = source->policy;
    destination->root = source->root;
    destination->count = source->count;
    return FT_STATUS_OK;
}

void ft_monotone_action_heap_move(
    ft_monotone_action_heap* destination,
    ft_monotone_action_heap* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    *destination = *source;
    (void)memset(source, 0, sizeof(*source));
}

void ft_monotone_action_heap_dispose(ft_monotone_action_heap* heap)
{
    if (heap == NULL) {
        return;
    }
    if (heap->policy != NULL) {
        ft_ma_tree_release(heap->policy, heap->root);
        ft_ma_policy_release(heap->policy);
    }
    (void)memset(heap, 0, sizeof(*heap));
}

bool ft_monotone_action_heap_empty(const ft_monotone_action_heap* heap)
{
    return ft_ma_heap_valid(heap) && heap->root == NULL;
}

size_t ft_monotone_action_heap_size(const ft_monotone_action_heap* heap)
{
    return ft_ma_heap_valid(heap) ? heap->count : 0;
}

ft_status ft_monotone_action_heap_try_get_minimum_ref(
    const ft_monotone_action_heap* heap,
    bool* found,
    ft_monotone_action_entry_ref* minimum_ref)
{
    if (!ft_ma_heap_valid(heap) || found == NULL || minimum_ref == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (heap->root == NULL) {
        *found = false;
        minimum_ref->element = NULL;
        minimum_ref->priority = NULL;
        return FT_STATUS_OK;
    }
    *found = true;
    minimum_ref->element = heap->root->element->bytes;
    minimum_ref->priority = heap->root->priority->bytes;
    return FT_STATUS_OK;
}

ft_status ft_monotone_action_heap_try_get_minimum_copy(
    const ft_monotone_action_heap* heap,
    bool* found,
    void* element,
    void* priority)
{
    ft_status status = FT_STATUS_OK;
    if (!ft_ma_heap_valid(heap) || found == NULL || element == NULL || priority == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (heap->root == NULL) {
        *found = false;
        return FT_STATUS_OK;
    }
    status = ft_ma_copy_out(
        &heap->policy->config.element, element, heap->root->element->bytes);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_ma_copy_out(
        &heap->policy->config.priority, priority, heap->root->priority->bytes);
    if (status != FT_STATUS_OK) {
        if (heap->policy->config.element.destroy != NULL) {
            heap->policy->config.element.destroy(
                element, heap->policy->config.element.context);
        }
        return status;
    }
    *found = true;
    return FT_STATUS_OK;
}

ft_status ft_monotone_action_heap_insert(
    const ft_monotone_action_heap* heap,
    const void* element,
    const void* priority,
    ft_monotone_action_heap* result)
{
    ft_ma_value* stored_element = NULL;
    ft_ma_value* stored_priority = NULL;
    ft_monotone_action_tree* singleton = NULL;
    ft_monotone_action_tree* root = NULL;
    ft_ma_forest* children = NULL;
    ft_monotone_action_heap produced = {0};
    size_t count = 0;
    bool new_wins = false;
    ft_status status = FT_STATUS_OK;
    if (!ft_ma_heap_valid(heap) || element == NULL || priority == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (heap->count == SIZE_MAX) {
        return FT_STATUS_OVERFLOW;
    }
    count = heap->count + 1;
    status = ft_ma_value_create(
        heap->policy, &heap->policy->config.element, element, &stored_element);
    if (status == FT_STATUS_OK) {
        status = ft_ma_value_create(
            heap->policy, &heap->policy->config.priority, priority, &stored_priority);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ma_tree_create(
            heap->policy,
            0,
            stored_element,
            stored_priority,
            NULL,
            heap->policy->identity_action,
            &singleton);
    }
    if (status != FT_STATUS_OK) {
        goto cleanup;
    }
    if (heap->root == NULL) {
        root = singleton;
        ft_ma_tree_retain(root);
    } else {
        status = ft_ma_less_equal(heap->policy, singleton, heap->root, &new_wins);
        if (status != FT_STATUS_OK) {
            goto cleanup;
        }
        if (new_wins) {
            status = ft_ma_cons(heap->policy, heap->root, NULL, &children);
            if (status == FT_STATUS_OK) {
                status = ft_ma_tree_create(
                    heap->policy,
                    0,
                    stored_element,
                    stored_priority,
                    children,
                    heap->policy->identity_action,
                    &root);
            }
        } else {
            /* The root is retained in exposed form, so the new child cannot inherit a transform
             * that happened before it existed. */
            status = ft_ma_skew_insert(
                heap->policy, singleton, heap->root->children, &children);
            if (status == FT_STATUS_OK) {
                status = ft_ma_tree_create(
                    heap->policy,
                    0,
                    heap->root->element,
                    heap->root->priority,
                    children,
                    heap->policy->identity_action,
                    &root);
            }
        }
    }
    if (status == FT_STATUS_OK) {
        status = ft_ma_heap_adopt(heap->policy, root, count, &produced);
    }
    if (status == FT_STATUS_OK) {
        root = NULL;
        ft_ma_publish_unary(heap, result, produced);
    }

cleanup:
    ft_ma_tree_release(heap->policy, root);
    ft_ma_forest_release(heap->policy, children);
    ft_ma_tree_release(heap->policy, singleton);
    ft_ma_value_release(heap->policy, &heap->policy->config.priority, stored_priority);
    ft_ma_value_release(heap->policy, &heap->policy->config.element, stored_element);
    return status;
}

ft_status ft_monotone_action_heap_from_array(
    ft_monotone_action_heap* heap,
    const ft_monotone_action_policy* policy,
    const ft_monotone_action_entry_input* entries,
    size_t count)
{
    ft_monotone_action_heap working;
    ft_status status = FT_STATUS_OK;
    if (heap == NULL || policy == NULL || policy->rep == NULL ||
        (count != 0 && entries == NULL)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_monotone_action_heap_init(&working, policy);
    for (size_t index = 0; status == FT_STATUS_OK && index != count; ++index) {
        status = ft_monotone_action_heap_insert(
            &working, entries[index].element, entries[index].priority, &working);
    }
    if (status == FT_STATUS_OK) {
        *heap = working;
    } else {
        ft_monotone_action_heap_dispose(&working);
    }
    return status;
}

ft_status ft_monotone_action_heap_transform_all(
    const ft_monotone_action_heap* heap,
    const void* action,
    ft_monotone_action_heap* result)
{
    ft_ma_value* stored_action = NULL;
    ft_monotone_action_tree* tagged = NULL;
    ft_monotone_action_tree* root = NULL;
    ft_monotone_action_heap produced = {0};
    bool identity = false;
    ft_status status = FT_STATUS_OK;
    if (!ft_ma_heap_valid(heap) || action == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (heap->root == NULL) {
        status = ft_monotone_action_heap_copy(heap, &produced);
        if (status == FT_STATUS_OK) {
            ft_ma_publish_unary(heap, result, produced);
        }
        return status;
    }
    status = ft_ma_is_identity_raw(heap->policy, action, &identity);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (identity) {
        status = ft_monotone_action_heap_copy(heap, &produced);
        if (status == FT_STATUS_OK) {
            ft_ma_publish_unary(heap, result, produced);
        }
        return status;
    }
    status = ft_ma_value_create(
        heap->policy, &heap->policy->config.action, action, &stored_action);
    if (status == FT_STATUS_OK) {
        status = ft_ma_tag_tree(heap->policy, heap->root, stored_action, &tagged);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ma_expose(heap->policy, tagged, &root);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ma_heap_adopt(heap->policy, root, heap->count, &produced);
    }
    if (status == FT_STATUS_OK) {
        root = NULL;
        ft_ma_publish_unary(heap, result, produced);
    }
    ft_ma_tree_release(heap->policy, root);
    ft_ma_tree_release(heap->policy, tagged);
    ft_ma_value_release(heap->policy, &heap->policy->config.action, stored_action);
    return status;
}

ft_status ft_monotone_action_heap_meld(
    const ft_monotone_action_heap* left,
    const ft_monotone_action_heap* right,
    ft_monotone_action_heap* result)
{
    ft_monotone_action_tree* root = NULL;
    ft_ma_forest* children = NULL;
    ft_monotone_action_heap produced = {0};
    const ft_monotone_action_tree* winner = NULL;
    ft_monotone_action_tree* loser = NULL;
    size_t count = 0;
    bool left_wins = false;
    ft_status status = FT_STATUS_OK;
    if (!ft_ma_heap_valid(left) || !ft_ma_heap_valid(right) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (left->policy != right->policy) {
        return FT_STATUS_INCOMPATIBLE_POLICY;
    }
    if (ft_ma_add_overflows(left->count, right->count, &count)) {
        return FT_STATUS_OVERFLOW;
    }
    if (left->root == NULL || right->root == NULL) {
        const ft_monotone_action_heap* selected = left->root == NULL ? right : left;
        status = ft_monotone_action_heap_copy(selected, &produced);
        if (status == FT_STATUS_OK) {
            ft_ma_publish_binary(left, right, result, produced);
        }
        return status;
    }
    status = ft_ma_less_equal(left->policy, left->root, right->root, &left_wins);
    if (status != FT_STATUS_OK) {
        return status;
    }
    /* Both roots are retained in exposed form, so the loser keeps its own action history. */
    winner = left_wins ? left->root : right->root;
    loser = left_wins ? right->root : left->root;
    status = ft_ma_skew_insert(left->policy, loser, winner->children, &children);
    if (status == FT_STATUS_OK) {
        status = ft_ma_tree_create(
            left->policy,
            0,
            winner->element,
            winner->priority,
            children,
            left->policy->identity_action,
            &root);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ma_heap_adopt(left->policy, root, count, &produced);
    }
    if (status == FT_STATUS_OK) {
        root = NULL;
        ft_ma_publish_binary(left, right, result, produced);
    }
    ft_ma_tree_release(left->policy, root);
    ft_ma_forest_release(left->policy, children);
    return status;
}

static ft_status ft_ma_delete_nonempty(
    const ft_monotone_action_heap* heap,
    ft_monotone_action_heap* produced)
{
    const ft_monotone_action_policy_rep* policy = heap->policy;
    ft_ma_tree_list children;
    ft_ma_tree_list remainder;
    ft_ma_tree_list minimum_children;
    ft_ma_tree_list zeros;
    ft_ma_tree_list trees;
    ft_ma_tree_list embedded;
    ft_ma_tree_list merged;
    ft_ma_tree_list all;
    ft_monotone_action_tree* minimum = NULL;
    ft_monotone_action_tree* root = NULL;
    ft_ma_forest* forest = NULL;
    size_t minimum_index = 0;
    size_t embedded_start = 0;
    ft_status status = FT_STATUS_OK;
    if (heap->root->children == NULL) {
        if (heap->count != 1) {
            return FT_STATUS_INCONSISTENT_POLICY;
        }
        return ft_ma_heap_adopt(heap->policy, NULL, 0, produced);
    }
    ft_ma_list_init(&children);
    ft_ma_list_init(&remainder);
    ft_ma_list_init(&minimum_children);
    ft_ma_list_init(&zeros);
    ft_ma_list_init(&trees);
    ft_ma_list_init(&embedded);
    ft_ma_list_init(&merged);
    ft_ma_list_init(&all);
    status = ft_ma_forest_to_list(policy, heap->root->children, heap->count, &children);
    if (status != FT_STATUS_OK) {
        goto cleanup;
    }
    if (children.count == 0) {
        status = FT_STATUS_INCONSISTENT_POLICY;
        goto cleanup;
    }
    for (size_t index = 1; index != children.count; ++index) {
        int comparison = 0;
        status = ft_ma_compare_trees(
            policy, children.items[index], children.items[minimum_index], &comparison);
        if (status != FT_STATUS_OK) {
            goto cleanup;
        }
        if (comparison < 0) {
            minimum_index = index;
        }
    }
    status = ft_ma_expose(policy, children.items[minimum_index], &minimum);
    if (status != FT_STATUS_OK) {
        goto cleanup;
    }
    for (size_t index = 0; index != children.count; ++index) {
        if (index == minimum_index) {
            continue;
        }
        status = ft_ma_list_push(policy, &remainder, children.items[index]);
        if (status != FT_STATUS_OK) {
            goto cleanup;
        }
    }
    if (minimum->children != NULL) {
        status = ft_ma_forest_to_list(
            policy, minimum->children, heap->count, &minimum_children);
        if (status != FT_STATUS_OK) {
            goto cleanup;
        }
    }
    status = ft_ma_split_forest(
        policy, minimum->rank, &minimum_children, &zeros, &trees, &embedded_start);
    if (status != FT_STATUS_OK) {
        goto cleanup;
    }
    for (size_t index = embedded_start; index != minimum_children.count; ++index) {
        status = ft_ma_list_push(policy, &embedded, minimum_children.items[index]);
        if (status != FT_STATUS_OK) {
            goto cleanup;
        }
    }
    status = ft_ma_skew_meld(policy, &trees, &remainder, &merged);
    if (status == FT_STATUS_OK) {
        status = ft_ma_skew_meld(policy, &merged, &embedded, &all);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ma_list_to_forest(policy, &all, &forest);
    }
    if (status != FT_STATUS_OK) {
        goto cleanup;
    }
    for (size_t index = 0; index != zeros.count; ++index) {
        ft_ma_forest* next = NULL;
        status = ft_ma_skew_insert(policy, zeros.items[index], forest, &next);
        if (status != FT_STATUS_OK) {
            goto cleanup;
        }
        ft_ma_forest_release(policy, forest);
        forest = next;
    }
    status = ft_ma_tree_create(
        policy, 0, minimum->element, minimum->priority, forest, policy->identity_action, &root);
    if (status == FT_STATUS_OK) {
        status = ft_ma_heap_adopt(heap->policy, root, heap->count - 1, produced);
    }
    if (status == FT_STATUS_OK) {
        root = NULL;
    }

cleanup:
    ft_ma_tree_release(policy, root);
    ft_ma_forest_release(policy, forest);
    ft_ma_tree_release(policy, minimum);
    ft_ma_list_dispose(policy, &all);
    ft_ma_list_dispose(policy, &merged);
    ft_ma_list_dispose(policy, &embedded);
    ft_ma_list_dispose(policy, &trees);
    ft_ma_list_dispose(policy, &zeros);
    ft_ma_list_dispose(policy, &minimum_children);
    ft_ma_list_dispose(policy, &remainder);
    ft_ma_list_dispose(policy, &children);
    return status;
}

ft_status ft_monotone_action_heap_delete_minimum(
    const ft_monotone_action_heap* heap,
    ft_monotone_action_heap* result)
{
    ft_monotone_action_heap produced = {0};
    ft_status status = FT_STATUS_OK;
    if (!ft_ma_heap_valid(heap) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (heap->root == NULL) {
        return FT_STATUS_EMPTY;
    }
    status = ft_ma_delete_nonempty(heap, &produced);
    if (status == FT_STATUS_OK) {
        ft_ma_publish_unary(heap, result, produced);
    }
    return status;
}

ft_status ft_monotone_action_heap_try_delete_minimum(
    const ft_monotone_action_heap* heap,
    bool* removed,
    void* element,
    void* priority,
    ft_monotone_action_heap* result)
{
    ft_monotone_action_heap produced = {0};
    ft_status status = FT_STATUS_OK;
    if (!ft_ma_heap_valid(heap) || removed == NULL || element == NULL || priority == NULL ||
        result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (heap->root == NULL) {
        status = ft_monotone_action_heap_copy(heap, &produced);
        if (status == FT_STATUS_OK) {
            ft_ma_publish_unary(heap, result, produced);
            *removed = false;
        }
        return status;
    }
    status = ft_ma_delete_nonempty(heap, &produced);
    if (status == FT_STATUS_OK) {
        status = ft_ma_copy_out(
            &heap->policy->config.element, element, heap->root->element->bytes);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ma_copy_out(
            &heap->policy->config.priority, priority, heap->root->priority->bytes);
        if (status != FT_STATUS_OK && heap->policy->config.element.destroy != NULL) {
            heap->policy->config.element.destroy(
                element, heap->policy->config.element.context);
        }
    }
    if (status == FT_STATUS_OK) {
        ft_ma_publish_unary(heap, result, produced);
        *removed = true;
    } else {
        ft_monotone_action_heap_dispose(&produced);
    }
    return status;
}

static ft_status ft_ma_allocate_stack(
    const ft_monotone_action_heap* heap,
    ft_ma_frame** stack)
{
    size_t bytes = 0;
    if (heap->count == 0) {
        *stack = NULL;
        return FT_STATUS_OK;
    }
    if (ft_ma_multiply_overflows(heap->count, sizeof(**stack), &bytes)) {
        return FT_STATUS_OVERFLOW;
    }
    *stack = (ft_ma_frame*)ft_ma_allocate(heap->policy, bytes);
    return *stack == NULL ? FT_STATUS_NO_MEMORY : FT_STATUS_OK;
}

ft_status ft_monotone_action_heap_visit(
    const ft_monotone_action_heap* heap,
    ft_monotone_action_visit_fn visitor,
    void* context)
{
    ft_ma_frame* stack = NULL;
    size_t pending = 0;
    ft_status status = FT_STATUS_OK;
    if (!ft_ma_heap_valid(heap) || visitor == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_ma_allocate_stack(heap, &stack);
    if (status != FT_STATUS_OK || heap->root == NULL) {
        return status;
    }
    ft_ma_tree_retain(heap->root);
    stack[pending].tree = heap->root;
    stack[pending].depth = 1;
    ++pending;
    while (pending != 0) {
        ft_monotone_action_tree* tagged = stack[--pending].tree;
        ft_monotone_action_tree* node = NULL;
        ft_ma_forest* cursor = NULL;
        status = ft_ma_expose(heap->policy, tagged, &node);
        ft_ma_tree_release(heap->policy, tagged);
        if (status != FT_STATUS_OK) {
            break;
        }
        status = visitor(node->element->bytes, node->priority->bytes, context);
        cursor = node->children;
        ft_ma_forest_retain(cursor);
        while (status == FT_STATUS_OK && cursor != NULL) {
            ft_monotone_action_tree* head = NULL;
            ft_ma_forest* next = NULL;
            if (pending == heap->count) {
                status = FT_STATUS_INCONSISTENT_POLICY;
                break;
            }
            status = ft_ma_forest_head(heap->policy, cursor, &head);
            if (status != FT_STATUS_OK) {
                break;
            }
            stack[pending].tree = head;
            stack[pending].depth = 0;
            ++pending;
            status = ft_ma_forest_tail(heap->policy, cursor, &next);
            if (status != FT_STATUS_OK) {
                break;
            }
            ft_ma_forest_release(heap->policy, cursor);
            cursor = next;
        }
        ft_ma_forest_release(heap->policy, cursor);
        ft_ma_tree_release(heap->policy, node);
        if (status != FT_STATUS_OK) {
            break;
        }
    }
    while (pending != 0) {
        ft_ma_tree_release(heap->policy, stack[--pending].tree);
    }
    ft_ma_deallocate(heap->policy, stack);
    return status;
}

const void* ft_monotone_action_heap_root_identity(const ft_monotone_action_heap* heap)
{
    return ft_ma_heap_valid(heap) ? heap->root : NULL;
}

/* -------------------------------------------------------------------------------------------
 * Validation
 * ------------------------------------------------------------------------------------------- */

/* Ranks are tag-invariant, so the rank encoding is audited over raw spine cells. */
static bool ft_ma_validate_skew_forest(
    const ft_ma_forest* forest,
    size_t bound,
    size_t* length_result)
{
    size_t length = 0;
    unsigned previous = 0;
    bool has_previous = false;
    bool duplicate = false;
    while (forest != NULL) {
        const unsigned rank = forest->head->rank;
        if (length == bound || (has_previous && rank < previous)) {
            return false;
        }
        if (has_previous && rank == previous) {
            if (length != 1 || duplicate) {
                return false;
            }
            duplicate = true;
        }
        previous = rank;
        has_previous = true;
        ++length;
        forest = forest->tail;
    }
    *length_result = length;
    return true;
}

static bool ft_ma_validate_fused_children(
    const ft_monotone_action_tree* tree,
    const ft_ma_forest** embedded)
{
    unsigned rank = tree->rank;
    const ft_ma_forest* forest = tree->children;
    while (rank != 0) {
        const ft_monotone_action_tree* first = NULL;
        const ft_monotone_action_tree* second = NULL;
        if (forest == NULL) {
            return false;
        }
        first = forest->head;
        if (rank == 1) {
            if (first->rank != 0) {
                return false;
            }
            if (forest->tail == NULL) {
                *embedded = NULL;
                return true;
            }
            *embedded = forest->tail->head->rank == 0 ? forest->tail->tail : forest->tail;
            return true;
        }
        if (forest->tail == NULL) {
            return false;
        }
        second = forest->tail->head;
        if (first->rank == second->rank) {
            if (first->rank != rank - 1) {
                return false;
            }
            *embedded = forest->tail->tail;
            return true;
        }
        if (first->rank == 0) {
            if (second->rank != rank - 1) {
                return false;
            }
            forest = forest->tail->tail;
        } else {
            if (first->rank != rank - 1) {
                return false;
            }
            forest = forest->tail;
        }
        --rank;
    }
    *embedded = forest;
    return true;
}

/* Walks one exposed node's children through the tag-composing accessors, checking logical heap
 * order and pushing each logical child. */
static ft_status ft_ma_validate_children(
    const ft_monotone_action_heap* heap,
    const ft_monotone_action_tree* node,
    size_t depth,
    ft_ma_frame* stack,
    size_t* pending,
    size_t* tagged_components,
    bool* valid)
{
    const ft_monotone_action_policy_rep* policy = heap->policy;
    ft_ma_forest* cursor = node->children;
    ft_status status = FT_STATUS_OK;
    ft_ma_forest_retain(cursor);
    while (cursor != NULL) {
        ft_monotone_action_tree* child = NULL;
        ft_ma_forest* next = NULL;
        bool identity = false;
        bool ordered = false;
        status = ft_ma_is_identity(policy, cursor->action, &identity);
        if (status != FT_STATUS_OK) {
            break;
        }
        if (!identity) {
            ++*tagged_components;
        }
        if (*pending == heap->count || depth == SIZE_MAX) {
            *valid = false;
            break;
        }
        status = ft_ma_forest_head(policy, cursor, &child);
        if (status != FT_STATUS_OK) {
            break;
        }
        status = ft_ma_less_equal(policy, node, child, &ordered);
        if (status != FT_STATUS_OK) {
            ft_ma_tree_release(policy, child);
            break;
        }
        if (!ordered) {
            ft_ma_tree_release(policy, child);
            *valid = false;
            break;
        }
        stack[*pending].tree = child;
        stack[*pending].depth = depth + 1;
        ++*pending;
        status = ft_ma_forest_tail(policy, cursor, &next);
        if (status != FT_STATUS_OK) {
            break;
        }
        ft_ma_forest_release(policy, cursor);
        cursor = next;
    }
    ft_ma_forest_release(policy, cursor);
    return status;
}

ft_status ft_monotone_action_heap_validate(
    const ft_monotone_action_heap* heap,
    bool* valid,
    ft_monotone_action_heap_statistics* statistics)
{
    ft_ma_frame* stack = NULL;
    ft_monotone_action_heap_statistics local = {0, 0, 0, 0, 0};
    size_t pending = 0;
    bool local_valid = true;
    ft_status status = FT_STATUS_OK;
    if (!ft_ma_heap_valid(heap) || valid == NULL || statistics == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (heap->root == NULL) {
        *valid = true;
        *statistics = local;
        return FT_STATUS_OK;
    }
    if (heap->root->rank != 0 ||
        !ft_ma_validate_skew_forest(
            heap->root->children, heap->count, &local.root_forest_length)) {
        *valid = false;
        *statistics = local;
        return FT_STATUS_OK;
    }
    status = ft_ma_allocate_stack(heap, &stack);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_ma_tree_retain(heap->root);
    stack[pending].tree = heap->root;
    stack[pending].depth = 1;
    ++pending;
    while (pending != 0 && local_valid) {
        ft_ma_frame frame = stack[--pending];
        ft_monotone_action_tree* node = NULL;
        const ft_ma_forest* embedded = NULL;
        size_t ignored_length = 0;
        bool identity = false;
        status = ft_ma_is_identity(heap->policy, frame.tree->action, &identity);
        if (status == FT_STATUS_OK) {
            if (!identity) {
                ++local.tagged_component_count;
            }
            status = ft_ma_expose(heap->policy, frame.tree, &node);
        }
        ft_ma_tree_release(heap->policy, frame.tree);
        if (status != FT_STATUS_OK) {
            goto cleanup;
        }
        if (local.count == heap->count ||
            !ft_ma_validate_fused_children(node, &embedded) ||
            !ft_ma_validate_skew_forest(embedded, heap->count, &ignored_length)) {
            ft_ma_tree_release(heap->policy, node);
            local_valid = false;
            break;
        }
        ++local.count;
        if (node->rank > local.maximum_rank) {
            local.maximum_rank = node->rank;
        }
        if (frame.depth > local.maximum_depth) {
            local.maximum_depth = frame.depth;
        }
        status = ft_ma_validate_children(
            heap,
            node,
            frame.depth,
            stack,
            &pending,
            &local.tagged_component_count,
            &local_valid);
        ft_ma_tree_release(heap->policy, node);
        if (status != FT_STATUS_OK) {
            goto cleanup;
        }
    }
    if (local_valid && local.count != heap->count) {
        local_valid = false;
    }

cleanup:
    while (pending != 0) {
        ft_ma_tree_release(heap->policy, stack[--pending].tree);
    }
    ft_ma_deallocate(heap->policy, stack);
    if (status == FT_STATUS_OK) {
        *valid = local_valid;
        *statistics = local;
    }
    return status;
}

/* -------------------------------------------------------------------------------------------
 * The shipped clamp family
 * ------------------------------------------------------------------------------------------- */

struct ft_monotone_action_clamp_bound {
    ft_ma_ref_count refs;
    void* bytes;
};

struct ft_monotone_action_clamp_family_rep {
    ft_ma_ref_count refs;
    ft_monotone_action_type_policy priority;
    ft_monotone_action_compare_fn compare;
    ft_monotone_action_allocator allocator;
    ft_monotone_action_clamp identity;
};

static const unsigned char ft_ma_clamp_type_identity = 0;

static void ft_ma_bound_retain(ft_monotone_action_clamp_bound* bound)
{
    if (bound != NULL) {
        ft_ma_ref_retain(&bound->refs);
    }
}

static void ft_ma_bound_release(
    const ft_monotone_action_clamp_family_rep* rep,
    ft_monotone_action_clamp_bound* bound)
{
    if (bound == NULL || !ft_ma_ref_release(&bound->refs)) {
        return;
    }
    if (rep->priority.destroy != NULL) {
        rep->priority.destroy(bound->bytes, rep->priority.context);
    }
    ft_ma_deallocate_with(&rep->allocator, bound->bytes);
    ft_ma_deallocate_with(&rep->allocator, bound);
}

static ft_status ft_ma_bound_create(
    const ft_monotone_action_clamp_family_rep* rep,
    const void* source,
    ft_monotone_action_clamp_bound** result)
{
    ft_monotone_action_clamp_bound* bound = NULL;
    ft_status status = FT_STATUS_OK;
    if (source == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    bound = (ft_monotone_action_clamp_bound*)ft_ma_allocate_with(
        &rep->allocator, sizeof(*bound));
    if (bound == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    bound->bytes = ft_ma_allocate_with(&rep->allocator, rep->priority.size);
    if (bound->bytes == NULL) {
        ft_ma_deallocate_with(&rep->allocator, bound);
        return FT_STATUS_NO_MEMORY;
    }
    if (rep->priority.copy == NULL) {
        (void)memcpy(bound->bytes, source, rep->priority.size);
    } else {
        status = rep->priority.copy(bound->bytes, source, rep->priority.context);
        if (status != FT_STATUS_OK) {
            ft_ma_deallocate_with(&rep->allocator, bound->bytes);
            ft_ma_deallocate_with(&rep->allocator, bound);
            return status;
        }
    }
    ft_ma_ref_init(&bound->refs);
    *result = bound;
    return FT_STATUS_OK;
}

static ft_status ft_ma_clamp_compare(
    const ft_monotone_action_clamp_family_rep* rep,
    const void* left,
    const void* right,
    int* comparison)
{
    return rep->compare(left, right, comparison, rep->priority.context);
}

/* Chooses the exact representative one clamp yields for a priority. A null selection means the
 * priority passes through unchanged. */
static ft_status ft_ma_clamp_select(
    const ft_monotone_action_clamp_family_rep* rep,
    const ft_monotone_action_clamp* action,
    const void* priority,
    ft_monotone_action_clamp_bound** selected)
{
    int comparison = 0;
    ft_status status = FT_STATUS_OK;
    if (action->constant != NULL) {
        *selected = action->constant;
        return FT_STATUS_OK;
    }
    if (action->lower != NULL) {
        status = ft_ma_clamp_compare(rep, priority, action->lower->bytes, &comparison);
        if (status != FT_STATUS_OK) {
            return status;
        }
        if (comparison < 0) {
            *selected = action->lower;
            return FT_STATUS_OK;
        }
    }
    if (action->upper != NULL) {
        status = ft_ma_clamp_compare(rep, priority, action->upper->bytes, &comparison);
        if (status != FT_STATUS_OK) {
            return status;
        }
        if (comparison > 0) {
            *selected = action->upper;
            return FT_STATUS_OK;
        }
    }
    *selected = NULL;
    return FT_STATUS_OK;
}

static ft_status ft_ma_clamp_copy_callback(
    void* destination,
    const void* source,
    void* context)
{
    const ft_monotone_action_clamp* from = (const ft_monotone_action_clamp*)source;
    ft_monotone_action_clamp* to = (ft_monotone_action_clamp*)destination;
    (void)context;
    to->constant = from->constant;
    to->lower = from->lower;
    to->upper = from->upper;
    ft_ma_bound_retain(to->constant);
    ft_ma_bound_retain(to->lower);
    ft_ma_bound_retain(to->upper);
    return FT_STATUS_OK;
}

static void ft_ma_clamp_destroy_callback(void* value, void* context)
{
    ft_monotone_action_clamp* action = (ft_monotone_action_clamp*)value;
    const ft_monotone_action_clamp_family_rep* rep =
        (const ft_monotone_action_clamp_family_rep*)context;
    ft_ma_bound_release(rep, action->constant);
    ft_ma_bound_release(rep, action->lower);
    ft_ma_bound_release(rep, action->upper);
    action->constant = NULL;
    action->lower = NULL;
    action->upper = NULL;
}

static ft_status ft_ma_clamp_is_identity_callback(
    const void* action,
    bool* is_identity,
    void* context)
{
    const ft_monotone_action_clamp* clamp = (const ft_monotone_action_clamp*)action;
    (void)context;
    *is_identity =
        clamp->constant == NULL && clamp->lower == NULL && clamp->upper == NULL;
    return FT_STATUS_OK;
}

static void ft_ma_clamp_publish(
    ft_monotone_action_clamp* destination,
    ft_monotone_action_clamp_bound* constant,
    ft_monotone_action_clamp_bound* lower,
    ft_monotone_action_clamp_bound* upper)
{
    ft_ma_bound_retain(constant);
    ft_ma_bound_retain(lower);
    ft_ma_bound_retain(upper);
    destination->constant = constant;
    destination->lower = lower;
    destination->upper = upper;
}

static ft_status ft_ma_clamp_compose_callback(
    void* destination,
    const void* outer_action,
    const void* inner_action,
    void* context)
{
    const ft_monotone_action_clamp* outer = (const ft_monotone_action_clamp*)outer_action;
    const ft_monotone_action_clamp* inner = (const ft_monotone_action_clamp*)inner_action;
    ft_monotone_action_clamp_family_rep* rep =
        (ft_monotone_action_clamp_family_rep*)context;
    ft_monotone_action_clamp_bound* lower = NULL;
    ft_monotone_action_clamp_bound* upper = NULL;
    ft_monotone_action_clamp_bound* selected = NULL;
    int comparison = 0;
    ft_status status = FT_STATUS_OK;
    bool outer_identity =
        outer->constant == NULL && outer->lower == NULL && outer->upper == NULL;
    bool inner_identity =
        inner->constant == NULL && inner->lower == NULL && inner->upper == NULL;
    if (outer_identity) {
        ft_ma_clamp_publish(
            (ft_monotone_action_clamp*)destination, inner->constant, inner->lower, inner->upper);
        return FT_STATUS_OK;
    }
    if (inner_identity || outer->constant != NULL) {
        ft_ma_clamp_publish(
            (ft_monotone_action_clamp*)destination, outer->constant, outer->lower, outer->upper);
        return FT_STATUS_OK;
    }
    if (inner->constant != NULL) {
        status = ft_ma_clamp_select(rep, outer, inner->constant->bytes, &selected);
        if (status != FT_STATUS_OK) {
            return status;
        }
        ft_ma_clamp_publish(
            (ft_monotone_action_clamp*)destination,
            selected == NULL ? inner->constant : selected,
            NULL,
            NULL);
        return FT_STATUS_OK;
    }
    /* Disjoint bounds collapse to an explicit constant carrying the newer boundary. */
    if (inner->upper != NULL && outer->lower != NULL) {
        status = ft_ma_clamp_compare(
            rep, inner->upper->bytes, outer->lower->bytes, &comparison);
        if (status != FT_STATUS_OK) {
            return status;
        }
        if (comparison < 0) {
            ft_ma_clamp_publish(
                (ft_monotone_action_clamp*)destination, outer->lower, NULL, NULL);
            return FT_STATUS_OK;
        }
    }
    if (inner->lower != NULL && outer->upper != NULL) {
        status = ft_ma_clamp_compare(
            rep, inner->lower->bytes, outer->upper->bytes, &comparison);
        if (status != FT_STATUS_OK) {
            return status;
        }
        if (comparison > 0) {
            ft_ma_clamp_publish(
                (ft_monotone_action_clamp*)destination, outer->upper, NULL, NULL);
            return FT_STATUS_OK;
        }
    }
    /* Overlapping bounds intersect; equal boundaries keep the older representative. */
    if (inner->lower == NULL) {
        lower = outer->lower;
    } else if (outer->lower == NULL) {
        lower = inner->lower;
    } else {
        status = ft_ma_clamp_compare(
            rep, inner->lower->bytes, outer->lower->bytes, &comparison);
        if (status != FT_STATUS_OK) {
            return status;
        }
        lower = comparison >= 0 ? inner->lower : outer->lower;
    }
    if (inner->upper == NULL) {
        upper = outer->upper;
    } else if (outer->upper == NULL) {
        upper = inner->upper;
    } else {
        status = ft_ma_clamp_compare(
            rep, inner->upper->bytes, outer->upper->bytes, &comparison);
        if (status != FT_STATUS_OK) {
            return status;
        }
        upper = comparison <= 0 ? inner->upper : outer->upper;
    }
    ft_ma_clamp_publish((ft_monotone_action_clamp*)destination, NULL, lower, upper);
    return FT_STATUS_OK;
}

static ft_status ft_ma_clamp_apply_callback(
    void* destination,
    const void* action,
    const void* priority,
    void* context)
{
    const ft_monotone_action_clamp* clamp = (const ft_monotone_action_clamp*)action;
    ft_monotone_action_clamp_family_rep* rep =
        (ft_monotone_action_clamp_family_rep*)context;
    ft_monotone_action_clamp_bound* selected = NULL;
    const void* source = NULL;
    ft_status status = ft_ma_clamp_select(rep, clamp, priority, &selected);
    if (status != FT_STATUS_OK) {
        return status;
    }
    source = selected == NULL ? priority : selected->bytes;
    if (rep->priority.copy == NULL) {
        (void)memcpy(destination, source, rep->priority.size);
        return FT_STATUS_OK;
    }
    return rep->priority.copy(destination, source, rep->priority.context);
}

ft_status ft_monotone_action_clamp_family_create(
    ft_monotone_action_clamp_family* family,
    const ft_monotone_action_type_policy* priority,
    ft_monotone_action_compare_fn compare,
    const ft_monotone_action_allocator* allocator)
{
    ft_monotone_action_clamp_family_rep* rep = NULL;
    ft_monotone_action_allocator effective;
    if (family == NULL || priority == NULL || compare == NULL ||
        !ft_ma_type_valid(priority)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (allocator == NULL) {
        effective.allocate = ft_ma_default_allocate;
        effective.deallocate = ft_ma_default_deallocate;
        effective.context = NULL;
    } else {
        if (allocator->allocate == NULL || allocator->deallocate == NULL) {
            return FT_STATUS_INVALID_ARGUMENT;
        }
        effective = *allocator;
    }
    rep = (ft_monotone_action_clamp_family_rep*)ft_ma_allocate_with(
        &effective, sizeof(*rep));
    if (rep == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    ft_ma_ref_init(&rep->refs);
    rep->priority = *priority;
    rep->compare = compare;
    rep->allocator = effective;
    rep->identity.constant = NULL;
    rep->identity.lower = NULL;
    rep->identity.upper = NULL;
    family->rep = rep;
    return FT_STATUS_OK;
}

ft_status ft_monotone_action_clamp_family_copy(
    const ft_monotone_action_clamp_family* source,
    ft_monotone_action_clamp_family* destination)
{
    if (source == NULL || source->rep == NULL || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return FT_STATUS_OK;
    }
    ft_ma_ref_retain(&source->rep->refs);
    destination->rep = source->rep;
    return FT_STATUS_OK;
}

void ft_monotone_action_clamp_family_move(
    ft_monotone_action_clamp_family* destination,
    ft_monotone_action_clamp_family* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    destination->rep = source->rep;
    source->rep = NULL;
}

void ft_monotone_action_clamp_family_dispose(ft_monotone_action_clamp_family* family)
{
    if (family == NULL || family->rep == NULL) {
        return;
    }
    if (ft_ma_ref_release(&family->rep->refs)) {
        ft_monotone_action_allocator allocator = family->rep->allocator;
        ft_ma_deallocate_with(&allocator, family->rep);
    }
    family->rep = NULL;
}

bool ft_monotone_action_clamp_family_same(
    const ft_monotone_action_clamp_family* left,
    const ft_monotone_action_clamp_family* right)
{
    return left != NULL && right != NULL && left->rep != NULL && left->rep == right->rep;
}

void ft_monotone_action_clamp_family_config_init(
    const ft_monotone_action_clamp_family* family,
    ft_monotone_action_policy_config* config,
    const ft_monotone_action_type_policy* element)
{
    if (config == NULL || family == NULL || family->rep == NULL) {
        return;
    }
    (void)memset(config, 0, sizeof(*config));
    if (element != NULL) {
        config->element = *element;
    }
    config->priority = family->rep->priority;
    config->action.size = sizeof(ft_monotone_action_clamp);
    config->action.type_identity = &ft_ma_clamp_type_identity;
    config->action.copy = ft_ma_clamp_copy_callback;
    config->action.destroy = ft_ma_clamp_destroy_callback;
    config->action.context = family->rep;
    config->identity_action = &family->rep->identity;
    config->priority_compare = family->rep->compare;
    config->is_identity = ft_ma_clamp_is_identity_callback;
    config->compose = ft_ma_clamp_compose_callback;
    config->apply = ft_ma_clamp_apply_callback;
    config->algebra_context = family->rep;
    config->allocator = family->rep->allocator;
}

void ft_monotone_action_clamp_init_identity(ft_monotone_action_clamp* action)
{
    if (action == NULL) {
        return;
    }
    action->constant = NULL;
    action->lower = NULL;
    action->upper = NULL;
}

ft_status ft_monotone_action_clamp_at_least(
    const ft_monotone_action_clamp_family* family,
    const void* lower_bound,
    ft_monotone_action_clamp* action)
{
    ft_monotone_action_clamp_bound* lower = NULL;
    ft_status status = FT_STATUS_OK;
    if (family == NULL || family->rep == NULL || action == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_ma_bound_create(family->rep, lower_bound, &lower);
    if (status != FT_STATUS_OK) {
        return status;
    }
    action->constant = NULL;
    action->lower = lower;
    action->upper = NULL;
    return FT_STATUS_OK;
}

ft_status ft_monotone_action_clamp_at_most(
    const ft_monotone_action_clamp_family* family,
    const void* upper_bound,
    ft_monotone_action_clamp* action)
{
    ft_monotone_action_clamp_bound* upper = NULL;
    ft_status status = FT_STATUS_OK;
    if (family == NULL || family->rep == NULL || action == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_ma_bound_create(family->rep, upper_bound, &upper);
    if (status != FT_STATUS_OK) {
        return status;
    }
    action->constant = NULL;
    action->lower = NULL;
    action->upper = upper;
    return FT_STATUS_OK;
}

ft_status ft_monotone_action_clamp_between(
    const ft_monotone_action_clamp_family* family,
    const void* lower_bound,
    const void* upper_bound,
    ft_monotone_action_clamp* action)
{
    ft_monotone_action_clamp_bound* lower = NULL;
    ft_monotone_action_clamp_bound* upper = NULL;
    int comparison = 0;
    ft_status status = FT_STATUS_OK;
    if (family == NULL || family->rep == NULL || action == NULL || lower_bound == NULL ||
        upper_bound == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_ma_clamp_compare(family->rep, lower_bound, upper_bound, &comparison);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (comparison > 0) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_ma_bound_create(family->rep, lower_bound, &lower);
    if (status == FT_STATUS_OK) {
        status = ft_ma_bound_create(family->rep, upper_bound, &upper);
        if (status != FT_STATUS_OK) {
            ft_ma_bound_release(family->rep, lower);
        }
    }
    if (status != FT_STATUS_OK) {
        return status;
    }
    action->constant = NULL;
    action->lower = lower;
    action->upper = upper;
    return FT_STATUS_OK;
}

ft_status ft_monotone_action_clamp_constant(
    const ft_monotone_action_clamp_family* family,
    const void* value,
    ft_monotone_action_clamp* action)
{
    ft_monotone_action_clamp_bound* constant = NULL;
    ft_status status = FT_STATUS_OK;
    if (family == NULL || family->rep == NULL || action == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_ma_bound_create(family->rep, value, &constant);
    if (status != FT_STATUS_OK) {
        return status;
    }
    action->constant = constant;
    action->lower = NULL;
    action->upper = NULL;
    return FT_STATUS_OK;
}

ft_status ft_monotone_action_clamp_copy(
    const ft_monotone_action_clamp_family* family,
    const ft_monotone_action_clamp* source,
    ft_monotone_action_clamp* destination)
{
    if (family == NULL || family->rep == NULL || source == NULL || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return FT_STATUS_OK;
    }
    return ft_ma_clamp_copy_callback(destination, source, family->rep);
}

void ft_monotone_action_clamp_dispose(
    const ft_monotone_action_clamp_family* family,
    ft_monotone_action_clamp* action)
{
    if (family == NULL || family->rep == NULL || action == NULL) {
        return;
    }
    ft_ma_clamp_destroy_callback(action, family->rep);
}

bool ft_monotone_action_clamp_is_identity(const ft_monotone_action_clamp* action)
{
    return action != NULL && action->constant == NULL && action->lower == NULL &&
        action->upper == NULL;
}

bool ft_monotone_action_clamp_is_constant(const ft_monotone_action_clamp* action)
{
    return action != NULL && action->constant != NULL;
}

const void* ft_monotone_action_clamp_constant_ref(const ft_monotone_action_clamp* action)
{
    if (action == NULL || action->constant == NULL) {
        return NULL;
    }
    return action->constant->bytes;
}

const void* ft_monotone_action_clamp_lower_bound_ref(const ft_monotone_action_clamp* action)
{
    if (action == NULL || action->lower == NULL) {
        return NULL;
    }
    return action->lower->bytes;
}

const void* ft_monotone_action_clamp_upper_bound_ref(const ft_monotone_action_clamp* action)
{
    if (action == NULL || action->upper == NULL) {
        return NULL;
    }
    return action->upper->bytes;
}
