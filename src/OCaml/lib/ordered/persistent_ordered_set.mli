(** Neutral persistent insertion-ordered set with explicit positional movement. *)

type 'element t

val empty : 'element Common.Hash_policy.t -> 'element t
(** The empty collection. *)

val of_list : 'element Common.Hash_policy.t -> 'element list -> 'element t
(** A collection holding a list's elements, built in bulk rather than by repeated insertion. *)

val policy : 'element t -> 'element Common.Hash_policy.t
(** The policy the collection retains. *)

val count : 'element t -> int
(** Number of elements in the collection. *)

val is_empty : 'element t -> bool
(** Whether the collection holds no elements. *)

val mem : 'element -> 'element t -> bool
(** Whether the element is present. *)

val find : 'element -> 'element t -> 'element option
(** The element matching the probe. Raises when absent. *)

val nth : int -> 'element t -> 'element option
(** The element at the given rank. *)

val index_of : 'element -> 'element t -> int option
(** The rank of the given element. *)

val first : 'element t -> 'element option
(** The first element. *)

val last : 'element t -> 'element option
(** The last element. *)

val add : 'element -> 'element t -> bool * 'element t
(** A collection containing the given element. *)

val add_first : 'element -> 'element t -> bool * 'element t
(** A collection with the element placed first in insertion order. *)

val insert : int -> 'element -> 'element t -> (bool * 'element t, string) result
(** A collection containing the given element. *)

val move_to : int -> 'element -> 'element t -> ('element t, string) result
(** A collection with the element moved to the given position in the insertion order. *)

val move_to_first : 'element -> 'element t -> 'element t
(** A collection with the element moved to the front of the insertion order. *)

val move_to_last : 'element -> 'element t -> 'element t
(** A collection with the element moved to the end of the insertion order. *)

val remove : 'element -> 'element t -> bool * 'element t
(** A collection without that element. *)

val remove_at : int -> 'element t -> ('element * 'element t, string) result
(** A collection without the element at the position. *)

val range : start:int -> count:int -> 'element t -> ('element t, string) result
(** The elements in the given range. *)

val take : int -> 'element t -> ('element t, string) result
(** The first n elements. *)

val drop : int -> 'element t -> ('element t, string) result
(** The elements after the first n. *)

val reverse : 'element t -> 'element t
(** The collection in the opposite order. *)

val sort : 'element Common.Comparator.t -> 'element t -> 'element t
(** The elements in ascending order. *)

val union : 'element t -> 'element list -> 'element t
(** The elements of both collections. Subtrees the operands already share are adopted whole rather
    than re-entered. *)

val intersect : 'element t -> 'element list -> 'element t
(** The elements present in both collections. *)

val difference : 'element t -> 'element list -> 'element t
(** This collection's elements that are absent from the other. *)

val symmetric_difference : 'element t -> 'element list -> 'element t
(** The elements present in exactly one of the two collections. *)

val to_list : 'element t -> 'element list
(** The elements, in the collection's own order. *)
