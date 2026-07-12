#ifndef TOOLS_DATA_STRUCTURES_HAMT_MERKLE_SEARCH_TREE_H
#define TOOLS_DATA_STRUCTURES_HAMT_MERKLE_SEARCH_TREE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TDS_MERKLE_DIGEST_BYTE_LENGTH ((size_t)32)
#define TDS_MERKLE_DIGEST_HEX_LENGTH ((size_t)64)

typedef enum tds_merkle_status {
    TDS_MERKLE_OK = 0,
    TDS_MERKLE_NO_MEMORY = 1,
    TDS_MERKLE_INVALID_ARGUMENT = 2,
    TDS_MERKLE_OVERFLOW = 3,
    TDS_MERKLE_CRYPTO_FAILURE = 4,
    TDS_MERKLE_INVALID_ENCODING = 5,
    TDS_MERKLE_INCOMPATIBLE_POLICY = 6,
    TDS_MERKLE_INCONSISTENT_POLICY = 7,
    TDS_MERKLE_CALLBACK_FAILURE = 8
} tds_merkle_status;

typedef struct tds_merkle_digest {
    unsigned char bytes[TDS_MERKLE_DIGEST_BYTE_LENGTH];
} tds_merkle_digest;

bool tds_merkle_digest_equal(tds_merkle_digest left, tds_merkle_digest right);
int tds_merkle_digest_compare(tds_merkle_digest left, tds_merkle_digest right);
tds_merkle_status tds_merkle_digest_parse(
    const unsigned char *bytes,
    size_t byte_count,
    tds_merkle_digest *digest);
tds_merkle_status tds_merkle_digest_parse_hex(
    const char *hex,
    size_t character_count,
    tds_merkle_digest *digest);
tds_merkle_status tds_merkle_digest_write(
    tds_merkle_digest digest,
    unsigned char *destination,
    size_t destination_size);
tds_merkle_status tds_merkle_digest_write_hex(
    tds_merkle_digest digest,
    char *destination,
    size_t destination_size);

typedef void *(*tds_merkle_allocate_fn)(size_t size, void *context);
typedef void (*tds_merkle_deallocate_fn)(void *allocation, void *context);

typedef struct tds_merkle_allocator {
    tds_merkle_allocate_fn allocate;
    tds_merkle_deallocate_fn deallocate;
    void *context;
} tds_merkle_allocator;

typedef tds_merkle_status (*tds_merkle_copy_fn)(
    void *destination,
    const void *source,
    const tds_merkle_allocator *allocator,
    void *context);
typedef void (*tds_merkle_destroy_fn)(
    void *value,
    const tds_merkle_allocator *allocator,
    void *context);
typedef tds_merkle_status (*tds_merkle_compare_fn)(
    const void *left,
    const void *right,
    int *comparison,
    void *context);
typedef tds_merkle_status (*tds_merkle_equal_fn)(
    const void *left,
    const void *right,
    bool *equal,
    void *context);

/* Encode is a deterministic two-pass callback. With destination == NULL and
 * destination_size == 0 it writes the exact required length to bytes_written.
 * The second call receives a destination of exactly that size and must report
 * exactly the same length. The library owns and eventually deallocates that
 * destination; the callback never returns byte ownership. */
typedef tds_merkle_status (*tds_merkle_encode_fn)(
    const void *value,
    unsigned char *destination,
    size_t destination_size,
    size_t *bytes_written,
    const tds_merkle_allocator *allocator,
    void *context);

/* Decode consumes one complete canonical encoding and constructs an owned
 * value in uninitialized destination storage. Failure must leave destination
 * ownership-free. Untrusted wire ingestion re-encodes the value and requires
 * byte-for-byte equality before publication. */
typedef tds_merkle_status (*tds_merkle_decode_fn)(
    const unsigned char *encoding,
    size_t encoding_size,
    void *destination,
    const tds_merkle_allocator *allocator,
    void *context);

typedef struct tds_merkle_identifier {
    const unsigned char *bytes;
    size_t size;
} tds_merkle_identifier;

typedef struct tds_merkle_type_policy {
    size_t size;
    const void *type_identity;
    tds_merkle_copy_fn copy;
    tds_merkle_destroy_fn destroy;
    tds_merkle_equal_fn equals;
    void *context;
} tds_merkle_type_policy;

typedef struct tds_merkle_codec {
    tds_merkle_identifier encoding_id;
    tds_merkle_encode_fn encode;
    tds_merkle_decode_fn decode;
    void *context;
} tds_merkle_codec;

typedef struct tds_merkle_policy_config {
    tds_merkle_identifier policy_id;
    tds_merkle_type_policy key_type;
    tds_merkle_type_policy value_type;
    tds_merkle_compare_fn key_compare;
    void *key_compare_context;
    tds_merkle_codec key_codec;
    tds_merkle_codec value_codec;
    tds_merkle_allocator allocator;
} tds_merkle_policy_config;

void tds_merkle_type_policy_init(
    tds_merkle_type_policy *type,
    size_t size,
    const void *type_identity);
void tds_merkle_codec_init(
    tds_merkle_codec *codec,
    const unsigned char *encoding_id,
    size_t encoding_id_size,
    tds_merkle_encode_fn encode,
    tds_merkle_decode_fn decode);
void tds_merkle_policy_config_init(tds_merkle_policy_config *config);

typedef struct tds_merkle_nullable_utf8 {
    bool has_value;
    const char *data;
    size_t size;
} tds_merkle_nullable_utf8;

typedef struct tds_merkle_nullable_bytes {
    bool has_value;
    const unsigned char *data;
    size_t size;
} tds_merkle_nullable_bytes;

typedef struct tds_merkle_guid {
    unsigned char bytes[16];
} tds_merkle_guid;

void tds_merkle_i32_type_policy_init(
    tds_merkle_type_policy *type,
    const void *type_identity);
void tds_merkle_i64_type_policy_init(
    tds_merkle_type_policy *type,
    const void *type_identity);
void tds_merkle_nullable_utf8_type_policy_init(
    tds_merkle_type_policy *type,
    const void *type_identity);
void tds_merkle_nullable_bytes_type_policy_init(
    tds_merkle_type_policy *type,
    const void *type_identity);
void tds_merkle_guid_type_policy_init(
    tds_merkle_type_policy *type,
    const void *type_identity);

void tds_merkle_i32_codec_init(tds_merkle_codec *codec);
void tds_merkle_i64_codec_init(tds_merkle_codec *codec);
void tds_merkle_nullable_utf8_codec_init(tds_merkle_codec *codec);
void tds_merkle_nullable_bytes_codec_init(tds_merkle_codec *codec);
void tds_merkle_guid_codec_init(tds_merkle_codec *codec);

struct tds_merkle_policy_rep;

typedef struct tds_merkle_policy {
    struct tds_merkle_policy_rep *rep;
} tds_merkle_policy;

tds_merkle_status tds_merkle_policy_create(
    const tds_merkle_policy_config *config,
    tds_merkle_policy *policy);
tds_merkle_status tds_merkle_policy_copy(
    const tds_merkle_policy *source,
    tds_merkle_policy *destination);
void tds_merkle_policy_move(
    tds_merkle_policy *destination,
    tds_merkle_policy *source);
void tds_merkle_policy_dispose(tds_merkle_policy *policy);
bool tds_merkle_policy_same_identity(
    const tds_merkle_policy *left,
    const tds_merkle_policy *right);
bool tds_merkle_policy_same_domain(
    const tds_merkle_policy *left,
    const tds_merkle_policy *right);
const char *tds_merkle_algorithm_id(void);
tds_merkle_digest tds_merkle_policy_domain_digest(const tds_merkle_policy *policy);
tds_merkle_digest tds_merkle_policy_empty_digest(const tds_merkle_policy *policy);

typedef struct tds_merkle_search_input {
    const void *key;
    const void *value;
} tds_merkle_search_input;

typedef struct tds_merkle_search_entry_ref {
    const void *key;
    const void *value;
    const unsigned char *key_bytes;
    size_t key_byte_count;
    const unsigned char *value_bytes;
    size_t value_byte_count;
    unsigned level;
} tds_merkle_search_entry_ref;

typedef enum tds_merkle_difference_kind {
    TDS_MERKLE_DIFFERENCE_ADDED = 0,
    TDS_MERKLE_DIFFERENCE_REMOVED = 1,
    TDS_MERKLE_DIFFERENCE_CHANGED = 2
} tds_merkle_difference_kind;

typedef struct tds_merkle_difference_ref {
    tds_merkle_difference_kind kind;
    const void *key;
    const void *before;
    const void *after;
} tds_merkle_difference_ref;

typedef struct tds_merkle_shape_ref {
    const void *node_identity;
    unsigned level;
    tds_merkle_search_entry_ref entry;
    size_t entries_in_block;
    size_t subtree_count;
} tds_merkle_shape_ref;

typedef struct tds_merkle_block_ref {
    tds_merkle_digest digest;
    const unsigned char *bytes;
    size_t byte_count;
} tds_merkle_block_ref;

typedef struct tds_merkle_search_tree_statistics {
    size_t count;
    size_t block_count;
    size_t height;
    size_t minimum_entries_per_block;
    size_t maximum_entries_per_block;
    size_t minimum_block_bytes;
    size_t maximum_block_bytes;
} tds_merkle_search_tree_statistics;

typedef tds_merkle_status (*tds_merkle_entry_visitor)(
    tds_merkle_search_entry_ref entry,
    void *context);
typedef tds_merkle_status (*tds_merkle_difference_visitor)(
    tds_merkle_difference_ref difference,
    void *context);
typedef tds_merkle_status (*tds_merkle_shape_visitor)(
    tds_merkle_shape_ref shape,
    void *context);
typedef tds_merkle_status (*tds_merkle_block_visitor)(
    tds_merkle_block_ref block,
    void *context);

struct tds_merkle_node;

typedef struct tds_merkle_search_tree {
    struct tds_merkle_policy_rep *policy;
    struct tds_merkle_node *root;
} tds_merkle_search_tree;

tds_merkle_status tds_merkle_search_tree_init(
    tds_merkle_search_tree *tree,
    const tds_merkle_policy *policy);
tds_merkle_status tds_merkle_search_tree_from_array(
    tds_merkle_search_tree *tree,
    const tds_merkle_policy *policy,
    const tds_merkle_search_input *entries,
    size_t entry_count);
tds_merkle_status tds_merkle_search_tree_copy(
    const tds_merkle_search_tree *source,
    tds_merkle_search_tree *destination);
void tds_merkle_search_tree_move(
    tds_merkle_search_tree *destination,
    tds_merkle_search_tree *source);
void tds_merkle_search_tree_dispose(tds_merkle_search_tree *tree);

bool tds_merkle_search_tree_empty(const tds_merkle_search_tree *tree);
size_t tds_merkle_search_tree_size(const tds_merkle_search_tree *tree);
size_t tds_merkle_search_tree_height(const tds_merkle_search_tree *tree);
size_t tds_merkle_search_tree_block_count(const tds_merkle_search_tree *tree);
tds_merkle_digest tds_merkle_search_tree_root_hash(const tds_merkle_search_tree *tree);

tds_merkle_status tds_merkle_search_tree_contains_key(
    const tds_merkle_search_tree *tree,
    const void *key,
    bool *found);
tds_merkle_status tds_merkle_search_tree_try_get_entry_ref(
    const tds_merkle_search_tree *tree,
    const void *key,
    bool *found,
    tds_merkle_search_entry_ref *entry);

/* Producing operations support exact source/result aliasing and publish only
 * after every allocation, callback, encoding, hash, and node construction has
 * succeeded. A distinct result must be uninitialized or disposed. */
tds_merkle_status tds_merkle_search_tree_set(
    const tds_merkle_search_tree *tree,
    const void *key,
    const void *value,
    tds_merkle_search_tree *result);
tds_merkle_status tds_merkle_search_tree_remove(
    const tds_merkle_search_tree *tree,
    const void *key,
    tds_merkle_search_tree *result);
tds_merkle_status tds_merkle_search_tree_clear(
    const tds_merkle_search_tree *tree,
    tds_merkle_search_tree *result);

/* Visitors borrow exact stored representatives. Calls are streaming: failure
 * stops traversal, but side effects of earlier successful visits remain. */
tds_merkle_status tds_merkle_search_tree_visit(
    const tds_merkle_search_tree *tree,
    tds_merkle_entry_visitor visitor,
    void *context);
tds_merkle_status tds_merkle_search_tree_visit_range(
    const tds_merkle_search_tree *tree,
    const void *minimum_key,
    const void *maximum_key,
    tds_merkle_entry_visitor visitor,
    void *context);

bool tds_merkle_search_tree_content_equals(
    const tds_merkle_search_tree *left,
    const tds_merkle_search_tree *right);
tds_merkle_status tds_merkle_search_tree_map_equals(
    const tds_merkle_search_tree *left,
    const tds_merkle_search_tree *right,
    bool *equal);
tds_merkle_status tds_merkle_search_tree_diff(
    const tds_merkle_search_tree *left,
    const tds_merkle_search_tree *right,
    tds_merkle_difference_visitor visitor,
    void *context);

const void *tds_merkle_search_tree_root_identity(
    const tds_merkle_search_tree *tree);
tds_merkle_status tds_merkle_search_tree_node_identity(
    const tds_merkle_search_tree *tree,
    const void *key,
    const void **identity);
tds_merkle_status tds_merkle_search_tree_shared_node_count(
    const tds_merkle_search_tree *left,
    const tds_merkle_search_tree *right,
    size_t *shared_count);
tds_merkle_status tds_merkle_search_tree_visit_shape(
    const tds_merkle_search_tree *tree,
    tds_merkle_shape_visitor visitor,
    void *context);
tds_merkle_status tds_merkle_search_tree_visit_blocks(
    const tds_merkle_search_tree *tree,
    tds_merkle_block_visitor visitor,
    void *context);
tds_merkle_status tds_merkle_search_tree_validate(
    const tds_merkle_search_tree *tree,
    bool *valid,
    tds_merkle_search_tree_statistics *statistics);

#ifdef __cplusplus
}
#endif

#endif
