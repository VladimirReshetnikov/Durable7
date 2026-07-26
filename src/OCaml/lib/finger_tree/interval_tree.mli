(** Persistent closed-interval collection ordered by low endpoint. *)

type 'endpoint interval
type 'endpoint t
(** A persistent bag of closed intervals. Each subtree's maximum high endpoint is cached, so a query
    visits only the subtrees that can still contain a match. *)

val make_interval :
  'endpoint Common.Comparator.t -> 'endpoint -> 'endpoint -> ('endpoint interval, string) result
(** A closed interval from its endpoints, rejecting an inverted pair rather than letting it produce
    silently empty query results. *)

val low : 'endpoint interval -> 'endpoint
(** The interval's low endpoint. *)

val high : 'endpoint interval -> 'endpoint
(** The interval's high endpoint. *)

val overlaps : 'endpoint Common.Comparator.t -> 'endpoint interval -> 'endpoint interval -> bool
(** Whether the two intervals share at least one point. *)

val contains_point : 'endpoint Common.Comparator.t -> 'endpoint -> 'endpoint interval -> bool
(** Whether the interval contains the point. *)

val empty : 'endpoint Common.Comparator.t -> 'endpoint t
(** The empty tree. *)

val of_list : 'endpoint Common.Comparator.t -> 'endpoint interval list -> 'endpoint t
(** A tree holding a list's intervals, built in bulk rather than by repeated insertion. *)

val comparator : 'endpoint t -> 'endpoint Common.Comparator.t
(** The retained ordering policy. *)

val count : 'endpoint t -> int
(** Number of intervals in the tree. *)

val is_empty : 'endpoint t -> bool
(** Whether the tree holds no intervals. *)

val maximum_high : 'endpoint t -> 'endpoint option
(** The largest high endpoint below this subtree. Cached, which is what lets a query skip a subtree
    without descending into it. *)

val nth : int -> 'endpoint t -> 'endpoint interval option
(** The interval at the given rank. *)

val lower_bound : 'endpoint -> 'endpoint t -> int
(** The rank of the first key not less than the probe. *)

val upper_bound : 'endpoint -> 'endpoint t -> int
(** The rank after any key equal to the probe. *)

val insert : 'endpoint interval -> 'endpoint t -> 'endpoint t
(** A tree containing the given interval. *)

val remove : 'endpoint interval -> 'endpoint t -> bool * 'endpoint t
(** A tree without that interval. *)

val remove_at : int -> 'endpoint t -> ('endpoint t, string) result
(** A tree without the interval at the position. *)

val find_overlap : 'endpoint interval -> 'endpoint t -> 'endpoint interval option
(** An interval overlapping the probe, or [None] when none does. *)

val find_all_overlaps : 'endpoint interval -> 'endpoint t -> 'endpoint interval list
(** Every interval overlapping the probe. *)

val query_point : 'endpoint -> 'endpoint t -> 'endpoint interval list
(** Every interval containing the point. *)

val to_list : 'endpoint t -> 'endpoint interval list
(** The intervals, in the tree's own order. *)
