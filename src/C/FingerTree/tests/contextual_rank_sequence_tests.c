/*
 * Tests for the persistent contextual rank sequence.
 */

#include <durable7/finger_tree/contextual_rank_sequence.h>
#include <durable7/test_support/headless_test_process.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#if !defined(__STDC_NO_THREADS__)
#define CRS_TEST_HAS_THREADS 1
#include <threads.h>
#endif
#endif

static int g_failures = 0;
static const unsigned char g_element_type_identity = 0;

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

typedef struct crs_test_context {
    size_t allocation_calls;
    size_t deallocation_calls;
    size_t outstanding_allocations;
    size_t fail_allocation_at;
    size_t copy_calls;
    size_t successful_copies;
    size_t destroy_calls;
    size_t fail_copy_at;
    size_t transition_calls;
    size_t fail_transition_at;
} crs_test_context;

static void* tracked_allocate(size_t size, void* context)
{
    crs_test_context* state = (crs_test_context*)context;
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
    crs_test_context* state = (crs_test_context*)context;
    if (allocation != NULL) {
        ++state->deallocation_calls;
        --state->outstanding_allocations;
        free(allocation);
    }
}

static ft_status tracked_copy(void* destination, const void* source, void* context)
{
    crs_test_context* state = (crs_test_context*)context;
    ++state->copy_calls;
    if (state->fail_copy_at != 0 && state->copy_calls == state->fail_copy_at) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    *(int*)destination = *(const int*)source;
    ++state->successful_copies;
    return FT_STATUS_OK;
}

static void tracked_destroy(void* value, void* context)
{
    crs_test_context* state = (crs_test_context*)context;
    (void)value;
    ++state->destroy_calls;
}

/* Two states: outside quotes (0) and inside quotes (1). A comma outside quotes is an event. */
static void quoted_comma_step(size_t state, int element, size_t* next, int64_t* events)
{
    if (element == '"') {
        *next = 1u - state;
        *events = 0;
        return;
    }
    if (element == ',' && state == 0) {
        *next = state;
        *events = 1;
        return;
    }
    *next = state;
    *events = 0;
}

static ft_status quoted_comma_transition(
    size_t state,
    const void* element,
    size_t* next_state,
    int64_t* event_count,
    void* context)
{
    crs_test_context* tracker = (crs_test_context*)context;
    if (tracker != NULL) {
        ++tracker->transition_calls;
    }
    quoted_comma_step(state, *(const int*)element, next_state, event_count);
    return FT_STATUS_OK;
}

/* Three states whose transitions emit zero, one, or two events. */
static void ternary_step(size_t state, int element, size_t* next, int64_t* events)
{
    const int64_t magnitude = element < 0 ? -(int64_t)element : (int64_t)element;
    const size_t computed = (size_t)(((int64_t)state + magnitude) % 3);
    *next = computed;
    *events = (int64_t)computed;
}

static ft_status ternary_transition(
    size_t state,
    const void* element,
    size_t* next_state,
    int64_t* event_count,
    void* context)
{
    crs_test_context* tracker = (crs_test_context*)context;
    if (tracker != NULL) {
        ++tracker->transition_calls;
    }
    ternary_step(state, *(const int*)element, next_state, event_count);
    return FT_STATUS_OK;
}

/* Counts every transition so tests can prove cached queries never rescan the input. */
static void counting_step(size_t state, int element, size_t* next, int64_t* events)
{
    const size_t value = (size_t)element;
    *next = (state + (value & 1u)) & 1u;
    *events = ((value ^ state) & 3u) == 0 ? 1 : 0;
}

static ft_status counting_transition(
    size_t state,
    const void* element,
    size_t* next_state,
    int64_t* event_count,
    void* context)
{
    crs_test_context* tracker = (crs_test_context*)context;
    ++tracker->transition_calls;
    counting_step(state, *(const int*)element, next_state, event_count);
    return FT_STATUS_OK;
}

/* Declares one state but leaves it, violating the machine contract. */
static ft_status invalid_state_transition(
    size_t state,
    const void* element,
    size_t* next_state,
    int64_t* event_count,
    void* context)
{
    (void)state;
    (void)element;
    (void)context;
    *next_state = 1;
    *event_count = 0;
    return FT_STATUS_OK;
}

/* Reports a negative emitted-event count, which select monotonicity forbids. */
static ft_status negative_event_transition(
    size_t state,
    const void* element,
    size_t* next_state,
    int64_t* event_count,
    void* context)
{
    (void)state;
    (void)element;
    (void)context;
    *next_state = 0;
    *event_count = -1;
    return FT_STATUS_OK;
}

/* Emits the largest representable event count, so two elements overflow. */
static ft_status huge_event_transition(
    size_t state,
    const void* element,
    size_t* next_state,
    int64_t* event_count,
    void* context)
{
    (void)state;
    (void)element;
    (void)context;
    *next_state = 0;
    *event_count = INT64_MAX;
    return FT_STATUS_OK;
}

/* Fails on a chosen call so failure atomicity can be observed mid-construction. */
static ft_status failing_transition(
    size_t state,
    const void* element,
    size_t* next_state,
    int64_t* event_count,
    void* context)
{
    crs_test_context* tracker = (crs_test_context*)context;
    ++tracker->transition_calls;
    if (tracker->fail_transition_at != 0 &&
        tracker->transition_calls == tracker->fail_transition_at) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    quoted_comma_step(state, *(const int*)element, next_state, event_count);
    return FT_STATUS_OK;
}

typedef void (*model_step_fn)(size_t state, int element, size_t* next, int64_t* events);

static void init_tracked_config(
    ft_crs_policy_config* config,
    size_t state_count,
    ft_crs_transition_fn transition,
    crs_test_context* context)
{
    ft_crs_policy_config_init(
        config,
        sizeof(int),
        &g_element_type_identity,
        state_count,
        transition,
        context);
    config->copy = tracked_copy;
    config->destroy = tracked_destroy;
    config->allocator.allocate = tracked_allocate;
    config->allocator.deallocate = tracked_deallocate;
    config->allocator.context = context;
}

static void init_plain_config(
    ft_crs_policy_config* config,
    size_t state_count,
    ft_crs_transition_fn transition,
    void* callback_context)
{
    ft_crs_policy_config_init(
        config,
        sizeof(int),
        &g_element_type_identity,
        state_count,
        transition,
        callback_context);
}

typedef struct crs_collect_state {
    int* buffer;
    size_t capacity;
    size_t count;
    bool overflowed;
} crs_collect_state;

static ft_status collect_visitor(const void* element, void* context)
{
    crs_collect_state* state = (crs_collect_state*)context;
    if (state->count == state->capacity) {
        state->overflowed = true;
        return FT_STATUS_OUT_OF_RANGE;
    }
    state->buffer[state->count] = *(const int*)element;
    ++state->count;
    return FT_STATUS_OK;
}

/* Checks a sequence against an independent array plus a direct machine scan, for every state,
 * every prefix boundary, and every event. */
static void check_model(
    const ft_contextual_rank_sequence* sequence,
    const int* model,
    size_t count,
    model_step_fn step,
    size_t state_count,
    crs_test_context* tracker)
{
    ft_contextual_rank_sequence_statistics statistics;
    crs_collect_state collected;
    bool valid = false;
    size_t initial_state = 0;
    size_t index = 0;
    int64_t maximum_total = 0;

    REQUIRE(ft_contextual_rank_sequence_size(sequence) == count);
    REQUIRE(ft_contextual_rank_sequence_empty(sequence) == (count == 0));
    REQUIRE(ft_contextual_rank_sequence_state_count(sequence) == state_count);

    collected.buffer = count == 0 ? NULL : (int*)malloc(count * sizeof(int));
    REQUIRE(count == 0 || collected.buffer != NULL);
    collected.capacity = count;
    collected.count = 0;
    collected.overflowed = false;
    if (ft_contextual_rank_sequence_visit(sequence, collect_visitor, &collected) !=
        FT_STATUS_OK) {
        free(collected.buffer);
        fail_at(__FILE__, __LINE__, "visit failed");
        return;
    }
    if (collected.count != count ||
        (count != 0 && memcmp(collected.buffer, model, count * sizeof(int)) != 0)) {
        free(collected.buffer);
        fail_at(__FILE__, __LINE__, "visited elements differ from the model");
        return;
    }
    free(collected.buffer);

    for (index = 0; index != count; ++index) {
        const void* borrowed = NULL;
        int owned = 0;
        REQUIRE_STATUS(
            ft_contextual_rank_sequence_at_ref(sequence, index, &borrowed),
            FT_STATUS_OK);
        REQUIRE(*(const int*)borrowed == model[index]);
        REQUIRE_STATUS(
            ft_contextual_rank_sequence_at_copy(sequence, index, &owned),
            FT_STATUS_OK);
        REQUIRE(owned == model[index]);
        /* at_copy hands back an independently owned value, so the caller destroys it. */
        tracked_destroy(&owned, tracker);
    }

    for (initial_state = 0; initial_state != state_count; ++initial_state) {
        ft_contextual_prefix_summary summary;
        size_t state = initial_state;
        int64_t events = 0;
        int64_t rank = 0;
        bool found = false;

        REQUIRE_STATUS(
            ft_contextual_rank_sequence_evaluate_prefix(
                sequence, 0, initial_state, &summary),
            FT_STATUS_OK);
        REQUIRE(summary.final_state == initial_state && summary.event_count == 0);

        for (index = 0; index != count; ++index) {
            ft_contextual_event_location location;
            size_t next_state = 0;
            int64_t emitted = 0;
            int64_t ordinal = 0;
            step(state, model[index], &next_state, &emitted);
            for (ordinal = 0; ordinal != emitted; ++ordinal) {
                REQUIRE_STATUS(
                    ft_contextual_rank_sequence_try_select_event(
                        sequence, events + ordinal, initial_state, &found, &location),
                    FT_STATUS_OK);
                REQUIRE(found);
                REQUIRE(location.element_index == index);
                REQUIRE(location.event_index_in_element == ordinal);
                REQUIRE(location.state_before == state);
                REQUIRE(location.state_after == next_state);
                REQUIRE(location.element_event_count == emitted);
            }
            state = next_state;
            events += emitted;
            REQUIRE_STATUS(
                ft_contextual_rank_sequence_evaluate_prefix(
                    sequence, index + 1, initial_state, &summary),
                FT_STATUS_OK);
            REQUIRE(summary.final_state == state && summary.event_count == events);
            REQUIRE_STATUS(
                ft_contextual_rank_sequence_event_rank(
                    sequence, index + 1, initial_state, &rank),
                FT_STATUS_OK);
            REQUIRE(rank == events);
        }

        REQUIRE_STATUS(
            ft_contextual_rank_sequence_evaluate(sequence, initial_state, &summary),
            FT_STATUS_OK);
        REQUIRE(summary.final_state == state && summary.event_count == events);
        REQUIRE_STATUS(
            ft_contextual_rank_sequence_try_select_event(
                sequence, events, initial_state, &found, NULL),
            FT_STATUS_INVALID_ARGUMENT);
        {
            ft_contextual_event_location location;
            REQUIRE_STATUS(
                ft_contextual_rank_sequence_try_select_event(
                    sequence, events, initial_state, &found, &location),
                FT_STATUS_OK);
            REQUIRE(!found);
            REQUIRE_STATUS(
                ft_contextual_rank_sequence_try_select_event(
                    sequence, -1, initial_state, &found, &location),
                FT_STATUS_OK);
            REQUIRE(!found);
        }
        if (events > maximum_total) {
            maximum_total = events;
        }
    }

    REQUIRE_STATUS(
        ft_contextual_rank_sequence_validate(sequence, &valid, &statistics),
        FT_STATUS_OK);
    REQUIRE(valid);
    REQUIRE(statistics.count == count);
    REQUIRE(statistics.state_count == state_count);
    REQUIRE(statistics.maximum_total_event_count == maximum_total);
}

static uint64_t next_random(uint64_t* state)
{
    *state = *state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return *state >> 32;
}

static size_t random_below(uint64_t* state, size_t bound)
{
    return (size_t)(next_random(state) % (uint64_t)bound);
}

static void test_quoted_delimiter_machine(void)
{
    static const int text[] = { '"', 'a', ',', 'b', '"', ',', 'c', ',', 'd' };
    ft_crs_policy_config config;
    ft_crs_policy policy;
    ft_contextual_rank_sequence sequence;
    ft_contextual_rank_sequence edited;
    ft_contextual_prefix_summary summary;
    ft_contextual_event_location location;
    crs_test_context context;
    const int quote = '"';
    int64_t rank = 0;
    bool found = false;

    (void)memset(&context, 0, sizeof(context));
    init_tracked_config(&config, 2, quoted_comma_transition, &context);
    REQUIRE_STATUS(ft_crs_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE(ft_crs_policy_state_count(&policy) == 2);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_from_array(
            &sequence, &policy, text, sizeof(text) / sizeof(text[0])),
        FT_STATUS_OK);

    REQUIRE_STATUS(
        ft_contextual_rank_sequence_evaluate(&sequence, 0, &summary), FT_STATUS_OK);
    REQUIRE(summary.final_state == 0 && summary.event_count == 2);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_evaluate(&sequence, 1, &summary), FT_STATUS_OK);
    REQUIRE(summary.final_state == 1 && summary.event_count == 1);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_event_rank(&sequence, 5, 0, &rank), FT_STATUS_OK);
    REQUIRE(rank == 0);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_event_rank(&sequence, 6, 0, &rank), FT_STATUS_OK);
    REQUIRE(rank == 1);

    REQUIRE_STATUS(
        ft_contextual_rank_sequence_try_select_event(
            &sequence, 0, 0, &found, &location),
        FT_STATUS_OK);
    REQUIRE(found);
    REQUIRE(location.element_index == 5);
    REQUIRE(location.event_index_in_element == 0);
    REQUIRE(location.state_before == 0);
    REQUIRE(location.state_after == 0);
    REQUIRE(location.element_event_count == 1);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_try_select_event(
            &sequence, 1, 0, &found, &location),
        FT_STATUS_OK);
    REQUIRE(found && location.element_index == 7);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_try_select_event(
            &sequence, 2, 0, &found, &location),
        FT_STATUS_OK);
    REQUIRE(!found);

    /* Starting inside a quoted region makes the comma at index 2 the first event instead. */
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_try_select_event(
            &sequence, 0, 1, &found, &location),
        FT_STATUS_OK);
    REQUIRE(found && location.element_index == 2);

    check_model(
        &sequence, text, sizeof(text) / sizeof(text[0]), quoted_comma_step, 2, &context);

    /* Editing publishes a new version and does not disturb the retained one. */
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_insert_at(&sequence, 0, &quote, &edited),
        FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_evaluate(&sequence, 0, &summary), FT_STATUS_OK);
    REQUIRE(summary.final_state == 0 && summary.event_count == 2);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_evaluate(&edited, 0, &summary), FT_STATUS_OK);
    REQUIRE(summary.final_state == 1 && summary.event_count == 1);

    ft_contextual_rank_sequence_dispose(&edited);
    ft_contextual_rank_sequence_dispose(&sequence);
    ft_crs_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.destroy_calls == context.successful_copies);
}

static void test_exhaustive_small_words(void)
{
    static const int alphabet[] = { '"', ',', 'x' };
    ft_crs_policy_config config;
    ft_crs_policy policy;
    crs_test_context context;
    const int baseline = g_failures;
    int word[8];
    size_t length = 0;

    (void)memset(&context, 0, sizeof(context));
    init_tracked_config(&config, 2, quoted_comma_transition, &context);
    REQUIRE_STATUS(ft_crs_policy_create(&policy, &config), FT_STATUS_OK);

    for (length = 0; length <= 7; ++length) {
        size_t word_count = 1;
        size_t code = 0;
        size_t index = 0;
        for (index = 0; index != length; ++index) {
            word_count *= 3;
        }
        for (code = 0; code != word_count; ++code) {
            ft_contextual_rank_sequence sequence;
            size_t value = code;
            for (index = 0; index != length; ++index) {
                word[index] = alphabet[value % 3];
                value /= 3;
            }
            REQUIRE_STATUS(
                ft_contextual_rank_sequence_from_array(&sequence, &policy, word, length),
                FT_STATUS_OK);
            check_model(&sequence, word, length, quoted_comma_step, 2, &context);
            ft_contextual_rank_sequence_dispose(&sequence);
            if (g_failures != baseline) {
                ft_crs_policy_dispose(&policy);
                return;
            }
        }
    }

    ft_crs_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
}

typedef struct crs_version {
    ft_contextual_rank_sequence sequence;
    int* model;
    size_t count;
} crs_version;

static void dispose_versions(crs_version* versions, size_t count)
{
    size_t index = 0;
    for (index = 0; index != count; ++index) {
        ft_contextual_rank_sequence_dispose(&versions[index].sequence);
        free(versions[index].model);
    }
}

static int* clone_model(const int* model, size_t count)
{
    int* copy = (int*)malloc(count == 0 ? 1 : count * sizeof(int));
    if (copy != NULL && count != 0) {
        (void)memcpy(copy, model, count * sizeof(int));
    }
    return copy;
}

static void test_randomized_retained_branches(void)
{
    enum { maximum_versions = 48, maximum_length = 96, step_count = 600 };
    ft_crs_policy_config config;
    ft_crs_policy policy;
    crs_test_context context;
    crs_version* versions = NULL;
    uint64_t random = UINT64_C(0x61C72026);
    const int baseline = g_failures;
    size_t version_count = 0;
    size_t step = 0;

    (void)memset(&context, 0, sizeof(context));
    init_tracked_config(&config, 3, ternary_transition, &context);
    REQUIRE_STATUS(ft_crs_policy_create(&policy, &config), FT_STATUS_OK);
    versions = (crs_version*)calloc(maximum_versions + 2, sizeof(crs_version));
    REQUIRE(versions != NULL);

    if (ft_contextual_rank_sequence_init(&versions[0].sequence, &policy) !=
        FT_STATUS_OK) {
        free(versions);
        ft_crs_policy_dispose(&policy);
        fail_at(__FILE__, __LINE__, "initial version");
        return;
    }
    versions[0].model = clone_model(NULL, 0);
    versions[0].count = 0;
    version_count = 1;

    for (step = 0; step != step_count && g_failures == baseline; ++step) {
        const size_t parent_index = random_below(&random, version_count);
        const crs_version* parent = &versions[parent_index];
        ft_contextual_rank_sequence next = {0};
        int* model = NULL;
        size_t count = parent->count;
        size_t choice = random_below(&random, 8);
        ft_status status = FT_STATUS_OK;

        model = (int*)malloc((maximum_length * 2 + 2) * sizeof(int));
        if (model == NULL) {
            fail_at(__FILE__, __LINE__, "model allocation");
            break;
        }
        if (count != 0) {
            (void)memcpy(model, parent->model, count * sizeof(int));
        }

        if ((choice == 3 || choice == 4) && count == 0) {
            choice = 0;
        }
        switch (choice) {
        case 0: {
            const int item = (int)random_below(&random, 19) - 9;
            (void)memmove(model + 1, model, count * sizeof(int));
            model[0] = item;
            ++count;
            status = ft_contextual_rank_sequence_push_front(
                &parent->sequence, &item, &next);
            break;
        }
        case 1: {
            const int item = (int)random_below(&random, 19) - 9;
            model[count] = item;
            ++count;
            status = ft_contextual_rank_sequence_push_back(
                &parent->sequence, &item, &next);
            break;
        }
        case 2: {
            const int item = (int)random_below(&random, 19) - 9;
            const size_t index = random_below(&random, count + 1);
            (void)memmove(model + index + 1, model + index, (count - index) * sizeof(int));
            model[index] = item;
            ++count;
            status = ft_contextual_rank_sequence_insert_at(
                &parent->sequence, index, &item, &next);
            break;
        }
        case 3: {
            const size_t index = random_below(&random, count);
            const int item = (int)random_below(&random, 19) - 9;
            model[index] = item;
            status = ft_contextual_rank_sequence_set_at(
                &parent->sequence, index, &item, &next);
            break;
        }
        case 4: {
            const size_t index = random_below(&random, count);
            (void)memmove(model + index, model + index + 1, (count - index - 1) * sizeof(int));
            --count;
            status = ft_contextual_rank_sequence_remove_at(
                &parent->sequence, index, &next);
            break;
        }
        case 5: {
            const size_t index = random_below(&random, count + 1);
            const size_t length = random_below(&random, count - index + 1);
            (void)memmove(model, model + index, length * sizeof(int));
            count = length;
            status = ft_contextual_rank_sequence_get_range(
                &parent->sequence, index, length, &next);
            break;
        }
        case 6: {
            const size_t index = random_below(&random, count + 1);
            ft_contextual_rank_sequence_split_result split;
            status = ft_contextual_rank_sequence_split_at(
                &parent->sequence, index, &split);
            if (status == FT_STATUS_OK) {
                ft_contextual_rank_sequence rejoined;
                status = ft_contextual_rank_sequence_concat(
                    &split.left, &split.right, &rejoined);
                if (status == FT_STATUS_OK) {
                    if (ft_contextual_rank_sequence_size(&split.left) != index ||
                        ft_contextual_rank_sequence_size(&split.right) != count - index) {
                        fail_at(__FILE__, __LINE__, "split halves");
                    }
                    next = rejoined;
                }
                ft_contextual_rank_sequence_dispose(&split.right);
                ft_contextual_rank_sequence_dispose(&split.left);
            }
            break;
        }
        default: {
            const size_t suffix_index = random_below(&random, version_count);
            const crs_version* suffix = &versions[suffix_index];
            if (count + suffix->count > maximum_length) {
                status = ft_contextual_rank_sequence_copy(&parent->sequence, &next);
            } else {
                if (suffix->count != 0) {
                    (void)memcpy(
                        model + count, suffix->model, suffix->count * sizeof(int));
                }
                count += suffix->count;
                status = ft_contextual_rank_sequence_concat(
                    &parent->sequence, &suffix->sequence, &next);
            }
            break;
        }
        }

        if (status != FT_STATUS_OK) {
            free(model);
            fail_at(__FILE__, __LINE__, "randomized step failed");
            break;
        }

        check_model(&next, model, count, ternary_step, 3, &context);
        versions[version_count].sequence = next;
        versions[version_count].model = model;
        versions[version_count].count = count;
        ++version_count;

        if (version_count > maximum_versions) {
            const size_t victim = 1 + random_below(&random, version_count - 2);
            ft_contextual_rank_sequence_dispose(&versions[victim].sequence);
            free(versions[victim].model);
            (void)memmove(
                &versions[victim],
                &versions[victim + 1],
                (version_count - victim - 1) * sizeof(crs_version));
            --version_count;
        }
        if (step % 37 == 0) {
            const size_t retained = random_below(&random, version_count);
            check_model(
                &versions[retained].sequence,
                versions[retained].model,
                versions[retained].count,
                ternary_step,
                3,
                &context);
        }
    }

    /* Every retained branch is still exactly what it was when it was recorded. */
    for (step = 0; step != version_count && g_failures == baseline; ++step) {
        check_model(
            &versions[step].sequence,
            versions[step].model,
            versions[step].count,
            ternary_step,
            3,
            &context);
    }

    dispose_versions(versions, version_count);
    free(versions);
    ft_crs_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.destroy_calls == context.successful_copies);
}

static void test_retained_versions_are_independent(void)
{
    static const int source_text[] = { 'a', ',', 'b', ',', 'c' };
    static const int quoted_text[] = { '"', 'a', ',', 'b', ',', 'c' };
    static const int trimmed_text[] = { 'a', 'b', ',', 'c' };
    static const int replaced_text[] = { 'a', '"', 'b', ',', 'c' };
    ft_crs_policy_config config;
    ft_crs_policy policy;
    ft_contextual_rank_sequence source;
    ft_contextual_rank_sequence quoted;
    ft_contextual_rank_sequence trimmed;
    ft_contextual_rank_sequence replaced;
    crs_test_context context;
    const int quote = '"';

    (void)memset(&context, 0, sizeof(context));
    init_tracked_config(&config, 2, quoted_comma_transition, &context);
    REQUIRE_STATUS(ft_crs_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_from_array(&source, &policy, source_text, 5),
        FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_insert_at(&source, 0, &quote, &quoted),
        FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_remove_at(&source, 1, &trimmed), FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_set_at(&source, 1, &quote, &replaced),
        FT_STATUS_OK);

    check_model(&source, source_text, 5, quoted_comma_step, 2, &context);
    check_model(&quoted, quoted_text, 6, quoted_comma_step, 2, &context);
    check_model(&trimmed, trimmed_text, 4, quoted_comma_step, 2, &context);
    check_model(&replaced, replaced_text, 5, quoted_comma_step, 2, &context);

    ft_contextual_rank_sequence_dispose(&replaced);
    ft_contextual_rank_sequence_dispose(&trimmed);
    ft_contextual_rank_sequence_dispose(&quoted);
    ft_contextual_rank_sequence_dispose(&source);
    ft_crs_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
}

static void test_boundaries_and_invalid_arguments(void)
{
    static const int items[] = { 2, -3, 4, 0 };
    ft_crs_policy_config config;
    ft_crs_policy policy;
    ft_crs_policy other_policy;
    ft_contextual_rank_sequence sequence;
    ft_contextual_rank_sequence empty;
    ft_contextual_rank_sequence produced;
    ft_contextual_rank_sequence singleton;
    ft_contextual_rank_sequence_split_result split;
    ft_contextual_prefix_summary summary;
    ft_contextual_event_location location;
    ft_contextual_rank_sequence_statistics statistics;
    crs_test_context context;
    const void* borrowed = NULL;
    const int item = 5;
    const int size = 4;
    int64_t rank = 0;
    int index = 0;
    bool found = false;
    bool valid = false;
    bool multi_event = false;

    (void)memset(&context, 0, sizeof(context));
    init_tracked_config(&config, 3, ternary_transition, &context);
    REQUIRE_STATUS(ft_crs_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_crs_policy_create(&other_policy, &config), FT_STATUS_OK);
    REQUIRE(!ft_crs_policy_same(&policy, &other_policy));
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_from_array(&sequence, &policy, items, 4),
        FT_STATUS_OK);
    REQUIRE_STATUS(ft_contextual_rank_sequence_init(&empty, &policy), FT_STATUS_OK);

    for (index = 0; index <= size; ++index) {
        REQUIRE_STATUS(
            ft_contextual_rank_sequence_split_at(&sequence, (size_t)index, &split),
            FT_STATUS_OK);
        REQUIRE_STATUS(
            ft_contextual_rank_sequence_concat(&split.left, &split.right, &produced),
            FT_STATUS_OK);
        check_model(&produced, items, 4, ternary_step, 3, &context);
        ft_contextual_rank_sequence_dispose(&produced);
        ft_contextual_rank_sequence_dispose(&split.right);
        ft_contextual_rank_sequence_dispose(&split.left);
    }

    /* A no-op shares the source root rather than copying it. */
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_concat(&sequence, &empty, &produced), FT_STATUS_OK);
    REQUIRE(ft_contextual_rank_sequence_root_identity(&produced) ==
        ft_contextual_rank_sequence_root_identity(&sequence));
    ft_contextual_rank_sequence_dispose(&produced);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_concat(&empty, &sequence, &produced), FT_STATUS_OK);
    REQUIRE(ft_contextual_rank_sequence_root_identity(&produced) ==
        ft_contextual_rank_sequence_root_identity(&sequence));
    ft_contextual_rank_sequence_dispose(&produced);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_get_range(&sequence, 0, 4, &produced), FT_STATUS_OK);
    REQUIRE(ft_contextual_rank_sequence_root_identity(&produced) ==
        ft_contextual_rank_sequence_root_identity(&sequence));
    ft_contextual_rank_sequence_dispose(&produced);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_split_at(&sequence, 0, &split), FT_STATUS_OK);
    REQUIRE(ft_contextual_rank_sequence_root_identity(&split.right) ==
        ft_contextual_rank_sequence_root_identity(&sequence));
    ft_contextual_rank_sequence_dispose(&split.right);
    ft_contextual_rank_sequence_dispose(&split.left);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_split_at(&sequence, 4, &split), FT_STATUS_OK);
    REQUIRE(ft_contextual_rank_sequence_root_identity(&split.left) ==
        ft_contextual_rank_sequence_root_identity(&sequence));
    ft_contextual_rank_sequence_dispose(&split.right);
    ft_contextual_rank_sequence_dispose(&split.left);

    /* The ternary machine must exercise a transition emitting more than one event. */
    for (index = 0; index != size; ++index) {
        int64_t before = 0;
        int64_t after = 0;
        REQUIRE_STATUS(
            ft_contextual_rank_sequence_event_rank(&sequence, (size_t)index, 0, &before),
            FT_STATUS_OK);
        REQUIRE_STATUS(
            ft_contextual_rank_sequence_event_rank(
                &sequence, (size_t)index + 1, 0, &after),
            FT_STATUS_OK);
        if (after - before > 1) {
            multi_event = true;
        }
    }
    REQUIRE(multi_event);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_try_select_event(&sequence, 0, 0, &found, &location),
        FT_STATUS_OK);
    REQUIRE(found);
    REQUIRE(location.event_index_in_element < location.element_event_count);

    /* Out-of-range arguments are reported, never trapped, and never publish anything. */
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_evaluate(&sequence, 3, &summary),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_evaluate_prefix(&sequence, 0, 3, &summary),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_evaluate_prefix(&sequence, 5, 0, &summary),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_event_rank(&sequence, 5, 0, &rank),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_try_select_event(&sequence, 0, 3, &found, &location),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_at_ref(&sequence, 4, &borrowed),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_insert_at(&sequence, 5, &item, &produced),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_set_at(&sequence, 4, &item, &produced),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_remove_at(&sequence, 4, &produced),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_split_at(&sequence, 5, &split),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_get_range(&sequence, 1, 4, &produced),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_get_range(&sequence, 5, 0, &produced),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_visit(&sequence, NULL, NULL),
        FT_STATUS_INVALID_ARGUMENT);

    /* Concatenation requires exact policy identity, exactly as the managed machine type does. */
    {
        ft_contextual_rank_sequence foreign;
        REQUIRE_STATUS(
            ft_contextual_rank_sequence_init(&foreign, &other_policy), FT_STATUS_OK);
        REQUIRE_STATUS(
            ft_contextual_rank_sequence_concat(&sequence, &foreign, &produced),
            FT_STATUS_INCOMPATIBLE_POLICY);
        ft_contextual_rank_sequence_dispose(&foreign);
    }

    /* The empty sequence is the monoid identity: every state maps to itself with no events. */
    check_model(&empty, NULL, 0, ternary_step, 3, &context);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_evaluate(&empty, 2, &summary), FT_STATUS_OK);
    REQUIRE(summary.final_state == 2 && summary.event_count == 0);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_try_select_event(&empty, 0, 0, &found, &location),
        FT_STATUS_OK);
    REQUIRE(!found);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_get_range(&empty, 0, 0, &produced), FT_STATUS_OK);
    REQUIRE(ft_contextual_rank_sequence_empty(&produced));
    ft_contextual_rank_sequence_dispose(&produced);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_validate(&empty, &valid, &statistics), FT_STATUS_OK);
    REQUIRE(valid && statistics.count == 0 && statistics.state_count == 3);

    REQUIRE_STATUS(
        ft_contextual_rank_sequence_push_back(&empty, &item, &singleton), FT_STATUS_OK);
    check_model(&singleton, &item, 1, ternary_step, 3, &context);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_remove_at(&singleton, 0, &produced), FT_STATUS_OK);
    REQUIRE(ft_contextual_rank_sequence_empty(&produced));
    ft_contextual_rank_sequence_dispose(&produced);

    /* Results may alias their operand: the successor replaces the handle in place. */
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_push_front(&singleton, &item, &singleton),
        FT_STATUS_OK);
    REQUIRE(ft_contextual_rank_sequence_size(&singleton) == 2);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_concat(&singleton, &singleton, &singleton),
        FT_STATUS_OK);
    REQUIRE(ft_contextual_rank_sequence_size(&singleton) == 4);

    ft_contextual_rank_sequence_dispose(&singleton);
    ft_contextual_rank_sequence_dispose(&empty);
    ft_contextual_rank_sequence_dispose(&sequence);
    ft_crs_policy_dispose(&other_policy);
    ft_crs_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
}

static void test_cached_rank_and_select_do_not_rescan(void)
{
    enum { element_count = 8192 };
    ft_crs_policy_config config;
    ft_crs_policy policy;
    ft_contextual_rank_sequence sequence;
    ft_contextual_prefix_summary summary;
    ft_contextual_event_location location;
    crs_test_context context;
    int* values = NULL;
    size_t boundary = 0;
    int64_t total = 0;
    int64_t event_index = 0;
    int index = 0;
    bool found = false;

    (void)memset(&context, 0, sizeof(context));
    init_plain_config(&config, 2, counting_transition, &context);
    REQUIRE_STATUS(ft_crs_policy_create(&policy, &config), FT_STATUS_OK);
    values = (int*)malloc(element_count * sizeof(int));
    REQUIRE(values != NULL);
    for (index = 0; index != element_count; ++index) {
        values[index] = index;
    }
    if (ft_contextual_rank_sequence_from_array(
            &sequence, &policy, values, element_count) != FT_STATUS_OK) {
        free(values);
        ft_crs_policy_dispose(&policy);
        fail_at(__FILE__, __LINE__, "bulk construction");
        return;
    }
    free(values);

    /* Building runs the machine exactly once per state per element and never again. */
    REQUIRE(context.transition_calls == 2 * (size_t)element_count);
    context.transition_calls = 0;

    REQUIRE_STATUS(
        ft_contextual_rank_sequence_evaluate(&sequence, 0, &summary), FT_STATUS_OK);
    total = summary.event_count;
    REQUIRE(total > 0);
    for (boundary = 0; boundary <= (size_t)element_count; boundary += 17) {
        int64_t rank = 0;
        REQUIRE_STATUS(
            ft_contextual_rank_sequence_event_rank(
                &sequence, boundary, boundary & 1u, &rank),
            FT_STATUS_OK);
    }
    REQUIRE(context.transition_calls == 0);

    for (event_index = 0; event_index < total; event_index += 31) {
        REQUIRE_STATUS(
            ft_contextual_rank_sequence_try_select_event(
                &sequence, event_index, 0, &found, &location),
            FT_STATUS_OK);
        REQUIRE(found);
    }
    /* Select needs no transition at all here: the located element carries its own cached
     * effect table, so it is strictly below the managed reference's one call per select. */
    REQUIRE(context.transition_calls == 0);

    {
        bool valid = false;
        ft_contextual_rank_sequence_statistics statistics;
        REQUIRE_STATUS(
            ft_contextual_rank_sequence_validate(&sequence, &valid, &statistics),
            FT_STATUS_OK);
        REQUIRE(valid);
        REQUIRE(statistics.count == (size_t)element_count);
        /* The audit is the only operation that deliberately rescans: s calls per element. */
        REQUIRE(context.transition_calls == 2 * (size_t)element_count);
    }

    ft_contextual_rank_sequence_dispose(&sequence);
    ft_crs_policy_dispose(&policy);
}

static void test_failure_atomicity_and_lifetimes(void)
{
    ft_crs_policy_config config;
    ft_crs_policy policy;
    ft_contextual_rank_sequence sequence;
    ft_contextual_rank_sequence produced;
    ft_contextual_prefix_summary summary;
    crs_test_context context;
    const int item = 1;
    const int other = 2;

    /* A machine declaring no states is rejected outright. */
    (void)memset(&context, 0, sizeof(context));
    init_tracked_config(&config, 0, quoted_comma_transition, &context);
    REQUIRE_STATUS(
        ft_crs_policy_create(&policy, &config), FT_STATUS_INVALID_ARGUMENT);
    REQUIRE(context.outstanding_allocations == 0);

    /* A transition leaving the declared range is an inconsistent policy, not a crash. */
    (void)memset(&context, 0, sizeof(context));
    init_tracked_config(&config, 1, invalid_state_transition, &context);
    REQUIRE_STATUS(ft_crs_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_contextual_rank_sequence_init(&sequence, &policy), FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_push_back(&sequence, &item, &produced),
        FT_STATUS_INCONSISTENT_POLICY);
    REQUIRE(ft_contextual_rank_sequence_empty(&sequence));
    ft_contextual_rank_sequence_dispose(&sequence);
    ft_crs_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.destroy_calls == context.successful_copies);

    /* A negative emitted-event count would break select monotonicity and is refused. */
    (void)memset(&context, 0, sizeof(context));
    init_tracked_config(&config, 1, negative_event_transition, &context);
    REQUIRE_STATUS(ft_crs_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_contextual_rank_sequence_init(&sequence, &policy), FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_push_back(&sequence, &item, &produced),
        FT_STATUS_INCONSISTENT_POLICY);
    ft_contextual_rank_sequence_dispose(&sequence);
    ft_crs_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.destroy_calls == context.successful_copies);

    /* A failing transition propagates and leaves the source version untouched. */
    (void)memset(&context, 0, sizeof(context));
    init_tracked_config(&config, 2, failing_transition, &context);
    REQUIRE_STATUS(ft_crs_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_contextual_rank_sequence_init(&sequence, &policy), FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_push_back(&sequence, &item, &sequence),
        FT_STATUS_OK);
    context.fail_transition_at = context.transition_calls + 2;
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_push_back(&sequence, &other, &produced),
        FT_STATUS_CALLBACK_FAILURE);
    REQUIRE(ft_contextual_rank_sequence_size(&sequence) == 1);
    context.fail_transition_at = 0;
    ft_contextual_rank_sequence_dispose(&sequence);
    ft_crs_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.destroy_calls == context.successful_copies);

    /* A failing element copy publishes nothing and constructs no owned value. */
    (void)memset(&context, 0, sizeof(context));
    init_tracked_config(&config, 2, quoted_comma_transition, &context);
    REQUIRE_STATUS(ft_crs_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_contextual_rank_sequence_init(&sequence, &policy), FT_STATUS_OK);
    context.fail_copy_at = 1;
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_push_front(&sequence, &item, &produced),
        FT_STATUS_CALLBACK_FAILURE);
    REQUIRE(ft_contextual_rank_sequence_empty(&sequence));
    context.fail_copy_at = 0;
    ft_contextual_rank_sequence_dispose(&sequence);
    ft_crs_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.destroy_calls == context.successful_copies);

    /* Every allocation this module performs is failure-atomic and leak-free. */
    {
        size_t attempt = 0;
        for (attempt = 1; attempt != 12; ++attempt) {
            ft_status status = FT_STATUS_OK;
            (void)memset(&context, 0, sizeof(context));
            init_tracked_config(&config, 2, quoted_comma_transition, &context);
            if (ft_crs_policy_create(&policy, &config) != FT_STATUS_OK) {
                continue;
            }
            if (ft_contextual_rank_sequence_init(&sequence, &policy) != FT_STATUS_OK) {
                ft_crs_policy_dispose(&policy);
                continue;
            }
            context.fail_allocation_at = context.allocation_calls + attempt;
            status = ft_contextual_rank_sequence_push_back(&sequence, &item, &produced);
            REQUIRE(status == FT_STATUS_OK || status == FT_STATUS_NO_MEMORY);
            if (status == FT_STATUS_OK) {
                REQUIRE(ft_contextual_rank_sequence_size(&produced) == 1);
                ft_contextual_rank_sequence_dispose(&produced);
            } else {
                REQUIRE(ft_contextual_rank_sequence_empty(&sequence));
            }
            context.fail_allocation_at = 0;
            ft_contextual_rank_sequence_dispose(&sequence);
            ft_crs_policy_dispose(&policy);
            REQUIRE(context.outstanding_allocations == 0);
            REQUIRE(context.destroy_calls == context.successful_copies);
        }
    }

    /* An event total that would exceed the representable range refuses to publish. */
    (void)memset(&context, 0, sizeof(context));
    init_tracked_config(&config, 1, huge_event_transition, &context);
    REQUIRE_STATUS(ft_crs_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_contextual_rank_sequence_init(&sequence, &policy), FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_push_back(&sequence, &item, &sequence),
        FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_evaluate(&sequence, 0, &summary), FT_STATUS_OK);
    REQUIRE(summary.event_count == INT64_MAX);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_push_back(&sequence, &other, &produced),
        FT_STATUS_OVERFLOW);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_push_front(&sequence, &other, &produced),
        FT_STATUS_OVERFLOW);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_insert_at(&sequence, 0, &other, &produced),
        FT_STATUS_OVERFLOW);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_concat(&sequence, &sequence, &produced),
        FT_STATUS_OVERFLOW);
    /* The rejected successors published nothing; the retained version is untouched. */
    REQUIRE(ft_contextual_rank_sequence_size(&sequence) == 1);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_evaluate(&sequence, 0, &summary), FT_STATUS_OK);
    REQUIRE(summary.event_count == INT64_MAX);
    ft_contextual_rank_sequence_dispose(&sequence);
    ft_crs_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.destroy_calls == context.successful_copies);
}

/* Emits nothing, so only the element count can reach its limit. */
static ft_status silent_transition(
    size_t state,
    const void* element,
    size_t* next_state,
    int64_t* event_count,
    void* context)
{
    (void)state;
    (void)element;
    (void)context;
    *next_state = 0;
    *event_count = 0;
    return FT_STATUS_OK;
}

static void test_maximum_count_fails_uniformly(void)
{
    /* Structural sharing makes the maximum count reachable: doubling plus one element per round
     * grows 2^k-1 to 2^(k+1)-1 in O(log n) work and O(log n) fresh nodes. */
    ft_crs_policy_config config;
    ft_crs_policy policy;
    ft_contextual_rank_sequence power = {0};
    ft_contextual_rank_sequence almost = {0};
    ft_contextual_rank_sequence full = {0};
    ft_contextual_rank_sequence failed = {0};
    ft_contextual_prefix_summary summary;
    const int item = 7;

    init_plain_config(&config, 1, silent_transition, NULL);
    REQUIRE_STATUS(ft_crs_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_from_array(&power, &policy, &item, 1), FT_STATUS_OK);
    while (ft_contextual_rank_sequence_size(&power) <= SIZE_MAX / 2) {
        REQUIRE_STATUS(
            ft_contextual_rank_sequence_concat(&power, &power, &power), FT_STATUS_OK);
    }
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_remove_at(&power, 0, &almost), FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_concat(&power, &almost, &full), FT_STATUS_OK);
    REQUIRE(ft_contextual_rank_sequence_size(&full) == SIZE_MAX);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_evaluate(&full, 0, &summary), FT_STATUS_OK);
    REQUIRE(summary.final_state == 0 && summary.event_count == 0);

    /* Every insertion entry point reports the same failure on a full sequence. */
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_push_front(&full, &item, &failed), FT_STATUS_OVERFLOW);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_push_back(&full, &item, &failed), FT_STATUS_OVERFLOW);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_insert_at(&full, 0, &item, &failed),
        FT_STATUS_OVERFLOW);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_insert_at(&full, 1, &item, &failed),
        FT_STATUS_OVERFLOW);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_insert_at(&full, SIZE_MAX, &item, &failed),
        FT_STATUS_OVERFLOW);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_concat(&full, &power, &failed), FT_STATUS_OVERFLOW);
    REQUIRE(failed.policy == NULL && failed.root == NULL);
    REQUIRE(ft_contextual_rank_sequence_size(&full) == SIZE_MAX);

    ft_contextual_rank_sequence_dispose(&full);
    ft_contextual_rank_sequence_dispose(&almost);
    ft_contextual_rank_sequence_dispose(&power);
    ft_crs_policy_dispose(&policy);
}

static void test_handle_lifecycle(void)
{
    ft_crs_policy_config config;
    ft_crs_policy policy;
    ft_crs_policy moved_policy;
    ft_crs_policy shared_policy;
    ft_contextual_rank_sequence sequence;
    ft_contextual_rank_sequence alias;
    ft_contextual_rank_sequence moved;
    crs_test_context context;
    static const int items[] = { 1, 2, 3 };

    (void)memset(&context, 0, sizeof(context));
    init_tracked_config(&config, 2, quoted_comma_transition, &context);
    REQUIRE_STATUS(ft_crs_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_crs_policy_copy(&policy, &shared_policy), FT_STATUS_OK);
    REQUIRE(ft_crs_policy_same(&policy, &shared_policy));
    ft_crs_policy_move(&moved_policy, &shared_policy);
    REQUIRE(shared_policy.rep == NULL);
    REQUIRE(ft_crs_policy_same(&policy, &moved_policy));

    REQUIRE_STATUS(
        ft_contextual_rank_sequence_from_array(&sequence, &policy, items, 3),
        FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_contextual_rank_sequence_copy(&sequence, &alias), FT_STATUS_OK);
    REQUIRE(ft_contextual_rank_sequence_root_identity(&alias) ==
        ft_contextual_rank_sequence_root_identity(&sequence));
    ft_contextual_rank_sequence_move(&moved, &alias);
    REQUIRE(alias.root == NULL && alias.policy == NULL);
    REQUIRE(ft_contextual_rank_sequence_size(&moved) == 3);

    ft_contextual_rank_sequence_dispose(&moved);
    ft_contextual_rank_sequence_dispose(&sequence);
    ft_crs_policy_dispose(&moved_policy);
    ft_crs_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.destroy_calls == context.successful_copies);
}

typedef struct crs_reader_context {
    const ft_contextual_rank_sequence* source;
    size_t worker;
    bool success;
} crs_reader_context;

static void concurrent_reader(crs_reader_context* reader)
{
    const size_t state_count =
        ft_contextual_rank_sequence_state_count(reader->source);
    const size_t size = ft_contextual_rank_sequence_size(reader->source);
    ft_contextual_rank_sequence branch;
    size_t iteration = 0;
    if (ft_contextual_rank_sequence_copy(reader->source, &branch) != FT_STATUS_OK) {
        return;
    }
    for (iteration = 0; iteration != 120; ++iteration) {
        ft_contextual_prefix_summary summary;
        ft_contextual_event_location location;
        const size_t state = (reader->worker + iteration) % state_count;
        const size_t boundary = (reader->worker * 31 + iteration * 17) % (size + 1);
        const int item = (int)reader->worker - (int)iteration;
        int64_t rank = 0;
        bool found = false;
        if (ft_contextual_rank_sequence_evaluate(reader->source, state, &summary) !=
                FT_STATUS_OK ||
            ft_contextual_rank_sequence_event_rank(
                reader->source, boundary, state, &rank) != FT_STATUS_OK) {
            ft_contextual_rank_sequence_dispose(&branch);
            return;
        }
        if (summary.event_count > 0 &&
            (ft_contextual_rank_sequence_try_select_event(
                 reader->source,
                 (int64_t)((reader->worker + iteration) % (size_t)summary.event_count),
                 state,
                 &found,
                 &location) != FT_STATUS_OK ||
                !found)) {
            ft_contextual_rank_sequence_dispose(&branch);
            return;
        }
        if (ft_contextual_rank_sequence_set_at(
                &branch, iteration % size, &item, &branch) != FT_STATUS_OK) {
            ft_contextual_rank_sequence_dispose(&branch);
            return;
        }
    }
    reader->success = ft_contextual_rank_sequence_size(&branch) == size;
    ft_contextual_rank_sequence_dispose(&branch);
}

#ifdef _WIN32
static DWORD WINAPI concurrent_thread(LPVOID argument)
{
    concurrent_reader((crs_reader_context*)argument);
    return 0;
}
#elif defined(CRS_TEST_HAS_THREADS)
static int concurrent_thread(void* argument)
{
    concurrent_reader((crs_reader_context*)argument);
    return 0;
}
#endif

static void test_concurrent_readers(void)
{
    enum { thread_count = 4, element_count = 257 };
    ft_crs_policy_config config;
    ft_crs_policy policy;
    ft_contextual_rank_sequence sequence;
    crs_reader_context contexts[thread_count];
    int* values = NULL;
    int index = 0;

    init_plain_config(&config, 3, ternary_transition, NULL);
    REQUIRE_STATUS(ft_crs_policy_create(&policy, &config), FT_STATUS_OK);
    values = (int*)malloc(element_count * sizeof(int));
    REQUIRE(values != NULL);
    for (index = 0; index != element_count; ++index) {
        values[index] = index - 128;
    }
    if (ft_contextual_rank_sequence_from_array(
            &sequence, &policy, values, element_count) != FT_STATUS_OK) {
        free(values);
        ft_crs_policy_dispose(&policy);
        fail_at(__FILE__, __LINE__, "shared version");
        return;
    }
    free(values);
    for (index = 0; index != thread_count; ++index) {
        contexts[index].source = &sequence;
        contexts[index].worker = (size_t)index;
        contexts[index].success = false;
    }

#ifdef _WIN32
    {
        HANDLE threads[thread_count];
        for (index = 0; index != thread_count; ++index) {
            threads[index] = CreateThread(
                NULL, 0, concurrent_thread, &contexts[index], 0, NULL);
            REQUIRE(threads[index] != NULL);
        }
        REQUIRE(WaitForMultipleObjects(thread_count, threads, TRUE, INFINITE) ==
            WAIT_OBJECT_0);
        for (index = 0; index != thread_count; ++index) {
            REQUIRE(CloseHandle(threads[index]) != 0);
        }
    }
#elif defined(CRS_TEST_HAS_THREADS)
    {
        thrd_t threads[thread_count];
        for (index = 0; index != thread_count; ++index) {
            REQUIRE(thrd_create(&threads[index], concurrent_thread, &contexts[index]) ==
                thrd_success);
        }
        for (index = 0; index != thread_count; ++index) {
            int result = 0;
            REQUIRE(thrd_join(threads[index], &result) == thrd_success);
            REQUIRE(result == 0);
        }
    }
#else
    for (index = 0; index != thread_count; ++index) {
        concurrent_reader(&contexts[index]);
    }
#endif

    for (index = 0; index != thread_count; ++index) {
        REQUIRE(contexts[index].success);
    }
    REQUIRE(ft_contextual_rank_sequence_size(&sequence) == (size_t)element_count);
    {
        bool valid = false;
        ft_contextual_rank_sequence_statistics statistics;
        REQUIRE_STATUS(
            ft_contextual_rank_sequence_validate(&sequence, &valid, &statistics),
            FT_STATUS_OK);
        REQUIRE(valid);
    }
    ft_contextual_rank_sequence_dispose(&sequence);
    ft_crs_policy_dispose(&policy);
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
    run_test("Contextual quoted-delimiter rank and select", test_quoted_delimiter_machine);
    run_test("Contextual exhaustive small words", test_exhaustive_small_words);
    run_test("Contextual randomized retained branches", test_randomized_retained_branches);
    run_test("Contextual retained-version independence", test_retained_versions_are_independent);
    run_test("Contextual boundaries and invalid arguments", test_boundaries_and_invalid_arguments);
    run_test("Contextual cached rank and select do not rescan", test_cached_rank_and_select_do_not_rescan);
    run_test("Contextual failure atomicity and lifetimes", test_failure_atomicity_and_lifetimes);
    run_test("Contextual maximum count fails uniformly", test_maximum_count_fails_uniformly);
    run_test("Contextual handle lifecycle", test_handle_lifecycle);
    run_test("Contextual concurrent readers", test_concurrent_readers);
    if (g_failures != 0) {
        (void)fprintf(stderr, "%d failure(s)\n", g_failures);
        return EXIT_FAILURE;
    }
    (void)printf("all C contextual rank sequence tests passed\n");
    return EXIT_SUCCESS;
}
