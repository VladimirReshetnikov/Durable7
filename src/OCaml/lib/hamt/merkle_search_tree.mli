(** Canonical immutable B=16 Merkle search tree with exact MST2 blocks. *)

type ('key, 'value) entry = { key : 'key; value : 'value }
type block = { digest : Merkle_encoding.digest; content : bytes }
(** One encoded block, paired with the digest it is stored and fetched under. *)

type ('key, 'value) t
(** A Merkle search tree: a persistent ordered map whose structure is fixed by its contents. Each
    block's digest covers its entries and its children's digests, so equal digests mean equal
    contents and synchronizing two trees costs only what actually differs. *)

val make_block : Merkle_encoding.digest -> bytes -> block
(** A block from its digest and its encoded bytes. *)

val block_digest : block -> Merkle_encoding.digest
(** The block's digest. *)

val block_content : block -> bytes
(** The block's encoded bytes. *)

val make_entry : 'key -> 'value -> ('key, 'value) entry
(** An entry from its key and value. *)

val entry_key : ('key, 'value) entry -> 'key
(** The entry's key. *)

val entry_value : ('key, 'value) entry -> 'value
(** The entry's value. *)

val empty : ('key, 'value) Merkle_encoding.policy -> ('key, 'value) t
(** The empty tree. *)

val of_list :
  ('key, 'value) Merkle_encoding.policy -> ('key * 'value) list -> (('key, 'value) t, string) result
(** A tree holding a list's entries, built in bulk rather than by repeated insertion. *)

val policy : ('key, 'value) t -> ('key, 'value) Merkle_encoding.policy
(** The policy the tree retains. *)

val count : ('key, 'value) t -> int
(** Number of entries in the tree. *)

val height : ('key, 'value) t -> int
(** The structure's height. *)

val block_count : ('key, 'value) t -> int
(** Number of entries in the tree. *)

val root_digest : ('key, 'value) t -> Merkle_encoding.digest
(** The tree's root digest. Equal digests mean equal contents. *)

val find_opt : 'key -> ('key, 'value) t -> 'value option
(** The value stored for the key, or [None] when absent. *)

val mem : 'key -> ('key, 'value) t -> bool
(** Whether the entry is present. *)

val set : 'key -> 'value -> ('key, 'value) t -> (('key, 'value) t, string) result
(** A tree with the key bound to the value, adding or replacing as needed. *)

val add : 'key -> 'value -> ('key, 'value) t -> (('key, 'value) t, string) result
(** A tree containing the given entry. *)

val remove : 'key -> ('key, 'value) t -> (('key, 'value) t, string) result
(** A tree without that entry. *)

val to_list : ('key, 'value) t -> ('key, 'value) entry list
(** The entries, in the tree's own order. *)

val range : minimum:'key -> maximum:'key -> ('key, 'value) t -> ('key, 'value) entry list
(** The entries in the given range. *)

val blocks_preorder : ('key, 'value) t -> block list
(** The tree's blocks in preorder. *)

val validate : ('key, 'value) t -> (unit, string) result

(** Immutable ordered gap cursors over canonical tree snapshots. *)
module Cursor : sig
  type ('key, 'value) cursor
(** An in-order gap cursor over one tree version's entries. *)

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
