#include <durable7/hamt/persistent_directed_graph.h>

#include <string.h>

static bool d7_hamt_directed_graph_valid(const d7_hamt_directed_graph* graph)
{
    return graph != NULL
        && graph->vertices.map.policy.hash != NULL
        && graph->edges.forward.context != NULL
        && graph->edges.reverse.context != NULL;
}

static void d7_hamt_directed_graph_publish(
    const d7_hamt_directed_graph* source,
    d7_hamt_directed_graph* result,
    d7_hamt_directed_graph* candidate)
{
    if (result == source) {
        d7_hamt_directed_graph_destroy(result);
    }
    *result = *candidate;
    (void)memset(candidate, 0, sizeof(*candidate));
}

static d7_hamt_status d7_hamt_directed_graph_publish_clone(
    const d7_hamt_directed_graph* source,
    d7_hamt_directed_graph* result)
{
    d7_hamt_directed_graph candidate;
    const d7_hamt_status status =
        d7_hamt_directed_graph_clone(source, &candidate);
    if (status == D7_HAMT_OK) {
        d7_hamt_directed_graph_publish(source, result, &candidate);
    }
    return status;
}

d7_hamt_status d7_hamt_directed_graph_init(
    d7_hamt_directed_graph* graph,
    const d7_hamt_set_policy* vertex_policy)
{
    if (graph == NULL || vertex_policy == NULL || vertex_policy->hash == NULL
        || vertex_policy->equal == NULL || vertex_policy->retain_item == NULL
        || vertex_policy->release_item == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    graph->vertices = d7_hamt_set_create(vertex_policy);
    const d7_hamt_status status = d7_hamt_relation_init(
        &graph->edges, vertex_policy, vertex_policy);
    if (status != D7_HAMT_OK) {
        d7_hamt_set_destroy(&graph->vertices);
        return status;
    }
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_directed_graph_clone(
    const d7_hamt_directed_graph* source,
    d7_hamt_directed_graph* destination)
{
    if (!d7_hamt_directed_graph_valid(source) || destination == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return D7_HAMT_OK;
    }
    destination->vertices = d7_hamt_set_clone(&source->vertices);
    const d7_hamt_status status =
        d7_hamt_relation_clone(&source->edges, &destination->edges);
    if (status != D7_HAMT_OK) {
        d7_hamt_set_destroy(&destination->vertices);
    }
    return status;
}

void d7_hamt_directed_graph_move(
    d7_hamt_directed_graph* destination,
    d7_hamt_directed_graph* source)
{
    if (destination != NULL && source != NULL && destination != source) {
        *destination = *source;
        (void)memset(source, 0, sizeof(*source));
    }
}

void d7_hamt_directed_graph_destroy(d7_hamt_directed_graph* graph)
{
    if (graph != NULL) {
        d7_hamt_set_destroy(&graph->vertices);
        d7_hamt_relation_destroy(&graph->edges);
        (void)memset(graph, 0, sizeof(*graph));
    }
}

size_t d7_hamt_directed_graph_vertex_count(const d7_hamt_directed_graph* graph)
{
    return d7_hamt_directed_graph_valid(graph)
        ? d7_hamt_set_count(&graph->vertices) : 0u;
}

int64_t d7_hamt_directed_graph_edge_count(const d7_hamt_directed_graph* graph)
{
    return d7_hamt_directed_graph_valid(graph)
        ? d7_hamt_relation_pair_count(&graph->edges) : 0;
}

bool d7_hamt_directed_graph_empty(const d7_hamt_directed_graph* graph)
{
    return d7_hamt_directed_graph_vertex_count(graph) == 0u;
}

bool d7_hamt_directed_graph_contains_vertex(
    const d7_hamt_directed_graph* graph,
    const void* vertex)
{
    return d7_hamt_directed_graph_valid(graph)
        && d7_hamt_set_contains(&graph->vertices, vertex);
}

bool d7_hamt_directed_graph_try_get_vertex(
    const d7_hamt_directed_graph* graph,
    const void* equal_vertex,
    const void** actual_vertex)
{
    return d7_hamt_directed_graph_valid(graph)
        && d7_hamt_set_try_get_value(
            &graph->vertices, equal_vertex, actual_vertex);
}

bool d7_hamt_directed_graph_contains_edge(
    const d7_hamt_directed_graph* graph,
    const void* source,
    const void* target)
{
    return d7_hamt_directed_graph_valid(graph)
        && d7_hamt_relation_contains(&graph->edges, source, target);
}

bool d7_hamt_directed_graph_try_get_successors(
    const d7_hamt_directed_graph* graph,
    const void* vertex,
    const d7_hamt_set** successors)
{
    return d7_hamt_directed_graph_valid(graph)
        && d7_hamt_relation_try_get_rights(&graph->edges, vertex, successors);
}

bool d7_hamt_directed_graph_try_get_predecessors(
    const d7_hamt_directed_graph* graph,
    const void* vertex,
    const d7_hamt_set** predecessors)
{
    return d7_hamt_directed_graph_valid(graph)
        && d7_hamt_relation_try_get_lefts(&graph->edges, vertex, predecessors);
}

size_t d7_hamt_directed_graph_out_degree(
    const d7_hamt_directed_graph* graph,
    const void* vertex)
{
    const d7_hamt_set* values = NULL;
    return d7_hamt_directed_graph_try_get_successors(graph, vertex, &values)
        ? d7_hamt_set_count(values) : 0u;
}

size_t d7_hamt_directed_graph_in_degree(
    const d7_hamt_directed_graph* graph,
    const void* vertex)
{
    const d7_hamt_set* values = NULL;
    return d7_hamt_directed_graph_try_get_predecessors(graph, vertex, &values)
        ? d7_hamt_set_count(values) : 0u;
}

d7_hamt_status d7_hamt_directed_graph_add_vertex(
    const d7_hamt_directed_graph* graph,
    const void* vertex,
    d7_hamt_directed_graph* result)
{
    if (!d7_hamt_directed_graph_valid(graph) || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    d7_hamt_set vertices;
    bool added = false;
    d7_hamt_status status = d7_hamt_set_try_add(
        &graph->vertices, vertex, &vertices, &added);
    if (status != D7_HAMT_OK) {
        return status;
    }
    if (!added) {
        d7_hamt_set_destroy(&vertices);
        return d7_hamt_directed_graph_publish_clone(graph, result);
    }
    d7_hamt_directed_graph candidate;
    candidate.vertices = vertices;
    status = d7_hamt_relation_clone(&graph->edges, &candidate.edges);
    if (status != D7_HAMT_OK) {
        d7_hamt_set_destroy(&candidate.vertices);
        return status;
    }
    d7_hamt_directed_graph_publish(graph, result, &candidate);
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_directed_graph_add_edge(
    const d7_hamt_directed_graph* graph,
    const void* source,
    const void* target,
    d7_hamt_directed_graph* result)
{
    if (!d7_hamt_directed_graph_valid(graph) || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    d7_hamt_set vertices;
    d7_hamt_status status =
        d7_hamt_set_add(&graph->vertices, source, &vertices);
    if (status != D7_HAMT_OK) {
        return status;
    }
    d7_hamt_set complete;
    status = d7_hamt_set_add(&vertices, target, &complete);
    d7_hamt_set_destroy(&vertices);
    if (status != D7_HAMT_OK) {
        return status;
    }
    const void* stored_source = NULL;
    const void* stored_target = NULL;
    if (!d7_hamt_set_try_get_value(&complete, source, &stored_source)
        || !d7_hamt_set_try_get_value(&complete, target, &stored_target)) {
        d7_hamt_set_destroy(&complete);
        return D7_HAMT_INVALID_ARGUMENT;
    }
    d7_hamt_relation edges;
    status = d7_hamt_relation_add(
        &graph->edges, stored_source, stored_target, &edges);
    if (status != D7_HAMT_OK) {
        d7_hamt_set_destroy(&complete);
        return status;
    }
    if (d7_hamt_set_shares_root(&complete, &graph->vertices)
        && d7_hamt_relation_debug_shares_roots(&edges, &graph->edges)) {
        d7_hamt_set_destroy(&complete);
        d7_hamt_relation_destroy(&edges);
        return d7_hamt_directed_graph_publish_clone(graph, result);
    }
    d7_hamt_directed_graph candidate = { complete, edges };
    d7_hamt_directed_graph_publish(graph, result, &candidate);
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_directed_graph_remove_edge(
    const d7_hamt_directed_graph* graph,
    const void* source,
    const void* target,
    d7_hamt_directed_graph* result)
{
    if (!d7_hamt_directed_graph_valid(graph) || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    d7_hamt_relation edges;
    d7_hamt_status status =
        d7_hamt_relation_remove(&graph->edges, source, target, &edges);
    if (status != D7_HAMT_OK) {
        return status;
    }
    if (d7_hamt_relation_debug_shares_roots(&edges, &graph->edges)) {
        d7_hamt_relation_destroy(&edges);
        return d7_hamt_directed_graph_publish_clone(graph, result);
    }
    d7_hamt_directed_graph candidate;
    candidate.vertices = d7_hamt_set_clone(&graph->vertices);
    candidate.edges = edges;
    d7_hamt_directed_graph_publish(graph, result, &candidate);
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_directed_graph_remove_vertex(
    const d7_hamt_directed_graph* graph,
    const void* vertex,
    d7_hamt_directed_graph* result)
{
    if (!d7_hamt_directed_graph_valid(graph) || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    const void* stored = NULL;
    if (!d7_hamt_set_try_get_value(&graph->vertices, vertex, &stored)) {
        return d7_hamt_directed_graph_publish_clone(graph, result);
    }
    d7_hamt_set vertices;
    d7_hamt_status status =
        d7_hamt_set_remove(&graph->vertices, stored, &vertices);
    if (status != D7_HAMT_OK) {
        return status;
    }
    d7_hamt_relation without_outgoing;
    status = d7_hamt_relation_remove_left(
        &graph->edges, stored, &without_outgoing);
    if (status != D7_HAMT_OK) {
        d7_hamt_set_destroy(&vertices);
        return status;
    }
    d7_hamt_relation edges;
    status = d7_hamt_relation_remove_right(
        &without_outgoing, stored, &edges);
    d7_hamt_relation_destroy(&without_outgoing);
    if (status != D7_HAMT_OK) {
        d7_hamt_set_destroy(&vertices);
        return status;
    }
    d7_hamt_directed_graph candidate = { vertices, edges };
    d7_hamt_directed_graph_publish(graph, result, &candidate);
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_directed_graph_clear(
    const d7_hamt_directed_graph* graph,
    d7_hamt_directed_graph* result)
{
    if (!d7_hamt_directed_graph_valid(graph) || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    if (d7_hamt_directed_graph_empty(graph)) {
        return d7_hamt_directed_graph_publish_clone(graph, result);
    }
    d7_hamt_set vertices;
    d7_hamt_status status = d7_hamt_set_clear(&graph->vertices, &vertices);
    if (status != D7_HAMT_OK) {
        return status;
    }
    d7_hamt_relation edges;
    status = d7_hamt_relation_clear(&graph->edges, &edges);
    if (status != D7_HAMT_OK) {
        d7_hamt_set_destroy(&vertices);
        return status;
    }
    d7_hamt_directed_graph candidate = { vertices, edges };
    d7_hamt_directed_graph_publish(graph, result, &candidate);
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_directed_graph_reverse(
    const d7_hamt_directed_graph* graph,
    d7_hamt_directed_graph* result)
{
    if (!d7_hamt_directed_graph_valid(graph) || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    d7_hamt_directed_graph candidate;
    candidate.vertices = d7_hamt_set_clone(&graph->vertices);
    const d7_hamt_status status =
        d7_hamt_relation_inverse(&graph->edges, &candidate.edges);
    if (status != D7_HAMT_OK) {
        d7_hamt_set_destroy(&candidate.vertices);
        return status;
    }
    d7_hamt_directed_graph_publish(graph, result, &candidate);
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_directed_graph_visit_edges(
    const d7_hamt_directed_graph* graph,
    d7_hamt_directed_graph_edge_visit_fn visitor,
    void* context)
{
    return !d7_hamt_directed_graph_valid(graph) || visitor == NULL
        ? D7_HAMT_INVALID_ARGUMENT
        : d7_hamt_multimap_visit(&graph->edges.forward, visitor, context);
}

typedef struct d7_hamt_graph_validation_context {
    const d7_hamt_directed_graph* graph;
    bool valid;
} d7_hamt_graph_validation_context;

static void d7_hamt_graph_validate_edge(
    const void* source,
    const void* target,
    void* raw_context)
{
    d7_hamt_graph_validation_context* context =
        (d7_hamt_graph_validation_context*)raw_context;
    if (!d7_hamt_set_contains(&context->graph->vertices, source)
        || !d7_hamt_set_contains(&context->graph->vertices, target)) {
        context->valid = false;
    }
}

bool d7_hamt_directed_graph_debug_validate(
    const d7_hamt_directed_graph* graph)
{
    if (!d7_hamt_directed_graph_valid(graph)
        || !d7_hamt_map_debug_validate_canonical(&graph->vertices.map)
        || !d7_hamt_relation_debug_validate(&graph->edges)) {
        return false;
    }
    d7_hamt_graph_validation_context context = { graph, true };
    return d7_hamt_directed_graph_visit_edges(
            graph, d7_hamt_graph_validate_edge, &context) == D7_HAMT_OK
        && context.valid;
}

bool d7_hamt_directed_graph_debug_shares_roots(
    const d7_hamt_directed_graph* left,
    const d7_hamt_directed_graph* right)
{
    return d7_hamt_directed_graph_valid(left)
        && d7_hamt_directed_graph_valid(right)
        && d7_hamt_set_shares_root(&left->vertices, &right->vertices)
        && d7_hamt_relation_debug_shares_roots(&left->edges, &right->edges);
}
