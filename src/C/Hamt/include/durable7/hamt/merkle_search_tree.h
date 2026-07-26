/*
 * A Merkle search tree: a persistent ordered map whose structure is fixed by its contents.
 *
 * Each block's digest covers its entries and its children's digests, so equal digests mean equal
 * contents. Because the shape is derived from the data rather than from the edit history, two
 * parties who inserted the same entries in different orders hold identical trees, and synchronizing
 * them costs only what actually differs: matching subtrees are skipped whole.
 *
 * Everything crossing a trust boundary is verified. Blocks are checked against the digest they were
 * fetched by, proofs and packs carry an algorithm and domain identifier, and every verification is
 * charged against a caller-supplied budget so crafted input cannot force unbounded work.
 */

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

/* Whether both digests hold the same bytes. */
bool d7_merkle_digest_equal(d7_merkle_digest left, d7_merkle_digest right);
/* Orders two bytes. */
int d7_merkle_digest_compare(d7_merkle_digest left, d7_merkle_digest right);
/* Parses a digest from its canonical byte form, rejecting a wrong length. */
d7_merkle_status d7_merkle_digest_parse(
    const unsigned char *bytes,
    size_t byte_count,
    d7_merkle_digest *digest);
/* Parses a digest from lowercase hexadecimal. */
d7_merkle_status d7_merkle_digest_parse_hex(
    const char *hex,
    size_t character_count,
    d7_merkle_digest *digest);
/* Writes the digest's canonical bytes into the destination. */
d7_merkle_status d7_merkle_digest_write(
    d7_merkle_digest digest,
    unsigned char *destination,
    size_t destination_size);
/* Writes the digest as lowercase hexadecimal into the destination. */
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

/* Fills a key or value type policy from an encoding codec and its comparison callbacks. */
void d7_merkle_type_policy_init(
    d7_merkle_type_policy *type,
    size_t size,
    const void *type_identity);
/* Fills a codec from its encoding identifier and its encode and decode callbacks. */
void d7_merkle_codec_init(
    d7_merkle_codec *codec,
    const unsigned char *encoding_id,
    size_t encoding_id_size,
    d7_merkle_encode_fn encode,
    d7_merkle_decode_fn decode);
/* Fills the configuration with its defaults, so a caller can set only the fields it cares about. */
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

/* Fills a type policy for signed 32-bit values. */
void d7_merkle_i32_type_policy_init(
    d7_merkle_type_policy *type,
    const void *type_identity);
/* Fills a type policy for signed 64-bit values. */
void d7_merkle_i64_type_policy_init(
    d7_merkle_type_policy *type,
    const void *type_identity);
/* Fills a type policy for nullable UTF-8 text. */
void d7_merkle_nullable_utf8_type_policy_init(
    d7_merkle_type_policy *type,
    const void *type_identity);
/* Fills a type policy for nullable byte strings. */
void d7_merkle_nullable_bytes_type_policy_init(
    d7_merkle_type_policy *type,
    const void *type_identity);
/* Fills a type policy for RFC 4122 UUIDs. */
void d7_merkle_guid_type_policy_init(
    d7_merkle_type_policy *type,
    const void *type_identity);

/* Fills the canonical codec for signed 32-bit values. */
void d7_merkle_i32_codec_init(d7_merkle_codec *codec);
/* Fills the canonical codec for signed 64-bit values. */
void d7_merkle_i64_codec_init(d7_merkle_codec *codec);
/* Fills the canonical codec for nullable UTF-8 text, which encodes a null distinguishably from an
 * empty string. */
void d7_merkle_nullable_utf8_codec_init(d7_merkle_codec *codec);
/* Fills the canonical codec for nullable byte strings, which encodes a null distinguishably from an
 * empty string. */
void d7_merkle_nullable_bytes_codec_init(d7_merkle_codec *codec);
/* Fills the canonical codec for RFC 4122 UUIDs. */
void d7_merkle_guid_codec_init(d7_merkle_codec *codec);

struct d7_merkle_policy_rep;

typedef struct d7_merkle_policy {
    struct d7_merkle_policy_rep *rep;
} d7_merkle_policy;

/* Initializes an empty policy using the supplied policies, which it retains. */
d7_merkle_status d7_merkle_policy_create(
    const d7_merkle_policy_config *config,
    d7_merkle_policy *policy);
/* Initializes a second handle on the same policy version, taking a reference rather than
 * copying. */
d7_merkle_status d7_merkle_policy_copy(
    const d7_merkle_policy *source,
    d7_merkle_policy *destination);
/* Relocates an initialized policy into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void d7_merkle_policy_move(
    d7_merkle_policy *destination,
    d7_merkle_policy *source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void d7_merkle_policy_dispose(d7_merkle_policy *policy);
/* Whether both policies have the same identity, meaning they produce identical digests for
 * identical data. */
bool d7_merkle_policy_same_identity(
    const d7_merkle_policy *left,
    const d7_merkle_policy *right);
/* Whether both policies share a domain, which is the precondition for comparing or merging their
 * trees. */
bool d7_merkle_policy_same_domain(
    const d7_merkle_policy *left,
    const d7_merkle_policy *right);
/* The algorithm identifier this build produces and accepts. Mixed into every digest, so a change of
 * algorithm cannot be mistaken for a change of data. */
const char *d7_merkle_algorithm_id(void);
/* The digest of the policy's domain: its algorithm and its key and value encodings together. Two
 * trees whose domain digests differ describe incomparable data and must not be synchronized. */
d7_merkle_digest d7_merkle_policy_domain_digest(const d7_merkle_policy *policy);
/* The digest of the empty tree under this policy. */
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

/* Initializes the tree in place. */
d7_merkle_status d7_merkle_search_tree_init(
    d7_merkle_search_tree *tree,
    const d7_merkle_policy *policy);
/* Initializes a tree holding the given entries, built in bulk rather than by repeated insertion. */
d7_merkle_status d7_merkle_search_tree_from_array(
    d7_merkle_search_tree *tree,
    const d7_merkle_policy *policy,
    const d7_merkle_search_input *entries,
    size_t entry_count);
/* Initializes a second handle on the same tree version, taking a reference rather than copying. */
d7_merkle_status d7_merkle_search_tree_copy(
    const d7_merkle_search_tree *source,
    d7_merkle_search_tree *destination);
/* Relocates an initialized tree into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void d7_merkle_search_tree_move(
    d7_merkle_search_tree *destination,
    d7_merkle_search_tree *source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void d7_merkle_search_tree_dispose(d7_merkle_search_tree *tree);

/* Whether the tree holds no entries. */
bool d7_merkle_search_tree_empty(const d7_merkle_search_tree *tree);
/* Number of entries in the tree. */
size_t d7_merkle_search_tree_size(const d7_merkle_search_tree *tree);
/* The structure's height. */
size_t d7_merkle_search_tree_height(const d7_merkle_search_tree *tree);
/* How many blocks the tree occupies. */
size_t d7_merkle_search_tree_block_count(const d7_merkle_search_tree *tree);
/* The tree's root digest. Equal digests mean equal contents, which is what makes comparison and
 * synchronization cheap. */
d7_merkle_digest d7_merkle_search_tree_root_hash(const d7_merkle_search_tree *tree);

/* Whether the key is present. */
d7_merkle_status d7_merkle_search_tree_contains_key(
    const d7_merkle_search_tree *tree,
    const void *key,
    bool *found);
/* Borrows the stored entry in place, reporting whether the key was present. The pointer stays valid
 * only while this version does. */
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
/* Produces a tree without that entry. */
d7_merkle_status d7_merkle_search_tree_remove(
    const d7_merkle_search_tree *tree,
    const void *key,
    d7_merkle_search_tree *result);
/* Produces an empty tree retaining the same policies. */
d7_merkle_status d7_merkle_search_tree_clear(
    const d7_merkle_search_tree *tree,
    d7_merkle_search_tree *result);

/* Visitors borrow exact stored representatives. Calls are streaming: failure
 * stops traversal, but side effects of earlier successful visits remain. */
d7_merkle_status d7_merkle_search_tree_visit(
    const d7_merkle_search_tree *tree,
    d7_merkle_entry_visitor visitor,
    void *context);
/* Calls the visitor once per entry in the range. */
d7_merkle_status d7_merkle_search_tree_visit_range(
    const d7_merkle_search_tree *tree,
    const void *minimum_key,
    const void *maximum_key,
    d7_merkle_entry_visitor visitor,
    void *context);

/* Whether both trees hold the same entries. */
bool d7_merkle_search_tree_content_equals(
    const d7_merkle_search_tree *left,
    const d7_merkle_search_tree *right);
/* Whether both maps hold the same entries. */
d7_merkle_status d7_merkle_search_tree_map_equals(
    const d7_merkle_search_tree *left,
    const d7_merkle_search_tree *right,
    bool *equal);
/* Produces the entry-level differences against another tree. Subtrees with equal digests are
 * skipped whole. */
d7_merkle_status d7_merkle_search_tree_diff(
    const d7_merkle_search_tree *left,
    const d7_merkle_search_tree *right,
    d7_merkle_difference_visitor visitor,
    void *context);

/* The root node's address, for tests that a no-op shared rather than copied. */
const void *d7_merkle_search_tree_root_identity(
    const d7_merkle_search_tree *tree);
/* A node's address, for sharing assertions. */
d7_merkle_status d7_merkle_search_tree_node_identity(
    const d7_merkle_search_tree *tree,
    const void *key,
    const void **identity);
/* How many nodes the two versions have in common. */
d7_merkle_status d7_merkle_search_tree_shared_node_count(
    const d7_merkle_search_tree *left,
    const d7_merkle_search_tree *right,
    size_t *shared_count);
/* Calls the visitor over the structure's shape, for tests and diagnostics. */
d7_merkle_status d7_merkle_search_tree_visit_shape(
    const d7_merkle_search_tree *tree,
    d7_merkle_shape_visitor visitor,
    void *context);
/* Calls the visitor once per block. */
d7_merkle_status d7_merkle_search_tree_visit_blocks(
    const d7_merkle_search_tree *tree,
    d7_merkle_block_visitor visitor,
    void *context);
/* Checks the tree's structural invariants, for tests and diagnostics. */
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

/* Initializes an empty tree using the supplied policies, which it retains. */
d7_merkle_status d7_merkle_search_tree_cursor_create(
    const d7_merkle_search_tree *tree,
    size_t position,
    d7_merkle_search_tree_cursor *result);
/* Whether the gap precedes the first entry. */
d7_merkle_status d7_merkle_search_tree_cursor_at_start(
    const d7_merkle_search_tree *tree,
    d7_merkle_search_tree_cursor *result);
/* Whether the gap follows the last entry. */
d7_merkle_status d7_merkle_search_tree_cursor_at_end(
    const d7_merkle_search_tree *tree,
    d7_merkle_search_tree_cursor *result);
/* Initializes a cursor before the first key not less than the probe. */
d7_merkle_status d7_merkle_search_tree_cursor_lower_bound(
    const d7_merkle_search_tree *tree,
    const void *key,
    d7_merkle_search_tree_cursor *result);
/* Initializes a cursor after any key equal to the probe. */
d7_merkle_status d7_merkle_search_tree_cursor_upper_bound(
    const d7_merkle_search_tree *tree,
    const void *key,
    d7_merkle_search_tree_cursor *result);
/* Initializes a cursor at the key, reporting whether it was actually present; on a miss the cursor
 * sits at the insertion point. */
d7_merkle_status d7_merkle_search_tree_cursor_at_key(
    const d7_merkle_search_tree *tree,
    const void *key,
    bool *found,
    d7_merkle_search_tree_cursor *result);
/* Initializes a second cursor at the same position on the same version. */
d7_merkle_status d7_merkle_search_tree_cursor_copy(
    const d7_merkle_search_tree_cursor *cursor,
    d7_merkle_search_tree_cursor *result);
/* Releases the cursor's reference on its version. */
void d7_merkle_search_tree_cursor_destroy(
    d7_merkle_search_tree_cursor *cursor);

/* Number of entries in the tree version the cursor is positioned in. */
size_t d7_merkle_search_tree_cursor_count(
    const d7_merkle_search_tree_cursor *cursor);
/* The cursor's gap position. */
size_t d7_merkle_search_tree_cursor_position(
    const d7_merkle_search_tree_cursor *cursor);
/* Whether the gap precedes the first entry. */
bool d7_merkle_search_tree_cursor_is_at_start(
    const d7_merkle_search_tree_cursor *cursor);
/* Whether the gap follows the last entry. */
bool d7_merkle_search_tree_cursor_is_at_end(
    const d7_merkle_search_tree_cursor *cursor);
/* Reads the entry immediately before the gap, reporting whether one exists. */
bool d7_merkle_search_tree_cursor_try_peek_previous(
    const d7_merkle_search_tree_cursor *cursor,
    d7_merkle_search_entry_ref *entry);
/* Reads the entry immediately after the gap, reporting whether one exists. */
bool d7_merkle_search_tree_cursor_try_peek_next(
    const d7_merkle_search_tree_cursor *cursor,
    d7_merkle_search_entry_ref *entry);

/* Moves the cursor one position earlier. */
d7_merkle_status d7_merkle_search_tree_cursor_move_previous(
    const d7_merkle_search_tree_cursor *cursor,
    d7_merkle_search_tree_cursor *result);
/* Moves the cursor one position later. */
d7_merkle_status d7_merkle_search_tree_cursor_move_next(
    const d7_merkle_search_tree_cursor *cursor,
    d7_merkle_search_tree_cursor *result);
/* Moves the cursor to the given position within the same tree version. */
d7_merkle_status d7_merkle_search_tree_cursor_seek(
    const d7_merkle_search_tree_cursor *cursor,
    size_t position,
    d7_merkle_search_tree_cursor *result);
/* Inserts an entry at the gap, producing a new version the cursor is then positioned in. */
d7_merkle_status d7_merkle_search_tree_cursor_insert(
    const d7_merkle_search_tree_cursor *cursor,
    const void *key,
    const void *value,
    d7_merkle_search_tree_cursor *result);
/* Produces a tree with the key bound to the value, adding or replacing as needed. */
d7_merkle_status d7_merkle_search_tree_cursor_put(
    const d7_merkle_search_tree_cursor *cursor,
    const void *key,
    const void *value,
    d7_merkle_search_tree_cursor *result);
/* Replaces the value of the entry after the gap, producing a new version the cursor is then
 * positioned in. */
d7_merkle_status d7_merkle_search_tree_cursor_set_next_value(
    const d7_merkle_search_tree_cursor *cursor,
    const void *value,
    d7_merkle_search_tree_cursor *result);
/* Removes the entry before the gap, producing a new version the cursor is then positioned in. */
d7_merkle_status d7_merkle_search_tree_cursor_delete_previous(
    const d7_merkle_search_tree_cursor *cursor,
    d7_merkle_search_tree_cursor *result);
/* Removes the entry after the gap, producing a new version the cursor is then positioned in. */
d7_merkle_status d7_merkle_search_tree_cursor_delete_next(
    const d7_merkle_search_tree_cursor *cursor,
    d7_merkle_search_tree_cursor *result);
/* Initializes a handle on the tree version this cursor is positioned in. */
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

/* Fills a verification error with its kind and the digest it concerns. */
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

/* Fills a verification budget with limits suited to ordinary use. The budget is what keeps
 * untrusted input from forcing unbounded work. */
void d7_merkle_verification_budget_init_default(
    d7_merkle_verification_budget *budget);
/* Checks that the budget's limits are internally consistent. */
d7_merkle_status d7_merkle_verification_budget_validate(
    const d7_merkle_verification_budget *budget);

struct d7_merkle_block_rep;

typedef struct d7_merkle_block {
    struct d7_merkle_block_rep *rep;
} d7_merkle_block;

/* Initializes the block in place. */
d7_merkle_status d7_merkle_block_init(
    d7_merkle_digest digest,
    const unsigned char *bytes,
    size_t byte_count,
    const d7_merkle_allocator *allocator,
    d7_merkle_block *block);
/* Initializes a second handle on the same block version, taking a reference rather than copying. */
d7_merkle_status d7_merkle_block_copy(
    const d7_merkle_block *source,
    d7_merkle_block *destination);
/* Relocates an initialized block into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void d7_merkle_block_move(
    d7_merkle_block *destination,
    d7_merkle_block *source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void d7_merkle_block_dispose(d7_merkle_block *block);
/* Whether both blocks hold the same bytes. */
bool d7_merkle_block_equal(
    const d7_merkle_block *left,
    const d7_merkle_block *right);
/* The block's digest, which is also the key it is stored and fetched under. */
d7_merkle_digest d7_merkle_block_digest(const d7_merkle_block *block);
/* The block's encoded bytes. */
const unsigned char *d7_merkle_block_bytes(const d7_merkle_block *block);
/* How many bytes the block occupies. */
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

/* Initializes the store in place. */
void d7_merkle_block_store_init(d7_merkle_block_store *store);
/* Number of blocks in the store. */
d7_merkle_status d7_merkle_block_store_count(
    const d7_merkle_block_store *store,
    size_t *count);
/* Whether the block is present. */
d7_merkle_status d7_merkle_block_store_contains(
    const d7_merkle_block_store *store,
    d7_merkle_digest digest,
    bool *contains);
/* Reads the value stored for the key, reporting whether it was present. */
d7_merkle_status d7_merkle_block_store_try_get(
    const d7_merkle_block_store *store,
    d7_merkle_digest digest,
    bool *found,
    d7_merkle_block *block);
/* Produces a store with the key bound to the value, adding or replacing as needed. */
d7_merkle_status d7_merkle_block_store_put(
    const d7_merkle_block_store *store,
    const d7_merkle_block *block,
    d7_merkle_store_put_result *result);
/* Produces a store without that block. */
d7_merkle_status d7_merkle_block_store_remove(
    const d7_merkle_block_store *store,
    d7_merkle_digest digest,
    bool *removed);
/* Produces an empty store retaining the same policies. */
d7_merkle_status d7_merkle_block_store_clear(
    const d7_merkle_block_store *store);

struct d7_merkle_memory_block_store_rep;

typedef struct d7_merkle_memory_block_store {
    struct d7_merkle_memory_block_store_rep *rep;
} d7_merkle_memory_block_store;

typedef d7_merkle_status (*d7_merkle_digest_visitor)(
    d7_merkle_digest digest,
    void *context);

/* Initializes the store in place. */
d7_merkle_status d7_merkle_memory_block_store_init(
    d7_merkle_memory_block_store *store,
    const d7_merkle_allocator *allocator);
/* Initializes a second handle on the same store version, taking a reference rather than copying. */
d7_merkle_status d7_merkle_memory_block_store_copy(
    const d7_merkle_memory_block_store *source,
    d7_merkle_memory_block_store *destination);
/* Relocates an initialized store into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void d7_merkle_memory_block_store_move(
    d7_merkle_memory_block_store *destination,
    d7_merkle_memory_block_store *source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void d7_merkle_memory_block_store_dispose(
    d7_merkle_memory_block_store *store);
/* Views the in-memory store through the generic block store interface. */
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

/* Initializes the pack in place. */
d7_merkle_status d7_merkle_block_pack_init(
    d7_merkle_identifier algorithm_id,
    d7_merkle_digest domain_digest,
    d7_merkle_digest root_hash,
    const d7_merkle_block *blocks,
    size_t block_count,
    const d7_merkle_allocator *allocator,
    d7_merkle_block_pack *pack,
    d7_merkle_verification_error *error);
/* Initializes a second handle on the same pack version, taking a reference rather than copying. */
d7_merkle_status d7_merkle_block_pack_copy(
    const d7_merkle_block_pack *source,
    d7_merkle_block_pack *destination);
/* Relocates an initialized pack into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void d7_merkle_block_pack_move(
    d7_merkle_block_pack *destination,
    d7_merkle_block_pack *source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void d7_merkle_block_pack_dispose(d7_merkle_block_pack *pack);
/* The algorithm identifier the pack was produced under, checked before anything inside it is
 * trusted. */
d7_merkle_identifier d7_merkle_block_pack_algorithm_id(
    const d7_merkle_block_pack *pack);
/* The domain digest the pack was produced under. */
d7_merkle_digest d7_merkle_block_pack_domain_digest(
    const d7_merkle_block_pack *pack);
/* The root digest the pack's blocks reconstruct. */
d7_merkle_digest d7_merkle_block_pack_root_hash(
    const d7_merkle_block_pack *pack);
/* How many blocks the pack carries. */
size_t d7_merkle_block_pack_block_count(const d7_merkle_block_pack *pack);
/* How many bytes the pack's blocks occupy in total, which a receiver charges against its
 * verification budget. */
uint64_t d7_merkle_block_pack_total_byte_count(
    const d7_merkle_block_pack *pack);
/* Whether the pack carries the block its root digest names. A pack that does not cannot stand on
 * its own. */
bool d7_merkle_block_pack_contains_root_block(
    const d7_merkle_block_pack *pack);
/* The block at the given index. */
const d7_merkle_block *d7_merkle_block_pack_block_at(
    const d7_merkle_block_pack *pack,
    size_t index);

/* Writes every block the tree needs into the store. */
d7_merkle_status d7_merkle_search_tree_save(
    const d7_merkle_search_tree *tree,
    const d7_merkle_block_store *store,
    size_t *added_block_count,
    d7_merkle_verification_error *error);
/* Produces a self-contained pack of the blocks a receiver needs to reconstruct the tree. */
d7_merkle_status d7_merkle_search_tree_export_pack(
    const d7_merkle_search_tree *tree,
    d7_merkle_block_pack *pack);
/* Produces the tree's blocks individually. */
d7_merkle_status d7_merkle_search_tree_export_blocks(
    const d7_merkle_search_tree *tree,
    const d7_merkle_digest *digests,
    size_t digest_count,
    d7_merkle_block_pack *pack,
    d7_merkle_verification_error *error);
/* Reconstructs a tree from a root digest and a store, verifying each block against the digest it
 * was fetched by. */
d7_merkle_status d7_merkle_search_tree_load(
    d7_merkle_digest root_hash,
    const d7_merkle_policy *policy,
    const d7_merkle_block_store *store,
    const d7_merkle_verification_budget *budget,
    d7_merkle_search_tree *tree,
    d7_merkle_verification_error *error);
/* Reconstructs a tree from a pack, verifying the pack's envelope and every block inside it against
 * the caller's budget. */
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

/* Initializes a second handle on the same plan version, taking a reference rather than copying. */
d7_merkle_status d7_merkle_sync_plan_copy(
    const d7_merkle_sync_plan *source,
    d7_merkle_sync_plan *destination);
/* Relocates an initialized plan into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void d7_merkle_sync_plan_move(
    d7_merkle_sync_plan *destination,
    d7_merkle_sync_plan *source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void d7_merkle_sync_plan_dispose(d7_merkle_sync_plan *plan);
/* The algorithm identifier the plan was produced under. */
d7_merkle_identifier d7_merkle_sync_plan_algorithm_id(
    const d7_merkle_sync_plan *plan);
/* The domain digest the plan was produced under. */
d7_merkle_digest d7_merkle_sync_plan_domain_digest(
    const d7_merkle_sync_plan *plan);
/* The root digest of the local tree the plan was computed from. */
d7_merkle_digest d7_merkle_sync_plan_local_root_hash(
    const d7_merkle_sync_plan *plan);
/* The root digest the plan is trying to reach. */
d7_merkle_digest d7_merkle_sync_plan_target_root_hash(
    const d7_merkle_sync_plan *plan);
/* How many blocks the plan asks for. */
size_t d7_merkle_sync_plan_requested_block_count(
    const d7_merkle_sync_plan *plan);
/* The digests of the blocks the plan asks for. */
const d7_merkle_digest *d7_merkle_sync_plan_requested_blocks(
    const d7_merkle_sync_plan *plan);
/* How many blocks the plan had to examine locally, which is what bounds its cost. */
size_t d7_merkle_sync_plan_examined_block_count(
    const d7_merkle_sync_plan *plan);
/* How many bytes the plan had to examine locally. */
uint64_t d7_merkle_sync_plan_examined_byte_count(
    const d7_merkle_sync_plan *plan);
/* Whether the two roots already agree, in which case nothing needs transferring. */
bool d7_merkle_sync_plan_roots_match(const d7_merkle_sync_plan *plan);
/* Whether the plan still needs blocks from the far side. */
bool d7_merkle_sync_plan_requires_blocks(const d7_merkle_sync_plan *plan);

/* Produces the pack answering a synchronization plan's requests. */
d7_merkle_status d7_merkle_search_tree_create_sync_pack(
    const d7_merkle_search_tree *target,
    const d7_merkle_block_store *receiver_store,
    d7_merkle_block_pack *pack);
/* Compares a local tree against a target root digest and reports which blocks are still needed.
 * Subtrees whose digests already match are never requested. */
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

/* Initializes the proof in place. */
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
/* Initializes a second handle on the same proof version, taking a reference rather than copying. */
d7_merkle_status d7_merkle_proof_copy(
    const d7_merkle_proof *source,
    d7_merkle_proof *destination);
/* Relocates an initialized proof into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void d7_merkle_proof_move(
    d7_merkle_proof *destination,
    d7_merkle_proof *source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void d7_merkle_proof_dispose(d7_merkle_proof *proof);
/* Whether the proof asserts presence or absence. */
d7_merkle_proof_kind d7_merkle_proof_kind_value(const d7_merkle_proof *proof);
/* The root digest the proof is against. */
d7_merkle_digest d7_merkle_proof_root_hash(const d7_merkle_proof *proof);
/* The domain digest the proof was produced under. */
d7_merkle_digest d7_merkle_proof_domain_digest(const d7_merkle_proof *proof);
/* The algorithm identifier the proof was produced under. */
d7_merkle_identifier d7_merkle_proof_algorithm_id(const d7_merkle_proof *proof);
/* The key or key range the proof answers. */
const unsigned char *d7_merkle_proof_query(const d7_merkle_proof *proof);
/* How many bytes the query occupies, charged against the verification budget. */
size_t d7_merkle_proof_query_byte_count(const d7_merkle_proof *proof);
/* How many blocks the proof's chain contains. */
size_t d7_merkle_proof_step_count(const d7_merkle_proof *proof);
/* The block at the given step of the chain. */
const d7_merkle_block *d7_merkle_proof_step_block(
    const d7_merkle_proof *proof,
    size_t step_index);
/* Which of the step's children the proof expands, so a verifier knows which subtrees it must
 * descend into and which it may take on their digests. */
const size_t *d7_merkle_proof_step_expanded_children(
    const d7_merkle_proof *proof,
    size_t step_index,
    size_t *expanded_child_count);
/* How many bytes the proof occupies in total. */
uint64_t d7_merkle_proof_total_byte_count(const d7_merkle_proof *proof);

typedef struct d7_merkle_proof_verification_result {
    bool is_valid;
    d7_merkle_verification_failure_kind failure_kind;
    bool has_computed_root_hash;
    d7_merkle_digest computed_root_hash;
    size_t verified_block_count;
    uint64_t verified_byte_count;
} d7_merkle_proof_verification_result;

/* Produces a proof of the key's presence or absence against the tree's root digest. */
d7_merkle_status d7_merkle_search_tree_create_proof(
    const d7_merkle_search_tree *tree,
    const void *key,
    d7_merkle_proof *proof);
/* Produces a proof covering a key range against the tree's root digest. */
d7_merkle_status d7_merkle_search_tree_create_range_proof(
    const d7_merkle_search_tree *tree,
    const void *minimum_key,
    const void *maximum_key,
    d7_merkle_proof *proof);
/* Checks a proof against a root digest and the caller's budget, so a crafted proof cannot force
 * unbounded work. */
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

/* Initializes a second handle on the same merge result version, taking a reference rather than
 * copying. */
d7_merkle_status d7_merkle_three_way_merge_result_copy(
    const d7_merkle_three_way_merge_result *source,
    d7_merkle_three_way_merge_result *destination);
/* Relocates an initialized merge result into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void d7_merkle_three_way_merge_result_move(
    d7_merkle_three_way_merge_result *destination,
    d7_merkle_three_way_merge_result *source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void d7_merkle_three_way_merge_result_dispose(
    d7_merkle_three_way_merge_result *result);
/* Whether every conflict was resolved. A single unresolved key fails the whole merge rather than
 * producing a partly merged tree. */
bool d7_merkle_three_way_merge_result_success(
    const d7_merkle_three_way_merge_result *result);
/* Takes a handle on the merged tree. */
d7_merkle_status d7_merkle_three_way_merge_result_copy_tree(
    const d7_merkle_three_way_merge_result *result,
    d7_merkle_search_tree *tree);
/* How many keys the resolver declined to settle. */
size_t d7_merkle_three_way_merge_result_conflict_count(
    const d7_merkle_three_way_merge_result *result);
/* The unresolved conflict at the given index. */
d7_merkle_status d7_merkle_three_way_merge_result_conflict_at(
    const d7_merkle_three_way_merge_result *result,
    size_t index,
    d7_merkle_three_way_merge_conflict_ref *conflict);

/* Three-way merges two trees against their common ancestor, asking the resolver to settle each
 * conflicting key. */
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
