(** Implementation of the persistent Brodal-Okasaki heap with lazily composable monotone priority
    actions.

    Every tree and every forest spine cell carries a pending action. The logical priority of a tree
    is [apply action raw_priority]; the logical head of a spine cell is its raw head with the
    cell's action pushed onto it; the logical tail is the raw tail with the same action pushed onto
    it. The kernel below therefore reaches structure only through [forest_head], [forest_tail], and
    [expose], so a pending action is never skipped and never applied twice.

    Pushdown always composes the newer action as the {e outer} one: [compose policy outer inner]
    denotes [outer (inner p)], and at every site the tag being pushed down is the newer of the two.
    Losing trees are attached through identity-tagged spine cells built by [cons], and a winner is
    materialized by [expose] before anything new is linked underneath it. Together those two rules
    are what keep a later insertion out of an earlier transform's reach. *)

type ('priority, 'action) policy = {
  policy_id : string;
  action_identity : 'action;
  action_is_identity : 'action -> bool;
  action_compose : 'action -> 'action -> 'action;
  action_apply : 'action -> 'priority -> 'priority;
  priority_order : 'priority Common.Comparator.t;
}

type ('element, 'priority, 'action) tree = {
  rank : int;
  payload : 'element;
  raw_priority : 'priority;
  raw_children : ('element, 'priority, 'action) forest option;
  tree_action : 'action;
}

and ('element, 'priority, 'action) forest = {
  raw_head : ('element, 'priority, 'action) tree;
  raw_tail : ('element, 'priority, 'action) forest option;
  forest_action : 'action;
}

type ('element, 'priority, 'action) t = {
  root : ('element, 'priority, 'action) tree option;
  size : int;
  heap_policy : ('priority, 'action) policy;
}

type ('element, 'priority) entry = { element : 'element; priority : 'priority }

type statistics = {
  count : int;
  root_forest_length : int;
  maximum_rank : int;
  maximum_depth : int;
  tagged_component_count : int;
}

type 'value clamp = {
  constant_value : 'value option;
  lower_bound : 'value option;
  upper_bound : 'value option;
}

(* Raised by the audit and caught at its boundary; the kernel never lets it escape. *)
exception Invariant of string

let invariant message = raise (Invariant message)

(* A structural impossibility rather than a caller error: only a corrupted representation, which
   this module cannot produce, can reach these branches. *)
let corrupt message = failwith message

let create_policy ~id ~identity ~is_identity ~compose ~apply ~comparator ~laws_verified () =
  if String.trim id = "" then Error "monotone action policy id must not be empty"
  else if not laws_verified then Error "monotone action policy laws must be explicitly verified"
  else
    Ok
      {
        policy_id = id;
        action_identity = identity;
        action_is_identity = is_identity;
        action_compose = compose;
        action_apply = apply;
        priority_order = comparator;
      }

let policy_id policy = policy.policy_id
let policy_comparator policy = policy.priority_order
let policy_identity policy = policy.action_identity
let policy_is_identity policy action = policy.action_is_identity action
let compose_actions policy outer inner = policy.action_compose outer inner
let apply_action policy action priority = policy.action_apply action priority

let is_identity policy action = policy.action_is_identity action
let compose policy outer inner = policy.action_compose outer inner
let apply policy action priority = policy.action_apply action priority
let order_compare policy left right = Common.Comparator.compare policy.priority_order left right

(* ------------------------------------------------------------------------------------------- *)
(* The shipped clamp family                                                                     *)
(* ------------------------------------------------------------------------------------------- *)

let clamp_identity = { constant_value = None; lower_bound = None; upper_bound = None }
let clamp_at_least lower = { constant_value = None; lower_bound = Some lower; upper_bound = None }
let clamp_at_most upper = { constant_value = None; lower_bound = None; upper_bound = Some upper }
let clamp_constant value = { constant_value = Some value; lower_bound = None; upper_bound = None }
let clamp_is_constant action = Option.is_some action.constant_value
let clamp_constant_value action = action.constant_value
let clamp_lower_bound action = action.lower_bound
let clamp_upper_bound action = action.upper_bound

let clamp_is_identity action =
  Option.is_none action.constant_value
  && Option.is_none action.lower_bound
  && Option.is_none action.upper_bound

(* Constant first, then the lower bound, then the upper bound. *)
let clamp_apply order action priority =
  match action.constant_value with
  | Some value -> value
  | None -> (
      match action.lower_bound with
      | Some lower when Common.Comparator.compare order priority lower < 0 -> lower
      | _ -> (
          match action.upper_bound with
          | Some upper when Common.Comparator.compare order priority upper > 0 -> upper
          | _ -> priority))

let clamp_compose order outer inner =
  let cmp = Common.Comparator.compare order in
  if clamp_is_identity outer then inner
  else if clamp_is_identity inner then outer
  else if Option.is_some outer.constant_value then outer
  else
    match inner.constant_value with
    | Some value -> clamp_constant (clamp_apply order outer value)
    | None -> (
        (* Bounds that cannot both be met collapse to the outer, newer, boundary. *)
        match (inner.upper_bound, outer.lower_bound) with
        | Some inner_upper, Some outer_lower when cmp inner_upper outer_lower < 0 ->
            clamp_constant outer_lower
        | _ -> (
            match (inner.lower_bound, outer.upper_bound) with
            | Some inner_lower, Some outer_upper when cmp inner_lower outer_upper > 0 ->
                clamp_constant outer_upper
            | _ ->
                (* Overlapping bounds intersect; an equal boundary keeps the inner, older,
                   representative. *)
                let lower =
                  match (inner.lower_bound, outer.lower_bound) with
                  | None, outer_lower -> outer_lower
                  | Some inner_lower, None -> Some inner_lower
                  | Some inner_lower, Some outer_lower ->
                      Some (if cmp inner_lower outer_lower >= 0 then inner_lower else outer_lower)
                in
                let upper =
                  match (inner.upper_bound, outer.upper_bound) with
                  | None, outer_upper -> outer_upper
                  | Some inner_upper, None -> Some inner_upper
                  | Some inner_upper, Some outer_upper ->
                      Some (if cmp inner_upper outer_upper <= 0 then inner_upper else outer_upper)
                in
                { constant_value = None; lower_bound = lower; upper_bound = upper }))

let clamp_between policy ~lower ~upper =
  if Common.Comparator.compare (policy_comparator policy) lower upper > 0 then
    Error "a clamp lower bound must not compare greater than its upper bound"
  else Ok { constant_value = None; lower_bound = Some lower; upper_bound = Some upper }

let clamp_policy ~id order =
  create_policy ~id ~identity:clamp_identity ~is_identity:clamp_is_identity
    ~compose:(clamp_compose order) ~apply:(clamp_apply order) ~comparator:order ~laws_verified:true
    ()

(* ------------------------------------------------------------------------------------------- *)
(* Tag pushdown                                                                                 *)
(* ------------------------------------------------------------------------------------------- *)

(* Pushes a newer action onto one tree. The newer action is the outer one. *)
let tag_tree policy outer tree =
  if is_identity policy outer then tree
  else { tree with tree_action = compose policy outer tree.tree_action }

(* Pushes a newer action onto one whole spine suffix. The newer action is the outer one. *)
let tag_forest policy outer forest =
  if is_identity policy outer then forest
  else { forest with forest_action = compose policy outer forest.forest_action }

(* Materializes one tree's pending action into its own priority and into its child forest. *)
let expose policy tree =
  if is_identity policy tree.tree_action then tree
  else
    {
      rank = tree.rank;
      payload = tree.payload;
      raw_priority = apply policy tree.tree_action tree.raw_priority;
      raw_children = Option.map (tag_forest policy tree.tree_action) tree.raw_children;
      tree_action = policy.action_identity;
    }

(* An identity-tagged spine cell, so an attached tree keeps only its own action history. *)
let cons policy head tail =
  { raw_head = head; raw_tail = tail; forest_action = policy.action_identity }

let forest_head policy cell = tag_tree policy cell.forest_action cell.raw_head
let forest_tail policy cell = Option.map (tag_forest policy cell.forest_action) cell.raw_tail
let logical_priority policy tree = apply policy tree.tree_action tree.raw_priority

let less_or_equal policy left right =
  order_compare policy (logical_priority policy left) (logical_priority policy right) <= 0

(* [winner] must already be exposed, so its raw fields are its logical ones. *)
let ranked_tree policy rank winner children =
  {
    rank;
    payload = winner.payload;
    raw_priority = winner.raw_priority;
    raw_children = children;
    tree_action = policy.action_identity;
  }

(* ------------------------------------------------------------------------------------------- *)
(* Skew-binomial kernel over logical, tag-composed views                                        *)
(* ------------------------------------------------------------------------------------------- *)

let skew_link policy zero first second =
  if less_or_equal policy first zero && less_or_equal policy first second then (
    let winner = expose policy first in
    ranked_tree policy (first.rank + 1) winner
      (Some (cons policy zero (Some (cons policy second winner.raw_children)))))
  else if less_or_equal policy second zero && less_or_equal policy second first then (
    let winner = expose policy second in
    ranked_tree policy (second.rank + 1) winner
      (Some (cons policy zero (Some (cons policy first winner.raw_children)))))
  else
    let winner = expose policy zero in
    ranked_tree policy (first.rank + 1) winner
      (Some (cons policy first (Some (cons policy second winner.raw_children))))

(* Ranks are tag-invariant, so the raw tail's rank decides the link before any tagged tail cell is
   materialized. Only the linking branch pays for the tagged tail. *)
let skew_insert policy tree forest =
  match forest with
  | None -> cons policy tree forest
  | Some cell -> (
      match cell.raw_tail with
      | Some raw_tail when cell.raw_head.rank = raw_tail.raw_head.rank ->
          let tail = tag_forest policy cell.forest_action raw_tail in
          cons policy
            (skew_link policy tree (forest_head policy cell) (forest_head policy tail))
            (forest_tail policy tail)
      | _ -> cons policy tree forest)

let link policy first second =
  let first_wins = less_or_equal policy first second in
  let winner = expose policy (if first_wins then first else second) in
  let loser = if first_wins then second else first in
  ranked_tree policy (winner.rank + 1) winner (Some (cons policy loser winner.raw_children))

(* Lifts a minimum-priority tree out of a forest. A tie keeps the earliest spine occurrence. *)
let rec get_minimum policy cell =
  let head = forest_head policy cell in
  match forest_tail policy cell with
  | None -> (head, None)
  | Some tail ->
      let minimum, remainder = get_minimum policy tail in
      if less_or_equal policy head minimum then (head, Some tail)
      else (minimum, Some (cons policy head remainder))

(* Decomposes the fused primitive-child encoding of a rank-[rank] tree into the rank-zero children
   skew-linking produced, the ranked children ordinary linking produced, and the embedded forest. *)
let rec split_forest policy rank zeros trees forest =
  if rank = 0 then (zeros, trees, forest)
  else
    match forest with
    | None -> corrupt "invalid skew-binomial forest invariant"
    | Some cell -> (
        let first = forest_head policy cell in
        match forest_tail policy cell with
        | None ->
            if rank = 1 then (zeros, Some (cons policy first trees), None)
            else corrupt "invalid skew-binomial forest invariant"
        | Some tail ->
            let second = forest_head policy tail in
            let rest = forest_tail policy tail in
            if rank = 1 then
              (* The rank-zero ambiguity is resolved in favour of the structural prefix. *)
              if second.rank = 0 then
                (Some (cons policy first zeros), Some (cons policy second trees), rest)
              else (zeros, Some (cons policy first trees), Some (cons policy second rest))
            else if first.rank = second.rank then
              (zeros, Some (cons policy first (Some (cons policy second trees))), rest)
            else if first.rank = 0 then
              split_forest policy (rank - 1)
                (Some (cons policy first zeros))
                (Some (cons policy second trees))
                rest
            else
              split_forest policy (rank - 1) zeros
                (Some (cons policy first trees))
                (Some (cons policy second rest)))

let rec insert_ranked policy tree forest =
  match forest with
  | None -> cons policy tree None
  | Some cell ->
      let head = forest_head policy cell in
      if tree.rank < head.rank then cons policy tree forest
      else insert_ranked policy (link policy tree head) (forest_tail policy cell)

let rec uniquify policy forest =
  match forest with
  | None -> None
  | Some cell ->
      let tail = uniquify policy (forest_tail policy cell) in
      Some (insert_ranked policy (forest_head policy cell) tail)

let rec union_unique policy left right =
  match (left, right) with
  | None, _ -> right
  | _, None -> left
  | Some left_cell, Some right_cell ->
      let left_head = forest_head policy left_cell in
      let right_head = forest_head policy right_cell in
      if left_head.rank < right_head.rank then
        Some (cons policy left_head (union_unique policy (forest_tail policy left_cell) right))
      else if left_head.rank > right_head.rank then
        Some (cons policy right_head (union_unique policy left (forest_tail policy right_cell)))
      else
        Some
          (insert_ranked policy
             (link policy left_head right_head)
             (union_unique policy (forest_tail policy left_cell) (forest_tail policy right_cell)))

let skew_meld policy left right = union_unique policy (uniquify policy left) (uniquify policy right)

(* The logical trees of a forest, latest spine position first. *)
let reverse_spine policy forest =
  let rec loop collected forest =
    match forest with
    | None -> collected
    | Some cell -> loop (forest_head policy cell :: collected) (forest_tail policy cell)
  in
  loop [] forest

(* ------------------------------------------------------------------------------------------- *)
(* Heap surface                                                                                 *)
(* ------------------------------------------------------------------------------------------- *)

let empty policy = { root = None; size = 0; heap_policy = policy }
let new_version heap root size = { heap with root; size }
let policy heap = heap.heap_policy
let comparator heap = heap.heap_policy.priority_order
let count heap = heap.size
let is_empty heap = Option.is_none heap.root

let entry_of heap tree =
  { element = tree.payload; priority = logical_priority heap.heap_policy tree }

let minimum heap = Option.map (entry_of heap) heap.root

let insert element priority heap =
  let policy = heap.heap_policy in
  let singleton =
    {
      rank = 0;
      payload = element;
      raw_priority = priority;
      raw_children = None;
      tree_action = policy.action_identity;
    }
  in
  match heap.root with
  | None -> new_version heap (Some singleton) 1
  | Some root ->
      let updated =
        (* A tie favours the new entry, which then keeps the old root as an untouched child. *)
        if less_or_equal policy singleton root then
          {
            rank = 0;
            payload = element;
            raw_priority = priority;
            raw_children = Some (cons policy root None);
            tree_action = policy.action_identity;
          }
        else
          (* Exposing the old root first is what stops its pending action from leaking onto a
             child that did not exist when that action was applied. *)
          let exposed = expose policy root in
          {
            rank = 0;
            payload = exposed.payload;
            raw_priority = exposed.raw_priority;
            raw_children = Some (skew_insert policy singleton exposed.raw_children);
            tree_action = policy.action_identity;
          }
      in
      new_version heap (Some updated) (heap.size + 1)

let transform_all action heap =
  match heap.root with
  | None -> heap
  | Some root ->
      if is_identity heap.heap_policy action then heap
      else new_version heap (Some (tag_tree heap.heap_policy action root)) heap.size

let meld left right =
  if not (String.equal left.heap_policy.policy_id right.heap_policy.policy_id) then
    Error "action-heap melding requires the same action-policy identity"
  else
    let policy = left.heap_policy in
    match (left.root, right.root) with
    | None, _ -> Ok { right with heap_policy = policy }
    | _, None -> Ok left
    | Some left_root, Some right_root ->
        let left_wins = less_or_equal policy left_root right_root in
        let winner = if left_wins then left_root else right_root in
        let loser = if left_wins then right_root else left_root in
        (* Same rule as insertion: the winner is materialized before the loser is attached, and the
           loser rides in on an identity-tagged spine cell keeping its own history. *)
        let exposed = expose policy winner in
        let root =
          {
            rank = 0;
            payload = exposed.payload;
            raw_priority = exposed.raw_priority;
            raw_children = Some (skew_insert policy loser exposed.raw_children);
            tree_action = policy.action_identity;
          }
        in
        Ok (new_version left (Some root) (left.size + right.size))

let delete_minimum heap =
  match heap.root with
  | None -> None
  | Some root_link -> (
      let policy = heap.heap_policy in
      let root = expose policy root_link in
      match root.raw_children with
      | None -> Some (new_version heap None 0)
      | Some children ->
          let tagged_minimum, remainder = get_minimum policy children in
          let selected = expose policy tagged_minimum in
          let zeros, trees, embedded =
            split_forest policy selected.rank None None selected.raw_children
          in
          let merged = skew_meld policy (skew_meld policy trees remainder) embedded in
          (* The rank-zero children go back one at a time, in first-encountered order. *)
          let merged =
            List.fold_left
              (fun merged zero -> Some (skew_insert policy zero merged))
              merged (reverse_spine policy zeros)
          in
          Some
            (new_version heap
               (Some
                  {
                    rank = 0;
                    payload = selected.payload;
                    raw_priority = selected.raw_priority;
                    raw_children = merged;
                    tree_action = policy.action_identity;
                  })
               (heap.size - 1)))

let minimum_view heap =
  match (minimum heap, delete_minimum heap) with
  | Some entry, Some remainder -> Some (entry, remainder)
  | _ -> None

let to_list heap =
  let policy = heap.heap_policy in
  let rec push pending forest =
    match forest with
    | None -> pending
    | Some cell -> push (forest_head policy cell :: pending) (forest_tail policy cell)
  in
  let rec loop collected pending =
    match pending with
    | [] -> List.rev collected
    | tagged :: rest ->
        let tree = expose policy tagged in
        loop
          ({ element = tree.payload; priority = tree.raw_priority } :: collected)
          (push rest tree.raw_children)
  in
  loop [] (match heap.root with None -> [] | Some root -> [ root ])

let shares_root_with left right =
  match (left.root, right.root) with
  | None, None -> true
  | Some left_root, Some right_root -> left_root == right_root
  | _ -> false

let shares_root_children_with left right =
  match (left.root, right.root) with
  | None, None -> true
  | Some left_root, Some right_root -> (
      match (left_root.raw_children, right_root.raw_children) with
      | None, None -> true
      | Some left_children, Some right_children -> left_children == right_children
      | _ -> false)
  | _ -> false

(* ------------------------------------------------------------------------------------------- *)
(* Audit                                                                                        *)
(* ------------------------------------------------------------------------------------------- *)

(* Nondecreasing ranks, with at most the first two equal. Ranks are tag-invariant, so the raw heads
   answer this without materializing a single tagged view. *)
let validate_skew_forest policy forest =
  let rec loop length previous duplicate forest =
    match forest with
    | None -> length
    | Some cell ->
        let rank = cell.raw_head.rank in
        if rank < 0 then invariant "an action-heap forest contains a negative rank";
        let duplicate =
          if rank < previous then invariant "action-heap forest ranks are not nondecreasing"
          else if rank = previous then
            if length <> 1 || duplicate then
              invariant "only the first two action-heap forest ranks may be equal"
            else true
          else duplicate
        in
        loop (length + 1) rank duplicate (forest_tail policy cell)
  in
  loop 0 (-1) false forest

(* Walks the fused primitive-child encoding of one exposed tree down to its embedded forest. *)
let validate_fused_children policy tree =
  let rec loop rank forest =
    if rank <= 0 then forest
    else
      match forest with
      | None -> invariant "a ranked action-heap tree is missing children"
      | Some cell -> (
          let first = forest_head policy cell in
          let tail = forest_tail policy cell in
          if rank = 1 then (
            if first.rank <> 0 then
              invariant "a rank-one action-heap tree must begin with a rank-zero child";
            match tail with
            | None -> None
            | Some tail_cell ->
                if (forest_head policy tail_cell).rank = 0 then forest_tail policy tail_cell
                else tail)
          else
            match tail with
            | None -> invariant "a ranked action-heap tree has incomplete children"
            | Some tail_cell ->
                let second = forest_head policy tail_cell in
                if first.rank = second.rank then (
                  if first.rank <> rank - 1 then
                    invariant "a skew-linked action-heap tree has invalid child ranks";
                  forest_tail policy tail_cell)
                else if first.rank = 0 then (
                  if second.rank <> rank - 1 then
                    invariant "a skew-linked action-heap tree has an invalid ranked child";
                  loop (rank - 1) (forest_tail policy tail_cell))
                else (
                  if first.rank <> rank - 1 then
                    invariant "a linked action-heap tree has an invalid ranked child";
                  loop (rank - 1) tail))
  in
  loop tree.rank tree.raw_children

let validate heap =
  let policy = heap.heap_policy in
  try
    match heap.root with
    | None ->
        if heap.size <> 0 then Error "an empty action heap has a nonzero count"
        else
          Ok
            {
              count = 0;
              root_forest_length = 0;
              maximum_rank = 0;
              maximum_depth = 0;
              tagged_component_count = 0;
            }
    | Some root_link ->
        if root_link.rank <> 0 then invariant "the global action-heap root must have rank zero";
        let tagged = ref 0 in
        let logical = ref 0 in
        let largest_rank = ref 0 in
        let deepest = ref 0 in
        let normalized = expose policy root_link in
        let root_forest_length = validate_skew_forest policy normalized.raw_children in
        let rec walk pending =
          match pending with
          | [] -> ()
          | (candidate, depth) :: rest ->
              if not (is_identity policy candidate.tree_action) then incr tagged;
              let tree = expose policy candidate in
              if tree.rank < 0 then invariant "an action-heap tree has a negative rank";
              incr logical;
              if tree.rank > !largest_rank then largest_rank := tree.rank;
              if depth > !deepest then deepest := depth;
              ignore (validate_skew_forest policy (validate_fused_children policy tree) : int);
              let rec push pending forest =
                match forest with
                | None -> pending
                | Some cell ->
                    if not (is_identity policy cell.forest_action) then incr tagged;
                    let child = forest_head policy cell in
                    if not (less_or_equal policy tree child) then
                      invariant "an action-heap child outranks its parent";
                    push ((child, depth + 1) :: pending) (forest_tail policy cell)
              in
              walk (push rest tree.raw_children)
        in
        walk [ (root_link, 1) ];
        if !logical <> heap.size then Error "the action heap's logical count is invalid"
        else
          Ok
            {
              count = heap.size;
              root_forest_length;
              maximum_rank = !largest_rank;
              maximum_depth = !deepest;
              tagged_component_count = !tagged;
            }
  with Invariant message -> Error message

let of_entries policy entries =
  List.fold_left (fun heap (element, priority) -> insert element priority heap) (empty policy)
    entries
