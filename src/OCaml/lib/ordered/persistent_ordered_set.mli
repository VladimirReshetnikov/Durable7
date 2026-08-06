(** Neutral persistent insertion-ordered set with explicit positional movement.

    Rebuilt from the flat-array placeholder — which answered membership by a Theta(n) linear scan
    and every write by a Theta(n) copy — onto the CHAMP-plus-stamped-sequence composite every
    sibling workspace ships: a CHAMP trie maps each stored representative to a private integer
    stamp, giving expected-O(1) membership, and a stamp-measured sequence holds the insertion
    order, resolving a stamp to its ordinal position in one O(log n) measure-directed descent.
    Point writes and positional movement are O(log n); the set algebra drops from Theta(n·m) to
    hashed-membership passes.

    One deliberate regression, per the census ruling precedent for the sorted family: the array
    placeholder answered {!nth} by direct indexing in O(1), and rank access is now an O(log n)
    size-directed descent. No persistent structure gives both O(log n) positional writes and O(1)
    select; the O(1) select was an artifact of the placeholder. {!first} and {!last} deliberately
    do {e not} regress: they stay O(1) digit reads. *)

type 'element t

val empty : 'element Common.Hash_policy.t -> 'element t
(** The empty collection. *)

val of_list : 'element Common.Hash_policy.t -> 'element list -> 'element t
(** A collection holding a list's elements, built in bulk rather than by repeated insertion:
    expected O(n) — one hashed dedup pass keeping first representatives, then an eager build of
    both indexes. *)

val policy : 'element t -> 'element Common.Hash_policy.t
(** The policy the collection retains. *)

val count : 'element t -> int
(** Number of elements in the collection. O(1). *)

val is_empty : 'element t -> bool
(** Whether the collection holds no elements. O(1). *)

val mem : 'element -> 'element t -> bool
(** Whether the element is present. Expected O(1) through the hashed index, replacing the
    placeholder's Theta(n) scan. *)

val find : 'element -> 'element t -> 'element option
(** The element matching the probe. Raises when absent. Expected O(1); the stored representative
    is returned. *)

val nth : int -> 'element t -> 'element option
(** The element at the given rank. O(log n) — the census-ruled regression from the array
    placeholder's O(1) indexing. *)

val index_of : 'element -> 'element t -> int option
(** The rank of the given element. Expected O(1) to find its stamp, then one O(log n)
    measure-directed descent to its position — never a scan of either index. *)

val first : 'element t -> 'element option
(** The first element. O(1) worst case: a digit read. *)

val last : 'element t -> 'element option
(** The last element. O(1) worst case. *)

val add : 'element -> 'element t -> bool * 'element t
(** A collection containing the given element. O(log n) (expected O(1) on the hashed index plus an
    O(1)-amortized order push); a duplicate returns the receiver with the first representative
    kept. *)

val add_first : 'element -> 'element t -> bool * 'element t
(** A collection with the element placed first in insertion order. O(log n). *)

val insert : int -> 'element -> 'element t -> (bool * 'element t, string) result
(** A collection containing the given element at the given position. O(log n): a fresh stamp is
    labeled between the position's neighbours; a duplicate leaves the collection unchanged. When
    the neighbouring stamps have no gap left, the whole collection relabels deterministically in
    O(n). *)

val move_to : int -> 'element -> 'element t -> ('element t, string) result
(** A collection with the element moved to the given position in the insertion order. O(log n),
    keeping the stored representative; moving an absent element returns the collection
    unchanged. *)

val move_to_first : 'element -> 'element t -> 'element t
(** A collection with the element moved to the front of the insertion order. O(log n). *)

val move_to_last : 'element -> 'element t -> 'element t
(** A collection with the element moved to the end of the insertion order. O(log n). *)

val remove : 'element -> 'element t -> bool * 'element t
(** A collection without that element. O(log n), replacing the placeholder's Theta(n) copy. *)

val remove_at : int -> 'element t -> ('element * 'element t, string) result
(** A collection without the element at the position. O(log n). *)

val range : start:int -> count:int -> 'element t -> ('element t, string) result
(** The elements in the given range. O(log n) structural splits of the order sequence plus
    expected O(min(count, n - count)) maintenance of the hashed index. *)

val take : int -> 'element t -> ('element t, string) result
(** The first n elements. *)

val drop : int -> 'element t -> ('element t, string) result
(** The elements after the first n. *)

val reverse : 'element t -> 'element t
(** The collection in the opposite order. Theta(n): a deterministic relabeling rebuild. *)

val sort : 'element Common.Comparator.t -> 'element t -> 'element t
(** The elements in ascending order. Stable, so equal elements keep their insertion order.
    O(n log n) comparisons plus a Theta(n) relabeling rebuild. *)

val union : 'element t -> 'element list -> 'element t
(** The elements of both collections, with this collection's representative kept for every class
    present in both and argument-only elements appended in argument order. Expected O(n + m) for
    the membership passes plus O(m log(n + m)) appends, replacing the placeholder's
    Theta(n·m). *)

val intersect : 'element t -> 'element list -> 'element t
(** The elements present in both collections, in this collection's order. Expected O(n + m). *)

val difference : 'element t -> 'element list -> 'element t
(** This collection's elements that are absent from the other. Expected O(n + m). *)

val symmetric_difference : 'element t -> 'element list -> 'element t
(** The elements present in exactly one of the two collections. Expected O(n + m). *)

val to_list : 'element t -> 'element list
(** The elements, in the collection's own order. Theta(n). *)
