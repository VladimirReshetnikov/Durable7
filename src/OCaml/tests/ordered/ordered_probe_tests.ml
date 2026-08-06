(** Bound probes for the ordered family rebuilt as the CHAMP-plus-stamped-sequence composite.

    The headline case is the stamp-to-position descent. The whole point of measuring the order
    sequence by maximum stamp is that a keyed position lookup becomes ONE O(log n) measure-directed
    descent; a binary search over indexed access would perform no measure combines at all (the
    core's indexing is size-directed), and the purged O(log{^ 2} n) shape would pay hundreds. Only
    counting the policy's combine calls distinguishes those shapes, so that is what the headline
    probe does — mirroring the Python workspace's [test_stamped_order] proof. The hash-policy
    counters pin expected-O(1) membership through the CHAMP index, and the allocation probe pins
    the flip from Theta(n) array-copy writes to O(log n) structural ones. *)

open Durable7
open Ordered

let combine_calls = ref 0

(* The production maximum-stamp monoid with a live combine counter, injected through the seam
   [Stamped_order] exposes for exactly this observation. *)
let counting_stamp_policy () =
  Finger_tree.Measures.create_policy ~id:"ordered-stamp-max-v1"
    ~monoid:
      (Finger_tree.Measures.create_monoid ~empty:min_int
         ~append:(fun left right ->
           incr combine_calls;
           if left >= right then left else right))
    ~measure:(fun entry -> entry.Stamped_order.stamp)
    ()

let rec ceiling_log2 value = if value <= 1 then 0 else 1 + ceiling_log2 ((value + 1) / 2)

(* Bytes allocated by one call, measured over the runtime's own cumulative allocation counter. *)
let allocation_of action =
  let before = Gc.allocated_bytes () in
  action ();
  Gc.allocated_bytes () -. before

(* Stride 3 leaves absent stamps between every pair of present ones. *)
let build_order count policy =
  Stamped_order.of_entries policy
    (List.init count (fun index -> { Stamped_order.stamp = 3 * index; item = index }))

let test_index_of_stamp_is_one_logarithmic_descent () =
  let small_policy = counting_stamp_policy () in
  let large_policy = counting_stamp_policy () in
  let small = build_order 1_024 small_policy in
  let large = build_order 32_768 large_policy in
  (* Warm both spines first: the lazy core defers construction work into suspensions, and the
     first descent pays that deferred cost exactly once. The bound below is about the DESCENT,
     which every later lookup pays, not the one-time memoized force. *)
  Alcotest.(check (option int)) "warm small" (Some 0) (Stamped_order.index_of_stamp 0 small);
  Alcotest.(check (option int)) "warm large" (Some 0) (Stamped_order.index_of_stamp 0 large);
  combine_calls := 0;
  Alcotest.(check (option int))
    "small stamp resolves" (Some 700)
    (Stamped_order.index_of_stamp (3 * 700) small);
  let small_cost = !combine_calls in
  combine_calls := 0;
  Alcotest.(check (option int))
    "large stamp resolves" (Some 20_000)
    (Stamped_order.index_of_stamp (3 * 20_000) large);
  let large_cost = !combine_calls in
  (* A locate performs a bounded number of combines per level of one root-to-target path. The
     factor-32 growth in size may add only the extra ~5 levels' worth of work; the O(log^2 n)
     shape this design forbids would pay hundreds of combines here, and a linear scan tens of
     thousands. The lower bound is the tripwire for the opposite mutation: a binary search over
     size-directed indexing performs no combines at all. *)
  let small_bound = (4 * ceiling_log2 1_024) + 4 in
  let large_bound = (4 * ceiling_log2 32_768) + 4 in
  Alcotest.(check bool)
    (Printf.sprintf "%d combines at n=1024 within the single-descent bound %d" small_cost
       small_bound)
    true
    (small_cost > 0 && small_cost <= small_bound);
  Alcotest.(check bool)
    (Printf.sprintf "%d combines at n=32768 within the single-descent bound %d" large_cost
       large_bound)
    true
    (large_cost > 0 && large_cost <= large_bound);
  Alcotest.(check bool)
    (Printf.sprintf "the 32x larger sequence adds %d combines — the depth difference, never 32x"
       (large_cost - small_cost))
    true
    (large_cost - small_cost <= 30)

let test_absent_stamps_return_none () =
  let policy = counting_stamp_policy () in
  let order = build_order 500 policy in
  let check_roundtrip index =
    Alcotest.(check (option int))
      (Printf.sprintf "stamp %d maps back" (3 * index))
      (Some index)
      (Stamped_order.index_of_stamp (3 * index) order)
  in
  List.iter check_roundtrip [ 0; 1; 249; 250; 498; 499 ];
  Alcotest.(check (option int)) "below every stamp" None (Stamped_order.index_of_stamp (-1) order);
  Alcotest.(check (option int))
    "between the first pair" None
    (Stamped_order.index_of_stamp 1 order);
  Alcotest.(check (option int))
    "between two interior stamps" None
    (Stamped_order.index_of_stamp ((3 * 250) + 1) order);
  Alcotest.(check (option int))
    "past the last stamp" None
    (Stamped_order.index_of_stamp (3 * 500) order);
  Alcotest.(check (option int))
    "empty sequence" None
    (Stamped_order.index_of_stamp 0 (Stamped_order.empty policy))

let hash_calls = ref 0
let equal_calls = ref 0

let counting_hash_policy () =
  Common.Hash_policy.create
    ~hash:(fun value ->
      incr hash_calls;
      Hashtbl.hash value)
    ~equal:(fun left right ->
      incr equal_calls;
      Int.equal left right)

let test_membership_is_hash_indexed () =
  (* Membership goes through the CHAMP index: an expected-O(1) handful of hash and equality calls,
     flat across a 32x size growth. The linear scan this replaced paid Theta(n) equality calls per
     miss. The negative control shows the counters are live. *)
  let membership_cost count =
    let policy = counting_hash_policy () in
    let set = Persistent_ordered_set.of_list policy (List.init count Fun.id) in
    hash_calls := 0;
    equal_calls := 0;
    Alcotest.(check bool) "a present element is found" true
      (Persistent_ordered_set.mem (count / 2) set);
    Alcotest.(check bool) "an absent element is missed" false
      (Persistent_ordered_set.mem (count + 1) set);
    (!hash_calls + !equal_calls, set)
  in
  let small_cost, _ = membership_cost 1_024 in
  let large_cost, large_set = membership_cost 32_768 in
  Alcotest.(check bool)
    (Printf.sprintf "%d policy calls for membership at n=1024, a constant handful" small_cost)
    true
    (small_cost > 0 && small_cost <= 8);
  Alcotest.(check bool)
    (Printf.sprintf "%d policy calls at n=32768 — no growth toward the linear scan's thousands"
       large_cost)
    true
    (large_cost > 0 && large_cost <= 8);
  (* index_of adds one measure-directed descent over the order sequence; the hash side stays a
     constant handful. *)
  hash_calls := 0;
  equal_calls := 0;
  Alcotest.(check (option int))
    "a keyed position resolves" (Some 16_384)
    (Persistent_ordered_set.index_of 16_384 large_set);
  Alcotest.(check bool)
    (Printf.sprintf "%d policy calls for a keyed position lookup, still a constant handful"
       (!hash_calls + !equal_calls))
    true
    (!hash_calls + !equal_calls <= 8);
  (* Negative control: bulk construction genuinely drives the counters. *)
  hash_calls := 0;
  equal_calls := 0;
  ignore (Persistent_ordered_set.of_list (counting_hash_policy ()) (List.init 512 Fun.id));
  Alcotest.(check bool) "and construction saw the counters" true (!hash_calls >= 512)

let test_write_allocation_is_polylogarithmic () =
  (* The structural flip: the flat array copied every element per write — Theta(n) words, over
     256 KB at n=32768. A CHAMP edit plus an O(1)-amortized endpoint push allocates a bounded
     handful of nodes. *)
  let write_allocation count =
    let policy = Common.Hash_policy.create ~hash:Hashtbl.hash ~equal:Int.equal in
    let set = Persistent_ordered_set.of_list policy (List.init count Fun.id) in
    let sink = ref set in
    let bytes =
      allocation_of (fun () -> sink := snd (Persistent_ordered_set.add (count + 1) set))
    in
    Alcotest.(check int) "the successor grew" (count + 1) (Persistent_ordered_set.count !sink);
    Alcotest.(check int) "the receiver is untouched" count (Persistent_ordered_set.count set);
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

let test_keyed_positions_stay_correct_through_edits () =
  (* The collection wiring above the mechanism: index_of must agree with the enumerated order
     after every reshaping, including the moves that reassign stamps and a removal. *)
  let policy = Common.Hash_policy.create ~hash:Hashtbl.hash ~equal:String.equal in
  let map =
    Persistent_ordered_map.of_list policy
      (List.init 256 (fun index -> (Printf.sprintf "k%d" index, index)))
  in
  let map = Persistent_ordered_map.move_to_first "k200" map in
  let map = Persistent_ordered_map.move_to_last "k7" map in
  let map =
    match Persistent_ordered_map.remove "k100" map with
    | Some (_, successor) -> successor
    | None -> Alcotest.fail "a present key failed to remove"
  in
  List.iteri
    (fun position (key, _) ->
      Alcotest.(check (option int))
        (Printf.sprintf "%s resolves to its enumerated position" key)
        (Some position)
        (Persistent_ordered_map.index_of key map))
    (Persistent_ordered_map.to_list map);
  Alcotest.(check (option int))
    "a removed key has no position" None
    (Persistent_ordered_map.index_of "k100" map)

let tests =
  [
    Alcotest.test_case "stamp-to-position is one logarithmic descent" `Quick
      test_index_of_stamp_is_one_logarithmic_descent;
    Alcotest.test_case "absent stamps return None" `Quick test_absent_stamps_return_none;
    Alcotest.test_case "membership is hash-indexed" `Quick test_membership_is_hash_indexed;
    Alcotest.test_case "write allocation is polylogarithmic" `Quick
      test_write_allocation_is_polylogarithmic;
    Alcotest.test_case "keyed positions stay correct through edits" `Quick
      test_keyed_positions_stay_correct_through_edits;
  ]
