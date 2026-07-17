(** Law-gated persistent sequence with range updates and cached range measures. *)

type ('element, 'measure, 'tag) algebra
type ('element, 'measure, 'tag) t

val create_algebra :
  id:string ->
  identity:'measure ->
  combine:('measure -> 'measure -> 'measure) ->
  measure:('element -> 'measure) ->
  apply_element:('tag -> 'element -> 'element) ->
  apply_measure:('tag -> length:int -> 'measure -> 'measure) ->
  compose:('tag -> 'tag -> 'tag) ->
  laws_verified:bool ->
  unit ->
  (('element, 'measure, 'tag) algebra, string) result

val algebra_id : ('element, 'measure, 'tag) algebra -> string
val compose_tags : ('element, 'measure, 'tag) algebra -> 'tag -> 'tag -> 'tag
val empty : ('element, 'measure, 'tag) algebra -> ('element, 'measure, 'tag) t
val of_list : ('element, 'measure, 'tag) algebra -> 'element list -> ('element, 'measure, 'tag) t
val length : ('element, 'measure, 'tag) t -> int
val nth : int -> ('element, 'measure, 'tag) t -> 'element option
val to_list : ('element, 'measure, 'tag) t -> 'element list
val measure : ('element, 'measure, 'tag) t -> 'measure

val measure_range :
  start:int -> length:int -> ('element, 'measure, 'tag) t -> ('measure, string) result

val update_range :
  start:int ->
  length:int ->
  'tag ->
  ('element, 'measure, 'tag) t ->
  (('element, 'measure, 'tag) t, string) result

val insert :
  int -> 'element -> ('element, 'measure, 'tag) t -> (('element, 'measure, 'tag) t, string) result

val remove :
  int -> ('element, 'measure, 'tag) t -> ('element * ('element, 'measure, 'tag) t, string) result

val split_at :
  int -> ('element, 'measure, 'tag) t -> ('element, 'measure, 'tag) t * ('element, 'measure, 'tag) t
