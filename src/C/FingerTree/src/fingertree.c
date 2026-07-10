#include <tools/data_structures/finger_tree/fingertree.h>

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/* 64-bit count: matches the POSIX build's atomic_size_t width so >2^31
 * retains of one shared node cannot wrap the count on 64-bit Windows. */
typedef volatile LONG64 ft_ref_count;
typedef PVOID volatile ft_atomic_ptr;

static void ft_ref_init(ft_ref_count* count)
{
    *count = 1;
}

static void ft_ref_retain(ft_ref_count* count)
{
    (void)InterlockedIncrement64(count);
}

static bool ft_ref_release(ft_ref_count* count)
{
    return InterlockedDecrement64(count) == 0;
}

static void ft_atomic_ptr_init(ft_atomic_ptr* target, void* value)
{
    *target = value;
}

static void* ft_atomic_ptr_load(ft_atomic_ptr* target)
{
    /* Acquire load without a locked RMW: reading an already-published cached
     * measure or forced middle must not dirty the cache line, or concurrent
     * readers of a shared snapshot serialize on it. */
#if defined(_MSC_VER)
    return ReadPointerAcquire(target);
#else
    /* MinGW's Windows SDK headers do not expose ReadPointerAcquire. GCC and
     * Clang provide the same acquire operation directly, without falling
     * back to an Interlocked compare/exchange read. */
    return __atomic_load_n(target, __ATOMIC_ACQUIRE);
#endif
}

static bool ft_atomic_ptr_compare_exchange(ft_atomic_ptr* target, void** expected, void* desired)
{
    void* actual = InterlockedCompareExchangePointer(target, desired, *expected);
    if (actual == *expected) {
        return true;
    }

    *expected = actual;
    return false;
}
#else
#include <stdatomic.h>
typedef atomic_size_t ft_ref_count;
typedef _Atomic(void*) ft_atomic_ptr;

static void ft_ref_init(ft_ref_count* count)
{
    atomic_init(count, 1);
}

static void ft_ref_retain(ft_ref_count* count)
{
    (void)atomic_fetch_add_explicit(count, 1, memory_order_relaxed);
}

static bool ft_ref_release(ft_ref_count* count)
{
    return atomic_fetch_sub_explicit(count, 1, memory_order_acq_rel) == 1;
}

static void ft_atomic_ptr_init(ft_atomic_ptr* target, void* value)
{
    atomic_init(target, value);
}

static void* ft_atomic_ptr_load(ft_atomic_ptr* target)
{
    return atomic_load_explicit(target, memory_order_acquire);
}

static bool ft_atomic_ptr_compare_exchange(ft_atomic_ptr* target, void** expected, void* desired)
{
    return atomic_compare_exchange_strong_explicit(
        target,
        expected,
        desired,
        memory_order_acq_rel,
        memory_order_acquire);
}
#endif

typedef enum ft_element_kind {
    FT_ELEMENT_LEAF,
    FT_ELEMENT_NODE
} ft_element_kind;

typedef struct ft_element ft_element;
typedef struct ft_node ft_node;
typedef struct ft_middle ft_middle;

struct ft_element {
    ft_element_kind kind;
    size_t leaf_count;
    const void* last_leaf;
    void* measure;
    union {
        void* value;
        ft_node* node;
    } as;
};

struct ft_node {
    ft_ref_count ref_count;
    size_t child_count;
    size_t leaf_count;
    void* measure;
    ft_element children[3];
};

typedef enum ft_rep_kind {
    FT_REP_EMPTY,
    FT_REP_SINGLE,
    FT_REP_DEEP
} ft_rep_kind;

struct ft_tree_rep {
    ft_ref_count ref_count;
    ft_rep_kind kind;
    size_t leaf_count;
    const void* last_leaf;
    ft_atomic_ptr measure;
    union {
        ft_element single;
        struct {
            size_t prefix_count;
            ft_element prefix[4];
            ft_middle* middle;
            size_t suffix_count;
            ft_element suffix[4];
        } deep;
    } as;
};

typedef enum ft_middle_kind {
    FT_MIDDLE_COMPUTED,
    FT_MIDDLE_PUSH_FRONT,
    FT_MIDDLE_PUSH_BACK,
    FT_MIDDLE_POP_FRONT,
    FT_MIDDLE_POP_BACK
} ft_middle_kind;

struct ft_middle {
    ft_ref_count ref_count;
    ft_atomic_ptr computed;
    ft_middle_kind kind;
    size_t leaf_count;
    void* measure;
    union {
        struct {
            ft_tree_rep* source;
            ft_element element;
        } push;
        struct {
            ft_tree_rep* source;
        } pop;
    } as;
};

typedef enum ft_rev_element_kind {
    FT_REV_ELEMENT_LEAF,
    FT_REV_ELEMENT_NODE
} ft_rev_element_kind;

typedef struct ft_rev_element ft_rev_element;
typedef struct ft_rev_node ft_rev_node;

struct ft_rev_element {
    ft_rev_element_kind kind;
    size_t leaf_count;
    union {
        void* value;
        ft_rev_node* node;
    } as;
};

struct ft_rev_node {
    ft_ref_count ref_count;
    bool reversed;
    size_t child_count;
    size_t leaf_count;
    ft_rev_element children[3];
};

typedef enum ft_rev_rep_kind {
    FT_REV_REP_EMPTY,
    FT_REV_REP_SINGLE,
    FT_REV_REP_DEEP
} ft_rev_rep_kind;

struct ft_reversible_deque_rep {
    ft_ref_count ref_count;
    ft_rev_rep_kind kind;
    bool reversed;
    size_t leaf_count;
    union {
        ft_rev_element single;
        struct {
            size_t prefix_count;
            ft_rev_element prefix[3];
            ft_reversible_deque_rep* middle;
            size_t suffix_count;
            ft_rev_element suffix[3];
        } deep;
    } as;
};

typedef struct ft_rev_split_result {
    ft_reversible_deque_rep* left;
    ft_rev_element hit;
    size_t index_in_hit;
    ft_reversible_deque_rep* right;
} ft_rev_split_result;

typedef struct ft_split_result {
    ft_tree_rep* left;
    ft_element hit;
    size_t index_in_hit;
    ft_tree_rep* right;
} ft_split_result;

typedef struct ft_locate_state {
    size_t index;
    const ft_tree_policy* policy;
    ft_measure_predicate_fn predicate;
    void* predicate_context;
    void* accumulator;
    const ft_element* hit;
} ft_locate_state;

static bool ft_add_overflows(size_t left, size_t right, size_t* result)
{
    if (left > SIZE_MAX - right) {
        return true;
    }

    *result = left + right;
    return false;
}

static void* ft_allocate(size_t size)
{
    return malloc(size == 0 ? 1 : size);
}

static void ft_value_copy(const ft_value_type* type, void* destination, const void* source)
{
    if (type->copy != NULL) {
        type->copy(destination, source, type->context);
        return;
    }

    (void)memcpy(destination, source, type->size);
}

static void ft_value_destroy(const ft_value_type* type, void* value)
{
    if (value != NULL && type->destroy != NULL) {
        type->destroy(value, type->context);
    }
}

static void ft_measure_identity(const ft_measure_policy* policy, void* destination)
{
    policy->identity(destination, policy->context);
}

static void ft_measure_for_value(const ft_measure_policy* policy, void* destination, const void* value)
{
    policy->measure(destination, value, policy->context);
}

static void ft_measure_combine(const ft_measure_policy* policy, void* destination, const void* left, const void* right)
{
    policy->combine(destination, left, right, policy->context);
}

static ft_status ft_measure_new_identity(const ft_measure_policy* policy, void** result)
{
    void* measure = ft_allocate(policy->size);
    if (measure == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    ft_measure_identity(policy, measure);
    *result = measure;
    return FT_STATUS_OK;
}

static ft_status ft_measure_new_copy(const ft_measure_policy* policy, const void* source, void** result)
{
    void* measure = ft_allocate(policy->size);
    if (measure == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    (void)memcpy(measure, source, policy->size);
    *result = measure;
    return FT_STATUS_OK;
}

static ft_status ft_measure_new_combine(
    const ft_measure_policy* policy,
    const void* left,
    const void* right,
    void** result)
{
    void* measure = ft_allocate(policy->size);
    if (measure == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    ft_measure_combine(policy, measure, left, right);
    *result = measure;
    return FT_STATUS_OK;
}

static void ft_node_retain(ft_node* node)
{
    if (node != NULL) {
        ft_ref_retain(&node->ref_count);
    }
}

static void ft_element_dispose(const ft_tree_policy* policy, ft_element* element);

static void ft_node_release(const ft_tree_policy* policy, ft_node* node)
{
    if (node == NULL) {
        return;
    }

    if (!ft_ref_release(&node->ref_count)) {
        return;
    }

    for (size_t index = 0; index != node->child_count; ++index) {
        ft_element_dispose(policy, &node->children[index]);
    }

    free(node->measure);
    free(node);
}

static ft_status ft_element_clone(const ft_tree_policy* policy, const ft_element* source, ft_element* destination)
{
    (void)memset(destination, 0, sizeof(*destination));
    destination->kind = source->kind;
    destination->leaf_count = source->leaf_count;

    if (source->kind == FT_ELEMENT_LEAF) {
        destination->as.value = ft_allocate(policy->value.size);
        if (destination->as.value == NULL) {
            (void)memset(destination, 0, sizeof(*destination));
            return FT_STATUS_NO_MEMORY;
        }

        ft_value_copy(&policy->value, destination->as.value, source->as.value);
        destination->last_leaf = destination->as.value;

        destination->measure = ft_allocate(policy->measure.size);
        if (destination->measure == NULL) {
            ft_value_destroy(&policy->value, destination->as.value);
            free(destination->as.value);
            (void)memset(destination, 0, sizeof(*destination));
            return FT_STATUS_NO_MEMORY;
        }

        /* Re-measure a copied leaf from its copied value. Besides preserving
         * ordinary value measures, this lets a measure safely borrow storage
         * from its owning leaf (the generic interval facade's max-high
         * annotation does so) instead of retaining a pointer into the source
         * leaf that may be released immediately after the clone. */
        ft_measure_for_value(&policy->measure, destination->measure, destination->as.value);
        return FT_STATUS_OK;
    }

    ft_status status = ft_measure_new_copy(&policy->measure, source->measure, &destination->measure);
    if (status != FT_STATUS_OK) {
        return status;
    }

    destination->as.node = source->as.node;
    destination->last_leaf = source->last_leaf;
    ft_node_retain(destination->as.node);
    return FT_STATUS_OK;
}

static ft_status ft_element_init_leaf(const ft_tree_policy* policy, const void* value, ft_element* destination)
{
    (void)memset(destination, 0, sizeof(*destination));
    destination->kind = FT_ELEMENT_LEAF;
    destination->leaf_count = 1;
    destination->as.value = ft_allocate(policy->value.size);
    if (destination->as.value == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    ft_value_copy(&policy->value, destination->as.value, value);
    destination->last_leaf = destination->as.value;

    destination->measure = ft_allocate(policy->measure.size);
    if (destination->measure == NULL) {
        ft_value_destroy(&policy->value, destination->as.value);
        free(destination->as.value);
        (void)memset(destination, 0, sizeof(*destination));
        return FT_STATUS_NO_MEMORY;
    }

    ft_measure_for_value(&policy->measure, destination->measure, destination->as.value);
    return FT_STATUS_OK;
}

static void ft_element_dispose(const ft_tree_policy* policy, ft_element* element)
{
    if (element->measure == NULL) {
        return;
    }

    if (element->kind == FT_ELEMENT_LEAF) {
        ft_value_destroy(&policy->value, element->as.value);
        free(element->as.value);
    } else {
        ft_node_release(policy, element->as.node);
    }

    free(element->measure);
    (void)memset(element, 0, sizeof(*element));
}

static ft_status ft_node_create(const ft_tree_policy* policy, const ft_element* children, size_t count, ft_node** result)
{
    if (count < 2 || count > 3) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_node* node = (ft_node*)calloc(1, sizeof(*node));
    if (node == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    ft_ref_init(&node->ref_count);
    node->child_count = count;

    ft_status status = FT_STATUS_OK;
    for (size_t index = 0; index != count; ++index) {
        status = ft_element_clone(policy, &children[index], &node->children[index]);
        if (status != FT_STATUS_OK) {
            node->child_count = index;
            ft_node_release(policy, node);
            return status;
        }

        if (ft_add_overflows(node->leaf_count, children[index].leaf_count, &node->leaf_count)) {
            node->child_count = index + 1;
            ft_node_release(policy, node);
            return FT_STATUS_OVERFLOW;
        }
    }

    status = ft_measure_new_copy(&policy->measure, node->children[0].measure, &node->measure);
    if (status != FT_STATUS_OK) {
        ft_node_release(policy, node);
        return status;
    }

    for (size_t index = 1; index != count; ++index) {
        void* combined = NULL;
        status = ft_measure_new_combine(&policy->measure, node->measure, node->children[index].measure, &combined);
        if (status != FT_STATUS_OK) {
            ft_node_release(policy, node);
            return status;
        }

        free(node->measure);
        node->measure = combined;
    }

    *result = node;
    return FT_STATUS_OK;
}

static ft_status ft_element_init_node(const ft_tree_policy* policy, const ft_element* children, size_t count, ft_element* result)
{
    (void)memset(result, 0, sizeof(*result));

    ft_node* node = NULL;
    ft_status status = ft_node_create(policy, children, count, &node);
    if (status != FT_STATUS_OK) {
        return status;
    }

    result->kind = FT_ELEMENT_NODE;
    result->leaf_count = node->leaf_count;
    result->as.node = node;
    result->last_leaf = node->children[node->child_count - 1].last_leaf;
    status = ft_measure_new_copy(&policy->measure, node->measure, &result->measure);
    if (status != FT_STATUS_OK) {
        ft_node_release(policy, node);
        (void)memset(result, 0, sizeof(*result));
        return status;
    }

    return FT_STATUS_OK;
}

static void ft_rep_retain(ft_tree_rep* rep)
{
    if (rep != NULL) {
        ft_ref_retain(&rep->ref_count);
    }
}

static void ft_middle_release(const ft_tree_policy* policy, ft_middle* middle);
static ft_status ft_rep_get_measure(const ft_tree_policy* policy, const ft_tree_rep* rep, const void** measure);
static ft_status ft_rep_snoc(const ft_tree_policy* policy, const ft_tree_rep* rep, const ft_element* value, ft_tree_rep** result);
static ft_status ft_rep_cons(const ft_tree_policy* policy, const ft_tree_rep* rep, const ft_element* value, ft_tree_rep** result);
static ft_status ft_rep_view_left(
    const ft_tree_policy* policy,
    const ft_tree_rep* rep,
    ft_element* value,
    ft_tree_rep** rest);
static ft_status ft_rep_view_right(
    const ft_tree_policy* policy,
    const ft_tree_rep* rep,
    ft_element* value,
    ft_tree_rep** rest);

static void ft_rep_release(const ft_tree_policy* policy, ft_tree_rep* rep)
{
    if (rep == NULL) {
        return;
    }

    if (!ft_ref_release(&rep->ref_count)) {
        return;
    }

    if (rep->kind == FT_REP_SINGLE) {
        ft_element_dispose(policy, &rep->as.single);
    } else if (rep->kind == FT_REP_DEEP) {
        for (size_t index = 0; index != rep->as.deep.prefix_count; ++index) {
            ft_element_dispose(policy, &rep->as.deep.prefix[index]);
        }

        ft_middle_release(policy, rep->as.deep.middle);

        for (size_t index = 0; index != rep->as.deep.suffix_count; ++index) {
            ft_element_dispose(policy, &rep->as.deep.suffix[index]);
        }
    }

    free(ft_atomic_ptr_load(&rep->measure));
    free(rep);
}

static ft_status ft_rep_create_empty(const ft_tree_policy* policy, ft_tree_rep** result)
{
    ft_tree_rep* rep = (ft_tree_rep*)calloc(1, sizeof(*rep));
    if (rep == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    ft_ref_init(&rep->ref_count);
    rep->kind = FT_REP_EMPTY;
    void* measure = NULL;
    ft_status status = ft_measure_new_identity(&policy->measure, &measure);
    if (status != FT_STATUS_OK) {
        free(rep);
        return status;
    }

    ft_atomic_ptr_init(&rep->measure, measure);
    *result = rep;
    return FT_STATUS_OK;
}

static ft_status ft_rep_create_single(const ft_tree_policy* policy, const ft_element* element, ft_tree_rep** result)
{
    ft_tree_rep* rep = (ft_tree_rep*)calloc(1, sizeof(*rep));
    if (rep == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    ft_ref_init(&rep->ref_count);
    rep->kind = FT_REP_SINGLE;
    rep->leaf_count = element->leaf_count;

    ft_status status = ft_element_clone(policy, element, &rep->as.single);
    if (status != FT_STATUS_OK) {
        free(rep);
        return status;
    }

    rep->last_leaf = rep->as.single.last_leaf;

    void* measure = NULL;
    status = ft_measure_new_copy(&policy->measure, rep->as.single.measure, &measure);
    if (status != FT_STATUS_OK) {
        ft_element_dispose(policy, &rep->as.single);
        free(rep);
        return status;
    }

    ft_atomic_ptr_init(&rep->measure, measure);
    *result = rep;
    return FT_STATUS_OK;
}

static ft_status ft_combine_element_array(
    const ft_tree_policy* policy,
    const ft_element* elements,
    size_t count,
    void** measure,
    size_t* leaf_count)
{
    if (count == 0) {
        *leaf_count = 0;
        return ft_measure_new_identity(&policy->measure, measure);
    }

    ft_status status = ft_measure_new_copy(&policy->measure, elements[0].measure, measure);
    if (status != FT_STATUS_OK) {
        return status;
    }

    *leaf_count = elements[0].leaf_count;
    for (size_t index = 1; index != count; ++index) {
        void* combined = NULL;
        status = ft_measure_new_combine(&policy->measure, *measure, elements[index].measure, &combined);
        if (status != FT_STATUS_OK) {
            free(*measure);
            *measure = NULL;
            return status;
        }

        free(*measure);
        *measure = combined;

        if (ft_add_overflows(*leaf_count, elements[index].leaf_count, leaf_count)) {
            free(*measure);
            *measure = NULL;
            return FT_STATUS_OVERFLOW;
        }
    }

    return FT_STATUS_OK;
}

static ft_status ft_count_element_array(const ft_element* elements, size_t count, size_t* leaf_count)
{
    *leaf_count = 0;
    for (size_t index = 0; index != count; ++index) {
        if (ft_add_overflows(*leaf_count, elements[index].leaf_count, leaf_count)) {
            return FT_STATUS_OVERFLOW;
        }
    }

    return FT_STATUS_OK;
}

static void ft_middle_retain(ft_middle* middle)
{
    if (middle != NULL) {
        ft_ref_retain(&middle->ref_count);
    }
}

static ft_status ft_middle_create_computed(const ft_tree_policy* policy, ft_tree_rep* rep, ft_middle** result)
{
    (void)policy;
    ft_middle* middle = (ft_middle*)calloc(1, sizeof(*middle));
    if (middle == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    ft_ref_init(&middle->ref_count);
    ft_atomic_ptr_init(&middle->computed, rep);
    middle->kind = FT_MIDDLE_COMPUTED;
    middle->leaf_count = rep->leaf_count;
    ft_rep_retain(rep);
    *result = middle;
    return FT_STATUS_OK;
}

static ft_status ft_middle_create_push(
    const ft_tree_policy* policy,
    ft_middle_kind kind,
    ft_tree_rep* source,
    const ft_element* element,
    ft_middle** result)
{
    size_t leaf_count = 0;
    if (ft_add_overflows(source->leaf_count, element->leaf_count, &leaf_count)) {
        return FT_STATUS_OVERFLOW;
    }

    ft_middle* middle = (ft_middle*)calloc(1, sizeof(*middle));
    if (middle == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    ft_ref_init(&middle->ref_count);
    ft_atomic_ptr_init(&middle->computed, NULL);
    middle->kind = kind;
    middle->leaf_count = leaf_count;
    middle->as.push.source = source;
    ft_rep_retain(source);

    ft_status status = ft_element_clone(policy, element, &middle->as.push.element);
    if (status != FT_STATUS_OK) {
        ft_middle_release(policy, middle);
        return status;
    }

    const void* source_measure = NULL;
    status = ft_rep_get_measure(policy, source, &source_measure);
    if (status != FT_STATUS_OK) {
        ft_middle_release(policy, middle);
        return status;
    }

    if (kind == FT_MIDDLE_PUSH_FRONT) {
        status = ft_measure_new_combine(&policy->measure, element->measure, source_measure, &middle->measure);
    } else {
        status = ft_measure_new_combine(&policy->measure, source_measure, element->measure, &middle->measure);
    }

    if (status != FT_STATUS_OK) {
        ft_middle_release(policy, middle);
        return status;
    }

    *result = middle;
    return FT_STATUS_OK;
}

static ft_status ft_middle_create_pop(
    const ft_tree_policy* policy,
    ft_middle_kind kind,
    ft_tree_rep* source,
    size_t removed_leaf_count,
    ft_middle** result)
{
    (void)policy;
    if (removed_leaf_count > source->leaf_count) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_middle* middle = (ft_middle*)calloc(1, sizeof(*middle));
    if (middle == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    ft_ref_init(&middle->ref_count);
    ft_atomic_ptr_init(&middle->computed, NULL);
    middle->kind = kind;
    middle->leaf_count = source->leaf_count - removed_leaf_count;
    middle->as.pop.source = source;
    ft_rep_retain(source);
    *result = middle;
    return FT_STATUS_OK;
}

static ft_status ft_middle_force(const ft_tree_policy* policy, const ft_middle* middle, ft_tree_rep** result)
{
    for (;;) {
        ft_tree_rep* existing = (ft_tree_rep*)ft_atomic_ptr_load((ft_atomic_ptr*)&middle->computed);
        if (existing != NULL) {
            ft_rep_retain(existing);
            *result = existing;
            return FT_STATUS_OK;
        }

        ft_tree_rep* computed = NULL;
        ft_status status = FT_STATUS_OK;
        if (middle->kind == FT_MIDDLE_PUSH_FRONT) {
            status = ft_rep_cons(policy, middle->as.push.source, &middle->as.push.element, &computed);
        } else if (middle->kind == FT_MIDDLE_PUSH_BACK) {
            status = ft_rep_snoc(policy, middle->as.push.source, &middle->as.push.element, &computed);
        } else if (middle->kind == FT_MIDDLE_POP_FRONT) {
            ft_element ignored;
            status = ft_rep_view_left(policy, middle->as.pop.source, &ignored, &computed);
            if (status == FT_STATUS_OK) {
                ft_element_dispose(policy, &ignored);
            }
        } else if (middle->kind == FT_MIDDLE_POP_BACK) {
            ft_element ignored;
            status = ft_rep_view_right(policy, middle->as.pop.source, &ignored, &computed);
            if (status == FT_STATUS_OK) {
                ft_element_dispose(policy, &ignored);
            }
        } else {
            return FT_STATUS_INVALID_ARGUMENT;
        }

        if (status != FT_STATUS_OK) {
            existing = (ft_tree_rep*)ft_atomic_ptr_load((ft_atomic_ptr*)&middle->computed);
            if (existing != NULL) {
                ft_rep_retain(existing);
                *result = existing;
                return FT_STATUS_OK;
            }

            return status;
        }

        void* expected = NULL;
        if (ft_atomic_ptr_compare_exchange((ft_atomic_ptr*)&middle->computed, &expected, computed)) {
            ft_rep_retain(computed);
            *result = computed;
            return FT_STATUS_OK;
        }

        ft_rep_release(policy, computed);
        if (expected != NULL) {
            ft_rep_retain((ft_tree_rep*)expected);
            *result = (ft_tree_rep*)expected;
            return FT_STATUS_OK;
        }
    }
}

static ft_status ft_middle_get_measure(const ft_tree_policy* policy, const ft_middle* middle, const void** measure)
{
    if (middle->measure != NULL) {
        *measure = middle->measure;
        return FT_STATUS_OK;
    }

    ft_tree_rep* computed = NULL;
    ft_status status = ft_middle_force(policy, middle, &computed);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_rep_get_measure(policy, computed, measure);
    ft_rep_release(policy, computed);
    return status;
}

static void ft_middle_release(const ft_tree_policy* policy, ft_middle* middle)
{
    if (middle == NULL) {
        return;
    }

    if (!ft_ref_release(&middle->ref_count)) {
        return;
    }

    ft_tree_rep* computed = (ft_tree_rep*)ft_atomic_ptr_load(&middle->computed);
    ft_rep_release(policy, computed);

    if (middle->kind == FT_MIDDLE_PUSH_FRONT || middle->kind == FT_MIDDLE_PUSH_BACK) {
        ft_rep_release(policy, middle->as.push.source);
        ft_element_dispose(policy, &middle->as.push.element);
    } else if (middle->kind == FT_MIDDLE_POP_FRONT || middle->kind == FT_MIDDLE_POP_BACK) {
        ft_rep_release(policy, middle->as.pop.source);
    }

    free(middle->measure);
    free(middle);
}

static ft_status ft_rep_get_measure(const ft_tree_policy* policy, const ft_tree_rep* rep, const void** measure)
{
    ft_atomic_ptr* measure_slot = &((ft_tree_rep*)rep)->measure;
    void* existing = ft_atomic_ptr_load(measure_slot);
    if (existing != NULL) {
        *measure = existing;
        return FT_STATUS_OK;
    }

    void* computed = NULL;
    ft_status status = FT_STATUS_OK;
    if (rep->kind == FT_REP_EMPTY) {
        status = ft_measure_new_identity(&policy->measure, &computed);
    } else if (rep->kind == FT_REP_SINGLE) {
        status = ft_measure_new_copy(&policy->measure, rep->as.single.measure, &computed);
    } else {
        void* prefix_measure = NULL;
        size_t prefix_leaves = 0;
        status = ft_combine_element_array(
            policy,
            rep->as.deep.prefix,
            rep->as.deep.prefix_count,
            &prefix_measure,
            &prefix_leaves);
        if (status != FT_STATUS_OK) {
            return status;
        }

        const void* middle_measure = NULL;
        status = ft_middle_get_measure(policy, rep->as.deep.middle, &middle_measure);
        if (status != FT_STATUS_OK) {
            free(prefix_measure);
            return status;
        }

        void* prefix_middle = NULL;
        status = ft_measure_new_combine(&policy->measure, prefix_measure, middle_measure, &prefix_middle);
        free(prefix_measure);
        if (status != FT_STATUS_OK) {
            return status;
        }

        void* suffix_measure = NULL;
        size_t suffix_leaves = 0;
        status = ft_combine_element_array(
            policy,
            rep->as.deep.suffix,
            rep->as.deep.suffix_count,
            &suffix_measure,
            &suffix_leaves);
        if (status != FT_STATUS_OK) {
            free(prefix_middle);
            return status;
        }

        status = ft_measure_new_combine(&policy->measure, prefix_middle, suffix_measure, &computed);
        free(prefix_middle);
        free(suffix_measure);
    }

    if (status != FT_STATUS_OK) {
        return status;
    }

    void* expected = NULL;
    if (ft_atomic_ptr_compare_exchange(measure_slot, &expected, computed)) {
        *measure = computed;
        return FT_STATUS_OK;
    }

    free(computed);
    *measure = expected;
    return FT_STATUS_OK;
}

static ft_status ft_rep_create_deep_with_middle(
    const ft_tree_policy* policy,
    const ft_element* prefix,
    size_t prefix_count,
    ft_middle* middle,
    const ft_element* suffix,
    size_t suffix_count,
    ft_tree_rep** result)
{
    if (prefix_count == 0 || prefix_count > 4 || suffix_count == 0 || suffix_count > 4 || middle == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_tree_rep* rep = (ft_tree_rep*)calloc(1, sizeof(*rep));
    if (rep == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    ft_ref_init(&rep->ref_count);
    ft_atomic_ptr_init(&rep->measure, NULL);
    rep->kind = FT_REP_DEEP;
    rep->as.deep.prefix_count = prefix_count;
    rep->as.deep.suffix_count = suffix_count;

    ft_status status = FT_STATUS_OK;
    for (size_t index = 0; index != prefix_count; ++index) {
        status = ft_element_clone(policy, &prefix[index], &rep->as.deep.prefix[index]);
        if (status != FT_STATUS_OK) {
            rep->as.deep.prefix_count = index;
            ft_rep_release(policy, rep);
            return status;
        }
    }

    rep->as.deep.middle = middle;
    ft_middle_retain(middle);

    for (size_t index = 0; index != suffix_count; ++index) {
        status = ft_element_clone(policy, &suffix[index], &rep->as.deep.suffix[index]);
        if (status != FT_STATUS_OK) {
            rep->as.deep.suffix_count = index;
            ft_rep_release(policy, rep);
            return status;
        }
    }

    size_t prefix_leaves = 0;
    status = ft_count_element_array(prefix, prefix_count, &prefix_leaves);
    if (status != FT_STATUS_OK) {
        ft_rep_release(policy, rep);
        return status;
    }

    size_t suffix_leaves = 0;
    status = ft_count_element_array(suffix, suffix_count, &suffix_leaves);
    if (status != FT_STATUS_OK) {
        ft_rep_release(policy, rep);
        return status;
    }

    size_t prefix_and_middle = 0;
    if (ft_add_overflows(prefix_leaves, middle->leaf_count, &prefix_and_middle) ||
        ft_add_overflows(prefix_and_middle, suffix_leaves, &rep->leaf_count)) {
        ft_rep_release(policy, rep);
        return FT_STATUS_OVERFLOW;
    }

    rep->last_leaf = rep->as.deep.suffix[suffix_count - 1].last_leaf;

    *result = rep;
    return FT_STATUS_OK;
}

static ft_status ft_rep_create_deep(
    const ft_tree_policy* policy,
    const ft_element* prefix,
    size_t prefix_count,
    ft_tree_rep* middle,
    const ft_element* suffix,
    size_t suffix_count,
    ft_tree_rep** result)
{
    ft_middle* computed_middle = NULL;
    ft_status status = ft_middle_create_computed(policy, middle, &computed_middle);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_rep_create_deep_with_middle(
        policy,
        prefix,
        prefix_count,
        computed_middle,
        suffix,
        suffix_count,
        result);
    ft_middle_release(policy, computed_middle);
    return status;
}

static ft_status ft_rep_from_buffer(const ft_tree_policy* policy, const ft_element* values, size_t count, ft_tree_rep** result);

static ft_status ft_rep_cons(const ft_tree_policy* policy, const ft_tree_rep* rep, const ft_element* value, ft_tree_rep** result)
{
    if (rep->kind == FT_REP_EMPTY) {
        return ft_rep_create_single(policy, value, result);
    }

    if (rep->kind == FT_REP_SINGLE) {
        ft_tree_rep* empty = NULL;
        ft_status status = ft_rep_create_empty(policy, &empty);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_rep_create_deep(policy, value, 1, empty, &rep->as.single, 1, result);
        ft_rep_release(policy, empty);
        return status;
    }

    if (rep->as.deep.prefix_count < 4) {
        ft_element prefix[4];
        prefix[0] = *value;
        for (size_t index = 0; index != rep->as.deep.prefix_count; ++index) {
            prefix[index + 1] = rep->as.deep.prefix[index];
        }

        return ft_rep_create_deep_with_middle(
            policy,
            prefix,
            rep->as.deep.prefix_count + 1,
            rep->as.deep.middle,
            rep->as.deep.suffix,
            rep->as.deep.suffix_count,
            result);
    }

    ft_element pushed;
    ft_status status = ft_element_init_node(policy, &rep->as.deep.prefix[1], 3, &pushed);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_tree_rep* forced_middle = NULL;
    status = ft_middle_force(policy, rep->as.deep.middle, &forced_middle);
    if (status != FT_STATUS_OK) {
        ft_element_dispose(policy, &pushed);
        return status;
    }

    ft_middle* next_middle = NULL;
    status = ft_middle_create_push(policy, FT_MIDDLE_PUSH_FRONT, forced_middle, &pushed, &next_middle);
    ft_rep_release(policy, forced_middle);
    ft_element_dispose(policy, &pushed);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_element prefix[2];
    prefix[0] = *value;
    prefix[1] = rep->as.deep.prefix[0];
    status = ft_rep_create_deep_with_middle(
        policy,
        prefix,
        2,
        next_middle,
        rep->as.deep.suffix,
        rep->as.deep.suffix_count,
        result);
    ft_middle_release(policy, next_middle);
    return status;
}

static ft_status ft_rep_snoc(const ft_tree_policy* policy, const ft_tree_rep* rep, const ft_element* value, ft_tree_rep** result)
{
    if (rep->kind == FT_REP_EMPTY) {
        return ft_rep_create_single(policy, value, result);
    }

    if (rep->kind == FT_REP_SINGLE) {
        ft_tree_rep* empty = NULL;
        ft_status status = ft_rep_create_empty(policy, &empty);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_rep_create_deep(policy, &rep->as.single, 1, empty, value, 1, result);
        ft_rep_release(policy, empty);
        return status;
    }

    if (rep->as.deep.suffix_count < 4) {
        ft_element suffix[4];
        for (size_t index = 0; index != rep->as.deep.suffix_count; ++index) {
            suffix[index] = rep->as.deep.suffix[index];
        }

        suffix[rep->as.deep.suffix_count] = *value;
        return ft_rep_create_deep_with_middle(
            policy,
            rep->as.deep.prefix,
            rep->as.deep.prefix_count,
            rep->as.deep.middle,
            suffix,
            rep->as.deep.suffix_count + 1,
            result);
    }

    ft_element pushed;
    ft_status status = ft_element_init_node(policy, rep->as.deep.suffix, 3, &pushed);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_tree_rep* forced_middle = NULL;
    status = ft_middle_force(policy, rep->as.deep.middle, &forced_middle);
    if (status != FT_STATUS_OK) {
        ft_element_dispose(policy, &pushed);
        return status;
    }

    ft_middle* next_middle = NULL;
    status = ft_middle_create_push(policy, FT_MIDDLE_PUSH_BACK, forced_middle, &pushed, &next_middle);
    ft_rep_release(policy, forced_middle);
    ft_element_dispose(policy, &pushed);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_element suffix[2];
    suffix[0] = rep->as.deep.suffix[3];
    suffix[1] = *value;
    status = ft_rep_create_deep_with_middle(
        policy,
        rep->as.deep.prefix,
        rep->as.deep.prefix_count,
        next_middle,
        suffix,
        2,
        result);
    ft_middle_release(policy, next_middle);
    return status;
}

static ft_status ft_rep_from_buffer(const ft_tree_policy* policy, const ft_element* values, size_t count, ft_tree_rep** result)
{
    ft_tree_rep* current = NULL;
    ft_status status = ft_rep_create_empty(policy, &current);
    if (status != FT_STATUS_OK) {
        return status;
    }

    for (size_t index = 0; index != count; ++index) {
        ft_tree_rep* next = NULL;
        status = ft_rep_snoc(policy, current, &values[index], &next);
        ft_rep_release(policy, current);
        if (status != FT_STATUS_OK) {
            return status;
        }

        current = next;
    }

    *result = current;
    return FT_STATUS_OK;
}

static ft_status ft_rep_view_left(
    const ft_tree_policy* policy,
    const ft_tree_rep* rep,
    ft_element* value,
    ft_tree_rep** rest)
{
    if (rep->kind == FT_REP_EMPTY) {
        return FT_STATUS_EMPTY;
    }

    if (rep->kind == FT_REP_SINGLE) {
        ft_status status = ft_element_clone(policy, &rep->as.single, value);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_rep_create_empty(policy, rest);
        if (status != FT_STATUS_OK) {
            ft_element_dispose(policy, value);
        }

        return status;
    }

    ft_status status = ft_element_clone(policy, &rep->as.deep.prefix[0], value);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (rep->as.deep.prefix_count > 1) {
        status = ft_rep_create_deep_with_middle(
            policy,
            &rep->as.deep.prefix[1],
            rep->as.deep.prefix_count - 1,
            rep->as.deep.middle,
            rep->as.deep.suffix,
            rep->as.deep.suffix_count,
            rest);
        if (status != FT_STATUS_OK) {
            ft_element_dispose(policy, value);
        }

        return status;
    }

    if (rep->as.deep.middle->leaf_count == 0) {
        status = ft_rep_from_buffer(policy, rep->as.deep.suffix, rep->as.deep.suffix_count, rest);
        if (status != FT_STATUS_OK) {
            ft_element_dispose(policy, value);
        }

        return status;
    }

    ft_tree_rep* forced_middle = NULL;
    status = ft_middle_force(policy, rep->as.deep.middle, &forced_middle);
    if (status != FT_STATUS_OK) {
        ft_element_dispose(policy, value);
        return status;
    }

    const ft_element* pulled = NULL;
    if (forced_middle->kind == FT_REP_SINGLE) {
        pulled = &forced_middle->as.single;
    } else if (forced_middle->kind == FT_REP_DEEP) {
        pulled = &forced_middle->as.deep.prefix[0];
    }

    if (pulled == NULL || pulled->kind != FT_ELEMENT_NODE) {
        ft_rep_release(policy, forced_middle);
        ft_element_dispose(policy, value);
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_middle* middle_rest = NULL;
    status = ft_middle_create_pop(policy, FT_MIDDLE_POP_FRONT, forced_middle, pulled->leaf_count, &middle_rest);
    if (status != FT_STATUS_OK) {
        ft_rep_release(policy, forced_middle);
        ft_element_dispose(policy, value);
        return status;
    }

    status = ft_rep_create_deep_with_middle(
        policy,
        pulled->as.node->children,
        pulled->as.node->child_count,
        middle_rest,
        rep->as.deep.suffix,
        rep->as.deep.suffix_count,
        rest);

    ft_middle_release(policy, middle_rest);
    ft_rep_release(policy, forced_middle);
    if (status != FT_STATUS_OK) {
        ft_element_dispose(policy, value);
    }

    return status;
}

static ft_status ft_rep_view_right(
    const ft_tree_policy* policy,
    const ft_tree_rep* rep,
    ft_element* value,
    ft_tree_rep** rest)
{
    if (rep->kind == FT_REP_EMPTY) {
        return FT_STATUS_EMPTY;
    }

    if (rep->kind == FT_REP_SINGLE) {
        ft_status status = ft_element_clone(policy, &rep->as.single, value);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_rep_create_empty(policy, rest);
        if (status != FT_STATUS_OK) {
            ft_element_dispose(policy, value);
        }

        return status;
    }

    ft_status status = ft_element_clone(
        policy,
        &rep->as.deep.suffix[rep->as.deep.suffix_count - 1],
        value);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (rep->as.deep.suffix_count > 1) {
        status = ft_rep_create_deep_with_middle(
            policy,
            rep->as.deep.prefix,
            rep->as.deep.prefix_count,
            rep->as.deep.middle,
            rep->as.deep.suffix,
            rep->as.deep.suffix_count - 1,
            rest);
        if (status != FT_STATUS_OK) {
            ft_element_dispose(policy, value);
        }

        return status;
    }

    if (rep->as.deep.middle->leaf_count == 0) {
        status = ft_rep_from_buffer(policy, rep->as.deep.prefix, rep->as.deep.prefix_count, rest);
        if (status != FT_STATUS_OK) {
            ft_element_dispose(policy, value);
        }

        return status;
    }

    ft_tree_rep* forced_middle = NULL;
    status = ft_middle_force(policy, rep->as.deep.middle, &forced_middle);
    if (status != FT_STATUS_OK) {
        ft_element_dispose(policy, value);
        return status;
    }

    const ft_element* pulled = NULL;
    if (forced_middle->kind == FT_REP_SINGLE) {
        pulled = &forced_middle->as.single;
    } else if (forced_middle->kind == FT_REP_DEEP) {
        pulled = &forced_middle->as.deep.suffix[forced_middle->as.deep.suffix_count - 1];
    }

    if (pulled == NULL || pulled->kind != FT_ELEMENT_NODE) {
        ft_rep_release(policy, forced_middle);
        ft_element_dispose(policy, value);
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_middle* middle_rest = NULL;
    status = ft_middle_create_pop(policy, FT_MIDDLE_POP_BACK, forced_middle, pulled->leaf_count, &middle_rest);
    if (status != FT_STATUS_OK) {
        ft_rep_release(policy, forced_middle);
        ft_element_dispose(policy, value);
        return status;
    }

    status = ft_rep_create_deep_with_middle(
        policy,
        rep->as.deep.prefix,
        rep->as.deep.prefix_count,
        middle_rest,
        pulled->as.node->children,
        pulled->as.node->child_count,
        rest);

    ft_middle_release(policy, middle_rest);
    ft_rep_release(policy, forced_middle);
    if (status != FT_STATUS_OK) {
        ft_element_dispose(policy, value);
    }

    return status;
}

static ft_status ft_nodes_from_elements(
    const ft_tree_policy* policy,
    const ft_element* values,
    size_t count,
    ft_element* nodes,
    size_t* node_count)
{
    if (count < 2) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    *node_count = 0;
    size_t index = 0;
    ft_status status = FT_STATUS_OK;
    while (count - index > 4) {
        status = ft_element_init_node(policy, &values[index], 3, &nodes[*node_count]);
        if (status != FT_STATUS_OK) {
            goto fail;
        }

        ++*node_count;
        index += 3;
    }

    switch (count - index) {
    case 2:
        status = ft_element_init_node(policy, &values[index], 2, &nodes[*node_count]);
        if (status != FT_STATUS_OK) {
            goto fail;
        }

        ++*node_count;
        return FT_STATUS_OK;
    case 3:
        status = ft_element_init_node(policy, &values[index], 3, &nodes[*node_count]);
        if (status != FT_STATUS_OK) {
            goto fail;
        }

        ++*node_count;
        return FT_STATUS_OK;
    case 4:
        status = ft_element_init_node(policy, &values[index], 2, &nodes[*node_count]);
        if (status != FT_STATUS_OK) {
            goto fail;
        }

        ++*node_count;
        status = ft_element_init_node(policy, &values[index + 2], 2, &nodes[*node_count]);
        if (status != FT_STATUS_OK) {
            goto fail;
        }

        ++*node_count;
        return FT_STATUS_OK;
    default:
        status = FT_STATUS_INVALID_ARGUMENT;
        goto fail;
    }

fail:
    for (size_t cleanup = 0; cleanup != *node_count; ++cleanup) {
        ft_element_dispose(policy, &nodes[cleanup]);
    }

    *node_count = 0;
    return status;
}

static ft_status ft_rep_append_all(
    const ft_tree_policy* policy,
    ft_tree_rep* left,
    const ft_element* values,
    size_t count,
    ft_tree_rep** result)
{
    ft_rep_retain(left);
    ft_tree_rep* current = left;
    ft_status status = FT_STATUS_OK;
    for (size_t index = 0; index != count; ++index) {
        ft_tree_rep* next = NULL;
        status = ft_rep_snoc(policy, current, &values[index], &next);
        ft_rep_release(policy, current);
        if (status != FT_STATUS_OK) {
            return status;
        }

        current = next;
    }

    *result = current;
    return FT_STATUS_OK;
}

static ft_status ft_rep_prepend_all(
    const ft_tree_policy* policy,
    const ft_element* values,
    size_t count,
    ft_tree_rep* right,
    ft_tree_rep** result)
{
    ft_rep_retain(right);
    ft_tree_rep* current = right;
    ft_status status = FT_STATUS_OK;
    for (size_t offset = count; offset != 0; --offset) {
        ft_tree_rep* next = NULL;
        status = ft_rep_cons(policy, current, &values[offset - 1], &next);
        ft_rep_release(policy, current);
        if (status != FT_STATUS_OK) {
            return status;
        }

        current = next;
    }

    *result = current;
    return FT_STATUS_OK;
}

static ft_status ft_rep_concat_with_middle(
    const ft_tree_policy* policy,
    ft_tree_rep* left,
    const ft_element* middle,
    size_t middle_count,
    ft_tree_rep* right,
    ft_tree_rep** result)
{
    if (left->kind == FT_REP_EMPTY) {
        return ft_rep_prepend_all(policy, middle, middle_count, right, result);
    }

    if (right->kind == FT_REP_EMPTY) {
        return ft_rep_append_all(policy, left, middle, middle_count, result);
    }

    if (left->kind == FT_REP_SINGLE) {
        ft_tree_rep* prefixed = NULL;
        ft_status status = ft_rep_prepend_all(policy, middle, middle_count, right, &prefixed);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_rep_cons(policy, prefixed, &left->as.single, result);
        ft_rep_release(policy, prefixed);
        return status;
    }

    if (right->kind == FT_REP_SINGLE) {
        ft_tree_rep* appended = NULL;
        ft_status status = ft_rep_append_all(policy, left, middle, middle_count, &appended);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_rep_snoc(policy, appended, &right->as.single, result);
        ft_rep_release(policy, appended);
        return status;
    }

    ft_element combined[16];
    size_t combined_count = 0;
    for (size_t index = 0; index != left->as.deep.suffix_count; ++index) {
        combined[combined_count++] = left->as.deep.suffix[index];
    }

    for (size_t index = 0; index != middle_count; ++index) {
        combined[combined_count++] = middle[index];
    }

    for (size_t index = 0; index != right->as.deep.prefix_count; ++index) {
        combined[combined_count++] = right->as.deep.prefix[index];
    }

    ft_element nodes[8];
    size_t node_count = 0;
    ft_status status = ft_nodes_from_elements(policy, combined, combined_count, nodes, &node_count);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_tree_rep* left_middle = NULL;
    status = ft_middle_force(policy, left->as.deep.middle, &left_middle);
    if (status != FT_STATUS_OK) {
        for (size_t index = 0; index != node_count; ++index) {
            ft_element_dispose(policy, &nodes[index]);
        }

        return status;
    }

    ft_tree_rep* right_middle = NULL;
    status = ft_middle_force(policy, right->as.deep.middle, &right_middle);
    if (status != FT_STATUS_OK) {
        ft_rep_release(policy, left_middle);
        for (size_t index = 0; index != node_count; ++index) {
            ft_element_dispose(policy, &nodes[index]);
        }

        return status;
    }

    ft_tree_rep* middle_result = NULL;
    status = ft_rep_concat_with_middle(policy, left_middle, nodes, node_count, right_middle, &middle_result);
    ft_rep_release(policy, right_middle);
    ft_rep_release(policy, left_middle);

    for (size_t index = 0; index != node_count; ++index) {
        ft_element_dispose(policy, &nodes[index]);
    }

    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_rep_create_deep(
        policy,
        left->as.deep.prefix,
        left->as.deep.prefix_count,
        middle_result,
        right->as.deep.suffix,
        right->as.deep.suffix_count,
        result);
    ft_rep_release(policy, middle_result);
    return status;
}

static ft_status ft_element_copy_leaf_at(
    const ft_tree_policy* policy,
    const ft_element* element,
    size_t index,
    void* destination)
{
    if (index >= element->leaf_count) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    if (element->kind == FT_ELEMENT_LEAF) {
        ft_value_copy(&policy->value, destination, element->as.value);
        return FT_STATUS_OK;
    }

    size_t offset = index;
    for (size_t child = 0; child != element->as.node->child_count; ++child) {
        const ft_element* current = &element->as.node->children[child];
        if (offset < current->leaf_count) {
            return ft_element_copy_leaf_at(policy, current, offset, destination);
        }

        offset -= current->leaf_count;
    }

    return FT_STATUS_OUT_OF_RANGE;
}

static void ft_elements_dispose(const ft_tree_policy* policy, ft_element* elements, size_t count)
{
    for (size_t index = 0; index != count; ++index) {
        ft_element_dispose(policy, &elements[index]);
    }
}

static ft_status ft_element_set_leaf(
    const ft_tree_policy* policy,
    const ft_element* element,
    size_t index,
    const void* value,
    ft_element* result)
{
    if (index >= element->leaf_count) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    if (element->kind == FT_ELEMENT_LEAF) {
        return ft_element_init_leaf(policy, value, result);
    }

    ft_element children[3];
    size_t child_count = 0;
    for (size_t child = 0; child != element->as.node->child_count; ++child) {
        ft_status status = ft_element_clone(policy, &element->as.node->children[child], &children[child_count]);
        if (status != FT_STATUS_OK) {
            ft_elements_dispose(policy, children, child_count);
            return status;
        }

        ++child_count;
    }

    for (size_t child = 0; child != child_count; ++child) {
        if (index < children[child].leaf_count) {
            ft_element replacement;
            ft_status status = ft_element_set_leaf(policy, &children[child], index, value, &replacement);
            if (status == FT_STATUS_OK) {
                ft_element_dispose(policy, &children[child]);
                children[child] = replacement;
                status = ft_element_init_node(policy, children, child_count, result);
            }

            ft_elements_dispose(policy, children, child_count);
            return status;
        }

        index -= children[child].leaf_count;
    }

    ft_elements_dispose(policy, children, child_count);
    return FT_STATUS_OUT_OF_RANGE;
}

static ft_status ft_clone_elements(
    const ft_tree_policy* policy,
    const ft_element* source,
    size_t count,
    ft_element* destination)
{
    for (size_t index = 0; index != count; ++index) {
        ft_status status = ft_element_clone(policy, &source[index], &destination[index]);
        if (status != FT_STATUS_OK) {
            ft_elements_dispose(policy, destination, index);
            return status;
        }
    }

    return FT_STATUS_OK;
}

static ft_status ft_replace_leaf_in_elements(
    const ft_tree_policy* policy,
    ft_element* elements,
    size_t count,
    size_t index,
    const void* value)
{
    for (size_t element_index = 0; element_index != count; ++element_index) {
        if (index < elements[element_index].leaf_count) {
            ft_element replacement;
            ft_status status = ft_element_set_leaf(policy, &elements[element_index], index, value, &replacement);
            if (status != FT_STATUS_OK) {
                return status;
            }

            ft_element_dispose(policy, &elements[element_index]);
            elements[element_index] = replacement;
            return FT_STATUS_OK;
        }

        index -= elements[element_index].leaf_count;
    }

    return FT_STATUS_OUT_OF_RANGE;
}

static ft_status ft_rep_set_leaf(
    const ft_tree_policy* policy,
    const ft_tree_rep* rep,
    size_t index,
    const void* value,
    ft_tree_rep** result)
{
    if (index >= rep->leaf_count) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    if (rep->kind == FT_REP_SINGLE) {
        ft_element replacement;
        ft_status status = ft_element_set_leaf(policy, &rep->as.single, index, value, &replacement);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_rep_create_single(policy, &replacement, result);
        ft_element_dispose(policy, &replacement);
        return status;
    }

    if (rep->kind != FT_REP_DEEP) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    size_t offset = index;
    size_t prefix_leaf_count = 0;
    ft_status status = ft_count_element_array(rep->as.deep.prefix, rep->as.deep.prefix_count, &prefix_leaf_count);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (offset < prefix_leaf_count) {
        ft_element prefix[4];
        status = ft_clone_elements(policy, rep->as.deep.prefix, rep->as.deep.prefix_count, prefix);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_replace_leaf_in_elements(policy, prefix, rep->as.deep.prefix_count, offset, value);
        if (status == FT_STATUS_OK) {
            status = ft_rep_create_deep_with_middle(
                policy,
                prefix,
                rep->as.deep.prefix_count,
                rep->as.deep.middle,
                rep->as.deep.suffix,
                rep->as.deep.suffix_count,
                result);
        }

        ft_elements_dispose(policy, prefix, rep->as.deep.prefix_count);
        return status;
    }

    offset -= prefix_leaf_count;
    const size_t middle_leaf_count = rep->as.deep.middle->leaf_count;
    if (offset < middle_leaf_count) {
        ft_tree_rep* middle = NULL;
        status = ft_middle_force(policy, rep->as.deep.middle, &middle);
        if (status != FT_STATUS_OK) {
            return status;
        }

        ft_tree_rep* next_middle = NULL;
        status = ft_rep_set_leaf(policy, middle, offset, value, &next_middle);
        if (status == FT_STATUS_OK) {
            status = ft_rep_create_deep(
                policy,
                rep->as.deep.prefix,
                rep->as.deep.prefix_count,
                next_middle,
                rep->as.deep.suffix,
                rep->as.deep.suffix_count,
                result);
            ft_rep_release(policy, next_middle);
        }

        ft_rep_release(policy, middle);
        return status;
    }

    offset -= middle_leaf_count;
    size_t suffix_leaf_count = 0;
    status = ft_count_element_array(rep->as.deep.suffix, rep->as.deep.suffix_count, &suffix_leaf_count);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (offset < suffix_leaf_count) {
        ft_element suffix[4];
        status = ft_clone_elements(policy, rep->as.deep.suffix, rep->as.deep.suffix_count, suffix);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_replace_leaf_in_elements(policy, suffix, rep->as.deep.suffix_count, offset, value);
        if (status == FT_STATUS_OK) {
            status = ft_rep_create_deep_with_middle(
                policy,
                rep->as.deep.prefix,
                rep->as.deep.prefix_count,
                rep->as.deep.middle,
                suffix,
                rep->as.deep.suffix_count,
                result);
        }

        ft_elements_dispose(policy, suffix, rep->as.deep.suffix_count);
        return status;
    }

    return FT_STATUS_OUT_OF_RANGE;
}

static ft_status ft_rep_copy_leaf_at(
    const ft_tree_policy* policy,
    const ft_tree_rep* rep,
    size_t index,
    void* destination)
{
    if (index >= rep->leaf_count) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    if (rep->kind == FT_REP_SINGLE) {
        return ft_element_copy_leaf_at(policy, &rep->as.single, index, destination);
    }

    if (rep->kind != FT_REP_DEEP) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    size_t offset = index;
    for (size_t child = 0; child != rep->as.deep.prefix_count; ++child) {
        const ft_element* current = &rep->as.deep.prefix[child];
        if (offset < current->leaf_count) {
            return ft_element_copy_leaf_at(policy, current, offset, destination);
        }

        offset -= current->leaf_count;
    }

    const size_t middle_leaf_count = rep->as.deep.middle->leaf_count;
    if (offset < middle_leaf_count) {
        ft_tree_rep* middle = NULL;
        ft_status status = ft_middle_force(policy, rep->as.deep.middle, &middle);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_rep_copy_leaf_at(policy, middle, offset, destination);
        ft_rep_release(policy, middle);
        return status;
    }

    offset -= middle_leaf_count;
    for (size_t child = 0; child != rep->as.deep.suffix_count; ++child) {
        const ft_element* current = &rep->as.deep.suffix[child];
        if (offset < current->leaf_count) {
            return ft_element_copy_leaf_at(policy, current, offset, destination);
        }

        offset -= current->leaf_count;
    }

    return FT_STATUS_OUT_OF_RANGE;
}

static ft_status ft_visit_element(const ft_tree_policy* policy, const ft_element* element, ft_visit_fn visitor, void* context)
{
    if (element->kind == FT_ELEMENT_LEAF) {
        visitor(element->as.value, context);
        return FT_STATUS_OK;
    }

    for (size_t index = 0; index != element->as.node->child_count; ++index) {
        ft_status status = ft_visit_element(policy, &element->as.node->children[index], visitor, context);
        if (status != FT_STATUS_OK) {
            return status;
        }
    }

    return FT_STATUS_OK;
}

static ft_status ft_visit_rep(const ft_tree_policy* policy, const ft_tree_rep* rep, ft_visit_fn visitor, void* context)
{
    if (rep->kind == FT_REP_EMPTY) {
        return FT_STATUS_OK;
    }

    if (rep->kind == FT_REP_SINGLE) {
        return ft_visit_element(policy, &rep->as.single, visitor, context);
    }

    for (size_t index = 0; index != rep->as.deep.prefix_count; ++index) {
        ft_status status = ft_visit_element(policy, &rep->as.deep.prefix[index], visitor, context);
        if (status != FT_STATUS_OK) {
            return status;
        }
    }

    ft_tree_rep* middle = NULL;
    ft_status status = ft_middle_force(policy, rep->as.deep.middle, &middle);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_visit_rep(policy, middle, visitor, context);
    ft_rep_release(policy, middle);
    if (status != FT_STATUS_OK) {
        return status;
    }

    for (size_t index = 0; index != rep->as.deep.suffix_count; ++index) {
        status = ft_visit_element(policy, &rep->as.deep.suffix[index], visitor, context);
        if (status != FT_STATUS_OK) {
            return status;
        }
    }

    return FT_STATUS_OK;
}

static ft_status ft_rep_from_digit(
    const ft_tree_policy* policy,
    const ft_element* elements,
    size_t count,
    ft_tree_rep** result)
{
    switch (count) {
    case 0:
        return ft_rep_create_empty(policy, result);
    case 1:
        return ft_rep_create_single(policy, &elements[0], result);
    default: {
        ft_tree_rep* empty = NULL;
        ft_status status = ft_rep_create_empty(policy, &empty);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_rep_create_deep(policy, &elements[0], 1, empty, &elements[1], count - 1, result);
        ft_rep_release(policy, empty);
        return status;
    }
    }
}

static ft_status ft_deep_left(
    const ft_tree_policy* policy,
    const ft_element* prefix,
    size_t prefix_count,
    ft_tree_rep* middle,
    const ft_element* suffix,
    size_t suffix_count,
    ft_tree_rep** result)
{
    if (prefix_count != 0) {
        return ft_rep_create_deep(policy, prefix, prefix_count, middle, suffix, suffix_count, result);
    }

    ft_element node;
    ft_tree_rep* rest = NULL;
    ft_status status = ft_rep_view_left(policy, middle, &node, &rest);
    if (status == FT_STATUS_EMPTY) {
        return ft_rep_from_digit(policy, suffix, suffix_count, result);
    }

    if (status != FT_STATUS_OK) {
        return status;
    }

    if (node.kind != FT_ELEMENT_NODE) {
        ft_element_dispose(policy, &node);
        ft_rep_release(policy, rest);
        return FT_STATUS_INVALID_ARGUMENT;
    }

    status = ft_rep_create_deep(
        policy,
        node.as.node->children,
        node.as.node->child_count,
        rest,
        suffix,
        suffix_count,
        result);
    ft_element_dispose(policy, &node);
    ft_rep_release(policy, rest);
    return status;
}

static ft_status ft_deep_right(
    const ft_tree_policy* policy,
    const ft_element* prefix,
    size_t prefix_count,
    ft_tree_rep* middle,
    const ft_element* suffix,
    size_t suffix_count,
    ft_tree_rep** result)
{
    if (suffix_count != 0) {
        return ft_rep_create_deep(policy, prefix, prefix_count, middle, suffix, suffix_count, result);
    }

    ft_tree_rep* rest = NULL;
    ft_element node;
    ft_status status = ft_rep_view_right(policy, middle, &node, &rest);
    if (status == FT_STATUS_EMPTY) {
        return ft_rep_from_digit(policy, prefix, prefix_count, result);
    }

    if (status != FT_STATUS_OK) {
        return status;
    }

    if (node.kind != FT_ELEMENT_NODE) {
        ft_element_dispose(policy, &node);
        ft_rep_release(policy, rest);
        return FT_STATUS_INVALID_ARGUMENT;
    }

    status = ft_rep_create_deep(
        policy,
        prefix,
        prefix_count,
        rest,
        node.as.node->children,
        node.as.node->child_count,
        result);
    ft_element_dispose(policy, &node);
    ft_rep_release(policy, rest);
    return status;
}

static ft_status ft_split_digit_at(
    const ft_tree_policy* policy,
    const ft_element* elements,
    size_t count,
    size_t index,
    ft_element* before,
    size_t* before_count,
    ft_element* hit,
    size_t* index_in_hit,
    ft_element* after,
    size_t* after_count)
{
    *before_count = 0;
    *after_count = 0;
    for (size_t element_index = 0; element_index != count; ++element_index) {
        if (index < elements[element_index].leaf_count) {
            ft_status status = ft_element_clone(policy, &elements[element_index], hit);
            if (status != FT_STATUS_OK) {
                ft_elements_dispose(policy, before, *before_count);
                *before_count = 0;
                return status;
            }

            *index_in_hit = index;
            for (size_t rest = element_index + 1; rest != count; ++rest) {
                status = ft_element_clone(policy, &elements[rest], &after[*after_count]);
                if (status != FT_STATUS_OK) {
                    ft_element_dispose(policy, hit);
                    ft_elements_dispose(policy, before, *before_count);
                    ft_elements_dispose(policy, after, *after_count);
                    *before_count = 0;
                    *after_count = 0;
                    return status;
                }

                ++*after_count;
            }

            return FT_STATUS_OK;
        }

        ft_status status = ft_element_clone(policy, &elements[element_index], &before[*before_count]);
        if (status != FT_STATUS_OK) {
            ft_elements_dispose(policy, before, *before_count);
            *before_count = 0;
            return status;
        }

        ++*before_count;
        index -= elements[element_index].leaf_count;
    }

    ft_elements_dispose(policy, before, *before_count);
    *before_count = 0;
    return FT_STATUS_OUT_OF_RANGE;
}

static void ft_split_result_dispose(const ft_tree_policy* policy, ft_split_result* split)
{
    ft_rep_release(policy, split->left);
    ft_element_dispose(policy, &split->hit);
    ft_rep_release(policy, split->right);
    (void)memset(split, 0, sizeof(*split));
}

static ft_status ft_rep_split_tree_at(
    const ft_tree_policy* policy,
    const ft_tree_rep* rep,
    size_t index,
    ft_split_result* result)
{
    (void)memset(result, 0, sizeof(*result));
    if (index >= rep->leaf_count) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    if (rep->kind == FT_REP_SINGLE) {
        ft_status status = ft_rep_create_empty(policy, &result->left);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_element_clone(policy, &rep->as.single, &result->hit);
        if (status != FT_STATUS_OK) {
            ft_rep_release(policy, result->left);
            result->left = NULL;
            return status;
        }

        status = ft_rep_create_empty(policy, &result->right);
        if (status != FT_STATUS_OK) {
            ft_split_result_dispose(policy, result);
            return status;
        }

        result->index_in_hit = index;
        return FT_STATUS_OK;
    }

    size_t prefix_size = 0;
    for (size_t element_index = 0; element_index != rep->as.deep.prefix_count; ++element_index) {
        prefix_size += rep->as.deep.prefix[element_index].leaf_count;
    }

    ft_tree_rep* middle = NULL;
    ft_status status = ft_middle_force(policy, rep->as.deep.middle, &middle);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (index < prefix_size) {
        ft_element before[4];
        ft_element after[4];
        size_t before_count = 0;
        size_t after_count = 0;
        status = ft_split_digit_at(
            policy,
            rep->as.deep.prefix,
            rep->as.deep.prefix_count,
            index,
            before,
            &before_count,
            &result->hit,
            &result->index_in_hit,
            after,
            &after_count);
        if (status == FT_STATUS_OK) {
            status = ft_rep_from_digit(policy, before, before_count, &result->left);
        }

        if (status == FT_STATUS_OK) {
            status = ft_deep_left(
                policy,
                after,
                after_count,
                middle,
                rep->as.deep.suffix,
                rep->as.deep.suffix_count,
                &result->right);
        }

        ft_elements_dispose(policy, before, before_count);
        ft_elements_dispose(policy, after, after_count);
    } else if ((index -= prefix_size) < middle->leaf_count) {
        ft_split_result middle_split;
        status = ft_rep_split_tree_at(policy, middle, index, &middle_split);
        if (status == FT_STATUS_OK) {
            if (middle_split.hit.kind != FT_ELEMENT_NODE) {
                ft_split_result_dispose(policy, &middle_split);
                status = FT_STATUS_INVALID_ARGUMENT;
            } else {
                ft_element before[3];
                ft_element after[3];
                size_t before_count = 0;
                size_t after_count = 0;
                status = ft_split_digit_at(
                    policy,
                    middle_split.hit.as.node->children,
                    middle_split.hit.as.node->child_count,
                    middle_split.index_in_hit,
                    before,
                    &before_count,
                    &result->hit,
                    &result->index_in_hit,
                    after,
                    &after_count);
                if (status == FT_STATUS_OK) {
                    status = ft_deep_right(
                        policy,
                        rep->as.deep.prefix,
                        rep->as.deep.prefix_count,
                        middle_split.left,
                        before,
                        before_count,
                        &result->left);
                }

                if (status == FT_STATUS_OK) {
                    status = ft_deep_left(
                        policy,
                        after,
                        after_count,
                        middle_split.right,
                        rep->as.deep.suffix,
                        rep->as.deep.suffix_count,
                        &result->right);
                }

                ft_elements_dispose(policy, before, before_count);
                ft_elements_dispose(policy, after, after_count);
                ft_split_result_dispose(policy, &middle_split);
            }
        }
    } else {
        index -= middle->leaf_count;
        ft_element before[4];
        ft_element after[4];
        size_t before_count = 0;
        size_t after_count = 0;
        status = ft_split_digit_at(
            policy,
            rep->as.deep.suffix,
            rep->as.deep.suffix_count,
            index,
            before,
            &before_count,
            &result->hit,
            &result->index_in_hit,
            after,
            &after_count);
        if (status == FT_STATUS_OK) {
            status = ft_deep_right(
                policy,
                rep->as.deep.prefix,
                rep->as.deep.prefix_count,
                middle,
                before,
                before_count,
                &result->left);
        }

        if (status == FT_STATUS_OK) {
            status = ft_rep_from_digit(policy, after, after_count, &result->right);
        }

        ft_elements_dispose(policy, before, before_count);
        ft_elements_dispose(policy, after, after_count);
    }

    ft_rep_release(policy, middle);
    if (status != FT_STATUS_OK) {
        ft_split_result_dispose(policy, result);
    }

    return status;
}

static ft_status ft_locate_consider(ft_locate_state* state, const void* measure, size_t leaf_count, bool* selected)
{
    void* next = NULL;
    ft_status status = ft_measure_new_combine(&state->policy->measure, state->accumulator, measure, &next);
    if (status != FT_STATUS_OK) {
        return status;
    }

    *selected = state->predicate(next, state->predicate_context);
    if (*selected) {
        free(next);
        return FT_STATUS_OK;
    }

    free(state->accumulator);
    state->accumulator = next;
    state->index += leaf_count;
    return FT_STATUS_OK;
}

static ft_status ft_locate_element(ft_locate_state* state, const ft_element* element)
{
    bool selected = false;
    ft_status status = ft_locate_consider(state, element->measure, element->leaf_count, &selected);
    if (status != FT_STATUS_OK || !selected) {
        return status;
    }

    if (element->kind == FT_ELEMENT_LEAF) {
        state->hit = element;
        return FT_STATUS_OK;
    }

    for (size_t child = 0; child != element->as.node->child_count && state->hit == NULL; ++child) {
        status = ft_locate_element(state, &element->as.node->children[child]);
        if (status != FT_STATUS_OK) {
            return status;
        }
    }

    return FT_STATUS_OK;
}

static ft_status ft_locate_rep(ft_locate_state* state, const ft_tree_rep* rep)
{
    if (rep->kind == FT_REP_EMPTY) {
        return FT_STATUS_OK;
    }

    if (rep->kind == FT_REP_SINGLE) {
        return ft_locate_element(state, &rep->as.single);
    }

    ft_status status = FT_STATUS_OK;
    for (size_t index = 0; index != rep->as.deep.prefix_count && state->hit == NULL; ++index) {
        status = ft_locate_element(state, &rep->as.deep.prefix[index]);
        if (status != FT_STATUS_OK) {
            return status;
        }
    }

    if (state->hit != NULL) {
        return FT_STATUS_OK;
    }

    const void* middle_measure = NULL;
    status = ft_middle_get_measure(state->policy, rep->as.deep.middle, &middle_measure);
    if (status != FT_STATUS_OK) {
        return status;
    }

    bool selected = false;
    status = ft_locate_consider(state, middle_measure, rep->as.deep.middle->leaf_count, &selected);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (selected) {
        ft_tree_rep* middle = NULL;
        status = ft_middle_force(state->policy, rep->as.deep.middle, &middle);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_locate_rep(state, middle);
        ft_rep_release(state->policy, middle);
        return status;
    }

    for (size_t index = 0; index != rep->as.deep.suffix_count && state->hit == NULL; ++index) {
        status = ft_locate_element(state, &rep->as.deep.suffix[index]);
        if (status != FT_STATUS_OK) {
            return status;
        }
    }

    return FT_STATUS_OK;
}

static bool ft_tree_is_valid(const ft_tree* tree)
{
    return tree != NULL && tree->policy != NULL && tree->rep != NULL;
}

typedef bool (*ft_leaf_bound_predicate_fn)(const void* leaf, void* context);

static ft_status ft_bound_element(
    const ft_tree_policy* policy,
    const ft_element* element,
    ft_leaf_bound_predicate_fn predicate,
    void* context,
    size_t* index,
    const void** hit)
{
    (void)policy;
    if (!predicate(element->last_leaf, context)) {
        *index += element->leaf_count;
        return FT_STATUS_OK;
    }

    if (element->kind == FT_ELEMENT_LEAF) {
        *hit = element->as.value;
        return FT_STATUS_OK;
    }

    for (size_t child = 0; child != element->as.node->child_count && *hit == NULL; ++child) {
        ft_status status = ft_bound_element(
            policy,
            &element->as.node->children[child],
            predicate,
            context,
            index,
            hit);
        if (status != FT_STATUS_OK) {
            return status;
        }
    }

    return FT_STATUS_OK;
}

static ft_status ft_bound_rep(
    const ft_tree_policy* policy,
    const ft_tree_rep* rep,
    ft_leaf_bound_predicate_fn predicate,
    void* context,
    size_t* index,
    const void** hit)
{
    if (rep->kind == FT_REP_EMPTY || *hit != NULL) {
        return FT_STATUS_OK;
    }

    if (!predicate(rep->last_leaf, context)) {
        *index += rep->leaf_count;
        return FT_STATUS_OK;
    }

    if (rep->kind == FT_REP_SINGLE) {
        return ft_bound_element(policy, &rep->as.single, predicate, context, index, hit);
    }

    for (size_t element = 0; element != rep->as.deep.prefix_count && *hit == NULL; ++element) {
        ft_status status = ft_bound_element(
            policy,
            &rep->as.deep.prefix[element],
            predicate,
            context,
            index,
            hit);
        if (status != FT_STATUS_OK) {
            return status;
        }
    }

    if (*hit == NULL) {
        ft_tree_rep* middle = NULL;
        ft_status status = ft_middle_force(policy, rep->as.deep.middle, &middle);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_bound_rep(policy, middle, predicate, context, index, hit);
        ft_rep_release(policy, middle);
        if (status != FT_STATUS_OK) {
            return status;
        }
    }

    for (size_t element = 0; element != rep->as.deep.suffix_count && *hit == NULL; ++element) {
        ft_status status = ft_bound_element(
            policy,
            &rep->as.deep.suffix[element],
            predicate,
            context,
            index,
            hit);
        if (status != FT_STATUS_OK) {
            return status;
        }
    }

    return FT_STATUS_OK;
}

/* Finds the first leaf satisfying a monotone predicate. The cached last-leaf
 * signpost on every grouping element lets the descent reject each preceding
 * subtree in O(1), so the whole search visits one root-to-leaf path. The hit
 * pointer is borrowed from the immutable tree and remains valid while the
 * caller retains its tree handle. */
static ft_status ft_tree_bound(
    const ft_tree* tree,
    ft_leaf_bound_predicate_fn predicate,
    void* context,
    size_t* index,
    const void** hit)
{
    if (!ft_tree_is_valid(tree) || predicate == NULL || index == NULL || hit == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    *index = 0;
    *hit = NULL;
    return ft_bound_rep(tree->policy, tree->rep, predicate, context, index, hit);
}

static void ft_size_identity(void* destination, void* context)
{
    (void)context;
    *(size_t*)destination = 0;
}

static void ft_size_measure(void* destination, const void* value, void* context)
{
    (void)value;
    (void)context;
    *(size_t*)destination = 1;
}

static void ft_size_combine(void* destination, const void* left, const void* right, void* context)
{
    (void)context;
    const size_t left_value = *(const size_t*)left;
    const size_t right_value = *(const size_t*)right;
    *(size_t*)destination = left_value + right_value;
}

void ft_value_type_init(ft_value_type* type, size_t size)
{
    if (type == NULL) {
        return;
    }

    type->size = size;
    type->copy = NULL;
    type->destroy = NULL;
    type->context = NULL;
}

void ft_size_measure_policy_init(ft_measure_policy* policy)
{
    if (policy == NULL) {
        return;
    }

    policy->size = sizeof(size_t);
    policy->identity = ft_size_identity;
    policy->measure = ft_size_measure;
    policy->combine = ft_size_combine;
    policy->context = NULL;
}

void ft_tree_policy_init_size(ft_tree_policy* policy, const ft_value_type* value_type)
{
    if (policy == NULL || value_type == NULL) {
        return;
    }

    policy->value = *value_type;
    ft_size_measure_policy_init(&policy->measure);
}

ft_status ft_tree_init(ft_tree* tree, const ft_tree_policy* policy)
{
    if (tree == NULL || policy == NULL || policy->value.size == 0 || policy->measure.size == 0 ||
        policy->measure.identity == NULL || policy->measure.measure == NULL || policy->measure.combine == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_tree_rep* rep = NULL;
    ft_status status = ft_rep_create_empty(policy, &rep);
    if (status != FT_STATUS_OK) {
        return status;
    }

    tree->policy = policy;
    tree->rep = rep;
    return FT_STATUS_OK;
}

ft_status ft_tree_copy(const ft_tree* source, ft_tree* destination)
{
    if (!ft_tree_is_valid(source) || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_rep_retain(source->rep);
    destination->policy = source->policy;
    destination->rep = source->rep;
    return FT_STATUS_OK;
}

void ft_tree_dispose(ft_tree* tree)
{
    if (tree == NULL || tree->policy == NULL || tree->rep == NULL) {
        return;
    }

    ft_rep_release(tree->policy, tree->rep);
    tree->policy = NULL;
    tree->rep = NULL;
}

bool ft_tree_empty(const ft_tree* tree)
{
    return !ft_tree_is_valid(tree) || tree->rep->leaf_count == 0;
}

size_t ft_tree_size(const ft_tree* tree)
{
    return ft_tree_is_valid(tree) ? tree->rep->leaf_count : 0;
}

ft_status ft_tree_measure(const ft_tree* tree, void* destination)
{
    if (!ft_tree_is_valid(tree) || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    const void* measure = NULL;
    ft_status status = ft_rep_get_measure(tree->policy, tree->rep, &measure);
    if (status != FT_STATUS_OK) {
        return status;
    }

    (void)memcpy(destination, measure, tree->policy->measure.size);
    return FT_STATUS_OK;
}

ft_status ft_tree_front(const ft_tree* tree, void* destination)
{
    return ft_tree_at(tree, 0, destination);
}

ft_status ft_tree_back(const ft_tree* tree, void* destination)
{
    if (!ft_tree_is_valid(tree)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    if (tree->rep->leaf_count == 0) {
        return FT_STATUS_EMPTY;
    }

    return ft_tree_at(tree, tree->rep->leaf_count - 1, destination);
}

ft_status ft_tree_at(const ft_tree* tree, size_t index, void* destination)
{
    if (!ft_tree_is_valid(tree) || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    if (index >= tree->rep->leaf_count) {
        return tree->rep->leaf_count == 0 ? FT_STATUS_EMPTY : FT_STATUS_OUT_OF_RANGE;
    }

    return ft_rep_copy_leaf_at(tree->policy, tree->rep, index, destination);
}

ft_status ft_tree_push_front(const ft_tree* tree, const void* value, ft_tree* result)
{
    if (!ft_tree_is_valid(tree) || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_element leaf;
    ft_status status = ft_element_init_leaf(tree->policy, value, &leaf);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_tree_rep* rep = NULL;
    status = ft_rep_cons(tree->policy, tree->rep, &leaf, &rep);
    ft_element_dispose(tree->policy, &leaf);
    if (status != FT_STATUS_OK) {
        return status;
    }

    result->policy = tree->policy;
    result->rep = rep;
    return FT_STATUS_OK;
}

ft_status ft_tree_push_back(const ft_tree* tree, const void* value, ft_tree* result)
{
    if (!ft_tree_is_valid(tree) || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_element leaf;
    ft_status status = ft_element_init_leaf(tree->policy, value, &leaf);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_tree_rep* rep = NULL;
    status = ft_rep_snoc(tree->policy, tree->rep, &leaf, &rep);
    ft_element_dispose(tree->policy, &leaf);
    if (status != FT_STATUS_OK) {
        return status;
    }

    result->policy = tree->policy;
    result->rep = rep;
    return FT_STATUS_OK;
}

ft_status ft_tree_pop_front(const ft_tree* tree, void* value, ft_tree* rest)
{
    if (!ft_tree_is_valid(tree) || rest == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_element hit;
    ft_tree_rep* next = NULL;
    ft_status status = ft_rep_view_left(tree->policy, tree->rep, &hit, &next);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (hit.kind != FT_ELEMENT_LEAF) {
        ft_element_dispose(tree->policy, &hit);
        ft_rep_release(tree->policy, next);
        return FT_STATUS_INVALID_ARGUMENT;
    }

    if (value != NULL) {
        ft_value_copy(&tree->policy->value, value, hit.as.value);
    }

    ft_element_dispose(tree->policy, &hit);
    rest->policy = tree->policy;
    rest->rep = next;
    return FT_STATUS_OK;
}

ft_status ft_tree_pop_back(const ft_tree* tree, void* value, ft_tree* rest)
{
    if (!ft_tree_is_valid(tree) || rest == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_element hit;
    ft_tree_rep* next = NULL;
    ft_status status = ft_rep_view_right(tree->policy, tree->rep, &hit, &next);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (hit.kind != FT_ELEMENT_LEAF) {
        ft_element_dispose(tree->policy, &hit);
        ft_rep_release(tree->policy, next);
        return FT_STATUS_INVALID_ARGUMENT;
    }

    if (value != NULL) {
        ft_value_copy(&tree->policy->value, value, hit.as.value);
    }

    ft_element_dispose(tree->policy, &hit);
    rest->policy = tree->policy;
    rest->rep = next;
    return FT_STATUS_OK;
}

ft_status ft_tree_concat(const ft_tree* left, const ft_tree* right, ft_tree* result)
{
    if (!ft_tree_is_valid(left) || !ft_tree_is_valid(right) || result == NULL || left->policy != right->policy) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_tree_rep* rep = NULL;
    ft_status status = ft_rep_concat_with_middle(left->policy, left->rep, NULL, 0, right->rep, &rep);
    if (status != FT_STATUS_OK) {
        return status;
    }

    result->policy = left->policy;
    result->rep = rep;
    return FT_STATUS_OK;
}

ft_status ft_tree_split_at(const ft_tree* tree, size_t index, ft_tree_split_result* result)
{
    if (!ft_tree_is_valid(tree) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    if (index > tree->rep->leaf_count) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    if (index == 0) {
        ft_status status = ft_tree_init(&result->left, tree->policy);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_tree_copy(tree, &result->right);
        if (status != FT_STATUS_OK) {
            ft_tree_dispose(&result->left);
        }

        return status;
    }

    if (index == tree->rep->leaf_count) {
        ft_status status = ft_tree_copy(tree, &result->left);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_tree_init(&result->right, tree->policy);
        if (status != FT_STATUS_OK) {
            ft_tree_dispose(&result->left);
        }

        return status;
    }

    ft_split_result split;
    ft_status status = ft_rep_split_tree_at(tree->policy, tree->rep, index, &split);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_tree_rep* right_with_hit = NULL;
    status = ft_rep_cons(tree->policy, split.right, &split.hit, &right_with_hit);
    if (status == FT_STATUS_OK) {
        result->left.policy = tree->policy;
        result->left.rep = split.left;
        result->right.policy = tree->policy;
        result->right.rep = right_with_hit;
        split.left = NULL;
    }

    ft_split_result_dispose(tree->policy, &split);
    return status;
}

ft_status ft_tree_set_at(const ft_tree* tree, size_t index, const void* value, ft_tree* result)
{
    if (!ft_tree_is_valid(tree) || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    if (index >= tree->rep->leaf_count) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    ft_tree_rep* rep = NULL;
    ft_status status = ft_rep_set_leaf(tree->policy, tree->rep, index, value, &rep);
    if (status != FT_STATUS_OK) {
        return status;
    }

    result->policy = tree->policy;
    result->rep = rep;
    return FT_STATUS_OK;
}

ft_status ft_tree_insert_at(const ft_tree* tree, size_t index, const void* value, ft_tree* result)
{
    if (!ft_tree_is_valid(tree) || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_tree_split_result split;
    ft_status status = ft_tree_split_at(tree, index, &split);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_tree with_item;
    status = ft_tree_push_back(&split.left, value, &with_item);
    if (status != FT_STATUS_OK) {
        ft_tree_dispose(&split.left);
        ft_tree_dispose(&split.right);
        return status;
    }

    status = ft_tree_concat(&with_item, &split.right, result);
    ft_tree_dispose(&with_item);
    ft_tree_dispose(&split.left);
    ft_tree_dispose(&split.right);
    return status;
}

ft_status ft_tree_remove_at(const ft_tree* tree, size_t index, ft_tree* result)
{
    if (!ft_tree_is_valid(tree) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    if (index >= ft_tree_size(tree)) {
        /* Report a bad index directly instead of surfacing the internal
         * pop-on-empty as FT_STATUS_EMPTY (matches ft_tree_at/set_at and the
         * reversible deque). */
        return FT_STATUS_OUT_OF_RANGE;
    }

    ft_tree_split_result split;
    ft_status status = ft_tree_split_at(tree, index, &split);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_tree tail;
    status = ft_tree_pop_front(&split.right, NULL, &tail);
    if (status != FT_STATUS_OK) {
        ft_tree_dispose(&split.left);
        ft_tree_dispose(&split.right);
        return status;
    }

    status = ft_tree_concat(&split.left, &tail, result);
    ft_tree_dispose(&tail);
    ft_tree_dispose(&split.left);
    ft_tree_dispose(&split.right);
    return status;
}

ft_status ft_tree_locate(
    const ft_tree* tree,
    ft_measure_predicate_fn predicate,
    void* predicate_context,
    bool* found,
    void* measure_before,
    void* value)
{
    if (!ft_tree_is_valid(tree) || predicate == NULL || found == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_locate_state state;
    (void)memset(&state, 0, sizeof(state));
    state.policy = tree->policy;
    state.predicate = predicate;
    state.predicate_context = predicate_context;

    ft_status status = ft_measure_new_identity(&tree->policy->measure, &state.accumulator);
    if (status != FT_STATUS_OK) {
        return status;
    }

    const void* total = NULL;
    status = ft_rep_get_measure(tree->policy, tree->rep, &total);
    if (status == FT_STATUS_OK && predicate(total, predicate_context)) {
        status = ft_locate_rep(&state, tree->rep);
    } else if (status == FT_STATUS_OK) {
        free(state.accumulator);
        state.accumulator = NULL;
        status = ft_measure_new_copy(&tree->policy->measure, total, &state.accumulator);
    }

    if (status == FT_STATUS_OK && measure_before != NULL) {
        (void)memcpy(measure_before, state.accumulator, tree->policy->measure.size);
    }

    if (status == FT_STATUS_OK && state.hit != NULL && value != NULL) {
        ft_value_copy(&tree->policy->value, value, state.hit->as.value);
    }

    *found = state.hit != NULL;
    free(state.accumulator);
    return status;
}

ft_status ft_tree_split(
    const ft_tree* tree,
    ft_measure_predicate_fn predicate,
    void* predicate_context,
    bool* found,
    ft_tree* left,
    void* value,
    ft_tree* right)
{
    if (!ft_tree_is_valid(tree) || predicate == NULL || found == NULL || left == NULL || right == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_locate_state state;
    (void)memset(&state, 0, sizeof(state));
    state.policy = tree->policy;
    state.predicate = predicate;
    state.predicate_context = predicate_context;

    ft_status status = ft_measure_new_identity(&tree->policy->measure, &state.accumulator);
    if (status != FT_STATUS_OK) {
        return status;
    }

    const void* total = NULL;
    status = ft_rep_get_measure(tree->policy, tree->rep, &total);
    if (status == FT_STATUS_OK && predicate(total, predicate_context)) {
        status = ft_locate_rep(&state, tree->rep);
    }

    free(state.accumulator);
    if (status != FT_STATUS_OK) {
        return status;
    }

    *found = state.hit != NULL;
    if (state.hit == NULL) {
        return FT_STATUS_OK;
    }

    ft_tree_split_result split;
    status = ft_tree_split_at(tree, state.index, &split);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_tree_pop_front(&split.right, value, right);
    if (status != FT_STATUS_OK) {
        ft_tree_dispose(&split.left);
        ft_tree_dispose(&split.right);
        return status;
    }

    *left = split.left;
    ft_tree_dispose(&split.right);
    return FT_STATUS_OK;
}

ft_status ft_tree_visit(const ft_tree* tree, ft_visit_fn visitor, void* context)
{
    if (!ft_tree_is_valid(tree) || visitor == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    return ft_visit_rep(tree->policy, tree->rep, visitor, context);
}

static void ft_rev_rep_retain(ft_reversible_deque_rep* rep)
{
    if (rep != NULL) {
        ft_ref_retain(&rep->ref_count);
    }
}

static void ft_rev_element_dispose(const ft_tree_policy* policy, ft_rev_element* element);
static void ft_rev_rep_release(const ft_tree_policy* policy, ft_reversible_deque_rep* rep);
static ft_status ft_rev_rep_cons(
    const ft_tree_policy* policy,
    const ft_reversible_deque_rep* rep,
    const ft_rev_element* value,
    ft_reversible_deque_rep** result);
static ft_status ft_rev_rep_snoc(
    const ft_tree_policy* policy,
    const ft_reversible_deque_rep* rep,
    const ft_rev_element* value,
    ft_reversible_deque_rep** result);
static ft_status ft_rev_rep_view_left(
    const ft_tree_policy* policy,
    const ft_reversible_deque_rep* rep,
    ft_rev_element* value,
    ft_reversible_deque_rep** rest);
static ft_status ft_rev_rep_view_right(
    const ft_tree_policy* policy,
    const ft_reversible_deque_rep* rep,
    ft_reversible_deque_rep** rest,
    ft_rev_element* value);
static ft_status ft_rev_rep_concat_with_middle(
    const ft_tree_policy* policy,
    const ft_reversible_deque_rep* left,
    const ft_rev_element* middle,
    size_t middle_count,
    const ft_reversible_deque_rep* right,
    ft_reversible_deque_rep** result);

static bool ft_reversible_deque_is_valid(const ft_reversible_deque* deque)
{
    return deque != NULL && deque->policy != NULL && deque->rep != NULL;
}

static void ft_rev_node_retain(ft_rev_node* node)
{
    if (node != NULL) {
        ft_ref_retain(&node->ref_count);
    }
}

static void ft_rev_node_release(const ft_tree_policy* policy, ft_rev_node* node)
{
    if (node == NULL) {
        return;
    }

    if (!ft_ref_release(&node->ref_count)) {
        return;
    }

    for (size_t index = 0; index != node->child_count; ++index) {
        ft_rev_element_dispose(policy, &node->children[index]);
    }

    free(node);
}

static void ft_rev_elements_dispose(const ft_tree_policy* policy, ft_rev_element* elements, size_t count)
{
    for (size_t index = 0; index != count; ++index) {
        ft_rev_element_dispose(policy, &elements[index]);
    }
}

static ft_status ft_rev_element_clone(
    const ft_tree_policy* policy,
    const ft_rev_element* source,
    ft_rev_element* destination)
{
    (void)memset(destination, 0, sizeof(*destination));
    destination->kind = source->kind;
    destination->leaf_count = source->leaf_count;
    if (source->kind == FT_REV_ELEMENT_LEAF) {
        destination->as.value = ft_allocate(policy->value.size);
        if (destination->as.value == NULL) {
            (void)memset(destination, 0, sizeof(*destination));
            return FT_STATUS_NO_MEMORY;
        }

        ft_value_copy(&policy->value, destination->as.value, source->as.value);
        return FT_STATUS_OK;
    }

    destination->as.node = source->as.node;
    ft_rev_node_retain(destination->as.node);
    return FT_STATUS_OK;
}

static ft_status ft_rev_element_init_leaf(
    const ft_tree_policy* policy,
    const void* value,
    ft_rev_element* destination)
{
    (void)memset(destination, 0, sizeof(*destination));
    destination->kind = FT_REV_ELEMENT_LEAF;
    destination->leaf_count = 1;
    destination->as.value = ft_allocate(policy->value.size);
    if (destination->as.value == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    ft_value_copy(&policy->value, destination->as.value, value);
    return FT_STATUS_OK;
}

static void ft_rev_element_dispose(const ft_tree_policy* policy, ft_rev_element* element)
{
    if (element->kind == FT_REV_ELEMENT_LEAF) {
        ft_value_destroy(&policy->value, element->as.value);
        free(element->as.value);
    } else if (element->kind == FT_REV_ELEMENT_NODE) {
        ft_rev_node_release(policy, element->as.node);
    }

    (void)memset(element, 0, sizeof(*element));
}

static ft_status ft_rev_node_create(
    const ft_tree_policy* policy,
    const ft_rev_element* children,
    size_t count,
    bool reversed,
    ft_rev_node** result)
{
    if (count < 2 || count > 3) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_rev_node* node = (ft_rev_node*)calloc(1, sizeof(*node));
    if (node == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    ft_ref_init(&node->ref_count);
    node->reversed = reversed;

    ft_status status = FT_STATUS_OK;
    for (size_t index = 0; index != count; ++index) {
        status = ft_rev_element_clone(policy, &children[index], &node->children[index]);
        if (status != FT_STATUS_OK) {
            ft_rev_node_release(policy, node);
            return status;
        }

        ++node->child_count;
        if (ft_add_overflows(node->leaf_count, children[index].leaf_count, &node->leaf_count)) {
            ft_rev_node_release(policy, node);
            return FT_STATUS_OVERFLOW;
        }
    }

    *result = node;
    return FT_STATUS_OK;
}

static ft_status ft_rev_element_init_node(
    const ft_tree_policy* policy,
    const ft_rev_element* children,
    size_t count,
    ft_rev_element* result)
{
    (void)memset(result, 0, sizeof(*result));
    ft_rev_node* node = NULL;
    ft_status status = ft_rev_node_create(policy, children, count, false, &node);
    if (status != FT_STATUS_OK) {
        return status;
    }

    result->kind = FT_REV_ELEMENT_NODE;
    result->leaf_count = node->leaf_count;
    result->as.node = node;
    return FT_STATUS_OK;
}

static ft_status ft_rev_element_mirror(
    const ft_tree_policy* policy,
    const ft_rev_element* source,
    ft_rev_element* destination)
{
    if (source->kind == FT_REV_ELEMENT_LEAF) {
        return ft_rev_element_clone(policy, source, destination);
    }

    ft_rev_node* node = NULL;
    ft_status status = ft_rev_node_create(
        policy,
        source->as.node->children,
        source->as.node->child_count,
        !source->as.node->reversed,
        &node);
    if (status != FT_STATUS_OK) {
        return status;
    }

    destination->kind = FT_REV_ELEMENT_NODE;
    destination->leaf_count = node->leaf_count;
    destination->as.node = node;
    return FT_STATUS_OK;
}

static ft_status ft_rev_node_logical_child(
    const ft_tree_policy* policy,
    const ft_rev_node* node,
    size_t index,
    ft_rev_element* result)
{
    if (index >= node->child_count) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    const size_t physical = node->reversed ? node->child_count - 1 - index : index;
    const ft_rev_element* child = &node->children[physical];
    return node->reversed
        ? ft_rev_element_mirror(policy, child, result)
        : ft_rev_element_clone(policy, child, result);
}

static ft_status ft_rev_node_logical_children(
    const ft_tree_policy* policy,
    const ft_rev_node* node,
    ft_rev_element* children,
    size_t* count)
{
    *count = 0;
    for (size_t index = 0; index != node->child_count; ++index) {
        ft_status status = ft_rev_node_logical_child(policy, node, index, &children[*count]);
        if (status != FT_STATUS_OK) {
            ft_rev_elements_dispose(policy, children, *count);
            *count = 0;
            return status;
        }

        ++*count;
    }

    return FT_STATUS_OK;
}

static ft_status ft_rev_count_elements(const ft_rev_element* elements, size_t count, size_t* leaf_count)
{
    *leaf_count = 0;
    for (size_t index = 0; index != count; ++index) {
        if (ft_add_overflows(*leaf_count, elements[index].leaf_count, leaf_count)) {
            return FT_STATUS_OVERFLOW;
        }
    }

    return FT_STATUS_OK;
}

static void ft_rev_rep_release(const ft_tree_policy* policy, ft_reversible_deque_rep* rep)
{
    if (rep == NULL) {
        return;
    }

    if (!ft_ref_release(&rep->ref_count)) {
        return;
    }

    if (rep->kind == FT_REV_REP_SINGLE) {
        ft_rev_element_dispose(policy, &rep->as.single);
    } else if (rep->kind == FT_REV_REP_DEEP) {
        ft_rev_elements_dispose(policy, rep->as.deep.prefix, rep->as.deep.prefix_count);
        ft_rev_rep_release(policy, rep->as.deep.middle);
        ft_rev_elements_dispose(policy, rep->as.deep.suffix, rep->as.deep.suffix_count);
    }

    free(rep);
}

static ft_status ft_rev_rep_create_empty(ft_reversible_deque_rep** result)
{
    ft_reversible_deque_rep* rep = (ft_reversible_deque_rep*)calloc(1, sizeof(*rep));
    if (rep == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    ft_ref_init(&rep->ref_count);
    rep->kind = FT_REV_REP_EMPTY;
    *result = rep;
    return FT_STATUS_OK;
}

static ft_status ft_rev_rep_create_single(
    const ft_tree_policy* policy,
    const ft_rev_element* element,
    ft_reversible_deque_rep** result)
{
    ft_reversible_deque_rep* rep = (ft_reversible_deque_rep*)calloc(1, sizeof(*rep));
    if (rep == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    ft_ref_init(&rep->ref_count);
    rep->kind = FT_REV_REP_SINGLE;
    rep->leaf_count = element->leaf_count;
    ft_status status = ft_rev_element_clone(policy, element, &rep->as.single);
    if (status != FT_STATUS_OK) {
        free(rep);
        return status;
    }

    *result = rep;
    return FT_STATUS_OK;
}

static ft_status ft_rev_rep_create_deep_ex(
    const ft_tree_policy* policy,
    const ft_rev_element* prefix,
    size_t prefix_count,
    ft_reversible_deque_rep* middle,
    const ft_rev_element* suffix,
    size_t suffix_count,
    bool reversed,
    ft_reversible_deque_rep** result)
{
    if (prefix_count == 0 || prefix_count > 3 || suffix_count == 0 || suffix_count > 3 || middle == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_reversible_deque_rep* rep = (ft_reversible_deque_rep*)calloc(1, sizeof(*rep));
    if (rep == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    ft_ref_init(&rep->ref_count);
    rep->kind = FT_REV_REP_DEEP;
    rep->reversed = reversed;
    rep->as.deep.prefix_count = prefix_count;
    rep->as.deep.suffix_count = suffix_count;

    ft_status status = FT_STATUS_OK;
    for (size_t index = 0; index != prefix_count; ++index) {
        status = ft_rev_element_clone(policy, &prefix[index], &rep->as.deep.prefix[index]);
        if (status != FT_STATUS_OK) {
            rep->as.deep.prefix_count = index;
            ft_rev_rep_release(policy, rep);
            return status;
        }
    }

    rep->as.deep.middle = middle;
    ft_rev_rep_retain(middle);

    for (size_t index = 0; index != suffix_count; ++index) {
        status = ft_rev_element_clone(policy, &suffix[index], &rep->as.deep.suffix[index]);
        if (status != FT_STATUS_OK) {
            rep->as.deep.suffix_count = index;
            ft_rev_rep_release(policy, rep);
            return status;
        }
    }

    size_t prefix_leaves = 0;
    size_t suffix_leaves = 0;
    size_t prefix_and_middle = 0;
    status = ft_rev_count_elements(prefix, prefix_count, &prefix_leaves);
    if (status != FT_STATUS_OK ||
        ft_rev_count_elements(suffix, suffix_count, &suffix_leaves) != FT_STATUS_OK ||
        ft_add_overflows(prefix_leaves, middle->leaf_count, &prefix_and_middle) ||
        ft_add_overflows(prefix_and_middle, suffix_leaves, &rep->leaf_count)) {
        ft_rev_rep_release(policy, rep);
        return status == FT_STATUS_OK ? FT_STATUS_OVERFLOW : status;
    }

    *result = rep;
    return FT_STATUS_OK;
}

static ft_status ft_rev_rep_create_deep(
    const ft_tree_policy* policy,
    const ft_rev_element* prefix,
    size_t prefix_count,
    ft_reversible_deque_rep* middle,
    const ft_rev_element* suffix,
    size_t suffix_count,
    ft_reversible_deque_rep** result)
{
    return ft_rev_rep_create_deep_ex(policy, prefix, prefix_count, middle, suffix, suffix_count, false, result);
}

static ft_status ft_rev_rep_mirror(
    const ft_tree_policy* policy,
    const ft_reversible_deque_rep* source,
    ft_reversible_deque_rep** result)
{
    if (source->kind == FT_REV_REP_EMPTY) {
        ft_rev_rep_retain((ft_reversible_deque_rep*)source);
        *result = (ft_reversible_deque_rep*)source;
        return FT_STATUS_OK;
    }

    if (source->kind == FT_REV_REP_SINGLE) {
        ft_rev_element element;
        ft_status status = ft_rev_element_mirror(policy, &source->as.single, &element);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_rev_rep_create_single(policy, &element, result);
        ft_rev_element_dispose(policy, &element);
        return status;
    }

    return ft_rev_rep_create_deep_ex(
        policy,
        source->as.deep.prefix,
        source->as.deep.prefix_count,
        source->as.deep.middle,
        source->as.deep.suffix,
        source->as.deep.suffix_count,
        !source->reversed,
        result);
}

static ft_status ft_rev_mirror_reversed(
    const ft_tree_policy* policy,
    const ft_rev_element* source,
    size_t count,
    ft_rev_element* result)
{
    for (size_t index = 0; index != count; ++index) {
        ft_status status = ft_rev_element_mirror(policy, &source[count - 1 - index], &result[index]);
        if (status != FT_STATUS_OK) {
            ft_rev_elements_dispose(policy, result, index);
            return status;
        }
    }

    return FT_STATUS_OK;
}

static ft_status ft_rev_rep_logical_parts(
    const ft_tree_policy* policy,
    const ft_reversible_deque_rep* rep,
    ft_rev_element* prefix,
    size_t* prefix_count,
    ft_reversible_deque_rep** middle,
    ft_rev_element* suffix,
    size_t* suffix_count)
{
    if (rep->kind != FT_REV_REP_DEEP) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    if (rep->reversed) {
        *prefix_count = rep->as.deep.suffix_count;
        *suffix_count = rep->as.deep.prefix_count;

        ft_status status = ft_rev_mirror_reversed(policy, rep->as.deep.suffix, *prefix_count, prefix);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_rev_rep_mirror(policy, rep->as.deep.middle, middle);
        if (status != FT_STATUS_OK) {
            ft_rev_elements_dispose(policy, prefix, *prefix_count);
            return status;
        }

        status = ft_rev_mirror_reversed(policy, rep->as.deep.prefix, *suffix_count, suffix);
        if (status != FT_STATUS_OK) {
            ft_rev_rep_release(policy, *middle);
            ft_rev_elements_dispose(policy, prefix, *prefix_count);
            return status;
        }

        return FT_STATUS_OK;
    }

    *prefix_count = rep->as.deep.prefix_count;
    *suffix_count = rep->as.deep.suffix_count;

    for (size_t index = 0; index != *prefix_count; ++index) {
        ft_status status = ft_rev_element_clone(policy, &rep->as.deep.prefix[index], &prefix[index]);
        if (status != FT_STATUS_OK) {
            ft_rev_elements_dispose(policy, prefix, index);
            return status;
        }
    }

    ft_rev_rep_retain(rep->as.deep.middle);
    *middle = rep->as.deep.middle;

    for (size_t index = 0; index != *suffix_count; ++index) {
        ft_status status = ft_rev_element_clone(policy, &rep->as.deep.suffix[index], &suffix[index]);
        if (status != FT_STATUS_OK) {
            ft_rev_rep_release(policy, *middle);
            ft_rev_elements_dispose(policy, prefix, *prefix_count);
            ft_rev_elements_dispose(policy, suffix, index);
            return status;
        }
    }

    return FT_STATUS_OK;
}

static ft_status ft_rev_rep_from_digit(
    const ft_tree_policy* policy,
    const ft_rev_element* elements,
    size_t count,
    ft_reversible_deque_rep** result)
{
    switch (count) {
    case 0:
        return ft_rev_rep_create_empty(result);
    case 1:
        return ft_rev_rep_create_single(policy, &elements[0], result);
    default: {
        ft_reversible_deque_rep* empty = NULL;
        ft_status status = ft_rev_rep_create_empty(&empty);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_rev_rep_create_deep(policy, &elements[0], 1, empty, &elements[1], count - 1, result);
        ft_rev_rep_release(policy, empty);
        return status;
    }
    }
}

static ft_status ft_rev_deep_left(
    const ft_tree_policy* policy,
    const ft_rev_element* prefix,
    size_t prefix_count,
    ft_reversible_deque_rep* middle,
    const ft_rev_element* suffix,
    size_t suffix_count,
    ft_reversible_deque_rep** result)
{
    if (prefix_count != 0) {
        return ft_rev_rep_create_deep(policy, prefix, prefix_count, middle, suffix, suffix_count, result);
    }

    ft_rev_element node;
    ft_reversible_deque_rep* rest = NULL;
    ft_status status = ft_rev_rep_view_left(policy, middle, &node, &rest);
    if (status == FT_STATUS_EMPTY) {
        return ft_rev_rep_from_digit(policy, suffix, suffix_count, result);
    }

    if (status != FT_STATUS_OK) {
        return status;
    }

    if (node.kind != FT_REV_ELEMENT_NODE) {
        ft_rev_element_dispose(policy, &node);
        ft_rev_rep_release(policy, rest);
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_rev_element children[3];
    size_t child_count = 0;
    status = ft_rev_node_logical_children(policy, node.as.node, children, &child_count);
    if (status == FT_STATUS_OK) {
        status = ft_rev_rep_create_deep(policy, children, child_count, rest, suffix, suffix_count, result);
        ft_rev_elements_dispose(policy, children, child_count);
    }

    ft_rev_element_dispose(policy, &node);
    ft_rev_rep_release(policy, rest);
    return status;
}

static ft_status ft_rev_deep_right(
    const ft_tree_policy* policy,
    const ft_rev_element* prefix,
    size_t prefix_count,
    ft_reversible_deque_rep* middle,
    const ft_rev_element* suffix,
    size_t suffix_count,
    ft_reversible_deque_rep** result)
{
    if (suffix_count != 0) {
        return ft_rev_rep_create_deep(policy, prefix, prefix_count, middle, suffix, suffix_count, result);
    }

    ft_reversible_deque_rep* rest = NULL;
    ft_rev_element node;
    ft_status status = ft_rev_rep_view_right(policy, middle, &rest, &node);
    if (status == FT_STATUS_EMPTY) {
        return ft_rev_rep_from_digit(policy, prefix, prefix_count, result);
    }

    if (status != FT_STATUS_OK) {
        return status;
    }

    if (node.kind != FT_REV_ELEMENT_NODE) {
        ft_rev_rep_release(policy, rest);
        ft_rev_element_dispose(policy, &node);
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_rev_element children[3];
    size_t child_count = 0;
    status = ft_rev_node_logical_children(policy, node.as.node, children, &child_count);
    if (status == FT_STATUS_OK) {
        status = ft_rev_rep_create_deep(policy, prefix, prefix_count, rest, children, child_count, result);
        ft_rev_elements_dispose(policy, children, child_count);
    }

    ft_rev_rep_release(policy, rest);
    ft_rev_element_dispose(policy, &node);
    return status;
}

static ft_status ft_rev_element_copy_first(
    const ft_tree_policy* policy,
    const ft_rev_element* element,
    void* destination)
{
    if (element->kind == FT_REV_ELEMENT_LEAF) {
        ft_value_copy(&policy->value, destination, element->as.value);
        return FT_STATUS_OK;
    }

    ft_rev_element child;
    ft_status status = ft_rev_node_logical_child(policy, element->as.node, 0, &child);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_rev_element_copy_first(policy, &child, destination);
    ft_rev_element_dispose(policy, &child);
    return status;
}

static ft_status ft_rev_element_copy_last(
    const ft_tree_policy* policy,
    const ft_rev_element* element,
    void* destination)
{
    if (element->kind == FT_REV_ELEMENT_LEAF) {
        ft_value_copy(&policy->value, destination, element->as.value);
        return FT_STATUS_OK;
    }

    ft_rev_element child;
    ft_status status = ft_rev_node_logical_child(policy, element->as.node, element->as.node->child_count - 1, &child);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_rev_element_copy_last(policy, &child, destination);
    ft_rev_element_dispose(policy, &child);
    return status;
}

static ft_status ft_rev_rep_copy_first(
    const ft_tree_policy* policy,
    const ft_reversible_deque_rep* rep,
    void* destination)
{
    if (rep->kind == FT_REV_REP_EMPTY) {
        return FT_STATUS_EMPTY;
    }

    if (rep->kind == FT_REV_REP_SINGLE) {
        return ft_rev_element_copy_first(policy, &rep->as.single, destination);
    }

    return rep->reversed
        ? ft_rev_element_copy_last(policy, &rep->as.deep.suffix[rep->as.deep.suffix_count - 1], destination)
        : ft_rev_element_copy_first(policy, &rep->as.deep.prefix[0], destination);
}

static ft_status ft_rev_rep_copy_last(
    const ft_tree_policy* policy,
    const ft_reversible_deque_rep* rep,
    void* destination)
{
    if (rep->kind == FT_REV_REP_EMPTY) {
        return FT_STATUS_EMPTY;
    }

    if (rep->kind == FT_REV_REP_SINGLE) {
        return ft_rev_element_copy_last(policy, &rep->as.single, destination);
    }

    return rep->reversed
        ? ft_rev_element_copy_first(policy, &rep->as.deep.prefix[0], destination)
        : ft_rev_element_copy_last(policy, &rep->as.deep.suffix[rep->as.deep.suffix_count - 1], destination);
}

static ft_status ft_rev_element_get_leaf(
    const ft_tree_policy* policy,
    const ft_rev_element* element,
    size_t index,
    void* destination)
{
    if (index >= element->leaf_count) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    if (element->kind == FT_REV_ELEMENT_LEAF) {
        ft_value_copy(&policy->value, destination, element->as.value);
        return FT_STATUS_OK;
    }

    for (size_t child_index = 0; child_index != element->as.node->child_count; ++child_index) {
        ft_rev_element child;
        ft_status status = ft_rev_node_logical_child(policy, element->as.node, child_index, &child);
        if (status != FT_STATUS_OK) {
            return status;
        }

        if (index < child.leaf_count) {
            status = ft_rev_element_get_leaf(policy, &child, index, destination);
            ft_rev_element_dispose(policy, &child);
            return status;
        }

        index -= child.leaf_count;
        ft_rev_element_dispose(policy, &child);
    }

    return FT_STATUS_OUT_OF_RANGE;
}

static ft_status ft_rev_get_in_digit(
    const ft_tree_policy* policy,
    const ft_rev_element* elements,
    size_t count,
    size_t index,
    void* destination)
{
    for (size_t element_index = 0; element_index != count; ++element_index) {
        if (index < elements[element_index].leaf_count) {
            return ft_rev_element_get_leaf(policy, &elements[element_index], index, destination);
        }

        index -= elements[element_index].leaf_count;
    }

    return FT_STATUS_OUT_OF_RANGE;
}

static ft_status ft_rev_rep_get_leaf(
    const ft_tree_policy* policy,
    const ft_reversible_deque_rep* rep,
    size_t index,
    void* destination)
{
    if (index >= rep->leaf_count) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    if (rep->kind == FT_REV_REP_SINGLE) {
        return ft_rev_element_get_leaf(policy, &rep->as.single, index, destination);
    }

    if (rep->kind != FT_REV_REP_DEEP) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    ft_rev_element prefix[3];
    ft_rev_element suffix[3];
    size_t prefix_count = 0;
    size_t suffix_count = 0;
    ft_reversible_deque_rep* middle = NULL;
    ft_status status = ft_rev_rep_logical_parts(policy, rep, prefix, &prefix_count, &middle, suffix, &suffix_count);
    if (status != FT_STATUS_OK) {
        return status;
    }

    size_t prefix_size = 0;
    (void)ft_rev_count_elements(prefix, prefix_count, &prefix_size);
    if (index < prefix_size) {
        status = ft_rev_get_in_digit(policy, prefix, prefix_count, index, destination);
        goto done;
    }

    index -= prefix_size;
    if (index < middle->leaf_count) {
        status = ft_rev_rep_get_leaf(policy, middle, index, destination);
        goto done;
    }

    index -= middle->leaf_count;
    status = ft_rev_get_in_digit(policy, suffix, suffix_count, index, destination);

done:
    ft_rev_elements_dispose(policy, prefix, prefix_count);
    ft_rev_elements_dispose(policy, suffix, suffix_count);
    ft_rev_rep_release(policy, middle);
    return status;
}

static ft_status ft_rev_element_set_leaf(
    const ft_tree_policy* policy,
    const ft_rev_element* element,
    size_t index,
    const void* value,
    ft_rev_element* result)
{
    if (index >= element->leaf_count) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    if (element->kind == FT_REV_ELEMENT_LEAF) {
        return ft_rev_element_init_leaf(policy, value, result);
    }

    ft_rev_element children[3];
    size_t child_count = 0;
    ft_status status = ft_rev_node_logical_children(policy, element->as.node, children, &child_count);
    if (status != FT_STATUS_OK) {
        return status;
    }

    for (size_t child_index = 0; child_index != child_count; ++child_index) {
        if (index < children[child_index].leaf_count) {
            ft_rev_element replacement;
            status = ft_rev_element_set_leaf(policy, &children[child_index], index, value, &replacement);
            if (status == FT_STATUS_OK) {
                ft_rev_element_dispose(policy, &children[child_index]);
                children[child_index] = replacement;
                status = ft_rev_element_init_node(policy, children, child_count, result);
            }

            ft_rev_elements_dispose(policy, children, child_count);
            return status;
        }

        index -= children[child_index].leaf_count;
    }

    ft_rev_elements_dispose(policy, children, child_count);
    return FT_STATUS_OUT_OF_RANGE;
}

static ft_status ft_rev_replace_in_digit(
    const ft_tree_policy* policy,
    ft_rev_element* elements,
    size_t count,
    size_t index,
    const void* value)
{
    for (size_t element_index = 0; element_index != count; ++element_index) {
        if (index < elements[element_index].leaf_count) {
            ft_rev_element replacement;
            ft_status status = ft_rev_element_set_leaf(policy, &elements[element_index], index, value, &replacement);
            if (status != FT_STATUS_OK) {
                return status;
            }

            ft_rev_element_dispose(policy, &elements[element_index]);
            elements[element_index] = replacement;
            return FT_STATUS_OK;
        }

        index -= elements[element_index].leaf_count;
    }

    return FT_STATUS_OUT_OF_RANGE;
}

static ft_status ft_rev_rep_set_leaf(
    const ft_tree_policy* policy,
    const ft_reversible_deque_rep* rep,
    size_t index,
    const void* value,
    ft_reversible_deque_rep** result)
{
    if (index >= rep->leaf_count) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    if (rep->kind == FT_REV_REP_SINGLE) {
        ft_rev_element replacement;
        ft_status status = ft_rev_element_set_leaf(policy, &rep->as.single, index, value, &replacement);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_rev_rep_create_single(policy, &replacement, result);
        ft_rev_element_dispose(policy, &replacement);
        return status;
    }

    ft_rev_element prefix[3];
    ft_rev_element suffix[3];
    size_t prefix_count = 0;
    size_t suffix_count = 0;
    ft_reversible_deque_rep* middle = NULL;
    ft_status status = ft_rev_rep_logical_parts(policy, rep, prefix, &prefix_count, &middle, suffix, &suffix_count);
    if (status != FT_STATUS_OK) {
        return status;
    }

    size_t prefix_size = 0;
    (void)ft_rev_count_elements(prefix, prefix_count, &prefix_size);
    if (index < prefix_size) {
        status = ft_rev_replace_in_digit(policy, prefix, prefix_count, index, value);
        if (status == FT_STATUS_OK) {
            status = ft_rev_rep_create_deep(policy, prefix, prefix_count, middle, suffix, suffix_count, result);
        }

        goto done;
    }

    index -= prefix_size;
    if (index < middle->leaf_count) {
        ft_reversible_deque_rep* next_middle = NULL;
        status = ft_rev_rep_set_leaf(policy, middle, index, value, &next_middle);
        if (status == FT_STATUS_OK) {
            status = ft_rev_rep_create_deep(policy, prefix, prefix_count, next_middle, suffix, suffix_count, result);
            ft_rev_rep_release(policy, next_middle);
        }

        goto done;
    }

    index -= middle->leaf_count;
    status = ft_rev_replace_in_digit(policy, suffix, suffix_count, index, value);
    if (status == FT_STATUS_OK) {
        status = ft_rev_rep_create_deep(policy, prefix, prefix_count, middle, suffix, suffix_count, result);
    }

done:
    ft_rev_elements_dispose(policy, prefix, prefix_count);
    ft_rev_elements_dispose(policy, suffix, suffix_count);
    ft_rev_rep_release(policy, middle);
    return status;
}

static ft_status ft_rev_rep_cons(
    const ft_tree_policy* policy,
    const ft_reversible_deque_rep* rep,
    const ft_rev_element* value,
    ft_reversible_deque_rep** result)
{
    if (rep->kind == FT_REV_REP_EMPTY) {
        return ft_rev_rep_create_single(policy, value, result);
    }

    if (rep->kind == FT_REV_REP_SINGLE) {
        ft_reversible_deque_rep* empty = NULL;
        ft_status status = ft_rev_rep_create_empty(&empty);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_rev_rep_create_deep(policy, value, 1, empty, &rep->as.single, 1, result);
        ft_rev_rep_release(policy, empty);
        return status;
    }

    ft_rev_element prefix[3];
    ft_rev_element suffix[3];
    size_t prefix_count = 0;
    size_t suffix_count = 0;
    ft_reversible_deque_rep* middle = NULL;
    ft_status status = ft_rev_rep_logical_parts(policy, rep, prefix, &prefix_count, &middle, suffix, &suffix_count);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (prefix_count < 3) {
        ft_rev_element next_prefix[3];
        next_prefix[0] = *value;
        for (size_t index = 0; index != prefix_count; ++index) {
            next_prefix[index + 1] = prefix[index];
        }

        status = ft_rev_rep_create_deep(policy, next_prefix, prefix_count + 1, middle, suffix, suffix_count, result);
        goto done;
    }

    ft_rev_element pushed;
    status = ft_rev_element_init_node(policy, &prefix[1], 2, &pushed);
    if (status != FT_STATUS_OK) {
        goto done;
    }

    ft_reversible_deque_rep* next_middle = NULL;
    status = ft_rev_rep_cons(policy, middle, &pushed, &next_middle);
    ft_rev_element_dispose(policy, &pushed);
    if (status != FT_STATUS_OK) {
        goto done;
    }

    ft_rev_element next_prefix[2];
    next_prefix[0] = *value;
    next_prefix[1] = prefix[0];
    status = ft_rev_rep_create_deep(policy, next_prefix, 2, next_middle, suffix, suffix_count, result);
    ft_rev_rep_release(policy, next_middle);

done:
    ft_rev_elements_dispose(policy, prefix, prefix_count);
    ft_rev_elements_dispose(policy, suffix, suffix_count);
    ft_rev_rep_release(policy, middle);
    return status;
}

static ft_status ft_rev_rep_snoc(
    const ft_tree_policy* policy,
    const ft_reversible_deque_rep* rep,
    const ft_rev_element* value,
    ft_reversible_deque_rep** result)
{
    if (rep->kind == FT_REV_REP_EMPTY) {
        return ft_rev_rep_create_single(policy, value, result);
    }

    if (rep->kind == FT_REV_REP_SINGLE) {
        ft_reversible_deque_rep* empty = NULL;
        ft_status status = ft_rev_rep_create_empty(&empty);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_rev_rep_create_deep(policy, &rep->as.single, 1, empty, value, 1, result);
        ft_rev_rep_release(policy, empty);
        return status;
    }

    ft_rev_element prefix[3];
    ft_rev_element suffix[3];
    size_t prefix_count = 0;
    size_t suffix_count = 0;
    ft_reversible_deque_rep* middle = NULL;
    ft_status status = ft_rev_rep_logical_parts(policy, rep, prefix, &prefix_count, &middle, suffix, &suffix_count);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (suffix_count < 3) {
        ft_rev_element next_suffix[3];
        for (size_t index = 0; index != suffix_count; ++index) {
            next_suffix[index] = suffix[index];
        }

        next_suffix[suffix_count] = *value;
        status = ft_rev_rep_create_deep(policy, prefix, prefix_count, middle, next_suffix, suffix_count + 1, result);
        goto done;
    }

    ft_rev_element pushed;
    status = ft_rev_element_init_node(policy, suffix, 2, &pushed);
    if (status != FT_STATUS_OK) {
        goto done;
    }

    ft_reversible_deque_rep* next_middle = NULL;
    status = ft_rev_rep_snoc(policy, middle, &pushed, &next_middle);
    ft_rev_element_dispose(policy, &pushed);
    if (status != FT_STATUS_OK) {
        goto done;
    }

    ft_rev_element next_suffix[2];
    next_suffix[0] = suffix[2];
    next_suffix[1] = *value;
    status = ft_rev_rep_create_deep(policy, prefix, prefix_count, next_middle, next_suffix, 2, result);
    ft_rev_rep_release(policy, next_middle);

done:
    ft_rev_elements_dispose(policy, prefix, prefix_count);
    ft_rev_elements_dispose(policy, suffix, suffix_count);
    ft_rev_rep_release(policy, middle);
    return status;
}

static ft_status ft_rev_rep_view_left(
    const ft_tree_policy* policy,
    const ft_reversible_deque_rep* rep,
    ft_rev_element* value,
    ft_reversible_deque_rep** rest)
{
    if (rep->kind == FT_REV_REP_EMPTY) {
        return FT_STATUS_EMPTY;
    }

    if (rep->kind == FT_REV_REP_SINGLE) {
        ft_status status = ft_rev_element_clone(policy, &rep->as.single, value);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_rev_rep_create_empty(rest);
        if (status != FT_STATUS_OK) {
            ft_rev_element_dispose(policy, value);
        }

        return status;
    }

    ft_rev_element prefix[3];
    ft_rev_element suffix[3];
    size_t prefix_count = 0;
    size_t suffix_count = 0;
    ft_reversible_deque_rep* middle = NULL;
    ft_status status = ft_rev_rep_logical_parts(policy, rep, prefix, &prefix_count, &middle, suffix, &suffix_count);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_rev_element_clone(policy, &prefix[0], value);
    if (status == FT_STATUS_OK) {
        if (prefix_count > 1) {
            status = ft_rev_rep_create_deep(policy, &prefix[1], prefix_count - 1, middle, suffix, suffix_count, rest);
        } else {
            status = ft_rev_deep_left(policy, NULL, 0, middle, suffix, suffix_count, rest);
        }

        if (status != FT_STATUS_OK) {
            ft_rev_element_dispose(policy, value);
        }
    }

    ft_rev_elements_dispose(policy, prefix, prefix_count);
    ft_rev_elements_dispose(policy, suffix, suffix_count);
    ft_rev_rep_release(policy, middle);
    return status;
}

static ft_status ft_rev_rep_view_right(
    const ft_tree_policy* policy,
    const ft_reversible_deque_rep* rep,
    ft_reversible_deque_rep** rest,
    ft_rev_element* value)
{
    if (rep->kind == FT_REV_REP_EMPTY) {
        return FT_STATUS_EMPTY;
    }

    if (rep->kind == FT_REV_REP_SINGLE) {
        ft_status status = ft_rev_element_clone(policy, &rep->as.single, value);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_rev_rep_create_empty(rest);
        if (status != FT_STATUS_OK) {
            ft_rev_element_dispose(policy, value);
        }

        return status;
    }

    ft_rev_element prefix[3];
    ft_rev_element suffix[3];
    size_t prefix_count = 0;
    size_t suffix_count = 0;
    ft_reversible_deque_rep* middle = NULL;
    ft_status status = ft_rev_rep_logical_parts(policy, rep, prefix, &prefix_count, &middle, suffix, &suffix_count);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_rev_element_clone(policy, &suffix[suffix_count - 1], value);
    if (status == FT_STATUS_OK) {
        if (suffix_count > 1) {
            status = ft_rev_rep_create_deep(policy, prefix, prefix_count, middle, suffix, suffix_count - 1, rest);
        } else {
            status = ft_rev_deep_right(policy, prefix, prefix_count, middle, NULL, 0, rest);
        }

        if (status != FT_STATUS_OK) {
            ft_rev_element_dispose(policy, value);
        }
    }

    ft_rev_elements_dispose(policy, prefix, prefix_count);
    ft_rev_elements_dispose(policy, suffix, suffix_count);
    ft_rev_rep_release(policy, middle);
    return status;
}

static ft_status ft_rev_rep_append_all(
    const ft_tree_policy* policy,
    const ft_reversible_deque_rep* left,
    const ft_rev_element* elements,
    size_t count,
    ft_reversible_deque_rep** result)
{
    ft_rev_rep_retain((ft_reversible_deque_rep*)left);
    ft_reversible_deque_rep* current = (ft_reversible_deque_rep*)left;
    for (size_t index = 0; index != count; ++index) {
        ft_reversible_deque_rep* next = NULL;
        ft_status status = ft_rev_rep_snoc(policy, current, &elements[index], &next);
        ft_rev_rep_release(policy, current);
        if (status != FT_STATUS_OK) {
            return status;
        }

        current = next;
    }

    *result = current;
    return FT_STATUS_OK;
}

static ft_status ft_rev_rep_prepend_all(
    const ft_tree_policy* policy,
    const ft_rev_element* elements,
    size_t count,
    const ft_reversible_deque_rep* right,
    ft_reversible_deque_rep** result)
{
    ft_rev_rep_retain((ft_reversible_deque_rep*)right);
    ft_reversible_deque_rep* current = (ft_reversible_deque_rep*)right;
    for (size_t offset = count; offset != 0; --offset) {
        ft_reversible_deque_rep* next = NULL;
        ft_status status = ft_rev_rep_cons(policy, current, &elements[offset - 1], &next);
        ft_rev_rep_release(policy, current);
        if (status != FT_STATUS_OK) {
            return status;
        }

        current = next;
    }

    *result = current;
    return FT_STATUS_OK;
}

static ft_status ft_rev_nodes(
    const ft_tree_policy* policy,
    const ft_rev_element* elements,
    size_t count,
    ft_rev_element* nodes,
    size_t* node_count)
{
    if (count < 2) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    *node_count = 0;
    size_t index = 0;
    ft_status status = FT_STATUS_OK;
    while (count - index > 4) {
        status = ft_rev_element_init_node(policy, &elements[index], 3, &nodes[*node_count]);
        if (status != FT_STATUS_OK) {
            goto fail;
        }

        ++*node_count;
        index += 3;
    }

    switch (count - index) {
    case 2:
        status = ft_rev_element_init_node(policy, &elements[index], 2, &nodes[*node_count]);
        break;
    case 3:
        status = ft_rev_element_init_node(policy, &elements[index], 3, &nodes[*node_count]);
        break;
    case 4:
        status = ft_rev_element_init_node(policy, &elements[index], 2, &nodes[*node_count]);
        if (status != FT_STATUS_OK) {
            goto fail;
        }

        ++*node_count;
        status = ft_rev_element_init_node(policy, &elements[index + 2], 2, &nodes[*node_count]);
        break;
    default:
        status = FT_STATUS_INVALID_ARGUMENT;
        goto fail;
    }

    if (status != FT_STATUS_OK) {
        goto fail;
    }

    ++*node_count;
    return FT_STATUS_OK;

fail:
    ft_rev_elements_dispose(policy, nodes, *node_count);
    *node_count = 0;
    return status;
}

static ft_status ft_rev_rep_concat_with_middle(
    const ft_tree_policy* policy,
    const ft_reversible_deque_rep* left,
    const ft_rev_element* middle,
    size_t middle_count,
    const ft_reversible_deque_rep* right,
    ft_reversible_deque_rep** result)
{
    if (left->kind == FT_REV_REP_EMPTY) {
        return ft_rev_rep_prepend_all(policy, middle, middle_count, right, result);
    }

    if (right->kind == FT_REV_REP_EMPTY) {
        return ft_rev_rep_append_all(policy, left, middle, middle_count, result);
    }

    if (left->kind == FT_REV_REP_SINGLE) {
        ft_reversible_deque_rep* prefixed = NULL;
        ft_status status = ft_rev_rep_prepend_all(policy, middle, middle_count, right, &prefixed);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_rev_rep_cons(policy, prefixed, &left->as.single, result);
        ft_rev_rep_release(policy, prefixed);
        return status;
    }

    if (right->kind == FT_REV_REP_SINGLE) {
        ft_reversible_deque_rep* appended = NULL;
        ft_status status = ft_rev_rep_append_all(policy, left, middle, middle_count, &appended);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_rev_rep_snoc(policy, appended, &right->as.single, result);
        ft_rev_rep_release(policy, appended);
        return status;
    }

    ft_rev_element left_prefix[3];
    ft_rev_element left_suffix[3];
    ft_rev_element right_prefix[3];
    ft_rev_element right_suffix[3];
    size_t left_prefix_count = 0;
    size_t left_suffix_count = 0;
    size_t right_prefix_count = 0;
    size_t right_suffix_count = 0;
    ft_reversible_deque_rep* left_middle = NULL;
    ft_reversible_deque_rep* right_middle = NULL;

    ft_status status = ft_rev_rep_logical_parts(
        policy,
        left,
        left_prefix,
        &left_prefix_count,
        &left_middle,
        left_suffix,
        &left_suffix_count);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_rev_rep_logical_parts(
        policy,
        right,
        right_prefix,
        &right_prefix_count,
        &right_middle,
        right_suffix,
        &right_suffix_count);
    if (status != FT_STATUS_OK) {
        ft_rev_elements_dispose(policy, left_prefix, left_prefix_count);
        ft_rev_elements_dispose(policy, left_suffix, left_suffix_count);
        ft_rev_rep_release(policy, left_middle);
        return status;
    }

    ft_rev_element combined[16];
    size_t combined_count = 0;
    for (size_t index = 0; index != left_suffix_count; ++index) {
        combined[combined_count++] = left_suffix[index];
    }

    for (size_t index = 0; index != middle_count; ++index) {
        combined[combined_count++] = middle[index];
    }

    for (size_t index = 0; index != right_prefix_count; ++index) {
        combined[combined_count++] = right_prefix[index];
    }

    ft_rev_element bridge[8];
    size_t bridge_count = 0;
    status = ft_rev_nodes(policy, combined, combined_count, bridge, &bridge_count);
    if (status == FT_STATUS_OK) {
        ft_reversible_deque_rep* middle_result = NULL;
        status = ft_rev_rep_concat_with_middle(
            policy,
            left_middle,
            bridge,
            bridge_count,
            right_middle,
            &middle_result);
        if (status == FT_STATUS_OK) {
            status = ft_rev_rep_create_deep(
                policy,
                left_prefix,
                left_prefix_count,
                middle_result,
                right_suffix,
                right_suffix_count,
                result);
            ft_rev_rep_release(policy, middle_result);
        }

        ft_rev_elements_dispose(policy, bridge, bridge_count);
    }

    ft_rev_elements_dispose(policy, left_prefix, left_prefix_count);
    ft_rev_elements_dispose(policy, left_suffix, left_suffix_count);
    ft_rev_elements_dispose(policy, right_prefix, right_prefix_count);
    ft_rev_elements_dispose(policy, right_suffix, right_suffix_count);
    ft_rev_rep_release(policy, left_middle);
    ft_rev_rep_release(policy, right_middle);
    return status;
}

static ft_status ft_rev_split_digit(
    const ft_tree_policy* policy,
    const ft_rev_element* elements,
    size_t count,
    size_t index,
    ft_rev_element* before,
    size_t* before_count,
    ft_rev_element* hit,
    size_t* index_in_hit,
    ft_rev_element* after,
    size_t* after_count)
{
    *before_count = 0;
    *after_count = 0;
    for (size_t element_index = 0; element_index != count; ++element_index) {
        if (index < elements[element_index].leaf_count) {
            ft_status status = ft_rev_element_clone(policy, &elements[element_index], hit);
            if (status != FT_STATUS_OK) {
                ft_rev_elements_dispose(policy, before, *before_count);
                *before_count = 0;
                return status;
            }

            *index_in_hit = index;
            for (size_t rest = element_index + 1; rest != count; ++rest) {
                status = ft_rev_element_clone(policy, &elements[rest], &after[*after_count]);
                if (status != FT_STATUS_OK) {
                    ft_rev_element_dispose(policy, hit);
                    ft_rev_elements_dispose(policy, before, *before_count);
                    ft_rev_elements_dispose(policy, after, *after_count);
                    *before_count = 0;
                    *after_count = 0;
                    return status;
                }

                ++*after_count;
            }

            return FT_STATUS_OK;
        }

        ft_status status = ft_rev_element_clone(policy, &elements[element_index], &before[*before_count]);
        if (status != FT_STATUS_OK) {
            ft_rev_elements_dispose(policy, before, *before_count);
            *before_count = 0;
            return status;
        }

        ++*before_count;
        index -= elements[element_index].leaf_count;
    }

    ft_rev_elements_dispose(policy, before, *before_count);
    *before_count = 0;
    return FT_STATUS_OUT_OF_RANGE;
}

static void ft_rev_split_result_dispose(const ft_tree_policy* policy, ft_rev_split_result* split)
{
    ft_rev_rep_release(policy, split->left);
    ft_rev_element_dispose(policy, &split->hit);
    ft_rev_rep_release(policy, split->right);
    (void)memset(split, 0, sizeof(*split));
}

static ft_status ft_rev_rep_split_tree(
    const ft_tree_policy* policy,
    const ft_reversible_deque_rep* rep,
    size_t index,
    ft_rev_split_result* result)
{
    (void)memset(result, 0, sizeof(*result));
    if (index >= rep->leaf_count) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    if (rep->kind == FT_REV_REP_SINGLE) {
        ft_status status = ft_rev_rep_create_empty(&result->left);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_rev_element_clone(policy, &rep->as.single, &result->hit);
        if (status != FT_STATUS_OK) {
            ft_rev_rep_release(policy, result->left);
            return status;
        }

        status = ft_rev_rep_create_empty(&result->right);
        if (status != FT_STATUS_OK) {
            ft_rev_rep_release(policy, result->left);
            ft_rev_element_dispose(policy, &result->hit);
            return status;
        }

        result->index_in_hit = index;
        return FT_STATUS_OK;
    }

    ft_rev_element prefix[3];
    ft_rev_element suffix[3];
    size_t prefix_count = 0;
    size_t suffix_count = 0;
    ft_reversible_deque_rep* middle = NULL;
    ft_status status = ft_rev_rep_logical_parts(policy, rep, prefix, &prefix_count, &middle, suffix, &suffix_count);
    if (status != FT_STATUS_OK) {
        return status;
    }

    size_t prefix_size = 0;
    (void)ft_rev_count_elements(prefix, prefix_count, &prefix_size);
    if (index < prefix_size) {
        ft_rev_element before[3];
        ft_rev_element after[3];
        size_t before_count = 0;
        size_t after_count = 0;
        status = ft_rev_split_digit(
            policy,
            prefix,
            prefix_count,
            index,
            before,
            &before_count,
            &result->hit,
            &result->index_in_hit,
            after,
            &after_count);
        if (status == FT_STATUS_OK) {
            status = ft_rev_rep_from_digit(policy, before, before_count, &result->left);
        }

        if (status == FT_STATUS_OK) {
            status = ft_rev_deep_left(policy, after, after_count, middle, suffix, suffix_count, &result->right);
        }

        ft_rev_elements_dispose(policy, before, before_count);
        ft_rev_elements_dispose(policy, after, after_count);
        goto done;
    }

    index -= prefix_size;
    if (index < middle->leaf_count) {
        ft_rev_split_result middle_split;
        status = ft_rev_rep_split_tree(policy, middle, index, &middle_split);
        if (status == FT_STATUS_OK) {
            if (middle_split.hit.kind != FT_REV_ELEMENT_NODE) {
                ft_rev_split_result_dispose(policy, &middle_split);
                status = FT_STATUS_INVALID_ARGUMENT;
                goto done;
            }

            ft_rev_element children[3];
            size_t child_count = 0;
            status = ft_rev_node_logical_children(policy, middle_split.hit.as.node, children, &child_count);
            if (status == FT_STATUS_OK) {
                ft_rev_element before[3];
                ft_rev_element after[3];
                size_t before_count = 0;
                size_t after_count = 0;
                status = ft_rev_split_digit(
                    policy,
                    children,
                    child_count,
                    middle_split.index_in_hit,
                    before,
                    &before_count,
                    &result->hit,
                    &result->index_in_hit,
                    after,
                    &after_count);
                if (status == FT_STATUS_OK) {
                    status = ft_rev_deep_right(
                        policy,
                        prefix,
                        prefix_count,
                        middle_split.left,
                        before,
                        before_count,
                        &result->left);
                }

                if (status == FT_STATUS_OK) {
                    status = ft_rev_deep_left(
                        policy,
                        after,
                        after_count,
                        middle_split.right,
                        suffix,
                        suffix_count,
                        &result->right);
                }

                ft_rev_elements_dispose(policy, before, before_count);
                ft_rev_elements_dispose(policy, after, after_count);
                ft_rev_elements_dispose(policy, children, child_count);
            }

            ft_rev_split_result_dispose(policy, &middle_split);
        }

        goto done;
    }

    index -= middle->leaf_count;
    ft_rev_element before[3];
    ft_rev_element after[3];
    size_t before_count = 0;
    size_t after_count = 0;
    status = ft_rev_split_digit(
        policy,
        suffix,
        suffix_count,
        index,
        before,
        &before_count,
        &result->hit,
        &result->index_in_hit,
        after,
        &after_count);
    if (status == FT_STATUS_OK) {
        status = ft_rev_deep_right(policy, prefix, prefix_count, middle, before, before_count, &result->left);
    }

    if (status == FT_STATUS_OK) {
        status = ft_rev_rep_from_digit(policy, after, after_count, &result->right);
    }

    ft_rev_elements_dispose(policy, before, before_count);
    ft_rev_elements_dispose(policy, after, after_count);

done:
    ft_rev_elements_dispose(policy, prefix, prefix_count);
    ft_rev_elements_dispose(policy, suffix, suffix_count);
    ft_rev_rep_release(policy, middle);
    if (status != FT_STATUS_OK) {
        ft_rev_split_result_dispose(policy, result);
    }

    return status;
}

static ft_status ft_rev_rep_split_at(
    const ft_tree_policy* policy,
    const ft_reversible_deque_rep* rep,
    size_t index,
    ft_reversible_deque_rep** left,
    ft_reversible_deque_rep** right)
{
    if (index > rep->leaf_count) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    if (index == 0) {
        ft_status status = ft_rev_rep_create_empty(left);
        if (status != FT_STATUS_OK) {
            return status;
        }

        ft_rev_rep_retain((ft_reversible_deque_rep*)rep);
        *right = (ft_reversible_deque_rep*)rep;
        return FT_STATUS_OK;
    }

    if (index == rep->leaf_count) {
        ft_status status = ft_rev_rep_create_empty(right);
        if (status != FT_STATUS_OK) {
            return status;
        }

        ft_rev_rep_retain((ft_reversible_deque_rep*)rep);
        *left = (ft_reversible_deque_rep*)rep;
        return FT_STATUS_OK;
    }

    ft_rev_split_result split;
    ft_status status = ft_rev_rep_split_tree(policy, rep, index, &split);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_reversible_deque_rep* right_with_hit = NULL;
    status = ft_rev_rep_cons(policy, split.right, &split.hit, &right_with_hit);
    if (status == FT_STATUS_OK) {
        *left = split.left;
        *right = right_with_hit;
        split.left = NULL;
    }

    ft_rev_split_result_dispose(policy, &split);
    return status;
}

static ft_status ft_rev_element_visit(const ft_tree_policy* policy, const ft_rev_element* element, ft_visit_fn visitor, void* context)
{
    if (element->kind == FT_REV_ELEMENT_LEAF) {
        visitor(element->as.value, context);
        return FT_STATUS_OK;
    }

    for (size_t index = 0; index != element->as.node->child_count; ++index) {
        ft_rev_element child;
        ft_status status = ft_rev_node_logical_child(policy, element->as.node, index, &child);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_rev_element_visit(policy, &child, visitor, context);
        ft_rev_element_dispose(policy, &child);
        if (status != FT_STATUS_OK) {
            return status;
        }
    }

    return FT_STATUS_OK;
}

static ft_status ft_rev_rep_visit(
    const ft_tree_policy* policy,
    const ft_reversible_deque_rep* rep,
    ft_visit_fn visitor,
    void* context)
{
    if (rep->kind == FT_REV_REP_EMPTY) {
        return FT_STATUS_OK;
    }

    if (rep->kind == FT_REV_REP_SINGLE) {
        return ft_rev_element_visit(policy, &rep->as.single, visitor, context);
    }

    ft_rev_element prefix[3];
    ft_rev_element suffix[3];
    size_t prefix_count = 0;
    size_t suffix_count = 0;
    ft_reversible_deque_rep* middle = NULL;
    ft_status status = ft_rev_rep_logical_parts(policy, rep, prefix, &prefix_count, &middle, suffix, &suffix_count);
    if (status != FT_STATUS_OK) {
        return status;
    }

    for (size_t index = 0; status == FT_STATUS_OK && index != prefix_count; ++index) {
        status = ft_rev_element_visit(policy, &prefix[index], visitor, context);
    }

    if (status == FT_STATUS_OK) {
        status = ft_rev_rep_visit(policy, middle, visitor, context);
    }

    for (size_t index = 0; status == FT_STATUS_OK && index != suffix_count; ++index) {
        status = ft_rev_element_visit(policy, &suffix[index], visitor, context);
    }

    ft_rev_elements_dispose(policy, prefix, prefix_count);
    ft_rev_elements_dispose(policy, suffix, suffix_count);
    ft_rev_rep_release(policy, middle);
    return status;
}

ft_status ft_reversible_deque_init(ft_reversible_deque* deque, const ft_tree_policy* policy)
{
    /* The reversible deque never consults the measure, so unlike ft_tree_init only the
     * value description is validated; a zero-size value would silently produce a deque
     * whose reads copy nothing. */
    if (deque == NULL || policy == NULL || policy->value.size == 0) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_reversible_deque_rep* rep = NULL;
    ft_status status = ft_rev_rep_create_empty(&rep);
    if (status != FT_STATUS_OK) {
        return status;
    }

    deque->policy = policy;
    deque->rep = rep;
    return FT_STATUS_OK;
}

ft_status ft_reversible_deque_copy(const ft_reversible_deque* source, ft_reversible_deque* destination)
{
    if (!ft_reversible_deque_is_valid(source) || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    destination->policy = source->policy;
    destination->rep = source->rep;
    ft_rev_rep_retain(destination->rep);
    return FT_STATUS_OK;
}

void ft_reversible_deque_dispose(ft_reversible_deque* deque)
{
    if (deque == NULL) {
        return;
    }

    if (deque->policy != NULL && deque->rep != NULL) {
        ft_rev_rep_release(deque->policy, deque->rep);
    }

    deque->policy = NULL;
    deque->rep = NULL;
}

bool ft_reversible_deque_empty(const ft_reversible_deque* deque)
{
    return !ft_reversible_deque_is_valid(deque) || deque->rep->leaf_count == 0;
}

size_t ft_reversible_deque_size(const ft_reversible_deque* deque)
{
    return ft_reversible_deque_is_valid(deque) ? deque->rep->leaf_count : 0;
}

ft_status ft_reversible_deque_reverse(const ft_reversible_deque* deque, ft_reversible_deque* result)
{
    if (!ft_reversible_deque_is_valid(deque) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_reversible_deque_rep* rep = NULL;
    ft_status status = ft_rev_rep_mirror(deque->policy, deque->rep, &rep);
    if (status != FT_STATUS_OK) {
        return status;
    }

    result->policy = deque->policy;
    result->rep = rep;
    return FT_STATUS_OK;
}

ft_status ft_reversible_deque_at(const ft_reversible_deque* deque, size_t index, void* destination)
{
    if (!ft_reversible_deque_is_valid(deque) || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    const size_t size = deque->rep->leaf_count;
    if (index >= size) {
        return size == 0 ? FT_STATUS_EMPTY : FT_STATUS_OUT_OF_RANGE;
    }

    return ft_rev_rep_get_leaf(deque->policy, deque->rep, index, destination);
}

ft_status ft_reversible_deque_front(const ft_reversible_deque* deque, void* destination)
{
    if (!ft_reversible_deque_is_valid(deque) || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    return ft_rev_rep_copy_first(deque->policy, deque->rep, destination);
}

ft_status ft_reversible_deque_back(const ft_reversible_deque* deque, void* destination)
{
    if (!ft_reversible_deque_is_valid(deque) || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    return ft_rev_rep_copy_last(deque->policy, deque->rep, destination);
}

ft_status ft_reversible_deque_push_front(const ft_reversible_deque* deque, const void* value, ft_reversible_deque* result)
{
    if (!ft_reversible_deque_is_valid(deque) || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_rev_element element;
    ft_status status = ft_rev_element_init_leaf(deque->policy, value, &element);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_reversible_deque_rep* rep = NULL;
    status = ft_rev_rep_cons(deque->policy, deque->rep, &element, &rep);
    ft_rev_element_dispose(deque->policy, &element);
    if (status != FT_STATUS_OK) {
        return status;
    }

    result->policy = deque->policy;
    result->rep = rep;
    return FT_STATUS_OK;
}

ft_status ft_reversible_deque_push_back(const ft_reversible_deque* deque, const void* value, ft_reversible_deque* result)
{
    if (!ft_reversible_deque_is_valid(deque) || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_rev_element element;
    ft_status status = ft_rev_element_init_leaf(deque->policy, value, &element);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_reversible_deque_rep* rep = NULL;
    status = ft_rev_rep_snoc(deque->policy, deque->rep, &element, &rep);
    ft_rev_element_dispose(deque->policy, &element);
    if (status != FT_STATUS_OK) {
        return status;
    }

    result->policy = deque->policy;
    result->rep = rep;
    return FT_STATUS_OK;
}

ft_status ft_reversible_deque_pop_front(const ft_reversible_deque* deque, void* value, ft_reversible_deque* rest)
{
    if (!ft_reversible_deque_is_valid(deque) || rest == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_rev_element element;
    ft_reversible_deque_rep* rep = NULL;
    ft_status status = ft_rev_rep_view_left(deque->policy, deque->rep, &element, &rep);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (value != NULL) {
        status = ft_rev_element_copy_first(deque->policy, &element, value);
    }

    ft_rev_element_dispose(deque->policy, &element);
    if (status != FT_STATUS_OK) {
        ft_rev_rep_release(deque->policy, rep);
        return status;
    }

    rest->policy = deque->policy;
    rest->rep = rep;
    return FT_STATUS_OK;
}

ft_status ft_reversible_deque_pop_back(const ft_reversible_deque* deque, void* value, ft_reversible_deque* rest)
{
    if (!ft_reversible_deque_is_valid(deque) || rest == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_reversible_deque_rep* rep = NULL;
    ft_rev_element element;
    ft_status status = ft_rev_rep_view_right(deque->policy, deque->rep, &rep, &element);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (value != NULL) {
        status = ft_rev_element_copy_last(deque->policy, &element, value);
    }

    ft_rev_element_dispose(deque->policy, &element);
    if (status != FT_STATUS_OK) {
        ft_rev_rep_release(deque->policy, rep);
        return status;
    }

    rest->policy = deque->policy;
    rest->rep = rep;
    return FT_STATUS_OK;
}

ft_status ft_reversible_deque_concat(
    const ft_reversible_deque* left,
    const ft_reversible_deque* right,
    ft_reversible_deque* result)
{
    if (!ft_reversible_deque_is_valid(left) ||
        !ft_reversible_deque_is_valid(right) ||
        result == NULL ||
        left->policy != right->policy) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    size_t combined = 0;
    if (ft_add_overflows(left->rep->leaf_count, right->rep->leaf_count, &combined)) {
        return FT_STATUS_OVERFLOW;
    }

    (void)combined;
    ft_reversible_deque_rep* rep = NULL;
    ft_status status = ft_rev_rep_concat_with_middle(left->policy, left->rep, NULL, 0, right->rep, &rep);
    if (status != FT_STATUS_OK) {
        return status;
    }

    result->policy = left->policy;
    result->rep = rep;
    return FT_STATUS_OK;
}

ft_status ft_reversible_deque_split_at(
    const ft_reversible_deque* deque,
    size_t index,
    ft_reversible_deque_split_result* result)
{
    if (!ft_reversible_deque_is_valid(deque) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_reversible_deque_rep* left = NULL;
    ft_reversible_deque_rep* right = NULL;
    ft_status status = ft_rev_rep_split_at(deque->policy, deque->rep, index, &left, &right);
    if (status != FT_STATUS_OK) {
        return status;
    }

    result->left.policy = deque->policy;
    result->left.rep = left;
    result->right.policy = deque->policy;
    result->right.rep = right;
    return FT_STATUS_OK;
}

ft_status ft_reversible_deque_set_at(
    const ft_reversible_deque* deque,
    size_t index,
    const void* value,
    ft_reversible_deque* result)
{
    if (!ft_reversible_deque_is_valid(deque) || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    if (index >= deque->rep->leaf_count) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    ft_reversible_deque_rep* rep = NULL;
    ft_status status = ft_rev_rep_set_leaf(deque->policy, deque->rep, index, value, &rep);
    if (status != FT_STATUS_OK) {
        return status;
    }

    result->policy = deque->policy;
    result->rep = rep;
    return FT_STATUS_OK;
}

ft_status ft_reversible_deque_insert_at(
    const ft_reversible_deque* deque,
    size_t index,
    const void* value,
    ft_reversible_deque* result)
{
    if (!ft_reversible_deque_is_valid(deque) || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    if (index > deque->rep->leaf_count) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    ft_reversible_deque_split_result split;
    ft_status status = ft_reversible_deque_split_at(deque, index, &split);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_reversible_deque with_item;
    status = ft_reversible_deque_push_back(&split.left, value, &with_item);
    if (status == FT_STATUS_OK) {
        status = ft_reversible_deque_concat(&with_item, &split.right, result);
        ft_reversible_deque_dispose(&with_item);
    }

    ft_reversible_deque_dispose(&split.left);
    ft_reversible_deque_dispose(&split.right);
    return status;
}

ft_status ft_reversible_deque_remove_at(const ft_reversible_deque* deque, size_t index, ft_reversible_deque* result)
{
    if (!ft_reversible_deque_is_valid(deque) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    if (index >= deque->rep->leaf_count) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    ft_reversible_deque_split_result split;
    ft_status status = ft_reversible_deque_split_at(deque, index, &split);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_reversible_deque tail;
    status = ft_reversible_deque_pop_front(&split.right, NULL, &tail);
    if (status == FT_STATUS_OK) {
        status = ft_reversible_deque_concat(&split.left, &tail, result);
        ft_reversible_deque_dispose(&tail);
    }

    ft_reversible_deque_dispose(&split.left);
    ft_reversible_deque_dispose(&split.right);
    return status;
}

ft_status ft_reversible_deque_visit(const ft_reversible_deque* deque, ft_visit_fn visitor, void* context)
{
    if (!ft_reversible_deque_is_valid(deque) || visitor == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    return ft_rev_rep_visit(deque->policy, deque->rep, visitor, context);
}

static int ft_compare_values(const ft_sorted_multiset* set, const void* left, const void* right)
{
    return set->compare(left, right, set->compare_context);
}

typedef struct ft_sorted_bound_context {
    const ft_sorted_multiset* set;
    const void* value;
    bool upper;
} ft_sorted_bound_context;

static bool ft_sorted_bound_reached(const void* leaf, void* context)
{
    const ft_sorted_bound_context* bound = (const ft_sorted_bound_context*)context;
    const int comparison = ft_compare_values(bound->set, leaf, bound->value);
    return bound->upper ? comparison > 0 : comparison >= 0;
}

static ft_status ft_sorted_bound(
    const ft_sorted_multiset* set,
    const void* value,
    bool upper,
    size_t* index,
    const void** hit)
{
    ft_sorted_bound_context context;
    context.set = set;
    context.value = value;
    context.upper = upper;
    return ft_tree_bound(&set->tree, ft_sorted_bound_reached, &context, index, hit);
}

static ft_status ft_sorted_bounds(
    const ft_sorted_multiset* set,
    const void* value,
    size_t* lower,
    size_t* upper)
{
    const void* hit = NULL;
    ft_status status = ft_sorted_bound(set, value, false, lower, &hit);
    if (status != FT_STATUS_OK) {
        return status;
    }

    return ft_sorted_bound(set, value, true, upper, &hit);
}

ft_status ft_sorted_multiset_init(
    ft_sorted_multiset* set,
    const ft_tree_policy* policy,
    ft_compare_fn compare,
    void* compare_context)
{
    if (set == NULL || policy == NULL || compare == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_status status = ft_tree_init(&set->tree, policy);
    if (status != FT_STATUS_OK) {
        return status;
    }

    set->compare = compare;
    set->compare_context = compare_context;
    return FT_STATUS_OK;
}

ft_status ft_sorted_multiset_copy(const ft_sorted_multiset* source, ft_sorted_multiset* destination)
{
    if (source == NULL || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_status status = ft_tree_copy(&source->tree, &destination->tree);
    if (status != FT_STATUS_OK) {
        return status;
    }

    destination->compare = source->compare;
    destination->compare_context = source->compare_context;
    return FT_STATUS_OK;
}

void ft_sorted_multiset_dispose(ft_sorted_multiset* set)
{
    if (set == NULL) {
        return;
    }

    ft_tree_dispose(&set->tree);
    set->compare = NULL;
    set->compare_context = NULL;
}

size_t ft_sorted_multiset_size(const ft_sorted_multiset* set)
{
    return set == NULL ? 0 : ft_tree_size(&set->tree);
}

bool ft_sorted_multiset_empty(const ft_sorted_multiset* set)
{
    return set == NULL || ft_tree_empty(&set->tree);
}

ft_status ft_sorted_multiset_add(const ft_sorted_multiset* set, const void* value, ft_sorted_multiset* result)
{
    if (set == NULL || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    size_t lower = 0;
    size_t upper = 0;
    ft_status status = ft_sorted_bounds(set, value, &lower, &upper);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_tree_insert_at(&set->tree, upper, value, &result->tree);
    if (status != FT_STATUS_OK) {
        return status;
    }

    result->compare = set->compare;
    result->compare_context = set->compare_context;
    (void)lower;
    return FT_STATUS_OK;
}

ft_status ft_sorted_multiset_remove_one(const ft_sorted_multiset* set, const void* value, ft_sorted_multiset* result)
{
    if (set == NULL || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    size_t lower = 0;
    size_t upper = 0;
    ft_status status = ft_sorted_bounds(set, value, &lower, &upper);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (lower == upper) {
        return ft_sorted_multiset_copy(set, result);
    }

    status = ft_tree_remove_at(&set->tree, lower, &result->tree);
    if (status != FT_STATUS_OK) {
        return status;
    }

    result->compare = set->compare;
    result->compare_context = set->compare_context;
    return FT_STATUS_OK;
}

bool ft_sorted_multiset_contains(const ft_sorted_multiset* set, const void* value)
{
    if (set == NULL || value == NULL) {
        return false;
    }

    size_t lower = 0;
    size_t upper = 0;
    if (ft_sorted_bounds(set, value, &lower, &upper) != FT_STATUS_OK) {
        return false;
    }

    return lower != upper;
}

size_t ft_sorted_multiset_count_of(const ft_sorted_multiset* set, const void* value)
{
    if (set == NULL || value == NULL) {
        return 0;
    }

    size_t lower = 0;
    size_t upper = 0;
    if (ft_sorted_bounds(set, value, &lower, &upper) != FT_STATUS_OK) {
        return 0;
    }

    return upper - lower;
}

ft_status ft_sorted_multiset_at(const ft_sorted_multiset* set, size_t index, void* destination)
{
    if (set == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    return ft_tree_at(&set->tree, index, destination);
}

ft_status ft_sorted_multiset_visit(const ft_sorted_multiset* set, ft_visit_fn visitor, void* context)
{
    if (set == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    return ft_tree_visit(&set->tree, visitor, context);
}

ft_status ft_sorted_set_init(
    ft_sorted_set* set,
    const ft_tree_policy* policy,
    ft_compare_fn compare,
    void* compare_context)
{
    return ft_sorted_multiset_init(set, policy, compare, compare_context);
}

ft_status ft_sorted_set_copy(const ft_sorted_set* source, ft_sorted_set* destination)
{
    return ft_sorted_multiset_copy(source, destination);
}

void ft_sorted_set_dispose(ft_sorted_set* set)
{
    ft_sorted_multiset_dispose(set);
}

size_t ft_sorted_set_size(const ft_sorted_set* set)
{
    return ft_sorted_multiset_size(set);
}

bool ft_sorted_set_empty(const ft_sorted_set* set)
{
    return ft_sorted_multiset_empty(set);
}

ft_status ft_sorted_set_add(const ft_sorted_set* set, const void* value, ft_sorted_set* result)
{
    if (set == NULL || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    size_t lower = 0;
    size_t upper = 0;
    ft_status status = ft_sorted_bounds(set, value, &lower, &upper);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (lower != upper) {
        return ft_sorted_set_copy(set, result);
    }

    status = ft_tree_insert_at(&set->tree, lower, value, &result->tree);
    if (status != FT_STATUS_OK) {
        return status;
    }

    result->compare = set->compare;
    result->compare_context = set->compare_context;
    return FT_STATUS_OK;
}

ft_status ft_sorted_set_remove(const ft_sorted_set* set, const void* value, ft_sorted_set* result)
{
    return ft_sorted_multiset_remove_one(set, value, result);
}

bool ft_sorted_set_contains(const ft_sorted_set* set, const void* value)
{
    return ft_sorted_multiset_contains(set, value);
}

ft_status ft_sorted_set_at(const ft_sorted_set* set, size_t index, void* destination)
{
    return ft_sorted_multiset_at(set, index, destination);
}

ft_status ft_sorted_set_visit(const ft_sorted_set* set, ft_visit_fn visitor, void* context)
{
    return ft_sorted_multiset_visit(set, visitor, context);
}

typedef struct ft_sorted_map_entry {
    void* key;
    void* value;
} ft_sorted_map_entry;

struct ft_sorted_map_entry_context {
    ft_value_type key_type;
    ft_value_type value_type;
};

static void ft_sorted_map_entry_destroy_value(ft_sorted_map_entry_context* context, ft_sorted_map_entry* entry)
{
    if (entry->key != NULL) {
        ft_value_destroy(&context->key_type, entry->key);
        free(entry->key);
    }

    if (entry->value != NULL) {
        ft_value_destroy(&context->value_type, entry->value);
        free(entry->value);
    }

    entry->key = NULL;
    entry->value = NULL;
}

static void ft_sorted_map_entry_copy(void* destination, const void* source, void* context)
{
    ft_sorted_map_entry_context* entry_context = (ft_sorted_map_entry_context*)context;
    const ft_sorted_map_entry* source_entry = (const ft_sorted_map_entry*)source;
    ft_sorted_map_entry* destination_entry = (ft_sorted_map_entry*)destination;
    destination_entry->key = ft_allocate(entry_context->key_type.size);
    destination_entry->value = ft_allocate(entry_context->value_type.size);

    if (destination_entry->key == NULL || destination_entry->value == NULL) {
        abort();
    }

    ft_value_copy(&entry_context->key_type, destination_entry->key, source_entry->key);
    ft_value_copy(&entry_context->value_type, destination_entry->value, source_entry->value);
}

static void ft_sorted_map_entry_destroy(void* value, void* context)
{
    ft_sorted_map_entry_destroy_value((ft_sorted_map_entry_context*)context, (ft_sorted_map_entry*)value);
}

static ft_status ft_sorted_map_entry_init(
    const ft_sorted_map* map,
    const void* key,
    const void* value,
    ft_sorted_map_entry* entry)
{
    entry->key = ft_allocate(map->key_type.size);
    entry->value = ft_allocate(map->value_type.size);
    if (entry->key == NULL || entry->value == NULL) {
        ft_sorted_map_entry_destroy_value(map->entry_context, entry);
        return FT_STATUS_NO_MEMORY;
    }

    ft_value_copy(&map->key_type, entry->key, key);
    ft_value_copy(&map->value_type, entry->value, value);
    return FT_STATUS_OK;
}

static ft_status ft_sorted_map_configure(
    ft_sorted_map* map,
    const ft_value_type* key_type,
    const ft_value_type* value_type,
    ft_compare_fn compare_key,
    void* compare_context)
{
    map->entry_context = (ft_sorted_map_entry_context*)calloc(1, sizeof(*map->entry_context));
    if (map->entry_context == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    map->key_type = *key_type;
    map->value_type = *value_type;
    map->compare_key = compare_key;
    map->compare_context = compare_context;
    map->entry_context->key_type = *key_type;
    map->entry_context->value_type = *value_type;

    map->policy.value.size = sizeof(ft_sorted_map_entry);
    map->policy.value.copy = ft_sorted_map_entry_copy;
    map->policy.value.destroy = ft_sorted_map_entry_destroy;
    map->policy.value.context = map->entry_context;
    ft_size_measure_policy_init(&map->policy.measure);
    return FT_STATUS_OK;
}

static int ft_sorted_map_compare_key(const ft_sorted_map* map, const void* left, const void* right)
{
    return map->compare_key(left, right, map->compare_context);
}

typedef struct ft_sorted_map_bound_context {
    const ft_sorted_map* map;
    const void* key;
} ft_sorted_map_bound_context;

static bool ft_sorted_map_lower_reached(const void* leaf, void* context)
{
    const ft_sorted_map_bound_context* bound = (const ft_sorted_map_bound_context*)context;
    const ft_sorted_map_entry* entry = (const ft_sorted_map_entry*)leaf;
    return ft_sorted_map_compare_key(bound->map, entry->key, bound->key) >= 0;
}

static ft_status ft_sorted_map_bounds(
    const ft_sorted_map* map,
    const void* key,
    size_t* lower,
    size_t* upper)
{
    ft_sorted_map_bound_context context;
    context.map = map;
    context.key = key;
    const void* hit = NULL;
    ft_status status = ft_tree_bound(
        &map->tree,
        ft_sorted_map_lower_reached,
        &context,
        lower,
        &hit);
    if (status != FT_STATUS_OK) {
        return status;
    }

    *upper = *lower;
    if (hit != NULL) {
        const ft_sorted_map_entry* entry = (const ft_sorted_map_entry*)hit;
        if (ft_sorted_map_compare_key(map, entry->key, key) == 0) {
            ++*upper;
        }
    }

    return FT_STATUS_OK;
}

static ft_status ft_sorted_map_prepare_result(const ft_sorted_map* map, ft_sorted_map* result)
{
    (void)memset(result, 0, sizeof(*result));
    return ft_sorted_map_configure(
        result,
        &map->key_type,
        &map->value_type,
        map->compare_key,
        map->compare_context);
}

static void ft_sorted_map_rebind(ft_sorted_map* map)
{
    map->policy.value.context = map->entry_context;
    map->tree.policy = &map->policy;
}

ft_status ft_sorted_map_init(
    ft_sorted_map* map,
    const ft_value_type* key_type,
    const ft_value_type* value_type,
    ft_compare_fn compare_key,
    void* compare_context)
{
    if (map == NULL || key_type == NULL || value_type == NULL || compare_key == NULL ||
        key_type->size == 0 || value_type->size == 0) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    (void)memset(map, 0, sizeof(*map));
    ft_status status = ft_sorted_map_configure(map, key_type, value_type, compare_key, compare_context);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_tree_init(&map->tree, &map->policy);
    if (status != FT_STATUS_OK) {
        free(map->entry_context);
        (void)memset(map, 0, sizeof(*map));
        return status;
    }

    return FT_STATUS_OK;
}

ft_status ft_sorted_map_copy(const ft_sorted_map* source, ft_sorted_map* destination)
{
    if (source == NULL || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_status status = ft_sorted_map_prepare_result(source, destination);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_tree_copy(&source->tree, &destination->tree);
    if (status != FT_STATUS_OK) {
        free(destination->entry_context);
        (void)memset(destination, 0, sizeof(*destination));
        return status;
    }

    ft_sorted_map_rebind(destination);
    return FT_STATUS_OK;
}

void ft_sorted_map_move(ft_sorted_map* destination, ft_sorted_map* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }

    *destination = *source;
    ft_sorted_map_rebind(destination);
    (void)memset(source, 0, sizeof(*source));
}

void ft_sorted_map_dispose(ft_sorted_map* map)
{
    if (map == NULL) {
        return;
    }

    ft_tree_dispose(&map->tree);
    free(map->entry_context);
    (void)memset(map, 0, sizeof(*map));
}

bool ft_sorted_map_empty(const ft_sorted_map* map)
{
    return map == NULL || ft_tree_empty(&map->tree);
}

size_t ft_sorted_map_size(const ft_sorted_map* map)
{
    return map == NULL ? 0 : ft_tree_size(&map->tree);
}

bool ft_sorted_map_contains_key(const ft_sorted_map* map, const void* key)
{
    if (map == NULL || key == NULL) {
        return false;
    }

    size_t lower = 0;
    size_t upper = 0;
    if (ft_sorted_map_bounds(map, key, &lower, &upper) != FT_STATUS_OK) {
        return false;
    }

    return lower != upper;
}

ft_status ft_sorted_map_try_get(
    const ft_sorted_map* map,
    const void* key,
    bool* found,
    void* value)
{
    if (map == NULL || key == NULL || found == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    size_t lower = 0;
    size_t upper = 0;
    ft_status status = ft_sorted_map_bounds(map, key, &lower, &upper);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (lower == upper) {
        *found = false;
        return FT_STATUS_OK;
    }

    ft_sorted_map_entry entry;
    status = ft_tree_at(&map->tree, lower, &entry);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (value != NULL) {
        ft_value_copy(&map->value_type, value, entry.value);
    }

    ft_sorted_map_entry_destroy_value(map->entry_context, &entry);
    *found = true;
    return FT_STATUS_OK;
}

ft_status ft_sorted_map_index_of_key(
    const ft_sorted_map* map,
    const void* key,
    bool* found,
    size_t* index)
{
    if (map == NULL || key == NULL || found == NULL || index == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    size_t lower = 0;
    size_t upper = 0;
    ft_status status = ft_sorted_map_bounds(map, key, &lower, &upper);
    if (status != FT_STATUS_OK) {
        return status;
    }

    *found = lower != upper;
    *index = lower;
    return FT_STATUS_OK;
}

ft_status ft_sorted_map_entry_at(
    const ft_sorted_map* map,
    size_t index,
    void* key,
    void* value)
{
    if (map == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_sorted_map_entry entry;
    ft_status status = ft_tree_at(&map->tree, index, &entry);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (key != NULL) {
        ft_value_copy(&map->key_type, key, entry.key);
    }

    if (value != NULL) {
        ft_value_copy(&map->value_type, value, entry.value);
    }

    ft_sorted_map_entry_destroy_value(map->entry_context, &entry);
    return FT_STATUS_OK;
}

ft_status ft_sorted_map_set(
    const ft_sorted_map* map,
    const void* key,
    const void* value,
    ft_sorted_map* result)
{
    if (map == NULL || key == NULL || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    size_t lower = 0;
    size_t upper = 0;
    ft_status status = ft_sorted_map_bounds(map, key, &lower, &upper);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_sorted_map_entry entry;
    status = ft_sorted_map_entry_init(map, key, value, &entry);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_sorted_map_prepare_result(map, result);
    if (status != FT_STATUS_OK) {
        ft_sorted_map_entry_destroy_value(map->entry_context, &entry);
        return status;
    }

    ft_tree without_old;
    bool has_without_old = false;
    if (lower != upper) {
        status = ft_tree_remove_at(&map->tree, lower, &without_old);
        if (status != FT_STATUS_OK) {
            ft_sorted_map_entry_destroy_value(map->entry_context, &entry);
            ft_sorted_map_dispose(result);
            return status;
        }

        has_without_old = true;
    } else {
        status = ft_tree_copy(&map->tree, &without_old);
        if (status != FT_STATUS_OK) {
            ft_sorted_map_entry_destroy_value(map->entry_context, &entry);
            ft_sorted_map_dispose(result);
            return status;
        }

        has_without_old = true;
    }

    status = ft_tree_insert_at(&without_old, lower, &entry, &result->tree);
    ft_sorted_map_entry_destroy_value(map->entry_context, &entry);
    if (has_without_old) {
        ft_tree_dispose(&without_old);
    }

    if (status != FT_STATUS_OK) {
        ft_sorted_map_dispose(result);
        return status;
    }

    ft_sorted_map_rebind(result);
    return FT_STATUS_OK;
}

ft_status ft_sorted_map_try_insert(
    const ft_sorted_map* map,
    const void* key,
    const void* value,
    bool* inserted,
    ft_sorted_map* result)
{
    if (map == NULL || key == NULL || value == NULL || inserted == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    size_t lower = 0;
    size_t upper = 0;
    ft_status status = ft_sorted_map_bounds(map, key, &lower, &upper);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (lower != upper) {
        *inserted = false;
        return ft_sorted_map_copy(map, result);
    }

    ft_sorted_map_entry entry;
    status = ft_sorted_map_entry_init(map, key, value, &entry);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_sorted_map_prepare_result(map, result);
    if (status != FT_STATUS_OK) {
        ft_sorted_map_entry_destroy_value(map->entry_context, &entry);
        return status;
    }

    status = ft_tree_insert_at(&map->tree, lower, &entry, &result->tree);
    ft_sorted_map_entry_destroy_value(map->entry_context, &entry);
    if (status != FT_STATUS_OK) {
        ft_sorted_map_dispose(result);
        return status;
    }

    ft_sorted_map_rebind(result);
    *inserted = true;
    return FT_STATUS_OK;
}

ft_status ft_sorted_map_insert(
    const ft_sorted_map* map,
    const void* key,
    const void* value,
    ft_sorted_map* result)
{
    bool inserted = false;
    ft_status status = ft_sorted_map_try_insert(map, key, value, &inserted, result);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (!inserted) {
        ft_sorted_map_dispose(result);
        return FT_STATUS_ALREADY_EXISTS;
    }

    return FT_STATUS_OK;
}

ft_status ft_sorted_map_remove(
    const ft_sorted_map* map,
    const void* key,
    ft_sorted_map* result)
{
    if (map == NULL || key == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    size_t lower = 0;
    size_t upper = 0;
    ft_status status = ft_sorted_map_bounds(map, key, &lower, &upper);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_sorted_map_prepare_result(map, result);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (lower == upper) {
        status = ft_tree_copy(&map->tree, &result->tree);
    } else {
        status = ft_tree_remove_at(&map->tree, lower, &result->tree);
    }

    if (status != FT_STATUS_OK) {
        ft_sorted_map_dispose(result);
        return status;
    }

    ft_sorted_map_rebind(result);
    return FT_STATUS_OK;
}

typedef struct ft_sorted_map_visit_context {
    ft_sorted_map_visit_fn visitor;
    void* context;
} ft_sorted_map_visit_context;

static void ft_sorted_map_visit_entry(const void* value, void* context)
{
    ft_sorted_map_visit_context* visit_context = (ft_sorted_map_visit_context*)context;
    const ft_sorted_map_entry* entry = (const ft_sorted_map_entry*)value;
    visit_context->visitor(entry->key, entry->value, visit_context->context);
}

ft_status ft_sorted_map_visit(const ft_sorted_map* map, ft_sorted_map_visit_fn visitor, void* context)
{
    if (map == NULL || visitor == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_sorted_map_visit_context visit_context;
    visit_context.visitor = visitor;
    visit_context.context = context;
    return ft_tree_visit(&map->tree, ft_sorted_map_visit_entry, &visit_context);
}

#define FT_ROPE_DEFAULT_MAX_CHUNK_LENGTH 2048u

typedef struct ft_rope_chunk {
    size_t length;
    unsigned char* data;
} ft_rope_chunk;

struct ft_rope_chunk_context {
    ft_value_type value_type;
};

static void ft_rope_chunk_destroy_value(ft_rope_chunk_context* context, ft_rope_chunk* chunk)
{
    if (chunk->data != NULL) {
        for (size_t index = 0; index != chunk->length; ++index) {
            void* item = chunk->data + index * context->value_type.size;
            ft_value_destroy(&context->value_type, item);
        }

        free(chunk->data);
    }

    chunk->length = 0;
    chunk->data = NULL;
}

static void ft_rope_chunk_copy(void* destination, const void* source, void* context)
{
    ft_rope_chunk_context* chunk_context = (ft_rope_chunk_context*)context;
    const ft_rope_chunk* source_chunk = (const ft_rope_chunk*)source;
    ft_rope_chunk* destination_chunk = (ft_rope_chunk*)destination;
    destination_chunk->length = source_chunk->length;
    destination_chunk->data = NULL;
    if (source_chunk->length == 0) {
        return;
    }

    destination_chunk->data = (unsigned char*)ft_allocate(source_chunk->length * chunk_context->value_type.size);
    if (destination_chunk->data == NULL) {
        abort();
    }

    for (size_t index = 0; index != source_chunk->length; ++index) {
        void* destination_item = destination_chunk->data + index * chunk_context->value_type.size;
        const void* source_item = source_chunk->data + index * chunk_context->value_type.size;
        ft_value_copy(&chunk_context->value_type, destination_item, source_item);
    }
}

static void ft_rope_chunk_destroy(void* value, void* context)
{
    ft_rope_chunk_destroy_value((ft_rope_chunk_context*)context, (ft_rope_chunk*)value);
}

static void ft_rope_chunk_measure(void* destination, const void* value, void* context)
{
    (void)context;
    *(size_t*)destination = ((const ft_rope_chunk*)value)->length;
}

static ft_status ft_rope_chunk_init_from_array(
    const ft_rope* rope,
    const void* values,
    size_t count,
    ft_rope_chunk* chunk)
{
    chunk->length = count;
    chunk->data = NULL;
    if (count == 0) {
        return FT_STATUS_OK;
    }

    if (count > SIZE_MAX / rope->value_type.size) {
        return FT_STATUS_OVERFLOW;
    }

    chunk->data = (unsigned char*)ft_allocate(count * rope->value_type.size);
    if (chunk->data == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    for (size_t index = 0; index != count; ++index) {
        void* destination = chunk->data + index * rope->value_type.size;
        const void* source = (const unsigned char*)values + index * rope->value_type.size;
        ft_value_copy(&rope->value_type, destination, source);
    }

    return FT_STATUS_OK;
}

static ft_status ft_rope_chunk_slice(
    const ft_rope* rope,
    const ft_rope_chunk* source,
    size_t index,
    size_t count,
    ft_rope_chunk* chunk)
{
    if (index > source->length || count > source->length - index) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    return ft_rope_chunk_init_from_array(
        rope,
        source->data + index * rope->value_type.size,
        count,
        chunk);
}

static ft_status ft_rope_chunk_concat(
    const ft_rope* rope,
    const ft_rope_chunk* left,
    const ft_rope_chunk* right,
    ft_rope_chunk* chunk)
{
    size_t length = 0;
    if (ft_add_overflows(left->length, right->length, &length) || length > rope->max_chunk_length) {
        return FT_STATUS_OVERFLOW;
    }

    chunk->length = length;
    chunk->data = NULL;
    if (length == 0) {
        return FT_STATUS_OK;
    }

    if (length > SIZE_MAX / rope->value_type.size) {
        return FT_STATUS_OVERFLOW;
    }

    chunk->data = (unsigned char*)ft_allocate(length * rope->value_type.size);
    if (chunk->data == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    size_t destination_index = 0;
    for (size_t index = 0; index != left->length; ++index, ++destination_index) {
        ft_value_copy(
            &rope->value_type,
            chunk->data + destination_index * rope->value_type.size,
            left->data + index * rope->value_type.size);
    }

    for (size_t index = 0; index != right->length; ++index, ++destination_index) {
        ft_value_copy(
            &rope->value_type,
            chunk->data + destination_index * rope->value_type.size,
            right->data + index * rope->value_type.size);
    }

    return FT_STATUS_OK;
}

static ft_status ft_rope_chunk_insert_value(
    const ft_rope* rope,
    const ft_rope_chunk* source,
    size_t index,
    const void* value,
    ft_rope_chunk* chunk)
{
    if (index > source->length || source->length == SIZE_MAX) {
        return index > source->length ? FT_STATUS_OUT_OF_RANGE : FT_STATUS_OVERFLOW;
    }

    chunk->length = source->length + 1u;
    chunk->data = NULL;
    if (chunk->length > SIZE_MAX / rope->value_type.size) {
        return FT_STATUS_OVERFLOW;
    }

    chunk->data = (unsigned char*)ft_allocate(chunk->length * rope->value_type.size);
    if (chunk->data == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    size_t destination_index = 0;
    for (size_t source_index = 0; source_index != index; ++source_index, ++destination_index) {
        ft_value_copy(
            &rope->value_type,
            chunk->data + destination_index * rope->value_type.size,
            source->data + source_index * rope->value_type.size);
    }

    ft_value_copy(
        &rope->value_type,
        chunk->data + destination_index * rope->value_type.size,
        value);
    ++destination_index;
    for (size_t source_index = index; source_index != source->length; ++source_index, ++destination_index) {
        ft_value_copy(
            &rope->value_type,
            chunk->data + destination_index * rope->value_type.size,
            source->data + source_index * rope->value_type.size);
    }

    return FT_STATUS_OK;
}

static ft_status ft_rope_chunk_remove_value(
    const ft_rope* rope,
    const ft_rope_chunk* source,
    size_t index,
    ft_rope_chunk* chunk)
{
    if (index >= source->length) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    chunk->length = source->length - 1u;
    chunk->data = NULL;
    if (chunk->length == 0) {
        return FT_STATUS_OK;
    }

    chunk->data = (unsigned char*)ft_allocate(chunk->length * rope->value_type.size);
    if (chunk->data == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    size_t destination_index = 0;
    for (size_t source_index = 0; source_index != source->length; ++source_index) {
        if (source_index == index) {
            continue;
        }

        ft_value_copy(
            &rope->value_type,
            chunk->data + destination_index * rope->value_type.size,
            source->data + source_index * rope->value_type.size);
        ++destination_index;
    }

    return FT_STATUS_OK;
}

static ft_status ft_rope_configure(ft_rope* rope, const ft_value_type* value_type)
{
    rope->chunk_context = (ft_rope_chunk_context*)calloc(1, sizeof(*rope->chunk_context));
    if (rope->chunk_context == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    rope->value_type = *value_type;
    rope->max_chunk_length = FT_ROPE_DEFAULT_MAX_CHUNK_LENGTH;
    rope->chunk_context->value_type = *value_type;
    rope->policy.value.size = sizeof(ft_rope_chunk);
    rope->policy.value.copy = ft_rope_chunk_copy;
    rope->policy.value.destroy = ft_rope_chunk_destroy;
    rope->policy.value.context = rope->chunk_context;
    rope->policy.measure.size = sizeof(size_t);
    rope->policy.measure.identity = ft_size_identity;
    rope->policy.measure.measure = ft_rope_chunk_measure;
    rope->policy.measure.combine = ft_size_combine;
    rope->policy.measure.context = NULL;
    return FT_STATUS_OK;
}

static ft_status ft_rope_prepare_result(const ft_rope* rope, ft_rope* result)
{
    (void)memset(result, 0, sizeof(*result));
    return ft_rope_configure(result, &rope->value_type);
}

static ft_status ft_rope_wrap_tree(const ft_rope* source, ft_tree tree, ft_rope* result)
{
    ft_status status = ft_rope_prepare_result(source, result);
    if (status != FT_STATUS_OK) {
        ft_tree_dispose(&tree);
        return status;
    }

    result->tree = tree;
    result->tree.policy = &result->policy;
    return FT_STATUS_OK;
}

static void ft_rope_rebind(ft_rope* rope)
{
    rope->policy.value.context = rope->chunk_context;
    rope->tree.policy = &rope->policy;
}

static bool ft_rope_length_reaches(const void* measure, void* context)
{
    return *(const size_t*)measure >= *(const size_t*)context;
}

static ft_status ft_rope_coalesce_tail(ft_rope* rope);
static ft_status ft_rope_coalesce_head(ft_rope* rope);

ft_status ft_rope_init(ft_rope* rope, const ft_value_type* value_type)
{
    if (rope == NULL || value_type == NULL || value_type->size == 0) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    (void)memset(rope, 0, sizeof(*rope));
    ft_status status = ft_rope_configure(rope, value_type);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_tree_init(&rope->tree, &rope->policy);
    if (status != FT_STATUS_OK) {
        free(rope->chunk_context);
        (void)memset(rope, 0, sizeof(*rope));
        return status;
    }

    return FT_STATUS_OK;
}

ft_status ft_rope_from_array(
    ft_rope* rope,
    const ft_value_type* value_type,
    const void* values,
    size_t count)
{
    if (rope == NULL || value_type == NULL || value_type->size == 0 || (values == NULL && count != 0)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_status status = ft_rope_init(rope, value_type);
    if (status != FT_STATUS_OK) {
        return status;
    }

    size_t offset = 0;
    while (offset != count) {
        size_t chunk_length = count - offset;
        if (chunk_length > rope->max_chunk_length) {
            chunk_length = rope->max_chunk_length;
        }

        ft_rope_chunk chunk;
        status = ft_rope_chunk_init_from_array(
            rope,
            (const unsigned char*)values + offset * value_type->size,
            chunk_length,
            &chunk);
        if (status != FT_STATUS_OK) {
            ft_rope_dispose(rope);
            return status;
        }

        ft_tree next;
        status = ft_tree_push_back(&rope->tree, &chunk, &next);
        ft_rope_chunk_destroy_value(rope->chunk_context, &chunk);
        if (status != FT_STATUS_OK) {
            ft_rope_dispose(rope);
            return status;
        }

        ft_tree_dispose(&rope->tree);
        rope->tree = next;
        rope->tree.policy = &rope->policy;
        offset += chunk_length;
    }

    return FT_STATUS_OK;
}

ft_status ft_rope_copy(const ft_rope* source, ft_rope* destination)
{
    if (source == NULL || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_status status = ft_rope_prepare_result(source, destination);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_tree_copy(&source->tree, &destination->tree);
    if (status != FT_STATUS_OK) {
        free(destination->chunk_context);
        (void)memset(destination, 0, sizeof(*destination));
        return status;
    }

    ft_rope_rebind(destination);
    return FT_STATUS_OK;
}

void ft_rope_move(ft_rope* destination, ft_rope* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }

    *destination = *source;
    ft_rope_rebind(destination);
    (void)memset(source, 0, sizeof(*source));
}

void ft_rope_dispose(ft_rope* rope)
{
    if (rope == NULL) {
        return;
    }

    ft_tree_dispose(&rope->tree);
    free(rope->chunk_context);
    (void)memset(rope, 0, sizeof(*rope));
}

bool ft_rope_empty(const ft_rope* rope)
{
    return rope == NULL || ft_tree_empty(&rope->tree);
}

ft_status ft_rope_try_size(const ft_rope* rope, size_t* size)
{
    if (rope == NULL || size == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    return ft_tree_measure(&rope->tree, size);
}

size_t ft_rope_size(const ft_rope* rope)
{
    /* The first size query on a fresh snapshot may need to compute and cache the root
     * measure, which can fail under allocation pressure; this convenience form collapses
     * that failure to 0. Callers that must distinguish an empty rope from a transient
     * failure should use ft_rope_try_size. */
    size_t size = 0;
    return ft_rope_try_size(rope, &size) == FT_STATUS_OK ? size : 0;
}

ft_status ft_rope_at(const ft_rope* rope, size_t index, void* destination)
{
    if (rope == NULL || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    const size_t size = ft_rope_size(rope);
    if (index >= size) {
        return size == 0 ? FT_STATUS_EMPTY : FT_STATUS_OUT_OF_RANGE;
    }

    size_t threshold = index + 1;
    size_t measure_before = 0;
    bool found = false;
    ft_rope_chunk chunk;
    ft_status status = ft_tree_locate(
        &rope->tree,
        ft_rope_length_reaches,
        &threshold,
        &found,
        &measure_before,
        &chunk);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (!found) {
        return FT_STATUS_NOT_FOUND;
    }

    const size_t chunk_index = index - measure_before;
    ft_value_copy(
        &rope->value_type,
        destination,
        chunk.data + chunk_index * rope->value_type.size);
    ft_rope_chunk_destroy_value(rope->chunk_context, &chunk);
    return FT_STATUS_OK;
}

ft_status ft_rope_split_at(const ft_rope* rope, size_t index, ft_rope_split_result* result)
{
    if (rope == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    const size_t size = ft_rope_size(rope);
    if (index > size) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    if (index == 0) {
        ft_status status = ft_rope_init(&result->left, &rope->value_type);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_rope_copy(rope, &result->right);
        if (status != FT_STATUS_OK) {
            ft_rope_dispose(&result->left);
        }

        return status;
    }

    if (index == size) {
        ft_status status = ft_rope_copy(rope, &result->left);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_rope_init(&result->right, &rope->value_type);
        if (status != FT_STATUS_OK) {
            ft_rope_dispose(&result->left);
        }

        return status;
    }

    const size_t threshold = index + 1;
    bool found = false;
    ft_tree left_tree = {0};
    ft_tree right_tree = {0};
    ft_rope_chunk hit;
    ft_status status = ft_tree_split(
        &rope->tree,
        ft_rope_length_reaches,
        (void*)&threshold,
        &found,
        &left_tree,
        &hit,
        &right_tree);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (!found) {
        return FT_STATUS_NOT_FOUND;
    }

    size_t measure_before = 0;
    status = ft_tree_measure(&left_tree, &measure_before);
    if (status != FT_STATUS_OK) {
        ft_tree_dispose(&left_tree);
        ft_tree_dispose(&right_tree);
        ft_rope_chunk_destroy_value(rope->chunk_context, &hit);
        return status;
    }

    status = ft_rope_wrap_tree(rope, left_tree, &result->left);
    if (status != FT_STATUS_OK) {
        ft_tree_dispose(&right_tree);
        ft_rope_chunk_destroy_value(rope->chunk_context, &hit);
        return status;
    }

    status = ft_rope_wrap_tree(rope, right_tree, &result->right);
    if (status != FT_STATUS_OK) {
        ft_rope_dispose(&result->left);
        ft_rope_chunk_destroy_value(rope->chunk_context, &hit);
        return status;
    }

    const size_t chunk_index = index - measure_before;
    if (chunk_index != 0) {
        ft_rope_chunk before;
        status = ft_rope_chunk_slice(rope, &hit, 0, chunk_index, &before);
        if (status != FT_STATUS_OK) {
            ft_rope_dispose(&result->left);
            ft_rope_dispose(&result->right);
            ft_rope_chunk_destroy_value(rope->chunk_context, &hit);
            return status;
        }

        ft_tree next_left;
        status = ft_tree_push_back(&result->left.tree, &before, &next_left);
        ft_rope_chunk_destroy_value(rope->chunk_context, &before);
        if (status != FT_STATUS_OK) {
            ft_rope_dispose(&result->left);
            ft_rope_dispose(&result->right);
            ft_rope_chunk_destroy_value(rope->chunk_context, &hit);
            return status;
        }

        ft_tree_dispose(&result->left.tree);
        result->left.tree = next_left;
        result->left.tree.policy = &result->left.policy;
    }

    if (chunk_index != hit.length) {
        ft_rope_chunk after;
        status = ft_rope_chunk_slice(rope, &hit, chunk_index, hit.length - chunk_index, &after);
        if (status != FT_STATUS_OK) {
            ft_rope_dispose(&result->left);
            ft_rope_dispose(&result->right);
            ft_rope_chunk_destroy_value(rope->chunk_context, &hit);
            return status;
        }

        ft_tree next_right;
        status = ft_tree_push_front(&result->right.tree, &after, &next_right);
        ft_rope_chunk_destroy_value(rope->chunk_context, &after);
        if (status != FT_STATUS_OK) {
            ft_rope_dispose(&result->left);
            ft_rope_dispose(&result->right);
            ft_rope_chunk_destroy_value(rope->chunk_context, &hit);
            return status;
        }

        ft_tree_dispose(&result->right.tree);
        result->right.tree = next_right;
        result->right.tree.policy = &result->right.policy;
    }

    ft_rope_chunk_destroy_value(rope->chunk_context, &hit);
    status = ft_rope_coalesce_tail(&result->left);
    if (status == FT_STATUS_OK) {
        status = ft_rope_coalesce_head(&result->right);
    }

    if (status != FT_STATUS_OK) {
        ft_rope_dispose(&result->left);
        ft_rope_dispose(&result->right);
    }

    return status;
}

static bool ft_rope_compatible(const ft_rope* left, const ft_rope* right)
{
    return left->value_type.size == right->value_type.size &&
        left->value_type.copy == right->value_type.copy &&
        left->value_type.destroy == right->value_type.destroy &&
        left->value_type.context == right->value_type.context;
}

static ft_status ft_rope_concat_raw(const ft_rope* left, const ft_rope* right, ft_rope* result)
{
    if (left == NULL || right == NULL || result == NULL || !ft_rope_compatible(left, right)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_status status = ft_rope_prepare_result(left, result);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_tree_rep* rep = NULL;
    status = ft_rep_concat_with_middle(&result->policy, left->tree.rep, NULL, 0, right->tree.rep, &rep);
    if (status != FT_STATUS_OK) {
        ft_rope_dispose(result);
        return status;
    }

    ft_rope_rebind(result);
    result->tree.rep = rep;
    return FT_STATUS_OK;
}

ft_status ft_rope_concat(const ft_rope* left, const ft_rope* right, ft_rope* result)
{
    if (left == NULL || right == NULL || result == NULL || !ft_rope_compatible(left, right)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    if (ft_rope_empty(left) || ft_rope_empty(right)) {
        return ft_rope_concat_raw(left, right, result);
    }

    ft_rope_chunk left_chunk;
    ft_status status = ft_tree_back(&left->tree, &left_chunk);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_rope_chunk right_chunk;
    status = ft_tree_front(&right->tree, &right_chunk);
    if (status != FT_STATUS_OK) {
        ft_rope_chunk_destroy_value(left->chunk_context, &left_chunk);
        return status;
    }

    size_t combined_length = 0;
    const bool can_merge = !ft_add_overflows(left_chunk.length, right_chunk.length, &combined_length) &&
        combined_length <= left->max_chunk_length;
    if (!can_merge) {
        ft_rope_chunk_destroy_value(right->chunk_context, &right_chunk);
        ft_rope_chunk_destroy_value(left->chunk_context, &left_chunk);
        return ft_rope_concat_raw(left, right, result);
    }

    ft_rope_chunk merged;
    status = ft_rope_chunk_concat(left, &left_chunk, &right_chunk, &merged);
    ft_rope_chunk_destroy_value(right->chunk_context, &right_chunk);
    ft_rope_chunk_destroy_value(left->chunk_context, &left_chunk);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_tree left_tree;
    status = ft_tree_pop_back(&left->tree, NULL, &left_tree);
    if (status != FT_STATUS_OK) {
        ft_rope_chunk_destroy_value(left->chunk_context, &merged);
        return status;
    }

    ft_tree right_tree;
    status = ft_tree_pop_front(&right->tree, NULL, &right_tree);
    if (status != FT_STATUS_OK) {
        ft_tree_dispose(&left_tree);
        ft_rope_chunk_destroy_value(left->chunk_context, &merged);
        return status;
    }

    ft_tree with_merged;
    status = ft_tree_push_back(&left_tree, &merged, &with_merged);
    ft_rope_chunk_destroy_value(left->chunk_context, &merged);
    ft_tree_dispose(&left_tree);
    if (status != FT_STATUS_OK) {
        ft_tree_dispose(&right_tree);
        return status;
    }

    ft_tree joined;
    joined.policy = with_merged.policy;
    joined.rep = NULL;
    status = ft_rep_concat_with_middle(
        with_merged.policy,
        with_merged.rep,
        NULL,
        0,
        right_tree.rep,
        &joined.rep);
    ft_tree_dispose(&with_merged);
    ft_tree_dispose(&right_tree);
    if (status != FT_STATUS_OK) {
        return status;
    }

    return ft_rope_wrap_tree(left, joined, result);
}

static ft_status ft_rope_single_chunk(const ft_rope* source, const ft_rope_chunk* chunk, ft_rope* result)
{
    ft_status status = ft_rope_init(result, &source->value_type);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (chunk->length == 0) {
        return FT_STATUS_OK;
    }

    ft_tree tree;
    status = ft_tree_push_back(&result->tree, chunk, &tree);
    if (status != FT_STATUS_OK) {
        ft_rope_dispose(result);
        return status;
    }

    ft_tree_dispose(&result->tree);
    result->tree = tree;
    result->tree.policy = &result->policy;
    return FT_STATUS_OK;
}

static ft_status ft_rope_replacement_from_chunk(
    const ft_rope* source,
    const ft_rope_chunk* chunk,
    ft_rope* result)
{
    if (chunk->length <= source->max_chunk_length) {
        return ft_rope_single_chunk(source, chunk, result);
    }

    const size_t first_length = chunk->length / 2u;
    ft_rope_chunk first;
    ft_status status = ft_rope_chunk_slice(source, chunk, 0, first_length, &first);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_rope_chunk second;
    status = ft_rope_chunk_slice(
        source,
        chunk,
        first_length,
        chunk->length - first_length,
        &second);
    if (status != FT_STATUS_OK) {
        ft_rope_chunk_destroy_value(source->chunk_context, &first);
        return status;
    }

    status = ft_rope_single_chunk(source, &first, result);
    ft_rope_chunk_destroy_value(source->chunk_context, &first);
    if (status == FT_STATUS_OK) {
        ft_tree next;
        status = ft_tree_push_back(&result->tree, &second, &next);
        if (status == FT_STATUS_OK) {
            ft_tree_dispose(&result->tree);
            result->tree = next;
            result->tree.policy = &result->policy;
        } else {
            ft_rope_dispose(result);
        }
    }

    ft_rope_chunk_destroy_value(source->chunk_context, &second);
    return status;
}

static ft_status ft_rope_coalesce_tail(ft_rope* rope)
{
    if (ft_tree_size(&rope->tree) < 2) {
        return FT_STATUS_OK;
    }

    ft_rope_chunk tail;
    ft_status status = ft_tree_back(&rope->tree, &tail);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_tree prefix_tree;
    status = ft_tree_pop_back(&rope->tree, NULL, &prefix_tree);
    if (status != FT_STATUS_OK) {
        ft_rope_chunk_destroy_value(rope->chunk_context, &tail);
        return status;
    }

    ft_rope prefix;
    status = ft_rope_wrap_tree(rope, prefix_tree, &prefix);
    if (status != FT_STATUS_OK) {
        ft_rope_chunk_destroy_value(rope->chunk_context, &tail);
        return status;
    }

    ft_rope tail_rope;
    status = ft_rope_single_chunk(rope, &tail, &tail_rope);
    ft_rope_chunk_destroy_value(rope->chunk_context, &tail);
    if (status != FT_STATUS_OK) {
        ft_rope_dispose(&prefix);
        return status;
    }

    ft_rope normalized;
    status = ft_rope_concat(&prefix, &tail_rope, &normalized);
    ft_rope_dispose(&prefix);
    ft_rope_dispose(&tail_rope);
    if (status == FT_STATUS_OK) {
        ft_rope_dispose(rope);
        ft_rope_move(rope, &normalized);
    }

    return status;
}

static ft_status ft_rope_coalesce_head(ft_rope* rope)
{
    if (ft_tree_size(&rope->tree) < 2) {
        return FT_STATUS_OK;
    }

    ft_rope_chunk head;
    ft_status status = ft_tree_front(&rope->tree, &head);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_tree suffix_tree;
    status = ft_tree_pop_front(&rope->tree, NULL, &suffix_tree);
    if (status != FT_STATUS_OK) {
        ft_rope_chunk_destroy_value(rope->chunk_context, &head);
        return status;
    }

    ft_rope suffix;
    status = ft_rope_wrap_tree(rope, suffix_tree, &suffix);
    if (status != FT_STATUS_OK) {
        ft_rope_chunk_destroy_value(rope->chunk_context, &head);
        return status;
    }

    ft_rope head_rope;
    status = ft_rope_single_chunk(rope, &head, &head_rope);
    ft_rope_chunk_destroy_value(rope->chunk_context, &head);
    if (status != FT_STATUS_OK) {
        ft_rope_dispose(&suffix);
        return status;
    }

    ft_rope normalized;
    status = ft_rope_concat(&head_rope, &suffix, &normalized);
    ft_rope_dispose(&head_rope);
    ft_rope_dispose(&suffix);
    if (status == FT_STATUS_OK) {
        ft_rope_dispose(rope);
        ft_rope_move(rope, &normalized);
    }

    return status;
}

ft_status ft_rope_insert_at(const ft_rope* rope, size_t index, const void* value, ft_rope* result)
{
    if (rope == NULL || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    const size_t size = ft_rope_size(rope);
    if (index > size) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    if (size == 0) {
        return ft_rope_from_array(result, &rope->value_type, value, 1);
    }

    const size_t threshold = index == size ? size : index + 1u;
    bool found = false;
    ft_tree left_tree;
    ft_tree right_tree;
    ft_rope_chunk hit;
    ft_status status = ft_tree_split(
        &rope->tree,
        ft_rope_length_reaches,
        (void*)&threshold,
        &found,
        &left_tree,
        &hit,
        &right_tree);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (!found) {
        return FT_STATUS_NOT_FOUND;
    }

    size_t measure_before = 0;
    status = ft_tree_measure(&left_tree, &measure_before);
    if (status != FT_STATUS_OK) {
        ft_tree_dispose(&left_tree);
        ft_tree_dispose(&right_tree);
        ft_rope_chunk_destroy_value(rope->chunk_context, &hit);
        return status;
    }

    ft_rope_chunk inserted;
    status = ft_rope_chunk_insert_value(rope, &hit, index - measure_before, value, &inserted);
    ft_rope_chunk_destroy_value(rope->chunk_context, &hit);
    if (status != FT_STATUS_OK) {
        ft_tree_dispose(&left_tree);
        ft_tree_dispose(&right_tree);
        return status;
    }

    ft_rope left;
    status = ft_rope_wrap_tree(rope, left_tree, &left);
    if (status != FT_STATUS_OK) {
        ft_tree_dispose(&right_tree);
        ft_rope_chunk_destroy_value(rope->chunk_context, &inserted);
        return status;
    }

    ft_rope right;
    status = ft_rope_wrap_tree(rope, right_tree, &right);
    if (status != FT_STATUS_OK) {
        ft_rope_dispose(&left);
        ft_rope_chunk_destroy_value(rope->chunk_context, &inserted);
        return status;
    }

    ft_rope replacement;
    status = ft_rope_replacement_from_chunk(rope, &inserted, &replacement);
    ft_rope_chunk_destroy_value(rope->chunk_context, &inserted);
    if (status != FT_STATUS_OK) {
        ft_rope_dispose(&left);
        ft_rope_dispose(&right);
        return status;
    }

    ft_rope prefix;
    status = ft_rope_concat(&left, &replacement, &prefix);
    if (status == FT_STATUS_OK) {
        status = ft_rope_concat(&prefix, &right, result);
        ft_rope_dispose(&prefix);
    }

    ft_rope_dispose(&replacement);
    ft_rope_dispose(&left);
    ft_rope_dispose(&right);
    return status;
}

ft_status ft_rope_push_back(const ft_rope* rope, const void* value, ft_rope* result)
{
    if (rope == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    return ft_rope_insert_at(rope, ft_rope_size(rope), value, result);
}

ft_status ft_rope_remove_at(const ft_rope* rope, size_t index, ft_rope* result)
{
    if (rope == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    if (index >= ft_rope_size(rope)) {
        return ft_rope_empty(rope) ? FT_STATUS_EMPTY : FT_STATUS_OUT_OF_RANGE;
    }

    const size_t threshold = index + 1u;
    bool found = false;
    ft_tree left_tree;
    ft_tree right_tree;
    ft_rope_chunk hit;
    ft_status status = ft_tree_split(
        &rope->tree,
        ft_rope_length_reaches,
        (void*)&threshold,
        &found,
        &left_tree,
        &hit,
        &right_tree);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (!found) {
        return FT_STATUS_NOT_FOUND;
    }

    size_t measure_before = 0;
    status = ft_tree_measure(&left_tree, &measure_before);
    if (status != FT_STATUS_OK) {
        ft_tree_dispose(&left_tree);
        ft_tree_dispose(&right_tree);
        ft_rope_chunk_destroy_value(rope->chunk_context, &hit);
        return status;
    }

    ft_rope_chunk removed;
    status = ft_rope_chunk_remove_value(rope, &hit, index - measure_before, &removed);
    ft_rope_chunk_destroy_value(rope->chunk_context, &hit);
    if (status != FT_STATUS_OK) {
        ft_tree_dispose(&left_tree);
        ft_tree_dispose(&right_tree);
        return status;
    }

    ft_rope left;
    status = ft_rope_wrap_tree(rope, left_tree, &left);
    if (status != FT_STATUS_OK) {
        ft_tree_dispose(&right_tree);
        ft_rope_chunk_destroy_value(rope->chunk_context, &removed);
        return status;
    }

    ft_rope right;
    status = ft_rope_wrap_tree(rope, right_tree, &right);
    if (status != FT_STATUS_OK) {
        ft_rope_dispose(&left);
        ft_rope_chunk_destroy_value(rope->chunk_context, &removed);
        return status;
    }

    ft_rope replacement;
    status = ft_rope_replacement_from_chunk(rope, &removed, &replacement);
    ft_rope_chunk_destroy_value(rope->chunk_context, &removed);
    if (status != FT_STATUS_OK) {
        ft_rope_dispose(&left);
        ft_rope_dispose(&right);
        return status;
    }

    ft_rope prefix;
    status = ft_rope_concat(&left, &replacement, &prefix);
    if (status == FT_STATUS_OK) {
        status = ft_rope_concat(&prefix, &right, result);
        ft_rope_dispose(&prefix);
    }

    ft_rope_dispose(&replacement);
    ft_rope_dispose(&left);
    ft_rope_dispose(&right);
    return status;
}

typedef struct ft_rope_visit_context {
    const ft_rope* rope;
    ft_visit_fn visitor;
    void* context;
} ft_rope_visit_context;

static void ft_rope_visit_chunk(const void* value, void* context)
{
    ft_rope_visit_context* visit_context = (ft_rope_visit_context*)context;
    const ft_rope_chunk* chunk = (const ft_rope_chunk*)value;
    for (size_t index = 0; index != chunk->length; ++index) {
        visit_context->visitor(
            chunk->data + index * visit_context->rope->value_type.size,
            visit_context->context);
    }
}

ft_status ft_rope_visit(const ft_rope* rope, ft_visit_fn visitor, void* context)
{
    if (rope == NULL || visitor == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_rope_visit_context visit_context;
    visit_context.rope = rope;
    visit_context.visitor = visitor;
    visit_context.context = context;
    return ft_tree_visit(&rope->tree, ft_rope_visit_chunk, &visit_context);
}

typedef struct ft_measured_rope_chunk {
    size_t length;
    unsigned char* data;
    void* user_measure;
} ft_measured_rope_chunk;

struct ft_measured_rope_chunk_context {
    ft_value_type value_type;
    ft_measure_policy user_measure;
    size_t pair_user_offset;
};

static size_t ft_align_up_size(size_t value, size_t alignment)
{
    const size_t remainder = value % alignment;
    return remainder == 0 ? value : value + alignment - remainder;
}

static size_t ft_measure_buffer_alignment(void)
{
    size_t alignment = sizeof(void*);
    if (sizeof(double) > alignment) {
        alignment = sizeof(double);
    }

    if (sizeof(long double) > alignment) {
        alignment = sizeof(long double);
    }

    return alignment;
}

static size_t ft_measured_rope_pair_size(const ft_measure_policy* user_measure)
{
    return ft_align_up_size(sizeof(size_t), ft_measure_buffer_alignment()) + user_measure->size;
}

static size_t* ft_measured_rope_pair_length(void* pair)
{
    return (size_t*)pair;
}

static const size_t* ft_measured_rope_pair_length_const(const void* pair)
{
    return (const size_t*)pair;
}

static void* ft_measured_rope_pair_user(const ft_measured_rope_chunk_context* context, void* pair)
{
    return (unsigned char*)pair + context->pair_user_offset;
}

static const void* ft_measured_rope_pair_user_const(const ft_measured_rope_chunk_context* context, const void* pair)
{
    return (const unsigned char*)pair + context->pair_user_offset;
}

static void ft_measured_rope_chunk_destroy_value(
    ft_measured_rope_chunk_context* context,
    ft_measured_rope_chunk* chunk)
{
    if (chunk->data != NULL) {
        for (size_t index = 0; index != chunk->length; ++index) {
            void* item = chunk->data + index * context->value_type.size;
            ft_value_destroy(&context->value_type, item);
        }

        free(chunk->data);
    }

    free(chunk->user_measure);
    chunk->length = 0;
    chunk->data = NULL;
    chunk->user_measure = NULL;
}

static void ft_measured_rope_chunk_copy(void* destination, const void* source, void* context)
{
    ft_measured_rope_chunk_context* chunk_context = (ft_measured_rope_chunk_context*)context;
    const ft_measured_rope_chunk* source_chunk = (const ft_measured_rope_chunk*)source;
    ft_measured_rope_chunk* destination_chunk = (ft_measured_rope_chunk*)destination;
    destination_chunk->length = source_chunk->length;
    destination_chunk->data = NULL;
    destination_chunk->user_measure = NULL;

    destination_chunk->user_measure = ft_allocate(chunk_context->user_measure.size);
    if (destination_chunk->user_measure == NULL) {
        abort();
    }

    (void)memcpy(destination_chunk->user_measure, source_chunk->user_measure, chunk_context->user_measure.size);

    if (source_chunk->length == 0) {
        return;
    }

    destination_chunk->data = (unsigned char*)ft_allocate(source_chunk->length * chunk_context->value_type.size);
    if (destination_chunk->data == NULL) {
        abort();
    }

    for (size_t index = 0; index != source_chunk->length; ++index) {
        void* destination_item = destination_chunk->data + index * chunk_context->value_type.size;
        const void* source_item = source_chunk->data + index * chunk_context->value_type.size;
        ft_value_copy(&chunk_context->value_type, destination_item, source_item);
    }
}

static void ft_measured_rope_chunk_destroy(void* value, void* context)
{
    ft_measured_rope_chunk_destroy_value(
        (ft_measured_rope_chunk_context*)context,
        (ft_measured_rope_chunk*)value);
}

static ft_status ft_measured_rope_chunk_compute_measure(
    const ft_measured_rope* rope,
    const ft_measured_rope_chunk* chunk,
    void* destination)
{
    void* accumulator = ft_allocate(rope->user_measure.size);
    void* element_measure = ft_allocate(rope->user_measure.size);
    void* combined = ft_allocate(rope->user_measure.size);
    if (accumulator == NULL || element_measure == NULL || combined == NULL) {
        free(accumulator);
        free(element_measure);
        free(combined);
        return FT_STATUS_NO_MEMORY;
    }

    ft_measure_identity(&rope->user_measure, accumulator);
    for (size_t index = 0; index != chunk->length; ++index) {
        const void* item = chunk->data + index * rope->value_type.size;
        ft_measure_for_value(&rope->user_measure, element_measure, item);
        ft_measure_combine(&rope->user_measure, combined, accumulator, element_measure);
        (void)memcpy(accumulator, combined, rope->user_measure.size);
    }

    (void)memcpy(destination, accumulator, rope->user_measure.size);
    free(accumulator);
    free(element_measure);
    free(combined);
    return FT_STATUS_OK;
}

static ft_status ft_measured_rope_chunk_init_from_array(
    const ft_measured_rope* rope,
    const void* values,
    size_t count,
    ft_measured_rope_chunk* chunk)
{
    chunk->length = count;
    chunk->data = NULL;
    chunk->user_measure = NULL;
    if (count > SIZE_MAX / rope->value_type.size) {
        return FT_STATUS_OVERFLOW;
    }

    chunk->user_measure = ft_allocate(rope->user_measure.size);
    if (chunk->user_measure == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    if (count != 0) {
        chunk->data = (unsigned char*)ft_allocate(count * rope->value_type.size);
        if (chunk->data == NULL) {
            ft_measured_rope_chunk_destroy_value(rope->chunk_context, chunk);
            return FT_STATUS_NO_MEMORY;
        }

        for (size_t index = 0; index != count; ++index) {
            void* destination = chunk->data + index * rope->value_type.size;
            const void* source = (const unsigned char*)values + index * rope->value_type.size;
            ft_value_copy(&rope->value_type, destination, source);
        }
    }

    ft_status status = ft_measured_rope_chunk_compute_measure(rope, chunk, chunk->user_measure);
    if (status != FT_STATUS_OK) {
        ft_measured_rope_chunk_destroy_value(rope->chunk_context, chunk);
    }

    return status;
}

static ft_status ft_measured_rope_chunk_slice(
    const ft_measured_rope* rope,
    const ft_measured_rope_chunk* source,
    size_t index,
    size_t count,
    ft_measured_rope_chunk* chunk)
{
    if (index > source->length || count > source->length - index) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    return ft_measured_rope_chunk_init_from_array(
        rope,
        source->data + index * rope->value_type.size,
        count,
        chunk);
}

static ft_status ft_measured_rope_chunk_from_bytes(
    const ft_measured_rope* rope,
    const unsigned char* bytes,
    size_t count,
    ft_measured_rope_chunk* chunk)
{
    return ft_measured_rope_chunk_init_from_array(rope, bytes, count, chunk);
}

static ft_status ft_measured_rope_chunk_concat(
    const ft_measured_rope* rope,
    const ft_measured_rope_chunk* left,
    const ft_measured_rope_chunk* right,
    ft_measured_rope_chunk* chunk)
{
    size_t length = 0;
    if (ft_add_overflows(left->length, right->length, &length) || length > rope->max_chunk_length ||
        length > SIZE_MAX / rope->value_type.size) {
        return FT_STATUS_OVERFLOW;
    }

    unsigned char* bytes = (unsigned char*)ft_allocate(length * rope->value_type.size);
    if (bytes == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    (void)memcpy(bytes, left->data, left->length * rope->value_type.size);
    (void)memcpy(
        bytes + left->length * rope->value_type.size,
        right->data,
        right->length * rope->value_type.size);
    const ft_status status = ft_measured_rope_chunk_from_bytes(rope, bytes, length, chunk);
    free(bytes);
    return status;
}

static ft_status ft_measured_rope_chunk_insert_value(
    const ft_measured_rope* rope,
    const ft_measured_rope_chunk* source,
    size_t index,
    const void* value,
    ft_measured_rope_chunk* chunk)
{
    if (index > source->length || source->length == SIZE_MAX) {
        return index > source->length ? FT_STATUS_OUT_OF_RANGE : FT_STATUS_OVERFLOW;
    }

    const size_t length = source->length + 1u;
    if (length > SIZE_MAX / rope->value_type.size) {
        return FT_STATUS_OVERFLOW;
    }

    unsigned char* bytes = (unsigned char*)ft_allocate(length * rope->value_type.size);
    if (bytes == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    const size_t prefix_bytes = index * rope->value_type.size;
    (void)memcpy(bytes, source->data, prefix_bytes);
    (void)memcpy(bytes + prefix_bytes, value, rope->value_type.size);
    (void)memcpy(
        bytes + prefix_bytes + rope->value_type.size,
        source->data + prefix_bytes,
        (source->length - index) * rope->value_type.size);
    const ft_status status = ft_measured_rope_chunk_from_bytes(rope, bytes, length, chunk);
    free(bytes);
    return status;
}

static ft_status ft_measured_rope_chunk_remove_value(
    const ft_measured_rope* rope,
    const ft_measured_rope_chunk* source,
    size_t index,
    ft_measured_rope_chunk* chunk)
{
    if (index >= source->length) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    const size_t length = source->length - 1u;
    if (length == 0) {
        return ft_measured_rope_chunk_init_from_array(rope, NULL, 0, chunk);
    }

    unsigned char* bytes = (unsigned char*)ft_allocate(length * rope->value_type.size);
    if (bytes == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    const size_t prefix_bytes = index * rope->value_type.size;
    (void)memcpy(bytes, source->data, prefix_bytes);
    (void)memcpy(
        bytes + prefix_bytes,
        source->data + prefix_bytes + rope->value_type.size,
        (source->length - index - 1u) * rope->value_type.size);
    const ft_status status = ft_measured_rope_chunk_from_bytes(rope, bytes, length, chunk);
    free(bytes);
    return status;
}

static void ft_measured_rope_tree_measure_identity(void* destination, void* context)
{
    ft_measured_rope_chunk_context* chunk_context = (ft_measured_rope_chunk_context*)context;
    *ft_measured_rope_pair_length(destination) = 0;
    ft_measure_identity(
        &chunk_context->user_measure,
        ft_measured_rope_pair_user(chunk_context, destination));
}

static void ft_measured_rope_tree_measure_value(void* destination, const void* value, void* context)
{
    ft_measured_rope_chunk_context* chunk_context = (ft_measured_rope_chunk_context*)context;
    const ft_measured_rope_chunk* chunk = (const ft_measured_rope_chunk*)value;
    *ft_measured_rope_pair_length(destination) = chunk->length;
    (void)memcpy(
        ft_measured_rope_pair_user(chunk_context, destination),
        chunk->user_measure,
        chunk_context->user_measure.size);
}

static void ft_measured_rope_tree_measure_combine(
    void* destination,
    const void* left,
    const void* right,
    void* context)
{
    ft_measured_rope_chunk_context* chunk_context = (ft_measured_rope_chunk_context*)context;
    *ft_measured_rope_pair_length(destination) =
        *ft_measured_rope_pair_length_const(left) + *ft_measured_rope_pair_length_const(right);
    ft_measure_combine(
        &chunk_context->user_measure,
        ft_measured_rope_pair_user(chunk_context, destination),
        ft_measured_rope_pair_user_const(chunk_context, left),
        ft_measured_rope_pair_user_const(chunk_context, right));
}

static ft_status ft_measured_rope_configure(
    ft_measured_rope* rope,
    const ft_value_type* value_type,
    const ft_measure_policy* user_measure)
{
    rope->chunk_context = (ft_measured_rope_chunk_context*)calloc(1, sizeof(*rope->chunk_context));
    if (rope->chunk_context == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    rope->value_type = *value_type;
    rope->user_measure = *user_measure;
    rope->max_chunk_length = FT_ROPE_DEFAULT_MAX_CHUNK_LENGTH;
    rope->chunk_context->value_type = *value_type;
    rope->chunk_context->user_measure = *user_measure;
    rope->chunk_context->pair_user_offset = ft_align_up_size(sizeof(size_t), ft_measure_buffer_alignment());

    rope->policy.value.size = sizeof(ft_measured_rope_chunk);
    rope->policy.value.copy = ft_measured_rope_chunk_copy;
    rope->policy.value.destroy = ft_measured_rope_chunk_destroy;
    rope->policy.value.context = rope->chunk_context;
    rope->policy.measure.size = ft_measured_rope_pair_size(user_measure);
    rope->policy.measure.identity = ft_measured_rope_tree_measure_identity;
    rope->policy.measure.measure = ft_measured_rope_tree_measure_value;
    rope->policy.measure.combine = ft_measured_rope_tree_measure_combine;
    rope->policy.measure.context = rope->chunk_context;
    return FT_STATUS_OK;
}

static ft_status ft_measured_rope_prepare_result(const ft_measured_rope* source, ft_measured_rope* result)
{
    (void)memset(result, 0, sizeof(*result));
    return ft_measured_rope_configure(result, &source->value_type, &source->user_measure);
}

static ft_status ft_measured_rope_wrap_tree(
    const ft_measured_rope* source,
    ft_tree tree,
    ft_measured_rope* result)
{
    ft_status status = ft_measured_rope_prepare_result(source, result);
    if (status != FT_STATUS_OK) {
        ft_tree_dispose(&tree);
        return status;
    }

    result->tree = tree;
    result->tree.policy = &result->policy;
    return FT_STATUS_OK;
}

static void ft_measured_rope_rebind(ft_measured_rope* rope)
{
    rope->policy.value.context = rope->chunk_context;
    rope->policy.measure.context = rope->chunk_context;
    rope->tree.policy = &rope->policy;
}

static bool ft_measured_rope_count_reaches(const void* measure, void* context)
{
    return *ft_measured_rope_pair_length_const(measure) >= *(const size_t*)context;
}

static ft_status ft_measured_rope_coalesce_tail(ft_measured_rope* rope);
static ft_status ft_measured_rope_coalesce_head(ft_measured_rope* rope);

typedef struct ft_measured_rope_user_predicate_context {
    const ft_measured_rope_chunk_context* chunk_context;
    ft_measure_predicate_fn predicate;
    void* predicate_context;
} ft_measured_rope_user_predicate_context;

static bool ft_measured_rope_user_predicate(const void* measure, void* context)
{
    ft_measured_rope_user_predicate_context* predicate_context =
        (ft_measured_rope_user_predicate_context*)context;
    return predicate_context->predicate(
        ft_measured_rope_pair_user_const(predicate_context->chunk_context, measure),
        predicate_context->predicate_context);
}

ft_status ft_measured_rope_init(
    ft_measured_rope* rope,
    const ft_value_type* value_type,
    const ft_measure_policy* user_measure)
{
    if (rope == NULL || value_type == NULL || user_measure == NULL || value_type->size == 0 ||
        user_measure->size == 0 || user_measure->identity == NULL || user_measure->measure == NULL ||
        user_measure->combine == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    (void)memset(rope, 0, sizeof(*rope));
    ft_status status = ft_measured_rope_configure(rope, value_type, user_measure);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_tree_init(&rope->tree, &rope->policy);
    if (status != FT_STATUS_OK) {
        free(rope->chunk_context);
        (void)memset(rope, 0, sizeof(*rope));
        return status;
    }

    return FT_STATUS_OK;
}

ft_status ft_measured_rope_from_array(
    ft_measured_rope* rope,
    const ft_value_type* value_type,
    const ft_measure_policy* user_measure,
    const void* values,
    size_t count)
{
    if (rope == NULL || value_type == NULL || user_measure == NULL || (values == NULL && count != 0)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_status status = ft_measured_rope_init(rope, value_type, user_measure);
    if (status != FT_STATUS_OK) {
        return status;
    }

    size_t offset = 0;
    while (offset != count) {
        size_t chunk_length = count - offset;
        if (chunk_length > rope->max_chunk_length) {
            chunk_length = rope->max_chunk_length;
        }

        ft_measured_rope_chunk chunk;
        status = ft_measured_rope_chunk_init_from_array(
            rope,
            (const unsigned char*)values + offset * value_type->size,
            chunk_length,
            &chunk);
        if (status != FT_STATUS_OK) {
            ft_measured_rope_dispose(rope);
            return status;
        }

        ft_tree next;
        status = ft_tree_push_back(&rope->tree, &chunk, &next);
        ft_measured_rope_chunk_destroy_value(rope->chunk_context, &chunk);
        if (status != FT_STATUS_OK) {
            ft_measured_rope_dispose(rope);
            return status;
        }

        ft_tree_dispose(&rope->tree);
        rope->tree = next;
        rope->tree.policy = &rope->policy;
        offset += chunk_length;
    }

    return FT_STATUS_OK;
}

ft_status ft_measured_rope_copy(const ft_measured_rope* source, ft_measured_rope* destination)
{
    if (source == NULL || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_status status = ft_measured_rope_prepare_result(source, destination);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_tree_copy(&source->tree, &destination->tree);
    if (status != FT_STATUS_OK) {
        free(destination->chunk_context);
        (void)memset(destination, 0, sizeof(*destination));
        return status;
    }

    ft_measured_rope_rebind(destination);
    return FT_STATUS_OK;
}

void ft_measured_rope_move(ft_measured_rope* destination, ft_measured_rope* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }

    *destination = *source;
    ft_measured_rope_rebind(destination);
    (void)memset(source, 0, sizeof(*source));
}

void ft_measured_rope_dispose(ft_measured_rope* rope)
{
    if (rope == NULL) {
        return;
    }

    ft_tree_dispose(&rope->tree);
    free(rope->chunk_context);
    (void)memset(rope, 0, sizeof(*rope));
}

bool ft_measured_rope_empty(const ft_measured_rope* rope)
{
    return rope == NULL || ft_tree_empty(&rope->tree);
}

ft_status ft_measured_rope_try_size(const ft_measured_rope* rope, size_t* size)
{
    if (rope == NULL || size == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    void* pair = ft_allocate(rope->policy.measure.size);
    if (pair == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    const ft_status status = ft_tree_measure(&rope->tree, pair);
    if (status == FT_STATUS_OK) {
        *size = *ft_measured_rope_pair_length_const(pair);
    }

    free(pair);
    return status;
}

size_t ft_measured_rope_size(const ft_measured_rope* rope)
{
    /* Collapses allocation failure to 0; see ft_rope_size. Use
     * ft_measured_rope_try_size to distinguish an empty rope from a failure. */
    size_t size = 0;
    return ft_measured_rope_try_size(rope, &size) == FT_STATUS_OK ? size : 0;
}

ft_status ft_measured_rope_measure(const ft_measured_rope* rope, void* destination)
{
    if (rope == NULL || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    void* pair = ft_allocate(rope->policy.measure.size);
    if (pair == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    ft_status status = ft_tree_measure(&rope->tree, pair);
    if (status == FT_STATUS_OK) {
        (void)memcpy(
            destination,
            ft_measured_rope_pair_user_const(rope->chunk_context, pair),
            rope->user_measure.size);
    }

    free(pair);
    return status;
}

ft_status ft_measured_rope_at(const ft_measured_rope* rope, size_t index, void* destination)
{
    if (rope == NULL || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    const size_t size = ft_measured_rope_size(rope);
    if (index >= size) {
        return size == 0 ? FT_STATUS_EMPTY : FT_STATUS_OUT_OF_RANGE;
    }

    size_t threshold = index + 1;
    void* pair_before = ft_allocate(rope->policy.measure.size);
    if (pair_before == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    bool found = false;
    ft_measured_rope_chunk chunk;
    ft_status status = ft_tree_locate(
        &rope->tree,
        ft_measured_rope_count_reaches,
        &threshold,
        &found,
        pair_before,
        &chunk);
    if (status != FT_STATUS_OK) {
        free(pair_before);
        return status;
    }

    if (!found) {
        free(pair_before);
        return FT_STATUS_NOT_FOUND;
    }

    const size_t chunk_index = index - *ft_measured_rope_pair_length_const(pair_before);
    ft_value_copy(
        &rope->value_type,
        destination,
        chunk.data + chunk_index * rope->value_type.size);
    ft_measured_rope_chunk_destroy_value(rope->chunk_context, &chunk);
    free(pair_before);
    return FT_STATUS_OK;
}

ft_status ft_measured_rope_prefix_measure(const ft_measured_rope* rope, size_t count, void* destination)
{
    if (rope == NULL || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    const size_t size = ft_measured_rope_size(rope);
    if (count > size) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    if (count == 0) {
        ft_measure_identity(&rope->user_measure, destination);
        return FT_STATUS_OK;
    }

    if (count == size) {
        return ft_measured_rope_measure(rope, destination);
    }

    void* pair_before = ft_allocate(rope->policy.measure.size);
    if (pair_before == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    ft_measured_rope_chunk chunk;
    bool found = false;
    ft_status status = ft_tree_locate(
        &rope->tree,
        ft_measured_rope_count_reaches,
        &count,
        &found,
        pair_before,
        &chunk);
    if (status != FT_STATUS_OK || !found) {
        free(pair_before);
        return status == FT_STATUS_OK ? FT_STATUS_NOT_FOUND : status;
    }

    const size_t base_index = *ft_measured_rope_pair_length_const(pair_before);
    const size_t local_count = count - base_index;
    (void)memcpy(
        destination,
        ft_measured_rope_pair_user_const(rope->chunk_context, pair_before),
        rope->user_measure.size);

    void* element_measure = ft_allocate(rope->user_measure.size);
    void* combined = ft_allocate(rope->user_measure.size);
    if (element_measure == NULL || combined == NULL) {
        ft_measured_rope_chunk_destroy_value(rope->chunk_context, &chunk);
        free(pair_before);
        free(element_measure);
        free(combined);
        return FT_STATUS_NO_MEMORY;
    }

    for (size_t index = 0; index != local_count; ++index) {
        const void* value = chunk.data + index * rope->value_type.size;
        ft_measure_for_value(&rope->user_measure, element_measure, value);
        ft_measure_combine(&rope->user_measure, combined, destination, element_measure);
        (void)memcpy(destination, combined, rope->user_measure.size);
    }

    ft_measured_rope_chunk_destroy_value(rope->chunk_context, &chunk);
    free(pair_before);
    free(element_measure);
    free(combined);
    return FT_STATUS_OK;
}

ft_status ft_measured_rope_split_at(
    const ft_measured_rope* rope,
    size_t index,
    ft_measured_rope_split_result* result)
{
    if (rope == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    const size_t size = ft_measured_rope_size(rope);
    if (index > size) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    if (index == 0) {
        ft_status status = ft_measured_rope_init(&result->left, &rope->value_type, &rope->user_measure);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_measured_rope_copy(rope, &result->right);
        if (status != FT_STATUS_OK) {
            ft_measured_rope_dispose(&result->left);
        }

        return status;
    }

    if (index == size) {
        ft_status status = ft_measured_rope_copy(rope, &result->left);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_measured_rope_init(&result->right, &rope->value_type, &rope->user_measure);
        if (status != FT_STATUS_OK) {
            ft_measured_rope_dispose(&result->left);
        }

        return status;
    }

    const size_t threshold = index + 1;
    bool found = false;
    ft_tree left_tree = {0};
    ft_tree right_tree = {0};
    ft_measured_rope_chunk hit;
    ft_status status = ft_tree_split(
        &rope->tree,
        ft_measured_rope_count_reaches,
        (void*)&threshold,
        &found,
        &left_tree,
        &hit,
        &right_tree);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (!found) {
        return FT_STATUS_NOT_FOUND;
    }

    void* pair_before = ft_allocate(rope->policy.measure.size);
    if (pair_before == NULL) {
        ft_tree_dispose(&left_tree);
        ft_tree_dispose(&right_tree);
        ft_measured_rope_chunk_destroy_value(rope->chunk_context, &hit);
        return FT_STATUS_NO_MEMORY;
    }

    status = ft_tree_measure(&left_tree, pair_before);
    if (status != FT_STATUS_OK) {
        free(pair_before);
        ft_tree_dispose(&left_tree);
        ft_tree_dispose(&right_tree);
        ft_measured_rope_chunk_destroy_value(rope->chunk_context, &hit);
        return status;
    }

    status = ft_measured_rope_wrap_tree(rope, left_tree, &result->left);
    if (status != FT_STATUS_OK) {
        free(pair_before);
        ft_tree_dispose(&right_tree);
        ft_measured_rope_chunk_destroy_value(rope->chunk_context, &hit);
        return status;
    }

    status = ft_measured_rope_wrap_tree(rope, right_tree, &result->right);
    if (status != FT_STATUS_OK) {
        free(pair_before);
        ft_measured_rope_dispose(&result->left);
        ft_measured_rope_chunk_destroy_value(rope->chunk_context, &hit);
        return status;
    }

    const size_t chunk_index = index - *ft_measured_rope_pair_length_const(pair_before);
    if (chunk_index != 0) {
        ft_measured_rope_chunk before;
        status = ft_measured_rope_chunk_slice(rope, &hit, 0, chunk_index, &before);
        if (status != FT_STATUS_OK) {
            free(pair_before);
            ft_measured_rope_dispose(&result->left);
            ft_measured_rope_dispose(&result->right);
            ft_measured_rope_chunk_destroy_value(rope->chunk_context, &hit);
            return status;
        }

        ft_tree next_left;
        status = ft_tree_push_back(&result->left.tree, &before, &next_left);
        ft_measured_rope_chunk_destroy_value(rope->chunk_context, &before);
        if (status != FT_STATUS_OK) {
            free(pair_before);
            ft_measured_rope_dispose(&result->left);
            ft_measured_rope_dispose(&result->right);
            ft_measured_rope_chunk_destroy_value(rope->chunk_context, &hit);
            return status;
        }

        ft_tree_dispose(&result->left.tree);
        result->left.tree = next_left;
        result->left.tree.policy = &result->left.policy;
    }

    if (chunk_index != hit.length) {
        ft_measured_rope_chunk after;
        status = ft_measured_rope_chunk_slice(rope, &hit, chunk_index, hit.length - chunk_index, &after);
        if (status != FT_STATUS_OK) {
            free(pair_before);
            ft_measured_rope_dispose(&result->left);
            ft_measured_rope_dispose(&result->right);
            ft_measured_rope_chunk_destroy_value(rope->chunk_context, &hit);
            return status;
        }

        ft_tree next_right;
        status = ft_tree_push_front(&result->right.tree, &after, &next_right);
        ft_measured_rope_chunk_destroy_value(rope->chunk_context, &after);
        if (status != FT_STATUS_OK) {
            free(pair_before);
            ft_measured_rope_dispose(&result->left);
            ft_measured_rope_dispose(&result->right);
            ft_measured_rope_chunk_destroy_value(rope->chunk_context, &hit);
            return status;
        }

        ft_tree_dispose(&result->right.tree);
        result->right.tree = next_right;
        result->right.tree.policy = &result->right.policy;
    }

    free(pair_before);
    ft_measured_rope_chunk_destroy_value(rope->chunk_context, &hit);
    status = ft_measured_rope_coalesce_tail(&result->left);
    if (status == FT_STATUS_OK) {
        status = ft_measured_rope_coalesce_head(&result->right);
    }

    if (status != FT_STATUS_OK) {
        ft_measured_rope_dispose(&result->left);
        ft_measured_rope_dispose(&result->right);
    }

    return status;
}

static bool ft_measured_rope_compatible(const ft_measured_rope* left, const ft_measured_rope* right)
{
    return left->value_type.size == right->value_type.size &&
        left->value_type.copy == right->value_type.copy &&
        left->value_type.destroy == right->value_type.destroy &&
        left->value_type.context == right->value_type.context &&
        left->user_measure.size == right->user_measure.size &&
        left->user_measure.identity == right->user_measure.identity &&
        left->user_measure.measure == right->user_measure.measure &&
        left->user_measure.combine == right->user_measure.combine &&
        left->user_measure.context == right->user_measure.context;
}

static ft_status ft_measured_rope_concat_raw(
    const ft_measured_rope* left,
    const ft_measured_rope* right,
    ft_measured_rope* result)
{
    if (left == NULL || right == NULL || result == NULL || !ft_measured_rope_compatible(left, right)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_status status = ft_measured_rope_prepare_result(left, result);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_tree_rep* rep = NULL;
    status = ft_rep_concat_with_middle(&result->policy, left->tree.rep, NULL, 0, right->tree.rep, &rep);
    if (status != FT_STATUS_OK) {
        ft_measured_rope_dispose(result);
        return status;
    }

    ft_measured_rope_rebind(result);
    result->tree.rep = rep;
    return FT_STATUS_OK;
}

ft_status ft_measured_rope_concat(
    const ft_measured_rope* left,
    const ft_measured_rope* right,
    ft_measured_rope* result)
{
    if (left == NULL || right == NULL || result == NULL || !ft_measured_rope_compatible(left, right)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    if (ft_measured_rope_empty(left) || ft_measured_rope_empty(right)) {
        return ft_measured_rope_concat_raw(left, right, result);
    }

    ft_measured_rope_chunk left_chunk;
    ft_status status = ft_tree_back(&left->tree, &left_chunk);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_measured_rope_chunk right_chunk;
    status = ft_tree_front(&right->tree, &right_chunk);
    if (status != FT_STATUS_OK) {
        ft_measured_rope_chunk_destroy_value(left->chunk_context, &left_chunk);
        return status;
    }

    size_t combined_length = 0;
    const bool can_merge = !ft_add_overflows(left_chunk.length, right_chunk.length, &combined_length) &&
        combined_length <= left->max_chunk_length;
    if (!can_merge) {
        ft_measured_rope_chunk_destroy_value(right->chunk_context, &right_chunk);
        ft_measured_rope_chunk_destroy_value(left->chunk_context, &left_chunk);
        return ft_measured_rope_concat_raw(left, right, result);
    }

    ft_measured_rope_chunk merged;
    status = ft_measured_rope_chunk_concat(left, &left_chunk, &right_chunk, &merged);
    ft_measured_rope_chunk_destroy_value(right->chunk_context, &right_chunk);
    ft_measured_rope_chunk_destroy_value(left->chunk_context, &left_chunk);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_tree left_tree;
    status = ft_tree_pop_back(&left->tree, NULL, &left_tree);
    if (status != FT_STATUS_OK) {
        ft_measured_rope_chunk_destroy_value(left->chunk_context, &merged);
        return status;
    }

    ft_tree right_tree;
    status = ft_tree_pop_front(&right->tree, NULL, &right_tree);
    if (status != FT_STATUS_OK) {
        ft_tree_dispose(&left_tree);
        ft_measured_rope_chunk_destroy_value(left->chunk_context, &merged);
        return status;
    }

    ft_tree with_merged;
    status = ft_tree_push_back(&left_tree, &merged, &with_merged);
    ft_measured_rope_chunk_destroy_value(left->chunk_context, &merged);
    ft_tree_dispose(&left_tree);
    if (status != FT_STATUS_OK) {
        ft_tree_dispose(&right_tree);
        return status;
    }

    ft_tree joined;
    joined.policy = with_merged.policy;
    joined.rep = NULL;
    status = ft_rep_concat_with_middle(
        with_merged.policy,
        with_merged.rep,
        NULL,
        0,
        right_tree.rep,
        &joined.rep);
    ft_tree_dispose(&with_merged);
    ft_tree_dispose(&right_tree);
    if (status != FT_STATUS_OK) {
        return status;
    }

    return ft_measured_rope_wrap_tree(left, joined, result);
}

static ft_status ft_measured_rope_single_chunk(
    const ft_measured_rope* source,
    const ft_measured_rope_chunk* chunk,
    ft_measured_rope* result)
{
    ft_status status = ft_measured_rope_init(result, &source->value_type, &source->user_measure);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (chunk->length == 0) {
        return FT_STATUS_OK;
    }

    ft_tree tree;
    status = ft_tree_push_back(&result->tree, chunk, &tree);
    if (status != FT_STATUS_OK) {
        ft_measured_rope_dispose(result);
        return status;
    }

    ft_tree_dispose(&result->tree);
    result->tree = tree;
    result->tree.policy = &result->policy;
    return FT_STATUS_OK;
}

static ft_status ft_measured_rope_replacement_from_chunk(
    const ft_measured_rope* source,
    const ft_measured_rope_chunk* chunk,
    ft_measured_rope* result)
{
    if (chunk->length <= source->max_chunk_length) {
        return ft_measured_rope_single_chunk(source, chunk, result);
    }

    const size_t first_length = chunk->length / 2u;
    ft_measured_rope_chunk first;
    ft_status status = ft_measured_rope_chunk_slice(source, chunk, 0, first_length, &first);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_measured_rope_chunk second;
    status = ft_measured_rope_chunk_slice(
        source,
        chunk,
        first_length,
        chunk->length - first_length,
        &second);
    if (status != FT_STATUS_OK) {
        ft_measured_rope_chunk_destroy_value(source->chunk_context, &first);
        return status;
    }

    status = ft_measured_rope_single_chunk(source, &first, result);
    ft_measured_rope_chunk_destroy_value(source->chunk_context, &first);
    if (status == FT_STATUS_OK) {
        ft_tree next;
        status = ft_tree_push_back(&result->tree, &second, &next);
        if (status == FT_STATUS_OK) {
            ft_tree_dispose(&result->tree);
            result->tree = next;
            result->tree.policy = &result->policy;
        } else {
            ft_measured_rope_dispose(result);
        }
    }

    ft_measured_rope_chunk_destroy_value(source->chunk_context, &second);
    return status;
}

static ft_status ft_measured_rope_coalesce_tail(ft_measured_rope* rope)
{
    if (ft_tree_size(&rope->tree) < 2) {
        return FT_STATUS_OK;
    }

    ft_measured_rope_chunk tail;
    ft_status status = ft_tree_back(&rope->tree, &tail);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_tree prefix_tree;
    status = ft_tree_pop_back(&rope->tree, NULL, &prefix_tree);
    if (status != FT_STATUS_OK) {
        ft_measured_rope_chunk_destroy_value(rope->chunk_context, &tail);
        return status;
    }

    ft_measured_rope prefix;
    status = ft_measured_rope_wrap_tree(rope, prefix_tree, &prefix);
    if (status != FT_STATUS_OK) {
        ft_measured_rope_chunk_destroy_value(rope->chunk_context, &tail);
        return status;
    }

    ft_measured_rope tail_rope;
    status = ft_measured_rope_single_chunk(rope, &tail, &tail_rope);
    ft_measured_rope_chunk_destroy_value(rope->chunk_context, &tail);
    if (status != FT_STATUS_OK) {
        ft_measured_rope_dispose(&prefix);
        return status;
    }

    ft_measured_rope normalized;
    status = ft_measured_rope_concat(&prefix, &tail_rope, &normalized);
    ft_measured_rope_dispose(&prefix);
    ft_measured_rope_dispose(&tail_rope);
    if (status == FT_STATUS_OK) {
        ft_measured_rope_dispose(rope);
        ft_measured_rope_move(rope, &normalized);
    }

    return status;
}

static ft_status ft_measured_rope_coalesce_head(ft_measured_rope* rope)
{
    if (ft_tree_size(&rope->tree) < 2) {
        return FT_STATUS_OK;
    }

    ft_measured_rope_chunk head;
    ft_status status = ft_tree_front(&rope->tree, &head);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_tree suffix_tree;
    status = ft_tree_pop_front(&rope->tree, NULL, &suffix_tree);
    if (status != FT_STATUS_OK) {
        ft_measured_rope_chunk_destroy_value(rope->chunk_context, &head);
        return status;
    }

    ft_measured_rope suffix;
    status = ft_measured_rope_wrap_tree(rope, suffix_tree, &suffix);
    if (status != FT_STATUS_OK) {
        ft_measured_rope_chunk_destroy_value(rope->chunk_context, &head);
        return status;
    }

    ft_measured_rope head_rope;
    status = ft_measured_rope_single_chunk(rope, &head, &head_rope);
    ft_measured_rope_chunk_destroy_value(rope->chunk_context, &head);
    if (status != FT_STATUS_OK) {
        ft_measured_rope_dispose(&suffix);
        return status;
    }

    ft_measured_rope normalized;
    status = ft_measured_rope_concat(&head_rope, &suffix, &normalized);
    ft_measured_rope_dispose(&head_rope);
    ft_measured_rope_dispose(&suffix);
    if (status == FT_STATUS_OK) {
        ft_measured_rope_dispose(rope);
        ft_measured_rope_move(rope, &normalized);
    }

    return status;
}

ft_status ft_measured_rope_insert_at(
    const ft_measured_rope* rope,
    size_t index,
    const void* value,
    ft_measured_rope* result)
{
    if (rope == NULL || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    const size_t size = ft_measured_rope_size(rope);
    if (index > size) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    if (size == 0) {
        return ft_measured_rope_from_array(
            result,
            &rope->value_type,
            &rope->user_measure,
            value,
            1);
    }

    const size_t threshold = index == size ? size : index + 1u;
    bool found = false;
    ft_tree left_tree;
    ft_tree right_tree;
    ft_measured_rope_chunk hit;
    ft_status status = ft_tree_split(
        &rope->tree,
        ft_measured_rope_count_reaches,
        (void*)&threshold,
        &found,
        &left_tree,
        &hit,
        &right_tree);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (!found) {
        return FT_STATUS_NOT_FOUND;
    }

    void* pair_before = ft_allocate(rope->policy.measure.size);
    if (pair_before == NULL) {
        ft_tree_dispose(&left_tree);
        ft_tree_dispose(&right_tree);
        ft_measured_rope_chunk_destroy_value(rope->chunk_context, &hit);
        return FT_STATUS_NO_MEMORY;
    }

    status = ft_tree_measure(&left_tree, pair_before);
    if (status != FT_STATUS_OK) {
        free(pair_before);
        ft_tree_dispose(&left_tree);
        ft_tree_dispose(&right_tree);
        ft_measured_rope_chunk_destroy_value(rope->chunk_context, &hit);
        return status;
    }

    const size_t measure_before = *ft_measured_rope_pair_length_const(pair_before);
    free(pair_before);
    ft_measured_rope_chunk inserted;
    status = ft_measured_rope_chunk_insert_value(rope, &hit, index - measure_before, value, &inserted);
    ft_measured_rope_chunk_destroy_value(rope->chunk_context, &hit);
    if (status != FT_STATUS_OK) {
        ft_tree_dispose(&left_tree);
        ft_tree_dispose(&right_tree);
        return status;
    }

    ft_measured_rope left;
    status = ft_measured_rope_wrap_tree(rope, left_tree, &left);
    if (status != FT_STATUS_OK) {
        ft_tree_dispose(&right_tree);
        ft_measured_rope_chunk_destroy_value(rope->chunk_context, &inserted);
        return status;
    }

    ft_measured_rope right;
    status = ft_measured_rope_wrap_tree(rope, right_tree, &right);
    if (status != FT_STATUS_OK) {
        ft_measured_rope_dispose(&left);
        ft_measured_rope_chunk_destroy_value(rope->chunk_context, &inserted);
        return status;
    }

    ft_measured_rope replacement;
    status = ft_measured_rope_replacement_from_chunk(rope, &inserted, &replacement);
    ft_measured_rope_chunk_destroy_value(rope->chunk_context, &inserted);
    if (status != FT_STATUS_OK) {
        ft_measured_rope_dispose(&left);
        ft_measured_rope_dispose(&right);
        return status;
    }

    ft_measured_rope prefix;
    status = ft_measured_rope_concat(&left, &replacement, &prefix);
    if (status == FT_STATUS_OK) {
        status = ft_measured_rope_concat(&prefix, &right, result);
        ft_measured_rope_dispose(&prefix);
    }

    ft_measured_rope_dispose(&replacement);
    ft_measured_rope_dispose(&left);
    ft_measured_rope_dispose(&right);
    return status;
}

ft_status ft_measured_rope_push_back(const ft_measured_rope* rope, const void* value, ft_measured_rope* result)
{
    if (rope == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    return ft_measured_rope_insert_at(rope, ft_measured_rope_size(rope), value, result);
}

ft_status ft_measured_rope_remove_at(const ft_measured_rope* rope, size_t index, ft_measured_rope* result)
{
    if (rope == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    if (index >= ft_measured_rope_size(rope)) {
        return ft_measured_rope_empty(rope) ? FT_STATUS_EMPTY : FT_STATUS_OUT_OF_RANGE;
    }

    const size_t threshold = index + 1u;
    bool found = false;
    ft_tree left_tree;
    ft_tree right_tree;
    ft_measured_rope_chunk hit;
    ft_status status = ft_tree_split(
        &rope->tree,
        ft_measured_rope_count_reaches,
        (void*)&threshold,
        &found,
        &left_tree,
        &hit,
        &right_tree);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (!found) {
        return FT_STATUS_NOT_FOUND;
    }

    void* pair_before = ft_allocate(rope->policy.measure.size);
    if (pair_before == NULL) {
        ft_tree_dispose(&left_tree);
        ft_tree_dispose(&right_tree);
        ft_measured_rope_chunk_destroy_value(rope->chunk_context, &hit);
        return FT_STATUS_NO_MEMORY;
    }

    status = ft_tree_measure(&left_tree, pair_before);
    if (status != FT_STATUS_OK) {
        free(pair_before);
        ft_tree_dispose(&left_tree);
        ft_tree_dispose(&right_tree);
        ft_measured_rope_chunk_destroy_value(rope->chunk_context, &hit);
        return status;
    }

    const size_t measure_before = *ft_measured_rope_pair_length_const(pair_before);
    free(pair_before);
    ft_measured_rope_chunk removed;
    status = ft_measured_rope_chunk_remove_value(rope, &hit, index - measure_before, &removed);
    ft_measured_rope_chunk_destroy_value(rope->chunk_context, &hit);
    if (status != FT_STATUS_OK) {
        ft_tree_dispose(&left_tree);
        ft_tree_dispose(&right_tree);
        return status;
    }

    ft_measured_rope left;
    status = ft_measured_rope_wrap_tree(rope, left_tree, &left);
    if (status != FT_STATUS_OK) {
        ft_tree_dispose(&right_tree);
        ft_measured_rope_chunk_destroy_value(rope->chunk_context, &removed);
        return status;
    }

    ft_measured_rope right;
    status = ft_measured_rope_wrap_tree(rope, right_tree, &right);
    if (status != FT_STATUS_OK) {
        ft_measured_rope_dispose(&left);
        ft_measured_rope_chunk_destroy_value(rope->chunk_context, &removed);
        return status;
    }

    ft_measured_rope replacement;
    status = ft_measured_rope_replacement_from_chunk(rope, &removed, &replacement);
    ft_measured_rope_chunk_destroy_value(rope->chunk_context, &removed);
    if (status != FT_STATUS_OK) {
        ft_measured_rope_dispose(&left);
        ft_measured_rope_dispose(&right);
        return status;
    }

    ft_measured_rope prefix;
    status = ft_measured_rope_concat(&left, &replacement, &prefix);
    if (status == FT_STATUS_OK) {
        status = ft_measured_rope_concat(&prefix, &right, result);
        ft_measured_rope_dispose(&prefix);
    }

    ft_measured_rope_dispose(&replacement);
    ft_measured_rope_dispose(&left);
    ft_measured_rope_dispose(&right);
    return status;
}

ft_status ft_measured_rope_locate_by_measure(
    const ft_measured_rope* rope,
    ft_measure_predicate_fn predicate,
    void* predicate_context,
    bool* found,
    size_t* index,
    void* measure_before,
    void* value)
{
    if (rope == NULL || predicate == NULL || found == NULL || index == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_measured_rope_user_predicate_context tree_predicate_context;
    tree_predicate_context.chunk_context = rope->chunk_context;
    tree_predicate_context.predicate = predicate;
    tree_predicate_context.predicate_context = predicate_context;

    void* pair_before = ft_allocate(rope->policy.measure.size);
    if (pair_before == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    ft_measured_rope_chunk chunk;
    bool tree_found = false;
    ft_status status = ft_tree_locate(
        &rope->tree,
        ft_measured_rope_user_predicate,
        &tree_predicate_context,
        &tree_found,
        pair_before,
        &chunk);
    if (status != FT_STATUS_OK) {
        free(pair_before);
        return status;
    }

    if (!tree_found) {
        *found = false;
        *index = ft_measured_rope_size(rope);
        if (measure_before != NULL) {
            (void)memcpy(
                measure_before,
                ft_measured_rope_pair_user_const(rope->chunk_context, pair_before),
                rope->user_measure.size);
        }

        free(pair_before);
        return FT_STATUS_OK;
    }

    void* accumulator = ft_allocate(rope->user_measure.size);
    void* element_measure = ft_allocate(rope->user_measure.size);
    void* combined = ft_allocate(rope->user_measure.size);
    if (accumulator == NULL || element_measure == NULL || combined == NULL) {
        free(accumulator);
        free(element_measure);
        free(combined);
        ft_measured_rope_chunk_destroy_value(rope->chunk_context, &chunk);
        free(pair_before);
        return FT_STATUS_NO_MEMORY;
    }

    (void)memcpy(
        accumulator,
        ft_measured_rope_pair_user_const(rope->chunk_context, pair_before),
        rope->user_measure.size);
    const size_t base_index = *ft_measured_rope_pair_length_const(pair_before);
    for (size_t local = 0; local != chunk.length; ++local) {
        const void* item = chunk.data + local * rope->value_type.size;
        ft_measure_for_value(&rope->user_measure, element_measure, item);
        ft_measure_combine(&rope->user_measure, combined, accumulator, element_measure);
        if (predicate(combined, predicate_context)) {
            *found = true;
            *index = base_index + local;
            if (measure_before != NULL) {
                (void)memcpy(measure_before, accumulator, rope->user_measure.size);
            }

            if (value != NULL) {
                ft_value_copy(&rope->value_type, value, item);
            }

            free(accumulator);
            free(element_measure);
            free(combined);
            ft_measured_rope_chunk_destroy_value(rope->chunk_context, &chunk);
            free(pair_before);
            return FT_STATUS_OK;
        }

        (void)memcpy(accumulator, combined, rope->user_measure.size);
    }

    *found = false;
    *index = ft_measured_rope_size(rope);
    if (measure_before != NULL) {
        (void)memcpy(measure_before, accumulator, rope->user_measure.size);
    }

    free(accumulator);
    free(element_measure);
    free(combined);
    ft_measured_rope_chunk_destroy_value(rope->chunk_context, &chunk);
    free(pair_before);
    return FT_STATUS_OK;
}

ft_status ft_measured_rope_split_by_measure(
    const ft_measured_rope* rope,
    ft_measure_predicate_fn predicate,
    void* predicate_context,
    ft_measured_rope_split_result* result)
{
    if (rope == NULL || predicate == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    bool found = false;
    size_t index = 0;
    ft_status status = ft_measured_rope_locate_by_measure(
        rope,
        predicate,
        predicate_context,
        &found,
        &index,
        NULL,
        NULL);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (!found) {
        status = ft_measured_rope_copy(rope, &result->left);
        if (status != FT_STATUS_OK) {
            return status;
        }

        status = ft_measured_rope_init(&result->right, &rope->value_type, &rope->user_measure);
        if (status != FT_STATUS_OK) {
            ft_measured_rope_dispose(&result->left);
        }

        return status;
    }

    return ft_measured_rope_split_at(rope, index, result);
}

typedef struct ft_measured_rope_visit_context {
    const ft_measured_rope* rope;
    ft_visit_fn visitor;
    void* context;
} ft_measured_rope_visit_context;

static void ft_measured_rope_visit_chunk(const void* value, void* context)
{
    ft_measured_rope_visit_context* visit_context = (ft_measured_rope_visit_context*)context;
    const ft_measured_rope_chunk* chunk = (const ft_measured_rope_chunk*)value;
    for (size_t index = 0; index != chunk->length; ++index) {
        visit_context->visitor(
            chunk->data + index * visit_context->rope->value_type.size,
            visit_context->context);
    }
}

ft_status ft_measured_rope_visit(const ft_measured_rope* rope, ft_visit_fn visitor, void* context)
{
    if (rope == NULL || visitor == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_measured_rope_visit_context visit_context;
    visit_context.rope = rope;
    visit_context.visitor = visitor;
    visit_context.context = context;
    return ft_tree_visit(&rope->tree, ft_measured_rope_visit_chunk, &visit_context);
}

typedef struct ft_priority_entry {
    uint64_t ordinal;
    void* priority;
    void* value;
} ft_priority_entry;

struct ft_priority_queue_entry_context {
    ft_value_type value_type;
    ft_value_type priority_type;
};

static void ft_priority_entry_destroy_value(ft_priority_queue_entry_context* context, ft_priority_entry* entry)
{
    if (entry->priority != NULL) {
        ft_value_destroy(&context->priority_type, entry->priority);
        free(entry->priority);
    }

    if (entry->value != NULL) {
        ft_value_destroy(&context->value_type, entry->value);
        free(entry->value);
    }

    entry->priority = NULL;
    entry->value = NULL;
}

static void ft_priority_entry_copy(void* destination, const void* source, void* context)
{
    ft_priority_queue_entry_context* entry_context = (ft_priority_queue_entry_context*)context;
    const ft_priority_entry* source_entry = (const ft_priority_entry*)source;
    ft_priority_entry* destination_entry = (ft_priority_entry*)destination;
    destination_entry->ordinal = source_entry->ordinal;
    destination_entry->priority = ft_allocate(entry_context->priority_type.size);
    destination_entry->value = ft_allocate(entry_context->value_type.size);

    if (destination_entry->priority == NULL || destination_entry->value == NULL) {
        abort();
    }

    ft_value_copy(&entry_context->priority_type, destination_entry->priority, source_entry->priority);
    ft_value_copy(&entry_context->value_type, destination_entry->value, source_entry->value);
}

static void ft_priority_entry_destroy(void* value, void* context)
{
    ft_priority_entry_destroy_value((ft_priority_queue_entry_context*)context, (ft_priority_entry*)value);
}

static ft_status ft_priority_entry_init(
    const ft_priority_queue* queue,
    const void* value,
    const void* priority,
    uint64_t ordinal,
    ft_priority_entry* entry)
{
    entry->ordinal = ordinal;
    entry->priority = ft_allocate(queue->priority_type.size);
    entry->value = ft_allocate(queue->value_type.size);
    if (entry->priority == NULL || entry->value == NULL) {
        ft_priority_entry_destroy_value(queue->entry_context, entry);
        return FT_STATUS_NO_MEMORY;
    }

    ft_value_copy(&queue->priority_type, entry->priority, priority);
    ft_value_copy(&queue->value_type, entry->value, value);
    return FT_STATUS_OK;
}

static int ft_priority_entry_compare(const ft_priority_queue* queue, const ft_priority_entry* left, const ft_priority_entry* right)
{
    const int priority_result = queue->compare_priority(left->priority, right->priority, queue->compare_context);
    if (priority_result != 0) {
        return priority_result;
    }

    return (left->ordinal > right->ordinal) - (left->ordinal < right->ordinal);
}

static ft_status ft_priority_queue_configure(
    ft_priority_queue* queue,
    const ft_value_type* value_type,
    const ft_value_type* priority_type,
    ft_compare_fn compare_priority,
    void* compare_context)
{
    queue->entry_context = (ft_priority_queue_entry_context*)calloc(1, sizeof(*queue->entry_context));
    if (queue->entry_context == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    queue->value_type = *value_type;
    queue->priority_type = *priority_type;
    queue->compare_priority = compare_priority;
    queue->compare_context = compare_context;
    queue->next_ordinal = 0;
    queue->entry_context->value_type = *value_type;
    queue->entry_context->priority_type = *priority_type;

    queue->policy.value.size = sizeof(ft_priority_entry);
    queue->policy.value.copy = ft_priority_entry_copy;
    queue->policy.value.destroy = ft_priority_entry_destroy;
    queue->policy.value.context = queue->entry_context;
    ft_size_measure_policy_init(&queue->policy.measure);
    return FT_STATUS_OK;
}

static void ft_priority_queue_rebind(ft_priority_queue* queue)
{
    queue->policy.value.context = queue->entry_context;
    queue->tree.policy = &queue->policy;
}

typedef struct ft_priority_bound_context {
    const ft_priority_queue* queue;
    const ft_priority_entry* value;
} ft_priority_bound_context;

static bool ft_priority_upper_reached(const void* leaf, void* context)
{
    const ft_priority_bound_context* bound = (const ft_priority_bound_context*)context;
    return ft_priority_entry_compare(bound->queue, bound->value, (const ft_priority_entry*)leaf) < 0;
}

static ft_status ft_priority_queue_upper_bound(const ft_priority_queue* queue, const ft_priority_entry* value, size_t* index)
{
    ft_priority_bound_context context;
    context.queue = queue;
    context.value = value;
    const void* hit = NULL;
    return ft_tree_bound(&queue->tree, ft_priority_upper_reached, &context, index, &hit);
}

ft_status ft_priority_queue_init(
    ft_priority_queue* queue,
    const ft_value_type* value_type,
    const ft_value_type* priority_type,
    ft_compare_fn compare_priority,
    void* compare_context)
{
    if (queue == NULL || value_type == NULL || priority_type == NULL || compare_priority == NULL ||
        value_type->size == 0 || priority_type->size == 0) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    (void)memset(queue, 0, sizeof(*queue));
    ft_status status = ft_priority_queue_configure(queue, value_type, priority_type, compare_priority, compare_context);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_tree_init(&queue->tree, &queue->policy);
    if (status != FT_STATUS_OK) {
        free(queue->entry_context);
        (void)memset(queue, 0, sizeof(*queue));
        return status;
    }

    return FT_STATUS_OK;
}

ft_status ft_priority_queue_copy(const ft_priority_queue* source, ft_priority_queue* destination)
{
    if (source == NULL || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    (void)memset(destination, 0, sizeof(*destination));
    ft_status status = ft_priority_queue_configure(
        destination,
        &source->value_type,
        &source->priority_type,
        source->compare_priority,
        source->compare_context);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_tree_copy(&source->tree, &destination->tree);
    if (status != FT_STATUS_OK) {
        free(destination->entry_context);
        (void)memset(destination, 0, sizeof(*destination));
        return status;
    }

    ft_priority_queue_rebind(destination);
    destination->next_ordinal = source->next_ordinal;
    return FT_STATUS_OK;
}

void ft_priority_queue_move(ft_priority_queue* destination, ft_priority_queue* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }

    *destination = *source;
    ft_priority_queue_rebind(destination);
    (void)memset(source, 0, sizeof(*source));
}

void ft_priority_queue_dispose(ft_priority_queue* queue)
{
    if (queue == NULL) {
        return;
    }

    ft_tree_dispose(&queue->tree);
    free(queue->entry_context);
    (void)memset(queue, 0, sizeof(*queue));
}

bool ft_priority_queue_empty(const ft_priority_queue* queue)
{
    return queue == NULL || ft_tree_empty(&queue->tree);
}

size_t ft_priority_queue_size(const ft_priority_queue* queue)
{
    return queue == NULL ? 0 : ft_tree_size(&queue->tree);
}

ft_status ft_priority_queue_push(
    const ft_priority_queue* queue,
    const void* value,
    const void* priority,
    ft_priority_queue* result)
{
    if (queue == NULL || value == NULL || priority == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    if (queue->next_ordinal == UINT64_MAX) {
        return FT_STATUS_OVERFLOW;
    }

    ft_priority_entry entry;
    ft_status status = ft_priority_entry_init(queue, value, priority, queue->next_ordinal, &entry);
    if (status != FT_STATUS_OK) {
        return status;
    }

    size_t index = 0;
    status = ft_priority_queue_upper_bound(queue, &entry, &index);
    if (status != FT_STATUS_OK) {
        ft_priority_entry_destroy_value(queue->entry_context, &entry);
        return status;
    }

    (void)memset(result, 0, sizeof(*result));
    status = ft_priority_queue_configure(
        result,
        &queue->value_type,
        &queue->priority_type,
        queue->compare_priority,
        queue->compare_context);
    if (status != FT_STATUS_OK) {
        ft_priority_entry_destroy_value(queue->entry_context, &entry);
        return status;
    }

    status = ft_tree_insert_at(&queue->tree, index, &entry, &result->tree);
    ft_priority_entry_destroy_value(queue->entry_context, &entry);
    if (status != FT_STATUS_OK) {
        free(result->entry_context);
        (void)memset(result, 0, sizeof(*result));
        return status;
    }

    ft_priority_queue_rebind(result);
    result->next_ordinal = queue->next_ordinal + 1;
    return FT_STATUS_OK;
}

ft_status ft_priority_queue_try_peek(
    const ft_priority_queue* queue,
    bool* found,
    void* value,
    void* priority)
{
    if (queue == NULL || found == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    if (ft_tree_empty(&queue->tree)) {
        *found = false;
        return FT_STATUS_OK;
    }

    /* The minimum lives at the front; read it through the O(1) digit access
     * instead of a root-to-leaf indexed descent. */
    ft_priority_entry entry;
    ft_status status = ft_tree_front(&queue->tree, &entry);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (value != NULL) {
        ft_value_copy(&queue->value_type, value, entry.value);
    }

    if (priority != NULL) {
        ft_value_copy(&queue->priority_type, priority, entry.priority);
    }

    ft_priority_entry_destroy_value(queue->entry_context, &entry);
    *found = true;
    return FT_STATUS_OK;
}

ft_status ft_priority_queue_try_pop(
    const ft_priority_queue* queue,
    bool* found,
    void* value,
    void* priority,
    ft_priority_queue* rest)
{
    if (queue == NULL || found == NULL || rest == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_status status = ft_priority_queue_try_peek(queue, found, value, priority);
    if (status != FT_STATUS_OK || !*found) {
        if (status == FT_STATUS_OK) {
            status = ft_priority_queue_copy(queue, rest);
        }

        return status;
    }

    (void)memset(rest, 0, sizeof(*rest));
    status = ft_priority_queue_configure(
        rest,
        &queue->value_type,
        &queue->priority_type,
        queue->compare_priority,
        queue->compare_context);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_tree_remove_at(&queue->tree, 0, &rest->tree);
    if (status != FT_STATUS_OK) {
        free(rest->entry_context);
        (void)memset(rest, 0, sizeof(*rest));
        return status;
    }

    ft_priority_queue_rebind(rest);
    rest->next_ordinal = queue->next_ordinal;
    return FT_STATUS_OK;
}

static int ft_interval_i64_compare(const void* left, const void* right, void* context)
{
    (void)context;
    const ft_interval_i64* left_interval = (const ft_interval_i64*)left;
    const ft_interval_i64* right_interval = (const ft_interval_i64*)right;
    if (left_interval->low != right_interval->low) {
        return (left_interval->low > right_interval->low) - (left_interval->low < right_interval->low);
    }

    return (left_interval->high > right_interval->high) - (left_interval->high < right_interval->high);
}

static bool ft_interval_i64_valid(ft_interval_i64 interval)
{
    return interval.low <= interval.high;
}

static void ft_interval_tree_i64_rebind(ft_interval_tree_i64* tree)
{
    tree->intervals.tree.policy = &tree->policy;
}

static bool ft_interval_i64_overlaps(ft_interval_i64 left, ft_interval_i64 right)
{
    return left.low <= right.high && right.low <= left.high;
}

typedef struct ft_interval_i64_measure {
    size_t count;
    bool has_max_high;
    int64_t max_high;
} ft_interval_i64_measure;

static void ft_interval_i64_measure_identity(void* destination, void* context)
{
    (void)context;
    ft_interval_i64_measure* measure = (ft_interval_i64_measure*)destination;
    measure->count = 0;
    measure->has_max_high = false;
    measure->max_high = 0;
}

static void ft_interval_i64_measure_value(void* destination, const void* value, void* context)
{
    (void)context;
    const ft_interval_i64* interval = (const ft_interval_i64*)value;
    ft_interval_i64_measure* measure = (ft_interval_i64_measure*)destination;
    measure->count = 1;
    measure->has_max_high = true;
    measure->max_high = interval->high;
}

static void ft_interval_i64_measure_combine(
    void* destination,
    const void* left,
    const void* right,
    void* context)
{
    (void)context;
    const ft_interval_i64_measure* left_measure = (const ft_interval_i64_measure*)left;
    const ft_interval_i64_measure* right_measure = (const ft_interval_i64_measure*)right;
    ft_interval_i64_measure* result = (ft_interval_i64_measure*)destination;
    result->count = left_measure->count + right_measure->count;
    result->has_max_high = left_measure->has_max_high || right_measure->has_max_high;
    if (!left_measure->has_max_high) {
        result->max_high = right_measure->max_high;
    } else if (!right_measure->has_max_high) {
        result->max_high = left_measure->max_high;
    } else {
        result->max_high = left_measure->max_high >= right_measure->max_high
            ? left_measure->max_high
            : right_measure->max_high;
    }
}

static bool ft_interval_i64_max_high_reaches(const void* measure, void* context)
{
    const ft_interval_i64_measure* interval_measure = (const ft_interval_i64_measure*)measure;
    const int64_t query_low = *(const int64_t*)context;
    return interval_measure->has_max_high && interval_measure->max_high >= query_low;
}

static bool ft_interval_i64_low_reached(const void* leaf, void* context)
{
    const ft_interval_i64* interval = (const ft_interval_i64*)leaf;
    return interval->low >= *(const int64_t*)context;
}

ft_status ft_interval_tree_i64_init(ft_interval_tree_i64* tree)
{
    if (tree == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_value_type_init(&tree->policy.value, sizeof(ft_interval_i64));
    tree->policy.measure.size = sizeof(ft_interval_i64_measure);
    tree->policy.measure.identity = ft_interval_i64_measure_identity;
    tree->policy.measure.measure = ft_interval_i64_measure_value;
    tree->policy.measure.combine = ft_interval_i64_measure_combine;
    tree->policy.measure.context = NULL;
    return ft_sorted_multiset_init(&tree->intervals, &tree->policy, ft_interval_i64_compare, NULL);
}

ft_status ft_interval_tree_i64_copy(const ft_interval_tree_i64* source, ft_interval_tree_i64* destination)
{
    if (source == NULL || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_status status = ft_interval_tree_i64_init(destination);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_sorted_multiset_dispose(&destination->intervals);
    status = ft_sorted_multiset_copy(&source->intervals, &destination->intervals);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_interval_tree_i64_rebind(destination);
    return FT_STATUS_OK;
}

void ft_interval_tree_i64_move(ft_interval_tree_i64* destination, ft_interval_tree_i64* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }

    *destination = *source;
    ft_interval_tree_i64_rebind(destination);
    (void)memset(source, 0, sizeof(*source));
}

void ft_interval_tree_i64_dispose(ft_interval_tree_i64* tree)
{
    if (tree == NULL) {
        return;
    }

    ft_sorted_multiset_dispose(&tree->intervals);
}

bool ft_interval_tree_i64_empty(const ft_interval_tree_i64* tree)
{
    return tree == NULL || ft_sorted_multiset_empty(&tree->intervals);
}

size_t ft_interval_tree_i64_size(const ft_interval_tree_i64* tree)
{
    return tree == NULL ? 0 : ft_sorted_multiset_size(&tree->intervals);
}

/* Rank of the first stored interval whose low endpoint is >= low. Intervals
 * are kept sorted by low only; the order among equal lows is insertion
 * history (new intervals precede existing equal-low ones), matching the C#
 * reference, so (low, high)-lexicographic binary searches must not be used. */
static ft_status ft_interval_tree_i64_lower_bound_low(
    const ft_interval_tree_i64* tree,
    int64_t low,
    size_t* index)
{
    const void* hit = NULL;
    return ft_tree_bound(
        &tree->intervals.tree,
        ft_interval_i64_low_reached,
        &low,
        index,
        &hit);
}

/* Rank of the first stored interval matching both endpoints: a structural
 * low-endpoint bound plus a scan over the equal-low run. */
static ft_status ft_interval_tree_i64_index_of(
    const ft_interval_tree_i64* tree,
    ft_interval_i64 interval,
    bool* found,
    size_t* index)
{
    *found = false;
    size_t position = 0;
    ft_status status = ft_interval_tree_i64_lower_bound_low(tree, interval.low, &position);
    if (status != FT_STATUS_OK) {
        return status;
    }

    const size_t count = ft_sorted_multiset_size(&tree->intervals);
    for (; position != count; ++position) {
        ft_interval_i64 current;
        status = ft_sorted_multiset_at(&tree->intervals, position, &current);
        if (status != FT_STATUS_OK) {
            return status;
        }

        if (current.low != interval.low) {
            break;
        }

        if (current.high == interval.high) {
            *found = true;
            *index = position;
            break;
        }
    }

    return FT_STATUS_OK;
}

ft_status ft_interval_tree_i64_insert(
    const ft_interval_tree_i64* tree,
    ft_interval_i64 interval,
    ft_interval_tree_i64* result)
{
    if (tree == NULL || result == NULL || !ft_interval_i64_valid(interval)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    /* Insert before every stored interval with low >= interval.low, matching
     * the C# reference: a new interval precedes existing equal-low ones. */
    size_t position = 0;
    ft_status status = ft_interval_tree_i64_lower_bound_low(tree, interval.low, &position);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_interval_tree_i64_init(result);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_sorted_multiset_dispose(&result->intervals);
    status = ft_tree_insert_at(&tree->intervals.tree, position, &interval, &result->intervals.tree);
    if (status != FT_STATUS_OK) {
        return status;
    }

    result->intervals.compare = tree->intervals.compare;
    result->intervals.compare_context = tree->intervals.compare_context;
    ft_interval_tree_i64_rebind(result);
    return FT_STATUS_OK;
}

ft_status ft_interval_tree_i64_remove_one(
    const ft_interval_tree_i64* tree,
    ft_interval_i64 interval,
    ft_interval_tree_i64* result)
{
    if (tree == NULL || result == NULL || !ft_interval_i64_valid(interval)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    bool found = false;
    size_t position = 0;
    ft_status status = ft_interval_tree_i64_index_of(tree, interval, &found, &position);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_status init_status = ft_interval_tree_i64_init(result);
    if (init_status != FT_STATUS_OK) {
        return init_status;
    }

    ft_sorted_multiset_dispose(&result->intervals);
    if (!found) {
        status = ft_sorted_multiset_copy(&tree->intervals, &result->intervals);
    } else {
        status = ft_tree_remove_at(&tree->intervals.tree, position, &result->intervals.tree);
        result->intervals.compare = tree->intervals.compare;
        result->intervals.compare_context = tree->intervals.compare_context;
    }

    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_interval_tree_i64_rebind(result);
    return FT_STATUS_OK;
}

bool ft_interval_tree_i64_contains(const ft_interval_tree_i64* tree, ft_interval_i64 interval)
{
    if (tree == NULL || !ft_interval_i64_valid(interval)) {
        return false;
    }

    bool found = false;
    size_t position = 0;
    if (ft_interval_tree_i64_index_of(tree, interval, &found, &position) != FT_STATUS_OK) {
        return false;
    }

    return found;
}

ft_status ft_interval_tree_i64_try_find_overlap(
    const ft_interval_tree_i64* tree,
    ft_interval_i64 query,
    bool* found,
    ft_interval_i64* overlap)
{
    if (tree == NULL || found == NULL || !ft_interval_i64_valid(query)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_interval_i64_measure before;
    ft_interval_i64 current;
    bool located = false;
    ft_status status = ft_tree_locate(
        &tree->intervals.tree,
        ft_interval_i64_max_high_reaches,
        &query.low,
        &located,
        &before,
        &current);
    if (status != FT_STATUS_OK) {
        return status;
    }

    *found = located && ft_interval_i64_overlaps(current, query);
    if (*found && overlap != NULL) {
        *overlap = current;
    }
    return FT_STATUS_OK;
}

size_t ft_interval_tree_i64_count_overlaps(const ft_interval_tree_i64* tree, ft_interval_i64 query)
{
    if (tree == NULL || !ft_interval_i64_valid(query)) {
        return 0;
    }

    ft_tree remaining;
    if (ft_tree_copy(&tree->intervals.tree, &remaining) != FT_STATUS_OK) {
        return 0;
    }

    size_t overlaps = 0;
    while (!ft_tree_empty(&remaining)) {
        ft_interval_i64_measure before;
        ft_interval_i64 current;
        bool found = false;
        ft_status status = ft_tree_locate(
            &remaining,
            ft_interval_i64_max_high_reaches,
            &query.low,
            &found,
            &before,
            &current);
        if (status != FT_STATUS_OK || !found || current.low > query.high) {
            break;
        }

        ++overlaps;
        ft_tree_split_result split;
        status = ft_tree_split_at(&remaining, before.count + 1u, &split);
        if (status != FT_STATUS_OK) {
            break;
        }

        ft_tree_dispose(&remaining);
        ft_tree_dispose(&split.left);
        remaining = split.right;
    }

    ft_tree_dispose(&remaining);
    return overlaps;
}

ft_status ft_interval_tree_i64_at(const ft_interval_tree_i64* tree, size_t index, ft_interval_i64* destination)
{
    if (tree == NULL || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    return ft_sorted_multiset_at(&tree->intervals, index, destination);
}

typedef struct ft_interval_entry {
    void* low;
    void* high;
} ft_interval_entry;

struct ft_interval_tree_context {
    ft_value_type endpoint_type;
    ft_compare_fn compare_endpoint;
    void* compare_context;
};

typedef struct ft_interval_measure {
    size_t count;
    const void* max_high;
} ft_interval_measure;

typedef struct ft_interval_overlap_query {
    const ft_interval_tree_context* interval_context;
    const void* low;
} ft_interval_overlap_query;

static int ft_interval_endpoint_compare(const ft_interval_tree_context* context, const void* left, const void* right)
{
    return context->compare_endpoint(left, right, context->compare_context);
}

typedef struct ft_interval_low_bound_context {
    const ft_interval_tree_context* interval_context;
    const void* low;
} ft_interval_low_bound_context;

static bool ft_interval_low_reached(const void* leaf, void* context)
{
    const ft_interval_entry* interval = (const ft_interval_entry*)leaf;
    const ft_interval_low_bound_context* bound = (const ft_interval_low_bound_context*)context;
    return ft_interval_endpoint_compare(bound->interval_context, interval->low, bound->low) >= 0;
}

static bool ft_interval_entry_valid(const ft_interval_tree_context* context, const void* low, const void* high)
{
    return ft_interval_endpoint_compare(context, low, high) <= 0;
}

static bool ft_interval_entry_overlaps(
    const ft_interval_tree_context* context,
    const ft_interval_entry* left,
    const void* query_low,
    const void* query_high)
{
    return ft_interval_endpoint_compare(context, left->low, query_high) <= 0 &&
        ft_interval_endpoint_compare(context, query_low, left->high) <= 0;
}

static void ft_interval_measure_identity(void* destination, void* context)
{
    (void)context;
    ft_interval_measure* measure = (ft_interval_measure*)destination;
    measure->count = 0;
    measure->max_high = NULL;
}

static void ft_interval_measure_value(void* destination, const void* value, void* context)
{
    (void)context;
    const ft_interval_entry* interval = (const ft_interval_entry*)value;
    ft_interval_measure* measure = (ft_interval_measure*)destination;
    measure->count = 1;
    measure->max_high = interval->high;
}

static void ft_interval_measure_combine(
    void* destination,
    const void* left,
    const void* right,
    void* context)
{
    const ft_interval_tree_context* interval_context = (const ft_interval_tree_context*)context;
    const ft_interval_measure* left_measure = (const ft_interval_measure*)left;
    const ft_interval_measure* right_measure = (const ft_interval_measure*)right;
    ft_interval_measure* result = (ft_interval_measure*)destination;
    result->count = left_measure->count + right_measure->count;
    if (left_measure->max_high == NULL) {
        result->max_high = right_measure->max_high;
    } else if (right_measure->max_high == NULL) {
        result->max_high = left_measure->max_high;
    } else {
        result->max_high = ft_interval_endpoint_compare(
            interval_context,
            left_measure->max_high,
            right_measure->max_high) >= 0
            ? left_measure->max_high
            : right_measure->max_high;
    }
}

static bool ft_interval_max_high_reaches(const void* measure, void* context)
{
    const ft_interval_measure* interval_measure = (const ft_interval_measure*)measure;
    const ft_interval_overlap_query* query = (const ft_interval_overlap_query*)context;
    return interval_measure->max_high != NULL &&
        ft_interval_endpoint_compare(query->interval_context, interval_measure->max_high, query->low) >= 0;
}

static void ft_interval_entry_destroy_value(ft_interval_tree_context* context, ft_interval_entry* entry)
{
    if (entry->low != NULL) {
        ft_value_destroy(&context->endpoint_type, entry->low);
        free(entry->low);
    }

    if (entry->high != NULL) {
        ft_value_destroy(&context->endpoint_type, entry->high);
        free(entry->high);
    }

    entry->low = NULL;
    entry->high = NULL;
}

static void ft_interval_entry_copy(void* destination, const void* source, void* context)
{
    ft_interval_tree_context* interval_context = (ft_interval_tree_context*)context;
    const ft_interval_entry* source_entry = (const ft_interval_entry*)source;
    ft_interval_entry* destination_entry = (ft_interval_entry*)destination;
    destination_entry->low = ft_allocate(interval_context->endpoint_type.size);
    destination_entry->high = ft_allocate(interval_context->endpoint_type.size);
    if (destination_entry->low == NULL || destination_entry->high == NULL) {
        abort();
    }

    ft_value_copy(&interval_context->endpoint_type, destination_entry->low, source_entry->low);
    ft_value_copy(&interval_context->endpoint_type, destination_entry->high, source_entry->high);
}

static void ft_interval_entry_destroy(void* value, void* context)
{
    ft_interval_entry_destroy_value((ft_interval_tree_context*)context, (ft_interval_entry*)value);
}

static int ft_interval_entry_compare(const void* left, const void* right, void* context)
{
    ft_interval_tree_context* interval_context = (ft_interval_tree_context*)context;
    const ft_interval_entry* left_entry = (const ft_interval_entry*)left;
    const ft_interval_entry* right_entry = (const ft_interval_entry*)right;
    const int low_comparison = ft_interval_endpoint_compare(interval_context, left_entry->low, right_entry->low);
    if (low_comparison != 0) {
        return low_comparison;
    }

    return ft_interval_endpoint_compare(interval_context, left_entry->high, right_entry->high);
}

static ft_status ft_interval_entry_init(
    const ft_interval_tree* tree,
    const void* low,
    const void* high,
    ft_interval_entry* entry)
{
    entry->low = ft_allocate(tree->endpoint_type.size);
    entry->high = ft_allocate(tree->endpoint_type.size);
    if (entry->low == NULL || entry->high == NULL) {
        ft_interval_entry_destroy_value(tree->interval_context, entry);
        return FT_STATUS_NO_MEMORY;
    }

    ft_value_copy(&tree->endpoint_type, entry->low, low);
    ft_value_copy(&tree->endpoint_type, entry->high, high);
    return FT_STATUS_OK;
}

static ft_status ft_interval_tree_configure(
    ft_interval_tree* tree,
    const ft_value_type* endpoint_type,
    ft_compare_fn compare_endpoint,
    void* compare_context)
{
    tree->interval_context = (ft_interval_tree_context*)calloc(1, sizeof(*tree->interval_context));
    if (tree->interval_context == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    tree->endpoint_type = *endpoint_type;
    tree->compare_endpoint = compare_endpoint;
    tree->compare_context = compare_context;
    tree->interval_context->endpoint_type = *endpoint_type;
    tree->interval_context->compare_endpoint = compare_endpoint;
    tree->interval_context->compare_context = compare_context;

    tree->policy.value.size = sizeof(ft_interval_entry);
    tree->policy.value.copy = ft_interval_entry_copy;
    tree->policy.value.destroy = ft_interval_entry_destroy;
    tree->policy.value.context = tree->interval_context;
    tree->policy.measure.size = sizeof(ft_interval_measure);
    tree->policy.measure.identity = ft_interval_measure_identity;
    tree->policy.measure.measure = ft_interval_measure_value;
    tree->policy.measure.combine = ft_interval_measure_combine;
    tree->policy.measure.context = tree->interval_context;
    return FT_STATUS_OK;
}

static ft_status ft_interval_tree_prepare_result(const ft_interval_tree* source, ft_interval_tree* result)
{
    (void)memset(result, 0, sizeof(*result));
    return ft_interval_tree_configure(
        result,
        &source->endpoint_type,
        source->compare_endpoint,
        source->compare_context);
}

static void ft_interval_tree_rebind(ft_interval_tree* tree)
{
    tree->policy.value.context = tree->interval_context;
    tree->policy.measure.context = tree->interval_context;
    tree->intervals.tree.policy = &tree->policy;
    tree->intervals.compare_context = tree->interval_context;
}

ft_status ft_interval_tree_init(
    ft_interval_tree* tree,
    const ft_value_type* endpoint_type,
    ft_compare_fn compare_endpoint,
    void* compare_context)
{
    if (tree == NULL || endpoint_type == NULL || endpoint_type->size == 0 || compare_endpoint == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    (void)memset(tree, 0, sizeof(*tree));
    ft_status status = ft_interval_tree_configure(tree, endpoint_type, compare_endpoint, compare_context);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_sorted_multiset_init(
        &tree->intervals,
        &tree->policy,
        ft_interval_entry_compare,
        tree->interval_context);
    if (status != FT_STATUS_OK) {
        free(tree->interval_context);
        (void)memset(tree, 0, sizeof(*tree));
        return status;
    }

    return FT_STATUS_OK;
}

ft_status ft_interval_tree_copy(const ft_interval_tree* source, ft_interval_tree* destination)
{
    if (source == NULL || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_status status = ft_interval_tree_prepare_result(source, destination);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_sorted_multiset_copy(&source->intervals, &destination->intervals);
    if (status != FT_STATUS_OK) {
        free(destination->interval_context);
        (void)memset(destination, 0, sizeof(*destination));
        return status;
    }

    ft_interval_tree_rebind(destination);
    return FT_STATUS_OK;
}

void ft_interval_tree_move(ft_interval_tree* destination, ft_interval_tree* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }

    *destination = *source;
    ft_interval_tree_rebind(destination);
    (void)memset(source, 0, sizeof(*source));
}

void ft_interval_tree_dispose(ft_interval_tree* tree)
{
    if (tree == NULL) {
        return;
    }

    ft_sorted_multiset_dispose(&tree->intervals);
    free(tree->interval_context);
    (void)memset(tree, 0, sizeof(*tree));
}

bool ft_interval_tree_empty(const ft_interval_tree* tree)
{
    return tree == NULL || ft_sorted_multiset_empty(&tree->intervals);
}

size_t ft_interval_tree_size(const ft_interval_tree* tree)
{
    return tree == NULL ? 0 : ft_sorted_multiset_size(&tree->intervals);
}

/* Rank of the first stored interval whose low endpoint is >= low; see the
 * i64 counterpart for the ordering contract. */
static ft_status ft_interval_tree_lower_bound_low(
    const ft_interval_tree* tree,
    const void* low,
    size_t* index)
{
    ft_interval_low_bound_context context;
    context.interval_context = tree->interval_context;
    context.low = low;
    const void* hit = NULL;
    return ft_tree_bound(
        &tree->intervals.tree,
        ft_interval_low_reached,
        &context,
        index,
        &hit);
}

static ft_status ft_interval_tree_index_of(
    const ft_interval_tree* tree,
    const void* low,
    const void* high,
    bool* found,
    size_t* index)
{
    *found = false;
    size_t position = 0;
    ft_status status = ft_interval_tree_lower_bound_low(tree, low, &position);
    if (status != FT_STATUS_OK) {
        return status;
    }

    const size_t count = ft_interval_tree_size(tree);
    for (; position != count; ++position) {
        ft_interval_entry current;
        status = ft_sorted_multiset_at(&tree->intervals, position, &current);
        if (status != FT_STATUS_OK) {
            return status;
        }

        const bool same_low = ft_interval_endpoint_compare(tree->interval_context, current.low, low) == 0;
        const bool same_high =
            same_low && ft_interval_endpoint_compare(tree->interval_context, current.high, high) == 0;
        ft_interval_entry_destroy_value(tree->interval_context, &current);
        if (!same_low) {
            break;
        }

        if (same_high) {
            *found = true;
            *index = position;
            break;
        }
    }

    return FT_STATUS_OK;
}

ft_status ft_interval_tree_insert(
    const ft_interval_tree* tree,
    const void* low,
    const void* high,
    ft_interval_tree* result)
{
    if (tree == NULL || low == NULL || high == NULL || result == NULL ||
        !ft_interval_entry_valid(tree->interval_context, low, high)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    /* Insert before every stored interval with low >= new low, matching the
     * C# reference: a new interval precedes existing equal-low ones. */
    size_t position = 0;
    ft_status status = ft_interval_tree_lower_bound_low(tree, low, &position);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_interval_entry entry;
    status = ft_interval_entry_init(tree, low, high, &entry);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_interval_tree_prepare_result(tree, result);
    if (status != FT_STATUS_OK) {
        ft_interval_entry_destroy_value(tree->interval_context, &entry);
        return status;
    }

    status = ft_tree_insert_at(&tree->intervals.tree, position, &entry, &result->intervals.tree);
    ft_interval_entry_destroy_value(tree->interval_context, &entry);
    if (status != FT_STATUS_OK) {
        ft_interval_tree_dispose(result);
        return status;
    }

    result->intervals.compare = tree->intervals.compare;
    result->intervals.compare_context = tree->intervals.compare_context;
    ft_interval_tree_rebind(result);
    return FT_STATUS_OK;
}

ft_status ft_interval_tree_remove_one(
    const ft_interval_tree* tree,
    const void* low,
    const void* high,
    ft_interval_tree* result)
{
    if (tree == NULL || low == NULL || high == NULL || result == NULL ||
        !ft_interval_entry_valid(tree->interval_context, low, high)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    bool found = false;
    size_t position = 0;
    ft_status status = ft_interval_tree_index_of(tree, low, high, &found, &position);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_interval_tree_prepare_result(tree, result);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (!found) {
        status = ft_sorted_multiset_copy(&tree->intervals, &result->intervals);
    } else {
        status = ft_tree_remove_at(&tree->intervals.tree, position, &result->intervals.tree);
        result->intervals.compare = tree->intervals.compare;
        result->intervals.compare_context = tree->intervals.compare_context;
    }

    if (status != FT_STATUS_OK) {
        ft_interval_tree_dispose(result);
        return status;
    }

    ft_interval_tree_rebind(result);
    return FT_STATUS_OK;
}

bool ft_interval_tree_contains(const ft_interval_tree* tree, const void* low, const void* high)
{
    if (tree == NULL || low == NULL || high == NULL ||
        !ft_interval_entry_valid(tree->interval_context, low, high)) {
        return false;
    }

    bool found = false;
    size_t position = 0;
    if (ft_interval_tree_index_of(tree, low, high, &found, &position) != FT_STATUS_OK) {
        return false;
    }

    return found;
}

ft_status ft_interval_tree_try_find_overlap(
    const ft_interval_tree* tree,
    const void* query_low,
    const void* query_high,
    bool* found,
    void* overlap_low,
    void* overlap_high)
{
    if (tree == NULL || query_low == NULL || query_high == NULL || found == NULL ||
        !ft_interval_entry_valid(tree->interval_context, query_low, query_high)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_interval_overlap_query query = { tree->interval_context, query_low };
    ft_interval_measure before;
    ft_interval_entry current = { 0 };
    bool located = false;
    ft_status status = ft_tree_locate(
        &tree->intervals.tree,
        ft_interval_max_high_reaches,
        &query,
        &located,
        &before,
        &current);
    if (status != FT_STATUS_OK) {
        return status;
    }

    *found = located && ft_interval_entry_overlaps(tree->interval_context, &current, query_low, query_high);
    if (*found && overlap_low != NULL) {
        ft_value_copy(&tree->endpoint_type, overlap_low, current.low);
    }

    if (*found && overlap_high != NULL) {
        ft_value_copy(&tree->endpoint_type, overlap_high, current.high);
    }

    if (located) {
        ft_interval_entry_destroy_value(tree->interval_context, &current);
    }
    return FT_STATUS_OK;
}

size_t ft_interval_tree_count_overlaps(
    const ft_interval_tree* tree,
    const void* query_low,
    const void* query_high)
{
    if (tree == NULL || query_low == NULL || query_high == NULL ||
        !ft_interval_entry_valid(tree->interval_context, query_low, query_high)) {
        return 0;
    }

    ft_tree remaining;
    if (ft_tree_copy(&tree->intervals.tree, &remaining) != FT_STATUS_OK) {
        return 0;
    }

    ft_interval_overlap_query query = { tree->interval_context, query_low };
    size_t overlaps = 0;
    while (!ft_tree_empty(&remaining)) {
        ft_interval_measure before;
        ft_interval_entry current = { 0 };
        bool found = false;
        ft_status status = ft_tree_locate(
            &remaining,
            ft_interval_max_high_reaches,
            &query,
            &found,
            &before,
            &current);
        if (status != FT_STATUS_OK || !found) {
            break;
        }

        const bool overlap = ft_interval_entry_overlaps(
            tree->interval_context,
            &current,
            query_low,
            query_high);
        ft_interval_entry_destroy_value(tree->interval_context, &current);
        if (!overlap) {
            break;
        }

        ++overlaps;
        ft_tree_split_result split;
        status = ft_tree_split_at(&remaining, before.count + 1u, &split);
        if (status != FT_STATUS_OK) {
            break;
        }

        ft_tree_dispose(&remaining);
        ft_tree_dispose(&split.left);
        remaining = split.right;
    }

    ft_tree_dispose(&remaining);
    return overlaps;
}

ft_status ft_interval_tree_at(
    const ft_interval_tree* tree,
    size_t index,
    void* low,
    void* high)
{
    if (tree == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_interval_entry entry;
    ft_status status = ft_sorted_multiset_at(&tree->intervals, index, &entry);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (low != NULL) {
        ft_value_copy(&tree->endpoint_type, low, entry.low);
    }

    if (high != NULL) {
        ft_value_copy(&tree->endpoint_type, high, entry.high);
    }

    ft_interval_entry_destroy_value(tree->interval_context, &entry);
    return FT_STATUS_OK;
}

static void ft_text_newline_identity(void* destination, void* context)
{
    (void)context;
    *(size_t*)destination = 0;
}

static void ft_text_newline_measure(void* destination, const void* value, void* context)
{
    (void)context;
    *(size_t*)destination = *(const char*)value == '\n' ? 1u : 0u;
}

static void ft_text_newline_combine(void* destination, const void* left, const void* right, void* context)
{
    (void)context;
    *(size_t*)destination = *(const size_t*)left + *(const size_t*)right;
}

static void ft_text_newline_policy_init(ft_measure_policy* policy)
{
    policy->size = sizeof(size_t);
    policy->identity = ft_text_newline_identity;
    policy->measure = ft_text_newline_measure;
    policy->combine = ft_text_newline_combine;
    policy->context = NULL;
}

static bool ft_text_newline_reaches(const void* measure, void* context)
{
    return *(const size_t*)measure >= *(const size_t*)context;
}

ft_status ft_text_rope_init(ft_text_rope* rope)
{
    if (rope == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_value_type char_type;
    ft_value_type_init(&char_type, sizeof(char));
    ft_measure_policy newline_measure;
    ft_text_newline_policy_init(&newline_measure);
    return ft_measured_rope_init(&rope->rope, &char_type, &newline_measure);
}

ft_status ft_text_rope_from_cstr(const char* text, ft_text_rope* rope)
{
    if (text == NULL || rope == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_value_type char_type;
    ft_value_type_init(&char_type, sizeof(char));
    ft_measure_policy newline_measure;
    ft_text_newline_policy_init(&newline_measure);
    return ft_measured_rope_from_array(&rope->rope, &char_type, &newline_measure, text, strlen(text));
}

ft_status ft_text_rope_copy(const ft_text_rope* source, ft_text_rope* destination)
{
    if (source == NULL || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    return ft_measured_rope_copy(&source->rope, &destination->rope);
}

void ft_text_rope_move(ft_text_rope* destination, ft_text_rope* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }

    ft_measured_rope_move(&destination->rope, &source->rope);
}

void ft_text_rope_dispose(ft_text_rope* rope)
{
    if (rope == NULL) {
        return;
    }

    ft_measured_rope_dispose(&rope->rope);
}

ft_status ft_text_rope_try_size(const ft_text_rope* rope, size_t* size)
{
    if (rope == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    return ft_measured_rope_try_size(&rope->rope, size);
}

size_t ft_text_rope_size(const ft_text_rope* rope)
{
    return rope == NULL ? 0 : ft_measured_rope_size(&rope->rope);
}

ft_status ft_text_rope_at(const ft_text_rope* rope, size_t index, char* value)
{
    if (rope == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    return ft_measured_rope_at(&rope->rope, index, value);
}

ft_status ft_text_rope_insert_char(const ft_text_rope* rope, size_t index, char value, ft_text_rope* result)
{
    if (rope == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    return ft_measured_rope_insert_at(&rope->rope, index, &value, &result->rope);
}

ft_status ft_text_rope_remove_at(const ft_text_rope* rope, size_t index, ft_text_rope* result)
{
    if (rope == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    return ft_measured_rope_remove_at(&rope->rope, index, &result->rope);
}

ft_status ft_text_rope_try_line_count(const ft_text_rope* rope, size_t* count)
{
    if (rope == NULL || count == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    size_t newlines = 0;
    const ft_status status = ft_measured_rope_measure(&rope->rope, &newlines);
    if (status == FT_STATUS_OK) {
        *count = newlines + 1u;
    }

    return status;
}

size_t ft_text_rope_line_count(const ft_text_rope* rope)
{
    /* An empty text rope has one line, so 0 here is otherwise impossible: it signals an
     * invalid rope or a transient allocation failure while caching the root measure. Use
     * ft_text_rope_try_line_count to observe the failure as a status. */
    size_t count = 0;
    return ft_text_rope_try_line_count(rope, &count) == FT_STATUS_OK ? count : 0;
}

ft_status ft_text_rope_line_of_offset(const ft_text_rope* rope, size_t offset, size_t* line)
{
    if (rope == NULL || line == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    if (offset > ft_text_rope_size(rope)) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    return ft_measured_rope_prefix_measure(&rope->rope, offset, line);
}

ft_status ft_text_rope_line_start_offset(const ft_text_rope* rope, size_t line, size_t* offset)
{
    if (rope == NULL || offset == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    if (line == 0) {
        *offset = 0;
        return FT_STATUS_OK;
    }

    size_t newlines = 0;
    ft_status status = ft_measured_rope_measure(&rope->rope, &newlines);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (line > newlines) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    bool found = false;
    size_t newline_index = 0;
    status = ft_measured_rope_locate_by_measure(
        &rope->rope,
        ft_text_newline_reaches,
        &line,
        &found,
        &newline_index,
        NULL,
        NULL);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (!found) {
        return FT_STATUS_NOT_FOUND;
    }

    *offset = newline_index + 1u;
    return FT_STATUS_OK;
}

ft_status ft_text_rope_line_column_of(const ft_text_rope* rope, size_t offset, ft_line_column* result)
{
    if (rope == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_status status = ft_text_rope_line_of_offset(rope, offset, &result->line);
    if (status != FT_STATUS_OK) {
        return status;
    }

    size_t line_start = 0;
    status = ft_text_rope_line_start_offset(rope, result->line, &line_start);
    if (status == FT_STATUS_OK) {
        result->column = offset - line_start;
    }

    return status;
}

ft_status ft_text_rope_offset_of(const ft_text_rope* rope, size_t line, size_t column, size_t* offset)
{
    if (rope == NULL || offset == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    size_t newlines = 0;
    ft_status status = ft_measured_rope_measure(&rope->rope, &newlines);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (line > newlines) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    size_t start = 0;
    status = ft_text_rope_line_start_offset(rope, line, &start);
    if (status != FT_STATUS_OK) {
        return status;
    }

    size_t end = ft_text_rope_size(rope);
    if (line < newlines) {
        const size_t next_line = line + 1u;
        bool found = false;
        status = ft_measured_rope_locate_by_measure(
            &rope->rope,
            ft_text_newline_reaches,
            (void*)&next_line,
            &found,
            &end,
            NULL,
            NULL);
        if (status != FT_STATUS_OK) {
            return status;
        }

        if (!found) {
            return FT_STATUS_NOT_FOUND;
        }
    }

    if (column > end - start) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    *offset = start + column;
    return FT_STATUS_OK;
}

ft_status ft_text_rope_visit(const ft_text_rope* rope, ft_visit_fn visitor, void* context)
{
    if (rope == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    return ft_measured_rope_visit(&rope->rope, visitor, context);
}
