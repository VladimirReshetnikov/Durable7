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
    TDS_MERKLE_CALLBACK_FAILURE = 8,
    TDS_MERKLE_VERIFICATION_FAILURE = 9
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

/* ------------------------------------------------------------------------- */
/* Verified persistence, proofs, synchronization, and three-way merge.       */

typedef enum tds_merkle_verification_failure_kind {
    TDS_MERKLE_VERIFY_NONE = 0,
    TDS_MERKLE_VERIFY_UNSUPPORTED_ALGORITHM = 1,
    TDS_MERKLE_VERIFY_DOMAIN_MISMATCH = 2,
    TDS_MERKLE_VERIFY_MISSING_BLOCK = 3,
    TDS_MERKLE_VERIFY_DIGEST_MISMATCH = 4,
    TDS_MERKLE_VERIFY_MALFORMED_BLOCK = 5,
    TDS_MERKLE_VERIFY_NONCANONICAL_BLOCK = 6,
    TDS_MERKLE_VERIFY_DUPLICATE_BLOCK = 7,
    TDS_MERKLE_VERIFY_CONFLICTING_BLOCK = 8,
    TDS_MERKLE_VERIFY_INVALID_REFERENCE = 9,
    TDS_MERKLE_VERIFY_CYCLE_DETECTED = 10,
    TDS_MERKLE_VERIFY_PROOF_MISMATCH = 11,
    TDS_MERKLE_VERIFY_ROOT_MISMATCH = 12,
    TDS_MERKLE_VERIFY_RESOURCE_LIMIT_EXCEEDED = 13
} tds_merkle_verification_failure_kind;

typedef struct tds_merkle_verification_error {
    tds_merkle_verification_failure_kind kind;
    bool has_block_digest;
    tds_merkle_digest block_digest;
    size_t verified_block_count;
    uint64_t verified_byte_count;
} tds_merkle_verification_error;

void tds_merkle_verification_error_init(tds_merkle_verification_error *error);

typedef struct tds_merkle_verification_budget {
    size_t max_block_count;
    uint64_t max_total_byte_count;
    size_t max_block_byte_count;
    size_t max_depth;
    uint64_t max_entry_count;
    size_t max_child_references_per_block;
    size_t max_proof_query_byte_count;
} tds_merkle_verification_budget;

void tds_merkle_verification_budget_init_default(
    tds_merkle_verification_budget *budget);
tds_merkle_status tds_merkle_verification_budget_validate(
    const tds_merkle_verification_budget *budget);

struct tds_merkle_block_rep;

typedef struct tds_merkle_block {
    struct tds_merkle_block_rep *rep;
} tds_merkle_block;

tds_merkle_status tds_merkle_block_init(
    tds_merkle_digest digest,
    const unsigned char *bytes,
    size_t byte_count,
    const tds_merkle_allocator *allocator,
    tds_merkle_block *block);
tds_merkle_status tds_merkle_block_copy(
    const tds_merkle_block *source,
    tds_merkle_block *destination);
void tds_merkle_block_move(
    tds_merkle_block *destination,
    tds_merkle_block *source);
void tds_merkle_block_dispose(tds_merkle_block *block);
bool tds_merkle_block_equal(
    const tds_merkle_block *left,
    const tds_merkle_block *right);
tds_merkle_digest tds_merkle_block_digest(const tds_merkle_block *block);
const unsigned char *tds_merkle_block_bytes(const tds_merkle_block *block);
size_t tds_merkle_block_byte_count(const tds_merkle_block *block);

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
typedef tds_merkle_status (*tds_merkle_store_count_fn)(
    size_t *count,
    void *context);
typedef tds_merkle_status (*tds_merkle_store_contains_fn)(
    tds_merkle_digest digest,
    bool *contains,
    void *context);
typedef tds_merkle_status (*tds_merkle_store_try_get_fn)(
    tds_merkle_digest digest,
    bool *found,
    tds_merkle_block *block,
    void *context);

typedef enum tds_merkle_store_put_result {
    TDS_MERKLE_STORE_ADDED = 0,
    TDS_MERKLE_STORE_PRESENT_IDENTICAL = 1,
    TDS_MERKLE_STORE_CONFLICT = 2
} tds_merkle_store_put_result;

typedef tds_merkle_status (*tds_merkle_store_put_fn)(
    const tds_merkle_block *block,
    tds_merkle_store_put_result *result,
    void *context);
typedef tds_merkle_status (*tds_merkle_store_remove_fn)(
    tds_merkle_digest digest,
    bool *removed,
    void *context);
typedef tds_merkle_status (*tds_merkle_store_clear_fn)(void *context);

typedef struct tds_merkle_block_store {
    tds_merkle_store_count_fn count;
    tds_merkle_store_contains_fn contains;
    tds_merkle_store_try_get_fn try_get;
    tds_merkle_store_put_fn put;
    tds_merkle_store_remove_fn remove;
    tds_merkle_store_clear_fn clear;
    void *context;
} tds_merkle_block_store;

void tds_merkle_block_store_init(tds_merkle_block_store *store);
tds_merkle_status tds_merkle_block_store_count(
    const tds_merkle_block_store *store,
    size_t *count);
tds_merkle_status tds_merkle_block_store_contains(
    const tds_merkle_block_store *store,
    tds_merkle_digest digest,
    bool *contains);
tds_merkle_status tds_merkle_block_store_try_get(
    const tds_merkle_block_store *store,
    tds_merkle_digest digest,
    bool *found,
    tds_merkle_block *block);
tds_merkle_status tds_merkle_block_store_put(
    const tds_merkle_block_store *store,
    const tds_merkle_block *block,
    tds_merkle_store_put_result *result);
tds_merkle_status tds_merkle_block_store_remove(
    const tds_merkle_block_store *store,
    tds_merkle_digest digest,
    bool *removed);
tds_merkle_status tds_merkle_block_store_clear(
    const tds_merkle_block_store *store);

struct tds_merkle_memory_block_store_rep;

typedef struct tds_merkle_memory_block_store {
    struct tds_merkle_memory_block_store_rep *rep;
} tds_merkle_memory_block_store;

typedef tds_merkle_status (*tds_merkle_digest_visitor)(
    tds_merkle_digest digest,
    void *context);

tds_merkle_status tds_merkle_memory_block_store_init(
    tds_merkle_memory_block_store *store,
    const tds_merkle_allocator *allocator);
tds_merkle_status tds_merkle_memory_block_store_copy(
    const tds_merkle_memory_block_store *source,
    tds_merkle_memory_block_store *destination);
void tds_merkle_memory_block_store_move(
    tds_merkle_memory_block_store *destination,
    tds_merkle_memory_block_store *source);
void tds_merkle_memory_block_store_dispose(
    tds_merkle_memory_block_store *store);
tds_merkle_status tds_merkle_memory_block_store_as_store(
    const tds_merkle_memory_block_store *memory_store,
    tds_merkle_block_store *store);
/* The adapter is borrowed: an owning memory-store handle must outlive every
 * adapter call. try_get returns an owning block snapshot. The implementation
 * invokes allocator/deallocator, block destruction, and visitors only outside
 * its non-recursive lock, so those callbacks may reenter live adapter reads.
 * Final owning-handle disposal ends adapter lifetime before allocator teardown. */
tds_merkle_status tds_merkle_memory_block_store_visit_digests(
    const tds_merkle_memory_block_store *store,
    tds_merkle_digest_visitor visitor,
    void *context);

struct tds_merkle_block_pack_rep;

typedef struct tds_merkle_block_pack {
    struct tds_merkle_block_pack_rep *rep;
} tds_merkle_block_pack;

tds_merkle_status tds_merkle_block_pack_init(
    tds_merkle_identifier algorithm_id,
    tds_merkle_digest domain_digest,
    tds_merkle_digest root_hash,
    const tds_merkle_block *blocks,
    size_t block_count,
    const tds_merkle_allocator *allocator,
    tds_merkle_block_pack *pack,
    tds_merkle_verification_error *error);
tds_merkle_status tds_merkle_block_pack_copy(
    const tds_merkle_block_pack *source,
    tds_merkle_block_pack *destination);
void tds_merkle_block_pack_move(
    tds_merkle_block_pack *destination,
    tds_merkle_block_pack *source);
void tds_merkle_block_pack_dispose(tds_merkle_block_pack *pack);
tds_merkle_identifier tds_merkle_block_pack_algorithm_id(
    const tds_merkle_block_pack *pack);
tds_merkle_digest tds_merkle_block_pack_domain_digest(
    const tds_merkle_block_pack *pack);
tds_merkle_digest tds_merkle_block_pack_root_hash(
    const tds_merkle_block_pack *pack);
size_t tds_merkle_block_pack_block_count(const tds_merkle_block_pack *pack);
uint64_t tds_merkle_block_pack_total_byte_count(
    const tds_merkle_block_pack *pack);
bool tds_merkle_block_pack_contains_root_block(
    const tds_merkle_block_pack *pack);
const tds_merkle_block *tds_merkle_block_pack_block_at(
    const tds_merkle_block_pack *pack,
    size_t index);

tds_merkle_status tds_merkle_search_tree_save(
    const tds_merkle_search_tree *tree,
    const tds_merkle_block_store *store,
    size_t *added_block_count,
    tds_merkle_verification_error *error);
tds_merkle_status tds_merkle_search_tree_export_pack(
    const tds_merkle_search_tree *tree,
    tds_merkle_block_pack *pack);
tds_merkle_status tds_merkle_search_tree_export_blocks(
    const tds_merkle_search_tree *tree,
    const tds_merkle_digest *digests,
    size_t digest_count,
    tds_merkle_block_pack *pack,
    tds_merkle_verification_error *error);
tds_merkle_status tds_merkle_search_tree_load(
    tds_merkle_digest root_hash,
    const tds_merkle_policy *policy,
    const tds_merkle_block_store *store,
    const tds_merkle_verification_budget *budget,
    tds_merkle_search_tree *tree,
    tds_merkle_verification_error *error);
tds_merkle_status tds_merkle_search_tree_import_pack(
    const tds_merkle_block_pack *pack,
    const tds_merkle_policy *policy,
    const tds_merkle_block_store *destination_store,
    const tds_merkle_verification_budget *budget,
    tds_merkle_search_tree *tree,
    tds_merkle_verification_error *error);
/* Import verifies every supplied block and the complete declared-root closure
 * before destination preflight/publication. Canonical root-unreachable blocks
 * are allowed as partial synchronization state. */

struct tds_merkle_sync_plan_rep;

typedef struct tds_merkle_sync_plan {
    struct tds_merkle_sync_plan_rep *rep;
} tds_merkle_sync_plan;

tds_merkle_status tds_merkle_sync_plan_copy(
    const tds_merkle_sync_plan *source,
    tds_merkle_sync_plan *destination);
void tds_merkle_sync_plan_move(
    tds_merkle_sync_plan *destination,
    tds_merkle_sync_plan *source);
void tds_merkle_sync_plan_dispose(tds_merkle_sync_plan *plan);
tds_merkle_identifier tds_merkle_sync_plan_algorithm_id(
    const tds_merkle_sync_plan *plan);
tds_merkle_digest tds_merkle_sync_plan_domain_digest(
    const tds_merkle_sync_plan *plan);
tds_merkle_digest tds_merkle_sync_plan_local_root_hash(
    const tds_merkle_sync_plan *plan);
tds_merkle_digest tds_merkle_sync_plan_target_root_hash(
    const tds_merkle_sync_plan *plan);
size_t tds_merkle_sync_plan_requested_block_count(
    const tds_merkle_sync_plan *plan);
const tds_merkle_digest *tds_merkle_sync_plan_requested_blocks(
    const tds_merkle_sync_plan *plan);
size_t tds_merkle_sync_plan_examined_block_count(
    const tds_merkle_sync_plan *plan);
uint64_t tds_merkle_sync_plan_examined_byte_count(
    const tds_merkle_sync_plan *plan);
bool tds_merkle_sync_plan_roots_match(const tds_merkle_sync_plan *plan);
bool tds_merkle_sync_plan_requires_blocks(const tds_merkle_sync_plan *plan);

tds_merkle_status tds_merkle_search_tree_create_sync_pack(
    const tds_merkle_search_tree *target,
    const tds_merkle_block_store *receiver_store,
    tds_merkle_block_pack *pack);
tds_merkle_status tds_merkle_search_tree_plan_sync(
    const tds_merkle_search_tree *target,
    const tds_merkle_search_tree *local,
    const tds_merkle_block_store *receiver_store,
    tds_merkle_sync_plan *plan);

typedef enum tds_merkle_proof_kind {
    TDS_MERKLE_PROOF_MEMBERSHIP = 0,
    TDS_MERKLE_PROOF_NONMEMBERSHIP = 1,
    TDS_MERKLE_PROOF_RANGE = 2
} tds_merkle_proof_kind;

typedef struct tds_merkle_proof_step_input {
    const tds_merkle_block *block;
    const size_t *expanded_child_indexes;
    size_t expanded_child_count;
} tds_merkle_proof_step_input;

struct tds_merkle_proof_rep;

typedef struct tds_merkle_proof {
    struct tds_merkle_proof_rep *rep;
} tds_merkle_proof;

tds_merkle_status tds_merkle_proof_init(
    tds_merkle_identifier algorithm_id,
    tds_merkle_digest domain_digest,
    tds_merkle_digest root_hash,
    tds_merkle_proof_kind kind,
    const unsigned char *query,
    size_t query_byte_count,
    const tds_merkle_proof_step_input *steps,
    size_t step_count,
    const tds_merkle_allocator *allocator,
    tds_merkle_proof *proof,
    tds_merkle_verification_error *error);
tds_merkle_status tds_merkle_proof_copy(
    const tds_merkle_proof *source,
    tds_merkle_proof *destination);
void tds_merkle_proof_move(
    tds_merkle_proof *destination,
    tds_merkle_proof *source);
void tds_merkle_proof_dispose(tds_merkle_proof *proof);
tds_merkle_proof_kind tds_merkle_proof_kind_value(const tds_merkle_proof *proof);
tds_merkle_digest tds_merkle_proof_root_hash(const tds_merkle_proof *proof);
tds_merkle_digest tds_merkle_proof_domain_digest(const tds_merkle_proof *proof);
tds_merkle_identifier tds_merkle_proof_algorithm_id(const tds_merkle_proof *proof);
const unsigned char *tds_merkle_proof_query(const tds_merkle_proof *proof);
size_t tds_merkle_proof_query_byte_count(const tds_merkle_proof *proof);
size_t tds_merkle_proof_step_count(const tds_merkle_proof *proof);
const tds_merkle_block *tds_merkle_proof_step_block(
    const tds_merkle_proof *proof,
    size_t step_index);
const size_t *tds_merkle_proof_step_expanded_children(
    const tds_merkle_proof *proof,
    size_t step_index,
    size_t *expanded_child_count);
uint64_t tds_merkle_proof_total_byte_count(const tds_merkle_proof *proof);

typedef struct tds_merkle_proof_verification_result {
    bool is_valid;
    tds_merkle_verification_failure_kind failure_kind;
    bool has_computed_root_hash;
    tds_merkle_digest computed_root_hash;
    size_t verified_block_count;
    uint64_t verified_byte_count;
} tds_merkle_proof_verification_result;

tds_merkle_status tds_merkle_search_tree_create_proof(
    const tds_merkle_search_tree *tree,
    const void *key,
    tds_merkle_proof *proof);
tds_merkle_status tds_merkle_search_tree_create_range_proof(
    const tds_merkle_search_tree *tree,
    const void *minimum_key,
    const void *maximum_key,
    tds_merkle_proof *proof);
tds_merkle_status tds_merkle_search_tree_verify_proof(
    const tds_merkle_proof *proof,
    const tds_merkle_policy *policy,
    const tds_merkle_verification_budget *budget,
    tds_merkle_proof_verification_result *result);
/* Invalid untrusted proofs return OK with result->is_valid == false.
 * Operational allocator/callback failures remain non-OK. Query, step-count,
 * and expansion-count limits are checked before verifier work in that order. */

typedef struct tds_merkle_merge_value_ref {
    bool present;
    const void *value;
} tds_merkle_merge_value_ref;

typedef struct tds_merkle_three_way_merge_conflict_ref {
    const void *key;
    tds_merkle_merge_value_ref base;
    tds_merkle_merge_value_ref left;
    tds_merkle_merge_value_ref right;
} tds_merkle_three_way_merge_conflict_ref;

typedef enum tds_merkle_merge_resolution_kind {
    TDS_MERKLE_MERGE_UNRESOLVED = 0,
    TDS_MERKLE_MERGE_USE_BASE = 1,
    TDS_MERKLE_MERGE_USE_LEFT = 2,
    TDS_MERKLE_MERGE_USE_RIGHT = 3,
    TDS_MERKLE_MERGE_SET_VALUE = 4,
    TDS_MERKLE_MERGE_DELETE = 5
} tds_merkle_merge_resolution_kind;

typedef struct tds_merkle_merge_resolution {
    tds_merkle_merge_resolution_kind kind;
    const void *value;
} tds_merkle_merge_resolution;

typedef tds_merkle_status (*tds_merkle_merge_resolver)(
    tds_merkle_three_way_merge_conflict_ref conflict,
    tds_merkle_merge_resolution *resolution,
    void *context);

struct tds_merkle_three_way_merge_result_rep;

typedef struct tds_merkle_three_way_merge_result {
    struct tds_merkle_three_way_merge_result_rep *rep;
} tds_merkle_three_way_merge_result;

tds_merkle_status tds_merkle_three_way_merge_result_copy(
    const tds_merkle_three_way_merge_result *source,
    tds_merkle_three_way_merge_result *destination);
void tds_merkle_three_way_merge_result_move(
    tds_merkle_three_way_merge_result *destination,
    tds_merkle_three_way_merge_result *source);
void tds_merkle_three_way_merge_result_dispose(
    tds_merkle_three_way_merge_result *result);
bool tds_merkle_three_way_merge_result_success(
    const tds_merkle_three_way_merge_result *result);
tds_merkle_status tds_merkle_three_way_merge_result_copy_tree(
    const tds_merkle_three_way_merge_result *result,
    tds_merkle_search_tree *tree);
size_t tds_merkle_three_way_merge_result_conflict_count(
    const tds_merkle_three_way_merge_result *result);
tds_merkle_status tds_merkle_three_way_merge_result_conflict_at(
    const tds_merkle_three_way_merge_result *result,
    size_t index,
    tds_merkle_three_way_merge_conflict_ref *conflict);

tds_merkle_status tds_merkle_search_tree_merge(
    const tds_merkle_search_tree *base,
    const tds_merkle_search_tree *left,
    const tds_merkle_search_tree *right,
    tds_merkle_merge_resolver resolver,
    void *resolver_context,
    tds_merkle_three_way_merge_result *result);
/* All inputs must share the exact policy representation. Unresolved conflicts
 * are a successful operation with result_success == false, owned conflicts,
 * and no partial tree. merge_value_ref.present distinguishes absence from a
 * present nullable value whose typed wrapper represents semantic null. */

#ifdef __cplusplus
}
#endif

#endif
