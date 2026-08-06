/*
 * The append-only incremental level-ancestor arena shared by the ancestry-backed collections.
 *
 * An incremental level-ancestor arena is a monotone forest that grows one leaf at a time and answers
 * "which ancestor of this node sits at absolute depth d?". Nodes are named by stable integer
 * handles, never move, and are never recycled, so a consumer can retain a handle forever and keep
 * reading the same immutable answer. A leaf may be added below any previously published node, not
 * only below the most recently added one, so an old handle simply starts a new branch; that is what
 * makes the arena a persistence substrate rather than a log. The distinguished bottom node sits at
 * depth -1, carries no value, and is published by construction; every labeled node has a depth of
 * zero or more.
 *
 * ft_incremental_ancestor_arena is the service handle, and its vtable is the extension seam: a
 * caller may supply its own backend, exactly as it supplies its own allocator and value callbacks
 * elsewhere in this workspace. The shipped backend is the Myers two-link scheme, created by
 * ft_incremental_ancestor_myers_arena_create. It stores one parent link plus one coalesced jump link
 * per immutable node, so a leaf addition does O(1) link work and an ancestor query follows O(log M)
 * parent/jump links in the worst case after M published nodes. Nodes live in blocks of sizes
 * 1, 3, 5, ..., whose square boundaries give O(1) addressing and O(sqrt(M)) unused slots. Block and
 * directory allocation makes an addition O(1) amortized rather than worst case: a single addition
 * at a square boundary pays Theta(sqrt(M)) to allocate the next odd block, the accepted contract
 * of the shared odd-block representation (deamortizing it was considered and declined; Haskell's
 * arena-free port exceeds this profile at O(1) worst case). Total arena storage
 * is O(M), and every published value stays reachable until the arena is disposed.
 *
 * An Alstrup-Holm incremental level-ancestor backend would supply O(1) worst-case addition and
 * O(1) worst-case queries in linear space; no such backend is included here. Nothing in this header
 * upgrades the Myers amortized/logarithmic bounds to worst-case ones.
 *
 * Concurrency. The C# arena serializes every read and write under one private lock; C11 has no
 * portable mutex without <threads.h>, so this port makes the weaker, explicit promise instead: an
 * arena is single-threaded unless the caller synchronizes it. Every operation declared here,
 * including the reads and the statistics snapshot, mutates shared arena state and must be treated as
 * a write. Published nodes are immutable, so a caller that serializes arena access externally gets
 * the same stable-handle semantics the other ports document; no lock-free progress is claimed in any
 * port. Handle reference counts are maintained atomically, so a handle may be disposed on a thread
 * other than the one that created it, but that does not make the arena itself concurrent.
 */

#ifndef DURABLE7_FINGER_TREE_C_INCREMENTAL_ANCESTOR_H
#define DURABLE7_FINGER_TREE_C_INCREMENTAL_ANCESTOR_H

#include <durable7/finger_tree/fingertree.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A stable arena-relative node handle. Handles are capabilities owned by one arena: range
 * validation cannot identify an in-range handle copied from a different arena, so callers must
 * preserve arena ownership themselves. */
typedef size_t ft_incremental_ancestor_node;

/* An absolute node depth. The bottom node has depth -1; every labeled node has depth zero or
 * more. */
typedef ptrdiff_t ft_incremental_ancestor_depth;

/* The distinguished unlabelled node every arena publishes at construction. */
#define FT_INCREMENTAL_ANCESTOR_BOTTOM ((ft_incremental_ancestor_node)0)
/* The depth of the distinguished unlabelled node. */
#define FT_INCREMENTAL_ANCESTOR_BOTTOM_DEPTH ((ft_incremental_ancestor_depth)-1)

typedef ft_status (*ft_incremental_ancestor_value_copy_fn)(
    void* destination,
    const void* source,
    void* context);
typedef void (*ft_incremental_ancestor_value_destroy_fn)(void* value, void* context);
typedef void* (*ft_incremental_ancestor_allocate_fn)(size_t size, void* context);
typedef void (*ft_incremental_ancestor_deallocate_fn)(void* allocation, void* context);

typedef struct ft_incremental_ancestor_allocator {
    ft_incremental_ancestor_allocate_fn allocate;
    ft_incremental_ancestor_deallocate_fn deallocate;
    void* context;
} ft_incremental_ancestor_allocator;

/* value_type_identity is a required, stable, non-null caller-owned tag. Copy constructs an owned
 * value in uninitialized storage; on failure it must leave that storage ownership-free. Destroy and
 * allocator hooks must return normally. Hooks must not reenter an in-flight operation through the
 * same policy/arena handles. */
typedef struct ft_incremental_ancestor_policy_config {
    size_t value_size;
    const void* value_type_identity;
    ft_incremental_ancestor_value_copy_fn copy;
    ft_incremental_ancestor_value_destroy_fn destroy;
    void* callback_context;
    ft_incremental_ancestor_allocator allocator;
} ft_incremental_ancestor_policy_config;

/* The shared, reference-counted representation behind an incremental-ancestor policy. */
typedef struct ft_incremental_ancestor_policy_rep ft_incremental_ancestor_policy_rep;

/* Policy identity gates the operations that require it. Copy preserves identity; independently
 * recreating an equivalent config deliberately does not. */
typedef struct ft_incremental_ancestor_policy {
    ft_incremental_ancestor_policy_rep* rep;
} ft_incremental_ancestor_policy;

/* Fills the configuration with its defaults, so a caller can set only the fields it cares about. */
void ft_incremental_ancestor_policy_config_init(
    ft_incremental_ancestor_policy_config* config,
    size_t value_size,
    const void* value_type_identity,
    void* callback_context);
/* Initializes a policy from the supplied configuration, which it copies. */
ft_status ft_incremental_ancestor_policy_create(
    ft_incremental_ancestor_policy* policy,
    const ft_incremental_ancestor_policy_config* config);
/* Initializes a second handle on the same policy, taking a reference rather than copying. */
ft_status ft_incremental_ancestor_policy_copy(
    const ft_incremental_ancestor_policy* source,
    ft_incremental_ancestor_policy* destination);
/* Relocates an initialized policy into another variable, leaving the source uninitialized. A handle
 * whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_incremental_ancestor_policy_move(
    ft_incremental_ancestor_policy* destination,
    ft_incremental_ancestor_policy* source);
/* Releases this handle's reference. Other handles on the same policy stay valid. */
void ft_incremental_ancestor_policy_dispose(ft_incremental_ancestor_policy* policy);
/* Whether both handles denote the same policy identity. */
bool ft_incremental_ancestor_policy_same(
    const ft_incremental_ancestor_policy* left,
    const ft_incremental_ancestor_policy* right);
/* Reads the configuration back out, so a consumer that only holds an arena can destroy the owned
 * values ..._value_copy hands it and size its own storage. This read is O(1). */
ft_status ft_incremental_ancestor_policy_get_config(
    const ft_incremental_ancestor_policy* policy,
    ft_incremental_ancestor_policy_config* config);

/* The extension seam: the operations a backend must provide, all of which receive the backend
 * pointer supplied at creation. This is the C analogue of the arena interface the other ports
 * expose, and the type-erased value contract is the workspace's usual one.
 *
 * A backend must guarantee that a successful add_leaf publishes a fresh, never-recycled handle whose
 * immutable parent is the supplied handle and whose depth is exactly one greater; that depth,
 * parent, value_ref, and ancestor answers for a published node never change; and that a failed
 * addition publishes no partially initialized node and leaves every counter it exposes untouched.
 * add_leaf receives a borrowed value and must construct its own owned copy through the policy.
 *
 * depth, parent, and value_ref must take O(1) time: the bounds documented by the consuming
 * collections assume those primitive reads are constant-time. A backend determines the bounds those
 * collections inherit. Let U be leaf-add time and Q be level-ancestor query time after M published
 * nodes; a backend with U = Q = O(1) worst case gives the consumers their optimal bound, which
 * neither binary lifting nor the shipped Myers backend satisfies.
 *
 * Every hook must report an unpublished handle as FT_STATUS_OUT_OF_RANGE, the bottom node's absent
 * parent or value as FT_STATUS_OUT_OF_RANGE, a depth outside -1 through the node's own depth as
 * FT_STATUS_OUT_OF_RANGE, and depth, node-count, or jump-distance exhaustion as FT_STATUS_OVERFLOW.
 * destroy is optional and releases the backend when the last arena handle is disposed. */
typedef struct ft_incremental_ancestor_arena_vtable {
    ft_status (*bottom)(void* backend, ft_incremental_ancestor_node* bottom);
    ft_status (*published_node_count)(void* backend, size_t* count);
    ft_status (*add_leaf)(
        void* backend,
        ft_incremental_ancestor_node parent,
        const void* value,
        ft_incremental_ancestor_node* leaf);
    ft_status (*depth)(
        void* backend,
        ft_incremental_ancestor_node node,
        ft_incremental_ancestor_depth* depth);
    ft_status (*parent)(
        void* backend,
        ft_incremental_ancestor_node node,
        ft_incremental_ancestor_node* parent);
    ft_status (*ancestor_at_depth)(
        void* backend,
        ft_incremental_ancestor_node node,
        ft_incremental_ancestor_depth depth,
        ft_incremental_ancestor_node* ancestor);
    ft_status (*value_ref)(
        void* backend,
        ft_incremental_ancestor_node node,
        const void** value_ref);
    void (*destroy)(void* backend);
} ft_incremental_ancestor_arena_vtable;

/* A caller-supplied backend. The vtable is copied into the arena; the backend pointer is retained
 * as given and released through the vtable's destroy hook. */
typedef struct ft_incremental_ancestor_arena_config {
    const ft_incremental_ancestor_arena_vtable* vtable;
    void* backend;
} ft_incremental_ancestor_arena_config;

/* The shared, reference-counted representation behind an arena handle. Opaque: handles are the
 * API. */
typedef struct ft_incremental_ancestor_arena_rep ft_incremental_ancestor_arena_rep;

typedef struct ft_incremental_ancestor_arena {
    ft_incremental_ancestor_arena_rep* rep;
} ft_incremental_ancestor_arena;

/* Deterministic representation and traversal counters for one Myers arena. published_node_count
 * excludes the bottom node; block_count and allocated_slot_count include its physical storage.
 * ancestor_query_count and total_ancestor_hop_count saturate at UINT64_MAX rather than wrapping; the
 * remaining counters are bounded by the arena's own size and cannot saturate. These counters support
 * representation tests and observation; they do not by themselves prove an asymptotic bound. */
typedef struct ft_incremental_ancestor_myers_statistics {
    size_t published_node_count;
    size_t block_count;
    uint64_t allocated_slot_count;
    uint64_t add_leaf_count;
    uint64_t ancestor_query_count;
    size_t last_ancestor_hop_count;
    size_t maximum_ancestor_hop_count;
    uint64_t total_ancestor_hop_count;
} ft_incremental_ancestor_myers_statistics;

/* Initializes an arena over a caller-supplied backend, retaining the policy. The vtable's hooks are
 * copied by value, so the vtable itself need not outlive the call; the backend pointer must stay
 * valid until the arena releases it through the vtable's destroy hook. On failure the arena takes no
 * ownership at all and the caller still owns the backend. This creation is O(1). */
ft_status ft_incremental_ancestor_arena_create(
    ft_incremental_ancestor_arena* arena,
    const ft_incremental_ancestor_policy* policy,
    const ft_incremental_ancestor_arena_config* config);
/* Initializes an arena over the shipped Myers backend, containing only its unlabelled bottom node
 * and retaining the policy. This creation is O(1). */
ft_status ft_incremental_ancestor_myers_arena_create(
    ft_incremental_ancestor_arena* arena,
    const ft_incremental_ancestor_policy* policy);
/* Initializes a second handle on the same arena, taking a reference rather than copying. Both
 * handles then observe every subsequent addition. */
ft_status ft_incremental_ancestor_arena_copy(
    const ft_incremental_ancestor_arena* source,
    ft_incremental_ancestor_arena* destination);
/* Relocates an initialized arena into another variable, leaving the source uninitialized. A handle
 * whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_incremental_ancestor_arena_move(
    ft_incremental_ancestor_arena* destination,
    ft_incremental_ancestor_arena* source);
/* Releases this handle's reference. The backend, every published node, and every published value
 * are released once the last handle goes away. */
void ft_incremental_ancestor_arena_dispose(ft_incremental_ancestor_arena* arena);
/* Whether both handles denote the same arena identity. Node handles are meaningful only to the arena
 * that issued them, so this is the check a consumer uses before mixing two handles' nodes. */
bool ft_incremental_ancestor_arena_same(
    const ft_incremental_ancestor_arena* left,
    const ft_incremental_ancestor_arena* right);
/* The shared representation's address, for tests that a copy shared the arena rather than
 * duplicating it. */
const void* ft_incremental_ancestor_arena_root_identity(
    const ft_incremental_ancestor_arena* arena);
/* Initializes a second handle on the arena's policy, taking a reference rather than copying. */
ft_status ft_incremental_ancestor_arena_get_policy(
    const ft_incremental_ancestor_arena* arena,
    ft_incremental_ancestor_policy* policy);

/* Reads the distinguished unlabelled node at depth -1. This read is O(1). */
ft_status ft_incremental_ancestor_arena_bottom(
    const ft_incremental_ancestor_arena* arena,
    ft_incremental_ancestor_node* bottom);
/* Reads the number of labelled nodes published by this arena, excluding the bottom node. This read
 * is O(1). */
ft_status ft_incremental_ancestor_arena_published_node_count(
    const ft_incremental_ancestor_arena* arena,
    size_t* count);
/* Adds a labelled leaf below the bottom node or a labelled node owned by this arena, publishing the
 * caller's value as an owned copy made through the policy. On the Myers backend this addition is
 * O(1) amortized over the block and directory allocations; a block-boundary call pays
 * Theta(sqrt(M)) for the new odd block. Outputs are written only on success; a
 * failed addition leaves the arena exactly as it was, including its counters. */
ft_status ft_incremental_ancestor_arena_add_leaf(
    const ft_incremental_ancestor_arena* arena,
    ft_incremental_ancestor_node parent,
    const void* value,
    ft_incremental_ancestor_node* leaf);
/* Reads a node's immutable absolute depth; the bottom node has depth -1. This read is O(1). */
ft_status ft_incremental_ancestor_arena_depth(
    const ft_incremental_ancestor_arena* arena,
    ft_incremental_ancestor_node node,
    ft_incremental_ancestor_depth* depth);
/* Reads a labelled node's parent, which is the bottom node when the node has depth zero. This read
 * is O(1). The bottom node has no parent and is rejected with FT_STATUS_OUT_OF_RANGE. */
ft_status ft_incremental_ancestor_arena_parent(
    const ft_incremental_ancestor_arena* arena,
    ft_incremental_ancestor_node node,
    ft_incremental_ancestor_node* parent);
/* Reads the unique ancestor of a node at an absolute depth from -1 through the node's own depth. On
 * the Myers backend this query follows O(log M) parent/jump links in the worst case after M
 * published nodes. */
ft_status ft_incremental_ancestor_arena_ancestor_at_depth(
    const ft_incremental_ancestor_arena* arena,
    ft_incremental_ancestor_node node,
    ft_incremental_ancestor_depth depth,
    ft_incremental_ancestor_node* ancestor);
/* On success, value_ref borrows the value stored for a labelled node and remains valid while the
 * arena is retained. This read is O(1). The bottom node has no value and is rejected with
 * FT_STATUS_OUT_OF_RANGE. */
ft_status ft_incremental_ancestor_arena_value_ref(
    const ft_incremental_ancestor_arena* arena,
    ft_incremental_ancestor_node node,
    const void** value_ref);
/* On success, value is an independently owned copy that must be destroyed with the policy's destroy
 * callback. This read is O(1) plus the policy's copy callback. */
ft_status ft_incremental_ancestor_arena_value_copy(
    const ft_incremental_ancestor_arena* arena,
    ft_incremental_ancestor_node node,
    void* value);

/* Reads the Myers backend's deterministic counters in O(1) time. An arena over a caller-supplied
 * backend has no such representation and is rejected with FT_STATUS_INVALID_ARGUMENT. */
ft_status ft_incremental_ancestor_myers_arena_get_statistics(
    const ft_incremental_ancestor_arena* arena,
    ft_incremental_ancestor_myers_statistics* statistics);

#ifdef __cplusplus
}
#endif

#endif
