#include <durable7/hamt/persistent_map_patch.h>

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct d7_hamt_map_patch_change {
    bool before_present;
    void* before;
    bool after_present;
    void* after;
} d7_hamt_map_patch_change;

typedef struct d7_hamt_map_patch_context {
    atomic_size_t references;
    d7_hamt_set_policy key_policy;
    d7_hamt_set_policy value_policy;
} d7_hamt_map_patch_context;

static bool d7_hamt_map_patch_valid(const d7_hamt_map_patch* patch)
{
    return patch != NULL && patch->context != NULL;
}

static void d7_hamt_map_patch_context_retain(d7_hamt_map_patch_context* context)
{
    if (context != NULL) {
        (void)atomic_fetch_add_explicit(
            &context->references, 1u, memory_order_relaxed);
    }
}

static void d7_hamt_map_patch_context_release(d7_hamt_map_patch_context* context)
{
    if (context != NULL
        && atomic_fetch_sub_explicit(
            &context->references, 1u, memory_order_acq_rel) == 1u) {
        free(context);
    }
}

static bool d7_hamt_map_patch_states_equal(
    const d7_hamt_map_patch_context* context,
    bool left_present,
    const void* left,
    bool right_present,
    const void* right)
{
    return left_present == right_present
        && (!left_present || context->value_policy.equal(
            left, right, context->value_policy.context));
}

static uint32_t d7_hamt_map_patch_key_hash(const void* key, void* raw_context)
{
    d7_hamt_map_patch_context* context =
        (d7_hamt_map_patch_context*)raw_context;
    return context->key_policy.hash(key, context->key_policy.context);
}

static bool d7_hamt_map_patch_key_equal(
    const void* left,
    const void* right,
    void* raw_context)
{
    d7_hamt_map_patch_context* context =
        (d7_hamt_map_patch_context*)raw_context;
    return context->key_policy.equal(left, right, context->key_policy.context);
}

static void* d7_hamt_map_patch_key_retain(const void* key, void* raw_context)
{
    d7_hamt_map_patch_context* context =
        (d7_hamt_map_patch_context*)raw_context;
    return context->key_policy.retain_item(key, context->key_policy.context);
}

static void d7_hamt_map_patch_key_release(void* key, void* raw_context)
{
    d7_hamt_map_patch_context* context =
        (d7_hamt_map_patch_context*)raw_context;
    context->key_policy.release_item(key, context->key_policy.context);
}

static bool d7_hamt_map_patch_change_equal(
    const void* raw_left,
    const void* raw_right,
    void* raw_context)
{
    const d7_hamt_map_patch_change* left =
        (const d7_hamt_map_patch_change*)raw_left;
    const d7_hamt_map_patch_change* right =
        (const d7_hamt_map_patch_change*)raw_right;
    const d7_hamt_map_patch_context* context =
        (const d7_hamt_map_patch_context*)raw_context;
    return d7_hamt_map_patch_states_equal(
            context, left->before_present, left->before,
            right->before_present, right->before)
        && d7_hamt_map_patch_states_equal(
            context, left->after_present, left->after,
            right->after_present, right->after);
}

static void* d7_hamt_map_patch_change_retain(
    const void* raw_change,
    void* raw_context)
{
    const d7_hamt_map_patch_change* change =
        (const d7_hamt_map_patch_change*)raw_change;
    d7_hamt_map_patch_context* context =
        (d7_hamt_map_patch_context*)raw_context;
    d7_hamt_map_patch_change* copy =
        (d7_hamt_map_patch_change*)malloc(sizeof(*copy));
    if (copy == NULL) {
        return NULL;
    }
    copy->before_present = change->before_present;
    copy->after_present = change->after_present;
    copy->before = NULL;
    copy->after = NULL;
    if (change->before_present) {
        copy->before = context->value_policy.retain_item(
            change->before, context->value_policy.context);
        if (change->before != NULL && copy->before == NULL) {
            free(copy);
            return NULL;
        }
    }
    if (change->after_present) {
        copy->after = context->value_policy.retain_item(
            change->after, context->value_policy.context);
        if (change->after != NULL && copy->after == NULL) {
            if (change->before_present) {
                context->value_policy.release_item(
                    copy->before, context->value_policy.context);
            }
            free(copy);
            return NULL;
        }
    }
    return copy;
}

static void d7_hamt_map_patch_change_release(void* raw_change, void* raw_context)
{
    d7_hamt_map_patch_change* change =
        (d7_hamt_map_patch_change*)raw_change;
    d7_hamt_map_patch_context* context =
        (d7_hamt_map_patch_context*)raw_context;
    if (change != NULL) {
        if (change->before_present) {
            context->value_policy.release_item(
                change->before, context->value_policy.context);
        }
        if (change->after_present) {
            context->value_policy.release_item(
                change->after, context->value_policy.context);
        }
        free(change);
    }
}

static d7_hamt_policy d7_hamt_map_patch_policy(
    d7_hamt_map_patch_context* context)
{
    d7_hamt_policy policy;
    (void)memset(&policy, 0, sizeof(policy));
    policy.hash = d7_hamt_map_patch_key_hash;
    policy.key_equal = d7_hamt_map_patch_key_equal;
    policy.value_equal = d7_hamt_map_patch_change_equal;
    policy.retain_key = d7_hamt_map_patch_key_retain;
    policy.release_key = d7_hamt_map_patch_key_release;
    policy.retain_value = d7_hamt_map_patch_change_retain;
    policy.release_value = d7_hamt_map_patch_change_release;
    policy.context = context;
    return policy;
}

static void d7_hamt_map_patch_publish(
    const d7_hamt_map_patch* source,
    d7_hamt_map_patch* result,
    d7_hamt_map_patch* candidate)
{
    if (result == source) {
        d7_hamt_map_patch_destroy(result);
    }
    *result = *candidate;
    (void)memset(candidate, 0, sizeof(*candidate));
}

static d7_hamt_status d7_hamt_map_patch_publish_clone(
    const d7_hamt_map_patch* source,
    d7_hamt_map_patch* result)
{
    d7_hamt_map_patch candidate;
    const d7_hamt_status status =
        d7_hamt_map_patch_clone(source, &candidate);
    if (status == D7_HAMT_OK) {
        d7_hamt_map_patch_publish(source, result, &candidate);
    }
    return status;
}

d7_hamt_status d7_hamt_map_patch_init(
    d7_hamt_map_patch* patch,
    const d7_hamt_set_policy* key_policy,
    const d7_hamt_set_policy* value_policy)
{
    if (patch == NULL || key_policy == NULL || value_policy == NULL
        || key_policy->hash == NULL || key_policy->equal == NULL
        || value_policy->equal == NULL || key_policy->retain_item == NULL
        || key_policy->release_item == NULL || value_policy->retain_item == NULL
        || value_policy->release_item == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    d7_hamt_map_patch_context* context =
        (d7_hamt_map_patch_context*)malloc(sizeof(*context));
    if (context == NULL) {
        return D7_HAMT_OUT_OF_MEMORY;
    }
    atomic_init(&context->references, 1u);
    context->key_policy = *key_policy;
    context->value_policy = *value_policy;
    const d7_hamt_policy policy = d7_hamt_map_patch_policy(context);
    patch->changes = d7_hamt_map_create(&policy);
    patch->context = context;
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_map_patch_clone(
    const d7_hamt_map_patch* source,
    d7_hamt_map_patch* destination)
{
    if (!d7_hamt_map_patch_valid(source) || destination == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return D7_HAMT_OK;
    }
    destination->changes = d7_hamt_map_clone(&source->changes);
    destination->context = source->context;
    d7_hamt_map_patch_context_retain(destination->context);
    return D7_HAMT_OK;
}

void d7_hamt_map_patch_move(
    d7_hamt_map_patch* destination,
    d7_hamt_map_patch* source)
{
    if (destination != NULL && source != NULL && destination != source) {
        *destination = *source;
        (void)memset(source, 0, sizeof(*source));
    }
}

void d7_hamt_map_patch_destroy(d7_hamt_map_patch* patch)
{
    if (patch != NULL) {
        d7_hamt_map_destroy(&patch->changes);
        d7_hamt_map_patch_context_release(patch->context);
        (void)memset(patch, 0, sizeof(*patch));
    }
}

size_t d7_hamt_map_patch_count(const d7_hamt_map_patch* patch)
{
    return d7_hamt_map_patch_valid(patch)
        ? d7_hamt_map_count(&patch->changes) : 0u;
}

bool d7_hamt_map_patch_empty(const d7_hamt_map_patch* patch)
{
    return d7_hamt_map_patch_count(patch) == 0u;
}

bool d7_hamt_map_patch_contains_key(
    const d7_hamt_map_patch* patch,
    const void* key)
{
    return d7_hamt_map_patch_valid(patch)
        && d7_hamt_map_contains_key(&patch->changes, key);
}

bool d7_hamt_map_patch_try_get_entry(
    const d7_hamt_map_patch* patch,
    const void* key,
    d7_hamt_map_patch_entry* entry)
{
    if (entry != NULL) {
        (void)memset(entry, 0, sizeof(*entry));
    }
    if (!d7_hamt_map_patch_valid(patch)) {
        return false;
    }
    const void* stored_key = NULL;
    const void* raw_change = NULL;
    if (!d7_hamt_map_try_get_key(&patch->changes, key, &stored_key)
        || !d7_hamt_map_try_get(&patch->changes, key, &raw_change)) {
        return false;
    }
    if (entry != NULL) {
        const d7_hamt_map_patch_change* change =
            (const d7_hamt_map_patch_change*)raw_change;
        entry->key = stored_key;
        entry->before.present = change->before_present;
        entry->before.value = change->before;
        entry->after.present = change->after_present;
        entry->after.value = change->after;
    }
    return true;
}

d7_hamt_status d7_hamt_map_patch_add(
    const d7_hamt_map_patch* patch,
    const d7_hamt_map_patch_entry* entry,
    d7_hamt_map_patch* result)
{
    if (!d7_hamt_map_patch_valid(patch) || entry == NULL || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    if (d7_hamt_map_patch_states_equal(
            patch->context,
            entry->before.present, entry->before.value,
            entry->after.present, entry->after.value)) {
        return d7_hamt_map_patch_publish_clone(patch, result);
    }
    const d7_hamt_map_patch_change change = {
        entry->before.present, (void*)entry->before.value,
        entry->after.present, (void*)entry->after.value };
    d7_hamt_map changes;
    const d7_hamt_status status =
        d7_hamt_map_add(&patch->changes, entry->key, &change, &changes);
    if (status != D7_HAMT_OK) {
        return status;
    }
    d7_hamt_map_patch candidate = { changes, patch->context };
    d7_hamt_map_patch_context_retain(candidate.context);
    d7_hamt_map_patch_publish(patch, result, &candidate);
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_map_patch_try_add(
    const d7_hamt_map_patch* patch,
    const d7_hamt_map_patch_entry* entry,
    bool* added,
    d7_hamt_map_patch* result)
{
    if (added == NULL || entry == NULL || !d7_hamt_map_patch_valid(patch)) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    *added = false;
    if (d7_hamt_map_patch_contains_key(patch, entry->key)
        || d7_hamt_map_patch_states_equal(
            patch->context,
            entry->before.present, entry->before.value,
            entry->after.present, entry->after.value)) {
        return d7_hamt_map_patch_publish_clone(patch, result);
    }
    const d7_hamt_status status = d7_hamt_map_patch_add(patch, entry, result);
    if (status == D7_HAMT_OK) {
        *added = true;
    }
    return status;
}

d7_hamt_status d7_hamt_map_patch_remove(
    const d7_hamt_map_patch* patch,
    const void* key,
    d7_hamt_map_patch* result)
{
    if (!d7_hamt_map_patch_valid(patch) || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    d7_hamt_map changes;
    const d7_hamt_status status =
        d7_hamt_map_remove(&patch->changes, key, &changes);
    if (status != D7_HAMT_OK) {
        return status;
    }
    if (d7_hamt_map_shares_root(&changes, &patch->changes)) {
        d7_hamt_map_destroy(&changes);
        return d7_hamt_map_patch_publish_clone(patch, result);
    }
    d7_hamt_map_patch candidate = { changes, patch->context };
    d7_hamt_map_patch_context_retain(candidate.context);
    d7_hamt_map_patch_publish(patch, result, &candidate);
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_map_patch_clear(
    const d7_hamt_map_patch* patch,
    d7_hamt_map_patch* result)
{
    if (!d7_hamt_map_patch_valid(patch) || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    if (d7_hamt_map_patch_empty(patch)) {
        return d7_hamt_map_patch_publish_clone(patch, result);
    }
    d7_hamt_map changes;
    const d7_hamt_status status =
        d7_hamt_map_clear(&patch->changes, &changes);
    if (status != D7_HAMT_OK) {
        return status;
    }
    d7_hamt_map_patch candidate = { changes, patch->context };
    d7_hamt_map_patch_context_retain(candidate.context);
    d7_hamt_map_patch_publish(patch, result, &candidate);
    return D7_HAMT_OK;
}

static bool d7_hamt_map_patch_compatible_map(
    const d7_hamt_map_patch* patch,
    const d7_hamt_map* map)
{
    return map != NULL
        && patch->context->key_policy.hash == map->policy.hash
        && patch->context->key_policy.equal == map->policy.key_equal
        && patch->context->key_policy.context == map->policy.context;
}

d7_hamt_status d7_hamt_map_patch_try_apply(
    const d7_hamt_map_patch* patch,
    const d7_hamt_map* source,
    bool* applied,
    const void** conflicting_key,
    d7_hamt_map* result)
{
    if (!d7_hamt_map_patch_valid(patch) || source == NULL
        || applied == NULL || result == NULL
        || !d7_hamt_map_patch_compatible_map(patch, source)) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    *applied = false;
    if (conflicting_key != NULL) {
        *conflicting_key = NULL;
    }
    d7_hamt_map_iterator iterator;
    d7_hamt_map_iterator_init(&patch->changes, &iterator);
    const void* key = NULL;
    const void* raw_change = NULL;
    while (d7_hamt_map_iterator_next(&iterator, &key, &raw_change)) {
        const d7_hamt_map_patch_change* change =
            (const d7_hamt_map_patch_change*)raw_change;
        const void* current = NULL;
        const bool found = d7_hamt_map_try_get(source, key, &current);
        if (found != change->before_present
            || (found && !patch->context->value_policy.equal(
                current, change->before,
                patch->context->value_policy.context))) {
            if (conflicting_key != NULL) {
                *conflicting_key = key;
            }
            d7_hamt_map clone = d7_hamt_map_clone(source);
            if (result == source) {
                d7_hamt_map_destroy(result);
            }
            *result = clone;
            return D7_HAMT_OK;
        }
    }
    d7_hamt_map current = d7_hamt_map_clone(source);
    d7_hamt_map_iterator_init(&patch->changes, &iterator);
    while (d7_hamt_map_iterator_next(&iterator, &key, &raw_change)) {
        const d7_hamt_map_patch_change* change =
            (const d7_hamt_map_patch_change*)raw_change;
        d7_hamt_map next;
        const d7_hamt_status status = change->after_present
            ? d7_hamt_map_set(&current, key, change->after, &next)
            : d7_hamt_map_remove(&current, key, &next);
        if (status != D7_HAMT_OK) {
            d7_hamt_map_destroy(&current);
            return status;
        }
        d7_hamt_map_destroy(&current);
        current = next;
    }
    if (result == source) {
        d7_hamt_map_destroy(result);
    }
    *result = current;
    *applied = true;
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_map_patch_apply(
    const d7_hamt_map_patch* patch,
    const d7_hamt_map* source,
    d7_hamt_map* result)
{
    bool applied = false;
    const d7_hamt_status status = d7_hamt_map_patch_try_apply(
        patch, source, &applied, NULL, result);
    if (status != D7_HAMT_OK) {
        return status;
    }
    if (!applied) {
        d7_hamt_map_destroy(result);
        return D7_HAMT_INVALID_ARGUMENT;
    }
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_map_patch_invert(
    const d7_hamt_map_patch* patch,
    d7_hamt_map_patch* result)
{
    if (!d7_hamt_map_patch_valid(patch) || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    if (d7_hamt_map_patch_empty(patch)) {
        return d7_hamt_map_patch_publish_clone(patch, result);
    }
    d7_hamt_map_patch candidate;
    d7_hamt_status status = d7_hamt_map_patch_init(
        &candidate, &patch->context->key_policy, &patch->context->value_policy);
    if (status != D7_HAMT_OK) {
        return status;
    }
    d7_hamt_map_iterator iterator;
    d7_hamt_map_iterator_init(&patch->changes, &iterator);
    const void* key = NULL;
    const void* raw_change = NULL;
    while (d7_hamt_map_iterator_next(&iterator, &key, &raw_change)) {
        const d7_hamt_map_patch_change* change =
            (const d7_hamt_map_patch_change*)raw_change;
        const d7_hamt_map_patch_entry entry = {
            key,
            {change->after_present, change->after},
            {change->before_present, change->before} };
        status = d7_hamt_map_patch_add(&candidate, &entry, &candidate);
        if (status != D7_HAMT_OK) {
            d7_hamt_map_patch_destroy(&candidate);
            return status;
        }
    }
    d7_hamt_map_patch_publish(patch, result, &candidate);
    return D7_HAMT_OK;
}

static bool d7_hamt_map_patch_compatible_patch(
    const d7_hamt_map_patch* left,
    const d7_hamt_map_patch* right)
{
    return left->context->key_policy.hash == right->context->key_policy.hash
        && left->context->key_policy.equal == right->context->key_policy.equal
        && left->context->key_policy.context == right->context->key_policy.context
        && left->context->value_policy.equal == right->context->value_policy.equal
        && left->context->value_policy.context == right->context->value_policy.context;
}

d7_hamt_status d7_hamt_map_patch_compose(
    const d7_hamt_map_patch* first,
    const d7_hamt_map_patch* next,
    d7_hamt_map_patch* result)
{
    if (!d7_hamt_map_patch_valid(first) || !d7_hamt_map_patch_valid(next)
        || result == NULL || !d7_hamt_map_patch_compatible_patch(first, next)) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    d7_hamt_map_patch current;
    d7_hamt_status status = d7_hamt_map_patch_clone(first, &current);
    if (status != D7_HAMT_OK) {
        return status;
    }
    d7_hamt_map_iterator next_iterator;
    d7_hamt_map_iterator_init(&next->changes, &next_iterator);
    const void* next_key = NULL;
    const void* raw_next = NULL;
    while (d7_hamt_map_iterator_next(&next_iterator, &next_key, &raw_next)) {
        const d7_hamt_map_patch_change* next_change =
            (const d7_hamt_map_patch_change*)raw_next;
        d7_hamt_map_patch_entry existing;
        if (!d7_hamt_map_patch_try_get_entry(&current, next_key, &existing)) {
            const d7_hamt_map_patch_entry entry = {
                next_key,
                {next_change->before_present, next_change->before},
                {next_change->after_present, next_change->after} };
            status = d7_hamt_map_patch_add(&current, &entry, &current);
            if (status != D7_HAMT_OK) {
                d7_hamt_map_patch_destroy(&current);
                return status;
            }
            continue;
        }
        if (!d7_hamt_map_patch_states_equal(
                current.context,
                existing.after.present, existing.after.value,
                next_change->before_present, next_change->before)) {
            d7_hamt_map_patch_destroy(&current);
            return D7_HAMT_INVALID_ARGUMENT;
        }

        d7_hamt_map_patch replacement;
        status = d7_hamt_map_patch_init(
            &replacement, &current.context->key_policy, &current.context->value_policy);
        if (status != D7_HAMT_OK) {
            d7_hamt_map_patch_destroy(&current);
            return status;
        }
        d7_hamt_map_iterator current_iterator;
        d7_hamt_map_iterator_init(&current.changes, &current_iterator);
        const void* current_key = NULL;
        const void* raw_current = NULL;
        while (d7_hamt_map_iterator_next(
            &current_iterator, &current_key, &raw_current)) {
            if (current.context->key_policy.equal(
                current_key, existing.key, current.context->key_policy.context)) {
                continue;
            }
            const d7_hamt_map_patch_change* change =
                (const d7_hamt_map_patch_change*)raw_current;
            const d7_hamt_map_patch_entry entry = {
                current_key,
                {change->before_present, change->before},
                {change->after_present, change->after} };
            status = d7_hamt_map_patch_add(&replacement, &entry, &replacement);
            if (status != D7_HAMT_OK) {
                d7_hamt_map_patch_destroy(&replacement);
                d7_hamt_map_patch_destroy(&current);
                return status;
            }
        }
        const d7_hamt_map_patch_entry combined = {
            existing.key,
            existing.before,
            {next_change->after_present, next_change->after} };
        status = d7_hamt_map_patch_add(&replacement, &combined, &replacement);
        if (status != D7_HAMT_OK) {
            d7_hamt_map_patch_destroy(&replacement);
            d7_hamt_map_patch_destroy(&current);
            return status;
        }
        d7_hamt_map_patch_destroy(&current);
        current = replacement;
    }
    d7_hamt_map_patch_publish(first, result, &current);
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_map_patch_visit(
    const d7_hamt_map_patch* patch,
    d7_hamt_map_patch_visit_fn visitor,
    void* context)
{
    if (!d7_hamt_map_patch_valid(patch) || visitor == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    d7_hamt_map_iterator iterator;
    d7_hamt_map_iterator_init(&patch->changes, &iterator);
    const void* key = NULL;
    const void* raw_change = NULL;
    while (d7_hamt_map_iterator_next(&iterator, &key, &raw_change)) {
        const d7_hamt_map_patch_change* change =
            (const d7_hamt_map_patch_change*)raw_change;
        const d7_hamt_map_patch_entry entry = {
            key,
            {change->before_present, change->before},
            {change->after_present, change->after} };
        visitor(&entry, context);
    }
    return D7_HAMT_OK;
}

bool d7_hamt_map_patch_debug_validate(const d7_hamt_map_patch* patch)
{
    if (!d7_hamt_map_patch_valid(patch)
        || !d7_hamt_map_debug_validate_canonical(&patch->changes)) {
        return false;
    }
    d7_hamt_map_iterator iterator;
    d7_hamt_map_iterator_init(&patch->changes, &iterator);
    const void* key = NULL;
    const void* raw_change = NULL;
    while (d7_hamt_map_iterator_next(&iterator, &key, &raw_change)) {
        (void)key;
        const d7_hamt_map_patch_change* change =
            (const d7_hamt_map_patch_change*)raw_change;
        if (d7_hamt_map_patch_states_equal(
                patch->context,
                change->before_present, change->before,
                change->after_present, change->after)) {
            return false;
        }
    }
    return true;
}

bool d7_hamt_map_patch_debug_shares_root(
    const d7_hamt_map_patch* left,
    const d7_hamt_map_patch* right)
{
    return d7_hamt_map_patch_valid(left)
        && d7_hamt_map_patch_valid(right)
        && d7_hamt_map_shares_root(&left->changes, &right->changes);
}

static uint32_t d7_hamt_map_patch_unused_hash(const void* value, void* context)
{
    (void)value;
    (void)context;
    return 0u;
}

d7_hamt_status d7_hamt_map_patch_between(
    const d7_hamt_map* source,
    const d7_hamt_map* target,
    d7_hamt_map_patch* result)
{
    if (source == NULL || target == NULL || result == NULL
        || source->policy.hash != target->policy.hash
        || source->policy.key_equal != target->policy.key_equal
        || source->policy.context != target->policy.context
        || source->policy.value_equal == NULL
        || source->policy.retain_key == NULL || source->policy.release_key == NULL
        || source->policy.retain_value == NULL || source->policy.release_value == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    const d7_hamt_set_policy key_policy = {
        source->policy.hash,
        source->policy.key_equal,
        source->policy.retain_key,
        source->policy.release_key,
        source->policy.context };
    const d7_hamt_set_policy value_policy = {
        d7_hamt_map_patch_unused_hash,
        source->policy.value_equal,
        source->policy.retain_value,
        source->policy.release_value,
        source->policy.context };
    d7_hamt_status status =
        d7_hamt_map_patch_init(result, &key_policy, &value_policy);
    if (status != D7_HAMT_OK) {
        return status;
    }
    d7_hamt_map_iterator iterator;
    d7_hamt_map_iterator_init(source, &iterator);
    const void* key = NULL;
    const void* value = NULL;
    while (d7_hamt_map_iterator_next(&iterator, &key, &value)) {
        const void* target_value = NULL;
        const bool found = d7_hamt_map_try_get(target, key, &target_value);
        if (!found || !source->policy.value_equal(
            value, target_value, source->policy.context)) {
            const d7_hamt_map_patch_entry entry = {
                key, {true, value}, {found, target_value} };
            status = d7_hamt_map_patch_add(result, &entry, result);
            if (status != D7_HAMT_OK) {
                d7_hamt_map_patch_destroy(result);
                return status;
            }
        }
    }
    d7_hamt_map_iterator_init(target, &iterator);
    while (d7_hamt_map_iterator_next(&iterator, &key, &value)) {
        if (!d7_hamt_map_contains_key(source, key)) {
            const d7_hamt_map_patch_entry entry = {
                key, {false, NULL}, {true, value} };
            status = d7_hamt_map_patch_add(result, &entry, result);
            if (status != D7_HAMT_OK) {
                d7_hamt_map_patch_destroy(result);
                return status;
            }
        }
    }
    return D7_HAMT_OK;
}
