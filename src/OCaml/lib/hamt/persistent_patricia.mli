(** Persistent big-endian Patricia maps and sets for fixed-width integer keys. *)

module type Map = sig
  type key
  type 'value t

  val empty : 'value t
  val count : 'value t -> int
  val is_empty : 'value t -> bool
  val mem : key -> 'value t -> bool
  val find_opt : key -> 'value t -> 'value option
  val add : key -> 'value -> 'value t -> 'value t
  val set : key -> 'value -> 'value t -> 'value t
  val remove : key -> 'value t -> 'value t
  val to_list : 'value t -> (key * 'value) list

  module Cursor : sig
    type 'value cursor

    val start : 'value t -> 'value cursor
    val at : int -> 'value t -> 'value cursor option
    val at_end : 'value t -> 'value cursor
    val lower_bound : key -> 'value t -> 'value cursor
    val upper_bound : key -> 'value t -> 'value cursor
    val exact : key -> 'value t -> 'value cursor * bool
    val count : 'value cursor -> int
    val position : 'value cursor -> int
    val is_at_start : 'value cursor -> bool
    val is_at_end : 'value cursor -> bool
    val peek_previous : 'value cursor -> (key * 'value) option
    val peek_next : 'value cursor -> (key * 'value) option
    val move_previous : 'value cursor -> 'value cursor option
    val move_next : 'value cursor -> 'value cursor option
    val seek : int -> 'value cursor -> 'value cursor option
    val insert : key -> 'value -> 'value cursor -> 'value cursor option
    val set : key -> 'value -> 'value cursor -> 'value cursor option
    val set_next_value : 'value -> 'value cursor -> 'value cursor option
    val delete_previous : 'value cursor -> 'value cursor option
    val delete_next : 'value cursor -> 'value cursor option
    val snapshot : 'value cursor -> 'value t
  end
end

module type Set = sig
  type element
  type t

  val empty : t
  val count : t -> int
  val mem : element -> t -> bool
  val add : element -> t -> t
  val remove : element -> t -> t
  val to_list : t -> element list

  module Cursor : sig
    type cursor

    val start : t -> cursor
    val at : int -> t -> cursor option
    val at_end : t -> cursor
    val lower_bound : element -> t -> cursor
    val upper_bound : element -> t -> cursor
    val exact : element -> t -> cursor * bool
    val count : cursor -> int
    val position : cursor -> int
    val is_at_start : cursor -> bool
    val is_at_end : cursor -> bool
    val peek_previous : cursor -> element option
    val peek_next : cursor -> element option
    val move_previous : cursor -> cursor option
    val move_next : cursor -> cursor option
    val seek : int -> cursor -> cursor option
    val add : element -> cursor -> cursor option
    val delete_previous : cursor -> cursor option
    val delete_next : cursor -> cursor option
    val snapshot : cursor -> t
  end
end

module Int32_map : Map with type key = int32
module Int64_map : Map with type key = int64
module Int32_set : Set with type element = int32
module Int64_set : Set with type element = int64
