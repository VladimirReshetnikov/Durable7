(** Tests for the persistent queue slice over an append ancestry path.

    Two properties are load-bearing here and nowhere else. The first is the anchored-empty rule: an
    empty value keeps the node immediately before its window, so a queue drained from the front and
    one drained from the back must keep {e different} anchors while behaving identically, which the
    depth of the next published node exposes. The second is the query discipline: the whole point of
    the structure is which operations reach the arena, so the boundary cases assert exact
    [ancestor_query_count] deltas rather than ceilings — an implementation that queried on every
    suffix slice, take-all, drop-zero, or split at the length would still return correct answers and
    would still pass every model test. *)

open Durable7
open Finger_tree
module Arena = Incremental_ancestor
module Queue = Ancestral_slice_queue

let ok label = function
  | Ok value -> value
  | Error message -> Alcotest.failf "%s: unexpected error %S" label message

let expect_error label = function
  | Error _ -> ()
  | Ok _ -> Alcotest.failf "%s: expected an error" label

(** The elements of [values] whose rank lies in the half-open range. *)
let window values low high = List.filteri (fun index _ -> index >= low && index < high) values

(** Every read the reference test helper checks, against one expected model. *)
let check_state label queue expected =
  let count = List.length expected in
  Alcotest.(check int) (label ^ ": length") count (Queue.length queue);
  Alcotest.(check bool) (label ^ ": is_empty") (count = 0) (Queue.is_empty queue);
  Alcotest.(check (list int)) (label ^ ": to_list") expected (Queue.to_list queue);
  List.iteri
    (fun index value ->
      Alcotest.(check (option int))
        (Printf.sprintf "%s: nth %d" label index)
        (Some value) (Queue.nth index queue))
    expected;
  Alcotest.(check (option int)) (label ^ ": nth past the end") None (Queue.nth count queue);
  Alcotest.(check (option int)) (label ^ ": negative nth") None (Queue.nth (-1) queue);
  Alcotest.(check (option int))
    (label ^ ": first")
    (match expected with [] -> None | value :: _ -> Some value)
    (Queue.first queue);
  Alcotest.(check (option int))
    (label ^ ": last")
    (if count = 0 then None else Some (List.nth expected (count - 1)))
    (Queue.last queue);
  Alcotest.(check bool)
    (label ^ ": remove_first")
    (count <> 0)
    (Option.is_some (Queue.remove_first queue));
  Alcotest.(check bool)
    (label ^ ": remove_last")
    (count <> 0)
    (Option.is_some (Queue.remove_last queue));
  Alcotest.(check (result unit string)) (label ^ ": validate") (Ok ()) (Queue.validate queue)

let build arena values =
  List.fold_left (fun queue value -> ok "seed" (Queue.add_last value queue)) (Queue.of_arena arena)
    values

let rec drain_front queue =
  match Queue.remove_first queue with None -> queue | Some rest -> drain_front rest

let rec drain_back queue =
  match Queue.remove_last queue with None -> queue | Some rest -> drain_back rest

let test_endpoint_contracts () =
  let empty = Queue.empty () in
  check_state "empty" empty [];
  Alcotest.(check bool)
    "an empty queue pops no front" true
    (Option.is_none (Queue.pop_front empty));
  Alcotest.(check bool) "an empty queue pops no back" true (Option.is_none (Queue.pop_back empty));
  let singleton = ok "singleton" (Queue.add_last 7 empty) in
  check_state "singleton" singleton [ 7 ];
  check_state "the empty source is retained" empty [];
  let pair = ok "pair" (Queue.add_last 8 singleton) in
  check_state "pair" pair [ 7; 8 ];
  check_state "the singleton source is retained" singleton [ 7 ];
  (match Queue.pop_front pair with
  | Some (value, rest) ->
      Alcotest.(check int) "popped front element" 7 value;
      check_state "the front pop remainder" rest [ 8 ]
  | None -> Alcotest.fail "a non-empty queue pops from the front");
  (match Queue.pop_back pair with
  | Some (rest, value) ->
      Alcotest.(check int) "popped back element" 8 value;
      check_state "the back pop remainder" rest [ 7 ]
  | None -> Alcotest.fail "a non-empty queue pops from the back");
  check_state "the popped source is retained" pair [ 7; 8 ];
  (* A stored [None] is a present element; absence is reported by the outer option. *)
  let optional = ok "stored option" (Queue.of_list [ None; Some 3 ]) in
  Alcotest.(check (option (option int)))
    "a stored None is present" (Some None) (Queue.first optional);
  Alcotest.(check (option (option int))) "a stored Some" (Some (Some 3)) (Queue.last optional);
  Alcotest.(check (list (option int))) "stored option order" [ None; Some 3 ]
    (Queue.to_list optional)

let test_anchored_empty_rule () =
  let length = 32 in
  let values = List.init length Fun.id in
  let arena = Arena.create () in
  let source = build arena values in
  let front_empty = drain_front source in
  let back_empty = drain_back source in
  check_state "the front-drained queue" front_empty [];
  check_state "the back-drained queue" back_empty [];
  (* Both are empty and both append to exactly the new element, yet their anchors differ: the
     front-drained value still retains the original tail, while the back-drained one has walked its
     tail all the way back to the arena's bottom node. The depth of the node each append publishes
     is what makes that difference observable. *)
  let published = Arena.published_node_count arena in
  let front_extended = ok "front append" (Queue.add_last (-1) front_empty) in
  Alcotest.(check int)
    "the front append publishes one node" (published + 1)
    (Arena.published_node_count arena);
  let back_extended = ok "back append" (Queue.add_last (-2) back_empty) in
  Alcotest.(check int)
    "the back append publishes one node" (published + 2)
    (Arena.published_node_count arena);
  check_state "appending to the front-drained empty" front_extended [ -1 ];
  check_state "appending to the back-drained empty" back_extended [ -2 ];
  Alcotest.(check (result int string))
    "the front anchor keeps the drained tail's depth" (Ok length)
    (Arena.depth_of arena (published + 1));
  Alcotest.(check (result int string))
    "the back anchor has walked to the arena bottom" (Ok 0)
    (Arena.depth_of arena (published + 2));
  Alcotest.(check (result int string))
    "the back anchor is the bottom node itself" (Ok Arena.bottom)
    (Arena.parent_of arena (published + 2));
  (* Draining a singleton from either end anchors it immediately before its own window. *)
  let singleton = ok "singleton" (Queue.take 1 source) in
  check_state "front-drained singleton"
    (ok "front" (Queue.add_last 8 (Option.get (Queue.remove_first singleton))))
    [ 8 ];
  check_state "back-drained singleton"
    (ok "back" (Queue.add_last 9 (Option.get (Queue.remove_last singleton))))
    [ 9 ];
  check_state "the source is retained" source values

let test_every_window_matches_the_model () =
  let length = 65 in
  let values = List.init length Fun.id in
  let source = ok "source" (Queue.of_list values) in
  for start = 0 to length do
    for count = 0 to length - start do
      let label = Printf.sprintf "window %d+%d" start count in
      let sliced = ok label (Queue.slice ~start ~length:count source) in
      check_state label sliced (window values start (start + count));
      if count = 0 then begin
        (* The anchored-empty rule: appending to any empty window yields exactly the new elements
           and never resurrects the discarded prefix. *)
        let marker = -start - 1 in
        let extended = ok label (Queue.add_last marker sliced) in
        let extended = ok label (Queue.add_last (10_000 + start) extended) in
        check_state (label ^ " extended") extended [ marker; 10_000 + start ];
        check_state (label ^ " retained") sliced []
      end
    done
  done;
  check_state "the whole window" (ok "whole" (Queue.slice ~start:0 ~length source)) values;
  check_state "taking the whole length" (ok "take" (Queue.take length source)) values;
  check_state "dropping nothing" (ok "drop" (Queue.drop 0 source)) values;
  check_state "the source is retained" source values

let test_boundaries_partition_the_source () =
  let length = 129 in
  let values = List.init length Fun.id in
  let source = ok "source" (Queue.of_list values) in
  for position = 0 to length do
    let label = Printf.sprintf "boundary %d" position in
    let prefix = ok label (Queue.take position source) in
    let suffix = ok label (Queue.drop position source) in
    let left, right = ok label (Queue.split_at position source) in
    let expected_prefix = window values 0 position in
    let expected_suffix = window values position length in
    check_state (label ^ " prefix") prefix expected_prefix;
    check_state (label ^ " suffix") suffix expected_suffix;
    check_state (label ^ " split left") left expected_prefix;
    check_state (label ^ " split right") right expected_suffix;
    (* Both halves stay real ancestry slices and start independent branches. *)
    let marker = -position - 1 in
    check_state (label ^ " prefix branch")
      (ok label (Queue.add_last marker prefix))
      (expected_prefix @ [ marker ]);
    check_state (label ^ " suffix branch")
      (ok label (Queue.add_last marker suffix))
      (expected_suffix @ [ marker ])
  done;
  check_state "the source is retained" source values

let test_whole_window_operations_share_the_tail () =
  let values = List.init 8 Fun.id in
  let source = ok "source" (Queue.of_list values) in
  Alcotest.(check bool)
    "taking the whole length returns the source value" true
    (ok "take" (Queue.take 8 source) == source);
  Alcotest.(check bool)
    "dropping nothing returns the source value" true
    (ok "drop" (Queue.drop 0 source) == source);
  Alcotest.(check bool)
    "the whole window returns the source value" true
    (ok "slice" (Queue.slice ~start:0 ~length:8 source) == source);
  let left, right = ok "split at the length" (Queue.split_at 8 source) in
  Alcotest.(check bool) "a split at the length reuses the source prefix" true (left == source);
  check_state "a split at the length empties the suffix" right [];
  let left, right = ok "split at zero" (Queue.split_at 0 source) in
  check_state "a split at zero empties the prefix" left [];
  Alcotest.(check bool) "a split at zero reuses the source suffix" true (right == source)

let test_query_discipline () =
  let arena = Arena.create () in
  let queue = build arena (List.init 16 Fun.id) in
  let check_delta label ~adds ~queries action =
    let before = Arena.statistics arena in
    action ();
    let after = Arena.statistics arena in
    Alcotest.(check int)
      (label ^ ": published nodes")
      (before.Arena.add_leaf_count + adds)
      after.Arena.add_leaf_count;
    Alcotest.(check int)
      (label ^ ": ancestor queries")
      (before.Arena.ancestor_query_count + queries)
      after.Arena.ancestor_query_count
  in
  check_delta "cached reads" ~adds:0 ~queries:0 (fun () ->
      Alcotest.(check int) "length" 16 (Queue.length queue);
      Alcotest.(check bool) "is_empty" false (Queue.is_empty queue);
      Alcotest.(check (option int)) "last" (Some 15) (Queue.last queue);
      Alcotest.(check (option int)) "nth at the last visible rank" (Some 15) (Queue.nth 15 queue));
  check_delta "front read" ~adds:0 ~queries:1 (fun () ->
      Alcotest.(check (option int)) "first" (Some 0) (Queue.first queue));
  check_delta "interior read" ~adds:0 ~queries:1 (fun () ->
      Alcotest.(check (option int)) "nth" (Some 7) (Queue.nth 7 queue));
  check_delta "endpoint removals" ~adds:0 ~queries:0 (fun () ->
      Alcotest.(check int)
        "remove_first" 15
        (Queue.length (Option.get (Queue.remove_first queue)));
      Alcotest.(check int) "remove_last" 15 (Queue.length (Option.get (Queue.remove_last queue))));
  check_delta "drop" ~adds:0 ~queries:0 (fun () ->
      Alcotest.(check int) "drop" 9 (Queue.length (ok "drop" (Queue.drop 7 queue))));
  check_delta "take" ~adds:0 ~queries:1 (fun () ->
      Alcotest.(check int) "take" 7 (Queue.length (ok "take" (Queue.take 7 queue))));
  check_delta "interior window" ~adds:0 ~queries:1 (fun () ->
      Alcotest.(check int)
        "slice" 5
        (Queue.length (ok "slice" (Queue.slice ~start:3 ~length:5 queue))));
  check_delta "suffix window" ~adds:0 ~queries:0 (fun () ->
      (* A suffix keeps the retained tail, so it makes no ancestor query. *)
      Alcotest.(check int)
        "slice" 13
        (Queue.length (ok "slice" (Queue.slice ~start:3 ~length:13 queue))));
  check_delta "interior split" ~adds:0 ~queries:1 (fun () ->
      let left, right = ok "split" (Queue.split_at 7 queue) in
      Alcotest.(check int) "left" 7 (Queue.length left);
      Alcotest.(check int) "right" 9 (Queue.length right));
  check_delta "split at zero" ~adds:0 ~queries:1 (fun () ->
      (* Not query-free: the anchored-empty rule makes the empty prefix retain the node at
         low_depth - 1, and only an ancestor query can name that node from the retained tail. *)
      let left, right = ok "split" (Queue.split_at 0 queue) in
      Alcotest.(check int) "left" 0 (Queue.length left);
      Alcotest.(check int) "right" 16 (Queue.length right);
      Alcotest.(check (option int)) "the suffix keeps the tail" (Some 15) (Queue.last right));
  check_delta "split at the length" ~adds:0 ~queries:0 (fun () ->
      let left, right = ok "split" (Queue.split_at 16 queue) in
      Alcotest.(check int) "left" 16 (Queue.length left);
      Alcotest.(check int) "right" 0 (Queue.length right));
  check_delta "front pop" ~adds:0 ~queries:1 (fun () ->
      let value, rest = Option.get (Queue.pop_front queue) in
      Alcotest.(check int) "popped" 0 value;
      Alcotest.(check int) "remainder" 15 (Queue.length rest));
  check_delta "back pop" ~adds:0 ~queries:0 (fun () ->
      let rest, value = Option.get (Queue.pop_back queue) in
      Alcotest.(check int) "popped" 15 value;
      Alcotest.(check int) "remainder" 15 (Queue.length rest));
  check_delta "enumeration" ~adds:0 ~queries:0 (fun () ->
      (* Enumeration follows parent links, so it never queries however long the window is. *)
      Alcotest.(check (list int)) "to_list" (List.init 16 Fun.id) (Queue.to_list queue));
  check_delta "audit" ~adds:0 ~queries:0 (fun () ->
      Alcotest.(check (result unit string)) "validate" (Ok ()) (Queue.validate queue));
  check_delta "append" ~adds:1 ~queries:0 (fun () ->
      let appended = ok "append" (Queue.add_last 16 queue) in
      Alcotest.(check int) "appended length" 17 (Queue.length appended);
      Alcotest.(check (option int)) "appended last" (Some 16) (Queue.last appended));
  let singleton = ok "singleton" (Queue.take 1 queue) in
  check_delta "singleton front" ~adds:0 ~queries:0 (fun () ->
      Alcotest.(check (option int)) "first" (Some 0) (Queue.first singleton);
      Alcotest.(check (option int)) "nth" (Some 0) (Queue.nth 0 singleton));
  check_delta "taking nothing from a non-empty queue" ~adds:0 ~queries:1 (fun () ->
      Alcotest.(check int) "take" 0 (Queue.length (ok "take" (Queue.take 0 queue))));
  let drained = ok "drained" (Queue.drop 16 queue) in
  check_delta "the boundaries of an empty queue" ~adds:0 ~queries:0 (fun () ->
      (* On an empty queue both window boundaries coincide, so its split at zero {e is} the split at
         its length and no ancestor query is needed to name the anchor. *)
      Alcotest.(check int) "take" 0 (Queue.length (ok "take" (Queue.take 0 drained)));
      Alcotest.(check int) "drop" 0 (Queue.length (ok "drop" (Queue.drop 0 drained)));
      Alcotest.(check int)
        "slice" 0
        (Queue.length (ok "slice" (Queue.slice ~start:0 ~length:0 drained)));
      let left, right = ok "split" (Queue.split_at 0 drained) in
      Alcotest.(check int) "left" 0 (Queue.length left);
      Alcotest.(check int) "right" 0 (Queue.length right))

let test_out_of_range_positions_are_rejected () =
  let source = ok "source" (Queue.of_list [ 10; 20; 30 ]) in
  Alcotest.(check (option int)) "nth past the end" None (Queue.nth 3 source);
  Alcotest.(check (option int)) "negative nth" None (Queue.nth (-1) source);
  expect_error "take past the end" (Queue.take 4 source);
  expect_error "negative take" (Queue.take (-1) source);
  expect_error "drop past the end" (Queue.drop 4 source);
  expect_error "negative drop" (Queue.drop (-1) source);
  expect_error "a window past the end" (Queue.slice ~start:2 ~length:2 source);
  expect_error "a window starting past the end" (Queue.slice ~start:4 ~length:0 source);
  expect_error "a negative window start" (Queue.slice ~start:(-1) ~length:1 source);
  expect_error "a negative window length" (Queue.slice ~start:0 ~length:(-1) source);
  expect_error "a split past the end" (Queue.split_at 4 source);
  expect_error "a negative split" (Queue.split_at (-1) source);
  let empty = Queue.empty () in
  expect_error "taking from an empty queue" (Queue.take 1 empty);
  expect_error "dropping from an empty queue" (Queue.drop 1 empty);
  expect_error "a window on an empty queue" (Queue.slice ~start:0 ~length:1 empty);
  check_state "the source is retained" source [ 10; 20; 30 ]

let test_retained_branches_stay_independent () =
  let values = List.init 200 Fun.id in
  let source = ok "source" (Queue.of_list values) in
  let middle = ok "middle" (Queue.slice ~start:50 ~length:100 source) in
  let branch label queue marker = ok label (Queue.add_last marker queue) in
  check_state "appended branch" (branch "append" middle (-1)) (window values 50 150 @ [ -1 ]);
  check_state "twice-appended branch"
    (branch "append" (branch "append" middle (-2)) (-3))
    (window values 50 150 @ [ -2; -3 ]);
  check_state "front-removed branch"
    (branch "front" (Option.get (Queue.remove_first middle)) (-4))
    (window values 51 150 @ [ -4 ]);
  check_state "back-removed branch"
    (branch "back" (Option.get (Queue.remove_last middle)) (-5))
    (window values 50 149 @ [ -5 ]);
  check_state "prefix branch"
    (branch "prefix" (ok "take" (Queue.take 40 middle)) (-6))
    (window values 50 90 @ [ -6 ]);
  check_state "suffix branch"
    (branch "suffix" (ok "drop" (Queue.drop 60 middle)) (-7))
    (window values 110 150 @ [ -7 ]);
  check_state "empty-window branch"
    (branch "empty" (ok "slice" (Queue.slice ~start:30 ~length:0 middle)) (-8))
    [ -8 ];
  check_state "the interior slice is retained" middle (window values 50 150);
  check_state "the source is retained" source values

let test_arenas_are_shared_or_private () =
  let arena = Arena.create () in
  let first = ok "first" (Queue.add_last 1 (Queue.of_arena arena)) in
  let second = ok "second" (Queue.add_last 2 (Queue.of_arena arena)) in
  check_state "the first shared branch" first [ 1 ];
  check_state "the second shared branch" second [ 2 ];
  Alcotest.(check int)
    "one shared arena holds both appends" 2 (Arena.published_node_count arena);
  (* Each convenience factory owns a private arena, so unrelated queues share no history. *)
  let private_first = ok "private" (Queue.add_last 1 (Queue.empty ())) in
  let private_second = ok "private" (Queue.of_list [ 2; 3 ]) in
  check_state "the first private queue" private_first [ 1 ];
  check_state "the second private queue" private_second [ 2; 3 ];
  check_state "an empty list builds an empty queue" (ok "empty" (Queue.of_list [])) []

let test_randomized_history_matches_the_model () =
  let state = ref 20_250_805 in
  let next_below bound =
    state := ((!state * 1_103_515_245) + 12_345) land 0x3FFF_FFFF;
    !state mod bound
  in
  let histories = ref [ (ok "seed" (Queue.of_list []), ([] : int list)) ] in
  let retained = ref 1 in
  for step = 0 to 599 do
    let source, model = List.nth !histories (next_below !retained) in
    let length = List.length model in
    let result, expected =
      match next_below 8 with
      | 1 when length <> 0 -> (Option.get (Queue.remove_first source), window model 1 length)
      | 2 when length <> 0 -> (Option.get (Queue.remove_last source), window model 0 (length - 1))
      | 3 ->
          let count = next_below (length + 1) in
          (ok "take" (Queue.take count source), window model 0 count)
      | 4 ->
          let count = next_below (length + 1) in
          (ok "drop" (Queue.drop count source), window model count length)
      | 5 ->
          let start = next_below (length + 1) in
          let count = next_below (length - start + 1) in
          (ok "slice" (Queue.slice ~start ~length:count source), window model start (start + count))
      | 6 ->
          let position = next_below (length + 1) in
          let left, right = ok "split" (Queue.split_at position source) in
          if next_below 2 = 0 then (left, window model 0 position)
          else (right, window model position length)
      | 7 when length <> 0 ->
          let value, rest = Option.get (Queue.pop_front source) in
          Alcotest.(check int) "popped front" (List.nth model 0) value;
          (rest, window model 1 length)
      | _ -> (ok "append" (Queue.add_last step source), model @ [ step ])
    in
    check_state (Printf.sprintf "step %d source" step) source model;
    check_state (Printf.sprintf "step %d result" step) result expected;
    histories := (result, expected) :: !histories;
    incr retained
  done;
  List.iteri
    (fun index (queue, model) ->
      if index mod 37 = 0 then check_state (Printf.sprintf "retained %d" index) queue model)
    !histories

let tests : unit Alcotest.test_case list =
  [
    Alcotest.test_case "endpoint contracts" `Quick test_endpoint_contracts;
    Alcotest.test_case "empty values stay anchored" `Quick test_anchored_empty_rule;
    Alcotest.test_case "every window matches the model" `Quick test_every_window_matches_the_model;
    Alcotest.test_case "boundaries partition the source" `Quick
      test_boundaries_partition_the_source;
    Alcotest.test_case "whole-window operations share the tail" `Quick
      test_whole_window_operations_share_the_tail;
    Alcotest.test_case "operations respect the query discipline" `Quick test_query_discipline;
    Alcotest.test_case "out-of-range positions are rejected" `Quick
      test_out_of_range_positions_are_rejected;
    Alcotest.test_case "retained branches stay independent" `Quick
      test_retained_branches_stay_independent;
    Alcotest.test_case "arenas are shared or private" `Quick test_arenas_are_shared_or_private;
    Alcotest.test_case "randomized history matches the model" `Quick
      test_randomized_history_matches_the_model;
  ]
