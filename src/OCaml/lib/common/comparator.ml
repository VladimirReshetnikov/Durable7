(** Implementation of the runtime ordering policy retained by sorted and ordered collections. *)

type 'a t = { compare : 'a -> 'a -> int }

let create compare = { compare }
let default () = create Stdlib.compare
let compare comparator left right = comparator.compare left right
let reverse comparator = create (fun left right -> comparator.compare right left)
let same left right = left == right
