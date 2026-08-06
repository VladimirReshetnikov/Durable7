(** Implementation of the persistent rank-addressable sorted set with representative retention.

    Built on the lazy Hinze–Paterson measured core with an order-statistic measure: each element
    measures as [(1, Some element)] and the monoid carries [(count, last element)] with a
    right-biased last, so a cached prefix measure names both how many elements it covers and the
    greatest element it contains. Key seeks are measure-directed descents ([O(log n)] comparator
    calls), point writes are split-and-join edits ([O(log n)]), extremes are O(1) worst-case digit
    reads that invoke no comparator, and rank select is the core's O(log n) size-directed descent.
    Bulk construction sorts once and builds eagerly bottom-up: O(n log n) comparisons plus O(n)
    construction. *)

type 'element t = {
  order : 'element Common.Comparator.t;
  tree : ('element, int * 'element option) Measured_tree.t;
}

let compare set = Common.Comparator.compare set.order

(* The order-statistic policy: element count paired with the right-biased last element, so a
   monotone prefix predicate can compare the probe against the greatest element covered so far.
   The combine never invokes the comparator, which keeps enumeration comparison-free. *)
let make_policy () =
  Measures.create_policy ~id:"sorted-order-statistic-v1"
    ~monoid:
      (Measures.create_monoid ~empty:(0, None)
         ~append:(fun (left_count, left_last) (right_count, right_last) ->
           ( left_count + right_count,
             match right_last with None -> left_last | Some _ -> right_last )))
    ~measure:(fun value -> (1, Some value))
    ()

let empty order = { order; tree = Measured_tree.empty (make_policy ()) }
let comparator set = set.order
let count set = Measured_tree.length set.tree
let is_empty set = count set = 0
let minimum set = Measured_tree.first set.tree
let maximum set = Measured_tree.last set.tree
let nth index set = Measured_tree.nth index set.tree

(* The first element ordering at-or-after the probe, with its rank: one measure-directed descent
   costing O(log n) comparator calls. *)
let at_least value set =
  Measured_tree.locate
    (fun (_, last) ->
      match last with None -> false | Some candidate -> compare set candidate value >= 0)
    set.tree

(* The first element ordering strictly after the probe, with its rank. *)
let above value set =
  Measured_tree.locate
    (fun (_, last) ->
      match last with None -> false | Some candidate -> compare set candidate value > 0)
    set.tree

let count_less_than value set =
  match at_least value set with Some (index, _, _) -> index | None -> count set

let count_at_most value set =
  match above value set with Some (index, _, _) -> index | None -> count set

let index_of value set =
  match at_least value set with
  | Some (index, _, found) when compare set found value = 0 -> Some index
  | Some _ | None -> None

let find value set =
  match at_least value set with
  | Some (_, _, found) when compare set found value = 0 -> Some found
  | Some _ | None -> None

let mem value set = Option.is_some (find value set)

let add value set =
  match at_least value set with
  | Some (index, _, found) ->
      if compare set found value = 0 then (false, set)
      else (true, { set with tree = Result.get_ok (Measured_tree.insert_at index value set.tree) })
  | None -> (true, { set with tree = Measured_tree.snoc set.tree value })

let remove value set =
  match at_least value set with
  | Some (index, _, found) when compare set found value = 0 ->
      let _, tree = Result.get_ok (Measured_tree.remove_at index set.tree) in
      (true, { set with tree })
  | Some _ | None -> (false, set)

(* Stable sort then drop later members of each equal run: stability makes the survivor the first
   occurrence, preserving first-representative retention. O(n log n) comparisons, then an eager
   O(n) bottom-up build that defers nothing. *)
let of_list order values =
  let compare = Common.Comparator.compare order in
  let sorted = List.stable_sort compare values in
  let rec dedup acc = function
    | [] -> List.rev acc
    | [ last ] -> List.rev (last :: acc)
    | first :: second :: rest ->
        if compare first second = 0 then dedup acc (first :: rest)
        else dedup (first :: acc) (second :: rest)
  in
  { order; tree = Measured_tree.of_list (make_policy ()) (dedup [] sorted) }

let floor value set = nth (count_at_most value set - 1) set

let ceiling value set =
  match at_least value set with Some (_, _, found) -> Some found | None -> None

let lower value set = nth (count_less_than value set - 1) set
let higher value set = match above value set with Some (_, _, found) -> Some found | None -> None

let require_compatible left right =
  if not (Common.Comparator.same left.order right.order) then
    invalid_arg "sorted-set comparators differ"

let to_list set = Measured_tree.to_list set.tree

let union left right =
  require_compatible left right;
  List.fold_left (fun result value -> snd (add value result)) left (to_list right)

(* Walks this set in ascending order and appends each member found in [right], so the result is
   built by endpoint pushes rather than searched insertions. *)
let intersect left right =
  require_compatible left right;
  Measured_tree.fold_left
    (fun result value ->
      if mem value right then { result with tree = Measured_tree.snoc result.tree value }
      else result)
    (empty left.order) left.tree

let difference left right =
  require_compatible left right;
  Measured_tree.fold_left
    (fun result value ->
      if mem value right then result
      else { result with tree = Measured_tree.snoc result.tree value })
    (empty left.order) left.tree

let range ~start ~count:range_count set =
  if start < 0 || range_count < 0 || start > count set - range_count then
    Error "sorted-set range is out of bounds"
  else
    Ok { set with tree = Measured_tree.take range_count (Measured_tree.drop start set.tree) }

let value_range ~minimum:low ~maximum:high set =
  if compare set low high > 0 then empty set.order
  else
    let first = count_less_than low set in
    let last = count_at_most high set in
    { set with tree = Measured_tree.take (last - first) (Measured_tree.drop first set.tree) }

module Builder = struct
  type 'element set = 'element t
  type 'element t = { mutable current : 'element set }

  let create current = { current }

  let add value builder =
    let added, successor = add value builder.current in
    builder.current <- successor;
    added

  let remove value builder =
    let removed, successor = remove value builder.current in
    builder.current <- successor;
    removed

  let freeze builder = builder.current
end
