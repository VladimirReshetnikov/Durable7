(** Bilateral ancestral deque: a persistent deque held as two oppositely oriented intervals of an
    append-only ancestry arena.

    A handle stores two interval descriptors over arena node handles, so it is immutable and
    constant-sized even though the arena underneath it grows. Every operation is the interval
    arithmetic of the C# baseline transcribed directly; the only structure this module owns is the
    pair of descriptors. *)

type segment = { anchor : int; base : int; tail : int; size : int }
(** One oriented ancestry interval running from the exclusive [anchor] down to the inclusive [tail].
    [base] is redundant but load-bearing: it caches the first physical node after [anchor], which
    keeps both public endpoint reads query-free even when the opposite arm is empty. For the left
    arm the logical first value sits at [tail] and the logical last at [base]; for the right arm
    those roles are exchanged. *)

type 'value t = {
  arena : 'value Incremental_ancestor.t;
  left : segment;
  right : segment;
  total : int;
}

type statistics = { count : int; left_count : int; right_count : int }

let ( let* ) = Result.bind

(** The empty interval anchored at one node. *)
let empty_at node = { anchor = node; base = node; tail = node; size = 0 }

(** Settles an arena read that cannot fail for a handle this module produced: every cached endpoint
    is a node this arena published, only labelled nodes are dereferenced, and every depth handed to
    the arena lies on the tail's own ancestry path. A failure here would mean the handle was
    corrupted, which the abstract type prevents, so it is reported as a defect rather than threaded
    through every result. *)
let settled what = function
  | Ok value -> value
  | Error message -> invalid_arg ("bilateral ancestral deque: " ^ what ^ ": " ^ message)

let value_of arena node = settled "value" (Incremental_ancestor.value_at arena node)
let depth_of arena node = settled "depth" (Incremental_ancestor.depth_of arena node)
let parent_of arena node = settled "parent" (Incremental_ancestor.parent_of arena node)

let ancestor_of arena node ~depth =
  settled "ancestor" (Incremental_ancestor.ancestor_at_depth arena node ~depth)

let create () =
  let empty = empty_at Incremental_ancestor.bottom in
  { arena = Incremental_ancestor.create (); left = empty; right = empty; total = 0 }

let of_arena arena =
  let* depth = Incremental_ancestor.depth_of arena Incremental_ancestor.bottom in
  if depth <> -1 then Error "the arena bottom node must have depth -1"
  else
    let empty = empty_at Incremental_ancestor.bottom in
    Ok { arena; left = empty; right = empty; total = 0 }

let arena deque = deque.arena
let count deque = deque.total
let is_empty deque = deque.total = 0

let shares_storage_with left right =
  left.arena == right.arena && left.left = right.left && left.right = right.right
  && left.total = right.total

(** An empty handle over the same arena, anchored at the arena's bottom node. *)
let empty_handle deque =
  let empty = empty_at Incremental_ancestor.bottom in
  { deque with left = empty; right = empty; total = 0 }

let first deque =
  if deque.total = 0 then None
  else
    let node = if deque.left.size <> 0 then deque.left.tail else deque.right.base in
    Some (value_of deque.arena node)

let last deque =
  if deque.total = 0 then None
  else
    let node = if deque.right.size <> 0 then deque.right.tail else deque.left.base in
    Some (value_of deque.arena node)

let nth index deque =
  if index < 0 || index >= deque.total then None
  else
    let node =
      if index < deque.left.size then
        if index = 0 then deque.left.tail
        else if index = deque.left.size - 1 then deque.left.base
        else
          ancestor_of deque.arena deque.left.tail
            ~depth:(depth_of deque.arena deque.left.tail - index)
      else
        let right_index = index - deque.left.size in
        if right_index = 0 then deque.right.base
        else if right_index = deque.right.size - 1 then deque.right.tail
        else
          ancestor_of deque.arena deque.right.tail
            ~depth:(depth_of deque.arena deque.right.anchor + right_index + 1)
    in
    Some (value_of deque.arena node)

(** Publishes one leaf below an interval's tail and extends the interval by one. *)
let add_to_tail arena segment value =
  let* tail = Incremental_ancestor.add_leaf arena ~parent:segment.tail value in
  let base = if segment.size = 0 then tail else segment.base in
  Ok { anchor = segment.anchor; base; tail; size = segment.size + 1 }

let add_first value deque =
  if deque.total = max_int then Error "the deque count cannot grow further"
  else
    let* left = add_to_tail deque.arena deque.left value in
    Ok { deque with left; total = deque.total + 1 }

let add_last value deque =
  if deque.total = max_int then Error "the deque count cannot grow further"
  else
    let* right = add_to_tail deque.arena deque.right value in
    Ok { deque with right; total = deque.total + 1 }

(** Drops an interval's newest physical label by moving its tail to the parent. No queries. *)
let remove_tail arena segment =
  let tail = parent_of arena segment.tail in
  if segment.size = 1 then empty_at tail
  else { anchor = segment.anchor; base = segment.base; tail; size = segment.size - 1 }

(** Drops an interval's oldest physical label by advancing its anchor to the cached base. Costs at
    most one level-ancestor query, which selects the next cached base; an interval of two values
    already holds that node as its tail. *)
let remove_base arena segment =
  if segment.size = 1 then empty_at segment.tail
  else
    let base =
      if segment.size = 2 then segment.tail
      else
        ancestor_of arena segment.tail ~depth:(depth_of arena segment.tail - segment.size + 2)
    in
    { anchor = segment.base; base; tail = segment.tail; size = segment.size - 1 }

let remove_first deque =
  if deque.total = 0 then None
  else if deque.total = 1 then Some (empty_handle deque)
  else if deque.left.size <> 0 then
    Some { deque with left = remove_tail deque.arena deque.left; total = deque.total - 1 }
  else Some { deque with right = remove_base deque.arena deque.right; total = deque.total - 1 }

let remove_last deque =
  if deque.total = 0 then None
  else if deque.total = 1 then Some (empty_handle deque)
  else if deque.right.size <> 0 then
    Some { deque with right = remove_tail deque.arena deque.right; total = deque.total - 1 }
  else Some { deque with left = remove_base deque.arena deque.left; total = deque.total - 1 }

let pop_first deque =
  match (first deque, remove_first deque) with
  | Some value, Some rest -> Some (value, rest)
  | _ -> None

let pop_last deque =
  match (last deque, remove_last deque) with
  | Some value, Some rest -> Some (value, rest)
  | _ -> None

(** Selects the sub-interval starting [start] values in and holding [size] values of a reversed left
    arm, where [start] counts down from the tail. At most two queries, and fewer whenever a cached
    endpoint already answers. *)
let slice_left arena segment start size =
  if size = 0 then empty_at Incremental_ancestor.bottom
  else if start = 0 && size = segment.size then segment
  else begin
    let tail_depth = depth_of arena segment.tail in
    let base_depth = tail_depth - segment.size + 1 in
    let sliced_tail_depth = tail_depth - start in
    let sliced_base_depth = sliced_tail_depth - size + 1 in
    let tail =
      if sliced_tail_depth = tail_depth then segment.tail
      else ancestor_of arena segment.tail ~depth:sliced_tail_depth
    in
    let base =
      if sliced_base_depth = sliced_tail_depth then tail
      else if sliced_base_depth = base_depth then segment.base
      else ancestor_of arena segment.tail ~depth:sliced_base_depth
    in
    let anchor = if base = segment.base then segment.anchor else parent_of arena base in
    { anchor; base; tail; size }
  end

(** Selects the sub-interval starting [start] values in and holding [size] values of a forward right
    arm. At most two queries, and fewer whenever a cached endpoint already answers. *)
let slice_right arena segment start size =
  if size = 0 then empty_at Incremental_ancestor.bottom
  else if start = 0 && size = segment.size then segment
  else begin
    let anchor_depth = depth_of arena segment.anchor in
    let sliced_base_depth = anchor_depth + start + 1 in
    let sliced_tail_depth = sliced_base_depth + size - 1 in
    let finish = start + size in
    let base =
      if start = 0 then segment.base
      else ancestor_of arena segment.tail ~depth:sliced_base_depth
    in
    let tail =
      if sliced_tail_depth = sliced_base_depth then base
      else if finish = segment.size then segment.tail
      else ancestor_of arena segment.tail ~depth:sliced_tail_depth
    in
    let anchor = if start = 0 then segment.anchor else parent_of arena base in
    { anchor; base; tail; size }
  end

let slice ~index ~count deque =
  if index < 0 then Error "the slice index is negative"
  else if count < 0 then Error "the slice count is negative"
  else if index > deque.total then Error "the slice index is outside the deque"
  else if index > deque.total - count then Error "the slice count is outside the deque"
  else if index = 0 && count = deque.total then Ok deque
  else if count = 0 then Ok (empty_handle deque)
  else begin
    let finish = index + count in
    let left_start = min index deque.left.size in
    let left_end = min finish deque.left.size in
    let right_start = max 0 (index - deque.left.size) in
    let right_end = max 0 (finish - deque.left.size) in
    let left = slice_left deque.arena deque.left left_start (left_end - left_start) in
    let right = slice_right deque.arena deque.right right_start (right_end - right_start) in
    Ok { deque with left; right; total = count }
  end

let take amount deque =
  if amount < 0 then Error "the prefix length is negative"
  else if amount > deque.total then Error "the prefix length is outside the deque"
  else if amount = deque.total then Ok deque
  else slice ~index:0 ~count:amount deque

let drop amount deque =
  if amount < 0 then Error "the omitted count is negative"
  else if amount > deque.total then Error "the omitted count is outside the deque"
  else if amount = 0 then Ok deque
  else slice ~index:amount ~count:(deque.total - amount) deque

let split_at index deque =
  if index < 0 then Error "the split boundary is negative"
  else if index > deque.total then Error "the split boundary is outside the deque"
  else
    let* prefix = take index deque in
    let* suffix = drop index deque in
    Ok (prefix, suffix)

let reverse deque =
  if deque.total <= 1 then deque else { deque with left = deque.right; right = deque.left }

let clear deque = if deque.total = 0 then deque else empty_handle deque

(** The values on the [length] nodes at and above [node], deepest ancestor first. The right arm
    reads in that order; the left arm reads in the reverse of it. *)
let rec ancestor_values arena node length collected =
  if length = 0 then collected
  else
    let collected = value_of arena node :: collected in
    if length = 1 then collected
    else ancestor_values arena (parent_of arena node) (length - 1) collected

let to_list deque =
  List.rev_append
    (ancestor_values deque.arena deque.left.tail deque.left.size [])
    (ancestor_values deque.arena deque.right.tail deque.right.size [])

let to_seq deque =
  let right_seq () =
    List.to_seq (ancestor_values deque.arena deque.right.tail deque.right.size []) ()
  in
  let rec left_seq node remaining () =
    if remaining = 0 then right_seq ()
    else
      let value = value_of deque.arena node in
      let next = if remaining = 1 then node else parent_of deque.arena node in
      Seq.Cons (value, left_seq next (remaining - 1))
  in
  left_seq deque.left.tail deque.left.size

let iter action deque =
  let node = ref deque.left.tail in
  for index = 0 to deque.left.size - 1 do
    action (value_of deque.arena !node);
    if index + 1 <> deque.left.size then node := parent_of deque.arena !node
  done;
  List.iter action (ancestor_values deque.arena deque.right.tail deque.right.size [])

let fold_left combine initial deque =
  let state = ref initial in
  let node = ref deque.left.tail in
  for index = 0 to deque.left.size - 1 do
    state := combine !state (value_of deque.arena !node);
    if index + 1 <> deque.left.size then node := parent_of deque.arena !node
  done;
  List.fold_left combine !state (ancestor_values deque.arena deque.right.tail deque.right.size [])

let of_list values =
  List.fold_left (fun deque value -> Result.bind deque (add_last value)) (Ok (create ())) values

(** Checks one interval's endpoint identities, cached base, and count/depth agreement. An empty
    interval is settled from cached fields alone; a nonempty one spends exactly two level-ancestor
    queries to place the anchor and the cached base on the tail's ancestry path. *)
let validate_segment arena segment =
  if segment.size = 0 then
    if segment.anchor <> segment.base || segment.base <> segment.tail then
      Error "an empty interval does not have one shared endpoint"
    else Ok ()
  else
    let* anchor_depth = Incremental_ancestor.depth_of arena segment.anchor in
    let* tail_depth = Incremental_ancestor.depth_of arena segment.tail in
    if tail_depth - anchor_depth <> segment.size then
      Error "an interval count disagrees with its endpoint depths"
    else
      let* base_parent = Incremental_ancestor.parent_of arena segment.base in
      if base_parent <> segment.anchor then
        Error "the cached base is not the first node after the anchor"
      else
        let* found_anchor =
          Incremental_ancestor.ancestor_at_depth arena segment.tail ~depth:anchor_depth
        in
        if found_anchor <> segment.anchor then
          Error "the interval anchor is not an ancestor of its tail"
        else
          let* found_base =
            Incremental_ancestor.ancestor_at_depth arena segment.tail ~depth:(anchor_depth + 1)
          in
          if found_base <> segment.base then
            Error "the cached base is not on the tail's ancestry path"
          else Ok ()

let validate deque =
  if deque.total <> deque.left.size + deque.right.size then
    Error "the total count disagrees with the interval counts"
  else
    let* () = validate_segment deque.arena deque.left in
    let* () = validate_segment deque.arena deque.right in
    Ok { count = deque.total; left_count = deque.left.size; right_count = deque.right.size }
