(** Implementation of the persistent order-statistic sorted map with first-key representative
    retention.

    Built on the lazy Hinze–Paterson measured core: each entry measures as [(1, Some key)] and the
    monoid carries [(count, last key)] with a right-biased last, so a cached prefix measure names
    both a rank and the greatest key it covers. Key seeks are measure-directed descents ([O(log N)]
    comparator calls), point writes are split-and-join edits ([O(log N)]), extremes are O(1)
    worst-case comparator-free digit reads, and rank select is the core's O(log N) size-directed
    descent. Key ranges restrict by two boundary seeks into a structurally shared subrange. The
    combine never invokes the comparator, so enumeration and range copies stay comparison-free. *)

type ('key, 'value) entry = { key : 'key; value : 'value }

type ('key, 'value) t = {
  order : 'key Common.Comparator.t;
  tree : (('key, 'value) entry, int * 'key option) Measured_tree.t;
}

let compare map left right = Common.Comparator.compare map.order left right

let make_policy () =
  Measures.create_policy ~id:"sorted-map-order-statistic-v1"
    ~monoid:
      (Measures.create_monoid ~empty:(0, None)
         ~append:(fun (left_count, left_last) (right_count, right_last) ->
           ( left_count + right_count,
             match right_last with None -> left_last | Some _ -> right_last )))
    ~measure:(fun entry -> (1, Some entry.key))
    ()

let empty order = { order; tree = Measured_tree.empty (make_policy ()) }
let comparator map = map.order
let count map = Measured_tree.length map.tree
let is_empty map = count map = 0
let entry_key entry = entry.key
let entry_value entry = entry.value

(* The first entry whose key orders at-or-after the probe, with its rank: one measure-directed
   descent costing O(log N) comparator calls. *)
let at_least key map =
  Measured_tree.locate
    (fun (_, last) ->
      match last with None -> false | Some candidate -> compare map candidate key >= 0)
    map.tree

(* The first entry whose key orders strictly after the probe, with its rank. *)
let above key map =
  Measured_tree.locate
    (fun (_, last) ->
      match last with None -> false | Some candidate -> compare map candidate key > 0)
    map.tree

let lower_bound key map =
  match at_least key map with Some (index, _, _) -> index | None -> count map

let upper_bound key map =
  match above key map with Some (index, _, _) -> index | None -> count map

let nth index map = Measured_tree.nth index map.tree

let find_entry key map =
  match at_least key map with
  | Some (_, _, entry) when compare map entry.key key = 0 -> Some entry
  | Some _ | None -> None

let find_opt key map = Option.map entry_value (find_entry key map)
let mem key map = Option.is_some (find_entry key map)

let add key value map =
  match at_least key map with
  | Some (index, _, entry) ->
      if compare map entry.key key = 0 then
        Error "sorted map already contains an equivalent key"
      else
        Ok { map with tree = Result.get_ok (Measured_tree.insert_at index { key; value } map.tree) }
  | None -> Ok { map with tree = Measured_tree.snoc map.tree { key; value } }

let set key value map =
  match at_least key map with
  | Some (index, _, entry) when compare map entry.key key = 0 ->
      (* Replacing a binding keeps the first-key representative already stored. *)
      let tree =
        Result.get_ok
          (Measured_tree.update_at index (fun existing -> { key = existing.key; value }) map.tree)
      in
      (false, { map with tree })
  | Some (index, _, _) ->
      (true, { map with tree = Result.get_ok (Measured_tree.insert_at index { key; value } map.tree) })
  | None -> (true, { map with tree = Measured_tree.snoc map.tree { key; value } })

let remove key map =
  match at_least key map with
  | Some (index, _, entry) when compare map entry.key key = 0 ->
      let removed, tree = Result.get_ok (Measured_tree.remove_at index map.tree) in
      Some (removed.value, { map with tree })
  | Some _ | None -> None

let minimum map = Measured_tree.first map.tree
let maximum map = Measured_tree.last map.tree
let floor key map = nth (upper_bound key map - 1) map
let ceiling key map = match at_least key map with Some (_, _, entry) -> Some entry | None -> None
let lower key map = nth (lower_bound key map - 1) map
let higher key map = match above key map with Some (_, _, entry) -> Some entry | None -> None

let range ~start ~count:range_count map =
  if start < 0 || range_count < 0 || start > count map - range_count then
    Error "sorted-map range is out of bounds"
  else
    Ok { map with tree = Measured_tree.take range_count (Measured_tree.drop start map.tree) }

let key_range ~minimum:low ~maximum:high map =
  if compare map low high > 0 then empty map.order
  else
    let first = lower_bound low map in
    let last = upper_bound high map in
    { map with tree = Measured_tree.take (last - first) (Measured_tree.drop first map.tree) }

let to_list map =
  Measured_tree.fold_right (fun entry acc -> (entry.key, entry.value) :: acc) map.tree []

(* Stable sort by key, reject any equivalent pair, then one eager O(m) bottom-up build: O(m log m)
   comparisons in total, where the placement-by-insertion build paid Theta(m^2) copying. *)
let of_list order entries =
  let compare_keys = Common.Comparator.compare order in
  let sorted =
    List.stable_sort (fun (left, _) (right, _) -> compare_keys left right) entries
  in
  let rec check_distinct = function
    | (first, _) :: ((second, _) :: _ as rest) ->
        if compare_keys first second = 0 then
          Error "sorted map already contains an equivalent key"
        else check_distinct rest
    | [] | [ _ ] -> Ok ()
  in
  Result.map
    (fun () ->
      {
        order;
        tree =
          Measured_tree.of_list (make_policy ())
            (List.map (fun (key, value) -> { key; value }) sorted);
      })
    (check_distinct sorted)

module Builder = struct
  type ('key, 'value) map = ('key, 'value) t
  type ('key, 'value) t = { mutable current : ('key, 'value) map }

  let create current = { current }

  let set key value builder =
    let added, successor = set key value builder.current in
    builder.current <- successor;
    added

  let remove key builder =
    match remove key builder.current with
    | None -> None
    | Some (value, successor) ->
        builder.current <- successor;
        Some value

  let freeze builder = builder.current
end
