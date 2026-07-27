(** Verified persistence, packs, and synchronization for MST2 blocks. *)

(** Why a block graph was rejected. Verification distinguishes these rather than reporting one
    opaque failure, so a caller can tell a recoverable gap - [Missing_block], which synchronization
    can fill - from evidence that the supplied data is wrong or hostile. *)
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
(** Why a verification rejected its input. *)

}

type budget = {
  maximum_blocks : int;
  maximum_total_bytes : int64;
  maximum_block_bytes : int;
  maximum_depth : int;
  maximum_entries : int64;
  maximum_children_per_block : int;
  maximum_proof_query_bytes : int;
(** The limits a verification is charged against, so untrusted input cannot force unbounded work. *)

}

val default_budget : budget
(** Limits suited to ordinary use. *)

val validate_budget : budget -> (unit, string) result
(** Checks that a budget's limits are internally consistent. *)

type block_store
(** Somewhere blocks are kept, addressed by digest. *)

val empty_store : block_store
(** The empty block store. *)

val store_count : block_store -> int
(** Number of blocks in the store. *)

val store_contains : Merkle_encoding.digest -> block_store -> bool
(** Whether the block is present. *)

val store_find : Merkle_encoding.digest -> block_store -> Merkle_search_tree.block option
(** The block matching the probe. Raises when absent. *)

type pack
(** A self-contained set of blocks a receiver can reconstruct a tree from. *)

val pack_root_digest : pack -> Merkle_encoding.digest
(** The root digest the pack's blocks reconstruct. *)

val pack_blocks : pack -> Merkle_search_tree.block list
(** The blocks the pack carries. *)

val make_pack :
  domain:Merkle_encoding.digest ->
  root:Merkle_encoding.digest ->
  Merkle_search_tree.block list ->
  (pack, error) result
(** A pack from a root digest and a set of blocks. *)

val export_pack : ('key, 'value) Merkle_search_tree.t -> pack
(** A pack carrying the blocks a receiver needs to reconstruct the tree. *)

val save : ('key, 'value) Merkle_search_tree.t -> block_store -> (int * block_store, error) result
(** Writes every block the tree needs into the store. *)

val load :
  ?budget:budget ->
  Merkle_encoding.digest ->
  ('key, 'value) Merkle_encoding.policy ->
  block_store ->
  (('key, 'value) Merkle_search_tree.t, error) result
(** Reconstructs a tree from a root digest and a store, checking each block against the digest it
    was fetched by. *)

val import_pack :
  ?budget:budget ->
  pack ->
  ('key, 'value) Merkle_encoding.policy ->
  block_store ->
  (('key, 'value) Merkle_search_tree.t * block_store, error) result
(** Reconstructs a tree from a pack, verifying its envelope and every block inside it against the
    budget. *)

val missing_pack : ('key, 'value) Merkle_search_tree.t -> block_store -> pack
(** The digests a pack references but does not carry. A pack with any missing cannot stand on its
    own. *)
