(** Persistent directed graph with explicit isolated vertices. *)

type 'vertex edge = { source : 'vertex; target : 'vertex }
type 'vertex t

val empty : 'vertex Common.Hash_policy.t -> 'vertex t
val vertex_count : 'vertex t -> int
val edge_count : 'vertex t -> int
val contains_vertex : 'vertex -> 'vertex t -> bool
val contains_edge : 'vertex -> 'vertex -> 'vertex t -> bool
val add_vertex : 'vertex -> 'vertex t -> 'vertex t
val add_edge : 'vertex -> 'vertex -> 'vertex t -> 'vertex t
val remove_edge : 'vertex -> 'vertex -> 'vertex t -> 'vertex t
val remove_vertex : 'vertex -> 'vertex t -> 'vertex t
val outgoing : 'vertex -> 'vertex t -> 'vertex Persistent_hash_set.t
val incoming : 'vertex -> 'vertex t -> 'vertex Persistent_hash_set.t
val vertices : 'vertex t -> 'vertex list
val edges : 'vertex t -> 'vertex edge list
val reverse : 'vertex t -> 'vertex t
