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
(** Walk the whole tree and check its invariants, reporting the first violation. A defensive check
    for tests and for data arriving from outside the process, not a routine operation. *)

(** Immutable ordered gap cursors over canonical tree snapshots.

    A cursor names a gap {e between} entries, not an entry, so insertion and deletion have an
    unambiguous target and an empty tree still has one valid position. Cursors are values: moving or
    editing returns a new cursor and leaves the receiver, and the version it holds, untouched.

    Two failure channels are deliberately kept apart. A boundary - no previous entry, no next entry
    - is [Ok None] or [None], because there was nothing to do and nothing went wrong. A rejected
    edit is [Error message], because the tree refused it. Reading [Ok None] as success and
    [Error _] as failure is therefore correct; collapsing the two loses the distinction. *)
module Cursor : sig
  type ('key, 'value) cursor
  (** An in-order gap cursor over one tree version's entries. *)

  val start : ('key, 'value) t -> ('key, 'value) cursor
  (** A cursor at the gap before the first entry. *)

  val at : int -> ('key, 'value) t -> ('key, 'value) cursor option
  (** A cursor at the given gap, or [None] when the position lies outside [0..count]. *)

  val at_end : ('key, 'value) t -> ('key, 'value) cursor
  (** A cursor at the gap after the final entry. *)

  val lower_bound : 'key -> ('key, 'value) t -> ('key, 'value) cursor
  (** A cursor at the first gap at or after the key: before the key's entry when it is present, and
      where it would be inserted when it is not. *)

  val upper_bound : 'key -> ('key, 'value) t -> ('key, 'value) cursor
  (** A cursor at the first gap strictly after the key: after the key's entry when it is present,
      and identically to [lower_bound] when it is not. *)

  val exact : 'key -> ('key, 'value) t -> ('key, 'value) cursor * bool
  (** [lower_bound] together with whether the key was actually found, so a hit at a gap is
      distinguishable from a miss that landed on the same gap. *)

  val count : ('key, 'value) cursor -> int
  (** Number of entries in the version this cursor holds. *)

  val position : ('key, 'value) cursor -> int
  (** The gap index, in [0..count]. *)

  val is_at_start : ('key, 'value) cursor -> bool
  (** Whether the gap precedes the first entry. *)

  val is_at_end : ('key, 'value) cursor -> bool
  (** Whether the gap follows the final entry. *)

  val peek_previous : ('key, 'value) cursor -> ('key, 'value) entry option
  (** The entry before the gap, or [None] at the start. Does not move the cursor. *)

  val peek_next : ('key, 'value) cursor -> ('key, 'value) entry option
  (** The entry after the gap, or [None] at the end. Does not move the cursor. *)

  val move_previous : ('key, 'value) cursor -> ('key, 'value) cursor option
  (** A cursor one gap earlier, or [None] at the start. *)

  val move_next : ('key, 'value) cursor -> ('key, 'value) cursor option
  (** A cursor one gap later, or [None] at the end. *)

  val seek : int -> ('key, 'value) cursor -> ('key, 'value) cursor option
  (** A cursor at the given gap of the same version, or [None] outside [0..count]. *)

  val insert : 'key -> 'value -> ('key, 'value) cursor -> (('key, 'value) cursor, string) result
  (** Insert a new entry at the gap and return a cursor after it. Rejected when the key is already
      present, and when the key does not sort to this gap - order is decided by the key, so placing
      it elsewhere would silently contradict the caller. The error names both the expected gap and
      the current one. *)

  val set : 'key -> 'value -> ('key, 'value) cursor -> (('key, 'value) cursor, string) result
  (** Insert or replace an entry that sorts to this gap and return a cursor after it. Rejected only
      when the key belongs at a different gap; unlike [insert], an existing key is replaced. *)

  val set_next_value :
    'value -> ('key, 'value) cursor -> (('key, 'value) cursor option, string) result
  (** Replace the value of the entry after the gap, keeping its key and the gap position. [Ok None]
      at the end, where there is no entry to replace. *)

  val delete_previous : ('key, 'value) cursor -> (('key, 'value) cursor option, string) result
  (** Remove the entry before the gap and return a cursor in its place. [Ok None] at the start. *)

  val delete_next : ('key, 'value) cursor -> (('key, 'value) cursor option, string) result
  (** Remove the entry after the gap and return a cursor in its place. [Ok None] at the end. *)

  val snapshot : ('key, 'value) cursor -> ('key, 'value) t
  (** The tree version this cursor holds, with its canonical blocks and digests intact. *)
end
