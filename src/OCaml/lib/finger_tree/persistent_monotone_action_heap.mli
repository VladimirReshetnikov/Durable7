(** Persistent Brodal-Okasaki heap with lazily composable monotone priority actions.

    This is the action-tagged sibling of the plain meldable heap. It keeps the bootstrapped
    skew-binomial representation - a global minimum root above a skew-binomial forest - and adds one
    immutable {e pending action} to every tree and to every forest spine cell. A tree tag applies to
    that tree and to all of its descendants; a forest tag applies uniformly to every tree in that
    spine suffix.

    The tags exist for {!transform_all}, which applies one monotone action to every priority
    {e currently} present by composing a single tag at the root: [O(1)] worst case, allocating
    [O(1)] new structure and resharing the whole child forest. Its semantics are deliberately
    temporal. Entries inserted later, and entries arriving through a later meld, are never
    retroactively transformed, and two independently transformed heaps still meld in [O(1)] worst
    case even when the actions have no inverse - a clamp does not.

    {!insert}, {!minimum}, and {!meld} are [O(1)] worst case; {!delete_minimum} is [O(log n)] worst
    case; {!to_list} and {!validate} are [Theta(n)]; retaining a version as a fork is free. Those
    bounds include the policy's own identity, composition, and application calls together with the
    priority comparisons they drive, so a policy whose operations are not [O(1)] over fixed-size
    actions invalidates them.

    The action policy is trusted. Violating its identity, associative-composition, monotonicity, or
    constant-time contract invalidates both the semantic and the complexity guarantees. Every
    operation is persistent and leaves its operands usable, and a rejected operation publishes
    nothing. *)

type ('priority, 'action) policy
(** A retained monotone-action algebra together with the priority ordering it is monotone for. *)

type ('element, 'priority, 'action) t
(** An immutable heap version. Every version retains the policy it was built with, so an empty
    result stays usable for further insertion, melding, and transformation. *)

type ('element, 'priority) entry = { element : 'element; priority : 'priority }
(** A payload paired with its current logical priority, every pending action already applied. *)

type statistics = {
  count : int;
  root_forest_length : int;
  maximum_rank : int;
  maximum_depth : int;
  tagged_component_count : int;
}
(** Shape measurements returned by a structural audit. [root_forest_length] counts the skew-binomial
    trees directly below the global root and [maximum_depth] measures the deepest root-to-tree path.
    [tagged_component_count] counts pending tree and spine-cell tag observations made during the
    walk; it is not a distinct-object count, because exposing one uniform spine tag yields several
    tagged logical views of the same retained cells. *)

type 'value clamp
(** One member of the shipped closed action family [x -> clamp (x, lower, upper)]: the identity, a
    floor, a cap, a validated two-sided clamp, or an exact constant function. A missing bound
    denotes negative or positive infinity. *)

(** {1 Action policies} *)

val create_policy :
  id:string ->
  identity:'action ->
  is_identity:('action -> bool) ->
  compose:('action -> 'action -> 'action) ->
  apply:('action -> 'priority -> 'priority) ->
  comparator:'priority Common.Comparator.t ->
  laws_verified:bool ->
  unit ->
  (('priority, 'action) policy, string) result
(** An action policy from its identity, identity recognition, composition, application, and the
    priority comparator it is monotone for. [compose outer inner] must denote [outer (inner p)],
    and composition must be associative. Every action must be monotone for [comparator]: if [p]
    orders at or before [q] then [apply a p] must order at or before [apply a q]. All four algebra
    calls must be [O(1)] over fixed-size actions, because the heap's advertised bounds include
    them. The [id] is the policy's declared identity: it must not be blank, and two policies
    sharing it are declared interchangeable by {!meld}. Fails for a blank [id] or when the caller
    has not asserted [laws_verified]. *)

val policy_id : ('priority, 'action) policy -> string
(** The policy's declared identity. *)

val policy_comparator : ('priority, 'action) policy -> 'priority Common.Comparator.t
(** The priority comparator this policy's actions are monotone for. *)

val policy_identity : ('priority, 'action) policy -> 'action
(** The algebra's identity action, whose application is a no-op. *)

val policy_is_identity : ('priority, 'action) policy -> 'action -> bool
(** Whether an action is the algebra's semantic identity. *)

val compose_actions : ('priority, 'action) policy -> 'action -> 'action -> 'action
(** [compose_actions policy outer inner] composes two actions, the outer applied after the inner.
    The outer one is the newer of the two at every tag-pushdown site inside the heap. *)

val apply_action : ('priority, 'action) policy -> 'action -> 'priority -> 'priority
(** Applies one action to one priority. *)

(** {1 The shipped clamp family} *)

val clamp_policy :
  id:string -> 'value Common.Comparator.t -> (('value, 'value clamp) policy, string) result
(** A policy over {!type-clamp} actions that is monotone for the supplied comparator. Composition
    yields either one constant-sized clamp or an explicit constant function, so it stays [O(1)].
    Identity operands short-circuit; an outer constant wins outright; an inner constant has the
    outer action applied to it; bounds that cannot both be met collapse to a constant carrying the
    outer (newer) boundary; overlapping bounds intersect, keeping the inner (older) representative
    when two boundaries compare equal. Application tests the constant first, then the lower bound,
    then the upper bound. The explicit constant case is what preserves exact results when the
    comparator treats distinct priority values as equal. Fails for a blank [id]. *)

val clamp_identity : 'value clamp
(** The clamp that leaves every priority alone. *)

val clamp_at_least : 'value -> 'value clamp
(** A floor action raising every priority below the bound up to it. *)

val clamp_at_most : 'value -> 'value clamp
(** A cap action lowering every priority above the bound down to it. *)

val clamp_constant : 'value -> 'value clamp
(** An exact constant action returning this very representative for every priority. *)

val clamp_between :
  ('value, 'value clamp) policy -> lower:'value -> upper:'value -> ('value clamp, string) result
(** A two-sided clamp, validated with the policy's own comparator so the bounds are checked by the
    same ordering that will apply them. Fails when [lower] compares greater than [upper]. *)

val clamp_is_constant : 'value clamp -> bool
(** Whether the action is an exact constant function. *)

val clamp_constant_value : 'value clamp -> 'value option
(** The exact constant result, or [None] when the action is not constant. *)

val clamp_lower_bound : 'value clamp -> 'value option
(** The lower bound, or [None] when the action has none. *)

val clamp_upper_bound : 'value clamp -> 'value option
(** The upper bound, or [None] when the action has none. *)

(** {1 Construction} *)

val empty : ('priority, 'action) policy -> ('element, 'priority, 'action) t
(** The empty heap retaining an action policy and the comparator that policy names. *)

val of_entries :
  ('priority, 'action) policy -> ('element * 'priority) list -> ('element, 'priority, 'action) t
(** A heap holding a list's entries, built in [O(n)] by repeated worst-case-[O(1)] insertion. *)

(** {1 Reading} *)

val policy : ('element, 'priority, 'action) t -> ('priority, 'action) policy
(** The retained action policy. *)

val comparator : ('element, 'priority, 'action) t -> 'priority Common.Comparator.t
(** The retained priority comparator, which is the one the policy names. *)

val count : ('element, 'priority, 'action) t -> int
(** Number of entries in the heap. *)

val is_empty : ('element, 'priority, 'action) t -> bool
(** Whether the heap holds no entries. *)

val minimum : ('element, 'priority, 'action) t -> ('element, 'priority) entry option
(** A minimum entry in [O(1)] worst case, or [None] when the heap is empty. *)

val to_list : ('element, 'priority, 'action) t -> ('element, 'priority) entry list
(** Every entry in [Theta(n)], in unspecified structural order, each priority fully composed. *)

(** {1 Updating} *)

val insert :
  'element -> 'priority -> ('element, 'priority, 'action) t -> ('element, 'priority, 'action) t
(** A heap containing the given entry, in [O(1)] worst case. The new entry joins untransformed: no
    earlier {!transform_all} action reaches it. A tie at the root is resolved in favour of the new
    entry. *)

val transform_all : 'action -> ('element, 'priority, 'action) t -> ('element, 'priority, 'action) t
(** A heap whose every {e current} priority has the action applied, in [O(1)] worst case and [O(1)]
    new structure. Later insertions and later melds are not retroactively transformed. An empty heap
    or an identity action returns the receiver itself. *)

val meld :
  ('element, 'priority, 'action) t ->
  ('element, 'priority, 'action) t ->
  (('element, 'priority, 'action) t, string) result
(** The heap holding both operands' entries, in [O(1)] worst case. Each operand keeps its own action
    history: neither heap's pending actions reach the other's entries, even for actions with no
    inverse. Melding a heap with itself therefore duplicates every logical occurrence. Compatibility
    follows the retained-identity precedent of the tag algebras elsewhere in this family: the
    operands' policies must carry the same {!policy_id}, which is the caller's declaration that the
    two algebras are interchangeable, and the result retains the left operand's policy. Fails when
    the declared identities differ. *)

val delete_minimum : ('element, 'priority, 'action) t -> ('element, 'priority, 'action) t option
(** A heap without one minimum entry, in [O(log n)] worst case, or [None] when the heap is empty. *)

val minimum_view :
  ('element, 'priority, 'action) t ->
  (('element, 'priority) entry * ('element, 'priority, 'action) t) option
(** A minimum entry together with the heap remaining, or [None] when the heap is empty. *)

(** {1 Auditing} *)

val validate : ('element, 'priority, 'action) t -> (statistics, string) result
(** Audits the whole representation in [Theta(n)] and reports its shape. Independently checks that
    the global root has rank zero, that every tree's fused primitive-child encoding walks down to a
    well-formed embedded forest, that every forest has nondecreasing ranks with at most the first
    two equal, that every child's logical priority orders at or after its parent's through all
    composed pending tags, and that the traversal reaches exactly {!count} entries. *)

val shares_root_with :
  ('element, 'priority, 'action) t -> ('element, 'priority, 'action) t -> bool
(** Whether two versions retain the very same root node. This is what proves that a no-op update
    reused its source instead of rebuilding it. *)

val shares_root_children_with :
  ('element, 'priority, 'action) t -> ('element, 'priority, 'action) t -> bool
(** Whether two versions' roots retain the very same child forest. A whole-heap transform composes
    one tag at the root, so its successor must reshare the receiver's entire child forest; this is
    the structural half of the [O(1)]-allocation claim. *)
