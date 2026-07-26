(** Implementation of the structurally shared persistent sequence with cached monoidal measurements.

    Nodes cache their subtree's combined measure, so a split can skip a subtree on that cached value
    alone instead of visiting the elements under it. Versions share every node an edit did not
    touch. *)

type ('element, 'measure) node =
  | Empty
  | Leaf of 'element * 'measure
  | Node of int * 'measure * ('element, 'measure) node * ('element, 'measure) node

type ('element, 'measure) t = {
  tree_policy : ('element, 'measure) Measures.policy;
  root : ('element, 'measure) node;
}

type ('element, 'measure) cursor = { cursor_snapshot : ('element, 'measure) t; cursor_index : int }

let node_length = function Empty -> 0 | Leaf _ -> 1 | Node (length, _, _, _) -> length

let node_measure policy = function
  | Empty -> Measures.empty policy
  | Leaf (_, measure) | Node (_, measure, _, _) -> measure

let make_node policy left right =
  if node_length left = 0 then right
  else if node_length right = 0 then left
  else
    Node
      ( node_length left + node_length right,
        Measures.append policy (node_measure policy left) (node_measure policy right),
        left,
        right )

(* Weight-balanced reconstruction parameters (Hirai and Yamamoto, "Balancing
   weight-balanced trees", JFP 2011; the delta = 3, gamma = 2 pair Haskell's
   containers library uses for its proven [link]/[link2]). [join] descends the
   heavier spine and this constructor restores balance on the way back up with a
   single or double rotation. Rotations preserve in-order, and the measure monoid
   is associative, so regrouping through [make_node] recomputes the identical
   cached measure and length. Without this step [make_node] alone never rebalances,
   so repeated end-insertion builds a tree of depth proportional to its length. *)
let balance_delta = 3
let balance_gamma = 2

let balance policy left right =
  let left_length = node_length left in
  let right_length = node_length right in
  if right_length > balance_delta * left_length then
    match right with
    | Node (_, _, right_left, right_right) ->
        if node_length right_left < balance_gamma * node_length right_right then
          (* single left rotation *)
          make_node policy (make_node policy left right_left) right_right
        else (
          (* double left rotation *)
          match right_left with
          | Node (_, _, right_left_left, right_left_right) ->
              make_node policy
                (make_node policy left right_left_left)
                (make_node policy right_left_right right_right)
          | Empty | Leaf _ -> make_node policy (make_node policy left right_left) right_right)
    | Empty | Leaf _ -> make_node policy left right
  else if left_length > balance_delta * right_length then
    match left with
    | Node (_, _, left_left, left_right) ->
        if node_length left_right < balance_gamma * node_length left_left then
          (* single right rotation *)
          make_node policy left_left (make_node policy left_right right)
        else (
          (* double right rotation *)
          match left_right with
          | Node (_, _, left_right_left, left_right_right) ->
              make_node policy
                (make_node policy left_left left_right_left)
                (make_node policy left_right_right right)
          | Empty | Leaf _ -> make_node policy left_left (make_node policy left_right right))
    | Empty | Leaf _ -> make_node policy left right
  else make_node policy left right

let rec join policy left right =
  let left_length = node_length left in
  let right_length = node_length right in
  if left_length = 0 then right
  else if right_length = 0 then left
  else if left_length > balance_delta * right_length then
    match left with
    | Node (_, _, left_left, left_right) ->
        balance policy left_left (join policy left_right right)
    | Empty | Leaf _ -> make_node policy left right
  else if right_length > balance_delta * left_length then
    match right with
    | Node (_, _, right_left, right_right) ->
        balance policy (join policy left right_left) right_right
    | Empty | Leaf _ -> make_node policy left right
  else make_node policy left right

let empty tree_policy = { tree_policy; root = Empty }

let singleton tree_policy value =
  { tree_policy; root = Leaf (value, Measures.measure tree_policy value) }

let of_list tree_policy values =
  let rec build length values =
    if length = 0 then (Empty, values)
    else if length = 1 then
      match values with
      | [] -> assert false
      | value :: rest -> (Leaf (value, Measures.measure tree_policy value), rest)
    else
      let left_length = length / 2 in
      let left, remaining = build left_length values in
      let right, remaining = build (length - left_length) remaining in
      (make_node tree_policy left right, remaining)
  in
  let root, _ = build (List.length values) values in
  { tree_policy; root }

let policy tree = tree.tree_policy
let is_empty tree = node_length tree.root = 0
let length tree = node_length tree.root
let measure tree = node_measure tree.tree_policy tree.root

let fold_left folder state tree =
  let rec loop state = function
    | Empty -> state
    | Leaf (value, _) -> folder state value
    | Node (_, _, left, right) -> loop (loop state left) right
  in
  loop state tree.root

let fold_right folder tree state =
  let rec loop node state =
    match node with
    | Empty -> state
    | Leaf (value, _) -> folder value state
    | Node (_, _, left, right) -> loop left (loop right state)
  in
  loop tree.root state

let to_list tree = fold_right List.cons tree []
let iter action tree = fold_left (fun () value -> action value) () tree

let cons value tree =
  {
    tree with
    root = join tree.tree_policy (Leaf (value, Measures.measure tree.tree_policy value)) tree.root;
  }

let snoc tree value =
  {
    tree with
    root = join tree.tree_policy tree.root (Leaf (value, Measures.measure tree.tree_policy value));
  }

let concat left right =
  if not (Measures.compatible left.tree_policy right.tree_policy) then
    Error "cannot concatenate trees with different measurement policies"
  else Ok { left with root = join left.tree_policy left.root right.root }

let rec first_node = function
  | Empty -> None
  | Leaf (value, _) -> Some value
  | Node (_, _, left, right) -> (
      match first_node left with Some _ as value -> value | None -> first_node right)

let rec last_node = function
  | Empty -> None
  | Leaf (value, _) -> Some value
  | Node (_, _, left, right) -> (
      match last_node right with Some _ as value -> value | None -> last_node left)

let first tree = first_node tree.root
let last tree = last_node tree.root

let rec nth_node index = function
  | Empty -> None
  | Leaf (value, _) -> if index = 0 then Some value else None
  | Node (_, _, left, right) ->
      let left_length = node_length left in
      if index < left_length then nth_node index left else nth_node (index - left_length) right

let nth index tree = if index < 0 then None else nth_node index tree.root

let rec split_node policy index node =
  if index <= 0 then (Empty, node)
  else if index >= node_length node then (node, Empty)
  else
    match node with
    | Empty -> (Empty, Empty)
    | Leaf _ -> if index = 0 then (Empty, node) else (node, Empty)
    | Node (_, _, left, right) ->
        let left_length = node_length left in
        if index < left_length then
          let prefix, suffix = split_node policy index left in
          (prefix, join policy suffix right)
        else if index = left_length then (left, right)
        else
          let prefix, suffix = split_node policy (index - left_length) right in
          (join policy left prefix, suffix)

let split_at index tree =
  let left, right = split_node tree.tree_policy index tree.root in
  ({ tree with root = left }, { tree with root = right })

let take count tree = fst (split_at count tree)
let drop count tree = snd (split_at count tree)

let insert_at index value tree =
  if index < 0 || index > length tree then Error "sequence insertion index is out of range"
  else
    let left, right = split_at index tree in
    let with_value = snoc left value in
    concat with_value right

let update_at index update tree =
  if index < 0 || index >= length tree then Error "sequence update index is out of range"
  else
    let left, tail = split_at index tree in
    let _, right = split_at 1 tail in
    match nth 0 tail with
    | None -> assert false
    | Some current -> concat (snoc left (update current)) right

let remove_at index tree =
  if index < 0 || index >= length tree then Error "sequence removal index is out of range"
  else
    let left, tail = split_at index tree in
    let removed, right = split_at 1 tail in
    match first removed with
    | None -> assert false
    | Some value -> Result.map (fun successor -> (value, successor)) (concat left right)

let measure_range start count tree =
  if start < 0 || count < 0 || start > length tree - count then
    Error "sequence measure range is out of bounds"
  else Ok (measure (take count (drop start tree)))

let locate predicate tree =
  let rec descend index prefix = function
    | Empty -> None
    | Leaf (value, value_measure) ->
        if predicate (Measures.append tree.tree_policy prefix value_measure) then
          Some (index, prefix, value)
        else None
    | Node (_, _, left, right) ->
        let through_left =
          Measures.append tree.tree_policy prefix (node_measure tree.tree_policy left)
        in
        if predicate through_left then descend index prefix left
        else descend (index + node_length left) through_left right
  in
  (* A monotone predicate that already holds at the identity selects index zero on a nonempty
     tree, matching every sibling port and this module's own interface. An empty tree still
     misses because [descend] returns [None] for [Empty]. *)
  descend 0 (Measures.empty tree.tree_policy) tree.root

let validate tree =
  let rec loop = function
    | Empty -> Ok 0
    | Leaf _ -> Ok 1
    | Node (cached_length, _, left, right) -> (
        match (loop left, loop right) with
        | Error message, _ | _, Error message -> Error message
        | Ok left_length, Ok right_length ->
            if cached_length <> left_length + right_length then
              Error "cached sequence length is invalid"
            else if
              left_length > (balance_delta * right_length) + 1
              || right_length > (balance_delta * left_length) + 1
            then
              (* A weight-balanced node keeps its two subtree lengths within [balance_delta]
                 (plus one unit of slack for the smallest trees). This guards logarithmic depth:
                 without it, repeated end-insertion through a non-rotating join degrades every
                 descent to linear time. *)
              Error "weight-balance invariant is violated"
            else Ok cached_length)
  in
  (* Generic measures need not support equality, so validation recomputes shape and length while
     construction centralizes every cached-measure write. *)
  match loop tree.root with
  | Error message -> Error message
  | Ok _ -> Ok ()

let cursor_at_start tree = { cursor_snapshot = tree; cursor_index = 0 }
let cursor_at_end tree = { cursor_snapshot = tree; cursor_index = length tree }

let cursor_by_measure predicate tree =
  match locate predicate tree with
  | Some (index, _, _) -> (true, { cursor_snapshot = tree; cursor_index = index })
  | None -> (false, cursor_at_end tree)

let cursor_is_at_start value = value.cursor_index = 0
let cursor_is_at_end value = value.cursor_index = length value.cursor_snapshot

let cursor_measure_before value =
  Result.get_ok (measure_range 0 value.cursor_index value.cursor_snapshot)

let cursor_measure_after value =
  Result.get_ok
    (measure_range value.cursor_index
       (length value.cursor_snapshot - value.cursor_index)
       value.cursor_snapshot)

let cursor_peek_previous value = nth (value.cursor_index - 1) value.cursor_snapshot
let cursor_peek_next value = nth value.cursor_index value.cursor_snapshot

let cursor_move_previous value =
  if cursor_is_at_start value then None
  else Some { value with cursor_index = value.cursor_index - 1 }

let cursor_move_next value =
  if cursor_is_at_end value then None else Some { value with cursor_index = value.cursor_index + 1 }

let cursor_seek_by_measure predicate value = cursor_by_measure predicate value.cursor_snapshot

let cursor_insert element value =
  {
    cursor_snapshot = Result.get_ok (insert_at value.cursor_index element value.cursor_snapshot);
    cursor_index = value.cursor_index + 1;
  }

let cursor_delete_previous value =
  if cursor_is_at_start value then None
  else
    let _, snapshot = Result.get_ok (remove_at (value.cursor_index - 1) value.cursor_snapshot) in
    Some { cursor_snapshot = snapshot; cursor_index = value.cursor_index - 1 }

let cursor_delete_next value =
  if cursor_is_at_end value then None
  else
    let _, snapshot = Result.get_ok (remove_at value.cursor_index value.cursor_snapshot) in
    Some { cursor_snapshot = snapshot; cursor_index = value.cursor_index }

let cursor_replace_next element value =
  if cursor_is_at_end value then None
  else
    let snapshot =
      Result.get_ok (update_at value.cursor_index (fun _ -> element) value.cursor_snapshot)
    in
    Some { cursor_snapshot = snapshot; cursor_index = value.cursor_index }

let cursor_snapshot value = value.cursor_snapshot
