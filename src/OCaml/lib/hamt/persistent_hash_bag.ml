(** Implementation of the persistent multiset with positive per-class multiplicities. *)

type 'element entry = { element : 'element; multiplicity : int }
type 'element t = { counts : ('element, int) Persistent_hamt.map; expanded_count : int64 }

let empty policy =
  { counts = Persistent_hamt.empty ~value_equal:Int.equal policy; expanded_count = 0L }

let distinct_count bag = Persistent_hamt.count bag.counts
let expanded_count bag = bag.expanded_count
let multiplicity element bag = Option.value ~default:0 (Persistent_hamt.find_opt element bag.counts)
let validate_count count = if count <= 0 then invalid_arg "multiplicity must be positive"

let checked_multiplicity left right =
  if left > Int32.to_int Int32.max_int - right then invalid_arg "multiplicity overflow";
  left + right

let add ?(count = 1) element bag =
  validate_count count;
  let existing = multiplicity element bag in
  let next = checked_multiplicity existing count in
  {
    counts = Persistent_hamt.set element next bag.counts;
    expanded_count = Int64.add bag.expanded_count (Int64.of_int count);
  }

let remove ?(count = 1) element bag =
  validate_count count;
  let existing = multiplicity element bag in
  if existing = 0 then bag
  else
    let removed = min existing count in
    let counts =
      if removed = existing then Persistent_hamt.remove element bag.counts
      else Persistent_hamt.set element (existing - removed) bag.counts
    in
    { counts; expanded_count = Int64.sub bag.expanded_count (Int64.of_int removed) }

let to_list bag =
  List.map
    (fun (element, multiplicity) -> { element; multiplicity })
    (Persistent_hamt.to_list bag.counts)

let combine operation left right =
  let keys =
    List.fold_left
      (fun result entry -> Persistent_hash_set.add entry.element result)
      (Persistent_hash_set.empty (Persistent_hamt.policy left.counts))
      (to_list left @ to_list right)
  in
  List.fold_left
    (fun result element ->
      let multiplicity = operation (multiplicity element left) (multiplicity element right) in
      if multiplicity = 0 then result else add ~count:multiplicity element result)
    (empty (Persistent_hamt.policy left.counts))
    (Persistent_hash_set.to_list keys)

let sum left right = combine checked_multiplicity left right
let union left right = combine max left right
let inter left right = combine min left right

let diff left right =
  combine (fun left_count right_count -> max 0 (left_count - right_count)) left right

let equal left right =
  left.expanded_count = right.expanded_count && Persistent_hamt.equal left.counts right.counts
