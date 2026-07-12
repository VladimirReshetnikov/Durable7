#include <Tools/DataStructures/Hamt/merkle_search_tree.h>

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
#endif

enum {
    TDS_MST_MAXIMUM_LEVEL = 64,
    TDS_MST_MAXIMUM_HEIGHT = 65,
    TDS_MST_BLOCK_HEADER_LENGTH = 4 + 1 + 32 + 1 + 4 + 4,
    TDS_MST_NODE_BLOCK_TAG = 1
};

static const unsigned char tds_mst_algorithm_id[] = "mst-sha256-b16-v2";
static const unsigned char tds_mst_block_magic[] = {'M', 'S', 'T', '2'};
static const char tds_mst_lower_hex[] = "0123456789abcdef";

#if defined(_MSC_VER) && !defined(__clang__)
typedef volatile LONG64 tds_mst_ref_count;

static void tds_mst_ref_init(tds_mst_ref_count *value) {
    *value = 1;
}

static void tds_mst_ref_retain(tds_mst_ref_count *value) {
    (void)InterlockedIncrement64(value);
}

static bool tds_mst_ref_release(tds_mst_ref_count *value) {
    return InterlockedDecrement64(value) == 0;
}
#else
typedef atomic_size_t tds_mst_ref_count;

static void tds_mst_ref_init(tds_mst_ref_count *value) {
    atomic_init(value, 1);
}

static void tds_mst_ref_retain(tds_mst_ref_count *value) {
    (void)atomic_fetch_add_explicit(value, 1, memory_order_relaxed);
}

static bool tds_mst_ref_release(tds_mst_ref_count *value) {
    return atomic_fetch_sub_explicit(value, 1, memory_order_acq_rel) == 1;
}
#endif

typedef struct tds_mst_bytes {
    tds_mst_ref_count refs;
    size_t size;
    unsigned char data[];
} tds_mst_bytes;

typedef struct tds_mst_object {
    tds_mst_ref_count refs;
    bool is_key;
    void *value;
} tds_mst_object;

typedef struct tds_mst_entry {
    tds_mst_ref_count refs;
    tds_mst_object *key;
    tds_mst_object *value;
    tds_mst_bytes *key_bytes;
    tds_mst_bytes *value_bytes;
    unsigned char level;
} tds_mst_entry;

struct tds_merkle_node {
    tds_mst_ref_count refs;
    struct tds_merkle_node *release_next;
    unsigned char level;
    size_t entry_count;
    size_t count;
    size_t height;
    size_t block_count;
    tds_mst_entry *minimum_entry;
    tds_mst_entry *maximum_entry;
    tds_mst_bytes *block_bytes;
    tds_merkle_digest digest;
    unsigned char storage[];
};

struct tds_merkle_policy_rep {
    tds_mst_ref_count refs;
    tds_merkle_policy_config config;
    tds_merkle_digest domain_digest;
    tds_merkle_digest empty_digest;
    unsigned char *policy_id;
    unsigned char *key_encoding_id;
    unsigned char *value_encoding_id;
};

typedef struct tds_mst_pending_entry {
    const void *key;
    const void *value;
    size_t sequence;
} tds_mst_pending_entry;

typedef struct tds_mst_iterator_frame {
    const struct tds_merkle_node *node;
    size_t index;
    bool child_visited;
} tds_mst_iterator_frame;

typedef struct tds_mst_iterator {
    tds_mst_iterator_frame frames[TDS_MST_MAXIMUM_HEIGHT];
    size_t depth;
} tds_mst_iterator;

typedef struct tds_mst_validation_accumulator {
    size_t count;
    size_t block_count;
    size_t minimum_entries;
    size_t maximum_entries;
    size_t minimum_block_bytes;
    size_t maximum_block_bytes;
} tds_mst_validation_accumulator;

static void *tds_mst_default_allocate(size_t size, void *context) {
    (void)context;
    return malloc(size);
}

static void tds_mst_default_deallocate(void *allocation, void *context) {
    (void)context;
    free(allocation);
}

static void *tds_mst_allocate_config(
    const tds_merkle_policy_config *config,
    size_t size) {
    return config->allocator.allocate(size, config->allocator.context);
}

static void tds_mst_deallocate_config(
    const tds_merkle_policy_config *config,
    void *allocation) {
    if (allocation != NULL) {
        config->allocator.deallocate(allocation, config->allocator.context);
    }
}

static void *tds_mst_allocate(
    const struct tds_merkle_policy_rep *policy,
    size_t size) {
    return tds_mst_allocate_config(&policy->config, size);
}

static void tds_mst_deallocate(
    const struct tds_merkle_policy_rep *policy,
    void *allocation) {
    tds_mst_deallocate_config(&policy->config, allocation);
}

static bool tds_mst_add_overflows(size_t left, size_t right, size_t *result) {
    if (right > SIZE_MAX - left) {
        return true;
    }
    *result = left + right;
    return false;
}

static bool tds_mst_multiply_overflows(size_t left, size_t right, size_t *result) {
    if (left != 0 && right > SIZE_MAX / left) {
        return true;
    }
    *result = left * right;
    return false;
}

static void tds_mst_write_be32(uint32_t value, unsigned char *destination) {
    destination[0] = (unsigned char)(value >> 24);
    destination[1] = (unsigned char)(value >> 16);
    destination[2] = (unsigned char)(value >> 8);
    destination[3] = (unsigned char)value;
}

static uint32_t tds_mst_read_be32(const unsigned char *source) {
    return ((uint32_t)source[0] << 24) |
        ((uint32_t)source[1] << 16) |
        ((uint32_t)source[2] << 8) |
        (uint32_t)source[3];
}

static int tds_mst_hex_value(char value) {
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

bool tds_merkle_digest_equal(tds_merkle_digest left, tds_merkle_digest right) {
    return memcmp(left.bytes, right.bytes, TDS_MERKLE_DIGEST_BYTE_LENGTH) == 0;
}

int tds_merkle_digest_compare(tds_merkle_digest left, tds_merkle_digest right) {
    const int comparison = memcmp(left.bytes, right.bytes, TDS_MERKLE_DIGEST_BYTE_LENGTH);
    return (comparison > 0) - (comparison < 0);
}

tds_merkle_status tds_merkle_digest_parse(
    const unsigned char *bytes,
    size_t byte_count,
    tds_merkle_digest *digest) {
    tds_merkle_digest parsed;
    if (bytes == NULL || digest == NULL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    if (byte_count != TDS_MERKLE_DIGEST_BYTE_LENGTH) {
        return TDS_MERKLE_INVALID_ENCODING;
    }
    memcpy(parsed.bytes, bytes, sizeof(parsed.bytes));
    *digest = parsed;
    return TDS_MERKLE_OK;
}

tds_merkle_status tds_merkle_digest_parse_hex(
    const char *hex,
    size_t character_count,
    tds_merkle_digest *digest) {
    tds_merkle_digest parsed;
    size_t index;
    if (hex == NULL || digest == NULL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    if (character_count != TDS_MERKLE_DIGEST_HEX_LENGTH) {
        return TDS_MERKLE_INVALID_ENCODING;
    }
    for (index = 0; index != TDS_MERKLE_DIGEST_BYTE_LENGTH; ++index) {
        const int high = tds_mst_hex_value(hex[index * 2]);
        const int low = tds_mst_hex_value(hex[index * 2 + 1]);
        if (high < 0 || low < 0) {
            return TDS_MERKLE_INVALID_ENCODING;
        }
        parsed.bytes[index] = (unsigned char)((high << 4) | low);
    }
    *digest = parsed;
    return TDS_MERKLE_OK;
}

tds_merkle_status tds_merkle_digest_write(
    tds_merkle_digest digest,
    unsigned char *destination,
    size_t destination_size) {
    if (destination == NULL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    if (destination_size < TDS_MERKLE_DIGEST_BYTE_LENGTH) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    memcpy(destination, digest.bytes, sizeof(digest.bytes));
    return TDS_MERKLE_OK;
}

tds_merkle_status tds_merkle_digest_write_hex(
    tds_merkle_digest digest,
    char *destination,
    size_t destination_size) {
    size_t index;
    if (destination == NULL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    if (destination_size < TDS_MERKLE_DIGEST_HEX_LENGTH) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    for (index = 0; index != TDS_MERKLE_DIGEST_BYTE_LENGTH; ++index) {
        destination[index * 2] = tds_mst_lower_hex[digest.bytes[index] >> 4];
        destination[index * 2 + 1] = tds_mst_lower_hex[digest.bytes[index] & 0x0f];
    }
    return TDS_MERKLE_OK;
}

static bool tds_mst_utf8_next(
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

static bool tds_mst_utf8_valid(const unsigned char *bytes, size_t size) {
    size_t offset = 0;
    uint32_t code_point = 0;
    if (size != 0 && bytes == NULL) {
        return false;
    }
    while (offset != size) {
        if (!tds_mst_utf8_next(bytes, size, &offset, &code_point)) {
            return false;
        }
    }
    return true;
}

static bool tds_mst_unicode_whitespace(uint32_t code_point) {
    return (code_point >= 0x0009 && code_point <= 0x000d) ||
        code_point == 0x0020 || code_point == 0x0085 ||
        code_point == 0x00a0 || code_point == 0x1680 ||
        (code_point >= 0x2000 && code_point <= 0x200a) ||
        code_point == 0x2028 || code_point == 0x2029 ||
        code_point == 0x202f || code_point == 0x205f ||
        code_point == 0x3000;
}

static bool tds_mst_policy_id_valid(tds_merkle_identifier identifier) {
    size_t offset = 0;
    uint32_t code_point = 0;
    bool non_whitespace = false;
    if (identifier.size == 0 || identifier.bytes == NULL) {
        return false;
    }
    while (offset != identifier.size) {
        if (!tds_mst_utf8_next(
                identifier.bytes,
                identifier.size,
                &offset,
                &code_point)) {
            return false;
        }
        non_whitespace = non_whitespace || !tds_mst_unicode_whitespace(code_point);
    }
    return non_whitespace;
}

static bool tds_mst_encoding_id_valid(tds_merkle_identifier identifier) {
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
        if (!tds_mst_utf8_next(
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
    if (tds_mst_unicode_whitespace(first) || tds_mst_unicode_whitespace(last)) {
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

static tds_merkle_status tds_mst_copy_bytes(
    const tds_merkle_policy_config *config,
    tds_merkle_identifier identifier,
    unsigned char **result) {
    unsigned char *copy;
    if (identifier.size == 0) {
        *result = NULL;
        return TDS_MERKLE_OK;
    }
    copy = (unsigned char *)tds_mst_allocate_config(config, identifier.size);
    if (copy == NULL) {
        return TDS_MERKLE_NO_MEMORY;
    }
    memcpy(copy, identifier.bytes, identifier.size);
    *result = copy;
    return TDS_MERKLE_OK;
}

#if defined(_WIN32)
static tds_merkle_status tds_mst_sha256_config(
    const tds_merkle_policy_config *config,
    const unsigned char *message,
    size_t message_size,
    tds_merkle_digest *digest) {
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    unsigned char *object = NULL;
    ULONG object_size = 0;
    ULONG digest_size = 0;
    ULONG copied = 0;
    NTSTATUS native_status;
    tds_merkle_digest staged;
    tds_merkle_status status = TDS_MERKLE_CRYPTO_FAILURE;
    if (message_size > ULONG_MAX) {
        return TDS_MERKLE_OVERFLOW;
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
        digest_size != TDS_MERKLE_DIGEST_BYTE_LENGTH) {
        goto cleanup;
    }
    object = (unsigned char *)tds_mst_allocate_config(config, object_size);
    if (object == NULL) {
        status = TDS_MERKLE_NO_MEMORY;
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
    status = TDS_MERKLE_OK;

cleanup:
    if (hash != NULL) {
        (void)BCryptDestroyHash(hash);
    }
    tds_mst_deallocate_config(config, object);
    if (algorithm != NULL) {
        (void)BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    return status;
}
#else
static tds_merkle_status tds_mst_sha256_config(
    const tds_merkle_policy_config *config,
    const unsigned char *message,
    size_t message_size,
    tds_merkle_digest *digest) {
    unsigned int written = 0;
    tds_merkle_digest staged;
    (void)config;
    if (EVP_Digest(
            message,
            message_size,
            staged.bytes,
            &written,
            EVP_sha256(),
            NULL) != 1 || written != TDS_MERKLE_DIGEST_BYTE_LENGTH) {
        return TDS_MERKLE_CRYPTO_FAILURE;
    }
    *digest = staged;
    return TDS_MERKLE_OK;
}
#endif

static tds_merkle_status tds_mst_hash_framed_config(
    const tds_merkle_policy_config *config,
    unsigned char tag,
    const tds_merkle_identifier *fields,
    size_t field_count,
    tds_merkle_digest *digest) {
    unsigned char *bytes = NULL;
    size_t size = 1;
    size_t offset = 0;
    size_t index;
    tds_merkle_status status;
    for (index = 0; index != field_count; ++index) {
        if (fields[index].size > INT32_MAX ||
            tds_mst_add_overflows(size, 4, &size) ||
            tds_mst_add_overflows(size, fields[index].size, &size)) {
            return TDS_MERKLE_OVERFLOW;
        }
    }
    bytes = (unsigned char *)tds_mst_allocate_config(config, size);
    if (bytes == NULL) {
        return TDS_MERKLE_NO_MEMORY;
    }
    bytes[offset++] = tag;
    for (index = 0; index != field_count; ++index) {
        tds_mst_write_be32((uint32_t)fields[index].size, bytes + offset);
        offset += 4;
        if (fields[index].size != 0) {
            memcpy(bytes + offset, fields[index].bytes, fields[index].size);
            offset += fields[index].size;
        }
    }
    status = tds_mst_sha256_config(config, bytes, size, digest);
    tds_mst_deallocate_config(config, bytes);
    return status;
}

static tds_merkle_status tds_mst_pod_equal(
    const void *left,
    const void *right,
    bool *equal,
    void *context) {
    const size_t size = *(const size_t *)context;
    *equal = memcmp(left, right, size) == 0;
    return TDS_MERKLE_OK;
}

static size_t tds_mst_i32_size = sizeof(int32_t);
static size_t tds_mst_i64_size = sizeof(int64_t);
static size_t tds_mst_guid_size = sizeof(tds_merkle_guid);

static tds_merkle_status tds_mst_i32_encode(
    const void *value,
    unsigned char *destination,
    size_t destination_size,
    size_t *bytes_written,
    const tds_merkle_allocator *allocator,
    void *context) {
    uint32_t bits;
    (void)allocator;
    (void)context;
    if (value == NULL || bytes_written == NULL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    *bytes_written = sizeof(int32_t);
    if (destination == NULL) {
        return destination_size == 0 ? TDS_MERKLE_OK : TDS_MERKLE_INVALID_ARGUMENT;
    }
    if (destination_size != sizeof(int32_t)) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    bits = (uint32_t)*(const int32_t *)value;
    tds_mst_write_be32(bits, destination);
    return TDS_MERKLE_OK;
}

static tds_merkle_status tds_mst_i32_decode(
    const unsigned char *encoding,
    size_t encoding_size,
    void *destination,
    const tds_merkle_allocator *allocator,
    void *context) {
    (void)allocator;
    (void)context;
    if (encoding == NULL || destination == NULL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    if (encoding_size != sizeof(int32_t)) {
        return TDS_MERKLE_INVALID_ENCODING;
    }
    *(int32_t *)destination = (int32_t)tds_mst_read_be32(encoding);
    return TDS_MERKLE_OK;
}

static tds_merkle_status tds_mst_i64_encode(
    const void *value,
    unsigned char *destination,
    size_t destination_size,
    size_t *bytes_written,
    const tds_merkle_allocator *allocator,
    void *context) {
    uint64_t bits;
    size_t index;
    (void)allocator;
    (void)context;
    if (value == NULL || bytes_written == NULL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    bits = (uint64_t)*(const int64_t *)value;
    *bytes_written = sizeof(int64_t);
    if (destination == NULL) {
        return destination_size == 0 ? TDS_MERKLE_OK : TDS_MERKLE_INVALID_ARGUMENT;
    }
    if (destination_size != sizeof(int64_t)) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    for (index = 0; index != sizeof(int64_t); ++index) {
        destination[index] = (unsigned char)(bits >> ((sizeof(int64_t) - 1 - index) * 8));
    }
    return TDS_MERKLE_OK;
}

static tds_merkle_status tds_mst_i64_decode(
    const unsigned char *encoding,
    size_t encoding_size,
    void *destination,
    const tds_merkle_allocator *allocator,
    void *context) {
    uint64_t bits = 0;
    size_t index;
    (void)allocator;
    (void)context;
    if (encoding == NULL || destination == NULL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    if (encoding_size != sizeof(int64_t)) {
        return TDS_MERKLE_INVALID_ENCODING;
    }
    for (index = 0; index != sizeof(int64_t); ++index) {
        bits = (bits << 8) | encoding[index];
    }
    *(int64_t *)destination = (int64_t)bits;
    return TDS_MERKLE_OK;
}

static tds_merkle_status tds_mst_guid_encode(
    const void *value,
    unsigned char *destination,
    size_t destination_size,
    size_t *bytes_written,
    const tds_merkle_allocator *allocator,
    void *context) {
    (void)allocator;
    (void)context;
    if (value == NULL || bytes_written == NULL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    *bytes_written = sizeof(((tds_merkle_guid *)0)->bytes);
    if (destination == NULL) {
        return destination_size == 0 ? TDS_MERKLE_OK : TDS_MERKLE_INVALID_ARGUMENT;
    }
    if (destination_size != sizeof(((tds_merkle_guid *)0)->bytes)) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    memcpy(destination, ((const tds_merkle_guid *)value)->bytes, destination_size);
    return TDS_MERKLE_OK;
}

static tds_merkle_status tds_mst_guid_decode(
    const unsigned char *encoding,
    size_t encoding_size,
    void *destination,
    const tds_merkle_allocator *allocator,
    void *context) {
    (void)allocator;
    (void)context;
    if (encoding == NULL || destination == NULL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    if (encoding_size != sizeof(((tds_merkle_guid *)0)->bytes)) {
        return TDS_MERKLE_INVALID_ENCODING;
    }
    memcpy(((tds_merkle_guid *)destination)->bytes, encoding, encoding_size);
    return TDS_MERKLE_OK;
}

static tds_merkle_status tds_mst_nullable_copy_core(
    bool has_value,
    const void *data,
    size_t size,
    bool utf8,
    void **copied,
    const tds_merkle_allocator *allocator) {
    void *bytes = NULL;
    if (!has_value) {
        if (size != 0) {
            return TDS_MERKLE_INVALID_ENCODING;
        }
        *copied = NULL;
        return TDS_MERKLE_OK;
    }
    if ((size != 0 && data == NULL) ||
        (utf8 && !tds_mst_utf8_valid((const unsigned char *)data, size))) {
        return TDS_MERKLE_INVALID_ENCODING;
    }
    if (size != 0) {
        bytes = allocator->allocate(size, allocator->context);
        if (bytes == NULL) {
            return TDS_MERKLE_NO_MEMORY;
        }
        memcpy(bytes, data, size);
    }
    *copied = bytes;
    return TDS_MERKLE_OK;
}

static tds_merkle_status tds_mst_nullable_utf8_copy(
    void *destination,
    const void *source,
    const tds_merkle_allocator *allocator,
    void *context) {
    const tds_merkle_nullable_utf8 *input = (const tds_merkle_nullable_utf8 *)source;
    tds_merkle_nullable_utf8 result = {false, NULL, 0};
    void *copy = NULL;
    tds_merkle_status status;
    (void)context;
    status = tds_mst_nullable_copy_core(
        input->has_value,
        input->data,
        input->size,
        true,
        &copy,
        allocator);
    if (status != TDS_MERKLE_OK) {
        return status;
    }
    result.has_value = input->has_value;
    result.data = (const char *)copy;
    result.size = input->size;
    *(tds_merkle_nullable_utf8 *)destination = result;
    return TDS_MERKLE_OK;
}

static void tds_mst_nullable_utf8_destroy(
    void *value,
    const tds_merkle_allocator *allocator,
    void *context) {
    tds_merkle_nullable_utf8 *nullable = (tds_merkle_nullable_utf8 *)value;
    (void)context;
    if (nullable->data != NULL) {
        allocator->deallocate((void *)nullable->data, allocator->context);
    }
    memset(nullable, 0, sizeof(*nullable));
}

static tds_merkle_status tds_mst_nullable_utf8_equal(
    const void *left,
    const void *right,
    bool *equal,
    void *context) {
    const tds_merkle_nullable_utf8 *first = (const tds_merkle_nullable_utf8 *)left;
    const tds_merkle_nullable_utf8 *second = (const tds_merkle_nullable_utf8 *)right;
    (void)context;
    *equal = first->has_value == second->has_value &&
        (!first->has_value ||
            (first->size == second->size &&
                (first->size == 0 ||
                    memcmp(first->data, second->data, first->size) == 0)));
    return TDS_MERKLE_OK;
}

static tds_merkle_status tds_mst_nullable_bytes_copy(
    void *destination,
    const void *source,
    const tds_merkle_allocator *allocator,
    void *context) {
    const tds_merkle_nullable_bytes *input = (const tds_merkle_nullable_bytes *)source;
    tds_merkle_nullable_bytes result = {false, NULL, 0};
    void *copy = NULL;
    tds_merkle_status status;
    (void)context;
    status = tds_mst_nullable_copy_core(
        input->has_value,
        input->data,
        input->size,
        false,
        &copy,
        allocator);
    if (status != TDS_MERKLE_OK) {
        return status;
    }
    result.has_value = input->has_value;
    result.data = (const unsigned char *)copy;
    result.size = input->size;
    *(tds_merkle_nullable_bytes *)destination = result;
    return TDS_MERKLE_OK;
}

static void tds_mst_nullable_bytes_destroy(
    void *value,
    const tds_merkle_allocator *allocator,
    void *context) {
    tds_merkle_nullable_bytes *nullable = (tds_merkle_nullable_bytes *)value;
    (void)context;
    if (nullable->data != NULL) {
        allocator->deallocate((void *)nullable->data, allocator->context);
    }
    memset(nullable, 0, sizeof(*nullable));
}

static tds_merkle_status tds_mst_nullable_bytes_equal(
    const void *left,
    const void *right,
    bool *equal,
    void *context) {
    const tds_merkle_nullable_bytes *first = (const tds_merkle_nullable_bytes *)left;
    const tds_merkle_nullable_bytes *second = (const tds_merkle_nullable_bytes *)right;
    (void)context;
    *equal = first->has_value == second->has_value &&
        (!first->has_value ||
            (first->size == second->size &&
                (first->size == 0 ||
                    memcmp(first->data, second->data, first->size) == 0)));
    return TDS_MERKLE_OK;
}

static tds_merkle_status tds_mst_nullable_encode_core(
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
        (has_value && utf8 && !tds_mst_utf8_valid(data, size)) ||
        tds_mst_add_overflows(1, has_value ? size : 0, &required)) {
        return TDS_MERKLE_INVALID_ENCODING;
    }
    *bytes_written = required;
    if (destination == NULL) {
        return destination_size == 0 ? TDS_MERKLE_OK : TDS_MERKLE_INVALID_ARGUMENT;
    }
    if (destination_size != required) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    destination[0] = has_value ? 1 : 0;
    if (has_value && size != 0) {
        memcpy(destination + 1, data, size);
    }
    return TDS_MERKLE_OK;
}

static tds_merkle_status tds_mst_nullable_utf8_encode(
    const void *value,
    unsigned char *destination,
    size_t destination_size,
    size_t *bytes_written,
    const tds_merkle_allocator *allocator,
    void *context) {
    const tds_merkle_nullable_utf8 *nullable = (const tds_merkle_nullable_utf8 *)value;
    (void)allocator;
    (void)context;
    if (nullable == NULL || bytes_written == NULL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    return tds_mst_nullable_encode_core(
        nullable->has_value,
        (const unsigned char *)nullable->data,
        nullable->size,
        true,
        destination,
        destination_size,
        bytes_written);
}

static tds_merkle_status tds_mst_nullable_bytes_encode(
    const void *value,
    unsigned char *destination,
    size_t destination_size,
    size_t *bytes_written,
    const tds_merkle_allocator *allocator,
    void *context) {
    const tds_merkle_nullable_bytes *nullable = (const tds_merkle_nullable_bytes *)value;
    (void)allocator;
    (void)context;
    if (nullable == NULL || bytes_written == NULL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    return tds_mst_nullable_encode_core(
        nullable->has_value,
        nullable->data,
        nullable->size,
        false,
        destination,
        destination_size,
        bytes_written);
}

static tds_merkle_status tds_mst_nullable_utf8_decode(
    const unsigned char *encoding,
    size_t encoding_size,
    void *destination,
    const tds_merkle_allocator *allocator,
    void *context) {
    tds_merkle_nullable_utf8 source;
    (void)context;
    if (encoding == NULL || destination == NULL || encoding_size == 0) {
        return TDS_MERKLE_INVALID_ENCODING;
    }
    if (encoding[0] == 0) {
        if (encoding_size != 1) {
            return TDS_MERKLE_INVALID_ENCODING;
        }
        source = (tds_merkle_nullable_utf8){false, NULL, 0};
    } else if (encoding[0] == 1 &&
        tds_mst_utf8_valid(encoding + 1, encoding_size - 1)) {
        source = (tds_merkle_nullable_utf8){
            true,
            (const char *)(encoding + 1),
            encoding_size - 1};
    } else {
        return TDS_MERKLE_INVALID_ENCODING;
    }
    return tds_mst_nullable_utf8_copy(
        destination,
        &source,
        allocator,
        NULL);
}

static tds_merkle_status tds_mst_nullable_bytes_decode(
    const unsigned char *encoding,
    size_t encoding_size,
    void *destination,
    const tds_merkle_allocator *allocator,
    void *context) {
    tds_merkle_nullable_bytes source;
    (void)context;
    if (encoding == NULL || destination == NULL || encoding_size == 0) {
        return TDS_MERKLE_INVALID_ENCODING;
    }
    if (encoding[0] == 0) {
        if (encoding_size != 1) {
            return TDS_MERKLE_INVALID_ENCODING;
        }
        source = (tds_merkle_nullable_bytes){false, NULL, 0};
    } else if (encoding[0] == 1) {
        source = (tds_merkle_nullable_bytes){true, encoding + 1, encoding_size - 1};
    } else {
        return TDS_MERKLE_INVALID_ENCODING;
    }
    return tds_mst_nullable_bytes_copy(
        destination,
        &source,
        allocator,
        NULL);
}

void tds_merkle_type_policy_init(
    tds_merkle_type_policy *type,
    size_t size,
    const void *type_identity) {
    if (type != NULL) {
        memset(type, 0, sizeof(*type));
        type->size = size;
        type->type_identity = type_identity;
    }
}

void tds_merkle_codec_init(
    tds_merkle_codec *codec,
    const unsigned char *encoding_id,
    size_t encoding_id_size,
    tds_merkle_encode_fn encode,
    tds_merkle_decode_fn decode) {
    if (codec != NULL) {
        memset(codec, 0, sizeof(*codec));
        codec->encoding_id.bytes = encoding_id;
        codec->encoding_id.size = encoding_id_size;
        codec->encode = encode;
        codec->decode = decode;
    }
}

void tds_merkle_policy_config_init(tds_merkle_policy_config *config) {
    if (config != NULL) {
        memset(config, 0, sizeof(*config));
        config->allocator.allocate = tds_mst_default_allocate;
        config->allocator.deallocate = tds_mst_default_deallocate;
    }
}

void tds_merkle_i32_type_policy_init(
    tds_merkle_type_policy *type,
    const void *type_identity) {
    tds_merkle_type_policy_init(type, sizeof(int32_t), type_identity);
    if (type != NULL) {
        type->equals = tds_mst_pod_equal;
        type->context = (void *)&tds_mst_i32_size;
    }
}

void tds_merkle_i64_type_policy_init(
    tds_merkle_type_policy *type,
    const void *type_identity) {
    tds_merkle_type_policy_init(type, sizeof(int64_t), type_identity);
    if (type != NULL) {
        type->equals = tds_mst_pod_equal;
        type->context = (void *)&tds_mst_i64_size;
    }
}

void tds_merkle_nullable_utf8_type_policy_init(
    tds_merkle_type_policy *type,
    const void *type_identity) {
    tds_merkle_type_policy_init(type, sizeof(tds_merkle_nullable_utf8), type_identity);
    if (type != NULL) {
        type->copy = tds_mst_nullable_utf8_copy;
        type->destroy = tds_mst_nullable_utf8_destroy;
        type->equals = tds_mst_nullable_utf8_equal;
    }
}

void tds_merkle_nullable_bytes_type_policy_init(
    tds_merkle_type_policy *type,
    const void *type_identity) {
    tds_merkle_type_policy_init(type, sizeof(tds_merkle_nullable_bytes), type_identity);
    if (type != NULL) {
        type->copy = tds_mst_nullable_bytes_copy;
        type->destroy = tds_mst_nullable_bytes_destroy;
        type->equals = tds_mst_nullable_bytes_equal;
    }
}

void tds_merkle_guid_type_policy_init(
    tds_merkle_type_policy *type,
    const void *type_identity) {
    tds_merkle_type_policy_init(type, sizeof(tds_merkle_guid), type_identity);
    if (type != NULL) {
        type->equals = tds_mst_pod_equal;
        type->context = (void *)&tds_mst_guid_size;
    }
}

void tds_merkle_i32_codec_init(tds_merkle_codec *codec) {
    static const unsigned char id[] = "i32-be-v1";
    tds_merkle_codec_init(codec, id, sizeof(id) - 1, tds_mst_i32_encode, tds_mst_i32_decode);
}

void tds_merkle_i64_codec_init(tds_merkle_codec *codec) {
    static const unsigned char id[] = "i64-be-v1";
    tds_merkle_codec_init(codec, id, sizeof(id) - 1, tds_mst_i64_encode, tds_mst_i64_decode);
}

void tds_merkle_nullable_utf8_codec_init(tds_merkle_codec *codec) {
    static const unsigned char id[] = "nullable-utf8-v1";
    tds_merkle_codec_init(
        codec,
        id,
        sizeof(id) - 1,
        tds_mst_nullable_utf8_encode,
        tds_mst_nullable_utf8_decode);
}

void tds_merkle_nullable_bytes_codec_init(tds_merkle_codec *codec) {
    static const unsigned char id[] = "nullable-bytes-v1";
    tds_merkle_codec_init(
        codec,
        id,
        sizeof(id) - 1,
        tds_mst_nullable_bytes_encode,
        tds_mst_nullable_bytes_decode);
}

void tds_merkle_guid_codec_init(tds_merkle_codec *codec) {
    static const unsigned char id[] = "guid-rfc4122-v1";
    tds_merkle_codec_init(codec, id, sizeof(id) - 1, tds_mst_guid_encode, tds_mst_guid_decode);
}

static bool tds_mst_type_valid(const tds_merkle_type_policy *type) {
    return type->size != 0 && type->type_identity != NULL &&
        (type->destroy == NULL || type->copy != NULL);
}

static bool tds_mst_config_valid(const tds_merkle_policy_config *config) {
    return config != NULL &&
        tds_mst_policy_id_valid(config->policy_id) &&
        tds_mst_type_valid(&config->key_type) &&
        tds_mst_type_valid(&config->value_type) &&
        config->key_compare != NULL &&
        tds_mst_encoding_id_valid(config->key_codec.encoding_id) &&
        config->key_codec.encode != NULL && config->key_codec.decode != NULL &&
        tds_mst_encoding_id_valid(config->value_codec.encoding_id) &&
        config->value_codec.encode != NULL && config->value_codec.decode != NULL &&
        config->allocator.allocate != NULL && config->allocator.deallocate != NULL;
}

static void tds_mst_policy_retain(struct tds_merkle_policy_rep *policy) {
    if (policy != NULL) {
        tds_mst_ref_retain(&policy->refs);
    }
}

static void tds_mst_policy_release(struct tds_merkle_policy_rep *policy) {
    if (policy != NULL && tds_mst_ref_release(&policy->refs)) {
        tds_mst_deallocate_config(&policy->config, policy->value_encoding_id);
        tds_mst_deallocate_config(&policy->config, policy->key_encoding_id);
        tds_mst_deallocate_config(&policy->config, policy->policy_id);
        tds_mst_deallocate_config(&policy->config, policy);
    }
}

tds_merkle_status tds_merkle_policy_create(
    const tds_merkle_policy_config *config,
    tds_merkle_policy *policy) {
    struct tds_merkle_policy_rep *rep = NULL;
    tds_merkle_identifier fields[4];
    unsigned char empty_manifest[5 + TDS_MERKLE_DIGEST_BYTE_LENGTH];
    tds_merkle_status status;
    if (policy == NULL || !tds_mst_config_valid(config)) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    rep = (struct tds_merkle_policy_rep *)tds_mst_allocate_config(config, sizeof(*rep));
    if (rep == NULL) {
        return TDS_MERKLE_NO_MEMORY;
    }
    memset(rep, 0, sizeof(*rep));
    rep->config = *config;
    status = tds_mst_copy_bytes(config, config->policy_id, &rep->policy_id);
    if (status == TDS_MERKLE_OK) {
        status = tds_mst_copy_bytes(
            config,
            config->key_codec.encoding_id,
            &rep->key_encoding_id);
    }
    if (status == TDS_MERKLE_OK) {
        status = tds_mst_copy_bytes(
            config,
            config->value_codec.encoding_id,
            &rep->value_encoding_id);
    }
    if (status != TDS_MERKLE_OK) {
        tds_mst_deallocate_config(config, rep->value_encoding_id);
        tds_mst_deallocate_config(config, rep->key_encoding_id);
        tds_mst_deallocate_config(config, rep->policy_id);
        tds_mst_deallocate_config(config, rep);
        return status;
    }
    rep->config.policy_id.bytes = rep->policy_id;
    rep->config.key_codec.encoding_id.bytes = rep->key_encoding_id;
    rep->config.value_codec.encoding_id.bytes = rep->value_encoding_id;
    fields[0] = (tds_merkle_identifier){
        tds_mst_algorithm_id,
        sizeof(tds_mst_algorithm_id) - 1};
    fields[1] = rep->config.policy_id;
    fields[2] = rep->config.key_codec.encoding_id;
    fields[3] = rep->config.value_codec.encoding_id;
    status = tds_mst_hash_framed_config(
        &rep->config,
        0x50,
        fields,
        4,
        &rep->domain_digest);
    if (status == TDS_MERKLE_OK) {
        memcpy(empty_manifest, tds_mst_block_magic, sizeof(tds_mst_block_magic));
        empty_manifest[4] = 0;
        memcpy(
            empty_manifest + 5,
            rep->domain_digest.bytes,
            TDS_MERKLE_DIGEST_BYTE_LENGTH);
        status = tds_mst_sha256_config(
            &rep->config,
            empty_manifest,
            sizeof(empty_manifest),
            &rep->empty_digest);
    }
    if (status != TDS_MERKLE_OK) {
        tds_mst_deallocate_config(&rep->config, rep->value_encoding_id);
        tds_mst_deallocate_config(&rep->config, rep->key_encoding_id);
        tds_mst_deallocate_config(&rep->config, rep->policy_id);
        tds_mst_deallocate_config(&rep->config, rep);
        return status;
    }
    tds_mst_ref_init(&rep->refs);
    policy->rep = rep;
    return TDS_MERKLE_OK;
}

tds_merkle_status tds_merkle_policy_copy(
    const tds_merkle_policy *source,
    tds_merkle_policy *destination) {
    if (source == NULL || source->rep == NULL || destination == NULL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    if (source != destination) {
        tds_mst_policy_retain(source->rep);
        destination->rep = source->rep;
    }
    return TDS_MERKLE_OK;
}

void tds_merkle_policy_move(
    tds_merkle_policy *destination,
    tds_merkle_policy *source) {
    if (destination != NULL && source != NULL && destination != source) {
        destination->rep = source->rep;
        source->rep = NULL;
    }
}

void tds_merkle_policy_dispose(tds_merkle_policy *policy) {
    if (policy != NULL) {
        tds_mst_policy_release(policy->rep);
        policy->rep = NULL;
    }
}

bool tds_merkle_policy_same_identity(
    const tds_merkle_policy *left,
    const tds_merkle_policy *right) {
    return left != NULL && right != NULL && left->rep != NULL && left->rep == right->rep;
}

bool tds_merkle_policy_same_domain(
    const tds_merkle_policy *left,
    const tds_merkle_policy *right) {
    return left != NULL && right != NULL && left->rep != NULL && right->rep != NULL &&
        tds_merkle_digest_equal(left->rep->domain_digest, right->rep->domain_digest);
}

const char *tds_merkle_algorithm_id(void) {
    return (const char *)tds_mst_algorithm_id;
}

tds_merkle_digest tds_merkle_policy_domain_digest(const tds_merkle_policy *policy) {
    tds_merkle_digest result = {{0}};
    return policy == NULL || policy->rep == NULL ? result : policy->rep->domain_digest;
}

tds_merkle_digest tds_merkle_policy_empty_digest(const tds_merkle_policy *policy) {
    tds_merkle_digest result = {{0}};
    return policy == NULL || policy->rep == NULL ? result : policy->rep->empty_digest;
}

static const tds_merkle_type_policy *tds_mst_object_type(
    const struct tds_merkle_policy_rep *policy,
    bool is_key) {
    return is_key ? &policy->config.key_type : &policy->config.value_type;
}

static void tds_mst_bytes_retain(tds_mst_bytes *bytes) {
    if (bytes != NULL) {
        tds_mst_ref_retain(&bytes->refs);
    }
}

static void tds_mst_bytes_release(
    const struct tds_merkle_policy_rep *policy,
    tds_mst_bytes *bytes) {
    if (bytes != NULL && tds_mst_ref_release(&bytes->refs)) {
        tds_mst_deallocate(policy, bytes);
    }
}

static tds_merkle_status tds_mst_bytes_allocate(
    const struct tds_merkle_policy_rep *policy,
    size_t size,
    tds_mst_bytes **result) {
    size_t allocation_size;
    tds_mst_bytes *bytes;
    if (tds_mst_add_overflows(sizeof(*bytes), size, &allocation_size)) {
        return TDS_MERKLE_OVERFLOW;
    }
    bytes = (tds_mst_bytes *)tds_mst_allocate(policy, allocation_size);
    if (bytes == NULL) {
        return TDS_MERKLE_NO_MEMORY;
    }
    tds_mst_ref_init(&bytes->refs);
    bytes->size = size;
    *result = bytes;
    return TDS_MERKLE_OK;
}

static tds_merkle_status tds_mst_encode_value(
    const struct tds_merkle_policy_rep *policy,
    const tds_merkle_codec *codec,
    const void *value,
    tds_mst_bytes **result) {
    size_t required = 0;
    size_t written = 0;
    tds_mst_bytes *bytes = NULL;
    tds_merkle_status status;
    status = codec->encode(
        value,
        NULL,
        0,
        &required,
        &policy->config.allocator,
        codec->context);
    if (status != TDS_MERKLE_OK) {
        return status;
    }
    if (required > INT32_MAX) {
        return TDS_MERKLE_OVERFLOW;
    }
    status = tds_mst_bytes_allocate(policy, required, &bytes);
    if (status != TDS_MERKLE_OK) {
        return status;
    }
    status = codec->encode(
        value,
        bytes->data,
        required,
        &written,
        &policy->config.allocator,
        codec->context);
    if (status == TDS_MERKLE_OK && written != required) {
        status = TDS_MERKLE_INCONSISTENT_POLICY;
    }
    if (status != TDS_MERKLE_OK) {
        tds_mst_bytes_release(policy, bytes);
        return status;
    }
    *result = bytes;
    return TDS_MERKLE_OK;
}

static void tds_mst_object_retain(tds_mst_object *object) {
    if (object != NULL) {
        tds_mst_ref_retain(&object->refs);
    }
}

static void tds_mst_object_release(
    const struct tds_merkle_policy_rep *policy,
    tds_mst_object *object) {
    const tds_merkle_type_policy *type;
    if (object == NULL || !tds_mst_ref_release(&object->refs)) {
        return;
    }
    type = tds_mst_object_type(policy, object->is_key);
    if (type->destroy != NULL) {
        type->destroy(
            object->value,
            &policy->config.allocator,
            type->context);
    }
    tds_mst_deallocate(policy, object->value);
    tds_mst_deallocate(policy, object);
}

static tds_merkle_status tds_mst_object_create(
    const struct tds_merkle_policy_rep *policy,
    bool is_key,
    const void *source,
    tds_mst_object **result) {
    const tds_merkle_type_policy *type = tds_mst_object_type(policy, is_key);
    tds_mst_object *object;
    tds_merkle_status status = TDS_MERKLE_OK;
    if (source == NULL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    object = (tds_mst_object *)tds_mst_allocate(policy, sizeof(*object));
    if (object == NULL) {
        return TDS_MERKLE_NO_MEMORY;
    }
    object->value = tds_mst_allocate(policy, type->size);
    if (object->value == NULL) {
        tds_mst_deallocate(policy, object);
        return TDS_MERKLE_NO_MEMORY;
    }
    if (type->copy == NULL) {
        memcpy(object->value, source, type->size);
    } else {
        status = type->copy(
            object->value,
            source,
            &policy->config.allocator,
            type->context);
        if (status != TDS_MERKLE_OK) {
            tds_mst_deallocate(policy, object->value);
            tds_mst_deallocate(policy, object);
            return status;
        }
    }
    tds_mst_ref_init(&object->refs);
    object->is_key = is_key;
    *result = object;
    return TDS_MERKLE_OK;
}

static void tds_mst_entry_retain(tds_mst_entry *entry) {
    if (entry != NULL) {
        tds_mst_ref_retain(&entry->refs);
    }
}

static void tds_mst_entry_release(
    const struct tds_merkle_policy_rep *policy,
    tds_mst_entry *entry) {
    if (entry == NULL || !tds_mst_ref_release(&entry->refs)) {
        return;
    }
    tds_mst_bytes_release(policy, entry->value_bytes);
    tds_mst_bytes_release(policy, entry->key_bytes);
    tds_mst_object_release(policy, entry->value);
    tds_mst_object_release(policy, entry->key);
    tds_mst_deallocate(policy, entry);
}

static tds_merkle_status tds_mst_entry_from_parts(
    const struct tds_merkle_policy_rep *policy,
    tds_mst_object *key,
    tds_mst_object *value,
    tds_mst_bytes *key_bytes,
    tds_mst_bytes *value_bytes,
    unsigned level,
    tds_mst_entry **result) {
    tds_mst_entry *entry;
    if (key == NULL || value == NULL || key_bytes == NULL || value_bytes == NULL ||
        level > TDS_MST_MAXIMUM_LEVEL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    entry = (tds_mst_entry *)tds_mst_allocate(policy, sizeof(*entry));
    if (entry == NULL) {
        return TDS_MERKLE_NO_MEMORY;
    }
    tds_mst_ref_init(&entry->refs);
    entry->key = key;
    entry->value = value;
    entry->key_bytes = key_bytes;
    entry->value_bytes = value_bytes;
    entry->level = (unsigned char)level;
    tds_mst_object_retain(key);
    tds_mst_object_retain(value);
    tds_mst_bytes_retain(key_bytes);
    tds_mst_bytes_retain(value_bytes);
    *result = entry;
    return TDS_MERKLE_OK;
}

static unsigned tds_mst_level(tds_merkle_digest digest) {
    unsigned result = 0;
    size_t index;
    for (index = 0; index != TDS_MERKLE_DIGEST_BYTE_LENGTH; ++index) {
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

static tds_merkle_status tds_mst_hash_key_bytes(
    const struct tds_merkle_policy_rep *policy,
    const tds_mst_bytes *key_bytes,
    tds_merkle_digest *digest) {
    const tds_merkle_identifier fields[2] = {
        {policy->domain_digest.bytes, TDS_MERKLE_DIGEST_BYTE_LENGTH},
        {key_bytes->data, key_bytes->size}};
    return tds_mst_hash_framed_config(
        &policy->config,
        0x4b,
        fields,
        2,
        digest);
}

static tds_merkle_status tds_mst_entry_create(
    const struct tds_merkle_policy_rep *policy,
    const void *key,
    const void *value,
    tds_mst_entry **result) {
    tds_mst_bytes *key_bytes = NULL;
    tds_mst_bytes *value_bytes = NULL;
    tds_mst_object *stored_key = NULL;
    tds_mst_object *stored_value = NULL;
    tds_merkle_digest key_digest = {{0}};
    tds_merkle_status status = tds_mst_encode_value(
        policy,
        &policy->config.key_codec,
        key,
        &key_bytes);
    if (status == TDS_MERKLE_OK) {
        status = tds_mst_encode_value(
            policy,
            &policy->config.value_codec,
            value,
            &value_bytes);
    }
    if (status == TDS_MERKLE_OK) {
        status = tds_mst_hash_key_bytes(policy, key_bytes, &key_digest);
    }
    if (status == TDS_MERKLE_OK) {
        status = tds_mst_object_create(policy, true, key, &stored_key);
    }
    if (status == TDS_MERKLE_OK) {
        status = tds_mst_object_create(policy, false, value, &stored_value);
    }
    if (status == TDS_MERKLE_OK) {
        status = tds_mst_entry_from_parts(
            policy,
            stored_key,
            stored_value,
            key_bytes,
            value_bytes,
            tds_mst_level(key_digest),
            result);
    }
    tds_mst_object_release(policy, stored_value);
    tds_mst_object_release(policy, stored_key);
    tds_mst_bytes_release(policy, value_bytes);
    tds_mst_bytes_release(policy, key_bytes);
    return status;
}

static tds_merkle_status tds_mst_entry_replace_value(
    const struct tds_merkle_policy_rep *policy,
    const tds_mst_entry *existing,
    const void *value,
    bool *changed,
    tds_mst_entry **result) {
    tds_mst_bytes *value_bytes = NULL;
    tds_mst_object *stored_value = NULL;
    tds_merkle_status status = tds_mst_encode_value(
        policy,
        &policy->config.value_codec,
        value,
        &value_bytes);
    if (status != TDS_MERKLE_OK) {
        return status;
    }
    if (existing->value_bytes->size == value_bytes->size &&
        memcmp(existing->value_bytes->data, value_bytes->data, value_bytes->size) == 0) {
        *changed = false;
        *result = NULL;
        tds_mst_bytes_release(policy, value_bytes);
        return TDS_MERKLE_OK;
    }
    status = tds_mst_object_create(policy, false, value, &stored_value);
    if (status == TDS_MERKLE_OK) {
        status = tds_mst_entry_from_parts(
            policy,
            existing->key,
            stored_value,
            existing->key_bytes,
            value_bytes,
            existing->level,
            result);
    }
    tds_mst_object_release(policy, stored_value);
    tds_mst_bytes_release(policy, value_bytes);
    if (status == TDS_MERKLE_OK) {
        *changed = true;
    }
    return status;
}

static tds_merkle_search_entry_ref tds_mst_entry_ref(const tds_mst_entry *entry) {
    tds_merkle_search_entry_ref result;
    result.key = entry->key->value;
    result.value = entry->value->value;
    result.key_bytes = entry->key_bytes->data;
    result.key_byte_count = entry->key_bytes->size;
    result.value_bytes = entry->value_bytes->data;
    result.value_byte_count = entry->value_bytes->size;
    result.level = entry->level;
    return result;
}

static tds_mst_entry **tds_mst_node_entries(struct tds_merkle_node *node) {
    return (tds_mst_entry **)(void *)node->storage;
}

static tds_mst_entry *const *tds_mst_node_entries_const(
    const struct tds_merkle_node *node) {
    return (tds_mst_entry *const *)(const void *)node->storage;
}

static struct tds_merkle_node **tds_mst_node_children(struct tds_merkle_node *node) {
    return (struct tds_merkle_node **)(void *)(
        tds_mst_node_entries(node) + node->entry_count);
}

static struct tds_merkle_node *const *tds_mst_node_children_const(
    const struct tds_merkle_node *node) {
    return (struct tds_merkle_node *const *)(const void *)(
        tds_mst_node_entries_const(node) + node->entry_count);
}

static void tds_mst_node_retain(struct tds_merkle_node *node) {
    if (node != NULL) {
        tds_mst_ref_retain(&node->refs);
    }
}

static void tds_mst_node_release(
    const struct tds_merkle_policy_rep *policy,
    struct tds_merkle_node *node) {
    struct tds_merkle_node *work = NULL;
    if (node == NULL || !tds_mst_ref_release(&node->refs)) {
        return;
    }
    node->release_next = NULL;
    work = node;
    while (work != NULL) {
        struct tds_merkle_node *current = work;
        tds_mst_entry **entries = tds_mst_node_entries(current);
        struct tds_merkle_node **children = tds_mst_node_children(current);
        const size_t entry_count = current->entry_count;
        size_t index;
        work = current->release_next;
        tds_mst_bytes_release(policy, current->block_bytes);
        for (index = 0; index != entry_count; ++index) {
            tds_mst_entry_release(policy, entries[index]);
        }
        for (index = 0; index != entry_count + 1; ++index) {
            struct tds_merkle_node *child = children[index];
            if (child != NULL && tds_mst_ref_release(&child->refs)) {
                child->release_next = work;
                work = child;
            }
        }
        tds_mst_deallocate(policy, current);
    }
}

static tds_merkle_status tds_mst_allocate_pointer_array(
    const struct tds_merkle_policy_rep *policy,
    size_t count,
    size_t element_size,
    void **result) {
    size_t size;
    if (count == 0) {
        *result = NULL;
        return TDS_MERKLE_OK;
    }
    if (tds_mst_multiply_overflows(count, element_size, &size)) {
        return TDS_MERKLE_OVERFLOW;
    }
    *result = tds_mst_allocate(policy, size);
    return *result == NULL ? TDS_MERKLE_NO_MEMORY : TDS_MERKLE_OK;
}

static tds_merkle_status tds_mst_encode_block(
    const struct tds_merkle_policy_rep *policy,
    unsigned level,
    size_t subtree_count,
    tds_mst_entry *const *entries,
    size_t entry_count,
    struct tds_merkle_node *const *children,
    tds_mst_bytes **result,
    tds_merkle_digest *digest) {
    size_t length = TDS_MST_BLOCK_HEADER_LENGTH;
    size_t child_bytes;
    size_t index;
    size_t offset = 0;
    tds_mst_bytes *bytes = NULL;
    tds_merkle_status status;
    if (level > TDS_MST_MAXIMUM_LEVEL || entry_count == 0 ||
        entry_count > INT32_MAX || subtree_count > INT32_MAX ||
        tds_mst_multiply_overflows(
            entry_count + 1,
            TDS_MERKLE_DIGEST_BYTE_LENGTH,
            &child_bytes) ||
        tds_mst_add_overflows(length, child_bytes, &length)) {
        return TDS_MERKLE_OVERFLOW;
    }
    for (index = 0; index != entry_count; ++index) {
        if (entries[index]->key_bytes->size > INT32_MAX ||
            entries[index]->value_bytes->size > INT32_MAX ||
            tds_mst_add_overflows(length, 8, &length) ||
            tds_mst_add_overflows(length, entries[index]->key_bytes->size, &length) ||
            tds_mst_add_overflows(length, entries[index]->value_bytes->size, &length)) {
            return TDS_MERKLE_OVERFLOW;
        }
    }
    status = tds_mst_bytes_allocate(policy, length, &bytes);
    if (status != TDS_MERKLE_OK) {
        return status;
    }
    memcpy(bytes->data + offset, tds_mst_block_magic, sizeof(tds_mst_block_magic));
    offset += sizeof(tds_mst_block_magic);
    bytes->data[offset++] = TDS_MST_NODE_BLOCK_TAG;
    memcpy(
        bytes->data + offset,
        policy->domain_digest.bytes,
        TDS_MERKLE_DIGEST_BYTE_LENGTH);
    offset += TDS_MERKLE_DIGEST_BYTE_LENGTH;
    bytes->data[offset++] = (unsigned char)level;
    tds_mst_write_be32((uint32_t)subtree_count, bytes->data + offset);
    offset += 4;
    tds_mst_write_be32((uint32_t)entry_count, bytes->data + offset);
    offset += 4;
    for (index = 0; index != entry_count; ++index) {
        tds_mst_write_be32((uint32_t)entries[index]->key_bytes->size, bytes->data + offset);
        offset += 4;
        memcpy(
            bytes->data + offset,
            entries[index]->key_bytes->data,
            entries[index]->key_bytes->size);
        offset += entries[index]->key_bytes->size;
        tds_mst_write_be32((uint32_t)entries[index]->value_bytes->size, bytes->data + offset);
        offset += 4;
        memcpy(
            bytes->data + offset,
            entries[index]->value_bytes->data,
            entries[index]->value_bytes->size);
        offset += entries[index]->value_bytes->size;
    }
    for (index = 0; index != entry_count + 1; ++index) {
        const tds_merkle_digest child_digest = children[index] == NULL
            ? policy->empty_digest
            : children[index]->digest;
        memcpy(
            bytes->data + offset,
            child_digest.bytes,
            TDS_MERKLE_DIGEST_BYTE_LENGTH);
        offset += TDS_MERKLE_DIGEST_BYTE_LENGTH;
    }
    if (offset != length) {
        tds_mst_bytes_release(policy, bytes);
        return TDS_MERKLE_INCONSISTENT_POLICY;
    }
    status = tds_mst_sha256_config(
        &policy->config,
        bytes->data,
        bytes->size,
        digest);
    if (status != TDS_MERKLE_OK) {
        tds_mst_bytes_release(policy, bytes);
        return status;
    }
    *result = bytes;
    return TDS_MERKLE_OK;
}

static tds_merkle_status tds_mst_node_create(
    const struct tds_merkle_policy_rep *policy,
    unsigned level,
    tds_mst_entry *const *entries,
    size_t entry_count,
    struct tds_merkle_node *const *children,
    struct tds_merkle_node **result) {
    struct tds_merkle_node *node = NULL;
    tds_mst_entry **stored_entries;
    struct tds_merkle_node **stored_children;
    tds_mst_bytes *block_bytes = NULL;
    tds_merkle_digest digest;
    size_t count = entry_count;
    size_t height = 1;
    size_t block_count = 1;
    size_t entry_storage;
    size_t child_storage;
    size_t allocation_size;
    size_t index;
    tds_merkle_status status;
    if (policy == NULL || entries == NULL || children == NULL || result == NULL ||
        level > TDS_MST_MAXIMUM_LEVEL || entry_count == 0 || entry_count == SIZE_MAX ||
        tds_mst_multiply_overflows(entry_count, sizeof(*stored_entries), &entry_storage) ||
        tds_mst_multiply_overflows(entry_count + 1, sizeof(*stored_children), &child_storage) ||
        tds_mst_add_overflows(sizeof(*node), entry_storage, &allocation_size) ||
        tds_mst_add_overflows(allocation_size, child_storage, &allocation_size)) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    for (index = 0; index != entry_count; ++index) {
        if (entries[index] == NULL || entries[index]->level != level) {
            return TDS_MERKLE_INCONSISTENT_POLICY;
        }
    }
    for (index = 0; index != entry_count + 1; ++index) {
        const struct tds_merkle_node *child = children[index];
        if (child != NULL) {
            size_t candidate_height;
            if (child->level >= level ||
                tds_mst_add_overflows(count, child->count, &count) ||
                tds_mst_add_overflows(block_count, child->block_count, &block_count) ||
                tds_mst_add_overflows(child->height, 1, &candidate_height)) {
                return TDS_MERKLE_OVERFLOW;
            }
            if (candidate_height > height) {
                height = candidate_height;
            }
        }
    }
    status = tds_mst_encode_block(
        policy,
        level,
        count,
        entries,
        entry_count,
        children,
        &block_bytes,
        &digest);
    if (status != TDS_MERKLE_OK) {
        return status;
    }
    node = (struct tds_merkle_node *)tds_mst_allocate(policy, allocation_size);
    if (node == NULL) {
        tds_mst_bytes_release(policy, block_bytes);
        return TDS_MERKLE_NO_MEMORY;
    }
    tds_mst_ref_init(&node->refs);
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
    stored_entries = tds_mst_node_entries(node);
    stored_children = tds_mst_node_children(node);
    for (index = 0; index != entry_count; ++index) {
        stored_entries[index] = entries[index];
        tds_mst_entry_retain(entries[index]);
    }
    for (index = 0; index != entry_count + 1; ++index) {
        stored_children[index] = children[index];
        tds_mst_node_retain(children[index]);
    }
    *result = node;
    return TDS_MERKLE_OK;
}

static bool tds_mst_tree_valid(const tds_merkle_search_tree *tree) {
    return tree != NULL && tree->policy != NULL;
}

static tds_merkle_status tds_mst_key_compare(
    const struct tds_merkle_policy_rep *policy,
    const void *left,
    const void *right,
    int *comparison) {
    return policy->config.key_compare(
        left,
        right,
        comparison,
        policy->config.key_compare_context);
}

static tds_merkle_status tds_mst_values_equal(
    const struct tds_merkle_policy_rep *policy,
    const tds_mst_entry *left,
    const tds_mst_entry *right,
    bool *equal) {
    if (left->value_bytes->size == right->value_bytes->size &&
        memcmp(left->value_bytes->data, right->value_bytes->data, left->value_bytes->size) == 0) {
        *equal = true;
        return TDS_MERKLE_OK;
    }
    if (policy->config.value_type.equals == NULL) {
        *equal = false;
        return TDS_MERKLE_OK;
    }
    return policy->config.value_type.equals(
        left->value->value,
        right->value->value,
        equal,
        policy->config.value_type.context);
}

static tds_merkle_status tds_mst_find_position(
    const struct tds_merkle_policy_rep *policy,
    tds_mst_entry *const *entries,
    size_t entry_count,
    const void *key,
    size_t *position,
    bool *found) {
    size_t low = 0;
    size_t high = entry_count;
    tds_merkle_status status;
    while (low < high) {
        const size_t middle = low + (high - low) / 2;
        int comparison = 0;
        status = tds_mst_key_compare(
            policy,
            entries[middle]->key->value,
            key,
            &comparison);
        if (status != TDS_MERKLE_OK) {
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
        status = tds_mst_key_compare(
            policy,
            entries[low]->key->value,
            key,
            &comparison);
        if (status != TDS_MERKLE_OK) {
            return status;
        }
        *found = comparison == 0;
    }
    *position = low;
    return TDS_MERKLE_OK;
}

static tds_merkle_status tds_mst_find_node(
    const tds_merkle_search_tree *tree,
    const void *key,
    struct tds_merkle_node **node,
    size_t *entry_index) {
    struct tds_merkle_node *cursor = tree->root;
    while (cursor != NULL) {
        tds_mst_entry *const *entries = tds_mst_node_entries_const(cursor);
        struct tds_merkle_node *const *children = tds_mst_node_children_const(cursor);
        size_t position = 0;
        bool found = false;
        tds_merkle_status status = tds_mst_find_position(
            tree->policy,
            entries,
            cursor->entry_count,
            key,
            &position,
            &found);
        if (status != TDS_MERKLE_OK) {
            return status;
        }
        if (found) {
            *node = cursor;
            *entry_index = position;
            return TDS_MERKLE_OK;
        }
        cursor = children[position];
    }
    *node = NULL;
    *entry_index = 0;
    return TDS_MERKLE_OK;
}

static tds_merkle_status tds_mst_node_rebuild(
    const struct tds_merkle_policy_rep *policy,
    const struct tds_merkle_node *source,
    size_t entry_index,
    tds_mst_entry *entry,
    size_t child_index,
    struct tds_merkle_node *child,
    struct tds_merkle_node **result) {
    tds_mst_entry **entries = NULL;
    struct tds_merkle_node **children = NULL;
    tds_merkle_status status = tds_mst_allocate_pointer_array(
        policy,
        source->entry_count,
        sizeof(*entries),
        (void **)&entries);
    if (status == TDS_MERKLE_OK) {
        status = tds_mst_allocate_pointer_array(
            policy,
            source->entry_count + 1,
            sizeof(*children),
            (void **)&children);
    }
    if (status == TDS_MERKLE_OK &&
        (source->entry_count == 0 || entries == NULL || children == NULL)) {
        status = TDS_MERKLE_INCONSISTENT_POLICY;
    }
    if (status == TDS_MERKLE_OK) {
        memcpy(
            entries,
            tds_mst_node_entries_const(source),
            source->entry_count * sizeof(*entries));
        memcpy(
            children,
            tds_mst_node_children_const(source),
            (source->entry_count + 1) * sizeof(*children));
        if (entry_index != SIZE_MAX) {
            entries[entry_index] = entry;
        }
        if (child_index != SIZE_MAX) {
            children[child_index] = child;
        }
        status = tds_mst_node_create(
            policy,
            source->level,
            entries,
            source->entry_count,
            children,
            result);
    }
    tds_mst_deallocate(policy, children);
    tds_mst_deallocate(policy, entries);
    return status;
}

static tds_merkle_status tds_mst_create_or_collapse(
    const struct tds_merkle_policy_rep *policy,
    unsigned level,
    tds_mst_entry *const *entries,
    size_t entry_count,
    struct tds_merkle_node *const *children,
    struct tds_merkle_node **result) {
    if (entry_count == 0) {
        tds_mst_node_retain(children[0]);
        *result = children[0];
        return TDS_MERKLE_OK;
    }
    return tds_mst_node_create(
        policy,
        level,
        entries,
        entry_count,
        children,
        result);
}

static tds_merkle_status tds_mst_build_canonical(
    const struct tds_merkle_policy_rep *policy,
    tds_mst_entry *const *ordered,
    size_t count,
    struct tds_merkle_node **result) {
    unsigned maximum_level = 0;
    size_t separator_count = 0;
    size_t segment_start = 0;
    size_t entry_index = 0;
    size_t index;
    tds_mst_entry **entries = NULL;
    struct tds_merkle_node **children = NULL;
    tds_merkle_status status = TDS_MERKLE_OK;
    if (count == 0) {
        *result = NULL;
        return TDS_MERKLE_OK;
    }
    for (index = 0; index != count; ++index) {
        if (ordered[index]->level > maximum_level) {
            maximum_level = ordered[index]->level;
        }
    }
    for (index = 0; index != count; ++index) {
        separator_count += ordered[index]->level == maximum_level ? 1u : 0u;
    }
    status = tds_mst_allocate_pointer_array(
        policy,
        separator_count,
        sizeof(*entries),
        (void **)&entries);
    if (status == TDS_MERKLE_OK) {
        status = tds_mst_allocate_pointer_array(
            policy,
            separator_count + 1,
            sizeof(*children),
            (void **)&children);
    }
    if (status != TDS_MERKLE_OK) {
        goto cleanup;
    }
    if (entries == NULL || children == NULL) {
        status = TDS_MERKLE_NO_MEMORY;
        goto cleanup;
    }
    memset(children, 0, (separator_count + 1) * sizeof(*children));
    for (index = 0; index != count; ++index) {
        if (ordered[index]->level != maximum_level) {
            continue;
        }
        status = tds_mst_build_canonical(
            policy,
            ordered + segment_start,
            index - segment_start,
            &children[entry_index]);
        if (status != TDS_MERKLE_OK) {
            goto cleanup;
        }
        entries[entry_index++] = ordered[index];
        segment_start = index + 1;
    }
    status = tds_mst_build_canonical(
        policy,
        ordered + segment_start,
        count - segment_start,
        &children[separator_count]);
    if (status == TDS_MERKLE_OK) {
        status = tds_mst_node_create(
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
            tds_mst_node_release(policy, children[index]);
        }
    }
    tds_mst_deallocate(policy, children);
    tds_mst_deallocate(policy, entries);
    return status;
}

static tds_merkle_status tds_mst_sort_pending(
    const struct tds_merkle_policy_rep *policy,
    tds_mst_pending_entry *entries,
    tds_mst_pending_entry *scratch,
    size_t count,
    tds_mst_pending_entry **sorted) {
    tds_mst_pending_entry *source = entries;
    tds_mst_pending_entry *target = scratch;
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
                tds_merkle_status status = tds_mst_key_compare(
                    policy,
                    source[left].key,
                    source[right].key,
                    &comparison);
                if (status != TDS_MERKLE_OK) {
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
            tds_mst_pending_entry *swap = source;
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
    return TDS_MERKLE_OK;
}

static tds_merkle_status tds_mst_split(
    const struct tds_merkle_policy_rep *policy,
    const struct tds_merkle_node *node,
    const void *key,
    struct tds_merkle_node **left_result,
    struct tds_merkle_node **right_result) {
    size_t position = 0;
    bool found = false;
    tds_mst_entry **left_entries = NULL;
    tds_mst_entry **right_entries = NULL;
    struct tds_merkle_node **left_children = NULL;
    struct tds_merkle_node **right_children = NULL;
    struct tds_merkle_node *child_left = NULL;
    struct tds_merkle_node *child_right = NULL;
    struct tds_merkle_node *left = NULL;
    struct tds_merkle_node *right = NULL;
    tds_mst_entry *const *source_entries;
    struct tds_merkle_node *const *source_children;
    size_t right_count;
    tds_merkle_status status;
    if (node == NULL) {
        *left_result = NULL;
        *right_result = NULL;
        return TDS_MERKLE_OK;
    }
    source_entries = tds_mst_node_entries_const(node);
    source_children = tds_mst_node_children_const(node);
    status = tds_mst_find_position(
        policy,
        source_entries,
        node->entry_count,
        key,
        &position,
        &found);
    if (status != TDS_MERKLE_OK) {
        return status;
    }
    if (found) {
        return TDS_MERKLE_INCONSISTENT_POLICY;
    }
    status = tds_mst_split(
        policy,
        source_children[position],
        key,
        &child_left,
        &child_right);
    if (status != TDS_MERKLE_OK) {
        goto cleanup;
    }
    right_count = node->entry_count - position;
    status = tds_mst_allocate_pointer_array(
        policy,
        position,
        sizeof(*left_entries),
        (void **)&left_entries);
    if (status == TDS_MERKLE_OK) {
        status = tds_mst_allocate_pointer_array(
            policy,
            position + 1,
            sizeof(*left_children),
            (void **)&left_children);
    }
    if (status == TDS_MERKLE_OK) {
        status = tds_mst_allocate_pointer_array(
            policy,
            right_count,
            sizeof(*right_entries),
            (void **)&right_entries);
    }
    if (status == TDS_MERKLE_OK) {
        status = tds_mst_allocate_pointer_array(
            policy,
            right_count + 1,
            sizeof(*right_children),
            (void **)&right_children);
    }
    if (status != TDS_MERKLE_OK) {
        goto cleanup;
    }
    if ((position != 0 && left_entries == NULL) || left_children == NULL ||
        (right_count != 0 && right_entries == NULL) || right_children == NULL) {
        status = TDS_MERKLE_NO_MEMORY;
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
    status = tds_mst_create_or_collapse(
        policy,
        node->level,
        left_entries,
        position,
        left_children,
        &left);
    if (status == TDS_MERKLE_OK) {
        status = tds_mst_create_or_collapse(
            policy,
            node->level,
            right_entries,
            right_count,
            right_children,
            &right);
    }
    if (status == TDS_MERKLE_OK) {
        *left_result = left;
        *right_result = right;
        left = NULL;
        right = NULL;
    }

cleanup:
    tds_mst_node_release(policy, right);
    tds_mst_node_release(policy, left);
    tds_mst_node_release(policy, child_right);
    tds_mst_node_release(policy, child_left);
    tds_mst_deallocate(policy, right_children);
    tds_mst_deallocate(policy, right_entries);
    tds_mst_deallocate(policy, left_children);
    tds_mst_deallocate(policy, left_entries);
    return status;
}

static tds_merkle_status tds_mst_insert(
    const struct tds_merkle_policy_rep *policy,
    const struct tds_merkle_node *node,
    tds_mst_entry *entry,
    struct tds_merkle_node **result) {
    tds_mst_entry *singleton_entries[1] = {entry};
    struct tds_merkle_node *singleton_children[2] = {NULL, NULL};
    size_t position = 0;
    bool found = false;
    tds_merkle_status status;
    if (node == NULL) {
        return tds_mst_node_create(
            policy,
            entry->level,
            singleton_entries,
            1,
            singleton_children,
            result);
    }
    if (entry->level > node->level) {
        struct tds_merkle_node *left = NULL;
        struct tds_merkle_node *right = NULL;
        status = tds_mst_split(policy, node, entry->key->value, &left, &right);
        if (status == TDS_MERKLE_OK) {
            singleton_children[0] = left;
            singleton_children[1] = right;
            status = tds_mst_node_create(
                policy,
                entry->level,
                singleton_entries,
                1,
                singleton_children,
                result);
        }
        tds_mst_node_release(policy, right);
        tds_mst_node_release(policy, left);
        return status;
    }
    status = tds_mst_find_position(
        policy,
        tds_mst_node_entries_const(node),
        node->entry_count,
        entry->key->value,
        &position,
        &found);
    if (status != TDS_MERKLE_OK) {
        return status;
    }
    if (found) {
        return TDS_MERKLE_INCONSISTENT_POLICY;
    }
    if (entry->level < node->level) {
        struct tds_merkle_node *child = NULL;
        status = tds_mst_insert(
            policy,
            tds_mst_node_children_const(node)[position],
            entry,
            &child);
        if (status == TDS_MERKLE_OK) {
            status = tds_mst_node_rebuild(
                policy,
                node,
                SIZE_MAX,
                NULL,
                position,
                child,
                result);
        }
        tds_mst_node_release(policy, child);
        return status;
    }
    {
        tds_mst_entry **entries = NULL;
        struct tds_merkle_node **children = NULL;
        struct tds_merkle_node *left = NULL;
        struct tds_merkle_node *right = NULL;
        tds_mst_entry *const *source_entries = tds_mst_node_entries_const(node);
        struct tds_merkle_node *const *source_children = tds_mst_node_children_const(node);
        status = tds_mst_split(
            policy,
            source_children[position],
            entry->key->value,
            &left,
            &right);
        if (status == TDS_MERKLE_OK) {
            status = tds_mst_allocate_pointer_array(
                policy,
                node->entry_count + 1,
                sizeof(*entries),
                (void **)&entries);
        }
        if (status == TDS_MERKLE_OK) {
            status = tds_mst_allocate_pointer_array(
                policy,
                node->entry_count + 2,
                sizeof(*children),
                (void **)&children);
        }
        if (status == TDS_MERKLE_OK && (entries == NULL || children == NULL)) {
            status = TDS_MERKLE_NO_MEMORY;
        }
        if (status == TDS_MERKLE_OK) {
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
            status = tds_mst_node_create(
                policy,
                node->level,
                entries,
                node->entry_count + 1,
                children,
                result);
        }
        tds_mst_deallocate(policy, children);
        tds_mst_deallocate(policy, entries);
        tds_mst_node_release(policy, right);
        tds_mst_node_release(policy, left);
        return status;
    }
}

static tds_merkle_status tds_mst_update_value(
    const struct tds_merkle_policy_rep *policy,
    const struct tds_merkle_node *node,
    const void *key,
    tds_mst_entry *replacement,
    struct tds_merkle_node **result) {
    size_t position = 0;
    bool found = false;
    tds_merkle_status status = tds_mst_find_position(
        policy,
        tds_mst_node_entries_const(node),
        node->entry_count,
        key,
        &position,
        &found);
    if (status != TDS_MERKLE_OK) {
        return status;
    }
    if (found) {
        return tds_mst_node_rebuild(
            policy,
            node,
            position,
            replacement,
            SIZE_MAX,
            NULL,
            result);
    }
    {
        struct tds_merkle_node *child = NULL;
        struct tds_merkle_node *source_child = tds_mst_node_children_const(node)[position];
        if (source_child == NULL) {
            return TDS_MERKLE_INCONSISTENT_POLICY;
        }
        status = tds_mst_update_value(
            policy,
            source_child,
            key,
            replacement,
            &child);
        if (status == TDS_MERKLE_OK) {
            status = tds_mst_node_rebuild(
                policy,
                node,
                SIZE_MAX,
                NULL,
                position,
                child,
                result);
        }
        tds_mst_node_release(policy, child);
        return status;
    }
}

static tds_merkle_status tds_mst_join(
    const struct tds_merkle_policy_rep *policy,
    const struct tds_merkle_node *left,
    const struct tds_merkle_node *right,
    struct tds_merkle_node **result) {
    if (left == NULL || right == NULL) {
        struct tds_merkle_node *retained = (struct tds_merkle_node *)(left == NULL ? right : left);
        tds_mst_node_retain(retained);
        *result = retained;
        return TDS_MERKLE_OK;
    }
    if (left->level > right->level) {
        struct tds_merkle_node *joined = NULL;
        tds_merkle_status status = tds_mst_join(
            policy,
            tds_mst_node_children_const(left)[left->entry_count],
            right,
            &joined);
        if (status == TDS_MERKLE_OK) {
            status = tds_mst_node_rebuild(
                policy,
                left,
                SIZE_MAX,
                NULL,
                left->entry_count,
                joined,
                result);
        }
        tds_mst_node_release(policy, joined);
        return status;
    }
    if (left->level < right->level) {
        struct tds_merkle_node *joined = NULL;
        tds_merkle_status status = tds_mst_join(
            policy,
            left,
            tds_mst_node_children_const(right)[0],
            &joined);
        if (status == TDS_MERKLE_OK) {
            status = tds_mst_node_rebuild(
                policy,
                right,
                SIZE_MAX,
                NULL,
                0,
                joined,
                result);
        }
        tds_mst_node_release(policy, joined);
        return status;
    }
    {
        const size_t entry_count = left->entry_count + right->entry_count;
        tds_mst_entry **entries = NULL;
        struct tds_merkle_node **children = NULL;
        struct tds_merkle_node *middle = NULL;
        tds_merkle_status status;
        if (entry_count < left->entry_count) {
            return TDS_MERKLE_OVERFLOW;
        }
        status = tds_mst_join(
            policy,
            tds_mst_node_children_const(left)[left->entry_count],
            tds_mst_node_children_const(right)[0],
            &middle);
        if (status == TDS_MERKLE_OK) {
            status = tds_mst_allocate_pointer_array(
                policy,
                entry_count,
                sizeof(*entries),
                (void **)&entries);
        }
        if (status == TDS_MERKLE_OK) {
            status = tds_mst_allocate_pointer_array(
                policy,
                entry_count + 1,
                sizeof(*children),
                (void **)&children);
        }
        if (status == TDS_MERKLE_OK && (entries == NULL || children == NULL)) {
            status = TDS_MERKLE_NO_MEMORY;
        }
        if (status == TDS_MERKLE_OK) {
            memcpy(
                entries,
                tds_mst_node_entries_const(left),
                left->entry_count * sizeof(*entries));
            memcpy(
                entries + left->entry_count,
                tds_mst_node_entries_const(right),
                right->entry_count * sizeof(*entries));
            memcpy(
                children,
                tds_mst_node_children_const(left),
                left->entry_count * sizeof(*children));
            children[left->entry_count] = middle;
            memcpy(
                children + left->entry_count + 1,
                tds_mst_node_children_const(right) + 1,
                right->entry_count * sizeof(*children));
            status = tds_mst_node_create(
                policy,
                left->level,
                entries,
                entry_count,
                children,
                result);
        }
        tds_mst_deallocate(policy, children);
        tds_mst_deallocate(policy, entries);
        tds_mst_node_release(policy, middle);
        return status;
    }
}

static tds_merkle_status tds_mst_remove_node(
    const struct tds_merkle_policy_rep *policy,
    const struct tds_merkle_node *node,
    const void *key,
    struct tds_merkle_node **result) {
    size_t position = 0;
    bool found = false;
    tds_merkle_status status = tds_mst_find_position(
        policy,
        tds_mst_node_entries_const(node),
        node->entry_count,
        key,
        &position,
        &found);
    if (status != TDS_MERKLE_OK) {
        return status;
    }
    if (!found) {
        struct tds_merkle_node *child = NULL;
        const struct tds_merkle_node *source_child = tds_mst_node_children_const(node)[position];
        if (source_child == NULL) {
            return TDS_MERKLE_INCONSISTENT_POLICY;
        }
        status = tds_mst_remove_node(policy, source_child, key, &child);
        if (status == TDS_MERKLE_OK) {
            status = tds_mst_node_rebuild(
                policy,
                node,
                SIZE_MAX,
                NULL,
                position,
                child,
                result);
        }
        tds_mst_node_release(policy, child);
        return status;
    }
    {
        const size_t new_entry_count = node->entry_count - 1;
        tds_mst_entry **entries = NULL;
        struct tds_merkle_node **children = NULL;
        struct tds_merkle_node *joined = NULL;
        tds_mst_entry *const *source_entries = tds_mst_node_entries_const(node);
        struct tds_merkle_node *const *source_children = tds_mst_node_children_const(node);
        status = tds_mst_join(
            policy,
            source_children[position],
            source_children[position + 1],
            &joined);
        if (status == TDS_MERKLE_OK) {
            status = tds_mst_allocate_pointer_array(
                policy,
                new_entry_count,
                sizeof(*entries),
                (void **)&entries);
        }
        if (status == TDS_MERKLE_OK) {
            status = tds_mst_allocate_pointer_array(
                policy,
                new_entry_count + 1,
                sizeof(*children),
                (void **)&children);
        }
        if (status == TDS_MERKLE_OK &&
            ((new_entry_count != 0 && entries == NULL) || children == NULL)) {
            status = TDS_MERKLE_NO_MEMORY;
        }
        if (status == TDS_MERKLE_OK) {
            if (position != 0) {
                if (entries == NULL || children == NULL) {
                    status = TDS_MERKLE_NO_MEMORY;
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
            status = tds_mst_create_or_collapse(
                policy,
                node->level,
                entries,
                new_entry_count,
                children,
                result);
        }
remove_cleanup:
        tds_mst_deallocate(policy, children);
        tds_mst_deallocate(policy, entries);
        tds_mst_node_release(policy, joined);
        return status;
    }
}

static void tds_mst_publish_tree(
    const tds_merkle_search_tree *source,
    tds_merkle_search_tree *destination,
    tds_merkle_search_tree produced) {
    if (source == destination) {
        tds_merkle_search_tree old = *destination;
        *destination = produced;
        tds_merkle_search_tree_dispose(&old);
    } else {
        *destination = produced;
    }
}

static tds_merkle_search_tree tds_mst_adopt_tree(
    struct tds_merkle_policy_rep *policy,
    struct tds_merkle_node *root) {
    tds_merkle_search_tree result;
    tds_mst_policy_retain(policy);
    result.policy = policy;
    result.root = root;
    return result;
}

tds_merkle_status tds_merkle_search_tree_init(
    tds_merkle_search_tree *tree,
    const tds_merkle_policy *policy) {
    if (tree == NULL || policy == NULL || policy->rep == NULL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    *tree = tds_mst_adopt_tree(policy->rep, NULL);
    return TDS_MERKLE_OK;
}

tds_merkle_status tds_merkle_search_tree_from_array(
    tds_merkle_search_tree *tree,
    const tds_merkle_policy *policy,
    const tds_merkle_search_input *inputs,
    size_t input_count) {
    tds_mst_pending_entry *pending = NULL;
    tds_mst_pending_entry *scratch = NULL;
    tds_mst_pending_entry *sorted = NULL;
    tds_mst_entry **entries = NULL;
    size_t entry_count = 0;
    struct tds_merkle_node *root = NULL;
    size_t index;
    tds_merkle_status status;
    if (tree == NULL || policy == NULL || policy->rep == NULL ||
        (input_count != 0 && inputs == NULL)) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    if (input_count == 0) {
        *tree = tds_mst_adopt_tree(policy->rep, NULL);
        return TDS_MERKLE_OK;
    }
    status = tds_mst_allocate_pointer_array(
        policy->rep,
        input_count,
        sizeof(*pending),
        (void **)&pending);
    if (status == TDS_MERKLE_OK) {
        status = tds_mst_allocate_pointer_array(
            policy->rep,
            input_count,
            sizeof(*scratch),
            (void **)&scratch);
    }
    if (status == TDS_MERKLE_OK) {
        status = tds_mst_allocate_pointer_array(
            policy->rep,
            input_count,
            sizeof(*entries),
            (void **)&entries);
    }
    if (status != TDS_MERKLE_OK) {
        goto cleanup;
    }
    memset(entries, 0, input_count * sizeof(*entries));
    for (index = 0; index != input_count; ++index) {
        if (inputs[index].key == NULL || inputs[index].value == NULL) {
            status = TDS_MERKLE_INVALID_ARGUMENT;
            goto cleanup;
        }
        pending[index] = (tds_mst_pending_entry){
            inputs[index].key,
            inputs[index].value,
            index};
    }
    status = tds_mst_sort_pending(
        policy->rep,
        pending,
        scratch,
        input_count,
        &sorted);
    if (status != TDS_MERKLE_OK) {
        goto cleanup;
    }
    index = 0;
    while (index != input_count) {
        size_t end = index + 1;
        while (end != input_count) {
            int comparison = 0;
            status = tds_mst_key_compare(
                policy->rep,
                sorted[index].key,
                sorted[end].key,
                &comparison);
            if (status != TDS_MERKLE_OK) {
                goto cleanup;
            }
            if (comparison != 0) {
                break;
            }
            ++end;
        }
        status = tds_mst_entry_create(
            policy->rep,
            sorted[index].key,
            sorted[end - 1].value,
            &entries[entry_count]);
        if (status != TDS_MERKLE_OK) {
            goto cleanup;
        }
        ++entry_count;
        index = end;
    }
    status = tds_mst_build_canonical(policy->rep, entries, entry_count, &root);
    if (status == TDS_MERKLE_OK) {
        *tree = tds_mst_adopt_tree(policy->rep, root);
        root = NULL;
    }

cleanup:
    tds_mst_node_release(policy->rep, root);
    if (entries != NULL) {
        for (index = 0; index != entry_count; ++index) {
            tds_mst_entry_release(policy->rep, entries[index]);
        }
    }
    tds_mst_deallocate(policy->rep, entries);
    tds_mst_deallocate(policy->rep, scratch);
    tds_mst_deallocate(policy->rep, pending);
    return status;
}

tds_merkle_status tds_merkle_search_tree_copy(
    const tds_merkle_search_tree *source,
    tds_merkle_search_tree *destination) {
    if (!tds_mst_tree_valid(source) || destination == NULL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    if (source != destination) {
        tds_mst_policy_retain(source->policy);
        tds_mst_node_retain(source->root);
        destination->policy = source->policy;
        destination->root = source->root;
    }
    return TDS_MERKLE_OK;
}

void tds_merkle_search_tree_move(
    tds_merkle_search_tree *destination,
    tds_merkle_search_tree *source) {
    if (destination != NULL && source != NULL && destination != source) {
        *destination = *source;
        memset(source, 0, sizeof(*source));
    }
}

void tds_merkle_search_tree_dispose(tds_merkle_search_tree *tree) {
    if (tree != NULL) {
        if (tree->policy != NULL) {
            tds_mst_node_release(tree->policy, tree->root);
            tds_mst_policy_release(tree->policy);
        }
        memset(tree, 0, sizeof(*tree));
    }
}

bool tds_merkle_search_tree_empty(const tds_merkle_search_tree *tree) {
    return tds_mst_tree_valid(tree) && tree->root == NULL;
}

size_t tds_merkle_search_tree_size(const tds_merkle_search_tree *tree) {
    return !tds_mst_tree_valid(tree) || tree->root == NULL ? 0 : tree->root->count;
}

size_t tds_merkle_search_tree_height(const tds_merkle_search_tree *tree) {
    return !tds_mst_tree_valid(tree) || tree->root == NULL ? 0 : tree->root->height;
}

size_t tds_merkle_search_tree_block_count(const tds_merkle_search_tree *tree) {
    return !tds_mst_tree_valid(tree) || tree->root == NULL ? 0 : tree->root->block_count;
}

tds_merkle_digest tds_merkle_search_tree_root_hash(const tds_merkle_search_tree *tree) {
    tds_merkle_digest result = {{0}};
    if (tds_mst_tree_valid(tree)) {
        result = tree->root == NULL ? tree->policy->empty_digest : tree->root->digest;
    }
    return result;
}

tds_merkle_status tds_merkle_search_tree_contains_key(
    const tds_merkle_search_tree *tree,
    const void *key,
    bool *found) {
    struct tds_merkle_node *node = NULL;
    size_t entry_index = 0;
    tds_merkle_status status;
    if (!tds_mst_tree_valid(tree) || key == NULL || found == NULL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    status = tds_mst_find_node(tree, key, &node, &entry_index);
    if (status == TDS_MERKLE_OK) {
        *found = node != NULL;
    }
    return status;
}

tds_merkle_status tds_merkle_search_tree_try_get_entry_ref(
    const tds_merkle_search_tree *tree,
    const void *key,
    bool *found,
    tds_merkle_search_entry_ref *entry) {
    struct tds_merkle_node *node = NULL;
    size_t entry_index = 0;
    tds_merkle_status status;
    if (!tds_mst_tree_valid(tree) || key == NULL || found == NULL || entry == NULL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    status = tds_mst_find_node(tree, key, &node, &entry_index);
    if (status == TDS_MERKLE_OK) {
        *found = node != NULL;
        if (node == NULL) {
            memset(entry, 0, sizeof(*entry));
        } else {
            *entry = tds_mst_entry_ref(tds_mst_node_entries(node)[entry_index]);
        }
    }
    return status;
}

tds_merkle_status tds_merkle_search_tree_set(
    const tds_merkle_search_tree *tree,
    const void *key,
    const void *value,
    tds_merkle_search_tree *result) {
    struct tds_merkle_node *found_node = NULL;
    size_t found_index = 0;
    tds_mst_entry *entry = NULL;
    struct tds_merkle_node *root = NULL;
    tds_merkle_status status;
    if (!tds_mst_tree_valid(tree) || key == NULL || value == NULL || result == NULL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    status = tds_mst_find_node(tree, key, &found_node, &found_index);
    if (status != TDS_MERKLE_OK) {
        return status;
    }
    if (found_node != NULL) {
        bool changed = false;
        status = tds_mst_entry_replace_value(
            tree->policy,
            tds_mst_node_entries(found_node)[found_index],
            value,
            &changed,
            &entry);
        if (status == TDS_MERKLE_OK && !changed) {
            tds_mst_node_retain(tree->root);
            root = tree->root;
        } else if (status == TDS_MERKLE_OK) {
            status = tds_mst_update_value(tree->policy, tree->root, key, entry, &root);
        }
    } else {
        status = tds_mst_entry_create(tree->policy, key, value, &entry);
        if (status == TDS_MERKLE_OK) {
            status = tds_mst_insert(tree->policy, tree->root, entry, &root);
        }
    }
    if (status == TDS_MERKLE_OK) {
        const tds_merkle_search_tree produced = tds_mst_adopt_tree(tree->policy, root);
        root = NULL;
        tds_mst_publish_tree(tree, result, produced);
    }
    tds_mst_node_release(tree->policy, root);
    tds_mst_entry_release(tree->policy, entry);
    return status;
}

tds_merkle_status tds_merkle_search_tree_remove(
    const tds_merkle_search_tree *tree,
    const void *key,
    tds_merkle_search_tree *result) {
    struct tds_merkle_node *found_node = NULL;
    size_t found_index = 0;
    struct tds_merkle_node *root = NULL;
    tds_merkle_status status;
    if (!tds_mst_tree_valid(tree) || key == NULL || result == NULL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    status = tds_mst_find_node(tree, key, &found_node, &found_index);
    (void)found_index;
    if (status == TDS_MERKLE_OK && found_node == NULL) {
        tds_mst_node_retain(tree->root);
        root = tree->root;
    } else if (status == TDS_MERKLE_OK) {
        status = tds_mst_remove_node(tree->policy, tree->root, key, &root);
    }
    if (status == TDS_MERKLE_OK) {
        const tds_merkle_search_tree produced = tds_mst_adopt_tree(tree->policy, root);
        root = NULL;
        tds_mst_publish_tree(tree, result, produced);
    }
    tds_mst_node_release(tree->policy, root);
    return status;
}

tds_merkle_status tds_merkle_search_tree_clear(
    const tds_merkle_search_tree *tree,
    tds_merkle_search_tree *result) {
    struct tds_merkle_node *root = NULL;
    if (!tds_mst_tree_valid(tree) || result == NULL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    if (tree->root == NULL) {
        tds_mst_node_retain(tree->root);
        root = tree->root;
    }
    {
        const tds_merkle_search_tree produced = tds_mst_adopt_tree(tree->policy, root);
        tds_mst_publish_tree(tree, result, produced);
    }
    return TDS_MERKLE_OK;
}

static void tds_mst_iterator_init(
    tds_mst_iterator *iterator,
    const struct tds_merkle_node *root) {
    memset(iterator, 0, sizeof(*iterator));
    if (root != NULL) {
        iterator->frames[0].node = root;
        iterator->depth = 1;
    }
}

static bool tds_mst_iterator_next(
    tds_mst_iterator *iterator,
    const tds_mst_entry **entry) {
    while (iterator->depth != 0) {
        tds_mst_iterator_frame *frame = &iterator->frames[iterator->depth - 1];
        const struct tds_merkle_node *node = frame->node;
        tds_mst_entry *const *entries = tds_mst_node_entries_const(node);
        struct tds_merkle_node *const *children = tds_mst_node_children_const(node);
        if (frame->index < node->entry_count) {
            if (!frame->child_visited) {
                const struct tds_merkle_node *child = children[frame->index];
                frame->child_visited = true;
                if (child != NULL) {
                    if (iterator->depth >= TDS_MST_MAXIMUM_HEIGHT) {
                        return false;
                    }
                    iterator->frames[iterator->depth] =
                        (tds_mst_iterator_frame){child, 0, false};
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
            const struct tds_merkle_node *child = children[node->entry_count];
            frame->child_visited = true;
            if (child != NULL) {
                if (iterator->depth >= TDS_MST_MAXIMUM_HEIGHT) {
                    return false;
                }
                iterator->frames[iterator->depth] =
                    (tds_mst_iterator_frame){child, 0, false};
                ++iterator->depth;
                continue;
            }
        }
        --iterator->depth;
    }
    *entry = NULL;
    return false;
}

tds_merkle_status tds_merkle_search_tree_visit(
    const tds_merkle_search_tree *tree,
    tds_merkle_entry_visitor visitor,
    void *context) {
    tds_mst_iterator iterator;
    const tds_mst_entry *entry;
    if (!tds_mst_tree_valid(tree) || visitor == NULL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    tds_mst_iterator_init(&iterator, tree->root);
    while (tds_mst_iterator_next(&iterator, &entry)) {
        const tds_merkle_status status = visitor(tds_mst_entry_ref(entry), context);
        if (status != TDS_MERKLE_OK) {
            return status;
        }
    }
    return TDS_MERKLE_OK;
}

static tds_merkle_status tds_mst_visit_range_node(
    const struct tds_merkle_policy_rep *policy,
    const struct tds_merkle_node *node,
    const void *minimum_key,
    const void *maximum_key,
    tds_merkle_entry_visitor visitor,
    void *context) {
    tds_mst_entry *const *entries;
    struct tds_merkle_node *const *children;
    size_t index;
    if (node == NULL) {
        return TDS_MERKLE_OK;
    }
    entries = tds_mst_node_entries_const(node);
    children = tds_mst_node_children_const(node);
    for (index = 0; index != node->entry_count; ++index) {
        int low = 0;
        int high = 0;
        tds_merkle_status status = tds_mst_key_compare(
            policy,
            entries[index]->key->value,
            minimum_key,
            &low);
        if (status == TDS_MERKLE_OK) {
            status = tds_mst_key_compare(
                policy,
                entries[index]->key->value,
                maximum_key,
                &high);
        }
        if (status != TDS_MERKLE_OK) {
            return status;
        }
        if (low > 0) {
            status = tds_mst_visit_range_node(
                policy,
                children[index],
                minimum_key,
                maximum_key,
                visitor,
                context);
            if (status != TDS_MERKLE_OK) {
                return status;
            }
        }
        if (low >= 0 && high <= 0) {
            status = visitor(tds_mst_entry_ref(entries[index]), context);
            if (status != TDS_MERKLE_OK) {
                return status;
            }
        }
        if (high > 0) {
            return TDS_MERKLE_OK;
        }
    }
    {
        int comparison = 0;
        tds_merkle_status status = tds_mst_key_compare(
            policy,
            entries[node->entry_count - 1]->key->value,
            maximum_key,
            &comparison);
        if (status != TDS_MERKLE_OK) {
            return status;
        }
        return comparison < 0
            ? tds_mst_visit_range_node(
                policy,
                children[node->entry_count],
                minimum_key,
                maximum_key,
                visitor,
                context)
            : TDS_MERKLE_OK;
    }
}

tds_merkle_status tds_merkle_search_tree_visit_range(
    const tds_merkle_search_tree *tree,
    const void *minimum_key,
    const void *maximum_key,
    tds_merkle_entry_visitor visitor,
    void *context) {
    int comparison = 0;
    tds_merkle_status status;
    if (!tds_mst_tree_valid(tree) || minimum_key == NULL || maximum_key == NULL ||
        visitor == NULL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    status = tds_mst_key_compare(tree->policy, minimum_key, maximum_key, &comparison);
    if (status != TDS_MERKLE_OK) {
        return status;
    }
    if (comparison > 0) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    return tds_mst_visit_range_node(
        tree->policy,
        tree->root,
        minimum_key,
        maximum_key,
        visitor,
        context);
}

static bool tds_mst_typed_compatible(
    const tds_merkle_search_tree *left,
    const tds_merkle_search_tree *right) {
    return tds_mst_tree_valid(left) && tds_mst_tree_valid(right) &&
        tds_merkle_digest_equal(left->policy->domain_digest, right->policy->domain_digest) &&
        left->policy->config.key_type.type_identity ==
            right->policy->config.key_type.type_identity &&
        left->policy->config.value_type.type_identity ==
            right->policy->config.value_type.type_identity;
}

bool tds_merkle_search_tree_content_equals(
    const tds_merkle_search_tree *left,
    const tds_merkle_search_tree *right) {
    return tds_mst_tree_valid(left) && tds_mst_tree_valid(right) &&
        tds_merkle_digest_equal(left->policy->domain_digest, right->policy->domain_digest) &&
        tds_merkle_digest_equal(
            tds_merkle_search_tree_root_hash(left),
            tds_merkle_search_tree_root_hash(right));
}

static tds_merkle_status tds_mst_nodes_equal(
    const struct tds_merkle_policy_rep *policy,
    const struct tds_merkle_node *left,
    const struct tds_merkle_node *right,
    bool *equal) {
    size_t index;
    if (left == right) {
        *equal = true;
        return TDS_MERKLE_OK;
    }
    if (left == NULL || right == NULL || left->level != right->level ||
        left->entry_count != right->entry_count) {
        *equal = false;
        return TDS_MERKLE_OK;
    }
    for (index = 0; index != left->entry_count; ++index) {
        tds_mst_entry *const *left_entries = tds_mst_node_entries_const(left);
        tds_mst_entry *const *right_entries = tds_mst_node_entries_const(right);
        int comparison = 0;
        bool values_equal = false;
        tds_merkle_status status = tds_mst_key_compare(
            policy,
            left_entries[index]->key->value,
            right_entries[index]->key->value,
            &comparison);
        if (status == TDS_MERKLE_OK) {
            status = tds_mst_values_equal(
                policy,
                left_entries[index],
                right_entries[index],
                &values_equal);
        }
        if (status != TDS_MERKLE_OK) {
            return status;
        }
        if (comparison != 0 || !values_equal) {
            *equal = false;
            return TDS_MERKLE_OK;
        }
    }
    for (index = 0; index != left->entry_count + 1; ++index) {
        bool children_equal = false;
        const tds_merkle_status status = tds_mst_nodes_equal(
            policy,
            tds_mst_node_children_const(left)[index],
            tds_mst_node_children_const(right)[index],
            &children_equal);
        if (status != TDS_MERKLE_OK) {
            return status;
        }
        if (!children_equal) {
            *equal = false;
            return TDS_MERKLE_OK;
        }
    }
    *equal = true;
    return TDS_MERKLE_OK;
}

static tds_merkle_status tds_mst_enumerated_equal(
    const tds_merkle_search_tree *left,
    const tds_merkle_search_tree *right,
    bool *equal) {
    tds_mst_iterator left_iterator;
    tds_mst_iterator right_iterator;
    const tds_mst_entry *left_entry = NULL;
    const tds_mst_entry *right_entry = NULL;
    bool has_left;
    bool has_right;
    tds_mst_iterator_init(&left_iterator, left->root);
    tds_mst_iterator_init(&right_iterator, right->root);
    has_left = tds_mst_iterator_next(&left_iterator, &left_entry);
    has_right = tds_mst_iterator_next(&right_iterator, &right_entry);
    while (has_left && has_right) {
        int comparison = 0;
        bool values_equal = false;
        tds_merkle_status status = tds_mst_key_compare(
            left->policy,
            left_entry->key->value,
            right_entry->key->value,
            &comparison);
        if (status == TDS_MERKLE_OK) {
            status = tds_mst_values_equal(
                left->policy,
                left_entry,
                right_entry,
                &values_equal);
        }
        if (status != TDS_MERKLE_OK) {
            return status;
        }
        if (comparison != 0 || !values_equal) {
            *equal = false;
            return TDS_MERKLE_OK;
        }
        has_left = tds_mst_iterator_next(&left_iterator, &left_entry);
        has_right = tds_mst_iterator_next(&right_iterator, &right_entry);
    }
    *equal = has_left == has_right;
    return TDS_MERKLE_OK;
}

tds_merkle_status tds_merkle_search_tree_map_equals(
    const tds_merkle_search_tree *left,
    const tds_merkle_search_tree *right,
    bool *equal) {
    if (!tds_mst_tree_valid(left) || !tds_mst_tree_valid(right) || equal == NULL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    if (!tds_mst_typed_compatible(left, right)) {
        return TDS_MERKLE_INCOMPATIBLE_POLICY;
    }
    if (left->root == right->root) {
        *equal = true;
        return TDS_MERKLE_OK;
    }
    if (tds_merkle_search_tree_size(left) != tds_merkle_search_tree_size(right)) {
        *equal = false;
        return TDS_MERKLE_OK;
    }
    if (tds_merkle_digest_equal(
            tds_merkle_search_tree_root_hash(left),
            tds_merkle_search_tree_root_hash(right))) {
        return tds_mst_nodes_equal(left->policy, left->root, right->root, equal);
    }
    return tds_mst_enumerated_equal(left, right, equal);
}

static tds_merkle_status tds_mst_emit_difference_subtree(
    const struct tds_merkle_node *node,
    tds_merkle_difference_kind kind,
    tds_merkle_difference_visitor visitor,
    void *context) {
    tds_mst_iterator iterator;
    const tds_mst_entry *entry;
    tds_mst_iterator_init(&iterator, node);
    while (tds_mst_iterator_next(&iterator, &entry)) {
        tds_merkle_difference_ref difference;
        difference.kind = kind;
        difference.key = entry->key->value;
        difference.before = kind == TDS_MERKLE_DIFFERENCE_REMOVED
            ? entry->value->value
            : NULL;
        difference.after = kind == TDS_MERKLE_DIFFERENCE_ADDED
            ? entry->value->value
            : NULL;
        {
            const tds_merkle_status status = visitor(difference, context);
            if (status != TDS_MERKLE_OK) {
                return status;
            }
        }
    }
    return TDS_MERKLE_OK;
}

static tds_merkle_status tds_mst_same_separators(
    const struct tds_merkle_policy_rep *policy,
    const struct tds_merkle_node *left,
    const struct tds_merkle_node *right,
    bool *same) {
    size_t index;
    if (left->level != right->level || left->entry_count != right->entry_count) {
        *same = false;
        return TDS_MERKLE_OK;
    }
    for (index = 0; index != left->entry_count; ++index) {
        int comparison = 0;
        const tds_merkle_status status = tds_mst_key_compare(
            policy,
            tds_mst_node_entries_const(left)[index]->key->value,
            tds_mst_node_entries_const(right)[index]->key->value,
            &comparison);
        if (status != TDS_MERKLE_OK) {
            return status;
        }
        if (comparison != 0) {
            *same = false;
            return TDS_MERKLE_OK;
        }
    }
    *same = true;
    return TDS_MERKLE_OK;
}

static tds_merkle_status tds_mst_merge_diff(
    const struct tds_merkle_policy_rep *policy,
    const struct tds_merkle_node *left,
    const struct tds_merkle_node *right,
    tds_merkle_difference_visitor visitor,
    void *context) {
    tds_mst_iterator left_iterator;
    tds_mst_iterator right_iterator;
    const tds_mst_entry *old_entry = NULL;
    const tds_mst_entry *new_entry = NULL;
    bool has_old;
    bool has_new;
    tds_mst_iterator_init(&left_iterator, left);
    tds_mst_iterator_init(&right_iterator, right);
    has_old = tds_mst_iterator_next(&left_iterator, &old_entry);
    has_new = tds_mst_iterator_next(&right_iterator, &new_entry);
    while (has_old || has_new) {
        int comparison;
        tds_merkle_status status;
        if (!has_old) {
            comparison = 1;
        } else if (!has_new) {
            comparison = -1;
        } else {
            comparison = 0;
            status = tds_mst_key_compare(
                policy,
                old_entry->key->value,
                new_entry->key->value,
                &comparison);
            if (status != TDS_MERKLE_OK) {
                return status;
            }
        }
        if (comparison < 0) {
            const tds_merkle_difference_ref difference = {
                TDS_MERKLE_DIFFERENCE_REMOVED,
                old_entry->key->value,
                old_entry->value->value,
                NULL};
            status = visitor(difference, context);
            if (status != TDS_MERKLE_OK) {
                return status;
            }
            has_old = tds_mst_iterator_next(&left_iterator, &old_entry);
        } else if (comparison > 0) {
            const tds_merkle_difference_ref difference = {
                TDS_MERKLE_DIFFERENCE_ADDED,
                new_entry->key->value,
                NULL,
                new_entry->value->value};
            status = visitor(difference, context);
            if (status != TDS_MERKLE_OK) {
                return status;
            }
            has_new = tds_mst_iterator_next(&right_iterator, &new_entry);
        } else {
            bool values_equal = false;
            status = tds_mst_values_equal(policy, old_entry, new_entry, &values_equal);
            if (status != TDS_MERKLE_OK) {
                return status;
            }
            if (!values_equal) {
                const tds_merkle_difference_ref difference = {
                    TDS_MERKLE_DIFFERENCE_CHANGED,
                    old_entry->key->value,
                    old_entry->value->value,
                    new_entry->value->value};
                status = visitor(difference, context);
                if (status != TDS_MERKLE_OK) {
                    return status;
                }
            }
            has_old = tds_mst_iterator_next(&left_iterator, &old_entry);
            has_new = tds_mst_iterator_next(&right_iterator, &new_entry);
        }
    }
    return TDS_MERKLE_OK;
}

static tds_merkle_status tds_mst_diff_nodes(
    const struct tds_merkle_policy_rep *policy,
    const struct tds_merkle_node *left,
    const struct tds_merkle_node *right,
    tds_merkle_difference_visitor visitor,
    void *context) {
    bool same = false;
    size_t index;
    tds_merkle_status status;
    if (left == right ||
        (left != NULL && right != NULL && tds_merkle_digest_equal(left->digest, right->digest))) {
        return TDS_MERKLE_OK;
    }
    if (left == NULL) {
        return tds_mst_emit_difference_subtree(
            right,
            TDS_MERKLE_DIFFERENCE_ADDED,
            visitor,
            context);
    }
    if (right == NULL) {
        return tds_mst_emit_difference_subtree(
            left,
            TDS_MERKLE_DIFFERENCE_REMOVED,
            visitor,
            context);
    }
    status = tds_mst_same_separators(policy, left, right, &same);
    if (status != TDS_MERKLE_OK) {
        return status;
    }
    if (!same) {
        return tds_mst_merge_diff(policy, left, right, visitor, context);
    }
    for (index = 0; index != left->entry_count; ++index) {
        tds_mst_entry *const *left_entries = tds_mst_node_entries_const(left);
        tds_mst_entry *const *right_entries = tds_mst_node_entries_const(right);
        bool values_equal = false;
        status = tds_mst_diff_nodes(
            policy,
            tds_mst_node_children_const(left)[index],
            tds_mst_node_children_const(right)[index],
            visitor,
            context);
        if (status == TDS_MERKLE_OK) {
            status = tds_mst_values_equal(
                policy,
                left_entries[index],
                right_entries[index],
                &values_equal);
        }
        if (status != TDS_MERKLE_OK) {
            return status;
        }
        if (!values_equal) {
            const tds_merkle_difference_ref difference = {
                TDS_MERKLE_DIFFERENCE_CHANGED,
                left_entries[index]->key->value,
                left_entries[index]->value->value,
                right_entries[index]->value->value};
            status = visitor(difference, context);
            if (status != TDS_MERKLE_OK) {
                return status;
            }
        }
    }
    return tds_mst_diff_nodes(
        policy,
        tds_mst_node_children_const(left)[left->entry_count],
        tds_mst_node_children_const(right)[right->entry_count],
        visitor,
        context);
}

tds_merkle_status tds_merkle_search_tree_diff(
    const tds_merkle_search_tree *left,
    const tds_merkle_search_tree *right,
    tds_merkle_difference_visitor visitor,
    void *context) {
    if (!tds_mst_tree_valid(left) || !tds_mst_tree_valid(right) || visitor == NULL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    if (!tds_mst_typed_compatible(left, right)) {
        return TDS_MERKLE_INCOMPATIBLE_POLICY;
    }
    return tds_mst_diff_nodes(left->policy, left->root, right->root, visitor, context);
}

const void *tds_merkle_search_tree_root_identity(
    const tds_merkle_search_tree *tree) {
    return tds_mst_tree_valid(tree) ? tree->root : NULL;
}

tds_merkle_status tds_merkle_search_tree_node_identity(
    const tds_merkle_search_tree *tree,
    const void *key,
    const void **identity) {
    struct tds_merkle_node *node = NULL;
    size_t entry_index = 0;
    tds_merkle_status status;
    if (!tds_mst_tree_valid(tree) || key == NULL || identity == NULL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    status = tds_mst_find_node(tree, key, &node, &entry_index);
    (void)entry_index;
    if (status == TDS_MERKLE_OK) {
        *identity = node;
    }
    return status;
}

static size_t tds_mst_pointer_hash(const void *pointer) {
    size_t value = (size_t)(uintptr_t)pointer;
    value ^= value >> 17;
    value *= (size_t)0xed5ad4bbU;
    value ^= value >> 11;
    return value;
}

static void tds_mst_pointer_set_add(
    const struct tds_merkle_node **table,
    size_t mask,
    const struct tds_merkle_node *node) {
    size_t index = tds_mst_pointer_hash(node) & mask;
    while (table[index] != NULL && table[index] != node) {
        index = (index + 1) & mask;
    }
    table[index] = node;
}

static bool tds_mst_pointer_set_contains(
    const struct tds_merkle_node *const *table,
    size_t mask,
    const struct tds_merkle_node *node) {
    size_t index = tds_mst_pointer_hash(node) & mask;
    while (table[index] != NULL) {
        if (table[index] == node) {
            return true;
        }
        index = (index + 1) & mask;
    }
    return false;
}

static void tds_mst_collect_node_pointers(
    const struct tds_merkle_node *node,
    const struct tds_merkle_node **table,
    size_t mask) {
    size_t index;
    if (node == NULL) {
        return;
    }
    tds_mst_pointer_set_add(table, mask, node);
    for (index = 0; index != node->entry_count + 1; ++index) {
        tds_mst_collect_node_pointers(
            tds_mst_node_children_const(node)[index],
            table,
            mask);
    }
}

static size_t tds_mst_count_node_pointers(
    const struct tds_merkle_node *node,
    const struct tds_merkle_node *const *table,
    size_t mask) {
    size_t count = 0;
    size_t index;
    if (node == NULL) {
        return 0;
    }
    if (tds_mst_pointer_set_contains(table, mask, node)) {
        ++count;
    }
    for (index = 0; index != node->entry_count + 1; ++index) {
        count += tds_mst_count_node_pointers(
            tds_mst_node_children_const(node)[index],
            table,
            mask);
    }
    return count;
}

tds_merkle_status tds_merkle_search_tree_shared_node_count(
    const tds_merkle_search_tree *left,
    const tds_merkle_search_tree *right,
    size_t *shared_count) {
    const struct tds_merkle_node **table = NULL;
    size_t capacity = 1;
    size_t minimum_capacity;
    size_t byte_count;
    if (!tds_mst_tree_valid(left) || !tds_mst_tree_valid(right) || shared_count == NULL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    if (left->policy != right->policy) {
        return TDS_MERKLE_INCOMPATIBLE_POLICY;
    }
    if (left->root == NULL || right->root == NULL) {
        *shared_count = 0;
        return TDS_MERKLE_OK;
    }
    if (tds_mst_multiply_overflows(left->root->block_count, 2, &minimum_capacity)) {
        return TDS_MERKLE_OVERFLOW;
    }
    while (capacity < minimum_capacity) {
        if (capacity > SIZE_MAX / 2) {
            return TDS_MERKLE_OVERFLOW;
        }
        capacity *= 2;
    }
    if (tds_mst_multiply_overflows(capacity, sizeof(*table), &byte_count)) {
        return TDS_MERKLE_OVERFLOW;
    }
    table = (const struct tds_merkle_node **)tds_mst_allocate(left->policy, byte_count);
    if (table == NULL) {
        return TDS_MERKLE_NO_MEMORY;
    }
    memset(table, 0, byte_count);
    tds_mst_collect_node_pointers(left->root, table, capacity - 1);
    *shared_count = tds_mst_count_node_pointers(right->root, table, capacity - 1);
    tds_mst_deallocate(left->policy, table);
    return TDS_MERKLE_OK;
}

static tds_merkle_status tds_mst_visit_shape_node(
    const struct tds_merkle_node *node,
    tds_merkle_shape_visitor visitor,
    void *context) {
    size_t index;
    if (node == NULL) {
        return TDS_MERKLE_OK;
    }
    for (index = 0; index != node->entry_count; ++index) {
        const tds_merkle_shape_ref shape = {
            node,
            node->level,
            tds_mst_entry_ref(tds_mst_node_entries_const(node)[index]),
            node->entry_count,
            node->count};
        const tds_merkle_status status = visitor(shape, context);
        if (status != TDS_MERKLE_OK) {
            return status;
        }
    }
    for (index = 0; index != node->entry_count + 1; ++index) {
        const tds_merkle_status status = tds_mst_visit_shape_node(
            tds_mst_node_children_const(node)[index],
            visitor,
            context);
        if (status != TDS_MERKLE_OK) {
            return status;
        }
    }
    return TDS_MERKLE_OK;
}

tds_merkle_status tds_merkle_search_tree_visit_shape(
    const tds_merkle_search_tree *tree,
    tds_merkle_shape_visitor visitor,
    void *context) {
    if (!tds_mst_tree_valid(tree) || visitor == NULL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    return tds_mst_visit_shape_node(tree->root, visitor, context);
}

static tds_merkle_status tds_mst_visit_blocks_node(
    const struct tds_merkle_node *node,
    tds_merkle_block_visitor visitor,
    void *context) {
    size_t index;
    if (node == NULL) {
        return TDS_MERKLE_OK;
    }
    {
        const tds_merkle_block_ref block = {
            node->digest,
            node->block_bytes->data,
            node->block_bytes->size};
        const tds_merkle_status status = visitor(block, context);
        if (status != TDS_MERKLE_OK) {
            return status;
        }
    }
    for (index = 0; index != node->entry_count + 1; ++index) {
        const tds_merkle_status status = tds_mst_visit_blocks_node(
            tds_mst_node_children_const(node)[index],
            visitor,
            context);
        if (status != TDS_MERKLE_OK) {
            return status;
        }
    }
    return TDS_MERKLE_OK;
}

tds_merkle_status tds_merkle_search_tree_visit_blocks(
    const tds_merkle_search_tree *tree,
    tds_merkle_block_visitor visitor,
    void *context) {
    if (!tds_mst_tree_valid(tree) || visitor == NULL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    return tds_mst_visit_blocks_node(tree->root, visitor, context);
}

static tds_merkle_status tds_mst_validate_object_encoding(
    const struct tds_merkle_policy_rep *policy,
    const tds_merkle_codec *codec,
    const tds_mst_object *object,
    const tds_mst_bytes *stored,
    bool expected_key,
    bool *valid) {
    tds_mst_bytes *encoded = NULL;
    tds_merkle_status status;
    if (object == NULL || stored == NULL || object->value == NULL ||
        object->is_key != expected_key) {
        *valid = false;
        return TDS_MERKLE_OK;
    }
    status = tds_mst_encode_value(policy, codec, object->value, &encoded);
    if (status != TDS_MERKLE_OK) {
        return status;
    }
    *valid = encoded->size == stored->size &&
        (encoded->size == 0 || memcmp(encoded->data, stored->data, encoded->size) == 0);
    tds_mst_bytes_release(policy, encoded);
    return TDS_MERKLE_OK;
}

static tds_merkle_status tds_mst_validate_bound(
    const struct tds_merkle_policy_rep *policy,
    const tds_mst_entry *entry,
    const tds_mst_entry *bound,
    bool lower_bound,
    bool *valid) {
    int comparison = 0;
    tds_merkle_status status;
    if (bound == NULL) {
        return TDS_MERKLE_OK;
    }
    status = tds_mst_key_compare(
        policy,
        entry->key->value,
        bound->key->value,
        &comparison);
    if (status == TDS_MERKLE_OK &&
        ((lower_bound && comparison <= 0) || (!lower_bound && comparison >= 0))) {
        *valid = false;
    }
    return status;
}

static tds_merkle_status tds_mst_validate_node(
    const struct tds_merkle_policy_rep *policy,
    const struct tds_merkle_node *node,
    const tds_mst_entry *lower_bound,
    const tds_mst_entry *upper_bound,
    tds_mst_validation_accumulator *accumulator,
    bool *valid) {
    tds_mst_entry *const *entries;
    struct tds_merkle_node *const *children;
    size_t count;
    size_t height = 1;
    size_t block_count = 1;
    size_t index;
    tds_mst_bytes *encoded_block = NULL;
    tds_merkle_digest encoded_digest = {{0}};
    tds_merkle_status status;
    if (node == NULL) {
        return TDS_MERKLE_OK;
    }
    if (node->entry_count == 0 || node->level > TDS_MST_MAXIMUM_LEVEL ||
        node->block_bytes == NULL || node->minimum_entry == NULL ||
        node->maximum_entry == NULL) {
        *valid = false;
        return TDS_MERKLE_OK;
    }
    entries = tds_mst_node_entries_const(node);
    children = tds_mst_node_children_const(node);
    count = node->entry_count;
    for (index = 0; index != node->entry_count; ++index) {
        tds_merkle_digest key_digest = {{0}};
        bool encoding_valid = true;
        if (entries[index] == NULL || entries[index]->key == NULL ||
            entries[index]->value == NULL || entries[index]->key_bytes == NULL ||
            entries[index]->value_bytes == NULL || entries[index]->level != node->level) {
            *valid = false;
            return TDS_MERKLE_OK;
        }
        status = tds_mst_validate_object_encoding(
            policy,
            &policy->config.key_codec,
            entries[index]->key,
            entries[index]->key_bytes,
            true,
            &encoding_valid);
        if (status == TDS_MERKLE_OK && encoding_valid) {
            status = tds_mst_validate_object_encoding(
                policy,
                &policy->config.value_codec,
                entries[index]->value,
                entries[index]->value_bytes,
                false,
                &encoding_valid);
        }
        if (status != TDS_MERKLE_OK) {
            return status;
        }
        if (!encoding_valid) {
            *valid = false;
            return TDS_MERKLE_OK;
        }
        status = tds_mst_hash_key_bytes(policy, entries[index]->key_bytes, &key_digest);
        if (status != TDS_MERKLE_OK) {
            return status;
        }
        if (tds_mst_level(key_digest) != entries[index]->level) {
            *valid = false;
            return TDS_MERKLE_OK;
        }
        if (index != 0) {
            int comparison = 0;
            status = tds_mst_key_compare(
                policy,
                entries[index - 1]->key->value,
                entries[index]->key->value,
                &comparison);
            if (status != TDS_MERKLE_OK) {
                return status;
            }
            if (comparison >= 0) {
                *valid = false;
                return TDS_MERKLE_OK;
            }
        }
        status = tds_mst_validate_bound(policy, entries[index], lower_bound, true, valid);
        if (status == TDS_MERKLE_OK && *valid) {
            status = tds_mst_validate_bound(policy, entries[index], upper_bound, false, valid);
        }
        if (status != TDS_MERKLE_OK || !*valid) {
            return status;
        }
    }
    for (index = 0; index != node->entry_count + 1; ++index) {
        const struct tds_merkle_node *child = children[index];
        if (child != NULL) {
            size_t candidate_height;
            if (child->level >= node->level) {
                *valid = false;
                return TDS_MERKLE_OK;
            }
            status = tds_mst_validate_node(
                policy,
                child,
                index == 0 ? lower_bound : entries[index - 1],
                index == node->entry_count ? upper_bound : entries[index],
                accumulator,
                valid);
            if (status != TDS_MERKLE_OK || !*valid) {
                return status;
            }
            if (tds_mst_add_overflows(count, child->count, &count) ||
                tds_mst_add_overflows(block_count, child->block_count, &block_count) ||
                tds_mst_add_overflows(child->height, 1, &candidate_height)) {
                *valid = false;
                return TDS_MERKLE_OK;
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
        return TDS_MERKLE_OK;
    }
    status = tds_mst_encode_block(
        policy,
        node->level,
        node->count,
        entries,
        node->entry_count,
        children,
        &encoded_block,
        &encoded_digest);
    if (status != TDS_MERKLE_OK) {
        return status;
    }
    if (encoded_block->size != node->block_bytes->size ||
        (encoded_block->size != 0 &&
            memcmp(encoded_block->data, node->block_bytes->data, encoded_block->size) != 0) ||
        !tds_merkle_digest_equal(encoded_digest, node->digest)) {
        *valid = false;
        tds_mst_bytes_release(policy, encoded_block);
        return TDS_MERKLE_OK;
    }
    tds_mst_bytes_release(policy, encoded_block);
    if (tds_mst_add_overflows(accumulator->count, node->entry_count, &accumulator->count) ||
        tds_mst_add_overflows(accumulator->block_count, 1, &accumulator->block_count)) {
        *valid = false;
        return TDS_MERKLE_OK;
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
    return TDS_MERKLE_OK;
}

tds_merkle_status tds_merkle_search_tree_validate(
    const tds_merkle_search_tree *tree,
    bool *valid,
    tds_merkle_search_tree_statistics *statistics) {
    tds_mst_validation_accumulator accumulator = {0, 0, SIZE_MAX, 0, SIZE_MAX, 0};
    tds_merkle_search_tree_statistics result = {0, 0, 0, 0, 0, 0, 0};
    bool structurally_valid = true;
    tds_merkle_status status;
    if (!tds_mst_tree_valid(tree) || valid == NULL || statistics == NULL) {
        return TDS_MERKLE_INVALID_ARGUMENT;
    }
    status = tds_mst_validate_node(
        tree->policy,
        tree->root,
        NULL,
        NULL,
        &accumulator,
        &structurally_valid);
    if (status != TDS_MERKLE_OK) {
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
    return TDS_MERKLE_OK;
}
