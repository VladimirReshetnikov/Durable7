(** Implementation of the persistent sequence that ranks and selects context-dependent events.

    The whole structure is one measure policy over {!Measured_tree}. A subtree's summary is a pair of
    eagerly filled arrays indexed by incoming machine state: where that state leaves the subtree, and
    how many events the subtree emits from it. Composition reads the left table first and uses its
    outgoing state to index the right table, which is associative but not commutative.

    A machine that breaks its contract cannot be reported through the measure monoid, which has no
    fallible channel. So a broken transition is neutralized in place — the offending column falls
    back to the identity effect, keeping every index in range and every later composition total — and
    the diagnostic is carried up the tree in [summary_fault]. The API boundary reads the root fault
    once and refuses to publish the successor, which is why no operation here raises. *)

type transition = { next_state : int; emitted_count : int }
type prefix_summary = { final_state : int; event_count : int }

type event_location = {
  element_index : int;
  event_index_in_element : int;
  state_before : int;
  state_after : int;
  element_event_count : int;
}

(* The cached per-state effect table. [summary_next] and [summary_events] both have one entry per
   machine state; [summary_fault] holds the first contract violation seen anywhere below. *)
type summary = {
  summary_count : int;
  summary_next : int array;
  summary_events : int array;
  summary_fault : string option;
}

type 'element machine = {
  machine_name : string;
  machine_states : int;
  machine_step : int -> 'element -> transition;
  machine_policy : ('element, summary) Measures.policy;
}

type 'element t = {
  seq_machine : 'element machine;
  seq_tree : ('element, summary) Measured_tree.t;
}

let invalid_state_fault = "the contextual event machine returned a state outside its declared range"
let negative_count_fault = "the contextual event machine returned a negative event count"
let overflow_fault = "the contextual event total overflowed the native integer range"

let identity_summary state_count =
  {
    summary_count = 0;
    summary_next = Array.init state_count (fun state -> state);
    summary_events = Array.make state_count 0;
    summary_fault = None;
  }

(* One machine run per state, eagerly. A transition outside the declared range is replaced by the
   identity effect so the table stays a total function into valid states. *)
let element_summary state_count step element =
  let next = Array.make state_count 0 in
  let events = Array.make state_count 0 in
  let fault = ref None in
  let record message = if Option.is_none !fault then fault := Some message in
  for state = 0 to state_count - 1 do
    let taken = step state element in
    if taken.next_state < 0 || taken.next_state >= state_count then begin
      record invalid_state_fault;
      next.(state) <- state
    end
    else next.(state) <- taken.next_state;
    if taken.emitted_count < 0 then begin
      record negative_count_fault;
      events.(state) <- 0
    end
    else events.(state) <- taken.emitted_count
  done;
  { summary_count = 1; summary_next = next; summary_events = events; summary_fault = !fault }

(* Ordered composition, [left] preceding [right]. The identity short-circuits are an optimization:
   composing against the genuine identity table already yields the other operand unchanged. *)
let combine_summaries left right =
  if left.summary_count = 0 then right
  else if right.summary_count = 0 then left
  else begin
    let width = Array.length left.summary_next in
    let next = Array.make width 0 in
    let events = Array.make width 0 in
    let fault =
      ref
        (match left.summary_fault with
        | Some _ as carried -> carried
        | None -> right.summary_fault)
    in
    let record message = if Option.is_none !fault then fault := Some message in
    for state = 0 to width - 1 do
      let middle = left.summary_next.(state) in
      next.(state) <- right.summary_next.(middle);
      let before = left.summary_events.(state) in
      let after = right.summary_events.(middle) in
      if before > max_int - after then begin
        record overflow_fault;
        events.(state) <- max_int
      end
      else events.(state) <- before + after
    done;
    {
      summary_count = left.summary_count + right.summary_count;
      summary_next = next;
      summary_events = events;
      summary_fault = !fault;
    }
  end

let create_machine ~id ~state_count ~transition () =
  if String.trim id = "" then Error "contextual event machine id must not be empty"
  else if state_count <= 0 then
    Error "a contextual event machine must declare at least one state"
  else
    let policy =
      Measures.create_policy
        ~id:("contextual-rank-sequence-v1:" ^ id)
        ~monoid:
          (Measures.create_monoid ~empty:(identity_summary state_count)
             ~append:combine_summaries)
        ~measure:(fun element -> element_summary state_count transition element)
        ()
    in
    Ok
      {
        machine_name = id;
        machine_states = state_count;
        machine_step = transition;
        machine_policy = policy;
      }

let machine_id event_machine = event_machine.machine_name
let machine_state_count event_machine = event_machine.machine_states

let empty event_machine =
  { seq_machine = event_machine; seq_tree = Measured_tree.empty event_machine.machine_policy }

(* Reads the root fault once. A faulted tree is never handed out, so every published sequence is
   fault-free and every query below may read its cached tables without rechecking them. *)
let publish event_machine tree =
  match (Measured_tree.measure tree).summary_fault with
  | Some message -> Error message
  | None -> Ok { seq_machine = event_machine; seq_tree = tree }

let of_list event_machine items =
  publish event_machine (Measured_tree.of_list event_machine.machine_policy items)

let machine sequence = sequence.seq_machine
let state_count sequence = sequence.seq_machine.machine_states
let length sequence = Measured_tree.length sequence.seq_tree
let is_empty sequence = Measured_tree.is_empty sequence.seq_tree
let nth index sequence = Measured_tree.nth index sequence.seq_tree
let to_list sequence = Measured_tree.to_list sequence.seq_tree
let iter action sequence = Measured_tree.iter action sequence.seq_tree
let valid_state event_machine state = state >= 0 && state < event_machine.machine_states

let evaluate ~initial_state sequence =
  if not (valid_state sequence.seq_machine initial_state) then None
  else
    let root = Measured_tree.measure sequence.seq_tree in
    Some
      {
        final_state = root.summary_next.(initial_state);
        event_count = root.summary_events.(initial_state);
      }

let evaluate_prefix ~element_count ~initial_state sequence =
  if not (valid_state sequence.seq_machine initial_state) then None
  else if element_count < 0 || element_count > length sequence then None
  else if element_count = 0 then Some { final_state = initial_state; event_count = 0 }
  else if element_count = length sequence then evaluate ~initial_state sequence
  else
    (* The first inclusive prefix holding more than [element_count] elements starts at the boundary,
       so the exclusive prefix measure the descent reports is exactly the prefix summary. *)
    match
      Measured_tree.locate
        (fun (value : summary) -> value.summary_count > element_count)
        sequence.seq_tree
    with
    | None -> None
    | Some (_, before, _) ->
        Some
          {
            final_state = before.summary_next.(initial_state);
            event_count = before.summary_events.(initial_state);
          }

let event_rank ~element_count ~initial_state sequence =
  Option.map
    (fun (value : prefix_summary) -> value.event_count)
    (evaluate_prefix ~element_count ~initial_state sequence)

let select_event ~event_index ~initial_state sequence =
  if not (valid_state sequence.seq_machine initial_state) then None
  else if event_index < 0 then None
  else
    let root = Measured_tree.measure sequence.seq_tree in
    if event_index >= root.summary_events.(initial_state) then None
    else
      (* Nonnegative emitted counts make this predicate monotone in the prefix, which is what buys
         the single descent. *)
      match
        Measured_tree.locate
          (fun (value : summary) -> value.summary_events.(initial_state) > event_index)
          sequence.seq_tree
      with
      | None -> None
      | Some (index, before, element) ->
          let state_before = before.summary_next.(initial_state) in
          let taken = sequence.seq_machine.machine_step state_before element in
          (* The identical call was validated when this element entered the tree, so a deterministic
             machine honouring its contract cannot fail here; the guard keeps the function total for
             one that does not. *)
          if
            taken.next_state < 0
            || taken.next_state >= sequence.seq_machine.machine_states
            || taken.emitted_count < 0
          then None
          else
            Some
              {
                element_index = index;
                event_index_in_element = event_index - before.summary_events.(initial_state);
                state_before;
                state_after = taken.next_state;
                element_event_count = taken.emitted_count;
              }

let cons item sequence =
  publish sequence.seq_machine (Measured_tree.cons item sequence.seq_tree)

let snoc sequence item =
  publish sequence.seq_machine (Measured_tree.snoc sequence.seq_tree item)

let concat left right =
  if not (String.equal left.seq_machine.machine_name right.seq_machine.machine_name) then
    Error "cannot concatenate contextual sequences built with different machines"
  else if left.seq_machine.machine_states <> right.seq_machine.machine_states then
    Error "cannot concatenate contextual sequences whose machines declare different state counts"
  else if is_empty right then Ok left
  else if is_empty left then Ok right
  else
    Result.bind
      (Measured_tree.concat left.seq_tree right.seq_tree)
      (publish left.seq_machine)

let insert_at index item sequence =
  if index < 0 || index > length sequence then
    Error "contextual sequence insertion index is out of range"
  else
    let left, right = Measured_tree.split_at index sequence.seq_tree in
    Result.bind
      (Measured_tree.concat (Measured_tree.snoc left item) right)
      (publish sequence.seq_machine)

let set_item index item sequence =
  if index < 0 || index >= length sequence then
    Error "contextual sequence replacement index is out of range"
  else
    let left, tail = Measured_tree.split_at index sequence.seq_tree in
    let _, right = Measured_tree.split_at 1 tail in
    Result.bind
      (Measured_tree.concat (Measured_tree.snoc left item) right)
      (publish sequence.seq_machine)

let remove_at index sequence =
  if index < 0 || index >= length sequence then
    Error "contextual sequence removal index is out of range"
  else
    let left, tail = Measured_tree.split_at index sequence.seq_tree in
    let _, right = Measured_tree.split_at 1 tail in
    Result.bind (Measured_tree.concat left right) (publish sequence.seq_machine)

let split_at index sequence =
  if index < 0 || index > length sequence then
    Error "contextual sequence split index is out of range"
  else if index = 0 then Ok (empty sequence.seq_machine, sequence)
  else if index = length sequence then Ok (sequence, empty sequence.seq_machine)
  else
    (* Both halves go through [publish]. Splitting re-joins subtrees the source never had to compose
       against each other, so a half can carry an event total the original tree never represented;
       handing one back unpublished would return a faulted summary as [Ok]. Either half faulting
       fails the whole operation, so nothing partial escapes. *)
    let left, right = Measured_tree.split_at index sequence.seq_tree in
    let machine = sequence.seq_machine in
    Result.bind (publish machine left) (fun published_left ->
        Result.bind (publish machine right) (fun published_right ->
            Ok (published_left, published_right)))

let sub ~start ~length:count sequence =
  let total = length sequence in
  if start < 0 || count < 0 || start > total - count then
    Error "contextual sequence range is out of bounds"
  else if count = 0 then Ok (empty sequence.seq_machine)
  else if count = total then Ok sequence
  else
    (* Published for the same reason as [split_at]: the selected window is a freshly recomposed
       tree, so its cached event total is not one the source already validated. *)
    let _, tail = Measured_tree.split_at start sequence.seq_tree in
    let selected, _ = Measured_tree.split_at count tail in
    publish sequence.seq_machine selected

let scan_from event_machine items state =
  let current = ref state in
  let total = ref 0 in
  let failure = ref None in
  List.iter
    (fun element ->
      if Option.is_none !failure then begin
        let taken = event_machine.machine_step !current element in
        if taken.next_state < 0 || taken.next_state >= event_machine.machine_states then
          failure := Some invalid_state_fault
        else if taken.emitted_count < 0 then failure := Some negative_count_fault
        else if !total > max_int - taken.emitted_count then failure := Some overflow_fault
        else begin
          total := !total + taken.emitted_count;
          current := taken.next_state
        end
      end)
    items;
  match !failure with Some message -> Error message | None -> Ok (!current, !total)

let validate sequence =
  let event_machine = sequence.seq_machine in
  let states = event_machine.machine_states in
  let root = Measured_tree.measure sequence.seq_tree in
  match Measured_tree.validate sequence.seq_tree with
  | Error message -> Error message
  | Ok () ->
      let items = to_list sequence in
      if Array.length root.summary_next <> states || Array.length root.summary_events <> states then
        Error "the cached contextual summary does not cover every machine state"
      else if root.summary_count <> List.length items || root.summary_count <> length sequence then
        Error "the cached element count disagrees with enumeration"
      else begin
        let failure = ref None in
        for state = 0 to states - 1 do
          if Option.is_none !failure then
            match scan_from event_machine items state with
            | Error message -> failure := Some message
            | Ok (final, total) ->
                if final <> root.summary_next.(state) then
                  failure := Some "a cached outgoing state disagrees with a direct scan"
                else if total <> root.summary_events.(state) then
                  failure := Some "a cached event count disagrees with a direct scan"
        done;
        match !failure with Some message -> Error message | None -> Ok ()
      end
