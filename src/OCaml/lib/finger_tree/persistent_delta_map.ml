(** Implementation of the checkpoint-differential persistent sorted map.

    Three immutable roots per value: the checkpoint state, the current state, and an ordered change
    index holding one record per key class on which the two states differ. The index is keyed by the
    same comparator as the states, which is what makes a range-restricted change query a boundary
    seek over the index rather than a filter over every record.

    The stored delta is a three-way variant rather than a pair of options, so "absent at both
    endpoints" is not representable: a cancellation removes the record instead of writing an empty
    one. That turns every endpoint and classification accessor into a total function with no
    defensive branch, which the C# and Rust ports have to guard dynamically. *)

(** One recorded net change's endpoints. A doubly-absent record is unrepresentable by construction;
    {!store_change} removes the record instead. *)
type 'value delta =
  | Added_at of 'value
  | Removed_at of 'value
  | Updated_at of 'value * 'value

type ('key, 'value) t = {
  current : ('key, 'value) Sorted_map.t;
  checkpoint : ('key, 'value) Sorted_map.t;
  changes : ('key, 'value delta) Sorted_map.t;
  order : 'key Common.Comparator.t;
  equal_values : 'value -> 'value -> bool;
}

type ('key, 'value) change = { key : 'key; delta : 'value delta }
type change_kind = Added | Removed | Updated

type statistics = {
  entry_count : int;
  checkpoint_entry_count : int;
  change_count : int;
  added_count : int;
  removed_count : int;
  updated_count : int;
  is_clean : bool;
}

let delta_before = function
  | Added_at _ -> None
  | Removed_at before -> Some before
  | Updated_at (before, _) -> Some before

let delta_after = function
  | Added_at after -> Some after
  | Removed_at _ -> None
  | Updated_at (_, after) -> Some after

(** Whether two presence-carrying endpoints agree under the retained value relation. Absence is an
    endpoint of its own, so it agrees only with absence. *)
let equivalent map left right =
  match (left, right) with
  | None, None -> true
  | Some left, Some right -> map.equal_values left right
  | Some _, None | None, Some _ -> false

let entry_pair entry = (Sorted_map.entry_key entry, Sorted_map.entry_value entry)

let create order ~equal_values =
  let state = Sorted_map.empty order in
  { current = state; checkpoint = state; changes = Sorted_map.empty order; order; equal_values }

let empty () = create (Common.Comparator.default ()) ~equal_values:( = )

let of_list order ~equal_values entries =
  (* Removing before setting makes the last member of an equivalent class supply the retained key
     representative as well as the value, matching the baseline. The substrate's own [set] would
     keep the first representative instead. *)
  let builder = Sorted_map.Builder.create (Sorted_map.empty order) in
  List.iter
    (fun (key, value) ->
      ignore (Sorted_map.Builder.remove key builder);
      ignore (Sorted_map.Builder.set key value builder))
    entries;
  let state = Sorted_map.Builder.freeze builder in
  { current = state; checkpoint = state; changes = Sorted_map.empty order; order; equal_values }

let comparator map = map.order
let value_equal map left right = map.equal_values left right
let count map = Sorted_map.count map.current
let is_empty map = Sorted_map.is_empty map.current
let change_count map = Sorted_map.count map.changes
let has_changes map = not (Sorted_map.is_empty map.changes)
let is_clean map = Sorted_map.is_empty map.changes && map.current == map.checkpoint

let shares_storage_with left right =
  left.current == right.current && left.checkpoint == right.checkpoint
  && left.changes == right.changes

let current_snapshot map = map.current
let checkpoint_snapshot map = map.checkpoint
let find_opt key map = Sorted_map.find_opt key map.current
let find_entry key map = Option.map entry_pair (Sorted_map.find_entry key map.current)
let mem key map = Sorted_map.mem key map.current

let index_of_key key map =
  let index = Sorted_map.lower_bound key map.current in
  match Sorted_map.nth index map.current with
  | Some entry when Common.Comparator.compare map.order (Sorted_map.entry_key entry) key = 0 ->
      Some index
  | Some _ | None -> None

let nth index map = Option.map entry_pair (Sorted_map.nth index map.current)
let minimum map = Option.map entry_pair (Sorted_map.minimum map.current)
let maximum map = Option.map entry_pair (Sorted_map.maximum map.current)
let floor key map = Option.map entry_pair (Sorted_map.floor key map.current)
let ceiling key map = Option.map entry_pair (Sorted_map.ceiling key map.current)
let lower key map = Option.map entry_pair (Sorted_map.lower key map.current)
let higher key map = Option.map entry_pair (Sorted_map.higher key map.current)
let key_range ~minimum ~maximum map = Sorted_map.key_range ~minimum ~maximum map.current
let to_list map = Sorted_map.to_list map.current
let keys map = List.map fst (to_list map)
let values map = List.map snd (to_list map)

(** Publishes a successor. A wholly cancelled epoch snaps the current root back to the {e exact}
    checkpoint root, so a clean value is storage-clean as well as extensionally clean and later
    checkpoint, rollback, and cleanliness queries stay [O(1)] root comparisons. *)
let successor map current changes =
  let current = if Sorted_map.is_empty changes then map.checkpoint else current in
  { map with current; changes }

(** Writes, rewrites, or cancels one record. An endpoint pair the retained relation calls equivalent
    is not a net change, so its record is removed rather than stored. *)
let store_change map representative ~before ~after changes =
  let drop () =
    match Sorted_map.remove representative changes with
    | Some (_, next) -> next
    | None -> changes
  in
  let write delta = snd (Sorted_map.set representative delta changes) in
  match (before, after) with
  | None, None -> drop ()
  | None, Some after -> write (Added_at after)
  | Some before, None -> write (Removed_at before)
  | Some before, Some after ->
      if map.equal_values before after then drop () else write (Updated_at (before, after))

let set key value map =
  let current_entry = Sorted_map.find_entry key map.current in
  let semantic_no_op =
    match current_entry with
    | Some entry -> map.equal_values (Sorted_map.entry_value entry) value
    | None -> false
  in
  if semantic_no_op then map
  else
    let change_entry = Sorted_map.find_entry key map.changes in
    (* A class present in the current state keeps its stored representative; a still-active addition
       episode keeps the representative its record retained across the delete leg; only a genuinely
       new class takes the supplied key. *)
    let representative =
      match (current_entry, change_entry) with
      | Some entry, _ -> Sorted_map.entry_key entry
      | None, Some entry -> Sorted_map.entry_key entry
      | None, None -> key
    in
    (* The first effective write to a class captures its checkpoint endpoint; every later write
       coalesces into that same record and replaces only the current endpoint. *)
    let before =
      match change_entry with
      | Some entry -> delta_before (Sorted_map.entry_value entry)
      | None -> Option.map Sorted_map.entry_value current_entry
    in
    let next_current = snd (Sorted_map.set representative value map.current) in
    successor map next_current
      (store_change map representative ~before ~after:(Some value) map.changes)

let set_list entries map =
  List.fold_left (fun state (key, value) -> set key value state) map entries

let remove key map =
  match Sorted_map.find_entry key map.current with
  | None -> map
  | Some current_entry ->
      let change_entry = Sorted_map.find_entry key map.changes in
      let representative =
        match change_entry with
        | Some entry -> Sorted_map.entry_key entry
        | None -> Sorted_map.entry_key current_entry
      in
      let before =
        match change_entry with
        | Some entry -> delta_before (Sorted_map.entry_value entry)
        | None -> Some (Sorted_map.entry_value current_entry)
      in
      let next_current =
        match Sorted_map.remove (Sorted_map.entry_key current_entry) map.current with
        | Some (_, next) -> next
        | None -> map.current
      in
      successor map next_current (store_change map representative ~before ~after:None map.changes)

let checkpoint map =
  if is_clean map then map
  else { map with checkpoint = map.current; changes = Sorted_map.empty map.order }

let rollback map =
  if is_clean map then map
  else { map with current = map.checkpoint; changes = Sorted_map.empty map.order }

let change_key change = change.key
let change_before change = delta_before change.delta
let change_after change = delta_after change.delta

let change_kind change =
  match change.delta with
  | Added_at _ -> Added
  | Removed_at _ -> Removed
  | Updated_at _ -> Updated

let change_of_pair (key, delta) = { key; delta }

let change_of_entry entry =
  { key = Sorted_map.entry_key entry; delta = Sorted_map.entry_value entry }

let changes map = List.map change_of_pair (Sorted_map.to_list map.changes)
let find_change key map = Option.map change_of_entry (Sorted_map.find_entry key map.changes)

(* A boundary seek over the change index followed by a bounded walk of the entries between the two
   boundaries. The substrate restricts by two binary searches and one contiguous copy, so the cost
   is O(log (k + 1)) comparisons plus Theta (output) and never touches an out-of-range record. No
   value-equality callback is reachable from here. *)
let changes_in_range ~minimum ~maximum map =
  List.map change_of_pair (Sorted_map.to_list (Sorted_map.key_range ~minimum ~maximum map.changes))

let ( let* ) = Result.bind

let rec check_ascending order message entries =
  match entries with
  | (first, _) :: (((second, _) :: _) as rest) ->
      if Common.Comparator.compare order first second < 0 then check_ascending order message rest
      else Error message
  | [] | [ _ ] -> Ok ()

let validate map =
  let current = Sorted_map.to_list map.current in
  let checkpoint_entries = Sorted_map.to_list map.checkpoint in
  let recorded = Sorted_map.to_list map.changes in
  let* () =
    check_ascending map.order "the current state is not strictly ascending by key" current
  in
  let* () =
    check_ascending map.order "the checkpoint state is not strictly ascending by key"
      checkpoint_entries
  in
  let* () =
    check_ascending map.order "the change index is not strictly ascending by key" recorded
  in
  let* () =
    if Sorted_map.is_empty map.changes && not (map.current == map.checkpoint) then
      Error "a value without changes does not reuse its checkpoint root"
    else Ok ()
  in
  let* added_count, removed_count, updated_count =
    List.fold_left
      (fun state (key, delta) ->
        let* added, removed, updated = state in
        let before = delta_before delta in
        let after = delta_after delta in
        if equivalent map before after then Error "a recorded change has equivalent endpoints"
        else if not (equivalent map before (Sorted_map.find_opt key map.checkpoint)) then
          Error "a change's before endpoint differs from the checkpoint state"
        else if not (equivalent map after (Sorted_map.find_opt key map.current)) then
          Error "a change's after endpoint differs from the current state"
        else
          match delta with
          | Added_at _ -> Ok (added + 1, removed, updated)
          | Removed_at _ -> Ok (added, removed + 1, updated)
          | Updated_at _ -> Ok (added, removed, updated + 1))
      (Ok (0, 0, 0))
      recorded
  in
  let* from_checkpoint =
    List.fold_left
      (fun state (key, value) ->
        let* total = state in
        let unchanged =
          match Sorted_map.find_opt key map.current with
          | Some current_value -> map.equal_values value current_value
          | None -> false
        in
        if unchanged then Ok total
        else if Sorted_map.mem key map.changes then Ok (total + 1)
        else Error "a checkpoint difference has no recorded change")
      (Ok 0) checkpoint_entries
  in
  let* expected =
    List.fold_left
      (fun state (key, _) ->
        let* total = state in
        if Sorted_map.mem key map.checkpoint then Ok total
        else if Sorted_map.mem key map.changes then Ok (total + 1)
        else Error "a current addition has no recorded change")
      (Ok from_checkpoint) current
  in
  if expected <> List.length recorded then Error "the recorded change cardinality is not exact"
  else
    Ok
      {
        entry_count = List.length current;
        checkpoint_entry_count = List.length checkpoint_entries;
        change_count = List.length recorded;
        added_count;
        removed_count;
        updated_count;
        is_clean = is_clean map;
      }
