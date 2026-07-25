#include <durable7/finger_tree/canonical_sorted_set.h>

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_MSC_VER) || defined(__clang__)
#include <stdatomic.h>
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#endif

enum {
    FT_CANONICAL_MINIMUM_KEY_BYTES = 32,
    FT_CANONICAL_DIGEST_BYTES = 32
};

#if defined(_MSC_VER) && !defined(__clang__)
typedef volatile LONG64 ft_canonical_ref_count;
typedef volatile LONG64 ft_canonical_atomic_u64;
typedef volatile LONG ft_canonical_atomic_bool;

static void ft_canonical_ref_init(ft_canonical_ref_count* value)
{
    *value = 1;
}

static void ft_canonical_ref_retain(ft_canonical_ref_count* value)
{
    (void)InterlockedIncrement64(value);
}

static bool ft_canonical_ref_release(ft_canonical_ref_count* value)
{
    return InterlockedDecrement64(value) == 0;
}

static void ft_canonical_atomic_init_digest(
    ft_canonical_atomic_u64* digest,
    ft_canonical_atomic_bool* ready)
{
    *digest = 0;
    *ready = 0;
}

static bool ft_canonical_digest_ready(const ft_canonical_atomic_bool* ready)
{
    return InterlockedCompareExchange((volatile LONG*)ready, 0, 0) != 0;
}

static uint64_t ft_canonical_digest_load(const ft_canonical_atomic_u64* digest)
{
    return (uint64_t)InterlockedCompareExchange64((volatile LONG64*)digest, 0, 0);
}

static void ft_canonical_digest_publish(
    ft_canonical_atomic_u64* destination,
    ft_canonical_atomic_bool* ready,
    uint64_t digest)
{
    (void)InterlockedExchange64(destination, (LONG64)digest);
    (void)InterlockedExchange(ready, 1);
}
#else
typedef atomic_size_t ft_canonical_ref_count;
typedef atomic_uint_fast64_t ft_canonical_atomic_u64;
typedef atomic_bool ft_canonical_atomic_bool;

static void ft_canonical_ref_init(ft_canonical_ref_count* value)
{
    atomic_init(value, 1);
}

static void ft_canonical_ref_retain(ft_canonical_ref_count* value)
{
    (void)atomic_fetch_add_explicit(value, 1, memory_order_relaxed);
}

static bool ft_canonical_ref_release(ft_canonical_ref_count* value)
{
    return atomic_fetch_sub_explicit(value, 1, memory_order_acq_rel) == 1;
}

static void ft_canonical_atomic_init_digest(
    ft_canonical_atomic_u64* digest,
    ft_canonical_atomic_bool* ready)
{
    atomic_init(digest, 0);
    atomic_init(ready, false);
}

static bool ft_canonical_digest_ready(const ft_canonical_atomic_bool* ready)
{
    return atomic_load_explicit(ready, memory_order_acquire);
}

static uint64_t ft_canonical_digest_load(const ft_canonical_atomic_u64* digest)
{
    return atomic_load_explicit(digest, memory_order_relaxed);
}

static void ft_canonical_digest_publish(
    ft_canonical_atomic_u64* destination,
    ft_canonical_atomic_bool* ready,
    uint64_t digest)
{
    atomic_store_explicit(destination, digest, memory_order_relaxed);
    atomic_store_explicit(ready, true, memory_order_release);
}
#endif

typedef struct ft_canonical_value {
    ft_canonical_ref_count refs;
    void* bytes;
} ft_canonical_value;

struct ft_canonical_policy_rep {
    ft_canonical_ref_count refs;
    ft_canonical_policy_config config;
    unsigned char* rank_key;
    size_t rank_key_size;
    bool has_public_seed;
    uint64_t public_seed;
};

struct ft_canonical_node {
    ft_canonical_ref_count refs;
    ft_canonical_value* value;
    ft_zip_tree_rank rank;
    ft_canonical_node* left;
    ft_canonical_node* right;
    size_t count;
    size_t height;
    ft_canonical_atomic_u64 digest;
    ft_canonical_atomic_bool digest_ready;
    ft_canonical_node* release_next;
};

typedef struct ft_canonical_path_step {
    ft_canonical_node* node;
    bool went_left;
} ft_canonical_path_step;

typedef struct ft_canonical_merge_step {
    ft_canonical_node* node;
    bool chose_left;
} ft_canonical_merge_step;

typedef struct ft_canonical_digest_frame {
    ft_canonical_node* node;
    bool expanded;
} ft_canonical_digest_frame;

typedef struct ft_canonical_node_pair {
    ft_canonical_node* left;
    ft_canonical_node* right;
} ft_canonical_node_pair;

static void* ft_canonical_default_allocate(size_t size, void* context)
{
    (void)context;
    return malloc(size == 0 ? 1 : size);
}

static void ft_canonical_default_deallocate(void* allocation, void* context)
{
    (void)context;
    free(allocation);
}

static bool ft_canonical_add_overflows(size_t left, size_t right, size_t* result)
{
    if (left > SIZE_MAX - right) {
        return true;
    }
    *result = left + right;
    return false;
}

static bool ft_canonical_multiply_overflows(size_t left, size_t right, size_t* result)
{
    if (left != 0 && right > SIZE_MAX / left) {
        return true;
    }
    *result = left * right;
    return false;
}

static void* ft_canonical_allocate_config(
    const ft_canonical_policy_config* config,
    size_t size)
{
    return config->allocator.allocate(size == 0 ? 1 : size, config->allocator.context);
}

static void ft_canonical_deallocate_config(
    const ft_canonical_policy_config* config,
    void* allocation)
{
    if (allocation != NULL) {
        config->allocator.deallocate(allocation, config->allocator.context);
    }
}

static void* ft_canonical_allocate(const ft_canonical_policy_rep* policy, size_t size)
{
    return ft_canonical_allocate_config(&policy->config, size);
}

static void ft_canonical_deallocate(const ft_canonical_policy_rep* policy, void* allocation)
{
    ft_canonical_deallocate_config(&policy->config, allocation);
}

static bool ft_canonical_config_valid(const ft_canonical_policy_config* config)
{
    return config != NULL && config->value_size != 0 && config->value_type_identity != NULL &&
        config->compare != NULL && config->rank_hash != NULL && config->allocator.allocate != NULL &&
        config->allocator.deallocate != NULL;
}

static void ft_canonical_write_be64(uint64_t value, unsigned char* destination)
{
    size_t index = 0;
    for (index = 0; index != sizeof(value); ++index) {
        destination[index] = (unsigned char)(value >> ((sizeof(value) - 1 - index) * CHAR_BIT));
    }
}

static uint64_t ft_canonical_read_be64(const unsigned char* source)
{
    uint64_t result = 0;
    size_t index = 0;
    for (index = 0; index != sizeof(result); ++index) {
        result = (result << CHAR_BIT) | source[index];
    }
    return result;
}

static unsigned ft_canonical_leading_zero_count(uint64_t value)
{
    unsigned result = 0;
    uint64_t bit = UINT64_C(1) << 63;
    while (bit != 0 && (value & bit) == 0) {
        ++result;
        bit >>= 1;
    }
    return result;
}

static uint64_t ft_canonical_rotate_left(uint64_t value, unsigned count)
{
    return (value << count) | (value >> (64U - count));
}

static uint64_t ft_canonical_mix(uint64_t value)
{
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}

#if defined(_WIN32)
static ft_status ft_canonical_bcrypt_digest(
    const ft_canonical_policy_config* config,
    bool hmac,
    const unsigned char* key,
    size_t key_size,
    const unsigned char* message,
    size_t message_size,
    unsigned char digest[FT_CANONICAL_DIGEST_BYTES])
{
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    unsigned char* object = NULL;
    ULONG object_size = 0;
    ULONG digest_size = 0;
    ULONG copied = 0;
    NTSTATUS native_status = 0;
    ft_status status = FT_STATUS_CRYPTO_FAILURE;
    if (key_size > ULONG_MAX || message_size > ULONG_MAX) {
        return FT_STATUS_OVERFLOW;
    }
    native_status = BCryptOpenAlgorithmProvider(
        &algorithm,
        BCRYPT_SHA256_ALGORITHM,
        NULL,
        hmac ? BCRYPT_ALG_HANDLE_HMAC_FLAG : 0);
    if (native_status < 0) {
        goto cleanup;
    }
    native_status = BCryptGetProperty(
        algorithm,
        BCRYPT_OBJECT_LENGTH,
        (PUCHAR)&object_size,
        (ULONG)sizeof(object_size),
        &copied,
        0);
    if (native_status < 0 || copied != sizeof(object_size)) {
        goto cleanup;
    }
    native_status = BCryptGetProperty(
        algorithm,
        BCRYPT_HASH_LENGTH,
        (PUCHAR)&digest_size,
        (ULONG)sizeof(digest_size),
        &copied,
        0);
    if (native_status < 0 || copied != sizeof(digest_size) || digest_size != FT_CANONICAL_DIGEST_BYTES) {
        goto cleanup;
    }
    object = (unsigned char*)ft_canonical_allocate_config(config, object_size);
    if (object == NULL) {
        status = FT_STATUS_NO_MEMORY;
        goto cleanup;
    }
    native_status = BCryptCreateHash(
        algorithm,
        &hash,
        object,
        object_size,
        key_size == 0 ? NULL : (PUCHAR)key,
        (ULONG)key_size,
        0);
    if (native_status < 0) {
        goto cleanup;
    }
    native_status = BCryptHashData(
        hash,
        message_size == 0 ? NULL : (PUCHAR)message,
        (ULONG)message_size,
        0);
    if (native_status < 0) {
        goto cleanup;
    }
    native_status = BCryptFinishHash(hash, digest, FT_CANONICAL_DIGEST_BYTES, 0);
    if (native_status < 0) {
        goto cleanup;
    }
    status = FT_STATUS_OK;

cleanup:
    if (hash != NULL) {
        (void)BCryptDestroyHash(hash);
    }
    ft_canonical_deallocate_config(config, object);
    if (algorithm != NULL) {
        (void)BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    return status;
}

#endif

static ft_status ft_canonical_compare(
    const ft_canonical_policy_rep* policy,
    const void* left,
    const void* right,
    int* comparison);
static bool ft_canonical_set_valid(const ft_canonical_sorted_set* set);

static ft_status ft_canonical_cursor_bound(
    const ft_canonical_sorted_set* set,
    const void* value,
    bool upper_bound,
    size_t* position,
    bool* found)
{
    if (!ft_canonical_set_valid(set) || value == NULL || position == NULL || found == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    size_t rank = 0;
    ft_canonical_node* node = set->root;
    *found = false;
    while (node != NULL) {
        int comparison = 0;
        ft_status status = ft_canonical_compare(set->policy, value, node->value->bytes, &comparison);
        if (status != FT_STATUS_OK) {
            return status;
        }
        const size_t left_count = node->left == NULL ? 0 : node->left->count;
        if (comparison == 0) {
            if (rank > SIZE_MAX - left_count ||
                (upper_bound && rank + left_count == SIZE_MAX)) {
                return FT_STATUS_OVERFLOW;
            }
            rank += left_count + (upper_bound ? 1u : 0u);
            *found = true;
            break;
        }
        if (comparison < 0) {
            node = node->left;
        } else {
            if (left_count == SIZE_MAX || rank > SIZE_MAX - left_count - 1u) {
                return FT_STATUS_OVERFLOW;
            }
            rank += left_count + 1u;
            node = node->right;
        }
    }
    *position = rank;
    return FT_STATUS_OK;
}

static const void* ft_canonical_cursor_item_at(
    const ft_canonical_sorted_set* set,
    size_t position)
{
    ft_canonical_node* node = set->root;
    while (node != NULL) {
        const size_t left_count = node->left == NULL ? 0 : node->left->count;
        if (position < left_count) {
            node = node->left;
        } else if (position == left_count) {
            return node->value->bytes;
        } else {
            position -= left_count + 1u;
            node = node->right;
        }
    }
    return NULL;
}

static bool ft_canonical_cursor_is_valid(const ft_canonical_sorted_set_cursor* cursor)
{
    return cursor != NULL && ft_canonical_set_valid(&cursor->set) &&
        cursor->position <= ft_canonical_sorted_set_size(&cursor->set);
}

static ft_status ft_canonical_cursor_stage(
    const ft_canonical_sorted_set* set,
    size_t position,
    ft_canonical_sorted_set_cursor* result)
{
    (void)memset(result, 0, sizeof(*result));
    ft_status status = ft_canonical_sorted_set_copy(set, &result->set);
    if (status == FT_STATUS_OK) {
        result->position = position;
    }
    return status;
}

static void ft_canonical_cursor_publish(
    const ft_canonical_sorted_set_cursor* source,
    ft_canonical_sorted_set_cursor* staged,
    ft_canonical_sorted_set_cursor* result)
{
    if (result == source) {
        ft_canonical_sorted_set_cursor_dispose(result);
    }
    ft_canonical_sorted_set_cursor_move(result, staged);
}

static ft_status ft_canonical_cursor_publish_set(
    const ft_canonical_sorted_set_cursor* source,
    ft_canonical_sorted_set* set,
    size_t position,
    ft_canonical_sorted_set_cursor* result)
{
    ft_canonical_sorted_set_cursor staged;
    (void)memset(&staged, 0, sizeof(staged));
    ft_canonical_sorted_set_move(&staged.set, set);
    staged.position = position;
    ft_canonical_cursor_publish(source, &staged, result);
    return FT_STATUS_OK;
}

ft_status ft_canonical_sorted_set_get_cursor(
    const ft_canonical_sorted_set* set,
    size_t position,
    ft_canonical_sorted_set_cursor* result)
{
    if (!ft_canonical_set_valid(set) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (position > ft_canonical_sorted_set_size(set)) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    return ft_canonical_cursor_stage(set, position, result);
}

static ft_status ft_canonical_get_bound_cursor(
    const ft_canonical_sorted_set* set,
    const void* value,
    bool upper_bound,
    bool* found,
    ft_canonical_sorted_set_cursor* result)
{
    if (!ft_canonical_set_valid(set) || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    size_t position = 0;
    bool located = false;
    ft_status status = ft_canonical_cursor_bound(set, value, upper_bound, &position, &located);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_canonical_sorted_set_cursor staged;
    status = ft_canonical_cursor_stage(set, position, &staged);
    if (status == FT_STATUS_OK) {
        if (found != NULL) {
            *found = located;
        }
        ft_canonical_sorted_set_cursor_move(result, &staged);
    }
    return status;
}

ft_status ft_canonical_sorted_set_get_cursor_lower_bound(
    const ft_canonical_sorted_set* set,
    const void* value,
    ft_canonical_sorted_set_cursor* result)
{
    return ft_canonical_get_bound_cursor(set, value, false, NULL, result);
}

ft_status ft_canonical_sorted_set_get_cursor_upper_bound(
    const ft_canonical_sorted_set* set,
    const void* value,
    ft_canonical_sorted_set_cursor* result)
{
    return ft_canonical_get_bound_cursor(set, value, true, NULL, result);
}

ft_status ft_canonical_sorted_set_get_cursor_at_item(
    const ft_canonical_sorted_set* set,
    const void* value,
    bool* found,
    ft_canonical_sorted_set_cursor* result)
{
    return found == NULL
        ? FT_STATUS_INVALID_ARGUMENT
        : ft_canonical_get_bound_cursor(set, value, false, found, result);
}

ft_status ft_canonical_sorted_set_cursor_copy(
    const ft_canonical_sorted_set_cursor* source,
    ft_canonical_sorted_set_cursor* destination)
{
    if (!ft_canonical_cursor_is_valid(source) || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return FT_STATUS_OK;
    }
    return ft_canonical_cursor_stage(&source->set, source->position, destination);
}

void ft_canonical_sorted_set_cursor_move(
    ft_canonical_sorted_set_cursor* destination,
    ft_canonical_sorted_set_cursor* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    (void)memset(destination, 0, sizeof(*destination));
    ft_canonical_sorted_set_move(&destination->set, &source->set);
    destination->position = source->position;
    source->position = 0;
}

void ft_canonical_sorted_set_cursor_dispose(ft_canonical_sorted_set_cursor* cursor)
{
    if (cursor != NULL) {
        ft_canonical_sorted_set_dispose(&cursor->set);
        (void)memset(cursor, 0, sizeof(*cursor));
    }
}

bool ft_canonical_sorted_set_cursor_valid(const ft_canonical_sorted_set_cursor* cursor)
{
    return ft_canonical_cursor_is_valid(cursor);
}

bool ft_canonical_sorted_set_cursor_empty(const ft_canonical_sorted_set_cursor* cursor)
{
    return !ft_canonical_cursor_is_valid(cursor) || ft_canonical_sorted_set_empty(&cursor->set);
}

size_t ft_canonical_sorted_set_cursor_size(const ft_canonical_sorted_set_cursor* cursor)
{
    return ft_canonical_cursor_is_valid(cursor) ? ft_canonical_sorted_set_size(&cursor->set) : 0;
}

size_t ft_canonical_sorted_set_cursor_position(const ft_canonical_sorted_set_cursor* cursor)
{
    return ft_canonical_cursor_is_valid(cursor) ? cursor->position : 0;
}

ft_status ft_canonical_sorted_set_cursor_is_at_start(
    const ft_canonical_sorted_set_cursor* cursor,
    bool* result)
{
    if (!ft_canonical_cursor_is_valid(cursor) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    *result = cursor->position == 0;
    return FT_STATUS_OK;
}

ft_status ft_canonical_sorted_set_cursor_is_at_end(
    const ft_canonical_sorted_set_cursor* cursor,
    bool* result)
{
    if (!ft_canonical_cursor_is_valid(cursor) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    *result = cursor->position == ft_canonical_sorted_set_size(&cursor->set);
    return FT_STATUS_OK;
}

static ft_status ft_canonical_cursor_peek(
    const ft_canonical_sorted_set_cursor* cursor,
    size_t position,
    bool* found,
    const void** value_ref)
{
    if (!ft_canonical_cursor_is_valid(cursor) || found == NULL || value_ref == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    const void* value = ft_canonical_cursor_item_at(&cursor->set, position);
    *found = value != NULL;
    if (value != NULL) {
        *value_ref = value;
    }
    return FT_STATUS_OK;
}

ft_status ft_canonical_sorted_set_cursor_try_peek_previous_ref(
    const ft_canonical_sorted_set_cursor* cursor,
    bool* found,
    const void** value_ref)
{
    if (!ft_canonical_cursor_is_valid(cursor) || found == NULL || value_ref == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (cursor->position == 0) {
        *found = false;
        return FT_STATUS_OK;
    }
    return ft_canonical_cursor_peek(cursor, cursor->position - 1u, found, value_ref);
}

ft_status ft_canonical_sorted_set_cursor_try_peek_next_ref(
    const ft_canonical_sorted_set_cursor* cursor,
    bool* found,
    const void** value_ref)
{
    return ft_canonical_cursor_peek(
        cursor,
        cursor == NULL ? 0 : cursor->position,
        found,
        value_ref);
}

ft_status ft_canonical_sorted_set_cursor_seek_rank(
    const ft_canonical_sorted_set_cursor* cursor,
    size_t position,
    ft_canonical_sorted_set_cursor* result)
{
    if (!ft_canonical_cursor_is_valid(cursor) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (position > ft_canonical_sorted_set_size(&cursor->set)) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    if (result == cursor && position == cursor->position) {
        return FT_STATUS_OK;
    }
    ft_canonical_sorted_set_cursor staged;
    ft_status status = ft_canonical_cursor_stage(&cursor->set, position, &staged);
    if (status == FT_STATUS_OK) {
        ft_canonical_cursor_publish(cursor, &staged, result);
    }
    return status;
}

ft_status ft_canonical_sorted_set_cursor_move_previous(
    const ft_canonical_sorted_set_cursor* cursor,
    ft_canonical_sorted_set_cursor* result)
{
    if (!ft_canonical_cursor_is_valid(cursor) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (cursor->position == 0) {
        return ft_canonical_sorted_set_empty(&cursor->set) ? FT_STATUS_EMPTY : FT_STATUS_OUT_OF_RANGE;
    }
    return ft_canonical_sorted_set_cursor_seek_rank(cursor, cursor->position - 1u, result);
}

ft_status ft_canonical_sorted_set_cursor_move_next(
    const ft_canonical_sorted_set_cursor* cursor,
    ft_canonical_sorted_set_cursor* result)
{
    if (!ft_canonical_cursor_is_valid(cursor) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    const size_t size = ft_canonical_sorted_set_size(&cursor->set);
    if (cursor->position == size) {
        return size == 0 ? FT_STATUS_EMPTY : FT_STATUS_OUT_OF_RANGE;
    }
    return ft_canonical_sorted_set_cursor_seek_rank(cursor, cursor->position + 1u, result);
}

ft_status ft_canonical_sorted_set_cursor_add(
    const ft_canonical_sorted_set_cursor* cursor,
    const void* value,
    ft_canonical_sorted_set_cursor* result)
{
    if (!ft_canonical_cursor_is_valid(cursor) || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    size_t position = 0;
    bool found = false;
    ft_status status = ft_canonical_cursor_bound(&cursor->set, value, false, &position, &found);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (position == SIZE_MAX) {
        return FT_STATUS_OVERFLOW;
    }
    ft_canonical_sorted_set edited;
    status = ft_canonical_sorted_set_add(&cursor->set, value, &edited);
    return status == FT_STATUS_OK
        ? ft_canonical_cursor_publish_set(cursor, &edited, position + 1u, result)
        : status;
}

static ft_status ft_canonical_cursor_delete_at(
    const ft_canonical_sorted_set_cursor* cursor,
    size_t rank,
    size_t position,
    ft_canonical_sorted_set_cursor* result)
{
    const void* value = ft_canonical_cursor_item_at(&cursor->set, rank);
    if (value == NULL) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    ft_canonical_sorted_set edited;
    ft_status status = ft_canonical_sorted_set_remove(&cursor->set, value, &edited);
    return status == FT_STATUS_OK
        ? ft_canonical_cursor_publish_set(cursor, &edited, position, result)
        : status;
}

ft_status ft_canonical_sorted_set_cursor_delete_previous(
    const ft_canonical_sorted_set_cursor* cursor,
    ft_canonical_sorted_set_cursor* result)
{
    if (!ft_canonical_cursor_is_valid(cursor) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (cursor->position == 0) {
        return ft_canonical_sorted_set_empty(&cursor->set) ? FT_STATUS_EMPTY : FT_STATUS_OUT_OF_RANGE;
    }
    return ft_canonical_cursor_delete_at(
        cursor,
        cursor->position - 1u,
        cursor->position - 1u,
        result);
}

ft_status ft_canonical_sorted_set_cursor_delete_next(
    const ft_canonical_sorted_set_cursor* cursor,
    ft_canonical_sorted_set_cursor* result)
{
    if (!ft_canonical_cursor_is_valid(cursor) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    const size_t size = ft_canonical_sorted_set_size(&cursor->set);
    if (cursor->position == size) {
        return size == 0 ? FT_STATUS_EMPTY : FT_STATUS_OUT_OF_RANGE;
    }
    return ft_canonical_cursor_delete_at(cursor, cursor->position, cursor->position, result);
}

ft_status ft_canonical_sorted_set_cursor_snapshot(
    const ft_canonical_sorted_set_cursor* cursor,
    ft_canonical_sorted_set* result)
{
    return !ft_canonical_cursor_is_valid(cursor) || result == NULL
        ? FT_STATUS_INVALID_ARGUMENT
        : ft_canonical_sorted_set_copy(&cursor->set, result);
}

#ifdef _WIN32
static ft_status ft_canonical_random(
    const ft_canonical_policy_config* config,
    unsigned char* destination,
    size_t size)
{
    (void)config;
    if (size > ULONG_MAX) {
        return FT_STATUS_OVERFLOW;
    }
    return BCryptGenRandom(
        NULL,
        destination,
        (ULONG)size,
        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0
        ? FT_STATUS_CRYPTO_FAILURE
        : FT_STATUS_OK;
}
#else
static ft_status ft_canonical_bcrypt_digest(
    const ft_canonical_policy_config* config,
    bool hmac,
    const unsigned char* key,
    size_t key_size,
    const unsigned char* message,
    size_t message_size,
    unsigned char digest[FT_CANONICAL_DIGEST_BYTES])
{
    unsigned int written = 0;
    (void)config;
    if (hmac) {
        if (key_size > (size_t)INT_MAX) {
            return FT_STATUS_OVERFLOW;
        }
        if (HMAC(
                EVP_sha256(),
                key,
                (int)key_size,
                message,
                message_size,
                digest,
                &written) == NULL || written != FT_CANONICAL_DIGEST_BYTES) {
            return FT_STATUS_CRYPTO_FAILURE;
        }
    } else if (EVP_Digest(
            message,
            message_size,
            digest,
            &written,
            EVP_sha256(),
            NULL) != 1 || written != FT_CANONICAL_DIGEST_BYTES) {
        return FT_STATUS_CRYPTO_FAILURE;
    }
    return FT_STATUS_OK;
}

static ft_status ft_canonical_random(
    const ft_canonical_policy_config* config,
    unsigned char* destination,
    size_t size)
{
    (void)config;
    if (size > (size_t)INT_MAX) {
        return FT_STATUS_OVERFLOW;
    }
    return RAND_bytes(destination, (int)size) == 1 ? FT_STATUS_OK : FT_STATUS_CRYPTO_FAILURE;
}
#endif

static ft_status ft_canonical_sha256(
    const ft_canonical_policy_config* config,
    const unsigned char* message,
    size_t message_size,
    unsigned char digest[FT_CANONICAL_DIGEST_BYTES])
{
    return ft_canonical_bcrypt_digest(
        config,
        false,
        NULL,
        0,
        message,
        message_size,
        digest);
}

static ft_status ft_canonical_hmac_sha256(
    const ft_canonical_policy_rep* policy,
    const unsigned char* message,
    size_t message_size,
    unsigned char digest[FT_CANONICAL_DIGEST_BYTES])
{
    return ft_canonical_bcrypt_digest(
        &policy->config,
        true,
        policy->rank_key,
        policy->rank_key_size,
        message,
        message_size,
        digest);
}

static void ft_canonical_policy_retain(ft_canonical_policy_rep* policy)
{
    if (policy != NULL) {
        ft_canonical_ref_retain(&policy->refs);
    }
}

static void ft_canonical_policy_release(ft_canonical_policy_rep* policy)
{
    size_t index = 0;
    if (policy == NULL || !ft_canonical_ref_release(&policy->refs)) {
        return;
    }
    for (index = 0; index != policy->rank_key_size; ++index) {
        ((volatile unsigned char*)policy->rank_key)[index] = 0;
    }
    ft_canonical_deallocate(policy, policy->rank_key);
    ft_canonical_deallocate_config(&policy->config, policy);
}

static ft_status ft_canonical_policy_create_with_key(
    ft_canonical_policy* policy,
    const ft_canonical_policy_config* config,
    const unsigned char* rank_key,
    size_t rank_key_size,
    bool has_seed,
    uint64_t seed)
{
    ft_canonical_policy_rep* rep = NULL;
    unsigned char* key_copy = NULL;
    if (policy == NULL || !ft_canonical_config_valid(config) || rank_key == NULL ||
        rank_key_size < FT_CANONICAL_MINIMUM_KEY_BYTES || rank_key_size > (size_t)INT_MAX) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    rep = (ft_canonical_policy_rep*)ft_canonical_allocate_config(config, sizeof(*rep));
    if (rep == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    key_copy = (unsigned char*)ft_canonical_allocate_config(config, rank_key_size);
    if (key_copy == NULL) {
        ft_canonical_deallocate_config(config, rep);
        return FT_STATUS_NO_MEMORY;
    }
    (void)memcpy(key_copy, rank_key, rank_key_size);
    (void)memset(rep, 0, sizeof(*rep));
    ft_canonical_ref_init(&rep->refs);
    rep->config = *config;
    rep->rank_key = key_copy;
    rep->rank_key_size = rank_key_size;
    rep->has_public_seed = has_seed;
    rep->public_seed = seed;
    policy->rep = rep;
    return FT_STATUS_OK;
}

void ft_canonical_policy_config_init(
    ft_canonical_policy_config* config,
    size_t value_size,
    const void* value_type_identity,
    ft_canonical_compare_fn compare,
    ft_canonical_rank_hash_fn rank_hash,
    void* callback_context)
{
    if (config == NULL) {
        return;
    }
    (void)memset(config, 0, sizeof(*config));
    config->value_size = value_size;
    config->value_type_identity = value_type_identity;
    config->compare = compare;
    config->rank_hash = rank_hash;
    config->callback_context = callback_context;
    config->allocator.allocate = ft_canonical_default_allocate;
    config->allocator.deallocate = ft_canonical_default_deallocate;
}

ft_status ft_canonical_policy_create_random(
    ft_canonical_policy* policy,
    const ft_canonical_policy_config* config)
{
    unsigned char key[FT_CANONICAL_MINIMUM_KEY_BYTES];
    ft_status status = FT_STATUS_OK;
    if (policy == NULL || !ft_canonical_config_valid(config)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_canonical_random(config, key, sizeof(key));
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_canonical_policy_create_with_key(policy, config, key, sizeof(key), false, 0);
    (void)memset(key, 0, sizeof(key));
    return status;
}

ft_status ft_canonical_policy_create_seeded(
    ft_canonical_policy* policy,
    const ft_canonical_policy_config* config,
    uint64_t seed)
{
    unsigned char material[12] = {'Z', 'Z', 'T', '2'};
    unsigned char key[FT_CANONICAL_DIGEST_BYTES];
    ft_status status = FT_STATUS_OK;
    if (policy == NULL || !ft_canonical_config_valid(config)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_canonical_write_be64(seed, material + 4);
    status = ft_canonical_sha256(config, material, sizeof(material), key);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_canonical_policy_create_with_key(policy, config, key, sizeof(key), true, seed);
    (void)memset(key, 0, sizeof(key));
    return status;
}

ft_status ft_canonical_policy_create_keyed(
    ft_canonical_policy* policy,
    const ft_canonical_policy_config* config,
    const unsigned char* rank_key,
    size_t rank_key_size)
{
    if (rank_key_size < FT_CANONICAL_MINIMUM_KEY_BYTES) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return ft_canonical_policy_create_with_key(
        policy,
        config,
        rank_key,
        rank_key_size,
        false,
        0);
}

ft_status ft_canonical_policy_copy(
    const ft_canonical_policy* source,
    ft_canonical_policy* destination)
{
    if (source == NULL || source->rep == NULL || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return FT_STATUS_OK;
    }
    ft_canonical_policy_retain(source->rep);
    destination->rep = source->rep;
    return FT_STATUS_OK;
}

void ft_canonical_policy_move(ft_canonical_policy* destination, ft_canonical_policy* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    destination->rep = source->rep;
    source->rep = NULL;
}

void ft_canonical_policy_dispose(ft_canonical_policy* policy)
{
    if (policy == NULL) {
        return;
    }
    ft_canonical_policy_release(policy->rep);
    policy->rep = NULL;
}

bool ft_canonical_policy_same(
    const ft_canonical_policy* left,
    const ft_canonical_policy* right)
{
    return left != NULL && right != NULL && left->rep != NULL && left->rep == right->rep;
}

bool ft_canonical_policy_has_public_seed(const ft_canonical_policy* policy)
{
    return policy != NULL && policy->rep != NULL && policy->rep->has_public_seed;
}

ft_status ft_canonical_policy_public_seed(
    const ft_canonical_policy* policy,
    uint64_t* seed)
{
    if (policy == NULL || policy->rep == NULL || seed == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (!policy->rep->has_public_seed) {
        return FT_STATUS_NOT_FOUND;
    }
    *seed = policy->rep->public_seed;
    return FT_STATUS_OK;
}

static ft_status ft_canonical_rank_for_rep(
    const ft_canonical_policy_rep* policy,
    const void* value,
    ft_zip_tree_rank* rank)
{
    uint64_t rank_hash = 0;
    unsigned char source[sizeof(rank_hash)];
    unsigned char digest[FT_CANONICAL_DIGEST_BYTES];
    ft_status status = FT_STATUS_OK;
    if (policy == NULL || value == NULL || rank == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = policy->config.rank_hash(value, &rank_hash, policy->config.callback_context);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_canonical_write_be64(rank_hash, source);
    status = ft_canonical_hmac_sha256(policy, source, sizeof(source), digest);
    if (status != FT_STATUS_OK) {
        return status;
    }
    rank->geometric = ft_canonical_leading_zero_count(ft_canonical_read_be64(digest));
    rank->secondary = ft_canonical_read_be64(digest + 8);
    rank->content = ft_canonical_read_be64(digest + 16);
    return FT_STATUS_OK;
}

ft_status ft_canonical_policy_rank_for(
    const ft_canonical_policy* policy,
    const void* value,
    ft_zip_tree_rank* rank)
{
    return policy == NULL ? FT_STATUS_INVALID_ARGUMENT :
        ft_canonical_rank_for_rep(policy->rep, value, rank);
}

static bool ft_canonical_rank_equal(ft_zip_tree_rank left, ft_zip_tree_rank right)
{
    return left.geometric == right.geometric && left.secondary == right.secondary &&
        left.content == right.content;
}

static ft_status ft_canonical_compare(
    const ft_canonical_policy_rep* policy,
    const void* left,
    const void* right,
    int* comparison)
{
    int value = 0;
    ft_status status = FT_STATUS_OK;
    if (policy == NULL || left == NULL || right == NULL || comparison == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = policy->config.compare(left, right, &value, policy->config.callback_context);
    if (status == FT_STATUS_OK) {
        *comparison = value;
    }
    return status;
}

static ft_status ft_canonical_value_create(
    ft_canonical_policy_rep* policy,
    const void* source,
    ft_canonical_value** result)
{
    ft_canonical_value* value = NULL;
    void* bytes = NULL;
    ft_status status = FT_STATUS_OK;
    if (policy == NULL || source == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    value = (ft_canonical_value*)ft_canonical_allocate(policy, sizeof(*value));
    if (value == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    bytes = ft_canonical_allocate(policy, policy->config.value_size);
    if (bytes == NULL) {
        ft_canonical_deallocate(policy, value);
        return FT_STATUS_NO_MEMORY;
    }
    ft_canonical_ref_init(&value->refs);
    value->bytes = bytes;
    if (policy->config.copy != NULL) {
        status = policy->config.copy(
            value->bytes,
            source,
            policy->config.callback_context);
        if (status != FT_STATUS_OK) {
            ft_canonical_deallocate(policy, value->bytes);
            ft_canonical_deallocate(policy, value);
            return status;
        }
    } else {
        (void)memcpy(value->bytes, source, policy->config.value_size);
    }
    *result = value;
    return FT_STATUS_OK;
}

static void ft_canonical_value_retain(ft_canonical_value* value)
{
    if (value != NULL) {
        ft_canonical_ref_retain(&value->refs);
    }
}

static void ft_canonical_value_release(
    const ft_canonical_policy_rep* policy,
    ft_canonical_value* value)
{
    if (value == NULL || !ft_canonical_ref_release(&value->refs)) {
        return;
    }
    if (policy->config.destroy != NULL) {
        policy->config.destroy(value->bytes, policy->config.callback_context);
    }
    ft_canonical_deallocate(policy, value->bytes);
    ft_canonical_deallocate(policy, value);
}

static void ft_canonical_node_retain(ft_canonical_node* node)
{
    if (node != NULL) {
        ft_canonical_ref_retain(&node->refs);
    }
}

static ft_status ft_canonical_node_create(
    ft_canonical_policy_rep* policy,
    ft_canonical_value* value,
    ft_zip_tree_rank rank,
    ft_canonical_node* left,
    ft_canonical_node* right,
    ft_canonical_node** result)
{
    size_t children = 0;
    size_t count = 0;
    size_t height = 0;
    ft_canonical_node* node = NULL;
    if (policy == NULL || value == NULL || result == NULL ||
        ft_canonical_add_overflows(left == NULL ? 0 : left->count, right == NULL ? 0 : right->count, &children) ||
        ft_canonical_add_overflows(children, 1, &count)) {
        return FT_STATUS_OVERFLOW;
    }
    height = left != NULL && (right == NULL || left->height > right->height)
        ? left->height
        : right == NULL ? 0 : right->height;
    if (height == SIZE_MAX) {
        return FT_STATUS_OVERFLOW;
    }
    node = (ft_canonical_node*)ft_canonical_allocate(policy, sizeof(*node));
    if (node == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    (void)memset(node, 0, sizeof(*node));
    ft_canonical_ref_init(&node->refs);
    node->value = value;
    ft_canonical_value_retain(value);
    node->rank = rank;
    node->left = left;
    ft_canonical_node_retain(left);
    node->right = right;
    ft_canonical_node_retain(right);
    node->count = count;
    node->height = height + 1;
    ft_canonical_atomic_init_digest(&node->digest, &node->digest_ready);
    *result = node;
    return FT_STATUS_OK;
}

static void ft_canonical_node_release(
    const ft_canonical_policy_rep* policy,
    ft_canonical_node* node)
{
    ft_canonical_node* pending = NULL;
    if (node == NULL || !ft_canonical_ref_release(&node->refs)) {
        return;
    }
    node->release_next = NULL;
    pending = node;
    while (pending != NULL) {
        ft_canonical_node* current = pending;
        ft_canonical_node* left = current->left;
        ft_canonical_node* right = current->right;
        pending = current->release_next;
        ft_canonical_value_release(policy, current->value);
        ft_canonical_deallocate(policy, current);
        if (left != NULL && ft_canonical_ref_release(&left->refs)) {
            left->release_next = pending;
            pending = left;
        }
        if (right != NULL && ft_canonical_ref_release(&right->refs)) {
            right->release_next = pending;
            pending = right;
        }
    }
}

static ft_status ft_canonical_higher_values(
    const ft_canonical_policy_rep* policy,
    const void* left_value,
    ft_zip_tree_rank left_rank,
    const void* right_value,
    ft_zip_tree_rank right_rank,
    bool* higher)
{
    int comparison = 0;
    ft_status status = FT_STATUS_OK;
    if (left_rank.geometric != right_rank.geometric) {
        *higher = left_rank.geometric > right_rank.geometric;
        return FT_STATUS_OK;
    }
    if (left_rank.secondary != right_rank.secondary) {
        *higher = left_rank.secondary > right_rank.secondary;
        return FT_STATUS_OK;
    }
    status = ft_canonical_compare(policy, left_value, right_value, &comparison);
    if (status == FT_STATUS_OK) {
        *higher = comparison < 0;
    }
    return status;
}

static ft_status ft_canonical_higher_nodes(
    const ft_canonical_policy_rep* policy,
    const ft_canonical_node* left,
    const ft_canonical_node* right,
    bool* higher)
{
    return ft_canonical_higher_values(
        policy,
        left->value->bytes,
        left->rank,
        right->value->bytes,
        right->rank,
        higher);
}

static bool ft_canonical_set_valid(const ft_canonical_sorted_set* set)
{
    return set != NULL && set->policy != NULL;
}

static ft_status ft_canonical_find_node(
    const ft_canonical_sorted_set* set,
    const void* value,
    ft_canonical_node** result)
{
    ft_canonical_node* node = NULL;
    if (!ft_canonical_set_valid(set) || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    node = set->root;
    while (node != NULL) {
        int comparison = 0;
        ft_status status = ft_canonical_compare(
            set->policy,
            value,
            node->value->bytes,
            &comparison);
        if (status != FT_STATUS_OK) {
            return status;
        }
        if (comparison == 0) {
            *result = node;
            return FT_STATUS_OK;
        }
        node = comparison < 0 ? node->left : node->right;
    }
    *result = NULL;
    return FT_STATUS_OK;
}

static ft_status ft_canonical_set_adopt(
    ft_canonical_policy_rep* policy,
    ft_canonical_node* root,
    ft_canonical_sorted_set* set)
{
    if (policy == NULL || set == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_canonical_policy_retain(policy);
    set->policy = policy;
    set->root = root;
    return FT_STATUS_OK;
}

static void ft_canonical_publish_unary(
    const ft_canonical_sorted_set* source,
    ft_canonical_sorted_set* result,
    ft_canonical_sorted_set produced)
{
    if (result == source) {
        ft_canonical_sorted_set_dispose(result);
    }
    *result = produced;
}

static void ft_canonical_publish_binary(
    const ft_canonical_sorted_set* left,
    const ft_canonical_sorted_set* right,
    ft_canonical_sorted_set* result,
    ft_canonical_sorted_set produced)
{
    if (result == left) {
        ft_canonical_sorted_set_dispose(result);
    }
    if (result == right && right != left) {
        ft_canonical_sorted_set_dispose(result);
    }
    *result = produced;
}

ft_status ft_canonical_sorted_set_init(
    ft_canonical_sorted_set* set,
    const ft_canonical_policy* policy)
{
    if (set == NULL || policy == NULL || policy->rep == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return ft_canonical_set_adopt(policy->rep, NULL, set);
}

ft_status ft_canonical_sorted_set_copy(
    const ft_canonical_sorted_set* source,
    ft_canonical_sorted_set* destination)
{
    if (!ft_canonical_set_valid(source) || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return FT_STATUS_OK;
    }
    ft_canonical_policy_retain(source->policy);
    ft_canonical_node_retain(source->root);
    destination->policy = source->policy;
    destination->root = source->root;
    return FT_STATUS_OK;
}

void ft_canonical_sorted_set_move(
    ft_canonical_sorted_set* destination,
    ft_canonical_sorted_set* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    destination->policy = source->policy;
    destination->root = source->root;
    source->policy = NULL;
    source->root = NULL;
}

void ft_canonical_sorted_set_dispose(ft_canonical_sorted_set* set)
{
    ft_canonical_policy_rep* policy = NULL;
    if (set == NULL) {
        return;
    }
    policy = set->policy;
    if (policy != NULL) {
        ft_canonical_node_release(policy, set->root);
        ft_canonical_policy_release(policy);
    }
    set->policy = NULL;
    set->root = NULL;
}

bool ft_canonical_sorted_set_empty(const ft_canonical_sorted_set* set)
{
    return !ft_canonical_set_valid(set) || set->root == NULL;
}

size_t ft_canonical_sorted_set_size(const ft_canonical_sorted_set* set)
{
    return !ft_canonical_set_valid(set) || set->root == NULL ? 0 : set->root->count;
}

size_t ft_canonical_sorted_set_height(const ft_canonical_sorted_set* set)
{
    return !ft_canonical_set_valid(set) || set->root == NULL ? 0 : set->root->height;
}

ft_status ft_canonical_sorted_set_contains(
    const ft_canonical_sorted_set* set,
    const void* value,
    bool* found)
{
    ft_canonical_node* node = NULL;
    ft_status status = FT_STATUS_OK;
    if (found == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_canonical_find_node(set, value, &node);
    if (status == FT_STATUS_OK) {
        *found = node != NULL;
    }
    return status;
}

ft_status ft_canonical_sorted_set_try_get_ref(
    const ft_canonical_sorted_set* set,
    const void* equal_value,
    bool* found,
    const void** value_ref)
{
    ft_canonical_node* node = NULL;
    ft_status status = FT_STATUS_OK;
    if (found == NULL || value_ref == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_canonical_find_node(set, equal_value, &node);
    if (status == FT_STATUS_OK) {
        *found = node != NULL;
        *value_ref = node == NULL ? NULL : node->value->bytes;
    }
    return status;
}

static ft_status ft_canonical_allocate_path(
    const ft_canonical_sorted_set* set,
    size_t element_size,
    void** result)
{
    size_t capacity = 0;
    size_t bytes = 0;
    if (set->root != NULL && set->root->height == SIZE_MAX) {
        return FT_STATUS_OVERFLOW;
    }
    capacity = (set->root == NULL ? 0 : set->root->height) + 1;
    if (ft_canonical_multiply_overflows(capacity, element_size, &bytes)) {
        return FT_STATUS_OVERFLOW;
    }
    *result = ft_canonical_allocate(set->policy, bytes);
    return *result == NULL ? FT_STATUS_NO_MEMORY : FT_STATUS_OK;
}

/*
 * The removal merge zips the right spine of a node's left child against the left
 * spine of its right child, so its scratch path can reach height(left) +
 * height(right) steps - up to 2 * root->height, not the single root-to-leaf
 * height that ft_canonical_allocate_path sizes for. Size the merge scratch for
 * two spines (2 * (height + 1), matching the digest traversal's sizing) and
 * report the capacity so ft_canonical_merge can bounds-check every push.
 */
static ft_status ft_canonical_allocate_merge_path(
    const ft_canonical_sorted_set* set,
    ft_canonical_merge_step** result,
    size_t* capacity)
{
    size_t height = 0;
    size_t entries = 0;
    size_t bytes = 0;
    if (set->root != NULL && set->root->height == SIZE_MAX) {
        return FT_STATUS_OVERFLOW;
    }
    height = set->root == NULL ? 0 : set->root->height;
    if (ft_canonical_multiply_overflows(height + 1, 2, &entries) ||
        ft_canonical_multiply_overflows(entries, sizeof(**result), &bytes)) {
        return FT_STATUS_OVERFLOW;
    }
    *result = ft_canonical_allocate(set->policy, bytes);
    if (*result == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    *capacity = entries;
    return FT_STATUS_OK;
}

static ft_status ft_canonical_split(
    const ft_canonical_sorted_set* set,
    ft_canonical_node* root,
    const void* value,
    ft_canonical_path_step* path,
    ft_canonical_node** left_result,
    ft_canonical_node** right_result)
{
    size_t path_count = 0;
    ft_canonical_node* cursor = root;
    ft_canonical_node* left = NULL;
    ft_canonical_node* right = NULL;
    ft_status status = FT_STATUS_OK;
    while (cursor != NULL) {
        int comparison = 0;
        status = ft_canonical_compare(set->policy, value, cursor->value->bytes, &comparison);
        if (status != FT_STATUS_OK) {
            goto cleanup;
        }
        path[path_count].node = cursor;
        path[path_count].went_left = comparison < 0;
        ++path_count;
        cursor = comparison < 0 ? cursor->left : cursor->right;
    }
    while (path_count != 0) {
        ft_canonical_path_step step = path[--path_count];
        ft_canonical_node* next = NULL;
        if (step.went_left) {
            status = ft_canonical_node_create(
                set->policy,
                step.node->value,
                step.node->rank,
                right,
                step.node->right,
                &next);
            if (status != FT_STATUS_OK) {
                goto cleanup;
            }
            ft_canonical_node_release(set->policy, right);
            right = next;
        } else {
            status = ft_canonical_node_create(
                set->policy,
                step.node->value,
                step.node->rank,
                step.node->left,
                left,
                &next);
            if (status != FT_STATUS_OK) {
                goto cleanup;
            }
            ft_canonical_node_release(set->policy, left);
            left = next;
        }
    }
    *left_result = left;
    *right_result = right;
    return FT_STATUS_OK;

cleanup:
    ft_canonical_node_release(set->policy, left);
    ft_canonical_node_release(set->policy, right);
    return status;
}

static ft_status ft_canonical_insert_value(
    const ft_canonical_sorted_set* set,
    ft_canonical_value* value,
    ft_zip_tree_rank rank,
    ft_canonical_path_step* insertion_path,
    ft_canonical_path_step* split_path,
    ft_canonical_node** root_result)
{
    size_t path_count = 0;
    ft_canonical_node* cursor = set->root;
    ft_canonical_node* left = NULL;
    ft_canonical_node* right = NULL;
    ft_canonical_node* result = NULL;
    ft_status status = FT_STATUS_OK;
    while (cursor != NULL) {
        bool higher = false;
        int comparison = 0;
        status = ft_canonical_higher_values(
            set->policy,
            value->bytes,
            rank,
            cursor->value->bytes,
            cursor->rank,
            &higher);
        if (status != FT_STATUS_OK || higher) {
            if (status != FT_STATUS_OK) {
                goto cleanup;
            }
            break;
        }
        status = ft_canonical_compare(set->policy, value->bytes, cursor->value->bytes, &comparison);
        if (status != FT_STATUS_OK) {
            goto cleanup;
        }
        insertion_path[path_count].node = cursor;
        insertion_path[path_count].went_left = comparison < 0;
        ++path_count;
        cursor = comparison < 0 ? cursor->left : cursor->right;
    }
    status = ft_canonical_split(set, cursor, value->bytes, split_path, &left, &right);
    if (status != FT_STATUS_OK) {
        goto cleanup;
    }
    status = ft_canonical_node_create(set->policy, value, rank, left, right, &result);
    if (status != FT_STATUS_OK) {
        goto cleanup;
    }
    ft_canonical_node_release(set->policy, left);
    left = NULL;
    ft_canonical_node_release(set->policy, right);
    right = NULL;
    while (path_count != 0) {
        ft_canonical_path_step step = insertion_path[--path_count];
        ft_canonical_node* next = NULL;
        status = step.went_left
            ? ft_canonical_node_create(
                set->policy,
                step.node->value,
                step.node->rank,
                result,
                step.node->right,
                &next)
            : ft_canonical_node_create(
                set->policy,
                step.node->value,
                step.node->rank,
                step.node->left,
                result,
                &next);
        if (status != FT_STATUS_OK) {
            goto cleanup;
        }
        ft_canonical_node_release(set->policy, result);
        result = next;
    }
    *root_result = result;
    return FT_STATUS_OK;

cleanup:
    ft_canonical_node_release(set->policy, left);
    ft_canonical_node_release(set->policy, right);
    ft_canonical_node_release(set->policy, result);
    return status;
}

ft_status ft_canonical_sorted_set_add(
    const ft_canonical_sorted_set* set,
    const void* value,
    ft_canonical_sorted_set* result)
{
    ft_canonical_node* existing = NULL;
    ft_canonical_value* owned_value = NULL;
    ft_canonical_node* root = NULL;
    ft_canonical_path_step* insertion_path = NULL;
    ft_canonical_path_step* split_path = NULL;
    ft_zip_tree_rank rank;
    ft_canonical_sorted_set produced;
    ft_status status = FT_STATUS_OK;
    if (!ft_canonical_set_valid(set) || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_canonical_find_node(set, value, &existing);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_canonical_rank_for_rep(set->policy, value, &rank);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (existing != NULL) {
        if (!ft_canonical_rank_equal(existing->rank, rank)) {
            return FT_STATUS_INCONSISTENT_POLICY;
        }
        status = ft_canonical_sorted_set_copy(set, &produced);
        if (status == FT_STATUS_OK) {
            ft_canonical_publish_unary(set, result, produced);
        }
        return status;
    }
    status = ft_canonical_allocate_path(set, sizeof(*insertion_path), (void**)&insertion_path);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_canonical_allocate_path(set, sizeof(*split_path), (void**)&split_path);
    if (status != FT_STATUS_OK) {
        ft_canonical_deallocate(set->policy, insertion_path);
        return status;
    }
    status = ft_canonical_value_create(set->policy, value, &owned_value);
    if (status == FT_STATUS_OK) {
        status = ft_canonical_insert_value(
            set,
            owned_value,
            rank,
            insertion_path,
            split_path,
            &root);
    }
    ft_canonical_value_release(set->policy, owned_value);
    ft_canonical_deallocate(set->policy, split_path);
    ft_canonical_deallocate(set->policy, insertion_path);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_canonical_set_adopt(set->policy, root, &produced);
    if (status != FT_STATUS_OK) {
        ft_canonical_node_release(set->policy, root);
        return status;
    }
    ft_canonical_publish_unary(set, result, produced);
    return FT_STATUS_OK;
}

static ft_status ft_canonical_merge(
    const ft_canonical_sorted_set* set,
    ft_canonical_node* initial_left,
    ft_canonical_node* initial_right,
    ft_canonical_merge_step* path,
    size_t path_capacity,
    ft_canonical_node** result_root)
{
    ft_canonical_node* left = initial_left;
    ft_canonical_node* right = initial_right;
    ft_canonical_node* result = NULL;
    size_t path_count = 0;
    ft_status status = FT_STATUS_OK;
    while (left != NULL && right != NULL) {
        bool higher = false;
        if (path_count >= path_capacity) {
            return FT_STATUS_OVERFLOW;
        }
        status = ft_canonical_higher_nodes(set->policy, left, right, &higher);
        if (status != FT_STATUS_OK) {
            return status;
        }
        path[path_count].node = higher ? left : right;
        path[path_count].chose_left = higher;
        ++path_count;
        if (higher) {
            left = left->right;
        } else {
            right = right->left;
        }
    }
    result = left != NULL ? left : right;
    ft_canonical_node_retain(result);
    while (path_count != 0) {
        ft_canonical_merge_step step = path[--path_count];
        ft_canonical_node* next = NULL;
        status = step.chose_left
            ? ft_canonical_node_create(
                set->policy,
                step.node->value,
                step.node->rank,
                step.node->left,
                result,
                &next)
            : ft_canonical_node_create(
                set->policy,
                step.node->value,
                step.node->rank,
                result,
                step.node->right,
                &next);
        if (status != FT_STATUS_OK) {
            ft_canonical_node_release(set->policy, result);
            return status;
        }
        ft_canonical_node_release(set->policy, result);
        result = next;
    }
    *result_root = result;
    return FT_STATUS_OK;
}

ft_status ft_canonical_sorted_set_remove(
    const ft_canonical_sorted_set* set,
    const void* value,
    ft_canonical_sorted_set* result)
{
    ft_canonical_path_step* search_path = NULL;
    ft_canonical_merge_step* merge_path = NULL;
    size_t merge_capacity = 0;
    ft_canonical_node* cursor = NULL;
    ft_canonical_node* root = NULL;
    size_t path_count = 0;
    ft_canonical_sorted_set produced;
    ft_status status = FT_STATUS_OK;
    if (!ft_canonical_set_valid(set) || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_canonical_allocate_path(set, sizeof(*search_path), (void**)&search_path);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_canonical_allocate_merge_path(set, &merge_path, &merge_capacity);
    if (status != FT_STATUS_OK) {
        ft_canonical_deallocate(set->policy, search_path);
        return status;
    }
    cursor = set->root;
    while (cursor != NULL) {
        int comparison = 0;
        status = ft_canonical_compare(set->policy, value, cursor->value->bytes, &comparison);
        if (status != FT_STATUS_OK) {
            goto cleanup;
        }
        if (comparison == 0) {
            break;
        }
        search_path[path_count].node = cursor;
        search_path[path_count].went_left = comparison < 0;
        ++path_count;
        cursor = comparison < 0 ? cursor->left : cursor->right;
    }
    if (cursor == NULL) {
        status = ft_canonical_sorted_set_copy(set, &produced);
        if (status == FT_STATUS_OK) {
            ft_canonical_publish_unary(set, result, produced);
        }
        goto cleanup;
    }
    status = ft_canonical_merge(set, cursor->left, cursor->right, merge_path, merge_capacity, &root);
    if (status != FT_STATUS_OK) {
        goto cleanup;
    }
    while (path_count != 0) {
        ft_canonical_path_step step = search_path[--path_count];
        ft_canonical_node* next = NULL;
        status = step.went_left
            ? ft_canonical_node_create(
                set->policy,
                step.node->value,
                step.node->rank,
                root,
                step.node->right,
                &next)
            : ft_canonical_node_create(
                set->policy,
                step.node->value,
                step.node->rank,
                step.node->left,
                root,
                &next);
        if (status != FT_STATUS_OK) {
            goto cleanup;
        }
        ft_canonical_node_release(set->policy, root);
        root = next;
    }
    status = ft_canonical_set_adopt(set->policy, root, &produced);
    if (status != FT_STATUS_OK) {
        goto cleanup;
    }
    root = NULL;
    ft_canonical_publish_unary(set, result, produced);

cleanup:
    ft_canonical_node_release(set->policy, root);
    ft_canonical_deallocate(set->policy, merge_path);
    ft_canonical_deallocate(set->policy, search_path);
    return status;
}

ft_status ft_canonical_sorted_set_clear(
    const ft_canonical_sorted_set* set,
    ft_canonical_sorted_set* result)
{
    ft_canonical_sorted_set produced;
    ft_status status = FT_STATUS_OK;
    if (!ft_canonical_set_valid(set) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (set->root == NULL) {
        status = ft_canonical_sorted_set_copy(set, &produced);
    } else {
        status = ft_canonical_set_adopt(set->policy, NULL, &produced);
    }
    if (status == FT_STATUS_OK) {
        ft_canonical_publish_unary(set, result, produced);
    }
    return status;
}

typedef struct ft_canonical_bulk_item {
    const unsigned char* source;
    size_t sequence;
} ft_canonical_bulk_item;

typedef struct ft_canonical_bulk_frame {
    ft_canonical_node* node;
    bool expanded;
} ft_canonical_bulk_frame;

static ft_status ft_canonical_bulk_sort(
    ft_canonical_policy_rep* policy,
    ft_canonical_bulk_item* items,
    ft_canonical_bulk_item* scratch,
    size_t count)
{
    size_t width = 1;
    ft_canonical_bulk_item* source = items;
    ft_canonical_bulk_item* destination = scratch;
    bool swapped = false;
    while (width < count) {
        size_t start = 0;
        while (start < count) {
            size_t middle = start + (width < count - start ? width : count - start);
            size_t end = middle + (width < count - middle ? width : count - middle);
            size_t left = start;
            size_t right = middle;
            size_t output = start;
            while (left < middle || right < end) {
                bool take_left = right == end;
                if (left < middle && right < end) {
                    int comparison = 0;
                    ft_status status = ft_canonical_compare(
                        policy,
                        source[left].source,
                        source[right].source,
                        &comparison);
                    if (status != FT_STATUS_OK) {
                        return status;
                    }
                    take_left = comparison < 0 ||
                        (comparison == 0 && source[left].sequence < source[right].sequence);
                }
                destination[output++] = take_left ? source[left++] : source[right++];
            }
            start = end;
        }
        {
            ft_canonical_bulk_item* temporary = source;
            source = destination;
            destination = temporary;
        }
        swapped = !swapped;
        if (width > count / 2) {
            width = count;
        } else {
            width *= 2;
        }
    }
    if (swapped) {
        (void)memcpy(items, source, count * sizeof(*items));
    }
    return FT_STATUS_OK;
}

static void ft_canonical_bulk_nodes_destroy_flat(
    ft_canonical_policy_rep* policy,
    ft_canonical_node** nodes,
    size_t count)
{
    size_t index = 0;
    for (index = 0; index != count; ++index) {
        if (nodes[index] != NULL) {
            ft_canonical_value_release(policy, nodes[index]->value);
            ft_canonical_deallocate(policy, nodes[index]);
            nodes[index] = NULL;
        }
    }
}

ft_status ft_canonical_sorted_set_from_array(
    ft_canonical_sorted_set* set,
    const ft_canonical_policy* policy,
    const void* values,
    size_t count)
{
    ft_canonical_bulk_item* items = NULL;
    ft_canonical_bulk_item* scratch = NULL;
    ft_canonical_node** nodes = NULL;
    ft_canonical_node** spine = NULL;
    ft_canonical_bulk_frame* frames = NULL;
    ft_canonical_node* root = NULL;
    size_t item_bytes = 0;
    size_t pointer_bytes = 0;
    size_t frame_count = 0;
    size_t frame_bytes = 0;
    size_t unique_count = 0;
    size_t index = 0;
    ft_status status = FT_STATUS_OK;
    if (set == NULL || policy == NULL || policy->rep == NULL ||
        (count != 0 && values == NULL) ||
        ft_canonical_multiply_overflows(count, policy->rep->config.value_size, &item_bytes)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    (void)item_bytes;
    if (count == 0) {
        return ft_canonical_sorted_set_init(set, policy);
    }
    if (ft_canonical_multiply_overflows(count, sizeof(*items), &item_bytes) ||
        ft_canonical_multiply_overflows(count, sizeof(*nodes), &pointer_bytes) ||
        ft_canonical_multiply_overflows(count, 2, &frame_count) || frame_count == SIZE_MAX ||
        ft_canonical_multiply_overflows(frame_count + 1, sizeof(*frames), &frame_bytes)) {
        return FT_STATUS_OVERFLOW;
    }
    items = (ft_canonical_bulk_item*)ft_canonical_allocate(policy->rep, item_bytes);
    scratch = (ft_canonical_bulk_item*)ft_canonical_allocate(policy->rep, item_bytes);
    nodes = (ft_canonical_node**)ft_canonical_allocate(policy->rep, pointer_bytes);
    spine = (ft_canonical_node**)ft_canonical_allocate(policy->rep, pointer_bytes);
    frames = (ft_canonical_bulk_frame*)ft_canonical_allocate(policy->rep, frame_bytes);
    if (items == NULL || scratch == NULL || nodes == NULL || spine == NULL || frames == NULL) {
        status = FT_STATUS_NO_MEMORY;
        goto cleanup;
    }
    (void)memset(nodes, 0, pointer_bytes);
    for (index = 0; index != count; ++index) {
        items[index].source = (const unsigned char*)values + index * policy->rep->config.value_size;
        items[index].sequence = index;
    }
    status = ft_canonical_bulk_sort(policy->rep, items, scratch, count);
    if (status != FT_STATUS_OK) {
        goto cleanup;
    }
    index = 0;
    while (index < count) {
        size_t end = index + 1;
        ft_zip_tree_rank rank;
        ft_canonical_value* value = NULL;
        status = ft_canonical_rank_for_rep(policy->rep, items[index].source, &rank);
        if (status != FT_STATUS_OK) {
            goto cleanup;
        }
        while (end < count) {
            int comparison = 0;
            status = ft_canonical_compare(
                policy->rep,
                items[index].source,
                items[end].source,
                &comparison);
            if (status != FT_STATUS_OK) {
                goto cleanup;
            }
            if (comparison != 0) {
                break;
            }
            {
                ft_zip_tree_rank duplicate_rank;
                status = ft_canonical_rank_for_rep(
                    policy->rep,
                    items[end].source,
                    &duplicate_rank);
                if (status != FT_STATUS_OK) {
                    goto cleanup;
                }
                if (!ft_canonical_rank_equal(rank, duplicate_rank)) {
                    status = FT_STATUS_INCONSISTENT_POLICY;
                    goto cleanup;
                }
            }
            ++end;
        }
        status = ft_canonical_value_create(policy->rep, items[index].source, &value);
        if (status == FT_STATUS_OK) {
            status = ft_canonical_node_create(
                policy->rep,
                value,
                rank,
                NULL,
                NULL,
                &nodes[unique_count]);
        }
        ft_canonical_value_release(policy->rep, value);
        if (status != FT_STATUS_OK) {
            goto cleanup;
        }
        ++unique_count;
        index = end;
    }
    {
        size_t spine_count = 0;
        for (index = 0; index != unique_count; ++index) {
            ft_canonical_node* node = nodes[index];
            ft_canonical_node* left = NULL;
            while (spine_count != 0) {
                bool higher = false;
                status = ft_canonical_higher_nodes(
                    policy->rep,
                    node,
                    spine[spine_count - 1],
                    &higher);
                if (status != FT_STATUS_OK) {
                    goto cleanup;
                }
                if (!higher) {
                    break;
                }
                left = spine[--spine_count];
            }
            node->left = left;
            if (spine_count != 0) {
                spine[spine_count - 1]->right = node;
            }
            spine[spine_count++] = node;
        }
        root = spine[0];
    }
    {
        size_t pending_count = 0;
        frames[pending_count].node = root;
        frames[pending_count].expanded = false;
        ++pending_count;
        while (pending_count != 0) {
            ft_canonical_bulk_frame frame = frames[--pending_count];
            if (frame.expanded) {
                size_t children = 0;
                size_t left_count = frame.node->left == NULL ? 0 : frame.node->left->count;
                size_t right_count = frame.node->right == NULL ? 0 : frame.node->right->count;
                size_t left_height = frame.node->left == NULL ? 0 : frame.node->left->height;
                size_t right_height = frame.node->right == NULL ? 0 : frame.node->right->height;
                if (ft_canonical_add_overflows(left_count, right_count, &children) || children == SIZE_MAX) {
                    status = FT_STATUS_OVERFLOW;
                    goto cleanup;
                }
                frame.node->count = children + 1;
                frame.node->height = (left_height > right_height ? left_height : right_height) + 1;
                continue;
            }
            frames[pending_count].node = frame.node;
            frames[pending_count].expanded = true;
            ++pending_count;
            if (frame.node->right != NULL) {
                frames[pending_count].node = frame.node->right;
                frames[pending_count].expanded = false;
                ++pending_count;
            }
            if (frame.node->left != NULL) {
                frames[pending_count].node = frame.node->left;
                frames[pending_count].expanded = false;
                ++pending_count;
            }
        }
    }
    status = ft_canonical_set_adopt(policy->rep, root, set);
    if (status == FT_STATUS_OK) {
        root = NULL;
    }

cleanup:
    if (root != NULL || status != FT_STATUS_OK) {
        ft_canonical_bulk_nodes_destroy_flat(policy->rep, nodes, unique_count);
    }
    ft_canonical_deallocate(policy->rep, frames);
    ft_canonical_deallocate(policy->rep, spine);
    ft_canonical_deallocate(policy->rep, nodes);
    ft_canonical_deallocate(policy->rep, scratch);
    ft_canonical_deallocate(policy->rep, items);
    return status;
}

ft_status ft_canonical_sorted_set_visit(
    const ft_canonical_sorted_set* set,
    ft_canonical_visit_fn visitor,
    void* context)
{
    ft_canonical_node** stack = NULL;
    ft_canonical_node* cursor = NULL;
    size_t stack_count = 0;
    ft_status status = FT_STATUS_OK;
    if (!ft_canonical_set_valid(set) || visitor == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (set->root == NULL) {
        return FT_STATUS_OK;
    }
    status = ft_canonical_allocate_path(set, sizeof(*stack), (void**)&stack);
    if (status != FT_STATUS_OK) {
        return status;
    }
    cursor = set->root;
    while (cursor != NULL || stack_count != 0) {
        while (cursor != NULL) {
            stack[stack_count++] = cursor;
            cursor = cursor->left;
        }
        cursor = stack[--stack_count];
        status = visitor(cursor->value->bytes, context);
        if (status != FT_STATUS_OK) {
            break;
        }
        cursor = cursor->right;
    }
    ft_canonical_deallocate(set->policy, stack);
    return status;
}

ft_status ft_canonical_sorted_set_visit_shape(
    const ft_canonical_sorted_set* set,
    ft_canonical_shape_visit_fn visitor,
    void* context)
{
    ft_canonical_node** stack = NULL;
    size_t stack_count = 0;
    ft_status status = FT_STATUS_OK;
    if (!ft_canonical_set_valid(set) || visitor == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (set->root == NULL) {
        return FT_STATUS_OK;
    }
    status = ft_canonical_allocate_path(set, sizeof(*stack), (void**)&stack);
    if (status != FT_STATUS_OK) {
        return status;
    }
    stack[stack_count++] = set->root;
    while (stack_count != 0) {
        ft_canonical_node* node = stack[--stack_count];
        status = visitor(
            node->value->bytes,
            node->left == NULL ? 0 : node->left->count,
            node->right == NULL ? 0 : node->right->count,
            context);
        if (status != FT_STATUS_OK) {
            break;
        }
        if (node->right != NULL) {
            stack[stack_count++] = node->right;
        }
        if (node->left != NULL) {
            stack[stack_count++] = node->left;
        }
    }
    ft_canonical_deallocate(set->policy, stack);
    return status;
}

static ft_status ft_canonical_digest_node(
    const ft_canonical_sorted_set* set,
    ft_canonical_node* root,
    uint64_t* result)
{
    ft_canonical_digest_frame* stack = NULL;
    size_t capacity = 0;
    size_t bytes = 0;
    size_t stack_count = 0;
    ft_status status = FT_STATUS_OK;
    if (ft_canonical_digest_ready(&root->digest_ready)) {
        *result = ft_canonical_digest_load(&root->digest);
        return FT_STATUS_OK;
    }
    if (root->height == SIZE_MAX ||
        ft_canonical_multiply_overflows(root->height + 1, 2, &capacity) ||
        ft_canonical_multiply_overflows(capacity, sizeof(*stack), &bytes)) {
        return FT_STATUS_OVERFLOW;
    }
    stack = (ft_canonical_digest_frame*)ft_canonical_allocate(set->policy, bytes);
    if (stack == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    stack[stack_count].node = root;
    stack[stack_count].expanded = false;
    ++stack_count;
    while (stack_count != 0) {
        ft_canonical_digest_frame frame = stack[--stack_count];
        if (ft_canonical_digest_ready(&frame.node->digest_ready)) {
            continue;
        }
        if (!frame.expanded) {
            if (stack_count + 3 > capacity) {
                status = FT_STATUS_OVERFLOW;
                break;
            }
            stack[stack_count].node = frame.node;
            stack[stack_count].expanded = true;
            ++stack_count;
            if (frame.node->right != NULL &&
                !ft_canonical_digest_ready(&frame.node->right->digest_ready)) {
                stack[stack_count].node = frame.node->right;
                stack[stack_count].expanded = false;
                ++stack_count;
            }
            if (frame.node->left != NULL &&
                !ft_canonical_digest_ready(&frame.node->left->digest_ready)) {
                stack[stack_count].node = frame.node->left;
                stack[stack_count].expanded = false;
                ++stack_count;
            }
        } else {
            uint64_t left_hash = frame.node->left == NULL
                ? UINT64_C(0x243f6a8885a308d3)
                : ft_canonical_digest_load(&frame.node->left->digest);
            uint64_t right_hash = frame.node->right == NULL
                ? UINT64_C(0x13198a2e03707344)
                : ft_canonical_digest_load(&frame.node->right->digest);
            uint64_t digest = ft_canonical_mix(
                frame.node->rank.content ^ ft_canonical_rotate_left(left_hash, 17) ^
                ft_canonical_rotate_left(right_hash, 43));
            ft_canonical_digest_publish(&frame.node->digest, &frame.node->digest_ready, digest);
        }
    }
    if (status == FT_STATUS_OK) {
        *result = ft_canonical_digest_load(&root->digest);
    }
    ft_canonical_deallocate(set->policy, stack);
    return status;
}

ft_status ft_canonical_sorted_set_content_hash(
    const ft_canonical_sorted_set* set,
    uint64_t* content_hash)
{
    if (!ft_canonical_set_valid(set) || content_hash == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (set->root == NULL) {
        *content_hash = 0;
        return FT_STATUS_OK;
    }
    return ft_canonical_digest_node(set, set->root, content_hash);
}

static ft_status ft_canonical_nodes_equal(
    const ft_canonical_sorted_set* set,
    ft_canonical_node* left,
    ft_canonical_node* right,
    bool* equal)
{
    ft_canonical_node_pair* stack = NULL;
    size_t capacity = 0;
    size_t bytes = 0;
    size_t stack_count = 0;
    size_t left_height = left == NULL ? 0 : left->height;
    size_t right_height = right == NULL ? 0 : right->height;
    ft_status status = FT_STATUS_OK;
    capacity = (left_height > right_height ? left_height : right_height) + 1;
    if (ft_canonical_multiply_overflows(capacity, sizeof(*stack), &bytes)) {
        return FT_STATUS_OVERFLOW;
    }
    stack = (ft_canonical_node_pair*)ft_canonical_allocate(set->policy, bytes);
    if (stack == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    stack[stack_count].left = left;
    stack[stack_count].right = right;
    ++stack_count;
    *equal = true;
    while (stack_count != 0) {
        ft_canonical_node_pair pair = stack[--stack_count];
        int comparison = 0;
        if (pair.left == pair.right) {
            continue;
        }
        if (pair.left == NULL || pair.right == NULL) {
            *equal = false;
            break;
        }
        status = ft_canonical_compare(
            set->policy,
            pair.left->value->bytes,
            pair.right->value->bytes,
            &comparison);
        if (status != FT_STATUS_OK) {
            break;
        }
        if (comparison != 0) {
            *equal = false;
            break;
        }
        if (stack_count + 2 > capacity) {
            status = FT_STATUS_OVERFLOW;
            break;
        }
        stack[stack_count].left = pair.left->right;
        stack[stack_count].right = pair.right->right;
        ++stack_count;
        stack[stack_count].left = pair.left->left;
        stack[stack_count].right = pair.right->left;
        ++stack_count;
    }
    ft_canonical_deallocate(set->policy, stack);
    return status;
}

static ft_status ft_canonical_same_policy_equals(
    const ft_canonical_sorted_set* left,
    const ft_canonical_sorted_set* right,
    bool* equal)
{
    uint64_t left_hash = 0;
    uint64_t right_hash = 0;
    ft_status status = FT_STATUS_OK;
    if (left->root == right->root) {
        *equal = true;
        return FT_STATUS_OK;
    }
    if (ft_canonical_sorted_set_size(left) != ft_canonical_sorted_set_size(right)) {
        *equal = false;
        return FT_STATUS_OK;
    }
    status = ft_canonical_sorted_set_content_hash(left, &left_hash);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_canonical_sorted_set_content_hash(right, &right_hash);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (left_hash != right_hash) {
        *equal = false;
        return FT_STATUS_OK;
    }
    return ft_canonical_nodes_equal(left, left->root, right->root, equal);
}

typedef struct ft_canonical_normalize_context {
    ft_canonical_sorted_set working;
} ft_canonical_normalize_context;

static ft_status ft_canonical_normalize_visit(const void* value, void* context)
{
    ft_canonical_normalize_context* normalize = (ft_canonical_normalize_context*)context;
    return ft_canonical_sorted_set_add(&normalize->working, value, &normalize->working);
}

static ft_status ft_canonical_normalize_other(
    const ft_canonical_sorted_set* receiver,
    const ft_canonical_sorted_set* other,
    ft_canonical_sorted_set* result)
{
    ft_canonical_normalize_context context;
    ft_status status = FT_STATUS_OK;
    if (receiver->policy == other->policy) {
        return ft_canonical_sorted_set_copy(other, result);
    }
    if (receiver->policy->config.value_type_identity !=
        other->policy->config.value_type_identity) {
        return FT_STATUS_INCOMPATIBLE_POLICY;
    }
    (void)memset(&context, 0, sizeof(context));
    status = ft_canonical_set_adopt(receiver->policy, NULL, &context.working);
    if (status == FT_STATUS_OK) {
        status = ft_canonical_sorted_set_visit(other, ft_canonical_normalize_visit, &context);
    }
    if (status != FT_STATUS_OK) {
        ft_canonical_sorted_set_dispose(&context.working);
        return status;
    }
    *result = context.working;
    return FT_STATUS_OK;
}

ft_status ft_canonical_sorted_set_equals(
    const ft_canonical_sorted_set* receiver,
    const ft_canonical_sorted_set* other,
    bool* equal)
{
    ft_canonical_sorted_set normalized;
    ft_status status = FT_STATUS_OK;
    bool value = false;
    if (!ft_canonical_set_valid(receiver) || !ft_canonical_set_valid(other) || equal == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (receiver->policy == other->policy) {
        return ft_canonical_same_policy_equals(receiver, other, equal);
    }
    (void)memset(&normalized, 0, sizeof(normalized));
    status = ft_canonical_normalize_other(receiver, other, &normalized);
    if (status == FT_STATUS_OK) {
        status = ft_canonical_same_policy_equals(receiver, &normalized, &value);
    }
    ft_canonical_sorted_set_dispose(&normalized);
    if (status == FT_STATUS_OK) {
        *equal = value;
    }
    return status;
}

typedef struct ft_canonical_union_context {
    ft_canonical_sorted_set working;
} ft_canonical_union_context;

static ft_status ft_canonical_union_visit(const void* value, void* context)
{
    ft_canonical_union_context* operation = (ft_canonical_union_context*)context;
    return ft_canonical_sorted_set_add(&operation->working, value, &operation->working);
}

typedef struct ft_canonical_intersect_context {
    const ft_canonical_sorted_set* other;
    ft_canonical_sorted_set working;
} ft_canonical_intersect_context;

static ft_status ft_canonical_intersect_visit(const void* value, void* context)
{
    ft_canonical_intersect_context* operation = (ft_canonical_intersect_context*)context;
    bool found = false;
    ft_status status = ft_canonical_sorted_set_contains(operation->other, value, &found);
    if (status == FT_STATUS_OK && found) {
        status = ft_canonical_sorted_set_add(&operation->working, value, &operation->working);
    }
    return status;
}

typedef struct ft_canonical_except_context {
    ft_canonical_sorted_set working;
} ft_canonical_except_context;

static ft_status ft_canonical_except_visit(const void* value, void* context)
{
    ft_canonical_except_context* operation = (ft_canonical_except_context*)context;
    return ft_canonical_sorted_set_remove(&operation->working, value, &operation->working);
}

static ft_status ft_canonical_require_compatible(
    const ft_canonical_sorted_set* left,
    const ft_canonical_sorted_set* right)
{
    if (!ft_canonical_set_valid(left) || !ft_canonical_set_valid(right)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return left->policy == right->policy ? FT_STATUS_OK : FT_STATUS_INCOMPATIBLE_POLICY;
}

ft_status ft_canonical_sorted_set_union(
    const ft_canonical_sorted_set* left,
    const ft_canonical_sorted_set* right,
    ft_canonical_sorted_set* result)
{
    ft_canonical_union_context context;
    ft_status status = FT_STATUS_OK;
    if (result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_canonical_require_compatible(left, right);
    if (status != FT_STATUS_OK) {
        return status;
    }
    (void)memset(&context, 0, sizeof(context));
    status = ft_canonical_sorted_set_copy(left, &context.working);
    if (status == FT_STATUS_OK && left->root != right->root) {
        status = ft_canonical_sorted_set_visit(right, ft_canonical_union_visit, &context);
    }
    if (status != FT_STATUS_OK) {
        ft_canonical_sorted_set_dispose(&context.working);
        return status;
    }
    ft_canonical_publish_binary(left, right, result, context.working);
    return FT_STATUS_OK;
}

ft_status ft_canonical_sorted_set_intersect(
    const ft_canonical_sorted_set* left,
    const ft_canonical_sorted_set* right,
    ft_canonical_sorted_set* result)
{
    ft_canonical_intersect_context context;
    ft_status status = FT_STATUS_OK;
    if (result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_canonical_require_compatible(left, right);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (left->root == right->root) {
        ft_canonical_sorted_set produced;
        status = ft_canonical_sorted_set_copy(left, &produced);
        if (status == FT_STATUS_OK) {
            ft_canonical_publish_binary(left, right, result, produced);
        }
        return status;
    }
    (void)memset(&context, 0, sizeof(context));
    context.other = right;
    status = ft_canonical_set_adopt(left->policy, NULL, &context.working);
    if (status == FT_STATUS_OK) {
        status = ft_canonical_sorted_set_visit(left, ft_canonical_intersect_visit, &context);
    }
    if (status != FT_STATUS_OK) {
        ft_canonical_sorted_set_dispose(&context.working);
        return status;
    }
    ft_canonical_publish_binary(left, right, result, context.working);
    return FT_STATUS_OK;
}

ft_status ft_canonical_sorted_set_except(
    const ft_canonical_sorted_set* left,
    const ft_canonical_sorted_set* right,
    ft_canonical_sorted_set* result)
{
    ft_canonical_except_context context;
    ft_status status = FT_STATUS_OK;
    if (result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_canonical_require_compatible(left, right);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (left->root == right->root) {
        ft_canonical_sorted_set produced;
        status = ft_canonical_set_adopt(left->policy, NULL, &produced);
        if (status == FT_STATUS_OK) {
            ft_canonical_publish_binary(left, right, result, produced);
        }
        return status;
    }
    (void)memset(&context, 0, sizeof(context));
    status = ft_canonical_sorted_set_copy(left, &context.working);
    if (status == FT_STATUS_OK) {
        status = ft_canonical_sorted_set_visit(right, ft_canonical_except_visit, &context);
    }
    if (status != FT_STATUS_OK) {
        ft_canonical_sorted_set_dispose(&context.working);
        return status;
    }
    ft_canonical_publish_binary(left, right, result, context.working);
    return FT_STATUS_OK;
}

typedef struct ft_canonical_relation_context {
    const ft_canonical_sorted_set* target;
    bool all_found;
    bool any_found;
} ft_canonical_relation_context;

static ft_status ft_canonical_relation_visit(const void* value, void* context)
{
    ft_canonical_relation_context* relation = (ft_canonical_relation_context*)context;
    bool found = false;
    ft_status status = ft_canonical_sorted_set_contains(relation->target, value, &found);
    if (status == FT_STATUS_OK) {
        relation->all_found = relation->all_found && found;
        relation->any_found = relation->any_found || found;
    }
    return status;
}

static ft_status ft_canonical_relation(
    const ft_canonical_sorted_set* receiver,
    const ft_canonical_sorted_set* other,
    bool receiver_in_other,
    bool require_proper,
    bool overlap,
    bool* result)
{
    ft_canonical_sorted_set normalized;
    ft_canonical_relation_context context;
    const ft_canonical_sorted_set* source = NULL;
    size_t receiver_size = 0;
    size_t other_size = 0;
    bool value = false;
    ft_status status = FT_STATUS_OK;
    if (!ft_canonical_set_valid(receiver) || !ft_canonical_set_valid(other) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    (void)memset(&normalized, 0, sizeof(normalized));
    status = ft_canonical_normalize_other(receiver, other, &normalized);
    if (status != FT_STATUS_OK) {
        return status;
    }
    receiver_size = ft_canonical_sorted_set_size(receiver);
    other_size = ft_canonical_sorted_set_size(&normalized);
    source = receiver_in_other ? receiver : &normalized;
    context.target = receiver_in_other ? &normalized : receiver;
    context.all_found = true;
    context.any_found = false;
    status = ft_canonical_sorted_set_visit(source, ft_canonical_relation_visit, &context);
    if (status == FT_STATUS_OK) {
        if (overlap) {
            value = context.any_found;
        } else if (receiver_in_other) {
            value = context.all_found && (!require_proper || receiver_size < other_size);
        } else {
            value = context.all_found && (!require_proper || receiver_size > other_size);
        }
    }
    ft_canonical_sorted_set_dispose(&normalized);
    if (status == FT_STATUS_OK) {
        *result = value;
    }
    return status;
}

ft_status ft_canonical_sorted_set_is_subset(
    const ft_canonical_sorted_set* receiver,
    const ft_canonical_sorted_set* other,
    bool* result)
{
    return ft_canonical_relation(receiver, other, true, false, false, result);
}

ft_status ft_canonical_sorted_set_is_proper_subset(
    const ft_canonical_sorted_set* receiver,
    const ft_canonical_sorted_set* other,
    bool* result)
{
    return ft_canonical_relation(receiver, other, true, true, false, result);
}

ft_status ft_canonical_sorted_set_is_superset(
    const ft_canonical_sorted_set* receiver,
    const ft_canonical_sorted_set* other,
    bool* result)
{
    return ft_canonical_relation(receiver, other, false, false, false, result);
}

ft_status ft_canonical_sorted_set_is_proper_superset(
    const ft_canonical_sorted_set* receiver,
    const ft_canonical_sorted_set* other,
    bool* result)
{
    return ft_canonical_relation(receiver, other, false, true, false, result);
}

ft_status ft_canonical_sorted_set_overlaps(
    const ft_canonical_sorted_set* receiver,
    const ft_canonical_sorted_set* other,
    bool* result)
{
    return ft_canonical_relation(receiver, other, true, false, true, result);
}

const void* ft_canonical_sorted_set_root_identity(const ft_canonical_sorted_set* set)
{
    return ft_canonical_set_valid(set) ? set->root : NULL;
}

ft_status ft_canonical_sorted_set_node_identity(
    const ft_canonical_sorted_set* set,
    const void* value,
    const void** identity)
{
    ft_canonical_node* node = NULL;
    ft_status status = FT_STATUS_OK;
    if (identity == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_canonical_find_node(set, value, &node);
    if (status == FT_STATUS_OK) {
        *identity = node;
    }
    return status;
}

ft_status ft_canonical_sorted_set_shared_node_count(
    const ft_canonical_sorted_set* left,
    const ft_canonical_sorted_set* right,
    size_t* shared_count)
{
    ft_canonical_node** stack = NULL;
    ft_canonical_node* cursor = NULL;
    size_t stack_count = 0;
    size_t count = 0;
    ft_status status = FT_STATUS_OK;
    if (shared_count == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_canonical_require_compatible(left, right);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (left->root == NULL) {
        *shared_count = 0;
        return FT_STATUS_OK;
    }
    status = ft_canonical_allocate_path(left, sizeof(*stack), (void**)&stack);
    if (status != FT_STATUS_OK) {
        return status;
    }
    cursor = left->root;
    while (cursor != NULL || stack_count != 0) {
        ft_canonical_node* other_node = NULL;
        while (cursor != NULL) {
            stack[stack_count++] = cursor;
            cursor = cursor->left;
        }
        cursor = stack[--stack_count];
        status = ft_canonical_find_node(right, cursor->value->bytes, &other_node);
        if (status != FT_STATUS_OK) {
            break;
        }
        if (cursor == other_node) {
            ++count;
        }
        cursor = cursor->right;
    }
    ft_canonical_deallocate(left->policy, stack);
    if (status == FT_STATUS_OK) {
        *shared_count = count;
    }
    return status;
}

typedef struct ft_canonical_validation_frame {
    ft_canonical_node* node;
    const void* lower;
    const void* upper;
    size_t depth;
} ft_canonical_validation_frame;

typedef struct ft_canonical_pointer_slot {
    ft_canonical_node* node;
} ft_canonical_pointer_slot;

typedef struct ft_canonical_priority_slot {
    bool occupied;
    unsigned geometric;
    uint64_t secondary;
} ft_canonical_priority_slot;

static ft_status ft_canonical_hash_capacity(size_t count, size_t* result)
{
    size_t desired = 0;
    size_t capacity = 8;
    if (ft_canonical_multiply_overflows(count, 2, &desired) || desired == SIZE_MAX) {
        return FT_STATUS_OVERFLOW;
    }
    ++desired;
    while (capacity < desired) {
        if (capacity > SIZE_MAX / 2) {
            return FT_STATUS_OVERFLOW;
        }
        capacity *= 2;
    }
    *result = capacity;
    return FT_STATUS_OK;
}

static bool ft_canonical_pointer_insert(
    ft_canonical_pointer_slot* slots,
    size_t capacity,
    ft_canonical_node* node)
{
    size_t index = (size_t)(ft_canonical_mix((uint64_t)(uintptr_t)node) & (capacity - 1));
    while (slots[index].node != NULL) {
        if (slots[index].node == node) {
            return false;
        }
        index = (index + 1) & (capacity - 1);
    }
    slots[index].node = node;
    return true;
}

static bool ft_canonical_priority_insert(
    ft_canonical_priority_slot* slots,
    size_t capacity,
    ft_zip_tree_rank rank)
{
    uint64_t hash = ft_canonical_mix(rank.secondary ^ ((uint64_t)rank.geometric << 32));
    size_t index = (size_t)(hash & (capacity - 1));
    while (slots[index].occupied) {
        if (slots[index].geometric == rank.geometric && slots[index].secondary == rank.secondary) {
            return false;
        }
        index = (index + 1) & (capacity - 1);
    }
    slots[index].occupied = true;
    slots[index].geometric = rank.geometric;
    slots[index].secondary = rank.secondary;
    return true;
}

ft_status ft_canonical_sorted_set_validate(
    const ft_canonical_sorted_set* set,
    bool* valid,
    ft_canonical_sorted_set_statistics* statistics)
{
    ft_canonical_validation_frame* stack = NULL;
    ft_canonical_pointer_slot* pointers = NULL;
    ft_canonical_priority_slot* priorities = NULL;
    size_t stack_capacity = 0;
    size_t hash_capacity = 0;
    size_t stack_bytes = 0;
    size_t pointer_bytes = 0;
    size_t priority_bytes = 0;
    size_t stack_count = 0;
    bool local_valid = true;
    ft_canonical_sorted_set_statistics local_statistics;
    ft_status status = FT_STATUS_OK;
    if (!ft_canonical_set_valid(set) || valid == NULL || statistics == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    (void)memset(&local_statistics, 0, sizeof(local_statistics));
    if (set->root == NULL) {
        *valid = true;
        *statistics = local_statistics;
        return FT_STATUS_OK;
    }
    stack_capacity = set->root->count;
    status = ft_canonical_hash_capacity(stack_capacity, &hash_capacity);
    if (status != FT_STATUS_OK ||
        ft_canonical_multiply_overflows(stack_capacity, sizeof(*stack), &stack_bytes) ||
        ft_canonical_multiply_overflows(hash_capacity, sizeof(*pointers), &pointer_bytes) ||
        ft_canonical_multiply_overflows(hash_capacity, sizeof(*priorities), &priority_bytes)) {
        return status == FT_STATUS_OK ? FT_STATUS_OVERFLOW : status;
    }
    stack = (ft_canonical_validation_frame*)ft_canonical_allocate(set->policy, stack_bytes);
    pointers = (ft_canonical_pointer_slot*)ft_canonical_allocate(set->policy, pointer_bytes);
    priorities = (ft_canonical_priority_slot*)ft_canonical_allocate(set->policy, priority_bytes);
    if (stack == NULL || pointers == NULL || priorities == NULL) {
        status = FT_STATUS_NO_MEMORY;
        goto cleanup;
    }
    (void)memset(pointers, 0, pointer_bytes);
    (void)memset(priorities, 0, priority_bytes);
    stack[stack_count].node = set->root;
    stack[stack_count].lower = NULL;
    stack[stack_count].upper = NULL;
    stack[stack_count].depth = 1;
    ++stack_count;
    while (stack_count != 0 && local_valid) {
        ft_canonical_validation_frame frame = stack[--stack_count];
        ft_canonical_node* node = frame.node;
        ft_zip_tree_rank reproduced;
        size_t child_count = 0;
        size_t expected_count = 0;
        size_t left_height = node->left == NULL ? 0 : node->left->height;
        size_t right_height = node->right == NULL ? 0 : node->right->height;
        size_t maximum_child_height = left_height > right_height ? left_height : right_height;
        size_t expected_height = 0;
        int comparison = 0;
        if (maximum_child_height == SIZE_MAX) {
            local_valid = false;
            break;
        }
        expected_height = maximum_child_height + 1;
        if (!ft_canonical_pointer_insert(pointers, hash_capacity, node)) {
            local_valid = false;
            break;
        }
        if (frame.lower != NULL) {
            status = ft_canonical_compare(set->policy, node->value->bytes, frame.lower, &comparison);
            if (status != FT_STATUS_OK) {
                goto cleanup;
            }
            if (comparison <= 0) {
                local_valid = false;
                break;
            }
        }
        if (frame.upper != NULL) {
            status = ft_canonical_compare(set->policy, node->value->bytes, frame.upper, &comparison);
            if (status != FT_STATUS_OK) {
                goto cleanup;
            }
            if (comparison >= 0) {
                local_valid = false;
                break;
            }
        }
        status = ft_canonical_rank_for_rep(set->policy, node->value->bytes, &reproduced);
        if (status != FT_STATUS_OK) {
            goto cleanup;
        }
        if (!ft_canonical_rank_equal(reproduced, node->rank)) {
            local_valid = false;
            break;
        }
        if (node->left != NULL) {
            bool parent_higher = false;
            status = ft_canonical_higher_nodes(set->policy, node, node->left, &parent_higher);
            if (status != FT_STATUS_OK) {
                goto cleanup;
            }
            if (!parent_higher) {
                local_valid = false;
                break;
            }
        }
        if (node->right != NULL) {
            bool parent_higher = false;
            status = ft_canonical_higher_nodes(set->policy, node, node->right, &parent_higher);
            if (status != FT_STATUS_OK) {
                goto cleanup;
            }
            if (!parent_higher) {
                local_valid = false;
                break;
            }
        }
        if (ft_canonical_add_overflows(
                node->left == NULL ? 0 : node->left->count,
                node->right == NULL ? 0 : node->right->count,
                &child_count) || child_count == SIZE_MAX) {
            local_valid = false;
            break;
        }
        expected_count = child_count + 1;
        if (node->count != expected_count || node->height != expected_height) {
            local_valid = false;
            break;
        }
        ++local_statistics.count;
        if (frame.depth > local_statistics.height) {
            local_statistics.height = frame.depth;
        }
        if (node->rank.geometric > local_statistics.maximum_geometric_rank) {
            local_statistics.maximum_geometric_rank = node->rank.geometric;
        }
        if (!ft_canonical_priority_insert(priorities, hash_capacity, node->rank)) {
            ++local_statistics.priority_collision_count;
        }
        if ((node->left != NULL || node->right != NULL) &&
            (frame.depth == SIZE_MAX || stack_count + 2 > stack_capacity)) {
            local_valid = false;
            break;
        }
        if (node->right != NULL) {
            stack[stack_count].node = node->right;
            stack[stack_count].lower = node->value->bytes;
            stack[stack_count].upper = frame.upper;
            stack[stack_count].depth = frame.depth + 1;
            ++stack_count;
        }
        if (node->left != NULL) {
            stack[stack_count].node = node->left;
            stack[stack_count].lower = frame.lower;
            stack[stack_count].upper = node->value->bytes;
            stack[stack_count].depth = frame.depth + 1;
            ++stack_count;
        }
    }
    if (local_valid &&
        (local_statistics.count != set->root->count || local_statistics.height != set->root->height)) {
        local_valid = false;
    }

cleanup:
    ft_canonical_deallocate(set->policy, priorities);
    ft_canonical_deallocate(set->policy, pointers);
    ft_canonical_deallocate(set->policy, stack);
    if (status == FT_STATUS_OK) {
        *valid = local_valid;
        if (!local_valid) {
            (void)memset(&local_statistics, 0, sizeof(local_statistics));
        }
        *statistics = local_statistics;
    }
    return status;
}
