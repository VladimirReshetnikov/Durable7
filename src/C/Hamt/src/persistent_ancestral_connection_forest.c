/*
 * Implementation of the persistent ancestral connection forest.
 *
 * A version owns a reference-counted history token and a CHAMP map of the parent cells touched by a
 * successful union. Version tokens form a tree whose nodes own their parent, so a token is released
 * iteratively rather than recursively: a history that is hundreds of thousands of links deep must
 * not overflow the stack while unwinding.
 */

#include <durable7/hamt/persistent_ancestral_connection_forest.h>

#include <stdlib.h>
#include <string.h>

/* Union by size at least doubles a component every time a vertex's depth grows, and no component
 * exceeds INT32_MAX, so a parent path holds at most 31 vertices. The extra slots let a walk detect a
 * corrupted structure instead of running off the array. */
#define D7_HAMT_ANCESTRAL_MAX_PARENT_PATH 33

#ifdef D7_HAMT_TESTING
static size_t d7_hamt_ancestral_test_fail_after = SIZE_MAX;
static size_t d7_hamt_ancestral_test_allocation_count = 0;

void d7_hamt_ancestral_connection_forest_test_fail_allocations_after(
    size_t successful_allocations);
void d7_hamt_ancestral_connection_forest_test_reset_allocator(void);

void d7_hamt_ancestral_connection_forest_test_fail_allocations_after(
    size_t successful_allocations)
{
    d7_hamt_ancestral_test_fail_after = successful_allocations;
    d7_hamt_ancestral_test_allocation_count = 0;
}

void d7_hamt_ancestral_connection_forest_test_reset_allocator(void)
{
    d7_hamt_ancestral_test_fail_after = SIZE_MAX;
    d7_hamt_ancestral_test_allocation_count = 0;
}
#endif

static void* d7_hamt_ancestral_allocate(size_t size)
{
#ifdef D7_HAMT_TESTING
    if (d7_hamt_ancestral_test_allocation_count == d7_hamt_ancestral_test_fail_after) {
        return NULL;
    }
    ++d7_hamt_ancestral_test_allocation_count;
#endif
    return malloc(size);
}

/* One immutable history token. The parent reference is owning, which keeps the whole root-to-node
 * lineage alive; the root reference is owning too, and cannot form a cycle because the history
 * root's own root reference is empty. */
struct d7_hamt_ancestral_connection_version_rep {
    size_t references;
    uint64_t depth;
    d7_hamt_ancestral_connection_version parent;
    d7_hamt_ancestral_connection_version root;
};

/* One sparse parent record.
 *
 * size is meaningful only for a root (parent equal to the vertex itself), and joined_at is present
 * only on a non-root edge. An absent cell canonically means "singleton root of size 1". */
typedef struct d7_hamt_ancestral_cell {
    int32_t parent;
    int32_t size;
    d7_hamt_ancestral_connection_version joined_at;
} d7_hamt_ancestral_cell;

/* One parent path: the vertices from a start vertex up to its root, and the union-edge tag stored on
 * the cell of each of them. */
typedef struct d7_hamt_ancestral_path {
    int32_t vertices[D7_HAMT_ANCESTRAL_MAX_PARENT_PATH];
    const d7_hamt_ancestral_connection_version* tags[D7_HAMT_ANCESTRAL_MAX_PARENT_PATH];
    size_t length;
} d7_hamt_ancestral_path;

static void d7_hamt_ancestral_version_retain_rep(
    d7_hamt_ancestral_connection_version_rep* rep)
{
    if (rep != NULL) {
        ++rep->references;
    }
}

static void d7_hamt_ancestral_version_release_rep(
    d7_hamt_ancestral_connection_version_rep* rep)
{
    while (rep != NULL) {
        if (rep->references > 1u) {
            --rep->references;
            return;
        }
        d7_hamt_ancestral_connection_version_rep* const parent = rep->parent.rep;
        d7_hamt_ancestral_connection_version_rep* const root = rep->root.rep;
        free(rep);
        if (root != NULL) {
            /* The root is retained by the parent chain as well: parent either is the root or holds
             * its own owning root reference. This decrement can therefore never be the last one
             * while parent is still alive, so it needs no cascade of its own. */
            --root->references;
        }
        rep = parent;
    }
}

static d7_hamt_status d7_hamt_ancestral_version_create_root(
    d7_hamt_ancestral_connection_version* version)
{
    d7_hamt_ancestral_connection_version_rep* const rep =
        (d7_hamt_ancestral_connection_version_rep*)d7_hamt_ancestral_allocate(sizeof(*rep));
    if (rep == NULL) {
        return D7_HAMT_OUT_OF_MEMORY;
    }
    rep->references = 1u;
    rep->depth = 0u;
    rep->parent.rep = NULL;
    rep->root.rep = NULL;
    version->rep = rep;
    return D7_HAMT_OK;
}

static d7_hamt_status d7_hamt_ancestral_version_create_child(
    const d7_hamt_ancestral_connection_version* parent,
    d7_hamt_ancestral_connection_version* version)
{
    if (parent->rep->depth == UINT64_MAX) {
        return D7_HAMT_OVERFLOW;
    }
    d7_hamt_ancestral_connection_version_rep* const rep =
        (d7_hamt_ancestral_connection_version_rep*)d7_hamt_ancestral_allocate(sizeof(*rep));
    if (rep == NULL) {
        return D7_HAMT_OUT_OF_MEMORY;
    }
    const d7_hamt_ancestral_connection_version* const root =
        d7_hamt_ancestral_connection_version_root_ref(parent);
    rep->references = 1u;
    rep->depth = parent->rep->depth + 1u;
    rep->parent = *parent;
    rep->root = *root;
    d7_hamt_ancestral_version_retain_rep(rep->parent.rep);
    d7_hamt_ancestral_version_retain_rep(rep->root.rep);
    version->rep = rep;
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_ancestral_connection_version_clone(
    const d7_hamt_ancestral_connection_version* source,
    d7_hamt_ancestral_connection_version* destination)
{
    if (source == NULL || source->rep == NULL || destination == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return D7_HAMT_OK;
    }
    destination->rep = source->rep;
    d7_hamt_ancestral_version_retain_rep(destination->rep);
    return D7_HAMT_OK;
}

void d7_hamt_ancestral_connection_version_move(
    d7_hamt_ancestral_connection_version* destination,
    d7_hamt_ancestral_connection_version* source)
{
    if (destination != NULL && source != NULL && destination != source) {
        *destination = *source;
        source->rep = NULL;
    }
}

void d7_hamt_ancestral_connection_version_destroy(
    d7_hamt_ancestral_connection_version* version)
{
    if (version != NULL) {
        d7_hamt_ancestral_version_release_rep(version->rep);
        version->rep = NULL;
    }
}

bool d7_hamt_ancestral_connection_version_same(
    const d7_hamt_ancestral_connection_version* left,
    const d7_hamt_ancestral_connection_version* right)
{
    return left != NULL && right != NULL && left->rep != NULL && left->rep == right->rep;
}

uint64_t d7_hamt_ancestral_connection_version_depth(
    const d7_hamt_ancestral_connection_version* version)
{
    return version != NULL && version->rep != NULL ? version->rep->depth : 0u;
}

const d7_hamt_ancestral_connection_version* d7_hamt_ancestral_connection_version_parent_ref(
    const d7_hamt_ancestral_connection_version* version)
{
    if (version == NULL || version->rep == NULL || version->rep->parent.rep == NULL) {
        return NULL;
    }
    return &version->rep->parent;
}

const d7_hamt_ancestral_connection_version* d7_hamt_ancestral_connection_version_root_ref(
    const d7_hamt_ancestral_connection_version* version)
{
    if (version == NULL || version->rep == NULL) {
        return NULL;
    }
    return version->rep->root.rep != NULL ? &version->rep->root : version;
}

bool d7_hamt_ancestral_connection_version_try_copy_parent(
    const d7_hamt_ancestral_connection_version* version,
    d7_hamt_ancestral_connection_version* parent)
{
    const d7_hamt_ancestral_connection_version* const borrowed =
        d7_hamt_ancestral_connection_version_parent_ref(version);
    if (borrowed == NULL || parent == NULL) {
        return false;
    }
    return d7_hamt_ancestral_connection_version_clone(borrowed, parent) == D7_HAMT_OK;
}

d7_hamt_status d7_hamt_ancestral_connection_version_copy_root(
    const d7_hamt_ancestral_connection_version* version,
    d7_hamt_ancestral_connection_version* root)
{
    return d7_hamt_ancestral_connection_version_clone(
        d7_hamt_ancestral_connection_version_root_ref(version), root);
}

/* A bijective 32-bit mix, so distinct vertices never share a hash and this CHAMP never forms a
 * collision bucket. */
static uint32_t d7_hamt_ancestral_vertex_hash(const void* item, void* context)
{
    (void)context;
    uint32_t value = (uint32_t)*(const int32_t*)item;
    value ^= value >> 16;
    value *= UINT32_C(0x85EBCA6B);
    value ^= value >> 13;
    value *= UINT32_C(0xC2B2AE35);
    value ^= value >> 16;
    return value;
}

static bool d7_hamt_ancestral_vertex_equal(const void* left, const void* right, void* context)
{
    (void)context;
    return left != NULL && right != NULL
        && *(const int32_t*)left == *(const int32_t*)right;
}

static void* d7_hamt_ancestral_vertex_retain(const void* item, void* context)
{
    (void)context;
    if (item == NULL) {
        return NULL;
    }
    int32_t* const copy = (int32_t*)d7_hamt_ancestral_allocate(sizeof(*copy));
    if (copy != NULL) {
        *copy = *(const int32_t*)item;
    }
    return copy;
}

static void d7_hamt_ancestral_vertex_release(void* item, void* context)
{
    (void)context;
    free(item);
}

static bool d7_hamt_ancestral_cell_equal(const void* left, const void* right, void* context)
{
    (void)context;
    if (left == NULL || right == NULL) {
        return left == right;
    }
    const d7_hamt_ancestral_cell* const first = (const d7_hamt_ancestral_cell*)left;
    const d7_hamt_ancestral_cell* const second = (const d7_hamt_ancestral_cell*)right;
    return first->parent == second->parent
        && first->size == second->size
        && first->joined_at.rep == second->joined_at.rep;
}

static void* d7_hamt_ancestral_cell_retain(const void* value, void* context)
{
    (void)context;
    if (value == NULL) {
        return NULL;
    }
    d7_hamt_ancestral_cell* const copy =
        (d7_hamt_ancestral_cell*)d7_hamt_ancestral_allocate(sizeof(*copy));
    if (copy != NULL) {
        *copy = *(const d7_hamt_ancestral_cell*)value;
        d7_hamt_ancestral_version_retain_rep(copy->joined_at.rep);
    }
    return copy;
}

static void d7_hamt_ancestral_cell_release(void* value, void* context)
{
    (void)context;
    if (value != NULL) {
        d7_hamt_ancestral_cell* const cell = (d7_hamt_ancestral_cell*)value;
        d7_hamt_ancestral_version_release_rep(cell->joined_at.rep);
        free(cell);
    }
}

static d7_hamt_policy d7_hamt_ancestral_cell_policy(void)
{
    d7_hamt_policy policy;
    (void)memset(&policy, 0, sizeof(policy));
    policy.hash = d7_hamt_ancestral_vertex_hash;
    policy.key_equal = d7_hamt_ancestral_vertex_equal;
    policy.value_equal = d7_hamt_ancestral_cell_equal;
    policy.retain_key = d7_hamt_ancestral_vertex_retain;
    policy.retain_value = d7_hamt_ancestral_cell_retain;
    policy.release_key = d7_hamt_ancestral_vertex_release;
    policy.release_value = d7_hamt_ancestral_cell_release;
    return policy;
}

/* The scratch map the audit uses to recount component sizes: root vertex to vertex count. */
static d7_hamt_policy d7_hamt_ancestral_count_policy(void)
{
    d7_hamt_policy policy;
    (void)memset(&policy, 0, sizeof(policy));
    policy.hash = d7_hamt_ancestral_vertex_hash;
    policy.key_equal = d7_hamt_ancestral_vertex_equal;
    policy.value_equal = d7_hamt_ancestral_vertex_equal;
    policy.retain_key = d7_hamt_ancestral_vertex_retain;
    policy.retain_value = d7_hamt_ancestral_vertex_retain;
    policy.release_key = d7_hamt_ancestral_vertex_release;
    policy.release_value = d7_hamt_ancestral_vertex_release;
    return policy;
}

static uint32_t d7_hamt_ancestral_identity_hash(const void* item, void* context)
{
    (void)context;
    const uint64_t bits = (uint64_t)(uintptr_t)item;
    uint32_t value = (uint32_t)(bits ^ (bits >> 32));
    value ^= value >> 16;
    value *= UINT32_C(0x85EBCA6B);
    value ^= value >> 13;
    value *= UINT32_C(0xC2B2AE35);
    value ^= value >> 16;
    return value;
}

static bool d7_hamt_ancestral_identity_equal(const void* left, const void* right, void* context)
{
    (void)context;
    return left == right;
}

/* The scratch set the audit uses to hold the version lineage. Items are borrowed token addresses
 * kept alive by the validated forest, so the set neither copies nor releases them. */
static d7_hamt_set_policy d7_hamt_ancestral_identity_policy(void)
{
    d7_hamt_set_policy policy;
    (void)memset(&policy, 0, sizeof(policy));
    policy.hash = d7_hamt_ancestral_identity_hash;
    policy.equal = d7_hamt_ancestral_identity_equal;
    return policy;
}

static bool d7_hamt_ancestral_forest_valid(
    const d7_hamt_ancestral_connection_forest* forest)
{
    return forest != NULL
        && forest->version.rep != NULL
        && forest->cells.policy.hash == d7_hamt_ancestral_vertex_hash;
}

static bool d7_hamt_ancestral_is_vertex(
    const d7_hamt_ancestral_connection_forest* forest,
    int32_t vertex)
{
    return vertex >= 0 && vertex < forest->vertex_count;
}

static const d7_hamt_ancestral_cell* d7_hamt_ancestral_cell_of(
    const d7_hamt_ancestral_connection_forest* forest,
    int32_t vertex)
{
    const void* value = NULL;
    return d7_hamt_map_try_get(&forest->cells, &vertex, &value)
        ? (const d7_hamt_ancestral_cell*)value : NULL;
}

/* Walks parents to the representative; an absent cell is canonically its own singleton root. */
static int32_t d7_hamt_ancestral_find_root(
    const d7_hamt_ancestral_connection_forest* forest,
    int32_t vertex)
{
    for (;;) {
        const d7_hamt_ancestral_cell* const cell =
            d7_hamt_ancestral_cell_of(forest, vertex);
        if (cell == NULL || cell->parent == vertex) {
            return vertex;
        }
        vertex = cell->parent;
    }
}

/* Reads the component size cached at a root; an absent cell is a singleton. */
static int32_t d7_hamt_ancestral_size_of(
    const d7_hamt_ancestral_connection_forest* forest,
    int32_t vertex)
{
    const d7_hamt_ancestral_cell* const cell = d7_hamt_ancestral_cell_of(forest, vertex);
    return cell != NULL ? cell->size : 1;
}

static bool d7_hamt_ancestral_build_path(
    const d7_hamt_ancestral_connection_forest* forest,
    int32_t vertex,
    d7_hamt_ancestral_path* path)
{
    path->length = 0u;
    for (;;) {
        if (path->length >= (size_t)D7_HAMT_ANCESTRAL_MAX_PARENT_PATH) {
            return false;
        }
        const d7_hamt_ancestral_cell* const cell =
            d7_hamt_ancestral_cell_of(forest, vertex);
        path->vertices[path->length] = vertex;
        path->tags[path->length] = cell != NULL && cell->joined_at.rep != NULL
            ? &cell->joined_at : NULL;
        ++path->length;
        if (cell == NULL || cell->parent == vertex) {
            return true;
        }
        vertex = cell->parent;
    }
}

static const d7_hamt_ancestral_connection_version* d7_hamt_ancestral_later(
    const d7_hamt_ancestral_connection_version* left,
    const d7_hamt_ancestral_connection_version* right)
{
    if (right == NULL) {
        return left;
    }
    if (left == NULL) {
        return right;
    }
    return right->rep->depth > left->rep->depth ? right : left;
}

static void d7_hamt_ancestral_publish(
    const d7_hamt_ancestral_connection_forest* source,
    d7_hamt_ancestral_connection_forest* result,
    d7_hamt_ancestral_connection_forest* candidate)
{
    if (result == source) {
        d7_hamt_ancestral_connection_forest_destroy(result);
    }
    *result = *candidate;
    (void)memset(candidate, 0, sizeof(*candidate));
}

d7_hamt_status d7_hamt_ancestral_connection_forest_init(
    d7_hamt_ancestral_connection_forest* forest,
    int32_t vertex_count)
{
    if (forest == NULL || vertex_count < 0) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    d7_hamt_ancestral_connection_version version;
    const d7_hamt_status status = d7_hamt_ancestral_version_create_root(&version);
    if (status != D7_HAMT_OK) {
        return status;
    }
    const d7_hamt_policy policy = d7_hamt_ancestral_cell_policy();
    forest->vertex_count = vertex_count;
    forest->component_count = vertex_count;
    forest->cells = d7_hamt_map_create(&policy);
    forest->version = version;
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_ancestral_connection_forest_clone(
    const d7_hamt_ancestral_connection_forest* source,
    d7_hamt_ancestral_connection_forest* destination)
{
    if (!d7_hamt_ancestral_forest_valid(source) || destination == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return D7_HAMT_OK;
    }
    destination->vertex_count = source->vertex_count;
    destination->component_count = source->component_count;
    destination->cells = d7_hamt_map_clone(&source->cells);
    destination->version = source->version;
    d7_hamt_ancestral_version_retain_rep(destination->version.rep);
    return D7_HAMT_OK;
}

void d7_hamt_ancestral_connection_forest_move(
    d7_hamt_ancestral_connection_forest* destination,
    d7_hamt_ancestral_connection_forest* source)
{
    if (destination != NULL && source != NULL && destination != source) {
        *destination = *source;
        (void)memset(source, 0, sizeof(*source));
    }
}

void d7_hamt_ancestral_connection_forest_destroy(
    d7_hamt_ancestral_connection_forest* forest)
{
    if (forest != NULL) {
        d7_hamt_map_destroy(&forest->cells);
        d7_hamt_ancestral_connection_version_destroy(&forest->version);
        (void)memset(forest, 0, sizeof(*forest));
    }
}

int32_t d7_hamt_ancestral_connection_forest_vertex_count(
    const d7_hamt_ancestral_connection_forest* forest)
{
    return d7_hamt_ancestral_forest_valid(forest) ? forest->vertex_count : 0;
}

int32_t d7_hamt_ancestral_connection_forest_component_count(
    const d7_hamt_ancestral_connection_forest* forest)
{
    return d7_hamt_ancestral_forest_valid(forest) ? forest->component_count : 0;
}

const d7_hamt_ancestral_connection_version* d7_hamt_ancestral_connection_forest_version_ref(
    const d7_hamt_ancestral_connection_forest* forest)
{
    return d7_hamt_ancestral_forest_valid(forest) ? &forest->version : NULL;
}

d7_hamt_status d7_hamt_ancestral_connection_forest_copy_version(
    const d7_hamt_ancestral_connection_forest* forest,
    d7_hamt_ancestral_connection_version* version)
{
    if (!d7_hamt_ancestral_forest_valid(forest)) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    return d7_hamt_ancestral_connection_version_clone(&forest->version, version);
}

d7_hamt_status d7_hamt_ancestral_connection_forest_find(
    const d7_hamt_ancestral_connection_forest* forest,
    int32_t vertex,
    int32_t* representative)
{
    if (!d7_hamt_ancestral_forest_valid(forest) || representative == NULL
        || !d7_hamt_ancestral_is_vertex(forest, vertex)) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    *representative = d7_hamt_ancestral_find_root(forest, vertex);
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_ancestral_connection_forest_connected(
    const d7_hamt_ancestral_connection_forest* forest,
    int32_t left,
    int32_t right,
    bool* connected)
{
    if (!d7_hamt_ancestral_forest_valid(forest) || connected == NULL
        || !d7_hamt_ancestral_is_vertex(forest, left)
        || !d7_hamt_ancestral_is_vertex(forest, right)) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    *connected = d7_hamt_ancestral_find_root(forest, left)
        == d7_hamt_ancestral_find_root(forest, right);
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_ancestral_connection_forest_component_size(
    const d7_hamt_ancestral_connection_forest* forest,
    int32_t vertex,
    int32_t* size)
{
    if (!d7_hamt_ancestral_forest_valid(forest) || size == NULL
        || !d7_hamt_ancestral_is_vertex(forest, vertex)) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    *size = d7_hamt_ancestral_size_of(
        forest, d7_hamt_ancestral_find_root(forest, vertex));
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_ancestral_connection_forest_link(
    const d7_hamt_ancestral_connection_forest* forest,
    int32_t left,
    int32_t right,
    d7_hamt_ancestral_connection_forest* result)
{
    if (!d7_hamt_ancestral_forest_valid(forest) || result == NULL
        || !d7_hamt_ancestral_is_vertex(forest, left)
        || !d7_hamt_ancestral_is_vertex(forest, right)) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    int32_t left_root = d7_hamt_ancestral_find_root(forest, left);
    int32_t right_root = d7_hamt_ancestral_find_root(forest, right);
    d7_hamt_ancestral_connection_version child;
    d7_hamt_status status =
        d7_hamt_ancestral_version_create_child(&forest->version, &child);
    if (status != D7_HAMT_OK) {
        return status;
    }

    d7_hamt_ancestral_connection_forest candidate;
    candidate.vertex_count = forest->vertex_count;
    if (left_root == right_root) {
        candidate.component_count = forest->component_count;
        candidate.cells = d7_hamt_map_clone(&forest->cells);
        candidate.version = child;
        d7_hamt_ancestral_publish(forest, result, &candidate);
        return D7_HAMT_OK;
    }

    int32_t left_size = d7_hamt_ancestral_size_of(forest, left_root);
    int32_t right_size = d7_hamt_ancestral_size_of(forest, right_root);
    if (left_size < right_size) {
        const int32_t swapped_root = left_root;
        const int32_t swapped_size = left_size;
        left_root = right_root;
        left_size = right_size;
        right_root = swapped_root;
        right_size = swapped_size;
    }
    /* On a size tie the first endpoint's root remains the representative, making the result
     * deterministic without changing the logarithmic-height proof. */
    if (left_size > INT32_MAX - right_size) {
        d7_hamt_ancestral_connection_version_destroy(&child);
        return D7_HAMT_OVERFLOW;
    }

    d7_hamt_ancestral_cell winner;
    winner.parent = left_root;
    winner.size = left_size + right_size;
    winner.joined_at.rep = NULL;
    d7_hamt_ancestral_cell loser;
    loser.parent = left_root;
    loser.size = 0;
    loser.joined_at = child;

    d7_hamt_map grown;
    status = d7_hamt_map_set(&forest->cells, &left_root, &winner, &grown);
    if (status != D7_HAMT_OK) {
        d7_hamt_ancestral_connection_version_destroy(&child);
        return status;
    }
    d7_hamt_map attached;
    status = d7_hamt_map_set(&grown, &right_root, &loser, &attached);
    d7_hamt_map_destroy(&grown);
    if (status != D7_HAMT_OK) {
        d7_hamt_ancestral_connection_version_destroy(&child);
        return status;
    }

    candidate.component_count = forest->component_count - 1;
    candidate.cells = attached;
    candidate.version = child;
    d7_hamt_ancestral_publish(forest, result, &candidate);
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_ancestral_connection_forest_first_connected_ref(
    const d7_hamt_ancestral_connection_forest* forest,
    int32_t left,
    int32_t right,
    const d7_hamt_ancestral_connection_version** version)
{
    if (!d7_hamt_ancestral_forest_valid(forest) || version == NULL
        || !d7_hamt_ancestral_is_vertex(forest, left)
        || !d7_hamt_ancestral_is_vertex(forest, right)) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    if (left == right) {
        *version = d7_hamt_ancestral_connection_version_root_ref(&forest->version);
        return D7_HAMT_OK;
    }

    d7_hamt_ancestral_path left_path;
    d7_hamt_ancestral_path right_path;
    if (!d7_hamt_ancestral_build_path(forest, left, &left_path)
        || !d7_hamt_ancestral_build_path(forest, right, &right_path)) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    if (left_path.vertices[left_path.length - 1u]
        != right_path.vertices[right_path.length - 1u]) {
        *version = NULL;
        return D7_HAMT_OK;
    }

    /* Delete the common rootward suffix. The remaining steps are precisely the two paths strictly
     * below the forest lowest common ancestor, and every one of those steps owns a
     * successful-union tag. */
    size_t left_length = left_path.length;
    size_t right_length = right_path.length;
    while (left_length > 0u && right_length > 0u
        && left_path.vertices[left_length - 1u] == right_path.vertices[right_length - 1u]) {
        --left_length;
        --right_length;
    }

    const d7_hamt_ancestral_connection_version* latest = NULL;
    for (size_t index = 0u; index < left_length; ++index) {
        latest = d7_hamt_ancestral_later(latest, left_path.tags[index]);
    }
    for (size_t index = 0u; index < right_length; ++index) {
        latest = d7_hamt_ancestral_later(latest, right_path.tags[index]);
    }
    if (latest == NULL) {
        /* Distinct connected vertices have at least one edge strictly below their lowest common
         * ancestor, so reaching here means the forest handed in is corrupted. */
        return D7_HAMT_INVALID_ARGUMENT;
    }
    *version = latest;
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_ancestral_connection_forest_first_connected_copy(
    const d7_hamt_ancestral_connection_forest* forest,
    int32_t left,
    int32_t right,
    bool* connected,
    d7_hamt_ancestral_connection_version* version)
{
    if (connected == NULL || version == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    const d7_hamt_ancestral_connection_version* borrowed = NULL;
    d7_hamt_status status = d7_hamt_ancestral_connection_forest_first_connected_ref(
        forest, left, right, &borrowed);
    if (status != D7_HAMT_OK) {
        return status;
    }
    if (borrowed == NULL) {
        *connected = false;
        return D7_HAMT_OK;
    }
    d7_hamt_ancestral_connection_version owned;
    status = d7_hamt_ancestral_connection_version_clone(borrowed, &owned);
    if (status != D7_HAMT_OK) {
        return status;
    }
    *connected = true;
    *version = owned;
    return D7_HAMT_OK;
}

/* Walks the retained lineage, checking depth order and root identity, and collects the ancestor
 * tokens a union tag is allowed to name. */
static d7_hamt_status d7_hamt_ancestral_collect_ancestors(
    const d7_hamt_ancestral_connection_forest* forest,
    d7_hamt_set* ancestors,
    bool* valid)
{
    const d7_hamt_ancestral_connection_version* const history_root =
        d7_hamt_ancestral_connection_version_root_ref(&forest->version);
    uint64_t expected_depth = forest->version.rep->depth;
    bool exhausted = false;
    for (const d7_hamt_ancestral_connection_version* current = &forest->version;
         current != NULL;
         current = d7_hamt_ancestral_connection_version_parent_ref(current)) {
        if (exhausted || current->rep->depth != expected_depth
            || d7_hamt_ancestral_connection_version_root_ref(current)->rep
                != history_root->rep) {
            *valid = false;
            return D7_HAMT_OK;
        }
        d7_hamt_set grown;
        const d7_hamt_status status = d7_hamt_set_add(ancestors, current->rep, &grown);
        if (status != D7_HAMT_OK) {
            return status;
        }
        d7_hamt_set_destroy(ancestors);
        *ancestors = grown;
        if (expected_depth == 0u) {
            exhausted = true;
        } else {
            --expected_depth;
        }
    }
    if (!exhausted
        || !d7_hamt_set_contains(ancestors, history_root->rep)
        || d7_hamt_ancestral_connection_version_parent_ref(history_root) != NULL
        || history_root->rep->depth != 0u) {
        *valid = false;
    }
    return D7_HAMT_OK;
}

/* Checks that every stored cell is canonical: inside the universe, and either a non-singleton root
 * with no tag or a child with no size, a lineage tag, and a stored parent. */
static void d7_hamt_ancestral_check_cells(
    const d7_hamt_ancestral_connection_forest* forest,
    const d7_hamt_set* ancestors,
    bool* valid)
{
    d7_hamt_map_iterator iterator;
    d7_hamt_map_iterator_init(&forest->cells, &iterator);
    const void* key = NULL;
    const void* value = NULL;
    while (d7_hamt_map_iterator_next(&iterator, &key, &value)) {
        const int32_t vertex = *(const int32_t*)key;
        const d7_hamt_ancestral_cell* const cell = (const d7_hamt_ancestral_cell*)value;
        if (!d7_hamt_ancestral_is_vertex(forest, vertex)
            || !d7_hamt_ancestral_is_vertex(forest, cell->parent)) {
            *valid = false;
            return;
        }
        if (cell->parent == vertex) {
            if (cell->size <= 1 || cell->joined_at.rep != NULL) {
                *valid = false;
                return;
            }
        } else if (cell->size != 0
            || cell->joined_at.rep == NULL
            || !d7_hamt_set_contains(ancestors, cell->joined_at.rep)
            || !d7_hamt_map_contains_key(&forest->cells, &cell->parent)) {
            *valid = false;
            return;
        }
    }
}

/* The union-by-size height ceiling: floor(log2(vertex_count)) edges. */
static size_t d7_hamt_ancestral_maximum_height(int32_t vertex_count)
{
    size_t height = 0u;
    uint32_t remaining = vertex_count > 1 ? (uint32_t)vertex_count : 1u;
    while (remaining > 1u) {
        ++height;
        remaining >>= 1;
    }
    return height;
}

/* Walks every stored vertex to its root, checking that union-edge tags strictly increase toward the
 * root and stay on the validated lineage, that the walk respects the union-by-size height ceiling,
 * and recounting how many stored vertices each root owns. */
static d7_hamt_status d7_hamt_ancestral_recount_components(
    const d7_hamt_ancestral_connection_forest* forest,
    const d7_hamt_set* ancestors,
    d7_hamt_map* component_sizes,
    size_t* component_root_count,
    size_t* maximum_parent_path_length,
    bool* valid)
{
    const size_t maximum_height = d7_hamt_ancestral_maximum_height(forest->vertex_count);
    d7_hamt_map_iterator iterator;
    d7_hamt_map_iterator_init(&forest->cells, &iterator);
    const void* key = NULL;
    const void* value = NULL;
    while (d7_hamt_map_iterator_next(&iterator, &key, &value)) {
        int32_t current = *(const int32_t*)key;
        size_t height = 0u;
        uint64_t previous_depth = 0u;
        bool has_previous = false;
        for (;;) {
            const d7_hamt_ancestral_cell* const cell =
                d7_hamt_ancestral_cell_of(forest, current);
            if (cell == NULL || cell->parent == current) {
                break;
            }
            if (cell->joined_at.rep == NULL
                || (has_previous && cell->joined_at.rep->depth <= previous_depth)
                || !d7_hamt_set_contains(ancestors, cell->joined_at.rep)) {
                *valid = false;
                return D7_HAMT_OK;
            }
            previous_depth = cell->joined_at.rep->depth;
            has_previous = true;
            current = cell->parent;
            ++height;
            if (height > maximum_height) {
                *valid = false;
                return D7_HAMT_OK;
            }
        }
        if (height > *maximum_parent_path_length) {
            *maximum_parent_path_length = height;
        }

        const void* counted = NULL;
        int32_t updated = 1;
        if (d7_hamt_map_try_get(component_sizes, &current, &counted)) {
            updated = *(const int32_t*)counted + 1;
        } else {
            ++*component_root_count;
        }
        d7_hamt_map grown;
        const d7_hamt_status status =
            d7_hamt_map_set(component_sizes, &current, &updated, &grown);
        if (status != D7_HAMT_OK) {
            return status;
        }
        d7_hamt_map_destroy(component_sizes);
        *component_sizes = grown;
    }
    return D7_HAMT_OK;
}

/* Checks that every recounted component size matches the count cached on its root. */
static void d7_hamt_ancestral_check_root_sizes(
    const d7_hamt_ancestral_connection_forest* forest,
    const d7_hamt_map* component_sizes,
    bool* valid)
{
    d7_hamt_map_iterator iterator;
    d7_hamt_map_iterator_init(component_sizes, &iterator);
    const void* key = NULL;
    const void* value = NULL;
    while (d7_hamt_map_iterator_next(&iterator, &key, &value)) {
        if (d7_hamt_ancestral_size_of(forest, *(const int32_t*)key)
            != *(const int32_t*)value) {
            *valid = false;
            return;
        }
    }
}

d7_hamt_status d7_hamt_ancestral_connection_forest_debug_validate(
    const d7_hamt_ancestral_connection_forest* forest,
    bool* valid,
    d7_hamt_ancestral_connection_forest_statistics* statistics)
{
    if (!d7_hamt_ancestral_forest_valid(forest) || valid == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    bool holds = forest->vertex_count >= 0
        && forest->component_count >= 0
        && forest->component_count <= forest->vertex_count;

    const d7_hamt_set_policy identity_policy = d7_hamt_ancestral_identity_policy();
    const d7_hamt_policy count_policy = d7_hamt_ancestral_count_policy();
    d7_hamt_set ancestors = d7_hamt_set_create(&identity_policy);
    d7_hamt_map component_sizes = d7_hamt_map_create(&count_policy);
    size_t component_root_count = 0u;
    size_t maximum_parent_path_length = 0u;
    d7_hamt_status status = D7_HAMT_OK;

    if (holds) {
        status = d7_hamt_ancestral_collect_ancestors(forest, &ancestors, &holds);
    }
    if (status == D7_HAMT_OK && holds) {
        d7_hamt_ancestral_check_cells(forest, &ancestors, &holds);
    }
    if (status == D7_HAMT_OK && holds) {
        status = d7_hamt_ancestral_recount_components(
            forest, &ancestors, &component_sizes, &component_root_count,
            &maximum_parent_path_length, &holds);
    }
    if (status == D7_HAMT_OK && holds) {
        const int64_t absent_singletons =
            (int64_t)forest->vertex_count - (int64_t)d7_hamt_map_count(&forest->cells);
        if (absent_singletons < 0
            || absent_singletons + (int64_t)component_root_count
                != (int64_t)forest->component_count) {
            holds = false;
        }
    }
    if (status == D7_HAMT_OK && holds) {
        d7_hamt_ancestral_check_root_sizes(forest, &component_sizes, &holds);
    }

    d7_hamt_map_destroy(&component_sizes);
    d7_hamt_set_destroy(&ancestors);
    if (status != D7_HAMT_OK) {
        return status;
    }

    if (holds && statistics != NULL) {
        statistics->vertex_count = forest->vertex_count;
        statistics->component_count = forest->component_count;
        statistics->stored_cell_count = d7_hamt_map_count(&forest->cells);
        statistics->maximum_parent_path_length = maximum_parent_path_length;
        statistics->history_depth = forest->version.rep->depth;
    }
    *valid = holds;
    return D7_HAMT_OK;
}

bool d7_hamt_ancestral_connection_forest_debug_shares_connectivity_root(
    const d7_hamt_ancestral_connection_forest* left,
    const d7_hamt_ancestral_connection_forest* right)
{
    return d7_hamt_ancestral_forest_valid(left)
        && d7_hamt_ancestral_forest_valid(right)
        && d7_hamt_map_shares_root(&left->cells, &right->cells);
}

const void* d7_hamt_ancestral_connection_forest_debug_root_identity(
    const d7_hamt_ancestral_connection_forest* forest)
{
    return d7_hamt_ancestral_forest_valid(forest)
        ? d7_hamt_map_debug_root_identity(&forest->cells) : NULL;
}
