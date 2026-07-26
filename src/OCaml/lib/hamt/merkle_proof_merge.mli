(** MSP2 proof envelopes and present-value-safe typed three-way merge. *)

type ('key, 'value) query =
  | Membership of 'key * 'value
  | Nonmembership of 'key
  | Range of 'key * 'key

type step = { proof_block : Merkle_search_tree.block; expanded_child_indexes : int list }
(** One block of a proof's chain, with the children the proof expands rather than takes on their
    digests. *)

type ('key, 'value) proof
(** A proof of a key's presence or absence against a root digest. *)

val create_proof : ('key, 'value) Merkle_search_tree.t -> 'key -> ('key, 'value) proof
(** A proof of the key's presence or absence against the tree's root digest. *)

val create_range_proof : ('key, 'value) Merkle_search_tree.t -> 'key -> 'key -> ('key, 'value) proof
(** A proof covering a key range against the tree's root digest. *)

val proof_query : ('key, 'value) proof -> ('key, 'value) query
(** The key or key range the proof answers. *)

val proof_query_bytes : ('key, 'value) proof -> bytes
(** The query's encoded bytes, charged against the verification budget. *)

val proof_steps : ('key, 'value) proof -> step list
(** The proof's chain of blocks. *)

type proof_failure =
  | Invalid_envelope
  | Budget_exceeded
  | Invalid_query
  | Invalid_block
  | Wrong_result
(** Why a proof failed to verify. *)

type ('key, 'value) verification = {
  valid : bool;
  failure : proof_failure option;
  entries : ('key, 'value) Merkle_search_tree.entry list;
  accounted_blocks : int;
  accounted_bytes : int64;
(** The outcome of checking a proof. *)

}

val verify :
  ?budget:Merkle_persistence.budget ->
  ('key, 'value) proof ->
  ('key, 'value) Merkle_encoding.policy ->
  ('key, 'value) verification
(** Checks a proof against a root digest and a budget, so a crafted proof cannot force unbounded
    work. *)

type 'value merge_state = Missing | Value of 'value
(** One side's state for a key during a merge. The explicit absent case keeps a stored [None]
    distinguishable from absence. *)

type 'value merge_resolution = Use_base | Use_left | Use_right | Delete | Use of 'value | Conflict
(** How a resolver settled one conflicting key. Declining fails the whole merge rather than
    producing a partly merged tree. *)

type ('key, 'value) merge_result =
  | Merged of ('key, 'value) Merkle_search_tree.t
  | Conflicted of 'key list
(** The outcome of a three-way merge: the merged tree, or the conflicts left unresolved. *)

val merge :
  base:('key, 'value) Merkle_search_tree.t ->
  left:('key, 'value) Merkle_search_tree.t ->
  right:('key, 'value) Merkle_search_tree.t ->
  resolve:
    ('key ->
    'value merge_state ->
    'value merge_state ->
    'value merge_state ->
    'value merge_resolution) ->
  ('key, 'value) merge_result
(** Three-way merges two trees against their common ancestor, asking the resolver to settle each
    conflicting key. *)
