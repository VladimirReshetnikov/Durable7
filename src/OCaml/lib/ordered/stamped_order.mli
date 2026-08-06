(** Stamp-measured insertion-order sequence shared by the ordered collections.

    The ordered set, map, and multimap keep their entries twice: a CHAMP index from key to a
    private integer {e stamp}, and this sequence of stamped entries in insertion order, where
    stamps rise strictly from front to back. Turning a stamp back into an ordinal position is
    therefore a search over a sorted-by-stamp sequence, and how that search is performed decides
    the family's keyed positional bounds. This sequence caches the maximum stamp of every subtree
    as its measure — the representation the reference workspaces share — so {!index_of_stamp} is
    one measure-directed descent: O(log n) worst case, never a binary search over indexing, whose
    every probe would itself be an O(log n) descent for an O(log{^ 2} n) total.

    The measure policy is injectable so a test can observe descents through a counting policy;
    production callers take {!make_policy}. *)

type 'element entry = { stamp : int; item : 'element }
(** An order entry carrying the strictly increasing stamp that positions it. *)

type 'element t
(** An insertion-order sequence of stamped entries over the lazy measured finger tree. *)

val stride : int
(** The sparse gap left between adjacent stamps so later insertions can label between them without
    relabeling. *)

val make_policy : unit -> ('element entry, int) Finger_tree.Measures.policy
(** The production maximum-stamp measure policy: stamps rise strictly left to right, so a subtree's
    cached maximum is its last stamp. *)

val empty : ('element entry, int) Finger_tree.Measures.policy -> 'element t
(** The empty sequence under the given measure policy. *)

val of_entries : ('element entry, int) Finger_tree.Measures.policy -> 'element entry list -> 'element t
(** A sequence holding the given stamped entries, built in one eager bulk pass. The caller is
    responsible for the stamps rising strictly. *)

val length : 'element t -> int
(** Number of entries in the sequence. O(1). *)

val is_empty : 'element t -> bool
(** Whether the sequence holds no entries. O(1). *)

val first : 'element t -> 'element entry option
(** The first entry. O(1) worst case: a digit read. *)

val last : 'element t -> 'element entry option
(** The last entry. O(1) worst case. *)

val nth : int -> 'element t -> 'element entry option
(** The entry at the given rank. O(log n), by size-directed descent. *)

val index_of_stamp : int -> 'element t -> int option
(** The ordinal position of the entry carrying the stamp, or [None] when absent. One
    measure-directed descent — O(log n) worst case. Because stamps rise strictly, "first prefix
    whose maximum stamp reaches the probe" is monotone, and the located entry either carries
    exactly the probe stamp or the stamp is absent altogether. *)

val pick_stamp : int -> 'element t -> int option
(** A fresh stamp for an insertion at the given gap: the midpoint of the neighbouring stamps, or a
    stride step beyond the end entries. [None] when the gap's stamps are exhausted, in which case
    the caller performs its deterministic full relabel. *)

val insert_at : int -> 'element entry -> 'element t -> ('element t, string) result
(** A sequence with the entry inserted at the position. O(log n); the endpoints are O(1) amortized
    pushes. *)

val remove_at : int -> 'element t -> ('element entry * 'element t, string) result
(** A sequence without the entry at the position, returned alongside it. O(log n). *)

val update_at : int -> 'element entry -> 'element t -> ('element t, string) result
(** A sequence with the entry at the position replaced. O(log n). *)

val sub : start:int -> count:int -> 'element t -> 'element t
(** The entries in the given range, sharing structure with the receiver. O(log n). The caller is
    responsible for the range being valid. *)

val to_list : 'element t -> 'element entry list
(** The entries, in insertion order. Theta(n). *)

val fold_left : ('state -> 'element entry -> 'state) -> 'state -> 'element t -> 'state
(** Folds over the entries from the front. Theta(n). *)
