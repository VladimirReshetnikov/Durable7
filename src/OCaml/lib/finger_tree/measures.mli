(** Monoidal measurement policies used by persistent sequence structures. *)

type 'measure monoid
type ('element, 'measure) policy

val create_monoid : empty:'measure -> append:('measure -> 'measure -> 'measure) -> 'measure monoid

val create_policy :
  id:string ->
  monoid:'measure monoid ->
  measure:('element -> 'measure) ->
  unit ->
  ('element, 'measure) policy

val policy_id : ('element, 'measure) policy -> string
val empty : ('element, 'measure) policy -> 'measure
val append : ('element, 'measure) policy -> 'measure -> 'measure -> 'measure
val measure : ('element, 'measure) policy -> 'element -> 'measure
val compatible : ('left, 'measure) policy -> ('right, 'measure) policy -> bool
val size : ('element, int) policy
val int_sum : (int, int) policy
val int64_sum : (int64, int64) policy

val pair :
  id:string ->
  ('element, 'left) policy ->
  ('element, 'right) policy ->
  ('element, 'left * 'right) policy

val optional_max : id:string -> compare:('value -> 'value -> int) -> ('value, 'value option) policy
val optional_min : id:string -> compare:('value -> 'value -> int) -> ('value, 'value option) policy
