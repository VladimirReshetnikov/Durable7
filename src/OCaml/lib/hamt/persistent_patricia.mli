(** Persistent big-endian Patricia maps and sets for fixed-width integer keys.

    A Patricia trie branches only at the bit positions where stored keys actually differ, so every
    operation is bounded by the key width - 32 or 64 bits - rather than by the number of entries,
    and needs no hashing, no rebalancing, and no comparison policy. Branching on the highest
    differing bit first makes the in-order traversal ascending and lets two tries share whole
    untouched subtrees.

    Keys are stored under a sign-flipped encoding, so ordering, [to_list], and every cursor rank are
    ascending by {e signed} key value, with negative keys before non-negative ones.

    Every value is immutable. An update copies the path from the root to the affected branch and
    shares everything else, so the receiver stays valid and retaining many versions is cheap. *)

(** The interface the Patricia maps share, since they differ only in key width. *)
module type Map = sig
  type key
  (** The integer key type: [int32] or [int64]. *)

  type 'value t
  (** A persistent map from [key] to ['value]. *)

  val empty : 'value t
  (** The empty map. *)

  val count : 'value t -> int
  (** Number of entries. O(1). *)

  val is_empty : 'value t -> bool
  (** Whether the map holds no entries. *)

  val mem : key -> 'value t -> bool
  (** Whether the key is present. O(key width). *)

  val find_opt : key -> 'value t -> 'value option
  (** The value stored for the key, or [None] when absent. O(key width). *)

  val add : key -> 'value -> 'value t -> 'value t
  (** A map with the entry added, raising [Persistent_hamt.Duplicate_key] when the key is already
      present. Use [set] to replace instead. *)

  val set : key -> 'value -> 'value t -> 'value t
  (** A map with the key bound to the value, replacing any existing binding. Returns the receiver
      itself when the binding is already present and unchanged. *)

  val remove : key -> 'value t -> 'value t
  (** A map without that key. Returns the receiver unchanged when the key is absent; removing an
      absent key is not an error. *)

  val to_list : 'value t -> (key * 'value) list
  (** The entries in ascending signed key order. *)

  (** Immutable cursors: a retained map version plus a gap position in [0..count].

      A cursor names a gap {e between} entries, not an entry, so insertion and deletion have an
      unambiguous target and an empty map still has one valid position. Cursors are values: moving
      or editing returns a new cursor and leaves the receiver, and the version it holds, untouched.
      Every operation that can fail at a boundary or on a misplaced key returns [None] rather than
      raising. *)
  module Cursor : sig
    type 'value cursor
    (** A map version together with a gap position in it. *)

    val start : 'value t -> 'value cursor
    (** A cursor at the gap before the first entry. O(1). *)

    val at : int -> 'value t -> 'value cursor option
    (** A cursor at the given gap, or [None] when the position lies outside [0..count]. O(1). *)

    val at_end : 'value t -> 'value cursor
    (** A cursor at the gap after the final entry. O(1). *)

    val lower_bound : key -> 'value t -> 'value cursor
    (** A cursor at the first gap at or after the key: positioned {e before} the key's entry when it
        is present, and where it would be inserted when it is not. O(key width). *)

    val upper_bound : key -> 'value t -> 'value cursor
    (** A cursor at the first gap strictly after the key: positioned {e after} the key's entry when
        it is present, and identically to [lower_bound] when it is not. O(key width). *)

    val exact : key -> 'value t -> 'value cursor * bool
    (** [lower_bound] together with whether the key was actually found, so a hit at a gap is
        distinguishable from a miss that landed on the same gap. O(key width). *)

    val count : 'value cursor -> int
    (** Number of entries in the version this cursor holds. O(1). *)

    val position : 'value cursor -> int
    (** The gap index, in [0..count]. *)

    val is_at_start : 'value cursor -> bool
    (** Whether the gap precedes the first entry. *)

    val is_at_end : 'value cursor -> bool
    (** Whether the gap follows the final entry. *)

    val peek_previous : 'value cursor -> (key * 'value) option
    (** The entry before the gap, or [None] at the start. Does not move the cursor. *)

    val peek_next : 'value cursor -> (key * 'value) option
    (** The entry after the gap, or [None] at the end. Does not move the cursor. *)

    val move_previous : 'value cursor -> 'value cursor option
    (** A cursor one gap earlier, or [None] at the start. O(1). *)

    val move_next : 'value cursor -> 'value cursor option
    (** A cursor one gap later, or [None] at the end. O(1). *)

    val seek : int -> 'value cursor -> 'value cursor option
    (** A cursor at the given gap of the same version, or [None] outside [0..count]. O(1). *)

    val insert : key -> 'value -> 'value cursor -> 'value cursor option
    (** Insert a new entry at the gap and return a cursor after it, or [None] when the key is
        already present or does not sort to this gap. Because order is decided by the key rather
        than by the cursor, this deliberately refuses rather than silently placing the entry
        elsewhere. *)

    val set : key -> 'value -> 'value cursor -> 'value cursor option
    (** Insert or replace an entry that sorts to this gap and return a cursor after it, or [None]
        when the key belongs at a different gap. Unlike [insert], an existing key is replaced
        rather than refused. *)

    val set_next_value : 'value -> 'value cursor -> 'value cursor option
    (** Replace the value of the entry after the gap, keeping its key and the gap position, or
        [None] at the end. *)

    val delete_previous : 'value cursor -> 'value cursor option
    (** Remove the entry before the gap and return a cursor in its place, or [None] at the start. *)

    val delete_next : 'value cursor -> 'value cursor option
    (** Remove the entry after the gap and return a cursor in its place, or [None] at the end. *)

    val snapshot : 'value cursor -> 'value t
    (** The map version this cursor holds. Edits made through other cursors are not visible here. *)
  end
end

(** The interface the Patricia sets share, since they differ only in key width. *)
module type Set = sig
  type element
  (** The integer element type: [int32] or [int64]. *)

  type t
  (** A persistent set of [element]. *)

  val empty : t
  (** The empty set. *)

  val count : t -> int
  (** Number of elements. O(1). *)

  val mem : element -> t -> bool
  (** Whether the element is present. O(key width). *)

  val add : element -> t -> t
  (** A set containing the element. Idempotent: adding a present element returns the receiver rather
      than raising, unlike the map's [add]. *)

  val remove : element -> t -> t
  (** A set without that element. Returns the receiver unchanged when it is absent. *)

  val to_list : t -> element list
  (** The elements in ascending signed order. *)

  (** Immutable cursors over a set version. The contract matches the map's cursors exactly; see
      the map's [Cursor]. *)
  module Cursor : sig
    type cursor
    (** A set version together with a gap position in it. *)

    val start : t -> cursor
    (** A cursor at the gap before the first element. O(1). *)

    val at : int -> t -> cursor option
    (** A cursor at the given gap, or [None] when the position lies outside [0..count]. O(1). *)

    val at_end : t -> cursor
    (** A cursor at the gap after the final element. O(1). *)

    val lower_bound : element -> t -> cursor
    (** A cursor at the first gap at or after the element. O(key width). *)

    val upper_bound : element -> t -> cursor
    (** A cursor at the first gap strictly after the element. O(key width). *)

    val exact : element -> t -> cursor * bool
    (** [lower_bound] together with whether the element was actually found. O(key width). *)

    val count : cursor -> int
    (** Number of elements in the version this cursor holds. O(1). *)

    val position : cursor -> int
    (** The gap index, in [0..count]. *)

    val is_at_start : cursor -> bool
    (** Whether the gap precedes the first element. *)

    val is_at_end : cursor -> bool
    (** Whether the gap follows the final element. *)

    val peek_previous : cursor -> element option
    (** The element before the gap, or [None] at the start. Does not move the cursor. *)

    val peek_next : cursor -> element option
    (** The element after the gap, or [None] at the end. Does not move the cursor. *)

    val move_previous : cursor -> cursor option
    (** A cursor one gap earlier, or [None] at the start. O(1). *)

    val move_next : cursor -> cursor option
    (** A cursor one gap later, or [None] at the end. O(1). *)

    val seek : int -> cursor -> cursor option
    (** A cursor at the given gap of the same version, or [None] outside [0..count]. O(1). *)

    val add : element -> cursor -> cursor option
    (** Add an element that sorts to this gap and return a cursor after it, or [None] when it
        belongs at a different gap. Adding an element already present at this gap succeeds and
        leaves the set unchanged. *)

    val delete_previous : cursor -> cursor option
    (** Remove the element before the gap and return a cursor in its place, or [None] at the
        start. *)

    val delete_next : cursor -> cursor option
    (** Remove the element after the gap and return a cursor in its place, or [None] at the end. *)

    val snapshot : cursor -> t
    (** The set version this cursor holds. *)
  end
end

module Int32_map : Map with type key = int32
(** A persistent Patricia map over 32-bit keys. Branch nodes store a common prefix and a
    discriminating bit, so a lookup is bounded by the key width rather than by the entry count. *)

module Int64_map : Map with type key = int64
(** A persistent Patricia map over 64-bit keys. *)

module Int32_set : Set with type element = int32
(** A persistent Patricia set over 32-bit keys. *)

module Int64_set : Set with type element = int64
(** A persistent Patricia set over 64-bit keys. *)
