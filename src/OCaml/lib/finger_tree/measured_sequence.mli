(** Public facade for general monoid-measured persistent sequences. *)

type ('element, 'measure) t

val empty : ('element, 'measure) Measures.policy -> ('element, 'measure) t
val of_list : ('element, 'measure) Measures.policy -> 'element list -> ('element, 'measure) t
val length : ('element, 'measure) t -> int
val measure : ('element, 'measure) t -> 'measure
val to_list : ('element, 'measure) t -> 'element list
val cons : 'element -> ('element, 'measure) t -> ('element, 'measure) t
val snoc : ('element, 'measure) t -> 'element -> ('element, 'measure) t

val concat :
  ('element, 'measure) t -> ('element, 'measure) t -> (('element, 'measure) t, string) result

val nth : int -> ('element, 'measure) t -> 'element option
val split_at : int -> ('element, 'measure) t -> ('element, 'measure) t * ('element, 'measure) t
val measure_range : int -> int -> ('element, 'measure) t -> ('measure, string) result
val locate : ('measure -> bool) -> ('element, 'measure) t -> (int * 'measure * 'element) option
val insert_at : int -> 'element -> ('element, 'measure) t -> (('element, 'measure) t, string) result
val remove_at : int -> ('element, 'measure) t -> ('element * ('element, 'measure) t, string) result
val validate : ('element, 'measure) t -> (unit, string) result
