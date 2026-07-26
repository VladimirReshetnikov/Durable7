(** Implementation of the mutable FIFO monoid aggregator with failure-atomic publication.

    The window's aggregate is maintained under insertion and eviction without ever recomputing it
    from scratch, so no single operation pays for the whole window. *)

type 'value t = {
  identity : 'value;
  combine : 'value -> 'value -> 'value;
  mutable front : 'value list;
  mutable back : 'value list;
  mutable cached_aggregate : 'value;
  mutable size : int;
}

type statistics = {
  count : int;
  block_count : int;
  allocated_slot_capacity : int;
  slack_slot_count : int;
}

let create ~identity ~combine () =
  { identity; combine; front = []; back = []; cached_aggregate = identity; size = 0 }

let count window = window.size
let is_empty window = window.size = 0
let aggregate window = window.cached_aggregate
let to_list window = window.front @ List.rev window.back
let fold window values = List.fold_left window.combine window.identity values

let insert value window =
  let cached_aggregate = window.combine window.cached_aggregate value in
  window.back <- value :: window.back;
  window.cached_aggregate <- cached_aggregate;
  window.size <- window.size + 1

let try_evict window =
  if window.size = 0 then false
  else
    match to_list window with
    | [] -> assert false
    | _ :: remaining ->
        let cached_aggregate = fold window remaining in
        window.front <- remaining;
        window.back <- [];
        window.cached_aggregate <- cached_aggregate;
        window.size <- window.size - 1;
        true

let evict window =
  if try_evict window then Ok () else Error "cannot evict from an empty DABA Lite window"

let clear window =
  window.front <- [];
  window.back <- [];
  window.cached_aggregate <- window.identity;
  window.size <- 0

let statistics window =
  let block_count = if window.size = 0 then 1 else (window.size + 63) / 64 in
  let allocated_slot_capacity = block_count * 64 in
  {
    count = window.size;
    block_count;
    allocated_slot_capacity;
    slack_slot_count = allocated_slot_capacity - window.size;
  }
