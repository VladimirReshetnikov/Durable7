(** Bound probes for the sorted family rebuilt on the measured finger-tree core.

    Each probe counts retained-comparator calls or allocation, because the bounds these rebuilds
    deliver are invisible to correctness tests: the flat-array placeholders answered every
    behavioural case correctly while paying Theta(n) copying per write. The comparator counters pin
    the measure-directed seek bounds (and that extremes and rank select invoke no comparator at
    all); the allocation probes pin the structural flip from Theta(n)-copy writes to O(log n)
    split-and-join writes, which no comparator counter can see because the array substrate's seeks
    were already logarithmic. *)

open Durable7
open Finger_tree

let key_calls = ref 0

let counting_order () =
  Common.Comparator.create (fun left right ->
      incr key_calls;
      Int.compare left right)

let rec ceiling_log2 value = if value <= 1 then 0 else 1 + ceiling_log2 ((value + 1) / 2)

(* Bytes allocated by one call, measured over the runtime's own cumulative allocation counter.
   Single-threaded and deterministic for a fixed code path. *)
let allocation_of action =
  let before = Gc.allocated_bytes () in
  action ();
  Gc.allocated_bytes () -. before

let even_set count order = Sorted_set.of_list order (List.init count (fun index -> 2 * index))

let test_point_write_seeks_logarithmically () =
  (* One insertion into the middle of an n-element set must cost O(log n) comparator calls. The
     probe runs at two sizes 32x apart: a linear scan would grow 32x, the measured seek grows by
     the depth difference only. *)
  let write_cost count =
    let order = counting_order () in
    let set = even_set count order in
    key_calls := 0;
    let added, grown = Sorted_set.add (count + 1) set in
    Alcotest.(check bool) "the write landed" true added;
    Alcotest.(check int) "the successor grew" (count + 1) (Sorted_set.count grown);
    Alcotest.(check int) "the receiver is untouched" count (Sorted_set.count set);
    !key_calls
  in
  let small_cost = write_cost 1_024 in
  let large_cost = write_cost 32_768 in
  let bound count = (4 * ceiling_log2 (count + 1)) + 8 in
  Alcotest.(check bool)
    (Printf.sprintf "%d comparisons at n=1024 within the logarithmic bound %d" small_cost
       (bound 1_024))
    true
    (small_cost > 0 && small_cost <= bound 1_024);
  Alcotest.(check bool)
    (Printf.sprintf "%d comparisons at n=32768 within the logarithmic bound %d" large_cost
       (bound 32_768))
    true
    (large_cost > 0 && large_cost <= bound 32_768);
  (* A 32x larger set may add at most the depth difference, never a 32x factor. *)
  Alcotest.(check bool)
    (Printf.sprintf "the 32x larger set adds %d comparisons, not 32x" (large_cost - small_cost))
    true
    (large_cost <= small_cost + 20)

let test_point_write_allocation_is_polylogarithmic () =
  (* The structural flip the comparator counter cannot see: the flat array substrate's seek was
     already logarithmic, but publishing a successor copied the whole array — Theta(n) words, over
     256 KB at n=32768. A split-and-join write allocates fresh nodes along two root paths only, so
     the same write at 32x the size must stay within a small constant of the smaller one and far
     below one word per element. *)
  let write_allocation count =
    let order = Common.Comparator.create Int.compare in
    let set = even_set count order in
    let sink = ref set in
    let bytes = allocation_of (fun () -> sink := snd (Sorted_set.add (count + 1) set)) in
    Alcotest.(check int) "the successor grew" (count + 1) (Sorted_set.count !sink);
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
    (Printf.sprintf "32x the elements grew one write's allocation %.0f -> %.0f bytes, not 32x"
       small_bytes large_bytes)
    true
    (large_bytes <= (4. *. small_bytes) +. 8_192.)

let test_extremes_are_comparator_free_digit_reads () =
  (* The census ruling: rank select regresses from the array's O(1) to O(log n), but extremes must
     stay O(1) — digit reads that invoke no comparator. The counter proves no seek is hiding
     behind [minimum]/[maximum]; the negative control shows the counter is live. *)
  let order = counting_order () in
  let set = even_set 8_192 order in
  let bag_order = counting_order () in
  let bag = Sorted_bag.of_list bag_order (List.init 4_096 (fun index -> index / 2)) in
  let map_order = counting_order () in
  let map =
    Result.get_ok
      (Sorted_map.of_list map_order (List.init 4_096 (fun index -> (index, string_of_int index))))
  in
  key_calls := 0;
  Alcotest.(check (option int)) "set minimum" (Some 0) (Sorted_set.minimum set);
  Alcotest.(check (option int)) "set maximum" (Some 16_382) (Sorted_set.maximum set);
  Alcotest.(check (option int)) "bag minimum" (Some 0) (Sorted_bag.minimum bag);
  Alcotest.(check (option int)) "bag maximum" (Some 2_047) (Sorted_bag.maximum bag);
  Alcotest.(check (option int))
    "map minimum" (Some 0)
    (Option.map Sorted_map.entry_key (Sorted_map.minimum map));
  Alcotest.(check (option int))
    "map maximum" (Some 4_095)
    (Option.map Sorted_map.entry_key (Sorted_map.maximum map));
  Alcotest.(check int) "extremes invoke no comparator at all" 0 !key_calls;
  (* Rank select is size-directed, so it is comparator-free too — its regression to O(log n) is a
     descent cost, not a comparison cost. *)
  Alcotest.(check (option int)) "set rank select" (Some 8_192) (Sorted_set.nth 4_096 set);
  Alcotest.(check int) "rank select invokes no comparator either" 0 !key_calls;
  (* Negative control: a genuine key seek does use the counter. *)
  Alcotest.(check (option int)) "a key seek finds its element" (Some 4_096)
    (Sorted_set.find 4_096 set);
  Alcotest.(check bool) "and the counter saw it" true (!key_calls > 0)

let test_bulk_build_sorts_once () =
  (* of_list is one stable sort plus an eager bottom-up build: O(n log n) comparisons. The old
     placement loop's comparisons were also O(n log n) — its Theta(n^2) was copying — so this pins
     the comparison budget of the new path and the allocation probe below pins the copy flip. *)
  let count = 4_096 in
  let order = counting_order () in
  key_calls := 0;
  let set = Sorted_set.of_list order (List.init count (fun index -> count - index)) in
  let build_calls = !key_calls in
  let bound = 2 * count * ceiling_log2 count in
  Alcotest.(check int) "every element arrived" count (Sorted_set.count set);
  Alcotest.(check bool)
    (Printf.sprintf "%d build comparisons within the n-log-n bound %d" build_calls bound)
    true
    (build_calls > 0 && build_calls <= bound);
  Alcotest.(check (option int)) "sorted minimum" (Some 1) (Sorted_set.minimum set);
  Alcotest.(check (option int)) "sorted maximum" (Some count) (Sorted_set.maximum set)

let test_delta_map_write_bound_flips () =
  (* The consumer flip: Persistent_delta_map.set was Theta(N + k) on the array substrate because
     publishing a successor copied the current root (and the change index). On the rebuilt
     Sorted_map it is O(log N) comparisons and O(log N) fresh nodes. Both counters run here: the
     comparator stays logarithmic and allocation stays far below one word per entry. *)
  let write_cost count =
    let order = counting_order () in
    let map =
      Persistent_delta_map.of_list order ~equal_values:Int.equal
        (List.init count (fun index -> (2 * index, index)))
    in
    key_calls := 0;
    let sink = ref map in
    let bytes = allocation_of (fun () -> sink := Persistent_delta_map.set (count + 1) (-1) map) in
    Alcotest.(check int) "the successor grew" (count + 1) (Persistent_delta_map.count !sink);
    Alcotest.(check int) "and records one change" 1 (Persistent_delta_map.change_count !sink);
    (!key_calls, bytes)
  in
  let small_calls, small_bytes = write_cost 1_024 in
  let large_calls, large_bytes = write_cost 32_768 in
  let bound count = (16 * ceiling_log2 (count + 1)) + 32 in
  Alcotest.(check bool)
    (Printf.sprintf "%d comparisons at N=1024 within the logarithmic bound %d" small_calls
       (bound 1_024))
    true
    (small_calls > 0 && small_calls <= bound 1_024);
  Alcotest.(check bool)
    (Printf.sprintf "%d comparisons at N=32768 within the logarithmic bound %d" large_calls
       (bound 32_768))
    true
    (large_calls > 0 && large_calls <= bound 32_768);
  Alcotest.(check bool)
    (Printf.sprintf "one write at N=32768 allocates %.0f bytes, far below the %d-byte root copy"
       large_bytes (32_768 * 8))
    true
    (large_bytes < 131_072.);
  Alcotest.(check bool)
    (Printf.sprintf "32x the entries grew one write's allocation %.0f -> %.0f bytes, not 32x"
       small_bytes large_bytes)
    true
    (large_bytes <= (4. *. small_bytes) +. 16_384.)

let tests =
  [
    Alcotest.test_case "point writes seek logarithmically" `Quick
      test_point_write_seeks_logarithmically;
    Alcotest.test_case "point write allocation is polylogarithmic" `Quick
      test_point_write_allocation_is_polylogarithmic;
    Alcotest.test_case "extremes are comparator-free digit reads" `Quick
      test_extremes_are_comparator_free_digit_reads;
    Alcotest.test_case "bulk build sorts once" `Quick test_bulk_build_sorts_once;
    Alcotest.test_case "the delta-map write bound flips" `Quick test_delta_map_write_bound_flips;
  ]
