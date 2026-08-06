(** Fixed-length persistent vector carrying a checkpoint and an exact maximal-run index of the
    positions whose current values differ from that checkpoint.

    A version keeps two logical views of one fixed-length sequence — the current values and a
    checkpoint — plus one ordered map holding a [start -> end_exclusive] record for every {e
    maximal} run of differing positions. Dirtiness is relative to the retained [equality] relation,
    not to write history: writing a relation-equal value is a semantic no-op, and returning a
    position to the checkpoint's equality class cancels that position's delta and restores the exact
    checkpoint representative.

    Let [n] be the fixed length, [k] the number of dirty positions, and [r] the number of maximal
    dirty runs, so [0 <= r <= k <= n]. Against an unindexed pair of current and checkpoint roots the
    one strict result is that exact dirty-run descriptor discovery is output-optimal [Theta (r)]
    rather than worst-case [Theta (n)]. The gap is unbounded: one contiguous changed block gives
    [k = n] and [r = 1]. Emitting the changed payload {e values} is still [Omega (k)] and is not
    claimed to be faster. The run index is an application of the discrete interval encoding tree
    idea, not a new interval algorithm.

    Shipped bounds, stated against this workspace's own substrate rather than against the reference
    port's:

    - Build [n] values: [Theta (n)].
    - {!length}, {!dirty_count}, {!dirty_run_count}, {!has_changes}: [O(1)].
    - {!nth} and {!checkpoint_nth}: [O(log n)] worst case.
    - {!set} and {!reset}: [O(log n)] for the root edit plus [O(r)] for the run-index edit.
    - Fork by retaining a value: [O(1)]. {!checkpoint} and {!rollback}: [O(1)] root swaps.
    - {!to_list} and {!checkpoint_to_list}: [Theta (n)].
    - {!is_dirty} and {!dirty_run_containing}: [O(log r)] worst case.
    - {!dirty_run_at}: [O(1)]. {!dirty_runs}: [Theta (r)].
    - {!accept_dirty_run_at}, {!revert_dirty_run_at}, {!accept_dirty_run_containing} and
      {!revert_dirty_run_containing}: [O(log n)] for the splice, {e independent of the selected
      run's length}, plus [O(r)] for the run-index edit, and {e no} calls to the retained relation
      at all.
    - {!validate}: [Theta (n + r)] plus [n] relation calls. An auditor, not a hot-path operation.

    Two substrate facts back those numbers and both differ from the reference ports. The vector
    roots are [Rrb_vector], a facade over the lazy finger-tree measured core rather than a real
    32-way radix trie, so indexed read, split and concatenation are [O(log n)] {e amortized}: a
    call may force memoized deferred spine work, paid once per suspension across all versions.
    (Until the Wave-1 core upgrade the facade's substrate was eager and these were worst-case
    bounds; the pending Wave-2 rebuild onto a genuine RRB restores the reference shape outright.) The run index is [Sorted_map],
    a sorted array, so a key lookup is [O(log r)] worst case and rank selection is [O(1)], but
    inserting or replacing a record copies the array, which is [Theta (r)] rather than [O(log r)].
    Run-index mutation is consequently the one place where this port is asymptotically weaker than
    its C# and Rust baselines. It never affects the load-bearing property that accepting or
    reverting a run costs the same whether the run covers one position or a million.

    Accepting or reverting a run is implemented by splitting both roots at the run's boundaries and
    concatenating the pieces, never by walking the run's positions. That is what makes it
    independent of the run's length and free of value comparisons.

    Length is fixed at construction: there is deliberately no insertion, deletion, concatenation,
    split, range assignment, or merge of unrelated branches, because checkpoint correspondence would
    need a position-transformation policy this abstract data type does not define. There is one
    checkpoint per version.

    Every version is immutable and every producing operation leaves its receiver usable, so any
    retained version can be branched again. Both relation calls in an edit happen before any
    successor is constructed, so a relation that raises publishes no partial version. *)

type 'value equality
(** A retained relation deciding which two values count as the same for checkpoint accounting.

    The relation is load-bearing rather than incidental: it decides which writes are semantic no-ops
    and when a recorded change cancels against its checkpoint, so it determines a version's
    observable change set.

    {b The caller must supply a true equivalence relation} — reflexive, symmetric and transitive —
    and it must stay stable and free of observable side effects for as long as any version retaining
    it is alive. Values held by a version must not mutate state the relation observes; replacing
    such a value through {!set} is the supported way to change relation-observable state.

    Reflexivity is the obligation that is easy to violate by accident. OCaml's [Stdlib.( = )] is
    {e not} reflexive on floating point: [nan = nan] is [false], and the same holds structurally for
    any payload containing a [nan]. A non-reflexive relation silently corrupts run accounting —
    rewriting a position with the value already there is recorded as a change, leaving a phantom run
    — so [Stdlib.( = )] must not be handed to [equality] for float-bearing payloads. Use
    {!default}, which is built on [Stdlib.compare] and {e is} reflexive on [nan], or
    {!reflexive_ieee} for raw [float]. {!validate} additionally rejects any version whose relation
    is observably non-reflexive at a clean position, so the mistake fails loudly here rather than
    silently. *)

val equality : ('value -> 'value -> bool) -> 'value equality
(** A distinct custom relation. Two independently created custom relations are never reported as the
    same policy by {!same_equality}, because there is no way to prove two functions agree. *)

val default : unit -> 'value equality
(** The canonical relation using the platform's own comparison, [Stdlib.compare x y = 0].

    Unlike [Stdlib.( = )] this is reflexive on [nan] at every nesting depth and puts [-0.] and [0.]
    in one class, which is exactly what the C# baseline's default comparer does. It raises on
    functional values, as polymorphic comparison always does. *)

val reflexive_ieee : unit -> float equality
(** The canonical reflexive IEEE-754 relation for raw [float] payloads, [Float.equal].

    Every [nan] is equivalent to every other [nan] and [-0.] is equivalent to [0.]. It agrees with
    {!default} at type [float] but is monomorphic, so it neither boxes nor risks the polymorphic
    comparison's failure modes; the two are nevertheless reported as distinct policy identities. *)

val equal : 'value equality -> 'value -> 'value -> bool
(** Whether the relation places two values in the same equivalence class. *)

val same_equality : 'value equality -> 'value equality -> bool
(** Whether both handles denote the same relation identity. Custom relations match only themselves;
    canonical relations match across independent construction. *)

type run = { start : int; length : int }
(** One maximal half-open run of checkpoint-relative changes, covering the positions [start] through
    [start + length] exclusive. A run is never empty, and the runs published by one version are
    ordered, non-overlapping, non-adjacent and maximal. *)

val run_end_exclusive : run -> int
(** The first position after the run. *)

val run_contains : int -> run -> bool
(** Whether the position lies inside the run. *)

type statistics = { count : int; dirty_count : int; dirty_run_count : int }
(** Cardinalities recounted from the run index by a successful audit. *)

type 'value t
(** One immutable version: a fixed-length current root, a checkpoint root, the maximal-run index,
    and the retained relation. *)

val empty : 'value equality -> 'value t
(** The clean empty vector retaining the relation. *)

val of_list : 'value equality -> 'value list -> 'value t
(** A clean fixed-length vector holding the list's values in positional order, built in bulk rather
    than by repeated insertion. Both roots start as the same root, so the version begins with no
    dirty runs. *)

val value_equality : 'value t -> 'value equality
(** The retained relation defining checkpoint-relative value equality. *)

val length : 'value t -> int
(** The fixed number of positions. *)

val is_empty : 'value t -> bool
(** Whether the vector has no positions. *)

val dirty_count : 'value t -> int
(** The number of positions whose current value differs from the checkpoint. *)

val dirty_run_count : 'value t -> int
(** The number of maximal dirty runs. *)

val has_changes : 'value t -> bool
(** Whether at least one current value differs from its checkpoint value. *)

val nth : int -> 'value t -> 'value option
(** The current value at the position, or [None] when the position is outside the vector. *)

val checkpoint_nth : int -> 'value t -> 'value option
(** The checkpoint value at the position, or [None] when the position is outside the vector. *)

val to_list : 'value t -> 'value list
(** The current values, in positional order. *)

val checkpoint_to_list : 'value t -> 'value list
(** The checkpoint values, in positional order. *)

val is_dirty : int -> 'value t -> bool option
(** Whether the position differs from its checkpoint value, or [None] when the position is outside
    the vector. This is the accessor that separates a clean position from an absent one. *)

val dirty_run_containing : int -> 'value t -> run option
(** The maximal dirty run containing the position, or [None] when that position is clean or outside
    the vector. *)

val dirty_run_at : int -> 'value t -> run option
(** The maximal dirty run at the zero-based ascending-start rank, or [None] when the rank falls
    outside the run index. *)

val dirty_runs : 'value t -> run list
(** The maximal dirty runs in ascending position order. Output-optimal: the cost tracks the number
    of runs, not how many positions they cover. *)

val set : int -> 'value -> 'value t -> ('value t, string) result
(** A version with the current value at the position replaced.

    A relation-equal replacement is a semantic no-op: the receiver itself comes back, so every root
    is shared and no dirty or run accounting changes. A replacement landing back in the checkpoint's
    equality class cancels that position's delta and restores the {e exact} checkpoint
    representative; when the last dirty position clears, the current root snaps back onto the
    checkpoint root. Otherwise the position becomes or stays dirty and at most two neighbouring run
    records change. Fails when the position is outside the vector. *)

val reset : int -> 'value t -> ('value t, string) result
(** A version whose current value at the position is reset to its checkpoint value. An already clean
    position returns the receiver. Fails when the position is outside the vector. *)

val checkpoint : 'value t -> 'value t
(** A version accepting every current value as its new checkpoint, in [O(1)]. An already clean
    version returns the receiver. *)

val rollback : 'value t -> 'value t
(** A version reverting every current value to the checkpoint, in [O(1)]. An already clean version
    returns the receiver. *)

val accept_dirty_run_at : int -> 'value t -> ('value t, string) result
(** A version in which the maximal dirty run at the given rank has been accepted into the checkpoint
    and every other run is untouched. Spliced structurally, so the cost is independent of the run's
    length and no relation call is made. Fails when the rank falls outside the run index. *)

val revert_dirty_run_at : int -> 'value t -> ('value t, string) result
(** A version in which the maximal dirty run at the given rank has been reverted to the checkpoint
    and every other run is untouched. Spliced structurally, so the cost is independent of the run's
    length and no relation call is made. Fails when the rank falls outside the run index. *)

val accept_dirty_run_containing : int -> 'value t -> ('value t, string) result
(** A version in which the maximal dirty run containing the given {e position} has been accepted
    into the checkpoint. The argument is a position, not a run rank, so a descriptor obtained from
    {!dirty_run_containing} can be acted on without rediscovering its rank. A clean position is
    vacuous rather than an error and returns the receiver. Fails when the position is outside the
    vector. *)

val revert_dirty_run_containing : int -> 'value t -> ('value t, string) result
(** A version in which the maximal dirty run containing the given {e position} has been reverted to
    the checkpoint. The argument is a position, not a run rank. A clean position is vacuous rather
    than an error and returns the receiver. Fails when the position is outside the vector. *)

val shares_current_root : 'value t -> 'value t -> bool
(** Whether two versions are backed by the same current root. A representation test, not an equality
    test: it answers physically and is only meaningful between two genuinely distinct versions. *)

val shares_checkpoint_root : 'value t -> 'value t -> bool
(** Whether two versions are backed by the same checkpoint root. A representation test, not an
    equality test. *)

val reuses_checkpoint_representative : int -> 'value t -> bool option
(** Whether the position's current slot is physically the very slot its checkpoint holds, or [None]
    when the position is outside the vector.

    This is the diagnostic behind the cleanliness invariant. Every value is stored in its own
    identity-bearing slot precisely so that this question has an answer independent of the retained
    relation, and independent of whether the payload type is boxed: a cancelled write must restore
    the checkpoint's own slot, not merely a relation-equal or structurally equal value. *)

val validate : 'value t -> (statistics, string) result
(** An independent audit of one version, returning its recounted cardinalities.

    It checks that both roots have the same length, that the dirty cardinality is in range, that a
    version with no runs is canonicalized onto its checkpoint root, that the run records are
    ordered, non-empty, in range, non-overlapping, non-adjacent and therefore maximal, that the run
    lengths sum to the dirty cardinality, that every clean position reuses its exact checkpoint
    slot, that the retained relation is reflexive at every clean position, and that every dirty
    position is genuinely not relation-equal to its checkpoint value. *)
