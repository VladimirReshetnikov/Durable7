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

d7_hamt_status d7_hamt_directed_graph_init(
    d7_hamt_directed_graph* graph,
    const d7_hamt_set_policy* vertex_policy);
d7_hamt_status d7_hamt_directed_graph_clone(
    const d7_hamt_directed_graph* source,
    d7_hamt_directed_graph* destination);
void d7_hamt_directed_graph_move(
    d7_hamt_directed_graph* destination,
    d7_hamt_directed_graph* source);
void d7_hamt_directed_graph_destroy(d7_hamt_directed_graph* graph);

size_t d7_hamt_directed_graph_vertex_count(const d7_hamt_directed_graph* graph);
int64_t d7_hamt_directed_graph_edge_count(const d7_hamt_directed_graph* graph);
bool d7_hamt_directed_graph_empty(const d7_hamt_directed_graph* graph);
bool d7_hamt_directed_graph_contains_vertex(
    const d7_hamt_directed_graph* graph,
    const void* vertex);
bool d7_hamt_directed_graph_try_get_vertex(
    const d7_hamt_directed_graph* graph,
    const void* equal_vertex,
    const void** actual_vertex);
bool d7_hamt_directed_graph_contains_edge(
    const d7_hamt_directed_graph* graph,
    const void* source,
    const void* target);
bool d7_hamt_directed_graph_try_get_successors(
    const d7_hamt_directed_graph* graph,
    const void* vertex,
    const d7_hamt_set** successors);
bool d7_hamt_directed_graph_try_get_predecessors(
    const d7_hamt_directed_graph* graph,
    const void* vertex,
    const d7_hamt_set** predecessors);
size_t d7_hamt_directed_graph_out_degree(
    const d7_hamt_directed_graph* graph,
    const void* vertex);
size_t d7_hamt_directed_graph_in_degree(
    const d7_hamt_directed_graph* graph,
    const void* vertex);

d7_hamt_status d7_hamt_directed_graph_add_vertex(
    const d7_hamt_directed_graph* graph,
    const void* vertex,
    d7_hamt_directed_graph* result);
d7_hamt_status d7_hamt_directed_graph_add_edge(
    const d7_hamt_directed_graph* graph,
    const void* source,
    const void* target,
    d7_hamt_directed_graph* result);
d7_hamt_status d7_hamt_directed_graph_remove_edge(
    const d7_hamt_directed_graph* graph,
    const void* source,
    const void* target,
    d7_hamt_directed_graph* result);
d7_hamt_status d7_hamt_directed_graph_remove_vertex(
    const d7_hamt_directed_graph* graph,
    const void* vertex,
    d7_hamt_directed_graph* result);
d7_hamt_status d7_hamt_directed_graph_clear(
    const d7_hamt_directed_graph* graph,
    d7_hamt_directed_graph* result);
d7_hamt_status d7_hamt_directed_graph_reverse(
    const d7_hamt_directed_graph* graph,
    d7_hamt_directed_graph* result);
d7_hamt_status d7_hamt_directed_graph_visit_edges(
    const d7_hamt_directed_graph* graph,
    d7_hamt_directed_graph_edge_visit_fn visitor,
    void* context);

bool d7_hamt_directed_graph_debug_validate(
    const d7_hamt_directed_graph* graph);
bool d7_hamt_directed_graph_debug_shares_roots(
    const d7_hamt_directed_graph* left,
    const d7_hamt_directed_graph* right);

#ifdef __cplusplus
}
#endif

#endif
