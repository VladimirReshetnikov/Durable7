(** Persistent key-ordered priority-search queue with a cached priority-then-key winner.

    Rebuilt from the flat sorted-array placeholder onto the winner-cached key-ordered AVL every
    sibling workspace ships: keyed writes and removals are O(log n) path-copied edits instead of the
    array's Theta(n) copies, the minimum-priority entry is an O(1) read of the root's cached winner,
    and delete-min is an O(log n) keyed delete of that winner. Range enumeration by key and priority
    prunes whole subtrees through the winner caches.

    One deliberate regression, per the census ruling precedent for the sorted family: the array
    placeholder answered {!nth} and the rank bounds by direct indexing — O(1) select, O(log n)
    binary-searched bounds — and rank selection is now an O(log n) size-directed descent. No
    persistent structure gives both O(log n) writes and O(1) select; the O(1) select was an artifact
    of the placeholder. {!minimum} deliberately does {e not} regress: it stays O(1). *)

type ('key, 'priority, 'value) entry
type ('key, 'priority, 'value) t
(** A persistent priority search queue: ordered by key, searchable by minimum priority. Every node
    of the key-ordered AVL caches its subtree's minimum-priority entry, so neither question is
    answered by scanning the other order. *)

type statistics = { count : int; estimated_height : int; winner_recomputations : int }
(** Shape measurements returned by a structural audit: the entry count, the AVL's real height, and
    how many node winner caches the lineage has recomputed across its path-copied writes. *)

val empty :
  key_comparator:'key Common.Comparator.t ->
  priority_comparator:'priority Common.Comparator.t ->
  ('key, 'priority, 'value) t
(** The empty queue. *)

val count : ('key, 'priority, 'value) t -> int
(** Number of entries in the queue. O(1): read from the root's cached size. *)

val is_empty : ('key, 'priority, 'value) t -> bool
(** Whether the queue holds no entries. O(1). *)

val entry_key : ('key, 'priority, 'value) entry -> 'key
(** The entry's key. *)

val entry_priority : ('key, 'priority, 'value) entry -> 'priority
(** The entry's priority. *)

val entry_value : ('key, 'priority, 'value) entry -> 'value
(** The entry's value. *)

val find : 'key -> ('key, 'priority, 'value) t -> ('key, 'priority, 'value) entry option
(** The entry matching the probe. Raises when absent. O(log n) key comparisons. *)

val mem : 'key -> ('key, 'priority, 'value) t -> bool
(** Whether the entry is present. O(log n). *)

val nth : int -> ('key, 'priority, 'value) t -> ('key, 'priority, 'value) entry option
(** The entry at the given rank. O(log n), by size-directed descent with no comparator call — the
    census-ruled regression from the array placeholder's O(1) indexing. *)

val lower_bound : 'key -> ('key, 'priority, 'value) t -> int
(** The rank of the first key not less than the probe. O(log n). *)

val upper_bound : 'key -> ('key, 'priority, 'value) t -> int
(** The rank after any key equal to the probe. O(log n). *)

val add :
  'key ->
  'priority ->
  'value ->
  ('key, 'priority, 'value) t ->
  (('key, 'priority, 'value) t, string) result
(** A queue containing the given entry. O(log n): a path-copied insertion that rebuilds the winner
    caches along one root path, replacing the placeholder's Theta(n) array copy. *)

val set :
  'key -> 'priority -> 'value -> ('key, 'priority, 'value) t -> bool * ('key, 'priority, 'value) t
(** A queue with the key bound to the value, adding or replacing as needed. Replacement keeps the
    stored key representative. O(log n). *)

val remove :
  'key ->
  ('key, 'priority, 'value) t ->
  (('key, 'priority, 'value) entry * ('key, 'priority, 'value) t) option
(** A queue without that entry. O(log n). *)

val minimum : ('key, 'priority, 'value) t -> ('key, 'priority, 'value) entry option
(** The smallest element, or the minimum-priority entry where the structure is a queue. O(1): reads
    the root's cached winner, with priority ties broken by key order. *)

val minimum_view :
  ('key, 'priority, 'value) t ->
  (('key, 'priority, 'value) entry * ('key, 'priority, 'value) t) option
(** The minimum-priority entry together with the queue remaining. O(log n): an O(1) winner read and
    a keyed delete of that winner. *)

val entries_by_key : ('key, 'priority, 'value) t -> ('key, 'priority, 'value) entry list
(** The entries bound to the key, in order. Theta(n), with no comparator call. *)

val statistics : ('key, 'priority, 'value) t -> statistics
(** Shape measurements from a structural audit. *)
