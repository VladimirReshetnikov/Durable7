(** Strict presence-aware persistent map patches. *)

type 'value state = Absent | Present of 'value
type ('key, 'value) entry = { patch_key : 'key; before : 'value state; after : 'value state }
(** One recorded change: a key with its expected prior state and its intended result. Recording both
    endpoints is what lets a patch be inverted and lets application preflight against a target that
    may have drifted. *)

type ('key, 'value) conflict = {
  conflict_key : 'key;
  expected : 'value state;
  actual : 'value state;
(** Why applying a change was refused. *)

}

type ('key, 'value) composition_conflict = {
  composition_key : 'key;
  left_after : 'value state;
  right_before : 'value state;
(** Why two patches could not be composed. *)

}

type ('key, 'value) t
(** A persistent, invertible record of changes between two map versions. *)

val empty : ?value_equal:('value -> 'value -> bool) -> 'key Common.Hash_policy.t -> ('key, 'value) t
(** The empty collection. *)

val of_list :
  ?value_equal:('value -> 'value -> bool) ->
  'key Common.Hash_policy.t ->
  ('key, 'value) entry list ->
  ('key, 'value) t
(** A collection holding a list's elements, built in bulk rather than by repeated insertion. *)

val between :
  ?value_equal:('value -> 'value -> bool) ->
  ('key, 'value) Persistent_hamt.map ->
  ('key, 'value) Persistent_hamt.map ->
  ('key, 'value) t
(** The patch carrying the source map to the target. *)

val count : ('key, 'value) t -> int
(** Number of elements in the collection. *)

val to_list : ('key, 'value) t -> ('key, 'value) entry list
(** The elements, in the collection's own order. *)

val apply :
  ('key, 'value) t ->
  ('key, 'value) Persistent_hamt.map ->
  (('key, 'value) Persistent_hamt.map, ('key, 'value) conflict) result
(** The map that results from applying the patch, or the conflicts that stopped it. *)

val invert : ('key, 'value) t -> ('key, 'value) t
(** The patch undoing this one. *)

val compose :
  ('key, 'value) t ->
  ('key, 'value) t ->
  (('key, 'value) t, ('key, 'value) composition_conflict) result
(** The patch equivalent to applying the first and then the second. *)
