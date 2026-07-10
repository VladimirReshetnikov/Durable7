#include <tools/data_structures/tungsten/tungsten.h>
#include <tools/data_structures/test_support/headless_test_process.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
typedef volatile LONG test_atomic_long;

static void test_atomic_long_init(test_atomic_long* value, long initial_value)
{
    *value = initial_value;
}

static void test_atomic_long_increment(test_atomic_long* value)
{
    (void)InterlockedIncrement(value);
}

static long test_atomic_long_read(test_atomic_long* value)
{
    return InterlockedCompareExchange(value, 0, 0);
}
#else
#include <stdatomic.h>
typedef atomic_long test_atomic_long;

static void test_atomic_long_init(test_atomic_long* value, long initial_value)
{
    atomic_init(value, initial_value);
}

static void test_atomic_long_increment(test_atomic_long* value)
{
    (void)atomic_fetch_add_explicit(value, 1, memory_order_relaxed);
}

static long test_atomic_long_read(test_atomic_long* value)
{
    return atomic_load_explicit(value, memory_order_relaxed);
}
#endif

static int g_failures = 0;

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

#define REQUIRE_STATUS(expression) \
    do { \
        const tds_tungsten_status actual_status__ = (expression); \
        if (actual_status__ != TDS_TUNGSTEN_OK) { \
            (void)fprintf(stderr, "%s:%d: %s returned %d\n", __FILE__, __LINE__, #expression, actual_status__); \
            ++g_failures; \
            return; \
        } \
    } while (0)

typedef struct int_buffer {
    int values[256];
    size_t count;
} int_buffer;

typedef struct pair_buffer {
    int keys[256];
    int values[256];
    size_t count;
} pair_buffer;

typedef struct model_assoc {
    int keys[64];
    int values[64];
    size_t count;
} model_assoc;

static void init_int_type(ft_value_type* type)
{
    ft_value_type_init(type, sizeof(int));
}

static uint32_t hash_int(const void* key, void* context)
{
    (void)context;
    uint32_t value = (uint32_t)*(const int*)key;
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

static bool equal_int(const void* left, const void* right, void* context)
{
    (void)context;
    return *(const int*)left == *(const int*)right;
}

static int compare_int(const void* left, const void* right, void* context)
{
    (void)context;
    const int left_value = *(const int*)left;
    const int right_value = *(const int*)right;
    return (left_value > right_value) - (left_value < right_value);
}

static uint32_t hash_mod10(const void* key, void* context)
{
    (void)context;
    int value = *(const int*)key % 10;
    if (value < 0) {
        value += 10;
    }
    return (uint32_t)value;
}

static bool equal_mod10(const void* left, const void* right, void* context)
{
    (void)context;
    int left_value = *(const int*)left % 10;
    int right_value = *(const int*)right % 10;
    if (left_value < 0) {
        left_value += 10;
    }
    if (right_value < 0) {
        right_value += 10;
    }
    return left_value == right_value;
}

static void init_assoc_policy(tds_tungsten_association_policy* policy)
{
    ft_value_type int_type;
    init_int_type(&int_type);
    tds_tungsten_association_policy_init(policy, &int_type, &int_type, hash_int, equal_int, NULL);
    policy->value_equal = equal_int;
}

static void init_mod10_policy(tds_tungsten_association_policy* policy)
{
    ft_value_type int_type;
    init_int_type(&int_type);
    tds_tungsten_association_policy_init(policy, &int_type, &int_type, hash_mod10, equal_mod10, NULL);
    policy->value_equal = equal_int;
}

static void collect_int(const void* value, void* context)
{
    int_buffer* buffer = (int_buffer*)context;
    buffer->values[buffer->count++] = *(const int*)value;
}

static void collect_pair(const void* key, const void* value, void* context)
{
    pair_buffer* buffer = (pair_buffer*)context;
    buffer->keys[buffer->count] = *(const int*)key;
    buffer->values[buffer->count] = *(const int*)value;
    ++buffer->count;
}

static bool list_matches(const tds_tungsten_list* list, const int* expected, size_t count)
{
    if (tds_tungsten_list_size(list) != count) {
        return false;
    }

    for (size_t index = 0; index != count; ++index) {
        int actual = 0;
        if (tds_tungsten_list_at(list, index, &actual) != TDS_TUNGSTEN_OK || actual != expected[index]) {
            return false;
        }
    }

    int_buffer buffer;
    buffer.count = 0;
    if (tds_tungsten_list_visit(list, collect_int, &buffer) != TDS_TUNGSTEN_OK || buffer.count != count) {
        return false;
    }

    for (size_t index = 0; index != count; ++index) {
        if (buffer.values[index] != expected[index]) {
            return false;
        }
    }

    return true;
}

static bool assoc_matches(const tds_tungsten_association* association, const int* keys, const int* values, size_t count)
{
    if (tds_tungsten_association_size(association) != count) {
        return false;
    }

    for (size_t index = 0; index != count; ++index) {
        int actual_key = 0;
        int actual_value = 0;
        if (tds_tungsten_association_entry_at(association, index, &actual_key, &actual_value) != TDS_TUNGSTEN_OK ||
            actual_key != keys[index] ||
            actual_value != values[index]) {
            return false;
        }

        int lookup_value = 0;
        if (!tds_tungsten_association_try_get(association, &keys[index], &lookup_value) ||
            lookup_value != values[index]) {
            return false;
        }

        size_t found_index = 0;
        if (!tds_tungsten_association_index_of_key(association, &keys[index], &found_index) ||
            found_index != index) {
            return false;
        }
    }

    pair_buffer buffer;
    buffer.count = 0;
    if (tds_tungsten_association_visit(association, collect_pair, &buffer) != TDS_TUNGSTEN_OK ||
        buffer.count != count) {
        return false;
    }

    for (size_t index = 0; index != count; ++index) {
        if (buffer.keys[index] != keys[index] || buffer.values[index] != values[index]) {
            return false;
        }
    }

    return true;
}

typedef struct concurrent_tungsten_context {
    const tds_tungsten_list* list;
    const int* list_values;
    size_t list_count;
    const tds_tungsten_association* association;
    const int* association_keys;
    const int* association_values;
    size_t association_count;
    test_atomic_long failures;
} concurrent_tungsten_context;

static void record_concurrent_failure(concurrent_tungsten_context* context)
{
    test_atomic_long_increment(&context->failures);
}

static void concurrent_tungsten_worker(concurrent_tungsten_context* context)
{
    for (int pass = 0; pass != 256; ++pass) {
        if (!list_matches(context->list, context->list_values, context->list_count) ||
            !assoc_matches(
                context->association,
                context->association_keys,
                context->association_values,
                context->association_count)) {
            record_concurrent_failure(context);
            return;
        }
    }
}

#ifdef _WIN32
static DWORD WINAPI concurrent_tungsten_thread_proc(void* parameter)
{
    concurrent_tungsten_worker((concurrent_tungsten_context*)parameter);
    return 0;
}
#endif

static void double_int(void* destination, const void* source, void* context)
{
    (void)context;
    *(int*)destination = *(const int*)source * 2;
}

static void test_list_examples(void)
{
    ft_value_type int_type;
    init_int_type(&int_type);

    const int values[] = {1, 2, 3, 4, 5};
    tds_tungsten_list list;
    REQUIRE_STATUS(tds_tungsten_list_from_array(&list, &int_type, values, 5));
    REQUIRE(tds_tungsten_list_size(&list) == 5);

    int front = 0;
    int back = 0;
    REQUIRE_STATUS(tds_tungsten_list_front(&list, &front));
    REQUIRE_STATUS(tds_tungsten_list_back(&list, &back));
    REQUIRE(front == 1);
    REQUIRE(back == 5);

    int inserted_value = 99;
    tds_tungsten_list inserted;
    REQUIRE_STATUS(tds_tungsten_list_insert_at(&list, 2, &inserted_value, &inserted));
    const int inserted_expected[] = {1, 2, 99, 3, 4, 5};
    REQUIRE(list_matches(&inserted, inserted_expected, 6));

    const int range_values[] = {7, 8};
    tds_tungsten_list range_inserted;
    REQUIRE_STATUS(tds_tungsten_list_insert_range(&list, 1, range_values, 2, &range_inserted));
    const int range_expected[] = {1, 7, 8, 2, 3, 4, 5};
    REQUIRE(list_matches(&range_inserted, range_expected, 7));

    tds_tungsten_list removed;
    REQUIRE_STATUS(tds_tungsten_list_remove_range(&range_inserted, 1, 2, &removed));
    REQUIRE(list_matches(&removed, values, 5));

    tds_tungsten_list slice;
    REQUIRE_STATUS(tds_tungsten_list_slice(&list, 1, 3, &slice));
    const int slice_expected[] = {2, 3, 4};
    REQUIRE(list_matches(&slice, slice_expected, 3));

    tds_tungsten_list reversed;
    REQUIRE_STATUS(tds_tungsten_list_reverse(&list, &reversed));
    const int reversed_expected[] = {5, 4, 3, 2, 1};
    REQUIRE(list_matches(&reversed, reversed_expected, 5));

    tds_tungsten_list mapped;
    REQUIRE_STATUS(tds_tungsten_list_map(&list, &int_type, double_int, NULL, &mapped));
    const int mapped_expected[] = {2, 4, 6, 8, 10};
    REQUIRE(list_matches(&mapped, mapped_expected, 5));

    size_t index = 0;
    const int needle = 4;
    REQUIRE(tds_tungsten_list_index_of(&list, &needle, equal_int, NULL, &index));
    REQUIRE(index == 3);

    tds_tungsten_list_dispose(&mapped);
    tds_tungsten_list_dispose(&reversed);
    tds_tungsten_list_dispose(&slice);
    tds_tungsten_list_dispose(&removed);
    tds_tungsten_list_dispose(&range_inserted);
    tds_tungsten_list_dispose(&inserted);
    tds_tungsten_list_dispose(&list);
}

static void test_result_aliasing_is_rejected(void)
{
    ft_value_type int_type;
    init_int_type(&int_type);

    const int values[] = {1, 2, 3};
    tds_tungsten_list list;
    REQUIRE_STATUS(tds_tungsten_list_from_array(&list, &int_type, values, 3));

    /* Every operation must reject a result that aliases the source and leave it intact. */
    const int extra = 4;
    REQUIRE(tds_tungsten_list_push_back(&list, &extra, &list) == TDS_TUNGSTEN_INVALID_ARGUMENT);
    REQUIRE(tds_tungsten_list_insert_range(&list, 0, values, 3, &list) == TDS_TUNGSTEN_INVALID_ARGUMENT);
    REQUIRE(tds_tungsten_list_slice(&list, 0, 2, &list) == TDS_TUNGSTEN_INVALID_ARGUMENT);
    REQUIRE(tds_tungsten_list_reverse(&list, &list) == TDS_TUNGSTEN_INVALID_ARGUMENT);
    REQUIRE(list_matches(&list, values, 3));

    /* An out-of-range drop is a range error, matching take/slice and the association. */
    tds_tungsten_list dropped;
    REQUIRE(tds_tungsten_list_drop(&list, 4, &dropped) == TDS_TUNGSTEN_OUT_OF_RANGE);

    tds_tungsten_association_policy policy;
    init_assoc_policy(&policy);

    tds_tungsten_association association;
    REQUIRE_STATUS(tds_tungsten_association_init(&association, &policy));

    const int key = 1;
    const int value = 10;
    REQUIRE(tds_tungsten_association_set_item(&association, &key, &value, &association) ==
            TDS_TUNGSTEN_INVALID_ARGUMENT);
    REQUIRE(tds_tungsten_association_copy(&association, &association) == TDS_TUNGSTEN_INVALID_ARGUMENT);
    REQUIRE(tds_tungsten_association_size(&association) == 0);

    tds_tungsten_association_dispose(&association);
    tds_tungsten_list_dispose(&list);
}

static tds_tungsten_assoc_pair pair_of(const int* key, const int* value)
{
    tds_tungsten_assoc_pair pair;
    pair.key = key;
    pair.value = value;
    return pair;
}

static void test_association_ordering_examples(void)
{
    tds_tungsten_association_policy policy;
    init_assoc_policy(&policy);

    int k1 = 1;
    int k2 = 2;
    int k3 = 3;
    int v1 = 1;
    int v2 = 2;
    int v3 = 3;
    int v4 = 4;
    int v5 = 5;
    int v9 = 9;

    const tds_tungsten_assoc_pair duplicate_pairs[] = {
        pair_of(&k1, &v1),
        pair_of(&k2, &v2),
        pair_of(&k1, &v3)
    };
    tds_tungsten_association duplicates;
    REQUIRE_STATUS(tds_tungsten_association_from_pairs(&duplicates, &policy, duplicate_pairs, 3));
    const int duplicate_keys[] = {1, 2};
    const int duplicate_values[] = {3, 2};
    REQUIRE(assoc_matches(&duplicates, duplicate_keys, duplicate_values, 2));

    tds_tungsten_association set;
    REQUIRE_STATUS(tds_tungsten_association_set_item(&duplicates, &k1, &v5, &set));
    const int set_values[] = {5, 2};
    REQUIRE(assoc_matches(&set, duplicate_keys, set_values, 2));

    tds_tungsten_association appended;
    REQUIRE_STATUS(tds_tungsten_association_append(&duplicates, &k1, &v9, &appended));
    const int append_keys[] = {2, 1};
    const int append_values[] = {2, 9};
    REQUIRE(assoc_matches(&appended, append_keys, append_values, 2));

    tds_tungsten_association prepended;
    REQUIRE_STATUS(tds_tungsten_association_prepend(&duplicates, &k2, &v9, &prepended));
    const int prepend_keys[] = {2, 1};
    const int prepend_values[] = {9, 3};
    REQUIRE(assoc_matches(&prepended, prepend_keys, prepend_values, 2));

    const tds_tungsten_assoc_pair left_pairs[] = {pair_of(&k1, &v1), pair_of(&k2, &v2)};
    const tds_tungsten_assoc_pair right_pairs[] = {pair_of(&k1, &v3), pair_of(&k3, &v4)};
    tds_tungsten_association left;
    tds_tungsten_association right;
    REQUIRE_STATUS(tds_tungsten_association_from_pairs(&left, &policy, left_pairs, 2));
    REQUIRE_STATUS(tds_tungsten_association_from_pairs(&right, &policy, right_pairs, 2));
    tds_tungsten_association joined;
    REQUIRE_STATUS(tds_tungsten_association_join(&left, &right, &joined));
    const int joined_keys[] = {1, 2, 3};
    const int joined_values[] = {3, 2, 4};
    REQUIRE(assoc_matches(&joined, joined_keys, joined_values, 3));

    tds_tungsten_association inserted;
    REQUIRE_STATUS(tds_tungsten_association_insert_at(&joined, 2, &k1, &v9, &inserted));
    const int inserted_keys[] = {2, 1, 3};
    const int inserted_values[] = {2, 9, 4};
    REQUIRE(assoc_matches(&inserted, inserted_keys, inserted_values, 3));

    tds_tungsten_association taken;
    REQUIRE_STATUS(tds_tungsten_association_take(&joined, 2, &taken));
    const int taken_keys[] = {1, 2};
    const int taken_values[] = {3, 2};
    REQUIRE(assoc_matches(&taken, taken_keys, taken_values, 2));

    tds_tungsten_association dropped;
    REQUIRE_STATUS(tds_tungsten_association_drop(&joined, 1, &dropped));
    const int dropped_keys[] = {2, 3};
    const int dropped_values[] = {2, 4};
    REQUIRE(assoc_matches(&dropped, dropped_keys, dropped_values, 2));

    tds_tungsten_association reversed;
    REQUIRE_STATUS(tds_tungsten_association_reverse(&joined, &reversed));
    const int reversed_keys[] = {3, 2, 1};
    const int reversed_values[] = {4, 2, 3};
    REQUIRE(assoc_matches(&reversed, reversed_keys, reversed_values, 3));

    tds_tungsten_association sorted_by_key;
    REQUIRE_STATUS(tds_tungsten_association_key_sort(&reversed, compare_int, NULL, &sorted_by_key));
    REQUIRE(assoc_matches(&sorted_by_key, joined_keys, joined_values, 3));

    tds_tungsten_association sorted_by_value;
    REQUIRE_STATUS(tds_tungsten_association_sort(&joined, compare_int, NULL, &sorted_by_value));
    const int sorted_value_keys[] = {2, 1, 3};
    const int sorted_value_values[] = {2, 3, 4};
    REQUIRE(assoc_matches(&sorted_by_value, sorted_value_keys, sorted_value_values, 3));

    const void* requested[] = {&k3, &k1, &k3};
    tds_tungsten_association key_taken;
    REQUIRE_STATUS(tds_tungsten_association_key_take(&joined, requested, 3, &key_taken));
    const int key_taken_keys[] = {3, 1};
    const int key_taken_values[] = {4, 3};
    REQUIRE(assoc_matches(&key_taken, key_taken_keys, key_taken_values, 2));

    tds_tungsten_association_dispose(&key_taken);
    tds_tungsten_association_dispose(&sorted_by_value);
    tds_tungsten_association_dispose(&sorted_by_key);
    tds_tungsten_association_dispose(&reversed);
    tds_tungsten_association_dispose(&dropped);
    tds_tungsten_association_dispose(&taken);
    tds_tungsten_association_dispose(&inserted);
    tds_tungsten_association_dispose(&joined);
    tds_tungsten_association_dispose(&right);
    tds_tungsten_association_dispose(&left);
    tds_tungsten_association_dispose(&prepended);
    tds_tungsten_association_dispose(&appended);
    tds_tungsten_association_dispose(&set);
    tds_tungsten_association_dispose(&duplicates);
}

static void test_association_custom_policy(void)
{
    tds_tungsten_association_policy policy;
    init_mod10_policy(&policy);

    int k11 = 11;
    int k21 = 21;
    int k31 = 31;
    int k12 = 12;
    int v1 = 1;
    int v2 = 2;
    int v3 = 3;
    const tds_tungsten_assoc_pair pairs[] = {pair_of(&k11, &v1), pair_of(&k12, &v2)};

    tds_tungsten_association association;
    REQUIRE_STATUS(tds_tungsten_association_from_pairs(&association, &policy, pairs, 2));
    tds_tungsten_association set;
    REQUIRE_STATUS(tds_tungsten_association_set_item(&association, &k21, &v3, &set));
    int actual_key = 0;
    REQUIRE(tds_tungsten_association_try_get_key(&set, &k21, &actual_key));
    REQUIRE(actual_key == 11);
    const int set_keys[] = {11, 12};
    const int set_values[] = {3, 2};
    REQUIRE(assoc_matches(&set, set_keys, set_values, 2));

    tds_tungsten_association appended;
    REQUIRE_STATUS(tds_tungsten_association_append(&set, &k31, &v1, &appended));
    const int append_keys[] = {12, 31};
    const int append_values[] = {2, 1};
    REQUIRE(assoc_matches(&appended, append_keys, append_values, 2));

    /* Rule-2 no-op fast path: a key already terminal with an equal value
     * returns the receiver's content and keeps the stored key payload. */
    int k22 = 22;
    tds_tungsten_association append_noop;
    REQUIRE_STATUS(tds_tungsten_association_append(&set, &k22, &v2, &append_noop));
    REQUIRE(assoc_matches(&append_noop, set_keys, set_values, 2));
    REQUIRE(tds_tungsten_association_try_get_key(&append_noop, &k22, &actual_key));
    REQUIRE(actual_key == 12);

    tds_tungsten_association prepend_noop;
    REQUIRE_STATUS(tds_tungsten_association_prepend(&set, &k21, &v3, &prepend_noop));
    REQUIRE(assoc_matches(&prepend_noop, set_keys, set_values, 2));
    REQUIRE(tds_tungsten_association_try_get_key(&prepend_noop, &k21, &actual_key));
    REQUIRE(actual_key == 11);

    tds_tungsten_association_dispose(&prepend_noop);
    tds_tungsten_association_dispose(&append_noop);
    tds_tungsten_association_dispose(&appended);
    tds_tungsten_association_dispose(&set);
    tds_tungsten_association_dispose(&association);
}

static void test_association_relabel_stress(void)
{
    tds_tungsten_association_policy policy;
    init_assoc_policy(&policy);

    int k0 = 0;
    int v0 = 0;
    int k999 = 999;
    int v999 = 999;
    const tds_tungsten_assoc_pair pairs[] = {pair_of(&k0, &v0), pair_of(&k999, &v999)};
    tds_tungsten_association association;
    REQUIRE_STATUS(tds_tungsten_association_from_pairs(&association, &policy, pairs, 2));

    for (int value = 1; value <= 25; ++value) {
        tds_tungsten_association next;
        REQUIRE_STATUS(tds_tungsten_association_insert_at(&association, 1, &value, &value, &next));
        tds_tungsten_association_dispose(&association);
        tds_tungsten_association_move(&association, &next);
    }

    REQUIRE(tds_tungsten_association_size(&association) == 27);
    int lookup = 0;
    int needle = 25;
    REQUIRE(tds_tungsten_association_try_get(&association, &needle, &lookup));
    REQUIRE(lookup == 25);

    const int expected_prefix[] = {0, 25, 24, 23, 22, 21};
    for (size_t index = 0; index != sizeof(expected_prefix) / sizeof(expected_prefix[0]); ++index) {
        int key = 0;
        int value = 0;
        REQUIRE_STATUS(tds_tungsten_association_entry_at(&association, index, &key, &value));
        REQUIRE(key == expected_prefix[index]);
        REQUIRE(value == expected_prefix[index]);
    }

    tds_tungsten_association_dispose(&association);
}

static void model_delete(model_assoc* model, int key)
{
    for (size_t index = 0; index != model->count; ++index) {
        if (model->keys[index] == key) {
            for (size_t move = index + 1u; move != model->count; ++move) {
                model->keys[move - 1u] = model->keys[move];
                model->values[move - 1u] = model->values[move];
            }
            --model->count;
            return;
        }
    }
}

static void model_set(model_assoc* model, int key, int value)
{
    for (size_t index = 0; index != model->count; ++index) {
        if (model->keys[index] == key) {
            model->values[index] = value;
            return;
        }
    }
    model->keys[model->count] = key;
    model->values[model->count] = value;
    ++model->count;
}

static void model_append(model_assoc* model, int key, int value)
{
    model_delete(model, key);
    model->keys[model->count] = key;
    model->values[model->count] = value;
    ++model->count;
}

static void model_prepend(model_assoc* model, int key, int value)
{
    model_delete(model, key);
    for (size_t index = model->count; index != 0; --index) {
        model->keys[index] = model->keys[index - 1u];
        model->values[index] = model->values[index - 1u];
    }
    model->keys[0] = key;
    model->values[0] = value;
    ++model->count;
}

static void model_insert_at(model_assoc* model, size_t position, int key, int value)
{
    size_t old_position = model->count;
    for (size_t index = 0; index != model->count; ++index) {
        if (model->keys[index] == key) {
            old_position = index;
            break;
        }
    }

    model_delete(model, key);
    if (old_position < position) {
        --position;
    }
    for (size_t index = model->count; index != position; --index) {
        model->keys[index] = model->keys[index - 1u];
        model->values[index] = model->values[index - 1u];
    }
    model->keys[position] = key;
    model->values[position] = value;
    ++model->count;
}

static void model_delete_at(model_assoc* model, size_t position)
{
    if (position >= model->count) {
        return;
    }
    for (size_t index = position + 1u; index != model->count; ++index) {
        model->keys[index - 1u] = model->keys[index];
        model->values[index - 1u] = model->values[index];
    }
    --model->count;
}

static void model_slice(model_assoc* model, size_t start, size_t count)
{
    for (size_t index = 0; index != count; ++index) {
        model->keys[index] = model->keys[start + index];
        model->values[index] = model->values[start + index];
    }
    model->count = count;
}

static bool association_matches_model(const tds_tungsten_association* association, const model_assoc* model)
{
    return assoc_matches(association, model->keys, model->values, model->count);
}

static void test_association_generated_history(void)
{
    tds_tungsten_association_policy policy;
    init_assoc_policy(&policy);

    tds_tungsten_association association;
    REQUIRE_STATUS(tds_tungsten_association_init(&association, &policy));
    model_assoc model;
    (void)memset(&model, 0, sizeof(model));

    for (int command = 0; command != 120; ++command) {
        const int key = (command * 37 + 11) % 17;
        const int value = command * 101 - key;
        tds_tungsten_association next;
        switch (command % 7) {
        case 0:
            REQUIRE_STATUS(tds_tungsten_association_set_item(&association, &key, &value, &next));
            model_set(&model, key, value);
            break;
        case 1:
            REQUIRE_STATUS(tds_tungsten_association_append(&association, &key, &value, &next));
            model_append(&model, key, value);
            break;
        case 2:
            REQUIRE_STATUS(tds_tungsten_association_prepend(&association, &key, &value, &next));
            model_prepend(&model, key, value);
            break;
        case 3: {
            const size_t position = model.count == 0 ? 0u : (size_t)command % (model.count + 1u);
            REQUIRE_STATUS(tds_tungsten_association_insert_at(&association, position, &key, &value, &next));
            model_insert_at(&model, position, key, value);
            break;
        }
        case 4:
            REQUIRE_STATUS(tds_tungsten_association_remove(&association, &key, &next));
            model_delete(&model, key);
            break;
        case 5: {
            const size_t position = model.count == 0 ? 0u : (size_t)command % model.count;
            if (model.count == 0) {
                REQUIRE_STATUS(tds_tungsten_association_copy(&association, &next));
            } else {
                REQUIRE_STATUS(tds_tungsten_association_remove_at(&association, position, &next));
                model_delete_at(&model, position);
            }
            break;
        }
        default: {
            const size_t start = model.count == 0 ? 0u : (size_t)command % model.count;
            const size_t length = model.count == 0 ? 0u : (size_t)(command / 3) % (model.count - start + 1u);
            REQUIRE_STATUS(tds_tungsten_association_slice(&association, start, length, &next));
            model_slice(&model, start, length);
            break;
        }
        }

        tds_tungsten_association_dispose(&association);
        tds_tungsten_association_move(&association, &next);
        REQUIRE(association_matches_model(&association, &model));
    }

    tds_tungsten_association_dispose(&association);
}

static void test_concurrent_retained_snapshot_reads(void)
{
    ft_value_type int_type;
    init_int_type(&int_type);

    int list_values[128];
    for (int index = 0; index != 128; ++index) {
        list_values[index] = index;
    }

    tds_tungsten_list list;
    REQUIRE_STATUS(tds_tungsten_list_from_array(&list, &int_type, list_values, 128));

    tds_tungsten_association_policy policy;
    init_assoc_policy(&policy);

    int keys[64];
    int values[64];
    tds_tungsten_assoc_pair pairs[64];
    for (int index = 0; index != 64; ++index) {
        keys[index] = index;
        values[index] = -index;
        pairs[index] = pair_of(&keys[index], &values[index]);
    }

    tds_tungsten_association association;
    REQUIRE_STATUS(tds_tungsten_association_from_pairs(&association, &policy, pairs, 64));

    concurrent_tungsten_context context;
    context.list = &list;
    context.list_values = list_values;
    context.list_count = 128;
    context.association = &association;
    context.association_keys = keys;
    context.association_values = values;
    context.association_count = 64;
    test_atomic_long_init(&context.failures, 0);

#ifdef _WIN32
    enum { thread_count = 8 };
    HANDLE threads[thread_count];
    for (DWORD index = 0; index != thread_count; ++index) {
        threads[index] = CreateThread(NULL, 0, concurrent_tungsten_thread_proc, &context, 0, NULL);
        REQUIRE(threads[index] != NULL);
    }

    const DWORD wait_result = WaitForMultipleObjects(thread_count, threads, TRUE, INFINITE);
    REQUIRE(wait_result == WAIT_OBJECT_0);
    for (DWORD index = 0; index != thread_count; ++index) {
        CloseHandle(threads[index]);
    }
#else
    for (int index = 0; index != 8; ++index) {
        concurrent_tungsten_worker(&context);
    }
#endif

    REQUIRE(test_atomic_long_read(&context.failures) == 0);
    tds_tungsten_association_dispose(&association);
    tds_tungsten_list_dispose(&list);
}

static void run_test(const char* name, void (*test)(void))
{
    const int before = g_failures;
    test();
    if (g_failures == before) {
        (void)printf("[pass] %s\n", name);
        (void)fflush(stdout);
    } else {
        (void)fprintf(stderr, "[fail] %s\n", name);
        (void)fflush(stderr);
    }
}

int main(void)
{
    if (!tds_enter_headless_test_process()) {
        return EXIT_FAILURE;
    }

    run_test("list examples", test_list_examples);
    run_test("result aliasing is rejected", test_result_aliasing_is_rejected);
    run_test("association ordering examples", test_association_ordering_examples);
    run_test("association custom policy", test_association_custom_policy);
    run_test("association relabel stress", test_association_relabel_stress);
    run_test("association generated history", test_association_generated_history);
    run_test("concurrent retained snapshot reads", test_concurrent_retained_snapshot_reads);

    if (g_failures != 0) {
        (void)fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }

    (void)printf("all C Tungsten tests passed\n");
    return 0;
}
