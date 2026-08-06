(** Implementation of the neutral persistent insertion-ordered map with retained key
    representatives.

    The representation is the CHAMP-plus-stamped-sequence composite every sibling workspace ships:
    a CHAMP trie maps each stored key representative to a private integer stamp — expected O(1)
    membership — and a stamp-measured sequence holds the entries in insertion order. A key's
    ordinal position is recovered by one measure-directed descent over that sequence, O(log n).
    Replacing an entry's value rewrites its order slot in place, leaving both its position and its
    key representative alone, which is what distinguishes this from a map rebuilt in iteration
    order. *)

type ('key, 'value) entry = { key : 'key; value : 'value }

type ('key, 'value) t = {
  equality : 'key Common.Hash_policy.t;
  value_equal : 'value -> 'value -> bool;
  order : ('key, 'value) entry Stamped_order.t;
  stamps : ('key, int) Hamt.Persistent_hamt.map;
}

let empty ?(value_equal = ( = )) equality =
  {
    equality;
    value_equal;
    order = Stamped_order.empty (Stamped_order.make_policy ());
    stamps = Hamt.Persistent_hamt.empty ~value_equal:Int.equal equality;
  }

let key_policy map = map.equality
let count map = Stamped_order.length map.order
let is_empty map = count map = 0
let entry_key entry = entry.key
let entry_value entry = entry.value
let mem key map = Hamt.Persistent_hamt.mem key map.stamps

(* The ordinal position of a stamp the membership index vouches for: one measure-directed descent.
   Disagreement between the two indexes is a broken invariant, not a caller error. *)
let index_of_stamp stamp map =
  match Stamped_order.index_of_stamp stamp map.order with
  | Some index -> index
  | None -> invalid_arg "ordered-map membership and order indexes disagree"

let index_of key map =
  Option.map
    (fun indexed -> index_of_stamp (Hamt.Persistent_hamt.entry_value indexed) map)
    (Hamt.Persistent_hamt.find_entry_opt key map.stamps)

let nth index map =
  Option.map (fun entry -> entry.Stamped_order.item) (Stamped_order.nth index map.order)

let find_entry key map = Option.bind (index_of key map) (fun index -> nth index map)
let find_opt key map = Option.map entry_value (find_entry key map)
let first map = nth 0 map
let last map = nth (count map - 1) map

(* Entries in their final order, relabeled with stride-spaced stamps and re-indexed: the
   deterministic full rebuild behind bulk reordering and stamp exhaustion. *)
let build_entries equality value_equal entries =
  let _, stamped =
    List.fold_left
      (fun (stamp, acc) entry ->
        (stamp + Stamped_order.stride, { Stamped_order.stamp; item = entry } :: acc))
      (0, []) entries
  in
  let stamped = List.rev stamped in
  let builder =
    Hamt.Persistent_hamt.Bulk_builder.create
      (Hamt.Persistent_hamt.empty ~value_equal:Int.equal equality)
  in
  List.iter
    (fun entry ->
      Hamt.Persistent_hamt.Bulk_builder.set entry.Stamped_order.item.key entry.Stamped_order.stamp
        builder)
    stamped;
  {
    equality;
    value_equal;
    order = Stamped_order.of_entries (Stamped_order.make_policy ()) stamped;
    stamps = Hamt.Persistent_hamt.Bulk_builder.freeze builder;
  }

let entries_in_order map =
  List.map (fun entry -> entry.Stamped_order.item) (Stamped_order.to_list map.order)

let insert index key value map =
  if index < 0 || index > count map then Error "ordered-map insertion index is out of bounds"
  else if mem key map then Error "ordered map already contains an equivalent key"
  else
    match Stamped_order.pick_stamp index map.order with
    | Some stamp ->
        let order =
          Result.get_ok
            (Stamped_order.insert_at index { Stamped_order.stamp; item = { key; value } } map.order)
        in
        Ok { map with order; stamps = Hamt.Persistent_hamt.add key stamp map.stamps }
    | None ->
        let rec splice position = function
          | rest when position = 0 -> { key; value } :: rest
          | [] -> [ { key; value } ]
          | head :: rest -> head :: splice (position - 1) rest
        in
        Ok (build_entries map.equality map.value_equal (splice index (entries_in_order map)))

let add key value map = insert (count map) key value map

let set key value map =
  match Hamt.Persistent_hamt.find_entry_opt key map.stamps with
  | None -> (true, Result.get_ok (add key value map))
  | Some indexed ->
      let stamp = Hamt.Persistent_hamt.entry_value indexed in
      let index = index_of_stamp stamp map in
      let stored =
        match Stamped_order.nth index map.order with
        | Some stored -> stored
        | None -> invalid_arg "ordered-map membership and order indexes disagree"
      in
      if map.value_equal stored.Stamped_order.item.value value then (false, map)
      else
        let replacement =
          { Stamped_order.stamp; item = { key = stored.Stamped_order.item.key; value } }
        in
        ( false,
          { map with order = Result.get_ok (Stamped_order.update_at index replacement map.order) }
        )

let of_list ?(value_equal = ( = )) equality entries =
  List.fold_left
    (fun map (key, value) -> snd (set key value map))
    (empty ~value_equal equality) entries

let remove_at index map =
  if index < 0 || index >= count map then Error "ordered-map removal index is out of bounds"
  else
    let removed, order = Result.get_ok (Stamped_order.remove_at index map.order) in
    Ok
      ( removed.Stamped_order.item,
        {
          map with
          order;
          stamps = Hamt.Persistent_hamt.remove removed.Stamped_order.item.key map.stamps;
        } )

let remove key map =
  match Hamt.Persistent_hamt.remove_entry key map.stamps with
  | _, None -> None
  | stamps, Some indexed ->
      let index = index_of_stamp (Hamt.Persistent_hamt.entry_value indexed) map in
      let removed, order = Result.get_ok (Stamped_order.remove_at index map.order) in
      Some (removed.Stamped_order.item, { map with order; stamps })

let move_to index key map =
  if index < 0 || index >= count map then Error "ordered-map movement index is out of bounds"
  else
    match Hamt.Persistent_hamt.find_entry_opt key map.stamps with
    | None -> Ok map
    | Some indexed ->
        let current = index_of_stamp (Hamt.Persistent_hamt.entry_value indexed) map in
        if current = index then Ok map
        else
          let stored, trimmed = Result.get_ok (Stamped_order.remove_at current map.order) in
          let entry = stored.Stamped_order.item in
          (match Stamped_order.pick_stamp index trimmed with
          | Some stamp ->
              let order =
                Result.get_ok
                  (Stamped_order.insert_at index { Stamped_order.stamp; item = entry } trimmed)
              in
              Ok { map with order; stamps = Hamt.Persistent_hamt.set entry.key stamp map.stamps }
          | None ->
              let rec splice position = function
                | rest when position = 0 -> entry :: rest
                | [] -> [ entry ]
                | head :: rest -> head :: splice (position - 1) rest
              in
              let items =
                List.map (fun other -> other.Stamped_order.item) (Stamped_order.to_list trimmed)
              in
              Ok (build_entries map.equality map.value_equal (splice index items)))

let move_to_first key map = if is_empty map then map else Result.get_ok (move_to 0 key map)

let move_to_last key map =
  if is_empty map then map else Result.get_ok (move_to (count map - 1) key map)

let range ~start ~count:range_count map =
  if start < 0 || range_count < 0 || start > count map - range_count then
    Error "ordered-map range is out of bounds"
  else
    let kept = Stamped_order.sub ~start ~count:range_count map.order in
    let dropped = count map - range_count in
    let stamps =
      if range_count <= dropped then begin
        let builder =
          Hamt.Persistent_hamt.Bulk_builder.create
            (Hamt.Persistent_hamt.empty ~value_equal:Int.equal map.equality)
        in
        Stamped_order.fold_left
          (fun () entry ->
            Hamt.Persistent_hamt.Bulk_builder.set entry.Stamped_order.item.key
              entry.Stamped_order.stamp builder)
          () kept;
        Hamt.Persistent_hamt.Bulk_builder.freeze builder
      end
      else begin
        let before = Stamped_order.sub ~start:0 ~count:start map.order in
        let after =
          Stamped_order.sub ~start:(start + range_count)
            ~count:(count map - start - range_count)
            map.order
        in
        let stamps =
          Stamped_order.fold_left
            (fun stamps entry -> Hamt.Persistent_hamt.remove entry.Stamped_order.item.key stamps)
            map.stamps before
        in
        Stamped_order.fold_left
          (fun stamps entry -> Hamt.Persistent_hamt.remove entry.Stamped_order.item.key stamps)
          stamps after
      end
    in
    Ok { map with order = kept; stamps }

let reverse map =
  if count map <= 1 then map
  else build_entries map.equality map.value_equal (List.rev (entries_in_order map))

let sort_by_key comparator map =
  let compare = Common.Comparator.compare comparator in
  let sorted =
    List.stable_sort
      (fun left right -> compare left.Stamped_order.item.key right.Stamped_order.item.key)
      (Stamped_order.to_list map.order)
  in
  build_entries map.equality map.value_equal
    (List.map (fun entry -> entry.Stamped_order.item) sorted)

let to_list map = List.map (fun entry -> (entry.key, entry.value)) (entries_in_order map)
