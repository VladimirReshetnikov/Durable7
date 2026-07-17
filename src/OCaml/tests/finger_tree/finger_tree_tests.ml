open Tools_data_structures.Finger_tree

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

let () =
  Alcotest.run "FingerTree core"
    [
      ( "sequence",
        [
          Alcotest.test_case "deque persistence" `Quick test_deque_persistence;
          Alcotest.test_case "measured search" `Quick test_measured_sequence;
          Alcotest.test_case "reversible facade" `Quick test_reversible_deque;
          Alcotest.test_case "deque list model" `Quick test_deque_model;
        ] );
    ]
