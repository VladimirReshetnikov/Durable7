(** MSP2 proof envelopes and present-value-safe typed three-way merge. *)

type ('key, 'value) query =
  | Membership of 'key * 'value
  | Nonmembership of 'key
  | Range of 'key * 'key

type step = { proof_block : Merkle_search_tree.block; expanded_child_indexes : int list }
type ('key, 'value) proof

val create_proof : ('key, 'value) Merkle_search_tree.t -> 'key -> ('key, 'value) proof
val create_range_proof : ('key, 'value) Merkle_search_tree.t -> 'key -> 'key -> ('key, 'value) proof
val proof_query : ('key, 'value) proof -> ('key, 'value) query
val proof_query_bytes : ('key, 'value) proof -> bytes
val proof_steps : ('key, 'value) proof -> step list

type proof_failure =
  | Invalid_envelope
  | Budget_exceeded
  | Invalid_query
  | Invalid_block
  | Wrong_result

type ('key, 'value) verification = {
  valid : bool;
  failure : proof_failure option;
  entries : ('key, 'value) Merkle_search_tree.entry list;
  accounted_blocks : int;
  accounted_bytes : int64;
}

val verify :
  ?budget:Merkle_persistence.budget ->
  ('key, 'value) proof ->
  ('key, 'value) Merkle_encoding.policy ->
  ('key, 'value) verification

type 'value merge_state = Missing | Value of 'value
type 'value merge_resolution = Use_base | Use_left | Use_right | Delete | Use of 'value | Conflict

type ('key, 'value) merge_result =
  | Merged of ('key, 'value) Merkle_search_tree.t
  | Conflicted of 'key list

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
