/*
 * A fully branching persistent insertion-only connectivity forest that reports the first ancestor
 * version in which two vertices became connected.
 *
 * Vertices are the fixed integer universe 0 .. vertex_count - 1. Each successful link performs union
 * by size without path compression and labels the one new root-parent edge with the child version.
 * A redundant (already connected) link still creates a child history version, but shares the
 * complete connectivity index.
 *
 * The parent cells live sparsely in a CHAMP map. An absent cell denotes a singleton root, so
 * construction is O(1) rather than O(vertex_count) and costs the same for a universe of two vertices
 * and for one of INT32_MAX. Union by size limits every parent path to O(log n) cells.
 * first_connected compares the two current parent paths and takes the latest edge-version strictly
 * below their forest lowest common ancestor, requiring O(w log n) time. It does not search the
 * version history, so its cost is independent of history depth.
 *
 * Throughout this header w denotes the cost of one cell lookup or insertion in the CHAMP map. This
 * port keys the map on the integer vertex through a bijective 32-bit mix, so distinct vertices never
 * share a hash and never form a collision bucket; the trie therefore visits at most seven levels and
 * w is bounded worst-case, matching the managed reference. Every bound stated here is worst-case,
 * never amortized.
 *
 * Versions and version tokens are immutable snapshots safe for concurrent readers. Deletion,
 * confluent merging, retroactive updates, and path compression are deliberately outside this type's
 * contract. The O(log n) parent-path factor is worst-case logarithmic in component size rather than
 * the inverse-Ackermann amortized bound of an ephemeral linear-history union-find.
 */

#ifndef DURABLE7_HAMT_C_PERSISTENT_ANCESTRAL_CONNECTION_FOREST_H
#define DURABLE7_HAMT_C_PERSISTENT_ANCESTRAL_CONNECTION_FOREST_H

#include <durable7/hamt/hamt.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The shared, reference-counted representation behind one history version token. */
typedef struct d7_hamt_ancestral_connection_version_rep
    d7_hamt_ancestral_connection_version_rep;

/* Identifies one immutable version in a connection-forest history tree.
 *
 * Tokens use identity, not structural equality: two versions at equal depths on sibling branches are
 * never the same version. The parent chain contains history identities, not mutable connectivity
 * state. Every initialized handle must be released with
 * d7_hamt_ancestral_connection_version_destroy. */
typedef struct d7_hamt_ancestral_connection_version {
    d7_hamt_ancestral_connection_version_rep* rep;
} d7_hamt_ancestral_connection_version;

/* One immutable connection-forest version: a connectivity snapshot and one node in a version tree.
 * The cells map holds only the parent records touched by a successful union; every other vertex is
 * an implicit singleton root. */
typedef struct d7_hamt_ancestral_connection_forest {
    int32_t vertex_count;
    int32_t component_count;
    d7_hamt_map cells;
    d7_hamt_ancestral_connection_version version;
} d7_hamt_ancestral_connection_forest;

/* Statistics recovered by a complete representation audit. */
typedef struct d7_hamt_ancestral_connection_forest_statistics {
    /* The fixed vertex-universe size. */
    int32_t vertex_count;
    /* The number of connected components, recounted from the parent forest. */
    int32_t component_count;
    /* The number of explicitly stored parent cells; absent cells are singleton roots. */
    size_t stored_cell_count;
    /* The longest parent path found, in edges. Bounded by floor(log2(vertex_count)). */
    size_t maximum_parent_path_length;
    /* The number of link events from the history root to the validated version. */
    uint64_t history_depth;
} d7_hamt_ancestral_connection_forest_statistics;

/* Initializes a second handle on the same version, taking a reference rather than copying. O(1). */
d7_hamt_status d7_hamt_ancestral_connection_version_clone(
    const d7_hamt_ancestral_connection_version* source,
    d7_hamt_ancestral_connection_version* destination);
/* Relocates an initialized version into another variable, leaving the source uninitialized. */
void d7_hamt_ancestral_connection_version_move(
    d7_hamt_ancestral_connection_version* destination,
    d7_hamt_ancestral_connection_version* source);
/* Releases this handle's reference. Other versions sharing the same history stay valid. O(1)
 * amortized over the released chain; the parent chain is released iteratively, so a history
 * hundreds of thousands of links deep cannot overflow the stack. */
void d7_hamt_ancestral_connection_version_destroy(
    d7_hamt_ancestral_connection_version* version);
/* Whether both handles denote the very same version. Equal depths on sibling branches are distinct
 * versions, so this is identity and not structural equality. O(1). */
bool d7_hamt_ancestral_connection_version_same(
    const d7_hamt_ancestral_connection_version* left,
    const d7_hamt_ancestral_connection_version* right);
/* The number of link events from the history root. O(1). */
uint64_t d7_hamt_ancestral_connection_version_depth(
    const d7_hamt_ancestral_connection_version* version);
/* Borrows this version's parent, or NULL at the history root. The borrow stays valid while the
 * source handle is retained. O(1). */
const d7_hamt_ancestral_connection_version* d7_hamt_ancestral_connection_version_parent_ref(
    const d7_hamt_ancestral_connection_version* version);
/* Borrows the root identity of this version tree, which is never NULL for an initialized handle.
 * The borrow stays valid while the source handle is retained. O(1). */
const d7_hamt_ancestral_connection_version* d7_hamt_ancestral_connection_version_root_ref(
    const d7_hamt_ancestral_connection_version* version);
/* Writes an independently owned handle on this version's parent, reporting whether there was one.
 * The caller releases it with d7_hamt_ancestral_connection_version_destroy. O(1). */
bool d7_hamt_ancestral_connection_version_try_copy_parent(
    const d7_hamt_ancestral_connection_version* version,
    d7_hamt_ancestral_connection_version* parent);
/* Writes an independently owned handle on the root identity of this version tree. The caller
 * releases it with d7_hamt_ancestral_connection_version_destroy. O(1). */
d7_hamt_status d7_hamt_ancestral_connection_version_copy_root(
    const d7_hamt_ancestral_connection_version* version,
    d7_hamt_ancestral_connection_version* root);

/* Initializes an edgeless history root over vertices 0 .. vertex_count - 1, in which every vertex is
 * its own component. O(1) for any universe size, because a singleton is represented by the absence
 * of a cell. Rejects a negative vertex_count with D7_HAMT_INVALID_ARGUMENT. */
d7_hamt_status d7_hamt_ancestral_connection_forest_init(
    d7_hamt_ancestral_connection_forest* forest,
    int32_t vertex_count);
/* Initializes a second handle on the same forest version, taking a reference rather than copying.
 * O(1). */
d7_hamt_status d7_hamt_ancestral_connection_forest_clone(
    const d7_hamt_ancestral_connection_forest* source,
    d7_hamt_ancestral_connection_forest* destination);
/* Relocates an initialized forest into another variable, leaving the source uninitialized. A handle
 * whose nested handles point at policy state it embeds must be moved rather than copied bytewise. */
void d7_hamt_ancestral_connection_forest_move(
    d7_hamt_ancestral_connection_forest* destination,
    d7_hamt_ancestral_connection_forest* source);
/* Releases this handle's reference. Other versions sharing the same cells stay valid. */
void d7_hamt_ancestral_connection_forest_destroy(
    d7_hamt_ancestral_connection_forest* forest);

/* The fixed number of vertices in this forest. O(1). */
int32_t d7_hamt_ancestral_connection_forest_vertex_count(
    const d7_hamt_ancestral_connection_forest* forest);
/* The number of connected components in this version. O(1). */
int32_t d7_hamt_ancestral_connection_forest_component_count(
    const d7_hamt_ancestral_connection_forest* forest);
/* Borrows the identity token for this history version. The borrow stays valid while the forest
 * handle is retained. O(1). */
const d7_hamt_ancestral_connection_version* d7_hamt_ancestral_connection_forest_version_ref(
    const d7_hamt_ancestral_connection_forest* forest);
/* Writes an independently owned handle on this version's identity token. The caller releases it with
 * d7_hamt_ancestral_connection_version_destroy. O(1). */
d7_hamt_status d7_hamt_ancestral_connection_forest_copy_version(
    const d7_hamt_ancestral_connection_forest* forest,
    d7_hamt_ancestral_connection_version* version);

/* Reads the current union-find representative of a vertex. O(w log n) worst-case. The representative
 * is selected by deterministic union-by-size history and is an implementation artifact: sibling
 * versions need not agree on it. */
d7_hamt_status d7_hamt_ancestral_connection_forest_find(
    const d7_hamt_ancestral_connection_forest* forest,
    int32_t vertex,
    int32_t* representative);
/* Reports whether two vertices are connected, that is, whether they have the same current root.
 * O(w log n) worst-case. */
d7_hamt_status d7_hamt_ancestral_connection_forest_connected(
    const d7_hamt_ancestral_connection_forest* forest,
    int32_t left,
    int32_t right,
    bool* connected);
/* Reads the number of vertices in a vertex's connected component, which is the union-by-size count
 * cached on the component's current root and 1 for an isolated vertex. One parent-path walk plus a
 * cached read; no path compression is performed. O(w log n) worst-case. */
d7_hamt_status d7_hamt_ancestral_connection_forest_component_size(
    const d7_hamt_ancestral_connection_forest* forest,
    int32_t vertex,
    int32_t* size);
/* Produces a child history version containing an undirected connection between two vertices.
 *
 * The child version is always distinct. When the endpoints were already connected its connectivity
 * cells are shared unchanged, which
 * d7_hamt_ancestral_connection_forest_debug_shares_connectivity_root can confirm; otherwise exactly
 * one union-by-size root edge is added and labelled with the child version. On a size tie the first
 * endpoint's root stays the representative.
 *
 * O(w log n) time and O(w) new CHAMP nodes for a successful union; O(w log n) time and O(1) space
 * when redundant. The result may alias the source. Fails with D7_HAMT_OVERFLOW when the history
 * depth counter saturates, and with D7_HAMT_OUT_OF_MEMORY when allocation fails. On failure every
 * output and input is left untouched. */
d7_hamt_status d7_hamt_ancestral_connection_forest_link(
    const d7_hamt_ancestral_connection_forest* forest,
    int32_t left,
    int32_t right,
    d7_hamt_ancestral_connection_forest* result);
/* Borrows the earliest version on this version's root-to-current history in which two vertices were
 * connected, writing NULL when they are disconnected in this version. A vertex is connected to
 * itself at the history root.
 *
 * The answer is computed from the two current parent paths as the latest union-edge version strictly
 * below their forest lowest common ancestor; the version history is never searched. The result is
 * never a version on another branch, and a later merge of the pair's component into a bigger one
 * never replaces the pair's earlier witness. The borrow stays valid while the forest handle is
 * retained.
 *
 * O(w log n) worst-case time and O(log n) temporary path space; independent of history depth. */
d7_hamt_status d7_hamt_ancestral_connection_forest_first_connected_ref(
    const d7_hamt_ancestral_connection_forest* forest,
    int32_t left,
    int32_t right,
    const d7_hamt_ancestral_connection_version** version);
/* Writes an independently owned handle on the first-connection version, reporting through connected
 * whether the pair is connected at all. The version is written only when connected is true, and the
 * caller releases it with d7_hamt_ancestral_connection_version_destroy. Same bounds and semantics as
 * d7_hamt_ancestral_connection_forest_first_connected_ref. */
d7_hamt_status d7_hamt_ancestral_connection_forest_first_connected_copy(
    const d7_hamt_ancestral_connection_forest* forest,
    int32_t left,
    int32_t right,
    bool* connected,
    d7_hamt_ancestral_connection_version* version);

/* Audits sparse-cell canonicality, current root sizes, the resulting union-by-size height ceiling,
 * union-tag ancestry and order, and the cached component count, reporting the verdict through valid
 * and, when the verdict holds and statistics is non-NULL, the recovered statistics.
 *
 * O(H + m w log n), where H is the history depth and m is the explicit-cell count. The audit builds
 * scratch indexes, so it returns D7_HAMT_OUT_OF_MEMORY rather than a false verdict when an
 * allocation fails; in that case no output is written. For tests and diagnostics. */
d7_hamt_status d7_hamt_ancestral_connection_forest_debug_validate(
    const d7_hamt_ancestral_connection_forest* forest,
    bool* valid,
    d7_hamt_ancestral_connection_forest_statistics* statistics);
/* Whether two versions retain the exact same sparse connectivity index, so neither can observe a
 * union recorded in the other. A representation check used to confirm a no-op shared rather than
 * copied, not an equality test. O(1). */
bool d7_hamt_ancestral_connection_forest_debug_shares_connectivity_root(
    const d7_hamt_ancestral_connection_forest* left,
    const d7_hamt_ancestral_connection_forest* right);
/* The connectivity index's root node address, for tests that a no-op shared rather than copied. */
const void* d7_hamt_ancestral_connection_forest_debug_root_identity(
    const d7_hamt_ancestral_connection_forest* forest);

#ifdef __cplusplus
}
#endif

#endif
