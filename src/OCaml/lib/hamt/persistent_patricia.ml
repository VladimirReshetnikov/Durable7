module type Key = sig
  type t

  val equal : t -> t -> bool
  val to_bits : t -> int64
end

module type Map = sig
  type key
  type 'value t

  val empty : 'value t
  val count : 'value t -> int
  val is_empty : 'value t -> bool
  val mem : key -> 'value t -> bool
  val find_opt : key -> 'value t -> 'value option
  val add : key -> 'value -> 'value t -> 'value t
  val set : key -> 'value -> 'value t -> 'value t
  val remove : key -> 'value t -> 'value t
  val to_list : 'value t -> (key * 'value) list
end

module Make_map (Key : Key) : Map with type key = Key.t = struct
  type key = Key.t

  type 'value tree =
    | Empty
    | Leaf of key * 'value
    | Branch of int64 * int64 * 'value tree * 'value tree

  type 'value t = { root : 'value tree; count : int }

  let empty = { root = Empty; count = 0 }
  let count map = map.count
  let is_empty map = map.count = 0
  let zero_bit key mask = Int64.equal (Int64.logand key mask) 0L

  let highest_bit value =
    let rec loop position =
      if position < 0 then invalid_arg "Patricia keys must differ"
      else
        let candidate = Int64.shift_left 1L position in
        if Int64.equal (Int64.logand value candidate) 0L then loop (position - 1) else candidate
    in
    loop 63

  let prefix_of key mask =
    let lower_and_branch = Int64.pred (Int64.shift_left mask 1) in
    Int64.logand key (Int64.lognot lower_and_branch)

  let matches key prefix mask = Int64.equal (prefix_of key mask) prefix

  let join left_key left right_key right =
    let mask = highest_bit (Int64.logxor left_key right_key) in
    let prefix = prefix_of left_key mask in
    if zero_bit left_key mask then Branch (prefix, mask, left, right)
    else Branch (prefix, mask, right, left)

  let rec find_tree key bits = function
    | Empty -> None
    | Leaf (stored_key, value) -> if Key.equal key stored_key then Some value else None
    | Branch (prefix, mask, left, right) ->
        if not (matches bits prefix mask) then None
        else find_tree key bits (if zero_bit bits mask then left else right)

  let find_opt key map = find_tree key (Key.to_bits key) map.root
  let mem key map = Option.is_some (find_opt key map)

  let rec set_tree key value bits replace = function
    | Empty -> (Leaf (key, value), true)
    | Leaf (stored_key, stored_value) as leaf ->
        if Key.equal key stored_key then
          if replace && stored_value != value then (Leaf (stored_key, value), false)
          else (leaf, false)
        else (join bits (Leaf (key, value)) (Key.to_bits stored_key) leaf, true)
    | Branch (prefix, mask, left, right) as branch ->
        if matches bits prefix mask then
          if zero_bit bits mask then
            let successor, added = set_tree key value bits replace left in
            if successor == left then (branch, added)
            else (Branch (prefix, mask, successor, right), added)
          else
            let successor, added = set_tree key value bits replace right in
            if successor == right then (branch, added)
            else (Branch (prefix, mask, left, successor), added)
        else (join bits (Leaf (key, value)) prefix branch, true)

  let update replace key value map =
    let root, added = set_tree key value (Key.to_bits key) replace map.root in
    ({ root; count = (if added then map.count + 1 else map.count) }, added)

  let set key value map = fst (update true key value map)

  let add key value map =
    let successor, added = update false key value map in
    if added then successor else raise Persistent_hamt.Duplicate_key

  let rec remove_tree key bits = function
    | Empty as empty -> (empty, false)
    | Leaf (stored_key, _) as leaf ->
        if Key.equal key stored_key then (Empty, true) else (leaf, false)
    | Branch (prefix, mask, left, right) as branch -> (
        if not (matches bits prefix mask) then (branch, false)
        else if zero_bit bits mask then
          let successor, removed = remove_tree key bits left in
          if not removed then (branch, false)
          else
            match successor with
            | Empty -> (right, true)
            | Leaf _ | Branch _ -> (Branch (prefix, mask, successor, right), true)
        else
          let successor, removed = remove_tree key bits right in
          if not removed then (branch, false)
          else
            match successor with
            | Empty -> (left, true)
            | Leaf _ | Branch _ -> (Branch (prefix, mask, left, successor), true))

  let remove key map =
    let root, removed = remove_tree key (Key.to_bits key) map.root in
    if removed then { root; count = map.count - 1 } else map

  let rec collect result = function
    | Empty -> result
    | Leaf (key, value) -> (key, value) :: result
    | Branch (_, _, left, right) -> collect (collect result right) left

  let to_list map = collect [] map.root
end

module Int32_map = Make_map (struct
  type t = int32

  let equal = Int32.equal
  let to_bits value = Int64.logand (Int64.of_int32 value) 0xffff_ffffL
end)

module Int64_map = Make_map (struct
  type t = int64

  let equal = Int64.equal
  let to_bits value = value
end)

module type Set = sig
  type element
  type t

  val empty : t
  val count : t -> int
  val mem : element -> t -> bool
  val add : element -> t -> t
  val remove : element -> t -> t
  val to_list : t -> element list
end

module Make_set (Map : Map) : Set with type element = Map.key = struct
  type element = Map.key
  type t = unit Map.t

  let empty = Map.empty
  let count = Map.count
  let mem = Map.mem
  let add element set = Map.set element () set
  let remove = Map.remove
  let to_list set = List.map fst (Map.to_list set)
end

module Int32_set = Make_set (Int32_map)
module Int64_set = Make_set (Int64_map)
