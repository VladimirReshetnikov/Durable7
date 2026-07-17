(** Fixed-width two's-complement integers backed by Zarith. *)

type byte_order = Little_endian | Big_endian

module type S = sig
  type t

  val width : int
  val signed : bool
  val zero : t
  val one : t
  val min_value : t
  val max_value : t
  val of_int : int -> t
  val of_z : Z.t -> t
  val of_z_checked : Z.t -> t
  val to_z : t -> Z.t
  val to_unsigned_z : t -> Z.t
  val parse : ?radix:int -> string -> t
  val to_string : ?radix:int -> t -> string
  val equal : t -> t -> bool
  val compare : t -> t -> int
  val add : t -> t -> t
  val sub : t -> t -> t
  val mul : t -> t -> t
  val div : t -> t -> t
  val rem : t -> t -> t
  val div_rem : t -> t -> t * t
  val negate : t -> t
  val abs : t -> t
  val succ : t -> t
  val pred : t -> t
  val checked_add : t -> t -> t
  val checked_sub : t -> t -> t
  val checked_mul : t -> t -> t
  val checked_negate : t -> t
  val logand : t -> t -> t
  val logor : t -> t -> t
  val logxor : t -> t -> t
  val lognot : t -> t
  val shift_left : t -> int -> t
  val shift_right : t -> int -> t
  val rotate_left : t -> int -> t
  val rotate_right : t -> int -> t
  val is_zero : t -> bool
  val is_negative : t -> bool
  val is_even : t -> bool
  val is_power_of_two : t -> bool
  val shortest_bit_length : t -> int
  val leading_zero_count : t -> int
  val trailing_zero_count : t -> int
  val pop_count : t -> int
  val log2 : t -> int
  val byte_count : int
  val to_bytes : ?byte_order:byte_order -> t -> bytes
  val of_bytes : ?byte_order:byte_order -> bytes -> t
end

module UInt256 : S
module Int256 : S
module UInt512 : S
module Int512 : S
module UInt1024 : S
module Int1024 : S

module Bit_converter_ex : sig
  val try_write_bytes :
    (module S with type t = 'a) -> ?byte_order:byte_order -> 'a -> bytes -> offset:int -> bool
end
