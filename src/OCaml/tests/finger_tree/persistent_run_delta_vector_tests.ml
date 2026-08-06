(** Tests for the fixed-length persistent vector with a checkpoint and a maximal-run change index.

    Three properties are load-bearing and each is stated so that a plausible wrong implementation
    fails it.

    The run index must hold the {e maximal} runs. A merge-free implementation answers every
    membership question correctly while publishing one descriptor per dirty position, so only the
    descriptor ladder below — left merge, right merge, both merge, and the four-way removal split —
    can catch it.

    Accepting or reverting a run must be a structural splice, not a walk. A walking implementation
    is observably correct, so the evidence has to be a counting relation that stays at zero across
    the splice; a zero-count assertion is worthless unless the counter is known to move, so every
    such window is preceded by a positive control that proves the counter is live.

    A cancelled write must restore the checkpoint's own slot, not an equal one. Value equality
    cannot see the difference, so the tests pin it with physical equality: once through a payload
    whose relation deliberately ignores identity, and once through an immediate [int] payload, where
    a slot-free implementation would make the question unanswerable.

    Structural-sharing claims are only made between two genuinely distinct versions, and each one is
    paired with the opposite claim on the other root, so a predicate stuck at [true] fails. *)

open Durable7
open Finger_tree
open Persistent_run_delta_vector

let ok label = function
  | Ok value -> value
  | Error message -> Alcotest.failf "%s: unexpected error %S" label message

let expect_error label = function
  | Error _ -> ()
  | Ok _ -> Alcotest.failf "%s: expected an error" label

let check_int_list label expected actual = Alcotest.(check (list int)) label expected actual
let check_bool label expected actual = Alcotest.(check bool) label expected actual

let run_testable =
  Alcotest.testable
    (fun formatter run -> Format.fprintf formatter "[%d, %d)" run.start (run_end_exclusive run))
    (fun left right -> left.start = right.start && left.length = right.length)

let total_length runs = List.fold_left (fun total run -> total + run.length) 0 runs

(** Checks the published descriptors, both addressing routes, the cardinalities, and the independent
    audit. Every position of every expected run is probed, so a run that is merely reported with the
    right bounds but not indexed at its interior positions still fails. *)
let check_runs label expected vector =
  Alcotest.(check (list run_testable)) (label ^ ": descriptors") expected (dirty_runs vector);
  Alcotest.(check int) (label ^ ": run count") (List.length expected) (dirty_run_count vector);
  Alcotest.(check int) (label ^ ": dirty count") (total_length expected) (dirty_count vector);
  check_bool (label ^ ": has changes") (expected <> []) (has_changes vector);
  List.iteri
    (fun rank run ->
      Alcotest.(check (option run_testable))
        (Printf.sprintf "%s: rank %d" label rank)
        (Some run) (dirty_run_at rank vector);
      for index = run.start to run_end_exclusive run - 1 do
        Alcotest.(check (option bool))
          (Printf.sprintf "%s: position %d is dirty" label index)
          (Some true) (is_dirty index vector);
        Alcotest.(check (option run_testable))
          (Printf.sprintf "%s: run containing %d" label index)
          (Some run)
          (dirty_run_containing index vector)
      done)
    expected;
  Alcotest.(check (option run_testable))
    (label ^ ": past the last rank")
    None
    (dirty_run_at (List.length expected) vector);
  for index = 0 to length vector - 1 do
    if not (List.exists (run_contains index) expected) then (
      Alcotest.(check (option bool))
        (Printf.sprintf "%s: position %d is clean" label index)
        (Some false) (is_dirty index vector);
      Alcotest.(check (option run_testable))
        (Printf.sprintf "%s: no run contains %d" label index)
        None
        (dirty_run_containing index vector))
  done;
  ignore (ok (label ^ ": audit") (validate vector) : statistics)

let test_empty_and_construction_contracts () =
  let vacant : int t = empty (default ()) in
  Alcotest.(check int) "empty length" 0 (length vacant);
  check_bool "empty is empty" true (is_empty vacant);
  check_bool "empty is clean" false (has_changes vacant);
  Alcotest.(check int) "empty dirty count" 0 (dirty_count vacant);
  Alcotest.(check int) "empty run count" 0 (dirty_run_count vacant);
  check_int_list "empty values" [] (to_list vacant);
  Alcotest.(check (option int)) "empty read" None (nth 0 vacant);
  Alcotest.(check (option int)) "empty checkpoint read" None (checkpoint_nth 0 vacant);
  Alcotest.(check (option bool)) "empty membership" None (is_dirty 0 vacant);
  Alcotest.(check (option run_testable)) "empty containing" None (dirty_run_containing 0 vacant);
  Alcotest.(check (option run_testable)) "empty rank" None (dirty_run_at 0 vacant);
  Alcotest.(check (list run_testable)) "empty descriptors" [] (dirty_runs vacant);
  expect_error "empty set" (set 0 1 vacant);
  expect_error "empty reset" (reset 0 vacant);
  expect_error "empty accept by rank" (accept_dirty_run_at 0 vacant);
  expect_error "empty revert by rank" (revert_dirty_run_at 0 vacant);
  expect_error "empty accept by position" (accept_dirty_run_containing 0 vacant);
  expect_error "empty revert by position" (revert_dirty_run_containing 0 vacant);
  check_bool "empty checkpoint returns the receiver" true (checkpoint vacant == vacant);
  check_bool "empty rollback returns the receiver" true (rollback vacant == vacant);
  ignore (ok "empty audit" (validate vacant) : statistics);
  let source = of_list (default ()) [ 3; 1; 4; 1; 5 ] in
  Alcotest.(check int) "source length" 5 (length source);
  check_int_list "source values" [ 3; 1; 4; 1; 5 ] (to_list source);
  check_int_list "source checkpoint values" [ 3; 1; 4; 1; 5 ] (checkpoint_to_list source);
  check_runs "clean source" [] source;
  Alcotest.(check (option int)) "negative read" None (nth (-1) source);
  Alcotest.(check (option int)) "past-the-end read" None (nth 5 source);
  Alcotest.(check (option bool)) "negative membership" None (is_dirty (-1) source);
  Alcotest.(check (option bool)) "past-the-end membership" None (is_dirty 5 source);
  Alcotest.(check (option bool))
    "past-the-end representative" None
    (reuses_checkpoint_representative 5 source);
  expect_error "past-the-end set" (set 5 0 source);
  expect_error "negative set" (set (-1) 0 source);
  expect_error "past-the-end accept" (accept_dirty_run_containing 5 source);
  expect_error "past-the-end revert" (revert_dirty_run_containing 5 source);
  expect_error "rank on a clean version" (accept_dirty_run_at 0 source);
  Alcotest.(check int) "run descriptor end" 5 (run_end_exclusive { start = 3; length = 2 });
  check_bool "run contains its start" true (run_contains 3 { start = 3; length = 2 });
  check_bool "run excludes its end" false (run_contains 5 { start = 3; length = 2 })

let test_relation_governs_no_ops_and_cancellation () =
  let relation =
    equality (fun left right ->
        String.equal (String.lowercase_ascii left) (String.lowercase_ascii right))
  in
  let source = of_list relation [ "Alpha"; "Beta" ] in
  check_bool "the retained relation is the one supplied" true
    (same_equality (value_equality source) relation);
  check_bool "an independent custom relation is a distinct policy" false
    (same_equality relation (equality String.equal));
  check_bool "canonical relations match across construction" true
    (same_equality (default ()) (default ()));

  (* A relation-equal write is a semantic no-op: the receiver itself comes back, which is the
     strongest available statement that every root is shared. *)
  let no_op = ok "relation-equal write" (set 0 "ALPHA" source) in
  check_bool "a relation-equal write returns the receiver" true (no_op == source);
  Alcotest.(check (option string)) "the current representative is kept" (Some "Alpha") (nth 0
  no_op); check_runs "after a relation-equal write" [] no_op;

  (* Negative control: a genuine write must produce a distinct version that shares neither the
     receiver nor its current root, so the sharing predicates above are not stuck at true. *)
  let dirty = ok "genuine write" (set 0 "Gamma" source) in
  check_bool "a genuine write produces a distinct version" false (dirty == source);
  check_bool "a genuine write forks the current root" false (shares_current_root dirty source);
  check_bool "a genuine write keeps the checkpoint root" true
    (shares_checkpoint_root dirty source);
  Alcotest.(check (option string)) "the new value is current" (Some "Gamma") (nth 0 dirty);
  Alcotest.(check (option string))
    "the checkpoint value is retained" (Some "Alpha") (checkpoint_nth 0 dirty);
  check_runs "after a genuine write" [ { start = 0; length = 1 } ] dirty;

  (* Landing back in the checkpoint's equality class cancels the delta and restores the checkpoint's
     own representative, not the relation-equal value the caller supplied. *)
  let cancelled = ok "cancelling write" (set 0 "alpha" dirty) in
  check_runs "after cancellation" [] cancelled;
  Alcotest.(check (option string))
    "cancellation restores the checkpoint representative" (Some "Alpha") (nth 0 cancelled);
  check_bool "cancellation produces a distinct version" false (cancelled == source);
  check_bool "cancellation canonicalizes onto the checkpoint root" true
    (shares_current_root cancelled source && shares_checkpoint_root cancelled source);
  Alcotest.(check (option bool))
    "the cancelled position reuses its checkpoint slot" (Some true)
    (reuses_checkpoint_representative 0 cancelled);
  Alcotest.(check (option bool))
    "the dirty position does not" (Some false)
    (reuses_checkpoint_representative 0 dirty);

  (* Every retained intermediate version is untouched. *)
  Alcotest.(check (option string)) "the dirty version is retained" (Some "Gamma") (nth 0 dirty);
  check_runs "the retained dirty version" [ { start = 0; length = 1 } ] dirty;
  check_runs "the retained source" [] source

let test_float_reflexivity_is_the_callers_obligation () =
  let missing = Float.nan in
  (* OCaml's structural equality is not reflexive on nan while its comparison is; the two disagree,
     and only the reflexive one is a legal relation here. *)
  check_bool "Stdlib.( = ) is not reflexive on nan" false (missing = missing);
  check_bool "Stdlib.compare is reflexive on nan" true (Stdlib.compare missing missing = 0);
  check_bool "Float.equal is reflexive on nan" true (Float.equal missing missing);
  check_bool "signed zeros are one class" true (Float.equal 0.0 (-0.0));

  let source = of_list (reflexive_ieee ()) [ missing; 1.0; 0.0 ] in
  ignore (ok "reflexive source audit" (validate source) : statistics);
  let rewritten = ok "nan rewrite" (set 0 missing source) in
  check_bool "rewriting nan is a no-op" true (rewritten == source);
  check_runs "after a nan rewrite" [] rewritten;
  let signed_zero = ok "signed-zero rewrite" (set 2 (-0.0) source) in
  check_bool "rewriting a signed zero is a no-op" true (signed_zero == source);

  (* A genuine change is still recorded, and writing nan back cancels it. *)
  let changed = ok "float change" (set 0 2.0 source) in
  check_runs "a genuine float change" [ { start = 0; length = 1 } ] changed;
  let restored = ok "float cancellation" (set 0 missing changed) in
  check_runs "after a nan cancellation" [] restored;
  check_bool "the restored value is nan" true
    (match nth 0 restored with Some value -> Float.is_nan value | None -> false);
  Alcotest.(check (option bool))
    "the restored position reuses its checkpoint slot" (Some true)
    (reuses_checkpoint_representative 0 restored);

  (* The polymorphic canonical relation is reflexive too, so it is a safe default even for floats.
  *) let canonical = of_list (default ()) [ missing; 1.0 ] in
  check_bool "the canonical relation makes a nan rewrite a no-op" true
    (ok "canonical nan rewrite" (set 0 missing canonical) == canonical);

  (* The inherited gotcha, made visible: a naive structural relation is not an equivalence relation
     on floats, so rewriting nan is recorded as a change and leaves a phantom run. The audit rejects
     the version rather than letting the corruption stay silent. *)
  let naive = of_list (equality (fun left right -> left = right)) [ missing; 1.0 ] in
  expect_error "a non-reflexive relation fails the audit" (validate naive);
  let phantom = ok "naive nan rewrite" (set 0 missing naive) in
  Alcotest.(check int) "a non-reflexive relation invents a change" 1 (dirty_count phantom);
  Alcotest.(check (list run_testable))
    "a non-reflexive relation invents a run"
    [ { start = 0; length = 1 } ]
    (dirty_runs phantom)

let test_point_edits_maintain_maximal_runs () =
  let source = of_list (default ()) (List.init 12 (fun _ -> 0)) in
  let apply label index value vector = ok label (set index value vector) in
  let clear label index vector = ok label (reset index vector) in
  let vector = apply "set 2" 2 20 source in
  let vector = apply "set 4" 4 40 vector in
  check_runs "two singleton runs" [ { start = 2; length = 1 }; { start = 4; length = 1 } ] vector;

  (* Filling the gap between two singleton runs merges on both sides. *)
  let vector = apply "set 3" 3 30 vector in
  check_runs "both-merge" [ { start = 2; length = 3 } ] vector;

  (* A detached position, then the gap that joins it to the run on its left. *)
  let vector = apply "set 6" 6 60 vector in
  check_runs "detached singleton" [ { start = 2; length = 3 }; { start = 6; length = 1 } ] vector;
  let vector = apply "set 5" 5 50 vector in
  check_runs "left and right merge" [ { start = 2; length = 5 } ] vector;

  (* Extending a run at its right edge, then clearing it back. *)
  let vector = apply "set 7" 7 70 vector in
  check_runs "right-extend" [ { start = 2; length = 6 } ] vector;
  let vector = clear "reset 7" 7 vector in
  check_runs "right-edge removal" [ { start = 2; length = 5 } ] vector;

  (* Clearing an interior position splits one run into two. *)
  let vector = clear "reset 4" 4 vector in
  check_runs "interior split" [ { start = 2; length = 2 }; { start = 5; length = 2 } ] vector;
  let vector = clear "reset 2" 2 vector in
  check_runs "left-edge removal" [ { start = 3; length = 1 }; { start = 5; length = 2 } ] vector;
  let vector = clear "reset 6" 6 vector in
  check_runs "right-edge removal again" [ { start = 3; length = 1 }; { start = 5; length = 1 } ]
    vector;

  (* Negative control for the root predicates: a dirty version shares neither current root. *)
  check_bool "a dirty version has forked its current root" false
    (shares_current_root vector source);
  check_bool "a dirty version keeps its checkpoint root" true
    (shares_checkpoint_root vector source);

  (* Clearing the last dirty position snaps the current root back onto the checkpoint root. The
     comparison is against the distinct clean source, whose two roots are already the same value, so
     it is a claim about two genuinely different versions rather than a self-comparison. *)
  let vector = clear "reset 3" 3 vector in
  let cleared = clear "reset 5" 5 vector in
  check_runs "fully cleared" [] cleared;
  check_int_list "the cleared values match the source" (to_list source) (to_list cleared);
  check_bool "the cleared version is distinct from the source" false (cleared == source);
  check_bool "the cleared version canonicalizes onto the checkpoint root" true
    (shares_current_root cleared source && shares_checkpoint_root cleared source);
  for index = 0 to length cleared - 1 do
    Alcotest.(check (option bool))
      (Printf.sprintf "position %d reuses its checkpoint slot" index)
      (Some true)
      (reuses_checkpoint_representative index cleared)
  done;

  (* Clearing an already clean position is a no-op returning the receiver. *)
  check_bool "resetting a clean position returns the receiver" true
    (clear "reset a clean position" 3 cleared == cleared);
  check_runs "the retained source" [] source

let counting_equality calls = equality (fun left right -> incr calls; Int.equal left right)

let test_run_splices_make_no_value_comparisons () =
  let calls = ref 0 in
  let source = of_list (counting_equality calls) (List.init 20 (fun index -> index)) in
  let dirty_positions = [ 2; 3; 4; 5; 9; 10; 11; 12; 13; 17 ] in
  let edited =
    List.fold_left
      (fun vector index -> ok "seed" (set index (1000 + index) vector))
      source dirty_positions
  in
  let expected =
    [ { start = 2; length = 4 }; { start = 9; length = 5 }; { start = 17; length = 1 } ]
  in
  check_runs "seeded runs" expected edited;

  (* Positive control: the counter must move for a comparison-free window to mean anything. *)
  let before_control = !calls in
  let control = ok "positive control" (set 0 999 edited) in
  check_bool "the relation counter is live" true (!calls > before_control);
  Alcotest.(check int) "the control write landed" 11 (dirty_count control);

  (* Accepting the middle run consults the relation zero times, keeps the current root, and forks
     only the checkpoint root. *)
  let before = !calls in
  let accepted = ok "accept rank 1" (accept_dirty_run_at 1 edited) in
  Alcotest.(check int) "accepting a run makes no comparison" before !calls;
  check_runs "after accepting the middle run"
    [ { start = 2; length = 4 }; { start = 17; length = 1 } ]
    accepted;
  check_bool "accepting keeps the current root" true (shares_current_root accepted edited);
  check_bool "accepting forks the checkpoint root" false (shares_checkpoint_root accepted edited);
  List.iter
    (fun index ->
      Alcotest.(check (option int))
        (Printf.sprintf "accepted current %d" index)
        (Some (1000 + index)) (nth index accepted);
      Alcotest.(check (option int))
        (Printf.sprintf "accepted checkpoint %d" index)
        (Some (1000 + index))
        (checkpoint_nth index accepted))
    [ 9; 10; 11; 12; 13 ];

  (* Reverting the first run consults the relation zero times, keeps the checkpoint root, and forks
     only the current root. *)
  let before = !calls in
  let reverted = ok "revert rank 0" (revert_dirty_run_at 0 accepted) in
  Alcotest.(check int) "reverting a run makes no comparison" before !calls;
  check_runs "after reverting the first run" [ { start = 17; length = 1 } ] reverted;
  check_bool "reverting keeps the checkpoint root" true (shares_checkpoint_root reverted accepted);
  check_bool "reverting forks the current root" false (shares_current_root reverted accepted);
  List.iter
    (fun index ->
      Alcotest.(check (option int))
        (Printf.sprintf "reverted current %d" index)
        (Some index) (nth index reverted))
    [ 2; 3; 4; 5 ];

  (* The sole-run case collapses onto the whole-checkpoint path and still makes no comparison. *)
  let before = !calls in
  let clean = ok "accept the last run" (accept_dirty_run_at 0 reverted) in
  Alcotest.(check int) "the sole-run collapse makes no comparison" before !calls;
  check_runs "after accepting the last run" [] clean;
  Alcotest.(check (option int)) "the accepted value became the checkpoint" (Some 1017)
    (checkpoint_nth 17 clean);

  expect_error "accepting past the last rank" (accept_dirty_run_at 3 edited);
  expect_error "reverting past the last rank" (revert_dirty_run_at 3 edited);
  expect_error "accepting a negative rank" (accept_dirty_run_at (-1) edited);
  check_runs "every retained source version is untouched" expected edited;
  check_runs "the original stays clean" [] source

let test_runs_are_addressable_by_rank_and_by_position () =
  let calls = ref 0 in
  let source = of_list (counting_equality calls) (List.init 16 (fun index -> index)) in
  let edited =
    List.fold_left
      (fun vector index -> ok "seed" (set index (1000 + index) vector))
      source
      [ 2; 3; 4; 5; 9; 10; 11 ]
  in
  let expected = [ { start = 2; length = 4 }; { start = 9; length = 3 } ] in
  check_runs "seeded runs" expected edited;

  (* A clean position is vacuous rather than an error: the receiver comes back and the relation is
     never consulted. *)
  List.iter
    (fun index ->
      let before = !calls in
      let accepted = ok "accept a clean position" (accept_dirty_run_containing index edited) in
      let reverted = ok "revert a clean position" (revert_dirty_run_containing index edited) in
      Alcotest.(check int) "a clean position makes no comparison" before !calls;
      check_bool "accepting a clean position returns the receiver" true (accepted == edited);
      check_bool "reverting a clean position returns the receiver" true (reverted == edited))
    [ 0; 6; 8; 15 ];

  (* Every position inside a run acts exactly as the rank-addressed operation on that run. *)
  List.iteri
    (fun rank run ->
      for index = run.start to run_end_exclusive run - 1 do
        let before = !calls in
        let by_position = ok "accept by position" (accept_dirty_run_containing index edited) in
        let reverted_by_position = ok "revert by position" (revert_dirty_run_containing index
        edited) in Alcotest.(check int) "a position-addressed splice makes no comparison" before
        !calls; let by_rank = ok "accept by rank" (accept_dirty_run_at rank edited) in
        let reverted_by_rank = ok "revert by rank" (revert_dirty_run_at rank edited) in
        check_int_list "accepting agrees on values" (to_list by_rank) (to_list by_position);
        check_int_list "accepting agrees on checkpoint values" (checkpoint_to_list by_rank)
          (checkpoint_to_list by_position);
        Alcotest.(check (list run_testable))
          "accepting agrees on descriptors" (dirty_runs by_rank) (dirty_runs by_position);
        check_int_list "reverting agrees on values" (to_list reverted_by_rank)
          (to_list reverted_by_position);
        check_int_list "reverting agrees on checkpoint values"
          (checkpoint_to_list reverted_by_rank)
          (checkpoint_to_list reverted_by_position);
        Alcotest.(check (list run_testable))
          "reverting agrees on descriptors" (dirty_runs reverted_by_rank)
          (dirty_runs reverted_by_position);
        ignore (ok "position-addressed audit" (validate by_position) : statistics);
        ignore (ok "rank-addressed audit" (validate reverted_by_position) : statistics)
      done)
    expected;
  check_runs "every retained source version is untouched" expected edited

let test_run_splices_are_independent_of_run_length () =
  (* The k-versus-r witness: thousands of dirty positions, exactly two descriptors, and a splice
     over the huge run that never looks at a payload. *)
  let count = 4096 in
  let calls = ref 0 in
  let source = of_list (counting_equality calls) (List.init count (fun _ -> 0)) in
  let edited =
    List.fold_left
      (fun vector index -> ok "seed" (set index 1 vector))
      source
      (List.init (count - 2) (fun index -> index))
  in
  let edited = ok "seed the detached tail" (set (count - 1) 1 edited) in
  Alcotest.(check int) "dirty positions" (count - 1) (dirty_count edited);
  Alcotest.(check int) "descriptors stay at two" 2 (dirty_run_count edited);
  Alcotest.(check int) "enumeration is output-optimal" 2 (List.length (dirty_runs edited));
  Alcotest.(check (list run_testable))
    "the two maximal runs"
    [ { start = 0; length = count - 2 }; { start = count - 1; length = 1 } ]
    (dirty_runs edited);

  (* Positive control before the comparison-free window. *)
  let before_control = !calls in
  ignore (ok "positive control" (set 5 7 edited) : int t);
  check_bool "the relation counter is live" true (!calls > before_control);

  let before = !calls in
  let accepted = ok "accept the huge run" (accept_dirty_run_at 0 edited) in
  Alcotest.(check int) "splicing a 4094-position run makes no comparison" before !calls;
  Alcotest.(check (list run_testable))
    "only the detached run survives"
    [ { start = count - 1; length = 1 } ]
    (dirty_runs accepted);
  Alcotest.(check int) "the accepted positions are clean" 1 (dirty_count accepted);
  Alcotest.(check (option int)) "the spliced checkpoint took the new value" (Some 1)
    (checkpoint_nth 0 accepted);
  Alcotest.(check (option int)) "the untouched checkpoint kept the old value" (Some 0)
    (checkpoint_nth (count - 1) accepted);

  let before = !calls in
  let reverted = ok "revert the huge run" (revert_dirty_run_at 0 edited) in
  Alcotest.(check int) "reverting a 4094-position run makes no comparison" before !calls;
  Alcotest.(check (option int)) "the reverted current took the checkpoint value" (Some 0)
    (nth 0 reverted);
  ignore (ok "spliced audit" (validate accepted) : statistics);
  ignore (ok "reverted audit" (validate reverted) : statistics);
  check_int_list "the original is untouched" (List.init count (fun _ -> 0)) (to_list source)

let test_cancellation_restores_the_exact_representative () =
  (* A relation that compares contents while the payloads carry identity: value equality cannot see
     which of two equal representatives was restored, so only physical equality can.

     Every cancellation here happens while a second, deliberately pinned run is still dirty. That
     matters: when the last change clears, the whole current root is swapped for the checkpoint
     root, and that wholesale swap masks which slot the cancelling write actually chose. Only a
     cancellation that keeps the rebuilt root can see the difference. *)
  let relation = equality (fun left right -> Int.equal !left !right) in
  let checkpoint_representative = ref 7 in
  let replacement_representative = ref 7 in
  check_bool "the two representatives are relation-equal" true
    (equal relation checkpoint_representative replacement_representative);
  check_bool "the two representatives are physically distinct" false
    (checkpoint_representative == replacement_representative);

  let source = of_list relation [ checkpoint_representative; ref 1; ref 2 ] in
  let pinned = ok "pin a second run" (set 2 (ref 200) source) in
  let dirty = ok "write" (set 0 (ref 9) pinned) in
  check_runs "a cancellable run beside a pinned one"
    [ { start = 0; length = 1 }; { start = 2; length = 1 } ]
    dirty;
  Alcotest.(check (option bool))
    "a dirty position does not reuse its slot" (Some false)
    (reuses_checkpoint_representative 0 dirty);

  let restored = ok "cancel" (set 0 replacement_representative dirty) in
  check_runs "only the pinned run survives" [ { start = 2; length = 1 } ] restored;
  check_bool "the version stays off the canonicalizing path" true (has_changes restored);
  Alcotest.(check (option bool))
    "the cancelled position reuses the checkpoint's own slot" (Some true)
    (reuses_checkpoint_representative 0 restored);
  check_bool "the checkpoint's own representative is restored" true
    (match nth 0 restored with Some value -> value == checkpoint_representative | None -> false);
  check_bool "the caller's equal representative is discarded" false
    (match nth 0 restored with Some value -> value == replacement_representative | None -> false);

  (* The reset route reaches the same place. *)
  let reset_back = ok "reset" (reset 0 dirty) in
  check_runs "resetting leaves the pinned run" [ { start = 2; length = 1 } ] reset_back;
  Alcotest.(check (option bool))
    "resetting reuses the checkpoint's own slot" (Some true)
    (reuses_checkpoint_representative 0 reset_back);
  check_bool "resetting restores the checkpoint's own representative" true
    (match nth 0 reset_back with Some value -> value == checkpoint_representative | None -> false);

  (* An immediate payload: without a per-position slot the question would be unanswerable, because
     physical equality on integers collapses into value equality. *)
  let immediate = ok "pin a run" (set 2 99 (of_list (default ()) [ 7; 8; 9 ])) in
  let changed = ok "immediate write" (set 0 5 immediate) in
  Alcotest.(check (option bool))
    "a dirty immediate position does not reuse its slot" (Some false)
    (reuses_checkpoint_representative 0 changed);
  Alcotest.(check (option bool))
    "an untouched immediate position still reuses its slot" (Some true)
    (reuses_checkpoint_representative 1 changed);
  let recovered = ok "immediate cancel" (set 0 7 changed) in
  check_runs "the cancelled immediate version keeps its pinned run"
    [ { start = 2; length = 1 } ]
    recovered;
  Alcotest.(check (option bool))
    "a cancelled immediate position reuses its slot again" (Some true)
    (reuses_checkpoint_representative 0 recovered);

  (* Clearing the last change does land back on the checkpoint root, compared against a distinct
     rolled-back version rather than against the receiver. *)
  let fully_clean = ok "clear the pinned run" (reset 2 recovered) in
  check_runs "fully cleared" [] fully_clean;
  check_bool "the fully cleared version canonicalizes onto the checkpoint root" true
    (shares_current_root fully_clean (rollback recovered))

let test_checkpoint_and_rollback_are_root_swaps () =
  let source = of_list (default ()) [ 1; 2; 3; 4 ] in
  check_bool "checkpointing a clean version returns the receiver" true
    (checkpoint source == source);
  check_bool "rolling back a clean version returns the receiver" true (rollback source == source);

  let dirty = ok "write" (set 1 20 source) in
  let accepted = checkpoint dirty in
  check_bool "checkpointing forks the version" false (accepted == dirty);
  check_bool "checkpointing keeps the current root" true (shares_current_root accepted dirty);
  check_bool "checkpointing replaces the checkpoint root" false
    (shares_checkpoint_root accepted dirty);
  check_runs "a checkpointed version is clean" [] accepted;
  check_int_list "the checkpointed values" [ 1; 20; 3; 4 ] (to_list accepted);
  check_int_list "the checkpoint took the current values" [ 1; 20; 3; 4 ]
    (checkpoint_to_list accepted);

  let rolled = rollback dirty in
  check_bool "rolling back forks the version" false (rolled == dirty);
  check_bool "rolling back keeps the checkpoint root" true (shares_checkpoint_root rolled dirty);
  check_bool "rolling back replaces the current root" false (shares_current_root rolled dirty);
  check_runs "a rolled-back version is clean" [] rolled;
  check_int_list "the rolled-back values" [ 1; 2; 3; 4 ] (to_list rolled);
  check_bool "rolling back lands on the source roots" true
    (shares_current_root rolled source && shares_checkpoint_root rolled source);
  check_runs "the retained dirty version" [ { start = 1; length = 1 } ] dirty

(* The independent model: the maximal runs of a pair of arrays, recomputed from scratch rather than
   maintained, so an incrementally maintained index that drifts is caught. *)
let expected_runs live saved =
  let count = Array.length live in
  let rec extend index =
    if index < count && live.(index) <> saved.(index) then extend (index + 1) else index
  in
  let rec scan index accumulated =
    if index >= count then List.rev accumulated
    else if live.(index) = saved.(index) then scan (index + 1) accumulated
    else
      let finish = extend (index + 1) in
      scan finish ({ start = index; length = finish - index } :: accumulated)
  in
  scan 0 []

let assert_matches_model label vector live saved =
  Alcotest.(check int) (label ^ ": length") (Array.length live) (length vector);
  check_int_list (label ^ ": values") (Array.to_list live) (to_list vector);
  check_int_list (label ^ ": checkpoint values") (Array.to_list saved) (checkpoint_to_list vector);
  check_runs label (expected_runs live saved) vector

let copy_range source destination run =
  for index = run.start to run_end_exclusive run - 1 do
    destination.(index) <- source.(index)
  done

let test_randomized_versions_match_an_independent_model () =
  let count = 24 in
  let state = Random.State.make [| 0x0B5E2026 |] in
  let initial = Array.init count (fun index -> index mod 5) in
  let versions =
    ref [ (of_list (default ()) (Array.to_list initial), Array.copy initial, Array.copy initial) ]
  in
  for step = 1 to 1200 do
    let label = Printf.sprintf "step %d" step in
    let parent, parent_live, parent_saved =
      List.nth !versions (Random.State.int state (List.length !versions))
    in
    let live = Array.copy parent_live in
    let saved = Array.copy parent_saved in
    let index = Random.State.int state count in
    let result =
      match Random.State.int state 9 with
      | 0 | 1 | 2 ->
          let value = Random.State.int state 7 - 3 in
          live.(index) <- value;
          ok label (set index value parent)
      | 3 ->
          live.(index) <- saved.(index);
          ok label (reset index parent)
      | 4 ->
          Array.blit live 0 saved 0 count;
          checkpoint parent
      | 5 ->
          Array.blit saved 0 live 0 count;
          rollback parent
      | 6 -> (
          let spliced = ok label (accept_dirty_run_containing index parent) in
          match List.find_opt (run_contains index) (expected_runs live saved) with
          | Some run ->
              copy_range live saved run;
              spliced
          | None -> spliced)
      | 7 -> (
          let spliced = ok label (revert_dirty_run_containing index parent) in
          match List.find_opt (run_contains index) (expected_runs live saved) with
          | Some run ->
              copy_range saved live run;
              spliced
          | None -> spliced)
      | _ -> (
          match expected_runs live saved with
          | [] -> parent
          | runs ->
              let rank = Random.State.int state (List.length runs) in
              let run = List.nth runs rank in
              if Random.State.bool state then (
                let spliced = ok label (accept_dirty_run_at rank parent) in
                copy_range live saved run;
                spliced)
              else
                let spliced = ok label (revert_dirty_run_at rank parent) in
                copy_range saved live run;
                spliced)
    in
    assert_matches_model label result live saved;
    (* Deriving a branch leaves the parent version exactly as it was. *)
    assert_matches_model (label ^ " parent") parent parent_live parent_saved;
    versions := (result, live, saved) :: !versions;
    if List.length !versions > 10 then
      versions := List.filteri (fun position _ -> position < 10) !versions
  done

let tests =
  [
    Alcotest.test_case "empty and construction contracts" `Quick
      test_empty_and_construction_contracts;
    Alcotest.test_case "the relation governs no-ops and cancellation" `Quick
      test_relation_governs_no_ops_and_cancellation;
    Alcotest.test_case "float reflexivity is the caller's obligation" `Quick
      test_float_reflexivity_is_the_callers_obligation;
    Alcotest.test_case "point edits maintain maximal runs" `Quick
      test_point_edits_maintain_maximal_runs;
    Alcotest.test_case "run splices make no value comparisons" `Quick
      test_run_splices_make_no_value_comparisons;
    Alcotest.test_case "runs are addressable by rank and by position" `Quick
      test_runs_are_addressable_by_rank_and_by_position;
    Alcotest.test_case "run splices are independent of run length" `Quick
      test_run_splices_are_independent_of_run_length;
    Alcotest.test_case "cancellation restores the exact representative" `Quick
      test_cancellation_restores_the_exact_representative;
    Alcotest.test_case "checkpoint and rollback are root swaps" `Quick
      test_checkpoint_and_rollback_are_root_swaps;
    Alcotest.test_case "randomized versions match an independent model" `Quick
      test_randomized_versions_match_an_independent_model;
  ]
