(** Persistent meldable minimum heap surface.

    A bootstrapped skew-binomial minimum heap, the same structure every sibling workspace ships: a
    global rank-zero root holds the minimum outside the forest, so the minimum is a field read and
    an insert or meld attaches one tree with at most one skew link. [insert], [meld], and [minimum]
    are [O(1)] worst case; [delete_minimum] is [O(log n)] worst case; [count] is [O(1)] from the
    cached size. The kernel is the untagged specialization of
    {!Persistent_monotone_action_heap}'s, which carries the action-tagged generalization of the
    same functions. *)

type 'element t

type statistics = {
  count : int;  (** Walked node count; disagreement with {!val:count} exposes a size-cache drift. *)
  root_forest_length : int;  (** Trees on the global root's child spine. *)
  maximum_rank : int;  (** Largest skew-binomial rank in the forest. *)
  maximum_depth : int;  (** Longest root-to-leaf node chain, counting the global root. *)
}
(** Shape measurements walked from the skew-binomial forest by a structural audit. Every field is
    measured from the nodes themselves, never synthesized from the count. *)

val empty : 'element Common.Comparator.t -> 'element t
(** The empty heap. *)

val of_list : 'element Common.Comparator.t -> 'element list -> 'element t
(** A heap holding a list's elements: a fold of [O(1)] insertions, so [Theta (n)] in total. *)

val comparator : 'element t -> 'element Common.Comparator.t
(** The retained ordering policy. *)

val count : 'element t -> int
(** Number of elements in the heap. [O(1)], read from the cached size. *)

val is_empty : 'element t -> bool
(** Whether the heap holds no elements. [O(1)]. *)

val insert : 'element -> 'element t -> 'element t
(** A heap containing the given element. [O(1)] worst case; a tie with the current minimum favours
    the new element. *)

val meld : 'element t -> 'element t -> 'element t
(** The heap holding both operands' elements. [O(1)] worst case: the losing root is skew-inserted
    into the winning root's forest. Both operands must retain the same comparator object. *)

val minimum : 'element t -> 'element option
(** The smallest element, or the minimum-priority entry where the structure is a queue. [O(1)]. *)

val minimum_view : 'element t -> ('element * 'element t) option
(** The minimum-priority entry together with the heap remaining. [O(log n)] worst case. *)

val delete_minimum : 'element t -> 'element t option
(** A heap without its minimum-priority entry. [O(log n)] worst case: the minimum child tree is
    lifted out, decomposed, and its pieces melded back. *)

val to_sorted_list : 'element t -> 'element list
(** The elements in ascending order, by draining. [O(n log n)]. *)

val statistics : 'element t -> statistics
(** Shape measurements walked from the forest in [Theta (n)]. *)
