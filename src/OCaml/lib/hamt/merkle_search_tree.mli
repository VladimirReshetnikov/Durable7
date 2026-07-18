(** Canonical immutable B=16 Merkle search tree with exact MST2 blocks. *)

type ('key, 'value) entry = { key : 'key; value : 'value }
type block = { digest : Merkle_encoding.digest; content : bytes }
type ('key, 'value) t

val make_block : Merkle_encoding.digest -> bytes -> block
val block_digest : block -> Merkle_encoding.digest
val block_content : block -> bytes
val make_entry : 'key -> 'value -> ('key, 'value) entry
val entry_key : ('key, 'value) entry -> 'key
val entry_value : ('key, 'value) entry -> 'value
val empty : ('key, 'value) Merkle_encoding.policy -> ('key, 'value) t

val of_list :
  ('key, 'value) Merkle_encoding.policy -> ('key * 'value) list -> (('key, 'value) t, string) result

val policy : ('key, 'value) t -> ('key, 'value) Merkle_encoding.policy
val count : ('key, 'value) t -> int
val height : ('key, 'value) t -> int
val block_count : ('key, 'value) t -> int
val root_digest : ('key, 'value) t -> Merkle_encoding.digest
val find_opt : 'key -> ('key, 'value) t -> 'value option
val mem : 'key -> ('key, 'value) t -> bool
val set : 'key -> 'value -> ('key, 'value) t -> (('key, 'value) t, string) result
val add : 'key -> 'value -> ('key, 'value) t -> (('key, 'value) t, string) result
val remove : 'key -> ('key, 'value) t -> (('key, 'value) t, string) result
val to_list : ('key, 'value) t -> ('key, 'value) entry list
val range : minimum:'key -> maximum:'key -> ('key, 'value) t -> ('key, 'value) entry list
val blocks_preorder : ('key, 'value) t -> block list
val validate : ('key, 'value) t -> (unit, string) result

(** Immutable ordered gap cursors over canonical tree snapshots. *)
module Cursor : sig
  type ('key, 'value) cursor

  val start : ('key, 'value) t -> ('key, 'value) cursor
  val at : int -> ('key, 'value) t -> ('key, 'value) cursor option
  val at_end : ('key, 'value) t -> ('key, 'value) cursor
  val lower_bound : 'key -> ('key, 'value) t -> ('key, 'value) cursor
  val upper_bound : 'key -> ('key, 'value) t -> ('key, 'value) cursor
  val exact : 'key -> ('key, 'value) t -> ('key, 'value) cursor * bool
  val count : ('key, 'value) cursor -> int
  val position : ('key, 'value) cursor -> int
  val is_at_start : ('key, 'value) cursor -> bool
  val is_at_end : ('key, 'value) cursor -> bool
  val peek_previous : ('key, 'value) cursor -> ('key, 'value) entry option
  val peek_next : ('key, 'value) cursor -> ('key, 'value) entry option
  val move_previous : ('key, 'value) cursor -> ('key, 'value) cursor option
  val move_next : ('key, 'value) cursor -> ('key, 'value) cursor option
  val seek : int -> ('key, 'value) cursor -> ('key, 'value) cursor option
  val insert : 'key -> 'value -> ('key, 'value) cursor -> (('key, 'value) cursor, string) result
  val set : 'key -> 'value -> ('key, 'value) cursor -> (('key, 'value) cursor, string) result

  val set_next_value :
    'value -> ('key, 'value) cursor -> (('key, 'value) cursor option, string) result

  val delete_previous : ('key, 'value) cursor -> (('key, 'value) cursor option, string) result
  val delete_next : ('key, 'value) cursor -> (('key, 'value) cursor option, string) result
  val snapshot : ('key, 'value) cursor -> ('key, 'value) t
end
