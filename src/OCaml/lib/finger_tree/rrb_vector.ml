type 'element t = 'element Persistent_deque.t
type statistics = { count : int; estimated_leaves : int; depth : int }

let empty = Persistent_deque.empty
let singleton = Persistent_deque.singleton
let of_list = Persistent_deque.of_list
let length = Persistent_deque.length
let is_empty = Persistent_deque.is_empty
let nth = Persistent_deque.nth
let set index value vector = Persistent_deque.update_at index (fun _ -> value) vector
let append value vector = Persistent_deque.snoc vector value
let prepend = Persistent_deque.cons
let concat = Persistent_deque.concat
let insert = Persistent_deque.insert_at
let remove = Persistent_deque.remove_at
let pop = Persistent_deque.pop_back
let split_at = Persistent_deque.split_at
let slice = Persistent_deque.slice
let to_list = Persistent_deque.to_list

let statistics vector =
  let count = length vector in
  let estimated_leaves = if count = 0 then 0 else (count + 31) / 32 in
  let rec depth nodes result =
    if nodes <= 1 then result else depth ((nodes + 31) / 32) (result + 1)
  in
  { count; estimated_leaves; depth = depth estimated_leaves (if count = 0 then 0 else 1) }

module Builder = struct
  type 'element vector = 'element t
  type 'element t = { mutable current : 'element vector }

  let create current = { current }
  let length builder = length builder.current
  let append value builder = builder.current <- append value builder.current

  let set index value builder =
    Result.map (fun successor -> builder.current <- successor) (set index value builder.current)

  let remove index builder =
    Result.map
      (fun (value, successor) ->
        builder.current <- successor;
        value)
      (remove index builder.current)

  let freeze builder = builder.current
end
