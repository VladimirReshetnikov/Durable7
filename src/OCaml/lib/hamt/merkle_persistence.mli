(** Verified persistence, packs, and synchronization for MST2 blocks. *)

type failure_kind =
  | Unsupported_algorithm
  | Domain_mismatch
  | Missing_block
  | Digest_mismatch
  | Malformed_block
  | Noncanonical_block
  | Duplicate_block
  | Conflicting_block
  | Invalid_reference
  | Cycle_detected
  | Root_mismatch
  | Resource_limit_exceeded

type error = {
  error_kind : failure_kind;
  error_message : string;
  error_digest : Merkle_encoding.digest option;
}

type budget = {
  maximum_blocks : int;
  maximum_total_bytes : int64;
  maximum_block_bytes : int;
  maximum_depth : int;
  maximum_entries : int64;
  maximum_children_per_block : int;
  maximum_proof_query_bytes : int;
}

val default_budget : budget
val validate_budget : budget -> (unit, string) result

type block_store

val empty_store : block_store
val store_count : block_store -> int
val store_contains : Merkle_encoding.digest -> block_store -> bool
val store_find : Merkle_encoding.digest -> block_store -> Merkle_search_tree.block option

type pack

val pack_root_digest : pack -> Merkle_encoding.digest
val pack_blocks : pack -> Merkle_search_tree.block list

val make_pack :
  domain:Merkle_encoding.digest ->
  root:Merkle_encoding.digest ->
  Merkle_search_tree.block list ->
  (pack, error) result

val export_pack : ('key, 'value) Merkle_search_tree.t -> pack
val save : ('key, 'value) Merkle_search_tree.t -> block_store -> (int * block_store, error) result

val load :
  ?budget:budget ->
  Merkle_encoding.digest ->
  ('key, 'value) Merkle_encoding.policy ->
  block_store ->
  (('key, 'value) Merkle_search_tree.t, error) result

val import_pack :
  ?budget:budget ->
  pack ->
  ('key, 'value) Merkle_encoding.policy ->
  block_store ->
  (('key, 'value) Merkle_search_tree.t * block_store, error) result

val missing_pack : ('key, 'value) Merkle_search_tree.t -> block_store -> pack
