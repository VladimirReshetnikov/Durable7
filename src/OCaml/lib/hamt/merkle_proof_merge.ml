type ('key, 'value) query =
  | Membership of 'key * 'value
  | Nonmembership of 'key
  | Range of 'key * 'key

type step = { proof_block : Merkle_search_tree.block; expanded_child_indexes : int list }

type ('key, 'value) proof = {
  proof_algorithm : string;
  proof_domain : Merkle_encoding.digest;
  proof_root : Merkle_encoding.digest;
  proof_query_value : ('key, 'value) query;
  proof_query_encoding : bytes;
  proof_step_values : step list;
}

let encode_query policy = function
  | Membership (key, value) -> (
      match
        ( Merkle_encoding.encode (Merkle_encoding.key_codec policy) key,
          Merkle_encoding.encode (Merkle_encoding.value_codec policy) value )
      with
      | Ok key_bytes, Ok value_bytes ->
          Bytes.concat Bytes.empty
            [
              Bytes.of_string "MSP2";
              Bytes.make 1 '\000';
              Merkle_encoding.int32_big_endian (Int32.of_int (Bytes.length key_bytes));
              key_bytes;
              Merkle_encoding.int32_big_endian (Int32.of_int (Bytes.length value_bytes));
              value_bytes;
            ]
      | Error message, _ | _, Error message -> invalid_arg message)
  | Nonmembership key -> (
      match Merkle_encoding.encode (Merkle_encoding.key_codec policy) key with
      | Error message -> invalid_arg message
      | Ok key_bytes ->
          Bytes.concat Bytes.empty
            [
              Bytes.of_string "MSP2";
              Bytes.make 1 '\001';
              Merkle_encoding.int32_big_endian (Int32.of_int (Bytes.length key_bytes));
              key_bytes;
            ])
  | Range (minimum, maximum) -> (
      match
        ( Merkle_encoding.encode (Merkle_encoding.key_codec policy) minimum,
          Merkle_encoding.encode (Merkle_encoding.key_codec policy) maximum )
      with
      | Ok minimum_bytes, Ok maximum_bytes ->
          Bytes.concat Bytes.empty
            [
              Bytes.of_string "MSP2";
              Bytes.make 1 '\002';
              Merkle_encoding.int32_big_endian (Int32.of_int (Bytes.length minimum_bytes));
              minimum_bytes;
              Merkle_encoding.int32_big_endian (Int32.of_int (Bytes.length maximum_bytes));
              maximum_bytes;
            ]
      | Error message, _ | _, Error message -> invalid_arg message)

let make_proof tree query =
  let policy = Merkle_search_tree.policy tree in
  {
    proof_algorithm = Merkle_encoding.algorithm_id;
    proof_domain = Merkle_encoding.domain_digest policy;
    proof_root = Merkle_search_tree.root_digest tree;
    proof_query_value = query;
    proof_query_encoding = encode_query policy query;
    proof_step_values =
      List.map
        (fun proof_block -> { proof_block; expanded_child_indexes = [] })
        (Merkle_search_tree.blocks_preorder tree);
  }

let create_proof tree key =
  match Merkle_search_tree.find_opt key tree with
  | None -> make_proof tree (Nonmembership key)
  | Some value -> make_proof tree (Membership (key, value))

let create_range_proof tree minimum maximum =
  if Merkle_encoding.compare (Merkle_search_tree.policy tree) minimum maximum > 0 then
    invalid_arg "minimum exceeds maximum";
  make_proof tree (Range (minimum, maximum))

let proof_query proof = proof.proof_query_value
let proof_query_bytes proof = proof.proof_query_encoding
let proof_steps proof = proof.proof_step_values

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

let invalid failure accounted_blocks accounted_bytes =
  { valid = false; failure = Some failure; entries = []; accounted_blocks; accounted_bytes }

let verify ?(budget = Merkle_persistence.default_budget) proof policy =
  let accounted_blocks = List.length proof.proof_step_values in
  let accounted_bytes =
    List.fold_left
      (fun total step ->
        Int64.add total
          (Int64.of_int (Bytes.length (Merkle_search_tree.block_content step.proof_block))))
      0L proof.proof_step_values
  in
  if
    (not (String.equal proof.proof_algorithm Merkle_encoding.algorithm_id))
    || not (Merkle_encoding.digest_equal proof.proof_domain (Merkle_encoding.domain_digest policy))
  then invalid Invalid_envelope accounted_blocks accounted_bytes
  else if
    Bytes.length proof.proof_query_encoding > budget.Merkle_persistence.maximum_proof_query_bytes
  then invalid Budget_exceeded accounted_blocks accounted_bytes
  else if not (Bytes.equal proof.proof_query_encoding (encode_query policy proof.proof_query_value))
  then invalid Invalid_query accounted_blocks accounted_bytes
  else
    match
      Merkle_persistence.make_pack ~domain:proof.proof_domain ~root:proof.proof_root
        (List.map (fun step -> step.proof_block) proof.proof_step_values)
    with
    | Error _ -> invalid Invalid_block accounted_blocks accounted_bytes
    | Ok pack -> (
        match Merkle_persistence.import_pack ~budget pack policy Merkle_persistence.empty_store with
        | Error problem ->
            let failure =
              match problem.Merkle_persistence.error_kind with
              | Merkle_persistence.Resource_limit_exceeded -> Budget_exceeded
              | Merkle_persistence.Unsupported_algorithm | Merkle_persistence.Domain_mismatch
              | Merkle_persistence.Missing_block | Merkle_persistence.Digest_mismatch
              | Merkle_persistence.Malformed_block | Merkle_persistence.Noncanonical_block
              | Merkle_persistence.Duplicate_block | Merkle_persistence.Conflicting_block
              | Merkle_persistence.Invalid_reference | Merkle_persistence.Cycle_detected
              | Merkle_persistence.Root_mismatch ->
                  Invalid_block
            in
            invalid failure accounted_blocks accounted_bytes
        | Ok (tree, _) -> (
            match proof.proof_query_value with
            | Membership (key, expected) -> (
                match Merkle_search_tree.find_opt key tree with
                | None -> invalid Wrong_result accounted_blocks accounted_bytes
                | Some actual -> (
                    match
                      ( Merkle_encoding.encode (Merkle_encoding.value_codec policy) expected,
                        Merkle_encoding.encode (Merkle_encoding.value_codec policy) actual )
                    with
                    | Ok expected_bytes, Ok actual_bytes
                      when Bytes.equal expected_bytes actual_bytes ->
                        {
                          valid = true;
                          failure = None;
                          entries = [ Merkle_search_tree.make_entry key actual ];
                          accounted_blocks;
                          accounted_bytes;
                        }
                    | Ok _, Ok _ | Error _, _ | _, Error _ ->
                        invalid Wrong_result accounted_blocks accounted_bytes))
            | Nonmembership key ->
                if Merkle_search_tree.mem key tree then
                  invalid Wrong_result accounted_blocks accounted_bytes
                else
                  { valid = true; failure = None; entries = []; accounted_blocks; accounted_bytes }
            | Range (minimum, maximum) ->
                {
                  valid = true;
                  failure = None;
                  entries = Merkle_search_tree.range ~minimum ~maximum tree;
                  accounted_blocks;
                  accounted_bytes;
                }))

type 'value merge_state = Missing | Value of 'value
type 'value merge_resolution = Use_base | Use_left | Use_right | Delete | Use of 'value | Conflict

type ('key, 'value) merge_result =
  | Merged of ('key, 'value) Merkle_search_tree.t
  | Conflicted of 'key list

let merge ~base ~left ~right ~resolve =
  let policy = Merkle_search_tree.policy base in
  if
    not
      (Merkle_encoding.compatible policy (Merkle_search_tree.policy left)
      && Merkle_encoding.compatible policy (Merkle_search_tree.policy right))
  then invalid_arg "Merkle merge inputs must have compatible policies";
  let base_entries = Merkle_search_tree.to_list base in
  let left_entries = Merkle_search_tree.to_list left in
  let right_entries = Merkle_search_tree.to_list right in
  let all = base_entries @ left_entries @ right_entries in
  let sorted =
    List.sort
      (fun left_entry right_entry ->
        Merkle_encoding.compare policy
          (Merkle_search_tree.entry_key left_entry)
          (Merkle_search_tree.entry_key right_entry))
      all
  in
  let rec drop_equivalent key = function
    | candidate :: rest
      when Merkle_encoding.compare policy key (Merkle_search_tree.entry_key candidate) = 0 ->
        drop_equivalent key rest
    | remaining -> remaining
  in
  let rec unique result = function
    | [] -> List.rev result
    | entry :: rest ->
        let key = Merkle_search_tree.entry_key entry in
        unique (key :: result) (drop_equivalent key rest)
  in
  let keys = unique [] sorted in
  let find key entries =
    List.find_opt
      (fun entry -> Merkle_encoding.compare policy (Merkle_search_tree.entry_key entry) key = 0)
      entries
  in
  let state key entries =
    match find key entries with
    | None -> Missing
    | Some entry -> Value (Merkle_search_tree.entry_value entry)
  in
  let state_equal left_state right_state =
    match (left_state, right_state) with
    | Missing, Missing -> true
    | Value left_value, Value right_value -> (
        match
          ( Merkle_encoding.encode (Merkle_encoding.value_codec policy) left_value,
            Merkle_encoding.encode (Merkle_encoding.value_codec policy) right_value )
        with
        | Ok left_bytes, Ok right_bytes -> Bytes.equal left_bytes right_bytes
        | Error message, _ | _, Error message -> invalid_arg message)
    | Missing, Value _ | Value _, Missing -> false
  in
  let rec combine merged conflicts = function
    | [] ->
        if conflicts <> [] then Conflicted (List.rev conflicts)
        else Merged (Result.get_ok (Merkle_search_tree.of_list policy (List.rev merged)))
    | key :: rest -> (
        let base_state = state key base_entries in
        let left_state = state key left_entries in
        let right_state = state key right_entries in
        let resolution =
          if state_equal left_state right_state then
            match left_state with Missing -> Delete | Value value -> Use value
          else if state_equal left_state base_state then Use_right
          else if state_equal right_state base_state then Use_left
          else resolve key base_state left_state right_state
        in
        let selected =
          match resolution with
          | Use_base -> base_state
          | Use_left -> left_state
          | Use_right -> right_state
          | Delete -> Missing
          | Use value -> Value value
          | Conflict -> Missing
        in
        match resolution with
        | Conflict -> combine merged (key :: conflicts) rest
        | Use_base | Use_left | Use_right | Delete | Use _ ->
            let merged =
              match selected with Missing -> merged | Value value -> (key, value) :: merged
            in
            combine merged conflicts rest)
  in
  combine [] [] keys
