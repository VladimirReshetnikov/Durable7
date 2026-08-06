(** Implementation of the persistent meldable minimum heap surface.

    A bootstrapped skew-binomial minimum heap: a global rank-zero root holds the minimum outside
    the forest, so [minimum] is a field read, [insert] and [meld] attach one tree with at most one
    skew link, and only [delete_minimum] pays the forest work. This is the same kernel as
    {!Persistent_monotone_action_heap} with the action tags removed — that module carries the
    tagged generalization of exactly these functions, which is why the two stay line-for-line
    comparable. Forest spines and tree depths are logarithmic in the element count, so the
    recursions here are shallow. *)

type 'element tree = { rank : int; payload : 'element; children : 'element tree list }
type 'element t = { order : 'element Common.Comparator.t; root : 'element tree option; size : int }

type statistics = {
  count : int;
  root_forest_length : int;
  maximum_rank : int;
  maximum_depth : int;
}

let empty order = { order; root = None; size = 0 }
let comparator heap = heap.order
let count heap = heap.size
let is_empty heap = Option.is_none heap.root
let le order left right = Common.Comparator.compare order left.payload right.payload <= 0

(* ------------------------------------------------------------------------------------------- *)
(* Skew-binomial kernel: the untagged specialization of Persistent_monotone_action_heap's       *)
(* ------------------------------------------------------------------------------------------- *)

(* Fuse a rank-zero tree with two equal-rank trees; the three-way winner takes both losers. *)
let skew_link order zero first second =
  if le order first zero && le order first second then
    { rank = first.rank + 1; payload = first.payload; children = zero :: second :: first.children }
  else if le order second zero && le order second first then
    {
      rank = second.rank + 1;
      payload = second.payload;
      children = zero :: first :: second.children;
    }
  else { rank = first.rank + 1; payload = zero.payload; children = first :: second :: zero.children }

(* O(1) worst case: at most one skew link, when the two leading ranks agree. *)
let skew_insert order tree forest =
  match forest with
  | first :: second :: rest when first.rank = second.rank ->
      skew_link order tree first second :: rest
  | _ -> tree :: forest

let link order first second =
  if le order first second then
    { rank = first.rank + 1; payload = first.payload; children = second :: first.children }
  else { rank = second.rank + 1; payload = second.payload; children = first :: second.children }

(* Lifts a minimum tree out of a non-empty forest. A tie keeps the earliest spine occurrence. *)
let rec get_minimum order forest =
  match forest with
  | [] -> invalid_arg "get_minimum requires a non-empty forest"
  | [ only ] -> (only, [])
  | head :: tail ->
      let minimum, remainder = get_minimum order tail in
      if le order head minimum then (head, tail) else (minimum, head :: remainder)

(* Decomposes the fused primitive-child encoding of a rank-[rank] tree into the rank-zero children
   skew-linking produced, the ranked children ordinary linking produced, and the embedded forest.
   The rank-zero ambiguity at [rank = 1] is resolved in favour of the structural prefix. *)
let rec split_forest rank zeros trees forest =
  if rank = 0 then (zeros, trees, forest)
  else
    match forest with
    | [] -> failwith "invalid skew-binomial forest invariant"
    | [ first ] ->
        if rank = 1 then (zeros, first :: trees, [])
        else failwith "invalid skew-binomial forest invariant"
    | first :: second :: rest ->
        if rank = 1 then
          if second.rank = 0 then (first :: zeros, second :: trees, rest)
          else (zeros, first :: trees, second :: rest)
        else if first.rank = second.rank then (zeros, first :: second :: trees, rest)
        else if first.rank = 0 then split_forest (rank - 1) (first :: zeros) (second :: trees) rest
        else split_forest (rank - 1) zeros (first :: trees) (second :: rest)

let rec insert_ranked order tree forest =
  match forest with
  | [] -> [ tree ]
  | head :: _ when tree.rank < head.rank -> tree :: forest
  | head :: tail -> insert_ranked order (link order tree head) tail

let rec uniquify order forest =
  match forest with [] -> [] | head :: tail -> insert_ranked order head (uniquify order tail)

let rec union_unique order left right =
  match (left, right) with
  | [], _ -> right
  | _, [] -> left
  | left_head :: left_tail, right_head :: right_tail ->
      if left_head.rank < right_head.rank then left_head :: union_unique order left_tail right
      else if left_head.rank > right_head.rank then
        right_head :: union_unique order left right_tail
      else
        insert_ranked order
          (link order left_head right_head)
          (union_unique order left_tail right_tail)

let skew_meld order left right = union_unique order (uniquify order left) (uniquify order right)

(* ------------------------------------------------------------------------------------------- *)
(* Heap surface                                                                                 *)
(* ------------------------------------------------------------------------------------------- *)

let insert value heap =
  let singleton = { rank = 0; payload = value; children = [] } in
  match heap.root with
  | None -> { heap with root = Some singleton; size = 1 }
  | Some root ->
      let updated =
        (* A tie favours the new entry, which then keeps the old root as an untouched child. *)
        if le heap.order singleton root then { singleton with children = [ root ] }
        else { root with children = skew_insert heap.order singleton root.children }
      in
      { heap with root = Some updated; size = heap.size + 1 }

let of_list order values = List.fold_left (fun heap value -> insert value heap) (empty order) values

let meld left right =
  if not (Common.Comparator.same left.order right.order) then
    invalid_arg "heap melding requires the same comparator object";
  match (left.root, right.root) with
  | None, _ -> { right with order = left.order }
  | _, None -> left
  | Some left_root, Some right_root ->
      let left_wins = le left.order left_root right_root in
      let winner = if left_wins then left_root else right_root in
      let loser = if left_wins then right_root else left_root in
      let root = { winner with children = skew_insert left.order loser winner.children } in
      { left with root = Some root; size = left.size + right.size }

let minimum heap = Option.map (fun tree -> tree.payload) heap.root

let delete_minimum heap =
  match heap.root with
  | None -> None
  | Some root -> (
      match root.children with
      | [] -> Some { heap with root = None; size = 0 }
      | children ->
          let selected, remainder = get_minimum heap.order children in
          let zeros, trees, embedded = split_forest selected.rank [] [] selected.children in
          let merged = skew_meld heap.order (skew_meld heap.order trees remainder) embedded in
          (* The rank-zero children go back one at a time, in first-encountered order. *)
          let merged =
            List.fold_left
              (fun forest zero -> skew_insert heap.order zero forest)
              merged (List.rev zeros)
          in
          Some
            {
              heap with
              root = Some { selected with rank = 0; children = merged };
              size = heap.size - 1;
            })

let minimum_view heap =
  match (minimum heap, delete_minimum heap) with
  | Some value, Some rest -> Some (value, rest)
  | _ -> None

let to_sorted_list heap =
  let rec drain collected heap =
    match minimum_view heap with
    | None -> List.rev collected
    | Some (value, rest) -> drain (value :: collected) rest
  in
  drain [] heap

(* The audit measures the forest that now exists: every field below is walked, not synthesized,
   and [count] is the walked node count so a size-cache drift is observable. *)
let statistics heap =
  let rec measure depth tree =
    List.fold_left
      (fun (nodes, max_rank, max_depth) child ->
        let child_nodes, child_rank, child_depth = measure (depth + 1) child in
        (nodes + child_nodes, max max_rank child_rank, max max_depth child_depth))
      (1, tree.rank, depth) tree.children
  in
  match heap.root with
  | None -> { count = 0; root_forest_length = 0; maximum_rank = 0; maximum_depth = 0 }
  | Some root ->
      let nodes, max_rank, max_depth = measure 1 root in
      {
        count = nodes;
        root_forest_length = List.length root.children;
        maximum_rank = max_rank;
        maximum_depth = max_depth;
      }
