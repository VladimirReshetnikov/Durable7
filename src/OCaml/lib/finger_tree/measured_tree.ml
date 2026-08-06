(** Implementation of the lazy Hinze–Paterson finger tree behind the measured sequence.

    The structure is the one the reference workspaces carry: 1–4-item digits at both ends of every
    level, 2-3 nodes below the surface, and each level's middle subtree held behind a memoized
    suspension — the paper's lazy tree realized in a strict language. Every element measure and
    every 2-3 node's size, measure, and end elements are computed strictly at construction; only
    the two spine-deepening steps are deferred. A digit overflow on [cons]/[snoc] pushes a 2-3 node
    into the middle inside a suspension, and [concat] recurses into the two middles inside a
    suspension, which is exactly what makes endpoint edits O(1) amortized and concatenation
    O(log(min(n, m))) amortized under fully persistent branching: a forced suspension memoizes its
    result in a cell shared by every version that references it, so work deferred by one version
    and forced by another is never repeated.

    Suspensions are defunctionalized: the deferred operations are stored as data
    ([Push_front]/[Push_back]/[Concat_middles]/[Reversed]) and interpreted by an explicit-stack
    force loop. Organic endpoint loops build Θ(n)-deep pending chains, and a recursive force —
    including the standard library's [Lazy.force] — takes one native frame per link and overflows
    the stack long before 200k links, which was proven by experiment in this repository. Forcing
    publishes by a single mutable-field write performed only after the deferred computation
    succeeds, so a raising measure policy publishes nothing and the force is retryable with every
    retained version intact. *)

type ('element, 'measure) leaf = { leaf_value : 'element; leaf_measure : 'measure }

(* An item is a user element at the surface level and a 2-3 node below it. Consumers treat items
   as opaque size/measure/first/last carriers: splitting deliberately rebuilds with items of mixed
   depth side by side, and everything downstream of a split stays correct because no operation
   assumes the items at one level share a depth. *)
type ('element, 'measure) item =
  | Element of ('element, 'measure) leaf
  | Node of ('element, 'measure) node

and ('element, 'measure) node = {
  node_children : ('element, 'measure) item list;
  node_size : int;
  node_measure : 'measure;
  node_first : ('element, 'measure) leaf;
  node_last : ('element, 'measure) leaf;
}

and ('element, 'measure) spine =
  | Empty_spine
  | Single of ('element, 'measure) item
  | Deep of ('element, 'measure) deep

and ('element, 'measure) deep = {
  deep_size : int;
  deep_prefix : ('element, 'measure) item list;
  deep_middle : ('element, 'measure) suspension;
  deep_middle_size : int;
  deep_suffix : ('element, 'measure) item list;
  (* The node's total measure, memoized on first read. [deep_size] and [deep_middle_size] are
     strict — every construction site knows them arithmetically — so no size read ever forces the
     middle. *)
  mutable deep_measure : 'measure option;
}

and ('element, 'measure) suspension = { mutable state : ('element, 'measure) suspension_state }

and ('element, 'measure) suspension_state =
  | Forced of ('element, 'measure) spine
  | Pending of ('element, 'measure) operation

and ('element, 'measure) operation =
  | Push_front of ('element, 'measure) item * ('element, 'measure) suspension
  | Push_back of ('element, 'measure) suspension * ('element, 'measure) item
  | Concat_middles of
      ('element, 'measure) suspension
      * ('element, 'measure) item list
      * ('element, 'measure) suspension
  | Reversed of ('element, 'measure) suspension

type ('element, 'measure) t = {
  tree_policy : ('element, 'measure) Measures.policy;
  root : ('element, 'measure) spine;
}

type ('element, 'measure) cursor = { cursor_snapshot : ('element, 'measure) t; cursor_index : int }

(* --- items and digits ------------------------------------------------------------------------ *)

let item_size = function Element _ -> 1 | Node node -> node.node_size
let item_measure = function Element leaf -> leaf.leaf_measure | Node node -> node.node_measure
let item_first = function Element leaf -> leaf | Node node -> node.node_first
let item_last = function Element leaf -> leaf | Node node -> node.node_last
let digit_size digit = List.fold_left (fun size item -> size + item_size item) 0 digit

let digit_measure policy = function
  | [] -> assert false
  | first :: rest ->
      List.fold_left
        (fun measure item -> Measures.append policy measure (item_measure item))
        (item_measure first) rest

(* Builds a 2-3 node, computing its size and measure strictly and caching its end elements. *)
let make_node policy children =
  match children with
  | [] -> assert false
  | first :: _ ->
      let rec last_item = function
        | [] -> assert false
        | [ item ] -> item
        | _ :: rest -> last_item rest
      in
      Node
        {
          node_children = children;
          node_size = digit_size children;
          node_measure = digit_measure policy children;
          node_first = item_first first;
          node_last = item_last (last_item children);
        }

(* The digit an item pulled up from a middle contributes: node children, or the item alone. *)
let item_digit = function Node node -> node.node_children | Element _ as leaf -> [ leaf ]

(* --- spines ---------------------------------------------------------------------------------- *)

let spine_size = function
  | Empty_spine -> 0
  | Single item -> item_size item
  | Deep deep -> deep.deep_size

let make_deep prefix middle middle_size suffix =
  Deep
    {
      deep_size = digit_size prefix + middle_size + digit_size suffix;
      deep_prefix = prefix;
      deep_middle = middle;
      deep_middle_size = middle_size;
      deep_suffix = suffix;
      deep_measure = None;
    }

let ready spine = { state = Forced spine }

(* A fresh already-forced empty middle. The value restriction rules out one polymorphic shared
   singleton (the record has a mutable field), so shallow deep nodes each hold their own cell;
   forcing one is a single field read either way. *)
let empty_middle () = ready Empty_spine

let digit_to_spine digit =
  match digit with
  | [] -> Empty_spine
  | [ item ] -> Single item
  | _ ->
      let rec split_list count acc rest =
        if count = 0 then (List.rev acc, rest)
        else
          match rest with
          | [] -> (List.rev acc, [])
          | item :: tail -> split_list (count - 1) (item :: acc) tail
      in
      let prefix, suffix = split_list (List.length digit / 2) [] digit in
      make_deep prefix (empty_middle ()) 0 suffix

(* --- endpoint pushes (never force; a digit overflow defers its push into the middle) --------- *)

let cons_spine policy item spine =
  match spine with
  | Empty_spine -> Single item
  | Single existing -> make_deep [ item ] (empty_middle ()) 0 [ existing ]
  | Deep deep -> (
      match deep.deep_prefix with
      | [ first; second; third; fourth ] ->
          let overflow = make_node policy [ second; third; fourth ] in
          make_deep [ item; first ]
            (* The push into the middle is the suspended step Hinze–Paterson amortization needs. *)
            { state = Pending (Push_front (overflow, deep.deep_middle)) }
            (deep.deep_middle_size + item_size overflow)
            deep.deep_suffix
      | prefix -> make_deep (item :: prefix) deep.deep_middle deep.deep_middle_size deep.deep_suffix
      )

let snoc_spine policy spine item =
  match spine with
  | Empty_spine -> Single item
  | Single existing -> make_deep [ existing ] (empty_middle ()) 0 [ item ]
  | Deep deep -> (
      match deep.deep_suffix with
      | [ first; second; third; fourth ] ->
          let overflow = make_node policy [ first; second; third ] in
          make_deep deep.deep_prefix
            { state = Pending (Push_back (deep.deep_middle, overflow)) }
            (deep.deep_middle_size + item_size overflow)
            [ fourth; item ]
      | suffix -> make_deep deep.deep_prefix deep.deep_middle deep.deep_middle_size (suffix @ [ item ])
      )

(* --- concatenation (never forces; the middle recursion is deferred) -------------------------- *)

(* Groups 2..12 items into 2-3 nodes, the standard Hinze–Paterson regrouping. *)
let rec group_nodes policy items =
  match items with
  | [] | [ _ ] -> assert false
  | [ first; second ] -> [ make_node policy [ first; second ] ]
  | [ first; second; third ] -> [ make_node policy [ first; second; third ] ]
  | [ first; second; third; fourth ] ->
      [ make_node policy [ first; second ]; make_node policy [ third; fourth ] ]
  | first :: second :: third :: rest ->
      make_node policy [ first; second; third ] :: group_nodes policy rest

let rec app3 policy left between right =
  match (left, right) with
  | Empty_spine, _ ->
      List.fold_right (fun item spine -> cons_spine policy item spine) between right
  | _, Empty_spine -> List.fold_left (fun spine item -> snoc_spine policy spine item) left between
  | Single item, _ -> cons_spine policy item (app3 policy Empty_spine between right)
  | _, Single item -> snoc_spine policy (app3 policy left between Empty_spine) item
  | Deep left_deep, Deep right_deep ->
      let nodes = group_nodes policy (left_deep.deep_suffix @ between @ right_deep.deep_prefix) in
      let nodes_size = digit_size nodes in
      make_deep left_deep.deep_prefix
        (* The recursion into both middles is deferred, which is what makes concatenation
           O(log(min(n, m))) amortized instead of costing its whole recursion up front. *)
        {
          state = Pending (Concat_middles (left_deep.deep_middle, nodes, right_deep.deep_middle));
        }
        (left_deep.deep_middle_size + nodes_size + right_deep.deep_middle_size)
        right_deep.deep_suffix

(* --- the lazy reversed view (never forces; the middle's reversal is deferred) ---------------- *)

(* Mirrors one item, recombining its summary in the mirrored order. Sizes and end elements survive
   with roles swapped, but the cached measure is rebuilt from the reversed children: a mirrored
   subtree's summary equals its cached one only under a commutative monoid, and this recombination
   is what keeps the reversed view correct under every monoid, at O(1) combines per node paid only
   when the enclosing reversal suspension forces. *)
let rec reverse_item policy = function
  | Element _ as item -> item
  | Node node -> make_node policy (List.rev_map (reverse_item policy) node.node_children)

let reverse_digit policy digit = List.rev_map (reverse_item policy) digit

let reverse_spine policy = function
  | Empty_spine -> Empty_spine
  | Single item -> Single (reverse_item policy item)
  | Deep deep ->
      let middle =
        match deep.deep_middle.state with
        | Forced Empty_spine -> empty_middle ()
        | Forced (Single _ | Deep _) | Pending _ ->
            { state = Pending (Reversed deep.deep_middle) }
      in
      Deep
        {
          deep_size = deep.deep_size;
          deep_prefix = reverse_digit policy deep.deep_suffix;
          deep_middle = middle;
          deep_middle_size = deep.deep_middle_size;
          deep_suffix = reverse_digit policy deep.deep_prefix;
          deep_measure = None;
        }

(* --- forcing --------------------------------------------------------------------------------- *)

(* Interprets the pending chain iteratively with an explicit stack; see the module comment for why
   recursion (native [Lazy.force] included) is banned here. Publication is the single [state]
   assignment after the deferred computation succeeds, so a raising policy leaves every cell on
   the failure path pending and retryable. *)
let force policy suspension =
  match suspension.state with
  | Forced spine -> spine
  | Pending _ ->
      let rec run stack =
        match stack with
        | [] -> ()
        | top :: rest -> (
            match top.state with
            | Forced _ -> run rest
            | Pending operation -> (
                match operation with
                | Push_front (item, inner) -> (
                    match inner.state with
                    | Pending _ -> run (inner :: stack)
                    | Forced inner_spine ->
                        top.state <- Forced (cons_spine policy item inner_spine);
                        run rest)
                | Push_back (inner, item) -> (
                    match inner.state with
                    | Pending _ -> run (inner :: stack)
                    | Forced inner_spine ->
                        top.state <- Forced (snoc_spine policy inner_spine item);
                        run rest)
                | Reversed inner -> (
                    match inner.state with
                    | Pending _ -> run (inner :: stack)
                    | Forced inner_spine ->
                        top.state <- Forced (reverse_spine policy inner_spine);
                        run rest)
                | Concat_middles (left, between, right) -> (
                    match (left.state, right.state) with
                    | Pending _, _ -> run (left :: stack)
                    | _, Pending _ -> run (right :: stack)
                    | Forced left_spine, Forced right_spine ->
                        top.state <- Forced (app3 policy left_spine between right_spine);
                        run rest)))
      in
      run [ suspension ];
      (match suspension.state with
      | Forced spine -> spine
      | Pending _ -> assert false)

(* The spine's total measure, memoized at each deep node. Forces the middles it enters, which is
   why a root measure read is O(1) amortized rather than worst-case. *)
let rec spine_measure policy = function
  | Empty_spine -> Measures.empty policy
  | Single item -> item_measure item
  | Deep deep -> (
      match deep.deep_measure with
      | Some measure -> measure
      | None ->
          let measure = digit_measure policy deep.deep_prefix in
          let measure =
            match force policy deep.deep_middle with
            | Empty_spine -> measure
            | (Single _ | Deep _) as middle ->
                Measures.append policy measure (spine_measure policy middle)
          in
          let measure = Measures.append policy measure (digit_measure policy deep.deep_suffix) in
          deep.deep_measure <- Some measure;
          measure)

(* --- endpoint views (used by splitting to pull nodes up from a middle) ----------------------- *)

let rec view_left policy spine =
  match spine with
  | Empty_spine -> None
  | Single item -> Some (item, Empty_spine)
  | Deep deep -> (
      match deep.deep_prefix with
      | [] -> assert false
      | head :: (_ :: _ as remaining) ->
          Some (head, make_deep remaining deep.deep_middle deep.deep_middle_size deep.deep_suffix)
      | [ head ] -> (
          match force policy deep.deep_middle with
          | Empty_spine -> Some (head, digit_to_spine deep.deep_suffix)
          | (Single _ | Deep _) as middle -> (
              match view_left policy middle with
              | None -> assert false
              | Some (node, rest) ->
                  Some
                    ( head,
                      make_deep (item_digit node) (ready rest)
                        (deep.deep_middle_size - item_size node)
                        deep.deep_suffix ))))

let rec view_right policy spine =
  match spine with
  | Empty_spine -> None
  | Single item -> Some (Empty_spine, item)
  | Deep deep -> (
      let rec detach_last acc = function
        | [] -> assert false
        | [ last ] -> (List.rev acc, last)
        | item :: rest -> detach_last (item :: acc) rest
      in
      match deep.deep_suffix with
      | [] -> assert false
      | [ last ] -> (
          match force policy deep.deep_middle with
          | Empty_spine -> Some (digit_to_spine deep.deep_prefix, last)
          | (Single _ | Deep _) as middle -> (
              match view_right policy middle with
              | None -> assert false
              | Some (rest, node) ->
                  Some
                    ( make_deep deep.deep_prefix (ready rest)
                        (deep.deep_middle_size - item_size node)
                        (item_digit node),
                      last )))
      | suffix ->
          let remaining, last = detach_last [] suffix in
          Some
            (make_deep deep.deep_prefix deep.deep_middle deep.deep_middle_size remaining, last))

(* Rebuilds a spine whose prefix digit may be empty by pulling a node up from the middle. *)
let deep_left policy before middle middle_size suffix =
  match before with
  | _ :: _ -> make_deep before middle middle_size suffix
  | [] -> (
      match force policy middle with
      | Empty_spine -> digit_to_spine suffix
      | (Single _ | Deep _) as forced -> (
          match view_left policy forced with
          | None -> assert false
          | Some (node, rest) ->
              make_deep (item_digit node) (ready rest) (middle_size - item_size node) suffix))

(* Rebuilds a spine whose suffix digit may be empty; the mirror image of [deep_left]. *)
let deep_right policy prefix middle middle_size after =
  match after with
  | _ :: _ -> make_deep prefix middle middle_size after
  | [] -> (
      match force policy middle with
      | Empty_spine -> digit_to_spine prefix
      | (Single _ | Deep _) as forced -> (
          match view_right policy forced with
          | None -> assert false
          | Some (rest, node) ->
              make_deep prefix (ready rest) (middle_size - item_size node) (item_digit node)))

(* --- index-directed splitting ---------------------------------------------------------------- *)

(* Splits a digit at an element index: (before, item, after, index into item). *)
let split_digit digit index =
  let rec loop before index = function
    | [] -> assert false
    | item :: rest ->
        let size = item_size item in
        if index < size then (List.rev before, item, rest, index)
        else loop (item :: before) (index - size) rest
  in
  loop [] index digit

(* Splits an item at an element index, collapsing its path into flat item lists. The returned
   lists deliberately mix levels; every consumer treats items as opaque size and measure carriers,
   so mixed levels rebuild correctly through [cons_spine] and [snoc_spine]. *)
let split_item item index =
  let rec loop before after item index =
    match item with
    | Element leaf -> (List.rev before, leaf, after)
    | Node node ->
        let rec find before index = function
          | [] -> assert false
          | child :: rest ->
              let size = item_size child in
              if index < size then (before, child, rest, index)
              else find (child :: before) (index - size) rest
        in
        let before, found, trailing, inner = find before index node.node_children in
        loop before (trailing @ after) found inner
  in
  loop [] [] item index

let push_all policy spine items =
  List.fold_left (fun spine item -> snoc_spine policy spine item) spine items

let unshift_all policy items spine =
  List.fold_right (fun item spine -> cons_spine policy item spine) items spine

(* Splits at an element index: (elements before, the element, elements after). *)
let rec split_spine policy spine index =
  match spine with
  | Empty_spine -> assert false
  | Single item ->
      let item_before, leaf, item_after = split_item item index in
      ( push_all policy Empty_spine item_before,
        leaf,
        unshift_all policy item_after Empty_spine )
  | Deep deep ->
      let prefix_size = digit_size deep.deep_prefix in
      if index < prefix_size then (
        let before, item, after, inner = split_digit deep.deep_prefix index in
        let item_before, leaf, item_after = split_item item inner in
        let left = push_all policy Empty_spine (before @ item_before) in
        let right =
          deep_left policy after deep.deep_middle deep.deep_middle_size deep.deep_suffix
        in
        (left, leaf, unshift_all policy item_after right))
      else
        let index = index - prefix_size in
        if index < deep.deep_middle_size then (
          (* The recursion already collapses down to the element level, so its halves are complete
             spines of items. *)
          let middle = force policy deep.deep_middle in
          let middle_left, leaf, middle_right = split_spine policy middle index in
          let left =
            deep_right policy deep.deep_prefix (ready middle_left) (spine_size middle_left) []
          in
          let right =
            deep_left policy [] (ready middle_right) (spine_size middle_right) deep.deep_suffix
          in
          (left, leaf, right))
        else
          let index = index - deep.deep_middle_size in
          let before, item, after, inner = split_digit deep.deep_suffix index in
          let item_before, leaf, item_after = split_item item inner in
          let left =
            deep_right policy deep.deep_prefix deep.deep_middle deep.deep_middle_size before
          in
          ( push_all policy left item_before,
            leaf,
            unshift_all policy (item_after @ after) Empty_spine )

(* --- eager bulk construction ----------------------------------------------------------------- *)

(* Builds a spine bottom-up with ready middles: bulk construction defers nothing. Deferral pays
   off when later operations may never force the work; a bulk build's caller almost always
   consumes the result, so suspending every overflow would only add cells to force and then
   discard. Grouping level by level costs the same O(n) with none of that churn. *)
let rec build_eager policy items count =
  if count = 0 then Empty_spine
  else if count = 1 then Single (List.hd items)
  else
    let rec split_list count acc rest =
      if count = 0 then (List.rev acc, rest)
      else
        match rest with
        | [] -> (List.rev acc, [])
        | item :: tail -> split_list (count - 1) (item :: acc) tail
    in
    if count <= 8 then
      let prefix, suffix = split_list (count / 2) [] items in
      make_deep prefix (empty_middle ()) 0 suffix
    else
      let prefix, rest = split_list 3 [] items in
      let middle_items, suffix = split_list (count - 6) [] rest in
      let middle_nodes = group_nodes policy middle_items in
      let middle = build_eager policy middle_nodes (List.length middle_nodes) in
      make_deep prefix (ready middle) (spine_size middle) suffix

(* --- public operations ----------------------------------------------------------------------- *)

let empty tree_policy = { tree_policy; root = Empty_spine }

let measured_element policy value =
  Element { leaf_value = value; leaf_measure = Measures.measure policy value }

let singleton tree_policy value = { tree_policy; root = Single (measured_element tree_policy value) }

let of_list tree_policy values =
  let items = List.map (measured_element tree_policy) values in
  { tree_policy; root = build_eager tree_policy items (List.length items) }

let policy tree = tree.tree_policy
let is_empty tree = match tree.root with Empty_spine -> true | Single _ | Deep _ -> false
let length tree = spine_size tree.root
let measure tree = spine_measure tree.tree_policy tree.root

let fold_left folder state tree =
  let tree_policy = tree.tree_policy in
  let rec item_fold state = function
    | Element leaf -> folder state leaf.leaf_value
    | Node node -> List.fold_left item_fold state node.node_children
  in
  let rec spine_fold state = function
    | Empty_spine -> state
    | Single item -> item_fold state item
    | Deep deep ->
        let state = List.fold_left item_fold state deep.deep_prefix in
        let state = spine_fold state (force tree_policy deep.deep_middle) in
        List.fold_left item_fold state deep.deep_suffix
  in
  spine_fold state tree.root

let fold_right folder tree state =
  let tree_policy = tree.tree_policy in
  let rec item_fold item state =
    match item with
    | Element leaf -> folder leaf.leaf_value state
    | Node node -> List.fold_right item_fold node.node_children state
  in
  let rec spine_fold spine state =
    match spine with
    | Empty_spine -> state
    | Single item -> item_fold item state
    | Deep deep ->
        let state = List.fold_right item_fold deep.deep_suffix state in
        let state = spine_fold (force tree_policy deep.deep_middle) state in
        List.fold_right item_fold deep.deep_prefix state
  in
  spine_fold tree.root state

let to_list tree = fold_right List.cons tree []
let iter action tree = fold_left (fun () value -> action value) () tree

let cons value tree =
  { tree with root = cons_spine tree.tree_policy (measured_element tree.tree_policy value) tree.root }

let snoc tree value =
  { tree with root = snoc_spine tree.tree_policy tree.root (measured_element tree.tree_policy value) }

let concat left right =
  if not (Measures.compatible left.tree_policy right.tree_policy) then
    Error "cannot concatenate trees with different measurement policies"
  else
    match (left.root, right.root) with
    | Empty_spine, (Empty_spine | Single _ | Deep _) -> Ok { left with root = right.root }
    | (Single _ | Deep _), Empty_spine -> Ok left
    | (Single _ | Deep _), (Single _ | Deep _) ->
        Ok { left with root = app3 left.tree_policy left.root [] right.root }

let reverse tree = { tree with root = reverse_spine tree.tree_policy tree.root }

let first tree =
  match tree.root with
  | Empty_spine -> None
  | Single item -> Some (item_first item).leaf_value
  | Deep deep -> (
      match deep.deep_prefix with
      | [] -> None
      | item :: _ -> Some (item_first item).leaf_value)

let last tree =
  match tree.root with
  | Empty_spine -> None
  | Single item -> Some (item_last item).leaf_value
  | Deep deep ->
      let rec last_item = function
        | [] -> None
        | [ item ] -> Some (item_last item).leaf_value
        | _ :: rest -> last_item rest
      in
      last_item deep.deep_suffix

let nth index tree =
  if index < 0 || index >= length tree then None
  else
    let tree_policy = tree.tree_policy in
    let rec item_nth index = function
      | Element leaf -> leaf.leaf_value
      | Node node -> digit_nth index node.node_children
    and digit_nth index = function
      | [] -> assert false
      | item :: rest ->
          let size = item_size item in
          if index < size then item_nth index item else digit_nth (index - size) rest
    in
    let rec spine_nth index = function
      | Empty_spine -> assert false
      | Single item -> item_nth index item
      | Deep deep ->
          let prefix_size = digit_size deep.deep_prefix in
          if index < prefix_size then digit_nth index deep.deep_prefix
          else
            let index = index - prefix_size in
            if index < deep.deep_middle_size then
              spine_nth index (force tree_policy deep.deep_middle)
            else digit_nth (index - deep.deep_middle_size) deep.deep_suffix
    in
    Some (spine_nth index tree.root)

let split_at index tree =
  if index <= 0 then ({ tree with root = Empty_spine }, tree)
  else if index >= length tree then (tree, { tree with root = Empty_spine })
  else
    let left, leaf, right = split_spine tree.tree_policy tree.root index in
    ( { tree with root = left },
      { tree with root = cons_spine tree.tree_policy (Element leaf) right } )

let take count tree = fst (split_at count tree)
let drop count tree = snd (split_at count tree)

let insert_at index value tree =
  if index < 0 || index > length tree then Error "sequence insertion index is out of range"
  else
    let left, right = split_at index tree in
    let with_value = snoc left value in
    concat with_value right

let update_at index update tree =
  if index < 0 || index >= length tree then Error "sequence update index is out of range"
  else
    let left, leaf, right = split_spine tree.tree_policy tree.root index in
    let updated = update leaf.leaf_value in
    Ok
      {
        tree with
        root =
          app3 tree.tree_policy
            (snoc_spine tree.tree_policy left (measured_element tree.tree_policy updated))
            [] right;
      }

let remove_at index tree =
  if index < 0 || index >= length tree then Error "sequence removal index is out of range"
  else
    let left, leaf, right = split_spine tree.tree_policy tree.root index in
    Ok (leaf.leaf_value, { tree with root = app3 tree.tree_policy left [] right })

let measure_range start count tree =
  if start < 0 || count < 0 || start > length tree - count then
    Error "sequence measure range is out of bounds"
  else Ok (measure (take count (drop start tree)))

let locate predicate tree =
  let tree_policy = tree.tree_policy in
  let before = ref (Measures.empty tree_policy) in
  let index = ref 0 in
  let rec descend_item = function
    | Element leaf -> (!index, !before, leaf.leaf_value)
    | Node node ->
        (* The last child is the fallback: for a monotone predicate the inclusive measure through
           it equals the inclusive measure through the whole node, which already satisfied. *)
        let rec pick = function
          | [] -> assert false
          | [ child ] -> descend_item child
          | child :: rest ->
              let with_child = Measures.append tree_policy !before (item_measure child) in
              if predicate with_child then descend_item child
              else begin
                before := with_child;
                index := !index + item_size child;
                pick rest
              end
        in
        pick node.node_children
  in
  let rec scan_items = function
    | [] -> None
    | item :: rest ->
        let with_item = Measures.append tree_policy !before (item_measure item) in
        if predicate with_item then Some (descend_item item)
        else begin
          before := with_item;
          index := !index + item_size item;
          scan_items rest
        end
  in
  let rec scan_spine spine =
    match spine with
    | Empty_spine -> None
    | Single item -> scan_items [ item ]
    | Deep deep ->
        let with_spine =
          Measures.append tree_policy !before (spine_measure tree_policy spine)
        in
        if not (predicate with_spine) then begin
          before := with_spine;
          index := !index + deep.deep_size;
          None
        end
        else begin
          match scan_items deep.deep_prefix with
          | Some _ as found -> found
          | None -> (
              match scan_spine (force tree_policy deep.deep_middle) with
              | Some _ as found -> found
              | None -> scan_items deep.deep_suffix)
        end
  in
  (* A monotone predicate that already holds at the identity selects index zero on a nonempty
     tree, matching every sibling port and this module's own interface. An empty tree still
     misses because the scan finds nothing to descend into. *)
  scan_spine tree.root

exception Invalid_structure of string

let validate tree =
  let tree_policy = tree.tree_policy in
  (* Generic measures need not support equality, so validation recomputes shape and sizes while
     construction centralizes every cached-measure write. The walk forces every middle it meets:
     deferred structure hides inside pending suspensions where no non-forcing walk can check it,
     and forcing is memoized so a validated version keeps its cost. Item depths are deliberately
     not compared across a digit — splitting rebuilds with mixed item levels by design. *)
  let rec item_stats item =
    match item with
    | Element leaf -> (1, leaf, leaf)
    | Node node ->
        let child_count = List.length node.node_children in
        if child_count < 2 || child_count > 3 then
          raise (Invalid_structure "a 2-3 node must hold two or three children");
        let size, first_leaf, last_leaf =
          List.fold_left
            (fun (size, first_leaf, _) child ->
              let child_size, child_first, child_last = item_stats child in
              let first_leaf =
                match first_leaf with None -> Some child_first | Some _ -> first_leaf
              in
              (size + child_size, first_leaf, Some child_last))
            (0, None, None) node.node_children
        in
        let first_leaf = Option.get first_leaf in
        let last_leaf = Option.get last_leaf in
        if node.node_size <> size then
          raise (Invalid_structure "a cached node size disagrees with its children");
        if not (node.node_first == first_leaf) then
          raise (Invalid_structure "a cached first element is not the leftmost descendant");
        if not (node.node_last == last_leaf) then
          raise (Invalid_structure "a cached last element is not the rightmost descendant");
        (size, first_leaf, last_leaf)
  in
  let check_digit label digit =
    let count = List.length digit in
    if count < 1 || count > 4 then
      raise (Invalid_structure (label ^ " digit must hold one to four items"));
    List.fold_left
      (fun size item ->
        let item_size, _, _ = item_stats item in
        size + item_size)
      0 digit
  in
  let rec check_spine = function
    | Empty_spine -> 0
    | Single item ->
        let size, _, _ = item_stats item in
        size
    | Deep deep ->
        let prefix_size = check_digit "a prefix" deep.deep_prefix in
        let suffix_size = check_digit "a suffix" deep.deep_suffix in
        let middle_size = check_spine (force tree_policy deep.deep_middle) in
        if deep.deep_middle_size <> middle_size then
          raise (Invalid_structure "a cached middle size disagrees with the forced middle");
        if deep.deep_size <> prefix_size + middle_size + suffix_size then
          raise (Invalid_structure "a cached spine size disagrees with its digits and middle");
        deep.deep_size
  in
  match check_spine tree.root with
  | (_ : int) -> Ok ()
  | exception Invalid_structure message -> Error message

(* --- cursors (index-based over one snapshot; edits go through the tree operations) ----------- *)

let cursor_at_start tree = { cursor_snapshot = tree; cursor_index = 0 }
let cursor_at_end tree = { cursor_snapshot = tree; cursor_index = length tree }

let cursor_by_measure predicate tree =
  match locate predicate tree with
  | Some (index, _, _) -> (true, { cursor_snapshot = tree; cursor_index = index })
  | None -> (false, cursor_at_end tree)

let cursor_is_at_start value = value.cursor_index = 0
let cursor_is_at_end value = value.cursor_index = length value.cursor_snapshot

let cursor_measure_before value =
  Result.get_ok (measure_range 0 value.cursor_index value.cursor_snapshot)

let cursor_measure_after value =
  Result.get_ok
    (measure_range value.cursor_index
       (length value.cursor_snapshot - value.cursor_index)
       value.cursor_snapshot)

let cursor_peek_previous value = nth (value.cursor_index - 1) value.cursor_snapshot
let cursor_peek_next value = nth value.cursor_index value.cursor_snapshot

let cursor_move_previous value =
  if cursor_is_at_start value then None
  else Some { value with cursor_index = value.cursor_index - 1 }

let cursor_move_next value =
  if cursor_is_at_end value then None else Some { value with cursor_index = value.cursor_index + 1 }

let cursor_seek_by_measure predicate value = cursor_by_measure predicate value.cursor_snapshot

let cursor_insert element value =
  {
    cursor_snapshot = Result.get_ok (insert_at value.cursor_index element value.cursor_snapshot);
    cursor_index = value.cursor_index + 1;
  }

let cursor_delete_previous value =
  if cursor_is_at_start value then None
  else
    let _, snapshot = Result.get_ok (remove_at (value.cursor_index - 1) value.cursor_snapshot) in
    Some { cursor_snapshot = snapshot; cursor_index = value.cursor_index - 1 }

let cursor_delete_next value =
  if cursor_is_at_end value then None
  else
    let _, snapshot = Result.get_ok (remove_at value.cursor_index value.cursor_snapshot) in
    Some { cursor_snapshot = snapshot; cursor_index = value.cursor_index }

let cursor_replace_next element value =
  if cursor_is_at_end value then None
  else
    let snapshot =
      Result.get_ok (update_at value.cursor_index (fun _ -> element) value.cursor_snapshot)
    in
    Some { cursor_snapshot = snapshot; cursor_index = value.cursor_index }

let cursor_snapshot value = value.cursor_snapshot
