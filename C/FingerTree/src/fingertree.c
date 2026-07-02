#include <tools/data_structures/finger_tree/fingertree.h>

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
    size_t ref_count;
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
    size_t ref_count;
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
        ++node->ref_count;
    }
}

static void ft_element_dispose(const ft_tree_policy* policy, ft_element* element);

static void ft_node_release(const ft_tree_policy* policy, ft_node* node)
{
    if (node == NULL) {
        return;
    }

    --node->ref_count;
    if (node->ref_count != 0) {
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

    node->ref_count = 1;
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
        ++rep->ref_count;
    }
}

static void ft_rep_release(const ft_tree_policy* policy, ft_tree_rep* rep)
{
    if (rep == NULL) {
        return;
    }

    --rep->ref_count;
    if (rep->ref_count != 0) {
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

    rep->ref_count = 1;
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

    rep->ref_count = 1;
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

    rep->ref_count = 1;
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
    }

    *upper = lo;
    ft_value_destroy(&set->tree.policy->value, current);
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
        ft_priority_entry_destroy_value(entry_context, destination_entry);
        return;
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
    ft_tree_policy_init_size(&rope->policy, &char_type);
    return ft_tree_init(&rope->tree, &rope->policy);
}

ft_status ft_text_rope_from_cstr(const char* text, ft_text_rope* rope)
{
    if (text == NULL || rope == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_status status = ft_text_rope_init(rope);
    if (status != FT_STATUS_OK) {
        return status;
    }

    const size_t length = strlen(text);
    for (size_t index = 0; index != length; ++index) {
        ft_tree next;
        status = ft_tree_push_back(&rope->tree, &text[index], &next);
        if (status != FT_STATUS_OK) {
            ft_text_rope_dispose(rope);
            return status;
        }

        ft_tree_dispose(&rope->tree);
        rope->tree = next;
        rope->tree.policy = &rope->policy;
    }

    return FT_STATUS_OK;
}

ft_status ft_text_rope_copy(const ft_text_rope* source, ft_text_rope* destination)
{
    if (source == NULL || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_value_type char_type;
    ft_value_type_init(&char_type, sizeof(char));
    ft_tree_policy_init_size(&destination->policy, &char_type);

    ft_status status = ft_tree_copy(&source->tree, &destination->tree);
    if (status != FT_STATUS_OK) {
        return status;
    }

    destination->tree.policy = &destination->policy;
    return FT_STATUS_OK;
}

void ft_text_rope_dispose(ft_text_rope* rope)
{
    if (rope == NULL) {
        return;
    }

    ft_tree_dispose(&rope->tree);
}

size_t ft_text_rope_size(const ft_text_rope* rope)
{
    return rope == NULL ? 0 : ft_tree_size(&rope->tree);
}

ft_status ft_text_rope_at(const ft_text_rope* rope, size_t index, char* value)
{
    if (rope == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    return ft_tree_at(&rope->tree, index, value);
}

ft_status ft_text_rope_insert_char(const ft_text_rope* rope, size_t index, char value, ft_text_rope* result)
{
    if (rope == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_status status = ft_text_rope_init(result);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_tree inserted;
    status = ft_tree_insert_at(&rope->tree, index, &value, &inserted);
    if (status != FT_STATUS_OK) {
        ft_text_rope_dispose(result);
        return status;
    }

    ft_tree_dispose(&result->tree);
    result->tree = inserted;
    result->tree.policy = &result->policy;
    return FT_STATUS_OK;
}

ft_status ft_text_rope_remove_at(const ft_text_rope* rope, size_t index, ft_text_rope* result)
{
    if (rope == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }

    ft_status status = ft_text_rope_init(result);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_tree removed;
    status = ft_tree_remove_at(&rope->tree, index, &removed);
    if (status != FT_STATUS_OK) {
        ft_text_rope_dispose(result);
        return status;
    }

    ft_tree_dispose(&result->tree);
    result->tree = removed;
    result->tree.policy = &result->policy;
    return FT_STATUS_OK;
}

size_t ft_text_rope_line_count(const ft_text_rope* rope)
{
    if (rope == NULL) {
        return 0;
    }

    size_t newlines = 0;
    (void)ft_tree_visit(&rope->tree, ft_count_newlines, &newlines);
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
    ft_status status = ft_tree_visit(&rope->tree, ft_line_column_visit, &scan);
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

    return ft_tree_visit(&rope->tree, visitor, context);
}
