type ('key, 'value) entry = { key : 'key; value : 'value }
type block = { digest : Merkle_encoding.digest; content : bytes }

let make_block digest content = { digest; content }
let block_digest block = block.digest
let block_content block = block.content
let make_entry key value = { key; value }
let entry_key entry = entry.key
let entry_value entry = entry.value

type ('key, 'value) stored_entry = {
  stored_key : 'key;
  stored_value : 'value;
  key_bytes : bytes;
  value_bytes : bytes;
  level : int;
}

type ('key, 'value) node = {
  entries : ('key, 'value) stored_entry array;
  children : ('key, 'value) node option array;
  node_count : int;
  node_height : int;
  node_block_count : int;
  node_content : bytes;
  node_digest : Merkle_encoding.digest;
}

type ('key, 'value) t = {
  tree_policy : ('key, 'value) Merkle_encoding.policy;
  root : ('key, 'value) node option;
}

let empty tree_policy = { tree_policy; root = None }
let policy tree = tree.tree_policy
let count tree = match tree.root with None -> 0 | Some root -> root.node_count
let height tree = match tree.root with None -> 0 | Some root -> root.node_height
let block_count tree = match tree.root with None -> 0 | Some root -> root.node_block_count

let root_digest tree =
  match tree.root with
  | None -> Merkle_encoding.empty_digest tree.tree_policy
  | Some root -> root.node_digest

let create_entry tree_policy key value =
  match
    ( Merkle_encoding.encode (Merkle_encoding.key_codec tree_policy) key,
      Merkle_encoding.encode (Merkle_encoding.value_codec tree_policy) value )
  with
  | Error message, _ | _, Error message -> Error message
  | Ok key_bytes, Ok value_bytes ->
      Ok
        {
          stored_key = key;
          stored_value = value;
          key_bytes;
          value_bytes;
          level = Merkle_encoding.level (Merkle_encoding.hash_key_bytes tree_policy key_bytes);
        }

let encode_node tree_policy level count entries children =
  let parts =
    ref
      [
        Bytes.of_string "MST2";
        Bytes.make 1 '\001';
        Merkle_encoding.digest_bytes (Merkle_encoding.domain_digest tree_policy);
        Bytes.make 1 (Char.chr level);
        Merkle_encoding.int32_big_endian (Int32.of_int count);
        Merkle_encoding.int32_big_endian (Int32.of_int (Array.length entries));
      ]
  in
  Array.iter
    (fun entry ->
      parts :=
        !parts
        @ [
            Merkle_encoding.int32_big_endian (Int32.of_int (Bytes.length entry.key_bytes));
            entry.key_bytes;
            Merkle_encoding.int32_big_endian (Int32.of_int (Bytes.length entry.value_bytes));
            entry.value_bytes;
          ])
    entries;
  Array.iter
    (fun child ->
      let digest =
        match child with
        | None -> Merkle_encoding.empty_digest tree_policy
        | Some node -> node.node_digest
      in
      parts := !parts @ [ Merkle_encoding.digest_bytes digest ])
    children;
  Bytes.concat Bytes.empty !parts

let new_node tree_policy level entries children =
  let node_count = ref (Array.length entries) in
  let node_height = ref 1 in
  let node_block_count = ref 1 in
  Array.iter
    (function
      | None -> ()
      | Some child ->
          node_count := !node_count + child.node_count;
          node_height := max !node_height (child.node_height + 1);
          node_block_count := !node_block_count + child.node_block_count)
    children;
  let content = encode_node tree_policy level !node_count entries children in
  {
    entries;
    children;
    node_count = !node_count;
    node_height = !node_height;
    node_block_count = !node_block_count;
    node_content = content;
    node_digest = Merkle_encoding.hash content;
  }

let rec build tree_policy entries =
  match entries with
  | [] -> None
  | _ ->
      let maximum = List.fold_left (fun result entry -> max result entry.level) 0 entries in
      let rec split segment separators children = function
        | [] ->
            let children = List.rev (build tree_policy (List.rev segment) :: children) in
            Some
              (new_node tree_policy maximum
                 (Array.of_list (List.rev separators))
                 (Array.of_list children))
        | entry :: rest when entry.level = maximum ->
            split [] (entry :: separators) (build tree_policy (List.rev segment) :: children) rest
        | entry :: rest -> split (entry :: segment) separators children rest
      in
      split [] [] [] entries

let of_list tree_policy items =
  let indexed = List.mapi (fun sequence (key, value) -> (key, value, sequence)) items in
  let sorted =
    List.stable_sort
      (fun (left, _, _) (right, _, _) -> Merkle_encoding.compare tree_policy left right)
      indexed
  in
  let rec group result = function
    | [] -> List.rev result
    | (key, value, _) :: rest ->
        let rec consume last_value = function
          | (candidate, candidate_value, _) :: tail
            when Merkle_encoding.compare tree_policy key candidate = 0 ->
              consume candidate_value tail
          | remaining -> (last_value, remaining)
        in
        let last_value, remaining = consume value rest in
        group ((key, last_value) :: result) remaining
  in
  let rec encode result = function
    | [] -> Ok (List.rev result)
    | (key, value) :: rest -> (
        match create_entry tree_policy key value with
        | Error message -> Error message
        | Ok entry -> encode (entry :: result) rest)
  in
  match encode [] (group [] sorted) with
  | Error message -> Error message
  | Ok entries -> Ok { tree_policy; root = build tree_policy entries }

let rec find_node tree_policy key = function
  | None -> None
  | Some node ->
      let rec search low high =
        if low >= high then low
        else
          let middle = (low + high) / 2 in
          if Merkle_encoding.compare tree_policy node.entries.(middle).stored_key key < 0 then
            search (middle + 1) high
          else search low middle
      in
      let position = search 0 (Array.length node.entries) in
      if
        position < Array.length node.entries
        && Merkle_encoding.compare tree_policy node.entries.(position).stored_key key = 0
      then Some node.entries.(position)
      else find_node tree_policy key node.children.(position)

let find_entry_opt key tree = find_node tree.tree_policy key tree.root
let find_opt key tree = Option.map (fun entry -> entry.stored_value) (find_entry_opt key tree)
let mem key tree = Option.is_some (find_entry_opt key tree)

let rec collect result = function
  | None -> result
  | Some node ->
      let result = ref result in
      for index = Array.length node.entries - 1 downto 0 do
        result := collect !result node.children.(index + 1);
        result := node.entries.(index) :: !result
      done;
      collect !result node.children.(0)

let stored_entries tree = collect [] tree.root

let to_list tree =
  List.map
    (fun entry -> { key = entry.stored_key; value = entry.stored_value })
    (stored_entries tree)

let set key value tree =
  match create_entry tree.tree_policy key value with
  | Error message -> Error message
  | Ok replacement -> (
      let existing = find_entry_opt key tree in
      match existing with
      | Some entry when Bytes.equal entry.value_bytes replacement.value_bytes -> Ok tree
      | _ ->
          let rec replace result = function
            | [] -> List.rev (replacement :: result)
            | entry :: rest ->
                let comparison = Merkle_encoding.compare tree.tree_policy entry.stored_key key in
                if comparison = 0 then
                  List.rev_append result
                    ({
                       replacement with
                       stored_key = entry.stored_key;
                       key_bytes = entry.key_bytes;
                       level = entry.level;
                     }
                    :: rest)
                else if comparison > 0 then List.rev_append result (replacement :: entry :: rest)
                else replace (entry :: result) rest
          in
          let entries = replace [] (stored_entries tree) in
          Ok { tree with root = build tree.tree_policy entries })

let add key value tree =
  if mem key tree then Error "an equivalent Merkle key is already present" else set key value tree

let remove key tree =
  if not (mem key tree) then Ok tree
  else
    let entries =
      List.filter
        (fun entry -> Merkle_encoding.compare tree.tree_policy entry.stored_key key <> 0)
        (stored_entries tree)
    in
    Ok { tree with root = build tree.tree_policy entries }

let range ~minimum ~maximum tree =
  if Merkle_encoding.compare tree.tree_policy minimum maximum > 0 then
    invalid_arg "minimum exceeds maximum";
  List.filter
    (fun entry ->
      Merkle_encoding.compare tree.tree_policy entry.key minimum >= 0
      && Merkle_encoding.compare tree.tree_policy entry.key maximum <= 0)
    (to_list tree)

let blocks_preorder tree =
  let rec visit result = function
    | None -> result
    | Some node ->
        let result = { digest = node.node_digest; content = node.node_content } :: result in
        Array.fold_left visit result node.children
  in
  List.rev (visit [] tree.root)

let validate tree =
  match
    of_list tree.tree_policy (List.map (fun entry -> (entry.key, entry.value)) (to_list tree))
  with
  | Error message -> Error message
  | Ok rebuilt ->
      if Merkle_encoding.digest_equal (root_digest tree) (root_digest rebuilt) then Ok ()
      else Error "Merkle root does not match its canonical contents"
