/*
 * Implementation of the chunked persistent bit set.
 *
 * Point edits split one measured route and rebuild it; algebra merges only the word streams both
 * operands actually represent, so absent chunks cost nothing.
 */

#include <durable7/finger_tree/persistent_chunked_bit_set.h>

#include <limits.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct ft_chunked_bit_set_chunk {
    int32_t word_index;
    uint64_t bits;
} ft_chunked_bit_set_chunk;

typedef struct ft_chunked_bit_set_measure {
    size_t chunk_count;
    uint64_t pop_count;
    int32_t last_word;
    bool has_last_word;
} ft_chunked_bit_set_measure;

typedef struct ft_chunked_bit_set_context {
    atomic_size_t references;
    ft_tree_policy tree_policy;
} ft_chunked_bit_set_context;

typedef enum ft_chunked_bit_set_operation {
    FT_CHUNKED_UNION,
    FT_CHUNKED_INTERSECT,
    FT_CHUNKED_EXCEPT,
    FT_CHUNKED_SYMMETRIC_EXCEPT
} ft_chunked_bit_set_operation;

static unsigned ft_chunked_pop_count(uint64_t value)
{
    unsigned count = 0u;
    while (value != 0u) {
        value &= value - 1u;
        ++count;
    }
    return count;
}

static unsigned ft_chunked_trailing_zero_count(uint64_t value)
{
    unsigned count = 0u;
    while ((value & 1u) == 0u) {
        value >>= 1u;
        ++count;
    }
    return count;
}

static void ft_chunked_measure_identity(void* destination, void* context)
{
    (void)context;
    (void)memset(destination, 0, sizeof(ft_chunked_bit_set_measure));
}

static void ft_chunked_measure_value(
    void* destination,
    const void* raw_value,
    void* context)
{
    (void)context;
    const ft_chunked_bit_set_chunk* value =
        (const ft_chunked_bit_set_chunk*)raw_value;
    ft_chunked_bit_set_measure* measure =
        (ft_chunked_bit_set_measure*)destination;
    measure->chunk_count = 1u;
    measure->pop_count = ft_chunked_pop_count(value->bits);
    measure->last_word = value->word_index;
    measure->has_last_word = true;
}

static void ft_chunked_measure_combine(
    void* destination,
    const void* raw_left,
    const void* raw_right,
    void* context)
{
    (void)context;
    const ft_chunked_bit_set_measure* left =
        (const ft_chunked_bit_set_measure*)raw_left;
    const ft_chunked_bit_set_measure* right =
        (const ft_chunked_bit_set_measure*)raw_right;
    ft_chunked_bit_set_measure* result =
        (ft_chunked_bit_set_measure*)destination;
    result->chunk_count = left->chunk_count + right->chunk_count;
    result->pop_count = left->pop_count + right->pop_count;
    result->has_last_word = left->has_last_word || right->has_last_word;
    result->last_word = right->has_last_word
        ? right->last_word : left->last_word;
}

static bool ft_chunked_bit_set_valid(const ft_persistent_chunked_bit_set* set)
{
    return set != NULL && set->context != NULL
        && set->chunks.policy == &set->context->tree_policy;
}

static void ft_chunked_context_retain(ft_chunked_bit_set_context* context)
{
    if (context != NULL) {
        (void)atomic_fetch_add_explicit(
            &context->references, 1u, memory_order_relaxed);
    }
}

static void ft_chunked_context_release(ft_chunked_bit_set_context* context)
{
    if (context != NULL
        && atomic_fetch_sub_explicit(
            &context->references, 1u, memory_order_acq_rel) == 1u) {
        free(context);
    }
}

static void ft_chunked_publish(
    const ft_persistent_chunked_bit_set* source,
    ft_persistent_chunked_bit_set* result,
    ft_persistent_chunked_bit_set* candidate)
{
    if (result == source) {
        ft_persistent_chunked_bit_set_dispose(result);
    }
    ft_persistent_chunked_bit_set_move(result, candidate);
}

static ft_status ft_chunked_publish_copy(
    const ft_persistent_chunked_bit_set* source,
    ft_persistent_chunked_bit_set* result)
{
    ft_persistent_chunked_bit_set candidate;
    const ft_status status =
        ft_persistent_chunked_bit_set_copy(source, &candidate);
    if (status == FT_STATUS_OK) {
        ft_chunked_publish(source, result, &candidate);
    }
    return status;
}

ft_status ft_persistent_chunked_bit_set_init(ft_persistent_chunked_bit_set* set)
{
    if (set == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_chunked_bit_set_context* context =
        (ft_chunked_bit_set_context*)malloc(sizeof(*context));
    if (context == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    atomic_init(&context->references, 1u);
    ft_value_type value_type;
    ft_value_type_init(&value_type, sizeof(ft_chunked_bit_set_chunk));
    context->tree_policy.value = value_type;
    context->tree_policy.measure.size = sizeof(ft_chunked_bit_set_measure);
    context->tree_policy.measure.identity = ft_chunked_measure_identity;
    context->tree_policy.measure.measure = ft_chunked_measure_value;
    context->tree_policy.measure.combine = ft_chunked_measure_combine;
    context->tree_policy.measure.context = context;
    const ft_status status = ft_tree_init(&set->chunks, &context->tree_policy);
    if (status != FT_STATUS_OK) {
        free(context);
        return status;
    }
    set->context = context;
    return FT_STATUS_OK;
}

ft_status ft_persistent_chunked_bit_set_copy(
    const ft_persistent_chunked_bit_set* source,
    ft_persistent_chunked_bit_set* destination)
{
    if (!ft_chunked_bit_set_valid(source) || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return FT_STATUS_OK;
    }
    const ft_status status = ft_tree_copy(&source->chunks, &destination->chunks);
    if (status != FT_STATUS_OK) {
        return status;
    }
    destination->context = source->context;
    ft_chunked_context_retain(destination->context);
    return FT_STATUS_OK;
}

void ft_persistent_chunked_bit_set_move(
    ft_persistent_chunked_bit_set* destination,
    ft_persistent_chunked_bit_set* source)
{
    if (destination != NULL && source != NULL && destination != source) {
        *destination = *source;
        (void)memset(source, 0, sizeof(*source));
    }
}

void ft_persistent_chunked_bit_set_dispose(ft_persistent_chunked_bit_set* set)
{
    if (set != NULL) {
        ft_tree_dispose(&set->chunks);
        ft_chunked_context_release(set->context);
        (void)memset(set, 0, sizeof(*set));
    }
}

static bool ft_chunked_measure_of(
    const ft_persistent_chunked_bit_set* set,
    ft_chunked_bit_set_measure* measure)
{
    return ft_chunked_bit_set_valid(set)
        && ft_tree_measure(&set->chunks, measure) == FT_STATUS_OK;
}

bool ft_persistent_chunked_bit_set_empty(const ft_persistent_chunked_bit_set* set)
{
    return !ft_chunked_bit_set_valid(set) || ft_tree_empty(&set->chunks);
}

uint64_t ft_persistent_chunked_bit_set_count(const ft_persistent_chunked_bit_set* set)
{
    ft_chunked_bit_set_measure measure;
    return ft_chunked_measure_of(set, &measure) ? measure.pop_count : 0u;
}

size_t ft_persistent_chunked_bit_set_chunk_count(const ft_persistent_chunked_bit_set* set)
{
    ft_chunked_bit_set_measure measure;
    return ft_chunked_measure_of(set, &measure) ? measure.chunk_count : 0u;
}

static bool ft_chunked_word_at_least(const void* raw_measure, void* raw_word)
{
    const ft_chunked_bit_set_measure* measure =
        (const ft_chunked_bit_set_measure*)raw_measure;
    return measure->has_last_word
        && measure->last_word >= *(const int32_t*)raw_word;
}

static bool ft_chunked_population_above(const void* raw_measure, void* raw_rank)
{
    return ((const ft_chunked_bit_set_measure*)raw_measure)->pop_count
        > *(const uint64_t*)raw_rank;
}

static ft_status ft_chunked_locate_word(
    const ft_persistent_chunked_bit_set* set,
    int32_t word,
    bool* found,
    ft_chunked_bit_set_measure* before,
    ft_chunked_bit_set_chunk* chunk)
{
    return ft_tree_locate(
        &set->chunks, ft_chunked_word_at_least, &word,
        found, before, chunk);
}

bool ft_persistent_chunked_bit_set_contains(
    const ft_persistent_chunked_bit_set* set,
    int32_t bit_index)
{
    if (!ft_chunked_bit_set_valid(set) || bit_index < 0) {
        return false;
    }
    int32_t word = bit_index >> 6;
    bool found = false;
    ft_chunked_bit_set_measure before;
    ft_chunked_bit_set_chunk chunk;
    return ft_chunked_locate_word(set, word, &found, &before, &chunk) == FT_STATUS_OK
        && found && chunk.word_index == word
        && (chunk.bits & (UINT64_C(1) << ((uint32_t)bit_index & 63u))) != 0u;
}

static ft_status ft_chunked_wrap_tree(
    const ft_persistent_chunked_bit_set* source,
    ft_tree tree,
    ft_persistent_chunked_bit_set* result)
{
    result->chunks = tree;
    result->context = source->context;
    ft_chunked_context_retain(result->context);
    return FT_STATUS_OK;
}

static ft_status ft_chunked_join_insert(
    const ft_persistent_chunked_bit_set* source,
    ft_tree* left,
    const ft_chunked_bit_set_chunk* inserted,
    const ft_chunked_bit_set_chunk* displaced,
    ft_tree* right,
    ft_persistent_chunked_bit_set* result)
{
    ft_tree left_with_inserted;
    ft_status status = ft_tree_push_back(left, inserted, &left_with_inserted);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_tree suffix;
    if (displaced != NULL) {
        status = ft_tree_push_front(right, displaced, &suffix);
    } else {
        status = ft_tree_copy(right, &suffix);
    }
    if (status != FT_STATUS_OK) {
        ft_tree_dispose(&left_with_inserted);
        return status;
    }
    ft_tree combined;
    status = ft_tree_concat(&left_with_inserted, &suffix, &combined);
    ft_tree_dispose(&left_with_inserted);
    ft_tree_dispose(&suffix);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_persistent_chunked_bit_set candidate;
    (void)ft_chunked_wrap_tree(source, combined, &candidate);
    ft_chunked_publish(source, result, &candidate);
    return FT_STATUS_OK;
}

ft_status ft_persistent_chunked_bit_set_add(
    const ft_persistent_chunked_bit_set* set,
    int32_t bit_index,
    ft_persistent_chunked_bit_set* result)
{
    if (!ft_chunked_bit_set_valid(set) || result == NULL || bit_index < 0) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    int32_t word = bit_index >> 6;
    const uint64_t bit = UINT64_C(1) << ((uint32_t)bit_index & 63u);
    bool located = false;
    ft_chunked_bit_set_measure before;
    ft_chunked_bit_set_chunk found_chunk;
    ft_status status = ft_chunked_locate_word(
        set, word, &located, &before, &found_chunk);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (!located) {
        ft_tree tree;
        const ft_chunked_bit_set_chunk inserted = { word, bit };
        status = ft_tree_push_back(&set->chunks, &inserted, &tree);
        if (status == FT_STATUS_OK) {
            ft_persistent_chunked_bit_set candidate;
            (void)ft_chunked_wrap_tree(set, tree, &candidate);
            ft_chunked_publish(set, result, &candidate);
        }
        return status;
    }
    if (found_chunk.word_index == word && (found_chunk.bits & bit) != 0u) {
        return ft_chunked_publish_copy(set, result);
    }
    bool split_found = false;
    ft_tree left;
    ft_tree right;
    ft_chunked_bit_set_chunk hit;
    status = ft_tree_split(
        &set->chunks, ft_chunked_word_at_least, &word,
        &split_found, &left, &hit, &right);
    if (status != FT_STATUS_OK || !split_found) {
        return status == FT_STATUS_OK ? FT_STATUS_NOT_FOUND : status;
    }
    const ft_chunked_bit_set_chunk inserted = {
        word, hit.word_index == word ? hit.bits | bit : bit };
    status = ft_chunked_join_insert(
        set, &left, &inserted,
        hit.word_index == word ? NULL : &hit,
        &right, result);
    ft_tree_dispose(&left);
    ft_tree_dispose(&right);
    return status;
}

ft_status ft_persistent_chunked_bit_set_remove(
    const ft_persistent_chunked_bit_set* set,
    int32_t bit_index,
    ft_persistent_chunked_bit_set* result)
{
    if (!ft_chunked_bit_set_valid(set) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (bit_index < 0 || !ft_persistent_chunked_bit_set_contains(set, bit_index)) {
        return ft_chunked_publish_copy(set, result);
    }
    int32_t word = bit_index >> 6;
    const uint64_t bit = UINT64_C(1) << ((uint32_t)bit_index & 63u);
    bool found = false;
    ft_tree left;
    ft_tree right;
    ft_chunked_bit_set_chunk hit;
    ft_status status = ft_tree_split(
        &set->chunks, ft_chunked_word_at_least, &word,
        &found, &left, &hit, &right);
    if (status != FT_STATUS_OK || !found || hit.word_index != word) {
        return status == FT_STATUS_OK ? FT_STATUS_NOT_FOUND : status;
    }
    const uint64_t updated = hit.bits & ~bit;
    ft_tree combined;
    if (updated == 0u) {
        status = ft_tree_concat(&left, &right, &combined);
        if (status == FT_STATUS_OK) {
            ft_persistent_chunked_bit_set candidate;
            (void)ft_chunked_wrap_tree(set, combined, &candidate);
            ft_chunked_publish(set, result, &candidate);
        }
    } else {
        const ft_chunked_bit_set_chunk replacement = { word, updated };
        status = ft_chunked_join_insert(
            set, &left, &replacement, NULL, &right, result);
    }
    ft_tree_dispose(&left);
    ft_tree_dispose(&right);
    return status;
}

ft_status ft_persistent_chunked_bit_set_rank(
    const ft_persistent_chunked_bit_set* set,
    int32_t bit_index,
    uint64_t* rank)
{
    if (!ft_chunked_bit_set_valid(set) || rank == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    *rank = 0u;
    if (bit_index < 0) {
        return FT_STATUS_OK;
    }
    const int32_t word = bit_index >> 6;
    bool found = false;
    ft_chunked_bit_set_measure before;
    ft_chunked_bit_set_chunk chunk;
    const ft_status status = ft_chunked_locate_word(
        set, word, &found, &before, &chunk);
    if (status != FT_STATUS_OK) {
        return status;
    }
    *rank = before.pop_count;
    if (found && chunk.word_index == word) {
        const unsigned offset = (uint32_t)bit_index & 63u;
        const uint64_t mask = offset == 63u
            ? UINT64_MAX : (UINT64_C(1) << (offset + 1u)) - 1u;
        *rank += ft_chunked_pop_count(chunk.bits & mask);
    }
    return FT_STATUS_OK;
}

ft_status ft_persistent_chunked_bit_set_try_select(
    const ft_persistent_chunked_bit_set* set,
    uint64_t rank,
    bool* found,
    int32_t* bit_index)
{
    if (!ft_chunked_bit_set_valid(set) || found == NULL || bit_index == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    *found = false;
    *bit_index = 0;
    if (rank >= ft_persistent_chunked_bit_set_count(set)) {
        return FT_STATUS_OK;
    }
    ft_chunked_bit_set_measure before;
    ft_chunked_bit_set_chunk chunk;
    ft_status status = ft_tree_locate(
        &set->chunks, ft_chunked_population_above, &rank,
        found, &before, &chunk);
    if (status != FT_STATUS_OK || !*found) {
        return status;
    }
    uint64_t bits = chunk.bits;
    const uint64_t local_rank = rank - before.pop_count;
    for (uint64_t index = 0u; index < local_rank; ++index) {
        bits &= bits - 1u;
    }
    const unsigned offset = ft_chunked_trailing_zero_count(bits);
    *bit_index = (int32_t)(((int64_t)chunk.word_index << 6) + offset);
    return FT_STATUS_OK;
}

ft_status ft_persistent_chunked_bit_set_select(
    const ft_persistent_chunked_bit_set* set,
    uint64_t rank,
    int32_t* bit_index)
{
    bool found = false;
    const ft_status status = ft_persistent_chunked_bit_set_try_select(
        set, rank, &found, bit_index);
    return status != FT_STATUS_OK ? status
        : found ? FT_STATUS_OK : FT_STATUS_OUT_OF_RANGE;
}

typedef struct ft_chunk_array {
    ft_chunked_bit_set_chunk* values;
    size_t count;
} ft_chunk_array;

static void ft_chunk_collect(const void* raw_chunk, void* raw_array)
{
    ft_chunk_array* array = (ft_chunk_array*)raw_array;
    array->values[array->count++] = *(const ft_chunked_bit_set_chunk*)raw_chunk;
}

static ft_status ft_chunked_collect(
    const ft_persistent_chunked_bit_set* set,
    ft_chunk_array* array)
{
    array->count = 0u;
    const size_t capacity = ft_persistent_chunked_bit_set_chunk_count(set);
    array->values = capacity == 0u ? NULL
        : (ft_chunked_bit_set_chunk*)malloc(capacity * sizeof(*array->values));
    if (capacity != 0u && array->values == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    const ft_status status = ft_tree_visit(&set->chunks, ft_chunk_collect, array);
    if (status != FT_STATUS_OK) {
        free(array->values);
    }
    return status;
}

static bool ft_chunked_chunks_equal(
    const ft_chunked_bit_set_chunk* values,
    size_t count,
    const ft_chunk_array* source)
{
    return count == source->count
        && (count == 0u
            || memcmp(values, source->values, count * sizeof(*values)) == 0);
}

static ft_status ft_chunked_build(
    const ft_persistent_chunked_bit_set* source,
    const ft_chunked_bit_set_chunk* values,
    size_t count,
    ft_persistent_chunked_bit_set* result)
{
    ft_tree tree;
    ft_status status = ft_tree_init(&tree, &source->context->tree_policy);
    if (status != FT_STATUS_OK) {
        return status;
    }
    for (size_t index = 0u; index < count; ++index) {
        ft_tree next;
        status = ft_tree_push_back(&tree, &values[index], &next);
        if (status != FT_STATUS_OK) {
            ft_tree_dispose(&tree);
            return status;
        }
        ft_tree_dispose(&tree);
        tree = next;
    }
    ft_persistent_chunked_bit_set candidate;
    (void)ft_chunked_wrap_tree(source, tree, &candidate);
    ft_chunked_publish(source, result, &candidate);
    return FT_STATUS_OK;
}

static ft_status ft_chunked_combine(
    const ft_persistent_chunked_bit_set* left,
    const ft_persistent_chunked_bit_set* right,
    ft_chunked_bit_set_operation operation,
    ft_persistent_chunked_bit_set* result)
{
    if (!ft_chunked_bit_set_valid(left) || !ft_chunked_bit_set_valid(right)
        || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_chunk_array left_values;
    ft_chunk_array right_values;
    ft_status status = ft_chunked_collect(left, &left_values);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_chunked_collect(right, &right_values);
    if (status != FT_STATUS_OK) {
        free(left_values.values);
        return status;
    }
    const size_t capacity = left_values.count + right_values.count;
    ft_chunked_bit_set_chunk* combined = capacity == 0u ? NULL
        : (ft_chunked_bit_set_chunk*)malloc(capacity * sizeof(*combined));
    if (capacity != 0u && combined == NULL) {
        free(left_values.values);
        free(right_values.values);
        return FT_STATUS_NO_MEMORY;
    }
    size_t left_index = 0u, right_index = 0u, count = 0u;
    while (left_index < left_values.count || right_index < right_values.count) {
        if (right_index == right_values.count
            || (left_index < left_values.count
                && left_values.values[left_index].word_index
                    < right_values.values[right_index].word_index)) {
            if (operation == FT_CHUNKED_UNION || operation == FT_CHUNKED_EXCEPT
                || operation == FT_CHUNKED_SYMMETRIC_EXCEPT) {
                combined[count++] = left_values.values[left_index];
            }
            ++left_index;
            continue;
        }
        if (left_index == left_values.count
            || right_values.values[right_index].word_index
                < left_values.values[left_index].word_index) {
            if (operation == FT_CHUNKED_UNION
                || operation == FT_CHUNKED_SYMMETRIC_EXCEPT) {
                combined[count++] = right_values.values[right_index];
            }
            ++right_index;
            continue;
        }
        const uint64_t left_bits = left_values.values[left_index].bits;
        const uint64_t right_bits = right_values.values[right_index].bits;
        const uint64_t bits = operation == FT_CHUNKED_UNION
            ? left_bits | right_bits
            : operation == FT_CHUNKED_INTERSECT
                ? left_bits & right_bits
                : operation == FT_CHUNKED_EXCEPT
                    ? left_bits & ~right_bits
                    : left_bits ^ right_bits;
        if (bits != 0u) {
            combined[count].word_index = left_values.values[left_index].word_index;
            combined[count].bits = bits;
            ++count;
        }
        ++left_index;
        ++right_index;
    }
    if (ft_chunked_chunks_equal(combined, count, &left_values)) {
        status = ft_chunked_publish_copy(left, result);
    } else {
        status = ft_chunked_build(left, combined, count, result);
    }
    free(combined);
    free(right_values.values);
    free(left_values.values);
    return status;
}

ft_status ft_persistent_chunked_bit_set_union(
    const ft_persistent_chunked_bit_set* left,
    const ft_persistent_chunked_bit_set* right,
    ft_persistent_chunked_bit_set* result)
{
    return ft_chunked_combine(left, right, FT_CHUNKED_UNION, result);
}

ft_status ft_persistent_chunked_bit_set_intersect(
    const ft_persistent_chunked_bit_set* left,
    const ft_persistent_chunked_bit_set* right,
    ft_persistent_chunked_bit_set* result)
{
    return ft_chunked_combine(left, right, FT_CHUNKED_INTERSECT, result);
}

ft_status ft_persistent_chunked_bit_set_except(
    const ft_persistent_chunked_bit_set* left,
    const ft_persistent_chunked_bit_set* right,
    ft_persistent_chunked_bit_set* result)
{
    return ft_chunked_combine(left, right, FT_CHUNKED_EXCEPT, result);
}

ft_status ft_persistent_chunked_bit_set_symmetric_except(
    const ft_persistent_chunked_bit_set* left,
    const ft_persistent_chunked_bit_set* right,
    ft_persistent_chunked_bit_set* result)
{
    return ft_chunked_combine(left, right, FT_CHUNKED_SYMMETRIC_EXCEPT, result);
}

ft_status ft_persistent_chunked_bit_set_clear(
    const ft_persistent_chunked_bit_set* set,
    ft_persistent_chunked_bit_set* result)
{
    if (!ft_chunked_bit_set_valid(set) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return ft_persistent_chunked_bit_set_empty(set)
        ? ft_chunked_publish_copy(set, result)
        : ft_chunked_build(set, NULL, 0u, result);
}

typedef struct ft_chunked_visit_context {
    ft_chunked_bit_set_visit_fn visitor;
    void* context;
} ft_chunked_visit_context;

static void ft_chunked_visit_chunk(const void* raw_chunk, void* raw_context)
{
    const ft_chunked_bit_set_chunk* chunk =
        (const ft_chunked_bit_set_chunk*)raw_chunk;
    ft_chunked_visit_context* context =
        (ft_chunked_visit_context*)raw_context;
    uint64_t bits = chunk->bits;
    while (bits != 0u) {
        const unsigned offset = ft_chunked_trailing_zero_count(bits);
        context->visitor(
            (int32_t)(((int64_t)chunk->word_index << 6) + offset),
            context->context);
        bits &= bits - 1u;
    }
}

ft_status ft_persistent_chunked_bit_set_visit(
    const ft_persistent_chunked_bit_set* set,
    ft_chunked_bit_set_visit_fn visitor,
    void* context)
{
    if (!ft_chunked_bit_set_valid(set) || visitor == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_chunked_visit_context bridge = { visitor, context };
    return ft_tree_visit(&set->chunks, ft_chunked_visit_chunk, &bridge);
}

typedef struct ft_chunked_validation_context {
    bool valid;
    int32_t previous_word;
    size_t chunk_count;
    uint64_t pop_count;
} ft_chunked_validation_context;

static void ft_chunked_validate_chunk(const void* raw_chunk, void* raw_context)
{
    const ft_chunked_bit_set_chunk* chunk =
        (const ft_chunked_bit_set_chunk*)raw_chunk;
    ft_chunked_validation_context* context =
        (ft_chunked_validation_context*)raw_context;
    if (chunk->word_index <= context->previous_word || chunk->bits == 0u) {
        context->valid = false;
    }
    context->previous_word = chunk->word_index;
    ++context->chunk_count;
    context->pop_count += ft_chunked_pop_count(chunk->bits);
}

bool ft_persistent_chunked_bit_set_debug_validate(
    const ft_persistent_chunked_bit_set* set)
{
    if (!ft_chunked_bit_set_valid(set)) {
        return false;
    }
    ft_chunked_validation_context context = { true, -1, 0u, 0u };
    if (ft_tree_visit(&set->chunks, ft_chunked_validate_chunk, &context)
        != FT_STATUS_OK) {
        return false;
    }
    ft_chunked_bit_set_measure measure;
    return ft_chunked_measure_of(set, &measure) && context.valid
        && context.chunk_count == measure.chunk_count
        && context.pop_count == measure.pop_count
        && ((context.chunk_count == 0u && !measure.has_last_word)
            || (context.chunk_count != 0u && measure.has_last_word
                && context.previous_word == measure.last_word));
}

bool ft_persistent_chunked_bit_set_debug_shares_root(
    const ft_persistent_chunked_bit_set* left,
    const ft_persistent_chunked_bit_set* right)
{
    return ft_chunked_bit_set_valid(left) && ft_chunked_bit_set_valid(right)
        && left->chunks.rep == right->chunks.rep;
}

ft_status ft_persistent_chunked_bit_set_from_array(
    ft_persistent_chunked_bit_set* set,
    const int32_t* bit_indexes,
    size_t count)
{
    if (set == NULL || (count != 0u && bit_indexes == NULL)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_status status = ft_persistent_chunked_bit_set_init(set);
    if (status != FT_STATUS_OK) {
        return status;
    }
    for (size_t index = 0u; index < count; ++index) {
        status = ft_persistent_chunked_bit_set_add(
            set, bit_indexes[index], set);
        if (status != FT_STATUS_OK) {
            ft_persistent_chunked_bit_set_dispose(set);
            return status;
        }
    }
    return FT_STATUS_OK;
}

static bool ft_chunked_cursor_is_valid(
    const ft_persistent_chunked_bit_set_cursor* cursor)
{
    return cursor != NULL && ft_chunked_bit_set_valid(&cursor->set) &&
        cursor->position <= ft_persistent_chunked_bit_set_count(&cursor->set);
}

static ft_status ft_chunked_cursor_stage(
    const ft_persistent_chunked_bit_set* set,
    uint64_t position,
    ft_persistent_chunked_bit_set_cursor* result)
{
    (void)memset(result, 0, sizeof(*result));
    ft_status status = ft_persistent_chunked_bit_set_copy(set, &result->set);
    if (status == FT_STATUS_OK) {
        result->position = position;
    }
    return status;
}

static void ft_chunked_cursor_publish(
    const ft_persistent_chunked_bit_set_cursor* source,
    ft_persistent_chunked_bit_set_cursor* staged,
    ft_persistent_chunked_bit_set_cursor* result)
{
    if (result == source) {
        ft_persistent_chunked_bit_set_cursor_dispose(result);
    }
    ft_persistent_chunked_bit_set_cursor_move(result, staged);
}

static void ft_chunked_cursor_publish_set(
    const ft_persistent_chunked_bit_set_cursor* source,
    ft_persistent_chunked_bit_set* set,
    uint64_t position,
    ft_persistent_chunked_bit_set_cursor* result)
{
    ft_persistent_chunked_bit_set_cursor staged;
    (void)memset(&staged, 0, sizeof(staged));
    ft_persistent_chunked_bit_set_move(&staged.set, set);
    staged.position = position;
    ft_chunked_cursor_publish(source, &staged, result);
}

ft_status ft_persistent_chunked_bit_set_get_cursor(
    const ft_persistent_chunked_bit_set* set,
    uint64_t position,
    ft_persistent_chunked_bit_set_cursor* result)
{
    if (!ft_chunked_bit_set_valid(set) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (position > ft_persistent_chunked_bit_set_count(set)) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    return ft_chunked_cursor_stage(set, position, result);
}

ft_status ft_persistent_chunked_bit_set_get_cursor_at_or_after(
    const ft_persistent_chunked_bit_set* set,
    int32_t bit_index,
    ft_persistent_chunked_bit_set_cursor* result)
{
    if (!ft_chunked_bit_set_valid(set) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    uint64_t position = 0;
    ft_status status = bit_index <= 0
        ? FT_STATUS_OK
        : ft_persistent_chunked_bit_set_rank(set, bit_index - 1, &position);
    return status == FT_STATUS_OK
        ? ft_persistent_chunked_bit_set_get_cursor(set, position, result)
        : status;
}

ft_status ft_persistent_chunked_bit_set_get_cursor_at_item(
    const ft_persistent_chunked_bit_set* set,
    int32_t bit_index,
    bool* found,
    ft_persistent_chunked_bit_set_cursor* result)
{
    if (!ft_chunked_bit_set_valid(set) || found == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_persistent_chunked_bit_set_cursor staged;
    ft_status status = ft_persistent_chunked_bit_set_get_cursor_at_or_after(
        set, bit_index, &staged);
    if (status == FT_STATUS_OK) {
        *found = ft_persistent_chunked_bit_set_contains(set, bit_index);
        ft_persistent_chunked_bit_set_cursor_move(result, &staged);
    }
    return status;
}

ft_status ft_persistent_chunked_bit_set_cursor_copy(
    const ft_persistent_chunked_bit_set_cursor* source,
    ft_persistent_chunked_bit_set_cursor* destination)
{
    if (!ft_chunked_cursor_is_valid(source) || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return FT_STATUS_OK;
    }
    return ft_chunked_cursor_stage(
        &source->set, source->position, destination);
}

void ft_persistent_chunked_bit_set_cursor_move(
    ft_persistent_chunked_bit_set_cursor* destination,
    ft_persistent_chunked_bit_set_cursor* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    (void)memset(destination, 0, sizeof(*destination));
    ft_persistent_chunked_bit_set_move(&destination->set, &source->set);
    destination->position = source->position;
    source->position = 0;
}

void ft_persistent_chunked_bit_set_cursor_dispose(
    ft_persistent_chunked_bit_set_cursor* cursor)
{
    if (cursor != NULL) {
        ft_persistent_chunked_bit_set_dispose(&cursor->set);
        (void)memset(cursor, 0, sizeof(*cursor));
    }
}

bool ft_persistent_chunked_bit_set_cursor_valid(
    const ft_persistent_chunked_bit_set_cursor* cursor)
{
    return ft_chunked_cursor_is_valid(cursor);
}

bool ft_persistent_chunked_bit_set_cursor_empty(
    const ft_persistent_chunked_bit_set_cursor* cursor)
{
    return !ft_chunked_cursor_is_valid(cursor) ||
        ft_persistent_chunked_bit_set_empty(&cursor->set);
}

uint64_t ft_persistent_chunked_bit_set_cursor_count(
    const ft_persistent_chunked_bit_set_cursor* cursor)
{
    return ft_chunked_cursor_is_valid(cursor)
        ? ft_persistent_chunked_bit_set_count(&cursor->set)
        : 0;
}

uint64_t ft_persistent_chunked_bit_set_cursor_position(
    const ft_persistent_chunked_bit_set_cursor* cursor)
{
    return ft_chunked_cursor_is_valid(cursor) ? cursor->position : 0;
}

ft_status ft_persistent_chunked_bit_set_cursor_is_at_start(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    bool* result)
{
    if (!ft_chunked_cursor_is_valid(cursor) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    *result = cursor->position == 0;
    return FT_STATUS_OK;
}

ft_status ft_persistent_chunked_bit_set_cursor_is_at_end(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    bool* result)
{
    if (!ft_chunked_cursor_is_valid(cursor) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    *result = cursor->position == ft_persistent_chunked_bit_set_count(&cursor->set);
    return FT_STATUS_OK;
}

ft_status ft_persistent_chunked_bit_set_cursor_try_peek_previous(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    bool* found,
    int32_t* bit_index)
{
    if (!ft_chunked_cursor_is_valid(cursor) || found == NULL || bit_index == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (cursor->position == 0) {
        *found = false;
        *bit_index = 0;
        return FT_STATUS_OK;
    }
    return ft_persistent_chunked_bit_set_try_select(
        &cursor->set, cursor->position - 1u, found, bit_index);
}

ft_status ft_persistent_chunked_bit_set_cursor_try_peek_next(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    bool* found,
    int32_t* bit_index)
{
    if (!ft_chunked_cursor_is_valid(cursor) || found == NULL || bit_index == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return ft_persistent_chunked_bit_set_try_select(
        &cursor->set, cursor->position, found, bit_index);
}

ft_status ft_persistent_chunked_bit_set_cursor_seek_rank(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    uint64_t position,
    ft_persistent_chunked_bit_set_cursor* result)
{
    if (!ft_chunked_cursor_is_valid(cursor) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (position > ft_persistent_chunked_bit_set_count(&cursor->set)) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    if (cursor == result && cursor->position == position) {
        return FT_STATUS_OK;
    }
    ft_persistent_chunked_bit_set_cursor staged;
    ft_status status = ft_chunked_cursor_stage(
        &cursor->set, position, &staged);
    if (status == FT_STATUS_OK) {
        ft_chunked_cursor_publish(cursor, &staged, result);
    }
    return status;
}

ft_status ft_persistent_chunked_bit_set_cursor_move_previous(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    ft_persistent_chunked_bit_set_cursor* result)
{
    if (!ft_chunked_cursor_is_valid(cursor) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (cursor->position == 0) {
        return ft_persistent_chunked_bit_set_empty(&cursor->set)
            ? FT_STATUS_EMPTY
            : FT_STATUS_OUT_OF_RANGE;
    }
    return ft_persistent_chunked_bit_set_cursor_seek_rank(
        cursor, cursor->position - 1u, result);
}

ft_status ft_persistent_chunked_bit_set_cursor_move_next(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    ft_persistent_chunked_bit_set_cursor* result)
{
    if (!ft_chunked_cursor_is_valid(cursor) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    const uint64_t count = ft_persistent_chunked_bit_set_count(&cursor->set);
    if (cursor->position == count) {
        return count == 0 ? FT_STATUS_EMPTY : FT_STATUS_OUT_OF_RANGE;
    }
    return ft_persistent_chunked_bit_set_cursor_seek_rank(
        cursor, cursor->position + 1u, result);
}

ft_status ft_persistent_chunked_bit_set_cursor_add(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    int32_t bit_index,
    ft_persistent_chunked_bit_set_cursor* result)
{
    if (!ft_chunked_cursor_is_valid(cursor) || bit_index < 0 || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (ft_persistent_chunked_bit_set_contains(&cursor->set, bit_index)) {
        if (cursor == result) {
            return FT_STATUS_OK;
        }
        return ft_chunked_cursor_stage(&cursor->set, cursor->position, result);
    }
    uint64_t position = 0;
    ft_status status = bit_index == 0
        ? FT_STATUS_OK
        : ft_persistent_chunked_bit_set_rank(
            &cursor->set, bit_index - 1, &position);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_persistent_chunked_bit_set edited;
    status = ft_persistent_chunked_bit_set_add(
        &cursor->set, bit_index, &edited);
    if (status == FT_STATUS_OK) {
        ft_chunked_cursor_publish_set(
            cursor, &edited, position + 1u, result);
    }
    return status;
}

static ft_status ft_chunked_cursor_delete_at(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    uint64_t rank,
    uint64_t position,
    ft_persistent_chunked_bit_set_cursor* result)
{
    int32_t bit_index = 0;
    ft_status status = ft_persistent_chunked_bit_set_select(
        &cursor->set, rank, &bit_index);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_persistent_chunked_bit_set edited;
    status = ft_persistent_chunked_bit_set_remove(
        &cursor->set, bit_index, &edited);
    if (status == FT_STATUS_OK) {
        ft_chunked_cursor_publish_set(cursor, &edited, position, result);
    }
    return status;
}

ft_status ft_persistent_chunked_bit_set_cursor_delete_previous(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    ft_persistent_chunked_bit_set_cursor* result)
{
    if (!ft_chunked_cursor_is_valid(cursor) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (cursor->position == 0) {
        return ft_persistent_chunked_bit_set_empty(&cursor->set)
            ? FT_STATUS_EMPTY
            : FT_STATUS_OUT_OF_RANGE;
    }
    return ft_chunked_cursor_delete_at(
        cursor, cursor->position - 1u, cursor->position - 1u, result);
}

ft_status ft_persistent_chunked_bit_set_cursor_delete_next(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    ft_persistent_chunked_bit_set_cursor* result)
{
    if (!ft_chunked_cursor_is_valid(cursor) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    const uint64_t count = ft_persistent_chunked_bit_set_count(&cursor->set);
    if (cursor->position == count) {
        return count == 0 ? FT_STATUS_EMPTY : FT_STATUS_OUT_OF_RANGE;
    }
    return ft_chunked_cursor_delete_at(
        cursor, cursor->position, cursor->position, result);
}

ft_status ft_persistent_chunked_bit_set_cursor_snapshot(
    const ft_persistent_chunked_bit_set_cursor* cursor,
    ft_persistent_chunked_bit_set* result)
{
    return !ft_chunked_cursor_is_valid(cursor) || result == NULL
        ? FT_STATUS_INVALID_ARGUMENT
        : ft_persistent_chunked_bit_set_copy(&cursor->set, result);
}
