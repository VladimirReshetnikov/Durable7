open Tools_data_structures
open Hamt

let int_policy () = Common.Hash_policy.create ~hash:(fun value -> value) ~equal:Int.equal

let test_map_persistence_and_collisions () =
  let collision_policy = Common.Hash_policy.create ~hash:(fun _ -> 7) ~equal:String.equal in
  let empty = Persistent_hamt.empty collision_policy in
  let first = Persistent_hamt.add "alpha" 1 empty in
  let second = first |> Persistent_hamt.add "beta" 2 |> Persistent_hamt.add "gamma" 3 in
  Alcotest.(check int) "empty retained" 0 (Persistent_hamt.count empty);
  Alcotest.(check int) "first retained" 1 (Persistent_hamt.count first);
  Alcotest.(check (option int)) "collision lookup" (Some 2) (Persistent_hamt.find_opt "beta" second);
  let removed = Persistent_hamt.remove "beta" second in
  Alcotest.(check (option int)) "removed" None (Persistent_hamt.find_opt "beta" removed);
  Alcotest.(check (option int)) "source retained" (Some 2) (Persistent_hamt.find_opt "beta" second)

let test_representative_and_single_descent_factories () =
  let policy =
    Common.Hash_policy.create
      ~hash:(fun value -> Hashtbl.hash (String.lowercase_ascii value))
      ~equal:(fun left right ->
        String.equal (String.lowercase_ascii left) (String.lowercase_ascii right))
  in
  let map = Persistent_hamt.singleton policy "First" 1 in
  let updated = Persistent_hamt.set "FIRST" 2 map in
  let entry = Option.get (Persistent_hamt.find_entry_opt "first" updated) in
  Alcotest.(check string) "stored representative" "First" (Persistent_hamt.entry_key entry);
  let calls = ref 0 in
  let unchanged, value, added =
    Persistent_hamt.get_or_add "fIrSt"
      (fun () ->
        incr calls;
        99)
      updated
  in
  Alcotest.(check int) "miss factory not called" 0 !calls;
  Alcotest.(check int) "stored value" 2 value;
  Alcotest.(check bool) "not added" false added;
  Alcotest.(check bool) "same map" true (unchanged == updated)

let test_builder_and_transient_lifecycle () =
  let source = Persistent_hamt.singleton (int_policy ()) 1 "one" in
  let builder = Persistent_hamt.Bulk_builder.create source in
  Persistent_hamt.Bulk_builder.set 2 "two" builder;
  let frozen = Persistent_hamt.Bulk_builder.freeze builder in
  Persistent_hamt.Bulk_builder.set 3 "three" builder;
  let later = Persistent_hamt.Bulk_builder.freeze builder in
  Alcotest.(check bool) "frozen detached" false (Persistent_hamt.mem 3 frozen);
  Alcotest.(check bool) "later contains edit" true (Persistent_hamt.mem 3 later);
  let transient = Persistent_hamt.Transient.create frozen in
  Persistent_hamt.Transient.set 4 "four" transient;
  let published = Persistent_hamt.Transient.persistent transient in
  Alcotest.(check (option string)) "published" (Some "four") (Persistent_hamt.find_opt 4 published);
  Alcotest.check_raises "consumed" Persistent_hamt.Transient_consumed (fun () ->
      ignore (Persistent_hamt.Transient.count transient))

let test_set_algebra () =
  let policy = int_policy () in
  let left = Persistent_hash_set.of_list policy [ 1; 2; 3 ] in
  let right = Persistent_hash_set.of_list policy [ 3; 4 ] in
  Alcotest.(check int) "union" 4 (Persistent_hash_set.count (Persistent_hash_set.union left right));
  Alcotest.(check (list int))
    "intersection" [ 3 ]
    (List.sort Int.compare (Persistent_hash_set.to_list (Persistent_hash_set.inter left right)));
  Alcotest.(check bool)
    "subset" true
    (Persistent_hash_set.subset (Persistent_hash_set.of_list policy [ 1; 2 ]) left);
  Alcotest.(check bool) "not disjoint" false (Persistent_hash_set.disjoint left right)

let test_bag_algebra () =
  let policy = int_policy () in
  let left =
    Persistent_hash_bag.empty policy
    |> Persistent_hash_bag.add ~count:2 1
    |> Persistent_hash_bag.add 2
  in
  let right = Persistent_hash_bag.empty policy |> Persistent_hash_bag.add ~count:3 1 in
  let sum = Persistent_hash_bag.sum left right in
  Alcotest.(check int) "multiplicity sum" 5 (Persistent_hash_bag.multiplicity 1 sum);
  Alcotest.(check int64) "expanded count" 6L (Persistent_hash_bag.expanded_count sum);
  Alcotest.(check int)
    "intersection" 2
    (Persistent_hash_bag.multiplicity 1 (Persistent_hash_bag.inter left right));
  Alcotest.(check int)
    "difference" 1
    (Persistent_hash_bag.multiplicity 1 (Persistent_hash_bag.diff right left))

let test_bimap_strictness () =
  let map =
    Persistent_bi_map.empty ~key_policy:(int_policy ())
      ~value_policy:(Common.Hash_policy.default ())
    |> Persistent_bi_map.add 1 "one" |> Persistent_bi_map.add 2 "two"
  in
  Alcotest.(check (option string)) "forward" (Some "one") (Persistent_bi_map.find_value_opt 1 map);
  Alcotest.(check (option int)) "inverse" (Some 2) (Persistent_bi_map.find_key_opt "two" map);
  (match Persistent_bi_map.try_add 3 "one" map with
  | Error Persistent_bi_map.Value_conflict -> ()
  | Error Persistent_bi_map.Key_conflict | Ok _ -> Alcotest.fail "expected a value conflict");
  let replaced = Persistent_bi_map.replace 2 "second" map in
  Alcotest.(check (option int))
    "old inverse removed" None
    (Persistent_bi_map.find_key_opt "two" replaced);
  Alcotest.(check (option int))
    "new inverse" (Some 2)
    (Persistent_bi_map.find_key_opt "second" replaced)

let map_model_property =
  let generator = QCheck.(list (pair (int_bound 63) (int_range (-10_000) 10_000))) in
  QCheck.Test.make ~count:200 ~name:"HAMT agrees with a mutable finite-map model" generator
    (fun operations ->
      let model = Hashtbl.create 64 in
      let map =
        List.fold_left
          (fun current (key, value) ->
            Hashtbl.replace model key value;
            Persistent_hamt.set key value current)
          (Persistent_hamt.empty (int_policy ()))
          operations
      in
      Persistent_hamt.count map = Hashtbl.length model
      && Hashtbl.fold
           (fun key value result -> result && Persistent_hamt.find_opt key map = Some value)
           model true)

let test_map_model () = QCheck.Test.check_exn map_model_property

let () =
  Alcotest.run "HAMT core"
    [
      ( "map",
        [
          Alcotest.test_case "persistence and collisions" `Quick test_map_persistence_and_collisions;
          Alcotest.test_case "representatives and factories" `Quick
            test_representative_and_single_descent_factories;
          Alcotest.test_case "builder and transient" `Quick test_builder_and_transient_lifecycle;
          Alcotest.test_case "model property" `Quick test_map_model;
        ] );
      ( "derived",
        [
          Alcotest.test_case "set algebra" `Quick test_set_algebra;
          Alcotest.test_case "bag algebra" `Quick test_bag_algebra;
          Alcotest.test_case "strict bimap" `Quick test_bimap_strictness;
        ] );
    ]
