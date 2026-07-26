/*
 * Implementation of the Merkle search tree: encoding, verification, and synchronization.
 *
 * Block encoding is canonical and injective, because a second encoding of the same contents would
 * produce a second digest and defeat the whole comparison scheme. Decoding rejects noncanonical
 * input rather than accepting it leniently.
 */

#include <durable7/hamt/merkle_search_tree.h>

#include <limits.h>
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
#include <threads.h>
#endif

enum {
    D7_MST_MAXIMUM_LEVEL = 64,
    D7_MST_MAXIMUM_HEIGHT = 65,
    D7_MST_BLOCK_HEADER_LENGTH = 4 + 1 + 32 + 1 + 4 + 4,
    D7_MST_NODE_BLOCK_TAG = 1
};

static const unsigned char d7_mst_algorithm_id[] = "mst-sha256-b16-v2";
static const unsigned char d7_mst_block_magic[] = {'M', 'S', 'T', '2'};
static const char d7_mst_lower_hex[] = "0123456789abcdef";

#if defined(_MSC_VER) && !defined(__clang__)
typedef volatile LONG64 d7_mst_ref_count;

static void d7_mst_ref_init(d7_mst_ref_count *value) {
    *value = 1;
}

static void d7_mst_ref_retain(d7_mst_ref_count *value) {
    const LONG64 incremented = InterlockedIncrement64(value);
    if (incremented <= 1) {
        /* Retaining a dead object or overflowing the signed platform counter
         * is an unrecoverable ownership invariant violation. */
        abort();
    }
}

static bool d7_mst_ref_release(d7_mst_ref_count *value) {
    const LONG64 decremented = InterlockedDecrement64(value);
    if (decremented < 0) {
        abort();
    }
    return decremented == 0;
}
#else
typedef atomic_size_t d7_mst_ref_count;

static void d7_mst_ref_init(d7_mst_ref_count *value) {
    atomic_init(value, 1);
}

static void d7_mst_ref_retain(d7_mst_ref_count *value) {
    const size_t previous = atomic_fetch_add_explicit(
        value,
        1,
        memory_order_relaxed);
    if (previous == 0 || previous == SIZE_MAX) {
        abort();
    }
}

static bool d7_mst_ref_release(d7_mst_ref_count *value) {
    const size_t previous = atomic_fetch_sub_explicit(
        value,
        1,
        memory_order_acq_rel);
    if (previous == 0) {
        abort();
    }
    return previous == 1;
}
#endif

typedef struct d7_mst_bytes {
    d7_mst_ref_count refs;
    size_t size;
    unsigned char data[];
} d7_mst_bytes;

typedef struct d7_mst_object {
    d7_mst_ref_count refs;
    bool is_key;
    void *value;
} d7_mst_object;

typedef struct d7_mst_entry {
    d7_mst_ref_count refs;
    d7_mst_object *key;
    d7_mst_object *value;
    d7_mst_bytes *key_bytes;
    d7_mst_bytes *value_bytes;
    unsigned char level;
} d7_mst_entry;

struct d7_merkle_node {
    d7_mst_ref_count refs;
    struct d7_merkle_node *release_next;
    unsigned char level;
    size_t entry_count;
    size_t count;
    size_t height;
    size_t block_count;
    d7_mst_entry *minimum_entry;
    d7_mst_entry *maximum_entry;
    d7_mst_bytes *block_bytes;
    d7_merkle_digest digest;
    unsigned char storage[];
};

struct d7_merkle_policy_rep {
    d7_mst_ref_count refs;
    d7_merkle_policy_config config;
    d7_merkle_digest domain_digest;
    d7_merkle_digest empty_digest;
    unsigned char *policy_id;
    unsigned char *key_encoding_id;
    unsigned char *value_encoding_id;
};

typedef struct d7_mst_pending_entry {
    const void *key;
    const void *value;
    size_t sequence;
} d7_mst_pending_entry;

typedef struct d7_mst_iterator_frame {
    const struct d7_merkle_node *node;
    size_t index;
    bool child_visited;
} d7_mst_iterator_frame;

typedef struct d7_mst_iterator {
    d7_mst_iterator_frame frames[D7_MST_MAXIMUM_HEIGHT];
    size_t depth;
} d7_mst_iterator;

typedef struct d7_mst_validation_accumulator {
    size_t count;
    size_t block_count;
    size_t minimum_entries;
    size_t maximum_entries;
    size_t minimum_block_bytes;
    size_t maximum_block_bytes;
} d7_mst_validation_accumulator;

static void *d7_mst_default_allocate(size_t size, void *context) {
    (void)context;
    return malloc(size);
}

static void d7_mst_default_deallocate(void *allocation, void *context) {
    (void)context;
    free(allocation);
}

static void *d7_mst_allocate_config(
    const d7_merkle_policy_config *config,
    size_t size) {
    return config->allocator.allocate(size, config->allocator.context);
}

static void d7_mst_deallocate_config(
    const d7_merkle_policy_config *config,
    void *allocation) {
    if (allocation != NULL) {
        config->allocator.deallocate(allocation, config->allocator.context);
    }
}

static void *d7_mst_allocate(
    const struct d7_merkle_policy_rep *policy,
    size_t size) {
    return d7_mst_allocate_config(&policy->config, size);
}

static void d7_mst_deallocate(
    const struct d7_merkle_policy_rep *policy,
    void *allocation) {
    d7_mst_deallocate_config(&policy->config, allocation);
}

static bool d7_mst_add_overflows(size_t left, size_t right, size_t *result) {
    if (right > SIZE_MAX - left) {
        return true;
    }
    *result = left + right;
    return false;
}

static bool d7_mst_multiply_overflows(size_t left, size_t right, size_t *result) {
    if (left != 0 && right > SIZE_MAX / left) {
        return true;
    }
    *result = left * right;
    return false;
}

static void d7_mst_write_be32(uint32_t value, unsigned char *destination) {
    destination[0] = (unsigned char)(value >> 24);
    destination[1] = (unsigned char)(value >> 16);
    destination[2] = (unsigned char)(value >> 8);
    destination[3] = (unsigned char)value;
}

static uint32_t d7_mst_read_be32(const unsigned char *source) {
    return ((uint32_t)source[0] << 24) |
        ((uint32_t)source[1] << 16) |
        ((uint32_t)source[2] << 8) |
        (uint32_t)source[3];
}

static int d7_mst_hex_value(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

bool d7_merkle_digest_equal(d7_merkle_digest left, d7_merkle_digest right) {
    return memcmp(left.bytes, right.bytes, D7_MERKLE_DIGEST_BYTE_LENGTH) == 0;
}

int d7_merkle_digest_compare(d7_merkle_digest left, d7_merkle_digest right) {
    const int comparison = memcmp(left.bytes, right.bytes, D7_MERKLE_DIGEST_BYTE_LENGTH);
    return (comparison > 0) - (comparison < 0);
}

d7_merkle_status d7_merkle_digest_parse(
    const unsigned char *bytes,
    size_t byte_count,
    d7_merkle_digest *digest) {
    d7_merkle_digest parsed;
    if (bytes == NULL || digest == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    if (byte_count != D7_MERKLE_DIGEST_BYTE_LENGTH) {
        return D7_MERKLE_INVALID_ENCODING;
    }
    memcpy(parsed.bytes, bytes, sizeof(parsed.bytes));
    *digest = parsed;
    return D7_MERKLE_OK;
}

d7_merkle_status d7_merkle_digest_parse_hex(
    const char *hex,
    size_t character_count,
    d7_merkle_digest *digest) {
    d7_merkle_digest parsed;
    size_t index;
    if (hex == NULL || digest == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    if (character_count != D7_MERKLE_DIGEST_HEX_LENGTH) {
        return D7_MERKLE_INVALID_ENCODING;
    }
    for (index = 0; index != D7_MERKLE_DIGEST_BYTE_LENGTH; ++index) {
        const int high = d7_mst_hex_value(hex[index * 2]);
        const int low = d7_mst_hex_value(hex[index * 2 + 1]);
        if (high < 0 || low < 0) {
            return D7_MERKLE_INVALID_ENCODING;
        }
        parsed.bytes[index] = (unsigned char)((high << 4) | low);
    }
    *digest = parsed;
    return D7_MERKLE_OK;
}

d7_merkle_status d7_merkle_digest_write(
    d7_merkle_digest digest,
    unsigned char *destination,
    size_t destination_size) {
    if (destination == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    if (destination_size < D7_MERKLE_DIGEST_BYTE_LENGTH) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    memcpy(destination, digest.bytes, sizeof(digest.bytes));
    return D7_MERKLE_OK;
}

d7_merkle_status d7_merkle_digest_write_hex(
    d7_merkle_digest digest,
    char *destination,
    size_t destination_size) {
    size_t index;
    if (destination == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    if (destination_size < D7_MERKLE_DIGEST_HEX_LENGTH) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    for (index = 0; index != D7_MERKLE_DIGEST_BYTE_LENGTH; ++index) {
        destination[index * 2] = d7_mst_lower_hex[digest.bytes[index] >> 4];
        destination[index * 2 + 1] = d7_mst_lower_hex[digest.bytes[index] & 0x0f];
    }
    return D7_MERKLE_OK;
}

static bool d7_mst_utf8_next(
    const unsigned char *bytes,
    size_t size,
    size_t *offset,
    uint32_t *code_point) {
    const size_t start = *offset;
    unsigned char first;
    uint32_t value;
    size_t length;
    size_t index;
    if (start >= size) {
        return false;
    }
    first = bytes[start];
    if (first <= 0x7f) {
        *code_point = first;
        *offset = start + 1;
        return true;
    }
    if (first >= 0xc2 && first <= 0xdf) {
        value = first & 0x1f;
        length = 2;
    } else if (first >= 0xe0 && first <= 0xef) {
        value = first & 0x0f;
        length = 3;
    } else if (first >= 0xf0 && first <= 0xf4) {
        value = first & 0x07;
        length = 4;
    } else {
        return false;
    }
    if (length > size - start) {
        return false;
    }
    for (index = 1; index != length; ++index) {
        const unsigned char continuation = bytes[start + index];
        if ((continuation & 0xc0) != 0x80) {
            return false;
        }
        value = (value << 6) | (continuation & 0x3f);
    }
    if ((length == 3 && value < 0x800) ||
        (length == 4 && value < 0x10000) ||
        value > 0x10ffff ||
        (value >= 0xd800 && value <= 0xdfff)) {
        return false;
    }
    *code_point = value;
    *offset = start + length;
    return true;
}

static bool d7_mst_utf8_valid(const unsigned char *bytes, size_t size) {
    size_t offset = 0;
    uint32_t code_point = 0;
    if (size != 0 && bytes == NULL) {
        return false;
    }
    while (offset != size) {
        if (!d7_mst_utf8_next(bytes, size, &offset, &code_point)) {
            return false;
        }
    }
    return true;
}

static bool d7_mst_unicode_whitespace(uint32_t code_point) {
    return (code_point >= 0x0009 && code_point <= 0x000d) ||
        code_point == 0x0020 || code_point == 0x0085 ||
        code_point == 0x00a0 || code_point == 0x1680 ||
        (code_point >= 0x2000 && code_point <= 0x200a) ||
        code_point == 0x2028 || code_point == 0x2029 ||
        code_point == 0x202f || code_point == 0x205f ||
        code_point == 0x3000;
}

static bool d7_mst_policy_id_valid(d7_merkle_identifier identifier) {
    size_t offset = 0;
    uint32_t code_point = 0;
    bool non_whitespace = false;
    if (identifier.size == 0 || identifier.bytes == NULL) {
        return false;
    }
    while (offset != identifier.size) {
        if (!d7_mst_utf8_next(
                identifier.bytes,
                identifier.size,
                &offset,
                &code_point)) {
            return false;
        }
        non_whitespace = non_whitespace || !d7_mst_unicode_whitespace(code_point);
    }
    return non_whitespace;
}

static bool d7_mst_encoding_id_valid(d7_merkle_identifier identifier) {
    size_t offset = 0;
    size_t last_offset = 0;
    size_t marker = SIZE_MAX;
    uint32_t first = 0;
    uint32_t last = 0;
    uint32_t code_point = 0;
    size_t index;
    if (identifier.size == 0 || identifier.bytes == NULL) {
        return false;
    }
    while (offset != identifier.size) {
        const size_t current = offset;
        if (!d7_mst_utf8_next(
                identifier.bytes,
                identifier.size,
                &offset,
                &code_point)) {
            return false;
        }
        if (current == 0) {
            first = code_point;
        }
        last = code_point;
        last_offset = current;
    }
    (void)last_offset;
    if (d7_mst_unicode_whitespace(first) || d7_mst_unicode_whitespace(last)) {
        return false;
    }
    if (identifier.size >= 2) {
        for (index = identifier.size - 1; index != 0; --index) {
            if (identifier.bytes[index - 1] == '-' && identifier.bytes[index] == 'v') {
                marker = index - 1;
                break;
            }
        }
    }
    if (marker == SIZE_MAX || marker == 0 || marker + 2 == identifier.size) {
        return false;
    }
    for (index = marker + 2; index != identifier.size; ++index) {
        if (identifier.bytes[index] < '0' || identifier.bytes[index] > '9') {
            return false;
        }
    }
    return true;
}

static d7_merkle_status d7_mst_copy_bytes(
    const d7_merkle_policy_config *config,
    d7_merkle_identifier identifier,
    unsigned char **result) {
    unsigned char *copy;
    if (identifier.size == 0) {
        *result = NULL;
        return D7_MERKLE_OK;
    }
    copy = (unsigned char *)d7_mst_allocate_config(config, identifier.size);
    if (copy == NULL) {
        return D7_MERKLE_NO_MEMORY;
    }
    memcpy(copy, identifier.bytes, identifier.size);
    *result = copy;
    return D7_MERKLE_OK;
}

#if defined(_WIN32)
static d7_merkle_status d7_mst_sha256_config(
    const d7_merkle_policy_config *config,
    const unsigned char *message,
    size_t message_size,
    d7_merkle_digest *digest) {
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    unsigned char *object = NULL;
    ULONG object_size = 0;
    ULONG digest_size = 0;
    ULONG copied = 0;
    NTSTATUS native_status;
    d7_merkle_digest staged;
    d7_merkle_status status = D7_MERKLE_CRYPTO_FAILURE;
    if (message_size > ULONG_MAX) {
        return D7_MERKLE_OVERFLOW;
    }
    native_status = BCryptOpenAlgorithmProvider(
        &algorithm,
        BCRYPT_SHA256_ALGORITHM,
        NULL,
        0);
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
    if (native_status < 0 || copied != sizeof(digest_size) ||
        digest_size != D7_MERKLE_DIGEST_BYTE_LENGTH) {
        goto cleanup;
    }
    object = (unsigned char *)d7_mst_allocate_config(config, object_size);
    if (object == NULL) {
        status = D7_MERKLE_NO_MEMORY;
        goto cleanup;
    }
    native_status = BCryptCreateHash(
        algorithm,
        &hash,
        object,
        object_size,
        NULL,
        0,
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
    native_status = BCryptFinishHash(
        hash,
        staged.bytes,
        (ULONG)sizeof(staged.bytes),
        0);
    if (native_status < 0) {
        goto cleanup;
    }
    *digest = staged;
    status = D7_MERKLE_OK;

cleanup:
    if (hash != NULL) {
        (void)BCryptDestroyHash(hash);
    }
    d7_mst_deallocate_config(config, object);
    if (algorithm != NULL) {
        (void)BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    return status;
}
#else
static d7_merkle_status d7_mst_sha256_config(
    const d7_merkle_policy_config *config,
    const unsigned char *message,
    size_t message_size,
    d7_merkle_digest *digest) {
    unsigned int written = 0;
    d7_merkle_digest staged;
    (void)config;
    if (EVP_Digest(
            message,
            message_size,
            staged.bytes,
            &written,
            EVP_sha256(),
            NULL) != 1 || written != D7_MERKLE_DIGEST_BYTE_LENGTH) {
        return D7_MERKLE_CRYPTO_FAILURE;
    }
    *digest = staged;
    return D7_MERKLE_OK;
}
#endif

static d7_merkle_status d7_mst_hash_framed_config(
    const d7_merkle_policy_config *config,
    unsigned char tag,
    const d7_merkle_identifier *fields,
    size_t field_count,
    d7_merkle_digest *digest) {
    unsigned char *bytes = NULL;
    size_t size = 1;
    size_t offset = 0;
    size_t index;
    d7_merkle_status status;
    for (index = 0; index != field_count; ++index) {
        if (fields[index].size > INT32_MAX ||
            d7_mst_add_overflows(size, 4, &size) ||
            d7_mst_add_overflows(size, fields[index].size, &size)) {
            return D7_MERKLE_OVERFLOW;
        }
    }
    bytes = (unsigned char *)d7_mst_allocate_config(config, size);
    if (bytes == NULL) {
        return D7_MERKLE_NO_MEMORY;
    }
    bytes[offset++] = tag;
    for (index = 0; index != field_count; ++index) {
        d7_mst_write_be32((uint32_t)fields[index].size, bytes + offset);
        offset += 4;
        if (fields[index].size != 0) {
            memcpy(bytes + offset, fields[index].bytes, fields[index].size);
            offset += fields[index].size;
        }
    }
    status = d7_mst_sha256_config(config, bytes, size, digest);
    d7_mst_deallocate_config(config, bytes);
    return status;
}

static d7_merkle_status d7_mst_pod_equal(
    const void *left,
    const void *right,
    bool *equal,
    void *context) {
    const size_t size = *(const size_t *)context;
    *equal = memcmp(left, right, size) == 0;
    return D7_MERKLE_OK;
}

static size_t d7_mst_i32_size = sizeof(int32_t);
static size_t d7_mst_i64_size = sizeof(int64_t);
static size_t d7_mst_guid_size = sizeof(d7_merkle_guid);

static d7_merkle_status d7_mst_i32_encode(
    const void *value,
    unsigned char *destination,
    size_t destination_size,
    size_t *bytes_written,
    const d7_merkle_allocator *allocator,
    void *context) {
    uint32_t bits;
    (void)allocator;
    (void)context;
    if (value == NULL || bytes_written == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    *bytes_written = sizeof(int32_t);
    if (destination == NULL) {
        return destination_size == 0 ? D7_MERKLE_OK : D7_MERKLE_INVALID_ARGUMENT;
    }
    if (destination_size != sizeof(int32_t)) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    bits = (uint32_t)*(const int32_t *)value;
    d7_mst_write_be32(bits, destination);
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_i32_decode(
    const unsigned char *encoding,
    size_t encoding_size,
    void *destination,
    const d7_merkle_allocator *allocator,
    void *context) {
    (void)allocator;
    (void)context;
    if (encoding == NULL || destination == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    if (encoding_size != sizeof(int32_t)) {
        return D7_MERKLE_INVALID_ENCODING;
    }
    *(int32_t *)destination = (int32_t)d7_mst_read_be32(encoding);
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_i64_encode(
    const void *value,
    unsigned char *destination,
    size_t destination_size,
    size_t *bytes_written,
    const d7_merkle_allocator *allocator,
    void *context) {
    uint64_t bits;
    size_t index;
    (void)allocator;
    (void)context;
    if (value == NULL || bytes_written == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    bits = (uint64_t)*(const int64_t *)value;
    *bytes_written = sizeof(int64_t);
    if (destination == NULL) {
        return destination_size == 0 ? D7_MERKLE_OK : D7_MERKLE_INVALID_ARGUMENT;
    }
    if (destination_size != sizeof(int64_t)) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    for (index = 0; index != sizeof(int64_t); ++index) {
        destination[index] = (unsigned char)(bits >> ((sizeof(int64_t) - 1 - index) * 8));
    }
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_i64_decode(
    const unsigned char *encoding,
    size_t encoding_size,
    void *destination,
    const d7_merkle_allocator *allocator,
    void *context) {
    uint64_t bits = 0;
    size_t index;
    (void)allocator;
    (void)context;
    if (encoding == NULL || destination == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    if (encoding_size != sizeof(int64_t)) {
        return D7_MERKLE_INVALID_ENCODING;
    }
    for (index = 0; index != sizeof(int64_t); ++index) {
        bits = (bits << 8) | encoding[index];
    }
    *(int64_t *)destination = (int64_t)bits;
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_guid_encode(
    const void *value,
    unsigned char *destination,
    size_t destination_size,
    size_t *bytes_written,
    const d7_merkle_allocator *allocator,
    void *context) {
    (void)allocator;
    (void)context;
    if (value == NULL || bytes_written == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    *bytes_written = sizeof(((d7_merkle_guid *)0)->bytes);
    if (destination == NULL) {
        return destination_size == 0 ? D7_MERKLE_OK : D7_MERKLE_INVALID_ARGUMENT;
    }
    if (destination_size != sizeof(((d7_merkle_guid *)0)->bytes)) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    memcpy(destination, ((const d7_merkle_guid *)value)->bytes, destination_size);
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_guid_decode(
    const unsigned char *encoding,
    size_t encoding_size,
    void *destination,
    const d7_merkle_allocator *allocator,
    void *context) {
    (void)allocator;
    (void)context;
    if (encoding == NULL || destination == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    if (encoding_size != sizeof(((d7_merkle_guid *)0)->bytes)) {
        return D7_MERKLE_INVALID_ENCODING;
    }
    memcpy(((d7_merkle_guid *)destination)->bytes, encoding, encoding_size);
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_nullable_copy_core(
    bool has_value,
    const void *data,
    size_t size,
    bool utf8,
    void **copied,
    const d7_merkle_allocator *allocator) {
    void *bytes = NULL;
    if (!has_value) {
        if (size != 0) {
            return D7_MERKLE_INVALID_ENCODING;
        }
        *copied = NULL;
        return D7_MERKLE_OK;
    }
    if ((size != 0 && data == NULL) ||
        (utf8 && !d7_mst_utf8_valid((const unsigned char *)data, size))) {
        return D7_MERKLE_INVALID_ENCODING;
    }
    if (size != 0) {
        bytes = allocator->allocate(size, allocator->context);
        if (bytes == NULL) {
            return D7_MERKLE_NO_MEMORY;
        }
        memcpy(bytes, data, size);
    }
    *copied = bytes;
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_nullable_utf8_copy(
    void *destination,
    const void *source,
    const d7_merkle_allocator *allocator,
    void *context) {
    const d7_merkle_nullable_utf8 *input = (const d7_merkle_nullable_utf8 *)source;
    d7_merkle_nullable_utf8 result = {false, NULL, 0};
    void *copy = NULL;
    d7_merkle_status status;
    (void)context;
    status = d7_mst_nullable_copy_core(
        input->has_value,
        input->data,
        input->size,
        true,
        &copy,
        allocator);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    result.has_value = input->has_value;
    result.data = (const char *)copy;
    result.size = input->size;
    *(d7_merkle_nullable_utf8 *)destination = result;
    return D7_MERKLE_OK;
}

static void d7_mst_nullable_utf8_destroy(
    void *value,
    const d7_merkle_allocator *allocator,
    void *context) {
    d7_merkle_nullable_utf8 *nullable = (d7_merkle_nullable_utf8 *)value;
    (void)context;
    if (nullable->data != NULL) {
        allocator->deallocate((void *)nullable->data, allocator->context);
    }
    memset(nullable, 0, sizeof(*nullable));
}

static d7_merkle_status d7_mst_nullable_utf8_equal(
    const void *left,
    const void *right,
    bool *equal,
    void *context) {
    const d7_merkle_nullable_utf8 *first = (const d7_merkle_nullable_utf8 *)left;
    const d7_merkle_nullable_utf8 *second = (const d7_merkle_nullable_utf8 *)right;
    (void)context;
    *equal = first->has_value == second->has_value &&
        (!first->has_value ||
            (first->size == second->size &&
                (first->size == 0 ||
                    memcmp(first->data, second->data, first->size) == 0)));
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_nullable_bytes_copy(
    void *destination,
    const void *source,
    const d7_merkle_allocator *allocator,
    void *context) {
    const d7_merkle_nullable_bytes *input = (const d7_merkle_nullable_bytes *)source;
    d7_merkle_nullable_bytes result = {false, NULL, 0};
    void *copy = NULL;
    d7_merkle_status status;
    (void)context;
    status = d7_mst_nullable_copy_core(
        input->has_value,
        input->data,
        input->size,
        false,
        &copy,
        allocator);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    result.has_value = input->has_value;
    result.data = (const unsigned char *)copy;
    result.size = input->size;
    *(d7_merkle_nullable_bytes *)destination = result;
    return D7_MERKLE_OK;
}

static void d7_mst_nullable_bytes_destroy(
    void *value,
    const d7_merkle_allocator *allocator,
    void *context) {
    d7_merkle_nullable_bytes *nullable = (d7_merkle_nullable_bytes *)value;
    (void)context;
    if (nullable->data != NULL) {
        allocator->deallocate((void *)nullable->data, allocator->context);
    }
    memset(nullable, 0, sizeof(*nullable));
}

static d7_merkle_status d7_mst_nullable_bytes_equal(
    const void *left,
    const void *right,
    bool *equal,
    void *context) {
    const d7_merkle_nullable_bytes *first = (const d7_merkle_nullable_bytes *)left;
    const d7_merkle_nullable_bytes *second = (const d7_merkle_nullable_bytes *)right;
    (void)context;
    *equal = first->has_value == second->has_value &&
        (!first->has_value ||
            (first->size == second->size &&
                (first->size == 0 ||
                    memcmp(first->data, second->data, first->size) == 0)));
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_nullable_encode_core(
    bool has_value,
    const unsigned char *data,
    size_t size,
    bool utf8,
    unsigned char *destination,
    size_t destination_size,
    size_t *bytes_written) {
    size_t required;
    if ((!has_value && size != 0) ||
        (has_value && size != 0 && data == NULL) ||
        (has_value && utf8 && !d7_mst_utf8_valid(data, size)) ||
        d7_mst_add_overflows(1, has_value ? size : 0, &required)) {
        return D7_MERKLE_INVALID_ENCODING;
    }
    *bytes_written = required;
    if (destination == NULL) {
        return destination_size == 0 ? D7_MERKLE_OK : D7_MERKLE_INVALID_ARGUMENT;
    }
    if (destination_size != required) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    destination[0] = has_value ? 1 : 0;
    if (has_value && size != 0) {
        memcpy(destination + 1, data, size);
    }
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_nullable_utf8_encode(
    const void *value,
    unsigned char *destination,
    size_t destination_size,
    size_t *bytes_written,
    const d7_merkle_allocator *allocator,
    void *context) {
    const d7_merkle_nullable_utf8 *nullable = (const d7_merkle_nullable_utf8 *)value;
    (void)allocator;
    (void)context;
    if (nullable == NULL || bytes_written == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    return d7_mst_nullable_encode_core(
        nullable->has_value,
        (const unsigned char *)nullable->data,
        nullable->size,
        true,
        destination,
        destination_size,
        bytes_written);
}

static d7_merkle_status d7_mst_nullable_bytes_encode(
    const void *value,
    unsigned char *destination,
    size_t destination_size,
    size_t *bytes_written,
    const d7_merkle_allocator *allocator,
    void *context) {
    const d7_merkle_nullable_bytes *nullable = (const d7_merkle_nullable_bytes *)value;
    (void)allocator;
    (void)context;
    if (nullable == NULL || bytes_written == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    return d7_mst_nullable_encode_core(
        nullable->has_value,
        nullable->data,
        nullable->size,
        false,
        destination,
        destination_size,
        bytes_written);
}

static d7_merkle_status d7_mst_nullable_utf8_decode(
    const unsigned char *encoding,
    size_t encoding_size,
    void *destination,
    const d7_merkle_allocator *allocator,
    void *context) {
    d7_merkle_nullable_utf8 source;
    (void)context;
    if (encoding == NULL || destination == NULL || encoding_size == 0) {
        return D7_MERKLE_INVALID_ENCODING;
    }
    if (encoding[0] == 0) {
        if (encoding_size != 1) {
            return D7_MERKLE_INVALID_ENCODING;
        }
        source = (d7_merkle_nullable_utf8){false, NULL, 0};
    } else if (encoding[0] == 1 &&
        d7_mst_utf8_valid(encoding + 1, encoding_size - 1)) {
        source = (d7_merkle_nullable_utf8){
            true,
            (const char *)(encoding + 1),
            encoding_size - 1};
    } else {
        return D7_MERKLE_INVALID_ENCODING;
    }
    return d7_mst_nullable_utf8_copy(
        destination,
        &source,
        allocator,
        NULL);
}

static d7_merkle_status d7_mst_nullable_bytes_decode(
    const unsigned char *encoding,
    size_t encoding_size,
    void *destination,
    const d7_merkle_allocator *allocator,
    void *context) {
    d7_merkle_nullable_bytes source;
    (void)context;
    if (encoding == NULL || destination == NULL || encoding_size == 0) {
        return D7_MERKLE_INVALID_ENCODING;
    }
    if (encoding[0] == 0) {
        if (encoding_size != 1) {
            return D7_MERKLE_INVALID_ENCODING;
        }
        source = (d7_merkle_nullable_bytes){false, NULL, 0};
    } else if (encoding[0] == 1) {
        source = (d7_merkle_nullable_bytes){true, encoding + 1, encoding_size - 1};
    } else {
        return D7_MERKLE_INVALID_ENCODING;
    }
    return d7_mst_nullable_bytes_copy(
        destination,
        &source,
        allocator,
        NULL);
}

void d7_merkle_type_policy_init(
    d7_merkle_type_policy *type,
    size_t size,
    const void *type_identity) {
    if (type != NULL) {
        memset(type, 0, sizeof(*type));
        type->size = size;
        type->type_identity = type_identity;
    }
}

void d7_merkle_codec_init(
    d7_merkle_codec *codec,
    const unsigned char *encoding_id,
    size_t encoding_id_size,
    d7_merkle_encode_fn encode,
    d7_merkle_decode_fn decode) {
    if (codec != NULL) {
        memset(codec, 0, sizeof(*codec));
        codec->encoding_id.bytes = encoding_id;
        codec->encoding_id.size = encoding_id_size;
        codec->encode = encode;
        codec->decode = decode;
    }
}

void d7_merkle_policy_config_init(d7_merkle_policy_config *config) {
    if (config != NULL) {
        memset(config, 0, sizeof(*config));
        config->allocator.allocate = d7_mst_default_allocate;
        config->allocator.deallocate = d7_mst_default_deallocate;
    }
}

void d7_merkle_i32_type_policy_init(
    d7_merkle_type_policy *type,
    const void *type_identity) {
    d7_merkle_type_policy_init(type, sizeof(int32_t), type_identity);
    if (type != NULL) {
        type->equals = d7_mst_pod_equal;
        type->context = (void *)&d7_mst_i32_size;
    }
}

void d7_merkle_i64_type_policy_init(
    d7_merkle_type_policy *type,
    const void *type_identity) {
    d7_merkle_type_policy_init(type, sizeof(int64_t), type_identity);
    if (type != NULL) {
        type->equals = d7_mst_pod_equal;
        type->context = (void *)&d7_mst_i64_size;
    }
}

void d7_merkle_nullable_utf8_type_policy_init(
    d7_merkle_type_policy *type,
    const void *type_identity) {
    d7_merkle_type_policy_init(type, sizeof(d7_merkle_nullable_utf8), type_identity);
    if (type != NULL) {
        type->copy = d7_mst_nullable_utf8_copy;
        type->destroy = d7_mst_nullable_utf8_destroy;
        type->equals = d7_mst_nullable_utf8_equal;
    }
}

void d7_merkle_nullable_bytes_type_policy_init(
    d7_merkle_type_policy *type,
    const void *type_identity) {
    d7_merkle_type_policy_init(type, sizeof(d7_merkle_nullable_bytes), type_identity);
    if (type != NULL) {
        type->copy = d7_mst_nullable_bytes_copy;
        type->destroy = d7_mst_nullable_bytes_destroy;
        type->equals = d7_mst_nullable_bytes_equal;
    }
}

void d7_merkle_guid_type_policy_init(
    d7_merkle_type_policy *type,
    const void *type_identity) {
    d7_merkle_type_policy_init(type, sizeof(d7_merkle_guid), type_identity);
    if (type != NULL) {
        type->equals = d7_mst_pod_equal;
        type->context = (void *)&d7_mst_guid_size;
    }
}

void d7_merkle_i32_codec_init(d7_merkle_codec *codec) {
    static const unsigned char id[] = "i32-be-v1";
    d7_merkle_codec_init(codec, id, sizeof(id) - 1, d7_mst_i32_encode, d7_mst_i32_decode);
}

void d7_merkle_i64_codec_init(d7_merkle_codec *codec) {
    static const unsigned char id[] = "i64-be-v1";
    d7_merkle_codec_init(codec, id, sizeof(id) - 1, d7_mst_i64_encode, d7_mst_i64_decode);
}

void d7_merkle_nullable_utf8_codec_init(d7_merkle_codec *codec) {
    static const unsigned char id[] = "nullable-utf8-v1";
    d7_merkle_codec_init(
        codec,
        id,
        sizeof(id) - 1,
        d7_mst_nullable_utf8_encode,
        d7_mst_nullable_utf8_decode);
}

void d7_merkle_nullable_bytes_codec_init(d7_merkle_codec *codec) {
    static const unsigned char id[] = "nullable-bytes-v1";
    d7_merkle_codec_init(
        codec,
        id,
        sizeof(id) - 1,
        d7_mst_nullable_bytes_encode,
        d7_mst_nullable_bytes_decode);
}

void d7_merkle_guid_codec_init(d7_merkle_codec *codec) {
    static const unsigned char id[] = "guid-rfc4122-v1";
    d7_merkle_codec_init(codec, id, sizeof(id) - 1, d7_mst_guid_encode, d7_mst_guid_decode);
}

static bool d7_mst_type_valid(const d7_merkle_type_policy *type) {
    return type->size != 0 && type->type_identity != NULL &&
        (type->destroy == NULL || type->copy != NULL);
}

static bool d7_mst_config_valid(const d7_merkle_policy_config *config) {
    return config != NULL &&
        d7_mst_policy_id_valid(config->policy_id) &&
        d7_mst_type_valid(&config->key_type) &&
        d7_mst_type_valid(&config->value_type) &&
        config->key_compare != NULL &&
        d7_mst_encoding_id_valid(config->key_codec.encoding_id) &&
        config->key_codec.encode != NULL && config->key_codec.decode != NULL &&
        d7_mst_encoding_id_valid(config->value_codec.encoding_id) &&
        config->value_codec.encode != NULL && config->value_codec.decode != NULL &&
        config->allocator.allocate != NULL && config->allocator.deallocate != NULL;
}

static void d7_mst_policy_retain(struct d7_merkle_policy_rep *policy) {
    if (policy != NULL) {
        d7_mst_ref_retain(&policy->refs);
    }
}

static void d7_mst_policy_release(struct d7_merkle_policy_rep *policy) {
    if (policy != NULL && d7_mst_ref_release(&policy->refs)) {
        d7_mst_deallocate_config(&policy->config, policy->value_encoding_id);
        d7_mst_deallocate_config(&policy->config, policy->key_encoding_id);
        d7_mst_deallocate_config(&policy->config, policy->policy_id);
        d7_mst_deallocate_config(&policy->config, policy);
    }
}

d7_merkle_status d7_merkle_policy_create(
    const d7_merkle_policy_config *config,
    d7_merkle_policy *policy) {
    struct d7_merkle_policy_rep *rep = NULL;
    d7_merkle_identifier fields[4];
    unsigned char empty_manifest[5 + D7_MERKLE_DIGEST_BYTE_LENGTH];
    d7_merkle_status status;
    if (policy == NULL || !d7_mst_config_valid(config)) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    rep = (struct d7_merkle_policy_rep *)d7_mst_allocate_config(config, sizeof(*rep));
    if (rep == NULL) {
        return D7_MERKLE_NO_MEMORY;
    }
    memset(rep, 0, sizeof(*rep));
    rep->config = *config;
    status = d7_mst_copy_bytes(config, config->policy_id, &rep->policy_id);
    if (status == D7_MERKLE_OK) {
        status = d7_mst_copy_bytes(
            config,
            config->key_codec.encoding_id,
            &rep->key_encoding_id);
    }
    if (status == D7_MERKLE_OK) {
        status = d7_mst_copy_bytes(
            config,
            config->value_codec.encoding_id,
            &rep->value_encoding_id);
    }
    if (status != D7_MERKLE_OK) {
        d7_mst_deallocate_config(config, rep->value_encoding_id);
        d7_mst_deallocate_config(config, rep->key_encoding_id);
        d7_mst_deallocate_config(config, rep->policy_id);
        d7_mst_deallocate_config(config, rep);
        return status;
    }
    rep->config.policy_id.bytes = rep->policy_id;
    rep->config.key_codec.encoding_id.bytes = rep->key_encoding_id;
    rep->config.value_codec.encoding_id.bytes = rep->value_encoding_id;
    fields[0] = (d7_merkle_identifier){
        d7_mst_algorithm_id,
        sizeof(d7_mst_algorithm_id) - 1};
    fields[1] = rep->config.policy_id;
    fields[2] = rep->config.key_codec.encoding_id;
    fields[3] = rep->config.value_codec.encoding_id;
    status = d7_mst_hash_framed_config(
        &rep->config,
        0x50,
        fields,
        4,
        &rep->domain_digest);
    if (status == D7_MERKLE_OK) {
        memcpy(empty_manifest, d7_mst_block_magic, sizeof(d7_mst_block_magic));
        empty_manifest[4] = 0;
        memcpy(
            empty_manifest + 5,
            rep->domain_digest.bytes,
            D7_MERKLE_DIGEST_BYTE_LENGTH);
        status = d7_mst_sha256_config(
            &rep->config,
            empty_manifest,
            sizeof(empty_manifest),
            &rep->empty_digest);
    }
    if (status != D7_MERKLE_OK) {
        d7_mst_deallocate_config(&rep->config, rep->value_encoding_id);
        d7_mst_deallocate_config(&rep->config, rep->key_encoding_id);
        d7_mst_deallocate_config(&rep->config, rep->policy_id);
        d7_mst_deallocate_config(&rep->config, rep);
        return status;
    }
    d7_mst_ref_init(&rep->refs);
    policy->rep = rep;
    return D7_MERKLE_OK;
}

d7_merkle_status d7_merkle_policy_copy(
    const d7_merkle_policy *source,
    d7_merkle_policy *destination) {
    if (source == NULL || source->rep == NULL || destination == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    if (source != destination) {
        d7_mst_policy_retain(source->rep);
        destination->rep = source->rep;
    }
    return D7_MERKLE_OK;
}

void d7_merkle_policy_move(
    d7_merkle_policy *destination,
    d7_merkle_policy *source) {
    if (destination != NULL && source != NULL && destination != source) {
        destination->rep = source->rep;
        source->rep = NULL;
    }
}

void d7_merkle_policy_dispose(d7_merkle_policy *policy) {
    if (policy != NULL) {
        d7_mst_policy_release(policy->rep);
        policy->rep = NULL;
    }
}

bool d7_merkle_policy_same_identity(
    const d7_merkle_policy *left,
    const d7_merkle_policy *right) {
    return left != NULL && right != NULL && left->rep != NULL && left->rep == right->rep;
}

bool d7_merkle_policy_same_domain(
    const d7_merkle_policy *left,
    const d7_merkle_policy *right) {
    return left != NULL && right != NULL && left->rep != NULL && right->rep != NULL &&
        d7_merkle_digest_equal(left->rep->domain_digest, right->rep->domain_digest);
}

const char *d7_merkle_algorithm_id(void) {
    return (const char *)d7_mst_algorithm_id;
}

d7_merkle_digest d7_merkle_policy_domain_digest(const d7_merkle_policy *policy) {
    d7_merkle_digest result = {{0}};
    return policy == NULL || policy->rep == NULL ? result : policy->rep->domain_digest;
}

d7_merkle_digest d7_merkle_policy_empty_digest(const d7_merkle_policy *policy) {
    d7_merkle_digest result = {{0}};
    return policy == NULL || policy->rep == NULL ? result : policy->rep->empty_digest;
}

static const d7_merkle_type_policy *d7_mst_object_type(
    const struct d7_merkle_policy_rep *policy,
    bool is_key) {
    return is_key ? &policy->config.key_type : &policy->config.value_type;
}

static void d7_mst_bytes_retain(d7_mst_bytes *bytes) {
    if (bytes != NULL) {
        d7_mst_ref_retain(&bytes->refs);
    }
}

static void d7_mst_bytes_release(
    const struct d7_merkle_policy_rep *policy,
    d7_mst_bytes *bytes) {
    if (bytes != NULL && d7_mst_ref_release(&bytes->refs)) {
        d7_mst_deallocate(policy, bytes);
    }
}

static d7_merkle_status d7_mst_bytes_allocate(
    const struct d7_merkle_policy_rep *policy,
    size_t size,
    d7_mst_bytes **result) {
    size_t allocation_size;
    d7_mst_bytes *bytes;
    if (d7_mst_add_overflows(sizeof(*bytes), size, &allocation_size)) {
        return D7_MERKLE_OVERFLOW;
    }
    bytes = (d7_mst_bytes *)d7_mst_allocate(policy, allocation_size);
    if (bytes == NULL) {
        return D7_MERKLE_NO_MEMORY;
    }
    d7_mst_ref_init(&bytes->refs);
    bytes->size = size;
    *result = bytes;
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_encode_value(
    const struct d7_merkle_policy_rep *policy,
    const d7_merkle_codec *codec,
    const void *value,
    d7_mst_bytes **result) {
    size_t required = 0;
    size_t written = 0;
    d7_mst_bytes *bytes = NULL;
    d7_merkle_status status;
    status = codec->encode(
        value,
        NULL,
        0,
        &required,
        &policy->config.allocator,
        codec->context);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    if (required > INT32_MAX) {
        return D7_MERKLE_OVERFLOW;
    }
    status = d7_mst_bytes_allocate(policy, required, &bytes);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    status = codec->encode(
        value,
        bytes->data,
        required,
        &written,
        &policy->config.allocator,
        codec->context);
    if (status == D7_MERKLE_OK && written != required) {
        status = D7_MERKLE_INCONSISTENT_POLICY;
    }
    if (status != D7_MERKLE_OK) {
        d7_mst_bytes_release(policy, bytes);
        return status;
    }
    *result = bytes;
    return D7_MERKLE_OK;
}

static void d7_mst_object_retain(d7_mst_object *object) {
    if (object != NULL) {
        d7_mst_ref_retain(&object->refs);
    }
}

static void d7_mst_object_release(
    const struct d7_merkle_policy_rep *policy,
    d7_mst_object *object) {
    const d7_merkle_type_policy *type;
    if (object == NULL || !d7_mst_ref_release(&object->refs)) {
        return;
    }
    type = d7_mst_object_type(policy, object->is_key);
    if (type->destroy != NULL) {
        type->destroy(
            object->value,
            &policy->config.allocator,
            type->context);
    }
    d7_mst_deallocate(policy, object->value);
    d7_mst_deallocate(policy, object);
}

static d7_merkle_status d7_mst_object_create(
    const struct d7_merkle_policy_rep *policy,
    bool is_key,
    const void *source,
    d7_mst_object **result) {
    const d7_merkle_type_policy *type = d7_mst_object_type(policy, is_key);
    d7_mst_object *object;
    d7_merkle_status status = D7_MERKLE_OK;
    if (source == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    object = (d7_mst_object *)d7_mst_allocate(policy, sizeof(*object));
    if (object == NULL) {
        return D7_MERKLE_NO_MEMORY;
    }
    object->value = d7_mst_allocate(policy, type->size);
    if (object->value == NULL) {
        d7_mst_deallocate(policy, object);
        return D7_MERKLE_NO_MEMORY;
    }
    if (type->copy == NULL) {
        memcpy(object->value, source, type->size);
    } else {
        status = type->copy(
            object->value,
            source,
            &policy->config.allocator,
            type->context);
        if (status != D7_MERKLE_OK) {
            d7_mst_deallocate(policy, object->value);
            d7_mst_deallocate(policy, object);
            return status;
        }
    }
    d7_mst_ref_init(&object->refs);
    object->is_key = is_key;
    *result = object;
    return D7_MERKLE_OK;
}

static void d7_mst_entry_retain(d7_mst_entry *entry) {
    if (entry != NULL) {
        d7_mst_ref_retain(&entry->refs);
    }
}

static void d7_mst_entry_release(
    const struct d7_merkle_policy_rep *policy,
    d7_mst_entry *entry) {
    if (entry == NULL || !d7_mst_ref_release(&entry->refs)) {
        return;
    }
    d7_mst_bytes_release(policy, entry->value_bytes);
    d7_mst_bytes_release(policy, entry->key_bytes);
    d7_mst_object_release(policy, entry->value);
    d7_mst_object_release(policy, entry->key);
    d7_mst_deallocate(policy, entry);
}

static d7_merkle_status d7_mst_entry_from_parts(
    const struct d7_merkle_policy_rep *policy,
    d7_mst_object *key,
    d7_mst_object *value,
    d7_mst_bytes *key_bytes,
    d7_mst_bytes *value_bytes,
    unsigned level,
    d7_mst_entry **result) {
    d7_mst_entry *entry;
    if (key == NULL || value == NULL || key_bytes == NULL || value_bytes == NULL ||
        level > D7_MST_MAXIMUM_LEVEL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    entry = (d7_mst_entry *)d7_mst_allocate(policy, sizeof(*entry));
    if (entry == NULL) {
        return D7_MERKLE_NO_MEMORY;
    }
    d7_mst_ref_init(&entry->refs);
    entry->key = key;
    entry->value = value;
    entry->key_bytes = key_bytes;
    entry->value_bytes = value_bytes;
    entry->level = (unsigned char)level;
    d7_mst_object_retain(key);
    d7_mst_object_retain(value);
    d7_mst_bytes_retain(key_bytes);
    d7_mst_bytes_retain(value_bytes);
    *result = entry;
    return D7_MERKLE_OK;
}

static unsigned d7_mst_level(d7_merkle_digest digest) {
    unsigned result = 0;
    size_t index;
    for (index = 0; index != D7_MERKLE_DIGEST_BYTE_LENGTH; ++index) {
        if ((digest.bytes[index] & 0xf0) != 0) {
            return result;
        }
        ++result;
        if ((digest.bytes[index] & 0x0f) != 0) {
            return result;
        }
        ++result;
    }
    return result;
}

static d7_merkle_status d7_mst_hash_key_bytes(
    const struct d7_merkle_policy_rep *policy,
    const d7_mst_bytes *key_bytes,
    d7_merkle_digest *digest) {
    const d7_merkle_identifier fields[2] = {
        {policy->domain_digest.bytes, D7_MERKLE_DIGEST_BYTE_LENGTH},
        {key_bytes->data, key_bytes->size}};
    return d7_mst_hash_framed_config(
        &policy->config,
        0x4b,
        fields,
        2,
        digest);
}

static d7_merkle_status d7_mst_entry_create(
    const struct d7_merkle_policy_rep *policy,
    const void *key,
    const void *value,
    d7_mst_entry **result) {
    d7_mst_bytes *key_bytes = NULL;
    d7_mst_bytes *value_bytes = NULL;
    d7_mst_object *stored_key = NULL;
    d7_mst_object *stored_value = NULL;
    d7_merkle_digest key_digest = {{0}};
    d7_merkle_status status = d7_mst_encode_value(
        policy,
        &policy->config.key_codec,
        key,
        &key_bytes);
    if (status == D7_MERKLE_OK) {
        status = d7_mst_encode_value(
            policy,
            &policy->config.value_codec,
            value,
            &value_bytes);
    }
    if (status == D7_MERKLE_OK) {
        status = d7_mst_hash_key_bytes(policy, key_bytes, &key_digest);
    }
    if (status == D7_MERKLE_OK) {
        status = d7_mst_object_create(policy, true, key, &stored_key);
    }
    if (status == D7_MERKLE_OK) {
        status = d7_mst_object_create(policy, false, value, &stored_value);
    }
    if (status == D7_MERKLE_OK) {
        status = d7_mst_entry_from_parts(
            policy,
            stored_key,
            stored_value,
            key_bytes,
            value_bytes,
            d7_mst_level(key_digest),
            result);
    }
    d7_mst_object_release(policy, stored_value);
    d7_mst_object_release(policy, stored_key);
    d7_mst_bytes_release(policy, value_bytes);
    d7_mst_bytes_release(policy, key_bytes);
    return status;
}

static d7_merkle_status d7_mst_entry_replace_value(
    const struct d7_merkle_policy_rep *policy,
    const d7_mst_entry *existing,
    const void *value,
    bool *changed,
    d7_mst_entry **result) {
    d7_mst_bytes *value_bytes = NULL;
    d7_mst_object *stored_value = NULL;
    d7_merkle_status status = d7_mst_encode_value(
        policy,
        &policy->config.value_codec,
        value,
        &value_bytes);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    if (existing->value_bytes->size == value_bytes->size &&
        memcmp(existing->value_bytes->data, value_bytes->data, value_bytes->size) == 0) {
        *changed = false;
        *result = NULL;
        d7_mst_bytes_release(policy, value_bytes);
        return D7_MERKLE_OK;
    }
    status = d7_mst_object_create(policy, false, value, &stored_value);
    if (status == D7_MERKLE_OK) {
        status = d7_mst_entry_from_parts(
            policy,
            existing->key,
            stored_value,
            existing->key_bytes,
            value_bytes,
            existing->level,
            result);
    }
    d7_mst_object_release(policy, stored_value);
    d7_mst_bytes_release(policy, value_bytes);
    if (status == D7_MERKLE_OK) {
        *changed = true;
    }
    return status;
}

static d7_merkle_search_entry_ref d7_mst_entry_ref(const d7_mst_entry *entry) {
    d7_merkle_search_entry_ref result;
    result.key = entry->key->value;
    result.value = entry->value->value;
    result.key_bytes = entry->key_bytes->data;
    result.key_byte_count = entry->key_bytes->size;
    result.value_bytes = entry->value_bytes->data;
    result.value_byte_count = entry->value_bytes->size;
    result.level = entry->level;
    return result;
}

static d7_mst_entry **d7_mst_node_entries(struct d7_merkle_node *node) {
    return (d7_mst_entry **)(void *)node->storage;
}

static d7_mst_entry *const *d7_mst_node_entries_const(
    const struct d7_merkle_node *node) {
    return (d7_mst_entry *const *)(const void *)node->storage;
}

static struct d7_merkle_node **d7_mst_node_children(struct d7_merkle_node *node) {
    return (struct d7_merkle_node **)(void *)(
        d7_mst_node_entries(node) + node->entry_count);
}

static struct d7_merkle_node *const *d7_mst_node_children_const(
    const struct d7_merkle_node *node) {
    return (struct d7_merkle_node *const *)(const void *)(
        d7_mst_node_entries_const(node) + node->entry_count);
}

static void d7_mst_node_retain(struct d7_merkle_node *node) {
    if (node != NULL) {
        d7_mst_ref_retain(&node->refs);
    }
}

static void d7_mst_node_release(
    const struct d7_merkle_policy_rep *policy,
    struct d7_merkle_node *node) {
    struct d7_merkle_node *work = NULL;
    if (node == NULL || !d7_mst_ref_release(&node->refs)) {
        return;
    }
    node->release_next = NULL;
    work = node;
    while (work != NULL) {
        struct d7_merkle_node *current = work;
        d7_mst_entry **entries = d7_mst_node_entries(current);
        struct d7_merkle_node **children = d7_mst_node_children(current);
        const size_t entry_count = current->entry_count;
        size_t index;
        work = current->release_next;
        d7_mst_bytes_release(policy, current->block_bytes);
        for (index = 0; index != entry_count; ++index) {
            d7_mst_entry_release(policy, entries[index]);
        }
        for (index = 0; index != entry_count + 1; ++index) {
            struct d7_merkle_node *child = children[index];
            if (child != NULL && d7_mst_ref_release(&child->refs)) {
                child->release_next = work;
                work = child;
            }
        }
        d7_mst_deallocate(policy, current);
    }
}

static d7_merkle_status d7_mst_allocate_pointer_array(
    const struct d7_merkle_policy_rep *policy,
    size_t count,
    size_t element_size,
    void **result) {
    size_t size;
    if (count == 0) {
        *result = NULL;
        return D7_MERKLE_OK;
    }
    if (d7_mst_multiply_overflows(count, element_size, &size)) {
        return D7_MERKLE_OVERFLOW;
    }
    *result = d7_mst_allocate(policy, size);
    return *result == NULL ? D7_MERKLE_NO_MEMORY : D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_encode_block(
    const struct d7_merkle_policy_rep *policy,
    unsigned level,
    size_t subtree_count,
    d7_mst_entry *const *entries,
    size_t entry_count,
    struct d7_merkle_node *const *children,
    d7_mst_bytes **result,
    d7_merkle_digest *digest) {
    size_t length = D7_MST_BLOCK_HEADER_LENGTH;
    size_t child_bytes;
    size_t index;
    size_t offset = 0;
    d7_mst_bytes *bytes = NULL;
    d7_merkle_status status;
    if (level > D7_MST_MAXIMUM_LEVEL || entry_count == 0 ||
        entry_count > INT32_MAX || subtree_count > INT32_MAX ||
        d7_mst_multiply_overflows(
            entry_count + 1,
            D7_MERKLE_DIGEST_BYTE_LENGTH,
            &child_bytes) ||
        d7_mst_add_overflows(length, child_bytes, &length)) {
        return D7_MERKLE_OVERFLOW;
    }
    for (index = 0; index != entry_count; ++index) {
        if (entries[index]->key_bytes->size > INT32_MAX ||
            entries[index]->value_bytes->size > INT32_MAX ||
            d7_mst_add_overflows(length, 8, &length) ||
            d7_mst_add_overflows(length, entries[index]->key_bytes->size, &length) ||
            d7_mst_add_overflows(length, entries[index]->value_bytes->size, &length)) {
            return D7_MERKLE_OVERFLOW;
        }
    }
    status = d7_mst_bytes_allocate(policy, length, &bytes);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    memcpy(bytes->data + offset, d7_mst_block_magic, sizeof(d7_mst_block_magic));
    offset += sizeof(d7_mst_block_magic);
    bytes->data[offset++] = D7_MST_NODE_BLOCK_TAG;
    memcpy(
        bytes->data + offset,
        policy->domain_digest.bytes,
        D7_MERKLE_DIGEST_BYTE_LENGTH);
    offset += D7_MERKLE_DIGEST_BYTE_LENGTH;
    bytes->data[offset++] = (unsigned char)level;
    d7_mst_write_be32((uint32_t)subtree_count, bytes->data + offset);
    offset += 4;
    d7_mst_write_be32((uint32_t)entry_count, bytes->data + offset);
    offset += 4;
    for (index = 0; index != entry_count; ++index) {
        d7_mst_write_be32((uint32_t)entries[index]->key_bytes->size, bytes->data + offset);
        offset += 4;
        memcpy(
            bytes->data + offset,
            entries[index]->key_bytes->data,
            entries[index]->key_bytes->size);
        offset += entries[index]->key_bytes->size;
        d7_mst_write_be32((uint32_t)entries[index]->value_bytes->size, bytes->data + offset);
        offset += 4;
        memcpy(
            bytes->data + offset,
            entries[index]->value_bytes->data,
            entries[index]->value_bytes->size);
        offset += entries[index]->value_bytes->size;
    }
    for (index = 0; index != entry_count + 1; ++index) {
        const d7_merkle_digest child_digest = children[index] == NULL
            ? policy->empty_digest
            : children[index]->digest;
        memcpy(
            bytes->data + offset,
            child_digest.bytes,
            D7_MERKLE_DIGEST_BYTE_LENGTH);
        offset += D7_MERKLE_DIGEST_BYTE_LENGTH;
    }
    if (offset != length) {
        d7_mst_bytes_release(policy, bytes);
        return D7_MERKLE_INCONSISTENT_POLICY;
    }
    status = d7_mst_sha256_config(
        &policy->config,
        bytes->data,
        bytes->size,
        digest);
    if (status != D7_MERKLE_OK) {
        d7_mst_bytes_release(policy, bytes);
        return status;
    }
    *result = bytes;
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_node_create(
    const struct d7_merkle_policy_rep *policy,
    unsigned level,
    d7_mst_entry *const *entries,
    size_t entry_count,
    struct d7_merkle_node *const *children,
    struct d7_merkle_node **result) {
    struct d7_merkle_node *node = NULL;
    d7_mst_entry **stored_entries;
    struct d7_merkle_node **stored_children;
    d7_mst_bytes *block_bytes = NULL;
    d7_merkle_digest digest;
    size_t count = entry_count;
    size_t height = 1;
    size_t block_count = 1;
    size_t entry_storage;
    size_t child_storage;
    size_t allocation_size;
    size_t index;
    d7_merkle_status status;
    if (policy == NULL || entries == NULL || children == NULL || result == NULL ||
        level > D7_MST_MAXIMUM_LEVEL || entry_count == 0 || entry_count == SIZE_MAX ||
        d7_mst_multiply_overflows(entry_count, sizeof(*stored_entries), &entry_storage) ||
        d7_mst_multiply_overflows(entry_count + 1, sizeof(*stored_children), &child_storage) ||
        d7_mst_add_overflows(sizeof(*node), entry_storage, &allocation_size) ||
        d7_mst_add_overflows(allocation_size, child_storage, &allocation_size)) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    for (index = 0; index != entry_count; ++index) {
        if (entries[index] == NULL || entries[index]->level != level) {
            return D7_MERKLE_INCONSISTENT_POLICY;
        }
    }
    for (index = 0; index != entry_count + 1; ++index) {
        const struct d7_merkle_node *child = children[index];
        if (child != NULL) {
            size_t candidate_height;
            if (child->level >= level ||
                d7_mst_add_overflows(count, child->count, &count) ||
                d7_mst_add_overflows(block_count, child->block_count, &block_count) ||
                d7_mst_add_overflows(child->height, 1, &candidate_height)) {
                return D7_MERKLE_OVERFLOW;
            }
            if (candidate_height > height) {
                height = candidate_height;
            }
        }
    }
    status = d7_mst_encode_block(
        policy,
        level,
        count,
        entries,
        entry_count,
        children,
        &block_bytes,
        &digest);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    node = (struct d7_merkle_node *)d7_mst_allocate(policy, allocation_size);
    if (node == NULL) {
        d7_mst_bytes_release(policy, block_bytes);
        return D7_MERKLE_NO_MEMORY;
    }
    d7_mst_ref_init(&node->refs);
    node->release_next = NULL;
    node->level = (unsigned char)level;
    node->entry_count = entry_count;
    node->count = count;
    node->height = height;
    node->block_count = block_count;
    node->minimum_entry = children[0] == NULL
        ? entries[0]
        : children[0]->minimum_entry;
    node->maximum_entry = children[entry_count] == NULL
        ? entries[entry_count - 1]
        : children[entry_count]->maximum_entry;
    node->block_bytes = block_bytes;
    node->digest = digest;
    stored_entries = d7_mst_node_entries(node);
    stored_children = d7_mst_node_children(node);
    for (index = 0; index != entry_count; ++index) {
        stored_entries[index] = entries[index];
        d7_mst_entry_retain(entries[index]);
    }
    for (index = 0; index != entry_count + 1; ++index) {
        stored_children[index] = children[index];
        d7_mst_node_retain(children[index]);
    }
    *result = node;
    return D7_MERKLE_OK;
}

static bool d7_mst_tree_valid(const d7_merkle_search_tree *tree) {
    return tree != NULL && tree->policy != NULL;
}

static d7_merkle_status d7_mst_key_compare(
    const struct d7_merkle_policy_rep *policy,
    const void *left,
    const void *right,
    int *comparison) {
    return policy->config.key_compare(
        left,
        right,
        comparison,
        policy->config.key_compare_context);
}

static d7_merkle_status d7_mst_values_equal(
    const struct d7_merkle_policy_rep *policy,
    const d7_mst_entry *left,
    const d7_mst_entry *right,
    bool *equal) {
    if (left->value_bytes->size == right->value_bytes->size &&
        memcmp(left->value_bytes->data, right->value_bytes->data, left->value_bytes->size) == 0) {
        *equal = true;
        return D7_MERKLE_OK;
    }
    if (policy->config.value_type.equals == NULL) {
        *equal = false;
        return D7_MERKLE_OK;
    }
    return policy->config.value_type.equals(
        left->value->value,
        right->value->value,
        equal,
        policy->config.value_type.context);
}

static d7_merkle_status d7_mst_find_position(
    const struct d7_merkle_policy_rep *policy,
    d7_mst_entry *const *entries,
    size_t entry_count,
    const void *key,
    size_t *position,
    bool *found) {
    size_t low = 0;
    size_t high = entry_count;
    d7_merkle_status status;
    while (low < high) {
        const size_t middle = low + (high - low) / 2;
        int comparison = 0;
        status = d7_mst_key_compare(
            policy,
            entries[middle]->key->value,
            key,
            &comparison);
        if (status != D7_MERKLE_OK) {
            return status;
        }
        if (comparison < 0) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    *found = false;
    if (low < entry_count) {
        int comparison = 0;
        status = d7_mst_key_compare(
            policy,
            entries[low]->key->value,
            key,
            &comparison);
        if (status != D7_MERKLE_OK) {
            return status;
        }
        *found = comparison == 0;
    }
    *position = low;
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_find_node(
    const d7_merkle_search_tree *tree,
    const void *key,
    struct d7_merkle_node **node,
    size_t *entry_index) {
    struct d7_merkle_node *cursor = tree->root;
    while (cursor != NULL) {
        d7_mst_entry *const *entries = d7_mst_node_entries_const(cursor);
        struct d7_merkle_node *const *children = d7_mst_node_children_const(cursor);
        size_t position = 0;
        bool found = false;
        d7_merkle_status status = d7_mst_find_position(
            tree->policy,
            entries,
            cursor->entry_count,
            key,
            &position,
            &found);
        if (status != D7_MERKLE_OK) {
            return status;
        }
        if (found) {
            *node = cursor;
            *entry_index = position;
            return D7_MERKLE_OK;
        }
        cursor = children[position];
    }
    *node = NULL;
    *entry_index = 0;
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_node_rebuild(
    const struct d7_merkle_policy_rep *policy,
    const struct d7_merkle_node *source,
    size_t entry_index,
    d7_mst_entry *entry,
    size_t child_index,
    struct d7_merkle_node *child,
    struct d7_merkle_node **result) {
    d7_mst_entry **entries = NULL;
    struct d7_merkle_node **children = NULL;
    d7_merkle_status status = d7_mst_allocate_pointer_array(
        policy,
        source->entry_count,
        sizeof(*entries),
        (void **)&entries);
    if (status == D7_MERKLE_OK) {
        status = d7_mst_allocate_pointer_array(
            policy,
            source->entry_count + 1,
            sizeof(*children),
            (void **)&children);
    }
    if (status == D7_MERKLE_OK &&
        (source->entry_count == 0 || entries == NULL || children == NULL)) {
        status = D7_MERKLE_INCONSISTENT_POLICY;
    }
    if (status == D7_MERKLE_OK) {
        memcpy(
            entries,
            d7_mst_node_entries_const(source),
            source->entry_count * sizeof(*entries));
        memcpy(
            children,
            d7_mst_node_children_const(source),
            (source->entry_count + 1) * sizeof(*children));
        if (entry_index != SIZE_MAX) {
            entries[entry_index] = entry;
        }
        if (child_index != SIZE_MAX) {
            children[child_index] = child;
        }
        status = d7_mst_node_create(
            policy,
            source->level,
            entries,
            source->entry_count,
            children,
            result);
    }
    d7_mst_deallocate(policy, children);
    d7_mst_deallocate(policy, entries);
    return status;
}

static d7_merkle_status d7_mst_create_or_collapse(
    const struct d7_merkle_policy_rep *policy,
    unsigned level,
    d7_mst_entry *const *entries,
    size_t entry_count,
    struct d7_merkle_node *const *children,
    struct d7_merkle_node **result) {
    if (entry_count == 0) {
        d7_mst_node_retain(children[0]);
        *result = children[0];
        return D7_MERKLE_OK;
    }
    return d7_mst_node_create(
        policy,
        level,
        entries,
        entry_count,
        children,
        result);
}

static d7_merkle_status d7_mst_build_canonical(
    const struct d7_merkle_policy_rep *policy,
    d7_mst_entry *const *ordered,
    size_t count,
    struct d7_merkle_node **result) {
    unsigned maximum_level = 0;
    size_t separator_count = 0;
    size_t segment_start = 0;
    size_t entry_index = 0;
    size_t index;
    d7_mst_entry **entries = NULL;
    struct d7_merkle_node **children = NULL;
    d7_merkle_status status = D7_MERKLE_OK;
    if (count == 0) {
        *result = NULL;
        return D7_MERKLE_OK;
    }
    for (index = 0; index != count; ++index) {
        if (ordered[index]->level > maximum_level) {
            maximum_level = ordered[index]->level;
        }
    }
    for (index = 0; index != count; ++index) {
        separator_count += ordered[index]->level == maximum_level ? 1u : 0u;
    }
    status = d7_mst_allocate_pointer_array(
        policy,
        separator_count,
        sizeof(*entries),
        (void **)&entries);
    if (status == D7_MERKLE_OK) {
        status = d7_mst_allocate_pointer_array(
            policy,
            separator_count + 1,
            sizeof(*children),
            (void **)&children);
    }
    if (status != D7_MERKLE_OK) {
        goto cleanup;
    }
    if (entries == NULL || children == NULL) {
        status = D7_MERKLE_NO_MEMORY;
        goto cleanup;
    }
    memset(children, 0, (separator_count + 1) * sizeof(*children));
    for (index = 0; index != count; ++index) {
        if (ordered[index]->level != maximum_level) {
            continue;
        }
        status = d7_mst_build_canonical(
            policy,
            ordered + segment_start,
            index - segment_start,
            &children[entry_index]);
        if (status != D7_MERKLE_OK) {
            goto cleanup;
        }
        entries[entry_index++] = ordered[index];
        segment_start = index + 1;
    }
    status = d7_mst_build_canonical(
        policy,
        ordered + segment_start,
        count - segment_start,
        &children[separator_count]);
    if (status == D7_MERKLE_OK) {
        status = d7_mst_node_create(
            policy,
            maximum_level,
            entries,
            separator_count,
            children,
            result);
    }

cleanup:
    if (children != NULL) {
        for (index = 0; index != separator_count + 1; ++index) {
            d7_mst_node_release(policy, children[index]);
        }
    }
    d7_mst_deallocate(policy, children);
    d7_mst_deallocate(policy, entries);
    return status;
}

static d7_merkle_status d7_mst_sort_pending(
    const struct d7_merkle_policy_rep *policy,
    d7_mst_pending_entry *entries,
    d7_mst_pending_entry *scratch,
    size_t count,
    d7_mst_pending_entry **sorted) {
    d7_mst_pending_entry *source = entries;
    d7_mst_pending_entry *target = scratch;
    size_t width = 1;
    while (width < count) {
        size_t start = 0;
        while (start < count) {
            const size_t middle = width > count - start
                ? count
                : start + width;
            const size_t remaining = count - middle;
            const size_t end = width > remaining
                ? count
                : middle + width;
            size_t left = start;
            size_t right = middle;
            size_t output = start;
            while (left != middle && right != end) {
                int comparison = 0;
                d7_merkle_status status = d7_mst_key_compare(
                    policy,
                    source[left].key,
                    source[right].key,
                    &comparison);
                if (status != D7_MERKLE_OK) {
                    return status;
                }
                target[output++] = comparison <= 0
                    ? source[left++]
                    : source[right++];
            }
            while (left != middle) {
                target[output++] = source[left++];
            }
            while (right != end) {
                target[output++] = source[right++];
            }
            start = end;
        }
        {
            d7_mst_pending_entry *swap = source;
            source = target;
            target = swap;
        }
        if (width > count / 2) {
            width = count;
        } else {
            width *= 2;
        }
    }
    *sorted = source;
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_split(
    const struct d7_merkle_policy_rep *policy,
    const struct d7_merkle_node *node,
    const void *key,
    struct d7_merkle_node **left_result,
    struct d7_merkle_node **right_result) {
    size_t position = 0;
    bool found = false;
    d7_mst_entry **left_entries = NULL;
    d7_mst_entry **right_entries = NULL;
    struct d7_merkle_node **left_children = NULL;
    struct d7_merkle_node **right_children = NULL;
    struct d7_merkle_node *child_left = NULL;
    struct d7_merkle_node *child_right = NULL;
    struct d7_merkle_node *left = NULL;
    struct d7_merkle_node *right = NULL;
    d7_mst_entry *const *source_entries;
    struct d7_merkle_node *const *source_children;
    size_t right_count;
    d7_merkle_status status;
    if (node == NULL) {
        *left_result = NULL;
        *right_result = NULL;
        return D7_MERKLE_OK;
    }
    source_entries = d7_mst_node_entries_const(node);
    source_children = d7_mst_node_children_const(node);
    status = d7_mst_find_position(
        policy,
        source_entries,
        node->entry_count,
        key,
        &position,
        &found);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    if (found) {
        return D7_MERKLE_INCONSISTENT_POLICY;
    }
    status = d7_mst_split(
        policy,
        source_children[position],
        key,
        &child_left,
        &child_right);
    if (status != D7_MERKLE_OK) {
        goto cleanup;
    }
    right_count = node->entry_count - position;
    status = d7_mst_allocate_pointer_array(
        policy,
        position,
        sizeof(*left_entries),
        (void **)&left_entries);
    if (status == D7_MERKLE_OK) {
        status = d7_mst_allocate_pointer_array(
            policy,
            position + 1,
            sizeof(*left_children),
            (void **)&left_children);
    }
    if (status == D7_MERKLE_OK) {
        status = d7_mst_allocate_pointer_array(
            policy,
            right_count,
            sizeof(*right_entries),
            (void **)&right_entries);
    }
    if (status == D7_MERKLE_OK) {
        status = d7_mst_allocate_pointer_array(
            policy,
            right_count + 1,
            sizeof(*right_children),
            (void **)&right_children);
    }
    if (status != D7_MERKLE_OK) {
        goto cleanup;
    }
    if ((position != 0 && left_entries == NULL) || left_children == NULL ||
        (right_count != 0 && right_entries == NULL) || right_children == NULL) {
        status = D7_MERKLE_NO_MEMORY;
        goto cleanup;
    }
    if (position != 0) {
        memcpy(left_entries, source_entries, position * sizeof(*left_entries));
        memcpy(left_children, source_children, position * sizeof(*left_children));
    }
    left_children[position] = child_left;
    if (right_count != 0) {
        memcpy(
            right_entries,
            source_entries + position,
            right_count * sizeof(*right_entries));
        memcpy(
            right_children + 1,
            source_children + position + 1,
            right_count * sizeof(*right_children));
    }
    right_children[0] = child_right;
    status = d7_mst_create_or_collapse(
        policy,
        node->level,
        left_entries,
        position,
        left_children,
        &left);
    if (status == D7_MERKLE_OK) {
        status = d7_mst_create_or_collapse(
            policy,
            node->level,
            right_entries,
            right_count,
            right_children,
            &right);
    }
    if (status == D7_MERKLE_OK) {
        *left_result = left;
        *right_result = right;
        left = NULL;
        right = NULL;
    }

cleanup:
    d7_mst_node_release(policy, right);
    d7_mst_node_release(policy, left);
    d7_mst_node_release(policy, child_right);
    d7_mst_node_release(policy, child_left);
    d7_mst_deallocate(policy, right_children);
    d7_mst_deallocate(policy, right_entries);
    d7_mst_deallocate(policy, left_children);
    d7_mst_deallocate(policy, left_entries);
    return status;
}

static d7_merkle_status d7_mst_insert(
    const struct d7_merkle_policy_rep *policy,
    const struct d7_merkle_node *node,
    d7_mst_entry *entry,
    struct d7_merkle_node **result) {
    d7_mst_entry *singleton_entries[1] = {entry};
    struct d7_merkle_node *singleton_children[2] = {NULL, NULL};
    size_t position = 0;
    bool found = false;
    d7_merkle_status status;
    if (node == NULL) {
        return d7_mst_node_create(
            policy,
            entry->level,
            singleton_entries,
            1,
            singleton_children,
            result);
    }
    if (entry->level > node->level) {
        struct d7_merkle_node *left = NULL;
        struct d7_merkle_node *right = NULL;
        status = d7_mst_split(policy, node, entry->key->value, &left, &right);
        if (status == D7_MERKLE_OK) {
            singleton_children[0] = left;
            singleton_children[1] = right;
            status = d7_mst_node_create(
                policy,
                entry->level,
                singleton_entries,
                1,
                singleton_children,
                result);
        }
        d7_mst_node_release(policy, right);
        d7_mst_node_release(policy, left);
        return status;
    }
    status = d7_mst_find_position(
        policy,
        d7_mst_node_entries_const(node),
        node->entry_count,
        entry->key->value,
        &position,
        &found);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    if (found) {
        return D7_MERKLE_INCONSISTENT_POLICY;
    }
    if (entry->level < node->level) {
        struct d7_merkle_node *child = NULL;
        status = d7_mst_insert(
            policy,
            d7_mst_node_children_const(node)[position],
            entry,
            &child);
        if (status == D7_MERKLE_OK) {
            status = d7_mst_node_rebuild(
                policy,
                node,
                SIZE_MAX,
                NULL,
                position,
                child,
                result);
        }
        d7_mst_node_release(policy, child);
        return status;
    }
    {
        d7_mst_entry **entries = NULL;
        struct d7_merkle_node **children = NULL;
        struct d7_merkle_node *left = NULL;
        struct d7_merkle_node *right = NULL;
        d7_mst_entry *const *source_entries = d7_mst_node_entries_const(node);
        struct d7_merkle_node *const *source_children = d7_mst_node_children_const(node);
        status = d7_mst_split(
            policy,
            source_children[position],
            entry->key->value,
            &left,
            &right);
        if (status == D7_MERKLE_OK) {
            status = d7_mst_allocate_pointer_array(
                policy,
                node->entry_count + 1,
                sizeof(*entries),
                (void **)&entries);
        }
        if (status == D7_MERKLE_OK) {
            status = d7_mst_allocate_pointer_array(
                policy,
                node->entry_count + 2,
                sizeof(*children),
                (void **)&children);
        }
        if (status == D7_MERKLE_OK && (entries == NULL || children == NULL)) {
            status = D7_MERKLE_NO_MEMORY;
        }
        if (status == D7_MERKLE_OK) {
            if (position != 0) {
                memcpy(entries, source_entries, position * sizeof(*entries));
                memcpy(children, source_children, position * sizeof(*children));
            }
            entries[position] = entry;
            memcpy(
                entries + position + 1,
                source_entries + position,
                (node->entry_count - position) * sizeof(*entries));
            children[position] = left;
            children[position + 1] = right;
            memcpy(
                children + position + 2,
                source_children + position + 1,
                (node->entry_count - position) * sizeof(*children));
            status = d7_mst_node_create(
                policy,
                node->level,
                entries,
                node->entry_count + 1,
                children,
                result);
        }
        d7_mst_deallocate(policy, children);
        d7_mst_deallocate(policy, entries);
        d7_mst_node_release(policy, right);
        d7_mst_node_release(policy, left);
        return status;
    }
}

static d7_merkle_status d7_mst_update_value(
    const struct d7_merkle_policy_rep *policy,
    const struct d7_merkle_node *node,
    const void *key,
    d7_mst_entry *replacement,
    struct d7_merkle_node **result) {
    size_t position = 0;
    bool found = false;
    d7_merkle_status status = d7_mst_find_position(
        policy,
        d7_mst_node_entries_const(node),
        node->entry_count,
        key,
        &position,
        &found);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    if (found) {
        return d7_mst_node_rebuild(
            policy,
            node,
            position,
            replacement,
            SIZE_MAX,
            NULL,
            result);
    }
    {
        struct d7_merkle_node *child = NULL;
        struct d7_merkle_node *source_child = d7_mst_node_children_const(node)[position];
        if (source_child == NULL) {
            return D7_MERKLE_INCONSISTENT_POLICY;
        }
        status = d7_mst_update_value(
            policy,
            source_child,
            key,
            replacement,
            &child);
        if (status == D7_MERKLE_OK) {
            status = d7_mst_node_rebuild(
                policy,
                node,
                SIZE_MAX,
                NULL,
                position,
                child,
                result);
        }
        d7_mst_node_release(policy, child);
        return status;
    }
}

static d7_merkle_status d7_mst_join(
    const struct d7_merkle_policy_rep *policy,
    const struct d7_merkle_node *left,
    const struct d7_merkle_node *right,
    struct d7_merkle_node **result) {
    if (left == NULL || right == NULL) {
        struct d7_merkle_node *retained = (struct d7_merkle_node *)(left == NULL ? right : left);
        d7_mst_node_retain(retained);
        *result = retained;
        return D7_MERKLE_OK;
    }
    if (left->level > right->level) {
        struct d7_merkle_node *joined = NULL;
        d7_merkle_status status = d7_mst_join(
            policy,
            d7_mst_node_children_const(left)[left->entry_count],
            right,
            &joined);
        if (status == D7_MERKLE_OK) {
            status = d7_mst_node_rebuild(
                policy,
                left,
                SIZE_MAX,
                NULL,
                left->entry_count,
                joined,
                result);
        }
        d7_mst_node_release(policy, joined);
        return status;
    }
    if (left->level < right->level) {
        struct d7_merkle_node *joined = NULL;
        d7_merkle_status status = d7_mst_join(
            policy,
            left,
            d7_mst_node_children_const(right)[0],
            &joined);
        if (status == D7_MERKLE_OK) {
            status = d7_mst_node_rebuild(
                policy,
                right,
                SIZE_MAX,
                NULL,
                0,
                joined,
                result);
        }
        d7_mst_node_release(policy, joined);
        return status;
    }
    {
        const size_t entry_count = left->entry_count + right->entry_count;
        d7_mst_entry **entries = NULL;
        struct d7_merkle_node **children = NULL;
        struct d7_merkle_node *middle = NULL;
        d7_merkle_status status;
        if (entry_count < left->entry_count) {
            return D7_MERKLE_OVERFLOW;
        }
        status = d7_mst_join(
            policy,
            d7_mst_node_children_const(left)[left->entry_count],
            d7_mst_node_children_const(right)[0],
            &middle);
        if (status == D7_MERKLE_OK) {
            status = d7_mst_allocate_pointer_array(
                policy,
                entry_count,
                sizeof(*entries),
                (void **)&entries);
        }
        if (status == D7_MERKLE_OK) {
            status = d7_mst_allocate_pointer_array(
                policy,
                entry_count + 1,
                sizeof(*children),
                (void **)&children);
        }
        if (status == D7_MERKLE_OK && (entries == NULL || children == NULL)) {
            status = D7_MERKLE_NO_MEMORY;
        }
        if (status == D7_MERKLE_OK) {
            memcpy(
                entries,
                d7_mst_node_entries_const(left),
                left->entry_count * sizeof(*entries));
            memcpy(
                entries + left->entry_count,
                d7_mst_node_entries_const(right),
                right->entry_count * sizeof(*entries));
            memcpy(
                children,
                d7_mst_node_children_const(left),
                left->entry_count * sizeof(*children));
            children[left->entry_count] = middle;
            memcpy(
                children + left->entry_count + 1,
                d7_mst_node_children_const(right) + 1,
                right->entry_count * sizeof(*children));
            status = d7_mst_node_create(
                policy,
                left->level,
                entries,
                entry_count,
                children,
                result);
        }
        d7_mst_deallocate(policy, children);
        d7_mst_deallocate(policy, entries);
        d7_mst_node_release(policy, middle);
        return status;
    }
}

static d7_merkle_status d7_mst_remove_node(
    const struct d7_merkle_policy_rep *policy,
    const struct d7_merkle_node *node,
    const void *key,
    struct d7_merkle_node **result) {
    size_t position = 0;
    bool found = false;
    d7_merkle_status status = d7_mst_find_position(
        policy,
        d7_mst_node_entries_const(node),
        node->entry_count,
        key,
        &position,
        &found);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    if (!found) {
        struct d7_merkle_node *child = NULL;
        const struct d7_merkle_node *source_child = d7_mst_node_children_const(node)[position];
        if (source_child == NULL) {
            return D7_MERKLE_INCONSISTENT_POLICY;
        }
        status = d7_mst_remove_node(policy, source_child, key, &child);
        if (status == D7_MERKLE_OK) {
            status = d7_mst_node_rebuild(
                policy,
                node,
                SIZE_MAX,
                NULL,
                position,
                child,
                result);
        }
        d7_mst_node_release(policy, child);
        return status;
    }
    {
        const size_t new_entry_count = node->entry_count - 1;
        d7_mst_entry **entries = NULL;
        struct d7_merkle_node **children = NULL;
        struct d7_merkle_node *joined = NULL;
        d7_mst_entry *const *source_entries = d7_mst_node_entries_const(node);
        struct d7_merkle_node *const *source_children = d7_mst_node_children_const(node);
        status = d7_mst_join(
            policy,
            source_children[position],
            source_children[position + 1],
            &joined);
        if (status == D7_MERKLE_OK) {
            status = d7_mst_allocate_pointer_array(
                policy,
                new_entry_count,
                sizeof(*entries),
                (void **)&entries);
        }
        if (status == D7_MERKLE_OK) {
            status = d7_mst_allocate_pointer_array(
                policy,
                new_entry_count + 1,
                sizeof(*children),
                (void **)&children);
        }
        if (status == D7_MERKLE_OK &&
            ((new_entry_count != 0 && entries == NULL) || children == NULL)) {
            status = D7_MERKLE_NO_MEMORY;
        }
        if (status == D7_MERKLE_OK) {
            if (position != 0) {
                if (entries == NULL || children == NULL) {
                    status = D7_MERKLE_NO_MEMORY;
                    goto remove_cleanup;
                }
                memcpy(entries, source_entries, position * sizeof(*entries));
                memcpy(children, source_children, position * sizeof(*children));
            }
            if (new_entry_count != position) {
                memcpy(
                    entries + position,
                    source_entries + position + 1,
                    (new_entry_count - position) * sizeof(*entries));
            }
            children[position] = joined;
            memcpy(
                children + position + 1,
                source_children + position + 2,
                (new_entry_count - position) * sizeof(*children));
            status = d7_mst_create_or_collapse(
                policy,
                node->level,
                entries,
                new_entry_count,
                children,
                result);
        }
remove_cleanup:
        d7_mst_deallocate(policy, children);
        d7_mst_deallocate(policy, entries);
        d7_mst_node_release(policy, joined);
        return status;
    }
}

static void d7_mst_publish_tree(
    const d7_merkle_search_tree *source,
    d7_merkle_search_tree *destination,
    d7_merkle_search_tree produced) {
    if (source == destination) {
        d7_merkle_search_tree old = *destination;
        *destination = produced;
        d7_merkle_search_tree_dispose(&old);
    } else {
        *destination = produced;
    }
}

static d7_merkle_search_tree d7_mst_adopt_tree(
    struct d7_merkle_policy_rep *policy,
    struct d7_merkle_node *root) {
    d7_merkle_search_tree result;
    d7_mst_policy_retain(policy);
    result.policy = policy;
    result.root = root;
    return result;
}

d7_merkle_status d7_merkle_search_tree_init(
    d7_merkle_search_tree *tree,
    const d7_merkle_policy *policy) {
    if (tree == NULL || policy == NULL || policy->rep == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    *tree = d7_mst_adopt_tree(policy->rep, NULL);
    return D7_MERKLE_OK;
}

d7_merkle_status d7_merkle_search_tree_from_array(
    d7_merkle_search_tree *tree,
    const d7_merkle_policy *policy,
    const d7_merkle_search_input *inputs,
    size_t input_count) {
    d7_mst_pending_entry *pending = NULL;
    d7_mst_pending_entry *scratch = NULL;
    d7_mst_pending_entry *sorted = NULL;
    d7_mst_entry **entries = NULL;
    size_t entry_count = 0;
    struct d7_merkle_node *root = NULL;
    size_t index;
    d7_merkle_status status;
    if (tree == NULL || policy == NULL || policy->rep == NULL ||
        (input_count != 0 && inputs == NULL)) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    if (input_count == 0) {
        *tree = d7_mst_adopt_tree(policy->rep, NULL);
        return D7_MERKLE_OK;
    }
    status = d7_mst_allocate_pointer_array(
        policy->rep,
        input_count,
        sizeof(*pending),
        (void **)&pending);
    if (status == D7_MERKLE_OK) {
        status = d7_mst_allocate_pointer_array(
            policy->rep,
            input_count,
            sizeof(*scratch),
            (void **)&scratch);
    }
    if (status == D7_MERKLE_OK) {
        status = d7_mst_allocate_pointer_array(
            policy->rep,
            input_count,
            sizeof(*entries),
            (void **)&entries);
    }
    if (status != D7_MERKLE_OK) {
        goto cleanup;
    }
    memset(entries, 0, input_count * sizeof(*entries));
    for (index = 0; index != input_count; ++index) {
        if (inputs[index].key == NULL || inputs[index].value == NULL) {
            status = D7_MERKLE_INVALID_ARGUMENT;
            goto cleanup;
        }
        pending[index] = (d7_mst_pending_entry){
            inputs[index].key,
            inputs[index].value,
            index};
    }
    status = d7_mst_sort_pending(
        policy->rep,
        pending,
        scratch,
        input_count,
        &sorted);
    if (status != D7_MERKLE_OK) {
        goto cleanup;
    }
    index = 0;
    while (index != input_count) {
        size_t end = index + 1;
        while (end != input_count) {
            int comparison = 0;
            status = d7_mst_key_compare(
                policy->rep,
                sorted[index].key,
                sorted[end].key,
                &comparison);
            if (status != D7_MERKLE_OK) {
                goto cleanup;
            }
            if (comparison != 0) {
                break;
            }
            ++end;
        }
        status = d7_mst_entry_create(
            policy->rep,
            sorted[index].key,
            sorted[end - 1].value,
            &entries[entry_count]);
        if (status != D7_MERKLE_OK) {
            goto cleanup;
        }
        ++entry_count;
        index = end;
    }
    status = d7_mst_build_canonical(policy->rep, entries, entry_count, &root);
    if (status == D7_MERKLE_OK) {
        *tree = d7_mst_adopt_tree(policy->rep, root);
        root = NULL;
    }

cleanup:
    d7_mst_node_release(policy->rep, root);
    if (entries != NULL) {
        for (index = 0; index != entry_count; ++index) {
            d7_mst_entry_release(policy->rep, entries[index]);
        }
    }
    d7_mst_deallocate(policy->rep, entries);
    d7_mst_deallocate(policy->rep, scratch);
    d7_mst_deallocate(policy->rep, pending);
    return status;
}

d7_merkle_status d7_merkle_search_tree_copy(
    const d7_merkle_search_tree *source,
    d7_merkle_search_tree *destination) {
    if (!d7_mst_tree_valid(source) || destination == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    if (source != destination) {
        d7_mst_policy_retain(source->policy);
        d7_mst_node_retain(source->root);
        destination->policy = source->policy;
        destination->root = source->root;
    }
    return D7_MERKLE_OK;
}

void d7_merkle_search_tree_move(
    d7_merkle_search_tree *destination,
    d7_merkle_search_tree *source) {
    if (destination != NULL && source != NULL && destination != source) {
        *destination = *source;
        memset(source, 0, sizeof(*source));
    }
}

void d7_merkle_search_tree_dispose(d7_merkle_search_tree *tree) {
    if (tree != NULL) {
        if (tree->policy != NULL) {
            d7_mst_node_release(tree->policy, tree->root);
            d7_mst_policy_release(tree->policy);
        }
        memset(tree, 0, sizeof(*tree));
    }
}

bool d7_merkle_search_tree_empty(const d7_merkle_search_tree *tree) {
    return d7_mst_tree_valid(tree) && tree->root == NULL;
}

size_t d7_merkle_search_tree_size(const d7_merkle_search_tree *tree) {
    return !d7_mst_tree_valid(tree) || tree->root == NULL ? 0 : tree->root->count;
}

size_t d7_merkle_search_tree_height(const d7_merkle_search_tree *tree) {
    return !d7_mst_tree_valid(tree) || tree->root == NULL ? 0 : tree->root->height;
}

size_t d7_merkle_search_tree_block_count(const d7_merkle_search_tree *tree) {
    return !d7_mst_tree_valid(tree) || tree->root == NULL ? 0 : tree->root->block_count;
}

d7_merkle_digest d7_merkle_search_tree_root_hash(const d7_merkle_search_tree *tree) {
    d7_merkle_digest result = {{0}};
    if (d7_mst_tree_valid(tree)) {
        result = tree->root == NULL ? tree->policy->empty_digest : tree->root->digest;
    }
    return result;
}

d7_merkle_status d7_merkle_search_tree_contains_key(
    const d7_merkle_search_tree *tree,
    const void *key,
    bool *found) {
    struct d7_merkle_node *node = NULL;
    size_t entry_index = 0;
    d7_merkle_status status;
    if (!d7_mst_tree_valid(tree) || key == NULL || found == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    status = d7_mst_find_node(tree, key, &node, &entry_index);
    if (status == D7_MERKLE_OK) {
        *found = node != NULL;
    }
    return status;
}

d7_merkle_status d7_merkle_search_tree_try_get_entry_ref(
    const d7_merkle_search_tree *tree,
    const void *key,
    bool *found,
    d7_merkle_search_entry_ref *entry) {
    struct d7_merkle_node *node = NULL;
    size_t entry_index = 0;
    d7_merkle_status status;
    if (!d7_mst_tree_valid(tree) || key == NULL || found == NULL || entry == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    status = d7_mst_find_node(tree, key, &node, &entry_index);
    if (status == D7_MERKLE_OK) {
        *found = node != NULL;
        if (node == NULL) {
            memset(entry, 0, sizeof(*entry));
        } else {
            *entry = d7_mst_entry_ref(d7_mst_node_entries(node)[entry_index]);
        }
    }
    return status;
}

d7_merkle_status d7_merkle_search_tree_set(
    const d7_merkle_search_tree *tree,
    const void *key,
    const void *value,
    d7_merkle_search_tree *result) {
    struct d7_merkle_node *found_node = NULL;
    size_t found_index = 0;
    d7_mst_entry *entry = NULL;
    struct d7_merkle_node *root = NULL;
    d7_merkle_status status;
    if (!d7_mst_tree_valid(tree) || key == NULL || value == NULL || result == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    status = d7_mst_find_node(tree, key, &found_node, &found_index);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    if (found_node != NULL) {
        bool changed = false;
        status = d7_mst_entry_replace_value(
            tree->policy,
            d7_mst_node_entries(found_node)[found_index],
            value,
            &changed,
            &entry);
        if (status == D7_MERKLE_OK && !changed) {
            d7_mst_node_retain(tree->root);
            root = tree->root;
        } else if (status == D7_MERKLE_OK) {
            status = d7_mst_update_value(tree->policy, tree->root, key, entry, &root);
        }
    } else {
        status = d7_mst_entry_create(tree->policy, key, value, &entry);
        if (status == D7_MERKLE_OK) {
            status = d7_mst_insert(tree->policy, tree->root, entry, &root);
        }
    }
    if (status == D7_MERKLE_OK) {
        const d7_merkle_search_tree produced = d7_mst_adopt_tree(tree->policy, root);
        root = NULL;
        d7_mst_publish_tree(tree, result, produced);
    }
    d7_mst_node_release(tree->policy, root);
    d7_mst_entry_release(tree->policy, entry);
    return status;
}

d7_merkle_status d7_merkle_search_tree_remove(
    const d7_merkle_search_tree *tree,
    const void *key,
    d7_merkle_search_tree *result) {
    struct d7_merkle_node *found_node = NULL;
    size_t found_index = 0;
    struct d7_merkle_node *root = NULL;
    d7_merkle_status status;
    if (!d7_mst_tree_valid(tree) || key == NULL || result == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    status = d7_mst_find_node(tree, key, &found_node, &found_index);
    (void)found_index;
    if (status == D7_MERKLE_OK && found_node == NULL) {
        d7_mst_node_retain(tree->root);
        root = tree->root;
    } else if (status == D7_MERKLE_OK) {
        status = d7_mst_remove_node(tree->policy, tree->root, key, &root);
    }
    if (status == D7_MERKLE_OK) {
        const d7_merkle_search_tree produced = d7_mst_adopt_tree(tree->policy, root);
        root = NULL;
        d7_mst_publish_tree(tree, result, produced);
    }
    d7_mst_node_release(tree->policy, root);
    return status;
}

d7_merkle_status d7_merkle_search_tree_clear(
    const d7_merkle_search_tree *tree,
    d7_merkle_search_tree *result) {
    struct d7_merkle_node *root = NULL;
    if (!d7_mst_tree_valid(tree) || result == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    if (tree->root == NULL) {
        d7_mst_node_retain(tree->root);
        root = tree->root;
    }
    {
        const d7_merkle_search_tree produced = d7_mst_adopt_tree(tree->policy, root);
        d7_mst_publish_tree(tree, result, produced);
    }
    return D7_MERKLE_OK;
}

static bool d7_mst_cursor_valid(const d7_merkle_search_tree_cursor *cursor) {
    return cursor != NULL && d7_mst_tree_valid(&cursor->tree) &&
        cursor->position <= d7_merkle_search_tree_size(&cursor->tree);
}

static d7_merkle_status d7_mst_add_cursor_rank(
    size_t *rank,
    size_t increment) {
    size_t result = 0;
    if (d7_mst_add_overflows(*rank, increment, &result)) {
        return D7_MERKLE_OVERFLOW;
    }
    *rank = result;
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_cursor_lower_bound_rank(
    const d7_merkle_search_tree *tree,
    const void *key,
    size_t *rank,
    bool *found) {
    const struct d7_merkle_node *node = tree->root;
    size_t result = 0;
    while (node != NULL) {
        d7_mst_entry *const *entries = d7_mst_node_entries_const(node);
        struct d7_merkle_node *const *children = d7_mst_node_children_const(node);
        size_t position = 0;
        size_t index;
        bool hit = false;
        d7_merkle_status status = d7_mst_find_position(
            tree->policy,
            entries,
            node->entry_count,
            key,
            &position,
            &hit);
        if (status != D7_MERKLE_OK) {
            return status;
        }
        for (index = 0; index != position; ++index) {
            const size_t child_count = children[index] == NULL ? 0 : children[index]->count;
            status = d7_mst_add_cursor_rank(&result, child_count);
            if (status == D7_MERKLE_OK) {
                status = d7_mst_add_cursor_rank(&result, 1);
            }
            if (status != D7_MERKLE_OK) {
                return status;
            }
        }
        if (hit) {
            const size_t child_count = children[position] == NULL
                ? 0
                : children[position]->count;
            status = d7_mst_add_cursor_rank(&result, child_count);
            if (status != D7_MERKLE_OK) {
                return status;
            }
            *rank = result;
            *found = true;
            return D7_MERKLE_OK;
        }
        node = children[position];
    }
    *rank = result;
    *found = false;
    return D7_MERKLE_OK;
}

static const d7_mst_entry *d7_mst_cursor_entry_at_rank(
    const d7_merkle_search_tree *tree,
    size_t rank) {
    const struct d7_merkle_node *node = tree->root;
    if (rank >= d7_merkle_search_tree_size(tree)) {
        return NULL;
    }
    while (node != NULL) {
        d7_mst_entry *const *entries = d7_mst_node_entries_const(node);
        struct d7_merkle_node *const *children = d7_mst_node_children_const(node);
        size_t index;
        bool descended = false;
        for (index = 0; index != node->entry_count; ++index) {
            const size_t child_count = children[index] == NULL ? 0 : children[index]->count;
            if (rank < child_count) {
                node = children[index];
                descended = true;
                break;
            }
            rank -= child_count;
            if (rank == 0) {
                return entries[index];
            }
            --rank;
        }
        if (!descended) {
            node = children[node->entry_count];
        }
    }
    return NULL;
}

static void d7_mst_cursor_publish(
    const d7_merkle_search_tree_cursor *source,
    d7_merkle_search_tree_cursor *result,
    d7_merkle_search_tree tree,
    size_t position) {
    const d7_merkle_search_tree_cursor produced = {tree, position};
    if (source == result) {
        d7_merkle_search_tree_cursor old = *result;
        *result = produced;
        d7_merkle_search_tree_dispose(&old.tree);
    } else {
        *result = produced;
    }
}

static d7_merkle_status d7_mst_cursor_reposition(
    const d7_merkle_search_tree_cursor *cursor,
    size_t position,
    d7_merkle_search_tree_cursor *result) {
    d7_merkle_search_tree tree = {0};
    d7_merkle_status status;
    if (!d7_mst_cursor_valid(cursor) || result == NULL ||
        position > d7_merkle_search_tree_size(&cursor->tree)) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    status = d7_merkle_search_tree_copy(&cursor->tree, &tree);
    if (status == D7_MERKLE_OK) {
        d7_mst_cursor_publish(cursor, result, tree, position);
    }
    return status;
}

d7_merkle_status d7_merkle_search_tree_cursor_create(
    const d7_merkle_search_tree *tree,
    size_t position,
    d7_merkle_search_tree_cursor *result) {
    d7_merkle_search_tree snapshot = {0};
    d7_merkle_status status;
    if (!d7_mst_tree_valid(tree) || result == NULL ||
        position > d7_merkle_search_tree_size(tree)) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    status = d7_merkle_search_tree_copy(tree, &snapshot);
    if (status == D7_MERKLE_OK) {
        *result = (d7_merkle_search_tree_cursor){snapshot, position};
    }
    return status;
}

d7_merkle_status d7_merkle_search_tree_cursor_at_start(
    const d7_merkle_search_tree *tree,
    d7_merkle_search_tree_cursor *result) {
    return d7_merkle_search_tree_cursor_create(tree, 0, result);
}

d7_merkle_status d7_merkle_search_tree_cursor_at_end(
    const d7_merkle_search_tree *tree,
    d7_merkle_search_tree_cursor *result) {
    if (!d7_mst_tree_valid(tree)) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    return d7_merkle_search_tree_cursor_create(
        tree,
        d7_merkle_search_tree_size(tree),
        result);
}

static d7_merkle_status d7_mst_cursor_from_bound(
    const d7_merkle_search_tree *tree,
    const void *key,
    bool upper,
    bool *found,
    d7_merkle_search_tree_cursor *result) {
    size_t position = 0;
    bool hit = false;
    d7_merkle_status status;
    if (!d7_mst_tree_valid(tree) || key == NULL || result == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    status = d7_mst_cursor_lower_bound_rank(tree, key, &position, &hit);
    if (status == D7_MERKLE_OK && upper && hit) {
        status = d7_mst_add_cursor_rank(&position, 1);
    }
    if (status == D7_MERKLE_OK) {
        status = d7_merkle_search_tree_cursor_create(tree, position, result);
    }
    if (status == D7_MERKLE_OK && found != NULL) {
        *found = hit;
    }
    return status;
}

d7_merkle_status d7_merkle_search_tree_cursor_lower_bound(
    const d7_merkle_search_tree *tree,
    const void *key,
    d7_merkle_search_tree_cursor *result) {
    return d7_mst_cursor_from_bound(tree, key, false, NULL, result);
}

d7_merkle_status d7_merkle_search_tree_cursor_upper_bound(
    const d7_merkle_search_tree *tree,
    const void *key,
    d7_merkle_search_tree_cursor *result) {
    return d7_mst_cursor_from_bound(tree, key, true, NULL, result);
}

d7_merkle_status d7_merkle_search_tree_cursor_at_key(
    const d7_merkle_search_tree *tree,
    const void *key,
    bool *found,
    d7_merkle_search_tree_cursor *result) {
    if (found == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    return d7_mst_cursor_from_bound(tree, key, false, found, result);
}

d7_merkle_status d7_merkle_search_tree_cursor_copy(
    const d7_merkle_search_tree_cursor *cursor,
    d7_merkle_search_tree_cursor *result) {
    if (!d7_mst_cursor_valid(cursor) || result == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    if (cursor == result) {
        return D7_MERKLE_OK;
    }
    return d7_merkle_search_tree_cursor_create(
        &cursor->tree,
        cursor->position,
        result);
}

void d7_merkle_search_tree_cursor_destroy(
    d7_merkle_search_tree_cursor *cursor) {
    if (cursor != NULL) {
        d7_merkle_search_tree_dispose(&cursor->tree);
        cursor->position = 0;
    }
}

size_t d7_merkle_search_tree_cursor_count(
    const d7_merkle_search_tree_cursor *cursor) {
    return !d7_mst_cursor_valid(cursor)
        ? 0
        : d7_merkle_search_tree_size(&cursor->tree);
}

size_t d7_merkle_search_tree_cursor_position(
    const d7_merkle_search_tree_cursor *cursor) {
    return !d7_mst_cursor_valid(cursor) ? 0 : cursor->position;
}

bool d7_merkle_search_tree_cursor_is_at_start(
    const d7_merkle_search_tree_cursor *cursor) {
    return d7_mst_cursor_valid(cursor) && cursor->position == 0;
}

bool d7_merkle_search_tree_cursor_is_at_end(
    const d7_merkle_search_tree_cursor *cursor) {
    return d7_mst_cursor_valid(cursor) &&
        cursor->position == d7_merkle_search_tree_size(&cursor->tree);
}

static bool d7_mst_cursor_try_peek(
    const d7_merkle_search_tree_cursor *cursor,
    size_t position,
    d7_merkle_search_entry_ref *entry) {
    const d7_mst_entry *stored;
    if (!d7_mst_cursor_valid(cursor) || entry == NULL) {
        return false;
    }
    stored = d7_mst_cursor_entry_at_rank(&cursor->tree, position);
    if (stored == NULL) {
        return false;
    }
    *entry = d7_mst_entry_ref(stored);
    return true;
}

bool d7_merkle_search_tree_cursor_try_peek_previous(
    const d7_merkle_search_tree_cursor *cursor,
    d7_merkle_search_entry_ref *entry) {
    return d7_mst_cursor_valid(cursor) && cursor->position != 0 &&
        d7_mst_cursor_try_peek(cursor, cursor->position - 1, entry);
}

bool d7_merkle_search_tree_cursor_try_peek_next(
    const d7_merkle_search_tree_cursor *cursor,
    d7_merkle_search_entry_ref *entry) {
    return d7_mst_cursor_valid(cursor) &&
        d7_mst_cursor_try_peek(cursor, cursor->position, entry);
}

d7_merkle_status d7_merkle_search_tree_cursor_move_previous(
    const d7_merkle_search_tree_cursor *cursor,
    d7_merkle_search_tree_cursor *result) {
    if (!d7_mst_cursor_valid(cursor) || cursor->position == 0) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    return d7_mst_cursor_reposition(cursor, cursor->position - 1, result);
}

d7_merkle_status d7_merkle_search_tree_cursor_move_next(
    const d7_merkle_search_tree_cursor *cursor,
    d7_merkle_search_tree_cursor *result) {
    if (!d7_mst_cursor_valid(cursor) ||
        cursor->position == d7_merkle_search_tree_size(&cursor->tree)) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    return d7_mst_cursor_reposition(cursor, cursor->position + 1, result);
}

d7_merkle_status d7_merkle_search_tree_cursor_seek(
    const d7_merkle_search_tree_cursor *cursor,
    size_t position,
    d7_merkle_search_tree_cursor *result) {
    return d7_mst_cursor_reposition(cursor, position, result);
}

static d7_merkle_status d7_mst_cursor_put_core(
    const d7_merkle_search_tree_cursor *cursor,
    const void *key,
    const void *value,
    bool strict,
    d7_merkle_search_tree_cursor *result) {
    d7_merkle_search_tree tree = {0};
    size_t expected = 0;
    bool found = false;
    d7_merkle_status status;
    if (!d7_mst_cursor_valid(cursor) || key == NULL || value == NULL || result == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    status = d7_mst_cursor_lower_bound_rank(&cursor->tree, key, &expected, &found);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    if (strict && found) {
        return D7_MERKLE_DUPLICATE_KEY;
    }
    if (expected != cursor->position) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    status = d7_merkle_search_tree_set(&cursor->tree, key, value, &tree);
    if (status == D7_MERKLE_OK) {
        d7_mst_cursor_publish(
            cursor,
            result,
            tree,
            cursor->position + (found ? 0 : 1));
    }
    return status;
}

d7_merkle_status d7_merkle_search_tree_cursor_insert(
    const d7_merkle_search_tree_cursor *cursor,
    const void *key,
    const void *value,
    d7_merkle_search_tree_cursor *result) {
    return d7_mst_cursor_put_core(cursor, key, value, true, result);
}

d7_merkle_status d7_merkle_search_tree_cursor_put(
    const d7_merkle_search_tree_cursor *cursor,
    const void *key,
    const void *value,
    d7_merkle_search_tree_cursor *result) {
    return d7_mst_cursor_put_core(cursor, key, value, false, result);
}

d7_merkle_status d7_merkle_search_tree_cursor_set_next_value(
    const d7_merkle_search_tree_cursor *cursor,
    const void *value,
    d7_merkle_search_tree_cursor *result) {
    const d7_mst_entry *entry;
    d7_merkle_search_tree tree = {0};
    d7_merkle_status status;
    if (!d7_mst_cursor_valid(cursor) || value == NULL || result == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    entry = d7_mst_cursor_entry_at_rank(&cursor->tree, cursor->position);
    if (entry == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    status = d7_merkle_search_tree_set(
        &cursor->tree,
        entry->key->value,
        value,
        &tree);
    if (status == D7_MERKLE_OK) {
        d7_mst_cursor_publish(cursor, result, tree, cursor->position);
    }
    return status;
}

static d7_merkle_status d7_mst_cursor_delete(
    const d7_merkle_search_tree_cursor *cursor,
    bool previous,
    d7_merkle_search_tree_cursor *result) {
    size_t position;
    const d7_mst_entry *entry;
    d7_merkle_search_tree tree = {0};
    d7_merkle_status status;
    if (!d7_mst_cursor_valid(cursor) || result == NULL ||
        (previous && cursor->position == 0) ||
        (!previous && cursor->position == d7_merkle_search_tree_size(&cursor->tree))) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    position = previous ? cursor->position - 1 : cursor->position;
    entry = d7_mst_cursor_entry_at_rank(&cursor->tree, position);
    if (entry == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    status = d7_merkle_search_tree_remove(
        &cursor->tree,
        entry->key->value,
        &tree);
    if (status == D7_MERKLE_OK) {
        d7_mst_cursor_publish(cursor, result, tree, position);
    }
    return status;
}

d7_merkle_status d7_merkle_search_tree_cursor_delete_previous(
    const d7_merkle_search_tree_cursor *cursor,
    d7_merkle_search_tree_cursor *result) {
    return d7_mst_cursor_delete(cursor, true, result);
}

d7_merkle_status d7_merkle_search_tree_cursor_delete_next(
    const d7_merkle_search_tree_cursor *cursor,
    d7_merkle_search_tree_cursor *result) {
    return d7_mst_cursor_delete(cursor, false, result);
}

d7_merkle_status d7_merkle_search_tree_cursor_snapshot(
    const d7_merkle_search_tree_cursor *cursor,
    d7_merkle_search_tree *result) {
    if (!d7_mst_cursor_valid(cursor) || result == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    if (result == &cursor->tree) {
        return D7_MERKLE_OK;
    }
    return d7_merkle_search_tree_copy(&cursor->tree, result);
}

static void d7_mst_iterator_init(
    d7_mst_iterator *iterator,
    const struct d7_merkle_node *root) {
    memset(iterator, 0, sizeof(*iterator));
    if (root != NULL) {
        iterator->frames[0].node = root;
        iterator->depth = 1;
    }
}

static bool d7_mst_iterator_next(
    d7_mst_iterator *iterator,
    const d7_mst_entry **entry) {
    while (iterator->depth != 0) {
        d7_mst_iterator_frame *frame = &iterator->frames[iterator->depth - 1];
        const struct d7_merkle_node *node = frame->node;
        d7_mst_entry *const *entries = d7_mst_node_entries_const(node);
        struct d7_merkle_node *const *children = d7_mst_node_children_const(node);
        if (frame->index < node->entry_count) {
            if (!frame->child_visited) {
                const struct d7_merkle_node *child = children[frame->index];
                frame->child_visited = true;
                if (child != NULL) {
                    if (iterator->depth >= D7_MST_MAXIMUM_HEIGHT) {
                        return false;
                    }
                    iterator->frames[iterator->depth] =
                        (d7_mst_iterator_frame){child, 0, false};
                    ++iterator->depth;
                    continue;
                }
            }
            *entry = entries[frame->index];
            ++frame->index;
            frame->child_visited = false;
            return true;
        }
        if (!frame->child_visited) {
            const struct d7_merkle_node *child = children[node->entry_count];
            frame->child_visited = true;
            if (child != NULL) {
                if (iterator->depth >= D7_MST_MAXIMUM_HEIGHT) {
                    return false;
                }
                iterator->frames[iterator->depth] =
                    (d7_mst_iterator_frame){child, 0, false};
                ++iterator->depth;
                continue;
            }
        }
        --iterator->depth;
    }
    *entry = NULL;
    return false;
}

d7_merkle_status d7_merkle_search_tree_visit(
    const d7_merkle_search_tree *tree,
    d7_merkle_entry_visitor visitor,
    void *context) {
    d7_mst_iterator iterator;
    const d7_mst_entry *entry;
    if (!d7_mst_tree_valid(tree) || visitor == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    d7_mst_iterator_init(&iterator, tree->root);
    while (d7_mst_iterator_next(&iterator, &entry)) {
        const d7_merkle_status status = visitor(d7_mst_entry_ref(entry), context);
        if (status != D7_MERKLE_OK) {
            return status;
        }
    }
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_visit_range_node(
    const struct d7_merkle_policy_rep *policy,
    const struct d7_merkle_node *node,
    const void *minimum_key,
    const void *maximum_key,
    d7_merkle_entry_visitor visitor,
    void *context) {
    d7_mst_entry *const *entries;
    struct d7_merkle_node *const *children;
    size_t index;
    if (node == NULL) {
        return D7_MERKLE_OK;
    }
    entries = d7_mst_node_entries_const(node);
    children = d7_mst_node_children_const(node);
    for (index = 0; index != node->entry_count; ++index) {
        int low = 0;
        int high = 0;
        d7_merkle_status status = d7_mst_key_compare(
            policy,
            entries[index]->key->value,
            minimum_key,
            &low);
        if (status == D7_MERKLE_OK) {
            status = d7_mst_key_compare(
                policy,
                entries[index]->key->value,
                maximum_key,
                &high);
        }
        if (status != D7_MERKLE_OK) {
            return status;
        }
        if (low > 0) {
            status = d7_mst_visit_range_node(
                policy,
                children[index],
                minimum_key,
                maximum_key,
                visitor,
                context);
            if (status != D7_MERKLE_OK) {
                return status;
            }
        }
        if (low >= 0 && high <= 0) {
            status = visitor(d7_mst_entry_ref(entries[index]), context);
            if (status != D7_MERKLE_OK) {
                return status;
            }
        }
        if (high > 0) {
            return D7_MERKLE_OK;
        }
    }
    {
        int comparison = 0;
        d7_merkle_status status = d7_mst_key_compare(
            policy,
            entries[node->entry_count - 1]->key->value,
            maximum_key,
            &comparison);
        if (status != D7_MERKLE_OK) {
            return status;
        }
        return comparison < 0
            ? d7_mst_visit_range_node(
                policy,
                children[node->entry_count],
                minimum_key,
                maximum_key,
                visitor,
                context)
            : D7_MERKLE_OK;
    }
}

d7_merkle_status d7_merkle_search_tree_visit_range(
    const d7_merkle_search_tree *tree,
    const void *minimum_key,
    const void *maximum_key,
    d7_merkle_entry_visitor visitor,
    void *context) {
    int comparison = 0;
    d7_merkle_status status;
    if (!d7_mst_tree_valid(tree) || minimum_key == NULL || maximum_key == NULL ||
        visitor == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    status = d7_mst_key_compare(tree->policy, minimum_key, maximum_key, &comparison);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    if (comparison > 0) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    return d7_mst_visit_range_node(
        tree->policy,
        tree->root,
        minimum_key,
        maximum_key,
        visitor,
        context);
}

static bool d7_mst_typed_compatible(
    const d7_merkle_search_tree *left,
    const d7_merkle_search_tree *right) {
    return d7_mst_tree_valid(left) && d7_mst_tree_valid(right) &&
        d7_merkle_digest_equal(left->policy->domain_digest, right->policy->domain_digest) &&
        left->policy->config.key_type.type_identity ==
            right->policy->config.key_type.type_identity &&
        left->policy->config.value_type.type_identity ==
            right->policy->config.value_type.type_identity;
}

bool d7_merkle_search_tree_content_equals(
    const d7_merkle_search_tree *left,
    const d7_merkle_search_tree *right) {
    return d7_mst_tree_valid(left) && d7_mst_tree_valid(right) &&
        d7_merkle_digest_equal(left->policy->domain_digest, right->policy->domain_digest) &&
        d7_merkle_digest_equal(
            d7_merkle_search_tree_root_hash(left),
            d7_merkle_search_tree_root_hash(right));
}

static d7_merkle_status d7_mst_nodes_equal(
    const struct d7_merkle_policy_rep *policy,
    const struct d7_merkle_node *left,
    const struct d7_merkle_node *right,
    bool *equal) {
    size_t index;
    if (left == right) {
        *equal = true;
        return D7_MERKLE_OK;
    }
    if (left == NULL || right == NULL || left->level != right->level ||
        left->entry_count != right->entry_count) {
        *equal = false;
        return D7_MERKLE_OK;
    }
    for (index = 0; index != left->entry_count; ++index) {
        d7_mst_entry *const *left_entries = d7_mst_node_entries_const(left);
        d7_mst_entry *const *right_entries = d7_mst_node_entries_const(right);
        int comparison = 0;
        bool values_equal = false;
        d7_merkle_status status = d7_mst_key_compare(
            policy,
            left_entries[index]->key->value,
            right_entries[index]->key->value,
            &comparison);
        if (status == D7_MERKLE_OK) {
            status = d7_mst_values_equal(
                policy,
                left_entries[index],
                right_entries[index],
                &values_equal);
        }
        if (status != D7_MERKLE_OK) {
            return status;
        }
        if (comparison != 0 || !values_equal) {
            *equal = false;
            return D7_MERKLE_OK;
        }
    }
    for (index = 0; index != left->entry_count + 1; ++index) {
        bool children_equal = false;
        const d7_merkle_status status = d7_mst_nodes_equal(
            policy,
            d7_mst_node_children_const(left)[index],
            d7_mst_node_children_const(right)[index],
            &children_equal);
        if (status != D7_MERKLE_OK) {
            return status;
        }
        if (!children_equal) {
            *equal = false;
            return D7_MERKLE_OK;
        }
    }
    *equal = true;
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_enumerated_equal(
    const d7_merkle_search_tree *left,
    const d7_merkle_search_tree *right,
    bool *equal) {
    d7_mst_iterator left_iterator;
    d7_mst_iterator right_iterator;
    const d7_mst_entry *left_entry = NULL;
    const d7_mst_entry *right_entry = NULL;
    bool has_left;
    bool has_right;
    d7_mst_iterator_init(&left_iterator, left->root);
    d7_mst_iterator_init(&right_iterator, right->root);
    has_left = d7_mst_iterator_next(&left_iterator, &left_entry);
    has_right = d7_mst_iterator_next(&right_iterator, &right_entry);
    while (has_left && has_right) {
        int comparison = 0;
        bool values_equal = false;
        d7_merkle_status status = d7_mst_key_compare(
            left->policy,
            left_entry->key->value,
            right_entry->key->value,
            &comparison);
        if (status == D7_MERKLE_OK) {
            status = d7_mst_values_equal(
                left->policy,
                left_entry,
                right_entry,
                &values_equal);
        }
        if (status != D7_MERKLE_OK) {
            return status;
        }
        if (comparison != 0 || !values_equal) {
            *equal = false;
            return D7_MERKLE_OK;
        }
        has_left = d7_mst_iterator_next(&left_iterator, &left_entry);
        has_right = d7_mst_iterator_next(&right_iterator, &right_entry);
    }
    *equal = has_left == has_right;
    return D7_MERKLE_OK;
}

d7_merkle_status d7_merkle_search_tree_map_equals(
    const d7_merkle_search_tree *left,
    const d7_merkle_search_tree *right,
    bool *equal) {
    if (!d7_mst_tree_valid(left) || !d7_mst_tree_valid(right) || equal == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    if (!d7_mst_typed_compatible(left, right)) {
        return D7_MERKLE_INCOMPATIBLE_POLICY;
    }
    if (left->root == right->root) {
        *equal = true;
        return D7_MERKLE_OK;
    }
    if (d7_merkle_search_tree_size(left) != d7_merkle_search_tree_size(right)) {
        *equal = false;
        return D7_MERKLE_OK;
    }
    if (d7_merkle_digest_equal(
            d7_merkle_search_tree_root_hash(left),
            d7_merkle_search_tree_root_hash(right))) {
        return d7_mst_nodes_equal(left->policy, left->root, right->root, equal);
    }
    return d7_mst_enumerated_equal(left, right, equal);
}

static d7_merkle_status d7_mst_emit_difference_subtree(
    const struct d7_merkle_node *node,
    d7_merkle_difference_kind kind,
    d7_merkle_difference_visitor visitor,
    void *context) {
    d7_mst_iterator iterator;
    const d7_mst_entry *entry;
    d7_mst_iterator_init(&iterator, node);
    while (d7_mst_iterator_next(&iterator, &entry)) {
        d7_merkle_difference_ref difference;
        difference.kind = kind;
        difference.key = entry->key->value;
        difference.before = kind == D7_MERKLE_DIFFERENCE_REMOVED
            ? entry->value->value
            : NULL;
        difference.after = kind == D7_MERKLE_DIFFERENCE_ADDED
            ? entry->value->value
            : NULL;
        {
            const d7_merkle_status status = visitor(difference, context);
            if (status != D7_MERKLE_OK) {
                return status;
            }
        }
    }
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_same_separators(
    const struct d7_merkle_policy_rep *policy,
    const struct d7_merkle_node *left,
    const struct d7_merkle_node *right,
    bool *same) {
    size_t index;
    if (left->level != right->level || left->entry_count != right->entry_count) {
        *same = false;
        return D7_MERKLE_OK;
    }
    for (index = 0; index != left->entry_count; ++index) {
        int comparison = 0;
        const d7_merkle_status status = d7_mst_key_compare(
            policy,
            d7_mst_node_entries_const(left)[index]->key->value,
            d7_mst_node_entries_const(right)[index]->key->value,
            &comparison);
        if (status != D7_MERKLE_OK) {
            return status;
        }
        if (comparison != 0) {
            *same = false;
            return D7_MERKLE_OK;
        }
    }
    *same = true;
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_merge_diff(
    const struct d7_merkle_policy_rep *policy,
    const struct d7_merkle_node *left,
    const struct d7_merkle_node *right,
    d7_merkle_difference_visitor visitor,
    void *context) {
    d7_mst_iterator left_iterator;
    d7_mst_iterator right_iterator;
    const d7_mst_entry *old_entry = NULL;
    const d7_mst_entry *new_entry = NULL;
    bool has_old;
    bool has_new;
    d7_mst_iterator_init(&left_iterator, left);
    d7_mst_iterator_init(&right_iterator, right);
    has_old = d7_mst_iterator_next(&left_iterator, &old_entry);
    has_new = d7_mst_iterator_next(&right_iterator, &new_entry);
    while (has_old || has_new) {
        int comparison;
        d7_merkle_status status;
        if (!has_old) {
            comparison = 1;
        } else if (!has_new) {
            comparison = -1;
        } else {
            comparison = 0;
            status = d7_mst_key_compare(
                policy,
                old_entry->key->value,
                new_entry->key->value,
                &comparison);
            if (status != D7_MERKLE_OK) {
                return status;
            }
        }
        if (comparison < 0) {
            const d7_merkle_difference_ref difference = {
                D7_MERKLE_DIFFERENCE_REMOVED,
                old_entry->key->value,
                old_entry->value->value,
                NULL};
            status = visitor(difference, context);
            if (status != D7_MERKLE_OK) {
                return status;
            }
            has_old = d7_mst_iterator_next(&left_iterator, &old_entry);
        } else if (comparison > 0) {
            const d7_merkle_difference_ref difference = {
                D7_MERKLE_DIFFERENCE_ADDED,
                new_entry->key->value,
                NULL,
                new_entry->value->value};
            status = visitor(difference, context);
            if (status != D7_MERKLE_OK) {
                return status;
            }
            has_new = d7_mst_iterator_next(&right_iterator, &new_entry);
        } else {
            bool values_equal = false;
            status = d7_mst_values_equal(policy, old_entry, new_entry, &values_equal);
            if (status != D7_MERKLE_OK) {
                return status;
            }
            if (!values_equal) {
                const d7_merkle_difference_ref difference = {
                    D7_MERKLE_DIFFERENCE_CHANGED,
                    old_entry->key->value,
                    old_entry->value->value,
                    new_entry->value->value};
                status = visitor(difference, context);
                if (status != D7_MERKLE_OK) {
                    return status;
                }
            }
            has_old = d7_mst_iterator_next(&left_iterator, &old_entry);
            has_new = d7_mst_iterator_next(&right_iterator, &new_entry);
        }
    }
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_diff_nodes(
    const struct d7_merkle_policy_rep *policy,
    const struct d7_merkle_node *left,
    const struct d7_merkle_node *right,
    d7_merkle_difference_visitor visitor,
    void *context) {
    bool same = false;
    size_t index;
    d7_merkle_status status;
    if (left == right ||
        (left != NULL && right != NULL && d7_merkle_digest_equal(left->digest, right->digest))) {
        return D7_MERKLE_OK;
    }
    if (left == NULL) {
        return d7_mst_emit_difference_subtree(
            right,
            D7_MERKLE_DIFFERENCE_ADDED,
            visitor,
            context);
    }
    if (right == NULL) {
        return d7_mst_emit_difference_subtree(
            left,
            D7_MERKLE_DIFFERENCE_REMOVED,
            visitor,
            context);
    }
    status = d7_mst_same_separators(policy, left, right, &same);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    if (!same) {
        return d7_mst_merge_diff(policy, left, right, visitor, context);
    }
    for (index = 0; index != left->entry_count; ++index) {
        d7_mst_entry *const *left_entries = d7_mst_node_entries_const(left);
        d7_mst_entry *const *right_entries = d7_mst_node_entries_const(right);
        bool values_equal = false;
        status = d7_mst_diff_nodes(
            policy,
            d7_mst_node_children_const(left)[index],
            d7_mst_node_children_const(right)[index],
            visitor,
            context);
        if (status == D7_MERKLE_OK) {
            status = d7_mst_values_equal(
                policy,
                left_entries[index],
                right_entries[index],
                &values_equal);
        }
        if (status != D7_MERKLE_OK) {
            return status;
        }
        if (!values_equal) {
            const d7_merkle_difference_ref difference = {
                D7_MERKLE_DIFFERENCE_CHANGED,
                left_entries[index]->key->value,
                left_entries[index]->value->value,
                right_entries[index]->value->value};
            status = visitor(difference, context);
            if (status != D7_MERKLE_OK) {
                return status;
            }
        }
    }
    return d7_mst_diff_nodes(
        policy,
        d7_mst_node_children_const(left)[left->entry_count],
        d7_mst_node_children_const(right)[right->entry_count],
        visitor,
        context);
}

d7_merkle_status d7_merkle_search_tree_diff(
    const d7_merkle_search_tree *left,
    const d7_merkle_search_tree *right,
    d7_merkle_difference_visitor visitor,
    void *context) {
    if (!d7_mst_tree_valid(left) || !d7_mst_tree_valid(right) || visitor == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    if (!d7_mst_typed_compatible(left, right)) {
        return D7_MERKLE_INCOMPATIBLE_POLICY;
    }
    return d7_mst_diff_nodes(left->policy, left->root, right->root, visitor, context);
}

const void *d7_merkle_search_tree_root_identity(
    const d7_merkle_search_tree *tree) {
    return d7_mst_tree_valid(tree) ? tree->root : NULL;
}

d7_merkle_status d7_merkle_search_tree_node_identity(
    const d7_merkle_search_tree *tree,
    const void *key,
    const void **identity) {
    struct d7_merkle_node *node = NULL;
    size_t entry_index = 0;
    d7_merkle_status status;
    if (!d7_mst_tree_valid(tree) || key == NULL || identity == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    status = d7_mst_find_node(tree, key, &node, &entry_index);
    (void)entry_index;
    if (status == D7_MERKLE_OK) {
        *identity = node;
    }
    return status;
}

static size_t d7_mst_pointer_hash(const void *pointer) {
    size_t value = (size_t)(uintptr_t)pointer;
    value ^= value >> 17;
    value *= (size_t)0xed5ad4bbU;
    value ^= value >> 11;
    return value;
}

static void d7_mst_pointer_set_add(
    const struct d7_merkle_node **table,
    size_t mask,
    const struct d7_merkle_node *node) {
    size_t index = d7_mst_pointer_hash(node) & mask;
    while (table[index] != NULL && table[index] != node) {
        index = (index + 1) & mask;
    }
    table[index] = node;
}

static bool d7_mst_pointer_set_contains(
    const struct d7_merkle_node *const *table,
    size_t mask,
    const struct d7_merkle_node *node) {
    size_t index = d7_mst_pointer_hash(node) & mask;
    while (table[index] != NULL) {
        if (table[index] == node) {
            return true;
        }
        index = (index + 1) & mask;
    }
    return false;
}

static void d7_mst_collect_node_pointers(
    const struct d7_merkle_node *node,
    const struct d7_merkle_node **table,
    size_t mask) {
    size_t index;
    if (node == NULL) {
        return;
    }
    d7_mst_pointer_set_add(table, mask, node);
    for (index = 0; index != node->entry_count + 1; ++index) {
        d7_mst_collect_node_pointers(
            d7_mst_node_children_const(node)[index],
            table,
            mask);
    }
}

static size_t d7_mst_count_node_pointers(
    const struct d7_merkle_node *node,
    const struct d7_merkle_node *const *table,
    size_t mask) {
    size_t count = 0;
    size_t index;
    if (node == NULL) {
        return 0;
    }
    if (d7_mst_pointer_set_contains(table, mask, node)) {
        ++count;
    }
    for (index = 0; index != node->entry_count + 1; ++index) {
        count += d7_mst_count_node_pointers(
            d7_mst_node_children_const(node)[index],
            table,
            mask);
    }
    return count;
}

d7_merkle_status d7_merkle_search_tree_shared_node_count(
    const d7_merkle_search_tree *left,
    const d7_merkle_search_tree *right,
    size_t *shared_count) {
    const struct d7_merkle_node **table = NULL;
    size_t capacity = 1;
    size_t minimum_capacity;
    size_t byte_count;
    if (!d7_mst_tree_valid(left) || !d7_mst_tree_valid(right) || shared_count == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    if (left->policy != right->policy) {
        return D7_MERKLE_INCOMPATIBLE_POLICY;
    }
    if (left->root == NULL || right->root == NULL) {
        *shared_count = 0;
        return D7_MERKLE_OK;
    }
    if (d7_mst_multiply_overflows(left->root->block_count, 2, &minimum_capacity)) {
        return D7_MERKLE_OVERFLOW;
    }
    while (capacity < minimum_capacity) {
        if (capacity > SIZE_MAX / 2) {
            return D7_MERKLE_OVERFLOW;
        }
        capacity *= 2;
    }
    if (d7_mst_multiply_overflows(capacity, sizeof(*table), &byte_count)) {
        return D7_MERKLE_OVERFLOW;
    }
    table = (const struct d7_merkle_node **)d7_mst_allocate(left->policy, byte_count);
    if (table == NULL) {
        return D7_MERKLE_NO_MEMORY;
    }
    memset(table, 0, byte_count);
    d7_mst_collect_node_pointers(left->root, table, capacity - 1);
    *shared_count = d7_mst_count_node_pointers(right->root, table, capacity - 1);
    d7_mst_deallocate(left->policy, table);
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_visit_shape_node(
    const struct d7_merkle_node *node,
    d7_merkle_shape_visitor visitor,
    void *context) {
    size_t index;
    if (node == NULL) {
        return D7_MERKLE_OK;
    }
    for (index = 0; index != node->entry_count; ++index) {
        const d7_merkle_shape_ref shape = {
            node,
            node->level,
            d7_mst_entry_ref(d7_mst_node_entries_const(node)[index]),
            node->entry_count,
            node->count};
        const d7_merkle_status status = visitor(shape, context);
        if (status != D7_MERKLE_OK) {
            return status;
        }
    }
    for (index = 0; index != node->entry_count + 1; ++index) {
        const d7_merkle_status status = d7_mst_visit_shape_node(
            d7_mst_node_children_const(node)[index],
            visitor,
            context);
        if (status != D7_MERKLE_OK) {
            return status;
        }
    }
    return D7_MERKLE_OK;
}

d7_merkle_status d7_merkle_search_tree_visit_shape(
    const d7_merkle_search_tree *tree,
    d7_merkle_shape_visitor visitor,
    void *context) {
    if (!d7_mst_tree_valid(tree) || visitor == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    return d7_mst_visit_shape_node(tree->root, visitor, context);
}

static d7_merkle_status d7_mst_visit_blocks_node(
    const struct d7_merkle_node *node,
    d7_merkle_block_visitor visitor,
    void *context) {
    size_t index;
    if (node == NULL) {
        return D7_MERKLE_OK;
    }
    {
        const d7_merkle_block_ref block = {
            node->digest,
            node->block_bytes->data,
            node->block_bytes->size};
        const d7_merkle_status status = visitor(block, context);
        if (status != D7_MERKLE_OK) {
            return status;
        }
    }
    for (index = 0; index != node->entry_count + 1; ++index) {
        const d7_merkle_status status = d7_mst_visit_blocks_node(
            d7_mst_node_children_const(node)[index],
            visitor,
            context);
        if (status != D7_MERKLE_OK) {
            return status;
        }
    }
    return D7_MERKLE_OK;
}

d7_merkle_status d7_merkle_search_tree_visit_blocks(
    const d7_merkle_search_tree *tree,
    d7_merkle_block_visitor visitor,
    void *context) {
    if (!d7_mst_tree_valid(tree) || visitor == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    return d7_mst_visit_blocks_node(tree->root, visitor, context);
}

static d7_merkle_status d7_mst_validate_object_encoding(
    const struct d7_merkle_policy_rep *policy,
    const d7_merkle_codec *codec,
    const d7_mst_object *object,
    const d7_mst_bytes *stored,
    bool expected_key,
    bool *valid) {
    d7_mst_bytes *encoded = NULL;
    d7_merkle_status status;
    if (object == NULL || stored == NULL || object->value == NULL ||
        object->is_key != expected_key) {
        *valid = false;
        return D7_MERKLE_OK;
    }
    status = d7_mst_encode_value(policy, codec, object->value, &encoded);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    *valid = encoded->size == stored->size &&
        (encoded->size == 0 || memcmp(encoded->data, stored->data, encoded->size) == 0);
    d7_mst_bytes_release(policy, encoded);
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_validate_bound(
    const struct d7_merkle_policy_rep *policy,
    const d7_mst_entry *entry,
    const d7_mst_entry *bound,
    bool lower_bound,
    bool *valid) {
    int comparison = 0;
    d7_merkle_status status;
    if (bound == NULL) {
        return D7_MERKLE_OK;
    }
    status = d7_mst_key_compare(
        policy,
        entry->key->value,
        bound->key->value,
        &comparison);
    if (status == D7_MERKLE_OK &&
        ((lower_bound && comparison <= 0) || (!lower_bound && comparison >= 0))) {
        *valid = false;
    }
    return status;
}

static d7_merkle_status d7_mst_validate_node(
    const struct d7_merkle_policy_rep *policy,
    const struct d7_merkle_node *node,
    const d7_mst_entry *lower_bound,
    const d7_mst_entry *upper_bound,
    d7_mst_validation_accumulator *accumulator,
    bool *valid) {
    d7_mst_entry *const *entries;
    struct d7_merkle_node *const *children;
    size_t count;
    size_t height = 1;
    size_t block_count = 1;
    size_t index;
    d7_mst_bytes *encoded_block = NULL;
    d7_merkle_digest encoded_digest = {{0}};
    d7_merkle_status status;
    if (node == NULL) {
        return D7_MERKLE_OK;
    }
    if (node->entry_count == 0 || node->level > D7_MST_MAXIMUM_LEVEL ||
        node->block_bytes == NULL || node->minimum_entry == NULL ||
        node->maximum_entry == NULL) {
        *valid = false;
        return D7_MERKLE_OK;
    }
    entries = d7_mst_node_entries_const(node);
    children = d7_mst_node_children_const(node);
    count = node->entry_count;
    for (index = 0; index != node->entry_count; ++index) {
        d7_merkle_digest key_digest = {{0}};
        bool encoding_valid = true;
        if (entries[index] == NULL || entries[index]->key == NULL ||
            entries[index]->value == NULL || entries[index]->key_bytes == NULL ||
            entries[index]->value_bytes == NULL || entries[index]->level != node->level) {
            *valid = false;
            return D7_MERKLE_OK;
        }
        status = d7_mst_validate_object_encoding(
            policy,
            &policy->config.key_codec,
            entries[index]->key,
            entries[index]->key_bytes,
            true,
            &encoding_valid);
        if (status == D7_MERKLE_OK && encoding_valid) {
            status = d7_mst_validate_object_encoding(
                policy,
                &policy->config.value_codec,
                entries[index]->value,
                entries[index]->value_bytes,
                false,
                &encoding_valid);
        }
        if (status != D7_MERKLE_OK) {
            return status;
        }
        if (!encoding_valid) {
            *valid = false;
            return D7_MERKLE_OK;
        }
        status = d7_mst_hash_key_bytes(policy, entries[index]->key_bytes, &key_digest);
        if (status != D7_MERKLE_OK) {
            return status;
        }
        if (d7_mst_level(key_digest) != entries[index]->level) {
            *valid = false;
            return D7_MERKLE_OK;
        }
        if (index != 0) {
            int comparison = 0;
            status = d7_mst_key_compare(
                policy,
                entries[index - 1]->key->value,
                entries[index]->key->value,
                &comparison);
            if (status != D7_MERKLE_OK) {
                return status;
            }
            if (comparison >= 0) {
                *valid = false;
                return D7_MERKLE_OK;
            }
        }
        status = d7_mst_validate_bound(policy, entries[index], lower_bound, true, valid);
        if (status == D7_MERKLE_OK && *valid) {
            status = d7_mst_validate_bound(policy, entries[index], upper_bound, false, valid);
        }
        if (status != D7_MERKLE_OK || !*valid) {
            return status;
        }
    }
    for (index = 0; index != node->entry_count + 1; ++index) {
        const struct d7_merkle_node *child = children[index];
        if (child != NULL) {
            size_t candidate_height;
            if (child->level >= node->level) {
                *valid = false;
                return D7_MERKLE_OK;
            }
            status = d7_mst_validate_node(
                policy,
                child,
                index == 0 ? lower_bound : entries[index - 1],
                index == node->entry_count ? upper_bound : entries[index],
                accumulator,
                valid);
            if (status != D7_MERKLE_OK || !*valid) {
                return status;
            }
            if (d7_mst_add_overflows(count, child->count, &count) ||
                d7_mst_add_overflows(block_count, child->block_count, &block_count) ||
                d7_mst_add_overflows(child->height, 1, &candidate_height)) {
                *valid = false;
                return D7_MERKLE_OK;
            }
            if (candidate_height > height) {
                height = candidate_height;
            }
        }
    }
    if (node->count != count || node->height != height ||
        node->block_count != block_count ||
        node->minimum_entry != (children[0] == NULL
            ? entries[0]
            : children[0]->minimum_entry) ||
        node->maximum_entry != (children[node->entry_count] == NULL
            ? entries[node->entry_count - 1]
            : children[node->entry_count]->maximum_entry)) {
        *valid = false;
        return D7_MERKLE_OK;
    }
    status = d7_mst_encode_block(
        policy,
        node->level,
        node->count,
        entries,
        node->entry_count,
        children,
        &encoded_block,
        &encoded_digest);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    if (encoded_block->size != node->block_bytes->size ||
        (encoded_block->size != 0 &&
            memcmp(encoded_block->data, node->block_bytes->data, encoded_block->size) != 0) ||
        !d7_merkle_digest_equal(encoded_digest, node->digest)) {
        *valid = false;
        d7_mst_bytes_release(policy, encoded_block);
        return D7_MERKLE_OK;
    }
    d7_mst_bytes_release(policy, encoded_block);
    if (d7_mst_add_overflows(accumulator->count, node->entry_count, &accumulator->count) ||
        d7_mst_add_overflows(accumulator->block_count, 1, &accumulator->block_count)) {
        *valid = false;
        return D7_MERKLE_OK;
    }
    if (node->entry_count < accumulator->minimum_entries) {
        accumulator->minimum_entries = node->entry_count;
    }
    if (node->entry_count > accumulator->maximum_entries) {
        accumulator->maximum_entries = node->entry_count;
    }
    if (node->block_bytes->size < accumulator->minimum_block_bytes) {
        accumulator->minimum_block_bytes = node->block_bytes->size;
    }
    if (node->block_bytes->size > accumulator->maximum_block_bytes) {
        accumulator->maximum_block_bytes = node->block_bytes->size;
    }
    return D7_MERKLE_OK;
}

d7_merkle_status d7_merkle_search_tree_validate(
    const d7_merkle_search_tree *tree,
    bool *valid,
    d7_merkle_search_tree_statistics *statistics) {
    d7_mst_validation_accumulator accumulator = {0, 0, SIZE_MAX, 0, SIZE_MAX, 0};
    d7_merkle_search_tree_statistics result = {0, 0, 0, 0, 0, 0, 0};
    bool structurally_valid = true;
    d7_merkle_status status;
    if (!d7_mst_tree_valid(tree) || valid == NULL || statistics == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    status = d7_mst_validate_node(
        tree->policy,
        tree->root,
        NULL,
        NULL,
        &accumulator,
        &structurally_valid);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    if (structurally_valid && tree->root != NULL &&
        (accumulator.count != tree->root->count ||
            accumulator.block_count != tree->root->block_count)) {
        structurally_valid = false;
    }
    if (structurally_valid && tree->root != NULL) {
        result.count = accumulator.count;
        result.block_count = accumulator.block_count;
        result.height = tree->root->height;
        result.minimum_entries_per_block = accumulator.minimum_entries;
        result.maximum_entries_per_block = accumulator.maximum_entries;
        result.minimum_block_bytes = accumulator.minimum_block_bytes;
        result.maximum_block_bytes = accumulator.maximum_block_bytes;
    }
    *valid = structurally_valid;
    *statistics = result;
    return D7_MERKLE_OK;
}

/* ------------------------------------------------------------------------- */
/* Verified persistence infrastructure.                                      */

struct d7_merkle_block_rep {
    d7_mst_ref_count refs;
    d7_merkle_allocator allocator;
    struct d7_merkle_policy_rep *policy_owner;
    d7_merkle_digest digest;
    size_t byte_count;
    unsigned char bytes[];
};

typedef struct d7_mst_digest_slot {
    bool occupied;
    d7_merkle_digest digest;
    size_t value;
    struct d7_merkle_node *node;
} d7_mst_digest_slot;

struct d7_merkle_block_pack_rep {
    d7_mst_ref_count refs;
    d7_merkle_allocator allocator;
    struct d7_merkle_policy_rep *policy_owner;
    unsigned char *algorithm_id;
    size_t algorithm_id_size;
    d7_merkle_digest domain_digest;
    d7_merkle_digest root_hash;
    d7_merkle_block *blocks;
    size_t block_count;
    uint64_t total_byte_count;
    bool contains_root_block;
};

#if defined(_WIN32)
typedef SRWLOCK d7_mst_store_lock;

static bool d7_mst_store_lock_init(d7_mst_store_lock *lock) {
    InitializeSRWLock(lock);
    return true;
}

static void d7_mst_store_lock_dispose(d7_mst_store_lock *lock) {
    (void)lock;
}

static d7_merkle_status d7_mst_store_lock_acquire(d7_mst_store_lock *lock) {
    AcquireSRWLockExclusive(lock);
    return D7_MERKLE_OK;
}

static void d7_mst_store_lock_release(d7_mst_store_lock *lock) {
    ReleaseSRWLockExclusive(lock);
}
#else
typedef mtx_t d7_mst_store_lock;

static bool d7_mst_store_lock_init(d7_mst_store_lock *lock) {
    return mtx_init(lock, mtx_plain) == thrd_success;
}

static void d7_mst_store_lock_dispose(d7_mst_store_lock *lock) {
    mtx_destroy(lock);
}

static d7_merkle_status d7_mst_store_lock_acquire(d7_mst_store_lock *lock) {
    return mtx_lock(lock) == thrd_success
        ? D7_MERKLE_OK
        : D7_MERKLE_CALLBACK_FAILURE;
}

static void d7_mst_store_lock_release(d7_mst_store_lock *lock) {
    /* Failure means the internal synchronization invariant is no longer
     * recoverable: returning would permit shared state access without a known
     * lock state. Treat it as a process-fatal implementation invariant. */
    if (mtx_unlock(lock) != thrd_success) {
        abort();
    }
}
#endif

struct d7_merkle_memory_block_store_rep {
    d7_mst_ref_count refs;
    d7_merkle_allocator allocator;
    d7_mst_store_lock lock;
    d7_merkle_block *blocks;
    size_t count;
    size_t capacity;
};

static bool d7_mst_allocator_valid(const d7_merkle_allocator *allocator) {
    return allocator == NULL ||
        (allocator->allocate != NULL && allocator->deallocate != NULL);
}

static d7_merkle_allocator d7_mst_normalize_allocator(
    const d7_merkle_allocator *allocator) {
    d7_merkle_allocator result;
    if (allocator == NULL) {
        result.allocate = d7_mst_default_allocate;
        result.deallocate = d7_mst_default_deallocate;
        result.context = NULL;
    } else {
        result = *allocator;
    }
    return result;
}

static void *d7_mst_allocator_allocate(
    const d7_merkle_allocator *allocator,
    size_t size) {
    return allocator->allocate(size, allocator->context);
}

static void d7_mst_allocator_deallocate(
    const d7_merkle_allocator *allocator,
    void *allocation) {
    if (allocation != NULL) {
        allocator->deallocate(allocation, allocator->context);
    }
}

void d7_merkle_verification_error_init(d7_merkle_verification_error *error) {
    if (error != NULL) {
        memset(error, 0, sizeof(*error));
    }
}

static d7_merkle_status d7_mst_verification_fail(
    d7_merkle_verification_error *error,
    d7_merkle_verification_failure_kind kind,
    const d7_merkle_digest *digest) {
    if (error != NULL) {
        error->kind = kind;
        error->has_block_digest = digest != NULL;
        if (digest != NULL) {
            error->block_digest = *digest;
        }
    }
    return D7_MERKLE_VERIFICATION_FAILURE;
}

void d7_merkle_verification_budget_init_default(
    d7_merkle_verification_budget *budget) {
    if (budget != NULL) {
        budget->max_block_count = 1000000u;
        budget->max_total_byte_count = UINT64_C(1) << 30;
        budget->max_block_byte_count = (size_t)16 << 20;
        budget->max_depth = 256u;
        budget->max_entry_count = UINT64_C(100000000);
        budget->max_child_references_per_block = 65536u;
        budget->max_proof_query_byte_count = (size_t)16 << 20;
    }
}

d7_merkle_status d7_merkle_verification_budget_validate(
    const d7_merkle_verification_budget *budget) {
    if (budget == NULL || budget->max_block_count == 0 ||
        budget->max_total_byte_count == 0 || budget->max_block_byte_count == 0 ||
        budget->max_depth == 0 || budget->max_entry_count == 0 ||
        budget->max_child_references_per_block == 0 ||
        budget->max_proof_query_byte_count == 0 ||
        (uint64_t)budget->max_block_byte_count > budget->max_total_byte_count ||
        (uint64_t)budget->max_proof_query_byte_count > budget->max_total_byte_count) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    return D7_MERKLE_OK;
}

static void d7_mst_block_retain(struct d7_merkle_block_rep *block) {
    if (block != NULL) {
        d7_mst_ref_retain(&block->refs);
    }
}

static void d7_mst_block_release(struct d7_merkle_block_rep *block) {
    if (block != NULL && d7_mst_ref_release(&block->refs)) {
        const d7_merkle_allocator allocator = block->allocator;
        struct d7_merkle_policy_rep *owner = block->policy_owner;
        d7_mst_allocator_deallocate(&allocator, block);
        d7_mst_policy_release(owner);
    }
}

d7_merkle_status d7_merkle_block_init(
    d7_merkle_digest digest,
    const unsigned char *bytes,
    size_t byte_count,
    const d7_merkle_allocator *allocator,
    d7_merkle_block *block) {
    d7_merkle_allocator selected;
    struct d7_merkle_block_rep *rep;
    size_t allocation_size;
    if (block == NULL || !d7_mst_allocator_valid(allocator) ||
        (byte_count != 0 && bytes == NULL) ||
        d7_mst_add_overflows(sizeof(*rep), byte_count, &allocation_size)) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    selected = d7_mst_normalize_allocator(allocator);
    rep = (struct d7_merkle_block_rep *)d7_mst_allocator_allocate(
        &selected,
        allocation_size);
    if (rep == NULL) {
        return D7_MERKLE_NO_MEMORY;
    }
    d7_mst_ref_init(&rep->refs);
    rep->allocator = selected;
    rep->policy_owner = NULL;
    rep->digest = digest;
    rep->byte_count = byte_count;
    if (byte_count != 0) {
        memcpy(rep->bytes, bytes, byte_count);
    }
    block->rep = rep;
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_block_init_policy(
    const struct d7_merkle_policy_rep *policy,
    d7_merkle_digest digest,
    const unsigned char *bytes,
    size_t byte_count,
    d7_merkle_block *block) {
    d7_merkle_status status = d7_merkle_block_init(
        digest,
        bytes,
        byte_count,
        &policy->config.allocator,
        block);
    if (status == D7_MERKLE_OK) {
        block->rep->policy_owner = (struct d7_merkle_policy_rep *)policy;
        d7_mst_policy_retain(block->rep->policy_owner);
    }
    return status;
}

d7_merkle_status d7_merkle_block_copy(
    const d7_merkle_block *source,
    d7_merkle_block *destination) {
    if (source == NULL || source->rep == NULL || destination == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    if (source != destination) {
        d7_mst_block_retain(source->rep);
        destination->rep = source->rep;
    }
    return D7_MERKLE_OK;
}

void d7_merkle_block_move(
    d7_merkle_block *destination,
    d7_merkle_block *source) {
    if (destination != NULL && source != NULL && destination != source) {
        destination->rep = source->rep;
        source->rep = NULL;
    }
}

void d7_merkle_block_dispose(d7_merkle_block *block) {
    if (block != NULL) {
        d7_mst_block_release(block->rep);
        block->rep = NULL;
    }
}

bool d7_merkle_block_equal(
    const d7_merkle_block *left,
    const d7_merkle_block *right) {
    return left != NULL && right != NULL && left->rep != NULL && right->rep != NULL &&
        d7_merkle_digest_equal(left->rep->digest, right->rep->digest) &&
        left->rep->byte_count == right->rep->byte_count &&
        memcmp(left->rep->bytes, right->rep->bytes, left->rep->byte_count) == 0;
}

d7_merkle_digest d7_merkle_block_digest(const d7_merkle_block *block) {
    d7_merkle_digest zero = {{0}};
    return block == NULL || block->rep == NULL ? zero : block->rep->digest;
}

const unsigned char *d7_merkle_block_bytes(const d7_merkle_block *block) {
    return block == NULL || block->rep == NULL ? NULL : block->rep->bytes;
}

size_t d7_merkle_block_byte_count(const d7_merkle_block *block) {
    return block == NULL || block->rep == NULL ? 0 : block->rep->byte_count;
}

void d7_merkle_block_store_init(d7_merkle_block_store *store) {
    if (store != NULL) {
        memset(store, 0, sizeof(*store));
    }
}

static bool d7_mst_store_valid(const d7_merkle_block_store *store) {
    return store != NULL && store->count != NULL && store->contains != NULL &&
        store->try_get != NULL && store->put != NULL && store->remove != NULL &&
        store->clear != NULL;
}

d7_merkle_status d7_merkle_block_store_count(
    const d7_merkle_block_store *store,
    size_t *count) {
    size_t produced = 0;
    d7_merkle_status status;
    if (!d7_mst_store_valid(store) || count == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    status = store->count(&produced, store->context);
    if (status == D7_MERKLE_OK) {
        *count = produced;
    }
    return status;
}

d7_merkle_status d7_merkle_block_store_contains(
    const d7_merkle_block_store *store,
    d7_merkle_digest digest,
    bool *contains) {
    bool produced = false;
    d7_merkle_status status;
    if (!d7_mst_store_valid(store) || contains == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    status = store->contains(digest, &produced, store->context);
    if (status == D7_MERKLE_OK) {
        *contains = produced;
    }
    return status;
}

d7_merkle_status d7_merkle_block_store_try_get(
    const d7_merkle_block_store *store,
    d7_merkle_digest digest,
    bool *found,
    d7_merkle_block *block) {
    bool produced_found = false;
    d7_merkle_block produced_block = {NULL};
    d7_merkle_status status;
    if (!d7_mst_store_valid(store) || found == NULL || block == NULL ||
        block->rep != NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    status = store->try_get(
        digest,
        &produced_found,
        &produced_block,
        store->context);
    if (status == D7_MERKLE_OK &&
        produced_found != (produced_block.rep != NULL)) {
        status = D7_MERKLE_CALLBACK_FAILURE;
    }
    if (status == D7_MERKLE_OK) {
        *found = produced_found;
        if (produced_found) {
            d7_merkle_block_move(block, &produced_block);
        }
    }
    d7_merkle_block_dispose(&produced_block);
    return status;
}

d7_merkle_status d7_merkle_block_store_put(
    const d7_merkle_block_store *store,
    const d7_merkle_block *block,
    d7_merkle_store_put_result *result) {
    d7_merkle_store_put_result produced = (d7_merkle_store_put_result)-1;
    d7_merkle_status status;
    if (!d7_mst_store_valid(store) || block == NULL || block->rep == NULL ||
        result == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    status = store->put(block, &produced, store->context);
    if (status == D7_MERKLE_OK &&
        produced != D7_MERKLE_STORE_ADDED &&
        produced != D7_MERKLE_STORE_PRESENT_IDENTICAL) {
        return D7_MERKLE_CALLBACK_FAILURE;
    }
    if (status == D7_MERKLE_VERIFICATION_FAILURE &&
        produced != D7_MERKLE_STORE_CONFLICT) {
        return D7_MERKLE_CALLBACK_FAILURE;
    }
    if (status != D7_MERKLE_OK &&
        status != D7_MERKLE_VERIFICATION_FAILURE) {
        return status;
    }
    *result = produced;
    return status;
}

d7_merkle_status d7_merkle_block_store_remove(
    const d7_merkle_block_store *store,
    d7_merkle_digest digest,
    bool *removed) {
    bool produced = false;
    d7_merkle_status status;
    if (!d7_mst_store_valid(store) || removed == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    status = store->remove(digest, &produced, store->context);
    if (status == D7_MERKLE_OK) {
        *removed = produced;
    }
    return status;
}

d7_merkle_status d7_merkle_block_store_clear(
    const d7_merkle_block_store *store) {
    return !d7_mst_store_valid(store)
        ? D7_MERKLE_INVALID_ARGUMENT
        : store->clear(store->context);
}

static size_t d7_mst_store_lower_bound(
    const struct d7_merkle_memory_block_store_rep *rep,
    d7_merkle_digest digest,
    bool *found) {
    size_t low = 0;
    size_t high = rep->count;
    while (low < high) {
        const size_t middle = low + (high - low) / 2;
        if (d7_merkle_digest_compare(
                rep->blocks[middle].rep->digest,
                digest) < 0) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    *found = low < rep->count &&
        d7_merkle_digest_equal(rep->blocks[low].rep->digest, digest);
    return low;
}

static d7_merkle_status d7_mst_memory_count(size_t *count, void *context) {
    struct d7_merkle_memory_block_store_rep *rep =
        (struct d7_merkle_memory_block_store_rep *)context;
    size_t produced;
    d7_merkle_status status = d7_mst_store_lock_acquire(&rep->lock);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    produced = rep->count;
    d7_mst_store_lock_release(&rep->lock);
    *count = produced;
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_memory_contains(
    d7_merkle_digest digest,
    bool *contains,
    void *context) {
    struct d7_merkle_memory_block_store_rep *rep =
        (struct d7_merkle_memory_block_store_rep *)context;
    bool produced;
    d7_merkle_status status = d7_mst_store_lock_acquire(&rep->lock);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    (void)d7_mst_store_lower_bound(rep, digest, &produced);
    d7_mst_store_lock_release(&rep->lock);
    *contains = produced;
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_memory_try_get(
    d7_merkle_digest digest,
    bool *found,
    d7_merkle_block *block,
    void *context) {
    struct d7_merkle_memory_block_store_rep *rep =
        (struct d7_merkle_memory_block_store_rep *)context;
    d7_merkle_block produced = {NULL};
    size_t index;
    bool produced_found;
    d7_merkle_status status = D7_MERKLE_OK;
    status = d7_mst_store_lock_acquire(&rep->lock);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    index = d7_mst_store_lower_bound(rep, digest, &produced_found);
    if (produced_found) {
        status = d7_merkle_block_copy(&rep->blocks[index], &produced);
    }
    d7_mst_store_lock_release(&rep->lock);
    if (status == D7_MERKLE_OK) {
        *found = produced_found;
        if (produced_found) {
            d7_merkle_block_move(block, &produced);
        }
    }
    d7_merkle_block_dispose(&produced);
    return status;
}

static d7_merkle_status d7_mst_memory_growth(
    size_t current_capacity,
    size_t required,
    size_t *capacity,
    size_t *bytes) {
    size_t produced = current_capacity == 0 ? 8 : current_capacity;
    if (required == 0) {
        return D7_MERKLE_OVERFLOW;
    }
    while (produced < required) {
        if (produced > SIZE_MAX / 2) {
            return D7_MERKLE_OVERFLOW;
        }
        produced *= 2;
    }
    if (d7_mst_multiply_overflows(
            produced,
            sizeof(d7_merkle_block),
            bytes)) {
        return D7_MERKLE_OVERFLOW;
    }
    *capacity = produced;
    return D7_MERKLE_OK;
}

static void d7_mst_memory_insert_locked(
    struct d7_merkle_memory_block_store_rep *rep,
    size_t index,
    const d7_merkle_block *block) {
    if (index != rep->count) {
        memmove(
            rep->blocks + index + 1,
            rep->blocks + index,
            (rep->count - index) * sizeof(*rep->blocks));
    }
    rep->blocks[index].rep = block->rep;
    d7_mst_block_retain(rep->blocks[index].rep);
    ++rep->count;
}

static d7_merkle_status d7_mst_memory_put(
    const d7_merkle_block *block,
    d7_merkle_store_put_result *result,
    void *context) {
    struct d7_merkle_memory_block_store_rep *rep =
        (struct d7_merkle_memory_block_store_rep *)context;
    d7_merkle_block *replacement = NULL;
    size_t replacement_capacity = 0;
    for (;;) {
        d7_merkle_block *retired = NULL;
        size_t current_capacity;
        size_t required;
        size_t replacement_bytes;
        size_t index;
        bool found;
        d7_merkle_store_put_result produced;
        d7_merkle_status status = d7_mst_store_lock_acquire(&rep->lock);
        if (status != D7_MERKLE_OK) {
            d7_mst_allocator_deallocate(&rep->allocator, replacement);
            return status;
        }
        index = d7_mst_store_lower_bound(rep, block->rep->digest, &found);
        if (found) {
            produced = d7_merkle_block_equal(&rep->blocks[index], block)
                ? D7_MERKLE_STORE_PRESENT_IDENTICAL
                : D7_MERKLE_STORE_CONFLICT;
            d7_mst_store_lock_release(&rep->lock);
            d7_mst_allocator_deallocate(&rep->allocator, replacement);
            *result = produced;
            return produced == D7_MERKLE_STORE_CONFLICT
                ? D7_MERKLE_VERIFICATION_FAILURE
                : D7_MERKLE_OK;
        }
        if (rep->count < rep->capacity) {
            d7_mst_memory_insert_locked(rep, index, block);
            d7_mst_store_lock_release(&rep->lock);
            d7_mst_allocator_deallocate(&rep->allocator, replacement);
            *result = D7_MERKLE_STORE_ADDED;
            return D7_MERKLE_OK;
        }
        if (rep->count != SIZE_MAX && replacement != NULL &&
            rep->count + 1 <= replacement_capacity &&
            replacement_capacity > rep->capacity) {
            retired = rep->blocks;
            if (rep->count != 0) {
                memcpy(
                    replacement,
                    rep->blocks,
                    rep->count * sizeof(*replacement));
            }
            rep->blocks = replacement;
            rep->capacity = replacement_capacity;
            replacement = NULL;
            replacement_capacity = 0;
            index = d7_mst_store_lower_bound(rep, block->rep->digest, &found);
            (void)found;
            d7_mst_memory_insert_locked(rep, index, block);
            d7_mst_store_lock_release(&rep->lock);
            d7_mst_allocator_deallocate(&rep->allocator, retired);
            *result = D7_MERKLE_STORE_ADDED;
            return D7_MERKLE_OK;
        }
        if (rep->count == SIZE_MAX) {
            d7_mst_store_lock_release(&rep->lock);
            d7_mst_allocator_deallocate(&rep->allocator, replacement);
            return D7_MERKLE_OVERFLOW;
        }
        required = rep->count + 1;
        current_capacity = rep->capacity;
        d7_mst_store_lock_release(&rep->lock);
        d7_mst_allocator_deallocate(&rep->allocator, replacement);
        replacement = NULL;
        replacement_capacity = 0;
        status = d7_mst_memory_growth(
            current_capacity,
            required,
            &replacement_capacity,
            &replacement_bytes);
        if (status != D7_MERKLE_OK) {
            return status;
        }
        replacement = (d7_merkle_block *)d7_mst_allocator_allocate(
            &rep->allocator,
            replacement_bytes);
        if (replacement == NULL) {
            return D7_MERKLE_NO_MEMORY;
        }
    }
}

static d7_merkle_status d7_mst_memory_remove(
    d7_merkle_digest digest,
    bool *removed,
    void *context) {
    struct d7_merkle_memory_block_store_rep *rep =
        (struct d7_merkle_memory_block_store_rep *)context;
    d7_merkle_block removed_block = {NULL};
    size_t index;
    bool produced;
    d7_merkle_status status = d7_mst_store_lock_acquire(&rep->lock);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    index = d7_mst_store_lower_bound(rep, digest, &produced);
    if (produced) {
        removed_block = rep->blocks[index];
        --rep->count;
        if (index != rep->count) {
            memmove(
                rep->blocks + index,
                rep->blocks + index + 1,
                (rep->count - index) * sizeof(*rep->blocks));
        }
    }
    d7_mst_store_lock_release(&rep->lock);
    d7_merkle_block_dispose(&removed_block);
    *removed = produced;
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_memory_clear(void *context) {
    struct d7_merkle_memory_block_store_rep *rep =
        (struct d7_merkle_memory_block_store_rep *)context;
    d7_merkle_block *blocks;
    size_t count;
    size_t index;
    d7_merkle_status status = d7_mst_store_lock_acquire(&rep->lock);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    blocks = rep->blocks;
    count = rep->count;
    rep->blocks = NULL;
    rep->count = 0;
    rep->capacity = 0;
    d7_mst_store_lock_release(&rep->lock);
    for (index = 0; index != count; ++index) {
        d7_merkle_block_dispose(&blocks[index]);
    }
    d7_mst_allocator_deallocate(&rep->allocator, blocks);
    return D7_MERKLE_OK;
}

d7_merkle_status d7_merkle_memory_block_store_init(
    d7_merkle_memory_block_store *store,
    const d7_merkle_allocator *allocator) {
    d7_merkle_allocator selected;
    struct d7_merkle_memory_block_store_rep *rep;
    if (store == NULL || !d7_mst_allocator_valid(allocator)) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    selected = d7_mst_normalize_allocator(allocator);
    rep = (struct d7_merkle_memory_block_store_rep *)d7_mst_allocator_allocate(
        &selected,
        sizeof(*rep));
    if (rep == NULL) {
        return D7_MERKLE_NO_MEMORY;
    }
    memset(rep, 0, sizeof(*rep));
    rep->allocator = selected;
    if (!d7_mst_store_lock_init(&rep->lock)) {
        d7_mst_allocator_deallocate(&selected, rep);
        return D7_MERKLE_CALLBACK_FAILURE;
    }
    d7_mst_ref_init(&rep->refs);
    store->rep = rep;
    return D7_MERKLE_OK;
}

d7_merkle_status d7_merkle_memory_block_store_copy(
    const d7_merkle_memory_block_store *source,
    d7_merkle_memory_block_store *destination) {
    if (source == NULL || source->rep == NULL || destination == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    if (source != destination) {
        d7_mst_ref_retain(&source->rep->refs);
        destination->rep = source->rep;
    }
    return D7_MERKLE_OK;
}

void d7_merkle_memory_block_store_move(
    d7_merkle_memory_block_store *destination,
    d7_merkle_memory_block_store *source) {
    if (destination != NULL && source != NULL && destination != source) {
        destination->rep = source->rep;
        source->rep = NULL;
    }
}

void d7_merkle_memory_block_store_dispose(
    d7_merkle_memory_block_store *store) {
    struct d7_merkle_memory_block_store_rep *rep;
    size_t index;
    if (store == NULL || store->rep == NULL) {
        return;
    }
    rep = store->rep;
    store->rep = NULL;
    if (!d7_mst_ref_release(&rep->refs)) {
        return;
    }
    for (index = 0; index != rep->count; ++index) {
        d7_merkle_block_dispose(&rep->blocks[index]);
    }
    d7_mst_allocator_deallocate(&rep->allocator, rep->blocks);
    d7_mst_store_lock_dispose(&rep->lock);
    d7_mst_allocator_deallocate(&rep->allocator, rep);
}

d7_merkle_status d7_merkle_memory_block_store_as_store(
    const d7_merkle_memory_block_store *memory_store,
    d7_merkle_block_store *store) {
    if (memory_store == NULL || memory_store->rep == NULL || store == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    store->count = d7_mst_memory_count;
    store->contains = d7_mst_memory_contains;
    store->try_get = d7_mst_memory_try_get;
    store->put = d7_mst_memory_put;
    store->remove = d7_mst_memory_remove;
    store->clear = d7_mst_memory_clear;
    store->context = memory_store->rep;
    return D7_MERKLE_OK;
}

d7_merkle_status d7_merkle_memory_block_store_visit_digests(
    const d7_merkle_memory_block_store *store,
    d7_merkle_digest_visitor visitor,
    void *context) {
    d7_merkle_digest *snapshot = NULL;
    size_t snapshot_capacity = 0;
    size_t count = 0;
    size_t bytes;
    size_t index;
    d7_merkle_status status = D7_MERKLE_OK;
    if (store == NULL || store->rep == NULL || visitor == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    for (;;) {
        status = d7_mst_store_lock_acquire(&store->rep->lock);
        if (status != D7_MERKLE_OK) {
            break;
        }
        count = store->rep->count;
        if (count <= snapshot_capacity) {
            for (index = 0; index != count; ++index) {
                snapshot[index] = store->rep->blocks[index].rep->digest;
            }
            d7_mst_store_lock_release(&store->rep->lock);
            break;
        }
        d7_mst_store_lock_release(&store->rep->lock);
        d7_mst_allocator_deallocate(&store->rep->allocator, snapshot);
        snapshot = NULL;
        if (d7_mst_multiply_overflows(count, sizeof(*snapshot), &bytes)) {
            status = D7_MERKLE_OVERFLOW;
            break;
        }
        snapshot = (d7_merkle_digest *)d7_mst_allocator_allocate(
            &store->rep->allocator,
            bytes);
        if (snapshot == NULL) {
            status = D7_MERKLE_NO_MEMORY;
            break;
        }
        snapshot_capacity = count;
    }
    if (status == D7_MERKLE_OK) {
        for (index = 0; index != count; ++index) {
            status = visitor(snapshot[index], context);
            if (status != D7_MERKLE_OK) {
                break;
            }
        }
    }
    d7_mst_allocator_deallocate(&store->rep->allocator, snapshot);
    return status;
}

static uint64_t d7_mst_digest_hash(d7_merkle_digest digest) {
    uint64_t value = UINT64_C(1469598103934665603);
    size_t index;
    for (index = 0; index != D7_MERKLE_DIGEST_BYTE_LENGTH; ++index) {
        value ^= digest.bytes[index];
        value *= UINT64_C(1099511628211);
    }
    return value;
}

static d7_merkle_status d7_mst_digest_table_capacity(
    size_t count,
    size_t *capacity) {
    size_t required;
    size_t value = 8;
    if (count > (SIZE_MAX - 1) / 2) {
        return D7_MERKLE_OVERFLOW;
    }
    required = count * 2 + 1;
    while (value < required) {
        if (value > SIZE_MAX / 2) {
            return D7_MERKLE_OVERFLOW;
        }
        value *= 2;
    }
    *capacity = value;
    return D7_MERKLE_OK;
}

static d7_mst_digest_slot *d7_mst_digest_table_find(
    d7_mst_digest_slot *slots,
    size_t capacity,
    d7_merkle_digest digest,
    bool *found) {
    size_t index = (size_t)(d7_mst_digest_hash(digest) & (uint64_t)(capacity - 1));
    for (;;) {
        if (!slots[index].occupied) {
            *found = false;
            return &slots[index];
        }
        if (d7_merkle_digest_equal(slots[index].digest, digest)) {
            *found = true;
            return &slots[index];
        }
        index = (index + 1) & (capacity - 1);
    }
}

static void d7_mst_pack_retain(struct d7_merkle_block_pack_rep *pack) {
    if (pack != NULL) {
        d7_mst_ref_retain(&pack->refs);
    }
}

static void d7_mst_pack_release(struct d7_merkle_block_pack_rep *pack) {
    d7_merkle_allocator allocator;
    struct d7_merkle_policy_rep *owner;
    size_t index;
    if (pack == NULL || !d7_mst_ref_release(&pack->refs)) {
        return;
    }
    allocator = pack->allocator;
    owner = pack->policy_owner;
    for (index = 0; index != pack->block_count; ++index) {
        d7_merkle_block_dispose(&pack->blocks[index]);
    }
    d7_mst_allocator_deallocate(&allocator, pack->blocks);
    d7_mst_allocator_deallocate(&allocator, pack->algorithm_id);
    d7_mst_allocator_deallocate(&allocator, pack);
    d7_mst_policy_release(owner);
}

d7_merkle_status d7_merkle_block_pack_init(
    d7_merkle_identifier algorithm_id,
    d7_merkle_digest domain_digest,
    d7_merkle_digest root_hash,
    const d7_merkle_block *blocks,
    size_t block_count,
    const d7_merkle_allocator *allocator,
    d7_merkle_block_pack *pack,
    d7_merkle_verification_error *error) {
    d7_merkle_allocator selected;
    struct d7_merkle_block_pack_rep *rep = NULL;
    d7_mst_digest_slot *slots = NULL;
    size_t capacity = 0;
    size_t bytes;
    size_t index;
    d7_merkle_status status;
    uint64_t total = 0;
    bool contains_root = false;
    if (pack == NULL || algorithm_id.bytes == NULL || algorithm_id.size == 0 ||
        (block_count != 0 && blocks == NULL) || !d7_mst_allocator_valid(allocator)) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    d7_merkle_verification_error_init(error);
    selected = d7_mst_normalize_allocator(allocator);
    status = d7_mst_digest_table_capacity(block_count, &capacity);
    if (status != D7_MERKLE_OK ||
        d7_mst_multiply_overflows(capacity, sizeof(*slots), &bytes)) {
        return D7_MERKLE_OVERFLOW;
    }
    slots = (d7_mst_digest_slot *)d7_mst_allocator_allocate(&selected, bytes);
    if (slots == NULL) {
        return D7_MERKLE_NO_MEMORY;
    }
    memset(slots, 0, bytes);
    for (index = 0; index != block_count; ++index) {
        d7_mst_digest_slot *slot;
        bool found;
        uint64_t length;
        if (blocks[index].rep == NULL) {
            status = D7_MERKLE_INVALID_ARGUMENT;
            goto cleanup;
        }
        slot = d7_mst_digest_table_find(
            slots,
            capacity,
            blocks[index].rep->digest,
            &found);
        if (found) {
            status = d7_mst_verification_fail(
                error,
                D7_MERKLE_VERIFY_DUPLICATE_BLOCK,
                &blocks[index].rep->digest);
            goto cleanup;
        }
        slot->occupied = true;
        slot->digest = blocks[index].rep->digest;
        length = (uint64_t)blocks[index].rep->byte_count;
        if (UINT64_MAX - total < length) {
            status = D7_MERKLE_OVERFLOW;
            goto cleanup;
        }
        total += length;
        contains_root = contains_root ||
            d7_merkle_digest_equal(blocks[index].rep->digest, root_hash);
    }
    rep = (struct d7_merkle_block_pack_rep *)d7_mst_allocator_allocate(
        &selected,
        sizeof(*rep));
    if (rep == NULL) {
        status = D7_MERKLE_NO_MEMORY;
        goto cleanup;
    }
    memset(rep, 0, sizeof(*rep));
    rep->allocator = selected;
    rep->policy_owner = NULL;
    rep->algorithm_id = (unsigned char *)d7_mst_allocator_allocate(
        &selected,
        algorithm_id.size);
    if (rep->algorithm_id == NULL) {
        status = D7_MERKLE_NO_MEMORY;
        goto cleanup;
    }
    memcpy(rep->algorithm_id, algorithm_id.bytes, algorithm_id.size);
    rep->algorithm_id_size = algorithm_id.size;
    rep->domain_digest = domain_digest;
    rep->root_hash = root_hash;
    rep->total_byte_count = total;
    rep->contains_root_block = contains_root;
    if (block_count != 0) {
        if (d7_mst_multiply_overflows(block_count, sizeof(*rep->blocks), &bytes)) {
            status = D7_MERKLE_OVERFLOW;
            goto cleanup;
        }
        rep->blocks = (d7_merkle_block *)d7_mst_allocator_allocate(&selected, bytes);
        if (rep->blocks == NULL) {
            status = D7_MERKLE_NO_MEMORY;
            goto cleanup;
        }
        memset(rep->blocks, 0, bytes);
        for (index = 0; index != block_count; ++index) {
            status = d7_merkle_block_copy(&blocks[index], &rep->blocks[index]);
            if (status != D7_MERKLE_OK) {
                goto cleanup;
            }
            ++rep->block_count;
        }
    }
    d7_mst_ref_init(&rep->refs);
    pack->rep = rep;
    rep = NULL;
    status = D7_MERKLE_OK;

cleanup:
    if (rep != NULL) {
        for (index = 0; index != rep->block_count; ++index) {
            d7_merkle_block_dispose(&rep->blocks[index]);
        }
        d7_mst_allocator_deallocate(&selected, rep->blocks);
        d7_mst_allocator_deallocate(&selected, rep->algorithm_id);
        d7_mst_allocator_deallocate(&selected, rep);
    }
    d7_mst_allocator_deallocate(&selected, slots);
    return status;
}

static void d7_mst_pack_attach_policy(
    d7_merkle_block_pack *pack,
    struct d7_merkle_policy_rep *policy) {
    pack->rep->policy_owner = policy;
    d7_mst_policy_retain(policy);
}

d7_merkle_status d7_merkle_block_pack_copy(
    const d7_merkle_block_pack *source,
    d7_merkle_block_pack *destination) {
    if (source == NULL || source->rep == NULL || destination == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    if (source != destination) {
        d7_mst_pack_retain(source->rep);
        destination->rep = source->rep;
    }
    return D7_MERKLE_OK;
}

void d7_merkle_block_pack_move(
    d7_merkle_block_pack *destination,
    d7_merkle_block_pack *source) {
    if (destination != NULL && source != NULL && destination != source) {
        destination->rep = source->rep;
        source->rep = NULL;
    }
}

void d7_merkle_block_pack_dispose(d7_merkle_block_pack *pack) {
    if (pack != NULL) {
        d7_mst_pack_release(pack->rep);
        pack->rep = NULL;
    }
}

d7_merkle_identifier d7_merkle_block_pack_algorithm_id(
    const d7_merkle_block_pack *pack) {
    d7_merkle_identifier empty = {NULL, 0};
    return pack == NULL || pack->rep == NULL
        ? empty
        : (d7_merkle_identifier){pack->rep->algorithm_id, pack->rep->algorithm_id_size};
}

d7_merkle_digest d7_merkle_block_pack_domain_digest(
    const d7_merkle_block_pack *pack) {
    d7_merkle_digest zero = {{0}};
    return pack == NULL || pack->rep == NULL ? zero : pack->rep->domain_digest;
}

d7_merkle_digest d7_merkle_block_pack_root_hash(
    const d7_merkle_block_pack *pack) {
    d7_merkle_digest zero = {{0}};
    return pack == NULL || pack->rep == NULL ? zero : pack->rep->root_hash;
}

size_t d7_merkle_block_pack_block_count(const d7_merkle_block_pack *pack) {
    return pack == NULL || pack->rep == NULL ? 0 : pack->rep->block_count;
}

uint64_t d7_merkle_block_pack_total_byte_count(
    const d7_merkle_block_pack *pack) {
    return pack == NULL || pack->rep == NULL ? 0 : pack->rep->total_byte_count;
}

bool d7_merkle_block_pack_contains_root_block(
    const d7_merkle_block_pack *pack) {
    return pack != NULL && pack->rep != NULL && pack->rep->contains_root_block;
}

const d7_merkle_block *d7_merkle_block_pack_block_at(
    const d7_merkle_block_pack *pack,
    size_t index) {
    return pack == NULL || pack->rep == NULL || index >= pack->rep->block_count
        ? NULL
        : &pack->rep->blocks[index];
}

typedef struct d7_mst_block_vector {
    const struct d7_merkle_policy_rep *policy;
    d7_merkle_block *items;
    size_t count;
    size_t capacity;
} d7_mst_block_vector;

typedef struct d7_mst_digest_vector {
    const struct d7_merkle_policy_rep *policy;
    d7_merkle_digest *items;
    size_t count;
    size_t capacity;
} d7_mst_digest_vector;

static void d7_mst_block_vector_dispose(d7_mst_block_vector *vector) {
    size_t index;
    for (index = 0; index != vector->count; ++index) {
        d7_merkle_block_dispose(&vector->items[index]);
    }
    d7_mst_deallocate(vector->policy, vector->items);
    memset(vector, 0, sizeof(*vector));
}

static d7_merkle_status d7_mst_block_vector_reserve(
    d7_mst_block_vector *vector,
    size_t required) {
    d7_merkle_block *items;
    size_t capacity;
    size_t bytes;
    if (required <= vector->capacity) {
        return D7_MERKLE_OK;
    }
    capacity = vector->capacity == 0 ? 8 : vector->capacity;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2) {
            return D7_MERKLE_OVERFLOW;
        }
        capacity *= 2;
    }
    if (d7_mst_multiply_overflows(capacity, sizeof(*items), &bytes)) {
        return D7_MERKLE_OVERFLOW;
    }
    items = (d7_merkle_block *)d7_mst_allocate(vector->policy, bytes);
    if (items == NULL) {
        return D7_MERKLE_NO_MEMORY;
    }
    if (vector->count != 0) {
        memcpy(items, vector->items, vector->count * sizeof(*items));
    }
    d7_mst_deallocate(vector->policy, vector->items);
    vector->items = items;
    vector->capacity = capacity;
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_block_vector_push_node(
    d7_mst_block_vector *vector,
    const struct d7_merkle_node *node) {
    d7_merkle_status status = d7_mst_block_vector_reserve(
        vector,
        vector->count + 1);
    if (status == D7_MERKLE_OK) {
        vector->items[vector->count].rep = NULL;
        status = d7_mst_block_init_policy(
            vector->policy,
            node->digest,
            node->block_bytes->data,
            node->block_bytes->size,
            &vector->items[vector->count]);
        if (status == D7_MERKLE_OK) {
            ++vector->count;
        }
    }
    return status;
}

static void d7_mst_digest_vector_dispose(d7_mst_digest_vector *vector) {
    d7_mst_deallocate(vector->policy, vector->items);
    memset(vector, 0, sizeof(*vector));
}

static d7_merkle_status d7_mst_digest_vector_push(
    d7_mst_digest_vector *vector,
    d7_merkle_digest digest) {
    d7_merkle_digest *items;
    size_t capacity;
    size_t bytes;
    if (vector->count == vector->capacity) {
        capacity = vector->capacity == 0 ? 8 : vector->capacity;
        if (vector->capacity != 0) {
            if (capacity > SIZE_MAX / 2) {
                return D7_MERKLE_OVERFLOW;
            }
            capacity *= 2;
        }
        if (d7_mst_multiply_overflows(capacity, sizeof(*items), &bytes)) {
            return D7_MERKLE_OVERFLOW;
        }
        items = (d7_merkle_digest *)d7_mst_allocate(vector->policy, bytes);
        if (items == NULL) {
            return D7_MERKLE_NO_MEMORY;
        }
        if (vector->count != 0) {
            memcpy(items, vector->items, vector->count * sizeof(*items));
        }
        d7_mst_deallocate(vector->policy, vector->items);
        vector->items = items;
        vector->capacity = capacity;
    }
    vector->items[vector->count++] = digest;
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_collect_export_blocks(
    const struct d7_merkle_node *node,
    d7_mst_block_vector *blocks) {
    struct d7_merkle_node *const *children;
    size_t index;
    d7_merkle_status status;
    if (node == NULL) {
        return D7_MERKLE_OK;
    }
    status = d7_mst_block_vector_push_node(blocks, node);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    children = d7_mst_node_children_const(node);
    for (index = 0; index != node->entry_count + 1; ++index) {
        status = d7_mst_collect_export_blocks(children[index], blocks);
        if (status != D7_MERKLE_OK) {
            return status;
        }
    }
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_pack_from_vector(
    const d7_merkle_search_tree *tree,
    const d7_mst_block_vector *blocks,
    d7_merkle_block_pack *pack,
    d7_merkle_verification_error *error) {
    const d7_merkle_identifier algorithm = {
        d7_mst_algorithm_id,
        sizeof(d7_mst_algorithm_id) - 1};
    d7_merkle_status status = d7_merkle_block_pack_init(
        algorithm,
        tree->policy->domain_digest,
        d7_merkle_search_tree_root_hash(tree),
        blocks->items,
        blocks->count,
        &tree->policy->config.allocator,
        pack,
        error);
    if (status == D7_MERKLE_OK) {
        d7_mst_pack_attach_policy(pack, tree->policy);
    }
    return status;
}

d7_merkle_status d7_merkle_search_tree_export_pack(
    const d7_merkle_search_tree *tree,
    d7_merkle_block_pack *pack) {
    d7_mst_block_vector blocks;
    d7_merkle_status status;
    if (!d7_mst_tree_valid(tree) || pack == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    memset(&blocks, 0, sizeof(blocks));
    blocks.policy = tree->policy;
    status = d7_mst_collect_export_blocks(tree->root, &blocks);
    if (status == D7_MERKLE_OK) {
        status = d7_mst_pack_from_vector(tree, &blocks, pack, NULL);
    }
    d7_mst_block_vector_dispose(&blocks);
    return status;
}

static const struct d7_merkle_node *d7_mst_find_block_digest(
    const struct d7_merkle_node *node,
    d7_merkle_digest digest) {
    struct d7_merkle_node *const *children;
    size_t index;
    if (node == NULL || d7_merkle_digest_equal(node->digest, digest)) {
        return node;
    }
    children = d7_mst_node_children_const(node);
    for (index = 0; index != node->entry_count + 1; ++index) {
        const struct d7_merkle_node *found = d7_mst_find_block_digest(
            children[index],
            digest);
        if (found != NULL) {
            return found;
        }
    }
    return NULL;
}

d7_merkle_status d7_merkle_search_tree_export_blocks(
    const d7_merkle_search_tree *tree,
    const d7_merkle_digest *digests,
    size_t digest_count,
    d7_merkle_block_pack *pack,
    d7_merkle_verification_error *error) {
    d7_mst_block_vector blocks;
    size_t index;
    d7_merkle_status status = D7_MERKLE_OK;
    if (!d7_mst_tree_valid(tree) || pack == NULL ||
        (digest_count != 0 && digests == NULL)) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    d7_merkle_verification_error_init(error);
    memset(&blocks, 0, sizeof(blocks));
    blocks.policy = tree->policy;
    for (index = 0; index != digest_count; ++index) {
        const struct d7_merkle_node *node;
        size_t prior;
        for (prior = 0; prior != index; ++prior) {
            if (d7_merkle_digest_equal(digests[prior], digests[index])) {
                status = d7_mst_verification_fail(
                    error,
                    D7_MERKLE_VERIFY_DUPLICATE_BLOCK,
                    &digests[index]);
                goto cleanup;
            }
        }
        node = d7_mst_find_block_digest(tree->root, digests[index]);
        if (node == NULL) {
            status = d7_mst_verification_fail(
                error,
                D7_MERKLE_VERIFY_MISSING_BLOCK,
                &digests[index]);
            goto cleanup;
        }
        status = d7_mst_block_vector_push_node(&blocks, node);
        if (status != D7_MERKLE_OK) {
            goto cleanup;
        }
    }
    status = d7_mst_pack_from_vector(tree, &blocks, pack, error);

cleanup:
    d7_mst_block_vector_dispose(&blocks);
    return status;
}

static d7_merkle_status d7_mst_preflight_blocks(
    const d7_merkle_block *blocks,
    size_t block_count,
    const d7_merkle_block_store *store,
    d7_merkle_verification_error *error) {
    size_t index;
    for (index = 0; index != block_count; ++index) {
        d7_merkle_block existing = {NULL};
        bool found = false;
        d7_merkle_status status = d7_merkle_block_store_try_get(
            store,
            blocks[index].rep->digest,
            &found,
            &existing);
        if (status != D7_MERKLE_OK) {
            d7_merkle_block_dispose(&existing);
            return status;
        }
        if (found && !d7_merkle_block_equal(&existing, &blocks[index])) {
            const d7_merkle_digest digest = blocks[index].rep->digest;
            d7_merkle_block_dispose(&existing);
            return d7_mst_verification_fail(
                error,
                D7_MERKLE_VERIFY_CONFLICTING_BLOCK,
                &digest);
        }
        d7_merkle_block_dispose(&existing);
    }
    return D7_MERKLE_OK;
}

d7_merkle_status d7_merkle_search_tree_save(
    const d7_merkle_search_tree *tree,
    const d7_merkle_block_store *store,
    size_t *added_block_count,
    d7_merkle_verification_error *error) {
    d7_merkle_block_pack pack = {NULL};
    size_t added = 0;
    size_t index;
    d7_merkle_status status;
    if (!d7_mst_tree_valid(tree) || !d7_mst_store_valid(store) ||
        added_block_count == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    d7_merkle_verification_error_init(error);
    status = d7_merkle_search_tree_export_pack(tree, &pack);
    if (status == D7_MERKLE_OK) {
        status = d7_mst_preflight_blocks(
            pack.rep->blocks,
            pack.rep->block_count,
            store,
            error);
    }
    for (index = 0; status == D7_MERKLE_OK && index != pack.rep->block_count; ++index) {
        d7_merkle_store_put_result put_result = D7_MERKLE_STORE_PRESENT_IDENTICAL;
        status = d7_merkle_block_store_put(
            store,
            &pack.rep->blocks[index],
            &put_result);
        if (status == D7_MERKLE_VERIFICATION_FAILURE ||
            put_result == D7_MERKLE_STORE_CONFLICT) {
            status = d7_mst_verification_fail(
                error,
                D7_MERKLE_VERIFY_CONFLICTING_BLOCK,
                &pack.rep->blocks[index].rep->digest);
        } else if (status == D7_MERKLE_OK && put_result == D7_MERKLE_STORE_ADDED) {
            ++added;
        }
    }
    if (status == D7_MERKLE_OK) {
        *added_block_count = added;
    }
    d7_merkle_block_pack_dispose(&pack);
    return status;
}

struct d7_merkle_sync_plan_rep {
    d7_mst_ref_count refs;
    d7_merkle_allocator allocator;
    struct d7_merkle_policy_rep *policy_owner;
    unsigned char *algorithm_id;
    size_t algorithm_id_size;
    d7_merkle_digest domain_digest;
    d7_merkle_digest local_root_hash;
    d7_merkle_digest target_root_hash;
    d7_merkle_digest *requested_blocks;
    size_t requested_block_count;
    size_t examined_block_count;
    uint64_t examined_byte_count;
};

static void d7_mst_sync_plan_retain(struct d7_merkle_sync_plan_rep *plan) {
    if (plan != NULL) {
        d7_mst_ref_retain(&plan->refs);
    }
}

static void d7_mst_sync_plan_release(struct d7_merkle_sync_plan_rep *plan) {
    d7_merkle_allocator allocator;
    struct d7_merkle_policy_rep *owner;
    if (plan == NULL || !d7_mst_ref_release(&plan->refs)) {
        return;
    }
    allocator = plan->allocator;
    owner = plan->policy_owner;
    d7_mst_allocator_deallocate(&allocator, plan->requested_blocks);
    d7_mst_allocator_deallocate(&allocator, plan->algorithm_id);
    d7_mst_allocator_deallocate(&allocator, plan);
    d7_mst_policy_release(owner);
}

static d7_merkle_status d7_mst_sync_plan_create(
    const d7_merkle_search_tree *target,
    d7_merkle_digest local_root_hash,
    const d7_mst_digest_vector *requested,
    size_t examined_block_count,
    uint64_t examined_byte_count,
    d7_merkle_sync_plan *plan) {
    struct d7_merkle_sync_plan_rep *rep;
    size_t bytes;
    rep = (struct d7_merkle_sync_plan_rep *)d7_mst_allocate(
        target->policy,
        sizeof(*rep));
    if (rep == NULL) {
        return D7_MERKLE_NO_MEMORY;
    }
    memset(rep, 0, sizeof(*rep));
    rep->allocator = target->policy->config.allocator;
    rep->policy_owner = target->policy;
    rep->algorithm_id_size = sizeof(d7_mst_algorithm_id) - 1;
    rep->algorithm_id = (unsigned char *)d7_mst_allocate(
        target->policy,
        rep->algorithm_id_size);
    if (rep->algorithm_id == NULL) {
        d7_mst_deallocate(target->policy, rep);
        return D7_MERKLE_NO_MEMORY;
    }
    memcpy(rep->algorithm_id, d7_mst_algorithm_id, rep->algorithm_id_size);
    if (requested->count != 0) {
        if (d7_mst_multiply_overflows(
                requested->count,
                sizeof(*rep->requested_blocks),
                &bytes)) {
            d7_mst_deallocate(target->policy, rep->algorithm_id);
            d7_mst_deallocate(target->policy, rep);
            return D7_MERKLE_OVERFLOW;
        }
        rep->requested_blocks = (d7_merkle_digest *)d7_mst_allocate(
            target->policy,
            bytes);
        if (rep->requested_blocks == NULL) {
            d7_mst_deallocate(target->policy, rep->algorithm_id);
            d7_mst_deallocate(target->policy, rep);
            return D7_MERKLE_NO_MEMORY;
        }
        memcpy(rep->requested_blocks, requested->items, bytes);
    }
    rep->requested_block_count = requested->count;
    rep->domain_digest = target->policy->domain_digest;
    rep->local_root_hash = local_root_hash;
    rep->target_root_hash = d7_merkle_search_tree_root_hash(target);
    rep->examined_block_count = examined_block_count;
    rep->examined_byte_count = examined_byte_count;
    d7_mst_policy_retain(rep->policy_owner);
    d7_mst_ref_init(&rep->refs);
    plan->rep = rep;
    return D7_MERKLE_OK;
}

d7_merkle_status d7_merkle_sync_plan_copy(
    const d7_merkle_sync_plan *source,
    d7_merkle_sync_plan *destination) {
    if (source == NULL || source->rep == NULL || destination == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    if (source != destination) {
        d7_mst_sync_plan_retain(source->rep);
        destination->rep = source->rep;
    }
    return D7_MERKLE_OK;
}

void d7_merkle_sync_plan_move(
    d7_merkle_sync_plan *destination,
    d7_merkle_sync_plan *source) {
    if (destination != NULL && source != NULL && destination != source) {
        destination->rep = source->rep;
        source->rep = NULL;
    }
}

void d7_merkle_sync_plan_dispose(d7_merkle_sync_plan *plan) {
    if (plan != NULL) {
        d7_mst_sync_plan_release(plan->rep);
        plan->rep = NULL;
    }
}

d7_merkle_identifier d7_merkle_sync_plan_algorithm_id(
    const d7_merkle_sync_plan *plan) {
    d7_merkle_identifier empty = {NULL, 0};
    return plan == NULL || plan->rep == NULL
        ? empty
        : (d7_merkle_identifier){plan->rep->algorithm_id, plan->rep->algorithm_id_size};
}

d7_merkle_digest d7_merkle_sync_plan_domain_digest(
    const d7_merkle_sync_plan *plan) {
    d7_merkle_digest zero = {{0}};
    return plan == NULL || plan->rep == NULL ? zero : plan->rep->domain_digest;
}

d7_merkle_digest d7_merkle_sync_plan_local_root_hash(
    const d7_merkle_sync_plan *plan) {
    d7_merkle_digest zero = {{0}};
    return plan == NULL || plan->rep == NULL ? zero : plan->rep->local_root_hash;
}

d7_merkle_digest d7_merkle_sync_plan_target_root_hash(
    const d7_merkle_sync_plan *plan) {
    d7_merkle_digest zero = {{0}};
    return plan == NULL || plan->rep == NULL ? zero : plan->rep->target_root_hash;
}

size_t d7_merkle_sync_plan_requested_block_count(
    const d7_merkle_sync_plan *plan) {
    return plan == NULL || plan->rep == NULL ? 0 : plan->rep->requested_block_count;
}

const d7_merkle_digest *d7_merkle_sync_plan_requested_blocks(
    const d7_merkle_sync_plan *plan) {
    return plan == NULL || plan->rep == NULL ? NULL : plan->rep->requested_blocks;
}

size_t d7_merkle_sync_plan_examined_block_count(
    const d7_merkle_sync_plan *plan) {
    return plan == NULL || plan->rep == NULL ? 0 : plan->rep->examined_block_count;
}

uint64_t d7_merkle_sync_plan_examined_byte_count(
    const d7_merkle_sync_plan *plan) {
    return plan == NULL || plan->rep == NULL ? 0 : plan->rep->examined_byte_count;
}

bool d7_merkle_sync_plan_roots_match(const d7_merkle_sync_plan *plan) {
    return plan != NULL && plan->rep != NULL && d7_merkle_digest_equal(
        plan->rep->local_root_hash,
        plan->rep->target_root_hash);
}

bool d7_merkle_sync_plan_requires_blocks(const d7_merkle_sync_plan *plan) {
    return plan != NULL && plan->rep != NULL && plan->rep->requested_block_count != 0;
}

static d7_merkle_status d7_mst_collect_missing_closure(
    const struct d7_merkle_node *node,
    const d7_merkle_block_store *store,
    d7_mst_block_vector *blocks) {
    struct d7_merkle_node *const *children;
    bool contains = false;
    size_t index;
    d7_merkle_status status;
    if (node == NULL) {
        return D7_MERKLE_OK;
    }
    status = d7_merkle_block_store_contains(store, node->digest, &contains);
    if (status != D7_MERKLE_OK || contains) {
        return status;
    }
    status = d7_mst_block_vector_push_node(blocks, node);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    children = d7_mst_node_children_const(node);
    for (index = 0; index != node->entry_count + 1; ++index) {
        status = d7_mst_collect_missing_closure(children[index], store, blocks);
        if (status != D7_MERKLE_OK) {
            return status;
        }
    }
    return D7_MERKLE_OK;
}

d7_merkle_status d7_merkle_search_tree_create_sync_pack(
    const d7_merkle_search_tree *target,
    const d7_merkle_block_store *receiver_store,
    d7_merkle_block_pack *pack) {
    d7_mst_block_vector blocks;
    d7_merkle_status status;
    if (!d7_mst_tree_valid(target) || !d7_mst_store_valid(receiver_store) ||
        pack == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    memset(&blocks, 0, sizeof(blocks));
    blocks.policy = target->policy;
    status = d7_mst_collect_missing_closure(target->root, receiver_store, &blocks);
    if (status == D7_MERKLE_OK) {
        status = d7_mst_pack_from_vector(target, &blocks, pack, NULL);
    }
    d7_mst_block_vector_dispose(&blocks);
    return status;
}

static d7_merkle_status d7_mst_collect_missing_frontier(
    const struct d7_merkle_node *node,
    const d7_merkle_block_store *store,
    d7_mst_digest_vector *requested,
    size_t *examined_block_count,
    uint64_t *examined_byte_count) {
    struct d7_merkle_node *const *children;
    bool contains = false;
    size_t index;
    d7_merkle_status status;
    if (node == NULL) {
        return D7_MERKLE_OK;
    }
    if (*examined_block_count == SIZE_MAX ||
        UINT64_MAX - *examined_byte_count < (uint64_t)node->block_bytes->size) {
        return D7_MERKLE_OVERFLOW;
    }
    ++*examined_block_count;
    *examined_byte_count += (uint64_t)node->block_bytes->size;
    status = d7_merkle_block_store_contains(store, node->digest, &contains);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    if (!contains) {
        return d7_mst_digest_vector_push(requested, node->digest);
    }
    children = d7_mst_node_children_const(node);
    for (index = 0; index != node->entry_count + 1; ++index) {
        status = d7_mst_collect_missing_frontier(
            children[index],
            store,
            requested,
            examined_block_count,
            examined_byte_count);
        if (status != D7_MERKLE_OK) {
            return status;
        }
    }
    return D7_MERKLE_OK;
}

d7_merkle_status d7_merkle_search_tree_plan_sync(
    const d7_merkle_search_tree *target,
    const d7_merkle_search_tree *local,
    const d7_merkle_block_store *receiver_store,
    d7_merkle_sync_plan *plan) {
    d7_mst_digest_vector requested;
    size_t examined_blocks = 0;
    uint64_t examined_bytes = 0;
    d7_merkle_status status = D7_MERKLE_OK;
    if (!d7_mst_typed_compatible(target, local) ||
        !d7_mst_store_valid(receiver_store) || plan == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    memset(&requested, 0, sizeof(requested));
    requested.policy = target->policy;
    if (!d7_merkle_digest_equal(
            d7_merkle_search_tree_root_hash(target),
            d7_merkle_search_tree_root_hash(local))) {
        status = d7_mst_collect_missing_frontier(
            target->root,
            receiver_store,
            &requested,
            &examined_blocks,
            &examined_bytes);
    }
    if (status == D7_MERKLE_OK) {
        status = d7_mst_sync_plan_create(
            target,
            d7_merkle_search_tree_root_hash(local),
            &requested,
            examined_blocks,
            examined_bytes,
            plan);
    }
    d7_mst_digest_vector_dispose(&requested);
    return status;
}

typedef struct d7_mst_decoded_block {
    d7_merkle_block block;
    unsigned level;
    size_t subtree_count;
    d7_mst_entry **entries;
    size_t entry_count;
    d7_merkle_digest *child_digests;
    bool proof_visited;
} d7_mst_decoded_block;

typedef struct d7_mst_verification_context {
    const struct d7_merkle_policy_rep *policy;
    d7_merkle_verification_budget budget;
    d7_merkle_verification_error *error;
    d7_mst_digest_slot *slots;
    size_t slot_capacity;
    size_t block_count;
    uint64_t total_byte_count;
    uint64_t entry_count;
    d7_merkle_digest *active;
    size_t active_count;
    size_t active_capacity;
} d7_mst_verification_context;

typedef struct d7_mst_wire_cursor {
    const unsigned char *bytes;
    size_t size;
    size_t offset;
    d7_merkle_digest digest;
    d7_mst_verification_context *context;
} d7_mst_wire_cursor;

static void d7_mst_context_publish_counts(d7_mst_verification_context *context) {
    if (context->error != NULL) {
        context->error->verified_block_count = context->block_count;
        context->error->verified_byte_count = context->total_byte_count;
    }
}

static d7_merkle_status d7_mst_context_fail(
    d7_mst_verification_context *context,
    d7_merkle_verification_failure_kind kind,
    const d7_merkle_digest *digest) {
    d7_mst_context_publish_counts(context);
    return d7_mst_verification_fail(context->error, kind, digest);
}

static void d7_mst_context_dispose(d7_mst_verification_context *context) {
    size_t index;
    if (context->slots != NULL) {
        for (index = 0; index != context->slot_capacity; ++index) {
            if (context->slots[index].occupied) {
                d7_mst_node_release(context->policy, context->slots[index].node);
            }
        }
    }
    d7_mst_deallocate(context->policy, context->active);
    d7_mst_deallocate(context->policy, context->slots);
    memset(context, 0, sizeof(*context));
}

static d7_merkle_status d7_mst_context_grow_slots(
    d7_mst_verification_context *context,
    size_t required_count) {
    d7_mst_digest_slot *slots;
    size_t capacity = context->slot_capacity == 0 ? 8 : context->slot_capacity;
    size_t bytes;
    size_t index;
    while (required_count > capacity / 2) {
        if (capacity > SIZE_MAX / 2) {
            return D7_MERKLE_OVERFLOW;
        }
        capacity *= 2;
    }
    if (capacity == context->slot_capacity) {
        return D7_MERKLE_OK;
    }
    if (d7_mst_multiply_overflows(capacity, sizeof(*slots), &bytes)) {
        return D7_MERKLE_OVERFLOW;
    }
    slots = (d7_mst_digest_slot *)d7_mst_allocate(context->policy, bytes);
    if (slots == NULL) {
        return D7_MERKLE_NO_MEMORY;
    }
    memset(slots, 0, bytes);
    for (index = 0; index != context->slot_capacity; ++index) {
        if (context->slots[index].occupied) {
            bool found;
            d7_mst_digest_slot *destination = d7_mst_digest_table_find(
                slots,
                capacity,
                context->slots[index].digest,
                &found);
            (void)found;
            *destination = context->slots[index];
        }
    }
    d7_mst_deallocate(context->policy, context->slots);
    context->slots = slots;
    context->slot_capacity = capacity;
    return D7_MERKLE_OK;
}

static d7_mst_digest_slot *d7_mst_context_find_slot(
    d7_mst_verification_context *context,
    d7_merkle_digest digest,
    bool *found) {
    if (context->slot_capacity == 0) {
        *found = false;
        return NULL;
    }
    return d7_mst_digest_table_find(
        context->slots,
        context->slot_capacity,
        digest,
        found);
}

static d7_merkle_status d7_mst_context_account_block(
    d7_mst_verification_context *context,
    const d7_merkle_block *block,
    size_t depth,
    bool *first_visit,
    d7_mst_digest_slot **slot_result) {
    d7_mst_digest_slot *slot;
    bool found;
    size_t byte_count = block->rep->byte_count;
    d7_merkle_status status;
    if (depth == 0 || depth > context->budget.max_depth) {
        return d7_mst_context_fail(
            context,
            D7_MERKLE_VERIFY_RESOURCE_LIMIT_EXCEEDED,
            &block->rep->digest);
    }
    slot = d7_mst_context_find_slot(context, block->rep->digest, &found);
    if (found) {
        *first_visit = false;
        *slot_result = slot;
        return D7_MERKLE_OK;
    }
    if (byte_count > context->budget.max_block_byte_count ||
        context->block_count >= context->budget.max_block_count ||
        (uint64_t)byte_count >
            context->budget.max_total_byte_count - context->total_byte_count) {
        return d7_mst_context_fail(
            context,
            D7_MERKLE_VERIFY_RESOURCE_LIMIT_EXCEEDED,
            &block->rep->digest);
    }
    status = d7_mst_context_grow_slots(context, context->block_count + 1);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    slot = d7_mst_digest_table_find(
        context->slots,
        context->slot_capacity,
        block->rep->digest,
        &found);
    if (found) {
        return D7_MERKLE_INCONSISTENT_POLICY;
    }
    slot->occupied = true;
    slot->digest = block->rep->digest;
    slot->value = SIZE_MAX;
    slot->node = NULL;
    ++context->block_count;
    context->total_byte_count += (uint64_t)byte_count;
    d7_mst_context_publish_counts(context);
    *first_visit = true;
    *slot_result = slot;
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_context_account_entries(
    d7_mst_verification_context *context,
    size_t count,
    d7_merkle_digest digest) {
    if ((uint64_t)count > context->budget.max_entry_count - context->entry_count) {
        return d7_mst_context_fail(
            context,
            D7_MERKLE_VERIFY_RESOURCE_LIMIT_EXCEEDED,
            &digest);
    }
    context->entry_count += (uint64_t)count;
    return D7_MERKLE_OK;
}

static bool d7_mst_wire_take(
    d7_mst_wire_cursor *cursor,
    size_t count,
    const unsigned char **bytes) {
    if (count > cursor->size - cursor->offset) {
        return false;
    }
    *bytes = cursor->bytes + cursor->offset;
    cursor->offset += count;
    return true;
}

static bool d7_mst_wire_read_be32(
    d7_mst_wire_cursor *cursor,
    uint32_t *value) {
    const unsigned char *bytes;
    if (!d7_mst_wire_take(cursor, 4, &bytes)) {
        return false;
    }
    *value = d7_mst_read_be32(bytes);
    return true;
}

static d7_merkle_status d7_mst_decode_object(
    const struct d7_merkle_policy_rep *policy,
    bool is_key,
    const d7_merkle_codec *codec,
    const unsigned char *encoding,
    size_t encoding_size,
    d7_mst_object **object_result,
    d7_mst_bytes **bytes_result) {
    const d7_merkle_type_policy *type = d7_mst_object_type(policy, is_key);
    d7_mst_object *object = NULL;
    d7_mst_bytes *bytes = NULL;
    d7_merkle_status status;
    object = (d7_mst_object *)d7_mst_allocate(policy, sizeof(*object));
    if (object == NULL) {
        return D7_MERKLE_NO_MEMORY;
    }
    memset(object, 0, sizeof(*object));
    object->value = d7_mst_allocate(policy, type->size);
    if (object->value == NULL) {
        d7_mst_deallocate(policy, object);
        return D7_MERKLE_NO_MEMORY;
    }
    status = codec->decode(
        encoding,
        encoding_size,
        object->value,
        &policy->config.allocator,
        codec->context);
    if (status != D7_MERKLE_OK) {
        d7_mst_deallocate(policy, object->value);
        d7_mst_deallocate(policy, object);
        return status;
    }
    d7_mst_ref_init(&object->refs);
    object->is_key = is_key;
    status = d7_mst_bytes_allocate(policy, encoding_size, &bytes);
    if (status != D7_MERKLE_OK) {
        d7_mst_object_release(policy, object);
        return status;
    }
    if (encoding_size != 0) {
        memcpy(bytes->data, encoding, encoding_size);
    }
    *object_result = object;
    *bytes_result = bytes;
    return D7_MERKLE_OK;
}

static bool d7_mst_wire_decode_failure(d7_merkle_status status) {
    return status == D7_MERKLE_INVALID_ARGUMENT ||
        status == D7_MERKLE_INVALID_ENCODING ||
        status == D7_MERKLE_INCONSISTENT_POLICY;
}

static void d7_mst_decoded_block_dispose(
    const struct d7_merkle_policy_rep *policy,
    d7_mst_decoded_block *block) {
    size_t index;
    if (block->entries != NULL) {
        for (index = 0; index != block->entry_count; ++index) {
            d7_mst_entry_release(policy, block->entries[index]);
        }
    }
    d7_mst_deallocate(policy, block->child_digests);
    d7_mst_deallocate(policy, block->entries);
    d7_merkle_block_dispose(&block->block);
    memset(block, 0, sizeof(*block));
}

static d7_merkle_status d7_mst_decode_wire_block(
    const d7_merkle_search_tree *verifier,
    const d7_merkle_block *block,
    d7_mst_verification_context *context,
    size_t depth,
    d7_mst_decoded_block *decoded) {
    d7_mst_wire_cursor cursor;
    d7_mst_digest_slot *slot = NULL;
    const unsigned char *bytes;
    d7_merkle_digest actual_digest;
    uint32_t subtree_count32;
    uint32_t entry_count32;
    size_t minimum_length;
    size_t child_bytes;
    size_t entries_bytes;
    size_t children_bytes;
    size_t index;
    bool first_visit;
    d7_merkle_status status;
    memset(decoded, 0, sizeof(*decoded));
    status = d7_mst_context_account_block(
        context,
        block,
        depth,
        &first_visit,
        &slot);
    (void)slot;
    if (status != D7_MERKLE_OK) {
        return status;
    }
    status = d7_mst_sha256_config(
        &verifier->policy->config,
        block->rep->bytes,
        block->rep->byte_count,
        &actual_digest);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    if (!d7_merkle_digest_equal(actual_digest, block->rep->digest)) {
        return d7_mst_context_fail(
            context,
            D7_MERKLE_VERIFY_DIGEST_MISMATCH,
            &block->rep->digest);
    }
    if (block->rep->byte_count <
        D7_MST_BLOCK_HEADER_LENGTH + D7_MERKLE_DIGEST_BYTE_LENGTH) {
        return d7_mst_context_fail(
            context,
            D7_MERKLE_VERIFY_MALFORMED_BLOCK,
            &block->rep->digest);
    }
    cursor = (d7_mst_wire_cursor){
        block->rep->bytes,
        block->rep->byte_count,
        0,
        block->rep->digest,
        context};
    if (!d7_mst_wire_take(&cursor, sizeof(d7_mst_block_magic), &bytes) ||
        memcmp(bytes, d7_mst_block_magic, sizeof(d7_mst_block_magic)) != 0 ||
        !d7_mst_wire_take(&cursor, 1, &bytes) || bytes[0] != D7_MST_NODE_BLOCK_TAG) {
        return d7_mst_context_fail(
            context,
            D7_MERKLE_VERIFY_MALFORMED_BLOCK,
            &block->rep->digest);
    }
    if (!d7_mst_wire_take(&cursor, D7_MERKLE_DIGEST_BYTE_LENGTH, &bytes)) {
        return d7_mst_context_fail(
            context,
            D7_MERKLE_VERIFY_MALFORMED_BLOCK,
            &block->rep->digest);
    }
    if (memcmp(
            bytes,
            verifier->policy->domain_digest.bytes,
            D7_MERKLE_DIGEST_BYTE_LENGTH) != 0) {
        return d7_mst_context_fail(
            context,
            D7_MERKLE_VERIFY_DOMAIN_MISMATCH,
            &block->rep->digest);
    }
    if (!d7_mst_wire_take(&cursor, 1, &bytes) || bytes[0] > D7_MST_MAXIMUM_LEVEL ||
        !d7_mst_wire_read_be32(&cursor, &subtree_count32) ||
        !d7_mst_wire_read_be32(&cursor, &entry_count32) ||
        subtree_count32 == 0 || entry_count32 == 0 ||
        subtree_count32 > INT32_MAX || entry_count32 > INT32_MAX ||
        subtree_count32 < entry_count32) {
        return d7_mst_context_fail(
            context,
            D7_MERKLE_VERIFY_MALFORMED_BLOCK,
            &block->rep->digest);
    }
    decoded->level = bytes[0];
    decoded->subtree_count = (size_t)subtree_count32;
    decoded->entry_count = (size_t)entry_count32;
    if (decoded->entry_count >= context->budget.max_child_references_per_block) {
        return d7_mst_context_fail(
            context,
            D7_MERKLE_VERIFY_RESOURCE_LIMIT_EXCEEDED,
            &block->rep->digest);
    }
    if (d7_mst_multiply_overflows(
            decoded->entry_count + 1,
            D7_MERKLE_DIGEST_BYTE_LENGTH,
            &child_bytes) ||
        d7_mst_multiply_overflows(
            decoded->entry_count,
            8 + D7_MERKLE_DIGEST_BYTE_LENGTH,
            &minimum_length) ||
        d7_mst_add_overflows(
            minimum_length,
            D7_MST_BLOCK_HEADER_LENGTH + D7_MERKLE_DIGEST_BYTE_LENGTH,
            &minimum_length) ||
        minimum_length > block->rep->byte_count) {
        return d7_mst_context_fail(
            context,
            D7_MERKLE_VERIFY_MALFORMED_BLOCK,
            &block->rep->digest);
    }
    (void)child_bytes;
    if (first_visit) {
        status = d7_mst_context_account_entries(
            context,
            decoded->entry_count,
            block->rep->digest);
        if (status != D7_MERKLE_OK) {
            return status;
        }
    }
    if (d7_mst_multiply_overflows(
            decoded->entry_count,
            sizeof(*decoded->entries),
            &entries_bytes) ||
        d7_mst_multiply_overflows(
            decoded->entry_count + 1,
            sizeof(*decoded->child_digests),
            &children_bytes)) {
        return D7_MERKLE_OVERFLOW;
    }
    decoded->entries = (d7_mst_entry **)d7_mst_allocate(
        verifier->policy,
        entries_bytes);
    if (decoded->entries == NULL) {
        status = D7_MERKLE_NO_MEMORY;
        goto cleanup;
    }
    memset(decoded->entries, 0, entries_bytes);
    decoded->child_digests = (d7_merkle_digest *)d7_mst_allocate(
        verifier->policy,
        children_bytes);
    if (decoded->child_digests == NULL) {
        status = D7_MERKLE_NO_MEMORY;
        goto cleanup;
    }
    status = d7_merkle_block_copy(block, &decoded->block);
    if (status != D7_MERKLE_OK) {
        goto cleanup;
    }
    for (index = 0; index != decoded->entry_count; ++index) {
        uint32_t key_length;
        uint32_t value_length;
        const unsigned char *key_encoding;
        const unsigned char *value_encoding;
        d7_mst_object *key = NULL;
        d7_mst_object *value = NULL;
        d7_mst_bytes *key_bytes = NULL;
        d7_mst_bytes *value_bytes = NULL;
        d7_mst_bytes *canonical = NULL;
        d7_merkle_digest key_digest;
        if (!d7_mst_wire_read_be32(&cursor, &key_length) || key_length > INT32_MAX ||
            !d7_mst_wire_take(&cursor, (size_t)key_length, &key_encoding) ||
            !d7_mst_wire_read_be32(&cursor, &value_length) || value_length > INT32_MAX ||
            !d7_mst_wire_take(&cursor, (size_t)value_length, &value_encoding)) {
            status = d7_mst_context_fail(
                context,
                D7_MERKLE_VERIFY_MALFORMED_BLOCK,
                &block->rep->digest);
            goto entry_cleanup;
        }
        status = d7_mst_decode_object(
            verifier->policy,
            true,
            &verifier->policy->config.key_codec,
            key_encoding,
            (size_t)key_length,
            &key,
            &key_bytes);
        if (status != D7_MERKLE_OK) {
            if (d7_mst_wire_decode_failure(status)) {
                status = d7_mst_context_fail(
                    context,
                    D7_MERKLE_VERIFY_MALFORMED_BLOCK,
                    &block->rep->digest);
            }
            goto entry_cleanup;
        }
        status = d7_mst_encode_value(
            verifier->policy,
            &verifier->policy->config.key_codec,
            key->value,
            &canonical);
        if (status != D7_MERKLE_OK || canonical->size != key_bytes->size ||
            memcmp(canonical->data, key_bytes->data, key_bytes->size) != 0) {
            if (status == D7_MERKLE_OK || d7_mst_wire_decode_failure(status)) {
                status = d7_mst_context_fail(
                    context,
                    D7_MERKLE_VERIFY_NONCANONICAL_BLOCK,
                    &block->rep->digest);
            }
            goto entry_cleanup;
        }
        d7_mst_bytes_release(verifier->policy, canonical);
        canonical = NULL;
        status = d7_mst_decode_object(
            verifier->policy,
            false,
            &verifier->policy->config.value_codec,
            value_encoding,
            (size_t)value_length,
            &value,
            &value_bytes);
        if (status != D7_MERKLE_OK) {
            if (d7_mst_wire_decode_failure(status)) {
                status = d7_mst_context_fail(
                    context,
                    D7_MERKLE_VERIFY_MALFORMED_BLOCK,
                    &block->rep->digest);
            }
            goto entry_cleanup;
        }
        status = d7_mst_encode_value(
            verifier->policy,
            &verifier->policy->config.value_codec,
            value->value,
            &canonical);
        if (status != D7_MERKLE_OK || canonical->size != value_bytes->size ||
            memcmp(canonical->data, value_bytes->data, value_bytes->size) != 0) {
            if (status == D7_MERKLE_OK || d7_mst_wire_decode_failure(status)) {
                status = d7_mst_context_fail(
                    context,
                    D7_MERKLE_VERIFY_NONCANONICAL_BLOCK,
                    &block->rep->digest);
            }
            goto entry_cleanup;
        }
        d7_mst_bytes_release(verifier->policy, canonical);
        canonical = NULL;
        status = d7_mst_hash_key_bytes(verifier->policy, key_bytes, &key_digest);
        if (status == D7_MERKLE_OK && d7_mst_level(key_digest) != decoded->level) {
            status = d7_mst_context_fail(
                context,
                D7_MERKLE_VERIFY_NONCANONICAL_BLOCK,
                &block->rep->digest);
        }
        if (status == D7_MERKLE_OK) {
            status = d7_mst_entry_from_parts(
                verifier->policy,
                key,
                value,
                key_bytes,
                value_bytes,
                decoded->level,
                &decoded->entries[index]);
        }
        if (status == D7_MERKLE_OK && index != 0) {
            int comparison = 0;
            status = d7_mst_key_compare(
                verifier->policy,
                decoded->entries[index - 1]->key->value,
                decoded->entries[index]->key->value,
                &comparison);
            if (status == D7_MERKLE_OK && comparison >= 0) {
                status = d7_mst_context_fail(
                    context,
                    D7_MERKLE_VERIFY_NONCANONICAL_BLOCK,
                    &block->rep->digest);
            }
        }

entry_cleanup:
        d7_mst_bytes_release(verifier->policy, canonical);
        d7_mst_bytes_release(verifier->policy, value_bytes);
        d7_mst_bytes_release(verifier->policy, key_bytes);
        d7_mst_object_release(verifier->policy, value);
        d7_mst_object_release(verifier->policy, key);
        if (status != D7_MERKLE_OK) {
            goto cleanup;
        }
    }
    for (index = 0; index != decoded->entry_count + 1; ++index) {
        if (!d7_mst_wire_take(
                &cursor,
                D7_MERKLE_DIGEST_BYTE_LENGTH,
                &bytes)) {
            status = d7_mst_context_fail(
                context,
                D7_MERKLE_VERIFY_MALFORMED_BLOCK,
                &block->rep->digest);
            goto cleanup;
        }
        memcpy(
            decoded->child_digests[index].bytes,
            bytes,
            D7_MERKLE_DIGEST_BYTE_LENGTH);
    }
    if (cursor.offset != cursor.size) {
        status = d7_mst_context_fail(
            context,
            D7_MERKLE_VERIFY_NONCANONICAL_BLOCK,
            &block->rep->digest);
        goto cleanup;
    }
    return D7_MERKLE_OK;

cleanup:
    d7_mst_decoded_block_dispose(verifier->policy, decoded);
    return status;
}

static d7_merkle_status d7_mst_context_push_active(
    d7_mst_verification_context *context,
    d7_merkle_digest digest) {
    d7_merkle_digest *active;
    size_t capacity;
    size_t bytes;
    size_t index;
    for (index = 0; index != context->active_count; ++index) {
        if (d7_merkle_digest_equal(context->active[index], digest)) {
            return d7_mst_context_fail(
                context,
                D7_MERKLE_VERIFY_CYCLE_DETECTED,
                &digest);
        }
    }
    if (context->active_count == context->active_capacity) {
        capacity = context->active_capacity == 0 ? 8 : context->active_capacity;
        if (context->active_capacity != 0) {
            if (capacity > SIZE_MAX / 2) {
                return D7_MERKLE_OVERFLOW;
            }
            capacity *= 2;
        }
        if (d7_mst_multiply_overflows(capacity, sizeof(*active), &bytes)) {
            return D7_MERKLE_OVERFLOW;
        }
        active = (d7_merkle_digest *)d7_mst_allocate(context->policy, bytes);
        if (active == NULL) {
            return D7_MERKLE_NO_MEMORY;
        }
        if (context->active_count != 0) {
            memcpy(active, context->active, context->active_count * sizeof(*active));
        }
        d7_mst_deallocate(context->policy, context->active);
        context->active = active;
        context->active_capacity = capacity;
    }
    context->active[context->active_count++] = digest;
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_validate_loaded_reference(
    const struct d7_merkle_policy_rep *policy,
    const d7_mst_decoded_block *parent,
    size_t child_index,
    const struct d7_merkle_node *child,
    d7_mst_verification_context *context) {
    int comparison;
    d7_merkle_status status;
    if (child->level >= parent->level) {
        return d7_mst_context_fail(
            context,
            D7_MERKLE_VERIFY_INVALID_REFERENCE,
            &child->digest);
    }
    if (child_index != 0) {
        status = d7_mst_key_compare(
            policy,
            child->minimum_entry->key->value,
            parent->entries[child_index - 1]->key->value,
            &comparison);
        if (status != D7_MERKLE_OK) {
            return status;
        }
        if (comparison <= 0) {
            return d7_mst_context_fail(
                context,
                D7_MERKLE_VERIFY_INVALID_REFERENCE,
                &child->digest);
        }
    }
    if (child_index != parent->entry_count) {
        status = d7_mst_key_compare(
            policy,
            child->maximum_entry->key->value,
            parent->entries[child_index]->key->value,
            &comparison);
        if (status != D7_MERKLE_OK) {
            return status;
        }
        if (comparison >= 0) {
            return d7_mst_context_fail(
                context,
                D7_MERKLE_VERIFY_INVALID_REFERENCE,
                &child->digest);
        }
    }
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_load_node(
    const d7_merkle_search_tree *verifier,
    d7_merkle_digest digest,
    const d7_merkle_block_store *store,
    d7_mst_verification_context *context,
    size_t depth,
    struct d7_merkle_node **node_result) {
    d7_mst_digest_slot *slot;
    d7_mst_decoded_block decoded;
    d7_merkle_block block = {NULL};
    struct d7_merkle_node **children = NULL;
    struct d7_merkle_node *node = NULL;
    size_t children_bytes;
    size_t actual_count;
    size_t index;
    bool found;
    bool block_found = false;
    bool pushed = false;
    d7_merkle_status status;
    if (depth == 0 || depth > context->budget.max_depth) {
        return d7_mst_context_fail(
            context,
            D7_MERKLE_VERIFY_RESOURCE_LIMIT_EXCEEDED,
            &digest);
    }
    slot = d7_mst_context_find_slot(context, digest, &found);
    if (found && slot->node != NULL) {
        d7_mst_node_retain(slot->node);
        *node_result = slot->node;
        return D7_MERKLE_OK;
    }
    status = d7_mst_context_push_active(context, digest);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    pushed = true;
    status = d7_merkle_block_store_try_get(
        store,
        digest,
        &block_found,
        &block);
    if (status != D7_MERKLE_OK) {
        goto cleanup;
    }
    if (block_found != (block.rep != NULL)) {
        status = D7_MERKLE_CALLBACK_FAILURE;
        goto cleanup;
    }
    if (!block_found) {
        status = d7_mst_context_fail(
            context,
            D7_MERKLE_VERIFY_MISSING_BLOCK,
            &digest);
        goto cleanup;
    }
    if (!d7_merkle_digest_equal(block.rep->digest, digest)) {
        status = d7_mst_context_fail(
            context,
            D7_MERKLE_VERIFY_DIGEST_MISMATCH,
            &digest);
        goto cleanup;
    }
    status = d7_mst_decode_wire_block(verifier, &block, context, depth, &decoded);
    if (status != D7_MERKLE_OK) {
        goto cleanup;
    }
    if (d7_mst_multiply_overflows(
            decoded.entry_count + 1,
            sizeof(*children),
            &children_bytes)) {
        status = D7_MERKLE_OVERFLOW;
        goto decoded_cleanup;
    }
    children = (struct d7_merkle_node **)d7_mst_allocate(
        verifier->policy,
        children_bytes);
    if (children == NULL) {
        status = D7_MERKLE_NO_MEMORY;
        goto decoded_cleanup;
    }
    memset(children, 0, children_bytes);
    for (index = 0; index != decoded.entry_count + 1; ++index) {
        if (!d7_merkle_digest_equal(
                decoded.child_digests[index],
                verifier->policy->empty_digest)) {
            status = d7_mst_load_node(
                verifier,
                decoded.child_digests[index],
                store,
                context,
                depth + 1,
                &children[index]);
            if (status != D7_MERKLE_OK) {
                goto decoded_cleanup;
            }
            status = d7_mst_validate_loaded_reference(
                verifier->policy,
                &decoded,
                index,
                children[index],
                context);
            if (status != D7_MERKLE_OK) {
                goto decoded_cleanup;
            }
        }
    }
    actual_count = decoded.entry_count;
    for (index = 0; index != decoded.entry_count + 1; ++index) {
        if (children[index] != NULL &&
            d7_mst_add_overflows(actual_count, children[index]->count, &actual_count)) {
            status = D7_MERKLE_OVERFLOW;
            goto decoded_cleanup;
        }
    }
    if (actual_count != decoded.subtree_count) {
        status = d7_mst_context_fail(
            context,
            D7_MERKLE_VERIFY_INVALID_REFERENCE,
            &digest);
        goto decoded_cleanup;
    }
    status = d7_mst_node_create(
        verifier->policy,
        decoded.level,
        decoded.entries,
        decoded.entry_count,
        children,
        &node);
    if (status != D7_MERKLE_OK) {
        goto decoded_cleanup;
    }
    if (!d7_merkle_digest_equal(node->digest, digest) ||
        node->block_bytes->size != block.rep->byte_count ||
        memcmp(node->block_bytes->data, block.rep->bytes, block.rep->byte_count) != 0) {
        status = d7_mst_context_fail(
            context,
            D7_MERKLE_VERIFY_NONCANONICAL_BLOCK,
            &digest);
        goto decoded_cleanup;
    }
    slot = d7_mst_context_find_slot(context, digest, &found);
    if (!found || slot == NULL || slot->node != NULL) {
        status = D7_MERKLE_INCONSISTENT_POLICY;
        goto decoded_cleanup;
    }
    slot->node = node;
    d7_mst_node_retain(node);
    *node_result = node;
    node = NULL;

decoded_cleanup:
    d7_mst_node_release(verifier->policy, node);
    if (children != NULL) {
        for (index = 0; index != decoded.entry_count + 1; ++index) {
            d7_mst_node_release(verifier->policy, children[index]);
        }
    }
    d7_mst_deallocate(verifier->policy, children);
    d7_mst_decoded_block_dispose(verifier->policy, &decoded);

cleanup:
    d7_merkle_block_dispose(&block);
    if (pushed) {
        --context->active_count;
    }
    return status;
}

static d7_merkle_status d7_mst_load_with_context(
    d7_merkle_digest root_hash,
    const d7_merkle_policy *policy,
    const d7_merkle_block_store *store,
    d7_mst_verification_context *context,
    d7_merkle_search_tree *tree) {
    d7_merkle_search_tree verifier = {NULL, NULL};
    d7_merkle_search_tree loaded = {NULL, NULL};
    struct d7_merkle_node *root = NULL;
    bool valid = false;
    d7_merkle_search_tree_statistics statistics;
    d7_merkle_status status = d7_merkle_search_tree_init(&verifier, policy);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    if (d7_merkle_digest_equal(root_hash, policy->rep->empty_digest)) {
        *tree = verifier;
        return D7_MERKLE_OK;
    }
    status = d7_mst_load_node(
        &verifier,
        root_hash,
        store,
        context,
        1,
        &root);
    if (status == D7_MERKLE_OK && !d7_merkle_digest_equal(root->digest, root_hash)) {
        status = d7_mst_context_fail(
            context,
            D7_MERKLE_VERIFY_ROOT_MISMATCH,
            &root_hash);
    }
    if (status == D7_MERKLE_OK) {
        loaded = d7_mst_adopt_tree(policy->rep, root);
        root = NULL;
        status = d7_merkle_search_tree_validate(&loaded, &valid, &statistics);
        if (status == D7_MERKLE_OK && !valid) {
            status = d7_mst_context_fail(
                context,
                D7_MERKLE_VERIFY_INVALID_REFERENCE,
                &root_hash);
        }
    }
    if (status == D7_MERKLE_OK) {
        *tree = loaded;
        memset(&loaded, 0, sizeof(loaded));
    }
    d7_merkle_search_tree_dispose(&loaded);
    d7_mst_node_release(policy->rep, root);
    d7_merkle_search_tree_dispose(&verifier);
    return status;
}

static d7_merkle_status d7_mst_prepare_context(
    const d7_merkle_policy *policy,
    const d7_merkle_verification_budget *budget,
    d7_merkle_verification_error *error,
    d7_mst_verification_context *context) {
    d7_merkle_verification_budget selected;
    if (policy == NULL || policy->rep == NULL || context == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    if (budget == NULL) {
        d7_merkle_verification_budget_init_default(&selected);
    } else {
        selected = *budget;
    }
    if (d7_merkle_verification_budget_validate(&selected) != D7_MERKLE_OK) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    d7_merkle_verification_error_init(error);
    memset(context, 0, sizeof(*context));
    context->policy = policy->rep;
    context->budget = selected;
    context->error = error;
    return D7_MERKLE_OK;
}

d7_merkle_status d7_merkle_search_tree_load(
    d7_merkle_digest root_hash,
    const d7_merkle_policy *policy,
    const d7_merkle_block_store *store,
    const d7_merkle_verification_budget *budget,
    d7_merkle_search_tree *tree,
    d7_merkle_verification_error *error) {
    d7_mst_verification_context context;
    d7_merkle_search_tree loaded = {NULL, NULL};
    d7_merkle_status status;
    if (!d7_mst_store_valid(store) || tree == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    status = d7_mst_prepare_context(policy, budget, error, &context);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    status = d7_mst_load_with_context(
        root_hash,
        policy,
        store,
        &context,
        &loaded);
    d7_mst_context_publish_counts(&context);
    if (status == D7_MERKLE_OK) {
        *tree = loaded;
        memset(&loaded, 0, sizeof(loaded));
    }
    d7_merkle_search_tree_dispose(&loaded);
    d7_mst_context_dispose(&context);
    return status;
}

typedef struct d7_mst_overlay_store {
    const struct d7_merkle_block_pack_rep *pack;
    const d7_merkle_block_store *fallback;
} d7_mst_overlay_store;

static const d7_merkle_block *d7_mst_overlay_find(
    const d7_mst_overlay_store *overlay,
    d7_merkle_digest digest) {
    size_t index;
    for (index = 0; index != overlay->pack->block_count; ++index) {
        if (d7_merkle_digest_equal(
                overlay->pack->blocks[index].rep->digest,
                digest)) {
            return &overlay->pack->blocks[index];
        }
    }
    return NULL;
}

static d7_merkle_status d7_mst_overlay_count(size_t *count, void *context) {
    const d7_mst_overlay_store *overlay = (const d7_mst_overlay_store *)context;
    size_t fallback_count = 0;
    d7_merkle_status status = overlay->fallback == NULL
        ? D7_MERKLE_OK
        : d7_merkle_block_store_count(overlay->fallback, &fallback_count);
    if (status == D7_MERKLE_OK) {
        if (overlay->pack->block_count > SIZE_MAX - fallback_count) {
            return D7_MERKLE_OVERFLOW;
        }
        *count = overlay->pack->block_count + fallback_count;
    }
    return status;
}

static d7_merkle_status d7_mst_overlay_contains(
    d7_merkle_digest digest,
    bool *contains,
    void *context) {
    const d7_mst_overlay_store *overlay = (const d7_mst_overlay_store *)context;
    if (d7_mst_overlay_find(overlay, digest) != NULL) {
        *contains = true;
        return D7_MERKLE_OK;
    }
    if (overlay->fallback == NULL) {
        *contains = false;
        return D7_MERKLE_OK;
    }
    return d7_merkle_block_store_contains(overlay->fallback, digest, contains);
}

static d7_merkle_status d7_mst_overlay_try_get(
    d7_merkle_digest digest,
    bool *found,
    d7_merkle_block *block,
    void *context) {
    const d7_mst_overlay_store *overlay = (const d7_mst_overlay_store *)context;
    const d7_merkle_block *staged = d7_mst_overlay_find(overlay, digest);
    if (staged != NULL) {
        *found = true;
        return d7_merkle_block_copy(staged, block);
    }
    if (overlay->fallback == NULL) {
        *found = false;
        return D7_MERKLE_OK;
    }
    return d7_merkle_block_store_try_get(overlay->fallback, digest, found, block);
}

static d7_merkle_status d7_mst_overlay_put(
    const d7_merkle_block *block,
    d7_merkle_store_put_result *result,
    void *context) {
    (void)block;
    (void)result;
    (void)context;
    return D7_MERKLE_INVALID_ARGUMENT;
}

static d7_merkle_status d7_mst_overlay_remove(
    d7_merkle_digest digest,
    bool *removed,
    void *context) {
    (void)digest;
    (void)removed;
    (void)context;
    return D7_MERKLE_INVALID_ARGUMENT;
}

static d7_merkle_status d7_mst_overlay_clear(void *context) {
    (void)context;
    return D7_MERKLE_INVALID_ARGUMENT;
}

static bool d7_mst_algorithm_matches(d7_merkle_identifier identifier) {
    return identifier.size == sizeof(d7_mst_algorithm_id) - 1 &&
        memcmp(identifier.bytes, d7_mst_algorithm_id, identifier.size) == 0;
}

d7_merkle_status d7_merkle_search_tree_import_pack(
    const d7_merkle_block_pack *pack,
    const d7_merkle_policy *policy,
    const d7_merkle_block_store *destination_store,
    const d7_merkle_verification_budget *budget,
    d7_merkle_search_tree *tree,
    d7_merkle_verification_error *error) {
    d7_mst_verification_context context;
    d7_mst_overlay_store overlay;
    d7_merkle_block_store overlay_store;
    d7_merkle_search_tree verifier = {NULL, NULL};
    d7_merkle_search_tree loaded = {NULL, NULL};
    size_t index;
    d7_merkle_status status;
    if (pack == NULL || pack->rep == NULL || policy == NULL || policy->rep == NULL ||
        tree == NULL || (destination_store != NULL && !d7_mst_store_valid(destination_store))) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    d7_merkle_verification_error_init(error);
    if (!d7_mst_algorithm_matches(d7_merkle_block_pack_algorithm_id(pack))) {
        return d7_mst_verification_fail(
            error,
            D7_MERKLE_VERIFY_UNSUPPORTED_ALGORITHM,
            NULL);
    }
    if (!d7_merkle_digest_equal(pack->rep->domain_digest, policy->rep->domain_digest)) {
        return d7_mst_verification_fail(
            error,
            D7_MERKLE_VERIFY_DOMAIN_MISMATCH,
            NULL);
    }
    status = d7_mst_prepare_context(policy, budget, error, &context);
    if (status != D7_MERKLE_OK) {
        return status;
    }
    status = d7_merkle_search_tree_init(&verifier, policy);
    for (index = 0; status == D7_MERKLE_OK && index != pack->rep->block_count; ++index) {
        d7_mst_decoded_block decoded;
        status = d7_mst_decode_wire_block(
            &verifier,
            &pack->rep->blocks[index],
            &context,
            1,
            &decoded);
        if (status == D7_MERKLE_OK) {
            d7_mst_decoded_block_dispose(verifier.policy, &decoded);
        }
    }
    overlay.pack = pack->rep;
    overlay.fallback = destination_store;
    overlay_store = (d7_merkle_block_store){
        d7_mst_overlay_count,
        d7_mst_overlay_contains,
        d7_mst_overlay_try_get,
        d7_mst_overlay_put,
        d7_mst_overlay_remove,
        d7_mst_overlay_clear,
        &overlay};
    if (status == D7_MERKLE_OK) {
        status = d7_mst_load_with_context(
            pack->rep->root_hash,
            policy,
            &overlay_store,
            &context,
            &loaded);
    }
    if (status == D7_MERKLE_OK && destination_store != NULL) {
        status = d7_mst_preflight_blocks(
            pack->rep->blocks,
            pack->rep->block_count,
            destination_store,
            error);
    }
    for (index = 0; status == D7_MERKLE_OK && destination_store != NULL &&
        index != pack->rep->block_count; ++index) {
        d7_merkle_store_put_result put_result = D7_MERKLE_STORE_PRESENT_IDENTICAL;
        status = d7_merkle_block_store_put(
            destination_store,
            &pack->rep->blocks[index],
            &put_result);
        if (status == D7_MERKLE_VERIFICATION_FAILURE ||
            put_result == D7_MERKLE_STORE_CONFLICT) {
            status = d7_mst_context_fail(
                &context,
                D7_MERKLE_VERIFY_CONFLICTING_BLOCK,
                &pack->rep->blocks[index].rep->digest);
        }
    }
    d7_mst_context_publish_counts(&context);
    if (status == D7_MERKLE_OK) {
        *tree = loaded;
        memset(&loaded, 0, sizeof(loaded));
    }
    d7_merkle_search_tree_dispose(&loaded);
    d7_merkle_search_tree_dispose(&verifier);
    d7_mst_context_dispose(&context);
    return status;
}

typedef struct d7_mst_proof_step {
    d7_merkle_block block;
    size_t *expanded_child_indexes;
    size_t expanded_child_count;
} d7_mst_proof_step;

struct d7_merkle_proof_rep {
    d7_mst_ref_count refs;
    d7_merkle_allocator allocator;
    struct d7_merkle_policy_rep *policy_owner;
    unsigned char *algorithm_id;
    size_t algorithm_id_size;
    d7_merkle_digest domain_digest;
    d7_merkle_digest root_hash;
    d7_merkle_proof_kind kind;
    unsigned char *query;
    size_t query_byte_count;
    d7_mst_proof_step *steps;
    size_t step_count;
    uint64_t total_byte_count;
};

static int d7_mst_size_compare(const void *left, const void *right) {
    const size_t left_value = *(const size_t *)left;
    const size_t right_value = *(const size_t *)right;
    return (left_value > right_value) - (left_value < right_value);
}

static void d7_mst_proof_retain(struct d7_merkle_proof_rep *proof) {
    if (proof != NULL) {
        d7_mst_ref_retain(&proof->refs);
    }
}

static void d7_mst_proof_release(struct d7_merkle_proof_rep *proof) {
    d7_merkle_allocator allocator;
    struct d7_merkle_policy_rep *owner;
    size_t index;
    if (proof == NULL || !d7_mst_ref_release(&proof->refs)) {
        return;
    }
    allocator = proof->allocator;
    owner = proof->policy_owner;
    for (index = 0; index != proof->step_count; ++index) {
        d7_merkle_block_dispose(&proof->steps[index].block);
        d7_mst_allocator_deallocate(
            &allocator,
            proof->steps[index].expanded_child_indexes);
    }
    d7_mst_allocator_deallocate(&allocator, proof->steps);
    d7_mst_allocator_deallocate(&allocator, proof->query);
    d7_mst_allocator_deallocate(&allocator, proof->algorithm_id);
    d7_mst_allocator_deallocate(&allocator, proof);
    d7_mst_policy_release(owner);
}

d7_merkle_status d7_merkle_proof_init(
    d7_merkle_identifier algorithm_id,
    d7_merkle_digest domain_digest,
    d7_merkle_digest root_hash,
    d7_merkle_proof_kind kind,
    const unsigned char *query,
    size_t query_byte_count,
    const d7_merkle_proof_step_input *steps,
    size_t step_count,
    const d7_merkle_allocator *allocator,
    d7_merkle_proof *proof,
    d7_merkle_verification_error *error) {
    d7_merkle_allocator selected;
    struct d7_merkle_proof_rep *rep = NULL;
    d7_mst_digest_slot *slots = NULL;
    size_t slot_capacity = 0;
    size_t bytes;
    size_t index;
    uint64_t total;
    d7_merkle_status status;
    if (proof == NULL || algorithm_id.bytes == NULL || algorithm_id.size == 0 ||
        (query_byte_count != 0 && query == NULL) ||
        (step_count != 0 && steps == NULL) ||
        (kind != D7_MERKLE_PROOF_MEMBERSHIP &&
            kind != D7_MERKLE_PROOF_NONMEMBERSHIP &&
            kind != D7_MERKLE_PROOF_RANGE) ||
        !d7_mst_allocator_valid(allocator)) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    d7_merkle_verification_error_init(error);
    selected = d7_mst_normalize_allocator(allocator);
    status = d7_mst_digest_table_capacity(step_count, &slot_capacity);
    if (status != D7_MERKLE_OK ||
        d7_mst_multiply_overflows(slot_capacity, sizeof(*slots), &bytes)) {
        return D7_MERKLE_OVERFLOW;
    }
    slots = (d7_mst_digest_slot *)d7_mst_allocator_allocate(&selected, bytes);
    if (slots == NULL) {
        return D7_MERKLE_NO_MEMORY;
    }
    memset(slots, 0, bytes);
    total = (uint64_t)query_byte_count;
    for (index = 0; index != step_count; ++index) {
        d7_mst_digest_slot *slot;
        bool found;
        uint64_t block_bytes;
        if (steps[index].block == NULL || steps[index].block->rep == NULL ||
            (steps[index].expanded_child_count != 0 &&
                steps[index].expanded_child_indexes == NULL)) {
            status = D7_MERKLE_INVALID_ARGUMENT;
            goto cleanup;
        }
        slot = d7_mst_digest_table_find(
            slots,
            slot_capacity,
            steps[index].block->rep->digest,
            &found);
        if (found) {
            status = d7_mst_verification_fail(
                error,
                D7_MERKLE_VERIFY_DUPLICATE_BLOCK,
                &steps[index].block->rep->digest);
            goto cleanup;
        }
        slot->occupied = true;
        slot->digest = steps[index].block->rep->digest;
        block_bytes = (uint64_t)steps[index].block->rep->byte_count;
        if (UINT64_MAX - total < block_bytes) {
            status = D7_MERKLE_OVERFLOW;
            goto cleanup;
        }
        total += block_bytes;
    }
    rep = (struct d7_merkle_proof_rep *)d7_mst_allocator_allocate(
        &selected,
        sizeof(*rep));
    if (rep == NULL) {
        status = D7_MERKLE_NO_MEMORY;
        goto cleanup;
    }
    memset(rep, 0, sizeof(*rep));
    rep->allocator = selected;
    rep->algorithm_id = (unsigned char *)d7_mst_allocator_allocate(
        &selected,
        algorithm_id.size);
    if (rep->algorithm_id == NULL) {
        status = D7_MERKLE_NO_MEMORY;
        goto cleanup;
    }
    memcpy(rep->algorithm_id, algorithm_id.bytes, algorithm_id.size);
    rep->algorithm_id_size = algorithm_id.size;
    if (query_byte_count != 0) {
        rep->query = (unsigned char *)d7_mst_allocator_allocate(
            &selected,
            query_byte_count);
        if (rep->query == NULL) {
            status = D7_MERKLE_NO_MEMORY;
            goto cleanup;
        }
        memcpy(rep->query, query, query_byte_count);
    }
    rep->query_byte_count = query_byte_count;
    if (step_count != 0) {
        if (d7_mst_multiply_overflows(step_count, sizeof(*rep->steps), &bytes)) {
            status = D7_MERKLE_OVERFLOW;
            goto cleanup;
        }
        rep->steps = (d7_mst_proof_step *)d7_mst_allocator_allocate(&selected, bytes);
        if (rep->steps == NULL) {
            status = D7_MERKLE_NO_MEMORY;
            goto cleanup;
        }
        memset(rep->steps, 0, bytes);
        for (index = 0; index != step_count; ++index) {
            size_t expanded_bytes;
            status = d7_merkle_block_copy(
                steps[index].block,
                &rep->steps[index].block);
            if (status != D7_MERKLE_OK) {
                goto cleanup;
            }
            ++rep->step_count;
            rep->steps[index].expanded_child_count = steps[index].expanded_child_count;
            if (steps[index].expanded_child_count != 0) {
                if (d7_mst_multiply_overflows(
                        steps[index].expanded_child_count,
                        sizeof(*rep->steps[index].expanded_child_indexes),
                        &expanded_bytes)) {
                    status = D7_MERKLE_OVERFLOW;
                    goto cleanup;
                }
                rep->steps[index].expanded_child_indexes = (size_t *)
                    d7_mst_allocator_allocate(&selected, expanded_bytes);
                if (rep->steps[index].expanded_child_indexes == NULL) {
                    status = D7_MERKLE_NO_MEMORY;
                    goto cleanup;
                }
                memcpy(
                    rep->steps[index].expanded_child_indexes,
                    steps[index].expanded_child_indexes,
                    expanded_bytes);
                qsort(
                    rep->steps[index].expanded_child_indexes,
                    steps[index].expanded_child_count,
                    sizeof(*rep->steps[index].expanded_child_indexes),
                    d7_mst_size_compare);
                {
                    size_t child_index;
                    for (child_index = 1;
                        child_index != steps[index].expanded_child_count;
                        ++child_index) {
                        if (rep->steps[index].expanded_child_indexes[child_index - 1] ==
                            rep->steps[index].expanded_child_indexes[child_index]) {
                            status = d7_mst_verification_fail(
                                error,
                                D7_MERKLE_VERIFY_PROOF_MISMATCH,
                                &steps[index].block->rep->digest);
                            goto cleanup;
                        }
                    }
                }
            }
        }
    }
    rep->domain_digest = domain_digest;
    rep->root_hash = root_hash;
    rep->kind = kind;
    rep->total_byte_count = total;
    d7_mst_ref_init(&rep->refs);
    proof->rep = rep;
    rep = NULL;
    status = D7_MERKLE_OK;

cleanup:
    if (rep != NULL) {
        if (rep->steps != NULL) {
            for (index = 0; index != rep->step_count; ++index) {
                d7_merkle_block_dispose(&rep->steps[index].block);
                d7_mst_allocator_deallocate(
                    &selected,
                    rep->steps[index].expanded_child_indexes);
            }
        }
        d7_mst_allocator_deallocate(&selected, rep->steps);
        d7_mst_allocator_deallocate(&selected, rep->query);
        d7_mst_allocator_deallocate(&selected, rep->algorithm_id);
        d7_mst_allocator_deallocate(&selected, rep);
    }
    d7_mst_allocator_deallocate(&selected, slots);
    return status;
}

static void d7_mst_proof_attach_policy(
    d7_merkle_proof *proof,
    struct d7_merkle_policy_rep *policy) {
    proof->rep->policy_owner = policy;
    d7_mst_policy_retain(policy);
}

d7_merkle_status d7_merkle_proof_copy(
    const d7_merkle_proof *source,
    d7_merkle_proof *destination) {
    if (source == NULL || source->rep == NULL || destination == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    if (source != destination) {
        d7_mst_proof_retain(source->rep);
        destination->rep = source->rep;
    }
    return D7_MERKLE_OK;
}

void d7_merkle_proof_move(
    d7_merkle_proof *destination,
    d7_merkle_proof *source) {
    if (destination != NULL && source != NULL && destination != source) {
        destination->rep = source->rep;
        source->rep = NULL;
    }
}

void d7_merkle_proof_dispose(d7_merkle_proof *proof) {
    if (proof != NULL) {
        d7_mst_proof_release(proof->rep);
        proof->rep = NULL;
    }
}

d7_merkle_proof_kind d7_merkle_proof_kind_value(const d7_merkle_proof *proof) {
    return proof == NULL || proof->rep == NULL
        ? D7_MERKLE_PROOF_MEMBERSHIP
        : proof->rep->kind;
}

d7_merkle_digest d7_merkle_proof_root_hash(const d7_merkle_proof *proof) {
    d7_merkle_digest zero = {{0}};
    return proof == NULL || proof->rep == NULL ? zero : proof->rep->root_hash;
}

d7_merkle_digest d7_merkle_proof_domain_digest(const d7_merkle_proof *proof) {
    d7_merkle_digest zero = {{0}};
    return proof == NULL || proof->rep == NULL ? zero : proof->rep->domain_digest;
}

d7_merkle_identifier d7_merkle_proof_algorithm_id(const d7_merkle_proof *proof) {
    d7_merkle_identifier empty = {NULL, 0};
    return proof == NULL || proof->rep == NULL
        ? empty
        : (d7_merkle_identifier){proof->rep->algorithm_id, proof->rep->algorithm_id_size};
}

const unsigned char *d7_merkle_proof_query(const d7_merkle_proof *proof) {
    return proof == NULL || proof->rep == NULL ? NULL : proof->rep->query;
}

size_t d7_merkle_proof_query_byte_count(const d7_merkle_proof *proof) {
    return proof == NULL || proof->rep == NULL ? 0 : proof->rep->query_byte_count;
}

size_t d7_merkle_proof_step_count(const d7_merkle_proof *proof) {
    return proof == NULL || proof->rep == NULL ? 0 : proof->rep->step_count;
}

const d7_merkle_block *d7_merkle_proof_step_block(
    const d7_merkle_proof *proof,
    size_t step_index) {
    return proof == NULL || proof->rep == NULL || step_index >= proof->rep->step_count
        ? NULL
        : &proof->rep->steps[step_index].block;
}

const size_t *d7_merkle_proof_step_expanded_children(
    const d7_merkle_proof *proof,
    size_t step_index,
    size_t *expanded_child_count) {
    if (proof == NULL || proof->rep == NULL ||
        step_index >= proof->rep->step_count || expanded_child_count == NULL) {
        return NULL;
    }
    *expanded_child_count = proof->rep->steps[step_index].expanded_child_count;
    return proof->rep->steps[step_index].expanded_child_indexes;
}

uint64_t d7_merkle_proof_total_byte_count(const d7_merkle_proof *proof) {
    return proof == NULL || proof->rep == NULL ? 0 : proof->rep->total_byte_count;
}

static d7_merkle_status d7_mst_encode_proof_query(
    const struct d7_merkle_policy_rep *policy,
    d7_merkle_proof_kind kind,
    const d7_mst_bytes *first,
    const d7_mst_bytes *second,
    unsigned char **query,
    size_t *query_size) {
    static const unsigned char magic[] = {'M', 'S', 'P', '2'};
    size_t size = sizeof(magic) + 1 + 4;
    size_t offset = 0;
    unsigned char *bytes;
    if (first == NULL || first->size > INT32_MAX ||
        d7_mst_add_overflows(size, first->size, &size)) {
        return D7_MERKLE_OVERFLOW;
    }
    if (second != NULL &&
        (second->size > INT32_MAX || d7_mst_add_overflows(size, 4, &size) ||
            d7_mst_add_overflows(size, second->size, &size))) {
        return D7_MERKLE_OVERFLOW;
    }
    bytes = (unsigned char *)d7_mst_allocate(policy, size);
    if (bytes == NULL) {
        return D7_MERKLE_NO_MEMORY;
    }
    memcpy(bytes + offset, magic, sizeof(magic));
    offset += sizeof(magic);
    bytes[offset++] = (unsigned char)kind;
    d7_mst_write_be32((uint32_t)first->size, bytes + offset);
    offset += 4;
    if (first->size != 0) {
        memcpy(bytes + offset, first->data, first->size);
        offset += first->size;
    }
    if (second != NULL) {
        d7_mst_write_be32((uint32_t)second->size, bytes + offset);
        offset += 4;
        if (second->size != 0) {
            memcpy(bytes + offset, second->data, second->size);
            offset += second->size;
        }
    }
    if (offset != size) {
        d7_mst_deallocate(policy, bytes);
        return D7_MERKLE_INCONSISTENT_POLICY;
    }
    *query = bytes;
    *query_size = size;
    return D7_MERKLE_OK;
}

d7_merkle_status d7_merkle_search_tree_create_proof(
    const d7_merkle_search_tree *tree,
    const void *key,
    d7_merkle_proof *proof) {
    d7_merkle_block blocks[D7_MST_MAXIMUM_HEIGHT];
    d7_merkle_proof_step_input steps[D7_MST_MAXIMUM_HEIGHT];
    size_t expanded[D7_MST_MAXIMUM_HEIGHT];
    const struct d7_merkle_node *node;
    d7_mst_bytes *key_bytes = NULL;
    const d7_mst_bytes *value_bytes = NULL;
    unsigned char *query = NULL;
    size_t query_size = 0;
    size_t step_count = 0;
    size_t index;
    d7_merkle_proof_kind kind = D7_MERKLE_PROOF_NONMEMBERSHIP;
    d7_merkle_status status;
    if (!d7_mst_tree_valid(tree) || key == NULL || proof == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    memset(blocks, 0, sizeof(blocks));
    memset(steps, 0, sizeof(steps));
    status = d7_mst_encode_value(
        tree->policy,
        &tree->policy->config.key_codec,
        key,
        &key_bytes);
    node = tree->root;
    while (status == D7_MERKLE_OK && node != NULL) {
        d7_mst_entry *const *entries = d7_mst_node_entries_const(node);
        struct d7_merkle_node *const *children = d7_mst_node_children_const(node);
        size_t position = 0;
        bool found = false;
        if (step_count == D7_MST_MAXIMUM_HEIGHT) {
            status = D7_MERKLE_OVERFLOW;
            break;
        }
        status = d7_mst_find_position(
            tree->policy,
            entries,
            node->entry_count,
            key,
            &position,
            &found);
        if (status != D7_MERKLE_OK) {
            break;
        }
        status = d7_mst_block_init_policy(
            tree->policy,
            node->digest,
            node->block_bytes->data,
            node->block_bytes->size,
            &blocks[step_count]);
        if (status != D7_MERKLE_OK) {
            break;
        }
        steps[step_count].block = &blocks[step_count];
        if (!found && children[position] != NULL) {
            expanded[step_count] = position;
            steps[step_count].expanded_child_indexes = &expanded[step_count];
            steps[step_count].expanded_child_count = 1;
        }
        ++step_count;
        if (found) {
            kind = D7_MERKLE_PROOF_MEMBERSHIP;
            value_bytes = entries[position]->value_bytes;
            break;
        }
        node = children[position];
    }
    if (status == D7_MERKLE_OK) {
        status = d7_mst_encode_proof_query(
            tree->policy,
            kind,
            key_bytes,
            value_bytes,
            &query,
            &query_size);
    }
    if (status == D7_MERKLE_OK) {
        const d7_merkle_identifier algorithm = {
            d7_mst_algorithm_id,
            sizeof(d7_mst_algorithm_id) - 1};
        status = d7_merkle_proof_init(
            algorithm,
            tree->policy->domain_digest,
            d7_merkle_search_tree_root_hash(tree),
            kind,
            query,
            query_size,
            steps,
            step_count,
            &tree->policy->config.allocator,
            proof,
            NULL);
        if (status == D7_MERKLE_OK) {
            d7_mst_proof_attach_policy(proof, tree->policy);
        }
    }
    for (index = 0; index != step_count; ++index) {
        d7_merkle_block_dispose(&blocks[index]);
    }
    d7_mst_deallocate(tree->policy, query);
    d7_mst_bytes_release(tree->policy, key_bytes);
    return status;
}

typedef struct d7_mst_range_proof_builder {
    const d7_merkle_search_tree *tree;
    const void *minimum_key;
    const void *maximum_key;
    d7_merkle_block *blocks;
    d7_merkle_proof_step_input *steps;
    size_t count;
    size_t capacity;
} d7_mst_range_proof_builder;

static void d7_mst_range_builder_dispose(d7_mst_range_proof_builder *builder) {
    size_t index;
    for (index = 0; index != builder->count; ++index) {
        d7_merkle_block_dispose(&builder->blocks[index]);
        d7_mst_deallocate(
            builder->tree->policy,
            (void *)builder->steps[index].expanded_child_indexes);
    }
    d7_mst_deallocate(builder->tree->policy, builder->steps);
    d7_mst_deallocate(builder->tree->policy, builder->blocks);
}

static d7_merkle_status d7_mst_range_interval_intersects(
    const d7_merkle_search_tree *tree,
    d7_mst_entry *const *entries,
    size_t entry_count,
    size_t child_index,
    const void *minimum_key,
    const void *maximum_key,
    bool *intersects) {
    int comparison;
    d7_merkle_status status;
    *intersects = true;
    if (child_index != 0) {
        status = d7_mst_key_compare(
            tree->policy,
            entries[child_index - 1]->key->value,
            maximum_key,
            &comparison);
        if (status != D7_MERKLE_OK) {
            return status;
        }
        *intersects = comparison < 0;
    }
    if (*intersects && child_index != entry_count) {
        status = d7_mst_key_compare(
            tree->policy,
            entries[child_index]->key->value,
            minimum_key,
            &comparison);
        if (status != D7_MERKLE_OK) {
            return status;
        }
        *intersects = comparison > 0;
    }
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_collect_range_proof(
    const struct d7_merkle_node *node,
    d7_mst_range_proof_builder *builder) {
    d7_mst_entry *const *entries;
    struct d7_merkle_node *const *children;
    size_t *expanded = NULL;
    size_t expanded_count = 0;
    size_t expanded_bytes;
    size_t step_index;
    size_t index;
    d7_merkle_status status = D7_MERKLE_OK;
    if (node == NULL) {
        return D7_MERKLE_OK;
    }
    if (builder->count == builder->capacity) {
        return D7_MERKLE_INCONSISTENT_POLICY;
    }
    entries = d7_mst_node_entries_const(node);
    children = d7_mst_node_children_const(node);
    if (d7_mst_multiply_overflows(
            node->entry_count + 1,
            sizeof(*expanded),
            &expanded_bytes)) {
        return D7_MERKLE_OVERFLOW;
    }
    expanded = (size_t *)d7_mst_allocate(builder->tree->policy, expanded_bytes);
    if (expanded == NULL) {
        return D7_MERKLE_NO_MEMORY;
    }
    for (index = 0; index != node->entry_count + 1; ++index) {
        bool intersects = false;
        status = d7_mst_range_interval_intersects(
            builder->tree,
            entries,
            node->entry_count,
            index,
            builder->minimum_key,
            builder->maximum_key,
            &intersects);
        if (status != D7_MERKLE_OK) {
            goto cleanup;
        }
        if (children[index] != NULL && intersects) {
            expanded[expanded_count++] = index;
        }
    }
    step_index = builder->count;
    status = d7_mst_block_init_policy(
        builder->tree->policy,
        node->digest,
        node->block_bytes->data,
        node->block_bytes->size,
        &builder->blocks[step_index]);
    if (status != D7_MERKLE_OK) {
        goto cleanup;
    }
    builder->steps[step_index].block = &builder->blocks[step_index];
    builder->steps[step_index].expanded_child_indexes = expanded;
    builder->steps[step_index].expanded_child_count = expanded_count;
    ++builder->count;
    expanded = NULL;
    for (index = 0; index != expanded_count; ++index) {
        status = d7_mst_collect_range_proof(
            children[builder->steps[step_index].expanded_child_indexes[index]],
            builder);
        if (status != D7_MERKLE_OK) {
            return status;
        }
    }
    return D7_MERKLE_OK;

cleanup:
    d7_mst_deallocate(builder->tree->policy, expanded);
    return status;
}

d7_merkle_status d7_merkle_search_tree_create_range_proof(
    const d7_merkle_search_tree *tree,
    const void *minimum_key,
    const void *maximum_key,
    d7_merkle_proof *proof) {
    d7_mst_range_proof_builder builder;
    d7_mst_bytes *minimum_bytes = NULL;
    d7_mst_bytes *maximum_bytes = NULL;
    unsigned char *query = NULL;
    size_t query_size = 0;
    size_t block_bytes;
    size_t step_bytes;
    int comparison = 0;
    d7_merkle_status status;
    if (!d7_mst_tree_valid(tree) || minimum_key == NULL || maximum_key == NULL ||
        proof == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    status = d7_mst_key_compare(
        tree->policy,
        minimum_key,
        maximum_key,
        &comparison);
    if (status != D7_MERKLE_OK || comparison > 0) {
        return status == D7_MERKLE_OK ? D7_MERKLE_INVALID_ARGUMENT : status;
    }
    memset(&builder, 0, sizeof(builder));
    builder.tree = tree;
    builder.minimum_key = minimum_key;
    builder.maximum_key = maximum_key;
    builder.capacity = d7_merkle_search_tree_block_count(tree);
    if (builder.capacity != 0) {
        if (d7_mst_multiply_overflows(
                builder.capacity,
                sizeof(*builder.blocks),
                &block_bytes) ||
            d7_mst_multiply_overflows(
                builder.capacity,
                sizeof(*builder.steps),
                &step_bytes)) {
            return D7_MERKLE_OVERFLOW;
        }
        builder.blocks = (d7_merkle_block *)d7_mst_allocate(tree->policy, block_bytes);
        builder.steps = (d7_merkle_proof_step_input *)d7_mst_allocate(
            tree->policy,
            step_bytes);
        if (builder.blocks == NULL || builder.steps == NULL) {
            status = D7_MERKLE_NO_MEMORY;
            goto cleanup;
        }
        memset(builder.blocks, 0, block_bytes);
        memset(builder.steps, 0, step_bytes);
    }
    status = d7_mst_encode_value(
        tree->policy,
        &tree->policy->config.key_codec,
        minimum_key,
        &minimum_bytes);
    if (status == D7_MERKLE_OK) {
        status = d7_mst_encode_value(
            tree->policy,
            &tree->policy->config.key_codec,
            maximum_key,
            &maximum_bytes);
    }
    if (status == D7_MERKLE_OK) {
        status = d7_mst_collect_range_proof(tree->root, &builder);
    }
    if (status == D7_MERKLE_OK) {
        status = d7_mst_encode_proof_query(
            tree->policy,
            D7_MERKLE_PROOF_RANGE,
            minimum_bytes,
            maximum_bytes,
            &query,
            &query_size);
    }
    if (status == D7_MERKLE_OK) {
        const d7_merkle_identifier algorithm = {
            d7_mst_algorithm_id,
            sizeof(d7_mst_algorithm_id) - 1};
        status = d7_merkle_proof_init(
            algorithm,
            tree->policy->domain_digest,
            d7_merkle_search_tree_root_hash(tree),
            D7_MERKLE_PROOF_RANGE,
            query,
            query_size,
            builder.steps,
            builder.count,
            &tree->policy->config.allocator,
            proof,
            NULL);
        if (status == D7_MERKLE_OK) {
            d7_mst_proof_attach_policy(proof, tree->policy);
        }
    }

cleanup:
    d7_mst_deallocate(tree->policy, query);
    d7_mst_bytes_release(tree->policy, maximum_bytes);
    d7_mst_bytes_release(tree->policy, minimum_bytes);
    d7_mst_range_builder_dispose(&builder);
    return status;
}

typedef struct d7_mst_decoded_query {
    d7_mst_object *first;
    d7_mst_bytes *first_bytes;
    d7_mst_object *second;
    d7_mst_bytes *second_bytes;
} d7_mst_decoded_query;

static void d7_mst_decoded_query_dispose(
    const struct d7_merkle_policy_rep *policy,
    d7_mst_decoded_query *query) {
    d7_mst_bytes_release(policy, query->second_bytes);
    d7_mst_object_release(policy, query->second);
    d7_mst_bytes_release(policy, query->first_bytes);
    d7_mst_object_release(policy, query->first);
    memset(query, 0, sizeof(*query));
}

static d7_merkle_status d7_mst_decode_query_field(
    const d7_merkle_search_tree *verifier,
    bool is_key,
    const d7_merkle_codec *codec,
    const unsigned char *encoding,
    size_t encoding_size,
    d7_mst_object **object,
    d7_mst_bytes **owned_bytes,
    d7_mst_verification_context *context,
    d7_merkle_digest root_hash) {
    d7_mst_bytes *canonical = NULL;
    d7_merkle_status status = d7_mst_decode_object(
        verifier->policy,
        is_key,
        codec,
        encoding,
        encoding_size,
        object,
        owned_bytes);
    if (status != D7_MERKLE_OK) {
        return d7_mst_wire_decode_failure(status)
            ? d7_mst_context_fail(
                context,
                D7_MERKLE_VERIFY_PROOF_MISMATCH,
                &root_hash)
            : status;
    }
    status = d7_mst_encode_value(
        verifier->policy,
        codec,
        (*object)->value,
        &canonical);
    if (status != D7_MERKLE_OK || canonical->size != (*owned_bytes)->size ||
        memcmp(canonical->data, (*owned_bytes)->data, canonical->size) != 0) {
        if (status == D7_MERKLE_OK || d7_mst_wire_decode_failure(status)) {
            status = d7_mst_context_fail(
                context,
                D7_MERKLE_VERIFY_PROOF_MISMATCH,
                &root_hash);
        }
    }
    d7_mst_bytes_release(verifier->policy, canonical);
    return status;
}

static d7_merkle_status d7_mst_decode_proof_query(
    const d7_merkle_search_tree *verifier,
    const d7_merkle_proof *proof,
    d7_mst_verification_context *context,
    d7_mst_decoded_query *query) {
    static const unsigned char magic[] = {'M', 'S', 'P', '2'};
    d7_mst_wire_cursor cursor;
    const unsigned char *bytes;
    const unsigned char *first_encoding;
    const unsigned char *second_encoding = NULL;
    uint32_t first_length;
    uint32_t second_length = 0;
    d7_merkle_status status;
    memset(query, 0, sizeof(*query));
    cursor = (d7_mst_wire_cursor){
        proof->rep->query,
        proof->rep->query_byte_count,
        0,
        proof->rep->root_hash,
        context};
    if (!d7_mst_wire_take(&cursor, sizeof(magic), &bytes) ||
        memcmp(bytes, magic, sizeof(magic)) != 0 ||
        !d7_mst_wire_take(&cursor, 1, &bytes) ||
        bytes[0] != (unsigned char)proof->rep->kind ||
        !d7_mst_wire_read_be32(&cursor, &first_length) || first_length > INT32_MAX ||
        !d7_mst_wire_take(&cursor, (size_t)first_length, &first_encoding)) {
        return d7_mst_context_fail(
            context,
            D7_MERKLE_VERIFY_PROOF_MISMATCH,
            &proof->rep->root_hash);
    }
    if (proof->rep->kind == D7_MERKLE_PROOF_MEMBERSHIP ||
        proof->rep->kind == D7_MERKLE_PROOF_RANGE) {
        if (!d7_mst_wire_read_be32(&cursor, &second_length) || second_length > INT32_MAX ||
            !d7_mst_wire_take(&cursor, (size_t)second_length, &second_encoding)) {
            return d7_mst_context_fail(
                context,
                D7_MERKLE_VERIFY_PROOF_MISMATCH,
                &proof->rep->root_hash);
        }
    }
    if (cursor.offset != cursor.size) {
        return d7_mst_context_fail(
            context,
            D7_MERKLE_VERIFY_PROOF_MISMATCH,
            &proof->rep->root_hash);
    }
    status = d7_mst_decode_query_field(
        verifier,
        true,
        &verifier->policy->config.key_codec,
        first_encoding,
        (size_t)first_length,
        &query->first,
        &query->first_bytes,
        context,
        proof->rep->root_hash);
    if (status == D7_MERKLE_OK &&
        proof->rep->kind == D7_MERKLE_PROOF_MEMBERSHIP) {
        status = d7_mst_decode_query_field(
            verifier,
            false,
            &verifier->policy->config.value_codec,
            second_encoding,
            (size_t)second_length,
            &query->second,
            &query->second_bytes,
            context,
            proof->rep->root_hash);
    } else if (status == D7_MERKLE_OK &&
        proof->rep->kind == D7_MERKLE_PROOF_RANGE) {
        status = d7_mst_decode_query_field(
            verifier,
            true,
            &verifier->policy->config.key_codec,
            second_encoding,
            (size_t)second_length,
            &query->second,
            &query->second_bytes,
            context,
            proof->rep->root_hash);
        if (status == D7_MERKLE_OK) {
            int comparison = 0;
            status = d7_mst_key_compare(
                verifier->policy,
                query->first->value,
                query->second->value,
                &comparison);
            if (status == D7_MERKLE_OK && comparison > 0) {
                status = d7_mst_context_fail(
                    context,
                    D7_MERKLE_VERIFY_PROOF_MISMATCH,
                    &proof->rep->root_hash);
            }
        }
    }
    if (status != D7_MERKLE_OK) {
        d7_mst_decoded_query_dispose(verifier->policy, query);
    }
    return status;
}

static bool d7_mst_proof_expansion_equals(
    const d7_mst_proof_step *step,
    const size_t *expected,
    size_t expected_count) {
    return step->expanded_child_count == expected_count &&
        (expected_count == 0 || memcmp(
            step->expanded_child_indexes,
            expected,
            expected_count * sizeof(*expected)) == 0);
}

static d7_mst_decoded_block *d7_mst_find_decoded_proof_block(
    d7_mst_verification_context *context,
    d7_mst_decoded_block *blocks,
    d7_merkle_digest digest) {
    bool found;
    d7_mst_digest_slot *slot = d7_mst_context_find_slot(context, digest, &found);
    return !found || slot == NULL || slot->value == SIZE_MAX
        ? NULL
        : &blocks[slot->value];
}

static d7_merkle_status d7_mst_validate_proof_reference(
    const d7_merkle_search_tree *verifier,
    const d7_mst_decoded_block *parent,
    size_t child_index,
    const d7_mst_decoded_block *child,
    d7_mst_verification_context *context) {
    size_t index;
    if (child->level >= parent->level) {
        return d7_mst_context_fail(
            context,
            D7_MERKLE_VERIFY_INVALID_REFERENCE,
            &child->block.rep->digest);
    }
    for (index = 0; index != child->entry_count; ++index) {
        int comparison;
        d7_merkle_status status;
        if (child_index != 0) {
            status = d7_mst_key_compare(
                verifier->policy,
                child->entries[index]->key->value,
                parent->entries[child_index - 1]->key->value,
                &comparison);
            if (status != D7_MERKLE_OK) {
                return status;
            }
            if (comparison <= 0) {
                return d7_mst_context_fail(
                    context,
                    D7_MERKLE_VERIFY_INVALID_REFERENCE,
                    &child->block.rep->digest);
            }
        }
        if (child_index != parent->entry_count) {
            status = d7_mst_key_compare(
                verifier->policy,
                child->entries[index]->key->value,
                parent->entries[child_index]->key->value,
                &comparison);
            if (status != D7_MERKLE_OK) {
                return status;
            }
            if (comparison >= 0) {
                return d7_mst_context_fail(
                    context,
                    D7_MERKLE_VERIFY_INVALID_REFERENCE,
                    &child->block.rep->digest);
            }
        }
    }
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_verify_point_proof(
    const d7_merkle_search_tree *verifier,
    const d7_merkle_proof *proof,
    const d7_mst_decoded_query *query,
    d7_mst_decoded_block *blocks,
    d7_mst_verification_context *context,
    size_t *visited_count) {
    d7_merkle_digest digest = proof->rep->root_hash;
    size_t depth = 1;
    for (;;) {
        d7_mst_decoded_block *block = d7_mst_find_decoded_proof_block(
            context,
            blocks,
            digest);
        d7_mst_proof_step *step;
        size_t position = 0;
        bool found = false;
        d7_merkle_status status;
        if (depth > context->budget.max_depth) {
            return d7_mst_context_fail(
                context,
                D7_MERKLE_VERIFY_RESOURCE_LIMIT_EXCEEDED,
                &digest);
        }
        if (block == NULL) {
            return d7_mst_context_fail(
                context,
                D7_MERKLE_VERIFY_MISSING_BLOCK,
                &digest);
        }
        if (block->proof_visited) {
            return d7_mst_context_fail(
                context,
                D7_MERKLE_VERIFY_CYCLE_DETECTED,
                &digest);
        }
        block->proof_visited = true;
        ++*visited_count;
        step = &proof->rep->steps[
            (size_t)(block - blocks)];
        status = d7_mst_find_position(
            verifier->policy,
            block->entries,
            block->entry_count,
            query->first->value,
            &position,
            &found);
        if (status != D7_MERKLE_OK) {
            return status;
        }
        if (found) {
            if (!d7_mst_proof_expansion_equals(step, NULL, 0) ||
                proof->rep->kind != D7_MERKLE_PROOF_MEMBERSHIP ||
                query->second_bytes == NULL ||
                block->entries[position]->value_bytes->size != query->second_bytes->size ||
                memcmp(
                    block->entries[position]->value_bytes->data,
                    query->second_bytes->data,
                    query->second_bytes->size) != 0) {
                return d7_mst_context_fail(
                    context,
                    D7_MERKLE_VERIFY_PROOF_MISMATCH,
                    &digest);
            }
            return D7_MERKLE_OK;
        }
        if (d7_merkle_digest_equal(
                block->child_digests[position],
                verifier->policy->empty_digest)) {
            if (!d7_mst_proof_expansion_equals(step, NULL, 0) ||
                proof->rep->kind != D7_MERKLE_PROOF_NONMEMBERSHIP) {
                return d7_mst_context_fail(
                    context,
                    D7_MERKLE_VERIFY_PROOF_MISMATCH,
                    &digest);
            }
            return D7_MERKLE_OK;
        }
        if (!d7_mst_proof_expansion_equals(step, &position, 1)) {
            return d7_mst_context_fail(
                context,
                D7_MERKLE_VERIFY_PROOF_MISMATCH,
                &digest);
        }
        {
            d7_mst_decoded_block *child = d7_mst_find_decoded_proof_block(
                context,
                blocks,
                block->child_digests[position]);
            if (child == NULL) {
                return d7_mst_context_fail(
                    context,
                    D7_MERKLE_VERIFY_MISSING_BLOCK,
                    &block->child_digests[position]);
            }
            status = d7_mst_validate_proof_reference(
                verifier,
                block,
                position,
                child,
                context);
            if (status != D7_MERKLE_OK) {
                return status;
            }
            digest = block->child_digests[position];
        }
        ++depth;
    }
}

static d7_merkle_status d7_mst_verify_range_proof_node(
    const d7_merkle_search_tree *verifier,
    const d7_merkle_proof *proof,
    const d7_mst_decoded_query *query,
    d7_mst_decoded_block *blocks,
    d7_mst_verification_context *context,
    d7_merkle_digest digest,
    size_t depth,
    size_t *visited_count) {
    d7_mst_decoded_block *block;
    d7_mst_proof_step *step;
    size_t expected_offset = 0;
    size_t index;
    d7_merkle_status status;
    if (depth == 0 || depth > context->budget.max_depth) {
        return d7_mst_context_fail(
            context,
            D7_MERKLE_VERIFY_RESOURCE_LIMIT_EXCEEDED,
            &digest);
    }
    block = d7_mst_find_decoded_proof_block(context, blocks, digest);
    if (block == NULL) {
        return d7_mst_context_fail(
            context,
            D7_MERKLE_VERIFY_MISSING_BLOCK,
            &digest);
    }
    if (block->proof_visited) {
        return d7_mst_context_fail(
            context,
            D7_MERKLE_VERIFY_CYCLE_DETECTED,
            &digest);
    }
    block->proof_visited = true;
    ++*visited_count;
    step = &proof->rep->steps[(size_t)(block - blocks)];
    for (index = 0; index != block->entry_count + 1; ++index) {
        bool intersects = false;
        status = d7_mst_range_interval_intersects(
            verifier,
            block->entries,
            block->entry_count,
            index,
            query->first->value,
            query->second->value,
            &intersects);
        if (status != D7_MERKLE_OK) {
            return status;
        }
        if (intersects && !d7_merkle_digest_equal(
                block->child_digests[index],
                verifier->policy->empty_digest)) {
            if (expected_offset == step->expanded_child_count ||
                step->expanded_child_indexes[expected_offset] != index) {
                return d7_mst_context_fail(
                    context,
                    D7_MERKLE_VERIFY_PROOF_MISMATCH,
                    &digest);
            }
            ++expected_offset;
        }
    }
    if (expected_offset != step->expanded_child_count) {
        return d7_mst_context_fail(
            context,
            D7_MERKLE_VERIFY_PROOF_MISMATCH,
            &digest);
    }
    for (index = 0; index != step->expanded_child_count; ++index) {
        const size_t child_index = step->expanded_child_indexes[index];
        d7_mst_decoded_block *child = d7_mst_find_decoded_proof_block(
            context,
            blocks,
            block->child_digests[child_index]);
        if (child == NULL) {
            return d7_mst_context_fail(
                context,
                D7_MERKLE_VERIFY_MISSING_BLOCK,
                &block->child_digests[child_index]);
        }
        status = d7_mst_validate_proof_reference(
            verifier,
            block,
            child_index,
            child,
            context);
        if (status != D7_MERKLE_OK) {
            return status;
        }
        status = d7_mst_verify_range_proof_node(
            verifier,
            proof,
            query,
            blocks,
            context,
            block->child_digests[child_index],
            depth + 1,
            visited_count);
        if (status != D7_MERKLE_OK) {
            return status;
        }
    }
    return D7_MERKLE_OK;
}

static d7_merkle_proof_verification_result d7_mst_proof_result(
    bool valid,
    d7_merkle_verification_failure_kind kind,
    d7_merkle_digest root_hash,
    size_t block_count,
    uint64_t byte_count) {
    d7_merkle_proof_verification_result result;
    result.is_valid = valid;
    result.failure_kind = kind;
    result.has_computed_root_hash = true;
    result.computed_root_hash = root_hash;
    result.verified_block_count = block_count;
    result.verified_byte_count = byte_count;
    return result;
}

d7_merkle_status d7_merkle_search_tree_verify_proof(
    const d7_merkle_proof *proof,
    const d7_merkle_policy *policy,
    const d7_merkle_verification_budget *budget,
    d7_merkle_proof_verification_result *result) {
    d7_merkle_verification_budget selected;
    d7_merkle_verification_error error;
    d7_mst_verification_context context;
    d7_merkle_search_tree verifier = {NULL, NULL};
    d7_mst_decoded_block *decoded = NULL;
    d7_mst_decoded_query query;
    size_t decoded_bytes = 0;
    size_t visited_count = 0;
    size_t index;
    d7_merkle_status status;
    if (proof == NULL || proof->rep == NULL || policy == NULL || policy->rep == NULL ||
        result == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    memset(&query, 0, sizeof(query));
    if (budget == NULL) {
        d7_merkle_verification_budget_init_default(&selected);
    } else {
        selected = *budget;
    }
    if (d7_merkle_verification_budget_validate(&selected) != D7_MERKLE_OK) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    /* Security-sensitive precedence: query gate/accounting, proof structure,
     * envelope, then and only then verifier allocation/hash/codec work. */
    if (proof->rep->query_byte_count > selected.max_proof_query_byte_count ||
        (uint64_t)proof->rep->query_byte_count > selected.max_total_byte_count) {
        *result = d7_mst_proof_result(
            false,
            D7_MERKLE_VERIFY_RESOURCE_LIMIT_EXCEEDED,
            proof->rep->root_hash,
            0,
            0);
        return D7_MERKLE_OK;
    }
    if (proof->rep->step_count > selected.max_block_count) {
        *result = d7_mst_proof_result(
            false,
            D7_MERKLE_VERIFY_RESOURCE_LIMIT_EXCEEDED,
            proof->rep->root_hash,
            0,
            (uint64_t)proof->rep->query_byte_count);
        return D7_MERKLE_OK;
    }
    for (index = 0; index != proof->rep->step_count; ++index) {
        if (proof->rep->steps[index].expanded_child_count >
            selected.max_child_references_per_block) {
            *result = d7_mst_proof_result(
                false,
                D7_MERKLE_VERIFY_RESOURCE_LIMIT_EXCEEDED,
                proof->rep->root_hash,
                0,
                (uint64_t)proof->rep->query_byte_count);
            return D7_MERKLE_OK;
        }
    }
    if (!d7_mst_algorithm_matches(d7_merkle_proof_algorithm_id(proof))) {
        *result = d7_mst_proof_result(
            false,
            D7_MERKLE_VERIFY_UNSUPPORTED_ALGORITHM,
            proof->rep->root_hash,
            0,
            (uint64_t)proof->rep->query_byte_count);
        return D7_MERKLE_OK;
    }
    if (!d7_merkle_digest_equal(proof->rep->domain_digest, policy->rep->domain_digest)) {
        *result = d7_mst_proof_result(
            false,
            D7_MERKLE_VERIFY_DOMAIN_MISMATCH,
            proof->rep->root_hash,
            0,
            (uint64_t)proof->rep->query_byte_count);
        return D7_MERKLE_OK;
    }
    d7_merkle_verification_error_init(&error);
    memset(&context, 0, sizeof(context));
    context.policy = policy->rep;
    context.budget = selected;
    context.error = &error;
    context.total_byte_count = (uint64_t)proof->rep->query_byte_count;
    d7_mst_context_publish_counts(&context);
    status = d7_merkle_search_tree_init(&verifier, policy);
    if (status != D7_MERKLE_OK) {
        goto cleanup;
    }
    if (proof->rep->step_count != 0) {
        if (d7_mst_multiply_overflows(
                proof->rep->step_count,
                sizeof(*decoded),
                &decoded_bytes)) {
            status = D7_MERKLE_OVERFLOW;
            goto cleanup;
        }
        decoded = (d7_mst_decoded_block *)d7_mst_allocate(
            policy->rep,
            decoded_bytes);
        if (decoded == NULL) {
            status = D7_MERKLE_NO_MEMORY;
            goto cleanup;
        }
        memset(decoded, 0, decoded_bytes);
    }
    for (index = 0; status == D7_MERKLE_OK && index != proof->rep->step_count; ++index) {
        d7_mst_digest_slot *slot;
        bool found;
        size_t child_index;
        status = d7_mst_decode_wire_block(
            &verifier,
            &proof->rep->steps[index].block,
            &context,
            1,
            &decoded[index]);
        if (status != D7_MERKLE_OK) {
            break;
        }
        slot = d7_mst_context_find_slot(
            &context,
            proof->rep->steps[index].block.rep->digest,
            &found);
        if (!found || slot == NULL || slot->value != SIZE_MAX) {
            status = d7_mst_context_fail(
                &context,
                D7_MERKLE_VERIFY_DUPLICATE_BLOCK,
                &proof->rep->steps[index].block.rep->digest);
            break;
        }
        slot->value = index;
        for (child_index = 0;
            child_index != proof->rep->steps[index].expanded_child_count;
            ++child_index) {
            const size_t expanded =
                proof->rep->steps[index].expanded_child_indexes[child_index];
            if (expanded >= decoded[index].entry_count + 1 ||
                d7_merkle_digest_equal(
                    decoded[index].child_digests[expanded],
                    policy->rep->empty_digest)) {
                status = d7_mst_context_fail(
                    &context,
                    D7_MERKLE_VERIFY_PROOF_MISMATCH,
                    &proof->rep->steps[index].block.rep->digest);
                break;
            }
        }
    }
    if (status == D7_MERKLE_OK) {
        status = d7_mst_decode_proof_query(&verifier, proof, &context, &query);
    }
    if (status == D7_MERKLE_OK &&
        d7_merkle_digest_equal(proof->rep->root_hash, policy->rep->empty_digest)) {
        if (proof->rep->step_count != 0 ||
            proof->rep->kind == D7_MERKLE_PROOF_MEMBERSHIP) {
            status = d7_mst_context_fail(
                &context,
                D7_MERKLE_VERIFY_PROOF_MISMATCH,
                &proof->rep->root_hash);
        }
    } else if (status == D7_MERKLE_OK &&
        d7_mst_find_decoded_proof_block(
            &context,
            decoded,
            proof->rep->root_hash) == NULL) {
        status = d7_mst_context_fail(
            &context,
            D7_MERKLE_VERIFY_ROOT_MISMATCH,
            &proof->rep->root_hash);
    } else if (status == D7_MERKLE_OK &&
        proof->rep->kind == D7_MERKLE_PROOF_RANGE) {
        status = d7_mst_verify_range_proof_node(
            &verifier,
            proof,
            &query,
            decoded,
            &context,
            proof->rep->root_hash,
            1,
            &visited_count);
    } else if (status == D7_MERKLE_OK) {
        status = d7_mst_verify_point_proof(
            &verifier,
            proof,
            &query,
            decoded,
            &context,
            &visited_count);
    }
    if (status == D7_MERKLE_OK && visited_count != proof->rep->step_count &&
        !d7_merkle_digest_equal(proof->rep->root_hash, policy->rep->empty_digest)) {
        status = d7_mst_context_fail(
            &context,
            D7_MERKLE_VERIFY_PROOF_MISMATCH,
            &proof->rep->root_hash);
    }
    if (status == D7_MERKLE_OK) {
        *result = d7_mst_proof_result(
            true,
            D7_MERKLE_VERIFY_NONE,
            proof->rep->root_hash,
            context.block_count,
            context.total_byte_count);
    } else if (status == D7_MERKLE_VERIFICATION_FAILURE) {
        *result = d7_mst_proof_result(
            false,
            error.kind,
            proof->rep->root_hash,
            context.block_count,
            context.total_byte_count);
        status = D7_MERKLE_OK;
    }

cleanup:
    d7_mst_decoded_query_dispose(policy->rep, &query);
    if (decoded != NULL) {
        for (index = 0; index != proof->rep->step_count; ++index) {
            d7_mst_decoded_block_dispose(policy->rep, &decoded[index]);
        }
    }
    d7_mst_deallocate(policy->rep, decoded);
    d7_merkle_search_tree_dispose(&verifier);
    d7_mst_context_dispose(&context);
    return status;
}

typedef struct d7_mst_merge_conflict {
    d7_mst_entry *key_entry;
    d7_mst_entry *base;
    d7_mst_entry *left;
    d7_mst_entry *right;
} d7_mst_merge_conflict;

struct d7_merkle_three_way_merge_result_rep {
    d7_mst_ref_count refs;
    struct d7_merkle_policy_rep *policy_owner;
    bool success;
    d7_merkle_search_tree tree;
    d7_mst_merge_conflict *conflicts;
    size_t conflict_count;
};

static d7_merkle_merge_value_ref d7_mst_merge_value_ref(
    const d7_mst_entry *entry) {
    d7_merkle_merge_value_ref result;
    result.present = entry != NULL;
    result.value = entry == NULL ? NULL : entry->value->value;
    return result;
}

static d7_merkle_three_way_merge_conflict_ref d7_mst_merge_conflict_ref(
    const d7_mst_entry *key_entry,
    const d7_mst_entry *base,
    const d7_mst_entry *left,
    const d7_mst_entry *right) {
    d7_merkle_three_way_merge_conflict_ref result;
    result.key = key_entry->key->value;
    result.base = d7_mst_merge_value_ref(base);
    result.left = d7_mst_merge_value_ref(left);
    result.right = d7_mst_merge_value_ref(right);
    return result;
}

static void d7_mst_merge_conflict_release(
    const struct d7_merkle_policy_rep *policy,
    d7_mst_merge_conflict *conflict) {
    if (conflict == NULL) {
        return;
    }
    d7_mst_entry_release(policy, conflict->right);
    d7_mst_entry_release(policy, conflict->left);
    d7_mst_entry_release(policy, conflict->base);
    d7_mst_entry_release(policy, conflict->key_entry);
    memset(conflict, 0, sizeof(*conflict));
}

static void d7_mst_merge_result_rep_release(
    struct d7_merkle_three_way_merge_result_rep *rep) {
    struct d7_merkle_policy_rep *owner;
    size_t index;
    if (rep == NULL || !d7_mst_ref_release(&rep->refs)) {
        return;
    }
    owner = rep->policy_owner;
    d7_merkle_search_tree_dispose(&rep->tree);
    for (index = 0; index != rep->conflict_count; ++index) {
        d7_mst_merge_conflict_release(owner, &rep->conflicts[index]);
    }
    d7_mst_deallocate(owner, rep->conflicts);
    d7_mst_deallocate(owner, rep);
    d7_mst_policy_release(owner);
}

d7_merkle_status d7_merkle_three_way_merge_result_copy(
    const d7_merkle_three_way_merge_result *source,
    d7_merkle_three_way_merge_result *destination) {
    if (source == NULL || source->rep == NULL || destination == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    if (source != destination) {
        d7_mst_ref_retain(&source->rep->refs);
        destination->rep = source->rep;
    }
    return D7_MERKLE_OK;
}

void d7_merkle_three_way_merge_result_move(
    d7_merkle_three_way_merge_result *destination,
    d7_merkle_three_way_merge_result *source) {
    if (destination != NULL && source != NULL && destination != source) {
        destination->rep = source->rep;
        source->rep = NULL;
    }
}

void d7_merkle_three_way_merge_result_dispose(
    d7_merkle_three_way_merge_result *result) {
    if (result != NULL) {
        d7_mst_merge_result_rep_release(result->rep);
        result->rep = NULL;
    }
}

bool d7_merkle_three_way_merge_result_success(
    const d7_merkle_three_way_merge_result *result) {
    return result != NULL && result->rep != NULL && result->rep->success;
}

d7_merkle_status d7_merkle_three_way_merge_result_copy_tree(
    const d7_merkle_three_way_merge_result *result,
    d7_merkle_search_tree *tree) {
    if (result == NULL || result->rep == NULL || !result->rep->success ||
        tree == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    return d7_merkle_search_tree_copy(&result->rep->tree, tree);
}

size_t d7_merkle_three_way_merge_result_conflict_count(
    const d7_merkle_three_way_merge_result *result) {
    return result == NULL || result->rep == NULL
        ? 0
        : result->rep->conflict_count;
}

d7_merkle_status d7_merkle_three_way_merge_result_conflict_at(
    const d7_merkle_three_way_merge_result *result,
    size_t index,
    d7_merkle_three_way_merge_conflict_ref *conflict) {
    const d7_mst_merge_conflict *stored;
    if (result == NULL || result->rep == NULL || conflict == NULL ||
        index >= result->rep->conflict_count) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    stored = &result->rep->conflicts[index];
    *conflict = d7_mst_merge_conflict_ref(
        stored->key_entry,
        stored->base,
        stored->left,
        stored->right);
    return D7_MERKLE_OK;
}

static d7_merkle_status d7_mst_merge_states_equal(
    const struct d7_merkle_policy_rep *policy,
    const d7_mst_entry *left,
    const d7_mst_entry *right,
    bool *equal) {
    if (left == NULL || right == NULL) {
        *equal = left == right;
        return D7_MERKLE_OK;
    }
    if (left == right) {
        *equal = true;
        return D7_MERKLE_OK;
    }
    return d7_mst_values_equal(policy, left, right, equal);
}

static d7_merkle_status d7_mst_merge_compare_entries(
    const struct d7_merkle_policy_rep *policy,
    const d7_mst_entry *left,
    const d7_mst_entry *right,
    int *comparison) {
    if (left == right) {
        *comparison = 0;
        return D7_MERKLE_OK;
    }
    return d7_mst_key_compare(
        policy,
        left->key->value,
        right->key->value,
        comparison);
}

static d7_merkle_status d7_mst_merge_append_entry(
    d7_mst_entry *entry,
    bool already_owned,
    d7_mst_entry **entries,
    size_t *entry_count) {
    if (entry == NULL) {
        return D7_MERKLE_OK;
    }
    if (!already_owned) {
        d7_mst_entry_retain(entry);
    }
    entries[*entry_count] = entry;
    ++*entry_count;
    return D7_MERKLE_OK;
}

static void d7_mst_merge_store_conflict(
    d7_mst_merge_conflict *destination,
    d7_mst_entry *key_entry,
    d7_mst_entry *base,
    d7_mst_entry *left,
    d7_mst_entry *right) {
    destination->key_entry = key_entry;
    destination->base = base;
    destination->left = left;
    destination->right = right;
    d7_mst_entry_retain(destination->key_entry);
    d7_mst_entry_retain(destination->base);
    d7_mst_entry_retain(destination->left);
    d7_mst_entry_retain(destination->right);
}

d7_merkle_status d7_merkle_search_tree_merge(
    const d7_merkle_search_tree *base,
    const d7_merkle_search_tree *left,
    const d7_merkle_search_tree *right,
    d7_merkle_merge_resolver resolver,
    void *resolver_context,
    d7_merkle_three_way_merge_result *result) {
    const struct d7_merkle_policy_rep *policy;
    d7_mst_iterator base_iterator;
    d7_mst_iterator left_iterator;
    d7_mst_iterator right_iterator;
    const d7_mst_entry *base_entry = NULL;
    const d7_mst_entry *left_entry = NULL;
    const d7_mst_entry *right_entry = NULL;
    bool has_base;
    bool has_left;
    bool has_right;
    size_t maximum_count;
    size_t partial_count;
    d7_mst_entry **entries = NULL;
    size_t entry_count = 0;
    d7_mst_merge_conflict *conflicts = NULL;
    size_t conflict_count = 0;
    struct d7_merkle_node *root = NULL;
    struct d7_merkle_three_way_merge_result_rep *rep = NULL;
    d7_merkle_status status = D7_MERKLE_OK;
    size_t index;
    if (!d7_mst_tree_valid(base) || !d7_mst_tree_valid(left) ||
        !d7_mst_tree_valid(right) || result == NULL) {
        return D7_MERKLE_INVALID_ARGUMENT;
    }
    /* Entries embed allocator-owned typed objects. Domain/type compatibility is
     * insufficient for entry reuse; all three trees must share the exact policy
     * representation and therefore the exact allocator/callback lifetime. */
    if (base->policy != left->policy || base->policy != right->policy) {
        return D7_MERKLE_INCOMPATIBLE_POLICY;
    }
    policy = base->policy;
    if (d7_mst_add_overflows(
            d7_merkle_search_tree_size(base),
            d7_merkle_search_tree_size(left),
            &partial_count) ||
        d7_mst_add_overflows(
            partial_count,
            d7_merkle_search_tree_size(right),
            &maximum_count)) {
        return D7_MERKLE_OVERFLOW;
    }
    status = d7_mst_allocate_pointer_array(
        policy,
        maximum_count,
        sizeof(*entries),
        (void **)&entries);
    if (status == D7_MERKLE_OK && maximum_count != 0) {
        size_t bytes;
        if (d7_mst_multiply_overflows(maximum_count, sizeof(*conflicts), &bytes)) {
            status = D7_MERKLE_OVERFLOW;
        } else {
            conflicts = (d7_mst_merge_conflict *)d7_mst_allocate(policy, bytes);
            if (conflicts == NULL) {
                status = D7_MERKLE_NO_MEMORY;
            } else {
                memset(conflicts, 0, bytes);
            }
        }
    }
    if (status != D7_MERKLE_OK) {
        goto cleanup;
    }

    d7_mst_iterator_init(&base_iterator, base->root);
    d7_mst_iterator_init(&left_iterator, left->root);
    d7_mst_iterator_init(&right_iterator, right->root);
    has_base = d7_mst_iterator_next(&base_iterator, &base_entry);
    has_left = d7_mst_iterator_next(&left_iterator, &left_entry);
    has_right = d7_mst_iterator_next(&right_iterator, &right_entry);
    while (has_base || has_left || has_right) {
        const d7_mst_entry *minimum = has_base
            ? base_entry
            : (has_left ? left_entry : right_entry);
        d7_mst_entry *current_base = NULL;
        d7_mst_entry *current_left = NULL;
        d7_mst_entry *current_right = NULL;
        d7_mst_entry *representative;
        d7_mst_entry *selected_entry = NULL;
        bool selected_owned = false;
        bool equal;
        int comparison;
        d7_merkle_merge_resolution resolution;
        if (has_left) {
            status = d7_mst_merge_compare_entries(
                policy,
                left_entry,
                minimum,
                &comparison);
            if (status != D7_MERKLE_OK) {
                goto cleanup;
            }
            if (comparison < 0) {
                minimum = left_entry;
            }
        }
        if (has_right) {
            status = d7_mst_merge_compare_entries(
                policy,
                right_entry,
                minimum,
                &comparison);
            if (status != D7_MERKLE_OK) {
                goto cleanup;
            }
            if (comparison < 0) {
                minimum = right_entry;
            }
        }
        if (has_base) {
            status = d7_mst_merge_compare_entries(
                policy,
                base_entry,
                minimum,
                &comparison);
            if (status != D7_MERKLE_OK) {
                goto cleanup;
            }
            if (comparison < 0) {
                status = D7_MERKLE_INCONSISTENT_POLICY;
                goto cleanup;
            }
            if (comparison == 0) {
                current_base = (d7_mst_entry *)base_entry;
            }
        }
        if (has_left) {
            status = d7_mst_merge_compare_entries(
                policy,
                left_entry,
                minimum,
                &comparison);
            if (status != D7_MERKLE_OK) {
                goto cleanup;
            }
            if (comparison < 0) {
                status = D7_MERKLE_INCONSISTENT_POLICY;
                goto cleanup;
            }
            if (comparison == 0) {
                current_left = (d7_mst_entry *)left_entry;
            }
        }
        if (has_right) {
            status = d7_mst_merge_compare_entries(
                policy,
                right_entry,
                minimum,
                &comparison);
            if (status != D7_MERKLE_OK) {
                goto cleanup;
            }
            if (comparison < 0) {
                status = D7_MERKLE_INCONSISTENT_POLICY;
                goto cleanup;
            }
            if (comparison == 0) {
                current_right = (d7_mst_entry *)right_entry;
            }
        }
        representative = current_left != NULL
            ? current_left
            : (current_right != NULL ? current_right : current_base);

        status = d7_mst_merge_states_equal(
            policy,
            current_left,
            current_right,
            &equal);
        if (status != D7_MERKLE_OK) {
            goto cleanup;
        }
        if (equal) {
            selected_entry = current_left;
        } else {
            status = d7_mst_merge_states_equal(
                policy,
                current_left,
                current_base,
                &equal);
            if (status != D7_MERKLE_OK) {
                goto cleanup;
            }
            if (equal) {
                selected_entry = current_right;
            } else {
                status = d7_mst_merge_states_equal(
                    policy,
                    current_right,
                    current_base,
                    &equal);
                if (status != D7_MERKLE_OK) {
                    goto cleanup;
                }
                if (equal) {
                    selected_entry = current_left;
                } else {
                    resolution.kind = D7_MERKLE_MERGE_UNRESOLVED;
                    resolution.value = NULL;
                    if (resolver != NULL) {
                        status = resolver(
                            d7_mst_merge_conflict_ref(
                                representative,
                                current_base,
                                current_left,
                                current_right),
                            &resolution,
                            resolver_context);
                        if (status != D7_MERKLE_OK) {
                            goto cleanup;
                        }
                    }
                    switch (resolution.kind) {
                        case D7_MERKLE_MERGE_UNRESOLVED:
                            d7_mst_merge_store_conflict(
                                &conflicts[conflict_count],
                                representative,
                                current_base,
                                current_left,
                                current_right);
                            ++conflict_count;
                            break;
                        case D7_MERKLE_MERGE_USE_BASE:
                            selected_entry = current_base;
                            break;
                        case D7_MERKLE_MERGE_USE_LEFT:
                            selected_entry = current_left;
                            break;
                        case D7_MERKLE_MERGE_USE_RIGHT:
                            selected_entry = current_right;
                            break;
                        case D7_MERKLE_MERGE_SET_VALUE: {
                            bool changed;
                            if (resolution.value == NULL) {
                                status = D7_MERKLE_INVALID_ARGUMENT;
                                goto cleanup;
                            }
                            status = d7_mst_entry_replace_value(
                                policy,
                                representative,
                                resolution.value,
                                &changed,
                                &selected_entry);
                            if (status != D7_MERKLE_OK) {
                                goto cleanup;
                            }
                            if (!changed) {
                                selected_entry = representative;
                            } else {
                                selected_owned = true;
                            }
                            break;
                        }
                        case D7_MERKLE_MERGE_DELETE:
                            selected_entry = NULL;
                            break;
                        default:
                            status = D7_MERKLE_INVALID_ARGUMENT;
                            goto cleanup;
                    }
                }
            }
        }
        status = d7_mst_merge_append_entry(
            selected_entry,
            selected_owned,
            entries,
            &entry_count);
        if (status != D7_MERKLE_OK) {
            if (selected_owned) {
                d7_mst_entry_release(policy, selected_entry);
            }
            goto cleanup;
        }
        if (current_base != NULL) {
            has_base = d7_mst_iterator_next(&base_iterator, &base_entry);
        }
        if (current_left != NULL) {
            has_left = d7_mst_iterator_next(&left_iterator, &left_entry);
        }
        if (current_right != NULL) {
            has_right = d7_mst_iterator_next(&right_iterator, &right_entry);
        }
    }

    if (conflict_count == 0) {
        status = d7_mst_build_canonical(policy, entries, entry_count, &root);
        if (status != D7_MERKLE_OK) {
            goto cleanup;
        }
    }
    rep = (struct d7_merkle_three_way_merge_result_rep *)d7_mst_allocate(
        policy,
        sizeof(*rep));
    if (rep == NULL) {
        status = D7_MERKLE_NO_MEMORY;
        goto cleanup;
    }
    memset(rep, 0, sizeof(*rep));
    d7_mst_ref_init(&rep->refs);
    rep->policy_owner = (struct d7_merkle_policy_rep *)policy;
    d7_mst_policy_retain(rep->policy_owner);
    rep->success = conflict_count == 0;
    if (rep->success) {
        rep->tree = d7_mst_adopt_tree(rep->policy_owner, root);
        root = NULL;
    } else {
        rep->conflicts = conflicts;
        rep->conflict_count = conflict_count;
        conflicts = NULL;
        conflict_count = 0;
    }
    result->rep = rep;
    rep = NULL;

cleanup:
    if (rep != NULL) {
        d7_mst_merge_result_rep_release(rep);
    }
    d7_mst_node_release(policy, root);
    for (index = 0; index != entry_count; ++index) {
        d7_mst_entry_release(policy, entries[index]);
    }
    for (index = 0; index != conflict_count; ++index) {
        d7_mst_merge_conflict_release(policy, &conflicts[index]);
    }
    d7_mst_deallocate(policy, conflicts);
    d7_mst_deallocate(policy, entries);
    return status;
}
