#include <Tools/DataStructures/Hamt/persistent_map_patch.h>

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct tds_hamt_map_patch_change {
    bool before_present;
    void* before;
    bool after_present;
    void* after;
} tds_hamt_map_patch_change;

typedef struct tds_hamt_map_patch_context {
    atomic_size_t references;
    tds_hamt_set_policy key_policy;
    tds_hamt_set_policy value_policy;
} tds_hamt_map_patch_context;

static bool tds_hamt_map_patch_valid(const tds_hamt_map_patch* patch)
{
    return patch != NULL && patch->context != NULL;
}

static void tds_hamt_map_patch_context_retain(tds_hamt_map_patch_context* context)
{
    if (context != NULL) {
        (void)atomic_fetch_add_explicit(
            &context->references, 1u, memory_order_relaxed);
    }
}

static void tds_hamt_map_patch_context_release(tds_hamt_map_patch_context* context)
{
    if (context != NULL
        && atomic_fetch_sub_explicit(
            &context->references, 1u, memory_order_acq_rel) == 1u) {
        free(context);
    }
}

static bool tds_hamt_map_patch_states_equal(
    const tds_hamt_map_patch_context* context,
    bool left_present,
    const void* left,
    bool right_present,
    const void* right)
{
    return left_present == right_present
        && (!left_present || context->value_policy.equal(
            left, right, context->value_policy.context));
}

static uint32_t tds_hamt_map_patch_key_hash(const void* key, void* raw_context)
{
    tds_hamt_map_patch_context* context =
        (tds_hamt_map_patch_context*)raw_context;
    return context->key_policy.hash(key, context->key_policy.context);
}

static bool tds_hamt_map_patch_key_equal(
    const void* left,
    const void* right,
    void* raw_context)
{
    tds_hamt_map_patch_context* context =
        (tds_hamt_map_patch_context*)raw_context;
    return context->key_policy.equal(left, right, context->key_policy.context);
}

static void* tds_hamt_map_patch_key_retain(const void* key, void* raw_context)
{
    tds_hamt_map_patch_context* context =
        (tds_hamt_map_patch_context*)raw_context;
    return context->key_policy.retain_item(key, context->key_policy.context);
}

static void tds_hamt_map_patch_key_release(void* key, void* raw_context)
{
    tds_hamt_map_patch_context* context =
        (tds_hamt_map_patch_context*)raw_context;
    context->key_policy.release_item(key, context->key_policy.context);
}

static bool tds_hamt_map_patch_change_equal(
    const void* raw_left,
    const void* raw_right,
    void* raw_context)
{
    const tds_hamt_map_patch_change* left =
        (const tds_hamt_map_patch_change*)raw_left;
    const tds_hamt_map_patch_change* right =
        (const tds_hamt_map_patch_change*)raw_right;
    const tds_hamt_map_patch_context* context =
        (const tds_hamt_map_patch_context*)raw_context;
    return tds_hamt_map_patch_states_equal(
            context, left->before_present, left->before,
            right->before_present, right->before)
        && tds_hamt_map_patch_states_equal(
            context, left->after_present, left->after,
            right->after_present, right->after);
}

static void* tds_hamt_map_patch_change_retain(
    const void* raw_change,
    void* raw_context)
{
    const tds_hamt_map_patch_change* change =
        (const tds_hamt_map_patch_change*)raw_change;
    tds_hamt_map_patch_context* context =
        (tds_hamt_map_patch_context*)raw_context;
    tds_hamt_map_patch_change* copy =
        (tds_hamt_map_patch_change*)malloc(sizeof(*copy));
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

static void tds_hamt_map_patch_change_release(void* raw_change, void* raw_context)
{
    tds_hamt_map_patch_change* change =
        (tds_hamt_map_patch_change*)raw_change;
    tds_hamt_map_patch_context* context =
        (tds_hamt_map_patch_context*)raw_context;
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

static tds_hamt_policy tds_hamt_map_patch_policy(
    tds_hamt_map_patch_context* context)
{
    tds_hamt_policy policy;
    (void)memset(&policy, 0, sizeof(policy));
    policy.hash = tds_hamt_map_patch_key_hash;
    policy.key_equal = tds_hamt_map_patch_key_equal;
    policy.value_equal = tds_hamt_map_patch_change_equal;
    policy.retain_key = tds_hamt_map_patch_key_retain;
    policy.release_key = tds_hamt_map_patch_key_release;
    policy.retain_value = tds_hamt_map_patch_change_retain;
    policy.release_value = tds_hamt_map_patch_change_release;
    policy.context = context;
    return policy;
}

static void tds_hamt_map_patch_publish(
    const tds_hamt_map_patch* source,
    tds_hamt_map_patch* result,
    tds_hamt_map_patch* candidate)
{
    if (result == source) {
        tds_hamt_map_patch_destroy(result);
    }
    *result = *candidate;
    (void)memset(candidate, 0, sizeof(*candidate));
}

static tds_hamt_status tds_hamt_map_patch_publish_clone(
    const tds_hamt_map_patch* source,
    tds_hamt_map_patch* result)
{
    tds_hamt_map_patch candidate;
    const tds_hamt_status status =
        tds_hamt_map_patch_clone(source, &candidate);
    if (status == TDS_HAMT_OK) {
        tds_hamt_map_patch_publish(source, result, &candidate);
    }
    return status;
}

tds_hamt_status tds_hamt_map_patch_init(
    tds_hamt_map_patch* patch,
    const tds_hamt_set_policy* key_policy,
    const tds_hamt_set_policy* value_policy)
{
    if (patch == NULL || key_policy == NULL || value_policy == NULL
        || key_policy->hash == NULL || key_policy->equal == NULL
        || value_policy->equal == NULL || key_policy->retain_item == NULL
        || key_policy->release_item == NULL || value_policy->retain_item == NULL
        || value_policy->release_item == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    tds_hamt_map_patch_context* context =
        (tds_hamt_map_patch_context*)malloc(sizeof(*context));
    if (context == NULL) {
        return TDS_HAMT_OUT_OF_MEMORY;
    }
    atomic_init(&context->references, 1u);
    context->key_policy = *key_policy;
    context->value_policy = *value_policy;
    const tds_hamt_policy policy = tds_hamt_map_patch_policy(context);
    patch->changes = tds_hamt_map_create(&policy);
    patch->context = context;
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_map_patch_clone(
    const tds_hamt_map_patch* source,
    tds_hamt_map_patch* destination)
{
    if (!tds_hamt_map_patch_valid(source) || destination == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return TDS_HAMT_OK;
    }
    destination->changes = tds_hamt_map_clone(&source->changes);
    destination->context = source->context;
    tds_hamt_map_patch_context_retain(destination->context);
    return TDS_HAMT_OK;
}

void tds_hamt_map_patch_move(
    tds_hamt_map_patch* destination,
    tds_hamt_map_patch* source)
{
    if (destination != NULL && source != NULL && destination != source) {
        *destination = *source;
        (void)memset(source, 0, sizeof(*source));
    }
}

void tds_hamt_map_patch_destroy(tds_hamt_map_patch* patch)
{
    if (patch != NULL) {
        tds_hamt_map_destroy(&patch->changes);
        tds_hamt_map_patch_context_release(patch->context);
        (void)memset(patch, 0, sizeof(*patch));
    }
}

size_t tds_hamt_map_patch_count(const tds_hamt_map_patch* patch)
{
    return tds_hamt_map_patch_valid(patch)
        ? tds_hamt_map_count(&patch->changes) : 0u;
}

bool tds_hamt_map_patch_empty(const tds_hamt_map_patch* patch)
{
    return tds_hamt_map_patch_count(patch) == 0u;
}

bool tds_hamt_map_patch_contains_key(
    const tds_hamt_map_patch* patch,
    const void* key)
{
    return tds_hamt_map_patch_valid(patch)
        && tds_hamt_map_contains_key(&patch->changes, key);
}

bool tds_hamt_map_patch_try_get_entry(
    const tds_hamt_map_patch* patch,
    const void* key,
    tds_hamt_map_patch_entry* entry)
{
    if (entry != NULL) {
        (void)memset(entry, 0, sizeof(*entry));
    }
    if (!tds_hamt_map_patch_valid(patch)) {
        return false;
    }
    const void* stored_key = NULL;
    const void* raw_change = NULL;
    if (!tds_hamt_map_try_get_key(&patch->changes, key, &stored_key)
        || !tds_hamt_map_try_get(&patch->changes, key, &raw_change)) {
        return false;
    }
    if (entry != NULL) {
        const tds_hamt_map_patch_change* change =
            (const tds_hamt_map_patch_change*)raw_change;
        entry->key = stored_key;
        entry->before.present = change->before_present;
        entry->before.value = change->before;
        entry->after.present = change->after_present;
        entry->after.value = change->after;
    }
    return true;
}

tds_hamt_status tds_hamt_map_patch_add(
    const tds_hamt_map_patch* patch,
    const tds_hamt_map_patch_entry* entry,
    tds_hamt_map_patch* result)
{
    if (!tds_hamt_map_patch_valid(patch) || entry == NULL || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    if (tds_hamt_map_patch_states_equal(
            patch->context,
            entry->before.present, entry->before.value,
            entry->after.present, entry->after.value)) {
        return tds_hamt_map_patch_publish_clone(patch, result);
    }
    const tds_hamt_map_patch_change change = {
        entry->before.present, (void*)entry->before.value,
        entry->after.present, (void*)entry->after.value };
    tds_hamt_map changes;
    const tds_hamt_status status =
        tds_hamt_map_add(&patch->changes, entry->key, &change, &changes);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    tds_hamt_map_patch candidate = { changes, patch->context };
    tds_hamt_map_patch_context_retain(candidate.context);
    tds_hamt_map_patch_publish(patch, result, &candidate);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_map_patch_try_add(
    const tds_hamt_map_patch* patch,
    const tds_hamt_map_patch_entry* entry,
    bool* added,
    tds_hamt_map_patch* result)
{
    if (added == NULL || entry == NULL || !tds_hamt_map_patch_valid(patch)) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    *added = false;
    if (tds_hamt_map_patch_contains_key(patch, entry->key)
        || tds_hamt_map_patch_states_equal(
            patch->context,
            entry->before.present, entry->before.value,
            entry->after.present, entry->after.value)) {
        return tds_hamt_map_patch_publish_clone(patch, result);
    }
    const tds_hamt_status status = tds_hamt_map_patch_add(patch, entry, result);
    if (status == TDS_HAMT_OK) {
        *added = true;
    }
    return status;
}

tds_hamt_status tds_hamt_map_patch_remove(
    const tds_hamt_map_patch* patch,
    const void* key,
    tds_hamt_map_patch* result)
{
    if (!tds_hamt_map_patch_valid(patch) || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    tds_hamt_map changes;
    const tds_hamt_status status =
        tds_hamt_map_remove(&patch->changes, key, &changes);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    if (tds_hamt_map_shares_root(&changes, &patch->changes)) {
        tds_hamt_map_destroy(&changes);
        return tds_hamt_map_patch_publish_clone(patch, result);
    }
    tds_hamt_map_patch candidate = { changes, patch->context };
    tds_hamt_map_patch_context_retain(candidate.context);
    tds_hamt_map_patch_publish(patch, result, &candidate);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_map_patch_clear(
    const tds_hamt_map_patch* patch,
    tds_hamt_map_patch* result)
{
    if (!tds_hamt_map_patch_valid(patch) || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    if (tds_hamt_map_patch_empty(patch)) {
        return tds_hamt_map_patch_publish_clone(patch, result);
    }
    tds_hamt_map changes;
    const tds_hamt_status status =
        tds_hamt_map_clear(&patch->changes, &changes);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    tds_hamt_map_patch candidate = { changes, patch->context };
    tds_hamt_map_patch_context_retain(candidate.context);
    tds_hamt_map_patch_publish(patch, result, &candidate);
    return TDS_HAMT_OK;
}

static bool tds_hamt_map_patch_compatible_map(
    const tds_hamt_map_patch* patch,
    const tds_hamt_map* map)
{
    return map != NULL
        && patch->context->key_policy.hash == map->policy.hash
        && patch->context->key_policy.equal == map->policy.key_equal
        && patch->context->key_policy.context == map->policy.context;
}

tds_hamt_status tds_hamt_map_patch_try_apply(
    const tds_hamt_map_patch* patch,
    const tds_hamt_map* source,
    bool* applied,
    const void** conflicting_key,
    tds_hamt_map* result)
{
    if (!tds_hamt_map_patch_valid(patch) || source == NULL
        || applied == NULL || result == NULL
        || !tds_hamt_map_patch_compatible_map(patch, source)) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    *applied = false;
    if (conflicting_key != NULL) {
        *conflicting_key = NULL;
    }
    tds_hamt_map_iterator iterator;
    tds_hamt_map_iterator_init(&patch->changes, &iterator);
    const void* key = NULL;
    const void* raw_change = NULL;
    while (tds_hamt_map_iterator_next(&iterator, &key, &raw_change)) {
        const tds_hamt_map_patch_change* change =
            (const tds_hamt_map_patch_change*)raw_change;
        const void* current = NULL;
        const bool found = tds_hamt_map_try_get(source, key, &current);
        if (found != change->before_present
            || (found && !patch->context->value_policy.equal(
                current, change->before,
                patch->context->value_policy.context))) {
            if (conflicting_key != NULL) {
                *conflicting_key = key;
            }
            tds_hamt_map clone = tds_hamt_map_clone(source);
            if (result == source) {
                tds_hamt_map_destroy(result);
            }
            *result = clone;
            return TDS_HAMT_OK;
        }
    }
    tds_hamt_map current = tds_hamt_map_clone(source);
    tds_hamt_map_iterator_init(&patch->changes, &iterator);
    while (tds_hamt_map_iterator_next(&iterator, &key, &raw_change)) {
        const tds_hamt_map_patch_change* change =
            (const tds_hamt_map_patch_change*)raw_change;
        tds_hamt_map next;
        const tds_hamt_status status = change->after_present
            ? tds_hamt_map_set(&current, key, change->after, &next)
            : tds_hamt_map_remove(&current, key, &next);
        if (status != TDS_HAMT_OK) {
            tds_hamt_map_destroy(&current);
            return status;
        }
        tds_hamt_map_destroy(&current);
        current = next;
    }
    if (result == source) {
        tds_hamt_map_destroy(result);
    }
    *result = current;
    *applied = true;
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_map_patch_apply(
    const tds_hamt_map_patch* patch,
    const tds_hamt_map* source,
    tds_hamt_map* result)
{
    bool applied = false;
    const tds_hamt_status status = tds_hamt_map_patch_try_apply(
        patch, source, &applied, NULL, result);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    if (!applied) {
        tds_hamt_map_destroy(result);
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_map_patch_invert(
    const tds_hamt_map_patch* patch,
    tds_hamt_map_patch* result)
{
    if (!tds_hamt_map_patch_valid(patch) || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    if (tds_hamt_map_patch_empty(patch)) {
        return tds_hamt_map_patch_publish_clone(patch, result);
    }
    tds_hamt_map_patch candidate;
    tds_hamt_status status = tds_hamt_map_patch_init(
        &candidate, &patch->context->key_policy, &patch->context->value_policy);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    tds_hamt_map_iterator iterator;
    tds_hamt_map_iterator_init(&patch->changes, &iterator);
    const void* key = NULL;
    const void* raw_change = NULL;
    while (tds_hamt_map_iterator_next(&iterator, &key, &raw_change)) {
        const tds_hamt_map_patch_change* change =
            (const tds_hamt_map_patch_change*)raw_change;
        const tds_hamt_map_patch_entry entry = {
            key,
            {change->after_present, change->after},
            {change->before_present, change->before} };
        status = tds_hamt_map_patch_add(&candidate, &entry, &candidate);
        if (status != TDS_HAMT_OK) {
            tds_hamt_map_patch_destroy(&candidate);
            return status;
        }
    }
    tds_hamt_map_patch_publish(patch, result, &candidate);
    return TDS_HAMT_OK;
}

static bool tds_hamt_map_patch_compatible_patch(
    const tds_hamt_map_patch* left,
    const tds_hamt_map_patch* right)
{
    return left->context->key_policy.hash == right->context->key_policy.hash
        && left->context->key_policy.equal == right->context->key_policy.equal
        && left->context->key_policy.context == right->context->key_policy.context
        && left->context->value_policy.equal == right->context->value_policy.equal
        && left->context->value_policy.context == right->context->value_policy.context;
}

tds_hamt_status tds_hamt_map_patch_compose(
    const tds_hamt_map_patch* first,
    const tds_hamt_map_patch* next,
    tds_hamt_map_patch* result)
{
    if (!tds_hamt_map_patch_valid(first) || !tds_hamt_map_patch_valid(next)
        || result == NULL || !tds_hamt_map_patch_compatible_patch(first, next)) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    tds_hamt_map_patch current;
    tds_hamt_status status = tds_hamt_map_patch_clone(first, &current);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    tds_hamt_map_iterator next_iterator;
    tds_hamt_map_iterator_init(&next->changes, &next_iterator);
    const void* next_key = NULL;
    const void* raw_next = NULL;
    while (tds_hamt_map_iterator_next(&next_iterator, &next_key, &raw_next)) {
        const tds_hamt_map_patch_change* next_change =
            (const tds_hamt_map_patch_change*)raw_next;
        tds_hamt_map_patch_entry existing;
        if (!tds_hamt_map_patch_try_get_entry(&current, next_key, &existing)) {
            const tds_hamt_map_patch_entry entry = {
                next_key,
                {next_change->before_present, next_change->before},
                {next_change->after_present, next_change->after} };
            status = tds_hamt_map_patch_add(&current, &entry, &current);
            if (status != TDS_HAMT_OK) {
                tds_hamt_map_patch_destroy(&current);
                return status;
            }
            continue;
        }
        if (!tds_hamt_map_patch_states_equal(
                current.context,
                existing.after.present, existing.after.value,
                next_change->before_present, next_change->before)) {
            tds_hamt_map_patch_destroy(&current);
            return TDS_HAMT_INVALID_ARGUMENT;
        }

        tds_hamt_map_patch replacement;
        status = tds_hamt_map_patch_init(
            &replacement, &current.context->key_policy, &current.context->value_policy);
        if (status != TDS_HAMT_OK) {
            tds_hamt_map_patch_destroy(&current);
            return status;
        }
        tds_hamt_map_iterator current_iterator;
        tds_hamt_map_iterator_init(&current.changes, &current_iterator);
        const void* current_key = NULL;
        const void* raw_current = NULL;
        while (tds_hamt_map_iterator_next(
            &current_iterator, &current_key, &raw_current)) {
            if (current.context->key_policy.equal(
                current_key, existing.key, current.context->key_policy.context)) {
                continue;
            }
            const tds_hamt_map_patch_change* change =
                (const tds_hamt_map_patch_change*)raw_current;
            const tds_hamt_map_patch_entry entry = {
                current_key,
                {change->before_present, change->before},
                {change->after_present, change->after} };
            status = tds_hamt_map_patch_add(&replacement, &entry, &replacement);
            if (status != TDS_HAMT_OK) {
                tds_hamt_map_patch_destroy(&replacement);
                tds_hamt_map_patch_destroy(&current);
                return status;
            }
        }
        const tds_hamt_map_patch_entry combined = {
            existing.key,
            existing.before,
            {next_change->after_present, next_change->after} };
        status = tds_hamt_map_patch_add(&replacement, &combined, &replacement);
        if (status != TDS_HAMT_OK) {
            tds_hamt_map_patch_destroy(&replacement);
            tds_hamt_map_patch_destroy(&current);
            return status;
        }
        tds_hamt_map_patch_destroy(&current);
        current = replacement;
    }
    tds_hamt_map_patch_publish(first, result, &current);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_map_patch_visit(
    const tds_hamt_map_patch* patch,
    tds_hamt_map_patch_visit_fn visitor,
    void* context)
{
    if (!tds_hamt_map_patch_valid(patch) || visitor == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    tds_hamt_map_iterator iterator;
    tds_hamt_map_iterator_init(&patch->changes, &iterator);
    const void* key = NULL;
    const void* raw_change = NULL;
    while (tds_hamt_map_iterator_next(&iterator, &key, &raw_change)) {
        const tds_hamt_map_patch_change* change =
            (const tds_hamt_map_patch_change*)raw_change;
        const tds_hamt_map_patch_entry entry = {
            key,
            {change->before_present, change->before},
            {change->after_present, change->after} };
        visitor(&entry, context);
    }
    return TDS_HAMT_OK;
}

bool tds_hamt_map_patch_debug_validate(const tds_hamt_map_patch* patch)
{
    if (!tds_hamt_map_patch_valid(patch)
        || !tds_hamt_map_debug_validate_canonical(&patch->changes)) {
        return false;
    }
    tds_hamt_map_iterator iterator;
    tds_hamt_map_iterator_init(&patch->changes, &iterator);
    const void* key = NULL;
    const void* raw_change = NULL;
    while (tds_hamt_map_iterator_next(&iterator, &key, &raw_change)) {
        (void)key;
        const tds_hamt_map_patch_change* change =
            (const tds_hamt_map_patch_change*)raw_change;
        if (tds_hamt_map_patch_states_equal(
                patch->context,
                change->before_present, change->before,
                change->after_present, change->after)) {
            return false;
        }
    }
    return true;
}

bool tds_hamt_map_patch_debug_shares_root(
    const tds_hamt_map_patch* left,
    const tds_hamt_map_patch* right)
{
    return tds_hamt_map_patch_valid(left)
        && tds_hamt_map_patch_valid(right)
        && tds_hamt_map_shares_root(&left->changes, &right->changes);
}

static uint32_t tds_hamt_map_patch_unused_hash(const void* value, void* context)
{
    (void)value;
    (void)context;
    return 0u;
}

tds_hamt_status tds_hamt_map_patch_between(
    const tds_hamt_map* source,
    const tds_hamt_map* target,
    tds_hamt_map_patch* result)
{
    if (source == NULL || target == NULL || result == NULL
        || source->policy.hash != target->policy.hash
        || source->policy.key_equal != target->policy.key_equal
        || source->policy.context != target->policy.context
        || source->policy.value_equal == NULL
        || source->policy.retain_key == NULL || source->policy.release_key == NULL
        || source->policy.retain_value == NULL || source->policy.release_value == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    const tds_hamt_set_policy key_policy = {
        source->policy.hash,
        source->policy.key_equal,
        source->policy.retain_key,
        source->policy.release_key,
        source->policy.context };
    const tds_hamt_set_policy value_policy = {
        tds_hamt_map_patch_unused_hash,
        source->policy.value_equal,
        source->policy.retain_value,
        source->policy.release_value,
        source->policy.context };
    tds_hamt_status status =
        tds_hamt_map_patch_init(result, &key_policy, &value_policy);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    tds_hamt_map_iterator iterator;
    tds_hamt_map_iterator_init(source, &iterator);
    const void* key = NULL;
    const void* value = NULL;
    while (tds_hamt_map_iterator_next(&iterator, &key, &value)) {
        const void* target_value = NULL;
        const bool found = tds_hamt_map_try_get(target, key, &target_value);
        if (!found || !source->policy.value_equal(
            value, target_value, source->policy.context)) {
            const tds_hamt_map_patch_entry entry = {
                key, {true, value}, {found, target_value} };
            status = tds_hamt_map_patch_add(result, &entry, result);
            if (status != TDS_HAMT_OK) {
                tds_hamt_map_patch_destroy(result);
                return status;
            }
        }
    }
    tds_hamt_map_iterator_init(target, &iterator);
    while (tds_hamt_map_iterator_next(&iterator, &key, &value)) {
        if (!tds_hamt_map_contains_key(source, key)) {
            const tds_hamt_map_patch_entry entry = {
                key, {false, NULL}, {true, value} };
            status = tds_hamt_map_patch_add(result, &entry, result);
            if (status != TDS_HAMT_OK) {
                tds_hamt_map_patch_destroy(result);
                return status;
            }
        }
    }
    return TDS_HAMT_OK;
}
