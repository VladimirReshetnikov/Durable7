(** Tests for the branching persistent connectivity forest and its first-connection witness.

    The load-bearing cases are the ones no simpler structure would pass: a witness must come from
    the pair's own forest lowest common ancestor rather than from whenever their component was last
    merged into a bigger one, sibling branches at equal depth must stay distinct version identities,
    and a history hundreds of thousands of links deep must be walkable without a stack. *)

open Durable7
open Hamt
open Persistent_ancestral_connection_forest

let ok label = function
  | Ok value -> value
  | Error message -> Alcotest.failf "%s: unexpected error %S" label message

let expect_error label = function
  | Error _ -> ()
  | Ok _ -> Alcotest.failf "%s: expected an error" label

(** Tokens are compared by identity, never structurally: a token records only a depth, a parent, and
    a root, so equal-depth siblings would compare equal, and the history root refers to itself, so a
    structural comparison would not terminate. *)
let version_token =
  Alcotest.testable
    (fun formatter token -> Format.fprintf formatter "version@%d" (version_depth token))
    same_version

let check_witness label expected actual =
  Alcotest.(check (option version_token)) label expected actual

let forest vertex_count = ok "create" (create vertex_count)
let joined left right tree = ok "link" (link left right tree)
let witness left right tree = ok "first connected" (first_connected left right tree)

let test_root_version () =
  let root = forest 5 in
  Alcotest.(check int) "vertex count" 5 (vertex_count root);
  Alcotest.(check int) "component count" 5 (component_count root);
  Alcotest.(check int) "history root depth" 0 (version_depth (version root));
  Alcotest.(check bool) "history root has no parent" true
    (Option.is_none (version_parent (version root)));
  Alcotest.(check bool) "history root is its own root" true
    (same_version (version root) (version_root (version root)));
  for vertex = 0 to vertex_count root - 1 do
    Alcotest.(check int) "singleton representative" vertex (ok "find" (find vertex root));
    Alcotest.(check bool) "self connected" true (ok "connected" (connected vertex vertex root));
    Alcotest.(check int) "singleton size" 1 (ok "component size" (component_size vertex root));
    check_witness "self witness is the history root" (Some (version root))
      (witness vertex vertex root)
  done;
  Alcotest.(check bool) "distinct singletons are disconnected" false
    (ok "connected" (connected 0 1 root));
  check_witness "a disconnected pair yields no witness" None (witness 0 1 root);
  let audit = ok "validate" (validate root) in
  Alcotest.(check int) "no cells stored" 0 audit.stored_cell_count;
  Alcotest.(check int) "audited components" 5 audit.component_count;
  Alcotest.(check int) "audited vertices" 5 audit.vertex_count;
  Alcotest.(check int) "flat forest" 0 audit.maximum_parent_path_length;
  Alcotest.(check int) "history depth" 0 audit.history_depth;
  let empty = forest 0 in
  Alcotest.(check int) "empty universe" 0 (vertex_count empty);
  Alcotest.(check int) "empty universe components" 0 (component_count empty);
  ignore (ok "validate empty" (validate empty) : statistics);
  expect_error "negative universe" (create (-1))

let test_link_records_the_first_connection () =
  let root = forest 6 in
  let first = joined 0 1 root in
  let second = joined 2 3 first in
  let bridge = joined 0 2 second in
  let attach = joined 4 0 bridge in
  Alcotest.(check int) "components after four unions" 2 (component_count attach);
  check_witness "pair born first" (Some (version first)) (witness 0 1 attach);
  check_witness "second pair" (Some (version second)) (witness 2 3 attach);
  check_witness "across the bridge" (Some (version bridge)) (witness 0 3 attach);
  check_witness "across the bridge, other endpoints" (Some (version bridge)) (witness 1 2 attach);
  check_witness "latest attachment" (Some (version attach)) (witness 4 1 attach);
  check_witness "self witness stays the history root" (Some (version root)) (witness 4 4 attach);
  check_witness "still disconnected" None (witness 0 5 attach);
  Alcotest.(check int) "representative of the first endpoint" 0 (ok "find" (find 0 attach));
  Alcotest.(check int) "representative of the attached vertex" 0 (ok "find" (find 4 attach));
  (* Every retained version keeps its own connectivity, untouched by later descendants. *)
  Alcotest.(check int) "root retained" 6 (component_count root);
  Alcotest.(check bool) "root still edgeless" false (ok "connected" (connected 0 1 root));
  Alcotest.(check bool) "first version has no bridge" false (ok "connected" (connected 0 2 first));
  Alcotest.(check int) "first version components" 5 (component_count first);
  List.iter
    (fun tree -> ignore (ok "validate" (validate tree) : statistics))
    [ root; first; second; bridge; attach ]

let test_redundant_links_share_connectivity () =
  let root = forest 4 in
  let linked = joined 0 1 root in
  let reverse_duplicate = joined 1 0 linked in
  let sibling_duplicate = joined 1 0 linked in
  let self_duplicate = joined 0 0 reverse_duplicate in
  Alcotest.(check int) "first union depth" 1 (version_depth (version linked));
  Alcotest.(check int) "redundant link still advances depth" 2
    (version_depth (version reverse_duplicate));
  Alcotest.(check int) "self link still advances depth" 3 (version_depth (version self_duplicate));
  check_witness "redundant child's parent" (Some (version linked))
    (version_parent (version reverse_duplicate));
  check_witness "sibling redundant child's parent" (Some (version linked))
    (version_parent (version sibling_duplicate));
  check_witness "self link's parent" (Some (version reverse_duplicate))
    (version_parent (version self_duplicate));
  Alcotest.(check bool) "a link always produces a distinct version" false
    (same_version (version linked) (version reverse_duplicate));
  Alcotest.(check bool) "two redundant siblings stay distinct" false
    (same_version (version reverse_duplicate) (version sibling_duplicate));
  Alcotest.(check int) "redundant link changes no component count" (component_count linked)
    (component_count reverse_duplicate);
  Alcotest.(check int) "self link changes no component count" (component_count linked)
    (component_count self_duplicate);
  Alcotest.(check bool) "redundant link shares the connectivity index" true
    (shares_connectivity_root_with linked reverse_duplicate);
  Alcotest.(check bool) "self link shares the connectivity index" true
    (shares_connectivity_root_with reverse_duplicate self_duplicate);
  Alcotest.(check bool) "a successful union does not share it" false
    (shares_connectivity_root_with root linked);
  check_witness "witness survives the redundant tail" (Some (version linked))
    (witness 0 1 self_duplicate);
  check_witness "self query still answers the history root" (Some (version root))
    (witness 2 2 self_duplicate);
  ignore (ok "validate" (validate self_duplicate) : statistics)

let test_sibling_branches_stay_distinct () =
  let root = forest 5 in
  let common = joined 0 1 root in
  let left = joined 0 2 common in
  let right = joined 0 3 common in
  let left_later = joined 2 4 left in
  let right_later = joined 3 4 right in
  Alcotest.(check int) "siblings share a depth" (version_depth (version left))
    (version_depth (version right));
  Alcotest.(check bool) "equal depth is not equal identity" false
    (same_version (version left) (version right));
  check_witness "left branch sees the shared past" (Some (version common)) (witness 0 1 left);
  check_witness "right branch sees the shared past" (Some (version common)) (witness 0 1 right);
  check_witness "left branch's own union" (Some (version left)) (witness 1 2 left_later);
  check_witness "right branch's own union" (Some (version right)) (witness 1 3 right_later);
  check_witness "left branch's latest" (Some (version left_later)) (witness 0 4 left_later);
  check_witness "right branch's latest" (Some (version right_later)) (witness 0 4 right_later);
  Alcotest.(check bool) "left branch never saw the right union" false
    (ok "connected" (connected 0 3 left_later));
  Alcotest.(check bool) "right branch never saw the left union" false
    (ok "connected" (connected 0 2 right_later));
  Alcotest.(check bool) "both branches keep one history root" true
    (same_version (version_root (version left_later)) (version root));
  Alcotest.(check bool) "right branch keeps the same history root" true
    (same_version (version_root (version right_later)) (version root));
  ignore (ok "validate" (validate left_later) : statistics);
  ignore (ok "validate" (validate right_later) : statistics)

let test_witness_comes_from_the_pair_lca () =
  let root = forest 8 in
  let ab = joined 0 1 root in
  let cd = joined 2 3 ab in
  let abcd = joined 1 3 cd in
  let ef = joined 4 5 abcd in
  let gh = joined 6 7 ef in
  let efgh = joined 4 6 gh in
  let all = joined 0 4 efgh in
  check_witness "first pair" (Some (version ab)) (witness 0 1 all);
  check_witness "second pair" (Some (version cd)) (witness 2 3 all);
  check_witness "first merge" (Some (version abcd)) (witness 0 2 all);
  check_witness "third pair" (Some (version ef)) (witness 4 5 all);
  check_witness "fourth pair" (Some (version gh)) (witness 6 7 all);
  check_witness "second merge" (Some (version efgh)) (witness 5 7 all);
  check_witness "final merge" (Some (version all)) (witness 1 6 all);
  ignore (ok "validate" (validate all) : statistics)

let test_endpoint_at_the_lca_after_it_becomes_a_child () =
  let root = forest 5 in
  let pair = joined 0 1 root in
  let other_pair = joined 2 3 pair in
  let larger = joined 2 4 other_pair in
  (* The component rooted at 0 becomes a child of 2, so one endpoint of the first pair now sits at
     the forest LCA of a later query. *)
  let merged = joined 2 0 larger in
  Alcotest.(check int) "the smaller root was absorbed" 2 (ok "find" (find 0 merged));
  check_witness "the old pair keeps its own witness" (Some (version pair)) (witness 0 1 merged);
  check_witness "the endpoint at the LCA" (Some (version merged)) (witness 0 2 merged);
  check_witness "across the merge" (Some (version merged)) (witness 1 4 merged);
  ignore (ok "validate" (validate merged) : statistics)

let test_balanced_unions_respect_the_height_bound () =
  let count = 1024 in
  let root = forest count in
  let current = ref root in
  let first_pair = ref None in
  let final_bridge = ref None in
  let width = ref 1 in
  while !width < count do
    let start = ref 0 in
    while !start < count do
      current := joined !start (!start + !width) !current;
      if !start = 0 && !width = 1 then first_pair := Some (version !current);
      if !start = 0 && !width = count / 2 then final_bridge := Some (version !current);
      start := !start + (!width * 2)
    done;
    width := !width * 2
  done;
  let built = !current in
  Alcotest.(check int) "one component" 1 (component_count built);
  Alcotest.(check int) "one representative" 0 (ok "find" (find (count - 1) built));
  check_witness "earliest pair witness retained" !first_pair (witness 0 1 built);
  check_witness "final bridge witness" !final_bridge (witness 0 (count - 1) built);
  Alcotest.(check int) "the history root is untouched" count (component_count root);
  let audit = ok "validate" (validate built) in
  Alcotest.(check int) "every vertex stored" count audit.stored_cell_count;
  Alcotest.(check int) "audited components" 1 audit.component_count;
  Alcotest.(check int) "height is exactly log2 n" 10 audit.maximum_parent_path_length;
  Alcotest.(check int) "one version per union" (count - 1) audit.history_depth

let test_union_by_size_tie_break () =
  let root = forest 16 in
  let first = joined 0 1 root in
  let second = joined 2 3 first in
  Alcotest.(check bool) "a successful union publishes a new index" false
    (shares_connectivity_root_with first second);
  Alcotest.(check bool) "a redundant union does not" true
    (shares_connectivity_root_with second (joined 0 1 second));
  Alcotest.(check bool) "nor does a reversed redundant union" true
    (shares_connectivity_root_with second (joined 3 2 second));
  (* On a size tie the first endpoint's root remains the representative. *)
  Alcotest.(check int) "first pair keeps its first endpoint" 0 (ok "find" (find 1 second));
  Alcotest.(check int) "second pair keeps its first endpoint" 2 (ok "find" (find 3 second));
  let merged = joined 3 1 second in
  Alcotest.(check int) "the tie keeps the first argument's root" 2 (ok "find" (find 0 merged));
  Alcotest.(check int) "one component fewer" 13 (component_count merged);
  let other = joined 1 3 second in
  Alcotest.(check int) "the mirrored tie keeps the mirrored root" 0 (ok "find" (find 2 other));
  ignore (ok "validate" (validate merged) : statistics);
  ignore (ok "validate" (validate other) : statistics)

let test_component_size_reports_the_cached_union_size () =
  let size vertex tree = ok "component size" (component_size vertex tree) in
  (* One cached size per distinct current root must recover the whole universe. *)
  let total tree =
    let roots = Hashtbl.create 16 in
    for vertex = 0 to vertex_count tree - 1 do
      Hashtbl.replace roots (ok "find" (find vertex tree)) (size vertex tree)
    done;
    Alcotest.(check int) "one root per component" (component_count tree) (Hashtbl.length roots);
    Hashtbl.fold (fun _ value sum -> sum + value) roots 0
  in
  let root = forest 6 in
  for vertex = 0 to 5 do
    Alcotest.(check int) "isolated vertex" 1 (size vertex root)
  done;
  Alcotest.(check int) "root total" 6 (total root);
  let pair = joined 0 1 root in
  let triple = joined 1 2 pair in
  let quad = joined 2 3 triple in
  Alcotest.(check int) "pair, first endpoint" 2 (size 0 pair);
  Alcotest.(check int) "pair, second endpoint" 2 (size 1 pair);
  Alcotest.(check int) "untouched vertex" 1 (size 5 pair);
  Alcotest.(check int) "triple" 3 (size 2 triple);
  for vertex = 0 to 3 do
    Alcotest.(check int) "the whole component agrees" 4 (size vertex quad)
  done;
  Alcotest.(check int) "outside the component" 1 (size 4 quad);
  Alcotest.(check int) "quad total" 6 (total quad);
  let redundant = joined 3 0 quad in
  for vertex = 0 to vertex_count quad - 1 do
    Alcotest.(check int) "a redundant link changes no size" (size vertex quad)
      (size vertex redundant)
  done;
  let branch = joined 4 5 quad in
  Alcotest.(check int) "the branch grew its own component" 2 (size 4 branch);
  Alcotest.(check int) "the parent version did not" 1 (size 4 quad);
  Alcotest.(check int) "an older version did not" 1 (size 2 pair);
  Alcotest.(check int) "branch total" 6 (total branch);
  Alcotest.(check int) "older total" 6 (total pair);
  let merged = joined 5 0 branch in
  Alcotest.(check int) "everything merged" 1 (component_count merged);
  for vertex = 0 to vertex_count merged - 1 do
    Alcotest.(check int) "one whole component" 6 (size vertex merged)
  done;
  ignore (ok "validate" (validate merged) : statistics)

let test_sparse_universe () =
  let root = forest max_int in
  let audit = ok "validate" (validate root) in
  Alcotest.(check int) "a huge universe stores nothing" 0 audit.stored_cell_count;
  let linked = joined 0 (max_int - 1) root in
  Alcotest.(check int) "components" (max_int - 1) (component_count linked);
  Alcotest.(check bool) "the linked pair" true (ok "connected" (connected 0 (max_int - 1) linked));
  Alcotest.(check bool) "an untouched vertex" false
    (ok "connected" (connected 0 (max_int - 2) linked));
  Alcotest.(check int) "an untouched vertex is its own root" (max_int - 2)
    (ok "find" (find (max_int - 2) linked));
  Alcotest.(check int) "an untouched vertex is a singleton" 1
    (ok "component size" (component_size (max_int - 2) linked));
  check_witness "the union that linked them" (Some (version linked))
    (witness 0 (max_int - 1) linked);
  let audit = ok "validate" (validate linked) in
  Alcotest.(check int) "only the touched vertices are stored" 2 audit.stored_cell_count;
  Alcotest.(check int) "one edge" 1 audit.maximum_parent_path_length

let test_deep_history_stays_walkable () =
  (* Deep enough that any non-tail walk over the version chain would exhaust the stack, in link, in
     the witness query, and in the audit's lineage pass. *)
  let redundant_links = 200_000 in
  let root = forest 3 in
  let first = joined 0 1 root in
  let current = ref first in
  for _ = 1 to redundant_links do
    current := joined 2 2 !current
  done;
  let deep = !current in
  Alcotest.(check int) "one version per link" (redundant_links + 1) (version_depth (version deep));
  check_witness "a long redundant tail never replaces the witness" (Some (version first))
    (witness 0 1 deep);
  check_witness "still disconnected" None (witness 0 2 deep);
  Alcotest.(check bool) "the whole tail shares one connectivity index" true
    (shares_connectivity_root_with first deep);
  let audit = ok "validate" (validate deep) in
  Alcotest.(check int) "audited history depth" (redundant_links + 1) audit.history_depth;
  Alcotest.(check int) "two stored cells" 2 audit.stored_cell_count

let test_rejected_vertices_publish_nothing () =
  let root = forest 3 in
  expect_error "find below the universe" (find (-1) root);
  expect_error "find above the universe" (find 3 root);
  expect_error "connected, first endpoint" (connected (-1) 0 root);
  expect_error "connected, second endpoint" (connected 0 3 root);
  expect_error "link, first endpoint" (link (-1) 0 root);
  expect_error "link, second endpoint" (link 0 3 root);
  expect_error "first connected, first endpoint" (first_connected (-1) 0 root);
  expect_error "first connected, second endpoint" (first_connected 0 3 root);
  expect_error "component size below" (component_size (-1) root);
  expect_error "component size above" (component_size 3 root);
  (* A rejected vertex and a disconnected pair are different answers. *)
  Alcotest.(check bool) "a rejected query is an error" true
    (Result.is_error (first_connected 0 3 root));
  check_witness "a disconnected pair is an absent witness" None (witness 0 1 root);
  Alcotest.(check bool) "nothing was published" true
    (same_version (version root) (version_root (version root)));
  Alcotest.(check int) "the history did not move" 0 (version_depth (version root));
  Alcotest.(check int) "the components did not move" 3 (component_count root);
  ignore (ok "validate" (validate root) : statistics)

let test_separate_forests_have_separate_histories () =
  (* Two edgeless roots carry structurally identical tokens. Identity has to tell them apart, which
     also means the root token must not be a shared compile-time constant. *)
  let left_root = forest 2 in
  let right_root = forest 2 in
  let left = joined 0 1 left_root in
  let right = joined 0 1 right_root in
  Alcotest.(check bool) "two history roots are distinct" false
    (same_version (version left_root) (version right_root));
  Alcotest.(check bool) "two first unions are distinct" false
    (same_version (version left) (version right));
  Alcotest.(check int) "and share a depth" (version_depth (version left))
    (version_depth (version right));
  check_witness "left witness" (Some (version left)) (witness 0 1 left);
  check_witness "right witness" (Some (version right)) (witness 0 1 right);
  Alcotest.(check bool) "the two witnesses are not the same version" false
    (match (witness 0 1 left, witness 0 1 right) with
    | Some one, Some other -> same_version one other
    | _ -> true)

type model = { actual : t; classes : int array; parent : int option }

(** The naive oracle: walk the version lineage from the history root down and report the first
    ancestor whose class partition already joins the pair. *)
let scan_for_first_connection states index left right =
  let rec lineage position accumulated =
    let state = states.(position) in
    match state.parent with
    | Some parent -> lineage parent (position :: accumulated)
    | None -> position :: accumulated
  in
  let rec earliest = function
    | [] -> None
    | position :: rest ->
        let state = states.(position) in
        if state.classes.(left) = state.classes.(right) then Some (version state.actual)
        else earliest rest
  in
  earliest (lineage index [])

let test_random_branching_matches_a_naive_scan () =
  let vertex_count = 12 in
  let operations = 250 in
  let random = Random.State.make [| 0x5EED; 2026 |] in
  let root =
    { actual = forest vertex_count; classes = Array.init vertex_count Fun.id; parent = None }
  in
  let states = Array.make (operations + 1) root in
  let published = ref 1 in
  for step = 1 to operations do
    let parent_index = Random.State.int random !published in
    let left = Random.State.int random vertex_count in
    let right = Random.State.int random vertex_count in
    let classes = Array.copy states.(parent_index).classes in
    let left_class = classes.(left) in
    let right_class = classes.(right) in
    if left_class <> right_class then
      Array.iteri
        (fun index value -> if value = right_class then classes.(index) <- left_class)
        classes;
    let parent_version = version states.(parent_index).actual in
    let actual = joined left right states.(parent_index).actual in
    check_witness "the child's parent token" (Some parent_version)
      (version_parent (version actual));
    Alcotest.(check int) "the child's depth"
      (version_depth parent_version + 1)
      (version_depth (version actual));
    let distinct = Hashtbl.create 16 in
    Array.iter (fun value -> Hashtbl.replace distinct value ()) classes;
    Alcotest.(check int) "component count matches the partition" (Hashtbl.length distinct)
      (component_count actual);
    states.(step) <- { actual; classes; parent = Some parent_index };
    published := step + 1;
    for query_left = 0 to vertex_count - 1 do
      for query_right = 0 to vertex_count - 1 do
        let state = states.(step) in
        Alcotest.(check bool) "connectivity matches the partition"
          (state.classes.(query_left) = state.classes.(query_right))
          (ok "connected" (connected query_left query_right state.actual));
        let expected = scan_for_first_connection states step query_left query_right in
        check_witness "witness matches the naive scan" expected
          (witness query_left query_right state.actual);
        (* Deterministic in the argument order: the fold that picks the deepest candidate keeps
           the incumbent on a tie, and each version tags exactly one edge, so no true tie arises. *)
        check_witness "witness is order independent" expected
          (witness query_right query_left state.actual)
      done
    done;
    if step mod 25 = 0 then ignore (ok "validate" (validate states.(step).actual) : statistics)
  done;
  (* Recheck retained versions once every sibling branch has been constructed. *)
  for _ = 1 to 200 do
    let index = Random.State.int random !published in
    let left = Random.State.int random vertex_count in
    let right = Random.State.int random vertex_count in
    check_witness "a retained version keeps its own answers"
      (scan_for_first_connection states index left right)
      (witness left right states.(index).actual)
  done

let tests =
  [
    Alcotest.test_case "edgeless history root" `Quick test_root_version;
    Alcotest.test_case "links record first connections" `Quick
      test_link_records_the_first_connection;
    Alcotest.test_case "redundant links share connectivity" `Quick
      test_redundant_links_share_connectivity;
    Alcotest.test_case "sibling branches stay distinct" `Quick test_sibling_branches_stay_distinct;
    Alcotest.test_case "witness comes from the pair LCA" `Quick
      test_witness_comes_from_the_pair_lca;
    Alcotest.test_case "endpoint at a demoted LCA" `Quick
      test_endpoint_at_the_lca_after_it_becomes_a_child;
    Alcotest.test_case "union by size height bound" `Quick
      test_balanced_unions_respect_the_height_bound;
    Alcotest.test_case "union by size tie break" `Quick test_union_by_size_tie_break;
    Alcotest.test_case "cached component sizes" `Quick
      test_component_size_reports_the_cached_union_size;
    Alcotest.test_case "sparse universe" `Quick test_sparse_universe;
    Alcotest.test_case "deep history stays walkable" `Quick test_deep_history_stays_walkable;
    Alcotest.test_case "rejected vertices publish nothing" `Quick
      test_rejected_vertices_publish_nothing;
    Alcotest.test_case "separate forests, separate histories" `Quick
      test_separate_forests_have_separate_histories;
    Alcotest.test_case "random branching matches a naive scan" `Quick
      test_random_branching_matches_a_naive_scan;
  ]
