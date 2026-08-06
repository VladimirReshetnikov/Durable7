(** Tests for the checkpoint-differential persistent sorted map.

    The load-bearing cases are the ones a plausible wrong implementation still passes a contents
    check on: that an equal write records nothing at all rather than a same-valued change, that
    repeated writes coalesce into one record instead of a log, that a cancelled record disappears
    and takes the current root back to the checkpoint root, and that a range-restricted change query
    seeks its boundaries instead of filtering every record. The last one is asserted with policy
    counters, because a filter over all [k] changes returns exactly the right answer and is caught
    only by counting the comparisons it needed to get there.

    Every structural-sharing assertion here relates two genuinely distinct values, or pairs a
    positive claim with a negative control. Asserting that an operation's result shares storage with
    the receiver it just returned unchanged is a self-comparison that cannot fail, and that is the
    defect this file is written to avoid. *)

open Durable7
open Finger_tree
open Persistent_delta_map

let case_insensitive_values left right =
  String.equal (String.lowercase_ascii left) (String.lowercase_ascii right)

let case_insensitive_keys () =
  Common.Comparator.create (fun left right ->
      String.compare (String.lowercase_ascii left) (String.lowercase_ascii right))

let string_map entries = of_list (Common.Comparator.default ()) ~equal_values:String.equal entries
let kind_name = function Added -> "added" | Removed -> "removed" | Updated -> "updated"

(** A change rendered so ordering, classification, and both endpoints fail together and readably. *)
let describe change =
  Printf.sprintf "%d:%s:%s->%s" (change_key change)
    (kind_name (change_kind change))
    (Option.value (change_before change) ~default:"-")
    (Option.value (change_after change) ~default:"-")

let rec ceiling_log2 value = if value <= 1 then 0 else 1 + ceiling_log2 ((value + 1) / 2)

let check_valid label map =
  match validate map with
  | Ok statistics -> statistics
  | Error message -> Alcotest.failf "%s: invariant violated: %s" label message

let test_equal_write_is_a_semantic_no_op () =
  let base = string_map [ (1, "one"); (2, "two") ] in
  let same = set 1 "one" base in
  Alcotest.(check bool) "a semantic no-op returns the receiver itself" true (same == base);
  Alcotest.(check bool) "so it shares every root" true (shares_storage_with same base);
  Alcotest.(check int) "and records nothing" 0 (change_count same);
  (* Negative control: the sharing claim above is only meaningful because a real write breaks it. *)
  let changed = set 1 "ONE" base in
  Alcotest.(check bool) "an effective write is a distinct value" false (changed == base);
  Alcotest.(check bool) "and does not share every root" false (shares_storage_with changed base);
  Alcotest.(check int) "and records exactly one change" 1 (change_count changed);
  Alcotest.(check int) "while the receiver still records nothing" 0 (change_count base);
  (* It is the retained relation, not structural equality, that decides. *)
  let relaxed =
    of_list (Common.Comparator.default ()) ~equal_values:case_insensitive_values [ (1, "one") ]
  in
  let recased = set 1 "ONE" relaxed in
  Alcotest.(check bool) "a relation-equal write is still a no-op" true (recased == relaxed);
  Alcotest.(check int) "recording nothing" 0 (change_count recased);
  Alcotest.(check (option string)) "and leaving the stored value alone" (Some "one")
    (find_opt 1 recased);
  ignore (check_valid "no-op" changed : statistics)

let test_repeated_writes_coalesce_into_one_record () =
  let base = string_map [ (1, "one"); (2, "two") ] in
  let thrice = base |> set 1 "a" |> set 1 "b" |> set 1 "c" in
  Alcotest.(check int) "three writes leave one record" 1 (change_count thrice);
  Alcotest.(check (list string)) "the record keeps the checkpoint before and the latest after"
    [ "1:updated:one->c" ] (List.map describe (changes thrice));
  (* The first effective write is the one that captures `before`, even on an absent key. *)
  let added = base |> set 9 "x" |> set 9 "y" in
  Alcotest.(check (list string)) "an addition coalesces the same way" [ "9:added:-->y" ]
    (List.map describe (changes added));
  let statistics = check_valid "coalesced" thrice in
  Alcotest.(check int) "one updated record" 1 statistics.updated_count;
  Alcotest.(check int) "and nothing else" 0 (statistics.added_count + statistics.removed_count)

let test_cancellation_removes_the_record_and_snaps_the_root () =
  let base = string_map [ (1, "one"); (2, "two") ] in
  let dirty = set 1 "changed" base in
  Alcotest.(check bool) "an edited value is not clean" false (is_clean dirty);
  Alcotest.(check bool) "and its two roots are distinct" false
    (current_snapshot dirty == checkpoint_snapshot dirty);
  let restored = set 1 "one" dirty in
  Alcotest.(check int) "set-then-restore cancels the record" 0 (change_count restored);
  Alcotest.(check bool) "the current root snaps back to the checkpoint root" true
    (current_snapshot restored == checkpoint_snapshot restored);
  Alcotest.(check bool) "which is the original checkpoint root, not a rebuilt equal one" true
    (current_snapshot restored == checkpoint_snapshot base);
  Alcotest.(check bool) "so the value is clean again" true (is_clean restored);
  (* An addition cancelled by a removal behaves the same way. *)
  let added = set 9 "nine" base in
  Alcotest.(check int) "the addition is recorded" 1 (change_count added);
  let cancelled = remove 9 added in
  Alcotest.(check int) "add-then-remove cancels" 0 (change_count cancelled);
  Alcotest.(check bool) "and snaps back to the same checkpoint root" true
    (current_snapshot cancelled == checkpoint_snapshot base);
  (* A removal cancelled by restoring the checkpoint value cancels too. *)
  let regrown = set 2 "two" (remove 2 base) in
  Alcotest.(check int) "remove-then-restore cancels" 0 (change_count regrown);
  Alcotest.(check bool) "and snaps back" true (is_clean regrown);
  (* Partial cancellation must not snap anything: only an empty index does. *)
  let partial = set 1 "one" (base |> set 1 "x" |> set 2 "y") in
  Alcotest.(check int) "one record survives partial cancellation" 1 (change_count partial);
  Alcotest.(check bool) "so the roots stay distinct" false
    (current_snapshot partial == checkpoint_snapshot partial);
  ignore (check_valid "restored" restored : statistics);
  ignore (check_valid "cancelled" cancelled : statistics);
  ignore (check_valid "partial" partial : statistics)

let test_key_representative_retention () =
  let equal_values = String.equal in
  let base = of_list (case_insensitive_keys ()) ~equal_values [ ("Key", "one") ] in
  let updated = set "KEY" "two" base in
  Alcotest.(check (option string)) "an update keeps the stored representative" (Some "Key")
    (Option.map fst (find_entry "key" updated));
  Alcotest.(check (option string)) "and so does its record" (Some "Key")
    (Option.map change_key (find_change "kEy" updated));
  let readded = set "kEy" "three" (remove "KEY" updated) in
  Alcotest.(check (option string)) "a delete/re-add round trip keeps the baseline representative"
    (Some "Key")
    (Option.map fst (find_entry "key" readded));
  Alcotest.(check (option string)) "and its record still carries it" (Some "Key")
    (Option.map change_key (find_change "key" readded));
  Alcotest.(check (list string)) "the round trip is one update, not two records"
    [ "Key:updated:one->three" ]
    (List.map
       (fun change ->
         Printf.sprintf "%s:%s:%s->%s" (change_key change)
           (kind_name (change_kind change))
           (Option.value (change_before change) ~default:"-")
           (Option.value (change_after change) ~default:"-"))
       (changes readded));
  (* A newly added class keeps its first representative for as long as its record is active. *)
  let fresh = of_list (case_insensitive_keys ()) ~equal_values [] in
  let episode = set "New" "a" fresh in
  Alcotest.(check (option string)) "a genuinely new class takes the supplied key" (Some "New")
    (Option.map change_key (find_change "new" episode));
  let kept = set "NEW" "b" episode in
  Alcotest.(check (option string))
    "the addition episode keeps its first representative" (Some "New")
    (Option.map change_key (find_change "new" kept));
  (* After complete cancellation, a later addition begins a new representative episode. *)
  let cancelled = remove "NEW" kept in
  Alcotest.(check int) "the episode cancelled entirely" 0 (change_count cancelled);
  let restarted = set "nEw" "c" cancelled in
  Alcotest.(check (option string)) "so a later addition starts a new episode" (Some "nEw")
    (Option.map change_key (find_change "new" restarted));
  Alcotest.(check (option string)) "and the state stores that representative" (Some "nEw")
    (Option.map fst (find_entry "NEW" restarted))

let test_removing_an_absent_key_is_a_no_op () =
  let base = string_map [ (1, "one") ] in
  let same = remove 2 base in
  Alcotest.(check bool) "removing an absent key returns the receiver itself" true (same == base);
  Alcotest.(check bool) "so it shares every root" true (shares_storage_with same base);
  Alcotest.(check int) "and records nothing" 0 (change_count same);
  (* Negative control. *)
  let removed = remove 1 base in
  Alcotest.(check bool) "removing a present key does not share storage" false
    (shares_storage_with removed base);
  Alcotest.(check int) "and records one change" 1 (change_count removed);
  Alcotest.(check (list string)) "classified as a removal" [ "1:removed:one->-" ]
    (List.map describe (changes removed))

let test_checkpoint_and_rollback_are_root_swaps () =
  let base = string_map [ (1, "one"); (2, "two") ] in
  Alcotest.(check bool) "checkpointing a clean value returns the receiver" true
    (checkpoint base == base);
  Alcotest.(check bool) "rolling back a clean value returns the receiver" true
    (rollback base == base);
  let dirty = set 1 "changed" (remove 2 base) in
  Alcotest.(check int) "two recorded changes" 2 (change_count dirty);
  let committed = checkpoint dirty in
  Alcotest.(check bool) "committing keeps the receiver's current root" true
    (current_snapshot committed == current_snapshot dirty);
  Alcotest.(check bool) "and adopts that very root as the checkpoint" true
    (checkpoint_snapshot committed == current_snapshot dirty);
  (* Negative control: committing must not keep the old checkpoint root. *)
  Alcotest.(check bool) "the superseded checkpoint root is gone" false
    (checkpoint_snapshot committed == checkpoint_snapshot dirty);
  Alcotest.(check int) "committing empties the index" 0 (change_count committed);
  Alcotest.(check bool) "so the committed value is clean" true (is_clean committed);
  let undone = rollback dirty in
  Alcotest.(check bool) "rolling back restores the receiver's checkpoint root" true
    (current_snapshot undone == checkpoint_snapshot dirty);
  (* Negative control: rolling back must not keep the edited current root. *)
  Alcotest.(check bool) "the edited current root is gone" false
    (current_snapshot undone == current_snapshot dirty);
  Alcotest.(check int) "rolling back empties the index" 0 (change_count undone);
  Alcotest.(check (list (pair int string)))
    "and restores the checkpoint contents"
    [ (1, "one"); (2, "two") ]
    (to_list undone);
  Alcotest.(check int) "the receiver keeps its own changes" 2 (change_count dirty);
  Alcotest.(check (list (pair int string)))
    "and its own contents"
    [ (1, "changed") ]
    (to_list dirty);
  ignore (check_valid "committed" committed : statistics);
  ignore (check_valid "undone" undone : statistics);
  ignore (check_valid "dirty" dirty : statistics)

let test_change_enumeration_is_ordered_and_classified () =
  let base = string_map [ (1, "one"); (2, "two"); (3, "three") ] in
  let edited = base |> set 2 "TWO" |> remove 3 |> set 4 "four" |> set 4 "four!" in
  Alcotest.(check (list string)) "ascending, once each, exactly classified"
    [ "2:updated:two->TWO"; "3:removed:three->-"; "4:added:-->four!" ]
    (List.map describe (changes edited));
  Alcotest.(check int) "the enumeration is output-optimal in the net-changed keys"
    (change_count edited)
    (List.length (changes edited));
  Alcotest.(check bool) "and the map reports having them" true (has_changes edited);
  Alcotest.(check (option string)) "a single change is looked up in the index"
    (Some "3:removed:three->-")
    (Option.map describe (find_change 3 edited));
  Alcotest.(check bool) "an unchanged key has no record" true (find_change 1 edited = None);
  Alcotest.(check (list (pair int string)))
    "the current state is independent of the change index"
    [ (1, "one"); (2, "TWO"); (4, "four!") ]
    (to_list edited);
  Alcotest.(check (list (pair int string)))
    "and the checkpoint state is untouched"
    [ (1, "one"); (2, "two"); (3, "three") ]
    (Sorted_map.to_list (checkpoint_snapshot edited));
  ignore (check_valid "edited" edited : statistics)

let test_range_restriction_seeks_boundaries () =
  let key_calls = ref 0 in
  let value_calls = ref 0 in
  let order =
    Common.Comparator.create (fun left right ->
        incr key_calls;
        Int.compare left right)
  in
  let equal_values left right =
    incr value_calls;
    String.equal left right
  in
  let size = 64 in
  let base = of_list order ~equal_values (List.init size (fun index -> (index, "v"))) in
  let edited =
    List.fold_left
      (fun state index -> set index (string_of_int index) state)
      base
      (List.init size Fun.id)
  in
  Alcotest.(check int) "every key is net-changed" size (change_count edited);
  (* Enumerating the whole change set touches neither policy. *)
  key_calls := 0;
  value_calls := 0;
  let all = changes edited in
  Alcotest.(check int) "full enumeration performs no key comparison" 0 !key_calls;
  Alcotest.(check int) "full enumeration performs no value comparison" 0 !value_calls;
  Alcotest.(check int) "and yields one record per changed key" size (List.length all);
  (* Checkpoint and rollback are root swaps, so they touch neither policy either. *)
  key_calls := 0;
  value_calls := 0;
  let committed = checkpoint edited in
  let undone = rollback edited in
  Alcotest.(check int) "checkpoint and rollback perform no key comparison" 0 !key_calls;
  Alcotest.(check int) "checkpoint and rollback perform no value comparison" 0 !value_calls;
  Alcotest.(check int) "committing empties the index" 0 (change_count committed);
  Alcotest.(check int) "rolling back empties the index" 0 (change_count undone);
  (* The restriction seeks two boundaries; it never filters the k records. *)
  key_calls := 0;
  value_calls := 0;
  let window = changes_in_range ~minimum:30 ~maximum:32 edited in
  Alcotest.(check (list int)) "the window holds exactly its keys" [ 30; 31; 32 ]
    (List.map change_key window);
  Alcotest.(check int) "the restriction performs no value comparison at all" 0 !value_calls;
  let bound = (4 * ceiling_log2 (size + 1)) + 4 in
  Alcotest.(check bool)
    (Printf.sprintf "%d key comparisons within the logarithmic seek bound %d" !key_calls bound)
    true
    (!key_calls > 0 && !key_calls <= bound);
  (* A filter over the change set would need at least one comparison per record. *)
  Alcotest.(check bool) "which is far below one comparison per change" true (!key_calls < size);
  (* Widening the window costs output, not extra seeking. *)
  key_calls := 0;
  let wide = changes_in_range ~minimum:8 ~maximum:55 edited in
  Alcotest.(check int) "a wide window yields its whole span" 48 (List.length wide);
  Alcotest.(check bool) "at the same seek cost" true (!key_calls <= bound);
  (* Boundaries are inclusive, and absent boundary keys still land correctly. *)
  Alcotest.(check (list int)) "an inclusive single-key window" [ 7 ]
    (List.map change_key (changes_in_range ~minimum:7 ~maximum:7 edited));
  Alcotest.(check (list int)) "a window past the end is empty" []
    (List.map change_key (changes_in_range ~minimum:size ~maximum:(size + 10) edited))

let test_inverted_ranges_follow_the_retained_comparator () =
  let descending = Common.Comparator.reverse (Common.Comparator.default ()) in
  let base =
    of_list descending ~equal_values:String.equal
      [ (1, "one"); (3, "three"); (5, "five"); (7, "seven") ]
  in
  Alcotest.(check (list int)) "the retained order drives enumeration" [ 7; 5; 3; 1 ] (keys base);
  let edited = base |> set 1 "ONE" |> set 3 "THREE" |> set 5 "FIVE" |> set 7 "SEVEN" in
  Alcotest.(check (list int)) "and the change order too" [ 7; 5; 3; 1 ]
    (List.map change_key (changes edited));
  (* Under a descending comparator the low endpoint is the numerically greater key. *)
  Alcotest.(check (list int)) "a well-formed descending window" [ 5; 3 ]
    (List.map change_key (changes_in_range ~minimum:5 ~maximum:3 edited));
  Alcotest.(check (list int)) "the same pair inverted yields nothing" []
    (List.map change_key (changes_in_range ~minimum:3 ~maximum:5 edited));
  Alcotest.(check (list int)) "and the state range agrees" []
    (List.map fst (Sorted_map.to_list (key_range ~minimum:3 ~maximum:5 edited)));
  (* Under an ascending comparator the inversion runs the other way. *)
  let ascending = set 1 "ONE" (string_map [ (1, "one"); (3, "three") ]) in
  Alcotest.(check (list int)) "an ascending inverted range yields nothing" []
    (List.map change_key (changes_in_range ~minimum:3 ~maximum:1 ascending));
  Alcotest.(check (list int)) "while the well-formed one does not" [ 1 ]
    (List.map change_key (changes_in_range ~minimum:1 ~maximum:3 ascending))

let test_reads_and_neighbours_see_the_current_state () =
  let base = string_map [ (2, "two"); (4, "four"); (6, "six") ] in
  let edited = base |> set 4 "FOUR" |> remove 6 |> set 8 "eight" in
  Alcotest.(check int) "count" 3 (count edited);
  Alcotest.(check bool) "not empty" false (is_empty edited);
  Alcotest.(check (option (pair int string))) "minimum" (Some (2, "two")) (minimum edited);
  Alcotest.(check (option (pair int string))) "maximum" (Some (8, "eight")) (maximum edited);
  Alcotest.(check (option (pair int string))) "rank select" (Some (4, "FOUR")) (nth 1 edited);
  Alcotest.(check (option int)) "rank of a present key" (Some 2) (index_of_key 8 edited);
  Alcotest.(check (option int)) "rank of an absent key" None (index_of_key 6 edited);
  Alcotest.(check bool) "membership follows the current state" false (mem 6 edited);
  Alcotest.(check (option string)) "lookup" (Some "FOUR") (find_opt 4 edited);
  (* Qualified, because the bare name would shadow [Stdlib.floor] for the rest of the file. *)
  Alcotest.(check (option (pair int string))) "floor at a present key" (Some (4, "FOUR"))
    (Persistent_delta_map.floor 4 edited);
  Alcotest.(check (option (pair int string))) "floor at an absent key" (Some (4, "FOUR"))
    (Persistent_delta_map.floor 5 edited);
  Alcotest.(check (option (pair int string))) "ceiling" (Some (8, "eight")) (ceiling 5 edited);
  Alcotest.(check (option (pair int string))) "strict predecessor" (Some (2, "two"))
    (lower 4 edited);
  Alcotest.(check (option (pair int string))) "strict successor" (Some (8, "eight"))
    (higher 4 edited);
  Alcotest.(check (option (pair int string))) "no predecessor below the minimum" None
    (lower 2 edited);
  Alcotest.(check (list int)) "keys" [ 2; 4; 8 ] (keys edited);
  Alcotest.(check (list string)) "values" [ "two"; "FOUR"; "eight" ] (values edited);
  Alcotest.(check (list (pair int string)))
    "a current key range" [ (2, "two"); (4, "FOUR") ]
    (Sorted_map.to_list (key_range ~minimum:0 ~maximum:5 edited));
  Alcotest.(check bool) "the retained comparator is the one supplied" true
    (Common.Comparator.same (comparator edited) (comparator base));
  Alcotest.(check bool) "and the retained value relation is usable" true
    (value_equal edited "x" "x" && not (value_equal edited "x" "y"))

let test_set_list_is_exactly_the_fold () =
  let base = string_map [ (1, "one"); (2, "two") ] in
  let folded = base |> set 2 "TWO" |> set 3 "three" |> set 2 "two" in
  let batched = set_list [ (2, "TWO"); (3, "three"); (2, "two") ] base in
  Alcotest.(check (list (pair int string))) "same contents" (to_list folded) (to_list batched);
  Alcotest.(check (list string)) "same changes"
    (List.map describe (changes folded))
    (List.map describe (changes batched));
  Alcotest.(check (list string)) "the round-tripped key cancelled" [ "3:added:-->three" ]
    (List.map describe (changes batched));
  Alcotest.(check bool) "an empty batch returns the receiver" true (set_list [] base == base);
  Alcotest.(check bool) "an all-no-op batch returns the receiver" true
    (set_list [ (1, "one"); (2, "two") ] base == base);
  (* A batch that cancels everything snaps the root back, exactly as the point writes would. *)
  let cancelling = set_list [ (1, "x"); (2, "y"); (1, "one"); (2, "two") ] base in
  Alcotest.(check int) "a wholly cancelling batch records nothing" 0 (change_count cancelling);
  Alcotest.(check bool) "and snaps back to the checkpoint root" true
    (current_snapshot cancelling == checkpoint_snapshot base);
  (* Later entries win, so the last equivalent key decides. *)
  Alcotest.(check (option string)) "last write wins within a batch" (Some "final")
    (find_opt 5 (set_list [ (5, "first"); (5, "final") ] base));
  ignore (check_valid "batched" batched : statistics)

let test_of_list_builds_a_clean_checkpoint () =
  let base = string_map [ (3, "three"); (1, "one"); (2, "two"); (1, "ONE") ] in
  Alcotest.(check (list (pair int string)))
    "sorted, with the last equivalent key winning"
    [ (1, "ONE"); (2, "two"); (3, "three") ]
    (to_list base);
  Alcotest.(check int) "a fresh map records nothing" 0 (change_count base);
  Alcotest.(check bool) "and is clean" true (is_clean base);
  Alcotest.(check bool) "with both states on one root" true
    (current_snapshot base == checkpoint_snapshot base);
  let blank = create (Common.Comparator.default ()) ~equal_values:String.equal in
  Alcotest.(check bool) "an explicitly created map is empty" true (is_empty blank);
  Alcotest.(check bool) "and clean" true (is_clean blank);
  (* The retained relation survives an empty result, so the map stays usable. *)
  let relaxed = create (Common.Comparator.default ()) ~equal_values:case_insensitive_values in
  let grown = set 1 "one" relaxed in
  Alcotest.(check bool) "the retained relation still decides no-ops" true
    (set 1 "ONE" grown == grown);
  let default_map : (int, string) t = empty () in
  Alcotest.(check bool) "the default map is empty and clean" true
    (is_empty default_map && is_clean default_map);
  Alcotest.(check (option string)) "and usable" (Some "v") (find_opt 1 (set 1 "v" default_map));
  ignore (check_valid "of_list" base : statistics)

let test_validate_reports_statistics () =
  let base = string_map [ (1, "one"); (2, "two"); (3, "three") ] in
  let clean = check_valid "clean" base in
  Alcotest.(check int) "clean entry count" 3 clean.entry_count;
  Alcotest.(check int) "clean checkpoint entry count" 3 clean.checkpoint_entry_count;
  Alcotest.(check int) "clean change count" 0 clean.change_count;
  Alcotest.(check bool) "a fresh value is clean" true clean.is_clean;
  let edited = base |> set 2 "TWO" |> remove 3 |> set 9 "nine" in
  let statistics = check_valid "edited" edited in
  Alcotest.(check int) "entry count" 3 statistics.entry_count;
  Alcotest.(check int) "checkpoint entry count" 3 statistics.checkpoint_entry_count;
  Alcotest.(check int) "change count" 3 statistics.change_count;
  Alcotest.(check int) "added" 1 statistics.added_count;
  Alcotest.(check int) "removed" 1 statistics.removed_count;
  Alcotest.(check int) "updated" 1 statistics.updated_count;
  Alcotest.(check bool) "an edited value is not clean" false statistics.is_clean;
  Alcotest.(check int) "the classifications account for every record" statistics.change_count
    (statistics.added_count + statistics.removed_count + statistics.updated_count)

let test_retained_versions_stay_independent () =
  let base = string_map [ (1, "one"); (2, "two") ] in
  let first = set 1 "a" base in
  let second = remove 2 first in
  let third = checkpoint second in
  let fourth = set 1 "b" third in
  Alcotest.(check (list (pair int string))) "the original is untouched"
    [ (1, "one"); (2, "two") ]
    (to_list base);
  Alcotest.(check int) "and still records nothing" 0 (change_count base);
  Alcotest.(check (list string)) "the first successor keeps its own change" [ "1:updated:one->a" ]
    (List.map describe (changes first));
  Alcotest.(check (list string)) "the second keeps both of its own"
    [ "1:updated:one->a"; "2:removed:two->-" ]
    (List.map describe (changes second));
  Alcotest.(check int) "the committed version is clean" 0 (change_count third);
  Alcotest.(check (list string)) "the fourth measures against the new checkpoint"
    [ "1:updated:a->b" ]
    (List.map describe (changes fourth));
  Alcotest.(check (list (pair int string))) "each version reports its own contents" [ (1, "b") ]
    (to_list fourth);
  List.iter
    (fun (label, map) -> ignore (check_valid label map : statistics))
    [ ("base", base); ("first", first); ("second", second); ("third", third); ("fourth", fourth) ]

(** A deterministic randomized history checked against a pair of plain maps. The model recomputes
    the net difference from scratch every step, so any drift in the online coalescing shows up at
    once. *)
let test_random_history_matches_a_model () =
  let module Model = Map.Make (Int) in
  let seed = ref 0x5DEECE6 in
  let next bound =
    seed := ((!seed * 25214903917) + 11) land 0xFFFFFFFFFFFF;
    (!seed lsr 17) mod bound
  in
  let model_changes checkpoint_model current_model =
    let all =
      List.sort_uniq Int.compare
        (List.map fst (Model.bindings checkpoint_model)
        @ List.map fst (Model.bindings current_model))
    in
    List.filter_map
      (fun key ->
        let before = Model.find_opt key checkpoint_model in
        let after = Model.find_opt key current_model in
        if before = after then None else Some (key, before, after))
      all
  in
  let map = ref (create (Common.Comparator.default ()) ~equal_values:Int.equal) in
  let checkpoint_model = ref Model.empty in
  let current_model = ref Model.empty in
  for step = 1 to 500 do
    let key = next 12 in
    (match next 10 with
    | 0 | 1 | 2 | 3 | 4 | 5 ->
        let value = next 4 in
        map := set key value !map;
        current_model := Model.add key value !current_model
    | 6 | 7 ->
        map := remove key !map;
        current_model := Model.remove key !current_model
    | 8 ->
        map := checkpoint !map;
        checkpoint_model := !current_model
    | _ ->
        map := rollback !map;
        current_model := !checkpoint_model);
    let label = Printf.sprintf "step %d" step in
    ignore (check_valid label !map : statistics);
    Alcotest.(check (list (pair int int)))
      (label ^ ": contents")
      (Model.bindings !current_model) (to_list !map);
    Alcotest.(check (list (triple int (option int) (option int))))
      (label ^ ": net changes")
      (model_changes !checkpoint_model !current_model)
      (List.map
         (fun change -> (change_key change, change_before change, change_after change))
         (changes !map));
    (* The snap rule, over hundreds of histories: an empty index always means a shared root. *)
    Alcotest.(check bool)
      (label ^ ": an empty index means a shared root")
      (change_count !map = 0) (is_clean !map);
    (* A restricted query must agree with the full enumeration filtered after the fact. *)
    let low = next 12 in
    let high = low + next 5 in
    Alcotest.(check (list int))
      (label ^ ": range restriction")
      (List.filter_map
         (fun change ->
           let key = change_key change in
           if key >= low && key <= high then Some key else None)
         (changes !map))
      (List.map change_key (changes_in_range ~minimum:low ~maximum:high !map))
  done

let tests =
  [
    Alcotest.test_case "an equal write is a semantic no-op" `Quick
      test_equal_write_is_a_semantic_no_op;
    Alcotest.test_case "repeated writes coalesce" `Quick
      test_repeated_writes_coalesce_into_one_record;
    Alcotest.test_case "cancellation snaps back to the checkpoint root" `Quick
      test_cancellation_removes_the_record_and_snaps_the_root;
    Alcotest.test_case "key representatives are retained" `Quick test_key_representative_retention;
    Alcotest.test_case "removing an absent key is a no-op" `Quick
      test_removing_an_absent_key_is_a_no_op;
    Alcotest.test_case "checkpoint and rollback are root swaps" `Quick
      test_checkpoint_and_rollback_are_root_swaps;
    Alcotest.test_case "change enumeration is ordered and classified" `Quick
      test_change_enumeration_is_ordered_and_classified;
    Alcotest.test_case "range restriction seeks boundaries" `Quick
      test_range_restriction_seeks_boundaries;
    Alcotest.test_case "inverted ranges follow the comparator" `Quick
      test_inverted_ranges_follow_the_retained_comparator;
    Alcotest.test_case "reads see the current state" `Quick
      test_reads_and_neighbours_see_the_current_state;
    Alcotest.test_case "set_list is exactly the fold" `Quick test_set_list_is_exactly_the_fold;
    Alcotest.test_case "of_list builds a clean checkpoint" `Quick
      test_of_list_builds_a_clean_checkpoint;
    Alcotest.test_case "validate reports statistics" `Quick test_validate_reports_statistics;
    Alcotest.test_case "retained versions stay independent" `Quick
      test_retained_versions_stay_independent;
    Alcotest.test_case "a random history matches a model" `Quick
      test_random_history_matches_a_model;
  ]
