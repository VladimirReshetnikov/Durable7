/*
 * A persistent directed graph over hashed vertices.
 *
 * Successors and predecessors are both indexed, so neither direction is a scan. Every operation
 * returns a new version and leaves its inputs valid, sharing unchanged structure, so an edit copies
 * a path rather than the whole collection.
 */

#ifndef DURABLE7_HAMT_PERSISTENT_DIRECTED_GRAPH_H
#define DURABLE7_HAMT_PERSISTENT_DIRECTED_GRAPH_H

#include <durable7/hamt/persistent_relation.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct d7_hamt_directed_graph {
    d7_hamt_set vertices;
    d7_hamt_relation edges;
} d7_hamt_directed_graph;

typedef void (*d7_hamt_directed_graph_edge_visit_fn)(
    const void* source,
    const void* target,
    void* context);

/* Initializes the graph in place. */
d7_hamt_status d7_hamt_directed_graph_init(
    d7_hamt_directed_graph* graph,
    const d7_hamt_set_policy* vertex_policy);
/* Initializes a second handle on the same graph version, taking a reference rather than copying. */
d7_hamt_status d7_hamt_directed_graph_clone(
    const d7_hamt_directed_graph* source,
    d7_hamt_directed_graph* destination);
/* Relocates an initialized graph into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void d7_hamt_directed_graph_move(
    d7_hamt_directed_graph* destination,
    d7_hamt_directed_graph* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void d7_hamt_directed_graph_destroy(d7_hamt_directed_graph* graph);

/* Number of vertices. */
size_t d7_hamt_directed_graph_vertex_count(const d7_hamt_directed_graph* graph);
/* Number of directed edges. */
int64_t d7_hamt_directed_graph_edge_count(const d7_hamt_directed_graph* graph);
/* Whether the graph holds no vertices. */
bool d7_hamt_directed_graph_empty(const d7_hamt_directed_graph* graph);
/* Whether the vertex is present. */
bool d7_hamt_directed_graph_contains_vertex(
    const d7_hamt_directed_graph* graph,
    const void* vertex);
/* Reads the stored vertex representative, reporting whether it was present. */
bool d7_hamt_directed_graph_try_get_vertex(
    const d7_hamt_directed_graph* graph,
    const void* equal_vertex,
    const void** actual_vertex);
/* Whether the directed edge is present. */
bool d7_hamt_directed_graph_contains_edge(
    const d7_hamt_directed_graph* graph,
    const void* source,
    const void* target);
/* Reads the set of vertices the given vertex points at, reporting whether the vertex was
 * present. */
bool d7_hamt_directed_graph_try_get_successors(
    const d7_hamt_directed_graph* graph,
    const void* vertex,
    const d7_hamt_set** successors);
/* Reads the set of vertices pointing at the given vertex, reporting whether the vertex was present.
 * Maintained as its own index, so this is a lookup rather than a scan. */
bool d7_hamt_directed_graph_try_get_predecessors(
    const d7_hamt_directed_graph* graph,
    const void* vertex,
    const d7_hamt_set** predecessors);
/* How many edges leave the vertex. */
size_t d7_hamt_directed_graph_out_degree(
    const d7_hamt_directed_graph* graph,
    const void* vertex);
/* How many edges enter the vertex. */
size_t d7_hamt_directed_graph_in_degree(
    const d7_hamt_directed_graph* graph,
    const void* vertex);

/* Produces a graph containing the vertex, with no edges added. */
d7_hamt_status d7_hamt_directed_graph_add_vertex(
    const d7_hamt_directed_graph* graph,
    const void* vertex,
    d7_hamt_directed_graph* result);
/* Produces a graph containing the directed edge, adding either endpoint that is missing. */
d7_hamt_status d7_hamt_directed_graph_add_edge(
    const d7_hamt_directed_graph* graph,
    const void* source,
    const void* target,
    d7_hamt_directed_graph* result);
/* Produces a graph without that directed edge, leaving both endpoints in place. */
d7_hamt_status d7_hamt_directed_graph_remove_edge(
    const d7_hamt_directed_graph* graph,
    const void* source,
    const void* target,
    d7_hamt_directed_graph* result);
/* Produces a graph without the vertex and without any edge touching it. */
d7_hamt_status d7_hamt_directed_graph_remove_vertex(
    const d7_hamt_directed_graph* graph,
    const void* vertex,
    d7_hamt_directed_graph* result);
/* Produces an empty graph retaining the same policies. */
d7_hamt_status d7_hamt_directed_graph_clear(
    const d7_hamt_directed_graph* graph,
    d7_hamt_directed_graph* result);
/* Produces the graph in the opposite order. */
d7_hamt_status d7_hamt_directed_graph_reverse(
    const d7_hamt_directed_graph* graph,
    d7_hamt_directed_graph* result);
/* Calls the visitor once per edge. */
d7_hamt_status d7_hamt_directed_graph_visit_edges(
    const d7_hamt_directed_graph* graph,
    d7_hamt_directed_graph_edge_visit_fn visitor,
    void* context);

/* Checks the graph's structural invariants. For tests and diagnostics. */
bool d7_hamt_directed_graph_debug_validate(
    const d7_hamt_directed_graph* graph);
/* Whether both handles reference the same roots. */
bool d7_hamt_directed_graph_debug_shares_roots(
    const d7_hamt_directed_graph* left,
    const d7_hamt_directed_graph* right);

#ifdef __cplusplus
}
#endif

#endif
