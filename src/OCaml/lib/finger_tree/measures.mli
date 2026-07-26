(** Monoidal measurement policies used by persistent sequence structures. *)

type 'measure monoid
type ('element, 'measure) policy
(** A measure policy: the monoid a measured tree caches, together with how one element measures. *)

val create_monoid : empty:'measure -> append:('measure -> 'measure -> 'measure) -> 'measure monoid
(** A monoid built from the supplied identity and combine. *)

val create_policy :
  id:string ->
  monoid:'measure monoid ->
  measure:('element -> 'measure) ->
  unit ->
  ('element, 'measure) policy
(** A policy built from the supplied callbacks. *)

val policy_id : ('element, 'measure) policy -> string
(** The policy's identity, which decides whether two collections may be combined. *)

val empty : ('element, 'measure) policy -> 'measure
(** The empty measure. *)

val append : ('element, 'measure) policy -> 'measure -> 'measure -> 'measure
(** The concatenation of two measures, sharing both operands' unchanged structure. *)

val measure : ('element, 'measure) policy -> 'element -> 'measure
(** The combined measure of every element, read from the cached root measure. *)

val compatible : ('left, 'measure) policy -> ('right, 'measure) policy -> bool
(** Whether the two operands' policies allow them to be combined. *)

val size : ('element, int) policy
(** Counts elements, making a measured tree indexable by position. *)

val int_sum : (int, int) policy
(** Sums elements, turning a measured tree into a cumulative-weight structure. *)

val int64_sum : (int64, int64) policy
(** Sums 64-bit elements. *)

val pair :
  id:string ->
  ('element, 'left) policy ->
  ('element, 'right) policy ->
  ('element, 'left * 'right) policy
(** Two measures carried side by side. The product of two monoids is a monoid, so one tree can be
    searched by either component. *)

val optional_max : id:string -> compare:('value -> 'value -> int) -> ('value, 'value option) policy
(** Tracks the largest element, with [None] as the identity. *)

val optional_min : id:string -> compare:('value -> 'value -> int) -> ('value, 'value option) policy
(** Tracks the smallest element, with [None] as the identity. *)
