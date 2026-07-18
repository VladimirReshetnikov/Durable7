(** Persistent payload-bearing interval map with exact-interval uniqueness. *)

type ('endpoint, 'value) entry
type ('endpoint, 'value) t

val empty : 'endpoint Common.Comparator.t -> ('endpoint, 'value) t
val comparator : ('endpoint, 'value) t -> 'endpoint Common.Comparator.t
val count : ('endpoint, 'value) t -> int
val nth : int -> ('endpoint, 'value) t -> ('endpoint, 'value) entry option
val lower_bound : low:'endpoint -> high:'endpoint -> ('endpoint, 'value) t -> int
val upper_bound : low:'endpoint -> high:'endpoint -> ('endpoint, 'value) t -> int
val entry_low : ('endpoint, 'value) entry -> 'endpoint
val entry_high : ('endpoint, 'value) entry -> 'endpoint
val entry_value : ('endpoint, 'value) entry -> 'value

val add :
  low:'endpoint ->
  high:'endpoint ->
  'value ->
  ('endpoint, 'value) t ->
  (('endpoint, 'value) t, string) result

val set :
  low:'endpoint ->
  high:'endpoint ->
  'value ->
  ('endpoint, 'value) t ->
  (bool * ('endpoint, 'value) t, string) result

val find_exact :
  low:'endpoint -> high:'endpoint -> ('endpoint, 'value) t -> ('endpoint, 'value) entry option

val remove :
  low:'endpoint ->
  high:'endpoint ->
  ('endpoint, 'value) t ->
  ('value * ('endpoint, 'value) t) option

val query_point : 'endpoint -> ('endpoint, 'value) t -> ('endpoint, 'value) entry list

val query_overlap :
  low:'endpoint ->
  high:'endpoint ->
  ('endpoint, 'value) t ->
  (('endpoint, 'value) entry list, string) result

val to_list : ('endpoint, 'value) t -> ('endpoint, 'value) entry list
