(** Synchronized live map over immutable HAMT roots.

    Operations are thread-safe but make no lock-free progress claim. *)

type ('key, 'value) t
type ('key, 'value) snapshot

val create :
  ?value_equal:('value -> 'value -> bool) -> 'key Common.Hash_policy.t -> ('key, 'value) t

val count : ('key, 'value) t -> int
val generation : ('key, 'value) t -> int64
val find_opt : 'key -> ('key, 'value) t -> 'value option
val set : 'key -> 'value -> ('key, 'value) t -> unit
val remove : 'key -> ('key, 'value) t -> unit
val snapshot : ('key, 'value) t -> ('key, 'value) snapshot
val snapshot_generation : ('key, 'value) snapshot -> int64
val snapshot_find_opt : 'key -> ('key, 'value) snapshot -> 'value option
val snapshot_to_list : ('key, 'value) snapshot -> ('key * 'value) list
val snapshot_to_persistent : ('key, 'value) snapshot -> ('key, 'value) Persistent_hamt.map
