/*
 * Implementation of the contextual rank sequence.
 *
 * The whole trick lives in the measure. A stored element caches, once, the machine's effect table:
 * for every incoming state, the outgoing state and the emitted-event count. That table is the leaf
 * measure, so the measured finger tree's monoid never has to call the machine again, and ordered
 * composition is a pure O(s) table walk that feeds the left summary's outgoing state into the right
 * summary.
 *
 * Two contract failures cannot travel through the tree's measure seam, which has no fallible
 * channel, so both are moved out of it. A failing or out-of-contract transition is detected once,
 * when the element is created and its table is filled, before any tree node exists. An event-count
 * overflow is carried inside the measure itself: composition saturates at INT64_MAX and records a
 * sticky flag, which is associative because saturating addition of nonnegative counts is, so the
 * root flag says exactly whether the true total exceeds INT64_MAX for some state regardless of how
 * the tree grouped the additions. Every mutating operation forces the successor's root measure and
 * refuses to publish a flagged one, which is what keeps overflow failure-atomic.
 */

#include <durable7/finger_tree/contextual_rank_sequence.h>

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
typedef volatile LONG64 ft_crs_ref_count;

static void ft_crs_ref_init(ft_crs_ref_count* value)
{
    *value = 1;
}

static void ft_crs_ref_retain(ft_crs_ref_count* value)
{
    (void)InterlockedIncrement64(value);
}

static bool ft_crs_ref_release(ft_crs_ref_count* value)
{
    return InterlockedDecrement64(value) == 0;
}
#else
typedef atomic_size_t ft_crs_ref_count;

static void ft_crs_ref_init(ft_crs_ref_count* value)
{
    atomic_init(value, 1);
}

static void ft_crs_ref_retain(ft_crs_ref_count* value)
{
    (void)atomic_fetch_add_explicit(value, 1, memory_order_relaxed);
}

static bool ft_crs_ref_release(ft_crs_ref_count* value)
{
    return atomic_fetch_sub_explicit(value, 1, memory_order_acq_rel) == 1;
}
#endif

/* The fixed part of a summary blob. The trailing arrays follow it in the same allocation: first
 * state_count emitted-event counts, then state_count outgoing states. The int64_t member keeps the
 * header a multiple of the event array's alignment on every supported target. */
typedef struct ft_crs_measure_head {
    size_t count;
    int64_t overflowed;
} ft_crs_measure_head;

/* One stored element: the caller's representative plus the leaf summary computed from it. */
typedef struct ft_crs_element {
    ft_crs_ref_count refs;
    void* value;
    void* summary;
} ft_crs_element;

struct ft_crs_policy_rep {
    ft_crs_ref_count refs;
    ft_crs_policy_config config;
    size_t measure_size;
    ft_tree_policy tree_policy;
};

typedef struct ft_crs_position_predicate {
    size_t index;
} ft_crs_position_predicate;

typedef struct ft_crs_event_predicate {
    size_t initial_state;
    int64_t event_index;
} ft_crs_event_predicate;

typedef struct ft_crs_visit_state {
    ft_contextual_rank_sequence_visit_fn visitor;
    void* context;
    ft_status status;
} ft_crs_visit_state;

typedef struct ft_crs_audit_state {
    const ft_crs_policy_rep* policy;
    size_t* states;
    int64_t* events;
    size_t count;
    bool consistent;
    ft_status status;
} ft_crs_audit_state;

static void* ft_crs_default_allocate(size_t size, void* context)
{
    (void)context;
    return malloc(size);
}

static void ft_crs_default_deallocate(void* allocation, void* context)
{
    (void)context;
    free(allocation);
}

static void* ft_crs_allocate(const ft_crs_policy_rep* policy, size_t size)
{
    return policy->config.allocator.allocate(size, policy->config.allocator.context);
}

static void ft_crs_deallocate(const ft_crs_policy_rep* policy, void* allocation)
{
    if (allocation != NULL) {
        policy->config.allocator.deallocate(
            allocation, policy->config.allocator.context);
    }
}

static int64_t* ft_crs_measure_events(void* measure)
{
    return (int64_t*)(void*)((unsigned char*)measure + sizeof(ft_crs_measure_head));
}

static const int64_t* ft_crs_measure_events_const(const void* measure)
{
    return (const int64_t*)(const void*)(
        (const unsigned char*)measure + sizeof(ft_crs_measure_head));
}

static size_t* ft_crs_measure_states(void* measure, size_t state_count)
{
    return (size_t*)(void*)((unsigned char*)ft_crs_measure_events(measure) +
        state_count * sizeof(int64_t));
}

static const size_t* ft_crs_measure_states_const(const void* measure, size_t state_count)
{
    return (const size_t*)(const void*)(
        (const unsigned char*)ft_crs_measure_events_const(measure) +
        state_count * sizeof(int64_t));
}

static int64_t ft_crs_add_saturating(int64_t left, int64_t right, bool* overflowed)
{
    if (right > INT64_MAX - left) {
        *overflowed = true;
        return INT64_MAX;
    }
    return left + right;
}

static void ft_crs_measure_identity_hook(void* destination, void* context)
{
    const ft_crs_policy_rep* policy = (const ft_crs_policy_rep*)context;
    ft_crs_measure_head* head = (ft_crs_measure_head*)destination;
    int64_t* events = ft_crs_measure_events(destination);
    size_t* states = ft_crs_measure_states(destination, policy->config.state_count);
    size_t state = 0;
    head->count = 0;
    head->overflowed = 0;
    for (state = 0; state != policy->config.state_count; ++state) {
        events[state] = 0;
        states[state] = state;
    }
}

static void ft_crs_measure_leaf_hook(void* destination, const void* value, void* context)
{
    const ft_crs_policy_rep* policy = (const ft_crs_policy_rep*)context;
    const ft_crs_element* element = *(ft_crs_element* const*)value;
    (void)memcpy(destination, element->summary, policy->measure_size);
}

static void ft_crs_measure_combine_hook(
    void* destination,
    const void* left,
    const void* right,
    void* context)
{
    const ft_crs_policy_rep* policy = (const ft_crs_policy_rep*)context;
    const size_t state_count = policy->config.state_count;
    const size_t left_count = ((const ft_crs_measure_head*)left)->count;
    const size_t right_count = ((const ft_crs_measure_head*)right)->count;
    const int64_t* left_events = NULL;
    const size_t* left_states = NULL;
    const int64_t* right_events = NULL;
    const size_t* right_states = NULL;
    int64_t* events = NULL;
    size_t* states = NULL;
    ft_crs_measure_head* head = NULL;
    size_t count = 0;
    size_t state = 0;
    bool overflowed = false;

    /* Only the identity holds no elements, so short-circuiting it is exact, not an approximation. */
    if (left_count == 0) {
        (void)memcpy(destination, right, policy->measure_size);
        return;
    }
    if (right_count == 0) {
        (void)memcpy(destination, left, policy->measure_size);
        return;
    }

    overflowed = ((const ft_crs_measure_head*)left)->overflowed != 0 ||
        ((const ft_crs_measure_head*)right)->overflowed != 0;
    left_events = ft_crs_measure_events_const(left);
    left_states = ft_crs_measure_states_const(left, state_count);
    right_events = ft_crs_measure_events_const(right);
    right_states = ft_crs_measure_states_const(right, state_count);
    events = ft_crs_measure_events(destination);
    states = ft_crs_measure_states(destination, state_count);
    for (state = 0; state != state_count; ++state) {
        const size_t middle = left_states[state];
        events[state] = ft_crs_add_saturating(
            left_events[state], right_events[middle], &overflowed);
        states[state] = right_states[middle];
    }

    if (right_count > SIZE_MAX - left_count) {
        overflowed = true;
        count = SIZE_MAX;
    } else {
        count = left_count + right_count;
    }
    head = (ft_crs_measure_head*)destination;
    head->count = count;
    head->overflowed = overflowed ? 1 : 0;
}

static void ft_crs_element_retain(ft_crs_element* element)
{
    if (element != NULL) {
        ft_crs_ref_retain(&element->refs);
    }
}

static void ft_crs_element_release(
    const ft_crs_policy_rep* policy,
    ft_crs_element* element)
{
    if (element == NULL || !ft_crs_ref_release(&element->refs)) {
        return;
    }
    if (policy->config.destroy != NULL) {
        policy->config.destroy(element->value, policy->config.callback_context);
    }
    ft_crs_deallocate(policy, element->summary);
    ft_crs_deallocate(policy, element->value);
    ft_crs_deallocate(policy, element);
}

static void ft_crs_value_copy_hook(void* destination, const void* source, void* context)
{
    ft_crs_element* element = *(ft_crs_element* const*)source;
    (void)context;
    ft_crs_element_retain(element);
    *(ft_crs_element**)destination = element;
}

static void ft_crs_value_destroy_hook(void* value, void* context)
{
    ft_crs_element_release(
        (const ft_crs_policy_rep*)context, *(ft_crs_element**)value);
}

/* Builds one stored element, running the machine once from every incoming state so that the leaf
 * summary is complete before any tree node can observe it. */
static ft_status ft_crs_element_create(
    const ft_crs_policy_rep* policy,
    const void* value,
    ft_crs_element** result)
{
    const ft_crs_policy_config* config = &policy->config;
    ft_crs_element* element = NULL;
    ft_crs_measure_head* head = NULL;
    int64_t* events = NULL;
    size_t* states = NULL;
    size_t state = 0;
    bool constructed = false;
    ft_status status = FT_STATUS_OK;

    element = (ft_crs_element*)ft_crs_allocate(policy, sizeof(*element));
    if (element == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    element->value = ft_crs_allocate(policy, config->value_size);
    element->summary = ft_crs_allocate(policy, policy->measure_size);
    if (element->value == NULL || element->summary == NULL) {
        ft_crs_deallocate(policy, element->summary);
        ft_crs_deallocate(policy, element->value);
        ft_crs_deallocate(policy, element);
        return FT_STATUS_NO_MEMORY;
    }

    if (config->copy == NULL) {
        (void)memcpy(element->value, value, config->value_size);
    } else {
        status = config->copy(element->value, value, config->callback_context);
    }
    constructed = status == FT_STATUS_OK;

    head = (ft_crs_measure_head*)element->summary;
    events = ft_crs_measure_events(element->summary);
    states = ft_crs_measure_states(element->summary, config->state_count);
    for (state = 0; status == FT_STATUS_OK && state != config->state_count; ++state) {
        size_t next_state = 0;
        int64_t emitted = 0;
        status = config->transition(
            state, element->value, &next_state, &emitted, config->callback_context);
        if (status != FT_STATUS_OK) {
            break;
        }
        if (next_state >= config->state_count || emitted < 0) {
            status = FT_STATUS_INCONSISTENT_POLICY;
            break;
        }
        events[state] = emitted;
        states[state] = next_state;
    }

    if (status != FT_STATUS_OK) {
        if (constructed && config->destroy != NULL) {
            config->destroy(element->value, config->callback_context);
        }
        ft_crs_deallocate(policy, element->summary);
        ft_crs_deallocate(policy, element->value);
        ft_crs_deallocate(policy, element);
        return status;
    }

    head->count = 1;
    head->overflowed = 0;
    ft_crs_ref_init(&element->refs);
    *result = element;
    return FT_STATUS_OK;
}

static void ft_crs_policy_retain(ft_crs_policy_rep* policy)
{
    if (policy != NULL) {
        ft_crs_ref_retain(&policy->refs);
    }
}

static void ft_crs_policy_release(ft_crs_policy_rep* policy)
{
    ft_crs_policy_config config = {0};
    if (policy == NULL || !ft_crs_ref_release(&policy->refs)) {
        return;
    }
    config = policy->config;
    config.allocator.deallocate(policy, config.allocator.context);
}

static bool ft_crs_config_valid(const ft_crs_policy_config* config)
{
    return config != NULL && config->value_size != 0 &&
        config->value_type_identity != NULL && config->state_count != 0 &&
        config->transition != NULL &&
        (config->destroy == NULL || config->copy != NULL) &&
        config->allocator.allocate != NULL && config->allocator.deallocate != NULL;
}

static bool ft_crs_sequence_valid(const ft_contextual_rank_sequence* sequence)
{
    return sequence != NULL && sequence->policy != NULL && sequence->root != NULL;
}

/* Rebuilds the borrowed measured-tree handle a sequence version stands for. The policy is embedded
 * in the reference-counted policy representation, so its address is stable and identical for every
 * sequence built under the same policy, which is what the underlying concatenation requires. */
static ft_tree ft_crs_tree_of(const ft_contextual_rank_sequence* sequence)
{
    ft_tree tree = {0};
    tree.policy = &sequence->policy->tree_policy;
    tree.rep = sequence->root;
    return tree;
}

static void ft_crs_adopt(
    ft_crs_policy_rep* policy,
    ft_tree* owned,
    ft_contextual_rank_sequence* produced)
{
    ft_crs_policy_retain(policy);
    produced->policy = policy;
    produced->root = owned->rep;
    owned->policy = NULL;
    owned->rep = NULL;
}

/* Forces the successor's root summary and refuses to publish one whose event totals saturated. */
static ft_status ft_crs_publish_tree(
    ft_crs_policy_rep* policy,
    ft_tree* owned,
    ft_contextual_rank_sequence* produced)
{
    void* scratch = ft_crs_allocate(policy, policy->measure_size);
    ft_status status = FT_STATUS_OK;
    if (scratch == NULL) {
        ft_tree_dispose(owned);
        return FT_STATUS_NO_MEMORY;
    }
    status = ft_tree_measure(owned, scratch);
    if (status == FT_STATUS_OK &&
        ((const ft_crs_measure_head*)scratch)->overflowed != 0) {
        status = FT_STATUS_OVERFLOW;
    }
    ft_crs_deallocate(policy, scratch);
    if (status != FT_STATUS_OK) {
        ft_tree_dispose(owned);
        return status;
    }
    ft_crs_adopt(policy, owned, produced);
    return FT_STATUS_OK;
}

static ft_status ft_crs_retain_handle(
    const ft_contextual_rank_sequence* source,
    ft_contextual_rank_sequence* produced)
{
    ft_tree tree = ft_crs_tree_of(source);
    ft_tree retained;
    ft_status status = ft_tree_copy(&tree, &retained);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_crs_adopt(source->policy, &retained, produced);
    return FT_STATUS_OK;
}

static void ft_crs_publish_unary(
    const ft_contextual_rank_sequence* source,
    ft_contextual_rank_sequence* result,
    ft_contextual_rank_sequence produced)
{
    if (source == result) {
        ft_contextual_rank_sequence old = *result;
        *result = produced;
        ft_contextual_rank_sequence_dispose(&old);
    } else {
        *result = produced;
    }
}

static void ft_crs_publish_binary(
    const ft_contextual_rank_sequence* left,
    const ft_contextual_rank_sequence* right,
    ft_contextual_rank_sequence* result,
    ft_contextual_rank_sequence produced)
{
    if (result == left || result == right) {
        ft_contextual_rank_sequence old = *result;
        *result = produced;
        ft_contextual_rank_sequence_dispose(&old);
    } else {
        *result = produced;
    }
}

static bool ft_crs_position_satisfied(const void* measure, void* context)
{
    const ft_crs_position_predicate* predicate =
        (const ft_crs_position_predicate*)context;
    return ((const ft_crs_measure_head*)measure)->count > predicate->index;
}

static bool ft_crs_event_satisfied(const void* measure, void* context)
{
    const ft_crs_event_predicate* predicate = (const ft_crs_event_predicate*)context;
    return ft_crs_measure_events_const(measure)[predicate->initial_state] >
        predicate->event_index;
}

static void ft_crs_visit_trampoline(const void* value, void* context)
{
    ft_crs_visit_state* state = (ft_crs_visit_state*)context;
    const ft_crs_element* element = *(ft_crs_element* const*)value;
    if (state->status != FT_STATUS_OK) {
        return;
    }
    state->status = state->visitor(element->value, state->context);
}

static void ft_crs_audit_visit(const void* value, void* context)
{
    ft_crs_audit_state* audit = (ft_crs_audit_state*)context;
    const ft_crs_policy_config* config = &audit->policy->config;
    const ft_crs_element* element = *(ft_crs_element* const*)value;
    size_t state = 0;
    if (audit->status != FT_STATUS_OK || !audit->consistent) {
        return;
    }
    for (state = 0; state != config->state_count; ++state) {
        size_t next_state = 0;
        int64_t emitted = 0;
        ft_status status = config->transition(
            audit->states[state],
            element->value,
            &next_state,
            &emitted,
            config->callback_context);
        if (status != FT_STATUS_OK) {
            audit->status = status;
            return;
        }
        if (next_state >= config->state_count || emitted < 0 ||
            emitted > INT64_MAX - audit->events[state]) {
            audit->consistent = false;
            return;
        }
        audit->events[state] += emitted;
        audit->states[state] = next_state;
    }
    audit->count += 1;
}

void ft_crs_policy_config_init(
    ft_crs_policy_config* config,
    size_t value_size,
    const void* value_type_identity,
    size_t state_count,
    ft_crs_transition_fn transition,
    void* callback_context)
{
    if (config == NULL) {
        return;
    }
    (void)memset(config, 0, sizeof(*config));
    config->value_size = value_size;
    config->value_type_identity = value_type_identity;
    config->state_count = state_count;
    config->transition = transition;
    config->callback_context = callback_context;
    config->allocator.allocate = ft_crs_default_allocate;
    config->allocator.deallocate = ft_crs_default_deallocate;
}

ft_status ft_crs_policy_create(
    ft_crs_policy* policy,
    const ft_crs_policy_config* config)
{
    const size_t per_state = sizeof(int64_t) + sizeof(size_t);
    ft_crs_policy_rep* rep = NULL;
    if (policy == NULL || !ft_crs_config_valid(config)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (config->state_count >
        (SIZE_MAX - sizeof(ft_crs_measure_head)) / per_state) {
        return FT_STATUS_OVERFLOW;
    }
    rep = (ft_crs_policy_rep*)config->allocator.allocate(
        sizeof(*rep), config->allocator.context);
    if (rep == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    ft_crs_ref_init(&rep->refs);
    rep->config = *config;
    rep->measure_size = sizeof(ft_crs_measure_head) + config->state_count * per_state;
    (void)memset(&rep->tree_policy, 0, sizeof(rep->tree_policy));
    rep->tree_policy.value.size = sizeof(ft_crs_element*);
    rep->tree_policy.value.copy = ft_crs_value_copy_hook;
    rep->tree_policy.value.destroy = ft_crs_value_destroy_hook;
    rep->tree_policy.value.context = rep;
    rep->tree_policy.measure.size = rep->measure_size;
    rep->tree_policy.measure.identity = ft_crs_measure_identity_hook;
    rep->tree_policy.measure.measure = ft_crs_measure_leaf_hook;
    rep->tree_policy.measure.combine = ft_crs_measure_combine_hook;
    rep->tree_policy.measure.context = rep;
    policy->rep = rep;
    return FT_STATUS_OK;
}

ft_status ft_crs_policy_copy(
    const ft_crs_policy* source,
    ft_crs_policy* destination)
{
    if (source == NULL || source->rep == NULL || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return FT_STATUS_OK;
    }
    ft_crs_policy_retain(source->rep);
    destination->rep = source->rep;
    return FT_STATUS_OK;
}

void ft_crs_policy_move(ft_crs_policy* destination, ft_crs_policy* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    destination->rep = source->rep;
    source->rep = NULL;
}

void ft_crs_policy_dispose(ft_crs_policy* policy)
{
    if (policy != NULL) {
        ft_crs_policy_release(policy->rep);
        policy->rep = NULL;
    }
}

bool ft_crs_policy_same(const ft_crs_policy* left, const ft_crs_policy* right)
{
    return left != NULL && right != NULL && left->rep != NULL &&
        left->rep == right->rep;
}

size_t ft_crs_policy_state_count(const ft_crs_policy* policy)
{
    if (policy == NULL || policy->rep == NULL) {
        return 0;
    }
    return policy->rep->config.state_count;
}

ft_status ft_contextual_rank_sequence_init(
    ft_contextual_rank_sequence* sequence,
    const ft_crs_policy* policy)
{
    ft_tree tree = {0};
    ft_status status = FT_STATUS_OK;
    if (sequence == NULL || policy == NULL || policy->rep == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_tree_init(&tree, &policy->rep->tree_policy);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_crs_adopt(policy->rep, &tree, sequence);
    return FT_STATUS_OK;
}

ft_status ft_contextual_rank_sequence_from_array(
    ft_contextual_rank_sequence* sequence,
    const ft_crs_policy* policy,
    const void* values,
    size_t count)
{
    ft_contextual_rank_sequence built = {0};
    size_t index = 0;
    ft_status status = FT_STATUS_OK;
    if (sequence == NULL || policy == NULL || policy->rep == NULL ||
        (values == NULL && count != 0)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_contextual_rank_sequence_init(&built, policy);
    if (status != FT_STATUS_OK) {
        return status;
    }
    for (index = 0; status == FT_STATUS_OK && index != count; ++index) {
        const void* value = (const void*)((const unsigned char*)values +
            index * policy->rep->config.value_size);
        status = ft_contextual_rank_sequence_push_back(&built, value, &built);
    }
    if (status != FT_STATUS_OK) {
        ft_contextual_rank_sequence_dispose(&built);
        return status;
    }
    *sequence = built;
    return FT_STATUS_OK;
}

ft_status ft_contextual_rank_sequence_copy(
    const ft_contextual_rank_sequence* source,
    ft_contextual_rank_sequence* destination)
{
    if (!ft_crs_sequence_valid(source) || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return FT_STATUS_OK;
    }
    return ft_crs_retain_handle(source, destination);
}

void ft_contextual_rank_sequence_move(
    ft_contextual_rank_sequence* destination,
    ft_contextual_rank_sequence* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    destination->policy = source->policy;
    destination->root = source->root;
    source->policy = NULL;
    source->root = NULL;
}

void ft_contextual_rank_sequence_dispose(ft_contextual_rank_sequence* sequence)
{
    ft_crs_policy_rep* policy = NULL;
    if (sequence == NULL || sequence->policy == NULL) {
        return;
    }
    policy = sequence->policy;
    if (sequence->root != NULL) {
        ft_tree tree = ft_crs_tree_of(sequence);
        ft_tree_dispose(&tree);
    }
    sequence->policy = NULL;
    sequence->root = NULL;
    ft_crs_policy_release(policy);
}

bool ft_contextual_rank_sequence_empty(const ft_contextual_rank_sequence* sequence)
{
    ft_tree tree = {0};
    if (!ft_crs_sequence_valid(sequence)) {
        return true;
    }
    tree = ft_crs_tree_of(sequence);
    return ft_tree_empty(&tree);
}

size_t ft_contextual_rank_sequence_size(const ft_contextual_rank_sequence* sequence)
{
    ft_tree tree = {0};
    if (!ft_crs_sequence_valid(sequence)) {
        return 0;
    }
    tree = ft_crs_tree_of(sequence);
    return ft_tree_size(&tree);
}

size_t ft_contextual_rank_sequence_state_count(
    const ft_contextual_rank_sequence* sequence)
{
    if (sequence == NULL || sequence->policy == NULL) {
        return 0;
    }
    return sequence->policy->config.state_count;
}

static ft_status ft_crs_locate_element(
    const ft_contextual_rank_sequence* sequence,
    size_t index,
    ft_crs_element** located)
{
    ft_tree tree = {0};
    ft_crs_element* found = NULL;
    ft_status status = FT_STATUS_OK;
    if (index >= ft_contextual_rank_sequence_size(sequence)) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    tree = ft_crs_tree_of(sequence);
    status = ft_tree_at(&tree, index, &found);
    if (status != FT_STATUS_OK) {
        return status;
    }
    *located = found;
    return FT_STATUS_OK;
}

ft_status ft_contextual_rank_sequence_at_ref(
    const ft_contextual_rank_sequence* sequence,
    size_t index,
    const void** element_ref)
{
    ft_crs_element* located = NULL;
    ft_status status = FT_STATUS_OK;
    if (!ft_crs_sequence_valid(sequence) || element_ref == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_crs_locate_element(sequence, index, &located);
    if (status != FT_STATUS_OK) {
        return status;
    }
    *element_ref = located->value;
    ft_crs_element_release(sequence->policy, located);
    return FT_STATUS_OK;
}

ft_status ft_contextual_rank_sequence_at_copy(
    const ft_contextual_rank_sequence* sequence,
    size_t index,
    void* element)
{
    const ft_crs_policy_config* config = NULL;
    ft_crs_element* located = NULL;
    ft_status status = FT_STATUS_OK;
    if (!ft_crs_sequence_valid(sequence) || element == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_crs_locate_element(sequence, index, &located);
    if (status != FT_STATUS_OK) {
        return status;
    }
    config = &sequence->policy->config;
    if (config->copy == NULL) {
        (void)memcpy(element, located->value, config->value_size);
    } else {
        status = config->copy(element, located->value, config->callback_context);
    }
    ft_crs_element_release(sequence->policy, located);
    return status;
}

ft_status ft_contextual_rank_sequence_evaluate(
    const ft_contextual_rank_sequence* sequence,
    size_t initial_state,
    ft_contextual_prefix_summary* summary)
{
    ft_tree tree = {0};
    void* scratch = NULL;
    ft_status status = FT_STATUS_OK;
    if (!ft_crs_sequence_valid(sequence) || summary == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (initial_state >= sequence->policy->config.state_count) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    scratch = ft_crs_allocate(sequence->policy, sequence->policy->measure_size);
    if (scratch == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    tree = ft_crs_tree_of(sequence);
    status = ft_tree_measure(&tree, scratch);
    if (status == FT_STATUS_OK) {
        summary->final_state = ft_crs_measure_states_const(
            scratch, sequence->policy->config.state_count)[initial_state];
        summary->event_count = ft_crs_measure_events_const(scratch)[initial_state];
    }
    ft_crs_deallocate(sequence->policy, scratch);
    return status;
}

ft_status ft_contextual_rank_sequence_evaluate_prefix(
    const ft_contextual_rank_sequence* sequence,
    size_t element_count,
    size_t initial_state,
    ft_contextual_prefix_summary* summary)
{
    ft_crs_position_predicate predicate = {0};
    ft_tree tree = {0};
    void* scratch = NULL;
    bool found = false;
    ft_status status = FT_STATUS_OK;
    if (!ft_crs_sequence_valid(sequence) || summary == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (initial_state >= sequence->policy->config.state_count ||
        element_count > ft_contextual_rank_sequence_size(sequence)) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    if (element_count == 0) {
        summary->final_state = initial_state;
        summary->event_count = 0;
        return FT_STATUS_OK;
    }
    if (element_count == ft_contextual_rank_sequence_size(sequence)) {
        return ft_contextual_rank_sequence_evaluate(sequence, initial_state, summary);
    }
    scratch = ft_crs_allocate(sequence->policy, sequence->policy->measure_size);
    if (scratch == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    predicate.index = element_count;
    tree = ft_crs_tree_of(sequence);
    status = ft_tree_locate(
        &tree, ft_crs_position_satisfied, &predicate, &found, scratch, NULL);
    if (status == FT_STATUS_OK && !found) {
        status = FT_STATUS_INCONSISTENT_POLICY;
    }
    if (status == FT_STATUS_OK) {
        summary->final_state = ft_crs_measure_states_const(
            scratch, sequence->policy->config.state_count)[initial_state];
        summary->event_count = ft_crs_measure_events_const(scratch)[initial_state];
    }
    ft_crs_deallocate(sequence->policy, scratch);
    return status;
}

ft_status ft_contextual_rank_sequence_event_rank(
    const ft_contextual_rank_sequence* sequence,
    size_t element_count,
    size_t initial_state,
    int64_t* event_rank)
{
    ft_contextual_prefix_summary summary = {0};
    ft_status status = FT_STATUS_OK;
    if (event_rank == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_contextual_rank_sequence_evaluate_prefix(
        sequence, element_count, initial_state, &summary);
    if (status == FT_STATUS_OK) {
        *event_rank = summary.event_count;
    }
    return status;
}

ft_status ft_contextual_rank_sequence_try_select_event(
    const ft_contextual_rank_sequence* sequence,
    int64_t event_index,
    size_t initial_state,
    bool* found,
    ft_contextual_event_location* location)
{
    ft_crs_event_predicate predicate = {0};
    ft_contextual_event_location produced = {0};
    ft_tree tree = {0};
    void* scratch = NULL;
    ft_crs_element* located = NULL;
    const ft_crs_policy_rep* policy = NULL;
    size_t state_count = 0;
    int64_t total = 0;
    bool hit = false;
    ft_status status = FT_STATUS_OK;

    if (!ft_crs_sequence_valid(sequence) || found == NULL || location == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    policy = sequence->policy;
    state_count = policy->config.state_count;
    if (initial_state >= state_count) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    scratch = ft_crs_allocate(policy, policy->measure_size);
    if (scratch == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    tree = ft_crs_tree_of(sequence);
    status = ft_tree_measure(&tree, scratch);
    if (status == FT_STATUS_OK) {
        total = ft_crs_measure_events_const(scratch)[initial_state];
    }
    if (status == FT_STATUS_OK && (event_index < 0 || event_index >= total)) {
        ft_crs_deallocate(policy, scratch);
        *found = false;
        return FT_STATUS_OK;
    }

    if (status == FT_STATUS_OK) {
        predicate.initial_state = initial_state;
        predicate.event_index = event_index;
        status = ft_tree_locate(
            &tree, ft_crs_event_satisfied, &predicate, &hit, scratch, &located);
    }
    if (status == FT_STATUS_OK && (!hit || located == NULL)) {
        status = FT_STATUS_INCONSISTENT_POLICY;
    }
    if (status == FT_STATUS_OK) {
        const size_t state_before =
            ft_crs_measure_states_const(scratch, state_count)[initial_state];
        produced.element_index = ((const ft_crs_measure_head*)scratch)->count;
        produced.event_index_in_element =
            event_index - ft_crs_measure_events_const(scratch)[initial_state];
        produced.state_before = state_before;
        produced.state_after =
            ft_crs_measure_states_const(located->summary, state_count)[state_before];
        produced.element_event_count =
            ft_crs_measure_events_const(located->summary)[state_before];
    }
    ft_crs_element_release(policy, located);
    ft_crs_deallocate(policy, scratch);
    if (status != FT_STATUS_OK) {
        return status;
    }
    *location = produced;
    *found = true;
    return FT_STATUS_OK;
}

/* Every insertion path shares one capacity guard, so a full sequence reports the same failure
 * whichever entry point reached it. */
static ft_status ft_crs_check_insertion_capacity(
    const ft_contextual_rank_sequence* sequence)
{
    return ft_contextual_rank_sequence_size(sequence) == SIZE_MAX
        ? FT_STATUS_OVERFLOW
        : FT_STATUS_OK;
}

static ft_status ft_crs_push(
    const ft_contextual_rank_sequence* sequence,
    const void* element,
    bool at_front,
    ft_contextual_rank_sequence* result)
{
    ft_contextual_rank_sequence produced = {0};
    ft_crs_element* created = NULL;
    ft_tree tree = {0};
    ft_tree grown = {0};
    ft_status status = FT_STATUS_OK;
    if (!ft_crs_sequence_valid(sequence) || element == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_crs_check_insertion_capacity(sequence);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_crs_element_create(sequence->policy, element, &created);
    if (status != FT_STATUS_OK) {
        return status;
    }
    tree = ft_crs_tree_of(sequence);
    status = at_front
        ? ft_tree_push_front(&tree, &created, &grown)
        : ft_tree_push_back(&tree, &created, &grown);
    ft_crs_element_release(sequence->policy, created);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_crs_publish_tree(sequence->policy, &grown, &produced);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_crs_publish_unary(sequence, result, produced);
    return FT_STATUS_OK;
}

ft_status ft_contextual_rank_sequence_push_front(
    const ft_contextual_rank_sequence* sequence,
    const void* element,
    ft_contextual_rank_sequence* result)
{
    return ft_crs_push(sequence, element, true, result);
}

ft_status ft_contextual_rank_sequence_push_back(
    const ft_contextual_rank_sequence* sequence,
    const void* element,
    ft_contextual_rank_sequence* result)
{
    return ft_crs_push(sequence, element, false, result);
}

ft_status ft_contextual_rank_sequence_concat(
    const ft_contextual_rank_sequence* left,
    const ft_contextual_rank_sequence* right,
    ft_contextual_rank_sequence* result)
{
    ft_contextual_rank_sequence produced = {0};
    ft_tree left_tree = {0};
    ft_tree right_tree = {0};
    ft_tree joined = {0};
    size_t left_size = 0;
    size_t right_size = 0;
    ft_status status = FT_STATUS_OK;
    if (!ft_crs_sequence_valid(left) || !ft_crs_sequence_valid(right) ||
        result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (left->policy != right->policy) {
        return FT_STATUS_INCOMPATIBLE_POLICY;
    }
    left_size = ft_contextual_rank_sequence_size(left);
    right_size = ft_contextual_rank_sequence_size(right);
    if (right_size > SIZE_MAX - left_size) {
        return FT_STATUS_OVERFLOW;
    }
    if (right_size == 0) {
        status = ft_crs_retain_handle(left, &produced);
    } else if (left_size == 0) {
        status = ft_crs_retain_handle(right, &produced);
    } else {
        left_tree = ft_crs_tree_of(left);
        right_tree = ft_crs_tree_of(right);
        status = ft_tree_concat(&left_tree, &right_tree, &joined);
        if (status == FT_STATUS_OK) {
            status = ft_crs_publish_tree(left->policy, &joined, &produced);
        }
    }
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_crs_publish_binary(left, right, result, produced);
    return FT_STATUS_OK;
}

ft_status ft_contextual_rank_sequence_insert_at(
    const ft_contextual_rank_sequence* sequence,
    size_t index,
    const void* element,
    ft_contextual_rank_sequence* result)
{
    ft_contextual_rank_sequence produced = {0};
    ft_crs_element* created = NULL;
    ft_tree tree = {0};
    ft_tree grown = {0};
    size_t size = 0;
    ft_status status = FT_STATUS_OK;
    if (!ft_crs_sequence_valid(sequence) || element == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    size = ft_contextual_rank_sequence_size(sequence);
    if (index > size) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    status = ft_crs_check_insertion_capacity(sequence);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (index == 0) {
        return ft_crs_push(sequence, element, true, result);
    }
    if (index == size) {
        return ft_crs_push(sequence, element, false, result);
    }
    status = ft_crs_element_create(sequence->policy, element, &created);
    if (status != FT_STATUS_OK) {
        return status;
    }
    tree = ft_crs_tree_of(sequence);
    status = ft_tree_insert_at(&tree, index, &created, &grown);
    ft_crs_element_release(sequence->policy, created);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_crs_publish_tree(sequence->policy, &grown, &produced);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_crs_publish_unary(sequence, result, produced);
    return FT_STATUS_OK;
}

ft_status ft_contextual_rank_sequence_set_at(
    const ft_contextual_rank_sequence* sequence,
    size_t index,
    const void* element,
    ft_contextual_rank_sequence* result)
{
    ft_contextual_rank_sequence produced = {0};
    ft_crs_element* created = NULL;
    ft_tree tree = {0};
    ft_tree replaced = {0};
    ft_status status = FT_STATUS_OK;
    if (!ft_crs_sequence_valid(sequence) || element == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (index >= ft_contextual_rank_sequence_size(sequence)) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    status = ft_crs_element_create(sequence->policy, element, &created);
    if (status != FT_STATUS_OK) {
        return status;
    }
    tree = ft_crs_tree_of(sequence);
    status = ft_tree_set_at(&tree, index, &created, &replaced);
    ft_crs_element_release(sequence->policy, created);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_crs_publish_tree(sequence->policy, &replaced, &produced);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_crs_publish_unary(sequence, result, produced);
    return FT_STATUS_OK;
}

ft_status ft_contextual_rank_sequence_remove_at(
    const ft_contextual_rank_sequence* sequence,
    size_t index,
    ft_contextual_rank_sequence* result)
{
    ft_contextual_rank_sequence produced = {0};
    ft_tree tree = {0};
    ft_tree shrunk = {0};
    ft_status status = FT_STATUS_OK;
    if (!ft_crs_sequence_valid(sequence) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (index >= ft_contextual_rank_sequence_size(sequence)) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    tree = ft_crs_tree_of(sequence);
    status = ft_tree_remove_at(&tree, index, &shrunk);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_crs_publish_tree(sequence->policy, &shrunk, &produced);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_crs_publish_unary(sequence, result, produced);
    return FT_STATUS_OK;
}

ft_status ft_contextual_rank_sequence_split_at(
    const ft_contextual_rank_sequence* sequence,
    size_t index,
    ft_contextual_rank_sequence_split_result* result)
{
    ft_contextual_rank_sequence produced_left = {0};
    ft_contextual_rank_sequence produced_right = {0};
    ft_contextual_rank_sequence old = {0};
    ft_tree_split_result split = {0};
    ft_tree tree = {0};
    bool aliases = false;
    ft_status status = FT_STATUS_OK;
    if (!ft_crs_sequence_valid(sequence) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (index > ft_contextual_rank_sequence_size(sequence)) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    tree = ft_crs_tree_of(sequence);
    status = ft_tree_split_at(&tree, index, &split);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_crs_publish_tree(sequence->policy, &split.left, &produced_left);
    if (status != FT_STATUS_OK) {
        ft_tree_dispose(&split.right);
        return status;
    }
    status = ft_crs_publish_tree(sequence->policy, &split.right, &produced_right);
    if (status != FT_STATUS_OK) {
        ft_contextual_rank_sequence_dispose(&produced_left);
        return status;
    }
    aliases = sequence == &result->left || sequence == &result->right;
    if (aliases) {
        old = *sequence;
    }
    result->left = produced_left;
    result->right = produced_right;
    if (aliases) {
        ft_contextual_rank_sequence_dispose(&old);
    }
    return FT_STATUS_OK;
}

ft_status ft_contextual_rank_sequence_get_range(
    const ft_contextual_rank_sequence* sequence,
    size_t index,
    size_t count,
    ft_contextual_rank_sequence* result)
{
    ft_contextual_rank_sequence produced = {0};
    ft_tree_split_result head = {0};
    ft_tree_split_result tail = {0};
    ft_tree tree = {0};
    size_t size = 0;
    ft_status status = FT_STATUS_OK;
    if (!ft_crs_sequence_valid(sequence) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    size = ft_contextual_rank_sequence_size(sequence);
    if (index > size || count > size - index) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    if (count == size) {
        status = ft_crs_retain_handle(sequence, &produced);
    } else if (count == 0) {
        ft_tree empty = {0};
        status = ft_tree_init(&empty, &sequence->policy->tree_policy);
        if (status == FT_STATUS_OK) {
            ft_crs_adopt(sequence->policy, &empty, &produced);
        }
    } else {
        tree = ft_crs_tree_of(sequence);
        status = ft_tree_split_at(&tree, index, &head);
        if (status == FT_STATUS_OK) {
            status = ft_tree_split_at(&head.right, count, &tail);
            if (status == FT_STATUS_OK) {
                ft_tree_dispose(&tail.right);
                status = ft_crs_publish_tree(sequence->policy, &tail.left, &produced);
            }
            ft_tree_dispose(&head.right);
            ft_tree_dispose(&head.left);
        }
    }
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_crs_publish_unary(sequence, result, produced);
    return FT_STATUS_OK;
}

ft_status ft_contextual_rank_sequence_visit(
    const ft_contextual_rank_sequence* sequence,
    ft_contextual_rank_sequence_visit_fn visitor,
    void* context)
{
    ft_crs_visit_state state;
    ft_tree tree = {0};
    ft_status status = FT_STATUS_OK;
    if (!ft_crs_sequence_valid(sequence) || visitor == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    state.visitor = visitor;
    state.context = context;
    state.status = FT_STATUS_OK;
    tree = ft_crs_tree_of(sequence);
    status = ft_tree_visit(&tree, ft_crs_visit_trampoline, &state);
    return status == FT_STATUS_OK ? state.status : status;
}

const void* ft_contextual_rank_sequence_root_identity(
    const ft_contextual_rank_sequence* sequence)
{
    return ft_crs_sequence_valid(sequence) ? (const void*)sequence->root : NULL;
}

ft_status ft_contextual_rank_sequence_validate(
    const ft_contextual_rank_sequence* sequence,
    bool* valid,
    ft_contextual_rank_sequence_statistics* statistics)
{
    ft_crs_audit_state audit;
    ft_contextual_rank_sequence_statistics produced = {0};
    ft_tree tree = {0};
    const ft_crs_policy_rep* policy = NULL;
    void* scratch = NULL;
    size_t state_count = 0;
    size_t state = 0;
    ft_status status = FT_STATUS_OK;

    if (!ft_crs_sequence_valid(sequence) || valid == NULL || statistics == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    policy = sequence->policy;
    state_count = policy->config.state_count;
    (void)memset(&audit, 0, sizeof(audit));
    audit.policy = policy;
    audit.consistent = true;
    audit.status = FT_STATUS_OK;
    audit.states = (size_t*)ft_crs_allocate(policy, state_count * sizeof(size_t));
    audit.events = (int64_t*)ft_crs_allocate(policy, state_count * sizeof(int64_t));
    scratch = ft_crs_allocate(policy, policy->measure_size);
    if (audit.states == NULL || audit.events == NULL || scratch == NULL) {
        ft_crs_deallocate(policy, scratch);
        ft_crs_deallocate(policy, audit.events);
        ft_crs_deallocate(policy, audit.states);
        return FT_STATUS_NO_MEMORY;
    }
    for (state = 0; state != state_count; ++state) {
        audit.states[state] = state;
        audit.events[state] = 0;
    }

    tree = ft_crs_tree_of(sequence);
    status = ft_tree_visit(&tree, ft_crs_audit_visit, &audit);
    if (status == FT_STATUS_OK) {
        status = audit.status;
    }
    if (status == FT_STATUS_OK) {
        status = ft_tree_measure(&tree, scratch);
    }
    if (status == FT_STATUS_OK) {
        const ft_crs_measure_head* head = (const ft_crs_measure_head*)scratch;
        const int64_t* events = ft_crs_measure_events_const(scratch);
        const size_t* states = ft_crs_measure_states_const(scratch, state_count);
        produced.count = audit.count;
        produced.state_count = state_count;
        produced.maximum_total_event_count = 0;
        if (head->count != audit.count || head->overflowed != 0 ||
            ft_tree_size(&tree) != audit.count) {
            audit.consistent = false;
        }
        for (state = 0; audit.consistent && state != state_count; ++state) {
            if (states[state] != audit.states[state] ||
                events[state] != audit.events[state]) {
                audit.consistent = false;
            } else if (audit.events[state] > produced.maximum_total_event_count) {
                produced.maximum_total_event_count = audit.events[state];
            }
        }
    }

    ft_crs_deallocate(policy, scratch);
    ft_crs_deallocate(policy, audit.events);
    ft_crs_deallocate(policy, audit.states);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (audit.consistent) {
        *statistics = produced;
    }
    *valid = audit.consistent;
    return FT_STATUS_OK;
}
