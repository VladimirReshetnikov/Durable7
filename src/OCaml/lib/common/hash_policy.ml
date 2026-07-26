(** Implementation of the runtime hashing and equality policy retained by persistent hash
    collections. *)

type 'a t = { hash : 'a -> int; equal : 'a -> 'a -> bool }

let create ~hash ~equal = { hash; equal }
let default () = create ~hash:Hashtbl.hash ~equal:( = )
let hash policy value = policy.hash value
let equal policy left right = policy.equal left right
let same left right = left == right
