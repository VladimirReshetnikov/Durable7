(** Persistent directed graph with explicit isolated vertices. *)

type 'vertex edge = { source : 'vertex; target : 'vertex }
type 'vertex t
(** A persistent directed graph over hashed vertices. Successors and predecessors are both indexed,
    so neither direction is a scan. *)

val empty : 'vertex Common.Hash_policy.t -> 'vertex t
(** The empty collection. *)

val vertex_count : 'vertex t -> int
(** Number of vertices. *)

val edge_count : 'vertex t -> int
(** Number of directed edges. *)

val contains_vertex : 'vertex -> 'vertex t -> bool
(** Whether the vertex is present. *)

val contains_edge : 'vertex -> 'vertex -> 'vertex t -> bool
(** Whether the directed edge is present. *)

val add_vertex : 'vertex -> 'vertex t -> 'vertex t
(** A graph containing the vertex, with no edges added. *)

val add_edge : 'vertex -> 'vertex -> 'vertex t -> 'vertex t
(** A graph containing the directed edge, adding either endpoint that is missing. *)

val remove_edge : 'vertex -> 'vertex -> 'vertex t -> 'vertex t
(** A graph without that directed edge, leaving both endpoints in place. *)

val remove_vertex : 'vertex -> 'vertex t -> 'vertex t
(** A graph without the vertex and without any edge touching it. *)

val outgoing : 'vertex -> 'vertex t -> 'vertex Persistent_hash_set.t
(** The vertices the given vertex points at. *)

val incoming : 'vertex -> 'vertex t -> 'vertex Persistent_hash_set.t
(** The vertices pointing at the given vertex. Maintained as its own index, so this is a lookup
    rather than a scan. *)

val vertices : 'vertex t -> 'vertex list
(** The vertices. *)

val edges : 'vertex t -> 'vertex edge list
(** The directed edges. *)

val reverse : 'vertex t -> 'vertex t
(** The collection in the opposite order. *)
