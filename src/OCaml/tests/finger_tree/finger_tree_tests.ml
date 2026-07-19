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

let test_sequence_cursors () =
  let deque_source = Persistent_deque.of_list [ Some 1; None; Some 3 ] in
  let deque = Option.get (Persistent_deque.cursor_at 2 deque_source) in
  Alcotest.(check (option (option int)))
    "stored None is present" (Some None)
    (Persistent_deque.cursor_peek_previous deque);
  let deque = Persistent_deque.cursor_insert_many [ Some 7; Some 8 ] deque in
  let deque = Option.get (Persistent_deque.cursor_delete_previous deque) in
  let deque = Option.get (Persistent_deque.cursor_replace_next (Some 9) deque) in
  Alcotest.(check int) "deque cursor position" 3 (Persistent_deque.cursor_position deque);
  Alcotest.(check (list (option int)))
    "deque cursor edit" [ Some 1; None; Some 7; Some 9 ]
    (Persistent_deque.to_list (Persistent_deque.cursor_snapshot deque));
  Alcotest.(check (list (option int)))
    "deque source retained" [ Some 1; None; Some 3 ]
    (Persistent_deque.to_list deque_source);

  let reversible_source = Reversible_deque.reverse (Reversible_deque.of_list [ 1; 2; 3; 4 ]) in
  let reversible = Option.get (Reversible_deque.cursor_at 1 reversible_source) in
  let reversible = Reversible_deque.cursor_insert 9 reversible in
  let reversible = Option.get (Reversible_deque.cursor_delete_next reversible) in
  check_int_list "reversible cursor edit" [ 4; 9; 2; 1 ]
    (Reversible_deque.to_list (Reversible_deque.cursor_snapshot reversible));
  let reversed = Reversible_deque.cursor_reverse reversible in
  Alcotest.(check int) "reversed cursor position" 2 (Reversible_deque.cursor_position reversed);
  check_int_list "reversed cursor snapshot" [ 1; 2; 9; 4 ]
    (Reversible_deque.to_list (Reversible_deque.cursor_snapshot reversed));

  let measured_source = Measured_tree.of_list Measures.int_sum [ 2; 3; 5; 7 ] in
  let found, measured = Measured_tree.cursor_by_measure (fun total -> total >= 6) measured_source in
  Alcotest.(check bool) "measured cursor found" true found;
  Alcotest.(check int) "measure before" 5 (Measured_tree.cursor_measure_before measured);
  Alcotest.(check int) "measure after" 12 (Measured_tree.cursor_measure_after measured);
  let measured = Measured_tree.cursor_insert 11 measured in
  let measured = Option.get (Measured_tree.cursor_delete_next measured) in
  let measured = Option.get (Measured_tree.cursor_replace_next 13 measured) in
  check_int_list "measured cursor edit" [ 2; 3; 11; 13 ]
    (Measured_tree.to_list (Measured_tree.cursor_snapshot measured));
  check_int_list "measured source retained" [ 2; 3; 5; 7 ] (Measured_tree.to_list measured_source);

  (* A monotone predicate already true at the identity selects the start gap on a nonempty tree,
     while an empty tree remains a miss with a usable end cursor. *)
  let identity_found, identity_cursor =
    Measured_tree.cursor_by_measure (fun total -> total >= 0) measured_source
  in
  Alcotest.(check bool) "identity predicate found" true identity_found;
  Alcotest.(check bool)
    "identity predicate at start" true
    (Measured_tree.cursor_is_at_start identity_cursor);
  Alcotest.(check int) "identity predicate measure before" 0
    (Measured_tree.cursor_measure_before identity_cursor);
  Alcotest.(check int) "identity predicate measure after" 17
    (Measured_tree.cursor_measure_after identity_cursor);
  let empty_found, empty_cursor =
    Measured_tree.cursor_by_measure
      (fun total -> total >= 0)
      (Measured_tree.empty Measures.int_sum)
  in
  Alcotest.(check bool) "identity predicate empty miss" false empty_found;
  Alcotest.(check bool)
    "identity predicate empty end" true
    (Measured_tree.cursor_is_at_end empty_cursor);

  let vector_source = Rrb_vector.of_list (List.init 96 Fun.id) in
  let vector = Option.get (Rrb_vector.cursor_at 32 vector_source) in
  let vector = Rrb_vector.cursor_insert_vector (Rrb_vector.of_list [ 500; 501; 502 ]) vector in
  Alcotest.(check int) "RRB cursor position" 35 (Rrb_vector.cursor_position vector);
  check_int_list "RRB cursor splice" [ 30; 31; 500; 501; 502; 32; 33 ]
    (Rrb_vector.to_list
       (Result.get_ok (Rrb_vector.slice ~start:30 ~length:7 (Rrb_vector.cursor_snapshot vector))));
  check_int_list "RRB source retained" (List.init 96 Fun.id) (Rrb_vector.to_list vector_source);

  let algebra =
    Result.get_ok
      (Range_update_sequence.create_algebra ~id:"cursor-add-sum-v1" ~identity:0 ~combine:( + )
         ~measure:Fun.id ~apply_element:( + )
         ~apply_measure:(fun tag ~length measure -> measure + (tag * length))
         ~compose:( + ) ~laws_verified:true ())
  in
  let range_source = Range_update_sequence.of_list algebra [ 1; 2; 3; 4 ] in
  let range = Option.get (Range_update_sequence.cursor_at 2 range_source) in
  Alcotest.(check int) "Range measure before" 3 (Range_update_sequence.cursor_measure_before range);
  Alcotest.(check int) "Range measure after" 7 (Range_update_sequence.cursor_measure_after range);
  let range = Result.get_ok (Range_update_sequence.cursor_update_previous 1 10 range) in
  let range = Result.get_ok (Range_update_sequence.cursor_update_next 2 20 range) in
  let range = Option.get (Range_update_sequence.cursor_replace_next 99 range) in
  check_int_list "Range cursor edit" [ 1; 12; 99; 24 ]
    (Range_update_sequence.to_list (Range_update_sequence.cursor_snapshot range));
  check_int_list "Range source retained" [ 1; 2; 3; 4 ] (Range_update_sequence.to_list range_source)

let test_rope_and_cursor () =
  let source = Rope.of_list [ 1; 2; 3; 4 ] in
  let cursor = Result.get_ok (Rope_cursor.create ~position:2 source) in
  let cursor = Rope_cursor.insert_many [ 8; 9 ] cursor in
  let edited = Result.get_ok (Rope_cursor.delete_after 1 cursor) in
  check_int_list "cursor edit" [ 1; 2; 8; 9; 4 ] (Rope.to_list (Rope_cursor.rope edited));
  check_int_list "cursor source retained" [ 1; 2; 3; 4 ] (Rope.to_list source);
  Alcotest.(check int) "cursor position after insertion" 4 (Rope_cursor.position edited);
  let builder = Rope.Builder.create () in
  Rope.Builder.append 1 builder;
  Rope.Builder.append_rope (Rope.of_list [ 2; 3 ]) builder;
  check_int_list "rope builder" [ 1; 2; 3 ] (Rope.to_list (Rope.Builder.freeze builder));
  let measured = Measured_rope.of_list Measures.int_sum [ 2; 3; 5; 7 ] in
  let measured_cursor = Result.get_ok (Measured_rope.create_cursor ~position:3 measured) in
  Alcotest.(check int)
    "measure before cursor" 10
    (Measured_rope.cursor_measure_before measured_cursor)

let test_text_rope_and_cursor () =
  let source = Result.get_ok (Text_rope.of_utf8 "α\n😀z") in
  Alcotest.(check int) "Unicode scalar length" 4 (Text_rope.length source);
  Alcotest.(check string) "UTF-8 round trip" "α\n😀z" (Text_rope.to_utf8 source);
  Alcotest.(check int) "line count" 2 (Text_rope.line_count source);
  Alcotest.(check (result (pair int int) string))
    "line column"
    (Ok (1, 1))
    (Text_rope.line_column 3 source);
  Alcotest.(check (result int string))
    "reverse line lookup" (Ok 3)
    (Text_rope.index_of_line_column ~line:1 ~column:1 source);
  let cursor = Result.get_ok (Text_rope_cursor.create ~position:2 source) in
  let cursor = Result.get_ok (Text_rope_cursor.insert_utf8 "λ" cursor) in
  Alcotest.(check string)
    "text cursor insertion" "α\nλ😀z"
    (Text_rope.to_utf8 (Text_rope_cursor.text cursor));
  Alcotest.(check (result (option int) string))
    "code-point search" (Ok (Some 3))
    (Text_rope_cursor.find_forward "😀" cursor);
  Alcotest.(check bool)
    "malformed UTF-8 rejected" true
    (Result.is_error (Text_rope.of_utf8 "\xC3\x28"))

let test_canonical_sorted_set () =
  let policy =
    Canonical_sorted_set.create_policy ~comparator:int_order ~rank_hash:Int64.of_int ~seed:42L
  in
  let first = Canonical_sorted_set.of_list policy [ 5; 1; 9; 3; 7; 2 ] in
  let second = Canonical_sorted_set.of_list policy [ 2; 7; 3; 9; 1; 5 ] in
  let rank = Canonical_sorted_set.rank policy 5 in
  Alcotest.(check int) "golden geometric rank" 2 rank.Canonical_sorted_set.geometric;
  Alcotest.(check int64)
    "golden secondary rank" (-8249747124219652860L) rank.Canonical_sorted_set.secondary;
  Alcotest.(check int64) "golden content rank" 840465094988975138L rank.Canonical_sorted_set.content;
  check_int_list "canonical order" [ 1; 2; 3; 5; 7; 9 ] (Canonical_sorted_set.to_list first);
  let first_stats = Canonical_sorted_set.statistics first in
  let second_stats = Canonical_sorted_set.statistics second in
  Alcotest.(check int)
    "canonical height" first_stats.Canonical_sorted_set.canonical_height
    second_stats.Canonical_sorted_set.canonical_height;
  Alcotest.(check int64)
    "canonical digest" first_stats.Canonical_sorted_set.structure_digest
    second_stats.Canonical_sorted_set.structure_digest;
  match Canonical_sorted_set.add 5 first with
  | Canonical_sorted_set.Existing representative ->
      Alcotest.(check int) "lookup representative" 5 representative
  | Canonical_sorted_set.Added _ -> Alcotest.fail "duplicate canonical member was added"

let test_meldable_heap () =
  let left = Brodal_okasaki_heap.of_list int_order [ 9; 3; 7 ] in
  let right = Brodal_okasaki_heap.of_list int_order [ 8; 1; 5 ] in
  let heap = Brodal_okasaki_heap.meld left right in
  Alcotest.(check (option int)) "heap minimum" (Some 1) (Brodal_okasaki_heap.minimum heap);
  check_int_list "heap sorted drain" [ 1; 3; 5; 7; 8; 9 ] (Brodal_okasaki_heap.to_sorted_list heap);
  let minimum, remainder = Option.get (Brodal_okasaki_heap.minimum_view heap) in
  Alcotest.(check int) "minimum view" 1 minimum;
  Alcotest.(check int) "persistent remainder" 5 (Brodal_okasaki_heap.count remainder);
  Alcotest.(check int) "source retained" 6 (Brodal_okasaki_heap.count heap)

let test_priority_search_queue () =
  let queue =
    Priority_search_queue.empty ~key_comparator:int_order ~priority_comparator:int_order
  in
  let _, queue = Priority_search_queue.set 5 1 "five" queue in
  let _, queue = Priority_search_queue.set 2 1 "two" queue in
  let _, queue = Priority_search_queue.set 8 0 "eight" queue in
  let winner = Option.get (Priority_search_queue.minimum queue) in
  Alcotest.(check int) "priority winner" 8 (Priority_search_queue.entry_key winner);
  let winner, remainder = Option.get (Priority_search_queue.minimum_view queue) in
  Alcotest.(check string) "winner payload" "eight" (Priority_search_queue.entry_value winner);
  let next = Option.get (Priority_search_queue.minimum remainder) in
  Alcotest.(check int) "key breaks priority tie" 2 (Priority_search_queue.entry_key next);
  Alcotest.(check bool) "source retained" true (Priority_search_queue.mem 8 queue)

let test_daba_lite () =
  let window = Daba_lite.create ~identity:"" ~combine:( ^ ) () in
  List.iter (fun value -> Daba_lite.insert value window) [ "a"; "b"; "c" ];
  Alcotest.(check string) "FIFO aggregate" "abc" (Daba_lite.aggregate window);
  Alcotest.(check bool) "evicted" true (Daba_lite.try_evict window);
  Alcotest.(check string) "aggregate after eviction" "bc" (Daba_lite.aggregate window);
  let fallible =
    Daba_lite.create ~identity:""
      ~combine:(fun left right ->
        if String.equal right "boom" then failwith "combine" else left ^ right)
      ()
  in
  Daba_lite.insert "safe" fallible;
  (try Daba_lite.insert "boom" fallible with Failure _ -> ());
  Alcotest.(check string) "failed insert leaves aggregate" "safe" (Daba_lite.aggregate fallible);
  Alcotest.(check int) "failed insert leaves count" 1 (Daba_lite.count fallible);
  Daba_lite.clear window;
  Alcotest.(check bool) "clear" true (Daba_lite.is_empty window)

type cursor_ranked = { rank : int; label : string }

let cursor_ranked_order =
  Common.Comparator.create (fun left right -> Int.compare left.rank right.rank)

let test_sorted_ordered_search_cursors () =
  let ranked rank label = { rank; label } in
  let bag =
    Sorted_bag.of_list cursor_ranked_order
      [ ranked 1 "low"; ranked 2 "first"; ranked 2 "second"; ranked 2 "third" ]
  in
  let selected = Option.get (Ordered_search_cursor.sorted_bag_at 2 bag) in
  let original_labels = List.map (fun item -> item.label) (Sorted_bag.to_list bag) in
  Alcotest.(check (option string))
    "selected duplicate" (Some "second")
    (Option.map (fun item -> item.label) (Ordered_search_cursor.sorted_bag_peek_next selected));
  let edited = Option.get (Ordered_search_cursor.sorted_bag_delete_next selected) in
  Alcotest.(check (list string))
    "exact duplicate deletion"
    (List.filteri (fun index _ -> index <> 2) original_labels)
    (List.map
       (fun item -> item.label)
       (Sorted_bag.to_list (Ordered_search_cursor.sorted_bag_snapshot edited)));
  Alcotest.(check (list string))
    "bag source retained" original_labels
    (List.map (fun item -> item.label) (Sorted_bag.to_list bag));
  let maybe_order =
    Common.Comparator.create (fun left right ->
        match (left, right) with
        | None, None -> 0
        | None, Some _ -> -1
        | Some _, None -> 1
        | Some left, Some right -> Int.compare left right)
  in
  let maybe_cursor =
    Option.get
      (Ordered_search_cursor.sorted_bag_at 1 (Sorted_bag.of_list maybe_order [ None; Some 2 ]))
  in
  Alcotest.(check (option (option int)))
    "stored None remains present" (Some None)
    (Ordered_search_cursor.sorted_bag_peek_previous maybe_cursor);
  let set = Sorted_set.of_list int_order [ 1; 3; 5 ] in
  let gap = Ordered_search_cursor.sorted_set_lower_bound 4 set in
  Alcotest.(check int) "set lower-bound rank" 2 (Ordered_search_cursor.sorted_set_position gap);
  Alcotest.(check (pair (option int) (option int)))
    "set neighbors" (Some 3, Some 5)
    ( Ordered_search_cursor.sorted_set_peek_previous gap,
      Ordered_search_cursor.sorted_set_peek_next gap );
  check_int_list "set cursor insertion" [ 1; 3; 4; 5 ]
    (Sorted_set.to_list
       (Ordered_search_cursor.sorted_set_snapshot (Ordered_search_cursor.sorted_set_add 4 gap)));
  let map =
    Result.get_ok (Sorted_map.of_list int_order [ (1, "one"); (3, "three"); (5, "five") ])
  in
  let found = Ordered_search_cursor.sorted_map_find 3 map in
  Alcotest.(check bool) "map exact cursor" true (Ordered_search_cursor.search_found found);
  let changed =
    Option.get
      (Ordered_search_cursor.sorted_map_set_next_value "THREE"
         (Ordered_search_cursor.search_value found))
  in
  let inserted = Result.get_ok (Ordered_search_cursor.sorted_map_insert 4 "four" changed) in
  Alcotest.(check (list (pair int string)))
    "map cursor edits"
    [ (1, "one"); (3, "THREE"); (4, "four"); (5, "five") ]
    (Sorted_map.to_list (Ordered_search_cursor.sorted_map_snapshot inserted))

let test_policy_and_priority_search_cursors () =
  let policy =
    Canonical_sorted_set.create_policy ~comparator:int_order ~rank_hash:Int64.of_int ~seed:0x5a17L
  in
  let set = Canonical_sorted_set.of_list policy [ 1; 3; 5 ] in
  let inserted =
    Ordered_search_cursor.canonical_add 4 (Ordered_search_cursor.canonical_lower_bound 4 set)
  in
  Alcotest.(check int)
    "canonical insertion rank" 3
    (Ordered_search_cursor.canonical_position inserted);
  check_int_list "canonical cursor edit" [ 1; 3; 4; 5 ]
    (Canonical_sorted_set.to_list (Ordered_search_cursor.canonical_snapshot inserted));
  let queue =
    Priority_search_queue.empty ~key_comparator:int_order ~priority_comparator:int_order
  in
  let _, queue = Priority_search_queue.set 1 30 "one" queue in
  let _, queue = Priority_search_queue.set 3 10 "three" queue in
  let _, queue = Priority_search_queue.set 5 20 "five" queue in
  let minimum = Ordered_search_cursor.priority_search_minimum queue in
  Alcotest.(check (option int))
    "priority minimum cursor" (Some 3)
    (Option.map Priority_search_queue.entry_key
       (Ordered_search_cursor.priority_search_peek_next minimum));
  let changed = Option.get (Ordered_search_cursor.priority_search_set_next 40 "THREE" minimum) in
  Alcotest.(check (option int))
    "priority winner moved" (Some 5)
    (Option.map Priority_search_queue.entry_key
       (Priority_search_queue.minimum (Ordered_search_cursor.priority_search_snapshot changed)));
  let insertion = Ordered_search_cursor.priority_search_try_insert 4 5 "four" changed in
  Alcotest.(check bool)
    "priority cursor insertion" true
    (Ordered_search_cursor.insertion_added insertion);
  Alcotest.(check (option int))
    "priority winner after insertion" (Some 4)
    (Option.map Priority_search_queue.entry_key
       (Priority_search_queue.minimum
          (Ordered_search_cursor.priority_search_snapshot
             (Ordered_search_cursor.insertion_value insertion))))

let test_interval_ordered_search_cursors () =
  let ranked rank label = { rank; label } in
  let interval low high =
    Result.get_ok (Interval_tree.make_interval cursor_ranked_order low high)
  in
  let first = interval (ranked 1 "first-low") (ranked 5 "first-high") in
  let second = interval (ranked 1 "second-low") (ranked 5 "second-high") in
  let late = interval (ranked 7 "late-low") (ranked 9 "late-high") in
  let tree = Interval_tree.of_list cursor_ranked_order [ first; second; late ] in
  let selected = Option.get (Ordered_search_cursor.interval_tree_at 1 tree) in
  Alcotest.(check (option string))
    "selected interval occurrence" (Some "second-low")
    (Option.map
       (fun item -> (Interval_tree.low item).label)
       (Ordered_search_cursor.interval_tree_peek_next selected));
  let edited = Option.get (Ordered_search_cursor.interval_tree_delete_next selected) in
  Alcotest.(check (option string))
    "remaining equal interval" (Some "first-low")
    (Option.map
       (fun item -> (Interval_tree.low item).label)
       (Ordered_search_cursor.interval_tree_peek_previous edited));
  Alcotest.(check (option string))
    "interval successor" (Some "late-low")
    (Option.map
       (fun item -> (Interval_tree.low item).label)
       (Ordered_search_cursor.interval_tree_peek_next edited));
  let probe = interval (ranked 4 "probe-low") (ranked 8 "probe-high") in
  let first_hit = Ordered_search_cursor.interval_tree_find_overlap probe tree in
  let second_hit =
    Ordered_search_cursor.interval_tree_seek_next_overlap probe
      (Ordered_search_cursor.search_value first_hit)
  in
  Alcotest.(check bool) "first overlap" true (Ordered_search_cursor.search_found first_hit);
  Alcotest.(check int)
    "first overlap rank" 0
    (Ordered_search_cursor.interval_tree_position (Ordered_search_cursor.search_value first_hit));
  Alcotest.(check bool) "continued overlap" true (Ordered_search_cursor.search_found second_hit);
  Alcotest.(check int)
    "continued overlap rank" 1
    (Ordered_search_cursor.interval_tree_position (Ordered_search_cursor.search_value second_hit))

let test_interval_map_and_bit_set_cursors () =
  let map = Persistent_interval_map.empty int_order in
  let map = Result.get_ok (Persistent_interval_map.add ~low:1 ~high:3 "one" map) in
  let map = Result.get_ok (Persistent_interval_map.add ~low:5 ~high:8 "five" map) in
  let map = Result.get_ok (Persistent_interval_map.add ~low:10 ~high:12 "ten" map) in
  let first_hit =
    Result.get_ok (Ordered_search_cursor.interval_map_find_overlap ~low:2 ~high:11 map)
  in
  let second_hit =
    Result.get_ok
      (Ordered_search_cursor.interval_map_seek_next_overlap ~low:2 ~high:11
         (Ordered_search_cursor.search_value first_hit))
  in
  Alcotest.(check (option string))
    "first interval-map overlap" (Some "one")
    (Option.map Persistent_interval_map.entry_value
       (Ordered_search_cursor.interval_map_peek_next (Ordered_search_cursor.search_value first_hit)));
  let changed =
    Option.get
      (Result.get_ok
         (Ordered_search_cursor.interval_map_set_next_value "FIVE"
            (Ordered_search_cursor.search_value second_hit)))
  in
  Alcotest.(check (option string))
    "interval-map cursor update" (Some "FIVE")
    (Option.map Persistent_interval_map.entry_value
       (Ordered_search_cursor.interval_map_peek_next changed));
  Alcotest.(check (option string))
    "interval-map source retained" (Some "five")
    (Option.map Persistent_interval_map.entry_value
       (Persistent_interval_map.find_exact ~low:5 ~high:8 map));
  let bits = Result.get_ok (Persistent_chunked_bit_set.of_list [ 1; 64; 130 ]) in
  let gap = Ordered_search_cursor.chunked_bit_set_at_or_after 65 bits in
  Alcotest.(check int) "bit-set cursor rank" 2 (Ordered_search_cursor.chunked_bit_set_position gap);
  Alcotest.(check (pair (option int) (option int)))
    "bit-set neighbors" (Some 64, Some 130)
    ( Ordered_search_cursor.chunked_bit_set_peek_previous gap,
      Ordered_search_cursor.chunked_bit_set_peek_next gap );
  let inserted = Result.get_ok (Ordered_search_cursor.chunked_bit_set_add 100 gap) in
  check_int_list "bit-set cursor insertion" [ 1; 64; 100; 130 ]
    (Persistent_chunked_bit_set.to_list (Ordered_search_cursor.chunked_bit_set_snapshot inserted));
  let deleted = Option.get (Ordered_search_cursor.chunked_bit_set_delete_previous inserted) in
  check_int_list "bit-set cursor deletion" [ 1; 64; 130 ]
    (Persistent_chunked_bit_set.to_list (Ordered_search_cursor.chunked_bit_set_snapshot deleted));
  check_int_list "bit-set source retained" [ 1; 64; 130 ] (Persistent_chunked_bit_set.to_list bits)

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
          Alcotest.test_case "sequence cursors" `Quick test_sequence_cursors;
          Alcotest.test_case "rope cursors" `Quick test_rope_and_cursor;
          Alcotest.test_case "text rope cursors" `Quick test_text_rope_and_cursor;
          Alcotest.test_case "canonical sorted set" `Quick test_canonical_sorted_set;
          Alcotest.test_case "meldable heap" `Quick test_meldable_heap;
          Alcotest.test_case "priority search queue" `Quick test_priority_search_queue;
          Alcotest.test_case "DABA Lite" `Quick test_daba_lite;
          Alcotest.test_case "sorted ordered-search cursors" `Quick
            test_sorted_ordered_search_cursors;
          Alcotest.test_case "policy and priority-search cursors" `Quick
            test_policy_and_priority_search_cursors;
          Alcotest.test_case "interval ordered-search cursors" `Quick
            test_interval_ordered_search_cursors;
          Alcotest.test_case "interval-map and bit-set cursors" `Quick
            test_interval_map_and_bit_set_cursors;
        ] );
    ]
