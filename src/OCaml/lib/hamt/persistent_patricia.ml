(** Implementation of the persistent big-endian Patricia maps and sets for fixed-width integer keys.

    Branch nodes store a common prefix and a discriminating bit, so a lookup is bounded by the key
    width rather than by the entry count. *)

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

  module Cursor : sig
    type 'value cursor

    val start : 'value t -> 'value cursor
    val at : int -> 'value t -> 'value cursor option
    val at_end : 'value t -> 'value cursor
    val lower_bound : key -> 'value t -> 'value cursor
    val upper_bound : key -> 'value t -> 'value cursor
    val exact : key -> 'value t -> 'value cursor * bool
    val count : 'value cursor -> int
    val position : 'value cursor -> int
    val is_at_start : 'value cursor -> bool
    val is_at_end : 'value cursor -> bool
    val peek_previous : 'value cursor -> (key * 'value) option
    val peek_next : 'value cursor -> (key * 'value) option
    val move_previous : 'value cursor -> 'value cursor option
    val move_next : 'value cursor -> 'value cursor option
    val seek : int -> 'value cursor -> 'value cursor option
    val insert : key -> 'value -> 'value cursor -> 'value cursor option
    val set : key -> 'value -> 'value cursor -> 'value cursor option
    val set_next_value : 'value -> 'value cursor -> 'value cursor option
    val delete_previous : 'value cursor -> 'value cursor option
    val delete_next : 'value cursor -> 'value cursor option
    val snapshot : 'value cursor -> 'value t
  end
end

module Make_map (Key : Key) : Map with type key = Key.t = struct
  type key = Key.t

  type 'value tree =
    | Empty
    | Leaf of key * 'value
    | Branch of int * int64 * int64 * 'value tree * 'value tree

  type 'value t = { root : 'value tree; count : int }

  let empty = { root = Empty; count = 0 }
  let count map = map.count
  let is_empty map = map.count = 0
  let zero_bit key mask = Int64.equal (Int64.logand key mask) 0L
  let tree_count = function Empty -> 0 | Leaf _ -> 1 | Branch (count, _, _, _, _) -> count

  let make_branch prefix mask left right =
    Branch (tree_count left + tree_count right, prefix, mask, left, right)

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
    if zero_bit left_key mask then make_branch prefix mask left right
    else make_branch prefix mask right left

  let rec find_tree key bits = function
    | Empty -> None
    | Leaf (stored_key, value) -> if Key.equal key stored_key then Some value else None
    | Branch (_, prefix, mask, left, right) ->
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
    | Branch (_, prefix, mask, left, right) as branch ->
        if matches bits prefix mask then
          if zero_bit bits mask then
            let successor, added = set_tree key value bits replace left in
            if successor == left then (branch, added)
            else (make_branch prefix mask successor right, added)
          else
            let successor, added = set_tree key value bits replace right in
            if successor == right then (branch, added)
            else (make_branch prefix mask left successor, added)
        else (join bits (Leaf (key, value)) prefix branch, true)

  let update replace key value map =
    let root, added = set_tree key value (Key.to_bits key) replace map.root in
    if root == map.root then (map, added)
    else ({ root; count = (if added then map.count + 1 else map.count) }, added)

  let set key value map = fst (update true key value map)

  let add key value map =
    let successor, added = update false key value map in
    if added then successor else raise Persistent_hamt.Duplicate_key

  let rec remove_tree key bits = function
    | Empty as empty -> (empty, false)
    | Leaf (stored_key, _) as leaf ->
        if Key.equal key stored_key then (Empty, true) else (leaf, false)
    | Branch (_, prefix, mask, left, right) as branch -> (
        if not (matches bits prefix mask) then (branch, false)
        else if zero_bit bits mask then
          let successor, removed = remove_tree key bits left in
          if not removed then (branch, false)
          else
            match successor with
            | Empty -> (right, true)
            | Leaf _ | Branch _ -> (make_branch prefix mask successor right, true)
        else
          let successor, removed = remove_tree key bits right in
          if not removed then (branch, false)
          else
            match successor with
            | Empty -> (left, true)
            | Leaf _ | Branch _ -> (make_branch prefix mask left successor, true))

  let remove key map =
    let root, removed = remove_tree key (Key.to_bits key) map.root in
    if removed then { root; count = map.count - 1 } else map

  let rec collect result = function
    | Empty -> result
    | Leaf (key, value) -> (key, value) :: result
    | Branch (_, _, _, left, right) -> collect (collect result right) left

  let to_list map = collect [] map.root

  let rec entry_at_tree index = function
    | Empty -> None
    | Leaf (key, value) -> if index = 0 then Some (key, value) else None
    | Branch (_, _, _, left, right) ->
        let left_count = tree_count left in
        if index < left_count then entry_at_tree index left
        else entry_at_tree (index - left_count) right

  let entry_at index map =
    if index < 0 || index >= map.count then None else entry_at_tree index map.root

  let lower_bound_rank key map =
    let bits = Key.to_bits key in
    let rec loop rank = function
      | Empty -> (0, false)
      | Leaf (stored_key, _) ->
          let stored_bits = Key.to_bits stored_key in
          if Int64.unsigned_compare stored_bits bits < 0 then (rank + 1, false)
          else (rank, Key.equal key stored_key)
      | Branch (branch_count, prefix, mask, left, right) ->
          if not (matches bits prefix mask) then
            if Int64.unsigned_compare bits prefix < 0 then (rank, false)
            else (rank + branch_count, false)
          else if zero_bit bits mask then loop rank left
          else loop (rank + tree_count left) right
    in
    loop 0 map.root

  let cursor_map_add = add
  let cursor_map_set = set
  let cursor_map_remove = remove

  module Cursor = struct
    type 'value cursor = { map : 'value t; position : int }

    let start map = { map; position = 0 }

    let at position map =
      if position < 0 || position > map.count then None else Some { map; position }

    let at_end map = { map; position = map.count }

    let lower_bound key map =
      let position, _ = lower_bound_rank key map in
      { map; position }

    let upper_bound key map =
      let position, found = lower_bound_rank key map in
      { map; position = (position + if found then 1 else 0) }

    let exact key map =
      let position, found = lower_bound_rank key map in
      ({ map; position }, found)

    let count cursor = cursor.map.count
    let position cursor = cursor.position
    let is_at_start cursor = cursor.position = 0
    let is_at_end cursor = cursor.position = cursor.map.count

    let peek_previous cursor =
      if is_at_start cursor then None else entry_at (cursor.position - 1) cursor.map

    let peek_next cursor = entry_at cursor.position cursor.map

    let move_previous cursor =
      if is_at_start cursor then None else Some { cursor with position = cursor.position - 1 }

    let move_next cursor =
      if is_at_end cursor then None else Some { cursor with position = cursor.position + 1 }

    let seek position cursor =
      if position < 0 || position > cursor.map.count then None
      else if position = cursor.position then Some cursor
      else Some { cursor with position }

    let insert key value cursor =
      let expected, found = lower_bound_rank key cursor.map in
      if found || expected <> cursor.position then None
      else Some { map = cursor_map_add key value cursor.map; position = cursor.position + 1 }

    let set key value cursor =
      let expected, found = lower_bound_rank key cursor.map in
      if expected <> cursor.position then None
      else
        let map = cursor_map_set key value cursor.map in
        Some { map; position = (cursor.position + if found then 0 else 1) }

    let set_next_value value cursor =
      match peek_next cursor with
      | None -> None
      | Some (key, _) -> Some { cursor with map = cursor_map_set key value cursor.map }

    let delete_previous cursor =
      match peek_previous cursor with
      | None -> None
      | Some (key, _) ->
          Some { map = cursor_map_remove key cursor.map; position = cursor.position - 1 }

    let delete_next cursor =
      match peek_next cursor with
      | None -> None
      | Some (key, _) -> Some { cursor with map = cursor_map_remove key cursor.map }

    let snapshot cursor = cursor.map
  end
end

module Int32_map = Make_map (struct
  type t = int32

  let equal = Int32.equal
  let to_bits value = Int64.logxor (Int64.logand (Int64.of_int32 value) 0xffff_ffffL) 0x8000_0000L
end)

module Int64_map = Make_map (struct
  type t = int64

  let equal = Int64.equal
  let to_bits value = Int64.logxor value Int64.min_int
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

  module Cursor : sig
    type cursor

    val start : t -> cursor
    val at : int -> t -> cursor option
    val at_end : t -> cursor
    val lower_bound : element -> t -> cursor
    val upper_bound : element -> t -> cursor
    val exact : element -> t -> cursor * bool
    val count : cursor -> int
    val position : cursor -> int
    val is_at_start : cursor -> bool
    val is_at_end : cursor -> bool
    val peek_previous : cursor -> element option
    val peek_next : cursor -> element option
    val move_previous : cursor -> cursor option
    val move_next : cursor -> cursor option
    val seek : int -> cursor -> cursor option
    val add : element -> cursor -> cursor option
    val delete_previous : cursor -> cursor option
    val delete_next : cursor -> cursor option
    val snapshot : cursor -> t
  end
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

  module Cursor = struct
    type cursor = unit Map.Cursor.cursor

    let start = Map.Cursor.start
    let at = Map.Cursor.at
    let at_end = Map.Cursor.at_end
    let lower_bound = Map.Cursor.lower_bound
    let upper_bound = Map.Cursor.upper_bound
    let exact = Map.Cursor.exact
    let count = Map.Cursor.count
    let position = Map.Cursor.position
    let is_at_start = Map.Cursor.is_at_start
    let is_at_end = Map.Cursor.is_at_end
    let peek_previous cursor = Option.map fst (Map.Cursor.peek_previous cursor)
    let peek_next cursor = Option.map fst (Map.Cursor.peek_next cursor)
    let move_previous = Map.Cursor.move_previous
    let move_next = Map.Cursor.move_next
    let seek = Map.Cursor.seek
    let add element cursor = Map.Cursor.set element () cursor
    let delete_previous = Map.Cursor.delete_previous
    let delete_next = Map.Cursor.delete_next
    let snapshot = Map.Cursor.snapshot
  end
end

module Int32_set = Make_set (Int32_map)
module Int64_set = Make_set (Int64_map)
