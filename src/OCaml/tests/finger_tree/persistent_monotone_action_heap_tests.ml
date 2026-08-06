(** Tests for the persistent Brodal-Okasaki heap with pending monotone priority actions.

    The load-bearing cases are the three places where a wrong answer is silent. Composition
    direction: the newer action is the outer one at every pushdown site, and the clamp algebra is
    chosen here precisely because it is not commutative - a cap composed after a floor collapses to
    the cap's boundary, the reverse order to the floor's, so a reversed [compose] changes the number
    a drain reports rather than crashing. Temporal exposure: the old root is materialized before a
    losing insert or meld child is attached, so a heap transformed yesterday must not transform an
    entry inserted today. Tie-breaking: an insert root tie goes to the new entry and a forest
    minimum tie to the earliest spine occurrence, which together pin one exact drain order for a
    four-entry heap. *)

open Durable7
open Finger_tree
open Persistent_monotone_action_heap

let ok label = function
  | Ok value -> value
  | Error message -> Alcotest.failf "%s: unexpected error %S" label message

let expect_error label = function
  | Error _ -> ()
  | Ok _ -> Alcotest.failf "%s: expected an error" label

let int_order : int Common.Comparator.t = Common.Comparator.default ()

(** An ordering that deliberately ties distinct priorities, so an exact constant and an inclusive
    two-point clamp become observably different actions. *)
let pair_order : (int * string) Common.Comparator.t =
  Common.Comparator.create (fun (left, _) (right, _) -> Int.compare left right)

let int_policy () = ok "int clamp policy" (clamp_policy ~id:"clamp/int" int_order)
let pair_policy () = ok "pair clamp policy" (clamp_policy ~id:"clamp/pair" pair_order)

(** A second, deliberately non-commutative algebra: [(scale, offset)] acting as
    [p -> scale * p + offset], monotone for the natural order while every scale stays positive. *)
let affine_policy () =
  ok "affine policy"
    (create_policy ~id:"affine/int" ~identity:(1, 0)
       ~is_identity:(fun (scale, offset) -> scale = 1 && offset = 0)
       ~compose:(fun (outer_scale, outer_offset) (inner_scale, inner_offset) ->
         (outer_scale * inner_scale, (outer_scale * inner_offset) + outer_offset))
       ~apply:(fun (scale, offset) priority -> (scale * priority) + offset)
       ~comparator:int_order ~laws_verified:true ())

let priorities heap =
  List.sort Int.compare (List.map (fun entry -> entry.priority) (to_list heap))

(** Repeated minimum removal, auditing every intermediate version. *)
let drain label heap =
  let rec loop collected heap =
    ignore (ok (label ^ ": audit") (validate heap) : statistics);
    match minimum_view heap with
    | None -> List.rev collected
    | Some (entry, remainder) ->
        Alcotest.(check int)
          (label ^ ": remainder shrinks by one")
          (count heap - 1) (count remainder);
        loop ((entry.element, entry.priority) :: collected) remainder
  in
  loop [] heap

let drained_priorities label heap = List.map snd (drain label heap)

(* ----------------------------------------------------------------------------------------- *)

let test_composition_direction () =
  let policy = int_policy () in
  let samples = [ -20; 0; 4; 5; 7; 10; 11; 100 ] in
  let check_pointwise label expected action =
    List.iter
      (fun sample ->
        Alcotest.(check int)
          (Printf.sprintf "%s at %d" label sample)
          (expected sample)
          (apply_action policy action sample))
      samples
  in
  (* Outer applied after inner: a cap composed after a floor collapses to the cap's boundary. *)
  let cap_after_floor = compose_actions policy (clamp_at_most 5) (clamp_at_least 10) in
  Alcotest.(check bool) "cap after floor is constant" true (clamp_is_constant cap_after_floor);
  Alcotest.(check (option int))
    "cap after floor keeps the outer boundary" (Some 5)
    (clamp_constant_value cap_after_floor);
  check_pointwise "cap after floor" (fun value -> Int.min (Int.max value 10) 5) cap_after_floor;
  let floor_after_cap = compose_actions policy (clamp_at_least 10) (clamp_at_most 5) in
  Alcotest.(check (option int))
    "floor after cap keeps the other outer boundary" (Some 10)
    (clamp_constant_value floor_after_cap);
  check_pointwise "floor after cap" (fun value -> Int.max (Int.min value 5) 10) floor_after_cap;
  Alcotest.(check bool)
    "the two composition orders disagree" true
    (apply_action policy cap_after_floor 0 <> apply_action policy floor_after_cap 0);
  (* The same asymmetry in the affine algebra, where neither operand is a bound. *)
  let affine = affine_policy () in
  Alcotest.(check int)
    "affine outer applied after inner" 11
    (apply_action affine (compose_actions affine (1, 5) (2, 0)) 3);
  Alcotest.(check int)
    "affine reversed" 16
    (apply_action affine (compose_actions affine (2, 0) (1, 5)) 3)

let test_clamp_collapse_cases () =
  let policy = int_policy () in
  (* Identity short-circuits on either side. *)
  let outer_identity = compose_actions policy clamp_identity (clamp_at_least 3) in
  Alcotest.(check (option int)) "identity outer keeps the inner" (Some 3)
    (clamp_lower_bound outer_identity);
  Alcotest.(check bool) "identity outer stays a band" false (clamp_is_constant outer_identity);
  let inner_identity = compose_actions policy (clamp_at_most 3) clamp_identity in
  Alcotest.(check (option int)) "identity inner keeps the outer" (Some 3)
    (clamp_upper_bound inner_identity);
  Alcotest.(check bool)
    "identity composed with identity stays the identity" true
    (policy_is_identity policy (compose_actions policy clamp_identity clamp_identity));
  (* An outer constant wins outright; an inner constant has the outer action applied to it. *)
  Alcotest.(check (option int))
    "outer constant wins" (Some 7)
    (clamp_constant_value (compose_actions policy (clamp_constant 7) (clamp_at_least 100)));
  Alcotest.(check (option int))
    "inner constant is capped by the outer" (Some 5)
    (clamp_constant_value (compose_actions policy (clamp_at_most 5) (clamp_constant 9)));
  Alcotest.(check (option int))
    "inner constant is floored by the outer" (Some 10)
    (clamp_constant_value (compose_actions policy (clamp_at_least 10) (clamp_constant 2)));
  Alcotest.(check (option int))
    "inner constant inside the outer band survives" (Some 9)
    (clamp_constant_value (compose_actions policy (clamp_at_most 50) (clamp_constant 9)));
  (* Disjoint bounds collapse to a constant carrying the outer, newer, boundary. *)
  let low_band = ok "low band" (clamp_between policy ~lower:0 ~upper:5) in
  let high_band = ok "high band" (clamp_between policy ~lower:10 ~upper:20) in
  Alcotest.(check (option int))
    "disjoint above collapses to the outer lower bound" (Some 10)
    (clamp_constant_value (compose_actions policy high_band low_band));
  Alcotest.(check (option int))
    "disjoint below collapses to the outer upper bound" (Some 5)
    (clamp_constant_value (compose_actions policy low_band high_band));
  (* Overlapping bounds intersect. *)
  let wide_band = ok "wide band" (clamp_between policy ~lower:3 ~upper:14) in
  let overlap = compose_actions policy high_band wide_band in
  Alcotest.(check bool) "an overlap stays a band" false (clamp_is_constant overlap);
  Alcotest.(check (option int)) "the intersection lower bound" (Some 10)
    (clamp_lower_bound overlap);
  Alcotest.(check (option int)) "the intersection upper bound" (Some 14)
    (clamp_upper_bound overlap);
  Alcotest.(check int) "the intersection applies" 10 (apply_action policy overlap 0);
  Alcotest.(check int) "the intersection passes through" 12 (apply_action policy overlap 12);
  Alcotest.(check int) "the intersection caps" 14 (apply_action policy overlap 99)

let test_clamp_representative_choices () =
  let policy = pair_policy () in
  let inner = ok "inner band" (clamp_between policy ~lower:(0, "inner") ~upper:(5, "inner")) in
  let outer = ok "outer band" (clamp_between policy ~lower:(0, "outer") ~upper:(5, "outer")) in
  let composed = compose_actions policy outer inner in
  (* Equal boundaries keep the inner, older, representative. *)
  Alcotest.(check (option (pair int string)))
    "equal lower boundaries keep the older representative"
    (Some (0, "inner"))
    (clamp_lower_bound composed);
  Alcotest.(check (option (pair int string)))
    "equal upper boundaries keep the older representative"
    (Some (5, "inner"))
    (clamp_upper_bound composed);
  (* Application tests the constant first: an inclusive two-point band leaves a tied priority alone,
     while an exact constant hands back its own representative. *)
  let band = ok "point band" (clamp_between policy ~lower:(5, "low") ~upper:(5, "high")) in
  Alcotest.(check (pair int string))
    "a band preserves a tied priority" (5, "own")
    (apply_action policy band (5, "own"));
  Alcotest.(check (pair int string))
    "a constant replaces a tied priority" (5, "chosen")
    (apply_action policy (clamp_constant (5, "chosen")) (5, "own"));
  Alcotest.(check (pair int string))
    "the lower bound is applied below the band" (5, "low")
    (apply_action policy band (2, "own"));
  Alcotest.(check (pair int string))
    "the upper bound is applied above the band" (5, "high")
    (apply_action policy band (9, "own"));
  expect_error "inverted bounds" (clamp_between policy ~lower:(5, "a") ~upper:(3, "b"));
  let equal_bounds = ok "equal bounds" (clamp_between policy ~lower:(5, "a") ~upper:(5, "b")) in
  Alcotest.(check (option (pair int string)))
    "equal bounds are accepted"
    (Some (5, "a"))
    (clamp_lower_bound equal_bounds)

let test_transform_is_not_retroactive () =
  let policy = int_policy () in
  let heap = of_entries policy [ ("a", 1); ("b", 2); ("c", 3) ] in
  let pinned = transform_all (clamp_constant 100) heap in
  Alcotest.(check (list int)) "every current priority moved" [ 100; 100; 100 ] (priorities pinned);
  Alcotest.(check (list int)) "the source version is retained" [ 1; 2; 3 ] (priorities heap);
  (* A later, larger insertion travels the exposing branch and must arrive untransformed. *)
  let larger = insert "d" 200 pinned in
  Alcotest.(check (list int))
    "a later large insertion is untouched" [ 100; 100; 100; 200 ]
    (drained_priorities "larger" larger);
  Alcotest.(check int) "the later entry keeps its own priority" 200
    (List.assoc "d" (drain "larger entries" larger));
  (* A later, smaller insertion travels the winning branch and must also arrive untransformed. *)
  let smaller = insert "e" 50 pinned in
  Alcotest.(check (option int))
    "a later small insertion becomes the minimum" (Some 50)
    (Option.map (fun entry -> entry.priority) (minimum smaller));
  Alcotest.(check (list int))
    "the earlier entries keep the transform" [ 50; 100; 100; 100 ]
    (drained_priorities "smaller" smaller);
  (* A later meld is not retroactively transformed either, in either operand position. *)
  let fresh = of_entries policy [ ("f", 20); ("g", 30) ] in
  Alcotest.(check (list int))
    "a later meld keeps both histories" [ 20; 30; 100; 100; 100 ]
    (drained_priorities "melded" (ok "meld" (meld pinned fresh)));
  Alcotest.(check (list int))
    "meld is symmetric in its histories" [ 20; 30; 100; 100; 100 ]
    (drained_priorities "melded flipped" (ok "meld flipped" (meld fresh pinned)))

let test_pushdown_composition_direction () =
  (* A tagged tree that reaches a child position: melding a transformed heap under an untransformed
     root leaves a non-identity tree tag one level down, so the next transform must compose over it
     with the newer action outermost. *)
  let policy = int_policy () in
  let capped = transform_all (clamp_at_most 20) (of_entries policy [ ("b", 100) ]) in
  Alcotest.(check (list int)) "the capped operand" [ 20 ] (priorities capped);
  let melded = ok "meld" (meld (of_entries policy [ ("a", 0) ]) capped) in
  Alcotest.(check (list int)) "the meld keeps both histories" [ 0; 20 ] (priorities melded);
  let floored = transform_all (clamp_at_least 50) melded in
  Alcotest.(check (list int))
    "the newer floor is applied after the older cap" [ 50; 50 ]
    (drained_priorities "floored" floored);
  (* The same site under the affine algebra, where every composition order is observable. *)
  let affine = affine_policy () in
  let heap = of_entries affine [ ("a", 1); ("b", 2); ("c", 3); ("d", 4) ] in
  let doubled = transform_all (2, 0) heap in
  Alcotest.(check (list int)) "doubling" [ 2; 4; 6; 8 ] (priorities doubled);
  let shortened = Option.get (delete_minimum doubled) in
  Alcotest.(check (list int)) "after one removal" [ 4; 6; 8 ] (priorities shortened);
  let shifted = transform_all (1, 5) shortened in
  Alcotest.(check (list int))
    "the newer shift is applied after the older doubling" [ 9; 11; 13 ]
    (drained_priorities "shifted" shifted)

let test_tie_breaks () =
  let policy = int_policy () in
  let heap = of_entries policy [ ("a", 1); ("b", 1); ("c", 2); ("d", 2) ] in
  Alcotest.(check (option string))
    "an insert root tie favours the new entry" (Some "b")
    (Option.map (fun entry -> entry.element) (minimum heap));
  Alcotest.(check (list (pair string int)))
    "the drain order pins both tie-breaks"
    [ ("b", 1); ("a", 1); ("d", 2); ("c", 2) ]
    (drain "ties" heap)

let test_self_meld_duplicates () =
  let policy = int_policy () in
  let heap = of_entries policy [ ("a", 3); ("b", 1); ("c", 2) ] in
  let doubled = ok "self meld" (meld heap heap) in
  Alcotest.(check int) "self meld doubles the count" 6 (count doubled);
  Alcotest.(check (list int)) "every occurrence is duplicated" [ 1; 1; 2; 2; 3; 3 ]
    (drained_priorities "doubled" doubled);
  Alcotest.(check (list string))
    "every payload is duplicated" [ "a"; "a"; "b"; "b"; "c"; "c" ]
    (List.sort String.compare (List.map (fun entry -> entry.element) (to_list doubled)));
  Alcotest.(check int) "the source is retained" 3 (count heap);
  let quadrupled = ok "second self meld" (meld doubled doubled) in
  Alcotest.(check (list int))
    "self meld composes" [ 1; 1; 1; 1; 2; 2; 2; 2; 3; 3; 3; 3 ]
    (drained_priorities "quadrupled" quadrupled)

let test_policy_identity_gates_melding () =
  let heap = of_entries (int_policy ()) [ ("x", 1) ] in
  (* A distinct policy value carrying the same declared identity is interchangeable. *)
  let sibling = of_entries (int_policy ()) [ ("y", 2) ] in
  Alcotest.(check int) "same declared identity melds" 2 (count (ok "meld" (meld heap sibling)));
  let stranger =
    of_entries (ok "other policy" (clamp_policy ~id:"clamp/other" int_order)) [ ("z", 3) ]
  in
  expect_error "different declared identity" (meld heap stranger);
  expect_error "different declared identity, flipped" (meld stranger heap);
  Alcotest.(check string) "the retained identity is readable" "clamp/int"
    (policy_id (policy heap));
  Alcotest.(check bool) "the retained comparator is the policy's" true
    (Common.Comparator.same (comparator heap) (policy_comparator (policy heap)));
  (* Empty operands reshare rather than rebuild. *)
  let blank = empty (int_policy ()) in
  Alcotest.(check bool) "an empty left operand reshares" true
    (shares_root_with (ok "meld blank left" (meld blank heap)) heap);
  Alcotest.(check bool) "an empty right operand reshares" true
    (shares_root_with (ok "meld blank right" (meld heap blank)) heap);
  expect_error "a policy needs a declared identity" (clamp_policy ~id:"  " int_order);
  expect_error "a policy needs verified laws"
    (create_policy ~id:"unverified" ~identity:0
       ~is_identity:(fun action -> action = 0)
       ~compose:( + )
       ~apply:( + )
       ~comparator:int_order ~laws_verified:false ())

let test_empty_heap () =
  let policy = int_policy () in
  let heap = empty policy in
  Alcotest.(check bool) "an empty heap reports empty" true (is_empty heap);
  Alcotest.(check int) "an empty heap has no entries" 0 (count heap);
  Alcotest.(check bool) "no minimum" true (Option.is_none (minimum heap));
  Alcotest.(check bool) "no deletion" true (Option.is_none (delete_minimum heap));
  Alcotest.(check bool) "no minimum view" true (Option.is_none (minimum_view heap));
  Alcotest.(check (list int)) "no entries to list" [] (priorities heap);
  let stats = ok "empty audit" (validate heap) in
  Alcotest.(check int) "an empty audit counts nothing" 0 stats.count;
  Alcotest.(check int) "an empty audit has no forest" 0 stats.root_forest_length;
  (* [shares_root_with] is unconditionally true for two empty heaps, so asserting it here could not
     fail. Physical identity of the whole value is the discriminating check: a [transform_all] that
     rebuilt the record instead of returning its receiver would fail this and pass the root test. *)
  Alcotest.(check bool) "transforming an empty heap returns the receiver" true
    (transform_all (clamp_at_most 3) heap == heap);
  (* The retained policy keeps an empty result usable. *)
  Alcotest.(check int) "an empty heap still accepts insertions" 1 (count (insert "a" 1 heap));
  let drained = Option.get (delete_minimum (of_entries policy [ ("a", 1) ])) in
  Alcotest.(check bool) "a drained heap reports empty" true (is_empty drained);
  Alcotest.(check (list int)) "a drained heap is reusable" [ 4; 9 ]
    (priorities (insert "c" 4 (insert "b" 9 drained)))

let test_transform_allocates_constant_structure () =
  let policy = int_policy () in
  let heap = of_entries policy [ ("a", 1); ("b", 2); ("c", 3); ("d", 4) ] in
  let capped = transform_all (clamp_at_most 3) heap in
  Alcotest.(check bool) "a transform rebuilds only the root" false (shares_root_with heap capped);
  Alcotest.(check bool) "a transform reshares the whole child forest" true
    (shares_root_children_with heap capped);
  Alcotest.(check bool) "an identity transform reshares the root" true
    (shares_root_with heap (transform_all clamp_identity heap))

let test_audit_reports_shape () =
  let policy = int_policy () in
  let heap = of_entries policy [ ("a", 1); ("b", 2); ("c", 3); ("d", 4) ] in
  let stats = ok "audit" (validate heap) in
  Alcotest.(check int) "audited count" 4 stats.count;
  Alcotest.(check int) "root forest length" 1 stats.root_forest_length;
  Alcotest.(check int) "maximum rank" 1 stats.maximum_rank;
  Alcotest.(check int) "maximum depth" 3 stats.maximum_depth;
  Alcotest.(check int) "no pending tags yet" 0 stats.tagged_component_count;
  let capped = transform_all (clamp_at_most 3) heap in
  let tagged = ok "tagged audit" (validate capped) in
  Alcotest.(check int) "the shape is unchanged by a transform" 4 tagged.count;
  Alcotest.(check int) "the rank is unchanged by a transform" 1 tagged.maximum_rank;
  Alcotest.(check int) "every component on the walk is tagged" 7 tagged.tagged_component_count

let test_randomized_model () =
  let policy = int_policy () in
  let seed = ref 0x1234ABCD in
  let next bound =
    seed := ((!seed * 25214903917) + 11) land 0xFFFFFFFFFFFF;
    (!seed lsr 17) mod bound
  in
  let random_action () =
    match next 5 with
    | 0 -> clamp_at_least (next 200)
    | 1 -> clamp_at_most (next 200)
    | 2 -> clamp_constant (next 200)
    | 3 ->
        let lower = next 200 in
        let upper = lower + next 50 in
        ok "random band" (clamp_between policy ~lower ~upper)
    | _ -> clamp_identity
  in
  let rec remove_first (value : int) values =
    match values with
    | [] -> []
    | head :: rest -> if head = value then rest else head :: remove_first value rest
  in
  let heap = ref (empty policy) in
  let model = ref [] in
  let largest = ref 0 in
  let deepest = ref 0 in
  let audit label =
    let stats = ok (label ^ ": audit") (validate !heap) in
    largest := Int.max !largest stats.count;
    deepest := Int.max !deepest stats.maximum_rank;
    Alcotest.(check int) (label ^ ": count") (List.length !model) (count !heap);
    Alcotest.(check int) (label ^ ": audited count") (List.length !model) stats.count;
    Alcotest.(check (list int))
      (label ^ ": priorities")
      (List.sort Int.compare !model)
      (priorities !heap);
    match !model with
    | [] -> Alcotest.(check bool) (label ^ ": empty") true (is_empty !heap)
    | first :: rest ->
        Alcotest.(check (option int))
          (label ^ ": minimum")
          (Some (List.fold_left Int.min first rest))
          (Option.map (fun entry -> entry.priority) (minimum !heap))
  in
  for step = 1 to 500 do
    (match next 10 with
    | 0 | 1 | 2 | 3 | 4 ->
        let priority = next 200 in
        heap := insert step priority !heap;
        model := priority :: !model
    | 5 | 6 | 7 -> (
        match (delete_minimum !heap, !model) with
        | None, _ ->
            Alcotest.(check int) "an empty deletion needs an empty model" 0 (List.length !model)
        | Some _, [] -> Alcotest.failf "step %d: deleted from an empty model" step
        | Some remainder, first :: rest ->
            heap := remainder;
            model := remove_first (List.fold_left Int.min first rest) !model)
    | 8 ->
        let action = random_action () in
        heap := transform_all action !heap;
        model := List.map (apply_action policy action) !model
    | _ ->
        let other = ref (empty policy) in
        let added = ref [] in
        for index = 1 to next 4 do
          let priority = next 200 in
          other := insert (1000 + (step * 4) + index) priority !other;
          added := priority :: !added
        done;
        let transformed =
          if next 2 = 0 then !other
          else
            let action = random_action () in
            added := List.map (apply_action policy action) !added;
            transform_all action !other
        in
        heap := ok "random meld" (meld !heap transformed);
        model := !added @ !model);
    audit (Printf.sprintf "step %d" step)
  done;
  (* Without a heap large enough to grow ranked trees, the split-forest decomposition never runs its
     interesting branches and the run would prove very little. *)
  Alcotest.(check bool)
    (Printf.sprintf "the run reached %d entries" !largest)
    true (!largest >= 60);
  Alcotest.(check bool)
    (Printf.sprintf "the run reached rank %d" !deepest)
    true (!deepest >= 4)

let tests =
  [
    Alcotest.test_case "composition direction" `Quick test_composition_direction;
    Alcotest.test_case "clamp collapse cases" `Quick test_clamp_collapse_cases;
    Alcotest.test_case "clamp representative choices" `Quick test_clamp_representative_choices;
    Alcotest.test_case "transforms are not retroactive" `Quick test_transform_is_not_retroactive;
    Alcotest.test_case "pushdown composition direction" `Quick test_pushdown_composition_direction;
    Alcotest.test_case "tie breaks" `Quick test_tie_breaks;
    Alcotest.test_case "self meld duplicates" `Quick test_self_meld_duplicates;
    Alcotest.test_case "policy identity gates melding" `Quick test_policy_identity_gates_melding;
    Alcotest.test_case "empty heap" `Quick test_empty_heap;
    Alcotest.test_case "transforms allocate constant structure" `Quick
      test_transform_allocates_constant_structure;
    Alcotest.test_case "audit reports shape" `Quick test_audit_reports_shape;
    Alcotest.test_case "randomized model" `Quick test_randomized_model;
  ]
