(** Fully persistent queue slice denoting one interval of an append ancestry path.

    Every operation is boundary arithmetic plus at most one arena call, because all the retained
    structure lives in the shared append-only arena rather than in the value. The boundary
    arithmetic cannot wrap: the arena refuses to publish a node once the ancestry depth reaches
    [max_int], and every window this module derives stays inside the source window, so no derived
    depth exceeds the retained tail's own depth by more than one. *)

type 'element t = {
  arena : 'element Incremental_ancestor.t;
  tail : int;
  low_depth : int;
  count : int;
}

let of_arena arena = { arena; tail = Incremental_ancestor.bottom; low_depth = 0; count = 0 }
let empty () = of_arena (Incremental_ancestor.create ())
let length queue = queue.count
let is_empty queue = queue.count = 0

(** A published node's label. The [Error] arm is unreachable for any value this module can build:
    every node a queue retains was published by its own arena, and a queue only ever reads nodes at
    depth zero or more, which are exactly the labelled ones. *)
let label queue node = Result.to_option (Incremental_ancestor.value_at queue.arena node)

(** The ancestor of the retained tail at an absolute depth inside the window. Unreachable [Error]
    arms are reported as absence for the same reason as in {!label}. *)
let ancestor queue ~depth =
  Result.to_option (Incremental_ancestor.ancestor_at_depth queue.arena queue.tail ~depth)

let first queue =
  if queue.count = 0 then None
  else if queue.count = 1 then label queue queue.tail
  else Option.bind (ancestor queue ~depth:queue.low_depth) (label queue)

let last queue = if queue.count = 0 then None else label queue queue.tail

let nth index queue =
  if index < 0 || index >= queue.count then None
  else if index = queue.count - 1 then label queue queue.tail
  else Option.bind (ancestor queue ~depth:(queue.low_depth + index)) (label queue)

let add_last value queue =
  if queue.count = max_int then Error "the ancestral slice queue cannot hold another element"
  else
    match Incremental_ancestor.add_leaf queue.arena ~parent:queue.tail value with
    | Error message -> Error message
    | Ok tail -> Ok { queue with tail; count = queue.count + 1 }

let of_list values =
  List.fold_left (fun queue value -> Result.bind queue (add_last value)) (Ok (empty ())) values

let remove_first queue =
  if queue.count = 0 then None
  else Some { queue with low_depth = queue.low_depth + 1; count = queue.count - 1 }

let remove_last queue =
  if queue.count = 0 then None
  else
    match Incremental_ancestor.parent_of queue.arena queue.tail with
    | Error _ -> None
    | Ok parent -> Some { queue with tail = parent; count = queue.count - 1 }

let pop_front queue =
  match first queue with
  | None -> None
  | Some value -> Option.map (fun rest -> (value, rest)) (remove_first queue)

let pop_back queue =
  match last queue with
  | None -> None
  | Some value -> Option.map (fun rest -> (rest, value)) (remove_last queue)

let slice ~start ~length queue =
  if start < 0 || length < 0 || start > queue.count - length then
    Error "the ancestral slice queue window is out of range"
  else if start = 0 && length = queue.count then Ok queue
  else begin
    let low_depth = queue.low_depth + start in
    (* An empty window ends one level above its start, which is exactly the anchor the
       anchored-empty rule requires; a window ending at the retained tail reuses it unqueried. *)
    let target_depth = low_depth + length - 1 in
    if target_depth = queue.low_depth + queue.count - 1 then
      Ok { queue with low_depth; count = length }
    else
      match Incremental_ancestor.ancestor_at_depth queue.arena queue.tail ~depth:target_depth with
      | Error message -> Error message
      | Ok tail -> Ok { queue with tail; low_depth; count = length }
  end

let take count queue =
  if count < 0 || count > queue.count then Error "the ancestral slice queue prefix is out of range"
  else if count = queue.count then Ok queue
  else slice ~start:0 ~length:count queue

let drop count queue =
  if count < 0 || count > queue.count then Error "the ancestral slice queue suffix is out of range"
  else if count = 0 then Ok queue
  else Ok { queue with low_depth = queue.low_depth + count; count = queue.count - count }

let split_at index queue =
  if index < 0 || index > queue.count then
    Error "the ancestral slice queue split boundary is out of range"
  else
    Result.bind (take index queue) (fun left ->
        Result.map (fun right -> (left, right)) (drop index queue))

let to_list queue =
  let rec walk node remaining accumulated =
    match Incremental_ancestor.value_at queue.arena node with
    | Error _ -> accumulated
    | Ok value -> (
        let accumulated = value :: accumulated in
        if remaining <= 1 then accumulated
        else
          match Incremental_ancestor.parent_of queue.arena node with
          | Error _ -> accumulated
          | Ok parent -> walk parent (remaining - 1) accumulated)
  in
  if queue.count = 0 then [] else walk queue.tail queue.count []

let validate queue =
  if queue.count < 0 then Error "the visible count is negative"
  else if queue.low_depth < 0 then Error "the low depth is negative"
  else
    match Incremental_ancestor.depth_of queue.arena queue.tail with
    | Error message -> Error message
    | Ok depth ->
        if depth <> queue.low_depth + queue.count - 1 then
          Error "the tail does not sit at the window's end depth"
        else Incremental_ancestor.validate queue.arena queue.tail
