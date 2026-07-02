#include <tools/data_structures/finger_tree/fingertree.h>

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef volatile LONG ft_ref_count;

static void ft_ref_init(ft_ref_count* count)
{
    *count = 1;
}

static void ft_ref_retain(ft_ref_count* count)
{
    (void)InterlockedIncrement(count);
}

static bool ft_ref_release(ft_ref_count* count)
{
    return InterlockedDecrement(count) == 0;
}
#else
#include <stdatomic.h>
typedef atomic_size_t ft_ref_count;

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
#endif

typedef enum ft_element_kind {
    FT_ELEMENT_LEAF,
    FT_ELEMENT_NODE
} ft_element_kind;

typedef struct ft_element ft_element;
typedef struct ft_node ft_node;

struct ft_element {
    ft_element_kind kind;
    size_t leaf_count;
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
    void* measure;
    union {
        ft_element single;
        struct {
            size_t prefix_count;
            ft_element prefix[4];
            ft_tree_rep* middle;
            size_t suffix_count;
            ft_element suffix[4];
        } deep;
    } as;
};

typedef struct ft_index_scan {
    size_t index;
    bool found;
    const ft_tree_policy* policy;
    ft_measure_predicate_fn predicate;
    void* predicate_context;
    void* accumulator;
    void* measure_before;
    void* value;
} ft_index_scan;

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

    ft_status status = ft_measure_new_copy(&policy->measure, source->measure, &destination->measure);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (source->kind == FT_ELEMENT_LEAF) {
        destination->as.value = ft_allocate(policy->value.size);
        if (destination->as.value == NULL) {
            free(destination->measure);
            (void)memset(destination, 0, sizeof(*destination));
            return FT_STATUS_NO_MEMORY;
        }

        ft_value_copy(&policy->value, destination->as.value, source->as.value);
        return FT_STATUS_OK;
    }

    destination->as.node = source->as.node;
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

        ft_rep_release(policy, rep->as.deep.middle);

        for (size_t index = 0; index != rep->as.deep.suffix_count; ++index) {
            ft_element_dispose(policy, &rep->as.deep.suffix[index]);
        }
    }

    free(rep->measure);
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
    ft_status status = ft_measure_new_identity(&policy->measure, &rep->measure);
    if (status != FT_STATUS_OK) {
        free(rep);
        return status;
    }

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

    status = ft_measure_new_copy(&policy->measure, element->measure, &rep->measure);
    if (status != FT_STATUS_OK) {
        ft_element_dispose(policy, &rep->as.single);
        free(rep);
        return status;
    }

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

static ft_status ft_rep_create_deep(
    const ft_tree_policy* policy,
    const ft_element* prefix,
    size_t prefix_count,
    ft_tree_rep* middle,
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
    ft_rep_retain(middle);

    for (size_t index = 0; index != suffix_count; ++index) {
        status = ft_element_clone(policy, &suffix[index], &rep->as.deep.suffix[index]);
        if (status != FT_STATUS_OK) {
            rep->as.deep.suffix_count = index;
            ft_rep_release(policy, rep);
            return status;
        }
    }

    void* prefix_measure = NULL;
    size_t prefix_leaves = 0;
    status = ft_combine_element_array(policy, prefix, prefix_count, &prefix_measure, &prefix_leaves);
    if (status != FT_STATUS_OK) {
        ft_rep_release(policy, rep);
        return status;
    }

    void* prefix_middle = NULL;
    status = ft_measure_new_combine(&policy->measure, prefix_measure, middle->measure, &prefix_middle);
    free(prefix_measure);
    if (status != FT_STATUS_OK) {
        ft_rep_release(policy, rep);
        return status;
    }

    void* suffix_measure = NULL;
    size_t suffix_leaves = 0;
    status = ft_combine_element_array(policy, suffix, suffix_count, &suffix_measure, &suffix_leaves);
    if (status != FT_STATUS_OK) {
        free(prefix_middle);
        ft_rep_release(policy, rep);
        return status;
    }

    status = ft_measure_new_combine(&policy->measure, prefix_middle, suffix_measure, &rep->measure);
    free(prefix_middle);
    free(suffix_measure);
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

    *result = rep;
    return FT_STATUS_OK;
}

static ft_status ft_rep_snoc(const ft_tree_policy* policy, const ft_tree_rep* rep, const ft_element* value, ft_tree_rep** result);
static ft_status ft_rep_cons(const ft_tree_policy* policy, const ft_tree_rep* rep, const ft_element* value, ft_tree_rep** result);
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

        return ft_rep_create_deep(
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

    ft_tree_rep* next_middle = NULL;
    status = ft_rep_cons(policy, rep->as.deep.middle, &pushed, &next_middle);
    ft_element_dispose(policy, &pushed);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_element prefix[2];
    prefix[0] = *value;
    prefix[1] = rep->as.deep.prefix[0];
    status = ft_rep_create_deep(
        policy,
        prefix,
        2,
        next_middle,
        rep->as.deep.suffix,
        rep->as.deep.suffix_count,
        result);
    ft_rep_release(policy, next_middle);
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
        return ft_rep_create_deep(
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

    ft_tree_rep* next_middle = NULL;
    status = ft_rep_snoc(policy, rep->as.deep.middle, &pushed, &next_middle);
    ft_element_dispose(policy, &pushed);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_element suffix[2];
    suffix[0] = rep->as.deep.suffix[3];
    suffix[1] = *value;
    status = ft_rep_create_deep(
        policy,
        rep->as.deep.prefix,
        rep->as.deep.prefix_count,
        next_middle,
        suffix,
        2,
        result);
    ft_rep_release(policy, next_middle);
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
        status = ft_rep_create_deep(
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

    if (rep->as.deep.middle->kind == FT_REP_EMPTY) {
        status = ft_rep_from_buffer(policy, rep->as.deep.suffix, rep->as.deep.suffix_count, rest);
        if (status != FT_STATUS_OK) {
            ft_element_dispose(policy, value);
        }

        return status;
    }

    ft_element pulled;
    ft_tree_rep* middle_rest = NULL;
    status = ft_rep_view_left(policy, rep->as.deep.middle, &pulled, &middle_rest);
    if (status != FT_STATUS_OK) {
        ft_element_dispose(policy, value);
        return status;
    }

    if (pulled.kind != FT_ELEMENT_NODE) {
        ft_element_dispose(policy, &pulled);
        ft_rep_release(policy, middle_rest);
        ft_element_dispose(policy, value);
        return FT_STATUS_INVALID_ARGUMENT;
    }

    status = ft_rep_create_deep(
        policy,
        pulled.as.node->children,
        pulled.as.node->child_count,
        middle_rest,
        rep->as.deep.suffix,
        rep->as.deep.suffix_count,
        rest);

    ft_element_dispose(policy, &pulled);
    ft_rep_release(policy, middle_rest);
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
        status = ft_rep_create_deep(
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

    if (rep->as.deep.middle->kind == FT_REP_EMPTY) {
        status = ft_rep_from_buffer(policy, rep->as.deep.prefix, rep->as.deep.prefix_count, rest);
        if (status != FT_STATUS_OK) {
            ft_element_dispose(policy, value);
        }

        return status;
    }

    ft_element pulled;
    ft_tree_rep* middle_rest = NULL;
    status = ft_rep_view_right(policy, rep->as.deep.middle, &pulled, &middle_rest);
    if (status != FT_STATUS_OK) {
        ft_element_dispose(policy, value);
        return status;
    }

    if (pulled.kind != FT_ELEMENT_NODE) {
        ft_element_dispose(policy, &pulled);
        ft_rep_release(policy, middle_rest);
        ft_element_dispose(policy, value);
        return FT_STATUS_INVALID_ARGUMENT;
    }

    status = ft_rep_create_deep(
        policy,
        rep->as.deep.prefix,
        rep->as.deep.prefix_count,
        middle_rest,
        pulled.as.node->children,
        pulled.as.node->child_count,
        rest);

    ft_element_dispose(policy, &pulled);
    ft_rep_release(policy, middle_rest);
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

    ft_tree_rep* middle_result = NULL;
    status = ft_rep_concat_with_middle(
        policy,
        left->as.deep.middle,
        nodes,
        node_count,
        right->as.deep.middle,
        &middle_result);

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

    if (offset < rep->as.deep.middle->leaf_count) {
        return ft_rep_copy_leaf_at(policy, rep->as.deep.middle, offset, destination);
    }

    offset -= rep->as.deep.middle->leaf_count;
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

    ft_status status = ft_visit_rep(policy, rep->as.deep.middle, visitor, context);
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

static ft_status ft_scan_leaf(ft_index_scan* scan, const ft_element* leaf)
{
    if (scan->found) {
        return FT_STATUS_OK;
    }

    void* next = NULL;
    ft_status status = ft_measure_new_combine(&scan->policy->measure, scan->accumulator, leaf->measure, &next);
    if (status != FT_STATUS_OK) {
        return status;
    }

    if (scan->predicate(next, scan->predicate_context)) {
        if (scan->measure_before != NULL) {
            (void)memcpy(scan->measure_before, scan->accumulator, scan->policy->measure.size);
        }

        if (scan->value != NULL) {
            ft_value_copy(&scan->policy->value, scan->value, leaf->as.value);
        }

        scan->found = true;
        free(next);
        return FT_STATUS_OK;
    }

    free(scan->accumulator);
    scan->accumulator = next;
    ++scan->index;
    return FT_STATUS_OK;
}

static ft_status ft_scan_element(ft_index_scan* scan, const ft_element* element)
{
    if (scan->found) {
        return FT_STATUS_OK;
    }

    if (element->kind == FT_ELEMENT_LEAF) {
        return ft_scan_leaf(scan, element);
    }

    for (size_t index = 0; index != element->as.node->child_count; ++index) {
        ft_status status = ft_scan_element(scan, &element->as.node->children[index]);
        if (status != FT_STATUS_OK || scan->found) {
            return status;
        }
    }

    return FT_STATUS_OK;
}

static ft_status ft_scan_rep(ft_index_scan* scan, const ft_tree_rep* rep)
{
    if (scan->found || rep->kind == FT_REP_EMPTY) {
        return FT_STATUS_OK;
    }

    if (rep->kind == FT_REP_SINGLE) {
        return ft_scan_element(scan, &rep->as.single);
    }

    for (size_t index = 0; index != rep->as.deep.prefix_count; ++index) {
        ft_status status = ft_scan_element(scan, &rep->as.deep.prefix[index]);
        if (status != FT_STATUS_OK || scan->found) {
            return status;
        }
    }

    ft_status status = ft_scan_rep(scan, rep->as.deep.middle);
    if (status != FT_STATUS_OK || scan->found) {
        return status;
    }

    for (size_t index = 0; index != rep->as.deep.suffix_count; ++index) {
        status = ft_scan_element(scan, &rep->as.deep.suffix[index]);
        if (status != FT_STATUS_OK || scan->found) {
            return status;
        }
    }

    return FT_STATUS_OK;
}

static bool ft_tree_is_valid(const ft_tree* tree)
{
    return tree != NULL && tree->policy != NULL && tree->rep != NULL;
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

    (void)memcpy(destination, tree->rep->measure, tree->policy->measure.size);
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

    ft_tree left;
    ft_status status = ft_tree_init(&left, tree->policy);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_tree rest;
    status = ft_tree_copy(tree, &rest);
    if (status != FT_STATUS_OK) {
        ft_tree_dispose(&left);
        return status;
    }

    for (size_t position = 0; position != index; ++position) {
        ft_element hit;
        ft_tree_rep* rest_rep = NULL;
        status = ft_rep_view_left(tree->policy, rest.rep, &hit, &rest_rep);
        if (status != FT_STATUS_OK) {
            ft_tree_dispose(&left);
            ft_tree_dispose(&rest);
            return status;
        }

        ft_tree_rep* left_rep = NULL;
        status = ft_rep_snoc(tree->policy, left.rep, &hit, &left_rep);
        ft_element_dispose(tree->policy, &hit);
        if (status != FT_STATUS_OK) {
            ft_rep_release(tree->policy, rest_rep);
            ft_tree_dispose(&left);
            ft_tree_dispose(&rest);
            return status;
        }

        ft_tree_dispose(&left);
        ft_tree_dispose(&rest);
        left.policy = tree->policy;
        left.rep = left_rep;
        rest.policy = tree->policy;
        rest.rep = rest_rep;
    }

    result->left = left;
    result->right = rest;
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

    ft_index_scan scan;
    (void)memset(&scan, 0, sizeof(scan));
    scan.policy = tree->policy;
    scan.predicate = predicate;
    scan.predicate_context = predicate_context;
    scan.measure_before = measure_before;
    scan.value = value;

    ft_status status = ft_measure_new_identity(&tree->policy->measure, &scan.accumulator);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_scan_rep(&scan, tree->rep);
    if (status == FT_STATUS_OK && !scan.found && measure_before != NULL) {
        (void)memcpy(measure_before, scan.accumulator, tree->policy->measure.size);
    }

    *found = scan.found;
    free(scan.accumulator);
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

    ft_index_scan scan;
    (void)memset(&scan, 0, sizeof(scan));
    scan.policy = tree->policy;
    scan.predicate = predicate;
    scan.predicate_context = predicate_context;

    ft_status status = ft_measure_new_identity(&tree->policy->measure, &scan.accumulator);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_scan_rep(&scan, tree->rep);
    free(scan.accumulator);
    if (status != FT_STATUS_OK) {
        return status;
    }

    *found = scan.found;
    if (!scan.found) {
        return FT_STATUS_OK;
    }

    ft_tree_split_result split;
    status = ft_tree_split_at(tree, scan.index, &split);
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

ft_status ft_reversible_deque_init(ft_reversible_deque* deque, const ft_tree_policy* policy)
{
    if (deque == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_status status = ft_tree_init(&deque->tree, policy);
    if (status != FT_STATUS_OK) {
        return status;
    }

    deque->reversed = false;
    return FT_STATUS_OK;
}

ft_status ft_reversible_deque_copy(const ft_reversible_deque* source, ft_reversible_deque* destination)
{
    if (source == NULL || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_status status = ft_tree_copy(&source->tree, &destination->tree);
    if (status != FT_STATUS_OK) {
        return status;
    }

    destination->reversed = source->reversed;
    return FT_STATUS_OK;
}

void ft_reversible_deque_dispose(ft_reversible_deque* deque)
{
    if (deque == NULL) {
        return;
    }

    ft_tree_dispose(&deque->tree);
    deque->reversed = false;
}

bool ft_reversible_deque_empty(const ft_reversible_deque* deque)
{
    return deque == NULL || ft_tree_empty(&deque->tree);
}

size_t ft_reversible_deque_size(const ft_reversible_deque* deque)
{
    return deque == NULL ? 0 : ft_tree_size(&deque->tree);
}

ft_status ft_reversible_deque_reverse(const ft_reversible_deque* deque, ft_reversible_deque* result)
{
    if (deque == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_status status = ft_tree_copy(&deque->tree, &result->tree);
    if (status != FT_STATUS_OK) {
        return status;
    }

    result->reversed = !deque->reversed;
    return FT_STATUS_OK;
}

ft_status ft_reversible_deque_at(const ft_reversible_deque* deque, size_t index, void* destination)
{
    if (deque == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    const size_t size = ft_tree_size(&deque->tree);
    if (index >= size) {
        return size == 0 ? FT_STATUS_EMPTY : FT_STATUS_OUT_OF_RANGE;
    }

    const size_t physical = deque->reversed ? size - 1 - index : index;
    return ft_tree_at(&deque->tree, physical, destination);
}

ft_status ft_reversible_deque_front(const ft_reversible_deque* deque, void* destination)
{
    return ft_reversible_deque_at(deque, 0, destination);
}

ft_status ft_reversible_deque_back(const ft_reversible_deque* deque, void* destination)
{
    if (deque == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    const size_t size = ft_tree_size(&deque->tree);
    if (size == 0) {
        return FT_STATUS_EMPTY;
    }

    return ft_reversible_deque_at(deque, size - 1, destination);
}

ft_status ft_reversible_deque_push_front(const ft_reversible_deque* deque, const void* value, ft_reversible_deque* result)
{
    if (deque == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_status status = deque->reversed
        ? ft_tree_push_back(&deque->tree, value, &result->tree)
        : ft_tree_push_front(&deque->tree, value, &result->tree);
    if (status != FT_STATUS_OK) {
        return status;
    }

    result->reversed = deque->reversed;
    return FT_STATUS_OK;
}

ft_status ft_reversible_deque_push_back(const ft_reversible_deque* deque, const void* value, ft_reversible_deque* result)
{
    if (deque == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_status status = deque->reversed
        ? ft_tree_push_front(&deque->tree, value, &result->tree)
        : ft_tree_push_back(&deque->tree, value, &result->tree);
    if (status != FT_STATUS_OK) {
        return status;
    }

    result->reversed = deque->reversed;
    return FT_STATUS_OK;
}

ft_status ft_reversible_deque_pop_front(const ft_reversible_deque* deque, void* value, ft_reversible_deque* rest)
{
    if (deque == NULL || rest == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_status status = deque->reversed
        ? ft_tree_pop_back(&deque->tree, value, &rest->tree)
        : ft_tree_pop_front(&deque->tree, value, &rest->tree);
    if (status != FT_STATUS_OK) {
        return status;
    }

    rest->reversed = deque->reversed;
    return FT_STATUS_OK;
}

ft_status ft_reversible_deque_pop_back(const ft_reversible_deque* deque, void* value, ft_reversible_deque* rest)
{
    if (deque == NULL || rest == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_status status = deque->reversed
        ? ft_tree_pop_front(&deque->tree, value, &rest->tree)
        : ft_tree_pop_back(&deque->tree, value, &rest->tree);
    if (status != FT_STATUS_OK) {
        return status;
    }

    rest->reversed = deque->reversed;
    return FT_STATUS_OK;
}

static int ft_compare_values(const ft_sorted_multiset* set, const void* left, const void* right)
{
    return set->compare(left, right, set->compare_context);
}

static ft_status ft_sorted_bounds(
    const ft_sorted_multiset* set,
    const void* value,
    size_t* lower,
    size_t* upper)
{
    void* current = ft_allocate(set->tree.policy->value.size);
    if (current == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    size_t lo = 0;
    size_t hi = ft_tree_size(&set->tree);
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        ft_status status = ft_tree_at(&set->tree, mid, current);
        if (status != FT_STATUS_OK) {
            free(current);
            return status;
        }

        if (ft_compare_values(set, current, value) < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }

        ft_value_destroy(&set->tree.policy->value, current);
    }

    *lower = lo;
    hi = ft_tree_size(&set->tree);
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        ft_status status = ft_tree_at(&set->tree, mid, current);
        if (status != FT_STATUS_OK) {
            free(current);
            return status;
        }

        if (ft_compare_values(set, value, current) < 0) {
            hi = mid;
        } else {
            lo = mid + 1;
        }

        ft_value_destroy(&set->tree.policy->value, current);
    }

    *upper = lo;
    free(current);
    return FT_STATUS_OK;
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

static ft_status ft_sorted_map_bounds(
    const ft_sorted_map* map,
    const void* key,
    size_t* lower,
    size_t* upper)
{
    ft_sorted_map_entry current;
    size_t lo = 0;
    size_t hi = ft_tree_size(&map->tree);
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        ft_status status = ft_tree_at(&map->tree, mid, &current);
        if (status != FT_STATUS_OK) {
            return status;
        }

        const int comparison = ft_sorted_map_compare_key(map, current.key, key);
        ft_sorted_map_entry_destroy_value(map->entry_context, &current);
        if (comparison < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    *lower = lo;
    hi = ft_tree_size(&map->tree);
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        ft_status status = ft_tree_at(&map->tree, mid, &current);
        if (status != FT_STATUS_OK) {
            return status;
        }

        const int comparison = ft_sorted_map_compare_key(map, key, current.key);
        ft_sorted_map_entry_destroy_value(map->entry_context, &current);
        if (comparison < 0) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }

    *upper = lo;
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

    destination->tree.policy = &destination->policy;
    return FT_STATUS_OK;
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

    result->tree.policy = &result->policy;
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

    result->tree.policy = &result->policy;
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

    result->tree.policy = &result->policy;
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

static bool ft_rope_length_reaches(const void* measure, void* context)
{
    return *(const size_t*)measure >= *(const size_t*)context;
}

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

    destination->tree.policy = &destination->policy;
    return FT_STATUS_OK;
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

size_t ft_rope_size(const ft_rope* rope)
{
    if (rope == NULL) {
        return 0;
    }

    size_t size = 0;
    if (ft_tree_measure(&rope->tree, &size) != FT_STATUS_OK) {
        return 0;
    }

    return size;
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
    return FT_STATUS_OK;
}

ft_status ft_rope_concat(const ft_rope* left, const ft_rope* right, ft_rope* result)
{
    if (left == NULL || right == NULL || result == NULL || left->value_type.size != right->value_type.size) {
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

    result->tree.policy = &result->policy;
    result->tree.rep = rep;
    return FT_STATUS_OK;
}

ft_status ft_rope_insert_at(const ft_rope* rope, size_t index, const void* value, ft_rope* result)
{
    if (rope == NULL || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    if (index > ft_rope_size(rope)) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    ft_rope_split_result split;
    ft_status status = ft_rope_split_at(rope, index, &split);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_rope single;
    status = ft_rope_from_array(&single, &rope->value_type, value, 1);
    if (status != FT_STATUS_OK) {
        ft_rope_dispose(&split.left);
        ft_rope_dispose(&split.right);
        return status;
    }

    ft_rope left_with_item;
    status = ft_rope_concat(&split.left, &single, &left_with_item);
    ft_rope_dispose(&single);
    if (status != FT_STATUS_OK) {
        ft_rope_dispose(&split.left);
        ft_rope_dispose(&split.right);
        return status;
    }

    status = ft_rope_concat(&left_with_item, &split.right, result);
    ft_rope_dispose(&left_with_item);
    ft_rope_dispose(&split.left);
    ft_rope_dispose(&split.right);
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

    ft_rope_split_result first;
    ft_status status = ft_rope_split_at(rope, index, &first);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_rope_split_result second;
    status = ft_rope_split_at(&first.right, 1, &second);
    if (status != FT_STATUS_OK) {
        ft_rope_dispose(&first.left);
        ft_rope_dispose(&first.right);
        return status;
    }

    status = ft_rope_concat(&first.left, &second.right, result);
    ft_rope_dispose(&second.left);
    ft_rope_dispose(&second.right);
    ft_rope_dispose(&first.left);
    ft_rope_dispose(&first.right);
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

static bool ft_measured_rope_count_reaches(const void* measure, void* context)
{
    return *ft_measured_rope_pair_length_const(measure) >= *(const size_t*)context;
}

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

    destination->tree.policy = &destination->policy;
    return FT_STATUS_OK;
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

size_t ft_measured_rope_size(const ft_measured_rope* rope)
{
    if (rope == NULL) {
        return 0;
    }

    void* pair = ft_allocate(rope->policy.measure.size);
    if (pair == NULL) {
        return 0;
    }

    const ft_status status = ft_tree_measure(&rope->tree, pair);
    const size_t result = status == FT_STATUS_OK ? *ft_measured_rope_pair_length_const(pair) : 0;
    free(pair);
    return result;
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

    if (count > ft_measured_rope_size(rope)) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    ft_measure_identity(&rope->user_measure, destination);
    void* element_measure = ft_allocate(rope->user_measure.size);
    void* combined = ft_allocate(rope->user_measure.size);
    if (element_measure == NULL || combined == NULL) {
        free(element_measure);
        free(combined);
        return FT_STATUS_NO_MEMORY;
    }

    for (size_t index = 0; index != count; ++index) {
        void* value = ft_allocate(rope->value_type.size);
        if (value == NULL) {
            free(element_measure);
            free(combined);
            return FT_STATUS_NO_MEMORY;
        }

        ft_status status = ft_measured_rope_at(rope, index, value);
        if (status != FT_STATUS_OK) {
            free(value);
            free(element_measure);
            free(combined);
            return status;
        }

        ft_measure_for_value(&rope->user_measure, element_measure, value);
        ft_measure_combine(&rope->user_measure, combined, destination, element_measure);
        (void)memcpy(destination, combined, rope->user_measure.size);
        ft_value_destroy(&rope->value_type, value);
        free(value);
    }

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
    return FT_STATUS_OK;
}

ft_status ft_measured_rope_concat(
    const ft_measured_rope* left,
    const ft_measured_rope* right,
    ft_measured_rope* result)
{
    if (left == NULL || right == NULL || result == NULL || left->value_type.size != right->value_type.size ||
        left->user_measure.size != right->user_measure.size) {
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

    result->tree.policy = &result->policy;
    result->tree.rep = rep;
    return FT_STATUS_OK;
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

    if (index > ft_measured_rope_size(rope)) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    ft_measured_rope_split_result split;
    ft_status status = ft_measured_rope_split_at(rope, index, &split);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_measured_rope single;
    status = ft_measured_rope_from_array(&single, &rope->value_type, &rope->user_measure, value, 1);
    if (status != FT_STATUS_OK) {
        ft_measured_rope_dispose(&split.left);
        ft_measured_rope_dispose(&split.right);
        return status;
    }

    ft_measured_rope left_with_item;
    status = ft_measured_rope_concat(&split.left, &single, &left_with_item);
    ft_measured_rope_dispose(&single);
    if (status != FT_STATUS_OK) {
        ft_measured_rope_dispose(&split.left);
        ft_measured_rope_dispose(&split.right);
        return status;
    }

    status = ft_measured_rope_concat(&left_with_item, &split.right, result);
    ft_measured_rope_dispose(&left_with_item);
    ft_measured_rope_dispose(&split.left);
    ft_measured_rope_dispose(&split.right);
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

    ft_measured_rope_split_result first;
    ft_status status = ft_measured_rope_split_at(rope, index, &first);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_measured_rope_split_result second;
    status = ft_measured_rope_split_at(&first.right, 1, &second);
    if (status != FT_STATUS_OK) {
        ft_measured_rope_dispose(&first.left);
        ft_measured_rope_dispose(&first.right);
        return status;
    }

    status = ft_measured_rope_concat(&first.left, &second.right, result);
    ft_measured_rope_dispose(&second.left);
    ft_measured_rope_dispose(&second.right);
    ft_measured_rope_dispose(&first.left);
    ft_measured_rope_dispose(&first.right);
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

static ft_status ft_priority_queue_upper_bound(const ft_priority_queue* queue, const ft_priority_entry* value, size_t* index)
{
    ft_priority_entry current;
    size_t lo = 0;
    size_t hi = ft_tree_size(&queue->tree);
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        ft_status status = ft_tree_at(&queue->tree, mid, &current);
        if (status != FT_STATUS_OK) {
            return status;
        }

        const int comparison = ft_priority_entry_compare(queue, value, &current);
        ft_priority_entry_destroy_value(queue->entry_context, &current);
        if (comparison < 0) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }

    *index = lo;
    return FT_STATUS_OK;
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

    destination->tree.policy = &destination->policy;
    destination->next_ordinal = source->next_ordinal;
    return FT_STATUS_OK;
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

    result->tree.policy = &result->policy;
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

    ft_priority_entry entry;
    ft_status status = ft_tree_at(&queue->tree, 0, &entry);
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

    rest->tree.policy = &rest->policy;
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

static bool ft_interval_i64_overlaps(ft_interval_i64 left, ft_interval_i64 right)
{
    return left.low <= right.high && right.low <= left.high;
}

ft_status ft_interval_tree_i64_init(ft_interval_tree_i64* tree)
{
    if (tree == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_value_type value_type;
    ft_value_type_init(&value_type, sizeof(ft_interval_i64));
    ft_tree_policy_init_size(&tree->policy, &value_type);
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

    destination->intervals.tree.policy = &destination->policy;
    return FT_STATUS_OK;
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

ft_status ft_interval_tree_i64_insert(
    const ft_interval_tree_i64* tree,
    ft_interval_i64 interval,
    ft_interval_tree_i64* result)
{
    if (tree == NULL || result == NULL || !ft_interval_i64_valid(interval)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_status status = ft_interval_tree_i64_init(result);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_sorted_multiset_dispose(&result->intervals);
    status = ft_sorted_multiset_add(&tree->intervals, &interval, &result->intervals);
    if (status != FT_STATUS_OK) {
        return status;
    }

    result->intervals.tree.policy = &result->policy;
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

    ft_status status = ft_interval_tree_i64_init(result);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_sorted_multiset_dispose(&result->intervals);
    status = ft_sorted_multiset_remove_one(&tree->intervals, &interval, &result->intervals);
    if (status != FT_STATUS_OK) {
        return status;
    }

    result->intervals.tree.policy = &result->policy;
    return FT_STATUS_OK;
}

bool ft_interval_tree_i64_contains(const ft_interval_tree_i64* tree, ft_interval_i64 interval)
{
    if (tree == NULL || !ft_interval_i64_valid(interval)) {
        return false;
    }

    return ft_sorted_multiset_contains(&tree->intervals, &interval);
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

    *found = false;
    const size_t count = ft_interval_tree_i64_size(tree);
    for (size_t index = 0; index != count; ++index) {
        ft_interval_i64 current;
        ft_status status = ft_sorted_multiset_at(&tree->intervals, index, &current);
        if (status != FT_STATUS_OK) {
            return status;
        }

        if (ft_interval_i64_overlaps(current, query)) {
            if (overlap != NULL) {
                *overlap = current;
            }

            *found = true;
            return FT_STATUS_OK;
        }
    }

    return FT_STATUS_OK;
}

size_t ft_interval_tree_i64_count_overlaps(const ft_interval_tree_i64* tree, ft_interval_i64 query)
{
    if (tree == NULL || !ft_interval_i64_valid(query)) {
        return 0;
    }

    size_t overlaps = 0;
    const size_t count = ft_interval_tree_i64_size(tree);
    for (size_t index = 0; index != count; ++index) {
        ft_interval_i64 current;
        if (ft_sorted_multiset_at(&tree->intervals, index, &current) != FT_STATUS_OK) {
            return overlaps;
        }

        if (ft_interval_i64_overlaps(current, query)) {
            ++overlaps;
        }
    }

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

static int ft_interval_endpoint_compare(const ft_interval_tree_context* context, const void* left, const void* right)
{
    return context->compare_endpoint(left, right, context->compare_context);
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
    ft_size_measure_policy_init(&tree->policy.measure);
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

    destination->intervals.tree.policy = &destination->policy;
    destination->intervals.compare_context = destination->interval_context;
    return FT_STATUS_OK;
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

    ft_interval_entry entry;
    ft_status status = ft_interval_entry_init(tree, low, high, &entry);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_interval_tree_prepare_result(tree, result);
    if (status != FT_STATUS_OK) {
        ft_interval_entry_destroy_value(tree->interval_context, &entry);
        return status;
    }

    status = ft_sorted_multiset_add(&tree->intervals, &entry, &result->intervals);
    ft_interval_entry_destroy_value(tree->interval_context, &entry);
    if (status != FT_STATUS_OK) {
        ft_interval_tree_dispose(result);
        return status;
    }

    result->intervals.tree.policy = &result->policy;
    result->intervals.compare_context = result->interval_context;
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

    ft_interval_entry key;
    key.low = (void*)low;
    key.high = (void*)high;

    ft_status status = ft_interval_tree_prepare_result(tree, result);
    if (status != FT_STATUS_OK) {
        return status;
    }

    status = ft_sorted_multiset_remove_one(&tree->intervals, &key, &result->intervals);
    if (status != FT_STATUS_OK) {
        ft_interval_tree_dispose(result);
        return status;
    }

    result->intervals.tree.policy = &result->policy;
    result->intervals.compare_context = result->interval_context;
    return FT_STATUS_OK;
}

bool ft_interval_tree_contains(const ft_interval_tree* tree, const void* low, const void* high)
{
    if (tree == NULL || low == NULL || high == NULL ||
        !ft_interval_entry_valid(tree->interval_context, low, high)) {
        return false;
    }

    ft_interval_entry key;
    key.low = (void*)low;
    key.high = (void*)high;
    return ft_sorted_multiset_contains(&tree->intervals, &key);
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

    *found = false;
    const size_t count = ft_interval_tree_size(tree);
    for (size_t index = 0; index != count; ++index) {
        ft_interval_entry current;
        ft_status status = ft_sorted_multiset_at(&tree->intervals, index, &current);
        if (status != FT_STATUS_OK) {
            return status;
        }

        if (ft_interval_entry_overlaps(tree->interval_context, &current, query_low, query_high)) {
            if (overlap_low != NULL) {
                ft_value_copy(&tree->endpoint_type, overlap_low, current.low);
            }

            if (overlap_high != NULL) {
                ft_value_copy(&tree->endpoint_type, overlap_high, current.high);
            }

            ft_interval_entry_destroy_value(tree->interval_context, &current);
            *found = true;
            return FT_STATUS_OK;
        }

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

    size_t overlaps = 0;
    const size_t count = ft_interval_tree_size(tree);
    for (size_t index = 0; index != count; ++index) {
        ft_interval_entry current;
        if (ft_sorted_multiset_at(&tree->intervals, index, &current) != FT_STATUS_OK) {
            return overlaps;
        }

        if (ft_interval_entry_overlaps(tree->interval_context, &current, query_low, query_high)) {
            ++overlaps;
        }

        ft_interval_entry_destroy_value(tree->interval_context, &current);
    }

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

static void ft_count_newlines(const void* value, void* context)
{
    const char c = *(const char*)value;
    size_t* count = (size_t*)context;
    if (c == '\n') {
        ++*count;
    }
}

typedef struct ft_line_scan {
    size_t target;
    size_t offset;
    size_t line;
    size_t column;
} ft_line_scan;

static void ft_line_column_visit(const void* value, void* context)
{
    ft_line_scan* scan = (ft_line_scan*)context;
    if (scan->offset >= scan->target) {
        return;
    }

    const char c = *(const char*)value;
    ++scan->offset;
    if (c == '\n') {
        ++scan->line;
        scan->column = 0;
    } else {
        ++scan->column;
    }
}

ft_status ft_text_rope_init(ft_text_rope* rope)
{
    if (rope == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_value_type char_type;
    ft_value_type_init(&char_type, sizeof(char));
    return ft_rope_init(&rope->rope, &char_type);
}

ft_status ft_text_rope_from_cstr(const char* text, ft_text_rope* rope)
{
    if (text == NULL || rope == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_value_type char_type;
    ft_value_type_init(&char_type, sizeof(char));
    return ft_rope_from_array(&rope->rope, &char_type, text, strlen(text));
}

ft_status ft_text_rope_copy(const ft_text_rope* source, ft_text_rope* destination)
{
    if (source == NULL || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    return ft_rope_copy(&source->rope, &destination->rope);
}

void ft_text_rope_dispose(ft_text_rope* rope)
{
    if (rope == NULL) {
        return;
    }

    ft_rope_dispose(&rope->rope);
}

size_t ft_text_rope_size(const ft_text_rope* rope)
{
    return rope == NULL ? 0 : ft_rope_size(&rope->rope);
}

ft_status ft_text_rope_at(const ft_text_rope* rope, size_t index, char* value)
{
    if (rope == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    return ft_rope_at(&rope->rope, index, value);
}

ft_status ft_text_rope_insert_char(const ft_text_rope* rope, size_t index, char value, ft_text_rope* result)
{
    if (rope == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    return ft_rope_insert_at(&rope->rope, index, &value, &result->rope);
}

ft_status ft_text_rope_remove_at(const ft_text_rope* rope, size_t index, ft_text_rope* result)
{
    if (rope == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    return ft_rope_remove_at(&rope->rope, index, &result->rope);
}

size_t ft_text_rope_line_count(const ft_text_rope* rope)
{
    if (rope == NULL) {
        return 0;
    }

    size_t newlines = 0;
    (void)ft_rope_visit(&rope->rope, ft_count_newlines, &newlines);
    return newlines + 1;
}

ft_status ft_text_rope_line_column_of(const ft_text_rope* rope, size_t offset, ft_line_column* result)
{
    if (rope == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    if (offset > ft_text_rope_size(rope)) {
        return FT_STATUS_OUT_OF_RANGE;
    }

    ft_line_scan scan;
    scan.target = offset;
    scan.offset = 0;
    scan.line = 0;
    scan.column = 0;
    ft_status status = ft_rope_visit(&rope->rope, ft_line_column_visit, &scan);
    if (status != FT_STATUS_OK) {
        return status;
    }

    result->line = scan.line;
    result->column = scan.column;
    return FT_STATUS_OK;
}

ft_status ft_text_rope_visit(const ft_text_rope* rope, ft_visit_fn visitor, void* context)
{
    if (rope == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    return ft_rope_visit(&rope->rope, visitor, context);
}
