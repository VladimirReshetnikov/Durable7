(** Tests for the bilateral ancestral deque.

    The load-bearing claims here are the level-ancestor query ceilings, and a ceiling assertion
    passes vacuously if the implementation quietly stops using the arena at all. Every ceiling is
    therefore pinned as an {e exact} profile: the full nine-row slice table, the split row, the
    indexing row that shows exactly which four positions are cached, and the removal rows that show
    where the count-of-two shortcut takes over. An implementation that spends one query too many
    fails, and so does one that spends one too few because it silently rebuilt the deque. *)

open Durable7
open Finger_tree
module Deque = Bilateral_ancestral_deque

let ok label = function
  | Ok value -> value
  | Error message -> Alcotest.failf "%s: unexpected error %S" label message

let some label = function
  | Some value -> value
  | None -> Alcotest.failf "%s: unexpected absence" label

let expect_error label = function
  | Error _ -> ()
  | Ok _ -> Alcotest.failf "%s: expected an error" label

let expect_none label = function
  | None -> ()
  | Some _ -> Alcotest.failf "%s: expected an absent result" label

let query_count arena = Incremental_ancestor.((statistics arena).ancestor_query_count)

(** Runs [body] and asserts it made exactly [expected] level-ancestor queries. *)
let queries arena expected label body =
  let before = query_count arena in
  let result = body () in
  Alcotest.(check int) (label ^ ": level-ancestor queries") expected (query_count arena - before);
  result

let spends arena expected label body = ignore (queries arena expected label body)
let push_first value deque = ok "add_first" (Deque.add_first value deque)
let push_last value deque = ok "add_last" (Deque.add_last value deque)

let sublist start length values =
  List.filteri (fun index _ -> index >= start && index < start + length) values

(** The canonical eight-value deque holding one through eight with four values on each arm, so every
    cross-arm case of the slice, split, and indexing profiles is exercised. *)
let eight_value_deque root =
  root |> push_first 4 |> push_first 3 |> push_first 2 |> push_first 1 |> push_last 5 |> push_last 6
  |> push_last 7 |> push_last 8

(** Asserts the whole observable contract of one handle against a list oracle. *)
let assert_deque label expected deque =
  let length = List.length expected in
  Alcotest.(check int) (label ^ ": count") length (Deque.count deque);
  Alcotest.(check bool) (label ^ ": emptiness") (length = 0) (Deque.is_empty deque);
  Alcotest.(check (list int)) (label ^ ": values") expected (Deque.to_list deque);
  Alcotest.(check (list int)) (label ^ ": sequence") expected (List.of_seq (Deque.to_seq deque));
  let visited = ref [] in
  Deque.iter (fun value -> visited := value :: !visited) deque;
  Alcotest.(check (list int)) (label ^ ": iteration") expected (List.rev !visited);
  Alcotest.(check (list int))
    (label ^ ": fold")
    expected
    (List.rev (Deque.fold_left (fun state value -> value :: state) [] deque));
  List.iteri
    (fun index value ->
      Alcotest.(check (option int))
        (Printf.sprintf "%s: nth %d" label index)
        (Some value) (Deque.nth index deque))
    expected;
  Alcotest.(check (option int)) (label ^ ": nth at the count") None (Deque.nth length deque);
  Alcotest.(check (option int)) (label ^ ": nth below zero") None (Deque.nth (-1) deque);
  Alcotest.(check (option int)) (label ^ ": first") (List.nth_opt expected 0) (Deque.first deque);
  Alcotest.(check (option int))
    (label ^ ": last")
    (if length = 0 then None else List.nth_opt expected (length - 1))
    (Deque.last deque);
  let { Deque.count = audited; left_count; right_count } =
    ok (label ^ ": validate") (Deque.validate deque)
  in
  Alcotest.(check int) (label ^ ": audited count") length audited;
  Alcotest.(check int) (label ^ ": audited interval counts") length (left_count + right_count)

let test_empty_handles () =
  let deque = Deque.create () in
  assert_deque "empty" [] deque;
  expect_none "remove_first" (Deque.remove_first deque);
  expect_none "remove_last" (Deque.remove_last deque);
  expect_none "pop_first" (Deque.pop_first deque);
  expect_none "pop_last" (Deque.pop_last deque);
  List.iter
    (fun (name, produced) ->
      assert_deque name [] produced;
      Alcotest.(check bool)
        (name ^ ": shares storage")
        true
        (Deque.shares_storage_with deque produced))
    [
      ("clear", Deque.clear deque);
      ("reverse", Deque.reverse deque);
      ("take nothing", ok "take" (Deque.take 0 deque));
      ("drop nothing", ok "drop" (Deque.drop 0 deque));
      ("the whole-range slice", ok "slice" (Deque.slice ~index:0 ~count:0 deque));
    ];
  let prefix, suffix = ok "split_at 0" (Deque.split_at 0 deque) in
  Alcotest.(check bool)
    "the prefix of an empty split shares storage" true
    (Deque.shares_storage_with deque prefix);
  Alcotest.(check bool)
    "the suffix of an empty split shares storage" true
    (Deque.shares_storage_with deque suffix);
  expect_error "oversized prefix" (Deque.take 1 deque);
  expect_error "negative prefix" (Deque.take (-1) deque);
  expect_error "oversized suffix" (Deque.drop 1 deque);
  expect_error "negative suffix" (Deque.drop (-1) deque);
  expect_error "oversized boundary" (Deque.split_at 1 deque);
  expect_error "negative boundary" (Deque.split_at (-1) deque);
  expect_error "oversized slice count" (Deque.slice ~index:0 ~count:1 deque);
  expect_error "oversized slice index" (Deque.slice ~index:1 ~count:0 deque);
  assert_deque "of_list on nothing" [] (ok "of_list" (Deque.of_list []))

let test_endpoint_edits () =
  let root = Deque.create () in
  let one = push_last 10 root in
  let two = push_last 20 one in
  let three = push_first 5 two in
  let four = push_first 1 three in
  let five = push_last 30 four in
  assert_deque "root" [] root;
  assert_deque "one" [ 10 ] one;
  assert_deque "two" [ 10; 20 ] two;
  assert_deque "three" [ 5; 10; 20 ] three;
  assert_deque "four" [ 1; 5; 10; 20 ] four;
  assert_deque "five" [ 1; 5; 10; 20; 30 ] five;
  assert_deque "five without its first" [ 5; 10; 20; 30 ]
    (some "remove_first" (Deque.remove_first five));
  assert_deque "five without its last" [ 1; 5; 10; 20 ]
    (some "remove_last" (Deque.remove_last five));
  assert_deque "five retained" [ 1; 5; 10; 20; 30 ] five;
  let popped_first, without_first = some "pop_first" (Deque.pop_first five) in
  Alcotest.(check int) "the popped first value" 1 popped_first;
  assert_deque "the deque after popping the front" [ 5; 10; 20; 30 ] without_first;
  let popped_last, without_last = some "pop_last" (Deque.pop_last five) in
  Alcotest.(check int) "the popped last value" 30 popped_last;
  assert_deque "the deque after popping the back" [ 1; 5; 10; 20 ] without_last;
  (* Two independent branches grow from one retained version. *)
  assert_deque "the left branch" [ -1; 10; 20 ] (push_first (-1) two);
  assert_deque "the right branch" [ 10; 20; 99 ] (push_last 99 two);
  assert_deque "the branched source" [ 10; 20 ] two;
  let right_only = root |> push_last 1 |> push_last 2 |> push_last 3 in
  let after_one = some "crossing removal" (Deque.remove_first right_only) in
  let after_two = some "crossing removal" (Deque.remove_first after_one) in
  assert_deque "a right-only deque after one crossing removal" [ 2; 3 ] after_one;
  assert_deque "a right-only deque after two crossing removals" [ 3 ] after_two;
  assert_deque "a drained right-only deque" []
    (some "crossing removal" (Deque.remove_first after_two));
  assert_deque "the retained right-only deque" [ 1; 2; 3 ] right_only;
  let left_only = root |> push_first 3 |> push_first 2 |> push_first 1 in
  let trimmed_once = some "crossing removal" (Deque.remove_last left_only) in
  let trimmed_twice = some "crossing removal" (Deque.remove_last trimmed_once) in
  assert_deque "a left-only deque after one crossing removal" [ 1; 2 ] trimmed_once;
  assert_deque "a left-only deque after two crossing removals" [ 1 ] trimmed_twice;
  assert_deque "a drained left-only deque" []
    (some "crossing removal" (Deque.remove_last trimmed_twice));
  let cleared = Deque.clear five in
  assert_deque "a cleared deque" [] cleared;
  assert_deque "a cleared deque regrown" [ -7; 8 ] (cleared |> push_first (-7) |> push_last 8);
  assert_deque "the source of a clear" [ 1; 5; 10; 20; 30 ] five;
  assert_deque "of_list" [ 1; 2; 3; 4; 5 ] (ok "of_list" (Deque.of_list [ 1; 2; 3; 4; 5 ]))

let test_reverse_exchanges_the_intervals () =
  let root = Deque.create () in
  let deque =
    root |> push_first 3 |> push_first 2 |> push_first 1 |> push_last 4 |> push_last 5
    |> push_last 6 |> push_last 7
  in
  let reversed = Deque.reverse deque in
  let restored = Deque.reverse reversed in
  assert_deque "the forward deque" [ 1; 2; 3; 4; 5; 6; 7 ] deque;
  assert_deque "the reversed deque" [ 7; 6; 5; 4; 3; 2; 1 ] reversed;
  assert_deque "the restored deque" [ 1; 2; 3; 4; 5; 6; 7 ] restored;
  Alcotest.(check bool)
    "reversing twice restores the representation" true
    (Deque.shares_storage_with deque restored);
  assert_deque "a reversed deque grown at both ends" [ 0; 7; 6; 5; 4; 3; 2; 1; 8 ]
    (reversed |> push_first 0 |> push_last 8);
  assert_deque "a reversed deque trimmed at both ends" [ 6; 5; 4; 3; 2 ]
    (some "trim" (Deque.remove_last (some "trim" (Deque.remove_first reversed))));
  assert_deque "the source of a reversal" [ 1; 2; 3; 4; 5; 6; 7 ] deque;
  (* Exchanging the two interval descriptors is the whole of the operation. *)
  let forward = ok "validate" (Deque.validate deque) in
  let backward = ok "validate" (Deque.validate reversed) in
  Alcotest.(check (pair int int))
    "the interval counts are exchanged"
    (forward.Deque.left_count, forward.Deque.right_count)
    (backward.Deque.right_count, backward.Deque.left_count);
  (* At a count of at most one the two orientations coincide, so the receiver is returned. *)
  let empty = Deque.create () in
  Alcotest.(check bool)
    "reversing an empty deque shares storage" true
    (Deque.shares_storage_with empty (Deque.reverse empty));
  let right_singleton = ok "of_list" (Deque.of_list [ 42 ]) in
  Alcotest.(check bool)
    "reversing a right-arm singleton shares storage" true
    (Deque.shares_storage_with right_singleton (Deque.reverse right_singleton));
  let left_singleton = push_first 42 (Deque.create ()) in
  Alcotest.(check bool)
    "reversing a left-arm singleton shares storage" true
    (Deque.shares_storage_with left_singleton (Deque.reverse left_singleton))

let test_identity_operations_share_storage () =
  let deque = eight_value_deque (Deque.create ()) in
  let shares name produced =
    Alcotest.(check bool)
      (name ^ ": shares storage")
      true
      (Deque.shares_storage_with deque produced)
  in
  shares "the whole-range slice" (ok "slice" (Deque.slice ~index:0 ~count:8 deque));
  shares "taking everything" (ok "take" (Deque.take 8 deque));
  shares "dropping nothing" (ok "drop" (Deque.drop 0 deque));
  let empty_prefix, whole_suffix = ok "split_at 0" (Deque.split_at 0 deque) in
  shares "the suffix of a split at zero" whole_suffix;
  assert_deque "the prefix of a split at zero" [] empty_prefix;
  let whole_prefix, empty_suffix = ok "split_at 8" (Deque.split_at 8 deque) in
  shares "the prefix of a split at the count" whole_prefix;
  assert_deque "the suffix of a split at the count" [] empty_suffix;
  let cleared = Deque.clear deque in
  Alcotest.(check bool)
    "clearing a nonempty deque produces a fresh handle" false
    (Deque.shares_storage_with deque cleared);
  Alcotest.(check bool)
    "clearing an already-empty deque shares storage" true
    (Deque.shares_storage_with cleared (Deque.clear cleared));
  Alcotest.(check bool)
    "an interior empty slice does not share the receiver" false
    (Deque.shares_storage_with deque (ok "slice" (Deque.slice ~index:3 ~count:0 deque)));
  (* Storage sharing is a representation test, not an equality test. *)
  Alcotest.(check bool)
    "an extensionally equal rebuild does not share storage" false
    (Deque.shares_storage_with deque (ok "of_list" (Deque.of_list [ 1; 2; 3; 4; 5; 6; 7; 8 ])))

let test_slice_closure () =
  let deque = eight_value_deque (Deque.create ()) in
  let expected = [ 1; 2; 3; 4; 5; 6; 7; 8 ] in
  for start = 0 to 8 do
    for amount = 0 to 8 - start do
      let model = sublist start amount expected in
      let label = Printf.sprintf "slice (%d, %d)" start amount in
      let sliced = ok label (Deque.slice ~index:start ~count:amount deque) in
      assert_deque label model sliced;
      assert_deque (label ^ " grown")
        ((-100 - start) :: (model @ [ 100 + amount ]))
        (sliced |> push_first (-100 - start) |> push_last (100 + amount));
      assert_deque (label ^ " reversed and grown")
        (-1 :: (List.rev model @ [ -2 ]))
        (Deque.reverse sliced |> push_first (-1) |> push_last (-2));
      assert_deque (label ^ " retained") model sliced
    done
  done;
  for boundary = 0 to 8 do
    let prefix, suffix = ok "split" (Deque.split_at boundary deque) in
    let label = Printf.sprintf "split at %d" boundary in
    assert_deque (label ^ " prefix") (sublist 0 boundary expected) prefix;
    assert_deque (label ^ " suffix") (sublist boundary (8 - boundary) expected) suffix;
    assert_deque (label ^ " prefix grown")
      (sublist 0 boundary expected @ [ 88 ])
      (push_last 88 prefix);
    assert_deque (label ^ " suffix grown")
      (77 :: sublist boundary (8 - boundary) expected)
      (push_first 77 suffix);
    assert_deque (label ^ " take")
      (sublist 0 boundary expected)
      (ok label (Deque.take boundary deque));
    assert_deque (label ^ " drop")
      (sublist boundary (8 - boundary) expected)
      (ok label (Deque.drop boundary deque))
  done;
  assert_deque "the source of every slice" expected deque

let test_construction_words_match_the_oracle () =
  for length = 0 to 6 do
    for word = 0 to (1 lsl length) - 1 do
      let deque = ref (Deque.create ()) in
      let model = ref [] in
      for value = 0 to length - 1 do
        if word land (1 lsl value) = 0 then begin
          deque := push_first value !deque;
          model := value :: !model
        end
        else begin
          deque := push_last value !deque;
          model := !model @ [ value ]
        end
      done;
      let label = Printf.sprintf "word %d of length %d" word length in
      assert_deque label !model !deque;
      for start = 0 to length do
        for amount = 0 to length - start do
          let expected = sublist start amount !model in
          let sliced = ok label (Deque.slice ~index:start ~count:amount !deque) in
          assert_deque (Printf.sprintf "%s: slice (%d, %d)" label start amount) expected sliced;
          assert_deque
            (Printf.sprintf "%s: grown slice (%d, %d)" label start amount)
            (-1 :: (expected @ [ -2 ]))
            (sliced |> push_first (-1) |> push_last (-2))
        done
      done;
      for boundary = 0 to length do
        let prefix, suffix = ok label (Deque.split_at boundary !deque) in
        assert_deque
          (Printf.sprintf "%s: prefix at %d" label boundary)
          (sublist 0 boundary !model) prefix;
        assert_deque
          (Printf.sprintf "%s: suffix at %d" label boundary)
          (sublist boundary (length - boundary) !model)
          suffix;
        assert_deque
          (Printf.sprintf "%s: take %d" label boundary)
          (sublist 0 boundary !model)
          (ok label (Deque.take boundary !deque));
        assert_deque
          (Printf.sprintf "%s: drop %d" label boundary)
          (sublist boundary (length - boundary) !model)
          (ok label (Deque.drop boundary !deque))
      done
    done
  done

let test_indexing_query_profile () =
  let root = Deque.create () in
  let arena = Deque.arena root in
  let deque = eight_value_deque root in
  assert_deque "the eight-value deque" [ 1; 2; 3; 4; 5; 6; 7; 8 ] deque;
  (* Visible index zero, the end of the left interval, the first right value, and the last visible
     index are the four cached endpoints; every other position spends exactly one query. Pinning the
     whole row keeps the ceiling of one from passing vacuously. *)
  List.iteri
    (fun index expected ->
      let label = Printf.sprintf "indexing %d" index in
      let value = queries arena expected label (fun () -> Deque.nth index deque) in
      Alcotest.(check (option int)) (label ^ ": value") (Some (index + 1)) value)
    [ 0; 1; 1; 0; 0; 1; 1; 0 ];
  expect_none "indexing past the end"
    (queries arena 0 "indexing past the end" (fun () -> Deque.nth 8 deque));
  expect_none "indexing below zero"
    (queries arena 0 "indexing below zero" (fun () -> Deque.nth (-1) deque));
  let right_only = root |> push_last 1 |> push_last 2 |> push_last 3 |> push_last 4 in
  List.iteri
    (fun index expected ->
      let label = Printf.sprintf "right-only indexing %d" index in
      let value = queries arena expected label (fun () -> Deque.nth index right_only) in
      Alcotest.(check (option int)) (label ^ ": value") (Some (index + 1)) value)
    [ 0; 1; 1; 0 ];
  let left_only = root |> push_first 4 |> push_first 3 |> push_first 2 |> push_first 1 in
  List.iteri
    (fun index expected ->
      let label = Printf.sprintf "left-only indexing %d" index in
      let value = queries arena expected label (fun () -> Deque.nth index left_only) in
      Alcotest.(check (option int)) (label ^ ": value") (Some (index + 1)) value)
    [ 0; 1; 1; 0 ]

(** Row [start] holds the exact query count for each [count] in zero through [8 - start]. The
    maximum is two, including every cross-arm entry, where each arm spends at most one. *)
let slice_query_profile =
  [
    [ 0; 0; 1; 1; 0; 0; 1; 1; 0 ];
    [ 0; 1; 2; 1; 1; 2; 2; 1 ];
    [ 0; 1; 1; 1; 2; 2; 1 ];
    [ 0; 1; 1; 2; 2; 1 ];
    [ 0; 0; 1; 1; 0 ];
    [ 0; 1; 2; 1 ];
    [ 0; 1; 1 ];
    [ 0; 1 ];
    [ 0 ];
  ]

let split_query_profile = [ 0; 1; 2; 2; 0; 1; 2; 2; 0 ]

let test_slice_and_split_query_profiles () =
  let root = Deque.create () in
  let arena = Deque.arena root in
  let deque = eight_value_deque root in
  let expected = [ 1; 2; 3; 4; 5; 6; 7; 8 ] in
  List.iteri
    (fun start row ->
      Alcotest.(check int)
        (Printf.sprintf "the slice profile row for %d" start)
        (9 - start) (List.length row);
      List.iteri
        (fun amount cost ->
          let label = Printf.sprintf "slicing (%d, %d)" start amount in
          let sliced =
            queries arena cost label (fun () ->
                ok label (Deque.slice ~index:start ~count:amount deque))
          in
          Alcotest.(check (list int))
            (label ^ ": values")
            (sublist start amount expected) (Deque.to_list sliced))
        row)
    slice_query_profile;
  Alcotest.(check int)
    "the slice ceiling of two is reached" 2
    (List.fold_left (fun best row -> List.fold_left max best row) 0 slice_query_profile);
  List.iteri
    (fun boundary cost ->
      let label = Printf.sprintf "splitting at %d" boundary in
      let prefix, suffix =
        queries arena cost label (fun () -> ok label (Deque.split_at boundary deque))
      in
      Alcotest.(check (list int))
        (label ^ ": prefix")
        (sublist 0 boundary expected) (Deque.to_list prefix);
      Alcotest.(check (list int))
        (label ^ ": suffix")
        (sublist boundary (8 - boundary) expected)
        (Deque.to_list suffix))
    split_query_profile;
  (* A prefix and a suffix are slices, so they answer with the corresponding profile entries. *)
  for amount = 0 to 8 do
    spends arena
      (List.nth (List.nth slice_query_profile 0) amount)
      (Printf.sprintf "taking %d" amount)
      (fun () -> ok "take" (Deque.take amount deque));
    spends arena
      (List.nth (List.nth slice_query_profile amount) (8 - amount))
      (Printf.sprintf "dropping %d" amount)
      (fun () -> ok "drop" (Deque.drop amount deque))
  done

let test_removal_query_profiles () =
  let root = Deque.create () in
  let arena = Deque.arena root in
  let deque = eight_value_deque root in
  (* A removal on its own nonempty arm moves one cached tail to its parent and queries nothing. *)
  let own_front =
    queries arena 0 "removing the first of a bilateral deque" (fun () ->
        some "remove_first" (Deque.remove_first deque))
  in
  let own_back =
    queries arena 0 "removing the last of a bilateral deque" (fun () ->
        some "remove_last" (Deque.remove_last deque))
  in
  assert_deque "an own-arm removal at the front" [ 2; 3; 4; 5; 6; 7; 8 ] own_front;
  assert_deque "an own-arm removal at the back" [ 1; 2; 3; 4; 5; 6; 7 ] own_back;
  (* Crossing the centre selects the next cached base with one query, except for the last two
     values: at a count of two the removal takes the shortcut that reuses the interval's own tail,
     and at a count of one the whole-deque empty shortcut answers. *)
  let profile = [ 1; 1; 1; 1; 0; 0 ] in
  let values = [ 1; 2; 3; 4; 5; 6 ] in
  let right_only =
    ref
      (root |> push_last 1 |> push_last 2 |> push_last 3 |> push_last 4 |> push_last 5
     |> push_last 6)
  in
  List.iteri
    (fun step cost ->
      let remaining = 5 - step in
      right_only :=
        queries arena cost
          (Printf.sprintf "a crossing removal at the front, step %d" step)
          (fun () -> some "remove_first" (Deque.remove_first !right_only));
      assert_deque
        (Printf.sprintf "a right-only deque after %d crossing removals" (step + 1))
        (sublist (step + 1) remaining values) !right_only;
      (* The base the removal reselected is what keeps the next front read query-free. *)
      if remaining <> 0 then
        Alcotest.(check (option int))
          "the reselected base answers the front read"
          (Some (step + 2))
          (queries arena 0 "a front read after a crossing removal" (fun () ->
               Deque.first !right_only)))
    profile;
  Alcotest.(check bool)
    "repeated crossing removal empties the deque" true (Deque.is_empty !right_only);
  let left_only =
    ref
      (root |> push_first 6 |> push_first 5 |> push_first 4 |> push_first 3 |> push_first 2
     |> push_first 1)
  in
  List.iteri
    (fun step cost ->
      let remaining = 5 - step in
      left_only :=
        queries arena cost
          (Printf.sprintf "a crossing removal at the back, step %d" step)
          (fun () -> some "remove_last" (Deque.remove_last !left_only));
      assert_deque
        (Printf.sprintf "a left-only deque after %d crossing removals" (step + 1))
        (sublist 0 remaining values) !left_only;
      if remaining <> 0 then
        Alcotest.(check (option int))
          "the reselected base answers the back read"
          (Some remaining)
          (queries arena 0 "a back read after a crossing removal" (fun () ->
               Deque.last !left_only)))
    profile;
  Alcotest.(check bool)
    "repeated crossing removal empties the reversed deque" true (Deque.is_empty !left_only)

let test_query_free_operations () =
  let root = Deque.create () in
  let arena = Deque.arena root in
  let deque = eight_value_deque root in
  spends arena 0 "reading the front" (fun () -> Deque.first deque);
  spends arena 0 "reading the back" (fun () -> Deque.last deque);
  spends arena 0 "reading the count" (fun () -> Deque.count deque);
  spends arena 0 "reading emptiness" (fun () -> Deque.is_empty deque);
  spends arena 0 "reversing" (fun () -> Deque.reverse deque);
  spends arena 0 "clearing" (fun () -> Deque.clear deque);
  spends arena 0 "adding at the front" (fun () -> push_first 0 deque);
  spends arena 0 "adding at the back" (fun () -> push_last 9 deque);
  spends arena 0 "copying to a list" (fun () -> Deque.to_list deque);
  spends arena 0 "enumerating" (fun () -> List.of_seq (Deque.to_seq deque));
  spends arena 0 "iterating" (fun () -> Deque.iter ignore deque);
  spends arena 0 "folding" (fun () -> Deque.fold_left ( + ) 0 deque);
  spends arena 0 "comparing storage" (fun () -> Deque.shares_storage_with deque deque);
  let before = Incremental_ancestor.statistics arena in
  ignore (Deque.to_list deque : int list);
  ignore (List.of_seq (Deque.to_seq deque) : int list);
  ignore (Deque.first deque : int option);
  ignore (Deque.last deque : int option);
  ignore (Deque.nth 2 deque : int option);
  ignore (Deque.reverse deque : int Deque.t);
  ignore (Deque.clear deque : int Deque.t);
  ignore (ok "slice" (Deque.slice ~index:1 ~count:2 deque) : int Deque.t);
  let after = Incremental_ancestor.statistics arena in
  Alcotest.(check int)
    "reading publishes no nodes"
    before.Incremental_ancestor.published_node_count
    after.Incremental_ancestor.published_node_count;
  Alcotest.(check int)
    "reading adds no leaves" before.Incremental_ancestor.add_leaf_count
    after.Incremental_ancestor.add_leaf_count

let test_rejected_boundaries () =
  let root = Deque.create () in
  let arena = Deque.arena root in
  let deque = root |> push_last 1 |> push_last 2 |> push_last 3 in
  let before = Incremental_ancestor.statistics arena in
  Alcotest.(check (option int)) "indexing past the end" None (Deque.nth 3 deque);
  Alcotest.(check (option int)) "indexing below zero" None (Deque.nth (-1) deque);
  Alcotest.(check (option int)) "indexing at max_int" None (Deque.nth max_int deque);
  expect_error "an oversized prefix" (Deque.take 4 deque);
  expect_error "a negative prefix" (Deque.take (-1) deque);
  expect_error "an oversized suffix" (Deque.drop 4 deque);
  expect_error "a negative suffix" (Deque.drop (-1) deque);
  expect_error "an oversized boundary" (Deque.split_at 4 deque);
  expect_error "a negative boundary" (Deque.split_at (-1) deque);
  expect_error "a slice index past the end" (Deque.slice ~index:4 ~count:0 deque);
  expect_error "an overlong slice" (Deque.slice ~index:2 ~count:2 deque);
  expect_error "a negative slice index" (Deque.slice ~index:(-1) ~count:0 deque);
  expect_error "a negative slice count" (Deque.slice ~index:0 ~count:(-1) deque);
  expect_error "a saturated slice index" (Deque.slice ~index:max_int ~count:1 deque);
  expect_error "a saturated slice count" (Deque.slice ~index:1 ~count:max_int deque);
  let after = Incremental_ancestor.statistics arena in
  Alcotest.(check int)
    "a rejected boundary queries nothing" before.Incremental_ancestor.ancestor_query_count
    after.Incremental_ancestor.ancestor_query_count;
  Alcotest.(check int)
    "a rejected boundary publishes nothing" before.Incremental_ancestor.published_node_count
    after.Incremental_ancestor.published_node_count;
  assert_deque "a deque after rejected boundaries" [ 1; 2; 3 ] deque

let test_validate_reports_interval_counts () =
  let root = Deque.create () in
  let arena = Deque.arena root in
  let deque = root |> push_first 2 |> push_first 1 |> push_last 3 in
  let audited label expected value =
    let { Deque.count; left_count; right_count } = ok label (Deque.validate value) in
    Alcotest.(check (list int)) label expected [ count; left_count; right_count ]
  in
  audited "a bilateral handle" [ 3; 2; 1 ] deque;
  audited "an empty handle" [ 0; 0; 0 ] root;
  audited "a right-arm-only slice" [ 1; 0; 1 ] (ok "slice" (Deque.slice ~index:2 ~count:1 deque));
  audited "a left-arm-only slice" [ 2; 2; 0 ] (ok "slice" (Deque.slice ~index:0 ~count:2 deque));
  audited "a reversed handle" [ 3; 1; 2 ] (Deque.reverse deque);
  (* The audit is not O(1) work: it confirms each interval's ancestry path with two queries. *)
  spends arena 0 "auditing an empty handle" (fun () -> ok "validate" (Deque.validate root));
  let right_only = root |> push_last 1 |> push_last 2 |> push_last 3 in
  let left_only = root |> push_first 3 |> push_first 2 |> push_first 1 in
  spends arena 2 "auditing a right-only handle" (fun () ->
      ok "validate" (Deque.validate right_only));
  spends arena 2 "auditing a left-only handle" (fun () -> ok "validate" (Deque.validate left_only));
  spends arena 4 "auditing a bilateral handle" (fun () -> ok "validate" (Deque.validate deque));
  (* Every arm shape an interior slice can reach audits cleanly. *)
  let wide = eight_value_deque root in
  for start = 0 to 8 do
    for amount = 0 to 8 - start do
      let label = Printf.sprintf "auditing slice (%d, %d)" start amount in
      let { Deque.count; left_count; right_count } =
        ok label (Deque.validate (ok label (Deque.slice ~index:start ~count:amount wide)))
      in
      Alcotest.(check (pair int int)) label (amount, amount) (count, left_count + right_count)
    done
  done

let test_a_shared_arena_serves_independent_families () =
  let arena = Incremental_ancestor.create () in
  let root = ok "of_arena" (Deque.of_arena arena) in
  let first_family = root |> push_last 1 |> push_last 2 in
  let second_family = root |> push_first 9 |> push_first 8 in
  assert_deque "the first family" [ 1; 2 ] first_family;
  assert_deque "the second family" [ 8; 9 ] second_family;
  assert_deque "the shared root" [] root;
  Alcotest.(check int)
    "the shared arena keeps every node" 4
    (Incremental_ancestor.published_node_count arena);
  Alcotest.(check bool)
    "the accessor returns the retained arena" true
    (Deque.arena root == arena);
  Alcotest.(check bool)
    "a privately created deque owns a different arena" false
    (Deque.arena (Deque.create ()) == arena)

let test_randomized_versions_match_a_list_model () =
  List.iter
    (fun seed ->
      let random = Random.State.make [| seed |] in
      let root = Deque.create () in
      let versions = ref [ (root, []) ] in
      for step = 0 to 199 do
        let source, model = List.nth !versions (Random.State.int random (List.length !versions)) in
        let length = List.length model in
        let value = seed + (step * 31) in
        let produced, expected =
          match Random.State.int random 10 with
          | 0 -> (push_first value source, value :: model)
          | 1 -> (push_last value source, model @ [ value ])
          | 2 when length <> 0 -> (some "remove_first" (Deque.remove_first source), List.tl model)
          | 3 when length <> 0 ->
              (some "remove_last" (Deque.remove_last source), sublist 0 (length - 1) model)
          | 4 -> (Deque.reverse source, List.rev model)
          | 5 ->
              let start = Random.State.int random (length + 1) in
              let amount = Random.State.int random (length - start + 1) in
              ( ok "slice" (Deque.slice ~index:start ~count:amount source),
                sublist start amount model )
          | 6 ->
              let boundary = Random.State.int random (length + 1) in
              let prefix, suffix = ok "split" (Deque.split_at boundary source) in
              if Random.State.bool random then (prefix, sublist 0 boundary model)
              else (suffix, sublist boundary (length - boundary) model)
          | 7 ->
              let amount = Random.State.int random (length + 1) in
              (ok "take" (Deque.take amount source), sublist 0 amount model)
          | 8 ->
              let amount = Random.State.int random (length + 1) in
              (ok "drop" (Deque.drop amount source), sublist amount (length - amount) model)
          | _ -> (Deque.clear source, [])
        in
        assert_deque (Printf.sprintf "randomized result at seed %d step %d" seed step) expected
          produced;
        assert_deque
          (Printf.sprintf "randomized source at seed %d step %d" seed step)
          model source;
        versions := (produced, expected) :: !versions
      done;
      assert_deque "the retained root" [] root;
      (* Every retained version is still exactly what it was when it was produced. *)
      List.iter
        (fun (retained, model) -> assert_deque "a retained version" model retained)
        !versions)
    [ 0; 1; 17; 91 ]

let tests =
  [
    Alcotest.test_case "empty handles read and share storage" `Quick test_empty_handles;
    Alcotest.test_case "endpoint edits retain every branch" `Quick test_endpoint_edits;
    Alcotest.test_case "reversal exchanges the two intervals" `Quick
      test_reverse_exchanges_the_intervals;
    Alcotest.test_case "identity operations share storage" `Quick
      test_identity_operations_share_storage;
    Alcotest.test_case "every slice and split stays growable" `Quick test_slice_closure;
    Alcotest.test_case "every construction word matches the oracle" `Quick
      test_construction_words_match_the_oracle;
    Alcotest.test_case "indexing matches its exact query profile" `Quick
      test_indexing_query_profile;
    Alcotest.test_case "slicing and splitting match their exact query profiles" `Quick
      test_slice_and_split_query_profiles;
    Alcotest.test_case "removals match their exact query profiles" `Quick
      test_removal_query_profiles;
    Alcotest.test_case "reads, edits, and enumeration make no query" `Quick
      test_query_free_operations;
    Alcotest.test_case "rejected boundaries publish and query nothing" `Quick
      test_rejected_boundaries;
    Alcotest.test_case "the audit reports interval counts" `Quick
      test_validate_reports_interval_counts;
    Alcotest.test_case "a shared arena serves independent families" `Quick
      test_a_shared_arena_serves_independent_families;
    Alcotest.test_case "randomized branches match a list model" `Quick
      test_randomized_versions_match_a_list_model;
  ]
