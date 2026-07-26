(** Implementation of the verified persistence, packs, and synchronization for MST2 blocks.

    Every block is checked against the digest it was fetched by, and every verification is charged
    against a caller-supplied budget so crafted input cannot force unbounded work. *)

module Digest_order = struct
  type t = Merkle_encoding.digest

  let compare = Merkle_encoding.digest_compare
end

module Digest_map = Map.Make (Digest_order)
module Digest_set = Set.Make (Digest_order)

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

let default_budget =
  {
    maximum_blocks = 1_000_000;
    maximum_total_bytes = Int64.shift_left 1L 30;
    maximum_block_bytes = 16 * 1024 * 1024;
    maximum_depth = 256;
    maximum_entries = 100_000_000L;
    maximum_children_per_block = 65_536;
    maximum_proof_query_bytes = 16 * 1024 * 1024;
  }

let validate_budget budget =
  if
    budget.maximum_blocks <= 0 || budget.maximum_total_bytes <= 0L
    || budget.maximum_block_bytes <= 0 || budget.maximum_depth <= 0 || budget.maximum_entries <= 0L
    || budget.maximum_children_per_block <= 0
    || budget.maximum_proof_query_bytes <= 0
  then Error "all Merkle verification limits must be positive"
  else if Int64.of_int budget.maximum_block_bytes > budget.maximum_total_bytes then
    Error "maximum block bytes exceeds maximum total bytes"
  else if Int64.of_int budget.maximum_proof_query_bytes > budget.maximum_total_bytes then
    Error "maximum proof-query bytes exceeds maximum total bytes"
  else Ok ()

type block_store = bytes Digest_map.t

let empty_store = Digest_map.empty
let store_count = Digest_map.cardinal
let store_contains digest store = Digest_map.mem digest store

let store_find digest store =
  Option.map
    (fun content -> Merkle_search_tree.make_block digest content)
    (Digest_map.find_opt digest store)

type pack = {
  pack_algorithm : string;
  pack_domain : Merkle_encoding.digest;
  pack_root : Merkle_encoding.digest;
  pack_block_values : Merkle_search_tree.block list;
}

let pack_root_digest pack = pack.pack_root
let pack_blocks pack = pack.pack_block_values

let failure error_kind error_message error_digest =
  Error { error_kind; error_message; error_digest }

let make_pack ~domain ~root blocks =
  let rec validate seen = function
    | [] ->
        Ok
          {
            pack_algorithm = Merkle_encoding.algorithm_id;
            pack_domain = domain;
            pack_root = root;
            pack_block_values = blocks;
          }
    | block :: rest ->
        let digest = Merkle_search_tree.block_digest block in
        if Digest_set.mem digest seen then
          failure Duplicate_block "Merkle pack repeats a block digest" (Some digest)
        else validate (Digest_set.add digest seen) rest
  in
  validate Digest_set.empty blocks

let export_pack tree =
  {
    pack_algorithm = Merkle_encoding.algorithm_id;
    pack_domain = Merkle_encoding.domain_digest (Merkle_search_tree.policy tree);
    pack_root = Merkle_search_tree.root_digest tree;
    pack_block_values = Merkle_search_tree.blocks_preorder tree;
  }

let add_block block store =
  let digest = Merkle_search_tree.block_digest block in
  let content = Merkle_search_tree.block_content block in
  match Digest_map.find_opt digest store with
  | None -> Ok (true, Digest_map.add digest content store)
  | Some existing when Bytes.equal existing content -> Ok (false, store)
  | Some _ ->
      failure Conflicting_block "a digest is already associated with different bytes" (Some digest)

let save tree store =
  let rec add count current = function
    | [] -> Ok (count, current)
    | block :: rest -> (
        match add_block block current with
        | Error problem -> Error problem
        | Ok (added, successor) -> add (if added then count + 1 else count) successor rest)
  in
  add 0 store (Merkle_search_tree.blocks_preorder tree)

type counters = {
  blocks : int;
  bytes : int64;
  entries : int64;
  seen : Digest_set.t;
  visiting : Digest_set.t;
}

type ('key, 'value) decoded = {
  decoded_level : int;
  decoded_subtree_count : int;
  decoded_entries : ('key * 'value) list;
  decoded_children : Merkle_encoding.digest list;
}

let starts_with bytes prefix =
  Bytes.length bytes >= String.length prefix
  &&
  let rec loop index =
    index = String.length prefix || (Bytes.get bytes index = prefix.[index] && loop (index + 1))
  in
  loop 0

let decode_block policy digest content budget =
  let length = Bytes.length content in
  if length < 78 then failure Malformed_block "an MST2 block is too short" (Some digest)
  else if not (starts_with content "MST2\001") then
    failure Malformed_block "an MST2 block has an invalid magic/version" (Some digest)
  else
    let domain = Bytes.sub content 5 32 in
    if
      not (Bytes.equal domain (Merkle_encoding.digest_bytes (Merkle_encoding.domain_digest policy)))
    then failure Domain_mismatch "an MST2 block belongs to another policy domain" (Some digest)
    else
      let decoded_level = Char.code (Bytes.get content 37) in
      match
        ( Merkle_encoding.read_int32_big_endian content 38,
          Merkle_encoding.read_int32_big_endian content 42 )
      with
      | Error message, _ | _, Error message -> failure Malformed_block message (Some digest)
      | Ok subtree_count, Ok entry_count -> (
          let decoded_subtree_count = Int32.to_int subtree_count in
          let entry_count = Int32.to_int entry_count in
          if decoded_subtree_count <= 0 || entry_count <= 0 then
            failure Malformed_block "MST2 subtree and entry counts must be positive" (Some digest)
          else if entry_count + 1 > budget.maximum_children_per_block then
            failure Resource_limit_exceeded "MST2 child-reference budget exceeded" (Some digest)
          else
            let rec entries offset previous result remaining =
              if remaining = 0 then Ok (offset, List.rev result)
              else
                match Merkle_encoding.read_int32_big_endian content offset with
                | Error message -> failure Malformed_block message (Some digest)
                | Ok key_length -> (
                    let key_length = Int32.to_int key_length in
                    if key_length < 0 || offset + 4 + key_length + 4 > length then
                      failure Malformed_block "invalid MST2 key length" (Some digest)
                    else
                      let key_bytes = Bytes.sub content (offset + 4) key_length in
                      let value_length_offset = offset + 4 + key_length in
                      match Merkle_encoding.read_int32_big_endian content value_length_offset with
                      | Error message -> failure Malformed_block message (Some digest)
                      | Ok value_length -> (
                          let value_length = Int32.to_int value_length in
                          let next = value_length_offset + 4 + value_length in
                          if value_length < 0 || next > length then
                            failure Malformed_block "invalid MST2 value length" (Some digest)
                          else
                            let value_bytes =
                              Bytes.sub content (value_length_offset + 4) value_length
                            in
                            match
                              ( Merkle_encoding.decode (Merkle_encoding.key_codec policy) key_bytes,
                                Merkle_encoding.decode
                                  (Merkle_encoding.value_codec policy)
                                  value_bytes )
                            with
                            | Error message, _ | _, Error message ->
                                failure Malformed_block message (Some digest)
                            | Ok key, Ok value -> (
                                match
                                  ( Merkle_encoding.encode (Merkle_encoding.key_codec policy) key,
                                    Merkle_encoding.encode
                                      (Merkle_encoding.value_codec policy)
                                      value )
                                with
                                | Ok canonical_key, Ok canonical_value
                                  when Bytes.equal canonical_key key_bytes
                                       && Bytes.equal canonical_value value_bytes ->
                                    if
                                      Merkle_encoding.level
                                        (Merkle_encoding.hash_key_bytes policy key_bytes)
                                      <> decoded_level
                                    then
                                      failure Noncanonical_block
                                        "separator level differs from block level" (Some digest)
                                    else if
                                      match previous with
                                      | Some previous_key ->
                                          Merkle_encoding.compare policy previous_key key >= 0
                                      | None -> false
                                    then
                                      failure Noncanonical_block
                                        "MST2 separators are not strictly ordered" (Some digest)
                                    else
                                      entries next (Some key) ((key, value) :: result)
                                        (remaining - 1)
                                | Ok _, Ok _ ->
                                    failure Noncanonical_block "codec bytes are not canonical"
                                      (Some digest)
                                | Error message, _ | _, Error message ->
                                    failure Malformed_block message (Some digest))))
            in
            match entries 46 None [] entry_count with
            | Error problem -> Error problem
            | Ok (offset, decoded_entries) -> (
                let child_bytes = (entry_count + 1) * 32 in
                if offset + child_bytes <> length then
                  failure Malformed_block "MST2 child digest section has the wrong length"
                    (Some digest)
                else
                  let rec children index result =
                    if index = entry_count + 1 then Ok (List.rev result)
                    else
                      match
                        Merkle_encoding.digest_of_bytes
                          (Bytes.sub content (offset + (index * 32)) 32)
                      with
                      | Error message -> failure Malformed_block message (Some digest)
                      | Ok child -> children (index + 1) (child :: result)
                  in
                  match children 0 [] with
                  | Error problem -> Error problem
                  | Ok decoded_children ->
                      Ok { decoded_level; decoded_subtree_count; decoded_entries; decoded_children }
                ))

let load ?(budget = default_budget) root policy store =
  match validate_budget budget with
  | Error message -> invalid_arg message
  | Ok () -> (
      if Merkle_encoding.digest_equal root (Merkle_encoding.empty_digest policy) then
        Ok (Merkle_search_tree.empty policy)
      else
        let initial =
          {
            blocks = 0;
            bytes = 0L;
            entries = 0L;
            seen = Digest_set.empty;
            visiting = Digest_set.empty;
          }
        in
        let rec walk depth parent_level counters digest =
          if depth > budget.maximum_depth then
            failure Resource_limit_exceeded "Merkle reference depth budget exceeded" (Some digest)
          else if Digest_set.mem digest counters.visiting then
            failure Cycle_detected "Merkle block graph contains a cycle" (Some digest)
          else if Digest_set.mem digest counters.seen then
            failure Invalid_reference "Merkle block is referenced more than once" (Some digest)
          else
            match Digest_map.find_opt digest store with
            | None -> failure Missing_block "a referenced Merkle block is missing" (Some digest)
            | Some content -> (
                let byte_count = Bytes.length content in
                let total = Int64.add counters.bytes (Int64.of_int byte_count) in
                if
                  byte_count > budget.maximum_block_bytes
                  || counters.blocks >= budget.maximum_blocks
                  || total > budget.maximum_total_bytes
                then
                  failure Resource_limit_exceeded "Merkle block/byte budget exceeded" (Some digest)
                else if not (Merkle_encoding.digest_equal digest (Merkle_encoding.hash content))
                then
                  failure Digest_mismatch "Merkle block bytes do not match their address"
                    (Some digest)
                else
                  let counters =
                    {
                      counters with
                      blocks = counters.blocks + 1;
                      bytes = total;
                      visiting = Digest_set.add digest counters.visiting;
                    }
                  in
                  match decode_block policy digest content budget with
                  | Error problem -> Error problem
                  | Ok decoded -> (
                      if
                        match parent_level with
                        | Some parent -> decoded.decoded_level >= parent
                        | None -> false
                      then
                        failure Invalid_reference "Merkle child levels must strictly decrease"
                          (Some digest)
                      else
                        let entries_total =
                          Int64.add counters.entries
                            (Int64.of_int (List.length decoded.decoded_entries))
                        in
                        if entries_total > budget.maximum_entries then
                          failure Resource_limit_exceeded "Merkle decoded-entry budget exceeded"
                            (Some digest)
                        else
                          let counters = { counters with entries = entries_total } in
                          let rec interleave accumulated counters entries children =
                            match (entries, children) with
                            | [], [ child ] -> (
                                if
                                  Merkle_encoding.digest_equal child
                                    (Merkle_encoding.empty_digest policy)
                                then Ok (accumulated, counters)
                                else
                                  match
                                    walk (depth + 1) (Some decoded.decoded_level) counters child
                                  with
                                  | Error problem -> Error problem
                                  | Ok (child_entries, successor) ->
                                      Ok
                                        ( List.rev_append (List.rev child_entries) accumulated,
                                          successor ))
                            | entry :: rest_entries, child :: rest_children -> (
                                if
                                  Merkle_encoding.digest_equal child
                                    (Merkle_encoding.empty_digest policy)
                                then
                                  interleave (entry :: accumulated) counters rest_entries
                                    rest_children
                                else
                                  match
                                    walk (depth + 1) (Some decoded.decoded_level) counters child
                                  with
                                  | Error problem -> Error problem
                                  | Ok (child_entries, successor) ->
                                      interleave
                                        (entry
                                        :: List.rev_append (List.rev child_entries) accumulated)
                                        successor rest_entries rest_children)
                            | _ ->
                                failure Malformed_block "MST2 entry/child arity mismatch"
                                  (Some digest)
                          in
                          match
                            interleave [] counters decoded.decoded_entries decoded.decoded_children
                          with
                          | Error problem -> Error problem
                          | Ok (reversed_entries, successor) ->
                              let successor =
                                {
                                  successor with
                                  visiting = Digest_set.remove digest successor.visiting;
                                  seen = Digest_set.add digest successor.seen;
                                }
                              in
                              let ordered = List.rev reversed_entries in
                              if List.length ordered <> decoded.decoded_subtree_count then
                                failure Noncanonical_block "MST2 subtree count is inconsistent"
                                  (Some digest)
                              else Ok (ordered, successor)))
        in
        match walk 1 None initial root with
        | Error problem -> Error problem
        | Ok (entries, _) -> (
            match Merkle_search_tree.of_list policy entries with
            | Error message -> failure Noncanonical_block message (Some root)
            | Ok tree ->
                if Merkle_encoding.digest_equal root (Merkle_search_tree.root_digest tree) then
                  Ok tree
                else
                  failure Root_mismatch "loaded entries do not rebuild the requested root"
                    (Some root)))

let import_pack ?(budget = default_budget) pack policy store =
  if not (String.equal pack.pack_algorithm Merkle_encoding.algorithm_id) then
    failure Unsupported_algorithm "Merkle pack algorithm is unsupported" None
  else if not (Merkle_encoding.digest_equal pack.pack_domain (Merkle_encoding.domain_digest policy))
  then failure Domain_mismatch "Merkle pack belongs to another policy domain" None
  else
    let rec stage seen candidate = function
      | [] -> Ok candidate
      | block :: rest -> (
          let digest = Merkle_search_tree.block_digest block in
          if Digest_set.mem digest seen then
            failure Duplicate_block "Merkle pack repeats a block digest" (Some digest)
          else
            match add_block block candidate with
            | Error problem -> Error problem
            | Ok (_, successor) -> stage (Digest_set.add digest seen) successor rest)
    in
    match stage Digest_set.empty store pack.pack_block_values with
    | Error problem -> Error problem
    | Ok candidate -> (
        match load ~budget pack.pack_root policy candidate with
        | Error problem -> Error problem
        | Ok tree -> Ok (tree, candidate))

let missing_pack tree store =
  let pack = export_pack tree in
  {
    pack with
    pack_block_values =
      List.filter
        (fun block -> not (store_contains (Merkle_search_tree.block_digest block) store))
        pack.pack_block_values;
  }
