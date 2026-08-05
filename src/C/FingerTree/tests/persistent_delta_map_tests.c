/*
 * Tests for the checkpoint-differential persistent ordered map: change classification, coalescing
 * and cancellation, representative retention, checkpoint and rollback identity, output-sensitive
 * change enumeration, the bulk-assignment fold, randomized model equivalence over retained
 * versions, and failure atomicity at every reachable allocation and callback ordinal.
 */

#include <durable7/finger_tree/persistent_delta_map.h>
#include <durable7/test_support/headless_test_process.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;
static const unsigned char g_key_type_identity = 0;
static const unsigned char g_value_type_identity = 0;

static void fail_at(const char* file, int line, const char* expression)
{
    (void)fprintf(stderr, "%s:%d: requirement failed: %s\n", file, line, expression);
    ++g_failures;
}

#define REQUIRE(expression) \
    do { \
        if (!(expression)) { \
            fail_at(__FILE__, __LINE__, #expression); \
            return; \
        } \
    } while (0)

#define REQUIRE_STATUS(expression, expected) \
    do { \
        const ft_status actual_status__ = (expression); \
        if (actual_status__ != (expected)) { \
            (void)fprintf(stderr, "%s:%d: %s returned %d, expected %d\n", \
                __FILE__, __LINE__, #expression, (int)actual_status__, (int)(expected)); \
            ++g_failures; \
            return; \
        } \
    } while (0)

/* The key class is decided by order alone, so tag observes which representative was retained. */
typedef struct test_key {
    int order;
    int tag;
} test_key;

/* Value equivalence is decided by magnitude alone, so tag observes which representative was
 * stored and proves that a tag-only rewrite is a semantic no-op. */
typedef struct test_value {
    int magnitude;
    int tag;
} test_value;

typedef struct test_context {
    size_t allocation_calls;
    size_t outstanding_allocations;
    size_t fail_allocation_at;
    size_t key_copy_calls;
    size_t key_copies;
    size_t key_destroy_calls;
    size_t fail_key_copy_at;
    size_t value_copy_calls;
    size_t value_copies;
    size_t value_destroy_calls;
    size_t fail_value_copy_at;
    size_t compare_calls;
    size_t fail_compare_at;
    size_t equal_calls;
    size_t fail_equal_at;
} test_context;

static void* tracked_allocate(size_t size, void* context)
{
    test_context* state = (test_context*)context;
    void* allocation = NULL;
    ++state->allocation_calls;
    if (state->fail_allocation_at != 0 &&
        state->allocation_calls == state->fail_allocation_at) {
        return NULL;
    }
    allocation = malloc(size);
    if (allocation != NULL) {
        ++state->outstanding_allocations;
    }
    return allocation;
}

static void tracked_deallocate(void* allocation, void* context)
{
    test_context* state = (test_context*)context;
    if (allocation != NULL) {
        --state->outstanding_allocations;
        free(allocation);
    }
}

static ft_status tracked_key_copy(void* destination, const void* source, void* context)
{
    test_context* state = (test_context*)context;
    ++state->key_copy_calls;
    if (state->fail_key_copy_at != 0 &&
        state->key_copy_calls == state->fail_key_copy_at) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    *(test_key*)destination = *(const test_key*)source;
    ++state->key_copies;
    return FT_STATUS_OK;
}

static void tracked_key_destroy(void* value, void* context)
{
    test_context* state = (test_context*)context;
    (void)value;
    ++state->key_destroy_calls;
}

static ft_status tracked_value_copy(void* destination, const void* source, void* context)
{
    test_context* state = (test_context*)context;
    ++state->value_copy_calls;
    if (state->fail_value_copy_at != 0 &&
        state->value_copy_calls == state->fail_value_copy_at) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    *(test_value*)destination = *(const test_value*)source;
    ++state->value_copies;
    return FT_STATUS_OK;
}

static void tracked_value_destroy(void* value, void* context)
{
    test_context* state = (test_context*)context;
    (void)value;
    ++state->value_destroy_calls;
}

static ft_status tracked_compare(
    const void* left,
    const void* right,
    int* comparison,
    void* context)
{
    test_context* state = (test_context*)context;
    const int first = ((const test_key*)left)->order;
    const int second = ((const test_key*)right)->order;
    ++state->compare_calls;
    if (state->fail_compare_at != 0 && state->compare_calls == state->fail_compare_at) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    *comparison = (first > second) - (first < second);
    return FT_STATUS_OK;
}

static ft_status descending_compare(
    const void* left,
    const void* right,
    int* comparison,
    void* context)
{
    const ft_status status = tracked_compare(right, left, comparison, context);
    return status;
}

static ft_status tracked_equal(
    const void* left,
    const void* right,
    bool* equal,
    void* context)
{
    test_context* state = (test_context*)context;
    ++state->equal_calls;
    if (state->fail_equal_at != 0 && state->equal_calls == state->fail_equal_at) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    *equal = ((const test_value*)left)->magnitude == ((const test_value*)right)->magnitude;
    return FT_STATUS_OK;
}

static void init_config(
    ft_delta_map_policy_config* config,
    test_context* context,
    ft_delta_map_compare_fn compare)
{
    ft_delta_map_policy_config_init(
        config,
        sizeof(test_key),
        &g_key_type_identity,
        compare,
        sizeof(test_value),
        &g_value_type_identity,
        tracked_equal,
        context);
    config->key_copy = tracked_key_copy;
    config->key_destroy = tracked_key_destroy;
    config->value_copy = tracked_value_copy;
    config->value_destroy = tracked_value_destroy;
    config->allocator.allocate = tracked_allocate;
    config->allocator.deallocate = tracked_deallocate;
    config->allocator.context = context;
}

static test_key make_key(int order, int tag)
{
    test_key key;
    key.order = order;
    key.tag = tag;
    return key;
}

static test_value make_value(int magnitude, int tag)
{
    test_value value;
    value.magnitude = magnitude;
    value.tag = tag;
    return value;
}

static bool map_valid(const ft_delta_map* map)
{
    bool valid = false;
    ft_delta_map_statistics statistics;
    return ft_delta_map_validate(map, &valid, &statistics) == FT_STATUS_OK && valid &&
        statistics.size == ft_delta_map_size(map) &&
        statistics.change_count == ft_delta_map_change_count(map) &&
        statistics.added_count + statistics.removed_count + statistics.updated_count ==
            statistics.change_count;
}

enum { log_capacity = 512 };

typedef struct entry_log {
    size_t count;
    size_t recorded;
    int orders[log_capacity];
    int key_tags[log_capacity];
    int magnitudes[log_capacity];
    int value_tags[log_capacity];
    size_t stop_after;
} entry_log;

static void log_reset(entry_log* log)
{
    (void)memset(log, 0, sizeof(*log));
}

static ft_status log_entry(const void* key, const void* value, void* context)
{
    entry_log* log = (entry_log*)context;
    if (log->recorded != log_capacity) {
        log->orders[log->recorded] = ((const test_key*)key)->order;
        log->key_tags[log->recorded] = ((const test_key*)key)->tag;
        log->magnitudes[log->recorded] = ((const test_value*)value)->magnitude;
        log->value_tags[log->recorded] = ((const test_value*)value)->tag;
        ++log->recorded;
    }
    ++log->count;
    if (log->stop_after != 0 && log->count == log->stop_after) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    return FT_STATUS_OK;
}

typedef struct change_log {
    size_t count;
    size_t recorded;
    int orders[log_capacity];
    int key_tags[log_capacity];
    bool before_present[log_capacity];
    int before_magnitudes[log_capacity];
    bool after_present[log_capacity];
    int after_magnitudes[log_capacity];
    ft_delta_map_change_kind kinds[log_capacity];
    bool kind_failed;
} change_log;

static void change_reset(change_log* log)
{
    (void)memset(log, 0, sizeof(*log));
}

static ft_status log_change(const ft_delta_map_change* change, void* context)
{
    change_log* log = (change_log*)context;
    if (log->recorded != log_capacity) {
        const size_t slot = log->recorded;
        ft_delta_map_change_kind kind = FT_DELTA_MAP_CHANGE_ADDED;
        log->orders[slot] = ((const test_key*)change->key)->order;
        log->key_tags[slot] = ((const test_key*)change->key)->tag;
        log->before_present[slot] = change->before.has_value;
        log->before_magnitudes[slot] = change->before.has_value
            ? ((const test_value*)change->before.value)->magnitude
            : 0;
        log->after_present[slot] = change->after.has_value;
        log->after_magnitudes[slot] = change->after.has_value
            ? ((const test_value*)change->after.value)->magnitude
            : 0;
        if (ft_delta_map_change_kind_of(change, &kind) != FT_STATUS_OK) {
            log->kind_failed = true;
        }
        log->kinds[slot] = kind;
        ++log->recorded;
    }
    ++log->count;
    return FT_STATUS_OK;
}

static bool orders_match(const int* observed, const int* expected, size_t count)
{
    size_t index = 0;
    for (index = 0; index != count; ++index) {
        if (observed[index] != expected[index]) {
            return false;
        }
    }
    return true;
}

static void test_point_edits_and_change_classification(void)
{
    test_context context;
    ft_delta_map_policy_config config;
    ft_delta_map_policy policy;
    ft_delta_map source;
    ft_delta_map changed;
    test_key keys[2];
    test_value values[2];
    entry_log entries;
    change_log changes;
    const int expected_changed_keys[3] = {0, 1, 2};
    const int expected_current_keys[2] = {0, 1};
    bool found = false;
    ft_delta_map_change change;
    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context, tracked_compare);
    REQUIRE_STATUS(ft_delta_map_policy_create(&policy, &config), FT_STATUS_OK);
    keys[0] = make_key(1, 100);
    keys[1] = make_key(2, 200);
    values[0] = make_value(11, 0);
    values[1] = make_value(22, 0);
    REQUIRE_STATUS(
        ft_delta_map_from_entries(&source, &policy, keys, values, 2), FT_STATUS_OK);
    REQUIRE(map_valid(&source));
    REQUIRE(!ft_delta_map_has_changes(&source));
    REQUIRE(ft_delta_map_change_count(&source) == 0);
    REQUIRE(ft_delta_map_current_identity(&source) ==
        ft_delta_map_checkpoint_identity(&source));

    {
        const test_key added = make_key(0, 300);
        const test_value added_value = make_value(0, 0);
        const test_key updated = make_key(1, 999);
        const test_value updated_value = make_value(111, 0);
        const test_key removed = make_key(2, 999);
        REQUIRE_STATUS(
            ft_delta_map_set_item(&source, &added, &added_value, &changed), FT_STATUS_OK);
        REQUIRE_STATUS(
            ft_delta_map_set_item(&changed, &updated, &updated_value, &changed), FT_STATUS_OK);
        REQUIRE_STATUS(ft_delta_map_remove(&changed, &removed, &changed), FT_STATUS_OK);
    }
    REQUIRE(map_valid(&changed));
    REQUIRE(ft_delta_map_change_count(&changed) == 3);
    REQUIRE(ft_delta_map_has_changes(&changed));
    REQUIRE(ft_delta_map_size(&changed) == 2);
    REQUIRE(ft_delta_map_checkpoint_size(&changed) == 2);

    log_reset(&entries);
    REQUIRE_STATUS(ft_delta_map_visit(&changed, log_entry, &entries), FT_STATUS_OK);
    REQUIRE(entries.count == 2);
    REQUIRE(orders_match(entries.orders, expected_current_keys, 2));
    REQUIRE(entries.magnitudes[0] == 0 && entries.magnitudes[1] == 111);
    /* The updated class keeps its baseline representative, not the probe's tag. */
    REQUIRE(entries.key_tags[0] == 300 && entries.key_tags[1] == 100);

    change_reset(&changes);
    REQUIRE_STATUS(ft_delta_map_visit_changes(&changed, log_change, &changes), FT_STATUS_OK);
    REQUIRE(changes.count == 3 && !changes.kind_failed);
    REQUIRE(orders_match(changes.orders, expected_changed_keys, 3));
    REQUIRE(changes.kinds[0] == FT_DELTA_MAP_CHANGE_ADDED);
    REQUIRE(!changes.before_present[0] && changes.after_present[0]);
    REQUIRE(changes.after_magnitudes[0] == 0);
    REQUIRE(changes.kinds[1] == FT_DELTA_MAP_CHANGE_UPDATED);
    REQUIRE(changes.before_present[1] && changes.before_magnitudes[1] == 11);
    REQUIRE(changes.after_present[1] && changes.after_magnitudes[1] == 111);
    REQUIRE(changes.key_tags[1] == 100);
    REQUIRE(changes.kinds[2] == FT_DELTA_MAP_CHANGE_REMOVED);
    REQUIRE(changes.before_present[2] && changes.before_magnitudes[2] == 22);
    REQUIRE(!changes.after_present[2]);
    REQUIRE(changes.key_tags[2] == 200);

    {
        const test_key probe = make_key(1, -1);
        const test_key absent = make_key(99, -1);
        REQUIRE_STATUS(
            ft_delta_map_try_get_change_ref(&changed, &probe, &found, &change), FT_STATUS_OK);
        REQUIRE(found);
        REQUIRE(((const test_key*)change.key)->tag == 100);
        REQUIRE(change.before.has_value && change.after.has_value);
        REQUIRE(((const test_value*)change.before.value)->magnitude == 11);
        REQUIRE_STATUS(
            ft_delta_map_try_get_change_ref(&changed, &absent, &found, &change), FT_STATUS_OK);
        REQUIRE(!found);
        {
            test_key key_copy = make_key(-7, -7);
            test_value before_copy = make_value(-7, -7);
            test_value after_copy = make_value(-7, -7);
            bool before_present = false;
            bool after_present = false;
            const test_key removed_probe = make_key(2, -1);
            REQUIRE_STATUS(
                ft_delta_map_try_get_change_copy(
                    &changed, &removed_probe, &found, &key_copy,
                    &before_present, &before_copy, &after_present, &after_copy),
                FT_STATUS_OK);
            REQUIRE(found && before_present && !after_present);
            REQUIRE(key_copy.order == 2 && key_copy.tag == 200);
            REQUIRE(before_copy.magnitude == 22);
            REQUIRE(after_copy.magnitude == -7);
            tracked_key_destroy(&key_copy, &context);
            tracked_value_destroy(&before_copy, &context);
        }
    }
    {
        /* An endpoint pair absent on both sides is not a net change and is rejected. */
        ft_delta_map_change absent_change;
        ft_delta_map_change_kind kind = FT_DELTA_MAP_CHANGE_UPDATED;
        absent_change.key = NULL;
        absent_change.before.has_value = false;
        absent_change.before.value = NULL;
        absent_change.after.has_value = false;
        absent_change.after.value = NULL;
        REQUIRE_STATUS(
            ft_delta_map_change_kind_of(&absent_change, &kind), FT_STATUS_INVALID_ARGUMENT);
        REQUIRE(kind == FT_DELTA_MAP_CHANGE_UPDATED);
    }

    /* The source version is untouched by every successor. */
    log_reset(&entries);
    REQUIRE_STATUS(ft_delta_map_visit(&source, log_entry, &entries), FT_STATUS_OK);
    REQUIRE(entries.count == 2 && entries.magnitudes[0] == 11 && entries.magnitudes[1] == 22);
    change_reset(&changes);
    REQUIRE_STATUS(ft_delta_map_visit_changes(&source, log_change, &changes), FT_STATUS_OK);
    REQUIRE(changes.count == 0);

    /* A visitor returning non-OK aborts the traversal and propagates. */
    log_reset(&entries);
    entries.stop_after = 1;
    REQUIRE_STATUS(
        ft_delta_map_visit(&changed, log_entry, &entries), FT_STATUS_CALLBACK_FAILURE);
    REQUIRE(entries.count == 1);

    ft_delta_map_dispose(&changed);
    ft_delta_map_dispose(&source);
    ft_delta_map_policy_dispose(&policy);
    REQUIRE(context.key_copies == context.key_destroy_calls);
    REQUIRE(context.value_copies == context.value_destroy_calls);
    REQUIRE(context.outstanding_allocations == 0);
}

static void test_coalescing_cancellation_and_representatives(void)
{
    test_context context;
    ft_delta_map_policy_config config;
    ft_delta_map_policy policy;
    ft_delta_map source;
    ft_delta_map first;
    ft_delta_map second;
    ft_delta_map restored;
    ft_delta_map probe_result;
    test_key keys[1];
    test_value values[1];
    change_log changes;
    entry_log entries;
    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context, tracked_compare);
    REQUIRE_STATUS(ft_delta_map_policy_create(&policy, &config), FT_STATUS_OK);
    keys[0] = make_key(1, 100);
    values[0] = make_value(11, 500);
    REQUIRE_STATUS(
        ft_delta_map_from_entries(&source, &policy, keys, values, 1), FT_STATUS_OK);

    {
        /* A value-equivalent rewrite and an absent removal are semantic no-ops sharing storage. */
        const test_key probe = make_key(1, 777);
        const test_value equivalent = make_value(11, 999);
        const test_key absent = make_key(99, 0);
        REQUIRE_STATUS(
            ft_delta_map_set_item(&source, &probe, &equivalent, &probe_result), FT_STATUS_OK);
        REQUIRE(ft_delta_map_shares_storage(&source, &probe_result));
        ft_delta_map_dispose(&probe_result);
        REQUIRE_STATUS(ft_delta_map_remove(&source, &absent, &probe_result), FT_STATUS_OK);
        REQUIRE(ft_delta_map_shares_storage(&source, &probe_result));
        ft_delta_map_dispose(&probe_result);
    }
    {
        const test_key probe = make_key(1, 777);
        const test_value two = make_value(22, 0);
        const test_value three = make_value(33, 0);
        const test_value original = make_value(11, 42);
        REQUIRE_STATUS(ft_delta_map_set_item(&source, &probe, &two, &first), FT_STATUS_OK);
        REQUIRE_STATUS(ft_delta_map_set_item(&first, &probe, &three, &second), FT_STATUS_OK);
        change_reset(&changes);
        REQUIRE_STATUS(ft_delta_map_visit_changes(&second, log_change, &changes), FT_STATUS_OK);
        /* Repeated writes coalesce into one record that keeps the original before endpoint. */
        REQUIRE(changes.count == 1 && ft_delta_map_change_count(&second) == 1);
        REQUIRE(changes.kinds[0] == FT_DELTA_MAP_CHANGE_UPDATED);
        REQUIRE(changes.before_magnitudes[0] == 11 && changes.after_magnitudes[0] == 33);
        REQUIRE(changes.key_tags[0] == 100);

        REQUIRE_STATUS(ft_delta_map_set_item(&second, &probe, &original, &restored), FT_STATUS_OK);
        REQUIRE(!ft_delta_map_has_changes(&restored));
        /* Returning the class to its checkpoint state snaps the current root back exactly. */
        REQUIRE(ft_delta_map_current_identity(&restored) ==
            ft_delta_map_current_identity(&source));
        REQUIRE(ft_delta_map_current_identity(&restored) ==
            ft_delta_map_checkpoint_identity(&restored));
        REQUIRE(map_valid(&restored));
        log_reset(&entries);
        REQUIRE_STATUS(ft_delta_map_visit(&restored, log_entry, &entries), FT_STATUS_OK);
        REQUIRE(entries.count == 1 && entries.value_tags[0] == 500);
        ft_delta_map_dispose(&restored);

        /* The retained intermediate versions still report their own coalesced state. */
        change_reset(&changes);
        REQUIRE_STATUS(ft_delta_map_visit_changes(&first, log_change, &changes), FT_STATUS_OK);
        REQUIRE(changes.count == 1 && changes.after_magnitudes[0] == 22);
        change_reset(&changes);
        REQUIRE_STATUS(ft_delta_map_visit_changes(&second, log_change, &changes), FT_STATUS_OK);
        REQUIRE(changes.count == 1 && changes.after_magnitudes[0] == 33);
        ft_delta_map_dispose(&second);
        ft_delta_map_dispose(&first);
    }
    {
        /* An added class keeps its first representative while its record is active, and a delete
         * of that class cancels the record instead of recording a removal. */
        const test_key added = make_key(5, 111);
        const test_key equivalent_added = make_key(5, 222);
        const test_value one = make_value(1, 0);
        const test_value two = make_value(2, 0);
        ft_delta_map added_map;
        ft_delta_map cancelled;
        ft_delta_map episode;
        REQUIRE_STATUS(ft_delta_map_set_item(&source, &added, &one, &added_map), FT_STATUS_OK);
        REQUIRE_STATUS(
            ft_delta_map_set_item(&added_map, &equivalent_added, &two, &added_map), FT_STATUS_OK);
        change_reset(&changes);
        REQUIRE_STATUS(
            ft_delta_map_visit_changes(&added_map, log_change, &changes), FT_STATUS_OK);
        REQUIRE(changes.count == 1 && changes.kinds[0] == FT_DELTA_MAP_CHANGE_ADDED);
        REQUIRE(changes.key_tags[0] == 111 && changes.after_magnitudes[0] == 2);
        REQUIRE(map_valid(&added_map));

        REQUIRE_STATUS(
            ft_delta_map_remove(&added_map, &equivalent_added, &cancelled), FT_STATUS_OK);
        REQUIRE(!ft_delta_map_has_changes(&cancelled));
        REQUIRE(ft_delta_map_current_identity(&cancelled) ==
            ft_delta_map_current_identity(&source));
        /* After full cancellation a later addition begins a new representative episode. */
        REQUIRE_STATUS(
            ft_delta_map_set_item(&cancelled, &equivalent_added, &one, &episode), FT_STATUS_OK);
        change_reset(&changes);
        REQUIRE_STATUS(ft_delta_map_visit_changes(&episode, log_change, &changes), FT_STATUS_OK);
        REQUIRE(changes.count == 1 && changes.key_tags[0] == 222);
        REQUIRE(map_valid(&episode));
        ft_delta_map_dispose(&episode);
        ft_delta_map_dispose(&cancelled);
        ft_delta_map_dispose(&added_map);
    }
    {
        /* A delete and re-add round trip keeps the baseline representative and cancels. */
        const test_key probe = make_key(1, 777);
        const test_value original = make_value(11, 0);
        ft_delta_map removed;
        ft_delta_map readded;
        REQUIRE_STATUS(ft_delta_map_remove(&source, &probe, &removed), FT_STATUS_OK);
        REQUIRE(ft_delta_map_change_count(&removed) == 1);
        REQUIRE(ft_delta_map_empty(&removed));
        REQUIRE_STATUS(
            ft_delta_map_set_item(&removed, &probe, &original, &readded), FT_STATUS_OK);
        REQUIRE(!ft_delta_map_has_changes(&readded));
        REQUIRE(ft_delta_map_current_identity(&readded) ==
            ft_delta_map_current_identity(&source));
        log_reset(&entries);
        REQUIRE_STATUS(ft_delta_map_visit(&readded, log_entry, &entries), FT_STATUS_OK);
        REQUIRE(entries.count == 1 && entries.key_tags[0] == 100);
        ft_delta_map_dispose(&readded);
        ft_delta_map_dispose(&removed);
    }
    {
        /* Cancelling one class beside another change keeps the surviving record and rewrites the
         * stored value, and a later write re-dirties from the retained current value. */
        const test_key first_key = make_key(1, 777);
        const test_key other_key = make_key(9, 900);
        const test_value other = make_value(90, 0);
        const test_value dirty = make_value(22, 0);
        const test_value restored_value = make_value(11, 4242);
        const test_value gamma = make_value(77, 0);
        ft_delta_map two_changes;
        ft_delta_map one_cancelled;
        ft_delta_map redirtied;
        REQUIRE_STATUS(
            ft_delta_map_set_item(&source, &other_key, &other, &two_changes), FT_STATUS_OK);
        REQUIRE_STATUS(
            ft_delta_map_set_item(&two_changes, &first_key, &dirty, &two_changes), FT_STATUS_OK);
        REQUIRE(ft_delta_map_change_count(&two_changes) == 2);
        REQUIRE_STATUS(
            ft_delta_map_set_item(&two_changes, &first_key, &restored_value, &one_cancelled),
            FT_STATUS_OK);
        REQUIRE(ft_delta_map_change_count(&one_cancelled) == 1);
        REQUIRE(map_valid(&one_cancelled));
        log_reset(&entries);
        REQUIRE_STATUS(ft_delta_map_visit(&one_cancelled, log_entry, &entries), FT_STATUS_OK);
        REQUIRE(entries.count == 2 && entries.value_tags[0] == 4242);
        REQUIRE(entries.key_tags[0] == 100);
        REQUIRE_STATUS(
            ft_delta_map_set_item(&one_cancelled, &first_key, &gamma, &redirtied), FT_STATUS_OK);
        change_reset(&changes);
        REQUIRE_STATUS(ft_delta_map_visit_changes(&redirtied, log_change, &changes), FT_STATUS_OK);
        REQUIRE(changes.count == 2);
        REQUIRE(changes.before_present[0] && changes.before_magnitudes[0] == 11);
        REQUIRE(changes.after_magnitudes[0] == 77);
        REQUIRE(map_valid(&redirtied));
        ft_delta_map_dispose(&redirtied);
        ft_delta_map_dispose(&one_cancelled);
        ft_delta_map_dispose(&two_changes);
    }

    ft_delta_map_dispose(&source);
    ft_delta_map_policy_dispose(&policy);
    REQUIRE(context.key_copies == context.key_destroy_calls);
    REQUIRE(context.value_copies == context.value_destroy_calls);
    REQUIRE(context.outstanding_allocations == 0);
}

static void test_checkpoint_rollback_and_branching(void)
{
    test_context context;
    ft_delta_map_policy_config config;
    ft_delta_map_policy policy;
    ft_delta_map root;
    ft_delta_map dirty;
    ft_delta_map checkpoint;
    ft_delta_map rollback;
    test_key keys[2];
    test_value values[2];
    entry_log entries;
    change_log changes;
    size_t comparisons = 0;
    size_t equalities = 0;
    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context, tracked_compare);
    REQUIRE_STATUS(ft_delta_map_policy_create(&policy, &config), FT_STATUS_OK);
    keys[0] = make_key(1, 10);
    keys[1] = make_key(2, 20);
    values[0] = make_value(1, 0);
    values[1] = make_value(2, 0);
    REQUIRE_STATUS(ft_delta_map_from_entries(&root, &policy, keys, values, 2), FT_STATUS_OK);

    {
        const test_key one = make_key(1, 0);
        const test_key three = make_key(3, 30);
        const test_value updated = make_value(111, 0);
        const test_value added = make_value(3, 0);
        REQUIRE_STATUS(ft_delta_map_set_item(&root, &one, &updated, &dirty), FT_STATUS_OK);
        REQUIRE_STATUS(ft_delta_map_set_item(&dirty, &three, &added, &dirty), FT_STATUS_OK);
    }
    REQUIRE(ft_delta_map_change_count(&dirty) == 2);
    REQUIRE(ft_delta_map_checkpoint_identity(&dirty) == ft_delta_map_current_identity(&root));

    comparisons = context.compare_calls;
    equalities = context.equal_calls;
    REQUIRE_STATUS(ft_delta_map_checkpoint(&dirty, &checkpoint), FT_STATUS_OK);
    REQUIRE_STATUS(ft_delta_map_rollback(&dirty, &rollback), FT_STATUS_OK);
    change_reset(&changes);
    REQUIRE_STATUS(ft_delta_map_visit_changes(&dirty, log_change, &changes), FT_STATUS_OK);
    /* Checkpoint, rollback, and change enumeration invoke no key or value policy callback. */
    REQUIRE(context.compare_calls == comparisons);
    REQUIRE(context.equal_calls == equalities);
    REQUIRE(changes.count == 2);

    REQUIRE(ft_delta_map_current_identity(&checkpoint) == ft_delta_map_current_identity(&dirty));
    REQUIRE(ft_delta_map_current_identity(&checkpoint) ==
        ft_delta_map_checkpoint_identity(&checkpoint));
    REQUIRE(!ft_delta_map_has_changes(&checkpoint));
    REQUIRE(ft_delta_map_current_identity(&rollback) ==
        ft_delta_map_checkpoint_identity(&dirty));
    REQUIRE(ft_delta_map_current_identity(&rollback) ==
        ft_delta_map_checkpoint_identity(&rollback));
    REQUIRE(!ft_delta_map_has_changes(&rollback));
    REQUIRE(map_valid(&checkpoint));
    REQUIRE(map_valid(&rollback));
    {
        /* A clean version is its own checkpoint and its own rollback. */
        ft_delta_map again;
        REQUIRE_STATUS(ft_delta_map_checkpoint(&checkpoint, &again), FT_STATUS_OK);
        REQUIRE(ft_delta_map_shares_storage(&checkpoint, &again));
        ft_delta_map_dispose(&again);
        REQUIRE_STATUS(ft_delta_map_rollback(&checkpoint, &again), FT_STATUS_OK);
        REQUIRE(ft_delta_map_shares_storage(&checkpoint, &again));
        ft_delta_map_dispose(&again);
        REQUIRE_STATUS(ft_delta_map_rollback(&rollback, &again), FT_STATUS_OK);
        REQUIRE(ft_delta_map_shares_storage(&rollback, &again));
        ft_delta_map_dispose(&again);
    }
    log_reset(&entries);
    REQUIRE_STATUS(ft_delta_map_visit(&rollback, log_entry, &entries), FT_STATUS_OK);
    REQUIRE(entries.count == 2 && entries.magnitudes[0] == 1 && entries.magnitudes[1] == 2);
    log_reset(&entries);
    REQUIRE_STATUS(ft_delta_map_visit(&checkpoint, log_entry, &entries), FT_STATUS_OK);
    REQUIRE(entries.count == 3);
    REQUIRE(entries.magnitudes[0] == 111 && entries.magnitudes[2] == 3);

    {
        /* Retained branches evolve independently across checkpoints. */
        ft_delta_map left;
        ft_delta_map right;
        ft_delta_map left_checkpoint;
        ft_delta_map branch_a;
        ft_delta_map branch_b;
        const test_key one = make_key(1, 0);
        const test_key two = make_key(2, 0);
        const test_key three = make_key(3, 0);
        const test_value left_value = make_value(50, 0);
        const test_value right_value = make_value(60, 0);
        const test_value branch_value = make_value(70, 0);
        REQUIRE_STATUS(ft_delta_map_set_item(&root, &one, &left_value, &left), FT_STATUS_OK);
        REQUIRE_STATUS(ft_delta_map_set_item(&root, &three, &right_value, &right), FT_STATUS_OK);
        REQUIRE(ft_delta_map_checkpoint_identity(&left) == ft_delta_map_current_identity(&root));
        REQUIRE(ft_delta_map_checkpoint_identity(&right) == ft_delta_map_current_identity(&root));
        REQUIRE(!ft_delta_map_has_changes(&root));
        REQUIRE_STATUS(ft_delta_map_checkpoint(&left, &left_checkpoint), FT_STATUS_OK);
        REQUIRE_STATUS(ft_delta_map_remove(&left_checkpoint, &two, &branch_a), FT_STATUS_OK);
        REQUIRE_STATUS(
            ft_delta_map_set_item(&left_checkpoint, &one, &branch_value, &branch_b),
            FT_STATUS_OK);
        REQUIRE(ft_delta_map_checkpoint_identity(&branch_a) ==
            ft_delta_map_current_identity(&left_checkpoint));
        REQUIRE(ft_delta_map_checkpoint_identity(&branch_b) ==
            ft_delta_map_current_identity(&left_checkpoint));
        change_reset(&changes);
        REQUIRE_STATUS(ft_delta_map_visit_changes(&branch_a, log_change, &changes), FT_STATUS_OK);
        REQUIRE(changes.count == 1 && changes.orders[0] == 2);
        change_reset(&changes);
        REQUIRE_STATUS(ft_delta_map_visit_changes(&branch_b, log_change, &changes), FT_STATUS_OK);
        REQUIRE(changes.count == 1 && changes.orders[0] == 1);
        log_reset(&entries);
        REQUIRE_STATUS(ft_delta_map_visit(&branch_a, log_entry, &entries), FT_STATUS_OK);
        REQUIRE(entries.count == 1 && entries.magnitudes[0] == 50);
        log_reset(&entries);
        REQUIRE_STATUS(ft_delta_map_visit(&branch_b, log_entry, &entries), FT_STATUS_OK);
        REQUIRE(entries.count == 2 && entries.magnitudes[0] == 70);
        log_reset(&entries);
        REQUIRE_STATUS(ft_delta_map_visit(&root, log_entry, &entries), FT_STATUS_OK);
        REQUIRE(entries.count == 2 && entries.magnitudes[0] == 1);
        log_reset(&entries);
        REQUIRE_STATUS(ft_delta_map_visit(&right, log_entry, &entries), FT_STATUS_OK);
        REQUIRE(entries.count == 3 && entries.magnitudes[2] == 60);
        REQUIRE(map_valid(&branch_a) && map_valid(&branch_b) && map_valid(&right));
        ft_delta_map_dispose(&branch_b);
        ft_delta_map_dispose(&branch_a);
        ft_delta_map_dispose(&left_checkpoint);
        ft_delta_map_dispose(&right);
        ft_delta_map_dispose(&left);
    }
    {
        /* Neighbor, rank, and extreme reads answer from the current state. */
        const test_key probe = make_key(2, 0);
        const void* key_ref = NULL;
        const void* value_ref = NULL;
        bool found = false;
        size_t index = 0;
        test_key key_copy = make_key(-1, -1);
        test_value value_copy = make_value(-1, -1);
        REQUIRE_STATUS(
            ft_delta_map_try_get_entry_ref(&checkpoint, &probe, &found, &key_ref, &value_ref),
            FT_STATUS_OK);
        REQUIRE(found && ((const test_key*)key_ref)->tag == 20);
        REQUIRE(((const test_value*)value_ref)->magnitude == 2);
        REQUIRE_STATUS(
            ft_delta_map_try_get_entry_copy(&checkpoint, &probe, &found, &key_copy, &value_copy),
            FT_STATUS_OK);
        REQUIRE(found && key_copy.tag == 20 && value_copy.magnitude == 2);
        tracked_key_destroy(&key_copy, &context);
        tracked_value_destroy(&value_copy, &context);
        REQUIRE_STATUS(
            ft_delta_map_index_of_key(&checkpoint, &probe, &found, &index), FT_STATUS_OK);
        REQUIRE(found && index == 1);
        REQUIRE_STATUS(ft_delta_map_contains_key(&checkpoint, &probe, &found), FT_STATUS_OK);
        REQUIRE(found);
        REQUIRE_STATUS(
            ft_delta_map_entry_at_ref(&checkpoint, 2, &key_ref, &value_ref), FT_STATUS_OK);
        REQUIRE(((const test_key*)key_ref)->order == 3);
        REQUIRE(
            ft_delta_map_entry_at_ref(&checkpoint, 3, &key_ref, &value_ref) ==
            FT_STATUS_OUT_OF_RANGE);
        REQUIRE_STATUS(
            ft_delta_map_try_min_entry_ref(&checkpoint, &found, &key_ref, &value_ref),
            FT_STATUS_OK);
        REQUIRE(found && ((const test_key*)key_ref)->order == 1);
        REQUIRE_STATUS(
            ft_delta_map_try_max_entry_ref(&checkpoint, &found, &key_ref, &value_ref),
            FT_STATUS_OK);
        REQUIRE(found && ((const test_key*)key_ref)->order == 3);
        REQUIRE_STATUS(
            ft_delta_map_try_floor_entry_ref(&checkpoint, &probe, &found, &key_ref, NULL),
            FT_STATUS_OK);
        REQUIRE(found && ((const test_key*)key_ref)->order == 2);
        REQUIRE_STATUS(
            ft_delta_map_try_lower_entry_ref(&checkpoint, &probe, &found, &key_ref, NULL),
            FT_STATUS_OK);
        REQUIRE(found && ((const test_key*)key_ref)->order == 1);
        REQUIRE_STATUS(
            ft_delta_map_try_ceiling_entry_ref(&checkpoint, &probe, &found, &key_ref, NULL),
            FT_STATUS_OK);
        REQUIRE(found && ((const test_key*)key_ref)->order == 2);
        REQUIRE_STATUS(
            ft_delta_map_try_higher_entry_ref(&checkpoint, &probe, &found, &key_ref, NULL),
            FT_STATUS_OK);
        REQUIRE(found && ((const test_key*)key_ref)->order == 3);
        {
            const test_key above = make_key(100, 0);
            const test_key below = make_key(-100, 0);
            REQUIRE_STATUS(
                ft_delta_map_try_higher_entry_ref(&checkpoint, &above, &found, &key_ref, NULL),
                FT_STATUS_OK);
            REQUIRE(!found);
            REQUIRE_STATUS(
                ft_delta_map_try_floor_entry_copy(
                    &checkpoint, &below, &found, &key_copy, &value_copy),
                FT_STATUS_OK);
            REQUIRE(!found);
        }
        {
            /* The empty map answers every extreme and neighbor query with absence. */
            ft_delta_map empty;
            REQUIRE_STATUS(ft_delta_map_init(&empty, &policy), FT_STATUS_OK);
            REQUIRE(ft_delta_map_empty(&empty) && ft_delta_map_size(&empty) == 0);
            REQUIRE(!ft_delta_map_has_changes(&empty) && map_valid(&empty));
            REQUIRE(ft_delta_map_current_identity(&empty) == NULL);
            REQUIRE_STATUS(
                ft_delta_map_try_min_entry_ref(&empty, &found, &key_ref, &value_ref),
                FT_STATUS_OK);
            REQUIRE(!found);
            REQUIRE_STATUS(
                ft_delta_map_try_ceiling_entry_ref(&empty, &probe, &found, &key_ref, NULL),
                FT_STATUS_OK);
            REQUIRE(!found);
            REQUIRE_STATUS(ft_delta_map_contains_key(&empty, &probe, &found), FT_STATUS_OK);
            REQUIRE(!found);
            log_reset(&entries);
            REQUIRE_STATUS(ft_delta_map_visit(&empty, log_entry, &entries), FT_STATUS_OK);
            REQUIRE(entries.count == 0);
            {
                ft_delta_map removed;
                REQUIRE_STATUS(ft_delta_map_remove(&empty, &probe, &removed), FT_STATUS_OK);
                REQUIRE(ft_delta_map_shares_storage(&empty, &removed));
                ft_delta_map_dispose(&removed);
            }
            ft_delta_map_dispose(&empty);
        }
    }

    ft_delta_map_dispose(&rollback);
    ft_delta_map_dispose(&checkpoint);
    ft_delta_map_dispose(&dirty);
    ft_delta_map_dispose(&root);
    ft_delta_map_policy_dispose(&policy);
    REQUIRE(context.key_copies == context.key_destroy_calls);
    REQUIRE(context.value_copies == context.value_destroy_calls);
    REQUIRE(context.outstanding_allocations == 0);
}

static ft_status build_sequential(
    ft_delta_map* map,
    const ft_delta_map_policy* policy,
    int count)
{
    ft_status status = FT_STATUS_OK;
    test_key* keys = (test_key*)malloc((size_t)count * sizeof(*keys));
    test_value* values = (test_value*)malloc((size_t)count * sizeof(*values));
    int index = 0;
    if (keys == NULL || values == NULL) {
        free(keys);
        free(values);
        return FT_STATUS_NO_MEMORY;
    }
    for (index = 0; index != count; ++index) {
        keys[index] = make_key(index, index);
        values[index] = make_value(index, 0);
    }
    status = ft_delta_map_from_entries(map, policy, keys, values, (size_t)count);
    free(keys);
    free(values);
    return status;
}

static void test_range_enumeration_and_output_sensitivity(void)
{
    test_context context;
    ft_delta_map_policy_config config;
    ft_delta_map_policy policy;
    ft_delta_map clean;
    ft_delta_map changed;
    change_log changes;
    entry_log entries;
    const test_key minimum = make_key(-1000000, 0);
    const test_key maximum = make_key(1000000, 0);
    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context, tracked_compare);
    REQUIRE_STATUS(ft_delta_map_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(build_sequential(&clean, &policy, 10), FT_STATUS_OK);

    change_reset(&changes);
    REQUIRE_STATUS(
        ft_delta_map_visit_changes_in_range(&clean, &minimum, &maximum, log_change, &changes),
        FT_STATUS_OK);
    REQUIRE(changes.count == 0);
    {
        const test_key low = make_key(9, 0);
        const test_key high = make_key(0, 0);
        change_reset(&changes);
        REQUIRE_STATUS(
            ft_delta_map_visit_changes_in_range(&clean, &low, &high, log_change, &changes),
            FT_STATUS_OK);
        REQUIRE(changes.count == 0);
        log_reset(&entries);
        REQUIRE_STATUS(
            ft_delta_map_visit_range(&clean, &low, &high, log_entry, &entries), FT_STATUS_OK);
        REQUIRE(entries.count == 0);
    }
    {
        const test_key minus_five = make_key(-5, 0);
        const test_key two = make_key(2, 0);
        const test_key four = make_key(4, 0);
        const test_key seven = make_key(7, 0);
        const test_key twenty = make_key(20, 0);
        const test_value replacement = make_value(-1, 0);
        REQUIRE_STATUS(
            ft_delta_map_set_item(&clean, &minus_five, &replacement, &changed), FT_STATUS_OK);
        REQUIRE_STATUS(
            ft_delta_map_set_item(&changed, &two, &replacement, &changed), FT_STATUS_OK);
        REQUIRE_STATUS(ft_delta_map_remove(&changed, &four, &changed), FT_STATUS_OK);
        REQUIRE_STATUS(
            ft_delta_map_set_item(&changed, &seven, &replacement, &changed), FT_STATUS_OK);
        REQUIRE_STATUS(
            ft_delta_map_set_item(&changed, &twenty, &replacement, &changed), FT_STATUS_OK);
    }
    REQUIRE(map_valid(&changed));
    {
        const int all[5] = {-5, 2, 4, 7, 20};
        change_reset(&changes);
        REQUIRE_STATUS(ft_delta_map_visit_changes(&changed, log_change, &changes), FT_STATUS_OK);
        REQUIRE(changes.count == 5 && orders_match(changes.orders, all, 5));
    }
    {
        struct {
            int low;
            int high;
            size_t count;
            int expected[5];
        } cases[9] = {
            {2, 7, 3, {2, 4, 7, 0, 0}},
            {1, 5, 2, {2, 4, 0, 0, 0}},
            {4, 4, 1, {4, 0, 0, 0, 0}},
            {-1000000, 0, 1, {-5, 0, 0, 0, 0}},
            {8, 1000000, 1, {20, 0, 0, 0, 0}},
            {8, 19, 0, {0, 0, 0, 0, 0}},
            {21, 100, 0, {0, 0, 0, 0, 0}},
            {-100, -6, 0, {0, 0, 0, 0, 0}},
            {7, 2, 0, {0, 0, 0, 0, 0}}
        };
        size_t index = 0;
        for (index = 0; index != sizeof(cases) / sizeof(cases[0]); ++index) {
            const test_key low = make_key(cases[index].low, 0);
            const test_key high = make_key(cases[index].high, 0);
            change_reset(&changes);
            REQUIRE_STATUS(
                ft_delta_map_visit_changes_in_range(&changed, &low, &high, log_change, &changes),
                FT_STATUS_OK);
            REQUIRE(changes.count == cases[index].count);
            REQUIRE(orders_match(changes.orders, cases[index].expected, changes.count));
        }
    }
    {
        /* Both endpoints of an in-range record survive the restriction. */
        const test_key low = make_key(2, 0);
        const test_key high = make_key(4, 0);
        change_reset(&changes);
        REQUIRE_STATUS(
            ft_delta_map_visit_changes_in_range(&changed, &low, &high, log_change, &changes),
            FT_STATUS_OK);
        REQUIRE(changes.count == 2);
        REQUIRE(changes.kinds[0] == FT_DELTA_MAP_CHANGE_UPDATED);
        REQUIRE(changes.before_magnitudes[0] == 2 && changes.after_magnitudes[0] == -1);
        REQUIRE(changes.kinds[1] == FT_DELTA_MAP_CHANGE_REMOVED);
        REQUIRE(changes.before_magnitudes[1] == 4 && !changes.after_present[1]);
    }
    {
        /* The current-state range read is inclusive and seeks its boundaries too. */
        const test_key low = make_key(3, 0);
        const test_key high = make_key(6, 0);
        const int expected[3] = {3, 5, 6};
        log_reset(&entries);
        REQUIRE_STATUS(
            ft_delta_map_visit_range(&changed, &low, &high, log_entry, &entries), FT_STATUS_OK);
        REQUIRE(entries.count == 3 && orders_match(entries.orders, expected, 3));
    }
    ft_delta_map_dispose(&changed);
    ft_delta_map_dispose(&clean);

    {
        /* Under a descending key policy the low endpoint is the numerically greater key. */
        ft_delta_map_policy_config descending_config;
        ft_delta_map_policy descending_policy;
        ft_delta_map reversed;
        const test_key one = make_key(1, 0);
        const test_key four = make_key(4, 0);
        const test_key eight = make_key(8, 0);
        const test_value replacement = make_value(-1, 0);
        const int expected[3] = {8, 4, 1};
        const int windowed[2] = {8, 4};
        init_config(&descending_config, &context, descending_compare);
        REQUIRE_STATUS(
            ft_delta_map_policy_create(&descending_policy, &descending_config), FT_STATUS_OK);
        REQUIRE_STATUS(build_sequential(&reversed, &descending_policy, 10), FT_STATUS_OK);
        REQUIRE_STATUS(ft_delta_map_set_item(&reversed, &one, &replacement, &reversed),
            FT_STATUS_OK);
        REQUIRE_STATUS(ft_delta_map_set_item(&reversed, &four, &replacement, &reversed),
            FT_STATUS_OK);
        REQUIRE_STATUS(ft_delta_map_set_item(&reversed, &eight, &replacement, &reversed),
            FT_STATUS_OK);
        REQUIRE(map_valid(&reversed));
        change_reset(&changes);
        REQUIRE_STATUS(ft_delta_map_visit_changes(&reversed, log_change, &changes), FT_STATUS_OK);
        REQUIRE(changes.count == 3 && orders_match(changes.orders, expected, 3));
        change_reset(&changes);
        REQUIRE_STATUS(
            ft_delta_map_visit_changes_in_range(&reversed, &eight, &four, log_change, &changes),
            FT_STATUS_OK);
        REQUIRE(changes.count == 2 && orders_match(changes.orders, windowed, 2));
        change_reset(&changes);
        REQUIRE_STATUS(
            ft_delta_map_visit_changes_in_range(&reversed, &four, &eight, log_change, &changes),
            FT_STATUS_OK);
        REQUIRE(changes.count == 0);
        ft_delta_map_dispose(&reversed);
        ft_delta_map_policy_dispose(&descending_policy);
    }
    {
        /* A fixed-size delta enumerates without any policy callback, no matter how large the
         * baseline is, and a restricted window seeks rather than scanning all k records. */
        enum { small_count = 128, large_count = 4096 };
        ft_delta_map small;
        ft_delta_map large;
        size_t comparisons = 0;
        size_t equalities = 0;
        int index = 0;
        REQUIRE_STATUS(build_sequential(&small, &policy, small_count), FT_STATUS_OK);
        REQUIRE_STATUS(build_sequential(&large, &policy, large_count), FT_STATUS_OK);
        {
            const test_key added = make_key(-1, 0);
            const test_key middle = make_key(small_count / 2, 0);
            const test_key last = make_key(small_count - 1, 0);
            const test_value replacement = make_value(-1, 0);
            REQUIRE_STATUS(
                ft_delta_map_set_item(&small, &added, &replacement, &small), FT_STATUS_OK);
            REQUIRE_STATUS(
                ft_delta_map_set_item(&small, &middle, &replacement, &small), FT_STATUS_OK);
            REQUIRE_STATUS(ft_delta_map_remove(&small, &last, &small), FT_STATUS_OK);
        }
        {
            const test_key added = make_key(-1, 0);
            const test_key middle = make_key(large_count / 2, 0);
            const test_key last = make_key(large_count - 1, 0);
            const test_value replacement = make_value(-1, 0);
            REQUIRE_STATUS(
                ft_delta_map_set_item(&large, &added, &replacement, &large), FT_STATUS_OK);
            REQUIRE_STATUS(
                ft_delta_map_set_item(&large, &middle, &replacement, &large), FT_STATUS_OK);
            REQUIRE_STATUS(ft_delta_map_remove(&large, &last, &large), FT_STATUS_OK);
        }
        REQUIRE(ft_delta_map_change_count(&small) == 3);
        REQUIRE(ft_delta_map_change_count(&large) == 3);
        comparisons = context.compare_calls;
        equalities = context.equal_calls;
        change_reset(&changes);
        REQUIRE_STATUS(ft_delta_map_visit_changes(&small, log_change, &changes), FT_STATUS_OK);
        {
            const int expected[3] = {-1, small_count / 2, small_count - 1};
            REQUIRE(changes.count == 3 && orders_match(changes.orders, expected, 3));
        }
        REQUIRE(context.compare_calls == comparisons && context.equal_calls == equalities);
        change_reset(&changes);
        REQUIRE_STATUS(ft_delta_map_visit_changes(&large, log_change, &changes), FT_STATUS_OK);
        {
            const int expected[3] = {-1, large_count / 2, large_count - 1};
            REQUIRE(changes.count == 3 && orders_match(changes.orders, expected, 3));
        }
        REQUIRE(context.compare_calls == comparisons && context.equal_calls == equalities);
        ft_delta_map_dispose(&small);

        {
            ft_delta_map windowed;
            const int expected[4] = {1000, 1002, 1004, 1006};
            const test_key low = make_key(1000, 0);
            const test_key high = make_key(1007, 0);
            test_key* keys = (test_key*)malloc((large_count / 2) * sizeof(*keys));
            test_value* values = (test_value*)malloc((large_count / 2) * sizeof(*values));
            REQUIRE(keys != NULL && values != NULL);
            for (index = 0; index != large_count / 2; ++index) {
                keys[index] = make_key(index * 2, 0);
                values[index] = make_value(-index - 1, 0);
            }
            REQUIRE_STATUS(ft_delta_map_rollback(&large, &windowed), FT_STATUS_OK);
            REQUIRE_STATUS(
                ft_delta_map_set_items(
                    &windowed, keys, values, (size_t)(large_count / 2), &windowed),
                FT_STATUS_OK);
            free(keys);
            free(values);
            REQUIRE(ft_delta_map_change_count(&windowed) == large_count / 2);
            comparisons = context.compare_calls;
            equalities = context.equal_calls;
            change_reset(&changes);
            REQUIRE_STATUS(
                ft_delta_map_visit_changes_in_range(
                    &windowed, &low, &high, log_change, &changes),
                FT_STATUS_OK);
            REQUIRE(changes.count == 4 && orders_match(changes.orders, expected, 4));
            REQUIRE(context.equal_calls == equalities);
            REQUIRE(context.compare_calls - comparisons < 512);
            ft_delta_map_dispose(&windowed);
        }
        ft_delta_map_dispose(&large);
    }

    ft_delta_map_policy_dispose(&policy);
    REQUIRE(context.key_copies == context.key_destroy_calls);
    REQUIRE(context.value_copies == context.value_destroy_calls);
    REQUIRE(context.outstanding_allocations == 0);
}

static void test_set_items_matches_the_single_entry_fold(void)
{
    enum { batch_count = 6 };
    test_context context;
    ft_delta_map_policy_config config;
    ft_delta_map_policy policy;
    ft_delta_map source;
    ft_delta_map folded;
    ft_delta_map bulk;
    test_key keys[3];
    test_value values[3];
    test_key batch_keys[batch_count];
    test_value batch_values[batch_count];
    entry_log folded_entries;
    entry_log bulk_entries;
    change_log folded_changes;
    change_log bulk_changes;
    size_t index = 0;
    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context, tracked_compare);
    REQUIRE_STATUS(ft_delta_map_policy_create(&policy, &config), FT_STATUS_OK);
    keys[0] = make_key(1, 10);
    keys[1] = make_key(2, 20);
    keys[2] = make_key(3, 30);
    values[0] = make_value(1, 0);
    values[1] = make_value(2, 0);
    values[2] = make_value(3, 0);
    REQUIRE_STATUS(ft_delta_map_from_entries(&source, &policy, keys, values, 3), FT_STATUS_OK);

    batch_keys[0] = make_key(4, 40);
    batch_values[0] = make_value(4, 0);
    batch_keys[1] = make_key(2, 21);
    batch_values[1] = make_value(22, 0);
    batch_keys[2] = make_key(2, 22);
    batch_values[2] = make_value(222, 0);
    batch_keys[3] = make_key(1, 11);
    batch_values[3] = make_value(1, 0);
    batch_keys[4] = make_key(4, 41);
    batch_values[4] = make_value(44, 0);
    batch_keys[5] = make_key(3, 31);
    batch_values[5] = make_value(3, 0);

    REQUIRE_STATUS(ft_delta_map_copy(&source, &folded), FT_STATUS_OK);
    for (index = 0; index != batch_count; ++index) {
        REQUIRE_STATUS(
            ft_delta_map_set_item(&folded, &batch_keys[index], &batch_values[index], &folded),
            FT_STATUS_OK);
    }
    REQUIRE_STATUS(
        ft_delta_map_set_items(&source, batch_keys, batch_values, batch_count, &bulk),
        FT_STATUS_OK);

    log_reset(&folded_entries);
    log_reset(&bulk_entries);
    REQUIRE_STATUS(ft_delta_map_visit(&folded, log_entry, &folded_entries), FT_STATUS_OK);
    REQUIRE_STATUS(ft_delta_map_visit(&bulk, log_entry, &bulk_entries), FT_STATUS_OK);
    REQUIRE(folded_entries.count == bulk_entries.count && bulk_entries.count == 4);
    REQUIRE(orders_match(folded_entries.orders, bulk_entries.orders, 4));
    REQUIRE(orders_match(folded_entries.key_tags, bulk_entries.key_tags, 4));
    REQUIRE(orders_match(folded_entries.magnitudes, bulk_entries.magnitudes, 4));
    change_reset(&folded_changes);
    change_reset(&bulk_changes);
    REQUIRE_STATUS(ft_delta_map_visit_changes(&folded, log_change, &folded_changes), FT_STATUS_OK);
    REQUIRE_STATUS(ft_delta_map_visit_changes(&bulk, log_change, &bulk_changes), FT_STATUS_OK);
    REQUIRE(folded_changes.count == bulk_changes.count && bulk_changes.count == 2);
    REQUIRE(orders_match(folded_changes.orders, bulk_changes.orders, 2));
    REQUIRE(orders_match(folded_changes.after_magnitudes, bulk_changes.after_magnitudes, 2));
    REQUIRE(bulk_changes.orders[0] == 2 && bulk_changes.orders[1] == 4);
    REQUIRE(bulk_entries.magnitudes[1] == 222 && bulk_entries.magnitudes[3] == 44);
    /* An added class keeps its first representative even across a batch. */
    REQUIRE(bulk_entries.key_tags[1] == 20 && bulk_entries.key_tags[3] == 40);
    REQUIRE(map_valid(&bulk) && map_valid(&folded));
    ft_delta_map_dispose(&bulk);
    ft_delta_map_dispose(&folded);

    {
        /* An empty batch and an all-no-op batch share every root. */
        ft_delta_map empty_batch;
        ft_delta_map noop_batch;
        test_key noop_keys[2];
        test_value noop_values[2];
        noop_keys[0] = make_key(1, 0);
        noop_values[0] = make_value(1, 0);
        noop_keys[1] = make_key(3, 0);
        noop_values[1] = make_value(3, 0);
        REQUIRE_STATUS(
            ft_delta_map_set_items(&source, NULL, NULL, 0, &empty_batch), FT_STATUS_OK);
        REQUIRE(ft_delta_map_shares_storage(&source, &empty_batch));
        REQUIRE_STATUS(
            ft_delta_map_set_items(&source, noop_keys, noop_values, 2, &noop_batch),
            FT_STATUS_OK);
        REQUIRE(ft_delta_map_shares_storage(&source, &noop_batch));
        ft_delta_map_dispose(&noop_batch);
        ft_delta_map_dispose(&empty_batch);
    }
    {
        /* A batch that ends by returning every touched class to its checkpoint state cancels and
         * snaps back, exactly as the equivalent point writes would. */
        ft_delta_map cancelled;
        test_key round_keys[3];
        test_value round_values[3];
        const test_key five = make_key(5, 0);
        round_keys[0] = make_key(2, 0);
        round_values[0] = make_value(22, 0);
        round_keys[1] = make_key(5, 50);
        round_values[1] = make_value(5, 0);
        round_keys[2] = make_key(2, 0);
        round_values[2] = make_value(2, 0);
        REQUIRE_STATUS(
            ft_delta_map_set_items(&source, round_keys, round_values, 3, &cancelled),
            FT_STATUS_OK);
        REQUIRE(ft_delta_map_change_count(&cancelled) == 1);
        REQUIRE_STATUS(ft_delta_map_remove(&cancelled, &five, &cancelled), FT_STATUS_OK);
        REQUIRE(!ft_delta_map_has_changes(&cancelled));
        REQUIRE(ft_delta_map_current_identity(&cancelled) ==
            ft_delta_map_current_identity(&source));
        REQUIRE(ft_delta_map_current_identity(&cancelled) ==
            ft_delta_map_checkpoint_identity(&cancelled));
        ft_delta_map_dispose(&cancelled);
    }

    ft_delta_map_dispose(&source);
    ft_delta_map_policy_dispose(&policy);
    REQUIRE(context.key_copies == context.key_destroy_calls);
    REQUIRE(context.value_copies == context.value_destroy_calls);
    REQUIRE(context.outstanding_allocations == 0);
}

enum { model_key_span = 40, retained_capacity = 24 };

typedef struct model_state {
    bool present[model_key_span];
    int magnitude[model_key_span];
} model_state;

typedef struct model_version {
    ft_delta_map map;
    model_state current;
    model_state checkpoint;
} model_version;

static uint64_t next_random(uint64_t* state)
{
    uint64_t value = *state;
    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    *state = value;
    return value;
}

static bool model_matches(const model_version* version)
{
    entry_log entries;
    change_log changes;
    size_t expected = 0;
    size_t index = 0;
    int key = 0;
    log_reset(&entries);
    if (ft_delta_map_visit(&version->map, log_entry, &entries) != FT_STATUS_OK) {
        return false;
    }
    for (key = 0; key != model_key_span; ++key) {
        if (!version->current.present[key]) {
            continue;
        }
        if (index == entries.count || entries.orders[index] != key ||
            entries.magnitudes[index] != version->current.magnitude[key]) {
            return false;
        }
        ++index;
    }
    if (index != entries.count || entries.count != ft_delta_map_size(&version->map)) {
        return false;
    }
    log_reset(&entries);
    if (ft_delta_map_visit_checkpoint(&version->map, log_entry, &entries) != FT_STATUS_OK) {
        return false;
    }
    index = 0;
    for (key = 0; key != model_key_span; ++key) {
        if (!version->checkpoint.present[key]) {
            continue;
        }
        if (index == entries.count || entries.orders[index] != key ||
            entries.magnitudes[index] != version->checkpoint.magnitude[key]) {
            return false;
        }
        ++index;
    }
    if (index != entries.count ||
        entries.count != ft_delta_map_checkpoint_size(&version->map)) {
        return false;
    }
    change_reset(&changes);
    if (ft_delta_map_visit_changes(&version->map, log_change, &changes) != FT_STATUS_OK) {
        return false;
    }
    index = 0;
    for (key = 0; key != model_key_span; ++key) {
        const bool before = version->checkpoint.present[key];
        const bool after = version->current.present[key];
        bool found = false;
        ft_delta_map_change observed;
        const test_key probe = make_key(key, 0);
        if (before == after &&
            (!before || version->checkpoint.magnitude[key] == version->current.magnitude[key])) {
            if (ft_delta_map_try_get_change_ref(&version->map, &probe, &found, &observed) !=
                    FT_STATUS_OK ||
                found) {
                return false;
            }
            continue;
        }
        ++expected;
        if (index == changes.count || changes.orders[index] != key ||
            changes.before_present[index] != before || changes.after_present[index] != after) {
            return false;
        }
        if (before && changes.before_magnitudes[index] != version->checkpoint.magnitude[key]) {
            return false;
        }
        if (after && changes.after_magnitudes[index] != version->current.magnitude[key]) {
            return false;
        }
        if (ft_delta_map_try_get_change_ref(&version->map, &probe, &found, &observed) !=
                FT_STATUS_OK ||
            !found || ((const test_key*)observed.key)->order != key ||
            observed.before.has_value != before || observed.after.has_value != after) {
            return false;
        }
        ++index;
    }
    if (index != changes.count || changes.kind_failed) {
        return false;
    }
    if (expected != ft_delta_map_change_count(&version->map)) {
        return false;
    }
    if ((expected != 0) != ft_delta_map_has_changes(&version->map)) {
        return false;
    }
    if (expected == 0 && ft_delta_map_current_identity(&version->map) !=
            ft_delta_map_checkpoint_identity(&version->map)) {
        return false;
    }
    return map_valid(&version->map);
}

static void test_randomized_history_matches_the_model(void)
{
    enum { operation_count = 4000 };
    test_context context;
    ft_delta_map_policy_config config;
    ft_delta_map_policy policy;
    model_version current;
    model_version retained[retained_capacity];
    size_t retained_count = 0;
    uint64_t random = UINT64_C(0x5eed0ffeeba11ad);
    int step = 0;
    size_t index = 0;
    (void)memset(&context, 0, sizeof(context));
    (void)memset(&current, 0, sizeof(current));
    (void)memset(retained, 0, sizeof(retained));
    init_config(&config, &context, tracked_compare);
    REQUIRE_STATUS(ft_delta_map_policy_create(&policy, &config), FT_STATUS_OK);
    {
        test_key keys[model_key_span / 2];
        test_value values[model_key_span / 2];
        int key = 0;
        for (key = 0; key != model_key_span / 2; ++key) {
            keys[key] = make_key(key * 2, key);
            values[key] = make_value(key * 10, 0);
            current.current.present[key * 2] = true;
            current.current.magnitude[key * 2] = key * 10;
        }
        current.checkpoint = current.current;
        REQUIRE_STATUS(
            ft_delta_map_from_entries(
                &current.map, &policy, keys, values, model_key_span / 2),
            FT_STATUS_OK);
    }
    REQUIRE(model_matches(&current));

    for (step = 0; step != operation_count; ++step) {
        const uint64_t choice = next_random(&random) % 100;
        const int key = (int)(next_random(&random) % model_key_span);
        const test_key probe = make_key(key, 1000 + step);
        if (choice < 46) {
            const int magnitude = (int)(next_random(&random) % 9) - 4;
            const test_value value = make_value(magnitude, step);
            const bool was_no_op = current.current.present[key] &&
                current.current.magnitude[key] == magnitude;
            const void* before_root = ft_delta_map_current_identity(&current.map);
            const void* before_changes = ft_delta_map_changes_identity(&current.map);
            REQUIRE_STATUS(
                ft_delta_map_set_item(&current.map, &probe, &value, &current.map),
                FT_STATUS_OK);
            current.current.present[key] = true;
            current.current.magnitude[key] = magnitude;
            if (was_no_op) {
                REQUIRE(ft_delta_map_current_identity(&current.map) == before_root);
                REQUIRE(ft_delta_map_changes_identity(&current.map) == before_changes);
            }
        } else if (choice < 70) {
            const bool was_absent = !current.current.present[key];
            const void* before_root = ft_delta_map_current_identity(&current.map);
            REQUIRE_STATUS(
                ft_delta_map_remove(&current.map, &probe, &current.map), FT_STATUS_OK);
            current.current.present[key] = false;
            if (was_absent) {
                REQUIRE(ft_delta_map_current_identity(&current.map) == before_root);
            }
        } else if (choice < 82) {
            REQUIRE_STATUS(
                ft_delta_map_checkpoint(&current.map, &current.map), FT_STATUS_OK);
            current.checkpoint = current.current;
        } else if (choice < 94) {
            REQUIRE_STATUS(ft_delta_map_rollback(&current.map, &current.map), FT_STATUS_OK);
            current.current = current.checkpoint;
        } else {
            bool found = false;
            const void* value_ref = NULL;
            REQUIRE_STATUS(
                ft_delta_map_try_get_entry_ref(&current.map, &probe, &found, NULL, &value_ref),
                FT_STATUS_OK);
            REQUIRE(found == current.current.present[key]);
            if (found) {
                REQUIRE(((const test_value*)value_ref)->magnitude ==
                    current.current.magnitude[key]);
            }
        }
        REQUIRE(model_matches(&current));
        if (step % 149 == 0) {
            const size_t slot = retained_count != retained_capacity
                ? retained_count++
                : (size_t)(next_random(&random) % retained_capacity);
            if (slot + 1 != retained_count || retained_count == retained_capacity) {
                ft_delta_map_dispose(&retained[slot].map);
            }
            REQUIRE_STATUS(
                ft_delta_map_copy(&current.map, &retained[slot].map), FT_STATUS_OK);
            retained[slot].current = current.current;
            retained[slot].checkpoint = current.checkpoint;
        }
    }
    /* Every retained version still reproduces the model it was branched from. */
    for (index = 0; index != retained_count; ++index) {
        REQUIRE(model_matches(&retained[index]));
        ft_delta_map_dispose(&retained[index].map);
    }
    ft_delta_map_dispose(&current.map);
    ft_delta_map_policy_dispose(&policy);
    REQUIRE(context.key_copies == context.key_destroy_calls);
    REQUIRE(context.value_copies == context.value_destroy_calls);
    REQUIRE(context.outstanding_allocations == 0);
}

typedef union map_output {
    ft_delta_map map;
    unsigned char bytes[sizeof(ft_delta_map)];
} map_output;

static void require_unchanged_bytes(const map_output* output, const map_output* expected)
{
    if (memcmp(output->bytes, expected->bytes, sizeof(output->bytes)) != 0) {
        fail_at(__FILE__, __LINE__, "failure output remained byte-identical");
    }
}

typedef struct failure_probe {
    size_t* counter;
    size_t* failpoint;
    ft_status expected;
} failure_probe;

static void test_failure_atomicity_and_lifetimes(void)
{
    enum { count = 16 };
    test_context context;
    ft_delta_map_policy_config config;
    ft_delta_map_policy policy;
    ft_delta_map base;
    test_key keys[count];
    test_value values[count];
    entry_log entries;
    change_log changes;
    failure_probe probes[4];
    size_t probe_index = 0;
    int index = 0;
    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context, tracked_compare);
    REQUIRE_STATUS(ft_delta_map_policy_create(&policy, &config), FT_STATUS_OK);
    for (index = 0; index != count; ++index) {
        keys[index] = make_key(index, index);
        values[index] = make_value(index, 0);
    }
    REQUIRE_STATUS(
        ft_delta_map_from_entries(&base, &policy, keys, values, count), FT_STATUS_OK);
    {
        const test_key dirty_key = make_key(3, 0);
        const test_value dirty_value = make_value(300, 0);
        REQUIRE_STATUS(
            ft_delta_map_set_item(&base, &dirty_key, &dirty_value, &base), FT_STATUS_OK);
    }
    REQUIRE(ft_delta_map_change_count(&base) == 1);

    probes[0].counter = &context.allocation_calls;
    probes[0].failpoint = &context.fail_allocation_at;
    probes[0].expected = FT_STATUS_NO_MEMORY;
    probes[1].counter = &context.compare_calls;
    probes[1].failpoint = &context.fail_compare_at;
    probes[1].expected = FT_STATUS_CALLBACK_FAILURE;
    probes[2].counter = &context.value_copy_calls;
    probes[2].failpoint = &context.fail_value_copy_at;
    probes[2].expected = FT_STATUS_CALLBACK_FAILURE;
    probes[3].counter = &context.equal_calls;
    probes[3].failpoint = &context.fail_equal_at;
    probes[3].expected = FT_STATUS_CALLBACK_FAILURE;

    for (probe_index = 0; probe_index != 4; ++probe_index) {
        const failure_probe* probe = &probes[probe_index];
        const test_key added = make_key(100, 7);
        const test_key removed = make_key(5, 0);
        const test_value value = make_value(1000, 0);
        size_t budget = 0;
        size_t offset = 0;
        {
            /* Learn how many of this probe's events a successful operation needs. */
            ft_delta_map successful;
            const size_t before = *probe->counter;
            REQUIRE_STATUS(
                ft_delta_map_set_item(&base, &added, &value, &successful), FT_STATUS_OK);
            budget = *probe->counter - before;
            ft_delta_map_dispose(&successful);
        }
        for (offset = 1; offset <= budget; ++offset) {
            map_output output;
            map_output expected;
            const void* root = ft_delta_map_current_identity(&base);
            (void)memset(output.bytes, 0xa5, sizeof(output.bytes));
            (void)memcpy(expected.bytes, output.bytes, sizeof(output.bytes));
            *probe->failpoint = *probe->counter + offset;
            REQUIRE_STATUS(
                ft_delta_map_set_item(&base, &added, &value, &output.map), probe->expected);
            *probe->failpoint = 0;
            require_unchanged_bytes(&output, &expected);
            REQUIRE(ft_delta_map_current_identity(&base) == root);
            REQUIRE(ft_delta_map_size(&base) == count);
            REQUIRE(map_valid(&base));
        }
        {
            ft_delta_map successful;
            const size_t before = *probe->counter;
            REQUIRE_STATUS(ft_delta_map_remove(&base, &removed, &successful), FT_STATUS_OK);
            budget = *probe->counter - before;
            ft_delta_map_dispose(&successful);
        }
        for (offset = 1; offset <= budget; ++offset) {
            map_output output;
            map_output expected;
            const void* root = ft_delta_map_current_identity(&base);
            (void)memset(output.bytes, 0x5a, sizeof(output.bytes));
            (void)memcpy(expected.bytes, output.bytes, sizeof(output.bytes));
            *probe->failpoint = *probe->counter + offset;
            {
                const ft_status status =
                    ft_delta_map_remove(&base, &removed, &output.map);
                if (status != probe->expected && status != FT_STATUS_OK) {
                    fail_at(__FILE__, __LINE__, "remove failed with an unexpected status");
                    *probe->failpoint = 0;
                    return;
                }
                *probe->failpoint = 0;
                if (status == FT_STATUS_OK) {
                    /* Removal performs no value comparison at all, so an equality failpoint
                     * beyond its budget simply cannot fire. */
                    ft_delta_map_dispose(&output.map);
                    continue;
                }
            }
            require_unchanged_bytes(&output, &expected);
            REQUIRE(ft_delta_map_current_identity(&base) == root);
            REQUIRE(map_valid(&base));
        }
    }
    {
        /* A key copy fails only where a genuinely new class is introduced. */
        const test_key added = make_key(200, 7);
        const test_value value = make_value(2000, 0);
        map_output output;
        map_output expected;
        (void)memset(output.bytes, 0x3c, sizeof(output.bytes));
        (void)memcpy(expected.bytes, output.bytes, sizeof(output.bytes));
        context.fail_key_copy_at = context.key_copy_calls + 1;
        REQUIRE_STATUS(
            ft_delta_map_set_item(&base, &added, &value, &output.map),
            FT_STATUS_CALLBACK_FAILURE);
        context.fail_key_copy_at = 0;
        require_unchanged_bytes(&output, &expected);
        REQUIRE(map_valid(&base));
    }
    {
        /* A batch that fails partway publishes nothing and leaves the receiver unchanged. */
        test_key batch_keys[3];
        test_value batch_values[3];
        size_t offset = 0;
        size_t budget = 0;
        batch_keys[0] = make_key(40, 0);
        batch_values[0] = make_value(40, 0);
        batch_keys[1] = make_key(41, 0);
        batch_values[1] = make_value(41, 0);
        batch_keys[2] = make_key(1, 0);
        batch_values[2] = make_value(-1, 0);
        {
            ft_delta_map successful;
            const size_t before = context.allocation_calls;
            REQUIRE_STATUS(
                ft_delta_map_set_items(&base, batch_keys, batch_values, 3, &successful),
                FT_STATUS_OK);
            budget = context.allocation_calls - before;
            ft_delta_map_dispose(&successful);
        }
        for (offset = 1; offset <= budget; ++offset) {
            map_output output;
            map_output expected;
            const void* root = ft_delta_map_current_identity(&base);
            (void)memset(output.bytes, 0x7e, sizeof(output.bytes));
            (void)memcpy(expected.bytes, output.bytes, sizeof(output.bytes));
            context.fail_allocation_at = context.allocation_calls + offset;
            REQUIRE_STATUS(
                ft_delta_map_set_items(&base, batch_keys, batch_values, 3, &output.map),
                FT_STATUS_NO_MEMORY);
            context.fail_allocation_at = 0;
            require_unchanged_bytes(&output, &expected);
            REQUIRE(ft_delta_map_current_identity(&base) == root);
            REQUIRE(map_valid(&base));
        }
    }
    {
        /* Bulk construction is failure-atomic at every allocation and copy ordinal. */
        size_t offset = 0;
        size_t allocations = 0;
        {
            ft_delta_map successful;
            const size_t before = context.allocation_calls;
            REQUIRE_STATUS(
                ft_delta_map_from_entries(&successful, &policy, keys, values, count),
                FT_STATUS_OK);
            allocations = context.allocation_calls - before;
            ft_delta_map_dispose(&successful);
        }
        for (offset = 1; offset <= allocations; ++offset) {
            map_output output;
            map_output expected;
            (void)memset(output.bytes, 0x19, sizeof(output.bytes));
            (void)memcpy(expected.bytes, output.bytes, sizeof(output.bytes));
            context.fail_allocation_at = context.allocation_calls + offset;
            REQUIRE_STATUS(
                ft_delta_map_from_entries(&output.map, &policy, keys, values, count),
                FT_STATUS_NO_MEMORY);
            context.fail_allocation_at = 0;
            require_unchanged_bytes(&output, &expected);
        }
        for (offset = 1; offset <= count; ++offset) {
            map_output output;
            map_output expected;
            (void)memset(output.bytes, 0x29, sizeof(output.bytes));
            (void)memcpy(expected.bytes, output.bytes, sizeof(output.bytes));
            context.fail_key_copy_at = context.key_copy_calls + offset;
            REQUIRE_STATUS(
                ft_delta_map_from_entries(&output.map, &policy, keys, values, count),
                FT_STATUS_CALLBACK_FAILURE);
            context.fail_key_copy_at = 0;
            require_unchanged_bytes(&output, &expected);
        }
    }
    {
        /* Diagnostic validation reports a failing callback rather than a verdict. */
        bool valid = true;
        ft_delta_map_statistics statistics;
        ft_delta_map_statistics unchanged;
        (void)memset(&statistics, 0x55, sizeof(statistics));
        (void)memcpy(&unchanged, &statistics, sizeof(statistics));
        context.fail_compare_at = context.compare_calls + 1;
        REQUIRE_STATUS(
            ft_delta_map_validate(&base, &valid, &statistics), FT_STATUS_CALLBACK_FAILURE);
        context.fail_compare_at = 0;
        REQUIRE(valid);
        REQUIRE(memcmp(&statistics, &unchanged, sizeof(statistics)) == 0);
    }

    /* The receiver survived every failure with its state and delta intact. */
    log_reset(&entries);
    REQUIRE_STATUS(ft_delta_map_visit(&base, log_entry, &entries), FT_STATUS_OK);
    REQUIRE(entries.count == count);
    REQUIRE(entries.magnitudes[3] == 300);
    change_reset(&changes);
    REQUIRE_STATUS(ft_delta_map_visit_changes(&base, log_change, &changes), FT_STATUS_OK);
    REQUIRE(changes.count == 1 && changes.orders[0] == 3);
    REQUIRE(map_valid(&base));

    ft_delta_map_dispose(&base);
    ft_delta_map_policy_dispose(&policy);
    REQUIRE(context.key_copies == context.key_destroy_calls);
    REQUIRE(context.value_copies == context.value_destroy_calls);
    REQUIRE(context.outstanding_allocations == 0);
}

static void test_handle_lifecycle_and_argument_rejection(void)
{
    test_context context;
    ft_delta_map_policy_config config;
    ft_delta_map_policy policy;
    ft_delta_map_policy policy_copy;
    ft_delta_map_policy moved;
    ft_delta_map_policy other;
    ft_delta_map map;
    ft_delta_map alias;
    ft_delta_map relocated;
    const test_key key = make_key(1, 1);
    const test_value value = make_value(1, 1);
    bool found = false;
    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context, tracked_compare);
    REQUIRE_STATUS(ft_delta_map_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_delta_map_policy_copy(&policy, &policy_copy), FT_STATUS_OK);
    REQUIRE(ft_delta_map_policy_same(&policy, &policy_copy));
    REQUIRE_STATUS(ft_delta_map_policy_create(&other, &config), FT_STATUS_OK);
    REQUIRE(!ft_delta_map_policy_same(&policy, &other));
    ft_delta_map_policy_move(&moved, &policy_copy);
    REQUIRE(ft_delta_map_policy_same(&policy, &moved));
    REQUIRE(policy_copy.rep == NULL);
    ft_delta_map_policy_dispose(&moved);
    ft_delta_map_policy_dispose(&other);

    REQUIRE_STATUS(ft_delta_map_init(&map, &policy), FT_STATUS_OK);
    REQUIRE_STATUS(ft_delta_map_set_item(&map, &key, &value, &map), FT_STATUS_OK);
    REQUIRE_STATUS(ft_delta_map_copy(&map, &alias), FT_STATUS_OK);
    REQUIRE(ft_delta_map_shares_storage(&map, &alias));
    ft_delta_map_move(&relocated, &alias);
    REQUIRE(alias.policy == NULL);
    REQUIRE(ft_delta_map_shares_storage(&map, &relocated));
    REQUIRE_STATUS(ft_delta_map_contains_key(&relocated, &key, &found), FT_STATUS_OK);
    REQUIRE(found);
    {
        ft_delta_map_policy retrieved;
        REQUIRE_STATUS(ft_delta_map_get_policy(&map, &retrieved), FT_STATUS_OK);
        REQUIRE(ft_delta_map_policy_same(&policy, &retrieved));
        ft_delta_map_policy_dispose(&retrieved);
    }
    ft_delta_map_dispose(&relocated);

    /* Null arguments are rejected without touching any output. */
    REQUIRE(ft_delta_map_set_item(&map, NULL, &value, &map) == FT_STATUS_INVALID_ARGUMENT);
    REQUIRE(ft_delta_map_set_item(&map, &key, NULL, &map) == FT_STATUS_INVALID_ARGUMENT);
    REQUIRE(ft_delta_map_set_item(NULL, &key, &value, &map) == FT_STATUS_INVALID_ARGUMENT);
    REQUIRE(ft_delta_map_remove(&map, &key, NULL) == FT_STATUS_INVALID_ARGUMENT);
    REQUIRE(ft_delta_map_visit(&map, NULL, NULL) == FT_STATUS_INVALID_ARGUMENT);
    REQUIRE(ft_delta_map_visit_changes(&map, NULL, NULL) == FT_STATUS_INVALID_ARGUMENT);
    REQUIRE(ft_delta_map_contains_key(&map, &key, NULL) == FT_STATUS_INVALID_ARGUMENT);
    REQUIRE(ft_delta_map_change_kind_of(NULL, NULL) == FT_STATUS_INVALID_ARGUMENT);
    {
        ft_delta_map_policy_config invalid;
        ft_delta_map_policy rejected;
        init_config(&invalid, &context, NULL);
        REQUIRE(ft_delta_map_policy_create(&rejected, &invalid) == FT_STATUS_INVALID_ARGUMENT);
        init_config(&invalid, &context, tracked_compare);
        invalid.value_equal = NULL;
        REQUIRE(ft_delta_map_policy_create(&rejected, &invalid) == FT_STATUS_INVALID_ARGUMENT);
    }

    ft_delta_map_dispose(&map);
    ft_delta_map_policy_dispose(&policy);
    REQUIRE(context.key_copies == context.key_destroy_calls);
    REQUIRE(context.value_copies == context.value_destroy_calls);
    REQUIRE(context.outstanding_allocations == 0);
}

typedef void (*test_fn)(void);

static void run_test(const char* name, test_fn test)
{
    const int before = g_failures;
    test();
    if (before == g_failures) {
        (void)printf("[pass] %s\n", name);
        (void)fflush(stdout);
    } else {
        (void)fprintf(stderr, "[fail] %s\n", name);
        (void)fflush(stderr);
    }
}

int main(void)
{
    if (!d7_enter_headless_test_process()) {
        return EXIT_FAILURE;
    }
    run_test("Delta map point edits and change classification",
        test_point_edits_and_change_classification);
    run_test("Delta map coalescing cancellation and representatives",
        test_coalescing_cancellation_and_representatives);
    run_test("Delta map checkpoint rollback and branching",
        test_checkpoint_rollback_and_branching);
    run_test("Delta map range enumeration and output sensitivity",
        test_range_enumeration_and_output_sensitivity);
    run_test("Delta map bulk assignment matches the fold",
        test_set_items_matches_the_single_entry_fold);
    run_test("Delta map randomized history matches the model",
        test_randomized_history_matches_the_model);
    run_test("Delta map failure atomicity and lifetimes",
        test_failure_atomicity_and_lifetimes);
    run_test("Delta map handle lifecycle and argument rejection",
        test_handle_lifecycle_and_argument_rejection);
    if (g_failures != 0) {
        (void)fprintf(stderr, "%d failure(s)\n", g_failures);
        return EXIT_FAILURE;
    }
    (void)printf("all C persistent delta map tests passed\n");
    return EXIT_SUCCESS;
}
