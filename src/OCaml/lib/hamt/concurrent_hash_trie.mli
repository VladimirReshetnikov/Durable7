(** Synchronized live map over immutable HAMT roots.

    Operations are thread-safe but make no lock-free progress claim. *)

type ('key, 'value) t
type ('key, 'value) snapshot
(** An immutable view of the trie taken at one moment, readable while the trie continues to
    change. *)

val create :
  ?value_equal:('value -> 'value -> bool) -> 'key Common.Hash_policy.t -> ('key, 'value) t
(** An empty collection using the supplied policies, which it retains. *)

val count : ('key, 'value) t -> int
(** Number of elements in the collection. *)

val generation : ('key, 'value) t -> int64
(** The version's generation counter. *)

val find_opt : 'key -> ('key, 'value) t -> 'value option
(** The value stored for the key, or [None] when absent. *)

val set : 'key -> 'value -> ('key, 'value) t -> unit
(** A collection with the key bound to the value, adding or replacing as needed. *)

val remove : 'key -> ('key, 'value) t -> unit
(** A collection without that element. *)

val snapshot : ('key, 'value) t -> ('key, 'value) snapshot
(** The collection version this cursor is positioned in. *)

val snapshot_generation : ('key, 'value) snapshot -> int64
(** The version's generation counter. *)

val snapshot_find_opt : 'key -> ('key, 'value) snapshot -> 'value option
(** The value stored for the key, or [None] when absent. *)

val snapshot_to_list : ('key, 'value) snapshot -> ('key * 'value) list
(** The elements, in the collection's own order. *)

val snapshot_to_persistent : ('key, 'value) snapshot -> ('key, 'value) Persistent_hamt.map
(** The persistent map holding the snapshot's contents. *)
