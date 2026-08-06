(** Structural probes for the real relaxed radix-balanced vector.

    [Rrb_vector.statistics] is now a genuine structural audit: it recounts every leaf, checks node
    widths, sibling heights, cached counts, and that a size table exists exactly when a branch's
    layout is irregular, raising on any violation. These probes lean on that audit plus exact leaf
    and depth counts, because the properties that distinguish a real RRB from the deque facade it
    replaced — arithmetic radix addressing on packed branches, seam-only concatenation, singleton
    endpoint pushes that merge into the boundary leaf — are shape properties invisible to plain
    contents checks. *)

open Durable7
open Finger_tree

let check_int_list label expected actual = Alcotest.(check (list int)) label expected actual
let audited vector = Rrb_vector.statistics vector

let test_bulk_build_is_fully_radix_packed () =
  (* An eager bulk build must produce the exact radix shape: all-full leaves and the minimal
     height, so every branch is regular and addresses arithmetically. The pinned numbers are the
     packed layout — any fragmentation or estimate would miss them. *)
  let vector = Rrb_vector.of_list (List.init 32_768 Fun.id) in
  let statistics = audited vector in
  Alcotest.(check int) "count" 32_768 statistics.Rrb_vector.count;
  Alcotest.(check int) "exactly 1024 all-full leaves" 1_024 statistics.Rrb_vector.estimated_leaves;
  Alcotest.(check int) "minimal radix height" 3 statistics.Rrb_vector.depth;
  let shallow = audited (Rrb_vector.of_list (List.init 1_024 Fun.id)) in
  Alcotest.(check int) "1024 elements pack into 32 leaves" 32 shallow.Rrb_vector.estimated_leaves;
  Alcotest.(check int) "under one root branch" 2 shallow.Rrb_vector.depth;
  Alcotest.(check (option int)) "first" (Some 0) (Rrb_vector.nth 0 vector);
  Alcotest.(check (option int)) "radix interior" (Some 17_291) (Rrb_vector.nth 17_291 vector);
  Alcotest.(check (option int)) "last" (Some 32_767) (Rrb_vector.nth 32_767 vector);
  Alcotest.(check (option int)) "past the end" None (Rrb_vector.nth 32_768 vector)

let test_endpoint_pushes_merge_into_the_boundary_leaf () =
  (* Endpoint push is a singleton concatenation, and the seam merge must keep leaves packed: a
     concat that stacked singleton leaves would leave one leaf per push (544 leaves below), while
     the seam merge's half-split floor keeps every leaf at 16 or more elements. *)
  let appended = ref (Rrb_vector.of_list (List.init 1_024 Fun.id)) in
  for value = 1_024 to 1_535 do
    appended := Rrb_vector.append value !appended
  done;
  let shape = audited !appended in
  Alcotest.(check int) "count after 512 appends" 1_536 shape.Rrb_vector.count;
  Alcotest.(check bool)
    (Printf.sprintf "512 appends left %d leaves, within the packed ceiling 96"
       shape.Rrb_vector.estimated_leaves)
    true
    (shape.Rrb_vector.estimated_leaves <= 96);
  Alcotest.(check bool)
    (Printf.sprintf "and depth %d within 4" shape.Rrb_vector.depth)
    true
    (shape.Rrb_vector.depth <= 4);
  check_int_list "append order" (List.init 1_536 Fun.id) (Rrb_vector.to_list !appended);
  let prepended = ref (Rrb_vector.of_list (List.init 1_024 (fun index -> 512 + index))) in
  for value = 511 downto 0 do
    prepended := Rrb_vector.prepend value !prepended
  done;
  let mirrored = audited !prepended in
  Alcotest.(check bool)
    (Printf.sprintf "512 prepends left %d leaves, within the packed ceiling 96"
       mirrored.Rrb_vector.estimated_leaves)
    true
    (mirrored.Rrb_vector.estimated_leaves <= 96);
  check_int_list "prepend order" (List.init 1_536 Fun.id) (Rrb_vector.to_list !prepended)

let test_concat_rebalances_only_the_seam () =
  (* Joining two radix-packed vectors must touch only the seam: with both boundary leaves full,
     every leaf survives — 128 + 3 exactly — and the height grows by at most one. A global
     rebalance or a rebuild would change the leaf census; a naive stack would deepen the tree. *)
  let big = Rrb_vector.of_list (List.init 4_096 Fun.id) in
  let small = Rrb_vector.of_list (List.init 96 (fun index -> 4_096 + index)) in
  let joined = Rrb_vector.concat big small in
  let shape = audited joined in
  Alcotest.(check int) "joined count" 4_192 shape.Rrb_vector.count;
  Alcotest.(check int) "every packed leaf survived the seam" 131
    shape.Rrb_vector.estimated_leaves;
  Alcotest.(check bool)
    (Printf.sprintf "joined depth %d within one of the taller operand" shape.Rrb_vector.depth)
    true
    (shape.Rrb_vector.depth <= 1 + (audited big).Rrb_vector.depth);
  Alcotest.(check (option int)) "across the seam" (Some 4_096) (Rrb_vector.nth 4_096 joined);
  let mirrored = Rrb_vector.concat small big in
  Alcotest.(check int) "the mirrored join keeps the census too" 131
    (audited mirrored).Rrb_vector.estimated_leaves;
  (* Round trip: splitting at the seam gives both operands' contents back, and both halves audit. *)
  let left, right = Rrb_vector.split_at 4_096 joined in
  Alcotest.(check int) "left half count" 4_096 (audited left).Rrb_vector.count;
  Alcotest.(check int) "right half count" 96 (audited right).Rrb_vector.count;
  check_int_list "right half order" (Rrb_vector.to_list small) (Rrb_vector.to_list right)

let test_random_split_concat_churn_stays_balanced () =
  (* Repeated rotations by split-and-concat are the workload that punishes a wrong rebalance:
     every iteration splits at a random gap and swaps the halves. The audit runs throughout — it
     raises on any stale size table or regularity mismatch — and the depth must stay bounded while
     the contents track the model exactly. *)
  let generator = Random.State.make [| 20260806 |] in
  let size = 4_096 in
  let vector = ref (Rrb_vector.of_list (List.init size Fun.id)) in
  let model = ref (List.init size Fun.id) in
  let deepest = ref 0 in
  for _ = 1 to 300 do
    let gap = Random.State.int generator (size + 1) in
    let left, right = Rrb_vector.split_at gap !vector in
    vector := Rrb_vector.concat right left;
    model :=
      List.filteri (fun index _ -> index >= gap) !model
      @ List.filteri (fun index _ -> index < gap) !model;
    let shape = audited !vector in
    Alcotest.(check int) "rotation preserves the count" size shape.Rrb_vector.count;
    deepest := max !deepest shape.Rrb_vector.depth
  done;
  Alcotest.(check bool)
    (Printf.sprintf "300 rotations peaked at depth %d, within 8" !deepest)
    true (!deepest <= 8);
  check_int_list "the survivor matches the model" !model (Rrb_vector.to_list !vector);
  (* Point edits on the churned, size-table-bearing tree still address correctly. *)
  let updated = Result.get_ok (Rrb_vector.set 2_048 (-1) !vector) in
  Alcotest.(check (option int)) "relaxed set" (Some (-1)) (Rrb_vector.nth 2_048 updated);
  Alcotest.(check (option int))
    "the churned source is untouched"
    (Some (List.nth !model 2_048))
    (Rrb_vector.nth 2_048 !vector)

let test_middle_splices_share_the_seam_shape () =
  (* An interior insert or removal is two splits and a join; the census must move by exactly the
     edit and the audit must stay clean, on top of the model agreement. *)
  let vector = Rrb_vector.of_list (List.init 2_048 Fun.id) in
  let inserted = Result.get_ok (Rrb_vector.insert 1_000 (-1) vector) in
  Alcotest.(check int) "insert count" 2_049 (audited inserted).Rrb_vector.count;
  Alcotest.(check (option int)) "inserted element" (Some (-1)) (Rrb_vector.nth 1_000 inserted);
  Alcotest.(check (option int)) "shifted successor" (Some 1_000) (Rrb_vector.nth 1_001 inserted);
  let removed, remainder = Result.get_ok (Rrb_vector.remove 1_000 inserted) in
  Alcotest.(check int) "removed element" (-1) removed;
  Alcotest.(check int) "removal count" 2_048 (audited remainder).Rrb_vector.count;
  check_int_list "the round trip restores the model" (Rrb_vector.to_list vector)
    (Rrb_vector.to_list remainder)

let tests =
  [
    Alcotest.test_case "bulk build is fully radix packed" `Quick
      test_bulk_build_is_fully_radix_packed;
    Alcotest.test_case "endpoint pushes merge into the boundary leaf" `Quick
      test_endpoint_pushes_merge_into_the_boundary_leaf;
    Alcotest.test_case "concat rebalances only the seam" `Quick test_concat_rebalances_only_the_seam;
    Alcotest.test_case "random split-concat churn stays balanced" `Quick
      test_random_split_concat_churn_stays_balanced;
    Alcotest.test_case "middle splices share the seam shape" `Quick
      test_middle_splices_share_the_seam_shape;
  ]
