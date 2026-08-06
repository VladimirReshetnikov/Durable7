(** The append-only incremental level-ancestor seam shared by the ancestry-backed collections.

    The store keeps nodes in blocks of sizes 1, 3, 5, ..., whose square boundaries give [O(1)]
    addressing and [O(sqrt M)] unused slots after [M] published nodes. Managed block allocation is
    what makes leaf addition [O(1)] amortized rather than worst case. *)

type 'value node = {
  value : 'value option;
  parent : int;
  depth : int;
  skip : int;
  skip_distance : int;
}

type 'value t = {
  mutable blocks : 'value node array array;
  mutable block_count : int;
  mutable count : int;
  mutable allocated_slot_count : int;
  mutable add_leaf_count : int;
  mutable ancestor_query_count : int;
  mutable last_ancestor_hop_count : int;
  mutable maximum_ancestor_hop_count : int;
  mutable total_ancestor_hop_count : int;
  gate : Mutex.t;
}

type statistics = {
  published_node_count : int;
  block_count : int;
  allocated_slot_count : int;
  add_leaf_count : int;
  ancestor_query_count : int;
  last_ancestor_hop_count : int;
  maximum_ancestor_hop_count : int;
  total_ancestor_hop_count : int;
}

let bottom = 0
let bottom_node = { value = None; parent = 0; depth = -1; skip = 0; skip_distance = 0 }

(** Runs [body] under the arena's mutex, releasing it even when [body] raises, so a rejected
    operation cannot leave the arena locked. *)
let with_gate arena body =
  Mutex.lock arena.gate;
  match body () with
  | result ->
      Mutex.unlock arena.gate;
      result
  | exception exn ->
      Mutex.unlock arena.gate;
      raise exn

(** [integer_square_root value] is [floor (sqrt value)], computed without floating point so the
    block boundary is exact at every perfect square. *)
let integer_square_root value =
  if value <= 0 then 0
  else begin
    let root = ref (int_of_float (sqrt (float_of_int value))) in
    while !root > 0 && !root > value / !root do
      decr root
    done;
    while (!root + 1) <= value / (!root + 1) do
      incr root
    done;
    !root
  end

let node_at arena index =
  let block = integer_square_root index in
  arena.blocks.(block).((index - (block * block)))

let append arena published =
  let index = arena.count in
  let block = integer_square_root index in
  if block = arena.block_count then begin
    let block_length = (2 * block) + 1 in
    (* Grow the directory before publishing the block, so a failed allocation leaves the counters
       and the block store exactly as they were. *)
    if arena.block_count = Array.length arena.blocks then begin
      let capacity = if arena.block_count = 0 then 4 else arena.block_count * 2 in
      let grown = Array.make capacity [||] in
      Array.blit arena.blocks 0 grown 0 arena.block_count;
      arena.blocks <- grown
    end;
    arena.blocks.(block) <- Array.make block_length published;
    arena.block_count <- arena.block_count + 1;
    arena.allocated_slot_count <- arena.allocated_slot_count + block_length
  end;
  arena.blocks.(block).((index - (block * block))) <- published;
  arena.count <- index + 1;
  index

let create () =
  let arena =
    {
      blocks = [||];
      block_count = 0;
      count = 0;
      allocated_slot_count = 0;
      add_leaf_count = 0;
      ancestor_query_count = 0;
      last_ancestor_hop_count = 0;
      maximum_ancestor_hop_count = 0;
      total_ancestor_hop_count = 0;
      gate = Mutex.create ();
    }
  in
  ignore (append arena bottom_node : int);
  arena

let published_node_count arena = with_gate arena (fun () -> arena.count - 1)

let checked_node arena index =
  if index < 0 || index >= arena.count then Error "the node was never published by this arena"
  else Ok (node_at arena index)

let saturating_add value increment =
  if value > max_int - increment then max_int else value + increment

let add_leaf arena ~parent value =
  with_gate arena (fun () ->
      match checked_node arena parent with
      | Error _ as error -> error
      | Ok parent_node ->
          if parent_node.depth = max_int then Error "the ancestry depth cannot grow further"
          else if arena.count = max_int then Error "the arena cannot hold another node"
          else begin
            let skip, skip_distance =
              if parent = bottom || parent_node.skip = bottom then (parent, 1)
              else begin
                let skipped = node_at arena parent_node.skip in
                if skipped.skip_distance <= parent_node.skip_distance then
                  (skipped.skip, 1 + parent_node.skip_distance + skipped.skip_distance)
                else (parent, 1)
              end
            in
            if skip_distance < 0 then Error "the coalesced jump distance cannot grow further"
            else begin
              let published =
                {
                  value = Some value;
                  parent;
                  depth = parent_node.depth + 1;
                  skip;
                  skip_distance;
                }
              in
              let handle = append arena published in
              arena.add_leaf_count <- arena.add_leaf_count + 1;
              Ok handle
            end
          end)

let depth_of arena node =
  with_gate arena (fun () ->
      match checked_node arena node with Error _ as error -> error | Ok found -> Ok found.depth)

let parent_of arena node =
  with_gate arena (fun () ->
      if node = bottom then Error "the bottom node has no parent"
      else
        match checked_node arena node with
        | Error _ as error -> error
        | Ok found -> Ok found.parent)

let value_at arena node =
  with_gate arena (fun () ->
      if node = bottom then Error "the bottom node has no value"
      else
        match checked_node arena node with
        | Error _ as error -> error
        | Ok found -> (
            match found.value with
            | Some value -> Ok value
            | None -> Error "the node carries no value"))

let ancestor_at_depth arena node ~depth =
  with_gate arena (fun () ->
      match checked_node arena node with
      | Error _ as error -> error
      | Ok start ->
          if depth < -1 || depth > start.depth then
            Error "the depth is outside the node's ancestry"
          else begin
            let handle = ref node in
            let current = ref start in
            let hops = ref 0 in
            while !current.depth > depth do
              let remaining = !current.depth - depth in
              handle :=
                if !current.skip_distance > 0 && !current.skip_distance <= remaining then
                  !current.skip
                else !current.parent;
              current := node_at arena !handle;
              incr hops
            done;
            if arena.ancestor_query_count <> max_int then
              arena.ancestor_query_count <- arena.ancestor_query_count + 1;
            arena.last_ancestor_hop_count <- !hops;
            arena.total_ancestor_hop_count <- saturating_add arena.total_ancestor_hop_count !hops;
            if !hops > arena.maximum_ancestor_hop_count then
              arena.maximum_ancestor_hop_count <- !hops;
            Ok !handle
          end)

let statistics arena =
  with_gate arena (fun () ->
      {
        published_node_count = arena.count - 1;
        block_count = arena.block_count;
        allocated_slot_count = arena.allocated_slot_count;
        add_leaf_count = arena.add_leaf_count;
        ancestor_query_count = arena.ancestor_query_count;
        last_ancestor_hop_count = arena.last_ancestor_hop_count;
        maximum_ancestor_hop_count = arena.maximum_ancestor_hop_count;
        total_ancestor_hop_count = arena.total_ancestor_hop_count;
      })

let validate arena node =
  with_gate arena (fun () ->
      match checked_node arena node with
      | Error _ as error -> error
      | Ok start ->
          let rec walk current =
            if current.depth < 0 then
              if current.parent = bottom && current.skip = bottom && current.depth = -1 then Ok ()
              else Error "the bottom node is malformed"
            else begin
              let parent_node = node_at arena current.parent in
              if parent_node.depth <> current.depth - 1 then
                Error "a parent link does not descend exactly one level"
              else if current.skip_distance < 1 then
                Error "a jump link has a non-positive distance"
              else begin
                let skipped = node_at arena current.skip in
                if skipped.depth <> current.depth - current.skip_distance then
                  Error "a jump link does not match its recorded distance"
                else walk parent_node
              end
            end
          in
          walk start)
