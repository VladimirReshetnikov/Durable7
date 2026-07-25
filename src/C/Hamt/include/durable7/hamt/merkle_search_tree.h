#ifndef DURABLE7_HAMT_MERKLE_SEARCH_TREE_H
#define DURABLE7_HAMT_MERKLE_SEARCH_TREE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define D7_MERKLE_DIGEST_BYTE_LENGTH ((size_t)32)
#define D7_MERKLE_DIGEST_HEX_LENGTH ((size_t)64)

typedef enum d7_merkle_status {
    D7_MERKLE_OK = 0,
    D7_MERKLE_NO_MEMORY = 1,
    D7_MERKLE_INVALID_ARGUMENT = 2,
    D7_MERKLE_OVERFLOW = 3,
    D7_MERKLE_CRYPTO_FAILURE = 4,
    D7_MERKLE_INVALID_ENCODING = 5,
    D7_MERKLE_INCOMPATIBLE_POLICY = 6,
    D7_MERKLE_INCONSISTENT_POLICY = 7,
    D7_MERKLE_CALLBACK_FAILURE = 8,
    D7_MERKLE_VERIFICATION_FAILURE = 9,
    D7_MERKLE_DUPLICATE_KEY = 10
} d7_merkle_status;

typedef struct d7_merkle_digest {
    unsigned char bytes[D7_MERKLE_DIGEST_BYTE_LENGTH];
} d7_merkle_digest;

bool d7_merkle_digest_equal(d7_merkle_digest left, d7_merkle_digest right);
int d7_merkle_digest_compare(d7_merkle_digest left, d7_merkle_digest right);
d7_merkle_status d7_merkle_digest_parse(
    const unsigned char *bytes,
    size_t byte_count,
    d7_merkle_digest *digest);
d7_merkle_status d7_merkle_digest_parse_hex(
    const char *hex,
    size_t character_count,
    d7_merkle_digest *digest);
d7_merkle_status d7_merkle_digest_write(
    d7_merkle_digest digest,
    unsigned char *destination,
    size_t destination_size);
d7_merkle_status d7_merkle_digest_write_hex(
    d7_merkle_digest digest,
    char *destination,
    size_t destination_size);

typedef void *(*d7_merkle_allocate_fn)(size_t size, void *context);
typedef void (*d7_merkle_deallocate_fn)(void *allocation, void *context);

typedef struct d7_merkle_allocator {
    d7_merkle_allocate_fn allocate;
    d7_merkle_deallocate_fn deallocate;
    void *context;
} d7_merkle_allocator;

typedef d7_merkle_status (*d7_merkle_copy_fn)(
    void *destination,
    const void *source,
    const d7_merkle_allocator *allocator,
    void *context);
typedef void (*d7_merkle_destroy_fn)(
    void *value,
    const d7_merkle_allocator *allocator,
    void *context);
typedef d7_merkle_status (*d7_merkle_compare_fn)(
    const void *left,
    const void *right,
    int *comparison,
    void *context);
typedef d7_merkle_status (*d7_merkle_equal_fn)(
    const void *left,
    const void *right,
    bool *equal,
    void *context);

/* Encode is a deterministic two-pass callback. With destination == NULL and
 * destination_size == 0 it writes the exact required length to bytes_written.
 * The second call receives a destination of exactly that size and must report
 * exactly the same length. The library owns and eventually deallocates that
 * destination; the callback never returns byte ownership. */
typedef d7_merkle_status (*d7_merkle_encode_fn)(
    const void *value,
    unsigned char *destination,
    size_t destination_size,
    size_t *bytes_written,
    const d7_merkle_allocator *allocator,
    void *context);

/* Decode consumes one complete canonical encoding and constructs an owned
 * value in uninitialized destination storage. Failure must leave destination
 * ownership-free. Untrusted wire ingestion re-encodes the value and requires
 * byte-for-byte equality before publication. */
typedef d7_merkle_status (*d7_merkle_decode_fn)(
    const unsigned char *encoding,
    size_t encoding_size,
    void *destination,
    const d7_merkle_allocator *allocator,
    void *context);

typedef struct d7_merkle_identifier {
    const unsigned char *bytes;
    size_t size;
} d7_merkle_identifier;

typedef struct d7_merkle_type_policy {
    size_t size;
    const void *type_identity;
    d7_merkle_copy_fn copy;
    d7_merkle_destroy_fn destroy;
    d7_merkle_equal_fn equals;
    void *context;
} d7_merkle_type_policy;

typedef struct d7_merkle_codec {
    d7_merkle_identifier encoding_id;
    d7_merkle_encode_fn encode;
    d7_merkle_decode_fn decode;
    void *context;
} d7_merkle_codec;

typedef struct d7_merkle_policy_config {
    d7_merkle_identifier policy_id;
    d7_merkle_type_policy key_type;
    d7_merkle_type_policy value_type;
    d7_merkle_compare_fn key_compare;
    void *key_compare_context;
    d7_merkle_codec key_codec;
    d7_merkle_codec value_codec;
    d7_merkle_allocator allocator;
} d7_merkle_policy_config;

void d7_merkle_type_policy_init(
    d7_merkle_type_policy *type,
    size_t size,
    const void *type_identity);
void d7_merkle_codec_init(
    d7_merkle_codec *codec,
    const unsigned char *encoding_id,
    size_t encoding_id_size,
    d7_merkle_encode_fn encode,
    d7_merkle_decode_fn decode);
void d7_merkle_policy_config_init(d7_merkle_policy_config *config);

typedef struct d7_merkle_nullable_utf8 {
    bool has_value;
    const char *data;
    size_t size;
} d7_merkle_nullable_utf8;

typedef struct d7_merkle_nullable_bytes {
    bool has_value;
    const unsigned char *data;
    size_t size;
} d7_merkle_nullable_bytes;

typedef struct d7_merkle_guid {
    unsigned char bytes[16];
} d7_merkle_guid;

void d7_merkle_i32_type_policy_init(
    d7_merkle_type_policy *type,
    const void *type_identity);
void d7_merkle_i64_type_policy_init(
    d7_merkle_type_policy *type,
    const void *type_identity);
void d7_merkle_nullable_utf8_type_policy_init(
    d7_merkle_type_policy *type,
    const void *type_identity);
void d7_merkle_nullable_bytes_type_policy_init(
    d7_merkle_type_policy *type,
    const void *type_identity);
void d7_merkle_guid_type_policy_init(
    d7_merkle_type_policy *type,
    const void *type_identity);

void d7_merkle_i32_codec_init(d7_merkle_codec *codec);
void d7_merkle_i64_codec_init(d7_merkle_codec *codec);
void d7_merkle_nullable_utf8_codec_init(d7_merkle_codec *codec);
void d7_merkle_nullable_bytes_codec_init(d7_merkle_codec *codec);
void d7_merkle_guid_codec_init(d7_merkle_codec *codec);

struct d7_merkle_policy_rep;

typedef struct d7_merkle_policy {
    struct d7_merkle_policy_rep *rep;
} d7_merkle_policy;

d7_merkle_status d7_merkle_policy_create(
    const d7_merkle_policy_config *config,
    d7_merkle_policy *policy);
d7_merkle_status d7_merkle_policy_copy(
    const d7_merkle_policy *source,
    d7_merkle_policy *destination);
void d7_merkle_policy_move(
    d7_merkle_policy *destination,
    d7_merkle_policy *source);
void d7_merkle_policy_dispose(d7_merkle_policy *policy);
bool d7_merkle_policy_same_identity(
    const d7_merkle_policy *left,
    const d7_merkle_policy *right);
bool d7_merkle_policy_same_domain(
    const d7_merkle_policy *left,
    const d7_merkle_policy *right);
const char *d7_merkle_algorithm_id(void);
d7_merkle_digest d7_merkle_policy_domain_digest(const d7_merkle_policy *policy);
d7_merkle_digest d7_merkle_policy_empty_digest(const d7_merkle_policy *policy);

typedef struct d7_merkle_search_input {
    const void *key;
    const void *value;
} d7_merkle_search_input;

typedef struct d7_merkle_search_entry_ref {
    const void *key;
    const void *value;
    const unsigned char *key_bytes;
    size_t key_byte_count;
    const unsigned char *value_bytes;
    size_t value_byte_count;
    unsigned level;
} d7_merkle_search_entry_ref;

typedef enum d7_merkle_difference_kind {
    D7_MERKLE_DIFFERENCE_ADDED = 0,
    D7_MERKLE_DIFFERENCE_REMOVED = 1,
    D7_MERKLE_DIFFERENCE_CHANGED = 2
} d7_merkle_difference_kind;

typedef struct d7_merkle_difference_ref {
    d7_merkle_difference_kind kind;
    const void *key;
    const void *before;
    const void *after;
} d7_merkle_difference_ref;

typedef struct d7_merkle_shape_ref {
    const void *node_identity;
    unsigned level;
    d7_merkle_search_entry_ref entry;
    size_t entries_in_block;
    size_t subtree_count;
} d7_merkle_shape_ref;

typedef struct d7_merkle_block_ref {
    d7_merkle_digest digest;
    const unsigned char *bytes;
    size_t byte_count;
} d7_merkle_block_ref;

typedef struct d7_merkle_search_tree_statistics {
    size_t count;
    size_t block_count;
    size_t height;
    size_t minimum_entries_per_block;
    size_t maximum_entries_per_block;
    size_t minimum_block_bytes;
    size_t maximum_block_bytes;
} d7_merkle_search_tree_statistics;

typedef d7_merkle_status (*d7_merkle_entry_visitor)(
    d7_merkle_search_entry_ref entry,
    void *context);
typedef d7_merkle_status (*d7_merkle_difference_visitor)(
    d7_merkle_difference_ref difference,
    void *context);
typedef d7_merkle_status (*d7_merkle_shape_visitor)(
    d7_merkle_shape_ref shape,
    void *context);
typedef d7_merkle_status (*d7_merkle_block_visitor)(
    d7_merkle_block_ref block,
    void *context);

struct d7_merkle_node;

typedef struct d7_merkle_search_tree {
    struct d7_merkle_policy_rep *policy;
    struct d7_merkle_node *root;
} d7_merkle_search_tree;

d7_merkle_status d7_merkle_search_tree_init(
    d7_merkle_search_tree *tree,
    const d7_merkle_policy *policy);
d7_merkle_status d7_merkle_search_tree_from_array(
    d7_merkle_search_tree *tree,
    const d7_merkle_policy *policy,
    const d7_merkle_search_input *entries,
    size_t entry_count);
d7_merkle_status d7_merkle_search_tree_copy(
    const d7_merkle_search_tree *source,
    d7_merkle_search_tree *destination);
void d7_merkle_search_tree_move(
    d7_merkle_search_tree *destination,
    d7_merkle_search_tree *source);
void d7_merkle_search_tree_dispose(d7_merkle_search_tree *tree);

bool d7_merkle_search_tree_empty(const d7_merkle_search_tree *tree);
size_t d7_merkle_search_tree_size(const d7_merkle_search_tree *tree);
size_t d7_merkle_search_tree_height(const d7_merkle_search_tree *tree);
size_t d7_merkle_search_tree_block_count(const d7_merkle_search_tree *tree);
d7_merkle_digest d7_merkle_search_tree_root_hash(const d7_merkle_search_tree *tree);

d7_merkle_status d7_merkle_search_tree_contains_key(
    const d7_merkle_search_tree *tree,
    const void *key,
    bool *found);
d7_merkle_status d7_merkle_search_tree_try_get_entry_ref(
    const d7_merkle_search_tree *tree,
    const void *key,
    bool *found,
    d7_merkle_search_entry_ref *entry);

/* Producing operations support exact source/result aliasing and publish only
 * after every allocation, callback, encoding, hash, and node construction has
 * succeeded. A distinct result must be uninitialized or disposed. */
d7_merkle_status d7_merkle_search_tree_set(
    const d7_merkle_search_tree *tree,
    const void *key,
    const void *value,
    d7_merkle_search_tree *result);
d7_merkle_status d7_merkle_search_tree_remove(
    const d7_merkle_search_tree *tree,
    const void *key,
    d7_merkle_search_tree *result);
d7_merkle_status d7_merkle_search_tree_clear(
    const d7_merkle_search_tree *tree,
    d7_merkle_search_tree *result);

/* Visitors borrow exact stored representatives. Calls are streaming: failure
 * stops traversal, but side effects of earlier successful visits remain. */
d7_merkle_status d7_merkle_search_tree_visit(
    const d7_merkle_search_tree *tree,
    d7_merkle_entry_visitor visitor,
    void *context);
d7_merkle_status d7_merkle_search_tree_visit_range(
    const d7_merkle_search_tree *tree,
    const void *minimum_key,
    const void *maximum_key,
    d7_merkle_entry_visitor visitor,
    void *context);

bool d7_merkle_search_tree_content_equals(
    const d7_merkle_search_tree *left,
    const d7_merkle_search_tree *right);
d7_merkle_status d7_merkle_search_tree_map_equals(
    const d7_merkle_search_tree *left,
    const d7_merkle_search_tree *right,
    bool *equal);
d7_merkle_status d7_merkle_search_tree_diff(
    const d7_merkle_search_tree *left,
    const d7_merkle_search_tree *right,
    d7_merkle_difference_visitor visitor,
    void *context);

const void *d7_merkle_search_tree_root_identity(
    const d7_merkle_search_tree *tree);
d7_merkle_status d7_merkle_search_tree_node_identity(
    const d7_merkle_search_tree *tree,
    const void *key,
    const void **identity);
d7_merkle_status d7_merkle_search_tree_shared_node_count(
    const d7_merkle_search_tree *left,
    const d7_merkle_search_tree *right,
    size_t *shared_count);
d7_merkle_status d7_merkle_search_tree_visit_shape(
    const d7_merkle_search_tree *tree,
    d7_merkle_shape_visitor visitor,
    void *context);
d7_merkle_status d7_merkle_search_tree_visit_blocks(
    const d7_merkle_search_tree *tree,
    d7_merkle_block_visitor visitor,
    void *context);
d7_merkle_status d7_merkle_search_tree_validate(
    const d7_merkle_search_tree *tree,
    bool *valid,
    d7_merkle_search_tree_statistics *statistics);

/* Immutable retained-tree-snapshot-plus-rank gap cursor in policy-comparer
 * order. Peeked entry references borrow from the retained snapshot and remain
 * valid until the cursor is destroyed. Producing operations support exact
 * source/result aliasing, publish only on success, and require a distinct
 * result to be uninitialized or destroyed. */
typedef struct d7_merkle_search_tree_cursor {
    d7_merkle_search_tree tree;
    size_t position;
} d7_merkle_search_tree_cursor;

d7_merkle_status d7_merkle_search_tree_cursor_create(
    const d7_merkle_search_tree *tree,
    size_t position,
    d7_merkle_search_tree_cursor *result);
d7_merkle_status d7_merkle_search_tree_cursor_at_start(
    const d7_merkle_search_tree *tree,
    d7_merkle_search_tree_cursor *result);
d7_merkle_status d7_merkle_search_tree_cursor_at_end(
    const d7_merkle_search_tree *tree,
    d7_merkle_search_tree_cursor *result);
d7_merkle_status d7_merkle_search_tree_cursor_lower_bound(
    const d7_merkle_search_tree *tree,
    const void *key,
    d7_merkle_search_tree_cursor *result);
d7_merkle_status d7_merkle_search_tree_cursor_upper_bound(
    const d7_merkle_search_tree *tree,
    const void *key,
    d7_merkle_search_tree_cursor *result);
d7_merkle_status d7_merkle_search_tree_cursor_at_key(
    const d7_merkle_search_tree *tree,
    const void *key,
    bool *found,
    d7_merkle_search_tree_cursor *result);
d7_merkle_status d7_merkle_search_tree_cursor_copy(
    const d7_merkle_search_tree_cursor *cursor,
    d7_merkle_search_tree_cursor *result);
void d7_merkle_search_tree_cursor_destroy(
    d7_merkle_search_tree_cursor *cursor);

size_t d7_merkle_search_tree_cursor_count(
    const d7_merkle_search_tree_cursor *cursor);
size_t d7_merkle_search_tree_cursor_position(
    const d7_merkle_search_tree_cursor *cursor);
bool d7_merkle_search_tree_cursor_is_at_start(
    const d7_merkle_search_tree_cursor *cursor);
bool d7_merkle_search_tree_cursor_is_at_end(
    const d7_merkle_search_tree_cursor *cursor);
bool d7_merkle_search_tree_cursor_try_peek_previous(
    const d7_merkle_search_tree_cursor *cursor,
    d7_merkle_search_entry_ref *entry);
bool d7_merkle_search_tree_cursor_try_peek_next(
    const d7_merkle_search_tree_cursor *cursor,
    d7_merkle_search_entry_ref *entry);

d7_merkle_status d7_merkle_search_tree_cursor_move_previous(
    const d7_merkle_search_tree_cursor *cursor,
    d7_merkle_search_tree_cursor *result);
d7_merkle_status d7_merkle_search_tree_cursor_move_next(
    const d7_merkle_search_tree_cursor *cursor,
    d7_merkle_search_tree_cursor *result);
d7_merkle_status d7_merkle_search_tree_cursor_seek(
    const d7_merkle_search_tree_cursor *cursor,
    size_t position,
    d7_merkle_search_tree_cursor *result);
d7_merkle_status d7_merkle_search_tree_cursor_insert(
    const d7_merkle_search_tree_cursor *cursor,
    const void *key,
    const void *value,
    d7_merkle_search_tree_cursor *result);
d7_merkle_status d7_merkle_search_tree_cursor_put(
    const d7_merkle_search_tree_cursor *cursor,
    const void *key,
    const void *value,
    d7_merkle_search_tree_cursor *result);
d7_merkle_status d7_merkle_search_tree_cursor_set_next_value(
    const d7_merkle_search_tree_cursor *cursor,
    const void *value,
    d7_merkle_search_tree_cursor *result);
d7_merkle_status d7_merkle_search_tree_cursor_delete_previous(
    const d7_merkle_search_tree_cursor *cursor,
    d7_merkle_search_tree_cursor *result);
d7_merkle_status d7_merkle_search_tree_cursor_delete_next(
    const d7_merkle_search_tree_cursor *cursor,
    d7_merkle_search_tree_cursor *result);
d7_merkle_status d7_merkle_search_tree_cursor_snapshot(
    const d7_merkle_search_tree_cursor *cursor,
    d7_merkle_search_tree *result);

/* ------------------------------------------------------------------------- */
/* Verified persistence, proofs, synchronization, and three-way merge.       */

typedef enum d7_merkle_verification_failure_kind {
    D7_MERKLE_VERIFY_NONE = 0,
    D7_MERKLE_VERIFY_UNSUPPORTED_ALGORITHM = 1,
    D7_MERKLE_VERIFY_DOMAIN_MISMATCH = 2,
    D7_MERKLE_VERIFY_MISSING_BLOCK = 3,
    D7_MERKLE_VERIFY_DIGEST_MISMATCH = 4,
    D7_MERKLE_VERIFY_MALFORMED_BLOCK = 5,
    D7_MERKLE_VERIFY_NONCANONICAL_BLOCK = 6,
    D7_MERKLE_VERIFY_DUPLICATE_BLOCK = 7,
    D7_MERKLE_VERIFY_CONFLICTING_BLOCK = 8,
    D7_MERKLE_VERIFY_INVALID_REFERENCE = 9,
    D7_MERKLE_VERIFY_CYCLE_DETECTED = 10,
    D7_MERKLE_VERIFY_PROOF_MISMATCH = 11,
    D7_MERKLE_VERIFY_ROOT_MISMATCH = 12,
    D7_MERKLE_VERIFY_RESOURCE_LIMIT_EXCEEDED = 13
} d7_merkle_verification_failure_kind;

typedef struct d7_merkle_verification_error {
    d7_merkle_verification_failure_kind kind;
    bool has_block_digest;
    d7_merkle_digest block_digest;
    size_t verified_block_count;
    uint64_t verified_byte_count;
} d7_merkle_verification_error;

void d7_merkle_verification_error_init(d7_merkle_verification_error *error);

typedef struct d7_merkle_verification_budget {
    size_t max_block_count;
    uint64_t max_total_byte_count;
    size_t max_block_byte_count;
    size_t max_depth;
    uint64_t max_entry_count;
    size_t max_child_references_per_block;
    size_t max_proof_query_byte_count;
} d7_merkle_verification_budget;

void d7_merkle_verification_budget_init_default(
    d7_merkle_verification_budget *budget);
d7_merkle_status d7_merkle_verification_budget_validate(
    const d7_merkle_verification_budget *budget);

struct d7_merkle_block_rep;

typedef struct d7_merkle_block {
    struct d7_merkle_block_rep *rep;
} d7_merkle_block;

d7_merkle_status d7_merkle_block_init(
    d7_merkle_digest digest,
    const unsigned char *bytes,
    size_t byte_count,
    const d7_merkle_allocator *allocator,
    d7_merkle_block *block);
d7_merkle_status d7_merkle_block_copy(
    const d7_merkle_block *source,
    d7_merkle_block *destination);
void d7_merkle_block_move(
    d7_merkle_block *destination,
    d7_merkle_block *source);
void d7_merkle_block_dispose(d7_merkle_block *block);
bool d7_merkle_block_equal(
    const d7_merkle_block *left,
    const d7_merkle_block *right);
d7_merkle_digest d7_merkle_block_digest(const d7_merkle_block *block);
const unsigned char *d7_merkle_block_bytes(const d7_merkle_block *block);
size_t d7_merkle_block_byte_count(const d7_merkle_block *block);

/* Generic block-store callback contract:
 *
 * - The public wrappers shield caller outputs and publish them only for the
 *   documented success/status combination.
 * - count/contains/remove write their supplied scratch output only on OK.
 * - The public try_get block out-parameter must be empty. It remains untouched
 *   on not-found/failure. The callback receives a zero-initialized scratch
 *   block. On OK it must set found
 *   true exactly when it returns a valid owning block snapshot; on not-found it
 *   leaves the block empty. On every non-OK return it must leave the block
 *   empty or validly owning so the wrapper can dispose it.
 * - put returns OK only with ADDED or PRESENT_IDENTICAL. A same-digest byte
 *   conflict returns VERIFICATION_FAILURE with CONFLICT. Other failures do not
 *   publish a put result.
 * - Callback-produced owning handles use the normal copy/move/dispose rules;
 *   borrowed block slots are never accepted as try_get results.
 *
 * A callback that returns OK with an inconsistent owning output is translated
 * to CALLBACK_FAILURE. Callback contexts are borrowed and must outlive every
 * store-adapter call. */
typedef d7_merkle_status (*d7_merkle_store_count_fn)(
    size_t *count,
    void *context);
typedef d7_merkle_status (*d7_merkle_store_contains_fn)(
    d7_merkle_digest digest,
    bool *contains,
    void *context);
typedef d7_merkle_status (*d7_merkle_store_try_get_fn)(
    d7_merkle_digest digest,
    bool *found,
    d7_merkle_block *block,
    void *context);

typedef enum d7_merkle_store_put_result {
    D7_MERKLE_STORE_ADDED = 0,
    D7_MERKLE_STORE_PRESENT_IDENTICAL = 1,
    D7_MERKLE_STORE_CONFLICT = 2
} d7_merkle_store_put_result;

typedef d7_merkle_status (*d7_merkle_store_put_fn)(
    const d7_merkle_block *block,
    d7_merkle_store_put_result *result,
    void *context);
typedef d7_merkle_status (*d7_merkle_store_remove_fn)(
    d7_merkle_digest digest,
    bool *removed,
    void *context);
typedef d7_merkle_status (*d7_merkle_store_clear_fn)(void *context);

typedef struct d7_merkle_block_store {
    d7_merkle_store_count_fn count;
    d7_merkle_store_contains_fn contains;
    d7_merkle_store_try_get_fn try_get;
    d7_merkle_store_put_fn put;
    d7_merkle_store_remove_fn remove;
    d7_merkle_store_clear_fn clear;
    void *context;
} d7_merkle_block_store;

void d7_merkle_block_store_init(d7_merkle_block_store *store);
d7_merkle_status d7_merkle_block_store_count(
    const d7_merkle_block_store *store,
    size_t *count);
d7_merkle_status d7_merkle_block_store_contains(
    const d7_merkle_block_store *store,
    d7_merkle_digest digest,
    bool *contains);
d7_merkle_status d7_merkle_block_store_try_get(
    const d7_merkle_block_store *store,
    d7_merkle_digest digest,
    bool *found,
    d7_merkle_block *block);
d7_merkle_status d7_merkle_block_store_put(
    const d7_merkle_block_store *store,
    const d7_merkle_block *block,
    d7_merkle_store_put_result *result);
d7_merkle_status d7_merkle_block_store_remove(
    const d7_merkle_block_store *store,
    d7_merkle_digest digest,
    bool *removed);
d7_merkle_status d7_merkle_block_store_clear(
    const d7_merkle_block_store *store);

struct d7_merkle_memory_block_store_rep;

typedef struct d7_merkle_memory_block_store {
    struct d7_merkle_memory_block_store_rep *rep;
} d7_merkle_memory_block_store;

typedef d7_merkle_status (*d7_merkle_digest_visitor)(
    d7_merkle_digest digest,
    void *context);

d7_merkle_status d7_merkle_memory_block_store_init(
    d7_merkle_memory_block_store *store,
    const d7_merkle_allocator *allocator);
d7_merkle_status d7_merkle_memory_block_store_copy(
    const d7_merkle_memory_block_store *source,
    d7_merkle_memory_block_store *destination);
void d7_merkle_memory_block_store_move(
    d7_merkle_memory_block_store *destination,
    d7_merkle_memory_block_store *source);
void d7_merkle_memory_block_store_dispose(
    d7_merkle_memory_block_store *store);
d7_merkle_status d7_merkle_memory_block_store_as_store(
    const d7_merkle_memory_block_store *memory_store,
    d7_merkle_block_store *store);
/* The adapter is borrowed: an owning memory-store handle must outlive every
 * adapter call. try_get returns an owning block snapshot. The implementation
 * invokes allocator/deallocator, block destruction, and visitors only outside
 * its non-recursive lock, so those callbacks may reenter live adapter reads.
 * Final owning-handle disposal ends adapter lifetime before allocator teardown. */
d7_merkle_status d7_merkle_memory_block_store_visit_digests(
    const d7_merkle_memory_block_store *store,
    d7_merkle_digest_visitor visitor,
    void *context);

struct d7_merkle_block_pack_rep;

typedef struct d7_merkle_block_pack {
    struct d7_merkle_block_pack_rep *rep;
} d7_merkle_block_pack;

d7_merkle_status d7_merkle_block_pack_init(
    d7_merkle_identifier algorithm_id,
    d7_merkle_digest domain_digest,
    d7_merkle_digest root_hash,
    const d7_merkle_block *blocks,
    size_t block_count,
    const d7_merkle_allocator *allocator,
    d7_merkle_block_pack *pack,
    d7_merkle_verification_error *error);
d7_merkle_status d7_merkle_block_pack_copy(
    const d7_merkle_block_pack *source,
    d7_merkle_block_pack *destination);
void d7_merkle_block_pack_move(
    d7_merkle_block_pack *destination,
    d7_merkle_block_pack *source);
void d7_merkle_block_pack_dispose(d7_merkle_block_pack *pack);
d7_merkle_identifier d7_merkle_block_pack_algorithm_id(
    const d7_merkle_block_pack *pack);
d7_merkle_digest d7_merkle_block_pack_domain_digest(
    const d7_merkle_block_pack *pack);
d7_merkle_digest d7_merkle_block_pack_root_hash(
    const d7_merkle_block_pack *pack);
size_t d7_merkle_block_pack_block_count(const d7_merkle_block_pack *pack);
uint64_t d7_merkle_block_pack_total_byte_count(
    const d7_merkle_block_pack *pack);
bool d7_merkle_block_pack_contains_root_block(
    const d7_merkle_block_pack *pack);
const d7_merkle_block *d7_merkle_block_pack_block_at(
    const d7_merkle_block_pack *pack,
    size_t index);

d7_merkle_status d7_merkle_search_tree_save(
    const d7_merkle_search_tree *tree,
    const d7_merkle_block_store *store,
    size_t *added_block_count,
    d7_merkle_verification_error *error);
d7_merkle_status d7_merkle_search_tree_export_pack(
    const d7_merkle_search_tree *tree,
    d7_merkle_block_pack *pack);
d7_merkle_status d7_merkle_search_tree_export_blocks(
    const d7_merkle_search_tree *tree,
    const d7_merkle_digest *digests,
    size_t digest_count,
    d7_merkle_block_pack *pack,
    d7_merkle_verification_error *error);
d7_merkle_status d7_merkle_search_tree_load(
    d7_merkle_digest root_hash,
    const d7_merkle_policy *policy,
    const d7_merkle_block_store *store,
    const d7_merkle_verification_budget *budget,
    d7_merkle_search_tree *tree,
    d7_merkle_verification_error *error);
d7_merkle_status d7_merkle_search_tree_import_pack(
    const d7_merkle_block_pack *pack,
    const d7_merkle_policy *policy,
    const d7_merkle_block_store *destination_store,
    const d7_merkle_verification_budget *budget,
    d7_merkle_search_tree *tree,
    d7_merkle_verification_error *error);
/* Import verifies every supplied block and the complete declared-root closure
 * before destination preflight/publication. Canonical root-unreachable blocks
 * are allowed as partial synchronization state. */

struct d7_merkle_sync_plan_rep;

typedef struct d7_merkle_sync_plan {
    struct d7_merkle_sync_plan_rep *rep;
} d7_merkle_sync_plan;

d7_merkle_status d7_merkle_sync_plan_copy(
    const d7_merkle_sync_plan *source,
    d7_merkle_sync_plan *destination);
void d7_merkle_sync_plan_move(
    d7_merkle_sync_plan *destination,
    d7_merkle_sync_plan *source);
void d7_merkle_sync_plan_dispose(d7_merkle_sync_plan *plan);
d7_merkle_identifier d7_merkle_sync_plan_algorithm_id(
    const d7_merkle_sync_plan *plan);
d7_merkle_digest d7_merkle_sync_plan_domain_digest(
    const d7_merkle_sync_plan *plan);
d7_merkle_digest d7_merkle_sync_plan_local_root_hash(
    const d7_merkle_sync_plan *plan);
d7_merkle_digest d7_merkle_sync_plan_target_root_hash(
    const d7_merkle_sync_plan *plan);
size_t d7_merkle_sync_plan_requested_block_count(
    const d7_merkle_sync_plan *plan);
const d7_merkle_digest *d7_merkle_sync_plan_requested_blocks(
    const d7_merkle_sync_plan *plan);
size_t d7_merkle_sync_plan_examined_block_count(
    const d7_merkle_sync_plan *plan);
uint64_t d7_merkle_sync_plan_examined_byte_count(
    const d7_merkle_sync_plan *plan);
bool d7_merkle_sync_plan_roots_match(const d7_merkle_sync_plan *plan);
bool d7_merkle_sync_plan_requires_blocks(const d7_merkle_sync_plan *plan);

d7_merkle_status d7_merkle_search_tree_create_sync_pack(
    const d7_merkle_search_tree *target,
    const d7_merkle_block_store *receiver_store,
    d7_merkle_block_pack *pack);
d7_merkle_status d7_merkle_search_tree_plan_sync(
    const d7_merkle_search_tree *target,
    const d7_merkle_search_tree *local,
    const d7_merkle_block_store *receiver_store,
    d7_merkle_sync_plan *plan);

typedef enum d7_merkle_proof_kind {
    D7_MERKLE_PROOF_MEMBERSHIP = 0,
    D7_MERKLE_PROOF_NONMEMBERSHIP = 1,
    D7_MERKLE_PROOF_RANGE = 2
} d7_merkle_proof_kind;

typedef struct d7_merkle_proof_step_input {
    const d7_merkle_block *block;
    const size_t *expanded_child_indexes;
    size_t expanded_child_count;
} d7_merkle_proof_step_input;

struct d7_merkle_proof_rep;

typedef struct d7_merkle_proof {
    struct d7_merkle_proof_rep *rep;
} d7_merkle_proof;

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
    d7_merkle_verification_error *error);
d7_merkle_status d7_merkle_proof_copy(
    const d7_merkle_proof *source,
    d7_merkle_proof *destination);
void d7_merkle_proof_move(
    d7_merkle_proof *destination,
    d7_merkle_proof *source);
void d7_merkle_proof_dispose(d7_merkle_proof *proof);
d7_merkle_proof_kind d7_merkle_proof_kind_value(const d7_merkle_proof *proof);
d7_merkle_digest d7_merkle_proof_root_hash(const d7_merkle_proof *proof);
d7_merkle_digest d7_merkle_proof_domain_digest(const d7_merkle_proof *proof);
d7_merkle_identifier d7_merkle_proof_algorithm_id(const d7_merkle_proof *proof);
const unsigned char *d7_merkle_proof_query(const d7_merkle_proof *proof);
size_t d7_merkle_proof_query_byte_count(const d7_merkle_proof *proof);
size_t d7_merkle_proof_step_count(const d7_merkle_proof *proof);
const d7_merkle_block *d7_merkle_proof_step_block(
    const d7_merkle_proof *proof,
    size_t step_index);
const size_t *d7_merkle_proof_step_expanded_children(
    const d7_merkle_proof *proof,
    size_t step_index,
    size_t *expanded_child_count);
uint64_t d7_merkle_proof_total_byte_count(const d7_merkle_proof *proof);

typedef struct d7_merkle_proof_verification_result {
    bool is_valid;
    d7_merkle_verification_failure_kind failure_kind;
    bool has_computed_root_hash;
    d7_merkle_digest computed_root_hash;
    size_t verified_block_count;
    uint64_t verified_byte_count;
} d7_merkle_proof_verification_result;

d7_merkle_status d7_merkle_search_tree_create_proof(
    const d7_merkle_search_tree *tree,
    const void *key,
    d7_merkle_proof *proof);
d7_merkle_status d7_merkle_search_tree_create_range_proof(
    const d7_merkle_search_tree *tree,
    const void *minimum_key,
    const void *maximum_key,
    d7_merkle_proof *proof);
d7_merkle_status d7_merkle_search_tree_verify_proof(
    const d7_merkle_proof *proof,
    const d7_merkle_policy *policy,
    const d7_merkle_verification_budget *budget,
    d7_merkle_proof_verification_result *result);
/* Invalid untrusted proofs return OK with result->is_valid == false.
 * Operational allocator/callback failures remain non-OK. Query, step-count,
 * and expansion-count limits are checked before verifier work in that order. */

typedef struct d7_merkle_merge_value_ref {
    bool present;
    const void *value;
} d7_merkle_merge_value_ref;

typedef struct d7_merkle_three_way_merge_conflict_ref {
    const void *key;
    d7_merkle_merge_value_ref base;
    d7_merkle_merge_value_ref left;
    d7_merkle_merge_value_ref right;
} d7_merkle_three_way_merge_conflict_ref;

typedef enum d7_merkle_merge_resolution_kind {
    D7_MERKLE_MERGE_UNRESOLVED = 0,
    D7_MERKLE_MERGE_USE_BASE = 1,
    D7_MERKLE_MERGE_USE_LEFT = 2,
    D7_MERKLE_MERGE_USE_RIGHT = 3,
    D7_MERKLE_MERGE_SET_VALUE = 4,
    D7_MERKLE_MERGE_DELETE = 5
} d7_merkle_merge_resolution_kind;

typedef struct d7_merkle_merge_resolution {
    d7_merkle_merge_resolution_kind kind;
    const void *value;
} d7_merkle_merge_resolution;

typedef d7_merkle_status (*d7_merkle_merge_resolver)(
    d7_merkle_three_way_merge_conflict_ref conflict,
    d7_merkle_merge_resolution *resolution,
    void *context);

struct d7_merkle_three_way_merge_result_rep;

typedef struct d7_merkle_three_way_merge_result {
    struct d7_merkle_three_way_merge_result_rep *rep;
} d7_merkle_three_way_merge_result;

d7_merkle_status d7_merkle_three_way_merge_result_copy(
    const d7_merkle_three_way_merge_result *source,
    d7_merkle_three_way_merge_result *destination);
void d7_merkle_three_way_merge_result_move(
    d7_merkle_three_way_merge_result *destination,
    d7_merkle_three_way_merge_result *source);
void d7_merkle_three_way_merge_result_dispose(
    d7_merkle_three_way_merge_result *result);
bool d7_merkle_three_way_merge_result_success(
    const d7_merkle_three_way_merge_result *result);
d7_merkle_status d7_merkle_three_way_merge_result_copy_tree(
    const d7_merkle_three_way_merge_result *result,
    d7_merkle_search_tree *tree);
size_t d7_merkle_three_way_merge_result_conflict_count(
    const d7_merkle_three_way_merge_result *result);
d7_merkle_status d7_merkle_three_way_merge_result_conflict_at(
    const d7_merkle_three_way_merge_result *result,
    size_t index,
    d7_merkle_three_way_merge_conflict_ref *conflict);

d7_merkle_status d7_merkle_search_tree_merge(
    const d7_merkle_search_tree *base,
    const d7_merkle_search_tree *left,
    const d7_merkle_search_tree *right,
    d7_merkle_merge_resolver resolver,
    void *resolver_context,
    d7_merkle_three_way_merge_result *result);
/* All inputs must share the exact policy representation. Unresolved conflicts
 * are a successful operation with result_success == false, owned conflicts,
 * and no partial tree. merge_value_ref.present distinguishes absence from a
 * present nullable value whose typed wrapper represents semantic null. */

#ifdef __cplusplus
}
#endif

#endif
