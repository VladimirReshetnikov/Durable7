(** Persistent generic rope with indexed split/concat editing. *)

type 'element t

val empty : 'element t
val singleton : 'element -> 'element t
val of_list : 'element list -> 'element t
val length : 'element t -> int
val is_empty : 'element t -> bool
val nth : int -> 'element t -> 'element option
val concat : 'element t -> 'element t -> 'element t
val split_at : int -> 'element t -> 'element t * 'element t
val insert : int -> 'element list -> 'element t -> ('element t, string) result
val remove : start:int -> length:int -> 'element t -> ('element t, string) result
val slice : start:int -> length:int -> 'element t -> ('element t, string) result
val to_list : 'element t -> 'element list

module Builder : sig
  type 'element rope = 'element t
  type 'element t

  val create : unit -> 'element t
  val append : 'element -> 'element t -> unit
  val append_rope : 'element rope -> 'element t -> unit
  val freeze : 'element t -> 'element rope
end
