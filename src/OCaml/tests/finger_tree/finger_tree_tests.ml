open Tools_data_structures
open Finger_tree

let check_int_list label expected actual = Alcotest.(check (list int)) label expected actual

let test_deque_persistence () =
  let original = Persistent_deque.of_list [ 1; 2; 3 ] in
  let successor = Persistent_deque.snoc (Persistent_deque.cons 0 original) 4 in
  check_int_list "original unchanged" [ 1; 2; 3 ] (Persistent_deque.to_list original);
  check_int_list "both ends" [ 0; 1; 2; 3; 4 ] (Persistent_deque.to_list successor);
  let left, right = Persistent_deque.split_at 2 successor in
  check_int_list "left split" [ 0; 1 ] (Persistent_deque.to_list left);
  check_int_list "right split" [ 2; 3; 4 ] (Persistent_deque.to_list right);
  let removed, without = Result.get_ok (Persistent_deque.remove_at 2 successor) in
  Alcotest.(check int) "removed" 2 removed;
  check_int_list "remove" [ 0; 1; 3; 4 ] (Persistent_deque.to_list without);
  Alcotest.(check (result unit string)) "invariants" (Ok ()) (Persistent_deque.validate successor)

let test_measured_sequence () =
  let sequence = Measured_sequence.of_list Measures.int_sum [ 2; 3; 5; 7 ] in
  Alcotest.(check int) "total" 17 (Measured_sequence.measure sequence);
  Alcotest.(check (result int string))
    "subrange" (Ok 8)
    (Measured_sequence.measure_range 1 2 sequence);
  match Measured_sequence.locate (fun prefix -> prefix >= 10) sequence with
  | Some (index, before, value) ->
      Alcotest.(check int) "located index" 2 index;
      Alcotest.(check int) "exclusive prefix" 5 before;
      Alcotest.(check int) "located value" 5 value
  | None -> Alcotest.fail "measure search missed a satisfying prefix"

let test_reversible_deque () =
  let forward = Reversible_deque.of_list [ 1; 2; 3; 4 ] in
  let reversed = Reversible_deque.reverse forward in
  check_int_list "logical reverse" [ 4; 3; 2; 1 ] (Reversible_deque.to_list reversed);
  let extended = Reversible_deque.snoc (Reversible_deque.cons 5 reversed) 0 in
  check_int_list "reversed end edits" [ 5; 4; 3; 2; 1; 0 ] (Reversible_deque.to_list extended);
  check_int_list "source unchanged" [ 1; 2; 3; 4 ] (Reversible_deque.to_list forward)

let deque_model_property =
  QCheck.Test.make ~count:250 ~name:"deque preserves list semantics"
    QCheck.(list (pair bool nat_small))
    (fun operations ->
      let expected =
        List.fold_left
          (fun values (at_front, value) -> if at_front then value :: values else values @ [ value ])
          [] operations
      in
      let actual =
        List.fold_left
          (fun deque (at_front, value) ->
            if at_front then Persistent_deque.cons value deque
            else Persistent_deque.snoc deque value)
          Persistent_deque.empty operations
      in
      expected = Persistent_deque.to_list actual)

let test_deque_model () = QCheck.Test.check_exn deque_model_property
let int_order = Common.Comparator.create Int.compare

let test_sorted_collections () =
  let bag = Sorted_bag.of_list int_order [ 3; 1; 2; 2; 4 ] in
  Alcotest.(check int) "bag multiplicity" 2 (Sorted_bag.count_of 2 bag);
  check_int_list "bag value range" [ 2; 2; 3 ]
    (Sorted_bag.to_list (Sorted_bag.value_range ~minimum:2 ~maximum:3 bag));
  check_int_list "bag removes all" [ 1; 3; 4 ] (Sorted_bag.to_list (Sorted_bag.remove_all 2 bag));
  let case_insensitive =
    Common.Comparator.create (fun left right ->
        String.compare (String.lowercase_ascii left) (String.lowercase_ascii right))
  in
  let set = Sorted_set.of_list case_insensitive [ "Alpha"; "beta" ] in
  let added, same = Sorted_set.add "alpha" set in
  Alcotest.(check bool) "equivalent set member rejected" false added;
  Alcotest.(check (option string))
    "first representative retained" (Some "Alpha") (Sorted_set.find "ALPHA" same);
  let map = Result.get_ok (Sorted_map.of_list case_insensitive [ ("Alpha", 1); ("beta", 2) ]) in
  let inserted, updated = Sorted_map.set "ALPHA" 10 map in
  Alcotest.(check bool) "equivalent map key updates" false inserted;
  let entry = Option.get (Sorted_map.find_entry "alpha" updated) in
  Alcotest.(check string) "map key representative retained" "Alpha" (Sorted_map.entry_key entry);
  Alcotest.(check int) "map value replaced" 10 (Sorted_map.entry_value entry);
  let builder = Sorted_map.Builder.create updated in
  ignore (Sorted_map.Builder.set "gamma" 3 builder);
  let first_snapshot = Sorted_map.Builder.freeze builder in
  ignore (Sorted_map.Builder.set "delta" 4 builder);
  Alcotest.(check bool)
    "frozen builder snapshot detached" false
    (Sorted_map.mem "delta" first_snapshot)

let test_priority_queue () =
  let queue =
    Priority_queue.empty int_order |> Priority_queue.enqueue "later" 2
    |> Priority_queue.enqueue "first-min" 1
    |> Priority_queue.enqueue "second-min" 1
  in
  let first, queue = Option.get (Priority_queue.dequeue queue) in
  Alcotest.(check string) "stable first minimum" "first-min" (Priority_queue.entry_value first);
  let second, _ = Option.get (Priority_queue.dequeue queue) in
  Alcotest.(check string) "stable second minimum" "second-min" (Priority_queue.entry_value second)

let test_interval_collections () =
  let interval low high = Result.get_ok (Interval_tree.make_interval int_order low high) in
  let tree =
    Interval_tree.of_list int_order [ interval 10 20; interval 15 18; interval 30 40; interval 0 5 ]
  in
  Alcotest.(check int) "point overlaps" 2 (List.length (Interval_tree.query_point 16 tree));
  Alcotest.(check (option int)) "maximum high cached" (Some 40) (Interval_tree.maximum_high tree);
  let query = interval 17 31 in
  Alcotest.(check int)
    "interval overlaps" 3
    (List.length (Interval_tree.find_all_overlaps query tree));
  let interval_map = Persistent_interval_map.empty int_order in
  let interval_map =
    Result.get_ok (Persistent_interval_map.add ~low:10 ~high:20 "outer" interval_map)
  in
  let interval_map =
    Result.get_ok (Persistent_interval_map.add ~low:12 ~high:14 "inner" interval_map)
  in
  Alcotest.(check int)
    "payload point query" 2
    (List.length (Persistent_interval_map.query_point 13 interval_map));
  Alcotest.(check bool)
    "duplicate exact interval rejected" true
    (Result.is_error (Persistent_interval_map.add ~low:10 ~high:20 "duplicate" interval_map));
  let removed, successor =
    Option.get (Persistent_interval_map.remove ~low:12 ~high:14 interval_map)
  in
  Alcotest.(check string) "removed payload" "inner" removed;
  Alcotest.(check int) "source remains intact" 2 (Persistent_interval_map.count interval_map);
  Alcotest.(check int)
    "successor removes exact interval" 1
    (Persistent_interval_map.count successor)

let test_rrb_vector () =
  let source = Rrb_vector.of_list (List.init 100 Fun.id) in
  let left, right = Rrb_vector.split_at 37 source in
  Alcotest.(check int) "left length" 37 (Rrb_vector.length left);
  Alcotest.(check int) "right first" 37 (Option.get (Rrb_vector.nth 0 right));
  let joined = Rrb_vector.concat left right in
  check_int_list "split concatenation" (List.init 100 Fun.id) (Rrb_vector.to_list joined);
  let updated = Result.get_ok (Rrb_vector.set 50 500 source) in
  Alcotest.(check int) "updated value" 500 (Option.get (Rrb_vector.nth 50 updated));
  Alcotest.(check int) "source retained" 50 (Option.get (Rrb_vector.nth 50 source));
  let builder = Rrb_vector.Builder.create source in
  Rrb_vector.Builder.append 100 builder;
  let frozen = Rrb_vector.Builder.freeze builder in
  Rrb_vector.Builder.append 101 builder;
  Alcotest.(check int) "detached vector freeze" 101 (Rrb_vector.length frozen)

let test_chunked_bit_set () =
  let set = Result.get_ok (Persistent_chunked_bit_set.of_list [ 0; 1; 63; 64; 1_000_000 ]) in
  Alcotest.(check int) "deduplicated count" 5 (Persistent_chunked_bit_set.count set);
  Alcotest.(check int) "rank" 4 (Persistent_chunked_bit_set.rank 65 set);
  Alcotest.(check (option int)) "select" (Some 64) (Persistent_chunked_bit_set.select 3 set);
  Alcotest.(check (option int)) "next" (Some 63) (Persistent_chunked_bit_set.next_set_bit 2 set);
  Alcotest.(check (option int))
    "previous" (Some 64)
    (Persistent_chunked_bit_set.previous_set_bit 999_999 set);
  let removed, successor = Persistent_chunked_bit_set.remove 64 set in
  Alcotest.(check bool) "removed" true removed;
  Alcotest.(check bool) "source retained" true (Persistent_chunked_bit_set.mem 64 set);
  Alcotest.(check bool) "successor changed" false (Persistent_chunked_bit_set.mem 64 successor);
  Alcotest.(check bool)
    "negative bit rejected" true
    (Result.is_error (Persistent_chunked_bit_set.add (-1) set))

let test_range_update_sequence () =
  let rejected =
    Range_update_sequence.create_algebra ~id:"unchecked-add" ~identity:0 ~combine:( + )
      ~measure:Fun.id ~apply_element:( + )
      ~apply_measure:(fun tag ~length measure -> measure + (tag * length))
      ~compose:( + ) ~laws_verified:false ()
  in
  Alcotest.(check bool) "law gate" true (Result.is_error rejected);
  let algebra =
    Result.get_ok
      (Range_update_sequence.create_algebra ~id:"range-add-sum-v1" ~identity:0 ~combine:( + )
         ~measure:Fun.id ~apply_element:( + )
         ~apply_measure:(fun tag ~length measure -> measure + (tag * length))
         ~compose:( + ) ~laws_verified:true ())
  in
  let source = Range_update_sequence.of_list algebra [ 1; 2; 3; 4; 5 ] in
  let updated = Result.get_ok (Range_update_sequence.update_range ~start:1 ~length:3 10 source) in
  check_int_list "range update" [ 1; 12; 13; 14; 5 ] (Range_update_sequence.to_list updated);
  check_int_list "range source retained" [ 1; 2; 3; 4; 5 ] (Range_update_sequence.to_list source);
  Alcotest.(check int) "updated aggregate" 45 (Range_update_sequence.measure updated);
  Alcotest.(check (result int string))
    "range aggregate" (Ok 39)
    (Range_update_sequence.measure_range ~start:1 ~length:3 updated)

let () =
  Alcotest.run "FingerTree core"
    [
      ( "sequence",
        [
          Alcotest.test_case "deque persistence" `Quick test_deque_persistence;
          Alcotest.test_case "measured search" `Quick test_measured_sequence;
          Alcotest.test_case "reversible facade" `Quick test_reversible_deque;
          Alcotest.test_case "deque list model" `Quick test_deque_model;
          Alcotest.test_case "sorted collections" `Quick test_sorted_collections;
          Alcotest.test_case "stable priority queue" `Quick test_priority_queue;
          Alcotest.test_case "interval collections" `Quick test_interval_collections;
          Alcotest.test_case "RRB vector" `Quick test_rrb_vector;
          Alcotest.test_case "chunked bit set" `Quick test_chunked_bit_set;
          Alcotest.test_case "range update sequence" `Quick test_range_update_sequence;
        ] );
    ]
