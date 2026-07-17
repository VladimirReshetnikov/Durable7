#include <Tools/DataStructures/Hamt/persistent_directed_graph.h>

#include <string.h>

static bool tds_hamt_directed_graph_valid(const tds_hamt_directed_graph* graph)
{
    return graph != NULL
        && graph->vertices.map.policy.hash != NULL
        && graph->edges.forward.context != NULL
        && graph->edges.reverse.context != NULL;
}

static void tds_hamt_directed_graph_publish(
    const tds_hamt_directed_graph* source,
    tds_hamt_directed_graph* result,
    tds_hamt_directed_graph* candidate)
{
    if (result == source) {
        tds_hamt_directed_graph_destroy(result);
    }
    *result = *candidate;
    (void)memset(candidate, 0, sizeof(*candidate));
}

static tds_hamt_status tds_hamt_directed_graph_publish_clone(
    const tds_hamt_directed_graph* source,
    tds_hamt_directed_graph* result)
{
    tds_hamt_directed_graph candidate;
    const tds_hamt_status status =
        tds_hamt_directed_graph_clone(source, &candidate);
    if (status == TDS_HAMT_OK) {
        tds_hamt_directed_graph_publish(source, result, &candidate);
    }
    return status;
}

tds_hamt_status tds_hamt_directed_graph_init(
    tds_hamt_directed_graph* graph,
    const tds_hamt_set_policy* vertex_policy)
{
    if (graph == NULL || vertex_policy == NULL || vertex_policy->hash == NULL
        || vertex_policy->equal == NULL || vertex_policy->retain_item == NULL
        || vertex_policy->release_item == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    graph->vertices = tds_hamt_set_create(vertex_policy);
    const tds_hamt_status status = tds_hamt_relation_init(
        &graph->edges, vertex_policy, vertex_policy);
    if (status != TDS_HAMT_OK) {
        tds_hamt_set_destroy(&graph->vertices);
        return status;
    }
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_directed_graph_clone(
    const tds_hamt_directed_graph* source,
    tds_hamt_directed_graph* destination)
{
    if (!tds_hamt_directed_graph_valid(source) || destination == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return TDS_HAMT_OK;
    }
    destination->vertices = tds_hamt_set_clone(&source->vertices);
    const tds_hamt_status status =
        tds_hamt_relation_clone(&source->edges, &destination->edges);
    if (status != TDS_HAMT_OK) {
        tds_hamt_set_destroy(&destination->vertices);
    }
    return status;
}

void tds_hamt_directed_graph_move(
    tds_hamt_directed_graph* destination,
    tds_hamt_directed_graph* source)
{
    if (destination != NULL && source != NULL && destination != source) {
        *destination = *source;
        (void)memset(source, 0, sizeof(*source));
    }
}

void tds_hamt_directed_graph_destroy(tds_hamt_directed_graph* graph)
{
    if (graph != NULL) {
        tds_hamt_set_destroy(&graph->vertices);
        tds_hamt_relation_destroy(&graph->edges);
        (void)memset(graph, 0, sizeof(*graph));
    }
}

size_t tds_hamt_directed_graph_vertex_count(const tds_hamt_directed_graph* graph)
{
    return tds_hamt_directed_graph_valid(graph)
        ? tds_hamt_set_count(&graph->vertices) : 0u;
}

int64_t tds_hamt_directed_graph_edge_count(const tds_hamt_directed_graph* graph)
{
    return tds_hamt_directed_graph_valid(graph)
        ? tds_hamt_relation_pair_count(&graph->edges) : 0;
}

bool tds_hamt_directed_graph_empty(const tds_hamt_directed_graph* graph)
{
    return tds_hamt_directed_graph_vertex_count(graph) == 0u;
}

bool tds_hamt_directed_graph_contains_vertex(
    const tds_hamt_directed_graph* graph,
    const void* vertex)
{
    return tds_hamt_directed_graph_valid(graph)
        && tds_hamt_set_contains(&graph->vertices, vertex);
}

bool tds_hamt_directed_graph_try_get_vertex(
    const tds_hamt_directed_graph* graph,
    const void* equal_vertex,
    const void** actual_vertex)
{
    return tds_hamt_directed_graph_valid(graph)
        && tds_hamt_set_try_get_value(
            &graph->vertices, equal_vertex, actual_vertex);
}

bool tds_hamt_directed_graph_contains_edge(
    const tds_hamt_directed_graph* graph,
    const void* source,
    const void* target)
{
    return tds_hamt_directed_graph_valid(graph)
        && tds_hamt_relation_contains(&graph->edges, source, target);
}

bool tds_hamt_directed_graph_try_get_successors(
    const tds_hamt_directed_graph* graph,
    const void* vertex,
    const tds_hamt_set** successors)
{
    return tds_hamt_directed_graph_valid(graph)
        && tds_hamt_relation_try_get_rights(&graph->edges, vertex, successors);
}

bool tds_hamt_directed_graph_try_get_predecessors(
    const tds_hamt_directed_graph* graph,
    const void* vertex,
    const tds_hamt_set** predecessors)
{
    return tds_hamt_directed_graph_valid(graph)
        && tds_hamt_relation_try_get_lefts(&graph->edges, vertex, predecessors);
}

size_t tds_hamt_directed_graph_out_degree(
    const tds_hamt_directed_graph* graph,
    const void* vertex)
{
    const tds_hamt_set* values = NULL;
    return tds_hamt_directed_graph_try_get_successors(graph, vertex, &values)
        ? tds_hamt_set_count(values) : 0u;
}

size_t tds_hamt_directed_graph_in_degree(
    const tds_hamt_directed_graph* graph,
    const void* vertex)
{
    const tds_hamt_set* values = NULL;
    return tds_hamt_directed_graph_try_get_predecessors(graph, vertex, &values)
        ? tds_hamt_set_count(values) : 0u;
}

tds_hamt_status tds_hamt_directed_graph_add_vertex(
    const tds_hamt_directed_graph* graph,
    const void* vertex,
    tds_hamt_directed_graph* result)
{
    if (!tds_hamt_directed_graph_valid(graph) || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    tds_hamt_set vertices;
    bool added = false;
    tds_hamt_status status = tds_hamt_set_try_add(
        &graph->vertices, vertex, &vertices, &added);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    if (!added) {
        tds_hamt_set_destroy(&vertices);
        return tds_hamt_directed_graph_publish_clone(graph, result);
    }
    tds_hamt_directed_graph candidate;
    candidate.vertices = vertices;
    status = tds_hamt_relation_clone(&graph->edges, &candidate.edges);
    if (status != TDS_HAMT_OK) {
        tds_hamt_set_destroy(&candidate.vertices);
        return status;
    }
    tds_hamt_directed_graph_publish(graph, result, &candidate);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_directed_graph_add_edge(
    const tds_hamt_directed_graph* graph,
    const void* source,
    const void* target,
    tds_hamt_directed_graph* result)
{
    if (!tds_hamt_directed_graph_valid(graph) || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    tds_hamt_set vertices;
    tds_hamt_status status =
        tds_hamt_set_add(&graph->vertices, source, &vertices);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    tds_hamt_set complete;
    status = tds_hamt_set_add(&vertices, target, &complete);
    tds_hamt_set_destroy(&vertices);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    const void* stored_source = NULL;
    const void* stored_target = NULL;
    if (!tds_hamt_set_try_get_value(&complete, source, &stored_source)
        || !tds_hamt_set_try_get_value(&complete, target, &stored_target)) {
        tds_hamt_set_destroy(&complete);
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    tds_hamt_relation edges;
    status = tds_hamt_relation_add(
        &graph->edges, stored_source, stored_target, &edges);
    if (status != TDS_HAMT_OK) {
        tds_hamt_set_destroy(&complete);
        return status;
    }
    if (tds_hamt_set_shares_root(&complete, &graph->vertices)
        && tds_hamt_relation_debug_shares_roots(&edges, &graph->edges)) {
        tds_hamt_set_destroy(&complete);
        tds_hamt_relation_destroy(&edges);
        return tds_hamt_directed_graph_publish_clone(graph, result);
    }
    tds_hamt_directed_graph candidate = { complete, edges };
    tds_hamt_directed_graph_publish(graph, result, &candidate);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_directed_graph_remove_edge(
    const tds_hamt_directed_graph* graph,
    const void* source,
    const void* target,
    tds_hamt_directed_graph* result)
{
    if (!tds_hamt_directed_graph_valid(graph) || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    tds_hamt_relation edges;
    tds_hamt_status status =
        tds_hamt_relation_remove(&graph->edges, source, target, &edges);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    if (tds_hamt_relation_debug_shares_roots(&edges, &graph->edges)) {
        tds_hamt_relation_destroy(&edges);
        return tds_hamt_directed_graph_publish_clone(graph, result);
    }
    tds_hamt_directed_graph candidate;
    candidate.vertices = tds_hamt_set_clone(&graph->vertices);
    candidate.edges = edges;
    tds_hamt_directed_graph_publish(graph, result, &candidate);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_directed_graph_remove_vertex(
    const tds_hamt_directed_graph* graph,
    const void* vertex,
    tds_hamt_directed_graph* result)
{
    if (!tds_hamt_directed_graph_valid(graph) || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    const void* stored = NULL;
    if (!tds_hamt_set_try_get_value(&graph->vertices, vertex, &stored)) {
        return tds_hamt_directed_graph_publish_clone(graph, result);
    }
    tds_hamt_set vertices;
    tds_hamt_status status =
        tds_hamt_set_remove(&graph->vertices, stored, &vertices);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    tds_hamt_relation without_outgoing;
    status = tds_hamt_relation_remove_left(
        &graph->edges, stored, &without_outgoing);
    if (status != TDS_HAMT_OK) {
        tds_hamt_set_destroy(&vertices);
        return status;
    }
    tds_hamt_relation edges;
    status = tds_hamt_relation_remove_right(
        &without_outgoing, stored, &edges);
    tds_hamt_relation_destroy(&without_outgoing);
    if (status != TDS_HAMT_OK) {
        tds_hamt_set_destroy(&vertices);
        return status;
    }
    tds_hamt_directed_graph candidate = { vertices, edges };
    tds_hamt_directed_graph_publish(graph, result, &candidate);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_directed_graph_clear(
    const tds_hamt_directed_graph* graph,
    tds_hamt_directed_graph* result)
{
    if (!tds_hamt_directed_graph_valid(graph) || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    if (tds_hamt_directed_graph_empty(graph)) {
        return tds_hamt_directed_graph_publish_clone(graph, result);
    }
    tds_hamt_set vertices;
    tds_hamt_status status = tds_hamt_set_clear(&graph->vertices, &vertices);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    tds_hamt_relation edges;
    status = tds_hamt_relation_clear(&graph->edges, &edges);
    if (status != TDS_HAMT_OK) {
        tds_hamt_set_destroy(&vertices);
        return status;
    }
    tds_hamt_directed_graph candidate = { vertices, edges };
    tds_hamt_directed_graph_publish(graph, result, &candidate);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_directed_graph_reverse(
    const tds_hamt_directed_graph* graph,
    tds_hamt_directed_graph* result)
{
    if (!tds_hamt_directed_graph_valid(graph) || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    tds_hamt_directed_graph candidate;
    candidate.vertices = tds_hamt_set_clone(&graph->vertices);
    const tds_hamt_status status =
        tds_hamt_relation_inverse(&graph->edges, &candidate.edges);
    if (status != TDS_HAMT_OK) {
        tds_hamt_set_destroy(&candidate.vertices);
        return status;
    }
    tds_hamt_directed_graph_publish(graph, result, &candidate);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_directed_graph_visit_edges(
    const tds_hamt_directed_graph* graph,
    tds_hamt_directed_graph_edge_visit_fn visitor,
    void* context)
{
    return !tds_hamt_directed_graph_valid(graph) || visitor == NULL
        ? TDS_HAMT_INVALID_ARGUMENT
        : tds_hamt_multimap_visit(&graph->edges.forward, visitor, context);
}

typedef struct tds_hamt_graph_validation_context {
    const tds_hamt_directed_graph* graph;
    bool valid;
} tds_hamt_graph_validation_context;

static void tds_hamt_graph_validate_edge(
    const void* source,
    const void* target,
    void* raw_context)
{
    tds_hamt_graph_validation_context* context =
        (tds_hamt_graph_validation_context*)raw_context;
    if (!tds_hamt_set_contains(&context->graph->vertices, source)
        || !tds_hamt_set_contains(&context->graph->vertices, target)) {
        context->valid = false;
    }
}

bool tds_hamt_directed_graph_debug_validate(
    const tds_hamt_directed_graph* graph)
{
    if (!tds_hamt_directed_graph_valid(graph)
        || !tds_hamt_map_debug_validate_canonical(&graph->vertices.map)
        || !tds_hamt_relation_debug_validate(&graph->edges)) {
        return false;
    }
    tds_hamt_graph_validation_context context = { graph, true };
    return tds_hamt_directed_graph_visit_edges(
            graph, tds_hamt_graph_validate_edge, &context) == TDS_HAMT_OK
        && context.valid;
}

bool tds_hamt_directed_graph_debug_shares_roots(
    const tds_hamt_directed_graph* left,
    const tds_hamt_directed_graph* right)
{
    return tds_hamt_directed_graph_valid(left)
        && tds_hamt_directed_graph_valid(right)
        && tds_hamt_set_shares_root(&left->vertices, &right->vertices)
        && tds_hamt_relation_debug_shares_roots(&left->edges, &right->edges);
}
