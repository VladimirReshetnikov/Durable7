(** Implementation of the fully branching persistent insertion-only connectivity forest.

    Parent cells live sparsely in a CHAMP map keyed by vertex, an absent cell meaning "singleton
    root of size 1". Union by size without path compression keeps every parent path within
    [floor (log2 vertex_count)] edges, and the one root-parent edge each successful link adds
    carries the child version, which is what turns two current parent paths into a first-connection
    witness without ever searching the version history.

    Every chain walk here is a loop or a tail call. Parent paths are logarithmic and could afford
    recursion, but a version chain cannot: histories run hundreds of thousands of links deep, and a
    non-tail walk over one would exhaust the stack. *)

type version = {
  version_parent : version option;
  version_depth : int;
  mutable root_link : version option;
      (* The history root of this version's tree, [None] only for a history root between its
         allocation and the assignment that ties its self-reference, which no caller can observe.
         The field is mutable for exactly that knot, and never assigned again. It is also what keeps
         a history-root record from being a compile-time constant: the native compiler lifts those
         into shared static data, which would make two independently created forests share one
         history root and defeat identity comparison. *)
}

(* One sparse parent record. [cell_size] is meaningful only for a root, that is when [cell_parent]
   is the vertex itself, and [joined_at] is present only on a non-root edge. *)
type cell = { cell_parent : int; cell_size : int; joined_at : version option }

type t = {
  universe : int; (* the fixed vertex-universe size *)
  components : int; (* the cached connected-component count *)
  cells : (int, cell) Persistent_hamt.map;
  current_version : version;
}

type statistics = {
  vertex_count : int;
  component_count : int;
  stored_cell_count : int;
  maximum_parent_path_length : int;
  history_depth : int;
}

(* Hashing a vertex to itself is what earns the unconditional cell-access bound: vertices are
   validated into [0 .. universe - 1] before any cell is touched, so the trie's [land max_int] mask
   is the identity and the hash is injective over the whole key universe. No collision bucket can
   then hold two distinct keys, and two distinct vertices diverge within the 62 significant hash
   bits, that is within 13 levels of 32-way nodes. *)
let vertex_policy : int Common.Hash_policy.t =
  Common.Hash_policy.create ~hash:(fun vertex -> vertex) ~equal:Int.equal

(* Cells are compared field by field, with version tags compared by identity. The map's default
   structural equality would instead walk a version's whole parent chain, and would not terminate at
   all on the history root's self-reference. *)
let cell_equal left right =
  left.cell_parent = right.cell_parent
  && left.cell_size = right.cell_size
  &&
  match (left.joined_at, right.joined_at) with
  | None, None -> true
  | Some left_tag, Some right_tag -> left_tag == right_tag
  | None, Some _ | Some _, None -> false

let version_root token = match token.root_link with Some root -> root | None -> token

let fresh_history_root () =
  let root = { version_parent = None; version_depth = 0; root_link = None } in
  root.root_link <- Some root;
  root

let child_version parent =
  if parent.version_depth = max_int then
    Error "the connection-forest history depth would overflow"
  else
    Ok
      {
        version_parent = Some parent;
        version_depth = parent.version_depth + 1;
        root_link = Some (version_root parent);
      }

let create vertex_count =
  if vertex_count < 0 then Error "vertex count cannot be negative"
  else
    Ok
      {
        universe = vertex_count;
        components = vertex_count;
        cells = Persistent_hamt.empty ~value_equal:cell_equal vertex_policy;
        current_version = fresh_history_root ();
      }

let vertex_count forest = forest.universe
let component_count forest = forest.components
let version forest = forest.current_version
let version_depth token = token.version_depth
let version_parent token = token.version_parent
let same_version left right = left == right
let in_universe forest vertex = vertex >= 0 && vertex < forest.universe

let check_vertex forest vertex =
  if in_universe forest vertex then Ok ()
  else Error "vertex must lie within the forest's fixed universe"

let check_vertices forest left right =
  match check_vertex forest left with
  | Error _ as error -> error
  | Ok () -> check_vertex forest right

let cell_at forest vertex =
  match Persistent_hamt.find_opt vertex forest.cells with
  | Some cell -> cell
  | None -> { cell_parent = vertex; cell_size = 1; joined_at = None }

(* An absent cell is canonically its own singleton root. *)
let parent_of forest vertex =
  match Persistent_hamt.find_opt vertex forest.cells with
  | Some cell -> cell.cell_parent
  | None -> vertex

let size_of forest vertex =
  match Persistent_hamt.find_opt vertex forest.cells with
  | Some cell -> cell.cell_size
  | None -> 1

let rec find_root forest vertex =
  let parent = parent_of forest vertex in
  if parent = vertex then vertex else find_root forest parent

let find vertex forest =
  match check_vertex forest vertex with
  | Error _ as error -> error
  | Ok () -> Ok (find_root forest vertex)

let connected left right forest =
  match check_vertices forest left right with
  | Error _ as error -> error
  | Ok () -> Ok (find_root forest left = find_root forest right)

let component_size vertex forest =
  match check_vertex forest vertex with
  | Error _ as error -> error
  | Ok () -> Ok (size_of forest (find_root forest vertex))

let link left right forest =
  match check_vertices forest left right with
  | Error _ as error -> error
  | Ok () -> (
      match child_version forest.current_version with
      | Error message -> Error message
      | Ok child ->
          let left_root = find_root forest left in
          let right_root = find_root forest right in
          if left_root = right_root then Ok { forest with current_version = child }
          else
            let left_size = size_of forest left_root in
            let right_size = size_of forest right_root in
            (* On a size tie the first endpoint's root remains the representative, making the
               result deterministic without changing the logarithmic-height proof. *)
            let keeper, absorbed =
              if left_size < right_size then (right_root, left_root) else (left_root, right_root)
            in
            let cells =
              forest.cells
              |> Persistent_hamt.set keeper
                   { cell_parent = keeper; cell_size = left_size + right_size; joined_at = None }
              |> Persistent_hamt.set absorbed
                   { cell_parent = keeper; cell_size = 0; joined_at = Some child }
            in
            Ok { forest with components = forest.components - 1; cells; current_version = child })

(* The parent path of a vertex, as [(vertex, joined_at)] steps ordered from the current root down to
   the vertex itself. Never empty. *)
let build_path forest vertex =
  let rec walk current accumulated =
    let cell = cell_at forest current in
    let accumulated = (current, cell.joined_at) :: accumulated in
    if cell.cell_parent = current then accumulated else walk cell.cell_parent accumulated
  in
  walk vertex []

(* Keeps the deeper of two candidate witnesses, and the left one on equal depth. *)
let later best candidate =
  match (best, candidate) with
  | _, None -> best
  | None, Some _ -> candidate
  | Some current, Some proposed ->
      if proposed.version_depth > current.version_depth then candidate else best

let rec drop_shared_prefix left right =
  match (left, right) with
  | (left_vertex, _) :: left_rest, (right_vertex, _) :: right_rest when left_vertex = right_vertex
    ->
      drop_shared_prefix left_rest right_rest
  | _ -> (left, right)

let first_connected left right forest =
  match check_vertices forest left right with
  | Error _ as error -> error
  | Ok () ->
      if left = right then Ok (Some (version_root forest.current_version))
      else
        let left_path = build_path forest left in
        let right_path = build_path forest right in
        let same_component =
          match (left_path, right_path) with
          | (left_root, _) :: _, (right_root, _) :: _ -> left_root = right_root
          | [], _ | _, [] -> false
        in
        if not same_component then Ok None
        else
          (* Delete the common rootward prefix. The remaining steps are precisely the two paths
             strictly below the forest LCA, and every one of those steps owns a union tag. *)
          let left_rest, right_rest = drop_shared_prefix left_path right_path in
          let latest = List.fold_left (fun best (_, tag) -> later best tag) None left_rest in
          let latest = List.fold_left (fun best (_, tag) -> later best tag) latest right_rest in
          (* Distinct connected vertices have at least one edge strictly below their LCA. *)
          if Option.is_none latest then Error "a connected pair has no union-edge witness"
          else Ok latest

let shares_connectivity_root_with left right = left.cells == right.cells
let rec floor_log2 value = if value <= 1 then 0 else 1 + floor_log2 (value / 2)

let validate forest =
  if forest.universe < 0 || forest.components < 0 || forest.components > forest.universe then
    Error "connection-forest counts are invalid"
  else
    let history_depth = forest.current_version.version_depth in
    let history_root = version_root forest.current_version in
    if history_depth < 0 then Error "the history-token parent chain is inconsistent"
    else begin
      (* One lineage slot per depth: the root-to-current chain holds exactly one version at each
         depth, so this indexes ancestry in constant time without hashing a token. *)
      let lineage = Array.make (history_depth + 1) forest.current_version in
      let is_ancestor token =
        let depth = token.version_depth in
        depth >= 0 && depth <= history_depth && lineage.(depth) == token
      in
      let rec walk_lineage token expected =
        if
          expected < 0 || token.version_depth <> expected
          || not (version_root token == history_root)
        then Error "the history-token parent chain is inconsistent"
        else begin
          lineage.(expected) <- token;
          match token.version_parent with
          | Some parent -> walk_lineage parent (expected - 1)
          | None -> Ok expected
        end
      in
      let stored = Persistent_hamt.to_list forest.cells in
      let rec check_cells = function
        | [] -> Ok ()
        | (vertex, cell) :: rest ->
            if not (in_universe forest vertex && in_universe forest cell.cell_parent) then
              Error "a sparse parent cell addresses a vertex outside the universe"
            else if cell.cell_parent = vertex then
              if cell.cell_size <= 1 || Option.is_some cell.joined_at then
                Error "a stored root cell is not a canonical non-singleton root"
              else check_cells rest
            else if
              cell.cell_size <> 0
              || (match cell.joined_at with Some tag -> not (is_ancestor tag) | None -> true)
              || not (Persistent_hamt.mem cell.cell_parent forest.cells)
            then Error "a non-root cell has an invalid size or union-version tag"
            else check_cells rest
      in
      let maximum_height = if forest.universe <= 1 then 0 else floor_log2 forest.universe in
      let rec climb current height previous_depth =
        let cell = cell_at forest current in
        if cell.cell_parent = current then Ok (current, height)
        else
          match cell.joined_at with
          | None -> Error "union-edge versions do not strictly increase toward the root"
          | Some tag ->
              if tag.version_depth <= previous_depth || not (is_ancestor tag) then
                Error "union-edge versions do not strictly increase toward the root"
              else if height + 1 > maximum_height then
                Error "a parent path exceeds the union-by-size height bound"
              else climb cell.cell_parent (height + 1) tag.version_depth
      in
      let component_sizes = Hashtbl.create 16 in
      let longest_path = ref 0 in
      let rec walk_paths = function
        | [] -> Ok ()
        | (vertex, _) :: rest -> (
            match climb vertex 0 (-1) with
            | Error message -> Error message
            | Ok (root, height) ->
                if height > !longest_path then longest_path := height;
                let counted = Option.value (Hashtbl.find_opt component_sizes root) ~default:0 in
                Hashtbl.replace component_sizes root (counted + 1);
                walk_paths rest)
      in
      let rec check_sizes = function
        | [] -> Ok ()
        | (root, size) :: rest ->
            if size_of forest root <> size then Error "a root's cached union size is incorrect"
            else check_sizes rest
      in
      match walk_lineage forest.current_version history_depth with
      | Error message -> Error message
      | Ok last_depth ->
          if
            last_depth <> 0
            || not (is_ancestor history_root)
            || Option.is_some history_root.version_parent
            || history_root.version_depth <> 0
          then Error "the history root is inconsistent"
          else (
            match check_cells stored with
            | Error message -> Error message
            | Ok () -> (
                match walk_paths stored with
                | Error message -> Error message
                | Ok () ->
                    let stored_cell_count = Persistent_hamt.count forest.cells in
                    let absent_singletons = forest.universe - stored_cell_count in
                    if
                      absent_singletons < 0
                      || absent_singletons + Hashtbl.length component_sizes <> forest.components
                    then Error "the cached component count disagrees with the parent forest"
                    else
                      let roots =
                        Hashtbl.fold (fun root size found -> (root, size) :: found)
                          component_sizes []
                      in
                      (match check_sizes roots with
                      | Error message -> Error message
                      | Ok () ->
                          Ok
                            {
                              vertex_count = forest.universe;
                              component_count = forest.components;
                              stored_cell_count;
                              maximum_parent_path_length = !longest_path;
                              history_depth;
                            })))
    end
