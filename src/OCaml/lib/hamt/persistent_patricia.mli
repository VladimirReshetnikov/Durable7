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
end

module Int32_map : Map with type key = int32
module Int64_map : Map with type key = int64
module Int32_set : Set with type element = int32
module Int64_set : Set with type element = int64
