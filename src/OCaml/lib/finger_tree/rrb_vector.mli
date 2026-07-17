(** Persistent indexed vector facade over the shared balanced sequence. *)

type 'element t
type statistics = { count : int; estimated_leaves : int; depth : int }

val empty : 'element t
val singleton : 'element -> 'element t
val of_list : 'element list -> 'element t
val length : 'element t -> int
val is_empty : 'element t -> bool
val nth : int -> 'element t -> 'element option
val set : int -> 'element -> 'element t -> ('element t, string) result
val append : 'element -> 'element t -> 'element t
val prepend : 'element -> 'element t -> 'element t
val concat : 'element t -> 'element t -> 'element t
val insert : int -> 'element -> 'element t -> ('element t, string) result
val remove : int -> 'element t -> ('element * 'element t, string) result
val pop : 'element t -> ('element t * 'element) option
val split_at : int -> 'element t -> 'element t * 'element t
val slice : start:int -> length:int -> 'element t -> ('element t, string) result
val to_list : 'element t -> 'element list
val statistics : 'element t -> statistics

module Builder : sig
  type 'element vector = 'element t
  type 'element t

  val create : 'element vector -> 'element t
  val length : 'element t -> int
  val append : 'element -> 'element t -> unit
  val set : int -> 'element -> 'element t -> (unit, string) result
  val remove : int -> 'element t -> ('element, string) result
  val freeze : 'element t -> 'element vector
end
