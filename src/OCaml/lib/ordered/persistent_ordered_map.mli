(** Neutral persistent insertion-ordered map with retained key representatives.

    Rebuilt from the flat-array placeholder — Theta(n) membership scans and Theta(n)-copy writes —
    onto the CHAMP-plus-stamped-sequence composite every sibling workspace ships: a CHAMP trie maps
    each stored key representative to a private integer stamp, giving expected-O(1) membership, and
    a stamp-measured sequence holds the entries in insertion order, resolving a stamp to its
    ordinal position in one O(log n) measure-directed descent. Keyed writes, removals, and
    positional movement are O(log n).

    One deliberate regression, per the census ruling precedent for the sorted family: the array
    placeholder answered {!nth} by direct indexing in O(1), and rank access is now an O(log n)
    size-directed descent — the price of O(log n) positional writes. {!first} and {!last} stay
    O(1). *)

type ('key, 'value) entry
type ('key, 'value) t
(** A persistent map that remembers insertion order. Replacing an entry's value leaves its position
    alone. *)

val empty : ?value_equal:('value -> 'value -> bool) -> 'key Common.Hash_policy.t -> ('key, 'value) t
(** The empty collection. *)

val of_list :
  ?value_equal:('value -> 'value -> bool) ->
  'key Common.Hash_policy.t ->
  ('key * 'value) list ->
  ('key, 'value) t
(** A collection holding a list's elements, built in bulk rather than by repeated insertion. A
    repeated key replaces the value in place, keeping the first occurrence's position. *)

val key_policy : ('key, 'value) t -> 'key Common.Hash_policy.t
(** The retained key policy. *)

val count : ('key, 'value) t -> int
(** Number of elements in the collection. O(1). *)

val is_empty : ('key, 'value) t -> bool
(** Whether the collection holds no elements. O(1). *)

val entry_key : ('key, 'value) entry -> 'key
(** The entry's key. *)

val entry_value : ('key, 'value) entry -> 'value
(** The entry's value. *)

val find_entry : 'key -> ('key, 'value) t -> ('key, 'value) entry option
(** The entry stored for the key, with its key representative retained. Expected O(1) to find its
    stamp, then one O(log n) descent to the stored entry. *)

val find_opt : 'key -> ('key, 'value) t -> 'value option
(** The value stored for the key, or [None] when absent. O(log n). *)

val mem : 'key -> ('key, 'value) t -> bool
(** Whether the element is present. Expected O(1) through the hashed index, replacing the
    placeholder's Theta(n) scan. *)

val nth : int -> ('key, 'value) t -> ('key, 'value) entry option
(** The element at the given rank. O(log n) — the census-ruled regression from the array
    placeholder's O(1) indexing. *)

val index_of : 'key -> ('key, 'value) t -> int option
(** The rank of the given element. Expected O(1) to find its stamp, then one O(log n)
    measure-directed descent to its position — never a scan of either index. *)

val first : ('key, 'value) t -> ('key, 'value) entry option
(** The first element. O(1) worst case: a digit read. *)

val last : ('key, 'value) t -> ('key, 'value) entry option
(** The last element. O(1) worst case. *)

val add : 'key -> 'value -> ('key, 'value) t -> (('key, 'value) t, string) result
(** A collection containing the given element, appended in insertion order. O(log n); an existing
    equivalent key is an error. *)

val insert : int -> 'key -> 'value -> ('key, 'value) t -> (('key, 'value) t, string) result
(** A collection containing the given element at the given position. O(log n): a fresh stamp is
    labeled between the position's neighbours. When the neighbouring stamps have no gap left, the
    whole collection relabels deterministically in O(n). *)

val set : 'key -> 'value -> ('key, 'value) t -> bool * ('key, 'value) t
(** A collection with the key bound to the value, adding or replacing as needed. O(log n).
    Replacement rewrites the order slot in place, keeping both the position and the stored key
    representative; a value the retained equality already accepts returns the receiver. *)

val remove : 'key -> ('key, 'value) t -> (('key, 'value) entry * ('key, 'value) t) option
(** A collection without that element. O(log n), replacing the placeholder's Theta(n) copy. *)

val remove_at : int -> ('key, 'value) t -> (('key, 'value) entry * ('key, 'value) t, string) result
(** A collection without the element at the position. O(log n). *)

val move_to : int -> 'key -> ('key, 'value) t -> (('key, 'value) t, string) result
(** A collection with the element moved to the given position in the insertion order. O(log n);
    moving an absent key returns the collection unchanged. *)

val move_to_first : 'key -> ('key, 'value) t -> ('key, 'value) t
(** A collection with the element moved to the front of the insertion order. O(log n). *)

val move_to_last : 'key -> ('key, 'value) t -> ('key, 'value) t
(** A collection with the element moved to the end of the insertion order. O(log n). *)

val range : start:int -> count:int -> ('key, 'value) t -> (('key, 'value) t, string) result
(** The elements in the given range. O(log n) structural splits of the order sequence plus
    expected O(min(count, n - count)) maintenance of the hashed index. *)

val reverse : ('key, 'value) t -> ('key, 'value) t
(** The collection in the opposite order. Theta(n): a deterministic relabeling rebuild. *)

val sort_by_key : 'key Common.Comparator.t -> ('key, 'value) t -> ('key, 'value) t
(** The entries ordered by key. Stable, so equal keys keep their insertion order. O(n log n)
    comparisons plus a Theta(n) relabeling rebuild. *)

val to_list : ('key, 'value) t -> ('key * 'value) list
(** The elements, in the collection's own order. Theta(n). *)
