(** Implementation of the neutral persistent insertion-ordered set with explicit positional
    movement.

    The representation is the CHAMP-plus-stamped-sequence composite every sibling workspace ships:
    a CHAMP trie maps each stored representative to a private integer stamp — expected O(1)
    membership through the hashed index — and a stamp-measured sequence holds the representatives
    in insertion order, where stamps rise strictly from front to back. A stored element's ordinal
    position is recovered by one measure-directed descent over that sequence, O(log n), never by
    scanning either index for the other. Stamps are issued with a sparse stride so positional
    insertion labels between its neighbours; when a gap is exhausted, the whole collection relabels
    deterministically. *)

type 'element t = {
  equality : 'element Common.Hash_policy.t;
  order : 'element Stamped_order.t;
  stamps : ('element, int) Hamt.Persistent_hamt.map;
}

let empty equality =
  {
    equality;
    order = Stamped_order.empty (Stamped_order.make_policy ());
    stamps = Hamt.Persistent_hamt.empty ~value_equal:Int.equal equality;
  }

let policy set = set.equality
let count set = Stamped_order.length set.order
let is_empty set = count set = 0

(* Distinct elements in their final order, relabeled with stride-spaced stamps and re-indexed:
   the deterministic full rebuild behind bulk construction, reordering, and stamp exhaustion. *)
let build_distinct equality values =
  let _, entries =
    List.fold_left
      (fun (stamp, acc) value ->
        (stamp + Stamped_order.stride, { Stamped_order.stamp; item = value } :: acc))
      (0, []) values
  in
  let entries = List.rev entries in
  let builder =
    Hamt.Persistent_hamt.Bulk_builder.create
      (Hamt.Persistent_hamt.empty ~value_equal:Int.equal equality)
  in
  List.iter
    (fun entry ->
      Hamt.Persistent_hamt.Bulk_builder.set entry.Stamped_order.item entry.Stamped_order.stamp
        builder)
    entries;
  {
    equality;
    order = Stamped_order.of_entries (Stamped_order.make_policy ()) entries;
    stamps = Hamt.Persistent_hamt.Bulk_builder.freeze builder;
  }

let of_list equality values =
  let _, distinct =
    List.fold_left
      (fun (seen, acc) value ->
        let seen, added = Hamt.Persistent_hamt.try_add value () seen in
        if added then (seen, value :: acc) else (seen, acc))
      (Hamt.Persistent_hamt.empty equality, [])
      values
  in
  build_distinct equality (List.rev distinct)

let mem value set = Hamt.Persistent_hamt.mem value set.stamps

let find value set =
  Option.map Hamt.Persistent_hamt.entry_key (Hamt.Persistent_hamt.find_entry_opt value set.stamps)

let nth index set =
  Option.map (fun entry -> entry.Stamped_order.item) (Stamped_order.nth index set.order)

let first set = nth 0 set
let last set = nth (count set - 1) set

(* The ordinal position of a stamp the membership index vouches for: one measure-directed descent.
   Disagreement between the two indexes is a broken invariant, not a caller error. *)
let index_of_stamp stamp set =
  match Stamped_order.index_of_stamp stamp set.order with
  | Some index -> index
  | None -> invalid_arg "ordered-set membership and order indexes disagree"

let index_of value set =
  Option.map
    (fun entry -> index_of_stamp (Hamt.Persistent_hamt.entry_value entry) set)
    (Hamt.Persistent_hamt.find_entry_opt value set.stamps)

let to_list set = List.map (fun entry -> entry.Stamped_order.item) (Stamped_order.to_list set.order)

(* Relabel everything after splicing the value into the order at the gap: the deterministic full
   relabel taken when the gap's stamps are exhausted. *)
let rebuild_inserted index value set =
  let items = to_list set in
  let rec splice position = function
    | rest when position = 0 -> value :: rest
    | [] -> [ value ]
    | head :: rest -> head :: splice (position - 1) rest
  in
  build_distinct set.equality (splice index items)

let insert index value set =
  if index < 0 || index > count set then Error "ordered-set insertion index is out of bounds"
  else if mem value set then Ok (false, set)
  else
    match Stamped_order.pick_stamp index set.order with
    | Some stamp ->
        let order =
          Result.get_ok (Stamped_order.insert_at index { Stamped_order.stamp; item = value } set.order)
        in
        Ok (true, { set with order; stamps = Hamt.Persistent_hamt.add value stamp set.stamps })
    | None -> Ok (true, rebuild_inserted index value set)

let add value set = Result.get_ok (insert (count set) value set)
let add_first value set = Result.get_ok (insert 0 value set)

let remove_at index set =
  if index < 0 || index >= count set then Error "ordered-set removal index is out of bounds"
  else
    let removed, order = Result.get_ok (Stamped_order.remove_at index set.order) in
    Ok
      ( removed.Stamped_order.item,
        {
          set with
          order;
          stamps = Hamt.Persistent_hamt.remove removed.Stamped_order.item set.stamps;
        } )

let remove value set =
  match Hamt.Persistent_hamt.remove_entry value set.stamps with
  | _, None -> (false, set)
  | stamps, Some entry ->
      let index = index_of_stamp (Hamt.Persistent_hamt.entry_value entry) set in
      let _, order = Result.get_ok (Stamped_order.remove_at index set.order) in
      (true, { set with order; stamps })

let move_to index value set =
  if index < 0 || index >= count set then Error "ordered-set movement index is out of bounds"
  else
    match Hamt.Persistent_hamt.find_entry_opt value set.stamps with
    | None -> Ok set
    | Some indexed ->
        let current = index_of_stamp (Hamt.Persistent_hamt.entry_value indexed) set in
        if current = index then Ok set
        else
          let stored, trimmed = Result.get_ok (Stamped_order.remove_at current set.order) in
          let representative = stored.Stamped_order.item in
          (match Stamped_order.pick_stamp index trimmed with
          | Some stamp ->
              let order =
                Result.get_ok
                  (Stamped_order.insert_at index
                     { Stamped_order.stamp; item = representative }
                     trimmed)
              in
              Ok
                {
                  set with
                  order;
                  stamps = Hamt.Persistent_hamt.set representative stamp set.stamps;
                }
          | None ->
              let items =
                List.map
                  (fun entry -> entry.Stamped_order.item)
                  (Stamped_order.to_list trimmed)
              in
              let rec splice position = function
                | rest when position = 0 -> representative :: rest
                | [] -> [ representative ]
                | head :: rest -> head :: splice (position - 1) rest
              in
              Ok (build_distinct set.equality (splice index items)))

let move_to_first value set = if is_empty set then set else Result.get_ok (move_to 0 value set)

let move_to_last value set =
  if is_empty set then set else Result.get_ok (move_to (count set - 1) value set)

let range ~start ~count:range_count set =
  if start < 0 || range_count < 0 || start > count set - range_count then
    Error "ordered-set range is out of bounds"
  else
    let kept = Stamped_order.sub ~start ~count:range_count set.order in
    let dropped = count set - range_count in
    let stamps =
      if range_count <= dropped then begin
        (* Rebuilding the smaller side: index the kept entries afresh. *)
        let builder =
          Hamt.Persistent_hamt.Bulk_builder.create
            (Hamt.Persistent_hamt.empty ~value_equal:Int.equal set.equality)
        in
        Stamped_order.fold_left
          (fun () entry ->
            Hamt.Persistent_hamt.Bulk_builder.set entry.Stamped_order.item
              entry.Stamped_order.stamp builder)
          () kept;
        Hamt.Persistent_hamt.Bulk_builder.freeze builder
      end
      else begin
        (* Removing the smaller side: prune the discarded entries from the shared index. *)
        let before = Stamped_order.sub ~start:0 ~count:start set.order in
        let after =
          Stamped_order.sub ~start:(start + range_count)
            ~count:(count set - start - range_count)
            set.order
        in
        let stamps =
          Stamped_order.fold_left
            (fun stamps entry -> Hamt.Persistent_hamt.remove entry.Stamped_order.item stamps)
            set.stamps before
        in
        Stamped_order.fold_left
          (fun stamps entry -> Hamt.Persistent_hamt.remove entry.Stamped_order.item stamps)
          stamps after
      end
    in
    Ok { set with order = kept; stamps }

let take range_count set = range ~start:0 ~count:range_count set
let drop range_count set = range ~start:range_count ~count:(count set - range_count) set

let reverse set = if count set <= 1 then set else build_distinct set.equality (List.rev (to_list set))

let sort comparator set =
  let compare = Common.Comparator.compare comparator in
  let sorted =
    List.stable_sort
      (fun left right -> compare left.Stamped_order.item right.Stamped_order.item)
      (Stamped_order.to_list set.order)
  in
  build_distinct set.equality (List.map (fun entry -> entry.Stamped_order.item) sorted)

let normalize set values = of_list set.equality values

let union set values =
  List.fold_left (fun result value -> snd (add value result)) set (to_list (normalize set values))

let intersect set values =
  let argument = normalize set values in
  of_list set.equality (List.filter (fun value -> mem value argument) (to_list set))

let difference set values =
  let argument = normalize set values in
  of_list set.equality (List.filter (fun value -> not (mem value argument)) (to_list set))

let symmetric_difference set values =
  let argument = normalize set values in
  let receiver_only = List.filter (fun value -> not (mem value argument)) (to_list set) in
  let argument_only = List.filter (fun value -> not (mem value set)) (to_list argument) in
  of_list set.equality (receiver_only @ argument_only)
