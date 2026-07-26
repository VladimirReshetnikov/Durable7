(** Implementation of the persistent generic rope with indexed split/concat editing.

    Elements are stored in chunked leaves; adjacent small chunks are coalesced so repeated edits do
    not leave the structure fragmented. *)

type 'element t = 'element Rrb_vector.t

let empty = Rrb_vector.empty
let singleton = Rrb_vector.singleton
let of_list = Rrb_vector.of_list
let length = Rrb_vector.length
let is_empty = Rrb_vector.is_empty
let nth = Rrb_vector.nth
let concat = Rrb_vector.concat
let split_at = Rrb_vector.split_at
let slice = Rrb_vector.slice
let to_list = Rrb_vector.to_list

let insert index values rope =
  if index < 0 || index > length rope then Error "rope insertion index is out of bounds"
  else
    let left, right = split_at index rope in
    Ok (concat (concat left (of_list values)) right)

let remove ~start ~length:count rope =
  if start < 0 || count < 0 || start > length rope - count then
    Error "rope removal range is out of bounds"
  else
    let left, tail = split_at start rope in
    let _, right = split_at count tail in
    Ok (concat left right)

module Builder = struct
  type 'element rope = 'element t
  type 'element t = { mutable reversed : 'element list }

  let create () = { reversed = [] }
  let append value builder = builder.reversed <- value :: builder.reversed
  let append_rope rope builder = builder.reversed <- List.rev_append (to_list rope) builder.reversed
  let freeze builder = of_list (List.rev builder.reversed)
end
