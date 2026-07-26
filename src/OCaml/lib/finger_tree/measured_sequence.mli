(** Public facade for general monoid-measured persistent sequences. *)

type ('element, 'measure) t

val empty : ('element, 'measure) Measures.policy -> ('element, 'measure) t
(** The empty sequence. *)

val of_list : ('element, 'measure) Measures.policy -> 'element list -> ('element, 'measure) t
(** A sequence holding a list's elements, built in bulk rather than by repeated insertion. *)

val length : ('element, 'measure) t -> int
(** Number of elements in the sequence. *)

val measure : ('element, 'measure) t -> 'measure
(** The combined measure of every element, read from the cached root measure. *)

val to_list : ('element, 'measure) t -> 'element list
(** The elements, in the sequence's own order. *)

val cons : 'element -> ('element, 'measure) t -> ('element, 'measure) t
(** A sequence with the element added at the front. *)

val snoc : ('element, 'measure) t -> 'element -> ('element, 'measure) t
(** A sequence with the element added at the back. *)

val concat :
  ('element, 'measure) t -> ('element, 'measure) t -> (('element, 'measure) t, string) result
(** The concatenation of two sequences, sharing both operands' unchanged structure. *)

val nth : int -> ('element, 'measure) t -> 'element option
(** The element at the given rank. *)

val split_at : int -> ('element, 'measure) t -> ('element, 'measure) t * ('element, 'measure) t
(** Splits into the elements before the position and those from it onward. *)

val measure_range : int -> int -> ('element, 'measure) t -> ('measure, string) result
(** The combined measure of the elements in the range. *)

val locate : ('measure -> bool) -> ('element, 'measure) t -> (int * 'measure * 'element) option
(** Where a measured search lands. *)

val insert_at : int -> 'element -> ('element, 'measure) t -> (('element, 'measure) t, string) result
(** A sequence with the element inserted at the position. *)

val remove_at : int -> ('element, 'measure) t -> ('element * ('element, 'measure) t, string) result
(** A sequence without the element at the position. *)

val validate : ('element, 'measure) t -> (unit, string) result
(** Checks the sequence's structural invariants. For tests and diagnostics. *)
