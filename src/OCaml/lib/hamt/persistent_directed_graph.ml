(** Implementation of the persistent directed graph with explicit isolated vertices.

    Successors and predecessors are both indexed, so neither direction is a scan. *)

type 'vertex edge = { source : 'vertex; target : 'vertex }

type 'vertex t = {
  vertices : 'vertex Persistent_hash_set.t;
  edges : ('vertex, 'vertex) Persistent_relation.t;
}

let empty policy =
  {
    vertices = Persistent_hash_set.empty policy;
    edges = Persistent_relation.empty ~left_policy:policy ~right_policy:policy;
  }

let vertex_count graph = Persistent_hash_set.count graph.vertices
let edge_count graph = Persistent_relation.pair_count graph.edges
let contains_vertex vertex graph = Persistent_hash_set.mem vertex graph.vertices
let contains_edge source target graph = Persistent_relation.contains source target graph.edges

let add_vertex vertex graph =
  let vertices = Persistent_hash_set.add vertex graph.vertices in
  if vertices == graph.vertices then graph else { graph with vertices }

let add_edge source target graph =
  let vertices =
    graph.vertices |> Persistent_hash_set.add source |> Persistent_hash_set.add target
  in
  let stored_source = Option.get (Persistent_hash_set.find_opt source vertices) in
  let stored_target = Option.get (Persistent_hash_set.find_opt target vertices) in
  let edges = Persistent_relation.add stored_source stored_target graph.edges in
  if vertices == graph.vertices && edges == graph.edges then graph else { vertices; edges }

let remove_edge source target graph =
  let edges = Persistent_relation.remove source target graph.edges in
  if edges == graph.edges then graph else { graph with edges }

let remove_vertex vertex graph =
  match Persistent_hash_set.find_opt vertex graph.vertices with
  | None -> graph
  | Some stored ->
      {
        vertices = Persistent_hash_set.remove stored graph.vertices;
        edges =
          graph.edges
          |> Persistent_relation.remove_left stored
          |> Persistent_relation.remove_right stored;
      }

let outgoing vertex graph = Persistent_relation.rights vertex graph.edges
let incoming vertex graph = Persistent_relation.lefts vertex graph.edges
let vertices graph = Persistent_hash_set.to_list graph.vertices

let edges graph =
  List.map
    (fun entry ->
      { source = entry.Persistent_relation.left; target = entry.Persistent_relation.right })
    (Persistent_relation.to_list graph.edges)

let reverse graph = { graph with edges = Persistent_relation.inverse graph.edges }
