(** Tests for the persistent sequence that ranks and selects context-dependent events.

    The load-bearing case is that ordered composition of the per-state effect tables is associative
    but {e not} commutative. A port that composes the operands the wrong way round still answers
    correctly for any machine whose emissions ignore the incoming state, so only a machine whose
    emission genuinely depends on that state can catch it, and only when the same two operands are
    concatenated both ways.

    The other is that rank and select read cached tables instead of replaying a prefix: a counting
    machine proves a prefix rank invokes no transition at all and a successful select invokes exactly
    one. *)

open Durable7
open Finger_tree
open Contextual_rank_sequence

let force label = function
  | Ok value -> value
  | Error message -> failwith (Printf.sprintf "%s: %s" label message)

let ok label = function
  | Ok value -> value
  | Error message -> Alcotest.failf "%s: unexpected error %S" label message

let expect_error label = function
  | Error _ -> ()
  | Ok _ -> Alcotest.failf "%s: expected an error" label

let prefix_summary_testable =
  Alcotest.testable
    (fun formatter (value : prefix_summary) ->
      Format.fprintf formatter "(state %d, events %d)" value.final_state value.event_count)
    (fun (left : prefix_summary) right ->
      left.final_state = right.final_state && left.event_count = right.event_count)

let event_location_testable =
  Alcotest.testable
    (fun formatter (value : event_location) ->
      Format.fprintf formatter "(element %d, ordinal %d, %d -> %d, of %d)" value.element_index
        value.event_index_in_element value.state_before value.state_after value.element_event_count)
    (fun (left : event_location) right ->
      left.element_index = right.element_index
      && left.event_index_in_element = right.event_index_in_element
      && left.state_before = right.state_before
      && left.state_after = right.state_after
      && left.element_event_count = right.element_event_count)

(* Two states: outside quotes (0) and inside quotes (1). A comma outside quotes is an event, and a
   quote flips the context without emitting anything. *)
let quoted_comma_step state element =
  match element with
  | '"' -> { next_state = 1 - state; emitted_count = 0 }
  | ',' when state = 0 -> { next_state = state; emitted_count = 1 }
  | _ -> { next_state = state; emitted_count = 0 }

let quoted_comma_machine =
  force "quoted-comma machine"
    (create_machine ~id:"quoted-comma" ~state_count:2 ~transition:quoted_comma_step ())

(* Three states, with transitions that emit zero, one, or two events. *)
let ternary_step state element =
  let next = (state + abs element) mod 3 in
  { next_state = next; emitted_count = next }

let ternary_machine =
  force "ternary machine" (create_machine ~id:"ternary" ~state_count:3 ~transition:ternary_step ())

let counting_calls = ref 0

let counting_machine =
  force "counting machine"
    (create_machine ~id:"counting" ~state_count:2
       ~transition:(fun state element ->
         incr counting_calls;
         {
           next_state = (state + (element land 1)) land 1;
           emitted_count = (if (element lxor state) land 3 = 0 then 1 else 0);
         })
       ())

let invalid_state_machine =
  force "invalid-state machine"
    (create_machine ~id:"invalid-state" ~state_count:1
       ~transition:(fun _ _ -> { next_state = 1; emitted_count = 0 })
       ())

let negative_count_machine =
  force "negative-count machine"
    (create_machine ~id:"negative-count" ~state_count:1
       ~transition:(fun _ _ -> { next_state = 0; emitted_count = -1 })
       ())

let huge_event_machine =
  force "huge-event machine"
    (create_machine ~id:"huge-event" ~state_count:1
       ~transition:(fun _ _ -> { next_state = 0; emitted_count = max_int })
       ())

(** Checks a sequence against an independent list plus a direct machine scan, for every state, every
    prefix boundary, every element index, and every event. *)
let assert_matches_model element_testable label event_machine step sequence model =
  Alcotest.check (Alcotest.list element_testable) (label ^ ": elements") model (to_list sequence);
  Alcotest.(check int) (label ^ ": length") (List.length model) (length sequence);
  Alcotest.(check bool) (label ^ ": is_empty") (List.length model = 0) (is_empty sequence);
  Alcotest.(check (result unit string)) (label ^ ": validate") (Ok ()) (validate sequence);
  Alcotest.(check int)
    (label ^ ": state count")
    (machine_state_count event_machine)
    (state_count sequence);
  for initial_state = 0 to machine_state_count event_machine - 1 do
    let state = ref initial_state in
    let events = ref 0 in
    let expected = ref [] in
    Alcotest.check
      (Alcotest.option prefix_summary_testable)
      (label ^ ": empty prefix")
      (Some { final_state = initial_state; event_count = 0 })
      (evaluate_prefix ~element_count:0 ~initial_state sequence);
    List.iteri
      (fun index element ->
        let taken = step !state element in
        for ordinal = 0 to taken.emitted_count - 1 do
          expected :=
            {
              element_index = index;
              event_index_in_element = ordinal;
              state_before = !state;
              state_after = taken.next_state;
              element_event_count = taken.emitted_count;
            }
            :: !expected
        done;
        state := taken.next_state;
        events := !events + taken.emitted_count;
        Alcotest.check
          (Alcotest.option prefix_summary_testable)
          (Printf.sprintf "%s: prefix %d from %d" label (index + 1) initial_state)
          (Some { final_state = !state; event_count = !events })
          (evaluate_prefix ~element_count:(index + 1) ~initial_state sequence);
        Alcotest.(check (option int))
          (Printf.sprintf "%s: rank %d from %d" label (index + 1) initial_state)
          (Some !events)
          (event_rank ~element_count:(index + 1) ~initial_state sequence);
        Alcotest.check
          (Alcotest.option element_testable)
          (Printf.sprintf "%s: element %d" label index)
          (Some element) (nth index sequence))
      model;
    Alcotest.check
      (Alcotest.option prefix_summary_testable)
      (Printf.sprintf "%s: whole from %d" label initial_state)
      (Some { final_state = !state; event_count = !events })
      (evaluate ~initial_state sequence);
    List.iteri
      (fun event_index location ->
        Alcotest.check
          (Alcotest.option event_location_testable)
          (Printf.sprintf "%s: event %d from %d" label event_index initial_state)
          (Some location)
          (select_event ~event_index ~initial_state sequence))
      (List.rev !expected);
    Alcotest.check
      (Alcotest.option event_location_testable)
      (Printf.sprintf "%s: event past end from %d" label initial_state)
      None
      (select_event ~event_index:(List.length !expected) ~initial_state sequence)
  done

let assert_chars label sequence model =
  assert_matches_model Alcotest.char label quoted_comma_machine quoted_comma_step sequence model

let assert_ints label sequence model =
  assert_matches_model Alcotest.int label ternary_machine ternary_step sequence model

let explode text = List.init (String.length text) (String.get text)
let element_index_of found = Option.map (fun (value : event_location) -> value.element_index) found

let test_quoted_delimiter_ranks_and_selects () =
  let text = ok "build" (of_list quoted_comma_machine (explode "\"a,b\",c,d")) in
  Alcotest.check
    (Alcotest.option prefix_summary_testable)
    "starting outside quotes"
    (Some { final_state = 0; event_count = 2 })
    (evaluate ~initial_state:0 text);
  Alcotest.check
    (Alcotest.option prefix_summary_testable)
    "starting inside quotes"
    (Some { final_state = 1; event_count = 1 })
    (evaluate ~initial_state:1 text);
  Alcotest.(check (option int))
    "rank before the separator" (Some 0)
    (event_rank ~element_count:5 ~initial_state:0 text);
  Alcotest.(check (option int))
    "rank after the separator" (Some 1)
    (event_rank ~element_count:6 ~initial_state:0 text);
  Alcotest.check
    (Alcotest.option event_location_testable)
    "the first unquoted comma"
    (Some
       {
         element_index = 5;
         event_index_in_element = 0;
         state_before = 0;
         state_after = 0;
         element_event_count = 1;
       })
    (select_event ~event_index:0 ~initial_state:0 text);
  Alcotest.(check (option int))
    "the second unquoted comma" (Some 7)
    (element_index_of (select_event ~event_index:1 ~initial_state:0 text));
  Alcotest.check
    (Alcotest.option event_location_testable)
    "no third event" None
    (select_event ~event_index:2 ~initial_state:0 text);
  (* Starting inside a quoted region makes the comma at index 2 the ranked event instead. *)
  Alcotest.(check (option int))
    "context decides which comma ranks" (Some 2)
    (element_index_of (select_event ~event_index:0 ~initial_state:1 text));
  let edited = ok "insert" (insert_at 0 '"' text) in
  Alcotest.check
    (Alcotest.option prefix_summary_testable)
    "the retained version is untouched"
    (Some { final_state = 0; event_count = 2 })
    (evaluate ~initial_state:0 text);
  Alcotest.check
    (Alcotest.option prefix_summary_testable)
    "the successor sees a different context"
    (Some { final_state = 1; event_count = 1 })
    (evaluate ~initial_state:0 edited);
  assert_chars "source" text (explode "\"a,b\",c,d");
  assert_chars "edited" edited (explode "\"\"a,b\",c,d")

let test_composition_is_not_commutative () =
  let quote = ok "quote" (of_list quoted_comma_machine [ '"' ]) in
  let comma = ok "comma" (of_list quoted_comma_machine [ ',' ]) in
  let quote_then_comma = ok "quote then comma" (concat quote comma) in
  let comma_then_quote = ok "comma then quote" (concat comma quote) in
  (* The same two operands reach the same final state but different event totals: the only thing
     that differs is which operand's outgoing state indexes the other's table. A composition that
     commuted would report one count for both. *)
  Alcotest.check
    (Alcotest.option prefix_summary_testable)
    "a quote before the comma hides it"
    (Some { final_state = 1; event_count = 0 })
    (evaluate ~initial_state:0 quote_then_comma);
  Alcotest.check
    (Alcotest.option prefix_summary_testable)
    "a quote after the comma does not"
    (Some { final_state = 1; event_count = 1 })
    (evaluate ~initial_state:0 comma_then_quote);
  assert_chars "quote then comma" quote_then_comma [ '"'; ',' ];
  assert_chars "comma then quote" comma_then_quote [ ','; '"' ];
  (* The same asymmetry through the three-state machine, whose transitions also emit two events. *)
  let low = ok "low" (of_list ternary_machine [ 1 ]) in
  let high = ok "high" (of_list ternary_machine [ 2 ]) in
  Alcotest.check
    (Alcotest.option prefix_summary_testable)
    "one then two"
    (Some { final_state = 0; event_count = 1 })
    (evaluate ~initial_state:0 (ok "low then high" (concat low high)));
  Alcotest.check
    (Alcotest.option prefix_summary_testable)
    "two then one"
    (Some { final_state = 0; event_count = 2 })
    (evaluate ~initial_state:0 (ok "high then low" (concat high low)))

let rec power base exponent = if exponent = 0 then 1 else base * power base (exponent - 1)

let test_exhaustive_small_words () =
  let alphabet = [| '"'; ','; 'x' |] in
  let width = Array.length alphabet in
  for size = 0 to 6 do
    for code = 0 to power width size - 1 do
      let value = ref code in
      let word =
        List.init size (fun _ ->
            let letter = alphabet.(!value mod width) in
            value := !value / width;
            letter)
      in
      let sequence = ok "exhaustive build" (of_list quoted_comma_machine word) in
      assert_chars (Printf.sprintf "word %d/%d" size code) sequence word
    done
  done

let next_random state =
  state := ((!state * 1103515245) + 12345) land 0x3FFFFFFF;
  !state lsr 5

let random_below state bound = next_random state mod bound

let list_insert index value model =
  List.filteri (fun position _ -> position < index) model
  @ (value :: List.filteri (fun position _ -> position >= index) model)

let list_set index value model = List.mapi (fun position old -> if position = index then value else old) model
let list_remove index model = List.filteri (fun position _ -> position <> index) model

let list_sub start count model =
  List.filteri (fun position _ -> position >= start && position < start + count) model

let test_randomized_versions_match_the_model () =
  let state = ref 0x61C72026 in
  let versions = ref [| (ok "seed" (of_list ternary_machine []), []) |] in
  for step = 0 to 299 do
    let parent, model = (!versions).(random_below state (Array.length !versions)) in
    let successor, expected =
      match random_below state 7 with
      | 0 ->
          let item = random_below state 19 - 9 in
          (ok "cons" (cons item parent), item :: model)
      | 1 ->
          let item = random_below state 19 - 9 in
          (ok "snoc" (snoc parent item), model @ [ item ])
      | 2 ->
          let item = random_below state 19 - 9 in
          let index = random_below state (List.length model + 1) in
          (ok "insert" (insert_at index item parent), list_insert index item model)
      | 3 when model <> [] ->
          let index = random_below state (List.length model) in
          let item = random_below state 19 - 9 in
          (ok "set" (set_item index item parent), list_set index item model)
      | 4 when model <> [] ->
          let index = random_below state (List.length model) in
          (ok "remove" (remove_at index parent), list_remove index model)
      | 5 ->
          let start = random_below state (List.length model + 1) in
          let count = random_below state (List.length model - start + 1) in
          (ok "sub" (sub ~start ~length:count parent), list_sub start count model)
      | _ ->
          let suffix, suffix_model = (!versions).(random_below state (Array.length !versions)) in
          if List.length model + List.length suffix_model > 64 then (parent, model)
          else (ok "concat" (concat parent suffix), model @ suffix_model)
    in
    assert_ints (Printf.sprintf "step %d" step) successor expected;
    versions := Array.append !versions [| (successor, expected) |];
    if Array.length !versions > 32 then
      versions := Array.sub !versions 1 (Array.length !versions - 1)
  done;
  (* Every retained branch is still exactly what it was when it was recorded. *)
  Array.iteri
    (fun index (sequence, model) -> assert_ints (Printf.sprintf "retained %d" index) sequence model)
    !versions

let test_split_slice_and_sharing () =
  let source = ok "build" (of_list ternary_machine [ 2; -3; 4; 0 ]) in
  for index = 0 to length source do
    let left, right = ok "split" (split_at index source) in
    Alcotest.(check (list int))
      (Printf.sprintf "split %d rejoins" index)
      [ 2; -3; 4; 0 ]
      (to_list (ok "rejoin" (concat left right)));
    Alcotest.(check int)
      (Printf.sprintf "split %d preserves length" index)
      (length source)
      (length left + length right);
    (* Rejoining and length-summing both stay true when a half's cached summary is wrong, because
       neither reads it. Auditing each half is what actually exercises the recomposed tables — the
       gap that let an unpublished split slip through. *)
    Alcotest.(check (result unit string))
      (Printf.sprintf "split %d left half audits" index)
      (Ok ()) (validate left);
    Alcotest.(check (result unit string))
      (Printf.sprintf "split %d right half audits" index)
      (Ok ()) (validate right);
    Alcotest.(check (list int))
      (Printf.sprintf "split %d left half contents" index)
      (List.filteri (fun position _ -> position < index) [ 2; -3; 4; 0 ])
      (to_list left);
    Alcotest.(check (list int))
      (Printf.sprintf "split %d right half contents" index)
      (List.filteri (fun position _ -> position >= index) [ 2; -3; 4; 0 ])
      (to_list right)
  done;
  let blank = empty ternary_machine in
  Alcotest.(check bool)
    "an empty suffix is not copied" true
    (ok "empty suffix" (concat source blank) == source);
  Alcotest.(check bool)
    "an empty prefix is not copied" true
    (ok "empty prefix" (concat blank source) == source);
  Alcotest.(check bool)
    "a full slice is not copied" true
    (ok "full slice" (sub ~start:0 ~length:(length source) source) == source);
  Alcotest.(check bool)
    "splitting at the start shares the receiver" true
    (snd (ok "split at start" (split_at 0 source)) == source);
  Alcotest.(check bool)
    "splitting at the end shares the receiver" true
    (fst (ok "split at end" (split_at (length source) source)) == source);
  (* A transition emitting more than one event has to occur, or select's ordinal goes untested. *)
  Alcotest.(check bool)
    "the ternary machine emits multi-event transitions" true
    (List.exists
       (fun index ->
         let after = Option.get (event_rank ~element_count:(index + 1) ~initial_state:0 source) in
         let before = Option.get (event_rank ~element_count:index ~initial_state:0 source) in
         after - before > 1)
       (List.init (length source) Fun.id))

let test_boundaries_and_misses () =
  let source = ok "build" (of_list ternary_machine [ 2; -3; 4; 0 ]) in
  let blank = empty ternary_machine in
  Alcotest.check
    (Alcotest.option prefix_summary_testable)
    "a state past the machine range misses" None (evaluate ~initial_state:3 source);
  Alcotest.check
    (Alcotest.option prefix_summary_testable)
    "a negative state misses" None
    (evaluate ~initial_state:(-1) source);
  Alcotest.check
    (Alcotest.option prefix_summary_testable)
    "a prefix past the end misses" None
    (evaluate_prefix ~element_count:(length source + 1) ~initial_state:0 source);
  Alcotest.check
    (Alcotest.option prefix_summary_testable)
    "a negative prefix misses" None
    (evaluate_prefix ~element_count:(-1) ~initial_state:0 source);
  Alcotest.(check (option int))
    "a rank past the end misses" None
    (event_rank ~element_count:(length source + 1) ~initial_state:0 source);
  Alcotest.check
    (Alcotest.option event_location_testable)
    "select from an invalid state misses" None
    (select_event ~event_index:0 ~initial_state:3 source);
  Alcotest.check
    (Alcotest.option event_location_testable)
    "a negative rank misses" None
    (select_event ~event_index:(-1) ~initial_state:0 source);
  Alcotest.(check (option int)) "an index past the end misses" None (nth (length source) source);
  Alcotest.(check (option int)) "a negative index misses" None (nth (-1) source);
  expect_error "insert past the boundary" (insert_at (length source + 1) 0 source);
  expect_error "insert before the start" (insert_at (-1) 0 source);
  expect_error "replace past the end" (set_item (length source) 0 source);
  expect_error "remove past the end" (remove_at (length source) source);
  expect_error "split past the boundary" (split_at (length source + 1) source);
  expect_error "a slice past the end" (sub ~start:1 ~length:(length source) source);
  expect_error "a negative slice length" (sub ~start:0 ~length:(-1) source);
  (* An empty sequence keeps its machine, so it stays usable rather than becoming inert. *)
  Alcotest.(check bool) "the empty sequence is empty" true (is_empty blank);
  Alcotest.check
    (Alcotest.option prefix_summary_testable)
    "the empty summary is the identity"
    (Some { final_state = 2; event_count = 0 })
    (evaluate ~initial_state:2 blank);
  Alcotest.check
    (Alcotest.option event_location_testable)
    "the empty sequence has no events" None
    (select_event ~event_index:0 ~initial_state:0 blank);
  assert_ints "empty" blank [];
  let single = ok "snoc onto empty" (snoc blank 5) in
  assert_ints "singleton" single [ 5 ];
  let emptied = ok "remove the only element" (remove_at 0 single) in
  Alcotest.(check bool) "removal reaches empty" true (is_empty emptied);
  assert_ints "regrown from empty" (ok "regrow" (cons 7 emptied)) [ 7 ]

let test_machine_identity_gates_concatenation () =
  expect_error "a blank id" (create_machine ~id:"  " ~state_count:2 ~transition:quoted_comma_step ());
  expect_error "a machine with no states"
    (create_machine ~id:"stateless" ~state_count:0 ~transition:quoted_comma_step ());
  expect_error "a machine with a negative state count"
    (create_machine ~id:"negative" ~state_count:(-1) ~transition:quoted_comma_step ());
  Alcotest.(check string) "the retained id" "quoted-comma" (machine_id quoted_comma_machine);
  Alcotest.(check int) "the retained state count" 2 (machine_state_count quoted_comma_machine);
  Alcotest.(check string)
    "a sequence retains its machine" "ternary"
    (machine_id (machine (empty ternary_machine)));
  let twin =
    force "twin" (create_machine ~id:"quoted-comma" ~state_count:2 ~transition:quoted_comma_step ())
  in
  let other =
    force "other" (create_machine ~id:"other-comma" ~state_count:2 ~transition:quoted_comma_step ())
  in
  let left = ok "left" (of_list quoted_comma_machine [ ','; 'x' ]) in
  let same = ok "same id" (of_list twin [ ','; 'x' ]) in
  let different = ok "different id" (of_list other [ ','; 'x' ]) in
  (* Concatenation is gated on the retained id, exactly as a measure policy's id gates a measured
     tree: two distinct handles carrying the same id denote the same machine. *)
  assert_chars "same-id concatenation" (ok "join twins" (concat left same)) [ ','; 'x'; ','; 'x' ];
  expect_error "different-id concatenation" (concat left different);
  expect_error "different-id concatenation, reversed" (concat different left)

let test_contract_violations_publish_nothing () =
  let invalid = empty invalid_state_machine in
  expect_error "an out-of-range next state" (snoc invalid 1);
  expect_error "an out-of-range next state in bulk" (of_list invalid_state_machine [ 1 ]);
  Alcotest.(check bool) "the retained version is untouched" true (is_empty invalid);
  let negative = empty negative_count_machine in
  expect_error "a negative event count" (cons 1 negative);
  expect_error "a negative event count in bulk" (of_list negative_count_machine [ 1; 2 ]);
  Alcotest.(check bool) "the retained version is still empty" true (is_empty negative);
  let huge = ok "one huge element" (of_list huge_event_machine [ 1 ]) in
  Alcotest.check
    (Alcotest.option prefix_summary_testable)
    "one element is representable"
    (Some { final_state = 0; event_count = max_int })
    (evaluate ~initial_state:0 huge);
  expect_error "a second element overflows the total" (snoc huge 2);
  expect_error "concatenating overflows the total" (concat huge huge);
  expect_error "inserting overflows the total" (insert_at 0 2 huge);
  (* The rejected successors published nothing: the retained version is unchanged and still valid. *)
  Alcotest.(check (list int)) "the retained version is unchanged" [ 1 ] (to_list huge);
  Alcotest.check
    (Alcotest.option prefix_summary_testable)
    "and still answers"
    (Some { final_state = 0; event_count = max_int })
    (evaluate ~initial_state:0 huge);
  Alcotest.(check (result unit string)) "and still validates" (Ok ()) (validate huge)

let test_cached_queries_do_not_rescan () =
  let size = 2049 in
  let sequence = ok "build" (of_list counting_machine (List.init size Fun.id)) in
  counting_calls := 0;
  let boundary = ref 0 in
  while !boundary <= size do
    let element_count = !boundary in
    ignore
      (Option.get (event_rank ~element_count ~initial_state:(element_count land 1) sequence) : int);
    ignore
      (Option.get (evaluate_prefix ~element_count ~initial_state:0 sequence) : prefix_summary);
    boundary := element_count + 17
  done;
  Alcotest.(check int) "a cached prefix rank invokes no transition" 0 !counting_calls;
  let total = (Option.get (evaluate ~initial_state:0 sequence)).event_count in
  Alcotest.(check bool) "the counting machine emits events" true (total > 0);
  let successful = ref 0 in
  let event_index = ref 0 in
  while !event_index < total do
    Alcotest.(check bool)
      (Printf.sprintf "event %d is found" !event_index)
      true
      (Option.is_some (select_event ~event_index:!event_index ~initial_state:0 sequence));
    incr successful;
    event_index := !event_index + 31
  done;
  Alcotest.(check int)
    "each successful select invokes exactly one transition" !successful !counting_calls

let tests =
  [
    Alcotest.test_case "quoted delimiters rank and select" `Quick
      test_quoted_delimiter_ranks_and_selects;
    Alcotest.test_case "composition is not commutative" `Quick test_composition_is_not_commutative;
    Alcotest.test_case "exhaustive small words" `Quick test_exhaustive_small_words;
    Alcotest.test_case "randomized versions match the model" `Quick
      test_randomized_versions_match_the_model;
    Alcotest.test_case "split, slice, and sharing" `Quick test_split_slice_and_sharing;
    Alcotest.test_case "boundaries and misses" `Quick test_boundaries_and_misses;
    Alcotest.test_case "machine identity gates concatenation" `Quick
      test_machine_identity_gates_concatenation;
    Alcotest.test_case "contract violations publish nothing" `Quick
      test_contract_violations_publish_nothing;
    Alcotest.test_case "cached queries do not rescan" `Quick test_cached_queries_do_not_rescan;
  ]
