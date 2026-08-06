(** Implementation of the persistent relaxed radix-balanced vector with 32-way branching.

    A genuine RRB tree, not a facade: leaves pack up to 32 elements in flat arrays, branches pack
    up to 32 children, and a branch whose children fill the 5-bit radix layout exactly is
    {e regular} and stores no size table — addressing it is pure shift-and-mask arithmetic. Only an
    irregular branch pays for cumulative sizes and answers by binary search, which is what makes
    O(log n) worst-case splits and concatenations affordable without a global occupancy rule.
    Concatenation rebalances only the seam between its operands, endpoint pushes are singleton
    concatenations, and everything is eager: indexing and point writes are O(log32 n) worst case,
    splits and concatenations O(log n) worst case, with no deferred work anywhere. *)

let branch_factor = 32
let radix_bits = 5

type 'element node =
  | Leaf of 'element array
  | Branch of {
      children : 'element node array;
      height : int;
      count : int;
      (* Cumulative child sizes, present exactly when the layout is irregular. A regular branch
         addresses arithmetically; storing a table there would be wasted space, and omitting it on
         an irregular branch would make radix arithmetic wrong. *)
      sizes : int array option;
    }

type 'element t = Empty | Root of 'element node
type 'element cursor = { cursor_snapshot : 'element t; cursor_position : int }
type statistics = { count : int; estimated_leaves : int; depth : int }

let node_count = function Leaf items -> Array.length items | Branch branch -> branch.count
let node_height = function Leaf _ -> 0 | Branch branch -> branch.height

(* The capacity of one child of a branch at the given height: 32^height, a power of two. *)
let child_capacity height = 1 lsl (height * radix_bits)

let has_regular_layout children height =
  let capacity = child_capacity height in
  let last = Array.length children - 1 in
  let regular = ref (node_count children.(last) <= capacity) in
  for index = 0 to last - 1 do
    if node_count children.(index) <> capacity then regular := false
  done;
  !regular

let build_sizes children =
  let sizes = Array.make (Array.length children) 0 in
  let total = ref 0 in
  Array.iteri
    (fun index child ->
      total := !total + node_count child;
      sizes.(index) <- !total)
    children;
  sizes

(* Builds a branch, deriving its height and count and deciding whether it needs a size table. *)
let make_branch children =
  let width = Array.length children in
  assert (width >= 1 && width <= branch_factor);
  let height = node_height children.(0) + 1 in
  assert (Array.for_all (fun child -> node_height child = height - 1) children);
  let count = Array.fold_left (fun total child -> total + node_count child) 0 children in
  let sizes = if has_regular_layout children height then None else Some (build_sizes children) in
  Branch { children; height; count; sizes }

(* The child covering the index, and how many elements precede that child. A regular branch
   computes both by radix arithmetic; an irregular one binary-searches its size table. *)
let find_child children height sizes index =
  match sizes with
  | None ->
      let shift = height * radix_bits in
      let child_index = index lsr shift in
      (child_index, child_index lsl shift)
  | Some sizes ->
      let low = ref 0 in
      let high = ref (Array.length children - 1) in
      while !low < !high do
        let middle = (!low + !high) / 2 in
        if index < sizes.(middle) then high := middle else low := middle + 1
      done;
      (!low, if !low = 0 then 0 else sizes.(!low - 1))

let rec get_node node index =
  match node with
  | Leaf items -> items.(index)
  | Branch branch ->
      let child_index, before = find_child branch.children branch.height branch.sizes index in
      get_node branch.children.(child_index) (index - before)

let rec set_node node index value =
  match node with
  | Leaf items ->
      let items = Array.copy items in
      items.(index) <- value;
      Leaf items
  | Branch branch ->
      let child_index, before = find_child branch.children branch.height branch.sizes index in
      let children = Array.copy branch.children in
      children.(child_index) <- set_node children.(child_index) (index - before) value;
      make_branch children

(* Wraps same-height siblings into one branch, or two when they overflow the branch factor. *)
let partition children =
  if Array.length children <= branch_factor then [| make_branch children |]
  else
    let split = Array.length children / 2 in
    [|
      make_branch (Array.sub children 0 split);
      make_branch (Array.sub children split (Array.length children - split));
    |]

let splice_children before boundary after =
  Array.concat [ before; boundary; after ]

(* Concatenates two same-height nodes, rebalancing only the seam between them and returning one or
   two nodes for the caller to wrap. Two full leaves are kept as they are; anything smaller merges
   or redistributes so the seam never accumulates fragmentation. *)
let rec concat_same_height left right =
  match (left, right) with
  | Leaf left_items, Leaf right_items ->
      let left_length = Array.length left_items in
      let right_length = Array.length right_items in
      if left_length = branch_factor && right_length = branch_factor then [| left; right |]
      else
        let combined = Array.append left_items right_items in
        if Array.length combined <= branch_factor then [| Leaf combined |]
        else
          let split = Array.length combined / 2 in
          [|
            Leaf (Array.sub combined 0 split);
            Leaf (Array.sub combined split (Array.length combined - split));
          |]
  | Branch left_branch, Branch right_branch ->
      let left_children = left_branch.children in
      let right_children = right_branch.children in
      let boundary =
        concat_same_height
          left_children.(Array.length left_children - 1)
          right_children.(0)
      in
      partition
        (splice_children
           (Array.sub left_children 0 (Array.length left_children - 1))
           boundary
           (Array.sub right_children 1 (Array.length right_children - 1)))
  | Leaf _, Branch _ | Branch _, Leaf _ -> assert false

(* Concatenates two nodes of any heights by descending the taller operand's seam edge. *)
let rec concat_nodes left right =
  if node_height left = node_height right then concat_same_height left right
  else if node_height left > node_height right then
    match left with
    | Branch branch ->
        let children = branch.children in
        let boundary = concat_nodes children.(Array.length children - 1) right in
        partition (splice_children (Array.sub children 0 (Array.length children - 1)) boundary [||])
    | Leaf _ -> assert false
  else
    match right with
    | Branch branch ->
        let children = branch.children in
        let boundary = concat_nodes left children.(0) in
        partition (splice_children [||] boundary (Array.sub children 1 (Array.length children - 1)))
    | Leaf _ -> assert false

(* Collapses single-child chains at the root; interior single-child branches left by splitting are
   legal and get merged away by later seam concatenations. *)
let rec normalize_root node =
  match node with
  | Branch branch when Array.length branch.children = 1 -> normalize_root branch.children.(0)
  | Leaf _ | Branch _ -> node

let wrap = function None -> Empty | Some node -> Root (normalize_root node)

let build_same_height children = if Array.length children = 0 then None else Some (make_branch children)

let rec split_node node index =
  if index = 0 then (None, Some node)
  else if index = node_count node then (Some node, None)
  else
    match node with
    | Leaf items ->
        ( Some (Leaf (Array.sub items 0 index)),
          Some (Leaf (Array.sub items index (Array.length items - index))) )
    | Branch branch ->
        let child_index, before = find_child branch.children branch.height branch.sizes index in
        let child_left, child_right = split_node branch.children.(child_index) (index - before) in
        let left =
          splice_children
            (Array.sub branch.children 0 child_index)
            (match child_left with None -> [||] | Some child -> [| child |])
            [||]
        in
        let right =
          splice_children [||]
            (match child_right with None -> [||] | Some child -> [| child |])
            (Array.sub branch.children (child_index + 1)
               (Array.length branch.children - child_index - 1))
        in
        (build_same_height left, build_same_height right)

(* --- public operations ----------------------------------------------------------------------- *)

let empty = Empty
let singleton value = Root (Leaf [| value |])
let length = function Empty -> 0 | Root node -> node_count node
let is_empty = function Empty -> true | Root _ -> false

(* Groups same-height nodes into branches level by level: an eager O(n) bulk build whose branches
   are all regular, so a freshly built vector addresses arithmetically everywhere. *)
let build_level nodes =
  let rec grow nodes =
    let width = Array.length nodes in
    if width = 1 then nodes.(0)
    else
      let groups = (width + branch_factor - 1) / branch_factor in
      grow
        (Array.init groups (fun group ->
             let start = group * branch_factor in
             make_branch (Array.sub nodes start (min branch_factor (width - start)))))
  in
  grow nodes

let of_list values =
  match values with
  | [] -> Empty
  | _ ->
      let items = Array.of_list values in
      let total = Array.length items in
      let leaves = (total + branch_factor - 1) / branch_factor in
      Root
        (build_level
           (Array.init leaves (fun leaf ->
                let start = leaf * branch_factor in
                Leaf (Array.sub items start (min branch_factor (total - start))))))

let nth index vector =
  match vector with
  | Empty -> None
  | Root node -> if index < 0 || index >= node_count node then None else Some (get_node node index)

let set index value vector =
  match vector with
  | Root node when index >= 0 && index < node_count node -> Ok (Root (set_node node index value))
  | Empty | Root _ -> Error "sequence update index is out of range"

let concat left right =
  match (left, right) with
  | Empty, other | other, Empty -> other
  | Root left_node, Root right_node -> (
      match concat_nodes left_node right_node with
      | [| single |] -> Root (normalize_root single)
      | pair -> Root (make_branch pair))

let append value vector = concat vector (singleton value)
let prepend value vector = concat (singleton value) vector

let split_at index vector =
  match vector with
  | Empty -> (Empty, Empty)
  | Root node ->
      if index <= 0 then (Empty, vector)
      else if index >= node_count node then (vector, Empty)
      else
        let left, right = split_node node index in
        (wrap left, wrap right)

let take count vector = fst (split_at count vector)
let drop count vector = snd (split_at count vector)

let insert index value vector =
  if index < 0 || index > length vector then Error "sequence insertion index is out of range"
  else
    let left, right = split_at index vector in
    Ok (concat (concat left (singleton value)) right)

let remove index vector =
  if index < 0 || index >= length vector then Error "sequence removal index is out of range"
  else
    let left, rest = split_at index vector in
    let removed, right = split_at 1 rest in
    match nth 0 removed with
    | Some value -> Ok (value, concat left right)
    | None -> assert false

let pop vector =
  match remove (length vector - 1) vector with
  | Ok (value, rest) -> Some (rest, value)
  | Error _ -> None

let slice ~start ~length:count vector =
  if start < 0 || count < 0 || start > length vector - count then
    Error "deque slice is out of range"
  else Ok (take count (drop start vector))

let to_list vector =
  let rec node_fold node acc =
    match node with
    | Leaf items -> Array.fold_right List.cons items acc
    | Branch branch -> Array.fold_right node_fold branch.children acc
  in
  match vector with Empty -> [] | Root node -> node_fold node []

(* A real structural audit rather than an estimate: it recounts every leaf, checks node widths,
   sibling heights, cached counts, and that a size table is present exactly when the layout is
   irregular (and then agrees with the children). A violation raises [Invalid_argument], which is
   unreachable through this module's own operations. *)
let statistics vector =
  match vector with
  | Empty -> { count = 0; estimated_leaves = 0; depth = 0 }
  | Root root ->
      let leaves = ref 0 in
      let fail message = invalid_arg ("RRB structural audit failed: " ^ message) in
      let rec visit ~is_root node =
        match node with
        | Leaf items ->
            let width = Array.length items in
            if width < 1 || width > branch_factor then fail "leaf size is outside 1..32";
            incr leaves;
            (width, 0)
        | Branch branch ->
            let width = Array.length branch.children in
            if width < 1 || width > branch_factor then fail "branch factor is outside 1..32";
            if is_root && width < 2 then fail "a root branch must hold at least two children";
            let total = ref 0 in
            let child_height = ref (-1) in
            Array.iter
              (fun child ->
                let child_count, height = visit ~is_root:false child in
                if !child_height = -1 then child_height := height
                else if height <> !child_height then fail "sibling heights differ";
                total := !total + child_count)
              branch.children;
            if !total <> branch.count then fail "a cached count disagrees with the children";
            if !child_height + 1 <> branch.height then fail "a cached height disagrees";
            let regular = has_regular_layout branch.children branch.height in
            (match branch.sizes with
            | None -> if not regular then fail "an irregular branch is missing its size table"
            | Some sizes ->
                if regular then fail "a regular branch carries a size table";
                if sizes <> build_sizes branch.children then fail "a size table is stale");
            (!total, branch.height)
      in
      let count, height = visit ~is_root:true root in
      { count; estimated_leaves = !leaves; depth = height + 1 }

(* --- cursors (index-based over one snapshot; edits go through the vector operations) ---------- *)

let cursor vector = { cursor_snapshot = vector; cursor_position = 0 }

let cursor_at position vector =
  if position < 0 || position > length vector then None
  else Some { cursor_snapshot = vector; cursor_position = position }

let cursor_position value = value.cursor_position
let cursor_length value = length value.cursor_snapshot
let cursor_is_at_start value = value.cursor_position = 0
let cursor_is_at_end value = value.cursor_position = cursor_length value
let cursor_peek_previous value = nth (value.cursor_position - 1) value.cursor_snapshot
let cursor_peek_next value = nth value.cursor_position value.cursor_snapshot
let cursor_move_previous value = cursor_at (value.cursor_position - 1) value.cursor_snapshot

let cursor_move_next value =
  if cursor_is_at_end value then None
  else cursor_at (value.cursor_position + 1) value.cursor_snapshot

let cursor_seek position value = cursor_at position value.cursor_snapshot

let cursor_insert element value =
  {
    cursor_snapshot = Result.get_ok (insert value.cursor_position element value.cursor_snapshot);
    cursor_position = value.cursor_position + 1;
  }

let cursor_insert_vector inserted value =
  if is_empty inserted then value
  else
    let left, right = split_at value.cursor_position value.cursor_snapshot in
    {
      cursor_snapshot = concat (concat left inserted) right;
      cursor_position = value.cursor_position + length inserted;
    }

let cursor_insert_many elements value = cursor_insert_vector (of_list elements) value

let cursor_delete_previous value =
  if cursor_is_at_start value then None
  else
    let _, snapshot = Result.get_ok (remove (value.cursor_position - 1) value.cursor_snapshot) in
    Some { cursor_snapshot = snapshot; cursor_position = value.cursor_position - 1 }

let cursor_delete_next value =
  if cursor_is_at_end value then None
  else
    let _, snapshot = Result.get_ok (remove value.cursor_position value.cursor_snapshot) in
    Some { cursor_snapshot = snapshot; cursor_position = value.cursor_position }

let cursor_replace_next element value =
  if cursor_is_at_end value then None
  else
    let snapshot = Result.get_ok (set value.cursor_position element value.cursor_snapshot) in
    Some { cursor_snapshot = snapshot; cursor_position = value.cursor_position }

let cursor_snapshot value = value.cursor_snapshot

module Builder = struct
  type 'element vector = 'element t

  (* Appends stage into a mutable tail buffer and become full leaves without touching the tree;
     indexed edits and freezes flush the staged suffix into the persistent prefix first. *)
  type 'element t = {
    mutable prefix : 'element vector;
    mutable staged_leaves : 'element node list; (* newest first *)
    mutable tail : 'element list; (* newest first *)
    mutable tail_length : int;
  }

  let create current = { prefix = current; staged_leaves = []; tail = []; tail_length = 0 }

  let staged_count builder =
    builder.tail_length + (branch_factor * List.length builder.staged_leaves)

  let length builder = length builder.prefix + staged_count builder

  let flush builder =
    if staged_count builder > 0 then begin
      let leaves =
        if builder.tail_length = 0 then builder.staged_leaves
        else Leaf (Array.of_list (List.rev builder.tail)) :: builder.staged_leaves
      in
      let staged = Root (build_level (Array.of_list (List.rev leaves))) in
      builder.prefix <- concat builder.prefix staged;
      builder.staged_leaves <- [];
      builder.tail <- [];
      builder.tail_length <- 0
    end

  let append value builder =
    builder.tail <- value :: builder.tail;
    builder.tail_length <- builder.tail_length + 1;
    if builder.tail_length = branch_factor then begin
      builder.staged_leaves <- Leaf (Array.of_list (List.rev builder.tail)) :: builder.staged_leaves;
      builder.tail <- [];
      builder.tail_length <- 0
    end

  let set index value builder =
    flush builder;
    Result.map (fun successor -> builder.prefix <- successor) (set index value builder.prefix)

  let remove index builder =
    flush builder;
    Result.map
      (fun (value, successor) ->
        builder.prefix <- successor;
        value)
      (remove index builder.prefix)

  let freeze builder =
    flush builder;
    builder.prefix
end
