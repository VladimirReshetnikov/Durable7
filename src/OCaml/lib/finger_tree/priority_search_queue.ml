(** Implementation of the persistent key-ordered priority-search queue with a cached priority-then-
    key winner.

    The representation is the winner-cached key-ordered AVL every sibling workspace ships: a
    path-copied AVL tree ordered by key in which every node also caches the minimum-priority entry
    of its whole subtree (ties broken by key order). The key index answers keyed reads and writes in
    O(log n); the winner caches answer the minimum-priority question at the root in O(1) with no
    search; and a keyed delete of the cached winner gives an O(log n) delete-min. Each node also
    caches its subtree size, so rank selection and key-rank bounds are O(log n) size-directed
    descents. *)

type ('key, 'priority, 'value) entry = { key : 'key; priority : 'priority; value : 'value }

type ('key, 'priority, 'value) node = {
  entry : ('key, 'priority, 'value) entry;
  left : ('key, 'priority, 'value) node option;
  right : ('key, 'priority, 'value) node option;
  winner : ('key, 'priority, 'value) entry;
  height : int;
  size : int;
}

type ('key, 'priority, 'value) t = {
  key_order : 'key Common.Comparator.t;
  priority_order : 'priority Common.Comparator.t;
  root : ('key, 'priority, 'value) node option;
  recomputation_count : int;
}

type statistics = { count : int; estimated_height : int; winner_recomputations : int }

let empty ~key_comparator ~priority_comparator =
  {
    key_order = key_comparator;
    priority_order = priority_comparator;
    root = None;
    recomputation_count = 0;
  }

let node_size = function None -> 0 | Some node -> node.size
let node_height = function None -> 0 | Some node -> node.height
let count queue = node_size queue.root
let is_empty queue = count queue = 0
let entry_key entry = entry.key
let entry_priority entry = entry.priority
let entry_value entry = entry.value
let compare_key queue = Common.Comparator.compare queue.key_order

let before queue left right =
  let by_priority = Common.Comparator.compare queue.priority_order left.priority right.priority in
  by_priority < 0 || (by_priority = 0 && compare_key queue left.key right.key < 0)

(* The subtree winner: the node's own entry unless a child subtree's cached winner beats it. One
   cached winner per node is what makes the root's winner the whole queue's minimum without a
   search. *)
let select_winner queue entry left right =
  let winner = match left with Some l when before queue l.winner entry -> l.winner | _ -> entry in
  match right with Some r when before queue r.winner winner -> r.winner | _ -> winner

(* Every path-copied node recomputes its winner cache; [recomputations] counts them so the
   statistics report how much winner maintenance a lineage has performed. *)
let make queue recomputations entry left right =
  incr recomputations;
  {
    entry;
    left;
    right;
    winner = select_winner queue entry left right;
    height = 1 + Int.max (node_height left) (node_height right);
    size = 1 + node_size left + node_size right;
  }

let rotate_left queue recomputations node =
  match node.right with
  | None -> invalid_arg "priority-search left rotation requires a right child"
  | Some pivot ->
      make queue recomputations pivot.entry
        (Some (make queue recomputations node.entry node.left pivot.left))
        pivot.right

let rotate_right queue recomputations node =
  match node.left with
  | None -> invalid_arg "priority-search right rotation requires a left child"
  | Some pivot ->
      make queue recomputations pivot.entry pivot.left
        (Some (make queue recomputations node.entry pivot.right node.right))

let balance queue recomputations node =
  let factor = node_height node.left - node_height node.right in
  if factor > 1 then
    match node.left with
    | None -> invalid_arg "priority-search left imbalance lacks a left child"
    | Some left ->
        if node_height left.left < node_height left.right then
          rotate_right queue recomputations
            (make queue recomputations node.entry
               (Some (rotate_left queue recomputations left))
               node.right)
        else rotate_right queue recomputations node
  else if factor < -1 then
    match node.right with
    | None -> invalid_arg "priority-search right imbalance lacks a right child"
    | Some right ->
        if node_height right.right < node_height right.left then
          rotate_left queue recomputations
            (make queue recomputations node.entry node.left
               (Some (rotate_right queue recomputations right)))
        else rotate_left queue recomputations node
  else node

let publish queue recomputations root =
  { queue with root; recomputation_count = queue.recomputation_count + !recomputations }

let rec find_node queue key = function
  | None -> None
  | Some node ->
      let ordering = compare_key queue key node.entry.key in
      if ordering = 0 then Some node.entry
      else if ordering < 0 then find_node queue key node.left
      else find_node queue key node.right

let find key queue = find_node queue key queue.root
let mem key queue = Option.is_some (find key queue)

let nth index queue =
  if index < 0 || index >= count queue then None
  else
    let rec select index = function
      | None -> None
      | Some node ->
          let left_size = node_size node.left in
          if index < left_size then select index node.left
          else if index = left_size then Some node.entry
          else select (index - left_size - 1) node.right
    in
    select index queue.root

(* The rank of the boundary before ([upper] = false) or after ([upper] = true) the probe's
   equivalence class: an O(log n) size-directed descent. *)
let bound_rank upper key queue =
  let rec descend rank = function
    | None -> rank
    | Some node ->
        let ordering = compare_key queue node.entry.key key in
        if ordering < 0 || (upper && ordering = 0) then
          descend (rank + node_size node.left + 1) node.right
        else descend rank node.left
  in
  descend 0 queue.root

let lower_bound key queue = bound_rank false key queue
let upper_bound key queue = bound_rank true key queue

let rec insert_node queue recomputations entry ~overwrite = function
  | None -> make queue recomputations entry None None
  | Some node ->
      let ordering = compare_key queue entry.key node.entry.key in
      if ordering = 0 then
        if overwrite then
          (* Replacement keeps the stored key representative and re-seats the entry in the winner
             caches along this path. *)
          make queue recomputations
            { key = node.entry.key; priority = entry.priority; value = entry.value }
            node.left node.right
        else invalid_arg "priority-search insertion reached an existing key"
      else if ordering < 0 then
        balance queue recomputations
          (make queue recomputations node.entry
             (Some (insert_node queue recomputations entry ~overwrite node.left))
             node.right)
      else
        balance queue recomputations
          (make queue recomputations node.entry node.left
             (Some (insert_node queue recomputations entry ~overwrite node.right)))

let add key priority value queue =
  if mem key queue then Error "priority-search queue already contains an equivalent key"
  else
    let recomputations = ref 0 in
    let root = insert_node queue recomputations { key; priority; value } ~overwrite:false queue.root in
    Ok (publish queue recomputations (Some root))

let set key priority value queue =
  let added = not (mem key queue) in
  let recomputations = ref 0 in
  let root = insert_node queue recomputations { key; priority; value } ~overwrite:true queue.root in
  (added, publish queue recomputations (Some root))

let rec extract_minimum queue recomputations node =
  match node.left with
  | None -> (node.entry, node.right)
  | Some left ->
      let minimum, rest = extract_minimum queue recomputations left in
      (minimum, Some (balance queue recomputations (make queue recomputations node.entry rest node.right)))

let rec remove_node queue recomputations key = function
  | None -> None
  | Some node -> (
      let ordering = compare_key queue key node.entry.key in
      if ordering = 0 then
        match (node.left, node.right) with
        | None, other | other, None -> Some (node.entry, other)
        | Some _, Some right ->
            let successor, rest = extract_minimum queue recomputations right in
            Some
              ( node.entry,
                Some (balance queue recomputations (make queue recomputations successor node.left rest))
              )
      else if ordering < 0 then
        Option.map
          (fun (removed, left) ->
            (removed, Some (balance queue recomputations (make queue recomputations node.entry left node.right))))
          (remove_node queue recomputations key node.left)
      else
        Option.map
          (fun (removed, right) ->
            (removed, Some (balance queue recomputations (make queue recomputations node.entry node.left right))))
          (remove_node queue recomputations key node.right))

let remove key queue =
  let recomputations = ref 0 in
  Option.map
    (fun (removed, root) -> (removed, publish queue recomputations root))
    (remove_node queue recomputations key queue.root)

let minimum queue = Option.map (fun node -> node.winner) queue.root

let minimum_view queue =
  match minimum queue with
  | None -> None
  | Some entry -> Option.map (fun (_, successor) -> (entry, successor)) (remove entry.key queue)

let entries_by_key queue =
  let rec walk node acc =
    match node with
    | None -> acc
    | Some node -> walk node.left (node.entry :: walk node.right acc)
  in
  walk queue.root []

let statistics queue =
  {
    count = count queue;
    estimated_height = node_height queue.root;
    winner_recomputations = queue.recomputation_count;
  }
