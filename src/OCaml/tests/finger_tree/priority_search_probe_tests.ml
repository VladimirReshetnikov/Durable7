(** Bound probes for the priority-search queue rebuilt as a winner-cached key-ordered AVL.

    Each probe counts retained-comparator calls or allocation, because the bounds the rebuild
    delivers are invisible to correctness tests: the flat-array placeholder answered every
    behavioural case correctly while paying Theta(n) copying per write and a full winner rescan per
    publish. The comparator counters pin the logarithmic write descents and that reading the
    minimum invokes no comparator at all (the root's cached winner); the allocation probe pins the
    structural flip from Theta(n)-copy publishes to O(log n) path-copied writes. *)

open Durable7
open Finger_tree

let key_calls = ref 0
let priority_calls = ref 0

let counting_key_order () =
  Common.Comparator.create (fun left right ->
      incr key_calls;
      Int.compare left right)

let counting_priority_order () =
  Common.Comparator.create (fun left right ->
      incr priority_calls;
      Int.compare left right)

let rec ceiling_log2 value = if value <= 1 then 0 else 1 + ceiling_log2 ((value + 1) / 2)

(* Bytes allocated by one call, measured over the runtime's own cumulative allocation counter.
   Single-threaded and deterministic for a fixed code path. *)
let allocation_of action =
  let before = Gc.allocated_bytes () in
  action ();
  Gc.allocated_bytes () -. before

(* Keys ascend as 2*i while priorities descend, so the minimum-priority entry is the LAST key: a
   winner read that fell back to key order, or to a rescan of the wrong index, is caught by
   value. *)
let build count key_order priority_order =
  let queue = ref (Priority_search_queue.empty ~key_comparator:key_order ~priority_comparator:priority_order) in
  for index = 0 to count - 1 do
    queue := snd (Priority_search_queue.set (2 * index) (count - index) index !queue)
  done;
  !queue

let test_writes_scale_logarithmically () =
  (* One keyed write into an n-entry queue costs O(log n) comparator calls: a duplicate-check
     descent, an insertion descent, and the winner-cache recomputations along one root path. The
     probe runs at two sizes 32x apart: the flat array's winner rescan would grow 32x, the AVL
     write grows by the depth difference only. *)
  let write_cost count =
    let key_order = counting_key_order () in
    let priority_order = counting_priority_order () in
    let queue = build count key_order priority_order in
    key_calls := 0;
    priority_calls := 0;
    let grown = Result.get_ok (Priority_search_queue.add ((2 * count) + 1) count (-1) queue) in
    Alcotest.(check int) "the successor grew" (count + 1) (Priority_search_queue.count grown);
    Alcotest.(check int) "the receiver is untouched" count (Priority_search_queue.count queue);
    let write = !key_calls + !priority_calls in
    key_calls := 0;
    priority_calls := 0;
    let winner, remainder = Option.get (Priority_search_queue.minimum_view queue) in
    Alcotest.(check int)
      "delete-min removed the winner" (2 * (count - 1))
      (Priority_search_queue.entry_key winner);
    Alcotest.(check int) "the remainder shrank" (count - 1) (Priority_search_queue.count remainder);
    (write, !key_calls + !priority_calls)
  in
  let small_write, small_pop = write_cost 1_024 in
  let large_write, large_pop = write_cost 32_768 in
  let bound count = (12 * ceiling_log2 (count + 1)) + 16 in
  Alcotest.(check bool)
    (Printf.sprintf "%d write comparisons at n=1024 within the logarithmic bound %d" small_write
       (bound 1_024))
    true
    (small_write > 0 && small_write <= bound 1_024);
  Alcotest.(check bool)
    (Printf.sprintf "%d write comparisons at n=32768 within the logarithmic bound %d" large_write
       (bound 32_768))
    true
    (large_write > 0 && large_write <= bound 32_768);
  Alcotest.(check bool)
    (Printf.sprintf "the 32x larger queue adds %d write comparisons, not 32x"
       (large_write - small_write))
    true
    (large_write <= small_write + 40);
  Alcotest.(check bool)
    (Printf.sprintf "%d delete-min comparisons at n=32768 within the logarithmic bound %d" large_pop
       (bound 32_768))
    true
    (large_pop > 0 && large_pop <= bound 32_768);
  Alcotest.(check bool)
    (Printf.sprintf "the 32x larger queue adds %d delete-min comparisons, not 32x"
       (large_pop - small_pop))
    true
    (large_pop <= small_pop + 40)

let test_find_min_reads_cached_winner_without_comparing () =
  (* The winner-cache payoff: [minimum] is an O(1) read of the root's cached winner that invokes
     neither comparator. The value assertion is the mutation tripwire — a broken winner cache
     surfaces the wrong entry here — and the negative control shows the counters are live. *)
  let key_order = counting_key_order () in
  let priority_order = counting_priority_order () in
  let queue = build 8_192 key_order priority_order in
  key_calls := 0;
  priority_calls := 0;
  let winner = Option.get (Priority_search_queue.minimum queue) in
  Alcotest.(check int)
    "the cached winner is the minimum-priority entry" (2 * 8_191)
    (Priority_search_queue.entry_key winner);
  Alcotest.(check int) "find-min invokes no key comparator" 0 !key_calls;
  Alcotest.(check int) "find-min invokes no priority comparator" 0 !priority_calls;
  (* Rank selection is size-directed, so it is comparator-free too — its regression to O(log n) is
     a descent cost, not a comparison cost. *)
  Alcotest.(check bool)
    "rank select finds an entry" true
    (Option.is_some (Priority_search_queue.nth 4_096 queue));
  Alcotest.(check int) "rank select invokes no comparator either" 0 !key_calls;
  (* Negative control: a genuine keyed seek does use the key comparator. *)
  Alcotest.(check bool)
    "a keyed seek finds its entry" true
    (Option.is_some (Priority_search_queue.find 4_096 queue));
  Alcotest.(check bool) "and the key counter saw it" true (!key_calls > 0)

let test_write_allocation_is_polylogarithmic () =
  (* The structural flip the comparator counter cannot see on the old substrate: the flat array's
     seek was already logarithmic, but publishing a successor copied the whole entry array —
     Theta(n) words, over 256 KB at n=32768. A path-copied AVL write allocates fresh nodes along
     one root path only, so the same write at 32x the size must stay within a small constant of
     the smaller one and far below one word per entry. *)
  let write_allocation count =
    let key_order = Common.Comparator.create Int.compare in
    let priority_order = Common.Comparator.create Int.compare in
    let queue = build count key_order priority_order in
    let sink = ref queue in
    let bytes =
      allocation_of (fun () ->
          sink := Result.get_ok (Priority_search_queue.add ((2 * count) + 1) count (-1) queue))
    in
    Alcotest.(check int) "the successor grew" (count + 1) (Priority_search_queue.count !sink);
    bytes
  in
  let small_bytes = write_allocation 1_024 in
  let large_bytes = write_allocation 32_768 in
  Alcotest.(check bool)
    (Printf.sprintf "one write at n=32768 allocates %.0f bytes, far below the %d-byte array copy"
       large_bytes (32_768 * 8))
    true
    (large_bytes < 65_536.);
  Alcotest.(check bool)
    (Printf.sprintf "32x the entries grew one write's allocation %.0f -> %.0f bytes, not 32x"
       small_bytes large_bytes)
    true
    (large_bytes <= (4. *. small_bytes) +. 8_192.)

let test_rank_bounds_seek_logarithmically () =
  (* The keyed rank bounds stay logarithmic on the AVL, and a bounded enumeration between them
     reads only the covered ranks. *)
  let key_order = counting_key_order () in
  let priority_order = counting_priority_order () in
  let queue = build 4_096 key_order priority_order in
  key_calls := 0;
  priority_calls := 0;
  let low = Priority_search_queue.lower_bound 100 queue in
  let high = Priority_search_queue.upper_bound 140 queue in
  Alcotest.(check int) "lower bound rank" 50 low;
  Alcotest.(check int) "upper bound rank" 71 high;
  let bound = (8 * ceiling_log2 4_097) + 16 in
  Alcotest.(check bool)
    (Printf.sprintf "%d bound-seek comparisons within the logarithmic bound %d" !key_calls bound)
    true
    (!key_calls > 0 && !key_calls <= bound)


(* The bounded enumeration's contract, pinned against a filter model: inclusive on both key
   boundaries and on the priority threshold, output in key order, inverted range rejected. *)
let test_enumerate_at_most_matches_a_filter_model () =
  key_calls := 0;
  priority_calls := 0;
  let queue =
    ref
      (Priority_search_queue.empty
         ~key_comparator:(counting_key_order ())
         ~priority_comparator:(counting_priority_order ()))
  in
  for key = 0 to 511 do
    queue := snd (Priority_search_queue.set key ((key * 37) mod 97) key !queue)
  done;
  let model minimum_key maximum_key maximum_priority =
    Priority_search_queue.entries_by_key !queue
    |> List.filter (fun entry ->
           let key = Priority_search_queue.entry_key entry in
           let priority = Priority_search_queue.entry_priority entry in
           key >= minimum_key && key <= maximum_key && priority <= maximum_priority)
    |> List.map (fun entry ->
           (Priority_search_queue.entry_key entry, Priority_search_queue.entry_priority entry))
  in
  let query minimum_key maximum_key maximum_priority =
    match
      Priority_search_queue.enumerate_at_most ~minimum_key ~maximum_key ~maximum_priority !queue
    with
    | Error message -> Alcotest.failf "unexpected enumeration failure: %s" message
    | Ok entries ->
        List.map
          (fun entry ->
            (Priority_search_queue.entry_key entry, Priority_search_queue.entry_priority entry))
          entries
  in
  List.iter
    (fun (minimum_key, maximum_key, maximum_priority) ->
      Alcotest.(check (list (pair int int)))
        (Printf.sprintf "window [%d,%d] priority<=%d" minimum_key maximum_key maximum_priority)
        (model minimum_key maximum_key maximum_priority)
        (query minimum_key maximum_key maximum_priority))
    [
      (0, 511, 96);
      (0, 511, 0);
      (100, 100, 96);
      (37, 205, 40);
      (0, 511, 48);
      (500, 511, 96);
      (17, 18, -1);
    ];
  (match Priority_search_queue.enumerate_at_most ~minimum_key:9 ~maximum_key:3 ~maximum_priority:96 !queue with
  | Error _ -> ()
  | Ok _ -> Alcotest.fail "an inverted key range must be rejected")

(* The pruning bound. Priorities equal keys, so a low threshold over the full key range qualifies
   only the leftmost few entries, and every disqualified subtree must be cut at its winner cache:
   O(log n + v) comparator work. The negative control lifts the threshold so nothing can be
   pruned, proving the counters are live and the pruning is what kept the first number small. *)
let test_enumerate_at_most_prunes_through_winner_caches () =
  let queue =
    ref
      (Priority_search_queue.empty
         ~key_comparator:(counting_key_order ())
         ~priority_comparator:(counting_priority_order ()))
  in
  for key = 0 to 4_095 do
    queue := snd (Priority_search_queue.set key key key !queue)
  done;
  key_calls := 0;
  priority_calls := 0;
  let selective =
    match
      Priority_search_queue.enumerate_at_most ~minimum_key:0 ~maximum_key:4_095
        ~maximum_priority:5 !queue
    with
    | Error message -> Alcotest.failf "selective enumeration failed: %s" message
    | Ok entries -> entries
  in
  let selective_calls = !key_calls + !priority_calls in
  Alcotest.(check int) "six entries qualify" 6 (List.length selective);
  let bound = (8 * ceiling_log2 4_097) + (8 * 6) + 16 in
  Alcotest.(check bool)
    (Printf.sprintf "%d selective-query comparisons within O(log n + v) bound %d" selective_calls
       bound)
    true
    (selective_calls > 0 && selective_calls <= bound);
  key_calls := 0;
  priority_calls := 0;
  let everything =
    match
      Priority_search_queue.enumerate_at_most ~minimum_key:0 ~maximum_key:4_095
        ~maximum_priority:4_095 !queue
    with
    | Error message -> Alcotest.failf "unselective enumeration failed: %s" message
    | Ok entries -> entries
  in
  let unselective_calls = !key_calls + !priority_calls in
  Alcotest.(check int) "everything qualifies" 4_096 (List.length everything);
  Alcotest.(check bool)
    (Printf.sprintf "unselective control visits every node (%d calls > %d)" unselective_calls
       (4 * 4_096))
    true
    (unselective_calls > 4 * 4_096)

let tests =
  [
    Alcotest.test_case "writes scale logarithmically" `Quick test_writes_scale_logarithmically;
    Alcotest.test_case "find-min reads the cached winner without comparing" `Quick
      test_find_min_reads_cached_winner_without_comparing;
    Alcotest.test_case "write allocation is polylogarithmic" `Quick
      test_write_allocation_is_polylogarithmic;
    Alcotest.test_case "rank bounds seek logarithmically" `Quick
      test_rank_bounds_seek_logarithmically;
    Alcotest.test_case "bounded enumeration matches a filter model" `Quick
      test_enumerate_at_most_matches_a_filter_model;
    Alcotest.test_case "bounded enumeration prunes through winner caches" `Quick
      test_enumerate_at_most_prunes_through_winner_caches;
  ]
