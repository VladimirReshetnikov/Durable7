(** Tests for the append-only incremental level-ancestor seam.

    The load-bearing case is the hop bound. Myers' coalesced jump links are the entire reason this
    backend exists over a plain parent array, and removing them leaves every ancestor {e answer}
    correct while turning each query into an [O(depth)] walk. Only a hop-count assertion can catch
    that, so the bound here is stated so a coalescing-free arena fails it. *)

open Durable7
open Finger_tree
open Incremental_ancestor

let ok label = function
  | Ok value -> value
  | Error message -> Alcotest.failf "%s: unexpected error %S" label message

let expect_error label = function
  | Error _ -> ()
  | Ok _ -> Alcotest.failf "%s: expected an error" label

(** The deepest depth in bits: what an ideal halving walk would need. The factor four is headroom for
    the constant in Myers' coalescing schedule, whose jumps do not close a full remaining half per
    hop. It stays [Theta (log M)], so no linear-hop implementation can satisfy it at scale. *)
let rec ceiling_log2 value = if value <= 1 then 0 else 1 + ceiling_log2 ((value + 1) / 2)

(** A multiply-only oracle, deliberately not the arena's sqrt-seeded division form, so the two
    disagree if the rewritten helper is off by one at a perfect square. *)
let reference_integer_square_root value =
  let root = ref 0 in
  while (!root + 1) * (!root + 1) <= value do
    incr root
  done;
  !root

let build_chain arena length =
  let tip = ref bottom in
  for index = 1 to length do
    tip := ok "chain add" (add_leaf arena ~parent:!tip index)
  done;
  !tip

let test_deep_chain_hops () =
  let node_count = 32768 in
  let arena = create () in
  let tip = build_chain arena node_count in
  let before = statistics arena in
  Alcotest.(check int) "additions moved no query counter" 0 before.ancestor_query_count;
  Alcotest.(check int) "no hops before any query" 0 before.maximum_ancestor_hop_count;
  for depth = -1 to node_count - 1 do
    ignore (ok "chain query" (ancestor_at_depth arena tip ~depth) : int)
  done;
  let after = statistics arena in
  let bound = 4 * ceiling_log2 (node_count + 1) in
  Alcotest.(check bool)
    (Printf.sprintf "max hops %d within logarithmic bound %d" after.maximum_ancestor_hop_count bound)
    true
    (after.maximum_ancestor_hop_count >= 1 && after.maximum_ancestor_hop_count <= bound);
  Alcotest.(check bool)
    "total hops within the per-query ceiling" true
    (after.total_ancestor_hop_count <= (node_count + 1) * bound)

let test_last_hop_count_is_replaced () =
  let arena = create () in
  let tip = build_chain arena 512 in
  let depth = ok "tip depth" (depth_of arena tip) in
  ignore (ok "self query" (ancestor_at_depth arena tip ~depth) : int);
  Alcotest.(check int) "a self query costs no hop" 0 (statistics arena).last_ancestor_hop_count;
  ignore (ok "walk to bottom" (ancestor_at_depth arena tip ~depth:(-1)) : int);
  let walked = (statistics arena).last_ancestor_hop_count in
  Alcotest.(check bool) "a full walk costs hops" true (walked > 1);
  ignore (ok "self query again" (ancestor_at_depth arena tip ~depth) : int);
  Alcotest.(check int)
    "the last hop count is replaced, not accumulated" 0
    (statistics arena).last_ancestor_hop_count

let test_odd_block_layout () =
  let arena = create () in
  let tip = ref bottom in
  for published = 0 to 2048 do
    let current = statistics arena in
    let expected_blocks = reference_integer_square_root published + 1 in
    Alcotest.(check int)
      (Printf.sprintf "published count at %d" published)
      published current.published_node_count;
    Alcotest.(check int)
      (Printf.sprintf "block count at %d" published)
      expected_blocks current.block_count;
    Alcotest.(check int)
      (Printf.sprintf "allocated slots at %d" published)
      (expected_blocks * expected_blocks)
      current.allocated_slot_count;
    tip := ok "layout add" (add_leaf arena ~parent:!tip published)
  done

let test_error_contracts () =
  let arena = create () in
  let node = ok "leaf" (add_leaf arena ~parent:bottom 1) in
  expect_error "depth of unpublished" (depth_of arena 99);
  expect_error "depth of negative" (depth_of arena (-1));
  expect_error "parent of bottom" (parent_of arena bottom);
  expect_error "value of bottom" (value_at arena bottom);
  expect_error "value of unpublished" (value_at arena 99);
  expect_error "add below unpublished" (add_leaf arena ~parent:99 2);
  expect_error "depth above own" (ancestor_at_depth arena node ~depth:5);
  expect_error "depth below bottom" (ancestor_at_depth arena node ~depth:(-2));
  Alcotest.(check int) "the published node is readable" 1 (ok "value" (value_at arena node));
  Alcotest.(check (result unit string)) "the root path audits" (Ok ()) (validate arena node)

let test_rejected_addition_publishes_nothing () =
  let arena = create () in
  let tip = build_chain arena 64 in
  let before = statistics arena in
  expect_error "rejected addition" (add_leaf arena ~parent:4096 7);
  let after = statistics arena in
  Alcotest.(check int)
    "published count unchanged" before.published_node_count after.published_node_count;
  Alcotest.(check int) "add counter unchanged" before.add_leaf_count after.add_leaf_count;
  Alcotest.(check int) "block count unchanged" before.block_count after.block_count;
  Alcotest.(check int)
    "allocated slots unchanged" before.allocated_slot_count after.allocated_slot_count;
  (* The next successful addition still receives the handle that was pending. *)
  let handle = ok "next addition" (add_leaf arena ~parent:tip 65) in
  Alcotest.(check int)
    "the next handle is the one that was pending"
    (before.published_node_count + 1)
    handle

let test_branches_are_independent () =
  let arena = create () in
  let root = ok "root" (add_leaf arena ~parent:bottom 0) in
  let left = ok "left" (add_leaf arena ~parent:root 1) in
  let right = ok "right" (add_leaf arena ~parent:root 2) in
  Alcotest.(check int) "left ancestor" root (ok "left at 0" (ancestor_at_depth arena left ~depth:0));
  Alcotest.(check int) "right ancestor" root
    (ok "right at 0" (ancestor_at_depth arena right ~depth:0));
  Alcotest.(check int) "left value" 1 (ok "left value" (value_at arena left));
  Alcotest.(check int) "right value" 2 (ok "right value" (value_at arena right));
  Alcotest.(check bool) "branches are distinct" true (left <> right)

let tests =
  [
    Alcotest.test_case "deep chain queries stay logarithmic" `Quick test_deep_chain_hops;
    Alcotest.test_case "last hop count reports one query" `Quick test_last_hop_count_is_replaced;
    Alcotest.test_case "odd block layout" `Quick test_odd_block_layout;
    Alcotest.test_case "error contracts" `Quick test_error_contracts;
    Alcotest.test_case "rejected additions publish nothing" `Quick
      test_rejected_addition_publishes_nothing;
    Alcotest.test_case "branches stay independent" `Quick test_branches_are_independent;
  ]
