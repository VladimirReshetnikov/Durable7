(** Bound probes for the measured finger-tree core.

    These are the inverted forms of the measurements that established the old weight-balanced join
    tree's weaker bounds. Each counts the policy's combine/measure calls, because asymptotic
    claims are invisible to correctness tests: an eager join tree would pass every behavioural
    case here while failing every counter. The probe shape and its pinned numbers match the
    Python, Rust, Kotlin, and TypeScript workspaces (340 combines for 512 endpoint pushes at both
    sizes; 0-vs-4 immediate concat combines; first-force replay versus a few combines per warmed
    branch). *)

open Durable7
open Finger_tree

let check_int_list label expected actual = Alcotest.(check (list int)) label expected actual

(* The size measure with live call counters. One policy value serves every probe; each resets the
   counters before measuring. *)
let combine_calls = ref 0
let measure_calls = ref 0

let counting_policy : (int, int) Measures.policy =
  Measures.create_policy ~id:"probe-counting-size-v1"
    ~monoid:
      (Measures.create_monoid ~empty:0 ~append:(fun left right ->
           incr combine_calls;
           left + right))
    ~measure:(fun _ ->
      incr measure_calls;
      1)
    ()

let reset_counters () =
  combine_calls := 0;
  measure_calls := 0

let total_calls () = !combine_calls + !measure_calls

let test_endpoint_push_cost_is_independent_of_length () =
  (* Endpoint pushes must cost amortized O(1) policy work - the join tree paid Theta(log n) per
     push. The immediate work of a push touches only the outer digit (one 2-3 node build per
     overflow, two combines each); every deeper cascade is suspended. The totals are therefore
     identical at both sizes, and pinned to the number every sibling workspace measured. *)
  let push_cost count =
    let tree = ref (Measured_tree.of_list counting_policy (List.init count Fun.id)) in
    reset_counters ();
    for value = 0 to 255 do
      tree := Measured_tree.snoc !tree value
    done;
    for value = 0 to 255 do
      tree := Measured_tree.cons value !tree
    done;
    let cost = !combine_calls in
    Alcotest.(check int) "pushed length" (count + 512) (Measured_tree.length !tree);
    cost
  in
  let small_cost = push_cost 1_024 in
  let large_cost = push_cost 32_768 in
  Alcotest.(check int) "512 pushes cost 340 combines at n=1024" 340 small_cost;
  Alcotest.(check int) "the 32x larger tree costs exactly the same" small_cost large_cost

let test_first_and_last_are_constant_reads () =
  (* Digit reads: no policy call, no middle forcing. *)
  let tree = ref (Measured_tree.of_list counting_policy (List.init 8_192 Fun.id)) in
  tree := Measured_tree.cons (-1) (Measured_tree.snoc !tree 8_192);
  reset_counters ();
  Alcotest.(check (option int)) "first" (Some (-1)) (Measured_tree.first !tree);
  Alcotest.(check (option int)) "last" (Some 8_192) (Measured_tree.last !tree);
  Alcotest.(check int) "length" 8_194 (Measured_tree.length !tree);
  Alcotest.(check int) "no policy calls" 0 (total_calls ())

let test_concat_immediate_cost_tracks_neither_operand () =
  (* Concatenation defers its middle recursion, so the immediate cost is one digit regrouping
     regardless of how unequal the operands are: zero combines against a singleton (a plain digit
     push) and four against a deep operand (two 2-3 nodes from a six-item boundary). The join
     tree's concat walked the taller operand's spine instead. *)
  let big = Measured_tree.of_list counting_policy (List.init 4_096 Fun.id) in
  let tiny = Measured_tree.of_list counting_policy [ 0 ] in
  let mid = Measured_tree.of_list counting_policy (List.init 1_024 Fun.id) in
  reset_counters ();
  let joined_tiny = Result.get_ok (Measured_tree.concat big tiny) in
  let tiny_cost = !combine_calls in
  reset_counters ();
  let joined_mid = Result.get_ok (Measured_tree.concat big mid) in
  let mid_cost = !combine_calls in
  Alcotest.(check int) "concat with a singleton costs 0 combines" 0 tiny_cost;
  Alcotest.(check int) "concat with a deep operand costs 4 combines" 4 mid_cost;
  Alcotest.(check int) "tiny join length" 4_097 (Measured_tree.length joined_tiny);
  Alcotest.(check int) "mid join length" 5_120 (Measured_tree.length joined_mid)

let test_forced_spines_are_memoized_across_persistent_branches () =
  (* Work deferred by one version and forced by another must never be repeated. This is the
     property that makes the amortized bounds valid under persistence: the suspended spine is
     shared, so the first full-measure force pays for everyone, and each of the 49 warmed sibling
     branches re-pays only its own unshared top-level digits. *)
  let base = ref (Measured_tree.empty counting_policy) in
  for value = 0 to 9_999 do
    base := Measured_tree.snoc !base value
  done;
  let branches = List.init 49 (fun value -> Measured_tree.snoc !base value) in
  reset_counters ();
  Alcotest.(check int) "first branch total" 10_001 (Measured_tree.measure (List.hd branches));
  let first_force = !combine_calls in
  reset_counters ();
  List.iter
    (fun branch -> Alcotest.(check int) "branch total" 10_001 (Measured_tree.measure branch))
    (List.tl branches);
  let rest_forces = !combine_calls in
  (* The numbers are deterministic and shared with every sibling workspace: the first force
     replays the whole deferred spine, and each warmed branch re-pays exactly its own unshared
     top-level digit work. *)
  Alcotest.(check int) "the first force replays the deferred spine" 3_336 first_force;
  Alcotest.(check int) "48 warmed branches pay 5 combines each" (48 * 5) rest_forces

let test_deep_pending_chains_force_iteratively () =
  (* Organic endpoint loops build Theta(n)-deep pending chains. The explicit-stack interpreter
     forces them iteratively; a recursive force - native [Lazy.force] included - overflows the
     stack long before 200k links, which was proven by experiment in this repository. *)
  let appended = ref (Measured_tree.empty Measures.size) in
  for value = 0 to 199_999 do
    appended := Measured_tree.snoc !appended value
  done;
  Alcotest.(check int) "measure forces the whole chain" 200_000 (Measured_tree.measure !appended);
  Alcotest.(check (option int)) "first" (Some 0) (Measured_tree.first !appended);
  Alcotest.(check (option int)) "last" (Some 199_999) (Measured_tree.last !appended);
  Alcotest.(check (option int)) "nth" (Some 123_456) (Measured_tree.nth 123_456 !appended);
  Alcotest.(check int)
    "iteration sees every element" 200_000
    (Measured_tree.fold_left (fun count _ -> count + 1) 0 !appended);
  let prepended = ref (Measured_tree.empty Measures.size) in
  for value = 0 to 99_999 do
    prepended := Measured_tree.cons value !prepended
  done;
  Alcotest.(check int) "prepend chain forces too" 100_000 (Measured_tree.measure !prepended);
  Alcotest.(check (option int)) "prepend first" (Some 99_999) (Measured_tree.first !prepended)

let test_validation_forces_pending_middles () =
  (* Deferred structure hides inside pending suspensions where no non-forcing walk can check it,
     so [validate] must force - and an append-built spine full of pending pushes must come out
     structurally sound. *)
  let tree = ref (Measured_tree.empty Measures.size) in
  for value = 0 to 9_999 do
    tree := Measured_tree.snoc !tree value
  done;
  Alcotest.(check (result unit string))
    "an unforced spine validates" (Ok ())
    (Measured_tree.validate !tree);
  Alcotest.(check int) "and still measures" 10_000 (Measured_tree.measure !tree)

let test_failed_forces_publish_nothing_and_are_retryable () =
  (* The eager join tree raised inside the edit itself; the lazy core raises on the first forced
     read instead. The raising force must publish nothing, be retryable, and leave the retained
     version intact once the failure clears. *)
  let armed = ref false in
  let failing_policy : (int, int) Measures.policy =
    Measures.create_policy ~id:"probe-failing-size-v1"
      ~monoid:
        (Measures.create_monoid ~empty:0 ~append:(fun left right ->
             if !armed then failwith "injected combine failure";
             left + right))
      ~measure:(fun _ -> 1)
      ()
  in
  let tree = ref (Measured_tree.empty failing_policy) in
  for value = 0 to 99 do
    tree := Measured_tree.snoc !tree value
  done;
  armed := true;
  let raises label =
    match Measured_tree.measure !tree with
    | (_ : int) -> Alcotest.fail (label ^ ": the armed policy must raise")
    | exception Failure _ -> ()
  in
  raises "first forced read";
  (* The failed force published nothing, so a retry fails the same way instead of reading a
     half-built spine. *)
  raises "retried forced read";
  armed := false;
  Alcotest.(check int) "the same suspensions force once the failure clears" 100
    (Measured_tree.measure !tree);
  check_int_list "the source survived" (List.init 100 Fun.id) (Measured_tree.to_list !tree);
  Alcotest.(check (result unit string)) "and validates" (Ok ()) (Measured_tree.validate !tree)

let test_reversed_view_is_lazy_and_correct () =
  (* Mirroring defers the middle's reversal: zero immediate policy calls (the outer digits hold
     bare elements), full order on read. *)
  let tree = ref (Measured_tree.empty counting_policy) in
  for value = 0 to 16_383 do
    tree := Measured_tree.snoc !tree value
  done;
  Alcotest.(check int) "warmed total" 16_384 (Measured_tree.measure !tree);
  reset_counters ();
  let mirrored = Measured_tree.reverse !tree in
  Alcotest.(check int) "mirroring costs no immediate policy calls" 0 (total_calls ());
  Alcotest.(check (option int)) "mirrored first" (Some 16_383) (Measured_tree.first mirrored);
  Alcotest.(check (option int)) "mirrored last" (Some 0) (Measured_tree.last mirrored);
  Alcotest.(check (option int)) "mirrored nth" (Some 16_383) (Measured_tree.nth 0 mirrored);
  Alcotest.(check (option int)) "mirrored end" (Some 0) (Measured_tree.nth 16_383 mirrored);
  check_int_list "mirrored order"
    (List.rev (List.init 16_384 Fun.id))
    (Measured_tree.to_list mirrored);
  Alcotest.(check (result unit string))
    "mirrored structure validates" (Ok ())
    (Measured_tree.validate mirrored)

let test_cross_orientation_concat_is_structural () =
  (* The reversible deque's mismatched concat must join structurally, not materialize. Measured at
     the substrate level: mirror-then-concat of a 16384-element tree with a small one costs the
     same handful of immediate policy calls as a same-orientation concat - the Theta(n + m)
     materialization this replaced would pay tens of thousands. *)
  let big = Measured_tree.of_list counting_policy (List.init 16_384 Fun.id) in
  let small = Measured_tree.of_list counting_policy (List.init 9 Fun.id) in
  Alcotest.(check int) "warm big" 16_384 (Measured_tree.measure big);
  Alcotest.(check int) "warm small" 9 (Measured_tree.measure small);
  reset_counters ();
  let joined = Result.get_ok (Measured_tree.concat big (Measured_tree.reverse small)) in
  let immediate = !combine_calls + !measure_calls in
  Alcotest.(check bool)
    (Printf.sprintf "mirror-then-concat costs a handful of immediate calls (%d)" immediate)
    true (immediate <= 8);
  Alcotest.(check int) "joined length" 16_393 (Measured_tree.length joined);
  Alcotest.(check (option int)) "the mirrored tail ends first" (Some 0)
    (Measured_tree.nth 16_392 joined);
  Alcotest.(check (option int)) "and starts last" (Some 8) (Measured_tree.nth 16_384 joined)

(* A deliberately non-commutative measure: concatenation of element spellings. Under this monoid a
   mirrored subtree's summary is not its cached summary read backwards, so any reversal that
   reuses cached measures reports the forward spelling for the reversed view. Only reversed-order
   recombination can pass these assertions. *)
let spelling_policy : (int, string) Measures.policy =
  Measures.create_policy ~id:"probe-spelling-v1"
    ~monoid:(Measures.create_monoid ~empty:"" ~append:( ^ ))
    ~measure:string_of_int ()

let test_reversed_view_is_correct_under_a_non_commutative_measure () =
  let values = List.init 300 (fun value -> (value * 7 mod 100) - 50) in
  let spelling items = String.concat "" (List.map string_of_int items) in
  let tree = ref (Measured_tree.empty spelling_policy) in
  List.iter (fun value -> tree := Measured_tree.snoc !tree value) values;
  let mirrored = Measured_tree.reverse !tree in
  let reversed_values = List.rev values in
  check_int_list "mirrored order" reversed_values (Measured_tree.to_list mirrored);
  Alcotest.(check string)
    "the summary mirrors too" (spelling reversed_values)
    (Measured_tree.measure mirrored);
  (* Interior structure must agree as well, not only the root summary: split inside the mirror and
     check both halves' summaries against the model. *)
  let left, right = Measured_tree.split_at 137 mirrored in
  Alcotest.(check string)
    "left half spelling"
    (spelling (List.filteri (fun index _ -> index < 137) reversed_values))
    (Measured_tree.measure left);
  Alcotest.(check string)
    "right half spelling"
    (spelling (List.filteri (fun index _ -> index >= 137) reversed_values))
    (Measured_tree.measure right);
  Alcotest.(check string)
    "a double reversal restores the forward spelling" (spelling values)
    (Measured_tree.measure (Measured_tree.reverse mirrored))

let test_reversible_deque_concat_is_orientation_aware () =
  (* Row A9/A8: every orientation pairing must concatenate to the logical model, structurally. *)
  let forward values = Reversible_deque.of_list values in
  let backward values = Reversible_deque.reverse (Reversible_deque.of_list (List.rev values)) in
  let left_model = List.init 100 Fun.id in
  let right_model = List.init 40 (fun value -> 500 + value) in
  let expected = left_model @ right_model in
  List.iter
    (fun (label, left, right) ->
      let joined = Reversible_deque.concat left right in
      check_int_list label expected (Reversible_deque.to_list joined);
      Alcotest.(check (option int))
        (label ^ ": first") (Some 0)
        (Reversible_deque.first joined);
      Alcotest.(check (option int))
        (label ^ ": last") (Some 539)
        (Reversible_deque.last joined);
      Alcotest.(check (option int))
        (label ^ ": nth across the seam") (Some 500)
        (Reversible_deque.nth 100 joined))
    [
      ("forward ++ forward", forward left_model, forward right_model);
      ("forward ++ reversed", forward left_model, backward right_model);
      ("reversed ++ forward", backward left_model, forward right_model);
      ("reversed ++ reversed", backward left_model, backward right_model);
    ];
  (* Operands are shared, not consumed: both still read back unchanged. *)
  check_int_list "left operand retained" left_model
    (Reversible_deque.to_list (backward left_model));
  check_int_list "right operand retained" right_model
    (Reversible_deque.to_list (forward right_model))

let test_reversible_deque_cursor_edits_are_tree_splices () =
  (* Row A9: cursor edits on a reversed deque go through O(log n) tree splices at the mirrored
     physical index, not a list rebuild. Verified against a list model on both orientations. *)
  let model = List.init 200 Fun.id in
  let deque = Reversible_deque.reverse (Reversible_deque.of_list (List.rev model)) in
  check_int_list "logical order" model (Reversible_deque.to_list deque);
  let cursor = Option.get (Reversible_deque.cursor_at 50 deque) in
  let cursor = Reversible_deque.cursor_insert_many [ 1_000; 1_001; 1_002 ] cursor in
  Alcotest.(check int) "position after insertion" 53 (Reversible_deque.cursor_position cursor);
  let cursor = Option.get (Reversible_deque.cursor_delete_previous cursor) in
  let cursor = Option.get (Reversible_deque.cursor_delete_next cursor) in
  let cursor = Option.get (Reversible_deque.cursor_replace_next 2_000 cursor) in
  (* Insert 1000-1002 at gap 50, delete 1002 behind the gap, delete the 50 ahead of it, then
     replace the 51 ahead of it with 2000. *)
  let expected =
    List.filteri (fun index _ -> index < 50) model
    @ [ 1_000; 1_001; 2_000 ]
    @ List.filteri (fun index _ -> index > 51) model
  in
  check_int_list "edited logical order" expected
    (Reversible_deque.to_list (Reversible_deque.cursor_snapshot cursor));
  check_int_list "source retained" model (Reversible_deque.to_list deque);
  (* The mirrored cursor addresses the same gap from the other end. *)
  let mirrored = Reversible_deque.cursor_reverse cursor in
  Alcotest.(check int)
    "mirrored position" (201 - 52)
    (Reversible_deque.cursor_position mirrored);
  check_int_list "mirrored snapshot" (List.rev expected)
    (Reversible_deque.to_list (Reversible_deque.cursor_snapshot mirrored))

let test_randomized_edits_match_a_list_model () =
  let generator = Random.State.make [| 20260806 |] in
  let tree = ref (Measured_tree.empty Measures.int_sum) in
  let model = ref [] in
  let sum items = List.fold_left ( + ) 0 items in
  for step = 0 to 599 do
    let choice = Random.State.int generator 6 in
    let size = List.length !model in
    (match choice with
    | 0 ->
        tree := Measured_tree.cons step !tree;
        model := step :: !model
    | 1 ->
        tree := Measured_tree.snoc !tree step;
        model := !model @ [ step ]
    | 2 when size > 0 ->
        let index = Random.State.int generator size in
        tree := Result.get_ok (Measured_tree.update_at index (fun value -> -value) !tree);
        model := List.mapi (fun at value -> if at = index then -value else value) !model
    | 3 when size > 0 ->
        let index = Random.State.int generator size in
        let removed, successor = Result.get_ok (Measured_tree.remove_at index !tree) in
        Alcotest.(check int) "removed element" (List.nth !model index) removed;
        tree := successor;
        model := List.filteri (fun at _ -> at <> index) !model
    | 4 ->
        let index = Random.State.int generator (size + 1) in
        tree := Result.get_ok (Measured_tree.insert_at index step !tree);
        model :=
          List.filteri (fun at _ -> at < index) !model
          @ (step :: List.filteri (fun at _ -> at >= index) !model)
    | _ ->
        let index = Random.State.int generator (size + 1) in
        let left, right = Measured_tree.split_at index !tree in
        check_int_list "left split"
          (List.filteri (fun at _ -> at < index) !model)
          (Measured_tree.to_list left);
        check_int_list "right split"
          (List.filteri (fun at _ -> at >= index) !model)
          (Measured_tree.to_list right);
        tree := Result.get_ok (Measured_tree.concat left right));
    Alcotest.(check int) "length" (List.length !model) (Measured_tree.length !tree);
    if !model <> [] then begin
      let probe = Random.State.int generator (List.length !model) in
      Alcotest.(check (option int))
        "random access" (Some (List.nth !model probe))
        (Measured_tree.nth probe !tree)
    end
  done;
  check_int_list "final order" !model (Measured_tree.to_list !tree);
  Alcotest.(check int) "final measure" (sum !model) (Measured_tree.measure !tree);
  Alcotest.(check (result unit string)) "final structure" (Ok ()) (Measured_tree.validate !tree)

let tests =
  [
    Alcotest.test_case "endpoint push cost is independent of length" `Quick
      test_endpoint_push_cost_is_independent_of_length;
    Alcotest.test_case "first and last are constant reads" `Quick
      test_first_and_last_are_constant_reads;
    Alcotest.test_case "concat immediate cost tracks neither operand" `Quick
      test_concat_immediate_cost_tracks_neither_operand;
    Alcotest.test_case "forced spines are memoized across branches" `Quick
      test_forced_spines_are_memoized_across_persistent_branches;
    Alcotest.test_case "deep pending chains force iteratively" `Quick
      test_deep_pending_chains_force_iteratively;
    Alcotest.test_case "validation forces pending middles" `Quick
      test_validation_forces_pending_middles;
    Alcotest.test_case "failed forces publish nothing and retry" `Quick
      test_failed_forces_publish_nothing_and_are_retryable;
    Alcotest.test_case "reversed view is lazy and correct" `Quick
      test_reversed_view_is_lazy_and_correct;
    Alcotest.test_case "cross-orientation concat is structural" `Quick
      test_cross_orientation_concat_is_structural;
    Alcotest.test_case "reversed view under a non-commutative measure" `Quick
      test_reversed_view_is_correct_under_a_non_commutative_measure;
    Alcotest.test_case "reversible deque concat is orientation-aware" `Quick
      test_reversible_deque_concat_is_orientation_aware;
    Alcotest.test_case "reversible deque cursor edits are tree splices" `Quick
      test_reversible_deque_cursor_edits_are_tree_splices;
    Alcotest.test_case "randomized edits match a list model" `Quick
      test_randomized_edits_match_a_list_model;
  ]
