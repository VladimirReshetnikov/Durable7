(** Tests for the persistent hash trie and everything derived from it, including the Merkle search
    tree's wire format. *)

open Durable7
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
      ignore (Persistent_hamt.Transient.count transient));
  let set_source = Persistent_hash_set.of_list (int_policy ()) [ 1; 2 ] in
  let set_session = Persistent_hash_set.Transient.create set_source in
  Persistent_hash_set.Transient.add 3 set_session;
  let set values = Persistent_hash_set.of_list (Persistent_hash_set.policy set_source) values in
  Alcotest.(check bool)
    "transient subset" true
    (Persistent_hash_set.Transient.subset set_session (set [ 1; 2; 3; 4 ]));
  Alcotest.(check bool)
    "transient proper subset" true
    (Persistent_hash_set.Transient.proper_subset set_session (set [ 1; 2; 3; 4 ]));
  Alcotest.(check bool)
    "transient superset" true
    (Persistent_hash_set.Transient.superset set_session (set [ 1; 2 ]));
  Alcotest.(check bool)
    "transient proper superset" true
    (Persistent_hash_set.Transient.proper_superset set_session (set [ 1; 2 ]));
  Alcotest.(check bool)
    "transient overlap" true
    (Persistent_hash_set.Transient.overlaps set_session (set [ 3; 9 ]));
  Alcotest.(check bool)
    "transient equality" true
    (Persistent_hash_set.Transient.equal set_session (set [ 1; 2; 3 ]));
  let set_published = Persistent_hash_set.Transient.persistent set_session in
  Alcotest.(check int) "set publication" 3 (Persistent_hash_set.count set_published);
  Alcotest.check_raises "set relation after publication" Persistent_hamt.Transient_consumed
    (fun () -> ignore (Persistent_hash_set.Transient.overlaps set_session (set [ 3 ])))

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

let test_multimap_and_relation () =
  let ints = int_policy () in
  let strings = Common.Hash_policy.default () in
  let multimap =
    Persistent_hash_multimap.empty ~key_policy:strings ~value_policy:ints
    |> Persistent_hash_multimap.add "odd" 1
    |> Persistent_hash_multimap.add "odd" 3
    |> Persistent_hash_multimap.add "even" 2
    |> Persistent_hash_multimap.add "odd" 3
  in
  Alcotest.(check int) "group count" 2 (Persistent_hash_multimap.key_count multimap);
  Alcotest.(check int) "pair count" 3 (Persistent_hash_multimap.pair_count multimap);
  let contracted =
    multimap
    |> Persistent_hash_multimap.remove "even" 2
    |> Persistent_hash_multimap.remove "missing" 4
  in
  Alcotest.(check bool)
    "empty groups contract" false
    (Persistent_hash_multimap.contains_key "even" contracted);
  let relation =
    Persistent_relation.empty ~left_policy:strings ~right_policy:ints
    |> Persistent_relation.add "a" 1 |> Persistent_relation.add "a" 2
    |> Persistent_relation.add "b" 2
  in
  Alcotest.(check int) "relation pairs" 3 (Persistent_relation.pair_count relation);
  Alcotest.(check bool) "forward" true (Persistent_relation.contains "a" 2 relation);
  Alcotest.(check bool)
    "inverse" true
    (Persistent_relation.contains 2 "a" (Persistent_relation.inverse relation));
  let removed = Persistent_relation.remove_left "a" relation in
  Alcotest.(check bool)
    "reverse index updated" false
    (Persistent_hash_set.mem "a" (Persistent_relation.lefts 2 removed))

let test_map_patch () =
  let policy = int_policy () in
  let before =
    Persistent_hamt.empty policy |> Persistent_hamt.add 1 "one" |> Persistent_hamt.add 2 "two"
  in
  let after =
    before |> Persistent_hamt.set 1 "first" |> Persistent_hamt.remove 2
    |> Persistent_hamt.add 3 "three"
  in
  let patch = Persistent_map_patch.between before after in
  Alcotest.(check int) "three changes" 3 (Persistent_map_patch.count patch);
  let applied = Result.get_ok (Persistent_map_patch.apply patch before) in
  Alcotest.(check bool) "apply" true (Persistent_hamt.equal after applied);
  let restored =
    Result.get_ok (Persistent_map_patch.apply (Persistent_map_patch.invert patch) applied)
  in
  Alcotest.(check bool) "invert" true (Persistent_hamt.equal before restored);
  let incompatible = Persistent_hamt.set 1 "unexpected" before in
  (match Persistent_map_patch.apply patch incompatible with
  | Error _ -> ()
  | Ok _ -> Alcotest.fail "strict application should reject an unexpected source");
  let final_map = Persistent_hamt.set 3 "THREE" after in
  let second = Persistent_map_patch.between after final_map in
  let composed = Result.get_ok (Persistent_map_patch.compose patch second) in
  Alcotest.(check bool)
    "compose" true
    (Persistent_hamt.equal final_map (Result.get_ok (Persistent_map_patch.apply composed before)))

let test_directed_graph () =
  let graph =
    Persistent_directed_graph.empty (int_policy ())
    |> Persistent_directed_graph.add_vertex 99
    |> Persistent_directed_graph.add_edge 1 2
    |> Persistent_directed_graph.add_edge 2 3
    |> Persistent_directed_graph.add_edge 3 1
  in
  Alcotest.(check int)
    "explicit and endpoint vertices" 4
    (Persistent_directed_graph.vertex_count graph);
  Alcotest.(check int) "edges" 3 (Persistent_directed_graph.edge_count graph);
  Alcotest.(check bool)
    "outgoing" true
    (Persistent_hash_set.mem 2 (Persistent_directed_graph.outgoing 1 graph));
  Alcotest.(check bool)
    "reversed" true
    (Persistent_directed_graph.contains_edge 2 1 (Persistent_directed_graph.reverse graph));
  let removed = Persistent_directed_graph.remove_vertex 2 graph in
  Alcotest.(check int) "incident edges removed" 1 (Persistent_directed_graph.edge_count removed);
  Alcotest.(check bool)
    "isolated retained" true
    (Persistent_directed_graph.contains_vertex 99 removed)

let test_indexed_map () =
  let map =
    Persistent_indexed_map.empty ~key_policy:(int_policy ())
      ~index_policy:(Common.Hash_policy.default ())
      ~index_selector:(fun _ value -> String.sub value 0 1)
      ()
    |> Persistent_indexed_map.add 1 "alpha"
    |> Persistent_indexed_map.add 2 "apple"
    |> Persistent_indexed_map.add 3 "beta"
  in
  Alcotest.(check int) "index groups" 2 (Persistent_indexed_map.index_count map);
  Alcotest.(check int)
    "nonunique group" 2
    (Persistent_hash_set.count (Persistent_indexed_map.keys_for_index "a" map));
  let moved = Persistent_indexed_map.set 2 "charlie" map in
  Alcotest.(check bool)
    "old group updated" false
    (Persistent_hash_set.mem 2 (Persistent_indexed_map.keys_for_index "a" moved));
  Alcotest.(check bool)
    "new group updated" true
    (Persistent_hash_set.mem 2 (Persistent_indexed_map.keys_for_index "c" moved));
  let removed = Persistent_indexed_map.remove 2 moved in
  Alcotest.(check int) "empty index contracted" 2 (Persistent_indexed_map.index_count removed);
  Alcotest.(check int)
    "removed group empty" 0
    (Persistent_hash_set.count (Persistent_indexed_map.keys_for_index "c" removed))

let test_patricia_maps_and_sets () =
  let map =
    Persistent_patricia.Int32_map.empty
    |> Persistent_patricia.Int32_map.add Int32.min_int "minimum"
    |> Persistent_patricia.Int32_map.add 0l "zero"
    |> Persistent_patricia.Int32_map.add Int32.max_int "maximum"
    |> Persistent_patricia.Int32_map.add (-1l) "minus-one"
  in
  Alcotest.(check int) "int32 count" 4 (Persistent_patricia.Int32_map.count map);
  Alcotest.(check (option string))
    "high-bit lookup" (Some "minimum")
    (Persistent_patricia.Int32_map.find_opt Int32.min_int map);
  let removed = Persistent_patricia.Int32_map.remove 0l map in
  Alcotest.(check bool) "source retained" true (Persistent_patricia.Int32_map.mem 0l map);
  Alcotest.(check bool) "removed" false (Persistent_patricia.Int32_map.mem 0l removed);
  let set =
    Persistent_patricia.Int64_set.empty
    |> Persistent_patricia.Int64_set.add Int64.min_int
    |> Persistent_patricia.Int64_set.add 0L
    |> Persistent_patricia.Int64_set.add Int64.max_int
  in
  Alcotest.(check int) "int64 set" 3 (Persistent_patricia.Int64_set.count set);
  Alcotest.(check (list int32))
    "int32 signed order"
    [ Int32.min_int; -1l; 0l; Int32.max_int ]
    (List.map fst (Persistent_patricia.Int32_map.to_list map))

let test_patricia_cursors () =
  let module M = Persistent_patricia.Int32_map in
  let module C = M.Cursor in
  let keys = [ Int32.min_int; -1l; 0l; 17l; Int32.max_int ] in
  let map =
    List.fold_left
      (fun map key ->
        M.set key (if Int32.equal key 0l then None else Some (Int32.to_string key)) map)
      M.empty keys
  in
  List.iteri
    (fun position _ ->
      let cursor = Option.get (C.at position map) in
      Alcotest.(check int) "cursor position" position (C.position cursor);
      Alcotest.(check int) "cursor count" (List.length keys) (C.count cursor);
      Alcotest.(check bool) "cursor start" (position = 0) (C.is_at_start cursor);
      Alcotest.(check bool) "cursor end" (position = List.length keys) (C.is_at_end cursor);
      Alcotest.(check (option int32))
        "cursor previous"
        (if position = 0 then None else List.nth_opt keys (position - 1))
        (Option.map fst (C.peek_previous cursor));
      Alcotest.(check (option int32))
        "cursor next" (List.nth_opt keys position)
        (Option.map fst (C.peek_next cursor));
      Alcotest.(check (list int32))
        "cursor snapshot" keys
        (List.map fst (M.to_list (C.snapshot cursor))))
    (keys @ [ 1l ]);
  Alcotest.(check int) "lower bound" 1 (C.position (C.lower_bound (-2l) map));
  Alcotest.(check int) "upper bound" 2 (C.position (C.upper_bound (-1l) map));
  Alcotest.(check int) "missing lower bound" 4 (C.position (C.lower_bound 18l map));
  Alcotest.(check int)
    "maximum upper bound" (List.length keys)
    (C.position (C.upper_bound Int32.max_int map));
  let exact, found = C.exact 0l map in
  Alcotest.(check bool) "exact hit" true found;
  Alcotest.(check (option (pair int32 (option string))))
    "stored None entry"
    (Some (0l, None))
    (C.peek_next exact);
  let miss, found = C.exact 1l map in
  Alcotest.(check bool) "exact miss" false found;
  Alcotest.(check int) "exact miss rank" 3 (C.position miss);
  Alcotest.(check (option int32))
    "exact miss candidate" (Some 17l)
    (Option.map fst (C.peek_next miss));
  Alcotest.(check bool) "negative rank rejected" true (Option.is_none (C.at (-1) map));
  Alcotest.(check bool) "excessive rank rejected" true (Option.is_none (C.at (M.count map + 1) map));
  Alcotest.(check bool) "start move previous" true (Option.is_none (C.move_previous (C.start map)));
  Alcotest.(check bool) "end move next" true (Option.is_none (C.move_next (C.at_end map)));

  let source = M.empty |> M.set (-10l) (Some "a") |> M.set 0l None |> M.set 10l (Some "c") in
  let at_zero = fst (C.exact 0l source) in
  let updated = Option.get (C.set_next_value (Some "b") at_zero) in
  Alcotest.(check int) "update rank" 1 (C.position updated);
  Alcotest.(check (option (option string)))
    "cursor update" (Some (Some "b"))
    (M.find_opt 0l (C.snapshot updated));
  Alcotest.(check (option (option string))) "retained source" (Some None) (M.find_opt 0l source);
  Alcotest.(check (list int32))
    "delete next" [ -10l; 10l ]
    (List.map fst (M.to_list (C.snapshot (Option.get (C.delete_next at_zero)))));
  Alcotest.(check (list int32))
    "delete previous" [ 0l; 10l ]
    (List.map fst (M.to_list (C.snapshot (Option.get (C.delete_previous at_zero)))));
  let inserted = Option.get (C.insert 5l (Some "five") (fst (C.exact 5l source))) in
  Alcotest.(check int) "insertion rank" 3 (C.position inserted);
  Alcotest.(check (list int32))
    "cursor insertion" [ -10l; 0l; 5l; 10l ]
    (List.map fst (M.to_list (C.snapshot inserted)));
  Alcotest.(check (list int32))
    "insertion retained source" [ -10l; 0l; 10l ]
    (List.map fst (M.to_list source));
  Alcotest.(check bool) "strict duplicate rejected" true (Option.is_none (C.insert 0l None at_zero));
  Alcotest.(check bool)
    "wrong gap rejected" true
    (Option.is_none (C.insert 5l (Some "wrong") (C.start source)));
  Alcotest.(check bool)
    "end update rejected" true
    (Option.is_none (C.set_next_value None (C.at_end source)));
  Alcotest.(check bool)
    "start delete rejected" true
    (Option.is_none (C.delete_previous (C.start source)));
  Alcotest.(check bool)
    "end delete rejected" true
    (Option.is_none (C.delete_next (C.at_end source)));

  let module LM = Persistent_patricia.Int64_map in
  let module LC = LM.Cursor in
  let long_map =
    LM.empty
    |> LM.set Int64.min_int Int64.min_int
    |> LM.set (-1L) (-1L) |> LM.set 0L 0L
    |> LM.set (Int64.shift_left 1L 40) 1L
    |> LM.set Int64.max_int Int64.max_int
  in
  Alcotest.(check int)
    "int64 lower boundary" 0
    (LC.position (LC.lower_bound Int64.min_int long_map));
  Alcotest.(check int)
    "int64 upper boundary" 1
    (LC.position (LC.upper_bound Int64.min_int long_map));
  Alcotest.(check int) "int64 missing lower" 3 (LC.position (LC.lower_bound 1L long_map));
  Alcotest.(check int) "int64 maximum upper" 5 (LC.position (LC.upper_bound Int64.max_int long_map));

  let module S = Persistent_patricia.Int32_set in
  let module SC = S.Cursor in
  let set =
    List.fold_left
      (fun set value -> S.add value set)
      S.empty
      [ Int32.min_int; -1l; 0l; Int32.max_int ]
  in
  let set_miss, found = SC.exact (-2l) set in
  Alcotest.(check bool) "set exact miss" false found;
  let set_added = Option.get (SC.add (-2l) set_miss) in
  Alcotest.(check int) "set insertion rank" 2 (SC.position set_added);
  Alcotest.(check (list int32))
    "set cursor insertion"
    [ Int32.min_int; -2l; -1l; 0l; Int32.max_int ]
    (S.to_list (SC.snapshot set_added));
  let set_exact, found = SC.exact 0l set in
  Alcotest.(check bool) "set exact hit" true found;
  Alcotest.(check bool)
    "set duplicate no-op" true
    (SC.snapshot (Option.get (SC.add 0l set_exact)) == set);

  let state = ref 0x6d2b79f5 in
  for _history = 0 to 127 do
    let generated = ref [] in
    for _index = 0 to 63 do
      state := (!state * 1_664_525) + 1_013_904_223;
      generated := Int32.of_int (((!state lsr 8) mod 1_001) - 500) :: !generated
    done;
    let sorted = List.sort_uniq Int32.compare !generated in
    let random_map = List.fold_left (fun map key -> M.set key key map) M.empty sorted in
    for probe = -550 to 550 do
      if probe mod 22 = 0 then (
        let probe = Int32.of_int probe in
        let lower = List.length (List.filter (fun key -> Int32.compare key probe < 0) sorted) in
        let upper = List.length (List.filter (fun key -> Int32.compare key probe <= 0) sorted) in
        Alcotest.(check int) "random lower rank" lower (C.position (C.lower_bound probe random_map));
        Alcotest.(check int) "random upper rank" upper (C.position (C.upper_bound probe random_map));
        let searched, found = C.exact probe random_map in
        Alcotest.(check int) "random exact rank" lower (C.position searched);
        Alcotest.(check bool) "random exact found" (List.mem probe sorted) found)
    done
  done

let test_concurrent_snapshot_facade () =
  let trie = Concurrent_hash_trie.create (int_policy ()) in
  Concurrent_hash_trie.set 1 "one" trie;
  let snapshot = Concurrent_hash_trie.snapshot trie in
  Concurrent_hash_trie.set 1 "first" trie;
  Concurrent_hash_trie.set 2 "two" trie;
  Alcotest.(check (option string))
    "snapshot isolated" (Some "one")
    (Concurrent_hash_trie.snapshot_find_opt 1 snapshot);
  Alcotest.(check (option string))
    "snapshot excludes later key" None
    (Concurrent_hash_trie.snapshot_find_opt 2 snapshot);
  let worker start =
    Thread.create
      (fun () ->
        for key = start to start + 99 do
          Concurrent_hash_trie.set key (string_of_int key) trie
        done)
      ()
  in
  let first = worker 1000 in
  let second = worker 2000 in
  Thread.join first;
  Thread.join second;
  Alcotest.(check int) "serialized concurrent writes" 202 (Concurrent_hash_trie.count trie);
  Alcotest.(check bool)
    "generation advances" true
    (Int64.compare (Concurrent_hash_trie.generation trie) 200L >= 0)

let bytes_hex bytes =
  let alphabet = "0123456789abcdef" in
  String.init
    (Bytes.length bytes * 2)
    (fun index ->
      let value = Char.code (Bytes.get bytes (index / 2)) in
      alphabet.[if index mod 2 = 0 then value lsr 4 else value land 15])

let test_merkle_golden_wire () =
  let policy =
    Result.get_ok
      (Merkle_encoding.create_policy ~policy_id:"golden-int-string-v1" ~compare:Int32.compare
         ~key_codec:Merkle_encoding.int32_codec ~value_codec:Merkle_encoding.nullable_utf8_codec ())
  in
  Alcotest.(check string)
    "policy domain" "fe140762a080abb39de83f70e7505c8b94c4baa428eea76d468a0f3163bc56c2"
    (Merkle_encoding.digest_hex (Merkle_encoding.domain_digest policy));
  Alcotest.(check string)
    "empty root" "98900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb3"
    (Merkle_encoding.digest_hex (Merkle_encoding.empty_digest policy));
  let tree =
    Result.get_ok (Merkle_search_tree.add 42l (Some "forty-two") (Merkle_search_tree.empty policy))
  in
  Alcotest.(check string)
    "single-entry root" "1b464818e8934692ad28f35f520fa0c834634e2200f9e5873d0327e6524bcc94"
    (Merkle_encoding.digest_hex (Merkle_search_tree.root_digest tree));
  let block = List.hd (Merkle_search_tree.blocks_preorder tree) in
  Alcotest.(check string)
    "exact MST2 block"
    "4d53543201fe140762a080abb39de83f70e7505c8b94c4baa428eea76d468a0f3163bc56c2000000000100000001000000040000002a0000000a01666f7274792d74776f98900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb398900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb3"
    (bytes_hex block.Merkle_search_tree.content);
  Alcotest.(check (result unit string)) "valid" (Ok ()) (Merkle_search_tree.validate tree)

let merkle_int_policy policy_id =
  Result.get_ok
    (Merkle_encoding.create_policy ~policy_id ~compare:Int32.compare
       ~key_codec:Merkle_encoding.int32_codec ~value_codec:Merkle_encoding.int32_codec ())

let test_merkle_persistence_and_proofs () =
  let policy = merkle_int_policy "ocaml-persistence-int32-v1" in
  let entries = List.init 100 (fun index -> (Int32.of_int index, Int32.of_int (-index - 1))) in
  let tree = Result.get_ok (Merkle_search_tree.of_list policy entries) in
  Alcotest.(check bool) "multi-block tree" true (Merkle_search_tree.block_count tree > 1);
  let added, store = Result.get_ok (Merkle_persistence.save tree Merkle_persistence.empty_store) in
  Alcotest.(check int) "all blocks saved" (Merkle_search_tree.block_count tree) added;
  let added_again, same_store = Result.get_ok (Merkle_persistence.save tree store) in
  Alcotest.(check int) "save is idempotent" 0 added_again;
  Alcotest.(check int)
    "store remains deduplicated"
    (Merkle_search_tree.block_count tree)
    (Merkle_persistence.store_count same_store);
  let loaded =
    Result.get_ok (Merkle_persistence.load (Merkle_search_tree.root_digest tree) policy store)
  in
  Alcotest.(check string)
    "root round trips"
    (Merkle_encoding.digest_hex (Merkle_search_tree.root_digest tree))
    (Merkle_encoding.digest_hex (Merkle_search_tree.root_digest loaded));
  Alcotest.(check (list (pair int32 int32)))
    "entries round trip" entries
    (List.map
       (fun entry -> (Merkle_search_tree.entry_key entry, Merkle_search_tree.entry_value entry))
       (Merkle_search_tree.to_list loaded));
  let pack = Merkle_persistence.export_pack tree in
  let imported, imported_store =
    Result.get_ok (Merkle_persistence.import_pack pack policy Merkle_persistence.empty_store)
  in
  Alcotest.(check string)
    "pack import preserves root"
    (Merkle_encoding.digest_hex (Merkle_search_tree.root_digest tree))
    (Merkle_encoding.digest_hex (Merkle_search_tree.root_digest imported));
  Alcotest.(check int)
    "complete peer needs no blocks" 0
    (List.length
       (Merkle_persistence.pack_blocks (Merkle_persistence.missing_pack tree imported_store)));
  let tight_budget =
    { Merkle_persistence.default_budget with Merkle_persistence.maximum_blocks = 1 }
  in
  (match
     Merkle_persistence.load ~budget:tight_budget (Merkle_search_tree.root_digest tree) policy store
   with
  | Error problem ->
      if problem.Merkle_persistence.error_kind <> Merkle_persistence.Resource_limit_exceeded then
        Alcotest.failf "unexpected budget failure: %s" problem.Merkle_persistence.error_message
  | Ok _ -> Alcotest.fail "a one-block budget unexpectedly loaded a multi-block tree");
  let membership = Merkle_proof_merge.create_proof tree 42l in
  Alcotest.(check string)
    "MSP2 query envelope" "4d535032"
    (String.sub (bytes_hex (Merkle_proof_merge.proof_query_bytes membership)) 0 8);
  let membership_result = Merkle_proof_merge.verify membership policy in
  Alcotest.(check bool) "membership proof verifies" true membership_result.Merkle_proof_merge.valid;
  Alcotest.(check int)
    "membership returns one entry" 1
    (List.length membership_result.Merkle_proof_merge.entries);
  let absence_result =
    Merkle_proof_merge.verify (Merkle_proof_merge.create_proof tree 999l) policy
  in
  Alcotest.(check bool) "nonmembership proof verifies" true absence_result.Merkle_proof_merge.valid;
  let range_result =
    Merkle_proof_merge.verify (Merkle_proof_merge.create_range_proof tree 10l 19l) policy
  in
  Alcotest.(check bool) "range proof verifies" true range_result.Merkle_proof_merge.valid;
  Alcotest.(check int)
    "range proof materializes bounds" 10
    (List.length range_result.Merkle_proof_merge.entries)

let test_merkle_three_way_merge () =
  let policy = merkle_int_policy "ocaml-merge-int32-v1" in
  let make entries = Result.get_ok (Merkle_search_tree.of_list policy entries) in
  let base = make [ (1l, 10l); (2l, 20l) ] in
  let left = Result.get_ok (Merkle_search_tree.set 1l 11l base) in
  let right = Result.get_ok (Merkle_search_tree.add 3l 30l base) in
  (match
     Merkle_proof_merge.merge ~base ~left ~right ~resolve:(fun _ _ _ _ ->
         Merkle_proof_merge.Conflict)
   with
  | Merkle_proof_merge.Merged merged ->
      Alcotest.(check (option int32))
        "left update retained" (Some 11l)
        (Merkle_search_tree.find_opt 1l merged);
      Alcotest.(check (option int32))
        "right insertion retained" (Some 30l)
        (Merkle_search_tree.find_opt 3l merged)
  | Merkle_proof_merge.Conflicted _ -> Alcotest.fail "independent changes conflicted");
  let competing_left = Result.get_ok (Merkle_search_tree.set 2l 21l base) in
  let competing_right = Result.get_ok (Merkle_search_tree.set 2l 22l base) in
  match
    Merkle_proof_merge.merge ~base ~left:competing_left ~right:competing_right
      ~resolve:(fun _ _ _ _ -> Merkle_proof_merge.Conflict)
  with
  | Merkle_proof_merge.Conflicted [ key ] -> Alcotest.(check int32) "conflict key" 2l key
  | Merkle_proof_merge.Conflicted _ -> Alcotest.fail "unexpected conflict set"
  | Merkle_proof_merge.Merged _ -> Alcotest.fail "competing updates unexpectedly merged"

let test_merkle_cursors () =
  let policy =
    Result.get_ok
      (Merkle_encoding.create_policy ~policy_id:"ocaml-cursor-int32-v1" ~compare:Int32.compare
         ~key_codec:Merkle_encoding.int32_codec ~value_codec:Merkle_encoding.nullable_utf8_codec ())
  in
  let source =
    Result.get_ok
      (Merkle_search_tree.of_list policy [ (-10l, Some "a"); (0l, None); (10l, Some "c") ])
  in
  let keys = [ -10l; 0l; 10l ] in
  List.iter
    (fun position ->
      let cursor = Option.get (Merkle_search_tree.Cursor.at position source) in
      Alcotest.(check int) "cursor position" position (Merkle_search_tree.Cursor.position cursor);
      Alcotest.(check bool)
        "cursor start" (position = 0)
        (Merkle_search_tree.Cursor.is_at_start cursor);
      Alcotest.(check bool)
        "cursor end"
        (position = Merkle_search_tree.count source)
        (Merkle_search_tree.Cursor.is_at_end cursor);
      Alcotest.(check (option int32))
        "cursor previous"
        (if position = 0 then None else List.nth_opt keys (position - 1))
        (Option.map Merkle_search_tree.entry_key (Merkle_search_tree.Cursor.peek_previous cursor));
      Alcotest.(check (option int32))
        "cursor next" (List.nth_opt keys position)
        (Option.map Merkle_search_tree.entry_key (Merkle_search_tree.Cursor.peek_next cursor)))
    (List.init (Merkle_search_tree.count source + 1) Fun.id);
  Alcotest.(check int)
    "cursor lower bound" 1
    (Merkle_search_tree.Cursor.position (Merkle_search_tree.Cursor.lower_bound (-5l) source));
  Alcotest.(check int)
    "cursor upper bound" 2
    (Merkle_search_tree.Cursor.position (Merkle_search_tree.Cursor.upper_bound 0l source));
  let exact, found = Merkle_search_tree.Cursor.exact 0l source in
  Alcotest.(check bool) "cursor exact hit" true found;
  let miss, found = Merkle_search_tree.Cursor.exact 5l source in
  Alcotest.(check bool) "cursor exact miss" false found;
  Alcotest.(check int) "cursor exact miss rank" 2 (Merkle_search_tree.Cursor.position miss);
  let changed =
    Option.get (Result.get_ok (Merkle_search_tree.Cursor.set_next_value (Some "b") exact))
  in
  Alcotest.(check (option (option string)))
    "cursor changed value" (Some (Some "b"))
    (Merkle_search_tree.find_opt 0l (Merkle_search_tree.Cursor.snapshot changed));
  Alcotest.(check (option (option string)))
    "cursor source value" (Some None)
    (Merkle_search_tree.find_opt 0l source);
  let inserted =
    Result.get_ok
      (Merkle_search_tree.Cursor.insert 5l (Some "five")
         (Merkle_search_tree.Cursor.lower_bound 5l source))
  in
  Alcotest.(check int) "cursor insertion position" 3 (Merkle_search_tree.Cursor.position inserted);
  Alcotest.(check (list int32))
    "cursor insertion keys" [ -10l; 0l; 5l; 10l ]
    (List.map Merkle_search_tree.entry_key
       (Merkle_search_tree.to_list (Merkle_search_tree.Cursor.snapshot inserted)));
  let restored = Option.get (Result.get_ok (Merkle_search_tree.Cursor.delete_previous inserted)) in
  Alcotest.(check string)
    "cursor restored root"
    (Merkle_encoding.digest_hex (Merkle_search_tree.root_digest source))
    (Merkle_encoding.digest_hex
       (Merkle_search_tree.root_digest (Merkle_search_tree.Cursor.snapshot restored)));
  Alcotest.(check bool)
    "cursor invalid rank" true
    (Option.is_none (Merkle_search_tree.Cursor.at 4 source));
  Alcotest.(check bool)
    "cursor before start" true
    (Option.is_none
       (Merkle_search_tree.Cursor.move_previous (Merkle_search_tree.Cursor.start source)));
  Alcotest.(check bool)
    "cursor after end" true
    (Option.is_none (Merkle_search_tree.Cursor.move_next (Merkle_search_tree.Cursor.at_end source)));
  Alcotest.(check bool)
    "cursor duplicate" true
    (Result.is_error (Merkle_search_tree.Cursor.insert 0l (Some "duplicate") exact));
  Alcotest.(check bool)
    "cursor wrong gap" true
    (Result.is_error
       (Merkle_search_tree.Cursor.insert 5l (Some "wrong gap")
          (Merkle_search_tree.Cursor.start source)));
  let rank_keys =
    List.filter
      (fun key -> Int32.rem key 7l <> 0l)
      (List.init 1001 (fun i -> Int32.of_int (i - 500)))
  in
  let rank_tree =
    Result.get_ok
      (Merkle_search_tree.of_list policy
         (List.map (fun key -> (key, Some (Int32.to_string key))) rank_keys))
  in
  List.iter
    (fun probe ->
      let rank = List.length (List.filter (fun key -> Int32.compare key probe < 0) rank_keys) in
      let found = List.nth_opt rank_keys rank = Some probe in
      Alcotest.(check int)
        "cursor model lower rank" rank
        (Merkle_search_tree.Cursor.position (Merkle_search_tree.Cursor.lower_bound probe rank_tree));
      Alcotest.(check int)
        "cursor model upper rank"
        (rank + if found then 1 else 0)
        (Merkle_search_tree.Cursor.position (Merkle_search_tree.Cursor.upper_bound probe rank_tree));
      let searched, actual_found = Merkle_search_tree.Cursor.exact probe rank_tree in
      Alcotest.(check int)
        "cursor model exact rank" rank
        (Merkle_search_tree.Cursor.position searched);
      Alcotest.(check bool) "cursor model exact presence" found actual_found)
    (List.init 101 (fun i -> Int32.of_int (-550 + (i * 11))))

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
          Alcotest.test_case "multimap and relation" `Quick test_multimap_and_relation;
          Alcotest.test_case "strict map patch" `Quick test_map_patch;
          Alcotest.test_case "directed graph" `Quick test_directed_graph;
          Alcotest.test_case "indexed map" `Quick test_indexed_map;
          Alcotest.test_case "Patricia maps and sets" `Quick test_patricia_maps_and_sets;
          Alcotest.test_case "Patricia cursors" `Quick test_patricia_cursors;
          Alcotest.test_case "concurrent snapshots" `Quick test_concurrent_snapshot_facade;
          Alcotest.test_case "Merkle golden wire" `Quick test_merkle_golden_wire;
          Alcotest.test_case "Merkle cursors" `Quick test_merkle_cursors;
          Alcotest.test_case "Merkle persistence and proofs" `Quick
            test_merkle_persistence_and_proofs;
          Alcotest.test_case "Merkle three-way merge" `Quick test_merkle_three_way_merge;
        ] );
      ("ancestral-connection-forest", Persistent_ancestral_connection_forest_tests.tests);
    ]
