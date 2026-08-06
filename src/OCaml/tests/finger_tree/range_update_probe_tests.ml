(** Bound probes for the range-update sequence rebuilt as a lazy-tag implicit AVL.

    The load-bearing case is range-length independence. The flat-array placeholder applied every
    tag eagerly to each covered element — Theta(length) [apply_element] calls per update and a
    Theta(n) republish — which no correctness test can distinguish from the deferred
    representation. Counting the algebra's own callbacks can: a deferred update records the tag at
    the covering subtree roots, so tagging 10 elements and tagging 10,000 elements of the same
    sequence must cost the {e same} bounded number of apply and compose calls, and a
    whole-sequence tag must be a constant handful. *)

open Durable7
open Finger_tree

let apply_element_calls = ref 0
let apply_measure_calls = ref 0
let compose_calls = ref 0
let combine_calls = ref 0

let reset_counters () =
  apply_element_calls := 0;
  apply_measure_calls := 0;
  compose_calls := 0;
  combine_calls := 0

let total_calls () = !apply_element_calls + !apply_measure_calls + !compose_calls + !combine_calls

let counting_algebra () =
  Result.get_ok
    (Range_update_sequence.create_algebra ~id:"probe-add-sum-v1" ~identity:0
       ~combine:(fun left right ->
         incr combine_calls;
         left + right)
       ~measure:Fun.id
       ~apply_element:(fun tag value ->
         incr apply_element_calls;
         tag + value)
       ~apply_measure:(fun tag ~length measure ->
         incr apply_measure_calls;
         measure + (tag * length))
       ~compose:(fun newer older ->
         incr compose_calls;
         newer + older)
       ~laws_verified:true ())

let rec ceiling_log2 value = if value <= 1 then 0 else 1 + ceiling_log2 ((value + 1) / 2)

let test_range_edit_cost_is_independent_of_range_length () =
  (* Tag a 10-element range and a 10,000-element range of the same 16,384-element sequence. Both
     must stay within one logarithmic budget, and the thousandfold-wider range may not add more
     than a fixed handful of calls — the eager per-element shape would pay at least 10,000
     [apply_element] calls for the wide range. *)
  let size = 16_384 in
  let algebra = counting_algebra () in
  let source = Range_update_sequence.of_list algebra (List.init size Fun.id) in
  let update_cost length =
    reset_counters ();
    let updated =
      Result.get_ok (Range_update_sequence.update_range ~start:100 ~length 1 source)
    in
    let cost = total_calls () in
    let applies = !apply_element_calls + !compose_calls in
    (* Correctness of the deferred tag, read from the O(1) cached root measure. *)
    Alcotest.(check int)
      "the deferred tag reached the cached aggregate"
      ((size * (size - 1) / 2) + length)
      (Range_update_sequence.measure updated);
    (cost, applies)
  in
  let narrow_cost, narrow_applies = update_cost 10 in
  let wide_cost, wide_applies = update_cost 10_000 in
  let bound = (40 * ceiling_log2 size) + 40 in
  Alcotest.(check bool)
    (Printf.sprintf "%d calls for the 10-element update within the logarithmic bound %d"
       narrow_cost bound)
    true
    (narrow_cost > 0 && narrow_cost <= bound);
  Alcotest.(check bool)
    (Printf.sprintf "%d calls for the 10,000-element update within the same bound %d" wide_cost
       bound)
    true
    (wide_cost > 0 && wide_cost <= bound);
  Alcotest.(check bool)
    (Printf.sprintf "a 1000x wider range moved apply+compose calls %d -> %d, not 1000x"
       narrow_applies wide_applies)
    true
    (wide_applies <= narrow_applies + 32 && narrow_applies <= wide_applies + 32);
  (* Reading a range measure is O(log n) cached-subtree reads, not a Theta(length) fold. *)
  reset_counters ();
  let updated = Result.get_ok (Range_update_sequence.update_range ~start:100 ~length:10_000 1 source) in
  reset_counters ();
  Alcotest.(check (result int string))
    "range measure sees through the pending tag"
    (Ok (((10_099 * 10_100) / 2) - ((99 * 100) / 2) + 10_000))
    (Range_update_sequence.measure_range ~start:100 ~length:10_000 updated);
  let read_bound = (12 * ceiling_log2 size) + 12 in
  Alcotest.(check bool)
    (Printf.sprintf "%d calls for a 10,000-element range measure within the logarithmic bound %d"
       (total_calls ()) read_bound)
    true
    (total_calls () <= read_bound)

let test_whole_sequence_tag_is_constant () =
  (* Tagging the entire sequence records one tag at the root: exactly one element apply and one
     measure apply, at most one composition, and no measure recombination at all. *)
  let size = 16_384 in
  let algebra = counting_algebra () in
  let source = Range_update_sequence.of_list algebra (List.init size Fun.id) in
  reset_counters ();
  let updated =
    Result.get_ok (Range_update_sequence.update_range ~start:0 ~length:size 5 source)
  in
  Alcotest.(check int) "one element apply" 1 !apply_element_calls;
  Alcotest.(check int) "one measure apply" 1 !apply_measure_calls;
  Alcotest.(check bool) "at most one composition" true (!compose_calls <= 1);
  Alcotest.(check int) "no measure recombination" 0 !combine_calls;
  Alcotest.(check int)
    "the whole-sequence tag reached the cached aggregate"
    ((size * (size - 1) / 2) + (5 * size))
    (Range_update_sequence.measure updated);
  (* Stacked whole-sequence tags compose newer-over-older instead of piling up pushes. *)
  reset_counters ();
  let stacked = Result.get_ok (Range_update_sequence.update_range ~start:0 ~length:size 7 updated) in
  Alcotest.(check int) "stacking composes once" 1 !compose_calls;
  Alcotest.(check int)
    "the stacked tags both reached the aggregate"
    ((size * (size - 1) / 2) + (12 * size))
    (Range_update_sequence.measure stacked)

let test_point_reads_see_through_pending_tags_logarithmically () =
  (* A point read under a pending tag applies the inherited tags for one root path only. *)
  let size = 16_384 in
  let algebra = counting_algebra () in
  let source = Range_update_sequence.of_list algebra (List.init size Fun.id) in
  let updated =
    Result.get_ok (Range_update_sequence.update_range ~start:1_000 ~length:14_000 3 source)
  in
  reset_counters ();
  Alcotest.(check (option int)) "a covered read is logical" (Some 8_003)
    (Range_update_sequence.nth 8_000 updated);
  let read_bound = (8 * ceiling_log2 size) + 8 in
  Alcotest.(check bool)
    (Printf.sprintf "%d calls for one covered point read within the logarithmic bound %d"
       (total_calls ()) read_bound)
    true
    (total_calls () <= read_bound);
  Alcotest.(check (option int)) "an uncovered read is literal" (Some 500)
    (Range_update_sequence.nth 500 updated);
  Alcotest.(check (option int)) "the source is untouched" (Some 8_000)
    (Range_update_sequence.nth 8_000 source)

let tests =
  [
    Alcotest.test_case "range edit cost is independent of range length" `Quick
      test_range_edit_cost_is_independent_of_range_length;
    Alcotest.test_case "whole-sequence tag is constant" `Quick test_whole_sequence_tag_is_constant;
    Alcotest.test_case "point reads see through pending tags" `Quick
      test_point_reads_see_through_pending_tags_logarithmically;
  ]
