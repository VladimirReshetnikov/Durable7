/*
 * A persistent monotone-action heap: a Brodal-Okasaki heap whose entries carry pending priority
 * transformations.
 *
 * The skeleton is the bootstrapped skew-binomial representation of ft_brodal_heap - a global
 * minimum root above a skew-binomial forest - with one immutable pending action added to every
 * tree and every forest spine cell. A tree tag denotes a transformation of that tree and all of
 * its descendants; a forest tag denotes the same transformation of every tree in that suffix.
 * Composing one tag at the root is what makes transform-all constant time: it transforms exactly
 * the priorities present at the call, so later insertions and later melds join untransformed and
 * two independently transformed heaps still meld without cross-applying each other's actions.
 * That matters because a monotone action need not be invertible; a clamp has no inverse.
 *
 * Every operation returns a new version and leaves its inputs valid, sharing unchanged structure,
 * so an edit copies a path rather than the whole collection. Insert, minimum, and meld are O(1)
 * worst-case; delete-minimum is O(log n) worst-case; transform-all is O(1) worst-case time and
 * allocates O(1) fresh structure; visiting and validation are Theta(n); retaining a version as a
 * fork is O(1). Those bounds include the O(1)-time action-policy calls and priority comparisons
 * they perform, so a policy whose identity test, composition, or application is not O(1), or whose
 * composed actions are not fixed size, invalidates them.
 *
 * The action policy is trusted. Violating its identity, associative-composition, monotonicity, or
 * O(1)-operation contract invalidates the semantic and complexity guarantees, and the comparer
 * must remain a stable total preorder over retained priorities. Mutating comparer-, policy-, or
 * priority-observable state can retroactively invalidate existing versions.
 */

#ifndef DURABLE7_FINGER_TREE_C_PERSISTENT_MONOTONE_ACTION_HEAP_H
#define DURABLE7_FINGER_TREE_C_PERSISTENT_MONOTONE_ACTION_HEAP_H

#include <durable7/finger_tree/fingertree.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef ft_status (*ft_monotone_action_copy_fn)(
    void* destination,
    const void* source,
    void* context);
typedef void (*ft_monotone_action_destroy_fn)(void* value, void* context);
typedef ft_status (*ft_monotone_action_compare_fn)(
    const void* left,
    const void* right,
    int* comparison,
    void* context);
/* Reports whether one action is the semantic identity. */
typedef ft_status (*ft_monotone_action_is_identity_fn)(
    const void* action,
    bool* is_identity,
    void* context);
/* Composes two actions into uninitialized storage. Compose(outer, inner) always denotes
 * outer(inner(priority)), so inner is the older action. */
typedef ft_status (*ft_monotone_action_compose_fn)(
    void* destination,
    const void* outer,
    const void* inner,
    void* context);
/* Applies one action to one priority, constructing the resulting priority in uninitialized
 * storage. */
typedef ft_status (*ft_monotone_action_apply_fn)(
    void* destination,
    const void* action,
    const void* priority,
    void* context);
typedef void* (*ft_monotone_action_allocate_fn)(size_t size, void* context);
typedef void (*ft_monotone_action_deallocate_fn)(void* allocation, void* context);

typedef struct ft_monotone_action_allocator {
    ft_monotone_action_allocate_fn allocate;
    ft_monotone_action_deallocate_fn deallocate;
    void* context;
} ft_monotone_action_allocator;

/* One erased type. type_identity is a required, stable, non-null caller-owned tag. Copy
 * constructs an owned value in uninitialized storage and must leave that storage ownership-free
 * on failure; a null copy means the type is trivially copyable. destroy requires copy. context is
 * passed to this type's copy and destroy hooks, and to the priority comparer for the priority
 * type. */
typedef struct ft_monotone_action_type_policy {
    size_t size;
    const void* type_identity;
    ft_monotone_action_copy_fn copy;
    ft_monotone_action_destroy_fn destroy;
    void* context;
} ft_monotone_action_type_policy;

/* The identity action is copied into the policy during creation, so the caller's storage need not
 * outlive the call. The algebra hooks receive algebra_context; every hook and context is
 * caller-owned and must outlive every policy and heap version built from this configuration.
 * Hooks must not reenter an in-flight operation through the same policy/heap handles. Distinct
 * immutable handles may be used concurrently only when every reachable hook and context is safe
 * for that call pattern. */
typedef struct ft_monotone_action_policy_config {
    ft_monotone_action_type_policy element;
    ft_monotone_action_type_policy priority;
    ft_monotone_action_type_policy action;
    const void* identity_action;
    ft_monotone_action_compare_fn priority_compare;
    ft_monotone_action_is_identity_fn is_identity;
    ft_monotone_action_compose_fn compose;
    ft_monotone_action_apply_fn apply;
    void* algebra_context;
    ft_monotone_action_allocator allocator;
} ft_monotone_action_policy_config;

/* The shared, reference-counted representation behind a monotone-action heap policy. */
typedef struct ft_monotone_action_policy_rep ft_monotone_action_policy_rep;

/* Policy identity gates melding. Copy preserves identity; independently recreating an equivalent
 * configuration deliberately does not. */
typedef struct ft_monotone_action_policy {
    ft_monotone_action_policy_rep* rep;
} ft_monotone_action_policy;

/* Fills one erased-type policy with its defaults, so a caller can set only the fields it cares
 * about. */
void ft_monotone_action_type_policy_init(
    ft_monotone_action_type_policy* type,
    size_t size,
    const void* type_identity,
    void* context);
/* Fills the configuration with its defaults, so a caller can set only the fields it cares
 * about. */
void ft_monotone_action_policy_config_init(
    ft_monotone_action_policy_config* config,
    const ft_monotone_action_type_policy* element,
    const ft_monotone_action_type_policy* priority,
    const ft_monotone_action_type_policy* action,
    const void* identity_action,
    ft_monotone_action_compare_fn priority_compare,
    ft_monotone_action_is_identity_fn is_identity,
    ft_monotone_action_compose_fn compose,
    ft_monotone_action_apply_fn apply,
    void* algebra_context);
/* Initializes a policy retaining the supplied comparer and action algebra. */
ft_status ft_monotone_action_policy_create(
    ft_monotone_action_policy* policy,
    const ft_monotone_action_policy_config* config);
/* Initializes a second handle on the same policy version, taking a reference rather than
 * copying. */
ft_status ft_monotone_action_policy_copy(
    const ft_monotone_action_policy* source,
    ft_monotone_action_policy* destination);
/* Relocates an initialized policy into another variable, leaving the source uninitialized. A
 * handle whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_monotone_action_policy_move(
    ft_monotone_action_policy* destination,
    ft_monotone_action_policy* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void ft_monotone_action_policy_dispose(ft_monotone_action_policy* policy);
/* Whether both handles denote the same policy identity. */
bool ft_monotone_action_policy_same(
    const ft_monotone_action_policy* left,
    const ft_monotone_action_policy* right);

/* One tagged skew-binomial tree inside a monotone-action heap. Opaque. */
typedef struct ft_monotone_action_tree ft_monotone_action_tree;

/* An immutable heap version. The global root is kept in exposed form, so its stored priority is
 * always the logical minimum priority and can be borrowed directly. */
typedef struct ft_monotone_action_heap {
    ft_monotone_action_policy_rep* policy;
    ft_monotone_action_tree* root;
    size_t count;
} ft_monotone_action_heap;

/* One payload paired with its logical priority, borrowed from the version that stores it. */
typedef struct ft_monotone_action_entry_ref {
    const void* element;
    const void* priority;
} ft_monotone_action_entry_ref;

/* One payload paired with the priority it is inserted under. */
typedef struct ft_monotone_action_entry_input {
    const void* element;
    const void* priority;
} ft_monotone_action_entry_input;

typedef struct ft_monotone_action_heap_statistics {
    size_t count;
    size_t root_forest_length;
    unsigned maximum_rank;
    size_t maximum_depth;
    /* Pending tree/forest-tag observations made during validation. This is not a distinct-object
     * count, because exposing one uniform forest tag produces several tagged logical views of the
     * same retained cells. */
    size_t tagged_component_count;
} ft_monotone_action_heap_statistics;

/* Receives every entry with its pending actions already composed. Returning non-OK aborts the
 * traversal and propagates. */
typedef ft_status (*ft_monotone_action_visit_fn)(
    const void* element,
    const void* priority,
    void* context);

/* Initializes the heap in place. */
ft_status ft_monotone_action_heap_init(
    ft_monotone_action_heap* heap,
    const ft_monotone_action_policy* policy);
/* Initializes a heap holding the given entries, built in O(n) time by repeated worst-case-O(1)
 * insertion. */
ft_status ft_monotone_action_heap_from_array(
    ft_monotone_action_heap* heap,
    const ft_monotone_action_policy* policy,
    const ft_monotone_action_entry_input* entries,
    size_t count);
/* Initializes a second handle on the same heap version, taking a reference rather than copying. */
ft_status ft_monotone_action_heap_copy(
    const ft_monotone_action_heap* source,
    ft_monotone_action_heap* destination);
/* Relocates an initialized heap into another variable, leaving the source uninitialized. A handle
 * whose nested handles point at policy state it embeds must be moved rather than copied
 * bytewise. */
void ft_monotone_action_heap_move(
    ft_monotone_action_heap* destination,
    ft_monotone_action_heap* source);
/* Releases this handle's reference. Other versions sharing the same nodes stay valid. */
void ft_monotone_action_heap_dispose(ft_monotone_action_heap* heap);

/* Whether the heap holds no entries. */
bool ft_monotone_action_heap_empty(const ft_monotone_action_heap* heap);
/* Number of entries in the heap. */
size_t ft_monotone_action_heap_size(const ft_monotone_action_heap* heap);

/* Reads a minimum entry in O(1) worst-case time. On success, minimum_ref borrows the stored
 * representatives and remains valid while the source version is retained. */
ft_status ft_monotone_action_heap_try_get_minimum_ref(
    const ft_monotone_action_heap* heap,
    bool* found,
    ft_monotone_action_entry_ref* minimum_ref);
/* Reads a minimum entry in O(1) worst-case time. On found=true, element and priority are
 * independently owned and must be destroyed with their type policies' destroy callbacks when
 * non-null. Both are withheld on any failure. */
ft_status ft_monotone_action_heap_try_get_minimum_copy(
    const ft_monotone_action_heap* heap,
    bool* found,
    void* element,
    void* priority);

/* Results are published only on success. Exact result/operand aliasing is supported. */

/* Inserts one entry in O(1) worst-case time. The entry joins untransformed: actions applied
 * before the call never reach it. */
ft_status ft_monotone_action_heap_insert(
    const ft_monotone_action_heap* heap,
    const void* element,
    const void* priority,
    ft_monotone_action_heap* result);
/* Applies one action to every priority present in this version in O(1) worst-case time,
 * allocating O(1) fresh structure. The semantics are temporal: later insertions and later melds
 * are not retroactively transformed. An empty heap or an identity action retains this version's
 * root rather than rebuilding it. */
ft_status ft_monotone_action_heap_transform_all(
    const ft_monotone_action_heap* heap,
    const void* action,
    ft_monotone_action_heap* result);
/* Produces the heap holding both operands' entries in O(1) worst-case time: melding links two
 * forests rather than merging their contents. Each operand keeps its own action history, so
 * neither heap's pending actions reach the other's entries even when an action has no inverse.
 * Requires exact policy identity and fails with FT_STATUS_INCOMPATIBLE_POLICY otherwise.
 * Empty-side melds retain the nonempty operand's root, while self-meld preserves both logical
 * occurrences. */
ft_status ft_monotone_action_heap_meld(
    const ft_monotone_action_heap* left,
    const ft_monotone_action_heap* right,
    ft_monotone_action_heap* result);
/* Produces the heap without one minimum entry in O(log n) worst-case time. An empty heap fails
 * with FT_STATUS_EMPTY. */
ft_status ft_monotone_action_heap_delete_minimum(
    const ft_monotone_action_heap* heap,
    ft_monotone_action_heap* result);
/* Produces the heap without one minimum entry in O(log n) worst-case time, reading that entry
 * out. On removed=true, element and priority receive independently owned copies of the exact
 * removed representatives. The copies and the remainder are all withheld on any failure,
 * including when result aliases heap. */
ft_status ft_monotone_action_heap_try_delete_minimum(
    const ft_monotone_action_heap* heap,
    bool* removed,
    void* element,
    void* priority,
    ft_monotone_action_heap* result);

/* Visits every entry in Theta(n), in unspecified structural order. Every visited priority is
 * fully composed: children are reached only through the tag-composing accessors, so a pending
 * action is never skipped and never applied twice. The traversal is explicit-stack and visits
 * logical occurrences, including duplicated shared subgraphs produced by self-meld. */
ft_status ft_monotone_action_heap_visit(
    const ft_monotone_action_heap* heap,
    ft_monotone_action_visit_fn visitor,
    void* context);

/* The root node's address, for tests that a no-op shared rather than copied. */
const void* ft_monotone_action_heap_root_identity(const ft_monotone_action_heap* heap);
/* Validates rank encodings, logical heap order through every pending action, and count in
 * Theta(n). Structural invalidity is FT_STATUS_OK with valid=false. Allocation and callback
 * failures remain distinguishable and outputs change only on success. */
ft_status ft_monotone_action_heap_validate(
    const ft_monotone_action_heap* heap,
    bool* valid,
    ft_monotone_action_heap_statistics* statistics);

/*
 * The shipped clamp family: the closed monotone action family x -> clamp(x, lower, upper) over an
 * ordered priority domain, containing the identity, a floor, a cap, a validated two-sided clamp,
 * and an exact constant. A missing bound denotes negative or positive infinity, and composition
 * always yields either one constant-sized clamp or an explicit constant function, so composition
 * and application are O(1). The explicit constant case preserves exact results even when the
 * comparer treats distinct priority values as equal: an inclusive [k, k] clamp preserves an input
 * comparer-equal to k, whereas the constant function must return the exact k representative. For
 * overlapping clamps, composition keeps the older boundary when two boundary values compare
 * equal.
 */

/* One retained clamp boundary or constant. Opaque and reference-counted, so composition and
 * application hand back the exact representative they selected. */
typedef struct ft_monotone_action_clamp_bound ft_monotone_action_clamp_bound;

/* One clamp action value. The fields are opaque; use the accessors. A clamp owns references and
 * must be released with ft_monotone_action_clamp_dispose. */
typedef struct ft_monotone_action_clamp {
    ft_monotone_action_clamp_bound* constant;
    ft_monotone_action_clamp_bound* lower;
    ft_monotone_action_clamp_bound* upper;
} ft_monotone_action_clamp;

/* The shared, reference-counted representation behind a clamp family. */
typedef struct ft_monotone_action_clamp_family_rep ft_monotone_action_clamp_family_rep;

/* A clamp family names the exact comparer it is monotone for. Configuring a heap policy from the
 * family adopts that comparer, so the two can never diverge. */
typedef struct ft_monotone_action_clamp_family {
    ft_monotone_action_clamp_family_rep* rep;
} ft_monotone_action_clamp_family;

/* Initializes a clamp family over one ordered priority type, retaining its comparer. */
ft_status ft_monotone_action_clamp_family_create(
    ft_monotone_action_clamp_family* family,
    const ft_monotone_action_type_policy* priority,
    ft_monotone_action_compare_fn compare,
    const ft_monotone_action_allocator* allocator);
/* Initializes a second handle on the same family, taking a reference rather than copying. */
ft_status ft_monotone_action_clamp_family_copy(
    const ft_monotone_action_clamp_family* source,
    ft_monotone_action_clamp_family* destination);
/* Relocates an initialized family into another variable, leaving the source uninitialized. */
void ft_monotone_action_clamp_family_move(
    ft_monotone_action_clamp_family* destination,
    ft_monotone_action_clamp_family* source);
/* Releases this handle's reference. Actions and heaps built from the family stay valid only while
 * some handle on it is retained. */
void ft_monotone_action_clamp_family_dispose(
    ft_monotone_action_clamp_family* family);
/* Whether both handles denote the same family identity. */
bool ft_monotone_action_clamp_family_same(
    const ft_monotone_action_clamp_family* left,
    const ft_monotone_action_clamp_family* right);
/* Fills a heap policy configuration with this family's priority type, comparer, action type, and
 * action algebra, leaving the caller to supply only the payload type. */
void ft_monotone_action_clamp_family_config_init(
    const ft_monotone_action_clamp_family* family,
    ft_monotone_action_policy_config* config,
    const ft_monotone_action_type_policy* element);

/* Initializes the identity action, which leaves every priority unchanged. */
void ft_monotone_action_clamp_init_identity(ft_monotone_action_clamp* action);
/* Initializes a floor action. */
ft_status ft_monotone_action_clamp_at_least(
    const ft_monotone_action_clamp_family* family,
    const void* lower_bound,
    ft_monotone_action_clamp* action);
/* Initializes a cap action. */
ft_status ft_monotone_action_clamp_at_most(
    const ft_monotone_action_clamp_family* family,
    const void* upper_bound,
    ft_monotone_action_clamp* action);
/* Initializes a two-sided clamp action. Fails with FT_STATUS_INVALID_ARGUMENT when the lower
 * bound compares greater than the upper bound. */
ft_status ft_monotone_action_clamp_between(
    const ft_monotone_action_clamp_family* family,
    const void* lower_bound,
    const void* upper_bound,
    ft_monotone_action_clamp* action);
/* Initializes an exact constant action. */
ft_status ft_monotone_action_clamp_constant(
    const ft_monotone_action_clamp_family* family,
    const void* value,
    ft_monotone_action_clamp* action);
/* Initializes a second handle on the same action, taking references rather than copying. */
ft_status ft_monotone_action_clamp_copy(
    const ft_monotone_action_clamp_family* family,
    const ft_monotone_action_clamp* source,
    ft_monotone_action_clamp* destination);
/* Releases this action's references. Other actions sharing the same boundaries stay valid. */
void ft_monotone_action_clamp_dispose(
    const ft_monotone_action_clamp_family* family,
    ft_monotone_action_clamp* action);

/* Whether the action leaves every priority unchanged. */
bool ft_monotone_action_clamp_is_identity(const ft_monotone_action_clamp* action);
/* Whether the action is an exact constant function. */
bool ft_monotone_action_clamp_is_constant(const ft_monotone_action_clamp* action);
/* Borrows the constant result, or null when the action is not constant. */
const void* ft_monotone_action_clamp_constant_ref(
    const ft_monotone_action_clamp* action);
/* Borrows the lower bound, or null when the action has none. */
const void* ft_monotone_action_clamp_lower_bound_ref(
    const ft_monotone_action_clamp* action);
/* Borrows the upper bound, or null when the action has none. */
const void* ft_monotone_action_clamp_upper_bound_ref(
    const ft_monotone_action_clamp* action);

#ifdef __cplusplus
}
#endif

#endif
