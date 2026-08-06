(** Tests for the insertion-ordered collections and their cursors. *)

open Durable7
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

let test_ordered_cursors () =
  let set_source =
    Persistent_ordered_set.of_list case_insensitive_policy [ "Alpha"; "beta"; "gamma" ]
  in
  let set_cursor = Option.get (Persistent_ordered_cursor.ordered_set_at 1 set_source) in
  Alcotest.(check (option string))
    "set cursor previous" (Some "Alpha")
    (Persistent_ordered_cursor.ordered_set_peek_previous set_cursor);
  Alcotest.(check (option string))
    "set cursor next" (Some "beta")
    (Persistent_ordered_cursor.ordered_set_peek_next set_cursor);
  let set_inserted = Persistent_ordered_cursor.ordered_set_insert "delta" set_cursor in
  Alcotest.(check int)
    "set cursor insertion gap" 2
    (Persistent_ordered_cursor.ordered_set_position set_inserted);
  check_strings "set cursor insertion"
    [ "Alpha"; "delta"; "beta"; "gamma" ]
    (Persistent_ordered_set.to_list (Persistent_ordered_cursor.ordered_set_snapshot set_inserted));
  let set_duplicate = Persistent_ordered_cursor.ordered_set_try_insert "BETA" set_inserted in
  Alcotest.(check bool)
    "set cursor duplicate" false
    (Persistent_ordered_cursor.insertion_added set_duplicate);
  Alcotest.(check int)
    "set cursor duplicate preserves gap" 2
    (Persistent_ordered_cursor.ordered_set_position
       (Persistent_ordered_cursor.insertion_value set_duplicate));
  let set_deleted =
    Option.get (Persistent_ordered_cursor.ordered_set_delete_previous set_inserted)
  in
  check_strings "set cursor deletion" [ "Alpha"; "beta"; "gamma" ]
    (Persistent_ordered_set.to_list (Persistent_ordered_cursor.ordered_set_snapshot set_deleted));
  Alcotest.(check bool)
    "set cursor retains policy" true
    (Persistent_ordered_set.mem "ALPHA"
       (Persistent_ordered_cursor.ordered_set_snapshot set_deleted));
  let set_found = Persistent_ordered_cursor.ordered_set_find "BETA" set_source in
  Alcotest.(check bool) "set cursor found" true (Persistent_ordered_cursor.search_found set_found);
  Alcotest.(check int)
    "set cursor found position" 1
    (Persistent_ordered_cursor.ordered_set_position
       (Persistent_ordered_cursor.search_value set_found));

  let map_source =
    Persistent_ordered_map.of_list case_insensitive_policy [ ("a", 1); ("b", 2); ("c", 3) ]
  in
  let map_cursor = Option.get (Persistent_ordered_cursor.ordered_map_at 1 map_source) in
  let map_inserted =
    Result.get_ok (Persistent_ordered_cursor.ordered_map_insert "x" 9 map_cursor)
  in
  let map_updated =
    Option.get (Persistent_ordered_cursor.ordered_map_set_next_value 20 map_inserted)
  in
  Alcotest.(check (list (pair string int)))
    "map cursor edits"
    [ ("a", 1); ("x", 9); ("b", 20); ("c", 3) ]
    (Persistent_ordered_map.to_list (Persistent_ordered_cursor.ordered_map_snapshot map_updated));
  let map_duplicate = Persistent_ordered_cursor.ordered_map_try_insert "B" 200 map_updated in
  Alcotest.(check bool)
    "map cursor duplicate" false
    (Persistent_ordered_cursor.insertion_added map_duplicate);
  Alcotest.(check int)
    "map duplicate focuses representative" 2
    (Persistent_ordered_cursor.ordered_map_position
       (Persistent_ordered_cursor.insertion_value map_duplicate));
  let map_deleted =
    Option.get
      (Persistent_ordered_cursor.ordered_map_delete_next
         (Option.get (Persistent_ordered_cursor.ordered_map_delete_previous map_updated)))
  in
  Alcotest.(check (list (pair string int)))
    "map cursor deletes"
    [ ("a", 1); ("c", 3) ]
    (Persistent_ordered_map.to_list (Persistent_ordered_cursor.ordered_map_snapshot map_deleted));
  Alcotest.(check (list (pair string int)))
    "map cursor source snapshot"
    [ ("a", 1); ("b", 2); ("c", 3) ]
    (Persistent_ordered_map.to_list (Persistent_ordered_cursor.ordered_map_snapshot map_cursor));

  let int_policy = Common.Hash_policy.create ~hash:Hashtbl.hash ~equal:Int.equal in
  let multimap_source =
    Persistent_ordered_multimap.of_list ~key_policy:case_insensitive_policy ~value_policy:int_policy
      [ ("b", 2); ("a", 9); ("B", 1); ("c", 7) ]
  in
  let pairs multimap =
    List.map
      (fun entry ->
        (Persistent_ordered_multimap.entry_key entry, Persistent_ordered_multimap.entry_value entry))
      (Persistent_ordered_multimap.entries multimap)
  in
  Alcotest.(check (list (pair string int)))
    "multimap grouped source"
    [ ("b", 2); ("b", 1); ("a", 9); ("c", 7) ]
    (pairs multimap_source);
  let pair_found = Persistent_ordered_cursor.ordered_multimap_find "B" 1 multimap_source in
  let multimap_start =
    Option.get (Persistent_ordered_cursor.ordered_multimap_at 0 multimap_source)
  in
  Alcotest.(check bool)
    "multimap start has no previous pair" true
    (Option.is_none (Persistent_ordered_cursor.ordered_multimap_peek_previous multimap_start));
  Alcotest.(check bool)
    "multimap cursor found" true
    (Persistent_ordered_cursor.search_found pair_found);
  Alcotest.(check int)
    "multimap cursor pair position" 1
    (Persistent_ordered_cursor.ordered_multimap_position
       (Persistent_ordered_cursor.search_value pair_found));
  let multimap_added =
    Persistent_ordered_cursor.ordered_multimap_insert "b" 3
      (Persistent_ordered_cursor.search_value pair_found)
  in
  Alcotest.(check int)
    "multimap grouped insertion gap" 3
    (Persistent_ordered_cursor.ordered_multimap_position multimap_added);
  let multimap_duplicate =
    Persistent_ordered_cursor.ordered_multimap_try_insert "B" 3 multimap_added
  in
  Alcotest.(check bool)
    "multimap cursor duplicate" false
    (Persistent_ordered_cursor.insertion_added multimap_duplicate);
  let multimap_deleted =
    Option.get
      (Persistent_ordered_cursor.ordered_multimap_delete_next
         (Option.get (Persistent_ordered_cursor.ordered_multimap_delete_previous multimap_added)))
  in
  Alcotest.(check (list (pair string int)))
    "multimap cursor deletes and contracts group"
    [ ("b", 2); ("b", 1); ("c", 7) ]
    (pairs (Persistent_ordered_cursor.ordered_multimap_snapshot multimap_deleted));
  Alcotest.(check int)
    "multimap cursor delete gap" 2
    (Persistent_ordered_cursor.ordered_multimap_position multimap_deleted);
  let group_found = Persistent_ordered_cursor.ordered_multimap_find_group "A" multimap_source in
  Alcotest.(check bool)
    "multimap group cursor found" true
    (Persistent_ordered_cursor.search_found group_found);
  Alcotest.(check int)
    "multimap group cursor position" 2
    (Persistent_ordered_cursor.ordered_multimap_position
       (Persistent_ordered_cursor.search_value group_found));
  Alcotest.(check (list (pair string int)))
    "multimap cursor source snapshot"
    [ ("b", 2); ("b", 1); ("a", 9); ("c", 7) ]
    (pairs
       (Persistent_ordered_cursor.ordered_multimap_snapshot
          (Persistent_ordered_cursor.search_value pair_found)))

let test_ordered_multimap_cursor_nan () =
  (* A value that is non-reflexive under the value policy (here [Float.nan], since [nan = nan] is
     [false]) exercises the multimap cursor's edit-then-publish contract. *)
  let float_policy = Common.Hash_policy.create ~hash:Hashtbl.hash ~equal:( = ) in
  let source =
    Persistent_ordered_multimap.empty ~key_policy:case_insensitive_policy ~value_policy:float_policy
  in
  let cursor = Option.get (Persistent_ordered_cursor.ordered_multimap_at 0 source) in
  (* Defect 1: inserting a pair the collection accepts must not raise. *)
  let inserted = Persistent_ordered_cursor.ordered_multimap_insert "k" Float.nan cursor in
  Alcotest.(check int)
    "nan pair accepted" 1
    (Persistent_ordered_multimap.pair_count
       (Persistent_ordered_cursor.ordered_multimap_snapshot inserted));
  let gap = Persistent_ordered_cursor.ordered_multimap_position inserted in
  Alcotest.(check bool) "nan cursor gap is valid" true (gap >= 0 && gap <= 1);
  (* Defect 2: a delete the collection cannot perform must report failure with [None] rather than
     publishing an unchanged version as success. *)
  let at_pair =
    Option.get
      (Persistent_ordered_cursor.ordered_multimap_at 0
         (Persistent_ordered_cursor.ordered_multimap_snapshot inserted))
  in
  Alcotest.(check bool)
    "nan delete does not falsely succeed" true
    (Option.is_none (Persistent_ordered_cursor.ordered_multimap_delete_next at_pair));
  (* Inserting into a NON-last key's group must land the gap at that group's end, never the whole
     collection's end. Here [nan] joins the leading "a" group, so the flattened enumeration becomes
     [("a", 1.); ("a", nan); ("b", 2.)] and the gap is the group boundary (rank 2), not
     [pair_count] (rank 3), which the discarded value re-scan fallback would have produced. *)
  let grouped =
    Persistent_ordered_multimap.of_list ~key_policy:case_insensitive_policy
      ~value_policy:float_policy
      [ ("a", 1.); ("b", 2.) ]
  in
  let grouped_cursor = Option.get (Persistent_ordered_cursor.ordered_multimap_at 0 grouped) in
  let grouped_inserted =
    Persistent_ordered_cursor.ordered_multimap_insert "a" Float.nan grouped_cursor
  in
  Alcotest.(check int)
    "nan joins non-last group" 3
    (Persistent_ordered_multimap.pair_count
       (Persistent_ordered_cursor.ordered_multimap_snapshot grouped_inserted));
  Alcotest.(check int)
    "nan gap lands at non-last group end, not pair_count" 2
    (Persistent_ordered_cursor.ordered_multimap_position grouped_inserted)

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
          Alcotest.test_case "ordered cursors" `Quick test_ordered_cursors;
          Alcotest.test_case "ordered multimap nan cursor" `Quick test_ordered_multimap_cursor_nan;
          Alcotest.test_case "ordered-set model" `Quick test_model;
        ] );
      ("probes", Ordered_probe_tests.tests);
    ]
