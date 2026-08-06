(** Implementation of the fixed-length persistent vector with a checkpoint and an exact maximal-run
    change index.

    Two representation choices carry the whole design. Values are stored in a one-field [cell]
    record, so every position owns an identity token that survives splitting and concatenation: the
    cleanliness invariant is "a clean position reuses its exact checkpoint slot", which needs
    reference identity, and the retained relation may be coarser or finer than any structural
    equality so it cannot decide representative identity. Boxing also keeps the invariant checkable
    for immediate payloads, where OCaml's [( == )] would otherwise collapse into value equality and
    make the check vacuous.

    The run index stores one [start -> end_exclusive] record per maximal run, so accepting or
    reverting a run is a boundary splice on both roots rather than a walk over the run's positions.
    That is the only reason those operations are independent of a run's length. *)

type 'value equality = { relation : 'value -> 'value -> bool; canonical : string option }

(* One element slot, identified by block rather than by value; see the module note above. *)
type 'value cell = { payload : 'value }

type run = { start : int; length : int }
type statistics = { count : int; dirty_count : int; dirty_run_count : int }

type 'value t = {
  current : 'value cell Rrb_vector.t;
  checkpoint_root : 'value cell Rrb_vector.t;
  runs : (int, int) Sorted_map.t;
  dirty : int;
  value_relation : 'value equality;
}

let out_of_range = "run-delta vector position is out of range"
let rank_out_of_range = "dirty-run rank is out of range"
let root_length_mismatch = "the current and checkpoint roots have different lengths"
let empty_runs : (int, int) Sorted_map.t = Sorted_map.empty (Common.Comparator.create Int.compare)
let canonical_equality name relation = { relation; canonical = Some name }
let equality relation = { relation; canonical = None }
let default () = canonical_equality "default" (fun left right -> Stdlib.compare left right = 0)
let reflexive_ieee () = canonical_equality "reflexive-ieee" Float.equal
let equal policy left right = policy.relation left right

let same_equality left right =
  left == right
  ||
  match (left.canonical, right.canonical) with
  | Some left_name, Some right_name -> String.equal left_name right_name
  | Some _, None | None, Some _ | None, None -> false

let run_end_exclusive run = run.start + run.length
let run_contains index run = index >= run.start && index < run_end_exclusive run

let to_run entry =
  let start = Sorted_map.entry_key entry in
  { start; length = Sorted_map.entry_value entry - start }

let set_run start finish runs = snd (Sorted_map.set start finish runs)

let remove_run start runs =
  match Sorted_map.remove start runs with Some (_, successor) -> successor | None -> runs

(* The unique run containing the position, found through the start-keyed index rather than by a
   scan: the greatest record whose start is at or below the position, kept only when it reaches past
   the position. *)
let find_run index runs =
  match Sorted_map.floor index runs with
  | Some entry when Sorted_map.entry_value entry > index -> Some (to_run entry)
  | Some _ | None -> None

(* Records the position as dirty, merging the runs on both sides, extending one side, or inserting a
   fresh singleton run. The result stays ordered, non-overlapping, non-adjacent and maximal, and at
   most two records change. *)
let add_dirty_position runs index =
  let left =
    match Sorted_map.floor index runs with
    | Some entry when Sorted_map.entry_value entry = index -> Some (Sorted_map.entry_key entry)
    | Some _ | None -> None
  in
  let right =
    match Sorted_map.ceiling index runs with
    | Some entry when Sorted_map.entry_key entry = index + 1 ->
        Some (Sorted_map.entry_key entry, Sorted_map.entry_value entry)
    | Some _ | None -> None
  in
  match (left, right) with
  | Some left_start, Some (right_start, right_end) ->
      set_run left_start right_end (remove_run right_start runs)
  | Some left_start, None -> set_run left_start (index + 1) runs
  | None, Some (right_start, right_end) -> set_run index right_end (remove_run right_start runs)
  | None, None -> set_run index (index + 1) runs

(* Clears the position from its containing run: deleting a singleton, shrinking at either edge, or
   splitting the run in two. Taking the already-located run keeps the operation total. *)
let remove_dirty_position runs containing index =
  let start = containing.start in
  let finish = run_end_exclusive containing in
  if start = index && finish = index + 1 then remove_run start runs
  else if start = index then set_run (index + 1) finish (remove_run start runs)
  else if finish = index + 1 then set_run start index runs
  else set_run (index + 1) finish (set_run start index runs)

(* Splices the source's slice over the destination's, sharing every untouched subtree. Four splits
   and two boundary concatenations, so the cost tracks the tree's height and not the run's length,
   and no payload is ever inspected. *)
let replace_range destination source run =
  if run.start = 0 && run.length = Rrb_vector.length destination then source
  else
    let left, destination_tail = Rrb_vector.split_at run.start destination in
    let _, right = Rrb_vector.split_at run.length destination_tail in
    let _, source_tail = Rrb_vector.split_at run.start source in
    let middle, _ = Rrb_vector.split_at run.length source_tail in
    Rrb_vector.concat (Rrb_vector.concat left middle) right

let clean_of root vector =
  { vector with current = root; checkpoint_root = root; runs = empty_runs; dirty = 0 }

let empty value_relation =
  {
    current = Rrb_vector.empty;
    checkpoint_root = Rrb_vector.empty;
    runs = empty_runs;
    dirty = 0;
    value_relation;
  }

let of_list value_relation values =
  let root = Rrb_vector.of_list (List.map (fun value -> { payload = value }) values) in
  { current = root; checkpoint_root = root; runs = empty_runs; dirty = 0; value_relation }

let value_equality vector = vector.value_relation
let length vector = Rrb_vector.length vector.current
let is_empty vector = length vector = 0
let dirty_count vector = vector.dirty
let dirty_run_count vector = Sorted_map.count vector.runs
let has_changes vector = vector.dirty <> 0
let payload_of cell = cell.payload
let nth index vector = Option.map payload_of (Rrb_vector.nth index vector.current)
let to_list vector = List.map payload_of (Rrb_vector.to_list vector.current)

let checkpoint_nth index vector =
  Option.map payload_of (Rrb_vector.nth index vector.checkpoint_root)

let checkpoint_to_list vector = List.map payload_of (Rrb_vector.to_list vector.checkpoint_root)
let in_range index vector = index >= 0 && index < length vector

let is_dirty index vector =
  if in_range index vector then Some (Option.is_some (find_run index vector.runs)) else None

let dirty_run_containing index vector =
  if in_range index vector then find_run index vector.runs else None

let dirty_run_at rank vector = Option.map to_run (Sorted_map.nth rank vector.runs)

let dirty_runs vector =
  List.map
    (fun (start, finish) -> { start; length = finish - start })
    (Sorted_map.to_list vector.runs)

let set index value vector =
  match Rrb_vector.nth index vector.current with
  | None -> Error out_of_range
  | Some current_cell -> (
      match Rrb_vector.nth index vector.checkpoint_root with
      | None -> Error root_length_mismatch
      | Some checkpoint_cell ->
          (* Both relation calls happen here, before any successor exists, so a relation that raises
             leaves the receiver untouched and publishes nothing. *)
          if vector.value_relation.relation current_cell.payload value then Ok vector
          else
            let containing = find_run index vector.runs in
            let remains_dirty =
              not (vector.value_relation.relation checkpoint_cell.payload value)
            in
            (* Cancellation restores the checkpoint's own slot, never a fresh equal one. *)
            let replacement = if remains_dirty then { payload = value } else checkpoint_cell in
            let next_runs, next_dirty =
              match (containing, remains_dirty) with
              | None, true -> (add_dirty_position vector.runs index, vector.dirty + 1)
              | Some found, false ->
                  (remove_dirty_position vector.runs found index, vector.dirty - 1)
              | None, false -> (vector.runs, vector.dirty)
              | Some _, true -> (vector.runs, vector.dirty)
            in
            Result.map
              (fun edited ->
                let next_current =
                  if Sorted_map.is_empty next_runs then vector.checkpoint_root else edited
                in
                { vector with current = next_current; runs = next_runs; dirty = next_dirty })
              (Rrb_vector.set index replacement vector.current))

let reset index vector =
  match Rrb_vector.nth index vector.checkpoint_root with
  | None -> Error out_of_range
  | Some checkpoint_cell -> set index checkpoint_cell.payload vector

let checkpoint vector = if has_changes vector then clean_of vector.current vector else vector
let rollback vector = if has_changes vector then clean_of vector.checkpoint_root vector else vector

let accept_run run vector =
  if Sorted_map.count vector.runs = 1 then checkpoint vector
  else
    {
      vector with
      checkpoint_root = replace_range vector.checkpoint_root vector.current run;
      runs = remove_run run.start vector.runs;
      dirty = vector.dirty - run.length;
    }

let revert_run run vector =
  if Sorted_map.count vector.runs = 1 then rollback vector
  else
    {
      vector with
      current = replace_range vector.current vector.checkpoint_root run;
      runs = remove_run run.start vector.runs;
      dirty = vector.dirty - run.length;
    }

let by_rank splice rank vector =
  match dirty_run_at rank vector with
  | None -> Error rank_out_of_range
  | Some run -> Ok (splice run vector)

let by_position splice index vector =
  if not (in_range index vector) then Error out_of_range
  else
    match find_run index vector.runs with
    | None -> Ok vector
    | Some run -> Ok (splice run vector)

let accept_dirty_run_at rank vector = by_rank accept_run rank vector
let revert_dirty_run_at rank vector = by_rank revert_run rank vector
let accept_dirty_run_containing index vector = by_position accept_run index vector
let revert_dirty_run_containing index vector = by_position revert_run index vector
let shares_current_root left right = left.current == right.current
let shares_checkpoint_root left right = left.checkpoint_root == right.checkpoint_root

let reuses_checkpoint_representative index vector =
  match (Rrb_vector.nth index vector.current, Rrb_vector.nth index vector.checkpoint_root) with
  | Some current_cell, Some checkpoint_cell -> Some (current_cell == checkpoint_cell)
  | Some _, None | None, Some _ | None, None -> None

(* Ordering, non-emptiness, range, non-overlap and non-adjacency together are exactly maximality:
   with every covered position genuinely dirty and every uncovered position clean, no run can be
   extended and no two runs can be joined. *)
let rec check_runs count previous_end counted = function
  | [] -> Ok counted
  | run :: rest ->
      let finish = run_end_exclusive run in
      if run.start < 0 || run.length <= 0 || finish > count || run.start <= previous_end then
        Error "dirty runs are empty, out of range, overlapping, adjacent, or unordered"
      else check_runs count finish (counted + run.length) rest

let rec drop_finished index runs =
  match runs with
  | run :: rest when run_end_exclusive run <= index -> drop_finished index rest
  | [] | _ :: _ -> runs

let rec check_positions vector index pending current_cells checkpoint_cells =
  match (current_cells, checkpoint_cells) with
  | [], [] -> Ok ()
  | [], _ :: _ -> Error root_length_mismatch
  | _ :: _, [] -> Error root_length_mismatch
  | current_cell :: current_rest, checkpoint_cell :: checkpoint_rest ->
      let pending = drop_finished index pending in
      let dirty = match pending with run :: _ -> run_contains index run | [] -> false in
      let related =
        vector.value_relation.relation current_cell.payload checkpoint_cell.payload
      in
      if (not dirty) && not (current_cell == checkpoint_cell) then
        Error "a clean position does not reuse its exact checkpoint representative"
      else if (not dirty) && not related then
        (* The slots are the same block, so a relation that disagrees is not reflexive. Reporting it
           turns the inherited silent-corruption failure mode into a detectable one. *)
        Error "the retained relation is not reflexive at a clean position"
      else if dirty && related then
        Error "a dirty position is relation-equal to its checkpoint value"
      else check_positions vector (index + 1) pending current_rest checkpoint_rest

let validate vector =
  let count = length vector in
  let descriptors = dirty_runs vector in
  if Rrb_vector.length vector.checkpoint_root <> count then Error root_length_mismatch
  else if vector.dirty < 0 || vector.dirty > count then
    Error "the dirty cardinality is outside the position count"
  else if
    Sorted_map.is_empty vector.runs
    && (vector.dirty <> 0 || not (vector.current == vector.checkpoint_root))
  then Error "a clean version is not canonicalized onto its checkpoint root"
  else
    match check_runs count (-1) 0 descriptors with
    | Error message -> Error message
    | Ok counted ->
        if counted <> vector.dirty then
          Error "dirty run lengths do not sum to the dirty cardinality"
        else
          Result.map
            (fun () ->
              {
                count;
                dirty_count = vector.dirty;
                dirty_run_count = Sorted_map.count vector.runs;
              })
            (check_positions vector 0 descriptors
               (Rrb_vector.to_list vector.current)
               (Rrb_vector.to_list vector.checkpoint_root))
