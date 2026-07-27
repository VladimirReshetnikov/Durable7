(** Persistent set over the shared HAMT map core. *)

type 'element t

val empty : 'element Common.Hash_policy.t -> 'element t
(** The empty collection. *)

val singleton : 'element Common.Hash_policy.t -> 'element -> 'element t
(** A collection holding one element. *)

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

val find_opt : 'element -> 'element t -> 'element option
(** The value stored for the key, or [None] when absent. *)

val add : 'element -> 'element t -> 'element t
(** A collection containing the given element. *)

val remove : 'element -> 'element t -> 'element t
(** A collection without that element. *)

val to_list : 'element t -> 'element list
(** The elements, in the collection's own order. *)

val union : 'element t -> 'element t -> 'element t
(** The elements of both collections. Subtrees the operands already share are adopted whole rather
    than re-entered. *)

val inter : 'element t -> 'element t -> 'element t
(** The elements present in both collections. *)

val diff : 'element t -> 'element t -> 'element t
(** The entry-level differences between two collections. Subtrees the two already share are skipped
    whole. *)

val symmetric_diff : 'element t -> 'element t -> 'element t
(** The elements present in exactly one of the two collections. *)

val subset : 'element t -> 'element t -> bool
(** Whether every element of this collection also occurs in the other. *)

val proper_subset : 'element t -> 'element t -> bool
(** Whether this collection is a subset of the other and the other holds an element it lacks. *)

val disjoint : 'element t -> 'element t -> bool
(** Whether the two collections share no element. *)

val equal : 'element t -> 'element t -> bool
(** Whether both collections hold the same elements. *)

(** A one-way mutable editing session.

    Edits are visible only through the session until [persistent] publishes them; the set the
    session was created from is never modified. The session is {e single-use}: [persistent] spends
    it, and every later operation on that handle - including a read - raises
    [Persistent_hamt.Transient_consumed].

    All six set relations are available during the session, so a bulk edit can be checked against a
    published set without first publishing the work in progress. Each takes the session on the left
    and a published set on the right. *)
module Transient : sig
  type 'element session
  (** A mutable session seeded from a set, for building a version in bulk. *)

  val create : 'element t -> 'element session
  (** A session seeded with the given set, which is itself left untouched. *)

  val count : 'element session -> int
  (** Number of elements currently in the session. *)

  val mem : 'element -> 'element session -> bool
  (** Whether the element is present in the session. *)

  val subset : 'element session -> 'element t -> bool
  (** Whether every element of the session is in the given set. *)

  val proper_subset : 'element session -> 'element t -> bool
  (** [subset] and the two are not equal. *)

  val superset : 'element session -> 'element t -> bool
  (** Whether the session contains every element of the given set. *)

  val proper_superset : 'element session -> 'element t -> bool
  (** [superset] and the two are not equal. *)

  val overlaps : 'element session -> 'element t -> bool
  (** Whether the session and the given set share at least one element. *)

  val equal : 'element session -> 'element t -> bool
  (** Whether the session holds exactly the elements of the given set. *)

  val add : 'element -> 'element session -> unit
  (** Add the element to the session; adding a present element is not an error. *)

  val remove : 'element -> 'element session -> unit
  (** Remove the element from the session; removing an absent element is not an error. *)

  val persistent : 'element session -> 'element t
  (** Publish the accumulated version and spend the session. Every later use of this handle raises
      [Persistent_hamt.Transient_consumed]. *)
end
