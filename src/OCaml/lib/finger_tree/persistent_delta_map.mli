(** Checkpoint-differential persistent sorted map.

    A value holds three immutable roots: the {e checkpoint} state, the {e current} state, and an
    ordered {e change index} carrying exactly one [(before, after)] record for every key class on
    which the two states differ. It answers one narrow question cheaply: which key classes differ
    from the last checkpoint, and what were their checkpoint and current values? A plain persistent
    map makes versions cheap but a root handle alone does not name the changed keys; a write log
    names operations but leaves repeated writes, cancellations, and ordering to be resolved later.
    This structure maintains the exact net answer online, while the edits happen.

    The first effective write to a class captures its checkpoint-relative [before]; later writes
    replace only [after], so repeated writes coalesce into one record. Returning a class to its
    checkpoint state removes its record entirely, so a set-then-restore and an add-then-remove both
    cancel. When the change index becomes empty the current root snaps back to the {e exact}
    checkpoint root, so a wholly cancelled epoch is storage-clean as well as extensionally clean and
    {!checkpoint} and {!rollback} stay [O(1)] root swaps.

    {1 Policies}

    Key identity and enumeration order come from a retained [Common.Comparator.t]: two keys name the
    same class exactly when the comparator reports them equal. Semantic no-ops and change
    cancellation come from a retained value-equality relation. Both are retained on the value rather
    than passed per call, so an empty or wholly cancelled map stays usable and the relation that
    decided a cancellation is the same one that will decide the next write. "Exact" therefore means
    exact {e extensionally} under those two policies, not by identity of the retained key or value
    representatives. The comparator must be a stable total preorder and the relation a stable
    equivalence; a relation that is not reflexive (IEEE [nan] equality, for instance) would record a
    write of a value over itself as a change that never cancels.

    A baseline key representative is retained across point updates and delete/re-add round trips. A
    newly added class retains its first representative while its net-added record stays active;
    after complete cancellation a later addition begins a new representative episode.

    {1 Bounds delivered by the substrate}

    Let [N] be the number of key classes in the current state and [k] the number of net-changed
    classes. This port is built on [Sorted_map], now an order-statistic sorted map over the lazy
    Hinze–Paterson measured core rather than the immutable sorted array it began as. The Wave-2
    rebuild flips this module's write bounds to the C# baseline's shape with no change to this
    file's logic: writes went from [Theta (N + k)] to [O(log N)], and extremes stay [O(1)] — now
    as worst-case digit reads rather than array indexing.

    - {!count}, {!is_empty}, {!change_count}, {!has_changes}, {!minimum}, {!maximum},
      {!checkpoint}, {!rollback}, {!current_snapshot}, {!checkpoint_snapshot}, {!is_clean} and
      {!shares_storage_with} are [O(1)]. {!minimum} and {!maximum} are [O(1)] worst case: digit
      reads that invoke no comparator.
    - {!nth} is [O(log N)]: rank select left the array substrate's [O(1)] with it, the regression
      the complexity-parity census ruled acceptable — flagged, not silent.
    - {!find_opt}, {!find_entry}, {!mem}, {!index_of_key}, {!floor}, {!ceiling}, {!lower},
      {!higher} are [O(log N)] comparisons, and {!find_change} is [O(log (k + 1))].
    - {!set} and {!remove} are [O(log N)] comparisons and fresh nodes for the state edit plus
      [O(log (k + 1))] for the change-index edit — the baseline bound the array substrate could
      not deliver, since publishing a successor there copied [Theta (N + k)] words.
    - {!changes} is [Theta (k + 1)] and invokes no comparator and no value-equality callback.
    - {!changes_in_range} is [O(log (k + 1))] comparisons to seek the two boundaries plus
      [Theta (output)] to copy them, and invokes the value-equality relation zero times. The change
      index is itself an ordered map under the same comparator, so this is a boundary seek and a
      bounded walk, never a filter over all [k] records: its cost does not depend on how many
      changes fall outside the range.
    - {!validate} is diagnostic, not part of any operation's cost, and does invoke both policies.

    The mutation surface is deliberately point-only. An eager bulk clear would produce [Theta (N)]
    removal records and could not also keep an ordinary map's [O(1)] clear, so callers start a fresh
    epoch with {!create} when they do not need an enumerable delta and remove entries explicitly
    when they do. {!set_list} is not an exception: it {e is} the left fold of {!set}, a convenience
    that claims no better bound than the sequence of point writes it replaces.

    {1 Divergences from the C# baseline}

    - C# needs a presence-safe [DeltaMapValue<T>] wrapper only because [null] inhabits every
      reference type, so an absent endpoint could not otherwise be told from a present [null].
      OCaml's [option] expresses exactly that distinction, and the nested [option] that arises when
      reading a change endpoint out of a lookup stays unambiguous. This is the already-precedented
      port-wide substitution of [option] for the presence wrapper.
    - A stored change is a three-way variant internally rather than a pair of options, so "absent at
      both endpoints" is unrepresentable: a cancellation removes the record instead of writing an
      empty one. {!change_kind}, {!change_before}, and {!change_after} are therefore total with no
      defensive branch, and the corresponding C# [InvalidOperationException] and Rust [unreachable!]
      have no counterpart here. {!validate} correspondingly has one fewer failure mode than the
      baselines.
    - Absent lookups return [option] rather than raising, and {!validate} returns a [result] rather
      than throwing, per this workspace's error convention.
    - C# caches one reference-shared [Empty] instance for the default comparers. {!empty} allocates
      instead, because a polymorphic OCaml value cannot be a shared static without losing its type,
      and nothing observable depends on it: two empty maps are extensionally equal and
      {!shares_storage_with} is only ever asked about values derived from one another.
    - {!changes} and {!changes_in_range} return lists rather than lazy sequences, so the
      [Theta (k + 1)] and [Theta (output)] terms are paid up front and abandoning an enumeration
      early saves nothing. The Rust port made the same choice for the same reason. Full consumption
      matches the baseline's bound; a partial one does not. *)

type ('key, 'value) t
(** A persistent sorted map carrying a checkpoint and the exact net changes from it. Every operation
    returns a new value and leaves the receiver, and every other retained value, unchanged. *)

type ('key, 'value) change
(** One exact net change between a value's checkpoint and its current state. Absent at both
    endpoints is not a net change and is structurally unrepresentable, so every accessor below is
    total. *)

(** How one recorded change relates its two endpoints. *)
type change_kind =
  | Added  (** Absent at the checkpoint and present in the current state. *)
  | Removed  (** Present at the checkpoint and absent from the current state. *)
  | Updated  (** Present at both endpoints with values the retained relation calls unequal. *)

type statistics = {
  entry_count : int;  (** Number of current entries. *)
  checkpoint_entry_count : int;  (** Number of checkpoint entries. *)
  change_count : int;  (** Number of exact net-changed key classes. *)
  added_count : int;  (** Number of records absent at the checkpoint and present now. *)
  removed_count : int;  (** Number of records present at the checkpoint and absent now. *)
  updated_count : int;  (** Number of records present at both endpoints with unequal values. *)
  is_clean : bool;  (** Whether the current root is storage-identical to the checkpoint root. *)
}
(** Representation counters returned by a successful {!validate}. *)

val create :
  'key Common.Comparator.t -> equal_values:('value -> 'value -> bool) -> ('key, 'value) t
(** [create order ~equal_values] is the empty map whose current state is also its checkpoint,
    retaining [order] for key identity and enumeration and [equal_values] for semantic no-ops and
    change cancellation. Both are retained on the value, so an empty result stays usable. [O(1)]. *)

val empty : unit -> ('key, 'value) t
(** The empty map under the platform's own comparison and structural value equality. [O(1)]. *)

val of_list :
  'key Common.Comparator.t ->
  equal_values:('value -> 'value -> bool) ->
  ('key * 'value) list ->
  ('key, 'value) t
(** [of_list order ~equal_values entries] is a clean map whose current state and checkpoint are both
    [entries], with the last member of a comparator-equivalent class supplying both the retained key
    representative and the value. It records no changes. [O(m log m)] for [m] entries: each entry
    is placed by an [O(log m)] seek and split-and-join write. *)

val comparator : ('key, 'value) t -> 'key Common.Comparator.t
(** The retained policy defining key equivalence and ascending order. [O(1)]. *)

val value_equal : ('key, 'value) t -> 'value -> 'value -> bool
(** [value_equal map left right] applies the retained value relation, the one that decides which
    writes are semantic no-ops and when a recorded change cancels. *)

val count : ('key, 'value) t -> int
(** Number of current entries. [O(1)]. *)

val is_empty : ('key, 'value) t -> bool
(** Whether the current state holds no entries. [O(1)]. *)

val change_count : ('key, 'value) t -> int
(** Number of exact net-changed key classes. [O(1)]. *)

val has_changes : ('key, 'value) t -> bool
(** Whether the current state differs semantically from its checkpoint. [O(1)]. *)

val is_clean : ('key, 'value) t -> bool
(** Whether the value records no change {e and} its current root is the very checkpoint root. A
    representation test, not an equality test: two extensionally identical maps built by different
    routes need not both be clean. [O(1)]. *)

val shares_storage_with : ('key, 'value) t -> ('key, 'value) t -> bool
(** Whether both values are backed by the same three roots, so neither can observe a change made
    through the other. A representation test, not an equality test. [O(1)]. *)

val current_snapshot : ('key, 'value) t -> ('key, 'value) Sorted_map.t
(** The current state as a plain ordered map, sharing storage rather than copying. [O(1)]. *)

val checkpoint_snapshot : ('key, 'value) t -> ('key, 'value) Sorted_map.t
(** The checkpoint state as a plain ordered map, sharing storage rather than copying. [O(1)]. *)

val find_opt : 'key -> ('key, 'value) t -> 'value option
(** The current value stored for the key's class, or [None] when absent. [O(log N)]. *)

val find_entry : 'key -> ('key, 'value) t -> ('key * 'value) option
(** The retained current representative and value for the key's class. The representative is the
    stored key, which may differ from the probe when the comparator calls them equal. [O(log N)]. *)

val mem : 'key -> ('key, 'value) t -> bool
(** Whether a current entry exists for the key's class. [O(log N)]. *)

val index_of_key : 'key -> ('key, 'value) t -> int option
(** The zero-based ascending rank of the key's class in the current state. [O(log N)]. *)

val nth : int -> ('key, 'value) t -> ('key * 'value) option
(** The current entry at the given zero-based rank. [O(log N)]: a comparator-free size-directed
    descent — the census-ruled regression from the array substrate's [O(1)]. *)

val minimum : ('key, 'value) t -> ('key * 'value) option
(** The least current entry by key. [O(1)] worst case: a digit read that invokes no comparator. *)

val maximum : ('key, 'value) t -> ('key * 'value) option
(** The greatest current entry by key. [O(1)] worst case: a digit read that invokes no
    comparator. *)

val floor : 'key -> ('key, 'value) t -> ('key * 'value) option
(** The greatest current entry whose key is not greater than the probe. [O(log N)]. *)

val ceiling : 'key -> ('key, 'value) t -> ('key * 'value) option
(** The least current entry whose key is not less than the probe. [O(log N)]. *)

val lower : 'key -> ('key, 'value) t -> ('key * 'value) option
(** The greatest current entry whose key is strictly less than the probe. [O(log N)]. *)

val higher : 'key -> ('key, 'value) t -> ('key * 'value) option
(** The least current entry whose key is strictly greater than the probe. [O(log N)]. *)

val key_range :
  minimum:'key -> maximum:'key -> ('key, 'value) t -> ('key, 'value) Sorted_map.t
(** The current entries whose keys lie in the inclusive range, as a plain ordered map. An inverted
    range yields an empty map rather than failing, and "inverted" is decided by the retained
    comparator, so under a descending order the low endpoint is the numerically greater key. This is
    a read: the result opens no delta epoch of its own.

    [O(log N)]: two boundary seeks and a structurally shared restriction, matching the managed and
    Rust baselines now that the substrate splits share subtrees instead of copying a window. The
    cost depends neither on the selected width nor on the number of excluded entries. *)

val keys : ('key, 'value) t -> 'key list
(** The current keys in ascending order. [Theta (N)]. *)

val values : ('key, 'value) t -> 'value list
(** The current values in ascending key order. [Theta (N)]. *)

val to_list : ('key, 'value) t -> ('key * 'value) list
(** The current entries in ascending key order. [Theta (N)]. *)

val set : 'key -> 'value -> ('key, 'value) t -> ('key, 'value) t
(** [set key value map] adds or replaces one current entry and coalesces its checkpoint-relative
    change.

    A write whose value the retained relation calls equal to the current value is a semantic no-op
    and returns the receiver itself, sharing every root and recording nothing. Otherwise the first
    effective write to the class captures its [before] and later writes replace only [after], so
    repeated writes coalesce into one record; a write returning the class to its checkpoint state
    removes the record, and if that empties the index the current root snaps back to the checkpoint
    root. A class present in the current state keeps its stored representative, a still-active
    addition episode keeps its first representative, and only a genuinely new class takes [key].
    [O(log N)] plus [O(log (k + 1))] for the change-index edit; see the bounds section. *)

val set_list : ('key * 'value) list -> ('key, 'value) t -> ('key, 'value) t
(** [set_list entries map] applies [entries] in order, exactly as the left fold of {!set} over them:
    every coalescing, cancellation, representative-retention, and checkpoint-snap rule holds
    verbatim, a later comparator-equivalent key wins, and no bound better than the [m] point writes
    it replaces is claimed. An empty list, or one whose every entry is a semantic no-op, returns the
    receiver. *)

val remove : 'key -> ('key, 'value) t -> ('key, 'value) t
(** [remove key map] removes one current entry and coalesces its checkpoint-relative change.

    Removing an absent key is a no-op that returns the receiver itself. Removing a class added since
    the checkpoint cancels its record rather than recording a removal, and if that empties the index
    the current root snaps back to the checkpoint root. [O(log N)] plus [O(log (k + 1))] for the
    change-index edit. *)

val checkpoint : ('key, 'value) t -> ('key, 'value) t
(** Makes the current state the new checkpoint and empties the change index. An already-clean value
    returns itself; otherwise the result shares the receiver's current root as both of its states.
    Invokes no comparator and no value-equality callback. [O(1)]. *)

val rollback : ('key, 'value) t -> ('key, 'value) t
(** Returns to this value's checkpoint and empties the change index. An already-clean value returns
    itself; otherwise the result shares the receiver's checkpoint root as both of its states.
    Invokes no comparator and no value-equality callback. [O(1)]. *)

val change_key : ('key, 'value) change -> 'key
(** The retained key representative of the changed class: the checkpoint representative when the
    class was present at the checkpoint, otherwise the current addition episode's first one. *)

val change_before : ('key, 'value) change -> 'value option
(** The checkpoint endpoint, or [None] when the class was absent at the checkpoint. *)

val change_after : ('key, 'value) change -> 'value option
(** The current endpoint, or [None] when the class is absent from the current state. *)

val change_kind : ('key, 'value) change -> change_kind
(** The classification implied by the two endpoints. Total, because a record absent at both
    endpoints is never constructed. [O(1)]. *)

val find_change : 'key -> ('key, 'value) t -> ('key, 'value) change option
(** The one exact checkpoint-relative net change recorded for the key's class, or [None] when the
    class matches its checkpoint. Searches the change index rather than the state, so
    [O(log (k + 1))]. *)

val changes : ('key, 'value) t -> ('key, 'value) change list
(** The exact net changes, once each, in ascending key order. [Theta (k + 1)], which is
    output-optimal in the number of net-changed classes, and invokes no comparator and no
    value-equality callback. *)

val changes_in_range :
  minimum:'key -> maximum:'key -> ('key, 'value) t -> ('key, 'value) change list
(** The exact net changes whose keys lie in the inclusive range, in the same ascending order
    {!changes} uses. The change index is an ordered map under the retained comparator, so this seeks
    the two boundaries in [O(log (k + 1))] comparisons and copies only the [Theta (output)] records
    between them; it never filters the full change set and invokes the value-equality relation zero
    times. An inverted range yields no changes rather than failing, decided by the retained
    comparator, so a descending order behaves correctly and takes its low endpoint first. *)

val validate : ('key, 'value) t -> (statistics, string) result
(** Audits the whole representation and returns its counters, or the first invariant failure: all
    three roots strictly ascending, no recorded change with equivalent endpoints, every [before]
    agreeing with the checkpoint state and every [after] with the current state, a recorded change
    for every key class on which the two states differ, exactly that many records, and a current
    root physically equal to the checkpoint root whenever no change is recorded. The baselines'
    further "absent at both endpoints" check has no counterpart because that state is
    unrepresentable here.

    Diagnostic, not part of any operation's cost: [Theta ((N + k) log N)], and it does invoke both
    retained policies. *)
