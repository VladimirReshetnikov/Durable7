(** Persistent payload-bearing interval map with exact-interval uniqueness. *)

type ('endpoint, 'value) entry
type ('endpoint, 'value) t
(** A persistent map keyed by closed intervals, with the same cached-maximum-endpoint trick as the
    interval tree and a payload per interval. *)

val empty : 'endpoint Common.Comparator.t -> ('endpoint, 'value) t
(** The empty map. *)

val comparator : ('endpoint, 'value) t -> 'endpoint Common.Comparator.t
(** The retained ordering policy. *)

val count : ('endpoint, 'value) t -> int
(** Number of entries in the map. *)

val nth : int -> ('endpoint, 'value) t -> ('endpoint, 'value) entry option
(** The entry at the given rank. *)

val lower_bound : low:'endpoint -> high:'endpoint -> ('endpoint, 'value) t -> int
(** The rank of the first key not less than the probe. *)

val upper_bound : low:'endpoint -> high:'endpoint -> ('endpoint, 'value) t -> int
(** The rank after any key equal to the probe. *)

val validate_interval :
  low:'endpoint -> high:'endpoint -> ('endpoint, 'value) t -> (unit, string) result
(** Checks that the interval's endpoints are not inverted. *)

val entry_low : ('endpoint, 'value) entry -> 'endpoint
(** The entry's low endpoint. *)

val entry_high : ('endpoint, 'value) entry -> 'endpoint
(** The entry's high endpoint. *)

val entry_value : ('endpoint, 'value) entry -> 'value
(** The entry's value. *)

val add :
  low:'endpoint ->
  high:'endpoint ->
  'value ->
  ('endpoint, 'value) t ->
  (('endpoint, 'value) t, string) result
(** A map containing the given entry. *)

val set :
  low:'endpoint ->
  high:'endpoint ->
  'value ->
  ('endpoint, 'value) t ->
  (bool * ('endpoint, 'value) t, string) result
(** A map with the key bound to the value, adding or replacing as needed. *)

val find_exact :
  low:'endpoint -> high:'endpoint -> ('endpoint, 'value) t -> ('endpoint, 'value) entry option
(** The stored interval exactly equal to the probe. *)

val remove :
  low:'endpoint ->
  high:'endpoint ->
  ('endpoint, 'value) t ->
  ('value * ('endpoint, 'value) t) option
(** A map without that entry. *)

val query_point : 'endpoint -> ('endpoint, 'value) t -> ('endpoint, 'value) entry list
(** Every entry whose interval contains the point. *)

val query_overlap :
  low:'endpoint ->
  high:'endpoint ->
  ('endpoint, 'value) t ->
  (('endpoint, 'value) entry list, string) result
(** Every entry whose interval overlaps the probe. *)

val to_list : ('endpoint, 'value) t -> ('endpoint, 'value) entry list
(** The entries, in the map's own order. *)
