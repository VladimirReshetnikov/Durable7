(** Persistent generic rope with indexed split/concat editing. *)

type 'element t

val empty : 'element t
(** The empty collection. *)

val singleton : 'element -> 'element t
(** A collection holding one element. *)

val of_list : 'element list -> 'element t
(** A collection holding a list's elements, built in bulk rather than by repeated insertion. *)

val length : 'element t -> int
(** Number of elements in the collection. *)

val is_empty : 'element t -> bool
(** Whether the collection holds no elements. *)

val nth : int -> 'element t -> 'element option
(** The element at the given rank. *)

val concat : 'element t -> 'element t -> 'element t
(** The concatenation of two collections, sharing both operands' unchanged structure. *)

val split_at : int -> 'element t -> 'element t * 'element t
(** Splits into the elements before the position and those from it onward. *)

val insert : int -> 'element list -> 'element t -> ('element t, string) result
(** A collection containing the given element. *)

val remove : start:int -> length:int -> 'element t -> ('element t, string) result
(** A collection without that element. *)

val slice : start:int -> length:int -> 'element t -> ('element t, string) result
(** The elements in the given range. *)

val to_list : 'element t -> 'element list
(** The elements, in the collection's own order. *)

(** An append-only accumulator for building a rope in bulk, cheaper than one retained version per
    element. *)
module Builder : sig
  type 'element rope = 'element t
  (** The rope type this builder produces. *)

  type 'element t
  (** A mutable accumulator for building a collection in bulk. Deliberately not a snapshot: it fills
      a buffer and produces a persistent value only on demand, which is cheaper than one version per
      element. *)

  val create : unit -> 'element t
  (** An empty builder. Append-only: there is no seed rope and no removal. *)

  val append : 'element -> 'element t -> unit
  (** Append one element to the buffer. O(1). *)

  val append_rope : 'element rope -> 'element t -> unit
  (** Append every element of an existing rope, in order. The source rope is unaffected. *)

  val freeze : 'element t -> 'element rope
  (** Build a rope from everything appended so far, in append order. The builder stays usable, and
      because ropes are immutable the returned value is unaffected by later appends. *)
end
