(** Persistent rank-addressable sorted set with representative retention. *)

type 'element t

val empty : 'element Common.Comparator.t -> 'element t
val of_list : 'element Common.Comparator.t -> 'element list -> 'element t
val comparator : 'element t -> 'element Common.Comparator.t
val count : 'element t -> int
val is_empty : 'element t -> bool
val minimum : 'element t -> 'element option
val maximum : 'element t -> 'element option
val nth : int -> 'element t -> 'element option
val index_of : 'element -> 'element t -> int option
val find : 'element -> 'element t -> 'element option
val mem : 'element -> 'element t -> bool
val add : 'element -> 'element t -> bool * 'element t
val remove : 'element -> 'element t -> bool * 'element t
val floor : 'element -> 'element t -> 'element option
val ceiling : 'element -> 'element t -> 'element option
val lower : 'element -> 'element t -> 'element option
val higher : 'element -> 'element t -> 'element option
val union : 'element t -> 'element t -> 'element t
val intersect : 'element t -> 'element t -> 'element t
val difference : 'element t -> 'element t -> 'element t
val range : start:int -> count:int -> 'element t -> ('element t, string) result
val value_range : minimum:'element -> maximum:'element -> 'element t -> 'element t
val to_list : 'element t -> 'element list

module Builder : sig
  type 'element set = 'element t
  type 'element t

  val create : 'element set -> 'element t
  val add : 'element -> 'element t -> bool
  val remove : 'element -> 'element t -> bool
  val freeze : 'element t -> 'element set
end
