(** Structurally shared persistent sequence with cached monoidal measurements. *)

type ('element, 'measure) t

val empty : ('element, 'measure) Measures.policy -> ('element, 'measure) t
val singleton : ('element, 'measure) Measures.policy -> 'element -> ('element, 'measure) t
val of_list : ('element, 'measure) Measures.policy -> 'element list -> ('element, 'measure) t
val policy : ('element, 'measure) t -> ('element, 'measure) Measures.policy
val is_empty : ('element, 'measure) t -> bool
val length : ('element, 'measure) t -> int
val measure : ('element, 'measure) t -> 'measure
val to_list : ('element, 'measure) t -> 'element list
val iter : ('element -> unit) -> ('element, 'measure) t -> unit
val fold_left : ('state -> 'element -> 'state) -> 'state -> ('element, 'measure) t -> 'state
val fold_right : ('element -> 'state -> 'state) -> ('element, 'measure) t -> 'state -> 'state
val cons : 'element -> ('element, 'measure) t -> ('element, 'measure) t
val snoc : ('element, 'measure) t -> 'element -> ('element, 'measure) t

val concat :
  ('element, 'measure) t -> ('element, 'measure) t -> (('element, 'measure) t, string) result

val first : ('element, 'measure) t -> 'element option
val last : ('element, 'measure) t -> 'element option
val nth : int -> ('element, 'measure) t -> 'element option
val split_at : int -> ('element, 'measure) t -> ('element, 'measure) t * ('element, 'measure) t
val take : int -> ('element, 'measure) t -> ('element, 'measure) t
val drop : int -> ('element, 'measure) t -> ('element, 'measure) t
val insert_at : int -> 'element -> ('element, 'measure) t -> (('element, 'measure) t, string) result

val update_at :
  int -> ('element -> 'element) -> ('element, 'measure) t -> (('element, 'measure) t, string) result

val remove_at : int -> ('element, 'measure) t -> ('element * ('element, 'measure) t, string) result
val measure_range : int -> int -> ('element, 'measure) t -> ('measure, string) result

val locate : ('measure -> bool) -> ('element, 'measure) t -> (int * 'measure * 'element) option
(** [locate predicate tree] returns the first element whose inclusive prefix measure satisfies
    [predicate], together with its index and the exclusive-prefix measure. *)

val validate : ('element, 'measure) t -> (unit, string) result
