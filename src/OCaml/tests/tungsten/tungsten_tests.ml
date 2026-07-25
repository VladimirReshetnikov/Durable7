open Durable7
open Tungsten

let check_ints label expected actual = Alcotest.(check (list int)) label expected actual
let check_strings label expected actual = Alcotest.(check (list string)) label expected actual

let case_insensitive_policy =
  Common.Hash_policy.create
    ~hash:(fun value -> Hashtbl.hash (String.lowercase_ascii value))
    ~equal:(fun left right ->
      String.equal (String.lowercase_ascii left) (String.lowercase_ascii right))

let test_persistent_list () =
  let source = Persistent_list.of_list [ 1; 2; 3; 4 ] in
  let edited =
    source |> Persistent_list.prepend 0 |> Persistent_list.append 5
    |> Persistent_list.insert_range 3 [ 8; 9 ]
    |> Result.get_ok
  in
  check_ints "end and range insertion" [ 0; 1; 2; 8; 9; 3; 4; 5 ] (Persistent_list.to_list edited);
  let removed = Result.get_ok (Persistent_list.remove_range ~start:2 ~count:3 edited) in
  check_ints "range removal" [ 0; 1; 3; 4; 5 ] (Persistent_list.to_list removed);
  check_ints "source retained" [ 1; 2; 3; 4 ] (Persistent_list.to_list source);
  let mapped = Persistent_list.map (fun value index -> value + index) source in
  check_ints "indexed map" [ 1; 3; 5; 7 ] (Persistent_list.to_list mapped);
  Alcotest.(check bool)
    "invalid range rejected" true
    (Result.is_error (Persistent_list.range ~start:3 ~count:2 source))

let test_association_ordering_rules () =
  let source =
    Persistent_association.of_list case_insensitive_policy [ ("a", 1); ("b", 2); ("c", 3) ]
  in
  let replaced = Persistent_association.set "A" 10 source in
  check_strings "set retains order and key" [ "a"; "b"; "c" ] (Persistent_association.keys replaced);
  Alcotest.(check (option int)) "set value" (Some 10) (Persistent_association.find_opt "a" replaced);
  let appended = Persistent_association.append "A" 11 replaced in
  check_strings "append moves and adopts caller representative" [ "b"; "c"; "A" ]
    (Persistent_association.keys appended);
  let inserted = Result.get_ok (Persistent_association.insert 2 "B" 20 appended) in
  check_strings "existing insertion adjusts target" [ "c"; "B"; "A" ]
    (Persistent_association.keys inserted);
  let selected = Persistent_association.key_take [ "a"; "C"; "A"; "missing" ] inserted in
  check_strings "key take follows request order once" [ "A"; "c" ]
    (Persistent_association.keys selected);
  check_strings "source retained" [ "a"; "b"; "c" ] (Persistent_association.keys source);
  let removed_value, successor = Option.get (Persistent_association.remove "b" source) in
  Alcotest.(check int) "removed value" 2 removed_value;
  check_strings "removal order" [ "a"; "c" ] (Persistent_association.keys successor)

let test_association_sort_and_join () =
  let left = Persistent_association.of_list case_insensitive_policy [ ("b", 2); ("a", 3) ] in
  let right = Persistent_association.of_list case_insensitive_policy [ ("A", 30); ("c", 1) ] in
  let joined = Persistent_association.join left right in
  check_strings "join keeps existing key position" [ "b"; "a"; "c" ]
    (Persistent_association.keys joined);
  Alcotest.(check (option int))
    "join updates existing value" (Some 30)
    (Persistent_association.find_opt "a" joined);
  let sorted = Persistent_association.value_sort (Common.Comparator.create Int.compare) joined in
  check_ints "stable value sort" [ 1; 2; 30 ] (Persistent_association.values sorted);
  let range = Result.get_ok (Persistent_association.range ~start:1 ~count:2 joined) in
  check_strings "association range" [ "a"; "c" ] (Persistent_association.keys range)

let list_model =
  QCheck.Test.make ~count:200 ~name:"Tungsten List matches list end edits"
    QCheck.(list (pair bool nat_small))
    (fun operations ->
      let expected =
        List.fold_left
          (fun values (prepend, value) -> if prepend then value :: values else values @ [ value ])
          [] operations
      in
      let actual =
        List.fold_left
          (fun values (prepend, value) ->
            if prepend then Persistent_list.prepend value values
            else Persistent_list.append value values)
          Persistent_list.empty operations
      in
      expected = Persistent_list.to_list actual)

let test_list_model () = QCheck.Test.check_exn list_model

let () =
  Alcotest.run "Tungsten collections"
    [
      ( "application leaf",
        [
          Alcotest.test_case "List vocabulary" `Quick test_persistent_list;
          Alcotest.test_case "Association ordering" `Quick test_association_ordering_rules;
          Alcotest.test_case "Association sort and join" `Quick test_association_sort_and_join;
          Alcotest.test_case "List model" `Quick test_list_model;
        ] );
    ]
