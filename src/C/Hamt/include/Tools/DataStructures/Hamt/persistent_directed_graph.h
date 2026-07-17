#ifndef TOOLS_DATA_STRUCTURES_HAMT_PERSISTENT_DIRECTED_GRAPH_H
#define TOOLS_DATA_STRUCTURES_HAMT_PERSISTENT_DIRECTED_GRAPH_H

#include <Tools/DataStructures/Hamt/persistent_relation.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tds_hamt_directed_graph {
    tds_hamt_set vertices;
    tds_hamt_relation edges;
} tds_hamt_directed_graph;

typedef void (*tds_hamt_directed_graph_edge_visit_fn)(
    const void* source,
    const void* target,
    void* context);

tds_hamt_status tds_hamt_directed_graph_init(
    tds_hamt_directed_graph* graph,
    const tds_hamt_set_policy* vertex_policy);
tds_hamt_status tds_hamt_directed_graph_clone(
    const tds_hamt_directed_graph* source,
    tds_hamt_directed_graph* destination);
void tds_hamt_directed_graph_move(
    tds_hamt_directed_graph* destination,
    tds_hamt_directed_graph* source);
void tds_hamt_directed_graph_destroy(tds_hamt_directed_graph* graph);

size_t tds_hamt_directed_graph_vertex_count(const tds_hamt_directed_graph* graph);
int64_t tds_hamt_directed_graph_edge_count(const tds_hamt_directed_graph* graph);
bool tds_hamt_directed_graph_empty(const tds_hamt_directed_graph* graph);
bool tds_hamt_directed_graph_contains_vertex(
    const tds_hamt_directed_graph* graph,
    const void* vertex);
bool tds_hamt_directed_graph_try_get_vertex(
    const tds_hamt_directed_graph* graph,
    const void* equal_vertex,
    const void** actual_vertex);
bool tds_hamt_directed_graph_contains_edge(
    const tds_hamt_directed_graph* graph,
    const void* source,
    const void* target);
bool tds_hamt_directed_graph_try_get_successors(
    const tds_hamt_directed_graph* graph,
    const void* vertex,
    const tds_hamt_set** successors);
bool tds_hamt_directed_graph_try_get_predecessors(
    const tds_hamt_directed_graph* graph,
    const void* vertex,
    const tds_hamt_set** predecessors);
size_t tds_hamt_directed_graph_out_degree(
    const tds_hamt_directed_graph* graph,
    const void* vertex);
size_t tds_hamt_directed_graph_in_degree(
    const tds_hamt_directed_graph* graph,
    const void* vertex);

tds_hamt_status tds_hamt_directed_graph_add_vertex(
    const tds_hamt_directed_graph* graph,
    const void* vertex,
    tds_hamt_directed_graph* result);
tds_hamt_status tds_hamt_directed_graph_add_edge(
    const tds_hamt_directed_graph* graph,
    const void* source,
    const void* target,
    tds_hamt_directed_graph* result);
tds_hamt_status tds_hamt_directed_graph_remove_edge(
    const tds_hamt_directed_graph* graph,
    const void* source,
    const void* target,
    tds_hamt_directed_graph* result);
tds_hamt_status tds_hamt_directed_graph_remove_vertex(
    const tds_hamt_directed_graph* graph,
    const void* vertex,
    tds_hamt_directed_graph* result);
tds_hamt_status tds_hamt_directed_graph_clear(
    const tds_hamt_directed_graph* graph,
    tds_hamt_directed_graph* result);
tds_hamt_status tds_hamt_directed_graph_reverse(
    const tds_hamt_directed_graph* graph,
    tds_hamt_directed_graph* result);
tds_hamt_status tds_hamt_directed_graph_visit_edges(
    const tds_hamt_directed_graph* graph,
    tds_hamt_directed_graph_edge_visit_fn visitor,
    void* context);

bool tds_hamt_directed_graph_debug_validate(
    const tds_hamt_directed_graph* graph);
bool tds_hamt_directed_graph_debug_shares_roots(
    const tds_hamt_directed_graph* left,
    const tds_hamt_directed_graph* right);

#ifdef __cplusplus
}
#endif

#endif
