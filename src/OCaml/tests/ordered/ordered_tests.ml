open Tools_data_structures
open Ordered

let case_insensitive_policy =
  Common.Hash_policy.create
    ~hash:(fun value -> Hashtbl.hash (String.lowercase_ascii value))
    ~equal:(fun left right ->
      String.equal (String.lowercase_ascii left) (String.lowercase_ascii right))

let check_strings label expected actual = Alcotest.(check (list string)) label expected actual

let test_ordered_set () =
  let source =
    Persistent_ordered_set.of_list case_insensitive_policy [ "Alpha"; "beta"; "gamma" ]
  in
  let added, duplicate = Persistent_ordered_set.add "ALPHA" source in
  Alcotest.(check bool) "duplicate rejected" false added;
  Alcotest.(check (option string))
    "stored representative" (Some "Alpha")
    (Persistent_ordered_set.find "alpha" duplicate);
  let moved = Result.get_ok (Persistent_ordered_set.move_to 2 "ALPHA" source) in
  check_strings "final-index movement" [ "beta"; "gamma"; "Alpha" ]
    (Persistent_ordered_set.to_list moved);
  check_strings "source retained" [ "Alpha"; "beta"; "gamma" ]
    (Persistent_ordered_set.to_list source);
  let range = Result.get_ok (Persistent_ordered_set.range ~start:1 ~count:2 moved) in
  check_strings "positional range" [ "gamma"; "Alpha" ] (Persistent_ordered_set.to_list range);
  let union = Persistent_ordered_set.union source [ "BETA"; "delta"; "DELTA"; "epsilon" ] in
  check_strings "receiver-policy union"
    [ "Alpha"; "beta"; "gamma"; "delta"; "epsilon" ]
    (Persistent_ordered_set.to_list union);
  let symmetric = Persistent_ordered_set.symmetric_difference source [ "BETA"; "delta" ] in
  check_strings "symmetric difference order" [ "Alpha"; "gamma"; "delta" ]
    (Persistent_ordered_set.to_list symmetric)

let test_ordered_map () =
  let source =
    Persistent_ordered_map.of_list case_insensitive_policy
      [ ("Alpha", 1); ("beta", 2); ("gamma", 3) ]
  in
  let inserted, updated = Persistent_ordered_map.set "ALPHA" 10 source in
  Alcotest.(check bool) "set updates equivalent key" false inserted;
  let entry = Option.get (Persistent_ordered_map.find_entry "alpha" updated) in
  Alcotest.(check string)
    "key representative retained" "Alpha"
    (Persistent_ordered_map.entry_key entry);
  Alcotest.(check int) "value updated" 10 (Persistent_ordered_map.entry_value entry);
  let moved = Result.get_ok (Persistent_ordered_map.move_to 0 "gamma" updated) in
  check_strings "map movement" [ "gamma"; "Alpha"; "beta" ]
    (List.map fst (Persistent_ordered_map.to_list moved));
  let removed, successor = Option.get (Persistent_ordered_map.remove "BETA" moved) in
  Alcotest.(check string) "removed representative" "beta" (Persistent_ordered_map.entry_key removed);
  Alcotest.(check bool) "source retains removed key" true (Persistent_ordered_map.mem "beta" moved);
  Alcotest.(check bool) "successor removes key" false (Persistent_ordered_map.mem "beta" successor)

let test_ordered_multimap () =
  let source =
    Persistent_ordered_multimap.of_list ~key_policy:case_insensitive_policy
      ~value_policy:case_insensitive_policy
      [ ("GroupA", "one"); ("groupb", "three"); ("GROUPA", "two"); ("groupa", "ONE") ]
  in
  Alcotest.(check int) "key count" 2 (Persistent_ordered_multimap.key_count source);
  Alcotest.(check int) "pair count" 3 (Persistent_ordered_multimap.pair_count source);
  Alcotest.(check (option string))
    "key representative" (Some "GroupA")
    (Persistent_ordered_multimap.find_key "groupa" source);
  check_strings "grouped value order" [ "one"; "two" ]
    (Persistent_ordered_set.to_list (Persistent_ordered_multimap.values "groupa" source));
  let values_moved =
    Result.get_ok (Persistent_ordered_multimap.move_value_to 0 ~key:"GROUPA" ~value:"two" source)
  in
  check_strings "value movement" [ "two"; "one" ]
    (Persistent_ordered_set.to_list (Persistent_ordered_multimap.values "groupa" values_moved));
  let keys_moved = Result.get_ok (Persistent_ordered_multimap.move_key_to 0 "groupb" source) in
  let flattened = Persistent_ordered_multimap.entries keys_moved in
  check_strings "key-group movement" [ "groupb"; "GroupA"; "GroupA" ]
    (List.map Persistent_ordered_multimap.entry_key flattened);
  let removed, without_pair = Persistent_ordered_multimap.remove "GROUPA" "one" source in
  Alcotest.(check bool) "pair removed" true removed;
  Alcotest.(check int)
    "pair count decremented" 2
    (Persistent_ordered_multimap.pair_count without_pair)

let ordered_set_model =
  QCheck.Test.make ~count:200 ~name:"ordered set retains first representatives"
    QCheck.(list (pair (int_bound 15) bool))
    (fun operations ->
      let expected = ref [] in
      let actual =
        ref (Persistent_ordered_set.empty (Common.Hash_policy.create ~hash:Fun.id ~equal:Int.equal))
      in
      List.iter
        (fun (value, add) ->
          if add then (
            if not (List.mem value !expected) then expected := !expected @ [ value ];
            actual := snd (Persistent_ordered_set.add value !actual))
          else (
            expected := List.filter (( <> ) value) !expected;
            actual := snd (Persistent_ordered_set.remove value !actual)))
        operations;
      !expected = Persistent_ordered_set.to_list !actual)

let test_model () = QCheck.Test.check_exn ordered_set_model

let () =
  Alcotest.run "Ordered collections"
    [
      ( "neutral",
        [
          Alcotest.test_case "ordered set" `Quick test_ordered_set;
          Alcotest.test_case "ordered map" `Quick test_ordered_map;
          Alcotest.test_case "ordered multimap" `Quick test_ordered_multimap;
          Alcotest.test_case "ordered-set model" `Quick test_model;
        ] );
    ]
